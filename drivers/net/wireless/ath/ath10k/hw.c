/*
 * Copyright (c) 2014-2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/types.h>
#include "core.h"
#include "hw.h"

const struct ath10k_hw_regs qca988x_regs = {
	.rtc_state_cold_reset_mask	= 0x00000400,
	.rtc_soc_base_address		= 0x00004000,
	.rtc_wmac_base_address		= 0x00005000,
	.soc_core_base_address		= 0x00009000,
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
	.rtc_state_cold_reset_mask		= 0x00002000,
	.rtc_soc_base_address			= 0x00000800,
	.rtc_wmac_base_address			= 0x00001000,
	.soc_core_base_address			= 0x0003a000,
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
};

const struct ath10k_hw_regs qca99x0_regs = {
	.rtc_state_cold_reset_mask		= 0x00000400,
	.rtc_soc_base_address			= 0x00080000,
	.rtc_wmac_base_address			= 0x00000000,
	.soc_core_base_address			= 0x00082000,
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
	 * It is not really neccessary to assign address for newly supported
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
};

const struct ath10k_hw_values qca99x0_values = {
	.rtc_state_val_on		= 5,
	.ce_count			= 12,
	.msi_assign_ce_max		= 12,
	.num_target_ce_config_wlan	= 10,
	.ce_desc_meta_data_mask		= 0xFFF0,
	.ce_desc_meta_data_lsb		= 4,
};

void ath10k_hw_fill_survey_time(struct ath10k *ar, struct survey_info *survey,
				u32 cc, u32 rcc, u32 cc_prev, u32 rcc_prev)
{
	u32 cc_fix = 0;

	survey->filled |= SURVEY_INFO_TIME |
			  SURVEY_INFO_TIME_BUSY;

	if (ar->hw_params.has_shifted_cc_wraparound && cc < cc_prev) {
		cc_fix = 0x7fffffff;
		survey->filled &= ~SURVEY_INFO_TIME_BUSY;
	}

	cc -= cc_prev - cc_fix;
	rcc -= rcc_prev;

	survey->time = CCNT_TO_MSEC(cc);
	survey->time_busy = CCNT_TO_MSEC(rcc);
}
