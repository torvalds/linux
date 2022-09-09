// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014-2017 Qualcomm Atheros, Inc.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include "core.h"
#include "hw.h"
#include "hif.h"
#include "wmi-ops.h"
#include "bmi.h"
#include "rx_desc.h"

const struct ath10k_hw_regs qca988x_regs = {
	.rtc_soc_base_address		= 0x00004000,
	.rtc_wmac_base_address		= 0x00005000,
	.soc_core_base_address		= 0x00009000,
	.wlan_mac_base_address		= 0x00020000,
	.ce_wrapper_base_address	= 0x00057000,
	.ce0_base_address		= 0x00057400,
	.ce1_base_address		= 0x00057800,
	.ce2_base_address		= 0x00057c00,
	.ce3_base_address		= 0x00058000,
	.ce4_base_address		= 0x00058400,
	.ce5_base_address		= 0x00058800,
	.ce6_base_address		= 0x00058c00,
	.ce7_base_address		= 0x00059000,
	.soc_reset_control_si0_rst_mask	= 0x00000001,
	.soc_reset_control_ce_rst_mask	= 0x00040000,
	.soc_chip_id_address		= 0x000000ec,
	.scratch_3_address		= 0x00000030,
	.fw_indicator_address		= 0x00009030,
	.pcie_local_base_address	= 0x00080000,
	.ce_wrap_intr_sum_host_msi_lsb	= 0x00000008,
	.ce_wrap_intr_sum_host_msi_mask	= 0x0000ff00,
	.pcie_intr_fw_mask		= 0x00000400,
	.pcie_intr_ce_mask_all		= 0x0007f800,
	.pcie_intr_clr_address		= 0x00000014,
};

const struct ath10k_hw_regs qca6174_regs = {
	.rtc_soc_base_address			= 0x00000800,
	.rtc_wmac_base_address			= 0x00001000,
	.soc_core_base_address			= 0x0003a000,
	.wlan_mac_base_address			= 0x00010000,
	.ce_wrapper_base_address		= 0x00034000,
	.ce0_base_address			= 0x00034400,
	.ce1_base_address			= 0x00034800,
	.ce2_base_address			= 0x00034c00,
	.ce3_base_address			= 0x00035000,
	.ce4_base_address			= 0x00035400,
	.ce5_base_address			= 0x00035800,
	.ce6_base_address			= 0x00035c00,
	.ce7_base_address			= 0x00036000,
	.soc_reset_control_si0_rst_mask		= 0x00000000,
	.soc_reset_control_ce_rst_mask		= 0x00000001,
	.soc_chip_id_address			= 0x000000f0,
	.scratch_3_address			= 0x00000028,
	.fw_indicator_address			= 0x0003a028,
	.pcie_local_base_address		= 0x00080000,
	.ce_wrap_intr_sum_host_msi_lsb		= 0x00000008,
	.ce_wrap_intr_sum_host_msi_mask		= 0x0000ff00,
	.pcie_intr_fw_mask			= 0x00000400,
	.pcie_intr_ce_mask_all			= 0x0007f800,
	.pcie_intr_clr_address			= 0x00000014,
	.cpu_pll_init_address			= 0x00404020,
	.cpu_speed_address			= 0x00404024,
	.core_clk_div_address			= 0x00404028,
};

const struct ath10k_hw_regs qca99x0_regs = {
	.rtc_soc_base_address			= 0x00080000,
	.rtc_wmac_base_address			= 0x00000000,
	.soc_core_base_address			= 0x00082000,
	.wlan_mac_base_address			= 0x00030000,
	.ce_wrapper_base_address		= 0x0004d000,
	.ce0_base_address			= 0x0004a000,
	.ce1_base_address			= 0x0004a400,
	.ce2_base_address			= 0x0004a800,
	.ce3_base_address			= 0x0004ac00,
	.ce4_base_address			= 0x0004b000,
	.ce5_base_address			= 0x0004b400,
	.ce6_base_address			= 0x0004b800,
	.ce7_base_address			= 0x0004bc00,
	/* Note: qca99x0 supports upto 12 Copy Engines. Other than address of
	 * CE0 and CE1 no other copy engine is directly referred in the code.
	 * It is not really necessary to assign address for newly supported
	 * CEs in this address table.
	 *	Copy Engine		Address
	 *	CE8			0x0004c000
	 *	CE9			0x0004c400
	 *	CE10			0x0004c800
	 *	CE11			0x0004cc00
	 */
	.soc_reset_control_si0_rst_mask		= 0x00000001,
	.soc_reset_control_ce_rst_mask		= 0x00000100,
	.soc_chip_id_address			= 0x000000ec,
	.scratch_3_address			= 0x00040050,
	.fw_indicator_address			= 0x00040050,
	.pcie_local_base_address		= 0x00000000,
	.ce_wrap_intr_sum_host_msi_lsb		= 0x0000000c,
	.ce_wrap_intr_sum_host_msi_mask		= 0x00fff000,
	.pcie_intr_fw_mask			= 0x00100000,
	.pcie_intr_ce_mask_all			= 0x000fff00,
	.pcie_intr_clr_address			= 0x00000010,
};

const struct ath10k_hw_regs qca4019_regs = {
	.rtc_soc_base_address                   = 0x00080000,
	.soc_core_base_address                  = 0x00082000,
	.wlan_mac_base_address                  = 0x00030000,
	.ce_wrapper_base_address                = 0x0004d000,
	.ce0_base_address                       = 0x0004a000,
	.ce1_base_address                       = 0x0004a400,
	.ce2_base_address                       = 0x0004a800,
	.ce3_base_address                       = 0x0004ac00,
	.ce4_base_address                       = 0x0004b000,
	.ce5_base_address                       = 0x0004b400,
	.ce6_base_address                       = 0x0004b800,
	.ce7_base_address                       = 0x0004bc00,
	/* qca4019 supports upto 12 copy engines. Since base address
	 * of ce8 to ce11 are not directly referred in the code,
	 * no need have them in separate members in this table.
	 *      Copy Engine             Address
	 *      CE8                     0x0004c000
	 *      CE9                     0x0004c400
	 *      CE10                    0x0004c800
	 *      CE11                    0x0004cc00
	 */
	.soc_reset_control_si0_rst_mask         = 0x00000001,
	.soc_reset_control_ce_rst_mask          = 0x00000100,
	.soc_chip_id_address                    = 0x000000ec,
	.fw_indicator_address                   = 0x0004f00c,
	.ce_wrap_intr_sum_host_msi_lsb          = 0x0000000c,
	.ce_wrap_intr_sum_host_msi_mask         = 0x00fff000,
	.pcie_intr_fw_mask                      = 0x00100000,
	.pcie_intr_ce_mask_all                  = 0x000fff00,
	.pcie_intr_clr_address                  = 0x00000010,
};

const struct ath10k_hw_values qca988x_values = {
	.rtc_state_val_on		= 3,
	.ce_count			= 8,
	.msi_assign_ce_max		= 7,
	.num_target_ce_config_wlan	= 7,
	.ce_desc_meta_data_mask		= 0xFFFC,
	.ce_desc_meta_data_lsb		= 2,
};

const struct ath10k_hw_values qca6174_values = {
	.rtc_state_val_on		= 3,
	.ce_count			= 8,
	.msi_assign_ce_max		= 7,
	.num_target_ce_config_wlan	= 7,
	.ce_desc_meta_data_mask		= 0xFFFC,
	.ce_desc_meta_data_lsb		= 2,
	.rfkill_pin			= 16,
	.rfkill_cfg			= 0,
	.rfkill_on_level		= 1,
};

const struct ath10k_hw_values qca99x0_values = {
	.rtc_state_val_on		= 7,
	.ce_count			= 12,
	.msi_assign_ce_max		= 12,
	.num_target_ce_config_wlan	= 10,
	.ce_desc_meta_data_mask		= 0xFFF0,
	.ce_desc_meta_data_lsb		= 4,
};

const struct ath10k_hw_values qca9888_values = {
	.rtc_state_val_on		= 3,
	.ce_count			= 12,
	.msi_assign_ce_max		= 12,
	.num_target_ce_config_wlan	= 10,
	.ce_desc_meta_data_mask		= 0xFFF0,
	.ce_desc_meta_data_lsb		= 4,
};

const struct ath10k_hw_values qca4019_values = {
	.ce_count                       = 12,
	.num_target_ce_config_wlan      = 10,
	.ce_desc_meta_data_mask         = 0xFFF0,
	.ce_desc_meta_data_lsb          = 4,
};

const struct ath10k_hw_regs wcn3990_regs = {
	.rtc_soc_base_address			= 0x00000000,
	.rtc_wmac_base_address			= 0x00000000,
	.soc_core_base_address			= 0x00000000,
	.ce_wrapper_base_address		= 0x0024C000,
	.ce0_base_address			= 0x00240000,
	.ce1_base_address			= 0x00241000,
	.ce2_base_address			= 0x00242000,
	.ce3_base_address			= 0x00243000,
	.ce4_base_address			= 0x00244000,
	.ce5_base_address			= 0x00245000,
	.ce6_base_address			= 0x00246000,
	.ce7_base_address			= 0x00247000,
	.ce8_base_address			= 0x00248000,
	.ce9_base_address			= 0x00249000,
	.ce10_base_address			= 0x0024A000,
	.ce11_base_address			= 0x0024B000,
	.soc_chip_id_address			= 0x000000f0,
	.soc_reset_control_si0_rst_mask		= 0x00000001,
	.soc_reset_control_ce_rst_mask		= 0x00000100,
	.ce_wrap_intr_sum_host_msi_lsb		= 0x0000000c,
	.ce_wrap_intr_sum_host_msi_mask		= 0x00fff000,
	.pcie_intr_fw_mask			= 0x00100000,
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_src_ring = {
	.msb	= 0x00000010,
	.lsb	= 0x00000010,
	.mask	= GENMASK(17, 17),
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_dst_ring = {
	.msb	= 0x00000012,
	.lsb	= 0x00000012,
	.mask	= GENMASK(18, 18),
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_dmax = {
	.msb	= 0x00000000,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_ctrl1 wcn3990_ctrl1 = {
	.addr		= 0x00000018,
	.src_ring	= &wcn3990_src_ring,
	.dst_ring	= &wcn3990_dst_ring,
	.dmax		= &wcn3990_dmax,
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_host_ie_cc = {
	.mask	= GENMASK(0, 0),
};

static struct ath10k_hw_ce_host_ie wcn3990_host_ie = {
	.copy_complete	= &wcn3990_host_ie_cc,
};

static struct ath10k_hw_ce_host_wm_regs wcn3990_wm_reg = {
	.dstr_lmask	= 0x00000010,
	.dstr_hmask	= 0x00000008,
	.srcr_lmask	= 0x00000004,
	.srcr_hmask	= 0x00000002,
	.cc_mask	= 0x00000001,
	.wm_mask	= 0x0000001E,
	.addr		= 0x00000030,
};

static struct ath10k_hw_ce_misc_regs wcn3990_misc_reg = {
	.axi_err	= 0x00000100,
	.dstr_add_err	= 0x00000200,
	.srcr_len_err	= 0x00000100,
	.dstr_mlen_vio	= 0x00000080,
	.dstr_overflow	= 0x00000040,
	.srcr_overflow	= 0x00000020,
	.err_mask	= 0x000003E0,
	.addr		= 0x00000038,
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_src_wm_low = {
	.msb	= 0x00000000,
	.lsb	= 0x00000010,
	.mask	= GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_src_wm_high = {
	.msb	= 0x0000000f,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs wcn3990_wm_src_ring = {
	.addr		= 0x0000004c,
	.low_rst	= 0x00000000,
	.high_rst	= 0x00000000,
	.wm_low		= &wcn3990_src_wm_low,
	.wm_high	= &wcn3990_src_wm_high,
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_dst_wm_low = {
	.lsb	= 0x00000010,
	.mask	= GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map wcn3990_dst_wm_high = {
	.msb	= 0x0000000f,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs wcn3990_wm_dst_ring = {
	.addr		= 0x00000050,
	.low_rst	= 0x00000000,
	.high_rst	= 0x00000000,
	.wm_low		= &wcn3990_dst_wm_low,
	.wm_high	= &wcn3990_dst_wm_high,
};

static struct ath10k_hw_ce_ctrl1_upd wcn3990_ctrl1_upd = {
	.shift = 19,
	.mask = 0x00080000,
	.enable = 0x00000000,
};

const struct ath10k_hw_ce_regs wcn3990_ce_regs = {
	.sr_base_addr_lo	= 0x00000000,
	.sr_base_addr_hi	= 0x00000004,
	.sr_size_addr		= 0x00000008,
	.dr_base_addr_lo	= 0x0000000c,
	.dr_base_addr_hi	= 0x00000010,
	.dr_size_addr		= 0x00000014,
	.misc_ie_addr		= 0x00000034,
	.sr_wr_index_addr	= 0x0000003c,
	.dst_wr_index_addr	= 0x00000040,
	.current_srri_addr	= 0x00000044,
	.current_drri_addr	= 0x00000048,
	.ce_rri_low		= 0x0024C004,
	.ce_rri_high		= 0x0024C008,
	.host_ie_addr		= 0x0000002c,
	.ctrl1_regs		= &wcn3990_ctrl1,
	.host_ie		= &wcn3990_host_ie,
	.wm_regs		= &wcn3990_wm_reg,
	.misc_regs		= &wcn3990_misc_reg,
	.wm_srcr		= &wcn3990_wm_src_ring,
	.wm_dstr		= &wcn3990_wm_dst_ring,
	.upd			= &wcn3990_ctrl1_upd,
};

const struct ath10k_hw_values wcn3990_values = {
	.rtc_state_val_on		= 5,
	.ce_count			= 12,
	.msi_assign_ce_max		= 12,
	.num_target_ce_config_wlan	= 12,
	.ce_desc_meta_data_mask		= 0xFFF0,
	.ce_desc_meta_data_lsb		= 4,
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_ring = {
	.msb	= 0x00000010,
	.lsb	= 0x00000010,
	.mask	= GENMASK(16, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_ring = {
	.msb	= 0x00000011,
	.lsb	= 0x00000011,
	.mask	= GENMASK(17, 17),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dmax = {
	.msb	= 0x0000000f,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_ctrl1 qcax_ctrl1 = {
	.addr		= 0x00000010,
	.hw_mask	= 0x0007ffff,
	.sw_mask	= 0x0007ffff,
	.hw_wr_mask	= 0x00000000,
	.sw_wr_mask	= 0x0007ffff,
	.reset_mask	= 0xffffffff,
	.reset		= 0x00000080,
	.src_ring	= &qcax_src_ring,
	.dst_ring	= &qcax_dst_ring,
	.dmax		= &qcax_dmax,
};

static struct ath10k_hw_ce_regs_addr_map qcax_cmd_halt_status = {
	.msb	= 0x00000003,
	.lsb	= 0x00000003,
	.mask	= GENMASK(3, 3),
};

static struct ath10k_hw_ce_cmd_halt qcax_cmd_halt = {
	.msb		= 0x00000000,
	.mask		= GENMASK(0, 0),
	.status_reset	= 0x00000000,
	.status		= &qcax_cmd_halt_status,
};

static struct ath10k_hw_ce_regs_addr_map qcax_host_ie_cc = {
	.msb	= 0x00000000,
	.lsb	= 0x00000000,
	.mask	= GENMASK(0, 0),
};

static struct ath10k_hw_ce_host_ie qcax_host_ie = {
	.copy_complete_reset	= 0x00000000,
	.copy_complete		= &qcax_host_ie_cc,
};

static struct ath10k_hw_ce_host_wm_regs qcax_wm_reg = {
	.dstr_lmask	= 0x00000010,
	.dstr_hmask	= 0x00000008,
	.srcr_lmask	= 0x00000004,
	.srcr_hmask	= 0x00000002,
	.cc_mask	= 0x00000001,
	.wm_mask	= 0x0000001E,
	.addr		= 0x00000030,
};

static struct ath10k_hw_ce_misc_regs qcax_misc_reg = {
	.axi_err	= 0x00000400,
	.dstr_add_err	= 0x00000200,
	.srcr_len_err	= 0x00000100,
	.dstr_mlen_vio	= 0x00000080,
	.dstr_overflow	= 0x00000040,
	.srcr_overflow	= 0x00000020,
	.err_mask	= 0x000007E0,
	.addr		= 0x00000038,
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_wm_low = {
	.msb    = 0x0000001f,
	.lsb	= 0x00000010,
	.mask	= GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_wm_high = {
	.msb	= 0x0000000f,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs qcax_wm_src_ring = {
	.addr		= 0x0000004c,
	.low_rst	= 0x00000000,
	.high_rst	= 0x00000000,
	.wm_low		= &qcax_src_wm_low,
	.wm_high        = &qcax_src_wm_high,
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_wm_low = {
	.lsb	= 0x00000010,
	.mask	= GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_wm_high = {
	.msb	= 0x0000000f,
	.lsb	= 0x00000000,
	.mask	= GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs qcax_wm_dst_ring = {
	.addr		= 0x00000050,
	.low_rst	= 0x00000000,
	.high_rst	= 0x00000000,
	.wm_low		= &qcax_dst_wm_low,
	.wm_high	= &qcax_dst_wm_high,
};

const struct ath10k_hw_ce_regs qcax_ce_regs = {
	.sr_base_addr_lo	= 0x00000000,
	.sr_size_addr		= 0x00000004,
	.dr_base_addr_lo	= 0x00000008,
	.dr_size_addr		= 0x0000000c,
	.ce_cmd_addr		= 0x00000018,
	.misc_ie_addr		= 0x00000034,
	.sr_wr_index_addr	= 0x0000003c,
	.dst_wr_index_addr	= 0x00000040,
	.current_srri_addr	= 0x00000044,
	.current_drri_addr	= 0x00000048,
	.host_ie_addr		= 0x0000002c,
	.ctrl1_regs		= &qcax_ctrl1,
	.cmd_halt		= &qcax_cmd_halt,
	.host_ie		= &qcax_host_ie,
	.wm_regs		= &qcax_wm_reg,
	.misc_regs		= &qcax_misc_reg,
	.wm_srcr		= &qcax_wm_src_ring,
	.wm_dstr                = &qcax_wm_dst_ring,
};

const struct ath10k_hw_clk_params qca6174_clk[ATH10K_HW_REFCLK_COUNT] = {
	{
		.refclk = 48000000,
		.div = 0xe,
		.rnfrac = 0x2aaa8,
		.settle_time = 2400,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 19200000,
		.div = 0x24,
		.rnfrac = 0x2aaa8,
		.settle_time = 960,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 24000000,
		.div = 0x1d,
		.rnfrac = 0x15551,
		.settle_time = 1200,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 26000000,
		.div = 0x1b,
		.rnfrac = 0x4ec4,
		.settle_time = 1300,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 37400000,
		.div = 0x12,
		.rnfrac = 0x34b49,
		.settle_time = 1870,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 38400000,
		.div = 0x12,
		.rnfrac = 0x15551,
		.settle_time = 1920,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 40000000,
		.div = 0x12,
		.rnfrac = 0x26665,
		.settle_time = 2000,
		.refdiv = 0,
		.outdiv = 1,
	},
	{
		.refclk = 52000000,
		.div = 0x1b,
		.rnfrac = 0x4ec4,
		.settle_time = 2600,
		.refdiv = 0,
		.outdiv = 1,
	},
};

void ath10k_hw_fill_survey_time(struct ath10k *ar, struct survey_info *survey,
				u32 cc, u32 rcc, u32 cc_prev, u32 rcc_prev)
{
	u32 cc_fix = 0;
	u32 rcc_fix = 0;
	enum ath10k_hw_cc_wraparound_type wraparound_type;

	survey->filled |= SURVEY_INFO_TIME |
			  SURVEY_INFO_TIME_BUSY;

	wraparound_type = ar->hw_params.cc_wraparound_type;

	if (cc < cc_prev || rcc < rcc_prev) {
		switch (wraparound_type) {
		case ATH10K_HW_CC_WRAP_SHIFTED_ALL:
			if (cc < cc_prev) {
				cc_fix = 0x7fffffff;
				survey->filled &= ~SURVEY_INFO_TIME_BUSY;
			}
			break;
		case ATH10K_HW_CC_WRAP_SHIFTED_EACH:
			if (cc < cc_prev)
				cc_fix = 0x7fffffff;

			if (rcc < rcc_prev)
				rcc_fix = 0x7fffffff;
			break;
		case ATH10K_HW_CC_WRAP_DISABLED:
			break;
		}
	}

	cc -= cc_prev - cc_fix;
	rcc -= rcc_prev - rcc_fix;

	survey->time = CCNT_TO_MSEC(ar, cc);
	survey->time_busy = CCNT_TO_MSEC(ar, rcc);
}

/* The firmware does not support setting the coverage class. Instead this
 * function monitors and modifies the corresponding MAC registers.
 */
static void ath10k_hw_qca988x_set_coverage_class(struct ath10k *ar,
						 s16 value)
{
	u32 slottime_reg;
	u32 slottime;
	u32 timeout_reg;
	u32 ack_timeout;
	u32 cts_timeout;
	u32 phyclk_reg;
	u32 phyclk;
	u64 fw_dbglog_mask;
	u32 fw_dbglog_level;

	mutex_lock(&ar->conf_mutex);

	/* Only modify registers if the core is started. */
	if ((ar->state != ATH10K_STATE_ON) &&
	    (ar->state != ATH10K_STATE_RESTARTED)) {
		spin_lock_bh(&ar->data_lock);
		/* Store config value for when radio boots up */
		ar->fw_coverage.coverage_class = value;
		spin_unlock_bh(&ar->data_lock);
		goto unlock;
	}

	/* Retrieve the current values of the two registers that need to be
	 * adjusted.
	 */
	slottime_reg = ath10k_hif_read32(ar, WLAN_MAC_BASE_ADDRESS +
					     WAVE1_PCU_GBL_IFS_SLOT);
	timeout_reg = ath10k_hif_read32(ar, WLAN_MAC_BASE_ADDRESS +
					    WAVE1_PCU_ACK_CTS_TIMEOUT);
	phyclk_reg = ath10k_hif_read32(ar, WLAN_MAC_BASE_ADDRESS +
					   WAVE1_PHYCLK);
	phyclk = MS(phyclk_reg, WAVE1_PHYCLK_USEC) + 1;

	if (value < 0)
		value = ar->fw_coverage.coverage_class;

	/* Break out if the coverage class and registers have the expected
	 * value.
	 */
	if (value == ar->fw_coverage.coverage_class &&
	    slottime_reg == ar->fw_coverage.reg_slottime_conf &&
	    timeout_reg == ar->fw_coverage.reg_ack_cts_timeout_conf &&
	    phyclk_reg == ar->fw_coverage.reg_phyclk)
		goto unlock;

	/* Store new initial register values from the firmware. */
	if (slottime_reg != ar->fw_coverage.reg_slottime_conf)
		ar->fw_coverage.reg_slottime_orig = slottime_reg;
	if (timeout_reg != ar->fw_coverage.reg_ack_cts_timeout_conf)
		ar->fw_coverage.reg_ack_cts_timeout_orig = timeout_reg;
	ar->fw_coverage.reg_phyclk = phyclk_reg;

	/* Calculate new value based on the (original) firmware calculation. */
	slottime_reg = ar->fw_coverage.reg_slottime_orig;
	timeout_reg = ar->fw_coverage.reg_ack_cts_timeout_orig;

	/* Do some sanity checks on the slottime register. */
	if (slottime_reg % phyclk) {
		ath10k_warn(ar,
			    "failed to set coverage class: expected integer microsecond value in register\n");

		goto store_regs;
	}

	slottime = MS(slottime_reg, WAVE1_PCU_GBL_IFS_SLOT);
	slottime = slottime / phyclk;
	if (slottime != 9 && slottime != 20) {
		ath10k_warn(ar,
			    "failed to set coverage class: expected slot time of 9 or 20us in HW register. It is %uus.\n",
			    slottime);

		goto store_regs;
	}

	/* Recalculate the register values by adding the additional propagation
	 * delay (3us per coverage class).
	 */

	slottime = MS(slottime_reg, WAVE1_PCU_GBL_IFS_SLOT);
	slottime += value * 3 * phyclk;
	slottime = min_t(u32, slottime, WAVE1_PCU_GBL_IFS_SLOT_MAX);
	slottime = SM(slottime, WAVE1_PCU_GBL_IFS_SLOT);
	slottime_reg = (slottime_reg & ~WAVE1_PCU_GBL_IFS_SLOT_MASK) | slottime;

	/* Update ack timeout (lower halfword). */
	ack_timeout = MS(timeout_reg, WAVE1_PCU_ACK_CTS_TIMEOUT_ACK);
	ack_timeout += 3 * value * phyclk;
	ack_timeout = min_t(u32, ack_timeout, WAVE1_PCU_ACK_CTS_TIMEOUT_MAX);
	ack_timeout = SM(ack_timeout, WAVE1_PCU_ACK_CTS_TIMEOUT_ACK);

	/* Update cts timeout (upper halfword). */
	cts_timeout = MS(timeout_reg, WAVE1_PCU_ACK_CTS_TIMEOUT_CTS);
	cts_timeout += 3 * value * phyclk;
	cts_timeout = min_t(u32, cts_timeout, WAVE1_PCU_ACK_CTS_TIMEOUT_MAX);
	cts_timeout = SM(cts_timeout, WAVE1_PCU_ACK_CTS_TIMEOUT_CTS);

	timeout_reg = ack_timeout | cts_timeout;

	ath10k_hif_write32(ar,
			   WLAN_MAC_BASE_ADDRESS + WAVE1_PCU_GBL_IFS_SLOT,
			   slottime_reg);
	ath10k_hif_write32(ar,
			   WLAN_MAC_BASE_ADDRESS + WAVE1_PCU_ACK_CTS_TIMEOUT,
			   timeout_reg);

	/* Ensure we have a debug level of WARN set for the case that the
	 * coverage class is larger than 0. This is important as we need to
	 * set the registers again if the firmware does an internal reset and
	 * this way we will be notified of the event.
	 */
	fw_dbglog_mask = ath10k_debug_get_fw_dbglog_mask(ar);
	fw_dbglog_level = ath10k_debug_get_fw_dbglog_level(ar);

	if (value > 0) {
		if (fw_dbglog_level > ATH10K_DBGLOG_LEVEL_WARN)
			fw_dbglog_level = ATH10K_DBGLOG_LEVEL_WARN;
		fw_dbglog_mask = ~0;
	}

	ath10k_wmi_dbglog_cfg(ar, fw_dbglog_mask, fw_dbglog_level);

store_regs:
	/* After an error we will not retry setting the coverage class. */
	spin_lock_bh(&ar->data_lock);
	ar->fw_coverage.coverage_class = value;
	spin_unlock_bh(&ar->data_lock);

	ar->fw_coverage.reg_slottime_conf = slottime_reg;
	ar->fw_coverage.reg_ack_cts_timeout_conf = timeout_reg;

unlock:
	mutex_unlock(&ar->conf_mutex);
}

/**
 * ath10k_hw_qca6174_enable_pll_clock() - enable the qca6174 hw pll clock
 * @ar: the ath10k blob
 *
 * This function is very hardware specific, the clock initialization
 * steps is very sensitive and could lead to unknown crash, so they
 * should be done in sequence.
 *
 * *** Be aware if you planned to refactor them. ***
 *
 * Return: 0 if successfully enable the pll, otherwise EINVAL
 */
static int ath10k_hw_qca6174_enable_pll_clock(struct ath10k *ar)
{
	int ret, wait_limit;
	u32 clk_div_addr, pll_init_addr, speed_addr;
	u32 addr, reg_val, mem_val;
	struct ath10k_hw_params *hw;
	const struct ath10k_hw_clk_params *hw_clk;

	hw = &ar->hw_params;

	if (ar->regs->core_clk_div_address == 0 ||
	    ar->regs->cpu_pll_init_address == 0 ||
	    ar->regs->cpu_speed_address == 0)
		return -EINVAL;

	clk_div_addr = ar->regs->core_clk_div_address;
	pll_init_addr = ar->regs->cpu_pll_init_address;
	speed_addr = ar->regs->cpu_speed_address;

	/* Read efuse register to find out the right hw clock configuration */
	addr = (RTC_SOC_BASE_ADDRESS | EFUSE_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	/* sanitize if the hw refclk index is out of the boundary */
	if (MS(reg_val, EFUSE_XTAL_SEL) > ATH10K_HW_REFCLK_COUNT)
		return -EINVAL;

	hw_clk = &hw->hw_clk[MS(reg_val, EFUSE_XTAL_SEL)];

	/* Set the rnfrac and outdiv params to bb_pll register */
	addr = (RTC_SOC_BASE_ADDRESS | BB_PLL_CONFIG_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~(BB_PLL_CONFIG_FRAC_MASK | BB_PLL_CONFIG_OUTDIV_MASK);
	reg_val |= (SM(hw_clk->rnfrac, BB_PLL_CONFIG_FRAC) |
		    SM(hw_clk->outdiv, BB_PLL_CONFIG_OUTDIV));
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* Set the correct settle time value to pll_settle register */
	addr = (RTC_WMAC_BASE_ADDRESS | WLAN_PLL_SETTLE_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~WLAN_PLL_SETTLE_TIME_MASK;
	reg_val |= SM(hw_clk->settle_time, WLAN_PLL_SETTLE_TIME);
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* Set the clock_ctrl div to core_clk_ctrl register */
	addr = (RTC_SOC_BASE_ADDRESS | SOC_CORE_CLK_CTRL_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~SOC_CORE_CLK_CTRL_DIV_MASK;
	reg_val |= SM(1, SOC_CORE_CLK_CTRL_DIV);
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* Set the clock_div register */
	mem_val = 1;
	ret = ath10k_bmi_write_memory(ar, clk_div_addr, &mem_val,
				      sizeof(mem_val));
	if (ret)
		return -EINVAL;

	/* Configure the pll_control register */
	addr = (RTC_WMAC_BASE_ADDRESS | WLAN_PLL_CONTROL_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val |= (SM(hw_clk->refdiv, WLAN_PLL_CONTROL_REFDIV) |
		    SM(hw_clk->div, WLAN_PLL_CONTROL_DIV) |
		    SM(1, WLAN_PLL_CONTROL_NOPWD));
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* busy wait (max 1s) the rtc_sync status register indicate ready */
	wait_limit = 100000;
	addr = (RTC_WMAC_BASE_ADDRESS | RTC_SYNC_STATUS_OFFSET);
	do {
		ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
		if (ret)
			return -EINVAL;

		if (!MS(reg_val, RTC_SYNC_STATUS_PLL_CHANGING))
			break;

		wait_limit--;
		udelay(10);

	} while (wait_limit > 0);

	if (MS(reg_val, RTC_SYNC_STATUS_PLL_CHANGING))
		return -EINVAL;

	/* Unset the pll_bypass in pll_control register */
	addr = (RTC_WMAC_BASE_ADDRESS | WLAN_PLL_CONTROL_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~WLAN_PLL_CONTROL_BYPASS_MASK;
	reg_val |= SM(0, WLAN_PLL_CONTROL_BYPASS);
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* busy wait (max 1s) the rtc_sync status register indicate ready */
	wait_limit = 100000;
	addr = (RTC_WMAC_BASE_ADDRESS | RTC_SYNC_STATUS_OFFSET);
	do {
		ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
		if (ret)
			return -EINVAL;

		if (!MS(reg_val, RTC_SYNC_STATUS_PLL_CHANGING))
			break;

		wait_limit--;
		udelay(10);

	} while (wait_limit > 0);

	if (MS(reg_val, RTC_SYNC_STATUS_PLL_CHANGING))
		return -EINVAL;

	/* Enable the hardware cpu clock register */
	addr = (RTC_SOC_BASE_ADDRESS | SOC_CPU_CLOCK_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~SOC_CPU_CLOCK_STANDARD_MASK;
	reg_val |= SM(1, SOC_CPU_CLOCK_STANDARD);
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* unset the nopwd from pll_control register */
	addr = (RTC_WMAC_BASE_ADDRESS | WLAN_PLL_CONTROL_OFFSET);
	ret = ath10k_bmi_read_soc_reg(ar, addr, &reg_val);
	if (ret)
		return -EINVAL;

	reg_val &= ~WLAN_PLL_CONTROL_NOPWD_MASK;
	ret = ath10k_bmi_write_soc_reg(ar, addr, reg_val);
	if (ret)
		return -EINVAL;

	/* enable the pll_init register */
	mem_val = 1;
	ret = ath10k_bmi_write_memory(ar, pll_init_addr, &mem_val,
				      sizeof(mem_val));
	if (ret)
		return -EINVAL;

	/* set the target clock frequency to speed register */
	ret = ath10k_bmi_write_memory(ar, speed_addr, &hw->target_cpu_freq,
				      sizeof(hw->target_cpu_freq));
	if (ret)
		return -EINVAL;

	return 0;
}

/* Program CPU_ADDR_MSB to allow different memory
 * region access.
 */
static void ath10k_hw_map_target_mem(struct ath10k *ar, u32 msb)
{
	u32 address = SOC_CORE_BASE_ADDRESS + FW_RAM_CONFIG_ADDRESS;

	ath10k_hif_write32(ar, address, msb);
}

/* 1. Write to memory region of target, such as IRAM adn DRAM.
 * 2. Target address( 0 ~ 00100000 & 0x00400000~0x00500000)
 *    can be written directly. See ath10k_pci_targ_cpu_to_ce_addr() too.
 * 3. In order to access the region other than the above,
 *    we need to set the value of register CPU_ADDR_MSB.
 * 4. Target memory access space is limited to 1M size. If the size is larger
 *    than 1M, need to split it and program CPU_ADDR_MSB accordingly.
 */
static int ath10k_hw_diag_segment_msb_download(struct ath10k *ar,
					       const void *buffer,
					       u32 address,
					       u32 length)
{
	u32 addr = address & REGION_ACCESS_SIZE_MASK;
	int ret, remain_size, size;
	const u8 *buf;

	ath10k_hw_map_target_mem(ar, CPU_ADDR_MSB_REGION_VAL(address));

	if (addr + length > REGION_ACCESS_SIZE_LIMIT) {
		size = REGION_ACCESS_SIZE_LIMIT - addr;
		remain_size = length - size;

		ret = ath10k_hif_diag_write(ar, address, buffer, size);
		if (ret) {
			ath10k_warn(ar,
				    "failed to download the first %d bytes segment to address:0x%x: %d\n",
				    size, address, ret);
			goto done;
		}

		/* Change msb to the next memory region*/
		ath10k_hw_map_target_mem(ar,
					 CPU_ADDR_MSB_REGION_VAL(address) + 1);
		buf = buffer +  size;
		ret = ath10k_hif_diag_write(ar,
					    address & ~REGION_ACCESS_SIZE_MASK,
					    buf, remain_size);
		if (ret) {
			ath10k_warn(ar,
				    "failed to download the second %d bytes segment to address:0x%x: %d\n",
				    remain_size,
				    address & ~REGION_ACCESS_SIZE_MASK,
				    ret);
			goto done;
		}
	} else {
		ret = ath10k_hif_diag_write(ar, address, buffer, length);
		if (ret) {
			ath10k_warn(ar,
				    "failed to download the only %d bytes segment to address:0x%x: %d\n",
				    length, address, ret);
			goto done;
		}
	}

done:
	/* Change msb to DRAM */
	ath10k_hw_map_target_mem(ar,
				 CPU_ADDR_MSB_REGION_VAL(DRAM_BASE_ADDRESS));
	return ret;
}

static int ath10k_hw_diag_segment_download(struct ath10k *ar,
					   const void *buffer,
					   u32 address,
					   u32 length)
{
	if (address >= DRAM_BASE_ADDRESS + REGION_ACCESS_SIZE_LIMIT)
		/* Needs to change MSB for memory write */
		return ath10k_hw_diag_segment_msb_download(ar, buffer,
							   address, length);
	else
		return ath10k_hif_diag_write(ar, address, buffer, length);
}

int ath10k_hw_diag_fast_download(struct ath10k *ar,
				 u32 address,
				 const void *buffer,
				 u32 length)
{
	const u8 *buf = buffer;
	bool sgmt_end = false;
	u32 base_addr = 0;
	u32 base_len = 0;
	u32 left = 0;
	struct bmi_segmented_file_header *hdr;
	struct bmi_segmented_metadata *metadata;
	int ret = 0;

	if (length < sizeof(*hdr))
		return -EINVAL;

	/* check firmware header. If it has no correct magic number
	 * or it's compressed, returns error.
	 */
	hdr = (struct bmi_segmented_file_header *)buf;
	if (__le32_to_cpu(hdr->magic_num) != BMI_SGMTFILE_MAGIC_NUM) {
		ath10k_dbg(ar, ATH10K_DBG_BOOT,
			   "Not a supported firmware, magic_num:0x%x\n",
			   hdr->magic_num);
		return -EINVAL;
	}

	if (hdr->file_flags != 0) {
		ath10k_dbg(ar, ATH10K_DBG_BOOT,
			   "Not a supported firmware, file_flags:0x%x\n",
			   hdr->file_flags);
		return -EINVAL;
	}

	metadata = (struct bmi_segmented_metadata *)hdr->data;
	left = length - sizeof(*hdr);

	while (left > 0) {
		if (left < sizeof(*metadata)) {
			ath10k_warn(ar, "firmware segment is truncated: %d\n",
				    left);
			ret = -EINVAL;
			break;
		}
		base_addr = __le32_to_cpu(metadata->addr);
		base_len = __le32_to_cpu(metadata->length);
		buf = metadata->data;
		left -= sizeof(*metadata);

		switch (base_len) {
		case BMI_SGMTFILE_BEGINADDR:
			/* base_addr is the start address to run */
			ret = ath10k_bmi_set_start(ar, base_addr);
			base_len = 0;
			break;
		case BMI_SGMTFILE_DONE:
			/* no more segment */
			base_len = 0;
			sgmt_end = true;
			ret = 0;
			break;
		case BMI_SGMTFILE_BDDATA:
		case BMI_SGMTFILE_EXEC:
			ath10k_warn(ar,
				    "firmware has unsupported segment:%d\n",
				    base_len);
			ret = -EINVAL;
			break;
		default:
			if (base_len > left) {
				/* sanity check */
				ath10k_warn(ar,
					    "firmware has invalid segment length, %d > %d\n",
					    base_len, left);
				ret = -EINVAL;
				break;
			}

			ret = ath10k_hw_diag_segment_download(ar,
							      buf,
							      base_addr,
							      base_len);

			if (ret)
				ath10k_warn(ar,
					    "failed to download firmware via diag interface:%d\n",
					    ret);
			break;
		}

		if (ret || sgmt_end)
			break;

		metadata = (struct bmi_segmented_metadata *)(buf + base_len);
		left -= base_len;
	}

	if (ret == 0)
		ath10k_dbg(ar, ATH10K_DBG_BOOT,
			   "boot firmware fast diag download successfully.\n");
	return ret;
}

static int ath10k_htt_tx_rssi_enable(struct htt_resp *resp)
{
	return (resp->data_tx_completion.flags2 & HTT_TX_CMPL_FLAG_DATA_RSSI);
}

static int ath10k_htt_tx_rssi_enable_wcn3990(struct htt_resp *resp)
{
	return (resp->data_tx_completion.flags2 &
		HTT_TX_DATA_RSSI_ENABLE_WCN3990);
}

static int ath10k_get_htt_tx_data_rssi_pad(struct htt_resp *resp)
{
	struct htt_data_tx_completion_ext extd;
	int pad_bytes = 0;

	if (resp->data_tx_completion.flags2 & HTT_TX_DATA_APPEND_RETRIES)
		pad_bytes += sizeof(extd.a_retries) /
			     sizeof(extd.msdus_rssi[0]);

	if (resp->data_tx_completion.flags2 & HTT_TX_DATA_APPEND_TIMESTAMP)
		pad_bytes += sizeof(extd.t_stamp) / sizeof(extd.msdus_rssi[0]);

	return pad_bytes;
}

const struct ath10k_hw_ops qca988x_ops = {
	.set_coverage_class = ath10k_hw_qca988x_set_coverage_class,
	.is_rssi_enable = ath10k_htt_tx_rssi_enable,
};

const struct ath10k_hw_ops qca99x0_ops = {
	.is_rssi_enable = ath10k_htt_tx_rssi_enable,
};

const struct ath10k_hw_ops qca6174_ops = {
	.set_coverage_class = ath10k_hw_qca988x_set_coverage_class,
	.enable_pll_clk = ath10k_hw_qca6174_enable_pll_clock,
	.is_rssi_enable = ath10k_htt_tx_rssi_enable,
};

const struct ath10k_hw_ops qca6174_sdio_ops = {
	.enable_pll_clk = ath10k_hw_qca6174_enable_pll_clock,
};

const struct ath10k_hw_ops wcn3990_ops = {
	.tx_data_rssi_pad_bytes = ath10k_get_htt_tx_data_rssi_pad,
	.is_rssi_enable = ath10k_htt_tx_rssi_enable_wcn3990,
};
