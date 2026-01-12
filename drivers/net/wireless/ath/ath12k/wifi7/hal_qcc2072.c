// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_qcc2072.h"

const struct ath12k_hw_regs qcc2072_regs = {
	/* SW2TCL(x) R0 ring configuration address */
	.tcl1_ring_id = 0x00000920,
	.tcl1_ring_misc = 0x00000928,
	.tcl1_ring_tp_addr_lsb = 0x00000934,
	.tcl1_ring_tp_addr_msb = 0x00000938,
	.tcl1_ring_consumer_int_setup_ix0 = 0x00000948,
	.tcl1_ring_consumer_int_setup_ix1 = 0x0000094c,
	.tcl1_ring_msi1_base_lsb = 0x00000960,
	.tcl1_ring_msi1_base_msb = 0x00000964,
	.tcl1_ring_msi1_data = 0x00000968,
	.tcl_ring_base_lsb = 0x00000b70,
	.tcl1_ring_base_lsb = 0x00000918,
	.tcl1_ring_base_msb = 0x0000091c,
	.tcl2_ring_base_lsb = 0x00000990,

	/* TCL STATUS ring address */
	.tcl_status_ring_base_lsb = 0x00000d50,

	.wbm_idle_ring_base_lsb = 0x00000d3c,
	.wbm_idle_ring_misc_addr = 0x00000d4c,
	.wbm_r0_idle_list_cntl_addr = 0x00000240,
	.wbm_r0_idle_list_size_addr = 0x00000244,
	.wbm_scattered_ring_base_lsb = 0x00000250,
	.wbm_scattered_ring_base_msb = 0x00000254,
	.wbm_scattered_desc_head_info_ix0 = 0x00000260,
	.wbm_scattered_desc_head_info_ix1 = 0x00000264,
	.wbm_scattered_desc_tail_info_ix0 = 0x00000270,
	.wbm_scattered_desc_tail_info_ix1 = 0x00000274,
	.wbm_scattered_desc_ptr_hp_addr = 0x00000027c,

	.wbm_sw_release_ring_base_lsb = 0x0000037c,
	.wbm_sw1_release_ring_base_lsb = ATH12K_HW_REG_UNDEFINED,
	.wbm0_release_ring_base_lsb = 0x00000e08,
	.wbm1_release_ring_base_lsb = 0x00000e80,

	/* PCIe base address */
	.pcie_qserdes_sysclk_en_sel = 0x01e0c0ac,
	.pcie_pcs_osc_dtct_config_base = 0x01e0cc58,

	/* PPE release ring address */
	.ppe_rel_ring_base = 0x0000046c,

	/* REO DEST ring address */
	.reo2_ring_base = 0x00000578,
	.reo1_misc_ctrl_addr = 0x00000ba0,
	.reo1_sw_cookie_cfg0 = 0x0000006c,
	.reo1_sw_cookie_cfg1 = 0x00000070,
	.reo1_qdesc_lut_base0 = ATH12K_HW_REG_UNDEFINED,
	.reo1_qdesc_lut_base1 = ATH12K_HW_REG_UNDEFINED,

	.reo1_ring_base_lsb = 0x00000500,
	.reo1_ring_base_msb = 0x00000504,
	.reo1_ring_id = 0x00000508,
	.reo1_ring_misc = 0x00000510,
	.reo1_ring_hp_addr_lsb = 0x00000514,
	.reo1_ring_hp_addr_msb = 0x00000518,
	.reo1_ring_producer_int_setup = 0x00000524,
	.reo1_ring_msi1_base_lsb = 0x00000548,
	.reo1_ring_msi1_base_msb = 0x0000054c,
	.reo1_ring_msi1_data = 0x00000550,
	.reo1_aging_thres_ix0 = 0x00000b2c,
	.reo1_aging_thres_ix1 = 0x00000b30,
	.reo1_aging_thres_ix2 = 0x00000b34,
	.reo1_aging_thres_ix3 = 0x00000b38,

	/* REO Exception ring address */
	.reo2_sw0_ring_base = 0x000008c0,

	/* REO Reinject ring address */
	.sw2reo_ring_base = 0x00000320,
	.sw2reo1_ring_base = 0x00000398,

	/* REO cmd ring address */
	.reo_cmd_ring_base = 0x000002a8,

	/* REO status ring address */
	.reo_status_ring_base = 0x00000aa0,

	/* CE base address */
	.umac_ce0_src_reg_base = 0x01b80000,
	.umac_ce0_dest_reg_base = 0x01b81000,
	.umac_ce1_src_reg_base = 0x01b82000,
	.umac_ce1_dest_reg_base = 0x01b83000,

	.gcc_gcc_pcie_hot_rst = 0x1e65304,
};
