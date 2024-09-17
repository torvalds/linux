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

#ifndef _NTB_INTEL_GEN1_H_
#define _NTB_INTEL_GEN1_H_

#include "ntb_hw_intel.h"

/* Intel Gen1 Xeon hardware */
#define XEON_PBAR23LMT_OFFSET		0x0000
#define XEON_PBAR45LMT_OFFSET		0x0008
#define XEON_PBAR4LMT_OFFSET		0x0008
#define XEON_PBAR5LMT_OFFSET		0x000c
#define XEON_PBAR23XLAT_OFFSET		0x0010
#define XEON_PBAR45XLAT_OFFSET		0x0018
#define XEON_PBAR4XLAT_OFFSET		0x0018
#define XEON_PBAR5XLAT_OFFSET		0x001c
#define XEON_SBAR23LMT_OFFSET		0x0020
#define XEON_SBAR45LMT_OFFSET		0x0028
#define XEON_SBAR4LMT_OFFSET		0x0028
#define XEON_SBAR5LMT_OFFSET		0x002c
#define XEON_SBAR23XLAT_OFFSET		0x0030
#define XEON_SBAR45XLAT_OFFSET		0x0038
#define XEON_SBAR4XLAT_OFFSET		0x0038
#define XEON_SBAR5XLAT_OFFSET		0x003c
#define XEON_SBAR0BASE_OFFSET		0x0040
#define XEON_SBAR23BASE_OFFSET		0x0048
#define XEON_SBAR45BASE_OFFSET		0x0050
#define XEON_SBAR4BASE_OFFSET		0x0050
#define XEON_SBAR5BASE_OFFSET		0x0054
#define XEON_SBDF_OFFSET		0x005c
#define XEON_NTBCNTL_OFFSET		0x0058
#define XEON_PDOORBELL_OFFSET		0x0060
#define XEON_PDBMSK_OFFSET		0x0062
#define XEON_SDOORBELL_OFFSET		0x0064
#define XEON_SDBMSK_OFFSET		0x0066
#define XEON_USMEMMISS_OFFSET		0x0070
#define XEON_SPAD_OFFSET		0x0080
#define XEON_PBAR23SZ_OFFSET		0x00d0
#define XEON_PBAR45SZ_OFFSET		0x00d1
#define XEON_PBAR4SZ_OFFSET		0x00d1
#define XEON_SBAR23SZ_OFFSET		0x00d2
#define XEON_SBAR45SZ_OFFSET		0x00d3
#define XEON_SBAR4SZ_OFFSET		0x00d3
#define XEON_PPD_OFFSET			0x00d4
#define XEON_PBAR5SZ_OFFSET		0x00d5
#define XEON_SBAR5SZ_OFFSET		0x00d6
#define XEON_WCCNTRL_OFFSET		0x00e0
#define XEON_UNCERRSTS_OFFSET		0x014c
#define XEON_CORERRSTS_OFFSET		0x0158
#define XEON_LINK_STATUS_OFFSET		0x01a2
#define XEON_SPCICMD_OFFSET		0x0504
#define XEON_DEVCTRL_OFFSET		0x0598
#define XEON_DEVSTS_OFFSET		0x059a
#define XEON_SLINK_STATUS_OFFSET	0x05a2
#define XEON_B2B_SPAD_OFFSET		0x0100
#define XEON_B2B_DOORBELL_OFFSET	0x0140
#define XEON_B2B_XLAT_OFFSETL		0x0144
#define XEON_B2B_XLAT_OFFSETU		0x0148
#define XEON_PPD_CONN_MASK		0x03
#define XEON_PPD_CONN_TRANSPARENT	0x00
#define XEON_PPD_CONN_B2B		0x01
#define XEON_PPD_CONN_RP		0x02
#define XEON_PPD_DEV_MASK		0x10
#define XEON_PPD_DEV_USD		0x00
#define XEON_PPD_DEV_DSD		0x10
#define XEON_PPD_SPLIT_BAR_MASK		0x40

#define XEON_PPD_TOPO_MASK	(XEON_PPD_CONN_MASK | XEON_PPD_DEV_MASK)
#define XEON_PPD_TOPO_PRI_USD	(XEON_PPD_CONN_RP | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_PRI_DSD	(XEON_PPD_CONN_RP | XEON_PPD_DEV_DSD)
#define XEON_PPD_TOPO_SEC_USD	(XEON_PPD_CONN_TRANSPARENT | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_SEC_DSD	(XEON_PPD_CONN_TRANSPARENT | XEON_PPD_DEV_DSD)
#define XEON_PPD_TOPO_B2B_USD	(XEON_PPD_CONN_B2B | XEON_PPD_DEV_USD)
#define XEON_PPD_TOPO_B2B_DSD	(XEON_PPD_CONN_B2B | XEON_PPD_DEV_DSD)

#define XEON_MW_COUNT			2
#define HSX_SPLIT_BAR_MW_COUNT		3
#define XEON_DB_COUNT			15
#define XEON_DB_LINK			15
#define XEON_DB_LINK_BIT			BIT_ULL(XEON_DB_LINK)
#define XEON_DB_MSIX_VECTOR_COUNT	4
#define XEON_DB_MSIX_VECTOR_SHIFT	5
#define XEON_DB_TOTAL_SHIFT		16
#define XEON_SPAD_COUNT			16

/* Use the following addresses for translation between b2b ntb devices in case
 * the hardware default values are not reliable. */
#define XEON_B2B_BAR0_ADDR	0x1000000000000000ull
#define XEON_B2B_BAR2_ADDR64	0x2000000000000000ull
#define XEON_B2B_BAR4_ADDR64	0x4000000000000000ull
#define XEON_B2B_BAR4_ADDR32	0x20000000u
#define XEON_B2B_BAR5_ADDR32	0x40000000u

/* The peer ntb secondary config space is 32KB fixed size */
#define XEON_B2B_MIN_SIZE		0x8000

/* flags to indicate hardware errata */
#define NTB_HWERR_SDOORBELL_LOCKUP	BIT_ULL(0)
#define NTB_HWERR_SB01BASE_LOCKUP	BIT_ULL(1)
#define NTB_HWERR_B2BDOORBELL_BIT14	BIT_ULL(2)
#define NTB_HWERR_MSIX_VECTOR32_BAD	BIT_ULL(3)
#define NTB_HWERR_BAR_ALIGN		BIT_ULL(4)
#define NTB_HWERR_LTR_BAD		BIT_ULL(5)

extern struct intel_b2b_addr xeon_b2b_usd_addr;
extern struct intel_b2b_addr xeon_b2b_dsd_addr;

int ndev_init_isr(struct intel_ntb_dev *ndev, int msix_min, int msix_max,
		int msix_shift, int total_shift);
enum ntb_topo xeon_ppd_topo(struct intel_ntb_dev *ndev, u8 ppd);
void ndev_db_addr(struct intel_ntb_dev *ndev,
				phys_addr_t *db_addr, resource_size_t *db_size,
				phys_addr_t reg_addr, unsigned long reg);
u64 ndev_db_read(struct intel_ntb_dev *ndev, void __iomem *mmio);
int ndev_db_write(struct intel_ntb_dev *ndev, u64 db_bits,
				void __iomem *mmio);
int ndev_mw_to_bar(struct intel_ntb_dev *ndev, int idx);
int intel_ntb_mw_count(struct ntb_dev *ntb, int pidx);
int intel_ntb_mw_get_align(struct ntb_dev *ntb, int pidx, int idx,
		resource_size_t *addr_align, resource_size_t *size_align,
		resource_size_t *size_max);
int intel_ntb_peer_mw_count(struct ntb_dev *ntb);
int intel_ntb_peer_mw_get_addr(struct ntb_dev *ntb, int idx,
		phys_addr_t *base, resource_size_t *size);
u64 intel_ntb_link_is_up(struct ntb_dev *ntb, enum ntb_speed *speed,
		enum ntb_width *width);
int intel_ntb_link_disable(struct ntb_dev *ntb);
u64 intel_ntb_db_valid_mask(struct ntb_dev *ntb);
int intel_ntb_db_vector_count(struct ntb_dev *ntb);
u64 intel_ntb_db_vector_mask(struct ntb_dev *ntb, int db_vector);
int intel_ntb_db_set_mask(struct ntb_dev *ntb, u64 db_bits);
int intel_ntb_db_clear_mask(struct ntb_dev *ntb, u64 db_bits);
int intel_ntb_spad_is_unsafe(struct ntb_dev *ntb);
int intel_ntb_spad_count(struct ntb_dev *ntb);
u32 intel_ntb_spad_read(struct ntb_dev *ntb, int idx);
int intel_ntb_spad_write(struct ntb_dev *ntb, int idx, u32 val);
u32 intel_ntb_peer_spad_read(struct ntb_dev *ntb, int pidx, int sidx);
int intel_ntb_peer_spad_write(struct ntb_dev *ntb, int pidx, int sidx,
		u32 val);
int intel_ntb_peer_spad_addr(struct ntb_dev *ntb, int pidx, int sidx,
				    phys_addr_t *spad_addr);
int xeon_link_is_up(struct intel_ntb_dev *ndev);

#endif
