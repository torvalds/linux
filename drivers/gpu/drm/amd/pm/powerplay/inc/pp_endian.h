/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _PP_ENDIAN_H_
#define _PP_ENDIAN_H_

#define PP_HOST_TO_SMC_UL(X) cpu_to_be32(X)
#define PP_SMC_TO_HOST_UL(X) be32_to_cpu(X)

#define PP_HOST_TO_SMC_US(X) cpu_to_be16(X)
#define PP_SMC_TO_HOST_US(X) be16_to_cpu(X)

#define CONVERT_FROM_HOST_TO_SMC_UL(X) ((X) = PP_HOST_TO_SMC_UL(X))
#define CONVERT_FROM_SMC_TO_HOST_UL(X) ((X) = PP_SMC_TO_HOST_UL(X))

#define CONVERT_FROM_HOST_TO_SMC_US(X) ((X) = PP_HOST_TO_SMC_US(X))

#endif /* _PP_ENDIAN_H_ */
