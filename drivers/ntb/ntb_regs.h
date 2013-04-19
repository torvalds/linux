/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
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
 *
 * Intel PCIe NTB Linux driver
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */

#define NTB_LINK_ENABLE		0x0000
#define NTB_LINK_DISABLE	0x0002
#define NTB_LINK_STATUS_ACTIVE	0x2000
#define NTB_LINK_SPEED_MASK	0x000f
#define NTB_LINK_WIDTH_MASK	0x03f0

#define SNB_MSIX_CNT		4
#define SNB_MAX_B2B_SPADS	16
#define SNB_MAX_COMPAT_SPADS	16
/* Reserve the uppermost bit for link interrupt */
#define SNB_MAX_DB_BITS		15
#define SNB_DB_BITS_PER_VEC	5
#define SNB_MAX_MW		2
#define SNB_ERRATA_MAX_MW	1

#define SNB_DB_HW_LINK		0x8000

#define SNB_PCICMD_OFFSET	0x0504
#define SNB_DEVCTRL_OFFSET	0x0598
#define SNB_LINK_STATUS_OFFSET	0x01A2

#define SNB_PBAR2LMT_OFFSET	0x0000
#define SNB_PBAR4LMT_OFFSET	0x0008
#define SNB_PBAR2XLAT_OFFSET	0x0010
#define SNB_PBAR4XLAT_OFFSET	0x0018
#define SNB_SBAR2LMT_OFFSET	0x0020
#define SNB_SBAR4LMT_OFFSET	0x0028
#define SNB_SBAR2XLAT_OFFSET	0x0030
#define SNB_SBAR4XLAT_OFFSET	0x0038
#define SNB_SBAR0BASE_OFFSET	0x0040
#define SNB_SBAR0BASE_OFFSET	0x0040
#define SNB_SBAR2BASE_OFFSET	0x0048
#define SNB_SBAR4BASE_OFFSET	0x0050
#define SNB_SBAR2BASE_OFFSET	0x0048
#define SNB_SBAR4BASE_OFFSET	0x0050
#define SNB_NTBCNTL_OFFSET	0x0058
#define SNB_SBDF_OFFSET		0x005C
#define SNB_PDOORBELL_OFFSET	0x0060
#define SNB_PDBMSK_OFFSET	0x0062
#define SNB_SDOORBELL_OFFSET	0x0064
#define SNB_SDBMSK_OFFSET	0x0066
#define SNB_USMEMMISS		0x0070
#define SNB_SPAD_OFFSET		0x0080
#define SNB_SPADSEMA4_OFFSET	0x00c0
#define SNB_WCCNTRL_OFFSET	0x00e0
#define SNB_B2B_SPAD_OFFSET	0x0100
#define SNB_B2B_DOORBELL_OFFSET	0x0140
#define SNB_B2B_XLAT_OFFSETL	0x0144
#define SNB_B2B_XLAT_OFFSETU	0x0148

#define SNB_MBAR01_USD_ADDR	0x000000210000000CULL
#define SNB_MBAR23_USD_ADDR	0x000000410000000CULL
#define SNB_MBAR45_USD_ADDR	0x000000810000000CULL
#define SNB_MBAR01_DSD_ADDR	0x000000200000000CULL
#define SNB_MBAR23_DSD_ADDR	0x000000400000000CULL
#define SNB_MBAR45_DSD_ADDR	0x000000800000000CULL

#define BWD_MSIX_CNT		34
#define BWD_MAX_SPADS		16
#define BWD_MAX_COMPAT_SPADS	16
#define BWD_MAX_DB_BITS		34
#define BWD_DB_BITS_PER_VEC	1
#define BWD_MAX_MW		2

#define BWD_PCICMD_OFFSET	0xb004
#define BWD_MBAR23_OFFSET	0xb018
#define BWD_MBAR45_OFFSET	0xb020
#define BWD_DEVCTRL_OFFSET	0xb048
#define BWD_LINK_STATUS_OFFSET	0xb052

#define BWD_SBAR2XLAT_OFFSET	0x0008
#define BWD_SBAR4XLAT_OFFSET	0x0010
#define BWD_PDOORBELL_OFFSET	0x0020
#define BWD_PDBMSK_OFFSET	0x0028
#define BWD_NTBCNTL_OFFSET	0x0060
#define BWD_EBDF_OFFSET		0x0064
#define BWD_SPAD_OFFSET		0x0080
#define BWD_SPADSEMA_OFFSET	0x00c0
#define BWD_STKYSPAD_OFFSET	0x00c4
#define BWD_PBAR2XLAT_OFFSET	0x8008
#define BWD_PBAR4XLAT_OFFSET	0x8010
#define BWD_B2B_DOORBELL_OFFSET	0x8020
#define BWD_B2B_SPAD_OFFSET	0x8080
#define BWD_B2B_SPADSEMA_OFFSET	0x80c0
#define BWD_B2B_STKYSPAD_OFFSET	0x80c4

#define NTB_CNTL_BAR23_SNOOP	(1 << 2)
#define NTB_CNTL_BAR45_SNOOP	(1 << 6)
#define BWD_CNTL_LINK_DOWN	(1 << 16)

#define NTB_PPD_OFFSET		0x00D4
#define SNB_PPD_CONN_TYPE	0x0003
#define SNB_PPD_DEV_TYPE	0x0010
#define BWD_PPD_INIT_LINK	0x0008
#define BWD_PPD_CONN_TYPE	0x0300
#define BWD_PPD_DEV_TYPE	0x1000
