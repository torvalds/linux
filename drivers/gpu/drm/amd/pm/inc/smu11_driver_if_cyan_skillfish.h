/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef __SMU11_DRIVER_IF_CYAN_SKILLFISH_H__
#define __SMU11_DRIVER_IF_CYAN_SKILLFISH_H__

// *** IMPORTANT ***
// Always increment the interface version if
// any structure is changed in this file
#define MP1_DRIVER_IF_VERSION 0x8

#define TABLE_BIOS_IF            0 // Called by BIOS
#define TABLE_WATERMARKS         1 // Called by Driver; defined here, but not used, for backward compatible
#define TABLE_PMSTATUSLOG        3 // Called by Tools for Agm logging
#define TABLE_DPMCLOCKS          4 // Called by Driver; defined here, but not used, for backward compatible
#define TABLE_MOMENTARY_PM       5 // Called by Tools; defined here, but not used, for backward compatible
#define TABLE_COUNT              6

#define NUM_DSPCLK_LEVELS		8
#define NUM_SOCCLK_DPM_LEVELS	8
#define NUM_DCEFCLK_DPM_LEVELS	4
#define NUM_FCLK_DPM_LEVELS		4
#define NUM_MEMCLK_DPM_LEVELS	4

#define NUMBER_OF_PSTATES		8
#define NUMBER_OF_CORES			8

typedef enum {
	S3_TYPE_ENTRY,
	S5_TYPE_ENTRY,
} Sleep_Type_e;

typedef enum {
	GFX_OFF = 0,
	GFX_ON  = 1,
} GFX_Mode_e;

typedef enum {
	CPU_P0 = 0,
	CPU_P1,
	CPU_P2,
	CPU_P3,
	CPU_P4,
	CPU_P5,
	CPU_P6,
	CPU_P7
} CPU_PState_e;

typedef enum {
	CPU_CORE0 = 0,
	CPU_CORE1,
	CPU_CORE2,
	CPU_CORE3,
	CPU_CORE4,
	CPU_CORE5,
	CPU_CORE6,
	CPU_CORE7
} CORE_ID_e;

typedef enum {
	DF_DPM0 = 0,
	DF_DPM1,
	DF_DPM2,
	DF_DPM3,
	DF_PState_Count
} DF_PState_e;

typedef enum {
	GFX_DPM0 = 0,
	GFX_DPM1,
	GFX_DPM2,
	GFX_DPM3,
	GFX_PState_Count
} GFX_PState_e;

#endif
