// SPDX-License-Identifier: GPL-2.0
/*
 * camss-csiphy-3ph-1-0.c
 *
 * Qualcomm MSM Camera Subsystem - CSIPHY Module 3phase v1.0
 *
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2016-2018 Linaro Ltd.
 */

#include "camss.h"
#include "camss-csiphy.h"

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define CSIPHY_3PH_LNn_CFG1(n)			(0x000 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG	(BIT(7) | BIT(6))
#define CSIPHY_3PH_LNn_CFG2(n)			(0x004 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG2_LP_REC_EN_INT	BIT(3)
#define CSIPHY_3PH_LNn_CFG3(n)			(0x008 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG4(n)			(0x00c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS	0xa4
#define CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS_660	0xa5
#define CSIPHY_3PH_LNn_CFG5(n)			(0x010 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG5_T_HS_DTERM		0x02
#define CSIPHY_3PH_LNn_CFG5_HS_REC_EQ_FQ_INT	0x50
#define CSIPHY_3PH_LNn_TEST_IMP(n)		(0x01c + 0x100 * (n))
#define CSIPHY_3PH_LNn_TEST_IMP_HS_TERM_IMP	0xa
#define CSIPHY_3PH_LNn_MISC1(n)			(0x028 + 0x100 * (n))
#define CSIPHY_3PH_LNn_MISC1_IS_CLKLANE		BIT(2)
#define CSIPHY_3PH_LNn_CFG6(n)			(0x02c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG6_SWI_FORCE_INIT_EXIT	BIT(0)
#define CSIPHY_3PH_LNn_CFG7(n)			(0x030 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG7_SWI_T_INIT		0x2
#define CSIPHY_3PH_LNn_CFG8(n)			(0x034 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG8_SWI_SKIP_WAKEUP	BIT(0)
#define CSIPHY_3PH_LNn_CFG8_SKEW_FILTER_ENABLE	BIT(1)
#define CSIPHY_3PH_LNn_CFG9(n)			(0x038 + 0x100 * (n))
#define CSIPHY_3PH_LNn_CFG9_SWI_T_WAKEUP	0x1
#define CSIPHY_3PH_LNn_CSI_LANE_CTRL15(n)	(0x03c + 0x100 * (n))
#define CSIPHY_3PH_LNn_CSI_LANE_CTRL15_SWI_SOT_SYMBOL	0xb8

#define CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(n)	(0x800 + 0x4 * (n))
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL5_CLK_ENABLE	BIT(7)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B	BIT(0)
#define CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID	BIT(1)
#define CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(n)	(0x8b0 + 0x4 * (n))

#define CSIPHY_DEFAULT_PARAMS            0
#define CSIPHY_LANE_ENABLE               1
#define CSIPHY_SETTLE_CNT_LOWER_BYTE     2
#define CSIPHY_SETTLE_CNT_HIGHER_BYTE    3
#define CSIPHY_DNP_PARAMS                4
#define CSIPHY_2PH_REGS                  5
#define CSIPHY_3PH_REGS                  6

struct csiphy_reg_t {
	s32 reg_addr;
	s32 reg_data;
	s32 delay;
	u32 csiphy_param_type;
};

/* GEN2 1.0 2PH */
static const struct
csiphy_reg_t lane_regs_sdm845[5][14] = {
	{
		{0x0004, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x002C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0034, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x001C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0014, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0028, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x003C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0000, 0x91, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0008, 0x00, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x000c, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0010, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0038, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0060, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0064, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0704, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x072C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0734, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x071C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0714, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0728, 0x04, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x073C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0700, 0x80, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0708, 0x14, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x070C, 0xA5, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0710, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0738, 0x1F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0760, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0764, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0204, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x022C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0234, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x021C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0214, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0228, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x023C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0200, 0x91, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0208, 0x00, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x020C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0210, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0238, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0260, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0264, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0404, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x042C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0434, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x041C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0414, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0428, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x043C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0400, 0x91, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0408, 0x00, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x040C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0410, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0438, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0460, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0464, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0604, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x062C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0634, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x061C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0614, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0628, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x063C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0600, 0x91, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0608, 0x00, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x060C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0610, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0638, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0660, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0664, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
};

/* GEN2 1.1 2PH */
static const struct
csiphy_reg_t lane_regs_sc8280xp[5][14] = {
	{
		{0x0004, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x002C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0034, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x001C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0014, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0028, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x003C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0000, 0x90, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0008, 0x0E, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x000C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0010, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0038, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0060, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0064, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0704, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x072C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0734, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x071C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0714, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0728, 0x04, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x073C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0700, 0x80, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0708, 0x0E, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x070C, 0xA5, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0710, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0738, 0x1F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0760, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0764, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0204, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x022C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0234, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x021C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0214, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0228, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x023C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0200, 0x90, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0208, 0x0E, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x020C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0210, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0238, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0260, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0264, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0404, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x042C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0434, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x041C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0414, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0428, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x043C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0400, 0x90, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0408, 0x0E, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x040C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0410, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0438, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0460, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0464, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0604, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x062C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0634, 0x0F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x061C, 0x0A, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0614, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0628, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x063C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0600, 0x90, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0608, 0x0E, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x060C, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0610, 0x52, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0638, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0660, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0664, 0x7F, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
};

/* GEN2 1.2.1 2PH */
static const struct
csiphy_reg_t lane_regs_sm8250[5][20] = {
	{
		{0x0030, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0900, 0x05, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0908, 0x10, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0904, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0904, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0004, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x002C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0034, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0010, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x001C, 0x08, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x003C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0008, 0x10, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x0000, 0x8D, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x000c, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0038, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0014, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0028, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0024, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0800, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0884, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0730, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C80, 0x05, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C88, 0x10, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C84, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C84, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0704, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x072C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0734, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0710, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x071C, 0x08, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x073C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0708, 0x10, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x0700, 0x80, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x070c, 0xA5, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0738, 0x1F, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0714, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0728, 0x04, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0724, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0800, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0884, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0230, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0A00, 0x05, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0A08, 0x10, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0A04, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0A04, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0204, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x022C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0234, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0210, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x021C, 0x08, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x023C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0208, 0x10, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x0200, 0x8D, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x020c, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0238, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0214, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0228, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0224, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0800, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0884, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0430, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0B00, 0x05, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0B08, 0x10, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0B04, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0B04, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0404, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x042C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0434, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0410, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x041C, 0x08, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x043C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0408, 0x10, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x0400, 0x8D, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x040c, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0438, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0414, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0428, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0424, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0800, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0884, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
	{
		{0x0630, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C00, 0x05, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C08, 0x10, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C04, 0x00, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0C04, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0604, 0x0C, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x062C, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0634, 0x07, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0610, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x061C, 0x08, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x063C, 0xB8, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0608, 0x10, 0x00, CSIPHY_SETTLE_CNT_LOWER_BYTE},
		{0x0600, 0x8D, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x060c, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0638, 0xFE, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0614, 0x60, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0628, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0624, 0x00, 0x00, CSIPHY_DNP_PARAMS},
		{0x0800, 0x02, 0x00, CSIPHY_DEFAULT_PARAMS},
		{0x0884, 0x01, 0x00, CSIPHY_DEFAULT_PARAMS},
	},
};

static void csiphy_hw_version_read(struct csiphy_device *csiphy,
				   struct device *dev)
{
	u32 hw_version;

	writel(CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_SHOW_REV_ID,
	       csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));

	hw_version = readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(12));
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(13)) << 8;
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(14)) << 16;
	hw_version |= readl_relaxed(csiphy->base +
				   CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(15)) << 24;

	dev_dbg(dev, "CSIPHY 3PH HW Version = 0x%08x\n", hw_version);
}

/*
 * csiphy_reset - Perform software reset on CSIPHY module
 * @csiphy: CSIPHY device
 */
static void csiphy_reset(struct csiphy_device *csiphy)
{
	writel_relaxed(0x1, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(0));
	usleep_range(5000, 8000);
	writel_relaxed(0x0, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(0));
}

static irqreturn_t csiphy_isr(int irq, void *dev)
{
	struct csiphy_device *csiphy = dev;
	int i;

	for (i = 0; i < 11; i++) {
		int c = i + 22;
		u8 val = readl_relaxed(csiphy->base +
				       CSIPHY_3PH_CMN_CSI_COMMON_STATUSn(i));

		writel_relaxed(val, csiphy->base +
				    CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(c));
	}

	writel_relaxed(0x1, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(10));
	writel_relaxed(0x0, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(10));

	for (i = 22; i < 33; i++)
		writel_relaxed(0x0, csiphy->base +
				    CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(i));

	return IRQ_HANDLED;
}

/*
 * csiphy_settle_cnt_calc - Calculate settle count value
 *
 * Helper function to calculate settle count value. This is
 * based on the CSI2 T_hs_settle parameter which in turn
 * is calculated based on the CSI2 transmitter link frequency.
 *
 * Return settle count value or 0 if the CSI2 link frequency
 * is not available
 */
static u8 csiphy_settle_cnt_calc(s64 link_freq, u32 timer_clk_rate)
{
	u32 ui; /* ps */
	u32 timer_period; /* ps */
	u32 t_hs_prepare_max; /* ps */
	u32 t_hs_settle; /* ps */
	u8 settle_cnt;

	if (link_freq <= 0)
		return 0;

	ui = div_u64(1000000000000LL, link_freq);
	ui /= 2;
	t_hs_prepare_max = 85000 + 6 * ui;
	t_hs_settle = t_hs_prepare_max;

	timer_period = div_u64(1000000000000LL, timer_clk_rate);
	settle_cnt = t_hs_settle / timer_period - 6;

	return settle_cnt;
}

static void csiphy_gen1_config_lanes(struct csiphy_device *csiphy,
				     struct csiphy_config *cfg,
				     u8 settle_cnt)
{
	struct csiphy_lanes_cfg *c = &cfg->csi2->lane_cfg;
	int i, l = 0;
	u8 val;

	for (i = 0; i <= c->num_data; i++) {
		if (i == c->num_data)
			l = 7;
		else
			l = c->data[i].pos * 2;

		val = CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG;
		val |= 0x17;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG1(l));

		val = CSIPHY_3PH_LNn_CFG2_LP_REC_EN_INT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG2(l));

		val = settle_cnt;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG3(l));

		val = CSIPHY_3PH_LNn_CFG5_T_HS_DTERM |
			CSIPHY_3PH_LNn_CFG5_HS_REC_EQ_FQ_INT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG5(l));

		val = CSIPHY_3PH_LNn_CFG6_SWI_FORCE_INIT_EXIT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG6(l));

		val = CSIPHY_3PH_LNn_CFG7_SWI_T_INIT;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG7(l));

		val = CSIPHY_3PH_LNn_CFG8_SWI_SKIP_WAKEUP |
			CSIPHY_3PH_LNn_CFG8_SKEW_FILTER_ENABLE;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG8(l));

		val = CSIPHY_3PH_LNn_CFG9_SWI_T_WAKEUP;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG9(l));

		val = CSIPHY_3PH_LNn_TEST_IMP_HS_TERM_IMP;
		writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_TEST_IMP(l));

		val = CSIPHY_3PH_LNn_CSI_LANE_CTRL15_SWI_SOT_SYMBOL;
		writel_relaxed(val, csiphy->base +
				    CSIPHY_3PH_LNn_CSI_LANE_CTRL15(l));
	}

	val = CSIPHY_3PH_LNn_CFG1_SWI_REC_DLY_PRG;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG1(l));

	if (csiphy->camss->res->version == CAMSS_660)
		val = CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS_660;
	else
		val = CSIPHY_3PH_LNn_CFG4_T_HS_CLK_MISS;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_CFG4(l));

	val = CSIPHY_3PH_LNn_MISC1_IS_CLKLANE;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_LNn_MISC1(l));
}

static void csiphy_gen2_config_lanes(struct csiphy_device *csiphy,
				     u8 settle_cnt)
{
	const struct csiphy_reg_t *r;
	int i, l, array_size;
	u32 val;

	switch (csiphy->camss->res->version) {
	case CAMSS_7280:
		r = &lane_regs_sm8250[0][0];
		array_size = ARRAY_SIZE(lane_regs_sm8250[0]);
		break;
	case CAMSS_8250:
		r = &lane_regs_sm8250[0][0];
		array_size = ARRAY_SIZE(lane_regs_sm8250[0]);
		break;
	case CAMSS_8280XP:
		r = &lane_regs_sc8280xp[0][0];
		array_size = ARRAY_SIZE(lane_regs_sc8280xp[0]);
		break;
	case CAMSS_845:
		r = &lane_regs_sdm845[0][0];
		array_size = ARRAY_SIZE(lane_regs_sdm845[0]);
		break;
	default:
		WARN(1, "unknown cspi version\n");
		return;
	}

	for (l = 0; l < 5; l++) {
		for (i = 0; i < array_size; i++, r++) {
			switch (r->csiphy_param_type) {
			case CSIPHY_SETTLE_CNT_LOWER_BYTE:
				val = settle_cnt & 0xff;
				break;
			case CSIPHY_DNP_PARAMS:
				continue;
			default:
				val = r->reg_data;
				break;
			}
			writel_relaxed(val, csiphy->base + r->reg_addr);
		}
	}
}

static u8 csiphy_get_lane_mask(struct csiphy_lanes_cfg *lane_cfg)
{
	u8 lane_mask;
	int i;

	lane_mask = CSIPHY_3PH_CMN_CSI_COMMON_CTRL5_CLK_ENABLE;

	for (i = 0; i < lane_cfg->num_data; i++)
		lane_mask |= 1 << lane_cfg->data[i].pos;

	return lane_mask;
}

static bool csiphy_is_gen2(u32 version)
{
	bool ret = false;

	switch (version) {
	case CAMSS_7280:
	case CAMSS_8250:
	case CAMSS_8280XP:
	case CAMSS_845:
		ret = true;
		break;
	}

	return ret;
}

static void csiphy_lanes_enable(struct csiphy_device *csiphy,
				struct csiphy_config *cfg,
				s64 link_freq, u8 lane_mask)
{
	struct csiphy_lanes_cfg *c = &cfg->csi2->lane_cfg;
	u8 settle_cnt;
	u8 val;
	int i;

	settle_cnt = csiphy_settle_cnt_calc(link_freq, csiphy->timer_clk_rate);

	val = CSIPHY_3PH_CMN_CSI_COMMON_CTRL5_CLK_ENABLE;
	for (i = 0; i < c->num_data; i++)
		val |= BIT(c->data[i].pos * 2);

	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(5));

	val = CSIPHY_3PH_CMN_CSI_COMMON_CTRL6_COMMON_PWRDN_B;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));

	val = 0x02;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(7));

	val = 0x00;
	writel_relaxed(val, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(0));

	if (csiphy_is_gen2(csiphy->camss->res->version))
		csiphy_gen2_config_lanes(csiphy, settle_cnt);
	else
		csiphy_gen1_config_lanes(csiphy, cfg, settle_cnt);

	/* IRQ_MASK registers - disable all interrupts */
	for (i = 11; i < 22; i++)
		writel_relaxed(0, csiphy->base + CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(i));
}

static void csiphy_lanes_disable(struct csiphy_device *csiphy,
				 struct csiphy_config *cfg)
{
	writel_relaxed(0, csiphy->base +
			  CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(5));

	writel_relaxed(0, csiphy->base +
			  CSIPHY_3PH_CMN_CSI_COMMON_CTRLn(6));
}

const struct csiphy_hw_ops csiphy_ops_3ph_1_0 = {
	.get_lane_mask = csiphy_get_lane_mask,
	.hw_version_read = csiphy_hw_version_read,
	.reset = csiphy_reset,
	.lanes_enable = csiphy_lanes_enable,
	.lanes_disable = csiphy_lanes_disable,
	.isr = csiphy_isr,
};
