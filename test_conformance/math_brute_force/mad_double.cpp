//
// Copyright (c) 2017 The Khronos Group Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "function_list.h"
#include "test_functions.h"
#include "utility.h"

#include <cstring>

static int BuildKernel(const char *name, int vectorSize, cl_kernel *k,
                       cl_program *p, bool relaxedMode)
{
    const char *c[] = { "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n",
                        "__kernel void math_kernel",
                        sizeNames[vectorSize],
                        "( __global double",
                        sizeNames[vectorSize],
                        "* out, __global double",
                        sizeNames[vectorSize],
                        "* in1, __global double",
                        sizeNames[vectorSize],
                        "* in2,  __global double",
                        sizeNames[vectorSize],
                        "* in3 )\n"
                        "{\n"
                        "   size_t i = get_global_id(0);\n"
                        "   out[i] = ",
                        name,
                        "( in1[i], in2[i], in3[i] );\n"
                        "}\n" };

    const char *c3[] = {
        "#pragma OPENCL EXTENSION cl_khr_fp64 : enable\n",
        "__kernel void math_kernel",
        sizeNames[vectorSize],
        "( __global double* out, __global double* in, __global double* in2, "
        "__global double* in3)\n"
        "{\n"
        "   size_t i = get_global_id(0);\n"
        "   if( i + 1 < get_global_size(0) )\n"
        "   {\n"
        "       double3 d0 = vload3( 0, in + 3 * i );\n"
        "       double3 d1 = vload3( 0, in2 + 3 * i );\n"
        "       double3 d2 = vload3( 0, in3 + 3 * i );\n"
        "       d0 = ",
        name,
        "( d0, d1, d2 );\n"
        "       vstore3( d0, 0, out + 3*i );\n"
        "   }\n"
        "   else\n"
        "   {\n"
        "       size_t parity = i & 1;   // Figure out how many elements are "
        "left over after BUFFER_SIZE % (3*sizeof(float)). Assume power of two "
        "buffer size \n"
        "       double3 d0;\n"
        "       double3 d1;\n"
        "       double3 d2;\n"
        "       switch( parity )\n"
        "       {\n"
        "           case 1:\n"
        "               d0 = (double3)( in[3*i], NAN, NAN ); \n"
        "               d1 = (double3)( in2[3*i], NAN, NAN ); \n"
        "               d2 = (double3)( in3[3*i], NAN, NAN ); \n"
        "               break;\n"
        "           case 0:\n"
        "               d0 = (double3)( in[3*i], in[3*i+1], NAN ); \n"
        "               d1 = (double3)( in2[3*i], in2[3*i+1], NAN ); \n"
        "               d2 = (double3)( in3[3*i], in3[3*i+1], NAN ); \n"
        "               break;\n"
        "       }\n"
        "       d0 = ",
        name,
        "( d0, d1, d2 );\n"
        "       switch( parity )\n"
        "       {\n"
        "           case 0:\n"
        "               out[3*i+1] = d0.y; \n"
        "               // fall through\n"
        "           case 1:\n"
        "               out[3*i] = d0.x; \n"
        "               break;\n"
        "       }\n"
        "   }\n"
        "}\n"
    };

    const char **kern = c;
    size_t kernSize = sizeof(c) / sizeof(c[0]);

    if (sizeValues[vectorSize] == 3)
    {
        kern = c3;
        kernSize = sizeof(c3) / sizeof(c3[0]);
    }

    char testName[32];
    snprintf(testName, sizeof(testName) - 1, "math_kernel%s",
             sizeNames[vectorSize]);

    return MakeKernel(kern, (cl_uint)kernSize, testName, k, p, relaxedMode);
}

typedef struct BuildKernelInfo
{
    cl_uint offset; // the first vector size to build
    cl_kernel *kernels;
    cl_program *programs;
    const char *nameInCode;
    bool relaxedMode; // Whether to build with -cl-fast-relaxed-math.
} BuildKernelInfo;

static cl_int BuildKernelFn(cl_uint job_id, cl_uint thread_id UNUSED, void *p)
{
    BuildKernelInfo *info = (BuildKernelInfo *)p;
    cl_uint i = info->offset + job_id;
    return BuildKernel(info->nameInCode, i, info->kernels + i,
                       info->programs + i, info->relaxedMode);
}

int TestFunc_mad_Double(const Func *f, MTdata d, bool relaxedMode)
{
    uint64_t i;
    uint32_t j, k;
    int error;
    cl_program programs[VECTOR_SIZE_COUNT];
    cl_kernel kernels[VECTOR_SIZE_COUNT];
    float maxError = 0.0f;
    double maxErrorVal = 0.0f;
    double maxErrorVal2 = 0.0f;
    double maxErrorVal3 = 0.0f;
    uint64_t step = getTestStep(sizeof(double), BUFFER_SIZE);

    logFunctionInfo(f->name, sizeof(cl_double), relaxedMode);

    // Init the kernels
    {
        BuildKernelInfo build_info = { gMinVectorSizeIndex, kernels, programs,
                                       f->nameInCode, relaxedMode };
        if ((error = ThreadPool_Do(BuildKernelFn,
                                   gMaxVectorSizeIndex - gMinVectorSizeIndex,
                                   &build_info)))
            return error;
    }

    for (i = 0; i < (1ULL << 32); i += step)
    {
        // Init input array
        double *p = (double *)gIn;
        double *p2 = (double *)gIn2;
        double *p3 = (double *)gIn3;
        for (j = 0; j < BUFFER_SIZE / sizeof(double); j++)
        {
            p[j] = DoubleFromUInt32(genrand_int32(d));
            p2[j] = DoubleFromUInt32(genrand_int32(d));
            p3[j] = DoubleFromUInt32(genrand_int32(d));
        }

        if ((error = clEnqueueWriteBuffer(gQueue, gInBuffer, CL_FALSE, 0,
                                          BUFFER_SIZE, gIn, 0, NULL, NULL)))
        {
            vlog_error("\n*** Error %d in clEnqueueWriteBuffer ***\n", error);
            return error;
        }

        if ((error = clEnqueueWriteBuffer(gQueue, gInBuffer2, CL_FALSE, 0,
                                          BUFFER_SIZE, gIn2, 0, NULL, NULL)))
        {
            vlog_error("\n*** Error %d in clEnqueueWriteBuffer2 ***\n", error);
            return error;
        }

        if ((error = clEnqueueWriteBuffer(gQueue, gInBuffer3, CL_FALSE, 0,
                                          BUFFER_SIZE, gIn3, 0, NULL, NULL)))
        {
            vlog_error("\n*** Error %d in clEnqueueWriteBuffer3 ***\n", error);
            return error;
        }

        // write garbage into output arrays
        for (j = gMinVectorSizeIndex; j < gMaxVectorSizeIndex; j++)
        {
            uint32_t pattern = 0xffffdead;
            memset_pattern4(gOut[j], &pattern, BUFFER_SIZE);
            if ((error =
                     clEnqueueWriteBuffer(gQueue, gOutBuffer[j], CL_FALSE, 0,
                                          BUFFER_SIZE, gOut[j], 0, NULL, NULL)))
            {
                vlog_error("\n*** Error %d in clEnqueueWriteBuffer2(%d) ***\n",
                           error, j);
                goto exit;
            }
        }

        // Run the kernels
        for (j = gMinVectorSizeIndex; j < gMaxVectorSizeIndex; j++)
        {
            size_t vectorSize = sizeof(cl_double) * sizeValues[j];
            size_t localCount = (BUFFER_SIZE + vectorSize - 1)
                / vectorSize; // BUFFER_SIZE / vectorSize  rounded up
            if ((error = clSetKernelArg(kernels[j], 0, sizeof(gOutBuffer[j]),
                                        &gOutBuffer[j])))
            {
                LogBuildError(programs[j]);
                goto exit;
            }
            if ((error = clSetKernelArg(kernels[j], 1, sizeof(gInBuffer),
                                        &gInBuffer)))
            {
                LogBuildError(programs[j]);
                goto exit;
            }
            if ((error = clSetKernelArg(kernels[j], 2, sizeof(gInBuffer2),
                                        &gInBuffer2)))
            {
                LogBuildError(programs[j]);
                goto exit;
            }
            if ((error = clSetKernelArg(kernels[j], 3, sizeof(gInBuffer3),
                                        &gInBuffer3)))
            {
                LogBuildError(programs[j]);
                goto exit;
            }

            if ((error =
                     clEnqueueNDRangeKernel(gQueue, kernels[j], 1, NULL,
                                            &localCount, NULL, 0, NULL, NULL)))
            {
                vlog_error("FAILED -- could not execute kernel\n");
                goto exit;
            }
        }

        // Get that moving
        if ((error = clFlush(gQueue))) vlog("clFlush failed\n");

        // Calculate the correctly rounded reference result
        double *r = (double *)gOut_Ref;
        double *s = (double *)gIn;
        double *s2 = (double *)gIn2;
        double *s3 = (double *)gIn3;
        for (j = 0; j < BUFFER_SIZE / sizeof(double); j++)
            r[j] = (double)f->dfunc.f_fff(s[j], s2[j], s3[j]);

        // Read the data back
        for (j = gMinVectorSizeIndex; j < gMaxVectorSizeIndex; j++)
        {
            if ((error =
                     clEnqueueReadBuffer(gQueue, gOutBuffer[j], CL_TRUE, 0,
                                         BUFFER_SIZE, gOut[j], 0, NULL, NULL)))
            {
                vlog_error("ReadArray failed %d\n", error);
                goto exit;
            }
        }

        if (gSkipCorrectnessTesting) break;

        // Verify data -- No verification possible.
        // MAD is a random number generator.
        if (0 == (i & 0x0fffffff))
        {
            vlog(".");
            fflush(stdout);
        }
    }

    if (!gSkipCorrectnessTesting)
    {
        if (gWimpyMode)
            vlog("Wimp pass");
        else
            vlog("passed");

        vlog("\t%8.2f @ {%a, %a, %a}", maxError, maxErrorVal, maxErrorVal2,
             maxErrorVal3);
    }

    vlog("\n");

exit:
    // Release
    for (k = gMinVectorSizeIndex; k < gMaxVectorSizeIndex; k++)
    {
        clReleaseKernel(kernels[k]);
        clReleaseProgram(programs[k]);
    }

    return error;
}
