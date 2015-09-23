/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#ifndef PP_ASICBLOCKS_H
#define PP_ASICBLOCKS_H


enum PHM_AsicBlock {
	PHM_AsicBlock_GFX,
	PHM_AsicBlock_UVD_MVC,
	PHM_AsicBlock_UVD,
	PHM_AsicBlock_UVD_HD,
	PHM_AsicBlock_UVD_SD,
	PHM_AsicBlock_Count
};

enum PHM_ClockGateSetting {
	PHM_ClockGateSetting_StaticOn,
	PHM_ClockGateSetting_StaticOff,
	PHM_ClockGateSetting_Dynamic
};

struct phm_asic_blocks {
	bool gfx : 1;
	bool uvd : 1;
};

#endif
