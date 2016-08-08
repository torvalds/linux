/*
 * Copyright (c) 2014, Sony Mobile Communications AB.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Author: Bjorn Andersson <bjorn.andersson@sonymobile.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mfd/qcom_rpm.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <dt-bindings/mfd/qcom-rpm.h>

struct qcom_rpm_resource {
	unsigned target_id;
	unsigned status_id;
	unsigned select_id;
	unsigned size;
};

struct qcom_rpm_data {
	u32 version;
	const struct qcom_rpm_resource *resource_table;
	unsigned int n_resources;
	unsigned int req_ctx_off;
	unsigned int req_sel_off;
	unsigned int ack_ctx_off;
	unsigned int ack_sel_off;
	unsigned int req_sel_size;
	unsigned int ack_sel_size;
};

struct qcom_rpm {
	struct device *dev;
	struct regmap *ipc_regmap;
	unsigned ipc_offset;
	unsigned ipc_bit;

	struct completion ack;
	struct mutex lock;

	void __iomem *status_regs;
	void __iomem *ctrl_regs;
	void __iomem *req_regs;

	u32 ack_status;

	const struct qcom_rpm_data *data;
};

#define RPM_STATUS_REG(rpm, i)	((rpm)->status_regs + (i) * 4)
#define RPM_CTRL_REG(rpm, i)	((rpm)->ctrl_regs + (i) * 4)
#define RPM_REQ_REG(rpm, i)	((rpm)->req_regs + (i) * 4)

#define RPM_REQUEST_TIMEOUT	(5 * HZ)

#define RPM_MAX_SEL_SIZE	7

#define RPM_NOTIFICATION	BIT(30)
#define RPM_REJECTED		BIT(31)

static const struct qcom_rpm_resource apq8064_rpm_resource_table[] = {
	[QCOM_RPM_CXO_CLK] =			{ 25, 9, 5, 1 },
	[QCOM_RPM_PXO_CLK] =			{ 26, 10, 6, 1 },
	[QCOM_RPM_APPS_FABRIC_CLK] =		{ 27, 11, 8, 1 },
	[QCOM_RPM_SYS_FABRIC_CLK] =		{ 28, 12, 9, 1 },
	[QCOM_RPM_MM_FABRIC_CLK] =		{ 29, 13, 10, 1 },
	[QCOM_RPM_DAYTONA_FABRIC_CLK] =		{ 30, 14, 11, 1 },
	[QCOM_RPM_SFPB_CLK] =			{ 31, 15, 12, 1 },
	[QCOM_RPM_CFPB_CLK] =			{ 32, 16, 13, 1 },
	[QCOM_RPM_MMFPB_CLK] =			{ 33, 17, 14, 1 },
	[QCOM_RPM_EBI1_CLK] =			{ 34, 18, 16, 1 },
	[QCOM_RPM_APPS_FABRIC_HALT] =		{ 35, 19, 18, 1 },
	[QCOM_RPM_APPS_FABRIC_MODE] =		{ 37, 20, 19, 1 },
	[QCOM_RPM_APPS_FABRIC_IOCTL] =		{ 40, 21, 20, 1 },
	[QCOM_RPM_APPS_FABRIC_ARB] =		{ 41, 22, 21, 12 },
	[QCOM_RPM_SYS_FABRIC_HALT] =		{ 53, 23, 22, 1 },
	[QCOM_RPM_SYS_FABRIC_MODE] =		{ 55, 24, 23, 1 },
	[QCOM_RPM_SYS_FABRIC_IOCTL] =		{ 58, 25, 24, 1 },
	[QCOM_RPM_SYS_FABRIC_ARB] =		{ 59, 26, 25, 30 },
	[QCOM_RPM_MM_FABRIC_HALT] =		{ 89, 27, 26, 1 },
	[QCOM_RPM_MM_FABRIC_MODE] =		{ 91, 28, 27, 1 },
	[QCOM_RPM_MM_FABRIC_IOCTL] =		{ 94, 29, 28, 1 },
	[QCOM_RPM_MM_FABRIC_ARB] =		{ 95, 30, 29, 21 },
	[QCOM_RPM_PM8921_SMPS1] =		{ 116, 31, 30, 2 },
	[QCOM_RPM_PM8921_SMPS2] =		{ 118, 33, 31, 2 },
	[QCOM_RPM_PM8921_SMPS3] =		{ 120, 35, 32, 2 },
	[QCOM_RPM_PM8921_SMPS4] =		{ 122, 37, 33, 2 },
	[QCOM_RPM_PM8921_SMPS5] =		{ 124, 39, 34, 2 },
	[QCOM_RPM_PM8921_SMPS6] =		{ 126, 41, 35, 2 },
	[QCOM_RPM_PM8921_SMPS7] =		{ 128, 43, 36, 2 },
	[QCOM_RPM_PM8921_SMPS8] =		{ 130, 45, 37, 2 },
	[QCOM_RPM_PM8921_LDO1] =		{ 132, 47, 38, 2 },
	[QCOM_RPM_PM8921_LDO2] =		{ 134, 49, 39, 2 },
	[QCOM_RPM_PM8921_LDO3] =		{ 136, 51, 40, 2 },
	[QCOM_RPM_PM8921_LDO4] =		{ 138, 53, 41, 2 },
	[QCOM_RPM_PM8921_LDO5] =		{ 140, 55, 42, 2 },
	[QCOM_RPM_PM8921_LDO6] =		{ 142, 57, 43, 2 },
	[QCOM_RPM_PM8921_LDO7] =		{ 144, 59, 44, 2 },
	[QCOM_RPM_PM8921_LDO8] =		{ 146, 61, 45, 2 },
	[QCOM_RPM_PM8921_LDO9] =		{ 148, 63, 46, 2 },
	[QCOM_RPM_PM8921_LDO10] =		{ 150, 65, 47, 2 },
	[QCOM_RPM_PM8921_LDO11] =		{ 152, 67, 48, 2 },
	[QCOM_RPM_PM8921_LDO12] =		{ 154, 69, 49, 2 },
	[QCOM_RPM_PM8921_LDO13] =		{ 156, 71, 50, 2 },
	[QCOM_RPM_PM8921_LDO14] =		{ 158, 73, 51, 2 },
	[QCOM_RPM_PM8921_LDO15] =		{ 160, 75, 52, 2 },
	[QCOM_RPM_PM8921_LDO16] =		{ 162, 77, 53, 2 },
	[QCOM_RPM_PM8921_LDO17] =		{ 164, 79, 54, 2 },
	[QCOM_RPM_PM8921_LDO18] =		{ 166, 81, 55, 2 },
	[QCOM_RPM_PM8921_LDO19] =		{ 168, 83, 56, 2 },
	[QCOM_RPM_PM8921_LDO20] =		{ 170, 85, 57, 2 },
	[QCOM_RPM_PM8921_LDO21] =		{ 172, 87, 58, 2 },
	[QCOM_RPM_PM8921_LDO22] =		{ 174, 89, 59, 2 },
	[QCOM_RPM_PM8921_LDO23] =		{ 176, 91, 60, 2 },
	[QCOM_RPM_PM8921_LDO24] =		{ 178, 93, 61, 2 },
	[QCOM_RPM_PM8921_LDO25] =		{ 180, 95, 62, 2 },
	[QCOM_RPM_PM8921_LDO26] =		{ 182, 97, 63, 2 },
	[QCOM_RPM_PM8921_LDO27] =		{ 184, 99, 64, 2 },
	[QCOM_RPM_PM8921_LDO28] =		{ 186, 101, 65, 2 },
	[QCOM_RPM_PM8921_LDO29] =		{ 188, 103, 66, 2 },
	[QCOM_RPM_PM8921_CLK1] =		{ 190, 105, 67, 2 },
	[QCOM_RPM_PM8921_CLK2] =		{ 192, 107, 68, 2 },
	[QCOM_RPM_PM8921_LVS1] =		{ 194, 109, 69, 1 },
	[QCOM_RPM_PM8921_LVS2] =		{ 195, 110, 70, 1 },
	[QCOM_RPM_PM8921_LVS3] =		{ 196, 111, 71, 1 },
	[QCOM_RPM_PM8921_LVS4] =		{ 197, 112, 72, 1 },
	[QCOM_RPM_PM8921_LVS5] =		{ 198, 113, 73, 1 },
	[QCOM_RPM_PM8921_LVS6] =		{ 199, 114, 74, 1 },
	[QCOM_RPM_PM8921_LVS7] =		{ 200, 115, 75, 1 },
	[QCOM_RPM_PM8821_SMPS1] =		{ 201, 116, 76, 2 },
	[QCOM_RPM_PM8821_SMPS2] =		{ 203, 118, 77, 2 },
	[QCOM_RPM_PM8821_LDO1] =		{ 205, 120, 78, 2 },
	[QCOM_RPM_PM8921_NCP] =			{ 207, 122, 80, 2 },
	[QCOM_RPM_CXO_BUFFERS] =		{ 209, 124, 81, 1 },
	[QCOM_RPM_USB_OTG_SWITCH] =		{ 210, 125, 82, 1 },
	[QCOM_RPM_HDMI_SWITCH] =		{ 211, 126, 83, 1 },
	[QCOM_RPM_DDR_DMM] =			{ 212, 127, 84, 2 },
	[QCOM_RPM_QDSS_CLK] =			{ 214, ~0, 7, 1 },
	[QCOM_RPM_VDDMIN_GPIO] =		{ 215, 131, 89, 1 },
};

static const struct qcom_rpm_data apq8064_template = {
	.version = 3,
	.resource_table = apq8064_rpm_resource_table,
	.n_resources = ARRAY_SIZE(apq8064_rpm_resource_table),
	.req_ctx_off = 3,
	.req_sel_off = 11,
	.ack_ctx_off = 15,
	.ack_sel_off = 23,
	.req_sel_size = 4,
	.ack_sel_size = 7,
};

static const struct qcom_rpm_resource msm8660_rpm_resource_table[] = {
	[QCOM_RPM_CXO_CLK] =			{ 32, 12, 5, 1 },
	[QCOM_RPM_PXO_CLK] =			{ 33, 13, 6, 1 },
	[QCOM_RPM_PLL_4] =			{ 34, 14, 7, 1 },
	[QCOM_RPM_APPS_FABRIC_CLK] =		{ 35, 15, 8, 1 },
	[QCOM_RPM_SYS_FABRIC_CLK] =		{ 36, 16, 9, 1 },
	[QCOM_RPM_MM_FABRIC_CLK] =		{ 37, 17, 10, 1 },
	[QCOM_RPM_DAYTONA_FABRIC_CLK] =		{ 38, 18, 11, 1 },
	[QCOM_RPM_SFPB_CLK] =			{ 39, 19, 12, 1 },
	[QCOM_RPM_CFPB_CLK] =			{ 40, 20, 13, 1 },
	[QCOM_RPM_MMFPB_CLK] =			{ 41, 21, 14, 1 },
	[QCOM_RPM_SMI_CLK] =			{ 42, 22, 15, 1 },
	[QCOM_RPM_EBI1_CLK] =			{ 43, 23, 16, 1 },
	[QCOM_RPM_APPS_L2_CACHE_CTL] =		{ 44, 24, 17, 1 },
	[QCOM_RPM_APPS_FABRIC_HALT] =		{ 45, 25, 18, 2 },
	[QCOM_RPM_APPS_FABRIC_MODE] =		{ 47, 26, 19, 3 },
	[QCOM_RPM_APPS_FABRIC_ARB] =		{ 51, 28, 21, 6 },
	[QCOM_RPM_SYS_FABRIC_HALT] =		{ 63, 29, 22, 2 },
	[QCOM_RPM_SYS_FABRIC_MODE] =		{ 65, 30, 23, 3 },
	[QCOM_RPM_SYS_FABRIC_ARB] =		{ 69, 32, 25, 22 },
	[QCOM_RPM_MM_FABRIC_HALT] =		{ 105, 33, 26, 2 },
	[QCOM_RPM_MM_FABRIC_MODE] =		{ 107, 34, 27, 3 },
	[QCOM_RPM_MM_FABRIC_ARB] =		{ 111, 36, 29, 23 },
	[QCOM_RPM_PM8901_SMPS0] =		{ 134, 37, 30, 2 },
	[QCOM_RPM_PM8901_SMPS1] =		{ 136, 39, 31, 2 },
	[QCOM_RPM_PM8901_SMPS2] =		{ 138, 41, 32, 2 },
	[QCOM_RPM_PM8901_SMPS3] =		{ 140, 43, 33, 2 },
	[QCOM_RPM_PM8901_SMPS4] =		{ 142, 45, 34, 2 },
	[QCOM_RPM_PM8901_LDO0] =		{ 144, 47, 35, 2 },
	[QCOM_RPM_PM8901_LDO1] =		{ 146, 49, 36, 2 },
	[QCOM_RPM_PM8901_LDO2] =		{ 148, 51, 37, 2 },
	[QCOM_RPM_PM8901_LDO3] =		{ 150, 53, 38, 2 },
	[QCOM_RPM_PM8901_LDO4] =		{ 152, 55, 39, 2 },
	[QCOM_RPM_PM8901_LDO5] =		{ 154, 57, 40, 2 },
	[QCOM_RPM_PM8901_LDO6] =		{ 156, 59, 41, 2 },
	[QCOM_RPM_PM8901_LVS0] =		{ 158, 61, 42, 1 },
	[QCOM_RPM_PM8901_LVS1] =		{ 159, 62, 43, 1 },
	[QCOM_RPM_PM8901_LVS2] =		{ 160, 63, 44, 1 },
	[QCOM_RPM_PM8901_LVS3] =		{ 161, 64, 45, 1 },
	[QCOM_RPM_PM8901_MVS] =			{ 162, 65, 46, 1 },
	[QCOM_RPM_PM8058_SMPS0] =		{ 163, 66, 47, 2 },
	[QCOM_RPM_PM8058_SMPS1] =		{ 165, 68, 48, 2 },
	[QCOM_RPM_PM8058_SMPS2] =		{ 167, 70, 49, 2 },
	[QCOM_RPM_PM8058_SMPS3] =		{ 169, 72, 50, 2 },
	[QCOM_RPM_PM8058_SMPS4] =		{ 171, 74, 51, 2 },
	[QCOM_RPM_PM8058_LDO0] =		{ 173, 76, 52, 2 },
	[QCOM_RPM_PM8058_LDO1] =		{ 175, 78, 53, 2 },
	[QCOM_RPM_PM8058_LDO2] =		{ 177, 80, 54, 2 },
	[QCOM_RPM_PM8058_LDO3] =		{ 179, 82, 55, 2 },
	[QCOM_RPM_PM8058_LDO4] =		{ 181, 84, 56, 2 },
	[QCOM_RPM_PM8058_LDO5] =		{ 183, 86, 57, 2 },
	[QCOM_RPM_PM8058_LDO6] =		{ 185, 88, 58, 2 },
	[QCOM_RPM_PM8058_LDO7] =		{ 187, 90, 59, 2 },
	[QCOM_RPM_PM8058_LDO8] =		{ 189, 92, 60, 2 },
	[QCOM_RPM_PM8058_LDO9] =		{ 191, 94, 61, 2 },
	[QCOM_RPM_PM8058_LDO10] =		{ 193, 96, 62, 2 },
	[QCOM_RPM_PM8058_LDO11] =		{ 195, 98, 63, 2 },
	[QCOM_RPM_PM8058_LDO12] =		{ 197, 100, 64, 2 },
	[QCOM_RPM_PM8058_LDO13] =		{ 199, 102, 65, 2 },
	[QCOM_RPM_PM8058_LDO14] =		{ 201, 104, 66, 2 },
	[QCOM_RPM_PM8058_LDO15] =		{ 203, 106, 67, 2 },
	[QCOM_RPM_PM8058_LDO16] =		{ 205, 108, 68, 2 },
	[QCOM_RPM_PM8058_LDO17] =		{ 207, 110, 69, 2 },
	[QCOM_RPM_PM8058_LDO18] =		{ 209, 112, 70, 2 },
	[QCOM_RPM_PM8058_LDO19] =		{ 211, 114, 71, 2 },
	[QCOM_RPM_PM8058_LDO20] =		{ 213, 116, 72, 2 },
	[QCOM_RPM_PM8058_LDO21] =		{ 215, 118, 73, 2 },
	[QCOM_RPM_PM8058_LDO22] =		{ 217, 120, 74, 2 },
	[QCOM_RPM_PM8058_LDO23] =		{ 219, 122, 75, 2 },
	[QCOM_RPM_PM8058_LDO24] =		{ 221, 124, 76, 2 },
	[QCOM_RPM_PM8058_LDO25] =		{ 223, 126, 77, 2 },
	[QCOM_RPM_PM8058_LVS0] =		{ 225, 128, 78, 1 },
	[QCOM_RPM_PM8058_LVS1] =		{ 226, 129, 79, 1 },
	[QCOM_RPM_PM8058_NCP] =			{ 227, 130, 80, 2 },
	[QCOM_RPM_CXO_BUFFERS] =		{ 229, 132, 81, 1 },
};

static const struct qcom_rpm_data msm8660_template = {
	.version = 2,
	.resource_table = msm8660_rpm_resource_table,
	.n_resources = ARRAY_SIZE(msm8660_rpm_resource_table),
	.req_ctx_off = 3,
	.req_sel_off = 11,
	.ack_ctx_off = 19,
	.ack_sel_off = 27,
	.req_sel_size = 7,
	.ack_sel_size = 7,
};

static const struct qcom_rpm_resource msm8960_rpm_resource_table[] = {
	[QCOM_RPM_CXO_CLK] =			{ 25, 9, 5, 1 },
	[QCOM_RPM_PXO_CLK] =			{ 26, 10, 6, 1 },
	[QCOM_RPM_APPS_FABRIC_CLK] =		{ 27, 11, 8, 1 },
	[QCOM_RPM_SYS_FABRIC_CLK] =		{ 28, 12, 9, 1 },
	[QCOM_RPM_MM_FABRIC_CLK] =		{ 29, 13, 10, 1 },
	[QCOM_RPM_DAYTONA_FABRIC_CLK] =		{ 30, 14, 11, 1 },
	[QCOM_RPM_SFPB_CLK] =			{ 31, 15, 12, 1 },
	[QCOM_RPM_CFPB_CLK] =			{ 32, 16, 13, 1 },
	[QCOM_RPM_MMFPB_CLK] =			{ 33, 17, 14, 1 },
	[QCOM_RPM_EBI1_CLK] =			{ 34, 18, 16, 1 },
	[QCOM_RPM_APPS_FABRIC_HALT] =		{ 35, 19, 18, 1 },
	[QCOM_RPM_APPS_FABRIC_MODE] =		{ 37, 20, 19, 1 },
	[QCOM_RPM_APPS_FABRIC_IOCTL] =		{ 40, 21, 20, 1 },
	[QCOM_RPM_APPS_FABRIC_ARB] =		{ 41, 22, 21, 12 },
	[QCOM_RPM_SYS_FABRIC_HALT] =		{ 53, 23, 22, 1 },
	[QCOM_RPM_SYS_FABRIC_MODE] =		{ 55, 24, 23, 1 },
	[QCOM_RPM_SYS_FABRIC_IOCTL] =		{ 58, 25, 24, 1 },
	[QCOM_RPM_SYS_FABRIC_ARB] =		{ 59, 26, 25, 29 },
	[QCOM_RPM_MM_FABRIC_HALT] =		{ 88, 27, 26, 1 },
	[QCOM_RPM_MM_FABRIC_MODE] =		{ 90, 28, 27, 1 },
	[QCOM_RPM_MM_FABRIC_IOCTL] =		{ 93, 29, 28, 1 },
	[QCOM_RPM_MM_FABRIC_ARB] =		{ 94, 30, 29, 23 },
	[QCOM_RPM_PM8921_SMPS1] =		{ 117, 31, 30, 2 },
	[QCOM_RPM_PM8921_SMPS2] =		{ 119, 33, 31, 2 },
	[QCOM_RPM_PM8921_SMPS3] =		{ 121, 35, 32, 2 },
	[QCOM_RPM_PM8921_SMPS4] =		{ 123, 37, 33, 2 },
	[QCOM_RPM_PM8921_SMPS5] =		{ 125, 39, 34, 2 },
	[QCOM_RPM_PM8921_SMPS6] =		{ 127, 41, 35, 2 },
	[QCOM_RPM_PM8921_SMPS7] =		{ 129, 43, 36, 2 },
	[QCOM_RPM_PM8921_SMPS8] =		{ 131, 45, 37, 2 },
	[QCOM_RPM_PM8921_LDO1] =		{ 133, 47, 38, 2 },
	[QCOM_RPM_PM8921_LDO2] =		{ 135, 49, 39, 2 },
	[QCOM_RPM_PM8921_LDO3] =		{ 137, 51, 40, 2 },
	[QCOM_RPM_PM8921_LDO4] =		{ 139, 53, 41, 2 },
	[QCOM_RPM_PM8921_LDO5] =		{ 141, 55, 42, 2 },
	[QCOM_RPM_PM8921_LDO6] =		{ 143, 57, 43, 2 },
	[QCOM_RPM_PM8921_LDO7] =		{ 145, 59, 44, 2 },
	[QCOM_RPM_PM8921_LDO8] =		{ 147, 61, 45, 2 },
	[QCOM_RPM_PM8921_LDO9] =		{ 149, 63, 46, 2 },
	[QCOM_RPM_PM8921_LDO10] =		{ 151, 65, 47, 2 },
	[QCOM_RPM_PM8921_LDO11] =		{ 153, 67, 48, 2 },
	[QCOM_RPM_PM8921_LDO12] =		{ 155, 69, 49, 2 },
	[QCOM_RPM_PM8921_LDO13] =		{ 157, 71, 50, 2 },
	[QCOM_RPM_PM8921_LDO14] =		{ 159, 73, 51, 2 },
	[QCOM_RPM_PM8921_LDO15] =		{ 161, 75, 52, 2 },
	[QCOM_RPM_PM8921_LDO16] =		{ 163, 77, 53, 2 },
	[QCOM_RPM_PM8921_LDO17] =		{ 165, 79, 54, 2 },
	[QCOM_RPM_PM8921_LDO18] =		{ 167, 81, 55, 2 },
	[QCOM_RPM_PM8921_LDO19] =		{ 169, 83, 56, 2 },
	[QCOM_RPM_PM8921_LDO20] =		{ 171, 85, 57, 2 },
	[QCOM_RPM_PM8921_LDO21] =		{ 173, 87, 58, 2 },
	[QCOM_RPM_PM8921_LDO22] =		{ 175, 89, 59, 2 },
	[QCOM_RPM_PM8921_LDO23] =		{ 177, 91, 60, 2 },
	[QCOM_RPM_PM8921_LDO24] =		{ 179, 93, 61, 2 },
	[QCOM_RPM_PM8921_LDO25] =		{ 181, 95, 62, 2 },
	[QCOM_RPM_PM8921_LDO26] =		{ 183, 97, 63, 2 },
	[QCOM_RPM_PM8921_LDO27] =		{ 185, 99, 64, 2 },
	[QCOM_RPM_PM8921_LDO28] =		{ 187, 101, 65, 2 },
	[QCOM_RPM_PM8921_LDO29] =		{ 189, 103, 66, 2 },
	[QCOM_RPM_PM8921_CLK1] =		{ 191, 105, 67, 2 },
	[QCOM_RPM_PM8921_CLK2] =		{ 193, 107, 68, 2 },
	[QCOM_RPM_PM8921_LVS1] =		{ 195, 109, 69, 1 },
	[QCOM_RPM_PM8921_LVS2] =		{ 196, 110, 70, 1 },
	[QCOM_RPM_PM8921_LVS3] =		{ 197, 111, 71, 1 },
	[QCOM_RPM_PM8921_LVS4] =		{ 198, 112, 72, 1 },
	[QCOM_RPM_PM8921_LVS5] =		{ 199, 113, 73, 1 },
	[QCOM_RPM_PM8921_LVS6] =		{ 200, 114, 74, 1 },
	[QCOM_RPM_PM8921_LVS7] =		{ 201, 115, 75, 1 },
	[QCOM_RPM_PM8921_NCP] =			{ 202, 116, 80, 2 },
	[QCOM_RPM_CXO_BUFFERS] =		{ 204, 118, 81, 1 },
	[QCOM_RPM_USB_OTG_SWITCH] =		{ 205, 119, 82, 1 },
	[QCOM_RPM_HDMI_SWITCH] =		{ 206, 120, 83, 1 },
	[QCOM_RPM_DDR_DMM] =			{ 207, 121, 84, 2 },
};

static const struct qcom_rpm_data msm8960_template = {
	.version = 3,
	.resource_table = msm8960_rpm_resource_table,
	.n_resources = ARRAY_SIZE(msm8960_rpm_resource_table),
	.req_ctx_off = 3,
	.req_sel_off = 11,
	.ack_ctx_off = 15,
	.ack_sel_off = 23,
	.req_sel_size = 4,
	.ack_sel_size = 7,
};

static const struct qcom_rpm_resource ipq806x_rpm_resource_table[] = {
	[QCOM_RPM_CXO_CLK] =			{ 25, 9, 5, 1 },
	[QCOM_RPM_PXO_CLK] =			{ 26, 10, 6, 1 },
	[QCOM_RPM_APPS_FABRIC_CLK] =		{ 27, 11, 8, 1 },
	[QCOM_RPM_SYS_FABRIC_CLK] =		{ 28, 12, 9, 1 },
	[QCOM_RPM_NSS_FABRIC_0_CLK] =		{ 29, 13, 10, 1 },
	[QCOM_RPM_DAYTONA_FABRIC_CLK] =		{ 30, 14, 11, 1 },
	[QCOM_RPM_SFPB_CLK] =			{ 31, 15, 12, 1 },
	[QCOM_RPM_CFPB_CLK] =			{ 32, 16, 13, 1 },
	[QCOM_RPM_NSS_FABRIC_1_CLK] =		{ 33, 17, 14, 1 },
	[QCOM_RPM_EBI1_CLK] =			{ 34, 18, 16, 1 },
	[QCOM_RPM_APPS_FABRIC_HALT] =		{ 35, 19, 18, 2 },
	[QCOM_RPM_APPS_FABRIC_MODE] =		{ 37, 20, 19, 3 },
	[QCOM_RPM_APPS_FABRIC_IOCTL] =		{ 40, 21, 20, 1 },
	[QCOM_RPM_APPS_FABRIC_ARB] =		{ 41, 22, 21, 12 },
	[QCOM_RPM_SYS_FABRIC_HALT] =		{ 53, 23, 22, 2 },
	[QCOM_RPM_SYS_FABRIC_MODE] =		{ 55, 24, 23, 3 },
	[QCOM_RPM_SYS_FABRIC_IOCTL] =		{ 58, 25, 24, 1 },
	[QCOM_RPM_SYS_FABRIC_ARB] =		{ 59, 26, 25, 30 },
	[QCOM_RPM_MM_FABRIC_HALT] =		{ 89, 27, 26, 2 },
	[QCOM_RPM_MM_FABRIC_MODE] =		{ 91, 28, 27, 3 },
	[QCOM_RPM_MM_FABRIC_IOCTL] =		{ 94, 29, 28, 1 },
	[QCOM_RPM_MM_FABRIC_ARB] =		{ 95, 30, 29, 2 },
	[QCOM_RPM_CXO_BUFFERS] =		{ 209, 33, 31, 1 },
	[QCOM_RPM_USB_OTG_SWITCH] =		{ 210, 34, 32, 1 },
	[QCOM_RPM_HDMI_SWITCH] =		{ 211, 35, 33, 1 },
	[QCOM_RPM_DDR_DMM] =			{ 212, 36, 34, 2 },
	[QCOM_RPM_VDDMIN_GPIO] =		{ 215, 40, 39, 1 },
	[QCOM_RPM_SMB208_S1a] =			{ 216, 41, 90, 2 },
	[QCOM_RPM_SMB208_S1b] =			{ 218, 43, 91, 2 },
	[QCOM_RPM_SMB208_S2a] =			{ 220, 45, 92, 2 },
	[QCOM_RPM_SMB208_S2b] =			{ 222, 47, 93, 2 },
};

static const struct qcom_rpm_data ipq806x_template = {
	.version = 3,
	.resource_table = ipq806x_rpm_resource_table,
	.n_resources = ARRAY_SIZE(ipq806x_rpm_resource_table),
	.req_ctx_off = 3,
	.req_sel_off = 11,
	.ack_ctx_off = 15,
	.ack_sel_off = 23,
	.req_sel_size = 4,
	.ack_sel_size = 7,
};

static const struct of_device_id qcom_rpm_of_match[] = {
	{ .compatible = "qcom,rpm-apq8064", .data = &apq8064_template },
	{ .compatible = "qcom,rpm-msm8660", .data = &msm8660_template },
	{ .compatible = "qcom,rpm-msm8960", .data = &msm8960_template },
	{ .compatible = "qcom,rpm-ipq8064", .data = &ipq806x_template },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_rpm_of_match);

int qcom_rpm_write(struct qcom_rpm *rpm,
		   int state,
		   int resource,
		   u32 *buf, size_t count)
{
	const struct qcom_rpm_resource *res;
	const struct qcom_rpm_data *data = rpm->data;
	u32 sel_mask[RPM_MAX_SEL_SIZE] = { 0 };
	int left;
	int ret = 0;
	int i;

	if (WARN_ON(resource < 0 || resource >= data->n_resources))
		return -EINVAL;

	res = &data->resource_table[resource];
	if (WARN_ON(res->size != count))
		return -EINVAL;

	mutex_lock(&rpm->lock);

	for (i = 0; i < res->size; i++)
		writel_relaxed(buf[i], RPM_REQ_REG(rpm, res->target_id + i));

	bitmap_set((unsigned long *)sel_mask, res->select_id, 1);
	for (i = 0; i < rpm->data->req_sel_size; i++) {
		writel_relaxed(sel_mask[i],
			       RPM_CTRL_REG(rpm, rpm->data->req_sel_off + i));
	}

	writel_relaxed(BIT(state), RPM_CTRL_REG(rpm, rpm->data->req_ctx_off));

	reinit_completion(&rpm->ack);
	regmap_write(rpm->ipc_regmap, rpm->ipc_offset, BIT(rpm->ipc_bit));

	left = wait_for_completion_timeout(&rpm->ack, RPM_REQUEST_TIMEOUT);
	if (!left)
		ret = -ETIMEDOUT;
	else if (rpm->ack_status & RPM_REJECTED)
		ret = -EIO;

	mutex_unlock(&rpm->lock);

	return ret;
}
EXPORT_SYMBOL(qcom_rpm_write);

static irqreturn_t qcom_rpm_ack_interrupt(int irq, void *dev)
{
	struct qcom_rpm *rpm = dev;
	u32 ack;
	int i;

	ack = readl_relaxed(RPM_CTRL_REG(rpm, rpm->data->ack_ctx_off));
	for (i = 0; i < rpm->data->ack_sel_size; i++)
		writel_relaxed(0,
			RPM_CTRL_REG(rpm, rpm->data->ack_sel_off + i));
	writel(0, RPM_CTRL_REG(rpm, rpm->data->ack_ctx_off));

	if (ack & RPM_NOTIFICATION) {
		dev_warn(rpm->dev, "ignoring notification!\n");
	} else {
		rpm->ack_status = ack;
		complete(&rpm->ack);
	}

	return IRQ_HANDLED;
}

static irqreturn_t qcom_rpm_err_interrupt(int irq, void *dev)
{
	struct qcom_rpm *rpm = dev;

	regmap_write(rpm->ipc_regmap, rpm->ipc_offset, BIT(rpm->ipc_bit));
	dev_err(rpm->dev, "RPM triggered fatal error\n");

	return IRQ_HANDLED;
}

static irqreturn_t qcom_rpm_wakeup_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

static int qcom_rpm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *syscon_np;
	struct resource *res;
	struct qcom_rpm *rpm;
	u32 fw_version[3];
	int irq_wakeup;
	int irq_ack;
	int irq_err;
	int ret;

	rpm = devm_kzalloc(&pdev->dev, sizeof(*rpm), GFP_KERNEL);
	if (!rpm)
		return -ENOMEM;

	rpm->dev = &pdev->dev;
	mutex_init(&rpm->lock);
	init_completion(&rpm->ack);

	irq_ack = platform_get_irq_byname(pdev, "ack");
	if (irq_ack < 0) {
		dev_err(&pdev->dev, "required ack interrupt missing\n");
		return irq_ack;
	}

	irq_err = platform_get_irq_byname(pdev, "err");
	if (irq_err < 0) {
		dev_err(&pdev->dev, "required err interrupt missing\n");
		return irq_err;
	}

	irq_wakeup = platform_get_irq_byname(pdev, "wakeup");
	if (irq_wakeup < 0) {
		dev_err(&pdev->dev, "required wakeup interrupt missing\n");
		return irq_wakeup;
	}

	match = of_match_device(qcom_rpm_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;
	rpm->data = match->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rpm->status_regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rpm->status_regs))
		return PTR_ERR(rpm->status_regs);
	rpm->ctrl_regs = rpm->status_regs + 0x400;
	rpm->req_regs = rpm->status_regs + 0x600;

	syscon_np = of_parse_phandle(pdev->dev.of_node, "qcom,ipc", 0);
	if (!syscon_np) {
		dev_err(&pdev->dev, "no qcom,ipc node\n");
		return -ENODEV;
	}

	rpm->ipc_regmap = syscon_node_to_regmap(syscon_np);
	if (IS_ERR(rpm->ipc_regmap))
		return PTR_ERR(rpm->ipc_regmap);

	ret = of_property_read_u32_index(pdev->dev.of_node, "qcom,ipc", 1,
					 &rpm->ipc_offset);
	if (ret < 0) {
		dev_err(&pdev->dev, "no offset in qcom,ipc\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "qcom,ipc", 2,
					 &rpm->ipc_bit);
	if (ret < 0) {
		dev_err(&pdev->dev, "no bit in qcom,ipc\n");
		return -EINVAL;
	}

	dev_set_drvdata(&pdev->dev, rpm);

	fw_version[0] = readl(RPM_STATUS_REG(rpm, 0));
	fw_version[1] = readl(RPM_STATUS_REG(rpm, 1));
	fw_version[2] = readl(RPM_STATUS_REG(rpm, 2));
	if (fw_version[0] != rpm->data->version) {
		dev_err(&pdev->dev,
			"RPM version %u.%u.%u incompatible with driver version %u",
			fw_version[0],
			fw_version[1],
			fw_version[2],
			rpm->data->version);
		return -EFAULT;
	}

	dev_info(&pdev->dev, "RPM firmware %u.%u.%u\n", fw_version[0],
							fw_version[1],
							fw_version[2]);

	ret = devm_request_irq(&pdev->dev,
			       irq_ack,
			       qcom_rpm_ack_interrupt,
			       IRQF_TRIGGER_RISING,
			       "qcom_rpm_ack",
			       rpm);
	if (ret) {
		dev_err(&pdev->dev, "failed to request ack interrupt\n");
		return ret;
	}

	ret = irq_set_irq_wake(irq_ack, 1);
	if (ret)
		dev_warn(&pdev->dev, "failed to mark ack irq as wakeup\n");

	ret = devm_request_irq(&pdev->dev,
			       irq_err,
			       qcom_rpm_err_interrupt,
			       IRQF_TRIGGER_RISING,
			       "qcom_rpm_err",
			       rpm);
	if (ret) {
		dev_err(&pdev->dev, "failed to request err interrupt\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev,
			       irq_wakeup,
			       qcom_rpm_wakeup_interrupt,
			       IRQF_TRIGGER_RISING,
			       "qcom_rpm_wakeup",
			       rpm);
	if (ret) {
		dev_err(&pdev->dev, "failed to request wakeup interrupt\n");
		return ret;
	}

	ret = irq_set_irq_wake(irq_wakeup, 1);
	if (ret)
		dev_warn(&pdev->dev, "failed to mark wakeup irq as wakeup\n");

	return of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
}

static int qcom_rpm_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	return 0;
}

static struct platform_driver qcom_rpm_driver = {
	.probe = qcom_rpm_probe,
	.remove = qcom_rpm_remove,
	.driver  = {
		.name  = "qcom_rpm",
		.of_match_table = qcom_rpm_of_match,
	},
};

static int __init qcom_rpm_init(void)
{
	return platform_driver_register(&qcom_rpm_driver);
}
arch_initcall(qcom_rpm_init);

static void __exit qcom_rpm_exit(void)
{
	platform_driver_unregister(&qcom_rpm_driver);
}
module_exit(qcom_rpm_exit)

MODULE_DESCRIPTION("Qualcomm Resource Power Manager driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
