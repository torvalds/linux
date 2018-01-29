/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012-2017 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012-2017 Intel Corporation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NTB_INTEL_GEN3_H_
#define _NTB_INTEL_GEN3_H_

/* Intel Skylake Xeon hardware */
#define SKX_IMBAR1SZ_OFFSET		0x00d0
#define SKX_IMBAR2SZ_OFFSET		0x00d1
#define SKX_EMBAR1SZ_OFFSET		0x00d2
#define SKX_EMBAR2SZ_OFFSET		0x00d3
#define SKX_DEVCTRL_OFFSET		0x0098
#define SKX_DEVSTS_OFFSET		0x009a
#define SKX_UNCERRSTS_OFFSET		0x014c
#define SKX_CORERRSTS_OFFSET		0x0158
#define SKX_LINK_STATUS_OFFSET		0x01a2

#define SKX_NTBCNTL_OFFSET		0x0000
#define SKX_IMBAR1XBASE_OFFSET		0x0010		/* SBAR2XLAT */
#define SKX_IMBAR1XLMT_OFFSET		0x0018		/* SBAR2LMT */
#define SKX_IMBAR2XBASE_OFFSET		0x0020		/* SBAR4XLAT */
#define SKX_IMBAR2XLMT_OFFSET		0x0028		/* SBAR4LMT */
#define SKX_IM_INT_STATUS_OFFSET	0x0040
#define SKX_IM_INT_DISABLE_OFFSET	0x0048
#define SKX_IM_SPAD_OFFSET		0x0080		/* SPAD */
#define SKX_USMEMMISS_OFFSET		0x0070
#define SKX_INTVEC_OFFSET		0x00d0
#define SKX_IM_DOORBELL_OFFSET		0x0100		/* SDOORBELL0 */
#define SKX_B2B_SPAD_OFFSET		0x0180		/* B2B SPAD */
#define SKX_EMBAR0XBASE_OFFSET		0x4008		/* B2B_XLAT */
#define SKX_EMBAR1XBASE_OFFSET		0x4010		/* PBAR2XLAT */
#define SKX_EMBAR1XLMT_OFFSET		0x4018		/* PBAR2LMT */
#define SKX_EMBAR2XBASE_OFFSET		0x4020		/* PBAR4XLAT */
#define SKX_EMBAR2XLMT_OFFSET		0x4028		/* PBAR4LMT */
#define SKX_EM_INT_STATUS_OFFSET	0x4040
#define SKX_EM_INT_DISABLE_OFFSET	0x4048
#define SKX_EM_SPAD_OFFSET		0x4080		/* remote SPAD */
#define SKX_EM_DOORBELL_OFFSET		0x4100		/* PDOORBELL0 */
#define SKX_SPCICMD_OFFSET		0x4504		/* SPCICMD */
#define SKX_EMBAR0_OFFSET		0x4510		/* SBAR0BASE */
#define SKX_EMBAR1_OFFSET		0x4518		/* SBAR23BASE */
#define SKX_EMBAR2_OFFSET		0x4520		/* SBAR45BASE */

#define SKX_DB_COUNT			32
#define SKX_DB_LINK			32
#define SKX_DB_LINK_BIT			BIT_ULL(SKX_DB_LINK)
#define SKX_DB_MSIX_VECTOR_COUNT	33
#define SKX_DB_MSIX_VECTOR_SHIFT	1
#define SKX_DB_TOTAL_SHIFT		33
#define SKX_SPAD_COUNT			16

#endif
