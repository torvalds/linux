// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Rockchip Electronics Co., Ltd. */
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <linux/sem.h>
#include <linux/seq_file.h>

#include "dev.h"
#include "procfs.h"
#include "version.h"
#include "regs.h"
#include "regs_v2x.h"
#include "isp_params_v3x.h"
#include "isp_params_v32.h"

#ifdef CONFIG_PROC_FS

static void isp20_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 val;

	val = rkisp_read(dev, ISP_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC2_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG",
		(val & CIF_ISP_CTRL_ISP_GAMMA_IN_ENA) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (gain: 0x%08x, 0x%08x)\n", "AWBGAIN",
		(val & CIF_ISP_CTRL_ISP_AWB_ENA) ? "ON" : "OFF", val,
		rkisp_read(dev, CIF_ISP_AWB_GAIN_G_V12, false),
		rkisp_read(dev, CIF_ISP_AWB_GAIN_RB_V12, false));
	val = rkisp_read(dev, ISP_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE",
		(val & 1) ? "ON" : "OFF", val,
		effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP_WDR_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "WDR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRTMO_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRTMO", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAIN_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAIN", val ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CSM",
		(val & full_range_flg) ? "FULL" : "LIMITED", val);

	val = rkisp_read(dev, ISP_AFM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_AWB_PROP_V10, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAWB",
		(val & CIF_ISP_AWB_ENABLE) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_YUVAE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YUVAE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HIST_HIST_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIHST", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
}

static void isp21_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 val, tmp;

	val = rkisp_read(dev, ISP_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG",
		(val & CIF_ISP_CTRL_ISP_GAMMA_IN_ENA) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (gain: 0x%08x, 0x%08x)\n", "AWBGAIN",
		(val & CIF_ISP_CTRL_ISP_AWB_ENA) ? "ON" : "OFF", val,
		rkisp_read(dev, CIF_ISP_AWB_GAIN_G_V12, false),
		rkisp_read(dev, CIF_ISP_AWB_GAIN_RB_V12, false));
	val = rkisp_read(dev, ISP_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE",
		(val & 1) ? "ON" : "OFF", val,
		effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP21_DRC_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRDRC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_BAYNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAYNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_BAY3D_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAY3D", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_YNR_GLOBAL_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_CNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_SHARP_SHARP_EN, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SHARP", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_CTRL, false);
	tmp = rkisp_read(dev, ISP_CC_COEFF_0, false);
	seq_printf(p, "%-10s %s(0x%x), y_offs:0x%x c_offs:0x%x\n"
		   "\t   coeff Y:0x%x 0x%x 0x%x CB:0x%x 0x%x 0x%x CR:0x%x 0x%x 0x%x\n",
		   "CSM", (val & full_range_flg) ? "FULL" : "LIMIT", val,
		   (tmp >> 24) & 0x3f,
		   (tmp >> 16) & 0xff ? (tmp >> 16) & 0xff : 128,
		   tmp & 0x1ff,
		   rkisp_read(dev, ISP_CC_COEFF_1, false),
		   rkisp_read(dev, ISP_CC_COEFF_2, false),
		   rkisp_read(dev, ISP_CC_COEFF_3, false),
		   rkisp_read(dev, ISP_CC_COEFF_4, false),
		   rkisp_read(dev, ISP_CC_COEFF_5, false),
		   rkisp_read(dev, ISP_CC_COEFF_6, false),
		   rkisp_read(dev, ISP_CC_COEFF_7, false),
		   rkisp_read(dev, ISP_CC_COEFF_8, false));
	val = rkisp_read(dev, ISP_AFM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, CIF_ISP_AWB_PROP_V10, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIAWB",
		(val & CIF_ISP_AWB_ENABLE) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_YUVAE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YUVAE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_HIST_HIST_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SIHST", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP21_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
}

static void isp30_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 val, tmp;

	val = rkisp_read(dev, ISP3X_CMSK_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CMSK", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DPCC2_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG", (val & BIT(6)) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x) (gain: 0x%08x, 0x%08x)\n", "AWBGAIN",
		   (val & BIT(7)) ? "ON" : "OFF", val,
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_G, false),
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_RB, false));
	val = rkisp_read(dev, ISP3X_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE", (val & 1) ? "ON" : "OFF", val,
		   effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP3X_DRC_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRDRC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BAYNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAYNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BAY3D_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAY3D", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_YNR_GLOBAL_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_SHARP_EN, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SHARP", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	tmp = rkisp_read(dev, ISP3X_ISP_CC_COEFF_0, false);
	seq_printf(p, "%-10s %s(0x%x), y_offs:0x%x c_offs:0x%x\n"
		   "\t   coeff Y:0x%x 0x%x 0x%x CB:0x%x 0x%x 0x%x CR:0x%x 0x%x 0x%x\n",
		   "CSM", (val & full_range_flg) ? "FULL" : "LIMIT", val,
		   (tmp >> 24) & 0x3f,
		   (tmp >> 16) & 0xff ? (tmp >> 16) & 0xff : 128,
		   tmp & 0x1ff,
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_1, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_2, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_3, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_4, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_5, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_6, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_7, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_8, false));
	val = rkisp_read(dev, ISP3X_CAC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CAC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GAIN_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAIN", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST2", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL1, true);
	seq_printf(p, "%-10s %s(0x%x)\n", "BigMode", val & BIT(28) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG1, true);
	seq_printf(p, "%-10s space full status group (0x%x)\n"
		   "\t   ibuf2:0x%x ibuf1:0x%x ibuf0:0x%x mpfbc_infifo:0x%x\n"
		   "\t   r1fifo:0x%x r0fifo:0x%x outfifo:0x%x lafifo:0x%x\n",
		   "DEBUG1", val,
		   val >> 28, (val >> 24) & 0xf, (val >> 20) & 0xf, (val >> 16) & 0xf,
		   (val >> 12) & 0xf, (val >> 8) & 0xf, (val >> 4) & 0xf, val & 0xf);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG2, true);
	seq_printf(p, "%-10s 0x%x\n"
		   "\t   bay3d_fifo_full iir:%d cur:%d\n"
		   "\t   module outform vertical counter:%d, out frame counter:%d\n"
		   "\t   isp output line counter:%d\n",
		   "DEBUG2", val, !!(val & BIT(31)), !!(val & BIT(30)),
		   (val >> 16) & 0x3fff, (val >> 14) & 0x3, val & 0x3fff);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG3, true);
	seq_printf(p, "%-10s isp pipeline group (0x%x)\n"
		   "\t   mge(%d %d) rawnr(%d %d) bay3d(%d %d) tmo(%d %d)\n"
		   "\t   gic(%d %d) dbr(%d %d) debayer(%d %d) dhaz(%d %d)\n"
		   "\t   lut3d(%d %d) ldch(%d %d) ynr(%d %d) shp(%d %d)\n"
		   "\t   cgc(%d %d) cac(%d %d) isp_out(%d %d) isp_in(%d %d)\n",
		   "DEBUG3", val,
		   !!(val & BIT(31)), !!(val & BIT(30)), !!(val & BIT(29)), !!(val & BIT(28)),
		   !!(val & BIT(27)), !!(val & BIT(26)), !!(val & BIT(25)), !!(val & BIT(24)),
		   !!(val & BIT(23)), !!(val & BIT(22)), !!(val & BIT(21)), !!(val & BIT(20)),
		   !!(val & BIT(19)), !!(val & BIT(18)), !!(val & BIT(17)), !!(val & BIT(16)),
		   !!(val & BIT(15)), !!(val & BIT(14)), !!(val & BIT(13)), !!(val & BIT(12)),
		   !!(val & BIT(11)), !!(val & BIT(10)), !!(val & BIT(9)), !!(val & BIT(8)),
		   !!(val & BIT(7)), !!(val & BIT(6)), !!(val & BIT(5)), !!(val & BIT(4)),
		   !!(val & BIT(3)), !!(val & BIT(2)), !!(val & BIT(1)), !!(val & BIT(0)));
}

static void isp30_unite_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	u32 v0, v1;

	v0 = rkisp_read(dev, ISP3X_CMSK_CTRL0, false);
	v1 = rkisp_next_read(dev, ISP3X_CMSK_CTRL0, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CMSK",
		   (v0 & 1) ? "ON" : "OFF",
		   v0, (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_DPCC0_MODE, false);
	v1 = rkisp_next_read(dev, ISP3X_DPCC0_MODE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "DPCC0",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_DPCC1_MODE, false);
	v1 = rkisp_next_read(dev, ISP3X_DPCC1_MODE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "DPCC1",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_DPCC2_MODE, false);
	v1 = rkisp_next_read(dev, ISP3X_DPCC2_MODE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "DPCC2",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_BLS_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_BLS_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "BLS",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	v1 = rkisp_next_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "SDG",
		   (v0 & BIT(6)) ? "ON" : "OFF", v0,
		   (v1 & BIT(6)) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_LSC_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_LSC_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "LSC",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	v1 = rkisp_next_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s Left %s(0x%x) gain:0x%08x 0x%08x, Right %s(0x%x) gain:0x%08x 0x%08x\n",
		   "AWBGAIN",
		   (v0 & BIT(7)) ? "ON" : "OFF", v0,
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_G, false),
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_RB, false),
		   (v1 & BIT(7)) ? "ON" : "OFF", v1,
		   rkisp_next_read(dev, ISP3X_ISP_AWB_GAIN0_G, false),
		   rkisp_next_read(dev, ISP3X_ISP_AWB_GAIN0_RB, false));
	v0 = rkisp_read(dev, ISP3X_DEBAYER_CONTROL, false);
	v1 = rkisp_next_read(dev, ISP3X_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "DEBAYER",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_CCM_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_CCM_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CCM",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_GAMMA_OUT_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "GAMMA_OUT",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_CPROC_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_CPROC_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CPROC",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_IMG_EFF_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x) effect:%s, Right %s(0x%x) effect:%s\n",
		   "IE",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   effect[(v0 & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1],
		   (v1 & 1) ? "ON" : "OFF", v1,
		   effect[(v1 & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	v0 = rkisp_read(dev, ISP3X_DRC_CTRL0, false);
	v1 = rkisp_next_read(dev, ISP3X_DRC_CTRL0, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "HDRDRC",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_HDRMGE_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "HDRMGE",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_BAYNR_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_BAYNR_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "BAYNR",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_BAY3D_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_BAY3D_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "BAY3D",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_YNR_GLOBAL_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_YNR_GLOBAL_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "YNR",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_CNR_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_CNR_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CNR",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_SHARP_EN, false);
	v1 = rkisp_next_read(dev, ISP3X_SHARP_EN, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "SHARP",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_GIC_CONTROL, false);
	v1 = rkisp_next_read(dev, ISP3X_GIC_CONTROL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "GIC",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_DHAZ_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_DHAZ_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "DHAZ",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_3DLUT_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_3DLUT_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "3DLUT",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_LDCH_STS, false);
	v1 = rkisp_next_read(dev, ISP3X_LDCH_STS, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "LDCH",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	v1 = rkisp_next_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CSM",
		   (v0 & full_range_flg) ? "FULL" : "LIMIT", v0,
		   (v1 & full_range_flg) ? "FULL" : "LIMIT", v1);
	v0 = rkisp_read(dev, ISP3X_CAC_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_CAC_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "CAC",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_GAIN_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_GAIN_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "GAIN",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAF_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAF_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAF",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAWB_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAWB",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAE_LITE_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAE0",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAE_BIG2_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAE1",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAE_BIG3_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAE_BIG3_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAE2",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWAE_BIG1_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWAE3",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWHIST_LITE_CTRL, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWHIST0",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWHIST_BIG2_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWHIST1",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWHIST_BIG3_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWHIST_BIG3_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWHIST2",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_RAWHIST_BIG1_BASE, false);
	v1 = rkisp_next_read(dev, ISP3X_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "RAWHIST3",
		   (v0 & 1) ? "ON" : "OFF", v0,
		   (v1 & 1) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_ISP_CTRL1, true);
	v1 = rkisp_next_read(dev, ISP3X_ISP_CTRL1, true);
	seq_printf(p, "%-10s Left %s(0x%x), Right %s(0x%x)\n",
		   "BigMode",
		   v0 & BIT(28) ? "ON" : "OFF", v0,
		   v1 & BIT(28) ? "ON" : "OFF", v1);
	v0 = rkisp_read(dev, ISP3X_ISP_DEBUG1, true);
	v1 = rkisp_next_read(dev, ISP3X_ISP_DEBUG1, true);
	seq_printf(p, "%-10s space full status group. Left:0x%x Right:0x%x\n"
		   "\t   ibuf2(L:0x%x R:0x%x) ibuf1(L:0x%x R:0x%x)\n"
		   "\t   ibuf0(L:0x%x R:0x%x) mpfbc_infifo(L:0x%x R:0x%x)\n"
		   "\t   r1fifo(L:0x%x R:0x%x) r0fifo(L:0x%x R:0x%x)\n"
		   "\t   outfifo(L:0x%x R:0x%x) lafifo(L:0x%x R:0x%x)\n",
		   "DEBUG1", v0, v1,
		   v0 >> 28, v1 >> 28, (v0 >> 24) & 0xf, (v1 >> 24) & 0xf,
		   (v0 >> 20) & 0xf, (v1 >> 20) & 0xf, (v0 >> 16) & 0xf, (v1 >> 16) & 0xf,
		   (v0 >> 12) & 0xf, (v1 >> 12) & 0xf, (v0 >> 8) & 0xf, (v1 >> 8) & 0xf,
		   (v0 >> 4) & 0xf, (v1 >> 4) & 0xf, v0 & 0xf, v1 & 0xf);
	v0 = rkisp_read(dev, ISP3X_ISP_DEBUG2, true);
	v1 = rkisp_next_read(dev, ISP3X_ISP_DEBUG2, true);
	seq_printf(p, "%-10s Left:0x%x Right:0x%x\n"
		   "\t   bay3d_fifo_full iir(L:%d R:%d) cur(L:%d R:%d)\n"
		   "\t   module outform vertical counter(L:%d R:%d), out frame counter:(L:%d R:%d)\n"
		   "\t   isp output line counter(L:%d R:%d)\n",
		   "DEBUG2", v0, v1,
		   !!(v0 & BIT(31)), !!(v1 & BIT(31)), !!(v0 & BIT(30)), !!(v1 & BIT(30)),
		   (v0 >> 16) & 0x3fff, (v1 >> 16) & 0x3fff, (v0 >> 14) & 0x3, (v1 >> 14) & 0x3,
		   v0 & 0x3fff, v1 & 0x3fff);
	v0 = rkisp_read(dev, ISP3X_ISP_DEBUG3, true);
	v1 = rkisp_next_read(dev, ISP3X_ISP_DEBUG3, true);
	seq_printf(p, "%-10s isp pipeline group Left:0x%x Right:0x%x\n"
		   "\t   mge(L:%d %d R:%d %d) rawnr(L:%d %d R:%d %d)\n"
		   "\t   bay3d(L:%d %d R:%d %d) tmo(L:%d %d R:%d %d)\n"
		   "\t   gic(L:%d %d R:%d %d) dbr(L:%d %d R:%d %d)\n"
		   "\t   debayer(L:%d %d R:%d %d) dhaz(L:%d %d R:%d %d)\n"
		   "\t   lut3d(L:%d %d R:%d %d) ldch(L:%d %d R:%d %d)\n"
		   "\t   ynr(L:%d %d R:%d %d) shp(L:%d %d R:%d %d)\n"
		   "\t   cgc(L:%d %d R:%d %d) cac(L:%d %d R:%d %d)\n"
		   "\t   isp_out(L:%d %d R:%d %d) isp_in(L:%d %d R:%d %d)\n",
		   "DEBUG3", v0, v1,
		   !!(v0 & BIT(31)), !!(v0 & BIT(30)), !!(v1 & BIT(31)), !!(v1 & BIT(30)),
		   !!(v0 & BIT(29)), !!(v0 & BIT(28)), !!(v1 & BIT(29)), !!(v1 & BIT(28)),
		   !!(v0 & BIT(27)), !!(v0 & BIT(26)), !!(v1 & BIT(27)), !!(v1 & BIT(26)),
		   !!(v0 & BIT(25)), !!(v0 & BIT(24)), !!(v1 & BIT(25)), !!(v1 & BIT(24)),
		   !!(v0 & BIT(23)), !!(v0 & BIT(22)), !!(v1 & BIT(23)), !!(v1 & BIT(22)),
		   !!(v0 & BIT(21)), !!(v0 & BIT(20)), !!(v1 & BIT(21)), !!(v1 & BIT(20)),
		   !!(v0 & BIT(19)), !!(v0 & BIT(18)), !!(v1 & BIT(19)), !!(v1 & BIT(18)),
		   !!(v0 & BIT(17)), !!(v0 & BIT(16)), !!(v1 & BIT(17)), !!(v1 & BIT(16)),
		   !!(v0 & BIT(15)), !!(v0 & BIT(14)), !!(v1 & BIT(15)), !!(v1 & BIT(14)),
		   !!(v0 & BIT(13)), !!(v0 & BIT(12)), !!(v1 & BIT(13)), !!(v1 & BIT(12)),
		   !!(v0 & BIT(11)), !!(v0 & BIT(10)), !!(v1 & BIT(11)), !!(v1 & BIT(10)),
		   !!(v0 & BIT(9)), !!(v0 & BIT(8)), !!(v1 & BIT(9)), !!(v1 & BIT(8)),
		   !!(v0 & BIT(7)), !!(v0 & BIT(6)), !!(v1 & BIT(7)), !!(v1 & BIT(6)),
		   !!(v0 & BIT(5)), !!(v0 & BIT(4)), !!(v1 & BIT(5)), !!(v1 & BIT(4)),
		   !!(v0 & BIT(3)), !!(v0 & BIT(2)), !!(v1 & BIT(3)), !!(v1 & BIT(2)),
		   !!(v0 & BIT(1)), !!(v0 & BIT(0)), !!(v1 & BIT(1)), !!(v1 & BIT(0)));
}

static void isp32_show(struct rkisp_device *dev, struct seq_file *p)
{
	u32 full_range_flg = CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA | CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA;
	static const char * const effect[] = {
		"BLACKWHITE",
		"NEGATIVE",
		"SEPIA",
		"COLOR_SEL",
		"EMBOSS",
		"SKETCH",
		"SHARPEN",
		"RKSHARPEN"
	};
	struct rkisp_isp_params_val_v32 *priv;
	u32 val, tmp;

	priv = (struct rkisp_isp_params_val_v32 *)dev->params_vdev.priv_val;

	seq_printf(p, "%-10s %s warp:%d\n", "ISP2ENC",
		   dev->cap_dev.wrap_line ? "online" : "offline",
		   dev->cap_dev.wrap_line);
	tmp = rkisp_read(dev, ISP32_MI_WR_VFLIP_CTRL, false);
	val = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	seq_printf(p, "%-10s mirror:%d flip(mp:%d sp:%d bp:%d mpds:%d bpds:%d)\n",
		   "MIR_FLIP", !!(val & BIT(5)),
		   !!(tmp & BIT(0)), !!(tmp & BIT(1)), !!(tmp & BIT(2)),
		   !!(tmp & BIT(4)), !!(tmp & BIT(5)));
	seq_printf(p, "%-10s %s(0x%x)\n", "SDG", (val & BIT(6)) ? "ON" : "OFF", val);
	seq_printf(p, "%-10s %s(0x%x) (gain0:0x%08x 0x%08x gain1:0x%x 0x%x)\n", "AWBGAIN",
		   (val & BIT(7)) ? "ON" : "OFF", val,
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_G, false),
		   rkisp_read(dev, ISP3X_ISP_AWB_GAIN0_RB, false),
		   rkisp_read(dev, ISP32_ISP_AWB1_GAIN_G, false),
		   rkisp_read(dev, ISP32_ISP_AWB1_GAIN_RB, false));
	val = rkisp_read(dev, ISP32_VSM_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "VSM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CMSK_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CMSK", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DPCC0_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DPCC1_MODE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DPCC1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BLS_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BLS", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_LSC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LSC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DEBAYER_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DEBAYER", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CCM_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CCM", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GAMMA_OUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAMMA_OUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CPROC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CPROC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_IMG_EFF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x) (effect: %s)\n", "IE", (val & 1) ? "ON" : "OFF", val,
		   effect[(val & CIF_IMG_EFF_CTRL_MODE_MASK) >> 1]);
	val = rkisp_read(dev, ISP3X_DRC_CTRL0, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRDRC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_HDRMGE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "HDRMGE", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BAYNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "BAYNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_BAY3D_CTRL, false);
	tmp = rkisp_read(dev, ISP32_BAY3D_CTRL1, false);
	seq_printf(p, "%-10s %s(0x%x 0x%x) bwsaving:%d mode:(%s %s)\n", "BAY3D",
		   (val & 1) ? "ON" : "OFF", val, tmp, !!(val & BIT(13)),
		   (tmp & BIT(4)) ? "lo4x4" : ((tmp & BIT(3)) ? "lo4x8" : "lo8x8"),
		   priv->is_sram ? "sram" : "ddr");
	val = rkisp_read(dev, ISP3X_YNR_GLOBAL_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "YNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_CNR_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CNR", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_SHARP_EN, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "SHARP", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GIC_CONTROL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GIC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_DHAZ_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "DHAZ", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_3DLUT_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "3DLUT", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_LDCH_STS, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "LDCH", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL0, false);
	tmp = rkisp_read(dev, ISP3X_ISP_CC_COEFF_0, false);
	seq_printf(p, "%-10s %s(0x%x), y_offs:0x%x c_offs:0x%x\n"
		   "\t   coeff Y:0x%x 0x%x 0x%x CB:0x%x 0x%x 0x%x CR:0x%x 0x%x 0x%x\n",
		   "CSM", (val & full_range_flg) ? "FULL" : "LIMIT", val,
		   (tmp >> 24) & 0x3f,
		   (tmp >> 16) & 0xff ? (tmp >> 16) & 0xff : 128,
		   tmp & 0x1ff,
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_1, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_2, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_3, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_4, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_5, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_6, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_7, false),
		   rkisp_read(dev, ISP3X_ISP_CC_COEFF_8, false));
	val = rkisp_read(dev, ISP3X_CAC_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "CAC", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_GAIN_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "GAIN", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAF_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAF", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAWB_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAWB", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWAE_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWAE3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_LITE_CTRL, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST0", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_BIG2_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST1", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_RAWHIST_BIG1_BASE, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "RAWHIST3", (val & 1) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_CTRL1, true);
	seq_printf(p, "%-10s %s(0x%x)\n", "BigMode", val & BIT(28) ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP32_BLS_ISP_OB_PREDGAIN, false);
	seq_printf(p, "%-10s %s(0x%x)\n", "OB", val ? "ON" : "OFF", val);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG1, true);
	seq_printf(p, "%-10s space full status group (0x%x)\n"
		   "\t   ibuf2:0x%x ibuf1:0x%x ibuf0:0x%x mpfbc_infifo:0x%x\n"
		   "\t   r1fifo:0x%x r0fifo:0x%x outfifo:0x%x lafifo:0x%x\n",
		   "DEBUG1", val,
		   val >> 28, (val >> 24) & 0xf, (val >> 20) & 0xf, (val >> 16) & 0xf,
		   (val >> 12) & 0xf, (val >> 8) & 0xf, (val >> 4) & 0xf, val & 0xf);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG2, true);
	seq_printf(p, "%-10s 0x%x\n"
		   "\t   bay3d_fifo_full iir:%d cur:%d\n"
		   "\t   module outform vertical counter:%d, out frame counter:%d\n"
		   "\t   isp output line counter:%d\n",
		   "DEBUG2", val, !!(val & BIT(31)), !!(val & BIT(30)),
		   (val >> 16) & 0x3fff, (val >> 14) & 0x3, val & 0x3fff);
	val = rkisp_read(dev, ISP3X_ISP_DEBUG3, true);
	seq_printf(p, "%-10s isp pipeline group (0x%x)\n"
		   "\t   mge(%d %d) rawnr(%d %d) bay3d(%d %d) tmo(%d %d)\n"
		   "\t   gic(%d %d) dbr(%d %d) debayer(%d %d) dhaz(%d %d)\n"
		   "\t   lut3d(%d %d) ldch(%d %d) ynr(%d %d) shp(%d %d)\n"
		   "\t   cgc(%d %d) cac(%d %d) isp_out(%d %d) isp_in(%d %d)\n",
		   "DEBUG3", val,
		   !!(val & BIT(31)), !!(val & BIT(30)), !!(val & BIT(29)), !!(val & BIT(28)),
		   !!(val & BIT(27)), !!(val & BIT(26)), !!(val & BIT(25)), !!(val & BIT(24)),
		   !!(val & BIT(23)), !!(val & BIT(22)), !!(val & BIT(21)), !!(val & BIT(20)),
		   !!(val & BIT(19)), !!(val & BIT(18)), !!(val & BIT(17)), !!(val & BIT(16)),
		   !!(val & BIT(15)), !!(val & BIT(14)), !!(val & BIT(13)), !!(val & BIT(12)),
		   !!(val & BIT(11)), !!(val & BIT(10)), !!(val & BIT(9)), !!(val & BIT(8)),
		   !!(val & BIT(7)), !!(val & BIT(6)), !!(val & BIT(5)), !!(val & BIT(4)),
		   !!(val & BIT(3)), !!(val & BIT(2)), !!(val & BIT(1)), !!(val & BIT(0)));
	val = rkisp_read(dev, ISP32_ISP_DEBUG4, true);
	seq_printf(p, "%-10s isp pipeline group (0x%x)\n"
		   "\t   expd(%d %d) ynr(%d %d)\n",
		   "DEBUG4", val,
		   !!(val & BIT(3)), !!(val & BIT(2)), !!(val & BIT(1)), !!(val & BIT(0)));
}

static int isp_show(struct seq_file *p, void *v)
{
	struct rkisp_device *dev = p->private;
	struct rkisp_isp_subdev *sdev = &dev->isp_sdev;
	struct rkisp_sensor_info *sensor = dev->active_sensor;
	u32 val = 0;

	seq_printf(p, "%-10s Version:v%02x.%02x.%02x\n",
		   dev->name,
		   RKISP_DRIVER_VERSION >> 16,
		   (RKISP_DRIVER_VERSION & 0xff00) >> 8,
		   RKISP_DRIVER_VERSION & 0x00ff);
	for (val = 0; val < dev->hw_dev->num_clks; val++) {
		seq_printf(p, "%-10s %ld\n",
			   dev->hw_dev->match_data->clks[val],
			   clk_get_rate(dev->hw_dev->clks[val]));
	}

	seq_printf(p, "%-10s Cnt:%d ErrCnt:%d\n",
		   "Interrupt",
		   dev->isp_isr_cnt,
		   dev->isp_err_cnt);

	if (sensor && sensor->fi.interval.numerator)
		val = sensor->fi.interval.denominator / sensor->fi.interval.numerator;
	seq_printf(p, "%-10s %s Format:%s Size:%dx%d@%dfps Offset(%d,%d)\n",
		   "Input",
		   sensor ? sensor->sd->name : NULL,
		   sdev->in_fmt.name,
		   sdev->in_crop.width, sdev->in_crop.height, val,
		   sdev->in_crop.left, sdev->in_crop.top);

	if (!(dev->isp_state & ISP_START))
		return 0;

	if (IS_HDR_RDBK(dev->hdr.op_mode)) {
		seq_printf(p, "%-10s mode:frame%d (frame:%d rate:%dms %s time:%dms frameloss:%d) cnt(total:%d X1:%d X2:%d X3:%d)\n",
			   "Isp Read",
			   dev->rd_mode - 3,
			   dev->dmarx_dev.cur_frame.id,
			   (u32)(dev->dmarx_dev.cur_frame.timestamp - dev->dmarx_dev.pre_frame.timestamp) / 1000 / 1000,
			   (dev->isp_state & ISP_FRAME_END) ? "idle" : "working",
			   sdev->dbg.interval / 1000 / 1000,
			   sdev->dbg.frameloss,
			   dev->rdbk_cnt,
			   dev->rdbk_cnt_x1,
			   dev->rdbk_cnt_x2,
			   dev->rdbk_cnt_x3);
		seq_printf(p, "\t   hw link:%d idle:%d vir(mode:%d index:%d)\n",
			   dev->hw_dev->dev_link_num, dev->hw_dev->is_idle,
			   dev->multi_mode, dev->multi_index);
	} else {
		seq_printf(p, "%-10s frame:%d %s time:%dms v-blank:%dus\n",
			   "Isp online",
			   sdev->dbg.id,
			   (dev->isp_state & ISP_FRAME_END) ? "idle" : "working",
			   sdev->dbg.interval / 1000 / 1000,
			   sdev->dbg.delay / 1000);
	}
	if (dev->br_dev.en)
		seq_printf(p, "%-10s rkispp%d Format:%s%s Size:%dx%d (frame:%d rate:%dms frameloss:%d)\n",
			   "Output",
			   dev->dev_id,
			   (dev->br_dev.work_mode & ISP_ISPP_FBC) ? "FBC" : "YUV",
			   (dev->br_dev.work_mode & ISP_ISPP_422) ? "422" : "420",
			   dev->br_dev.crop.width,
			   dev->br_dev.crop.height,
			   dev->br_dev.dbg.id,
			   dev->br_dev.dbg.interval / 1000 / 1000,
			   dev->br_dev.dbg.frameloss);
	for (val = 0; val < RKISP_MAX_STREAM; val++) {
		struct rkisp_stream *stream = &dev->cap_dev.stream[val];

		if (!stream->streaming)
			continue;
		seq_printf(p, "%-10s %s Format:%c%c%c%c Size:%dx%d Dcrop(%d,%d|%dx%d) (frame:%d rate:%dms delay:%dms frameloss:%d)\n",
			   "Output",
			   stream->vnode.vdev.name,
			   stream->out_fmt.pixelformat,
			   stream->out_fmt.pixelformat >> 8,
			   stream->out_fmt.pixelformat >> 16,
			   stream->out_fmt.pixelformat >> 24,
			   stream->out_fmt.width,
			   stream->out_fmt.height,
			   stream->dcrop.left,
			   stream->dcrop.top,
			   stream->dcrop.width,
			   stream->dcrop.height,
			   stream->dbg.id,
			   stream->dbg.interval / 1000 / 1000,
			   stream->dbg.delay / 1000 / 1000,
			   stream->dbg.frameloss);
	}

	switch (dev->isp_ver) {
	case ISP_V20:
		if (IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V20))
			isp20_show(dev, p);
		break;
	case ISP_V21:
		if (IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V21))
			isp21_show(dev, p);
		break;
	case ISP_V30:
		if (IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V30)) {
			if (dev->hw_dev->is_unite)
				isp30_unite_show(dev, p);
			else
				isp30_show(dev, p);
		}
		break;
	case ISP_V32:
		if (IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_ISP_VERSION_V32))
			isp32_show(dev, p);
		break;
	default:
		break;
	}

	seq_printf(p, "%-10s %s Cnt:%d\n\n",
		   "Monitor",
		   dev->hw_dev->monitor.is_en ? "ON" : "OFF",
		   dev->hw_dev->monitor.retry);
	seq_printf(p, "%-10s mode:0x%x\n",
		   "Debug", dev->procfs.mode);
	if (dev->procfs.mode & RKISP_PROCFS_DUMP_REG) {
		int ret, i;

		dev->procfs.is_fs_wait = true;
		ret = wait_event_timeout(dev->procfs.fs_wait,
					 !dev->procfs.is_fs_wait,
					 msecs_to_jiffies(1000));
		seq_printf(p, "****************HW REG*Ret:%d**************\n", ret);
		for (i = 0; i < ISP3X_RAWAWB_RAM_DATA_BASE; i += 16)
			seq_printf(p, "%04x:  %08x %08x %08x %08x\n", i,
				   rkisp_read(dev, i, true),
				   rkisp_read(dev, i + 4, true),
				   rkisp_read(dev, i + 8, true),
				   rkisp_read(dev, i + 12, true));
	}
	return 0;
}

static void rkisp_proc_dump_mem(struct rkisp_device *dev)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	void *iir_addr = NULL, *cur_addr = NULL, *ds_addr = NULL;
	u32 iir_size, cur_size, ds_size;
	struct file *fp = NULL;
	char file[256];

	if (!IS_ENABLED(CONFIG_NO_GKI))
		return;

	dev->procfs.is_fs_wait = true;
	wait_event_timeout(dev->procfs.fe_wait,
			   !dev->procfs.is_fe_wait,
			   msecs_to_jiffies(1000));

	if (dev->isp_ver == ISP_V30) {
		struct rkisp_isp_params_val_v3x *p = dev->params_vdev.priv_val;

		if (p->buf_3dnr_iir[0].mem_priv) {
			if (!p->buf_3dnr_iir[0].is_need_vaddr)
				p->buf_3dnr_iir[0].vaddr = g_ops->vaddr(p->buf_3dnr_iir[0].mem_priv);
			iir_addr = p->buf_3dnr_iir[0].vaddr;
			iir_size = p->buf_3dnr_iir[0].size;
		}
		if (p->buf_3dnr_cur[0].mem_priv) {
			if (!p->buf_3dnr_cur[0].is_need_vaddr)
				p->buf_3dnr_cur[0].vaddr = g_ops->vaddr(p->buf_3dnr_cur[0].mem_priv);
			cur_addr = p->buf_3dnr_cur[0].vaddr;
			cur_size = p->buf_3dnr_cur[0].size;
		}
		if (p->buf_3dnr_ds[0].mem_priv) {
			if (!p->buf_3dnr_ds[0].is_need_vaddr)
				p->buf_3dnr_ds[0].vaddr = g_ops->vaddr(p->buf_3dnr_ds[0].mem_priv);
			ds_addr = p->buf_3dnr_ds[0].vaddr;
			ds_size = p->buf_3dnr_ds[0].size;
		}
	}

	if (iir_addr) {
		snprintf(file, sizeof(file), "/tmp/%s_bay3d_iir", dev->name);
		fp = filp_open(file, O_RDWR | O_CREAT, 0644);
		if (IS_ERR(fp)) {
			dev_err(dev->dev, "open %s fail\n", file);
			return;
		}
		kernel_write(fp, iir_addr, iir_size, &fp->f_pos);
		filp_close(fp, NULL);
	}
	if (cur_addr) {
		snprintf(file, sizeof(file), "/tmp/%s_bay3d_cur", dev->name);
		fp = filp_open(file, O_RDWR | O_CREAT, 0644);
		if (IS_ERR(fp)) {
			dev_err(dev->dev, "open %s fail\n", file);
			return;
		}
		kernel_write(fp, cur_addr, cur_size, &fp->f_pos);
		filp_close(fp, NULL);
	}
	if (ds_addr) {
		snprintf(file, sizeof(file), "/tmp/%s_bay3d_ds", dev->name);
		fp = filp_open(file, O_RDWR | O_CREAT, 0644);
		if (IS_ERR(fp)) {
			dev_err(dev->dev, "open %s fail\n", file);
			return;
		}
		kernel_write(fp, ds_addr, ds_size, &fp->f_pos);
		filp_close(fp, NULL);
	}
}

static ssize_t rkisp_proc_write(struct file *file,
				const char __user *user_buf,
				size_t user_len, loff_t *pos)
{
	struct rkisp_device *dev = PDE_DATA(file_inode(file));
	char *tmp, *buf = vmalloc(user_len + 1);
	u32 val, reg;
	int ret;

	if (!buf)
		return -ENOMEM;
	if (copy_from_user(buf, user_buf, user_len) != 0) {
		vfree(buf);
		return -EFAULT;
	}
	if (buf[user_len - 1] == '\n')
		buf[user_len - 1] = 0;
	else
		buf[user_len] = 0;
	dev_info(dev->dev, "%s cnt:%zu %s\n", __func__, user_len, buf);
	tmp = strstr(buf, "mode=");
	if (tmp) {
		tmp += 5;
		ret = kstrtou32(tmp, 16, &val);
		if (ret)
			goto end;
		dev->procfs.mode = val;
		if (val & RKISP_PROCFS_DUMP_MEM)
			rkisp_proc_dump_mem(dev);
	} else if (dev->procfs.mode &
		   (RKISP_PROCFS_FIL_AIQ | RKISP_PROCFS_FIL_SW)) {
		char *p_reg, *p_val;

		p_reg = buf;
		tmp = strstr(p_reg, "=");
		while (tmp) {
			*tmp = '\0';
			ret = kstrtou32(p_reg, 16, &reg);
			if (ret)
				goto end;
			p_val = tmp + 1;
			tmp = strstr(p_val, " ");
			if (tmp) {
				*tmp = '\0';
				p_reg = tmp + 1;
			}
			ret = kstrtou32(p_val, 16, &val);
			if (ret)
				goto end;
			if (dev->procfs.mode & RKISP_PROCFS_FIL_SW)
				writel(val, dev->hw_dev->base_addr + reg);
			else
				rkisp_write(dev, reg, val, false);

			tmp = strstr(p_reg, "=");
		}
	}
end:
	vfree(buf);
	return user_len;
}

static int isp_open(struct inode *inode, struct file *file)
{
	struct rkisp_device *data = PDE_DATA(inode);

	return single_open(file, isp_show, data);
}

static const struct proc_ops ops = {
	.proc_open	= isp_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= rkisp_proc_write,
};

int rkisp_proc_init(struct rkisp_device *dev)
{
	memset(&dev->procfs, 0, sizeof(dev->procfs));
	dev->procfs.procfs = proc_create_data(dev->name, 0, NULL, &ops, dev);
	if (!dev->procfs.procfs)
		return -EINVAL;
	init_waitqueue_head(&dev->procfs.fs_wait);
	init_waitqueue_head(&dev->procfs.fe_wait);
	return 0;
}

void rkisp_proc_cleanup(struct rkisp_device *dev)
{
	if (dev->procfs.procfs)
		remove_proc_entry(dev->name, NULL);
	dev->procfs.procfs = NULL;
}

#endif /* CONFIG_PROC_FS */
