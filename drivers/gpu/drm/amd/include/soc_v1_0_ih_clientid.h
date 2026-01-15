/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __SOC_V1_0_IH_CLIENTID_H__
#define __SOC_V1_0_IH_CLIENTID_H__

extern const char *soc_v1_0_ih_clientid_name[];

enum soc_v1_0_ih_clientid {
	SOC_V1_0_IH_CLIENTID_IH			= 0x00,
	SOC_V1_0_IH_CLIENTID_ATHUB		= 0x02,
	SOC_V1_0_IH_CLIENTID_BIF		= 0x03,
	SOC_V1_0_IH_CLIENTID_RLC		= 0x07,
	SOC_V1_0_IH_CLIENTID_GFX		= 0x0a,
	SOC_V1_0_IH_CLIENTID_IMU		= 0x0b,
	SOC_V1_0_IH_CLIENTID_VCN1		= 0x0e,
	SOC_V1_0_IH_CLIENTID_THM		= 0x0f,
	SOC_V1_0_IH_CLIENTID_VCN		= 0x10,
	SOC_V1_0_IH_CLIENTID_VMC		= 0x12,
	SOC_V1_0_IH_CLIENTID_GRBM_CP		= 0x14,
	SOC_V1_0_IH_CLIENTID_GC_AID		= 0x15,
	SOC_V1_0_IH_CLIENTID_ROM_SMUIO		= 0x16,
	SOC_V1_0_IH_CLIENTID_DF			= 0x17,
	SOC_V1_0_IH_CLIENTID_PWR		= 0x19,
	SOC_V1_0_IH_CLIENTID_LSDMA		= 0x1a,
	SOC_V1_0_IH_CLIENTID_GC_UTCL2		= 0x1b,
	SOC_V1_0_IH_CLIENTID_nHT		= 0X1c,
	SOC_V1_0_IH_CLIENTID_MP0		= 0x1e,
	SOC_V1_0_IH_CLIENTID_MP1		= 0x1f,
	SOC_V1_0_IH_CLIENTID_MAX,
};

#endif
