/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register bitfield descriptions for Pondicherry2 memory controller.
 *
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef _PND2_REGS_H
#define _PND2_REGS_H

struct b_cr_touud_lo_pci {
	u32	lock : 1;
	u32	reserved_1 : 19;
	u32	touud : 12;
};

#define b_cr_touud_lo_pci_port 0x4c
#define b_cr_touud_lo_pci_offset 0xa8
#define b_cr_touud_lo_pci_r_opcode 0x04

struct b_cr_touud_hi_pci {
	u32	touud : 7;
	u32	reserved_0 : 25;
};

#define b_cr_touud_hi_pci_port 0x4c
#define b_cr_touud_hi_pci_offset 0xac
#define b_cr_touud_hi_pci_r_opcode 0x04

struct b_cr_tolud_pci {
	u32	lock : 1;
	u32	reserved_0 : 19;
	u32	tolud : 12;
};

#define b_cr_tolud_pci_port 0x4c
#define b_cr_tolud_pci_offset 0xbc
#define b_cr_tolud_pci_r_opcode 0x04

struct b_cr_mchbar_lo_pci {
	u32 enable : 1;
	u32 pad_3_1 : 3;
	u32 pad_14_4: 11;
	u32 base: 17;
};

struct b_cr_mchbar_hi_pci {
	u32 base : 7;
	u32 pad_31_7 : 25;
};

/* Symmetric region */
struct b_cr_slice_channel_hash {
	u64	slice_1_disabled : 1;
	u64	hvm_mode : 1;
	u64	interleave_mode : 2;
	u64	slice_0_mem_disabled : 1;
	u64	reserved_0 : 1;
	u64	slice_hash_mask : 14;
	u64	reserved_1 : 11;
	u64	enable_pmi_dual_data_mode : 1;
	u64	ch_1_disabled : 1;
	u64	reserved_2 : 1;
	u64	sym_slice0_channel_enabled : 2;
	u64	sym_slice1_channel_enabled : 2;
	u64	ch_hash_mask : 14;
	u64	reserved_3 : 11;
	u64	lock : 1;
};

#define b_cr_slice_channel_hash_port 0x4c
#define b_cr_slice_channel_hash_offset 0x4c58
#define b_cr_slice_channel_hash_r_opcode 0x06

struct b_cr_mot_out_base_mchbar {
	u32	reserved_0 : 14;
	u32	mot_out_base : 15;
	u32	reserved_1 : 1;
	u32	tr_en : 1;
	u32	imr_en : 1;
};

#define b_cr_mot_out_base_mchbar_port 0x4c
#define b_cr_mot_out_base_mchbar_offset 0x6af0
#define b_cr_mot_out_base_mchbar_r_opcode 0x00

struct b_cr_mot_out_mask_mchbar {
	u32	reserved_0 : 14;
	u32	mot_out_mask : 15;
	u32	reserved_1 : 1;
	u32	ia_iwb_en : 1;
	u32	gt_iwb_en : 1;
};

#define b_cr_mot_out_mask_mchbar_port 0x4c
#define b_cr_mot_out_mask_mchbar_offset 0x6af4
#define b_cr_mot_out_mask_mchbar_r_opcode 0x00

struct b_cr_asym_mem_region0_mchbar {
	u32	pad : 4;
	u32	slice0_asym_base : 11;
	u32	pad_18_15 : 4;
	u32	slice0_asym_limit : 11;
	u32	slice0_asym_channel_select : 1;
	u32	slice0_asym_enable : 1;
};

#define b_cr_asym_mem_region0_mchbar_port 0x4c
#define b_cr_asym_mem_region0_mchbar_offset 0x6e40
#define b_cr_asym_mem_region0_mchbar_r_opcode 0x00

struct b_cr_asym_mem_region1_mchbar {
	u32	pad : 4;
	u32	slice1_asym_base : 11;
	u32	pad_18_15 : 4;
	u32	slice1_asym_limit : 11;
	u32	slice1_asym_channel_select : 1;
	u32	slice1_asym_enable : 1;
};

#define b_cr_asym_mem_region1_mchbar_port 0x4c
#define b_cr_asym_mem_region1_mchbar_offset 0x6e44
#define b_cr_asym_mem_region1_mchbar_r_opcode 0x00

/* Some bit fields moved in above two structs on Denverton */
struct b_cr_asym_mem_region_denverton {
	u32	pad : 4;
	u32	slice_asym_base : 8;
	u32	pad_19_12 : 8;
	u32	slice_asym_limit : 8;
	u32	pad_28_30 : 3;
	u32	slice_asym_enable : 1;
};

struct b_cr_asym_2way_mem_region_mchbar {
	u32	pad : 2;
	u32	asym_2way_intlv_mode : 2;
	u32	asym_2way_base : 11;
	u32	pad_16_15 : 2;
	u32	asym_2way_limit : 11;
	u32	pad_30_28 : 3;
	u32	asym_2way_interleave_enable : 1;
};

#define b_cr_asym_2way_mem_region_mchbar_port 0x4c
#define b_cr_asym_2way_mem_region_mchbar_offset 0x6e50
#define b_cr_asym_2way_mem_region_mchbar_r_opcode 0x00

/* Apollo Lake d-unit */

struct d_cr_drp0 {
	u32	rken0 : 1;
	u32	rken1 : 1;
	u32	ddmen : 1;
	u32	rsvd3 : 1;
	u32	dwid : 2;
	u32	dden : 3;
	u32	rsvd13_9 : 5;
	u32	rsien : 1;
	u32	bahen : 1;
	u32	rsvd18_16 : 3;
	u32	caswizzle : 2;
	u32	eccen : 1;
	u32	dramtype : 3;
	u32	blmode : 3;
	u32	addrdec : 2;
	u32	dramdevice_pr : 2;
};

#define d_cr_drp0_offset 0x1400
#define d_cr_drp0_r_opcode 0x00

/* Denverton d-unit */

struct d_cr_dsch {
	u32	ch0en : 1;
	u32	ch1en : 1;
	u32	ddr4en : 1;
	u32	coldwake : 1;
	u32	newbypdis : 1;
	u32	chan_width : 1;
	u32	rsvd6_6 : 1;
	u32	ooodis : 1;
	u32	rsvd18_8 : 11;
	u32	ic : 1;
	u32	rsvd31_20 : 12;
};

#define d_cr_dsch_port 0x16
#define d_cr_dsch_offset 0x0
#define d_cr_dsch_r_opcode 0x0

struct d_cr_ecc_ctrl {
	u32	eccen : 1;
	u32	rsvd31_1 : 31;
};

#define d_cr_ecc_ctrl_offset 0x180
#define d_cr_ecc_ctrl_r_opcode 0x0

struct d_cr_drp {
	u32	rken0 : 1;
	u32	rken1 : 1;
	u32	rken2 : 1;
	u32	rken3 : 1;
	u32	dimmdwid0 : 2;
	u32	dimmdden0 : 2;
	u32	dimmdwid1 : 2;
	u32	dimmdden1 : 2;
	u32	rsvd15_12 : 4;
	u32	dimmflip : 1;
	u32	rsvd31_17 : 15;
};

#define d_cr_drp_offset 0x158
#define d_cr_drp_r_opcode 0x0

struct d_cr_dmap {
	u32	ba0 : 5;
	u32	ba1 : 5;
	u32	bg0 : 5; /* if ddr3, ba2 = bg0 */
	u32	bg1 : 5; /* if ddr3, ba3 = bg1 */
	u32	rs0 : 5;
	u32	rs1 : 5;
	u32	rsvd : 2;
};

#define d_cr_dmap_offset 0x174
#define d_cr_dmap_r_opcode 0x0

struct d_cr_dmap1 {
	u32	ca11 : 6;
	u32	bxor : 1;
	u32	rsvd : 25;
};

#define d_cr_dmap1_offset 0xb4
#define d_cr_dmap1_r_opcode 0x0

struct d_cr_dmap2 {
	u32	row0 : 5;
	u32	row1 : 5;
	u32	row2 : 5;
	u32	row3 : 5;
	u32	row4 : 5;
	u32	row5 : 5;
	u32	rsvd : 2;
};

#define d_cr_dmap2_offset 0x148
#define d_cr_dmap2_r_opcode 0x0

struct d_cr_dmap3 {
	u32	row6 : 5;
	u32	row7 : 5;
	u32	row8 : 5;
	u32	row9 : 5;
	u32	row10 : 5;
	u32	row11 : 5;
	u32	rsvd : 2;
};

#define d_cr_dmap3_offset 0x14c
#define d_cr_dmap3_r_opcode 0x0

struct d_cr_dmap4 {
	u32	row12 : 5;
	u32	row13 : 5;
	u32	row14 : 5;
	u32	row15 : 5;
	u32	row16 : 5;
	u32	row17 : 5;
	u32	rsvd : 2;
};

#define d_cr_dmap4_offset 0x150
#define d_cr_dmap4_r_opcode 0x0

struct d_cr_dmap5 {
	u32	ca3 : 4;
	u32	ca4 : 4;
	u32	ca5 : 4;
	u32	ca6 : 4;
	u32	ca7 : 4;
	u32	ca8 : 4;
	u32	ca9 : 4;
	u32	rsvd : 4;
};

#define d_cr_dmap5_offset 0x154
#define d_cr_dmap5_r_opcode 0x0

#endif /* _PND2_REGS_H */
