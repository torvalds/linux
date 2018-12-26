/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include "cif_isp10_regs.h"
#include "cif_isp10_isp.h"
#include "cif_isp10_pltfrm.h"
#include "cif_isp10.h"

#define CIFISP_MEAS_SEND_ALONE (CIF_ISP_AFM_FIN)

#define _GET_ 0
#define _SET_ 1
#define CIFISP_MODULE_EN(v, m)             ((v) |= (m))
#define CIFISP_MODULE_DIS(v, m)            ((v) &= ~(m))
#define CIFISP_MODULE_IS_EN(v, m)          (((v) & (m)) == (m))
#define CIFISP_MODULE_UPDATE(v, m)         ((v) |= (m))
#define CIFISP_MODULE_CLR_UPDATE(v, m)     ((v) &= ~(m))
#define CIFISP_MODULE_IS_UPDATE(v, m)      (((v) & (m)) == (m))

#define CIFISP_MODULE_UNACTIVE(v, m)		((v) |= (m))
#define CIFISP_MODULE_ACTIVE(v, m)		((v) &= ~(m))
#define CIFISP_MODULE_IS_UNACTIVE(v, m)	        (((v) & (m)) == (m))

/* Demosaic */
#define CIFISP_BDM_BYPASS_EN(val)          ((val) << 10)
/* HIST */
#define CIFISP_HIST_PREDIV_SET(val)        ((val) << 3)
#define CIFISP_HIST_WEIGHT_SET(v0, v1, v2, v3)	((v0) | ((v1) << 8)  |\
						((v2) << 16) | ((v3) << 24))
#define CIFISP_HIST_WINDOW_OFFSET_RESERVED (0xFFFFF000)
#define CIFISP_HIST_WINDOW_SIZE_RESERVED   (0xFFFFF800)
#define CIFISP_HIST_WEIGHT_RESERVED        (0xE0E0E0E0)
#define CIFISP_MAX_HIST_PREDIVIDER         (0x0000007F)
#define CIFISP_HIST_ROW_NUM                (5)
#define CIFISP_HIST_COLUMN_NUM             (5)
/* ISP Ctrl */
#define CIF_ISP_CTRL_ISP_GAMMA_IN_ENA       BIT(6)
#define CIF_ISP_CTRL_ISP_AWB_ENA            BIT(7)
#define CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA      BIT(11)
/* AWB */
#define CIFISP_AWB_GAIN_R_SET(val)   ((val) << 16)
#define CIFISP_AWB_GAIN_R_READ(val)  ((val) >> 16)
#define CIFISP_AWB_GAIN_B_READ(val)  ((val) & 0xFFFF)

#define CIFISP_AWB_YMAX_CMP_EN       BIT(2)
#define CIFISP_AWB_REF_CR_SET(val)   ((val) << 8)
#define CIFISP_AWB_REF_CR_READ(val)  ((val) >> 8)
#define CIFISP_AWB_REF_CB_READ(val)  ((val) & 0xFF)
#define CIFISP_AWB_MAX_CS_SET(val)   ((val) << 8)
#define CIFISP_AWB_MAX_CS_READ(val)  (((val) >> 8) & 0xFF)
#define CIFISP_AWB_MIN_C_READ(val)   ((val) & 0xFF)
#define CIFISP_AWB_MIN_Y_SET(val)    ((val) << 16)
#define CIFISP_AWB_MIN_Y_READ(val)   (((val) >> 16) & 0xFF)
#define CIFISP_AWB_MAX_Y_SET(val)    ((val) << 24)
#define CIFISP_AWB_MAX_Y_READ(val)   ((val) >> 24)
#define CIFISP_AWB_MODE_RGB_EN       ((1 << 31) | (0x02 << 0))
#define CIFISP_AWB_MODE_YCBCR_EN     ((0 << 31) | (0x02 << 0))
#define CIFISP_AWB_MODE_READ(val)    ((val) & 3)
#define CIFISP_AWB_YMAX_READ(val)    (((val) >> 2) & 1)

#define CIFISP_AWB_GET_MEAN_CR(val)   ((val) & 0xFF)
#define CIFISP_AWB_GET_MEAN_CB(val)   (((val) >> 8) & 0xFF)
#define CIFISP_AWB_GET_MEAN_Y(val)    (((val) >> 16) & 0xFF)
#define CIFISP_AWB_GET_MEAN_R(val)    ((val) & 0xFF)
#define CIFISP_AWB_GET_MEAN_B(val)    (((val) >> 8) & 0xFF)
#define CIFISP_AWB_GET_MEAN_G(val)    (((val) >> 16) & 0xFF)
#define CIFISP_AWB_GET_PIXEL_CNT(val) ((val) & 0x3FFFFFF)

#define CIFISP_AWB_GAINS_MAX_VAL           (0x000003FF)
#define CIFISP_AWB_WINDOW_OFFSET_MAX       (0x00000FFF)
#define CIFISP_AWB_WINDOW_MAX_SIZE         (0x00001FFF)
#define CIFISP_AWB_CBCR_MAX_REF            (0x000000FF)
#define CIFISP_AWB_THRES_MAX_YC            (0x000000FF)
/* AE */
#define CIFISP_EXP_ENA	                   (1)
#define CIFISP_EXP_DIS	                   (0)
#define CIFISP_EXP_ROW_NUM                 (5)
#define CIFISP_EXP_COLUMN_NUM              (5)
#define CIFISP_EXP_NUM_LUMA_REGS           (CIFISP_EXP_ROW_NUM *\
		CIFISP_EXP_COLUMN_NUM)
#define CIFISP_EXP_MAX_HOFFS               (2424)
#define CIFISP_EXP_MAX_VOFFS               (1806)
#define CIFISP_EXP_BLOCK_MAX_HSIZE         (516)
#define CIFISP_EXP_BLOCK_MIN_HSIZE         (35)
#define CIFISP_EXP_BLOCK_MAX_VSIZE         (390)
#define CIFISP_EXP_BLOCK_MIN_VSIZE         (28)
#define CIFISP_EXP_MAX_HSIZE	\
	(CIFISP_EXP_BLOCK_MAX_HSIZE * CIFISP_EXP_COLUMN_NUM + 1)
#define CIFISP_EXP_MIN_HSIZE	\
	(CIFISP_EXP_BLOCK_MIN_HSIZE * CIFISP_EXP_COLUMN_NUM + 1)
#define CIFISP_EXP_MAX_VSIZE	\
	(CIFISP_EXP_BLOCK_MAX_VSIZE * CIFISP_EXP_ROW_NUM + 1)
#define CIFISP_EXP_MIN_VSIZE	\
	(CIFISP_EXP_BLOCK_MIN_VSIZE * CIFISP_EXP_ROW_NUM + 1)
#define CIFISP_EXP_HEIGHT_MASK             (0x000007FF)
#define CIFISP_EXP_MAX_HOFFSET             (0x00000FFF)
#define CIFISP_EXP_MAX_VOFFSET             (0x00000FFF)

#define CIFISP_EXP_CTRL_AUTOSTOP(val)      ((val) << 1)
#define CIFISP_EXP_CTRL_MEASMODE(val)      ((val) << 31)
#define CIFISP_EXP_HSIZE(val)              ((val) & 0x7FF)
#define CIFISP_EXP_VSIZE(val)              ((val) & 0x7FE)
/* LSC */
#define CIFISP_LSC_GRADH_SET(val)          ((val) << 11)
#define CIFISP_LSC_SECTH_SET(val)          ((val) << 10)

/* FLT */
#define CIFISP_FLT_MODE_MAX	           (1)
#define CIFISP_FLT_CHROMA_MODE_MAX	   (3)
#define CIFISP_FLT_GREEN_STAGE1_MAX	   (8)
#define CIFISP_FLT_MODE(v)	           ((v) << 1)
#define CIFISP_FLT_CHROMA_V_MODE(v)	   ((v) << 4)
#define CIFISP_FLT_CHROMA_H_MODE(v)	   ((v) << 6)
#define CIFISP_FLT_GREEN_STAGE1(v)	   ((v) << 8)
#define CIFISP_FLT_THREAD_RESERVED	   (0xfffffc00)
#define CIFISP_FLT_FAC_RESERVED	       (0xffffffc0)
#define CIFISP_FLT_LUM_WEIGHT_RESERVED (0xfff80000)
#define CIFISP_FLT_ENA	               (1)
#define CIFISP_FLT_DIS                 (0)

#define CIFISP_CTK_COEFF_RESERVED      0xFFFFF800
#define CIFISP_XTALK_OFFSET_RESERVED   0xFFFFF000

/* GOC */
#define CIFISP_GOC_MODE_MAX            (1)
#define CIFISP_GOC_RESERVED            0xFFFFF800
#define CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA_READ(value) (((value) >> 11) & 1)
/* DPCC */
#define CIFISP_DPCC_ENA                BIT(0)
#define CIFISP_DPCC_DIS                (0 << 0)
#define CIFISP_DPCC_MODE_MAX           (0x07)
#define CIFISP_DPCC_OUTPUTMODE_MAX     (0x0f)
#define CIFISP_DPCC_SETUSE_MAX         (0x0f)
#define CIFISP_DPCC_METHODS_SET_RESERVED    (0xFFFFE000)
#define CIFISP_DPCC_LINE_THRESH_RESERVED    (0xFFFF0000)
#define CIFISP_DPCC_LINE_MAD_FAC_RESERVED   (0xFFFFC0C0)
#define CIFISP_DPCC_PG_FAC_RESERVED         (0xFFFFC0C0)
#define CIFISP_DPCC_RND_THRESH_RESERVED     (0xFFFF0000)
#define CIFISP_DPCC_RG_FAC_RESERVED         (0xFFFFC0C0)
#define CIFISP_DPCC_RO_LIMIT_RESERVED       (0xFFFFF000)
#define CIFISP_DPCC_RND_OFFS_RESERVED       (0xFFFFF000)
/* BLS */
#define CIFISP_BLS_ENA           BIT(0)
#define CIFISP_BLS_DIS           (0 << 0)
#define CIFISP_BLS_MODE_MEASURED BIT(1)
#define CIFISP_BLS_MODE_FIXED    (0 << 1)
#define CIFISP_BLS_WINDOW_1      BIT(2)
#define CIFISP_BLS_WINDOW_2      BIT(3)
/* GAMMA-IN */
#define CIFISP_DEGAMMA_X_RESERVED	\
	((1 << 31) | (1 << 27) | (1 << 23) | (1 << 19) |\
	(1 << 15) | (1 << 11) | (1 << 7) | (1 << 3))
#define CIFISP_DEGAMMA_Y_RESERVED          0xFFFFF000
/*CPROC*/
#define CIFISP_CPROC_CTRL_RESERVED         0xFFFFFFFE
#define CIFISP_CPROC_CONTRAST_RESERVED     0xFFFFFF00
#define CIFISP_CPROC_BRIGHTNESS_RESERVED   0xFFFFFF00
#define CIFISP_CPROC_HUE_RESERVED          0xFFFFFF00
#define CIFISP_CPROC_SATURATION_RESERVED   0xFFFFFF00
#define CIFISP_CPROC_MACC_RESERVED         0xE000E000
#define CIFISP_CPROC_TONE_RESERVED         0xF000
#define CIFISP_CPROC_TONE_Y(value)         ((value) << 16)
#define CIFISP_CPROC_TONE_C(value)         ((value))
#define CIFISP_CPROC_TONE_Y_READ(value)    ((value) >> 16)
#define CIFISP_CPROC_TONE_C_READ(value)    ((value) & 0xFFFF)
#define CIFISP_CPROC_EN                    1
#define CIFISP_CPROC_MACC_EN               BIT(4)
#define CIFISP_CPROC_TMAP_EN               BIT(5)
/* LSC */
#define CIFISP_LSC_SECT_SIZE_RESERVED      0xFC00FC00
#define CIFISP_LSC_GRAD_RESERVED           0xF000F000
#define CIFISP_LSC_SAMPLE_RESERVED         0xF000F000
#define CIFISP_LSC_SECTORS_MAX             16
#define CIFISP_LSC_TABLE_DATA(v0, v1)     (v0 | ((v1) << 12))
#define CIFISP_LSC_SECT_SIZE(v0, v1)      (v0 | ((v1) << 16))
#define CIFISP_LSC_GRAD_SIZE(v0, v1)      (v0 | ((v1) << 16))
/* AFC */
#define CIFISP_AFC_THRES_RESERVED     0xFFFF0000
#define CIFISP_AFC_VAR_SHIFT_RESERVED 0xFFF8FFF8
#define CIFISP_AFC_WINDOW_X_RESERVED  0xE000
#define CIFISP_AFC_WINDOW_Y_RESERVED  0xF000
#define CIFISP_AFC_WINDOW_X_MIN       0x5
#define CIFISP_AFC_WINDOW_Y_MIN       0x2
#define CIFISP_AFC_WINDOW_X(value)    ((value) << 16)
#define CIFISP_AFC_WINDOW_Y(value)    (value)
#define CIFISP_AFC_ENA                (1)
#define CIFISP_AFC_DIS                (0)

/* DPF */
#define CIFISP_DPF_NF_GAIN_RESERVED     0xFFFFF000
#define CIFISP_DPF_SPATIAL_COEFF_MAX    0x1f
#define CIFISP_DPF_NLL_COEFF_N_MAX      0x3ff

#define CIFISP_DPF_MODE_USE_NF_GAIN     BIT(9)
#define CIFISP_DPF_MODE_LSC_GAIN_COMP   BIT(8)
#define CIFISP_DPF_MODE_AWB_GAIN_COMP   BIT(7)
#define CIFISP_DPF_MODE_NLL_SEGMENTATION(a)   ((a) << 6)
#define CIFISP_DPF_MODE_RB_FLTSIZE(a)         ((a) << 5)
#define CIFISP_DPF_MODE_R_FLT_DIS             BIT(4)
#define CIFISP_DPF_MODE_R_FLT_EN              (0 << 4)
#define CIFISP_DPF_MODE_GR_FLT_DIS            BIT(3)
#define CIFISP_DPF_MODE_GR_FLT_EN             (0 << 3)
#define CIFISP_DPF_MODE_GB_FLT_DIS            BIT(2)
#define CIFISP_DPF_MODE_GB_FLT_EN             (0 << 2)
#define CIFISP_DPF_MODE_B_FLT_DIS             BIT(1)
#define CIFISP_DPF_MODE_B_FLT_EN              (0 << 1)
#define CIFISP_DPF_MODE_EN                    BIT(0)

#define CIFISP_DEBUG                        BIT(0)
#define CIFISP_ERROR                        BIT(1)

/*
 * Empirical rough (relative) times it takes to perform
 * given function.
 */
#define CIFISP_MODULE_DPCC_PROC_TIME     3
#define CIFISP_MODULE_BLS_PROC_TIME      10
#define CIFISP_MODULE_LSC_PROC_TIME      1747
#define CIFISP_MODULE_FLT_PROC_TIME      15
#define CIFISP_MODULE_BDM_PROC_TIME      1
#define CIFISP_MODULE_SDG_PROC_TIME      53
#define CIFISP_MODULE_GOC_PROC_TIME      1000
#define CIFISP_MODULE_CTK_PROC_TIME      772
#define CIFISP_MODULE_AWB_PROC_TIME      8
#define CIFISP_MODULE_HST_PROC_TIME      5
#define CIFISP_MODULE_AEC_PROC_TIME      5
#define CIFISP_MODULE_AWB_GAIN_PROC_TIME 2
#define CIFISP_MODULE_CPROC_PROC_TIME    5
#define CIFISP_MODULE_AFC_PROC_TIME      8
#define CIFISP_MODULE_IE_PROC_TIME       5
#define CIFISP_MODULE_DPF_TIME           5
#define CIFISP_MODULE_DPF_STRENGTH_TIME  2
#define CIFISP_MODULE_CSM_PROC_TIME      8

/* For Debugging only!!! */

#define CIFISP_MODULE_DEFAULT_VBLANKING_TIME 2000

#define V4L2_DEV_DEBUG_LEVEL 0

#define CIFISP_DPRINT(level, fmt, arg...) \
	do { \
		if (level == CIFISP_ERROR) \
			pr_err(fmt, ##arg); \
		else \
			pr_debug(fmt, ##arg); \
	} while (0)

#define cifisp_iowrite32(d, a) \
	cif_isp10_pltfrm_write_reg(NULL, (u32)(d), isp_dev->base_addr + (a))
#define cifisp_ioread32(a) \
	cif_isp10_pltfrm_read_reg(NULL, isp_dev->base_addr + (a))
#define cifisp_iowrite32OR(d, a) \
	cif_isp10_pltfrm_write_reg_OR(NULL, (u32)(d), isp_dev->base_addr + (a))
#define cifisp_iowrite32AND(d, a) \
	cif_isp10_pltfrm_write_reg_AND(NULL, (u32)(d), isp_dev->base_addr + (a))

/*
 * Set this flag to enable CIF ISP Register debug
 * #define CIFISP_DEBUG_REG
 */
/*
 * Set this flag to dump the parameters
 * #define CIFISP_DEBUG_PARAM
 */
/*
 * Set this flag to trace the capture params
 * #define LOG_CAPTURE_PARAMS
 */
/*
 * Set this flag to trace the isr execution time
 * #define LOG_ISR_EXE_TIME
 */
/*
 * Set this flag to exclude everything except
 * measurements
 * #define CIFISP_DEBUG_DISABLE_BLOCKS
 */

#ifdef LOG_CAPTURE_PARAMS
static struct cifisp_last_capture_config g_last_capture_config;
#endif

#ifdef LOG_ISR_EXE_TIME
static unsigned int g_longest_isr_time;
#endif

/* Functions for Debugging */
static void cifisp_param_dump(const void *config, unsigned int module);
#ifdef CIFISP_DEBUG_REG
static void cifisp_reg_dump(const struct cif_isp10_isp_dev *isp_dev,
			    unsigned int module, int level);
#endif
#ifdef LOG_CAPTURE_PARAMS
static void cifisp_reg_dump_capture(const struct cif_isp10_isp_dev *isp_dev);
#endif

static bool cifisp_isp_isr_other_config(
	struct cif_isp10_isp_dev *isp_dev,
	unsigned int *time_left);

static bool cifisp_isp_isr_meas_config(
	struct cif_isp10_isp_dev *isp_dev,
	unsigned int *time_left);

static struct cif_isp10_buffer *to_cif_isp10_vb(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct cif_isp10_buffer, vb);
}

static int cifisp_module_enable(struct cif_isp10_isp_dev *isp_dev,
				bool flag, __s32 *value, unsigned int module)
{
	unsigned int curr_id, new_id;
	unsigned int *updates, *curr_ens, *new_ens, *actives;
	unsigned long lock_flags = 0;

	if (module >= CIFISP_MODULE_MAX)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	if (module >= CIFISP_MEAS_ID) {
		curr_id = isp_dev->meas_cfgs.log[module].curr_id;
		new_id = isp_dev->meas_cfgs.log[module].new_id;
		updates = &isp_dev->meas_cfgs.module_updates;
		actives = &isp_dev->meas_cfgs.module_actives;
		curr_ens = &isp_dev->meas_cfgs.cfgs[curr_id].module_ens;
		new_ens = &isp_dev->meas_cfgs.cfgs[new_id].module_ens;
	} else {
		curr_id = isp_dev->other_cfgs.log[module].curr_id;
		new_id = isp_dev->other_cfgs.log[module].new_id;
		updates = &isp_dev->other_cfgs.module_updates;
		actives = &isp_dev->other_cfgs.module_actives;
		curr_ens = &isp_dev->other_cfgs.cfgs[curr_id].module_ens;
		new_ens = &isp_dev->other_cfgs.cfgs[new_id].module_ens;
	}

	if (flag == _GET_) {
		*value = CIFISP_MODULE_IS_EN(
			*curr_ens, (1 << module));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(*actives, (1 << module)) && *value)
		goto end;

	if ((CIFISP_MODULE_IS_EN(*curr_ens, (1 << module)) != *value) ||
	    (CIFISP_MODULE_IS_UPDATE(*updates, (1 << module)))) {
		if (*value)
			CIFISP_MODULE_EN(*new_ens, (1 << module));
		else
			CIFISP_MODULE_DIS(*new_ens, (1 << module));
		CIFISP_MODULE_UPDATE(*updates, (1 << module));
	}

end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return 0;
}

/* ISP BP interface function */
static int cifisp_dpcc_param(struct cif_isp10_isp_dev *isp_dev,
			     bool flag, struct cifisp_dpcc_config *arg)
{
	unsigned long lock_flags = 0;
	unsigned int i;
	struct cifisp_dpcc_methods_config *method;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_dpcc_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "arg is NULL: %s\n", __func__);

		return -EINVAL;
	}

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_DPCC_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].dpcc_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].dpcc_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_DPCC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_DPCC);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_DPCC) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->mode > CIFISP_DPCC_MODE_MAX ||
		arg->output_mode > CIFISP_DPCC_OUTPUTMODE_MAX ||
		arg->set_use > CIFISP_DPCC_SETUSE_MAX ||
		arg->ro_limits & CIFISP_DPCC_RO_LIMIT_RESERVED ||
		arg->rnd_offs & CIFISP_DPCC_RND_OFFS_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	method = &arg->methods[0];
	for (i = 0; i < CIFISP_DPCC_METHODS_MAX; i++) {
		if ((method->method &
		    CIFISP_DPCC_METHODS_SET_RESERVED) ||
		    (method->line_thresh &
		    CIFISP_DPCC_LINE_THRESH_RESERVED) ||
		    (method->line_mad_fac &
		    CIFISP_DPCC_LINE_MAD_FAC_RESERVED) ||
		    (method->pg_fac &
		    CIFISP_DPCC_PG_FAC_RESERVED) ||
		    (method->rnd_thresh &
		    CIFISP_DPCC_RND_THRESH_RESERVED) ||
		    (method->rg_fac & CIFISP_DPCC_RG_FAC_RESERVED)) {
			CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
			retval = -EINVAL;
			goto end;
		}
		method++;
	}

	memcpy(new_cfg, arg, sizeof(struct cifisp_dpcc_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_DPCC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP black level subtraction interface function */
static int cifisp_bls_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_bls_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_bls_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_BLS_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].bls_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].bls_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_BLS)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_BLS);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_BLS) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->bls_window1.h_offs > CIFISP_BLS_START_H_MAX ||
	    arg->bls_window1.h_size > CIFISP_BLS_STOP_H_MAX ||
	    arg->bls_window1.v_offs > CIFISP_BLS_START_V_MAX ||
	    arg->bls_window1.v_size > CIFISP_BLS_STOP_V_MAX ||
	    arg->bls_window2.h_offs > CIFISP_BLS_START_H_MAX ||
	    arg->bls_window2.h_size > CIFISP_BLS_STOP_H_MAX ||
	    arg->bls_window2.v_offs > CIFISP_BLS_START_V_MAX ||
	    arg->bls_window2.v_size > CIFISP_BLS_STOP_V_MAX ||
	    arg->bls_samples > CIFISP_BLS_SAMPLES_MAX ||
	    arg->fixed_val.r > CIFISP_BLS_FIX_SUB_MAX ||
	    arg->fixed_val.gr > CIFISP_BLS_FIX_SUB_MAX ||
	    arg->fixed_val.gb > CIFISP_BLS_FIX_SUB_MAX ||
	    arg->fixed_val.b > CIFISP_BLS_FIX_SUB_MAX ||
	    arg->fixed_val.r < (s16)CIFISP_BLS_FIX_SUB_MIN ||
	    arg->fixed_val.gr < (s16)CIFISP_BLS_FIX_SUB_MIN ||
	    arg->fixed_val.gb < (s16)CIFISP_BLS_FIX_SUB_MIN ||
	    arg->fixed_val.b < (s16)CIFISP_BLS_FIX_SUB_MIN) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_bls_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_BLS);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP LS correction interface function */
static int cifisp_lsc_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_lsc_config *arg)
{
	int i;
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_lsc_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_LSC_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].lsc_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].lsc_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_LSC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_LSC);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_LSC) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	for (i = 0; i < CIFISP_LSC_SIZE_TBL_SIZE; i++) {
		if ((*(arg->x_size_tbl + i) & CIFISP_LSC_SECT_SIZE_RESERVED) ||
		    (*(arg->y_size_tbl + i) & CIFISP_LSC_SECT_SIZE_RESERVED)) {
			CIFISP_DPRINT(CIFISP_ERROR,
				      "incompatible sect size x 0x%x y 0x%x in function: %s\n",
				      *(arg->x_size_tbl + i),
				      *(arg->y_size_tbl + i), __func__);
			retval = -EINVAL;
			goto end;
		}
	}

	for (i = 0; i < CIFISP_LSC_GRAD_TBL_SIZE; i++) {
		if ((*(arg->x_grad_tbl + i) & CIFISP_LSC_GRAD_RESERVED) ||
		    (*(arg->y_grad_tbl + i) & CIFISP_LSC_GRAD_RESERVED)) {
			CIFISP_DPRINT(CIFISP_ERROR,
				      "incompatible grad x 0x%x y 0x%xin function: %s\n",
				      *(arg->x_grad_tbl + i),
				      *(arg->y_grad_tbl + i), __func__);
			retval = -EINVAL;
			goto end;
		}
	}

	for (i = 0; i < CIFISP_LSC_DATA_TBL_SIZE; i++) {
		if ((*(arg->r_data_tbl + i) &
			CIFISP_LSC_SAMPLE_RESERVED) ||
			(*(arg->gr_data_tbl + i) &
			CIFISP_LSC_SAMPLE_RESERVED) ||
			(*(arg->gb_data_tbl + i) &
			CIFISP_LSC_SAMPLE_RESERVED) ||
			(*(arg->b_data_tbl + i) &
			CIFISP_LSC_SAMPLE_RESERVED)) {
			CIFISP_DPRINT(CIFISP_ERROR,
				      "incompatible sample r 0x%x gr 0x%x gb 0x%x b 0x%x in function: %s\n",
				      *(arg->r_data_tbl + i),
				      *(arg->gr_data_tbl + i),
				      *(arg->gb_data_tbl + i),
				      *(arg->b_data_tbl + i), __func__);
			retval = -EINVAL;
			goto end;
		}
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_lsc_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_LSC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP Filtering function */
static int cifisp_flt_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_flt_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_flt_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_FLT_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].flt_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].flt_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_FLT)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_FLT);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_FLT) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	/* Parameter check */
	if (arg->mode > CIFISP_FLT_MODE_MAX ||
	    arg->grn_stage1 > CIFISP_FLT_GREEN_STAGE1_MAX ||
	    arg->chr_v_mode > CIFISP_FLT_CHROMA_MODE_MAX ||
	    arg->chr_h_mode > CIFISP_FLT_CHROMA_MODE_MAX ||
	    arg->thresh_sh0 & CIFISP_FLT_THREAD_RESERVED ||
	    arg->thresh_sh1 & CIFISP_FLT_THREAD_RESERVED ||
	    arg->thresh_bl0 & CIFISP_FLT_THREAD_RESERVED ||
	    arg->thresh_bl1 & CIFISP_FLT_THREAD_RESERVED ||
	    arg->fac_bl0 & CIFISP_FLT_FAC_RESERVED ||
	    arg->fac_bl1 & CIFISP_FLT_FAC_RESERVED ||
	    arg->fac_sh0 & CIFISP_FLT_FAC_RESERVED ||
	    arg->fac_sh1 & CIFISP_FLT_FAC_RESERVED ||
	    arg->fac_mid & CIFISP_FLT_FAC_RESERVED ||
	    arg->lum_weight & CIFISP_FLT_LUM_WEIGHT_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_flt_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_FLT);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP demosaic interface function */
static int cifisp_bdm_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_bdm_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_bdm_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_BDM_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].bdm_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].bdm_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_BDM)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_BDM);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_BDM) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_bdm_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_BDM);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP GAMMA correction interface function */
static int cifisp_sdg_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_sdg_config *arg)
{
	unsigned long lock_flags = 0;
	unsigned int i;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_sdg_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_SDG_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].sdg_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].sdg_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_SDG)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_SDG);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_SDG) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->xa_pnts.gamma_dx0 & CIFISP_DEGAMMA_X_RESERVED ||
	    arg->xa_pnts.gamma_dx1 & CIFISP_DEGAMMA_X_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
		if ((arg->curve_b.gamma_y[i] & CIFISP_DEGAMMA_Y_RESERVED) ||
		    (arg->curve_r.gamma_y[i] & CIFISP_DEGAMMA_Y_RESERVED) ||
		    (arg->curve_g.gamma_y[i] & CIFISP_DEGAMMA_Y_RESERVED)) {
			CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
			retval = -EINVAL;
			goto end;
		}
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_sdg_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_SDG);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP GAMMA correction interface function */
static int cifisp_goc_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_goc_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_goc_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_GOC_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].goc_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].goc_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_GOC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_GOC);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_GOC) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->mode > CIFISP_GOC_MODE_MAX) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param 0x%x in  function: %s\n",
			      arg->mode, __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_goc_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_GOC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP Cross Talk */
static int cifisp_ctk_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_ctk_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_ctk_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_CTK_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].ctk_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].ctk_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_CTK)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_CTK);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_CTK) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	/* Perform parameter check */
	if (arg->coeff0 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff1 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff2 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff3 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff4 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff5 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff6 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff7 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->coeff8 & CIFISP_CTK_COEFF_RESERVED ||
	    arg->ct_offset_r & CIFISP_XTALK_OFFSET_RESERVED ||
	    arg->ct_offset_g & CIFISP_XTALK_OFFSET_RESERVED ||
	    arg->ct_offset_b & CIFISP_XTALK_OFFSET_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_ctk_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_CTK);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP White Balance Mode */
static int cifisp_awb_meas_param(struct cif_isp10_isp_dev *isp_dev,
				 bool flag, struct cifisp_awb_meas_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_awb_meas_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->meas_cfgs.log[CIFISP_AWB_ID];
	curr_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].awb_meas_config;
	new_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->new_id].awb_meas_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->meas_cfgs.module_actives,
		CIFISP_MODULE_AWB)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_AWB);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_AWB) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->awb_mode > CIFISP_AWB_MODE_YCBCR ||
	    arg->awb_wnd.h_offs > CIFISP_AWB_WINDOW_OFFSET_MAX ||
	    arg->awb_wnd.v_offs > CIFISP_AWB_WINDOW_OFFSET_MAX ||
	    arg->awb_wnd.h_size > CIFISP_AWB_WINDOW_MAX_SIZE ||
	    arg->awb_wnd.v_size > CIFISP_AWB_WINDOW_MAX_SIZE ||
	    arg->frames > CIFISP_AWB_MAX_FRAMES) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_awb_meas_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->meas_cfgs.module_updates,
		CIFISP_MODULE_AWB);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_awb_gain_param(struct cif_isp10_isp_dev *isp_dev,
				 bool flag, struct cifisp_awb_gain_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_awb_gain_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_AWB_GAIN_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].awb_gain_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].awb_gain_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_AWB_GAIN)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_AWB_GAIN);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_AWB_GAIN) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->gain_red > CIFISP_AWB_GAINS_MAX_VAL ||
	    arg->gain_green_r > CIFISP_AWB_GAINS_MAX_VAL ||
	    arg->gain_green_b > CIFISP_AWB_GAINS_MAX_VAL ||
	    arg->gain_blue > CIFISP_AWB_GAINS_MAX_VAL) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_awb_gain_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_AWB_GAIN);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_aec_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_aec_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_aec_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->meas_cfgs.log[CIFISP_AEC_ID];
	curr_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].aec_config;
	new_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->new_id].aec_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->meas_cfgs.module_actives,
		CIFISP_MODULE_AEC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_AEC);

	if (CIFISP_MODULE_IS_EN(
	    isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].module_ens,
	    CIFISP_MODULE_AEC) &&
	    memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_aec_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->meas_cfgs.module_updates,
		CIFISP_MODULE_AEC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_cproc_param(struct cif_isp10_isp_dev *isp_dev,
			      bool flag, struct cifisp_cproc_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_cproc_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_CPROC_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].cproc_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].cproc_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_CPROC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_CPROC);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_CPROC) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->c_out_range & CIFISP_CPROC_CTRL_RESERVED ||
	    arg->y_out_range & CIFISP_CPROC_CTRL_RESERVED ||
	    arg->y_in_range & CIFISP_CPROC_CTRL_RESERVED ||
	    arg->contrast & CIFISP_CPROC_CONTRAST_RESERVED ||
	    arg->brightness & CIFISP_CPROC_BRIGHTNESS_RESERVED ||
	    arg->sat & CIFISP_CPROC_SATURATION_RESERVED ||
	    arg->hue & CIFISP_CPROC_HUE_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_cproc_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_CPROC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_hst_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_hst_config *arg)
{
	unsigned long lock_flags = 0;
	unsigned int i;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_hst_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->meas_cfgs.log[CIFISP_HST_ID];
	curr_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].hst_config;
	new_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->new_id].hst_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->meas_cfgs.module_actives,
		CIFISP_MODULE_HST)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	if (CIFISP_MODULE_IS_EN(
		isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_HST) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->mode > CIFISP_HISTOGRAM_MODE_Y_HISTOGRAM ||
	    arg->histogram_predivider > CIFISP_MAX_HIST_PREDIVIDER ||
	    arg->meas_window.v_offs & CIFISP_HIST_WINDOW_OFFSET_RESERVED ||
	    arg->meas_window.h_offs & CIFISP_HIST_WINDOW_OFFSET_RESERVED ||
	    (arg->meas_window.v_size / (CIFISP_HIST_ROW_NUM - 1)) &
		CIFISP_HIST_WINDOW_SIZE_RESERVED ||
	    (arg->meas_window.h_size / (CIFISP_HIST_COLUMN_NUM - 1)) &
		CIFISP_HIST_WINDOW_SIZE_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			"incompatible param in function: %s line: %d\n",
			__func__, __LINE__);
		retval = -EINVAL;
		goto end;
	}

	for (i = 0; i < CIFISP_HISTOGRAM_WEIGHT_GRIDS_SIZE; i++) {
		if (arg->hist_weight[i] & CIFISP_HIST_WEIGHT_RESERVED) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"incompatible param in function: %s line: %d\n",
				__func__, __LINE__);
			retval = -EINVAL;
			goto end;
		}
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_hst_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->meas_cfgs.module_updates,
		CIFISP_MODULE_HST);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_afc_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_afc_config *arg)
{
	unsigned long lock_flags = 0;
	int i;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_afc_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->meas_cfgs.log[CIFISP_AFC_ID];
	curr_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].afc_config;
	new_cfg = &isp_dev->meas_cfgs.cfgs[cfg_log->new_id].afc_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->meas_cfgs.module_actives,
		CIFISP_MODULE_AFC)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_AFC);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->meas_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_AFC) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->num_afm_win > CIFISP_AFM_MAX_WINDOWS ||
	    arg->thres & CIFISP_AFC_THRES_RESERVED ||
	    arg->var_shift & CIFISP_AFC_VAR_SHIFT_RESERVED) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	for (i = 0; i < arg->num_afm_win; i++) {
		if (arg->afm_win[i].h_offs & CIFISP_AFC_WINDOW_X_RESERVED ||
		    arg->afm_win[i].h_offs < CIFISP_AFC_WINDOW_X_MIN ||
		    arg->afm_win[i].v_offs & CIFISP_AFC_WINDOW_Y_RESERVED ||
		    arg->afm_win[i].v_offs < CIFISP_AFC_WINDOW_Y_MIN ||
		    arg->afm_win[i].h_size & CIFISP_AFC_WINDOW_X_RESERVED ||
		    arg->afm_win[i].v_size & CIFISP_AFC_WINDOW_Y_RESERVED) {
			CIFISP_DPRINT(CIFISP_ERROR,
				      "incompatible param in function: %s\n",
				      __func__);
			retval = -EINVAL;
			goto end;
		}
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_afc_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->meas_cfgs.module_updates,
		CIFISP_MODULE_AFC);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_ie_param(struct cif_isp10_isp_dev *isp_dev,
			   bool flag, struct cifisp_ie_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_ie_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_IE_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].ie_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].ie_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_IE)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_IE);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_IE) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	if (arg->effect != V4L2_COLORFX_NONE &&
	    arg->effect != V4L2_COLORFX_BW &&
	    arg->effect != V4L2_COLORFX_SEPIA &&
	    arg->effect != V4L2_COLORFX_NEGATIVE &&
	    arg->effect != V4L2_COLORFX_EMBOSS &&
	    arg->effect != V4L2_COLORFX_SKETCH &&
	    arg->effect != V4L2_COLORFX_AQUA &&
	    arg->effect != V4L2_COLORFX_SET_CBCR) {
		CIFISP_DPRINT(CIFISP_ERROR,
			      "incompatible param in function: %s\n", __func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_ie_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_IE);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

/* ISP De-noise Pre-Filter(DPF) function */
static int cifisp_dpf_param(struct cif_isp10_isp_dev *isp_dev,
			    bool flag, struct cifisp_dpf_config *arg)
{
	unsigned long lock_flags = 0;
	unsigned int i;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_dpf_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_DPF_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].dpf_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].dpf_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_DPF)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_DPF);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_DPF) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	/* Parameter check */
	if ((arg->gain.mode >= CIFISP_DPF_GAIN_USAGE_MAX) ||
	    (arg->gain.mode < CIFISP_DPF_GAIN_USAGE_DISABLED) ||
	    (arg->gain.nf_b_gain & CIFISP_DPF_NF_GAIN_RESERVED) ||
	    (arg->gain.nf_r_gain & CIFISP_DPF_NF_GAIN_RESERVED) ||
	    (arg->gain.nf_gr_gain & CIFISP_DPF_NF_GAIN_RESERVED) ||
	    (arg->gain.nf_gb_gain & CIFISP_DPF_NF_GAIN_RESERVED)) {
		CIFISP_DPRINT(CIFISP_ERROR,
			"incompatible DPF GAIN param in function: %s\n",
			__func__);
		retval = -EINVAL;
		goto end;
	}

	for (i = 0; i < CIFISP_DPF_MAX_SPATIAL_COEFFS; i++) {
		if ((arg->g_flt.spatial_coeff[i] >
		    CIFISP_DPF_SPATIAL_COEFF_MAX)) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"incompatible DPF G Spatial param in function: %s\n",
				__func__);
			retval = -EINVAL;
			goto end;
		}

		if (arg->rb_flt.spatial_coeff[i] >
			CIFISP_DPF_SPATIAL_COEFF_MAX) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"incompatible DPF RB Spatial param in function: %s\n",
				__func__);
			retval = -EINVAL;
			goto end;
		}
	}

	if ((arg->rb_flt.fltsize != CIFISP_DPF_RB_FILTERSIZE_9x9) &&
	    (arg->rb_flt.fltsize != CIFISP_DPF_RB_FILTERSIZE_13x9)) {
		CIFISP_DPRINT(CIFISP_ERROR,
			"incompatible DPF RB filter size param in function: %s\n",
			__func__);
		retval = -EINVAL;
		goto end;
	}

	for (i = 0; i < CIFISP_DPF_MAX_NLF_COEFFS; i++) {
		if (arg->nll.coeff[i] > CIFISP_DPF_NLL_COEFF_N_MAX) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"incompatible DPF NLL coeff param in function: %s\n",
				__func__);
			retval = -EINVAL;
			goto end;
		}
	}

	if ((arg->nll.scale_mode != CIFISP_NLL_SCALE_LINEAR) &&
	    (arg->nll.scale_mode != CIFISP_NLL_SCALE_LOGARITHMIC)) {
		CIFISP_DPRINT(CIFISP_ERROR,
			"incompatible DPF NLL scale mode param in function: %s\n",
			__func__);
		retval = -EINVAL;
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_dpf_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_DPF);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_dpf_strength_param(struct cif_isp10_isp_dev *isp_dev,
				     bool flag, struct cifisp_dpf_strength_config *arg)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_isp_cfgs_log *cfg_log;
	struct cifisp_dpf_strength_config *curr_cfg, *new_cfg;
	int retval = 0;

	if (!arg)
		return -EINVAL;

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);

	cfg_log = &isp_dev->other_cfgs.log[CIFISP_DPF_STRENGTH_ID];
	curr_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->curr_id].dpf_strength_config;
	new_cfg = &isp_dev->other_cfgs.cfgs[cfg_log->new_id].dpf_strength_config;

	if (flag == _GET_) {
		memcpy(arg, curr_cfg, sizeof(*arg));
		goto end;
	}

	if (CIFISP_MODULE_IS_UNACTIVE(isp_dev->other_cfgs.module_actives,
		CIFISP_MODULE_DPF_STRENGTH)) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "module is unactive in function: %s\n", __func__);
		goto end;
	}

	cifisp_param_dump(arg, CIFISP_MODULE_DPF_STRENGTH);

	if (CIFISP_MODULE_IS_EN(
		isp_dev->other_cfgs.cfgs[cfg_log->curr_id].module_ens,
		CIFISP_MODULE_DPF_STRENGTH) &&
		memcmp(arg, curr_cfg, sizeof(*arg)) == 0) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "same param in function: %s\n", __func__);
		goto end;
	}

	memcpy(new_cfg,
	       arg,
	       sizeof(struct cifisp_dpf_strength_config));
	CIFISP_MODULE_UPDATE(
		isp_dev->other_cfgs.module_updates,
		CIFISP_MODULE_DPF_STRENGTH);
end:
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	return retval;
}

static int cifisp_last_capture_config(struct cifisp_last_capture_config *arg)
{
#ifdef LOG_CAPTURE_PARAMS
	if (!arg)
		return -EINVAL;

	memcpy(arg, &g_last_capture_config, sizeof(*arg));

	return 0;
#else
	return -EPERM;
#endif
}

/* DPCC */
static void cifisp_dpcc_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int i;
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_DPCC_ID].new_id;
	const struct cifisp_dpcc_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].dpcc_config;

	cifisp_iowrite32(pconfig->mode, CIF_ISP_DPCC_MODE);
	cifisp_iowrite32(pconfig->output_mode, CIF_ISP_DPCC_OUTPUT_MODE);
	cifisp_iowrite32(pconfig->set_use, CIF_ISP_DPCC_SET_USE);

	cifisp_iowrite32(pconfig->methods[0].method,
			 CIF_ISP_DPCC_METHODS_SET_1);
	cifisp_iowrite32(pconfig->methods[1].method,
			 CIF_ISP_DPCC_METHODS_SET_2);
	cifisp_iowrite32(pconfig->methods[2].method,
			 CIF_ISP_DPCC_METHODS_SET_3);
	for (i = 0; i < CIFISP_DPCC_METHODS_MAX; i++) {
		cifisp_iowrite32(pconfig->methods[i].line_thresh,
				 CIF_ISP_DPCC_LINE_THRESH_1 + 0x14 * i);
		cifisp_iowrite32(pconfig->methods[i].line_mad_fac,
				 CIF_ISP_DPCC_LINE_MAD_FAC_1 + 0x14 * i);
		cifisp_iowrite32(pconfig->methods[i].pg_fac,
				 CIF_ISP_DPCC_PG_FAC_1 + 0x14 * i);
		cifisp_iowrite32(pconfig->methods[i].rnd_thresh,
				 CIF_ISP_DPCC_RND_THRESH_1 + 0x14 * i);
		cifisp_iowrite32(pconfig->methods[i].rg_fac,
				 CIF_ISP_DPCC_RG_FAC_1 + 0x14 * i);
	}

	cifisp_iowrite32(pconfig->rnd_offs, CIF_ISP_DPCC_RND_OFFS);
	cifisp_iowrite32(pconfig->ro_limits, CIF_ISP_DPCC_RO_LIMITS);
}

static void cifisp_dpcc_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIFISP_DPCC_ENA, CIF_ISP_DPCC_MODE);
}

static void cifisp_dpcc_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(CIFISP_DPCC_DIS, CIF_ISP_DPCC_MODE);
}

/* Lens Shade Correction */

/*****************************************************************************/
static void cifisp_lsc_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(0, CIF_ISP_LSC_CTRL);
}

static bool cifisp_lsc_correct_matrix_config(struct cif_isp10_isp_dev *isp_dev)
{
	int i, n;
	unsigned int isp_lsc_status, sram_addr, isp_lsc_table_sel;
	unsigned int data;
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_LSC_ID].new_id;
	const struct cifisp_lsc_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].lsc_config;

	isp_lsc_status = cifisp_ioread32(CIF_ISP_LSC_STATUS);
	sram_addr = (isp_lsc_status & 0x2U) ? 0U : 153U; /* ( 17 * 18 ) >> 1 */

	cifisp_iowrite32(sram_addr, CIF_ISP_LSC_R_TABLE_ADDR);
	cifisp_iowrite32(sram_addr, CIF_ISP_LSC_GR_TABLE_ADDR);
	cifisp_iowrite32(sram_addr, CIF_ISP_LSC_GB_TABLE_ADDR);
	cifisp_iowrite32(sram_addr, CIF_ISP_LSC_B_TABLE_ADDR);

	/* program data tables (table size is 9 * 17 = 153) */
	for (n = 0; n < ((CIFISP_LSC_SECTORS_MAX + 1) *
			(CIFISP_LSC_SECTORS_MAX + 1));
			n += CIFISP_LSC_SECTORS_MAX + 1) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (8 steps + 1 outside loop)
		 */
		for (i = 0; i < (CIFISP_LSC_SECTORS_MAX); i += 2) {
			data = CIFISP_LSC_TABLE_DATA(
				pconfig->r_data_tbl[n + i],
				pconfig->r_data_tbl[n + i + 1]);
			cifisp_iowrite32(data, CIF_ISP_LSC_R_TABLE_DATA);

			data = CIFISP_LSC_TABLE_DATA(
				pconfig->gr_data_tbl[n + i],
				pconfig->gr_data_tbl[n + i + 1]);
			cifisp_iowrite32(data, CIF_ISP_LSC_GR_TABLE_DATA);

			data = CIFISP_LSC_TABLE_DATA(
				pconfig->gb_data_tbl[n + i],
				pconfig->gb_data_tbl[n + i + 1]);
			cifisp_iowrite32(data, CIF_ISP_LSC_GB_TABLE_DATA);

			data = CIFISP_LSC_TABLE_DATA(
				pconfig->b_data_tbl[n + i],
				pconfig->b_data_tbl[n + i + 1]);
			cifisp_iowrite32(data, CIF_ISP_LSC_B_TABLE_DATA);
		}

		data = CIFISP_LSC_TABLE_DATA(
			pconfig->r_data_tbl[n + CIFISP_LSC_SECTORS_MAX],
			/* isp_dev->lsc_config.r_data_tbl[n + i] */0);
		cifisp_iowrite32(data, CIF_ISP_LSC_R_TABLE_DATA);

		data = CIFISP_LSC_TABLE_DATA(
			pconfig->gr_data_tbl[n + CIFISP_LSC_SECTORS_MAX],
			/* isp_dev->lsc_config.gr_data_tbl[n + i] */0);
		cifisp_iowrite32(data, CIF_ISP_LSC_GR_TABLE_DATA);

		data = CIFISP_LSC_TABLE_DATA(
			pconfig->gb_data_tbl[n + CIFISP_LSC_SECTORS_MAX],
			/* isp_dev->lsc_config.gr_data_tbl[n + i] */0);
		cifisp_iowrite32(data, CIF_ISP_LSC_GB_TABLE_DATA);

		data = CIFISP_LSC_TABLE_DATA(
				pconfig->b_data_tbl[n + CIFISP_LSC_SECTORS_MAX],
				/* isp_dev->lsc_config.b_data_tbl[n + i] */0);
		cifisp_iowrite32(data, CIF_ISP_LSC_B_TABLE_DATA);
	}

	isp_lsc_table_sel = (isp_lsc_status & 0x2U) ? 0U : 1U;
	cifisp_iowrite32(isp_lsc_table_sel, CIF_ISP_LSC_TABLE_SEL);
	return true;
}

/*****************************************************************************/
static bool cifisp_lsc_config(struct cif_isp10_isp_dev *isp_dev)
{
	int i;
	unsigned int data;
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_LSC_ID].new_id;
	const struct cifisp_lsc_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].lsc_config;

	if (pconfig->config_width != isp_dev->input_width ||
	    pconfig->config_height != isp_dev->input_height) {
		CIFISP_DPRINT(CIFISP_DEBUG,
			"LSC config: lsc_w %d lsc_h %d act_w %d act_h %d\n",
			pconfig->config_width,
			pconfig->config_height,
			isp_dev->input_width,
			isp_dev->input_height);
		return false;
	}

	CIFISP_DPRINT(CIFISP_DEBUG,
		      "LSC config: lsc_w %d lsc_h %d\n",
		      pconfig->config_width,
		      pconfig->config_height);

	/* To config must be off */
	cifisp_iowrite32(0, CIF_ISP_LSC_CTRL);

	cifisp_lsc_correct_matrix_config(isp_dev);

	for (i = 0; i < 4; i++) {
		/* program x size tables */
		data = CIFISP_LSC_SECT_SIZE(
				pconfig->x_size_tbl[i*2],
				pconfig->x_size_tbl[i*2 + 1]);
		cifisp_iowrite32(data, CIF_ISP_LSC_XSIZE_01 + i * 4);

		/* program x grad tables */
		data = CIFISP_LSC_SECT_SIZE(
				pconfig->x_grad_tbl[i*2],
				pconfig->x_grad_tbl[i*2 + 1]);
		cifisp_iowrite32(data, CIF_ISP_LSC_XGRAD_01 + i * 4);

		/* program y size tables */
		data = CIFISP_LSC_SECT_SIZE(
				pconfig->y_size_tbl[i*2],
				pconfig->y_size_tbl[i*2 + 1]);
		cifisp_iowrite32(data, CIF_ISP_LSC_YSIZE_01 + i * 4);

		/* program y grad tables */
		data = CIFISP_LSC_SECT_SIZE(
				pconfig->y_grad_tbl[i*2],
				pconfig->y_grad_tbl[i*2 + 1]);
		cifisp_iowrite32(data, CIF_ISP_LSC_YGRAD_01 + i * 4);
	}

	isp_dev->active_lsc_width = pconfig->config_width;
	isp_dev->active_lsc_height = pconfig->config_height;

	cifisp_iowrite32(1, CIF_ISP_LSC_CTRL);

	return true;
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_lsc_config_read(const struct cif_isp10_isp_dev *isp_dev,
				   struct cifisp_lsc_config *pconfig)
{
);
}
#endif

/*****************************************************************************/
static void cifisp_bls_get_meas(const struct cif_isp10_isp_dev *isp_dev,
				struct cifisp_stat_buffer *pbuf)
{
	const struct cif_isp10_device *cif_dev =
		container_of(isp_dev, struct cif_isp10_device, isp_dev);
	enum cif_isp10_pix_fmt in_pix_fmt;

	in_pix_fmt = cif_dev->config.isp_config.input->pix_fmt;
	if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_BGGR(in_pix_fmt)) {
		pbuf->params.ae.bls_val.meas_b =
			cifisp_ioread32(CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_gb =
			cifisp_ioread32(CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_gr =
			cifisp_ioread32(CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_r =
			cifisp_ioread32(CIF_ISP_BLS_D_MEASURED);
	} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GBRG(in_pix_fmt)) {
		pbuf->params.ae.bls_val.meas_gb =
			cifisp_ioread32(CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_b =
			cifisp_ioread32(CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_r =
			cifisp_ioread32(CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_gr =
			cifisp_ioread32(CIF_ISP_BLS_D_MEASURED);
	} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GRBG(in_pix_fmt)) {
		pbuf->params.ae.bls_val.meas_gr =
			cifisp_ioread32(CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_r =
			cifisp_ioread32(CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_b =
			cifisp_ioread32(CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_gb =
			cifisp_ioread32(CIF_ISP_BLS_D_MEASURED);
	} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_RGGB(in_pix_fmt)) {
		pbuf->params.ae.bls_val.meas_r =
			cifisp_ioread32(CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_gr =
			cifisp_ioread32(CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_gb =
			cifisp_ioread32(CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_b =
			cifisp_ioread32(CIF_ISP_BLS_D_MEASURED);
	}
}

/*****************************************************************************/
static void cifisp_bls_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_BLS_ID].new_id;
	const struct cifisp_bls_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].bls_config;
	u32 new_control = 0;
	const struct cif_isp10_device *cif_dev =
		container_of(isp_dev, struct cif_isp10_device, isp_dev);
	enum cif_isp10_pix_fmt in_pix_fmt;

	in_pix_fmt = cif_dev->config.isp_config.input->pix_fmt;

	/* fixed subtraction values */
	if (!pconfig->enable_auto) {
		const struct cifisp_bls_fixed_val *pval =
			&pconfig->fixed_val;

		if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_BGGR(in_pix_fmt)) {
			cifisp_iowrite32(pval->r, CIF_ISP_BLS_D_FIXED);
			cifisp_iowrite32(pval->gr, CIF_ISP_BLS_C_FIXED);
			cifisp_iowrite32(pval->gb, CIF_ISP_BLS_B_FIXED);
			cifisp_iowrite32(pval->b, CIF_ISP_BLS_A_FIXED);
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GBRG(in_pix_fmt)) {
			cifisp_iowrite32(pval->r, CIF_ISP_BLS_C_FIXED);
			cifisp_iowrite32(pval->gr, CIF_ISP_BLS_D_FIXED);
			cifisp_iowrite32(pval->gb, CIF_ISP_BLS_A_FIXED);
			cifisp_iowrite32(pval->b, CIF_ISP_BLS_B_FIXED);
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_GRBG(in_pix_fmt)) {
			cifisp_iowrite32(pval->r, CIF_ISP_BLS_B_FIXED);
			cifisp_iowrite32(pval->gr, CIF_ISP_BLS_A_FIXED);
			cifisp_iowrite32(pval->gb, CIF_ISP_BLS_D_FIXED);
			cifisp_iowrite32(pval->b, CIF_ISP_BLS_C_FIXED);
		} else if (CIF_ISP10_PIX_FMT_BAYER_PAT_IS_RGGB(in_pix_fmt)) {
			cifisp_iowrite32(pval->r, CIF_ISP_BLS_A_FIXED);
			cifisp_iowrite32(pval->gr, CIF_ISP_BLS_B_FIXED);
			cifisp_iowrite32(pval->gb, CIF_ISP_BLS_C_FIXED);
			cifisp_iowrite32(pval->b, CIF_ISP_BLS_D_FIXED);
		}

		new_control = CIFISP_BLS_MODE_FIXED;
		cifisp_iowrite32(new_control, CIF_ISP_BLS_CTRL);
	} else {
		if (pconfig->en_windows & 2) {
			cifisp_iowrite32(pconfig->bls_window2.h_offs,
					 CIF_ISP_BLS_H2_START);
			cifisp_iowrite32(pconfig->bls_window2.h_size,
					 CIF_ISP_BLS_H2_STOP);
			cifisp_iowrite32(pconfig->bls_window2.v_offs,
					 CIF_ISP_BLS_V2_START);
			cifisp_iowrite32(pconfig->bls_window2.v_size,
					 CIF_ISP_BLS_V2_STOP);
			new_control |= CIFISP_BLS_WINDOW_2;
		}

		if (pconfig->en_windows & 1) {
			cifisp_iowrite32(pconfig->bls_window1.h_offs,
					 CIF_ISP_BLS_H1_START);
			cifisp_iowrite32(pconfig->bls_window1.h_size,
					 CIF_ISP_BLS_H1_STOP);
			cifisp_iowrite32(pconfig->bls_window1.v_offs,
					 CIF_ISP_BLS_V1_START);
			cifisp_iowrite32(pconfig->bls_window1.v_size,
					 CIF_ISP_BLS_V1_STOP);
			new_control |= CIFISP_BLS_WINDOW_1;
		}

		cifisp_iowrite32(pconfig->bls_samples, CIF_ISP_BLS_SAMPLES);

		new_control |= CIFISP_BLS_MODE_MEASURED;

		cifisp_iowrite32(new_control, CIF_ISP_BLS_CTRL);
	}
}

/*****************************************************************************/
static void cifisp_bls_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIFISP_BLS_ENA, CIF_ISP_BLS_CTRL);
}

/*****************************************************************************/
static void cifisp_bls_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(CIFISP_BLS_DIS, CIF_ISP_BLS_CTRL);
}

/* Gamma correction */
/*****************************************************************************/
static void cifisp_sdg_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_SDG_ID].new_id;
	const struct cifisp_sdg_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].sdg_config;
	unsigned int i;

	cifisp_iowrite32(pconfig->xa_pnts.gamma_dx0, CIF_ISP_GAMMA_DX_LO);
	cifisp_iowrite32(pconfig->xa_pnts.gamma_dx1, CIF_ISP_GAMMA_DX_HI);

	for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
		cifisp_iowrite32(pconfig->curve_r.gamma_y[i],
				 CIF_ISP_GAMMA_R_Y0 + i * 4);
		cifisp_iowrite32(pconfig->curve_g.gamma_y[i],
				 CIF_ISP_GAMMA_G_Y0 + i * 4);
		cifisp_iowrite32(pconfig->curve_b.gamma_y[i],
				 CIF_ISP_GAMMA_B_Y0 + i * 4);
	}
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_sdg_config_read(const struct cif_isp10_isp_dev *isp_dev,
				   struct cifisp_sdg_config *pconfig)
{
}
#endif

/*****************************************************************************/
static void cifisp_sdg_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIF_ISP_CTRL_ISP_GAMMA_IN_ENA, CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_sdg_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_GAMMA_IN_ENA, CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_goc_config(const struct cif_isp10_isp_dev *isp_dev)
{
	int i;
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_GOC_ID].new_id;
	const struct cifisp_goc_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].goc_config;

	cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA, CIF_ISP_CTRL);

	cifisp_iowrite32(pconfig->mode, CIF_ISP_GAMMA_OUT_MODE);
	for (i = 0; i < CIFISP_GAMMA_OUT_MAX_SAMPLES; i++)
		cifisp_iowrite32(pconfig->gamma_y[i],
				 CIF_ISP_GAMMA_OUT_Y_0 + i * 4);
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_goc_config_read(const struct cif_isp10_isp_dev *isp_dev,
				   struct cifisp_goc_config *pconfig)
{
}
#endif

/*****************************************************************************/
static void cifisp_goc_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA, CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_goc_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA, CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_bdm_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_BDM_ID].new_id;
	const struct cifisp_bdm_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].bdm_config;

	/*set demosaic threshold */
	cifisp_iowrite32(pconfig->demosaic_th, CIF_ISP_DEMOSAIC);
}

/*****************************************************************************/
static void cifisp_bdm_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32AND(~(CIFISP_BDM_BYPASS_EN(1)), CIF_ISP_DEMOSAIC);
}

/*****************************************************************************/
static void cifisp_bdm_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(0, CIF_ISP_DEMOSAIC);
}

/*****************************************************************************/
static void cifisp_flt_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_FLT_ID].new_id;
	const struct cifisp_flt_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].flt_config;

	cifisp_iowrite32(pconfig->thresh_bl0,
			 CIF_ISP_FILT_THRESH_BL0);
	cifisp_iowrite32(pconfig->thresh_bl1,
			 CIF_ISP_FILT_THRESH_BL1);
	cifisp_iowrite32(pconfig->thresh_sh0,
			 CIF_ISP_FILT_THRESH_SH0);
	cifisp_iowrite32(pconfig->thresh_sh1,
			 CIF_ISP_FILT_THRESH_SH1);
	cifisp_iowrite32(pconfig->fac_bl0,
			 CIF_ISP_FILT_FAC_BL0);
	cifisp_iowrite32(pconfig->fac_bl1,
			 CIF_ISP_FILT_FAC_BL1);
	cifisp_iowrite32(pconfig->fac_mid,
			 CIF_ISP_FILT_FAC_MID);
	cifisp_iowrite32(pconfig->fac_sh0,
			 CIF_ISP_FILT_FAC_SH0);
	cifisp_iowrite32(pconfig->fac_sh1,
			 CIF_ISP_FILT_FAC_SH1);
	cifisp_iowrite32(pconfig->lum_weight,
			 CIF_ISP_FILT_LUM_WEIGHT);

	cifisp_iowrite32(CIFISP_FLT_MODE(pconfig->mode) |
		CIFISP_FLT_CHROMA_V_MODE(pconfig->chr_v_mode) |
		CIFISP_FLT_CHROMA_H_MODE(pconfig->chr_h_mode) |
		CIFISP_FLT_GREEN_STAGE1(pconfig->grn_stage1),
		CIF_ISP_FILT_MODE);
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_flt_config_read(const struct cif_isp10_isp_dev *isp_dev,
				   struct cifisp_flt_config *pconfig)
{
}
#endif

/*****************************************************************************/
static void cifisp_flt_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIFISP_FLT_ENA, CIF_ISP_FILT_MODE);
}

/*****************************************************************************/
static void cifisp_flt_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(CIFISP_FLT_DIS, CIF_ISP_FILT_MODE);
}

/* Auto White Balance */
/*****************************************************************************/
static void cifisp_awb_gain_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_AWB_GAIN_ID].new_id;
	unsigned int new_ens =
		isp_dev->other_cfgs.cfgs[new_id].module_ens;
	const struct cifisp_awb_gain_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].awb_gain_config;

	if (CIFISP_MODULE_IS_EN(new_ens, CIFISP_MODULE_AWB_GAIN)) {
		cifisp_iowrite32(CIFISP_AWB_GAIN_R_SET(pconfig->gain_green_r) |
			pconfig->gain_green_b, CIF_ISP_AWB_GAIN_G);
		cifisp_iowrite32(CIFISP_AWB_GAIN_R_SET(pconfig->gain_red) |
			pconfig->gain_blue, CIF_ISP_AWB_GAIN_RB);
	} else {
		cifisp_iowrite32(0x01000100, CIF_ISP_AWB_GAIN_G);
		cifisp_iowrite32(0x01000100, CIF_ISP_AWB_GAIN_RB);
	}
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_awb_gain_config_read(const struct cif_isp10_isp_dev *isp_dev,
					struct cifisp_awb_gain_config *pconfig)
{
	unsigned int reg = cifisp_ioread32(CIF_ISP_AWB_GAIN_G);

	pconfig->gain_green_r = CIFISP_AWB_GAIN_R_READ(reg);
	pconfig->gain_green_b = CIFISP_AWB_GAIN_B_READ(reg);
	reg = cifisp_ioread32(CIF_ISP_AWB_GAIN_RB);
	pconfig->gain_red = CIFISP_AWB_GAIN_R_READ(reg);
	pconfig->gain_blue = CIFISP_AWB_GAIN_B_READ(reg);
}
#endif

/*****************************************************************************/
static void cifisp_awb_meas_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_AWB_ID].new_id;
	const struct cifisp_awb_meas_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].awb_meas_config;
	unsigned int awb_prob = 0;

	/* based on the mode,configure the awb module */
	if (pconfig->awb_mode == CIFISP_AWB_MODE_RGB) {
		awb_prob = CIFISP_AWB_MODE_RGB_EN;
	} else {
		if (pconfig->enable_ymax_cmp)
			awb_prob = CIFISP_AWB_YMAX_CMP_EN;

		/* Reference Cb and Cr */
		cifisp_iowrite32(CIFISP_AWB_REF_CR_SET(pconfig->awb_ref_cr) |
			pconfig->awb_ref_cb, CIF_ISP_AWB_REF);
		/* Yc Threshold */
		cifisp_iowrite32(CIFISP_AWB_MAX_Y_SET(pconfig->max_y) |
			CIFISP_AWB_MIN_Y_SET(pconfig->min_y) |
			CIFISP_AWB_MAX_CS_SET(pconfig->max_csum) |
			pconfig->min_c, CIF_ISP_AWB_THRESH);
	}

	/* Common Configuration */
	cifisp_iowrite32(awb_prob, CIF_ISP_AWB_PROP);
	/* window offset */
	cifisp_iowrite32(pconfig->awb_wnd.v_offs,
			 CIF_ISP_AWB_WND_V_OFFS);
	cifisp_iowrite32(pconfig->awb_wnd.h_offs,
			 CIF_ISP_AWB_WND_H_OFFS);
	/* AWB window size */
	cifisp_iowrite32(pconfig->awb_wnd.v_size, CIF_ISP_AWB_WND_V_SIZE);
	cifisp_iowrite32(pconfig->awb_wnd.h_size, CIF_ISP_AWB_WND_H_SIZE);
	/* Number of frames */
	cifisp_iowrite32(pconfig->frames, CIF_ISP_AWB_FRAMES);
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_awb_meas_config_read(const struct cif_isp10_isp_dev *isp_dev,
					struct cifisp_awb_meas_config *pconfig)
{
}
#endif

/*****************************************************************************/
static void cifisp_awb_meas_en(struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_AWB_ID].new_id;
	const struct cifisp_awb_meas_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].awb_meas_config;
	u32 reg_val = cifisp_ioread32(CIF_ISP_AWB_PROP);

	/* switch off */
	reg_val &= 0xFFFFFFFC;

	if (pconfig->awb_mode == CIFISP_AWB_MODE_RGB)
		reg_val |= CIFISP_AWB_MODE_RGB_EN;
	else
		reg_val |= CIFISP_AWB_MODE_YCBCR_EN;

	cifisp_iowrite32(reg_val, CIF_ISP_AWB_PROP);

	isp_dev->active_meas |= CIF_ISP_AWB_DONE;

	/* Measurements require AWB block be active. */
	cifisp_iowrite32OR(CIF_ISP_CTRL_ISP_AWB_ENA, CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_awb_meas_end(struct cif_isp10_isp_dev *isp_dev)
{
	u32 reg_val = cifisp_ioread32(CIF_ISP_AWB_PROP);

	/* switch off */
	reg_val &= 0xFFFFFFFC;

	cifisp_iowrite32(reg_val, CIF_ISP_AWB_PROP);

	isp_dev->active_meas &= ~CIF_ISP_AWB_DONE;

	cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_AWB_ENA,
			    CIF_ISP_CTRL);
}

/*****************************************************************************/
static void cifisp_get_awb_meas(struct cif_isp10_isp_dev *isp_dev,
				struct cifisp_stat_buffer *pbuf)
{
	/* Protect against concurrent access from ISR? */
	u32 reg_val;
	unsigned int curr_id =
		isp_dev->meas_cfgs.log[CIFISP_AWB_ID].curr_id;
	const struct cifisp_awb_meas_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[curr_id].awb_meas_config;

	pbuf->meas_type |= CIFISP_STAT_AWB;
	reg_val = cifisp_ioread32(CIF_ISP_AWB_WHITE_CNT);
	pbuf->params.awb.awb_mean[0].cnt =
		CIFISP_AWB_GET_PIXEL_CNT(reg_val);
	reg_val = cifisp_ioread32(CIF_ISP_AWB_MEAN);

	if (pconfig->awb_mode == CIFISP_AWB_MODE_RGB) {
		pbuf->params.awb.awb_mean[0].mean_r =
			CIFISP_AWB_GET_MEAN_R(reg_val);
		pbuf->params.awb.awb_mean[0].mean_b =
			CIFISP_AWB_GET_MEAN_B(reg_val);
		pbuf->params.awb.awb_mean[0].mean_g =
			CIFISP_AWB_GET_MEAN_G(reg_val);
	} else {
		pbuf->params.awb.awb_mean[0].mean_cr =
			(u8)CIFISP_AWB_GET_MEAN_CR(reg_val);
		pbuf->params.awb.awb_mean[0].mean_cb =
			(u8)CIFISP_AWB_GET_MEAN_CB(reg_val);
		pbuf->params.awb.awb_mean[0].mean_y =
			(u8)CIFISP_AWB_GET_MEAN_Y(reg_val);
	}
}

/* Auto Exposure */
/*****************************************************************************/
static void cifisp_aec_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_AEC_ID].new_id;
	const struct cifisp_aec_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].aec_config;
	unsigned int block_hsize, block_vsize;

	cifisp_iowrite32(CIFISP_EXP_CTRL_AUTOSTOP(pconfig->autostop) |
			CIFISP_EXP_CTRL_MEASMODE(pconfig->mode),
			CIF_ISP_EXP_CTRL);

	cifisp_iowrite32(pconfig->meas_window.h_offs, CIF_ISP_EXP_H_OFFSET);
	cifisp_iowrite32(pconfig->meas_window.v_offs, CIF_ISP_EXP_V_OFFSET);

	block_hsize = pconfig->meas_window.h_size /
		CIFISP_EXP_COLUMN_NUM - 1;
	block_vsize = pconfig->meas_window.v_size /
		CIFISP_EXP_ROW_NUM - 1;

	cifisp_iowrite32(CIFISP_EXP_HSIZE(block_hsize), CIF_ISP_EXP_H_SIZE);
	cifisp_iowrite32(CIFISP_EXP_VSIZE(block_vsize), CIF_ISP_EXP_V_SIZE);
}

/*****************************************************************************/
static void cifisp_aec_en(struct cif_isp10_isp_dev *isp_dev)
{
	isp_dev->active_meas |= CIF_ISP_EXP_END;

	cifisp_iowrite32OR(CIFISP_EXP_ENA, CIF_ISP_EXP_CTRL);
}

/*****************************************************************************/
static void cifisp_aec_end(struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(CIFISP_EXP_DIS, CIF_ISP_EXP_CTRL);

	isp_dev->active_meas &= ~CIF_ISP_EXP_END;
}

/*****************************************************************************/
static void cifisp_get_aec_meas(struct cif_isp10_isp_dev *isp_dev,
				struct cifisp_stat_buffer *pbuf)
{
	unsigned int i;

	pbuf->meas_type |= CIFISP_STAT_AUTOEXP;	/*Set the measurement type */
	for (i = 0; i < CIFISP_AE_MEAN_MAX; i++) {
		pbuf->params.ae.exp_mean[i] =
			(u8)cifisp_ioread32(CIF_ISP_EXP_MEAN_00 + i * 4);
	}
}

/* X-Talk Matrix */
/*****************************************************************************/
static void cifisp_ctk_config(const struct cif_isp10_isp_dev *isp_dev)
{
	/* Nothing to do */
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_ctk_config_read(const struct cif_isp10_isp_dev *isp_dev,
				   struct cifisp_ctk_config *pconfig)
{
	pconfig->coeff0 = cifisp_ioread32(CIF_ISP_CT_COEFF_0);
	pconfig->coeff1 = cifisp_ioread32(CIF_ISP_CT_COEFF_1);
	pconfig->coeff2 = cifisp_ioread32(CIF_ISP_CT_COEFF_2);
	pconfig->coeff3 = cifisp_ioread32(CIF_ISP_CT_COEFF_3);
	pconfig->coeff4 = cifisp_ioread32(CIF_ISP_CT_COEFF_4);
	pconfig->coeff5 = cifisp_ioread32(CIF_ISP_CT_COEFF_5);
	pconfig->coeff6 = cifisp_ioread32(CIF_ISP_CT_COEFF_6);
	pconfig->coeff7 = cifisp_ioread32(CIF_ISP_CT_COEFF_7);
	pconfig->coeff8 = cifisp_ioread32(CIF_ISP_CT_COEFF_8);
	pconfig->ct_offset_r = cifisp_ioread32(CIF_ISP_CT_OFFSET_R);
	pconfig->ct_offset_g = cifisp_ioread32(CIF_ISP_CT_OFFSET_G);
	pconfig->ct_offset_b = cifisp_ioread32(CIF_ISP_CT_OFFSET_B);
}
#endif

/*****************************************************************************/
static void cifisp_ctk_en(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_CTK_ID].new_id;
	const struct cifisp_ctk_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].ctk_config;

	cifisp_iowrite32(pconfig->coeff0, CIF_ISP_CT_COEFF_0);
	cifisp_iowrite32(pconfig->coeff1, CIF_ISP_CT_COEFF_1);
	cifisp_iowrite32(pconfig->coeff2, CIF_ISP_CT_COEFF_2);
	cifisp_iowrite32(pconfig->coeff3, CIF_ISP_CT_COEFF_3);
	cifisp_iowrite32(pconfig->coeff4, CIF_ISP_CT_COEFF_4);
	cifisp_iowrite32(pconfig->coeff5, CIF_ISP_CT_COEFF_5);
	cifisp_iowrite32(pconfig->coeff6, CIF_ISP_CT_COEFF_6);
	cifisp_iowrite32(pconfig->coeff7, CIF_ISP_CT_COEFF_7);
	cifisp_iowrite32(pconfig->coeff8, CIF_ISP_CT_COEFF_8);
	cifisp_iowrite32(pconfig->ct_offset_r, CIF_ISP_CT_OFFSET_R);
	cifisp_iowrite32(pconfig->ct_offset_g, CIF_ISP_CT_OFFSET_G);
	cifisp_iowrite32(pconfig->ct_offset_b, CIF_ISP_CT_OFFSET_B);
}

/*****************************************************************************/
static void cifisp_ctk_end(const struct cif_isp10_isp_dev *isp_dev)
{
	/* Write back the default values. */
	cifisp_iowrite32(0x80, CIF_ISP_CT_COEFF_0);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_1);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_2);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_3);
	cifisp_iowrite32(0x80, CIF_ISP_CT_COEFF_4);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_5);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_6);
	cifisp_iowrite32(0, CIF_ISP_CT_COEFF_7);
	cifisp_iowrite32(0x80, CIF_ISP_CT_COEFF_8);

	cifisp_iowrite32(0, CIF_ISP_CT_OFFSET_R);
	cifisp_iowrite32(0, CIF_ISP_CT_OFFSET_G);
	cifisp_iowrite32(0, CIF_ISP_CT_OFFSET_B);
}

/* CPROC */
/*****************************************************************************/
static void cifisp_cproc_config(const struct cif_isp10_isp_dev *isp_dev,
	enum cif_isp10_pix_fmt_quantization quantization)
{
	unsigned int new_id_cproc =
		isp_dev->other_cfgs.log[CIFISP_CPROC_ID].new_id;
	const struct cifisp_cproc_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id_cproc].cproc_config;
	unsigned int curr_id =
		isp_dev->other_cfgs.log[CIFISP_IE_ID].curr_id;
	const struct cifisp_ie_config *ie_pconfig =
		&isp_dev->other_cfgs.cfgs[curr_id].ie_config;

	cifisp_iowrite32(pconfig->contrast, CIF_C_PROC_CONTRAST);
	cifisp_iowrite32(pconfig->hue, CIF_C_PROC_HUE);
	cifisp_iowrite32(pconfig->sat, CIF_C_PROC_SATURATION);
	cifisp_iowrite32(pconfig->brightness, CIF_C_PROC_BRIGHTNESS);

	if ((quantization != CIF_ISP10_QUANTIZATION_FULL_RANGE) ||
	    (ie_pconfig->effect != V4L2_COLORFX_NONE)) {
		cifisp_iowrite32AND(
			~(CIF_C_PROC_YOUT_FULL |
			CIF_C_PROC_YIN_FULL |
			CIF_C_PROC_COUT_FULL),
			CIF_C_PROC_CTRL);
	} else {
		cifisp_iowrite32OR(
			(CIF_C_PROC_YOUT_FULL |
			CIF_C_PROC_YIN_FULL |
			CIF_C_PROC_COUT_FULL),
			CIF_C_PROC_CTRL);
	}
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_cproc_config_read(const struct cif_isp10_isp_dev *isp_dev,
				     struct cifisp_cproc_config *pconfig)
{
	unsigned int reg;

	pconfig->contrast = cifisp_ioread32(CIF_C_PROC_CONTRAST);
	pconfig->hue = cifisp_ioread32(CIF_C_PROC_HUE);
	pconfig->sat = cifisp_ioread32(CIF_C_PROC_SATURATION);
	pconfig->brightness = cifisp_ioread32(CIF_C_PROC_BRIGHTNESS);
	reg = cifisp_ioread32(CIF_C_PROC_CTRL);
	pconfig->y_out_range = (reg >> 1) & 1;
	pconfig->y_in_range = (reg >> 2) & 1;
	pconfig->c_out_range = (reg >> 3) & 1;
}
#endif

/*****************************************************************************/
static void cifisp_cproc_en(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIFISP_CPROC_EN, CIF_C_PROC_CTRL);
}

/*****************************************************************************/
static void cifisp_cproc_end(const struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32AND(~CIFISP_CPROC_EN, CIF_C_PROC_CTRL);
}

static void cifisp_afc_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_AFC_ID].new_id;
	const struct cifisp_afc_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].afc_config;
	int num_of_win = pconfig->num_afm_win, i;

	/* Switch off to configure. Enabled during normal flow in frame isr. */
	cifisp_iowrite32(0, CIF_ISP_AFM_CTRL);

	for (i = 0; i < num_of_win; i++) {
		cifisp_iowrite32(
			CIFISP_AFC_WINDOW_X(pconfig->afm_win[i].h_offs) |
			CIFISP_AFC_WINDOW_Y(pconfig->afm_win[i].v_offs),
			CIF_ISP_AFM_LT_A + i * 8);
		cifisp_iowrite32(
			CIFISP_AFC_WINDOW_X(pconfig->afm_win[i].h_size +
			pconfig->afm_win[i].h_offs) |
			CIFISP_AFC_WINDOW_Y(pconfig->afm_win[i].v_size +
			pconfig->afm_win[i].v_offs),
			CIF_ISP_AFM_RB_A + i * 8);
	}

	cifisp_iowrite32(pconfig->thres, CIF_ISP_AFM_THRES);
	cifisp_iowrite32(pconfig->var_shift, CIF_ISP_AFM_VAR_SHIFT);
}

/*****************************************************************************/
static void cifisp_afc_en(struct cif_isp10_isp_dev *isp_dev)
{
	isp_dev->active_meas |= CIF_ISP_AFM_FIN;

	cifisp_iowrite32(CIFISP_AFC_ENA, CIF_ISP_AFM_CTRL);
}

/*****************************************************************************/
static void cifisp_afc_end(struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32(CIFISP_AFC_DIS, CIF_ISP_AFM_CTRL);
	isp_dev->active_meas &= ~CIF_ISP_AFM_FIN;
}

/*****************************************************************************/
static void cifisp_get_afc_meas(struct cif_isp10_isp_dev *isp_dev,
				struct cifisp_stat_buffer *pbuf)
{
	pbuf->meas_type |= CIFISP_STAT_AFM_FIN;

	pbuf->params.af.window[0].sum =
		cifisp_ioread32(CIF_ISP_AFM_SUM_A);
	pbuf->params.af.window[0].lum =
		cifisp_ioread32(CIF_ISP_AFM_LUM_A);
	pbuf->params.af.window[1].sum =
		cifisp_ioread32(CIF_ISP_AFM_SUM_B);
	pbuf->params.af.window[1].lum =
		cifisp_ioread32(CIF_ISP_AFM_LUM_B);
	pbuf->params.af.window[2].sum =
		cifisp_ioread32(CIF_ISP_AFM_SUM_C);
	pbuf->params.af.window[2].lum =
		cifisp_ioread32(CIF_ISP_AFM_LUM_C);
}

/* HISTOGRAM CALCULATION */
/*****************************************************************************/
static void cifisp_hst_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_HST_ID].new_id;
	const struct cifisp_hst_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].hst_config;
	unsigned int block_hsize, block_vsize;

	cifisp_iowrite32(CIFISP_HIST_PREDIV_SET(pconfig->histogram_predivider),
			 CIF_ISP_HIST_PROP);
	cifisp_iowrite32(pconfig->meas_window.h_offs, CIF_ISP_HIST_H_OFFS);
	cifisp_iowrite32(pconfig->meas_window.v_offs, CIF_ISP_HIST_V_OFFS);

	block_hsize = pconfig->meas_window.h_size /
		CIFISP_HIST_COLUMN_NUM - 1;
	block_vsize = pconfig->meas_window.v_size /
		CIFISP_HIST_ROW_NUM - 1;

	cifisp_iowrite32(block_hsize, CIF_ISP_HIST_H_SIZE);
	cifisp_iowrite32(block_vsize, CIF_ISP_HIST_V_SIZE);

	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[0],
			 pconfig->hist_weight[1], pconfig->hist_weight[2],
			 pconfig->hist_weight[3]), CIF_ISP_HIST_WEIGHT_00TO30);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[4],
			 pconfig->hist_weight[5], pconfig->hist_weight[6],
			 pconfig->hist_weight[7]), CIF_ISP_HIST_WEIGHT_40TO21);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[8],
			 pconfig->hist_weight[9], pconfig->hist_weight[10],
			 pconfig->hist_weight[11]), CIF_ISP_HIST_WEIGHT_31TO12);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[12],
			 pconfig->hist_weight[13], pconfig->hist_weight[14],
			 pconfig->hist_weight[15]), CIF_ISP_HIST_WEIGHT_22TO03);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[16],
			 pconfig->hist_weight[17], pconfig->hist_weight[18],
			 pconfig->hist_weight[19]), CIF_ISP_HIST_WEIGHT_13TO43);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[20],
			 pconfig->hist_weight[21], pconfig->hist_weight[22],
			 pconfig->hist_weight[23]), CIF_ISP_HIST_WEIGHT_04TO34);
	cifisp_iowrite32(CIFISP_HIST_WEIGHT_SET(pconfig->hist_weight[24],
			 0, 0, 0), CIF_ISP_HIST_WEIGHT_44);
}

static void cifisp_hst_en(struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->meas_cfgs.log[CIFISP_HST_ID].new_id;
	const struct cifisp_hst_config *pconfig =
		&isp_dev->meas_cfgs.cfgs[new_id].hst_config;

	isp_dev->active_meas |= CIF_ISP_HIST_MEASURE_RDY;

	cifisp_iowrite32OR(pconfig->mode,
			   CIF_ISP_HIST_PROP);
}

/*****************************************************************************/
static void cifisp_hst_end(struct cif_isp10_isp_dev *isp_dev)
{
	/* Disable measurement */
	cifisp_iowrite32(CIFISP_HISTOGRAM_MODE_DISABLE,
			 CIF_ISP_HIST_PROP);

	isp_dev->active_meas &= ~CIF_ISP_HIST_MEASURE_RDY;
}

/*****************************************************************************/
static void cifisp_get_hst_meas(const struct cif_isp10_isp_dev *isp_dev,
				struct cifisp_stat_buffer *pbuf)
{
	int i;

	pbuf->meas_type |= CIFISP_STAT_HIST;
	for (i = 0; i < CIFISP_HIST_BIN_N_MAX; i++) {
		pbuf->params.hist.hist_bins[i] =
		    cifisp_ioread32(CIF_ISP_HIST_BIN_0 + (i * 4));
	}
}

/* IMAGE EFFECT */
/*****************************************************************************/
static void cifisp_ie_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_IE_ID].new_id;
	const struct cifisp_ie_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].ie_config;

	switch (pconfig->effect) {
	case V4L2_COLORFX_SET_CBCR:
		cifisp_iowrite32(pconfig->eff_tint, CIF_IMG_EFF_TINT);
		break;
	/*
	 * Color selection is similar to water color(AQUA):
	 * grayscale + selected color w threshold
	 */
	case V4L2_COLORFX_AQUA:
		cifisp_iowrite32(pconfig->color_sel, CIF_IMG_EFF_COLOR_SEL);
		break;
	case V4L2_COLORFX_EMBOSS:
		cifisp_iowrite32(pconfig->eff_mat_1, CIF_IMG_EFF_MAT_1);
		cifisp_iowrite32(pconfig->eff_mat_2, CIF_IMG_EFF_MAT_2);
		cifisp_iowrite32(pconfig->eff_mat_3, CIF_IMG_EFF_MAT_3);
		break;
	case V4L2_COLORFX_SKETCH:
		cifisp_iowrite32(pconfig->eff_mat_3, CIF_IMG_EFF_MAT_3);
		cifisp_iowrite32(pconfig->eff_mat_4, CIF_IMG_EFF_MAT_4);
		cifisp_iowrite32(pconfig->eff_mat_5, CIF_IMG_EFF_MAT_5);
		break;
	default:
		break;
	}
}

static void cifisp_ie_en(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_IE_ID].new_id;
	const struct cifisp_ie_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].ie_config;
	enum cif_isp10_image_effect effect;

	switch (pconfig->effect) {
	case V4L2_COLORFX_SEPIA:
	case V4L2_COLORFX_SET_CBCR:
		effect = CIF_ISP10_IE_SEPIA;
		break;
	case V4L2_COLORFX_BW:
		effect = CIF_ISP10_IE_BW;
		break;
	case V4L2_COLORFX_NEGATIVE:
		effect = CIF_ISP10_IE_NEGATIVE;
		break;
	case V4L2_COLORFX_EMBOSS:
		effect = CIF_ISP10_IE_EMBOSS;
		break;
	case V4L2_COLORFX_SKETCH:
		effect = CIF_ISP10_IE_SKETCH;
		break;
	case V4L2_COLORFX_AQUA:
		effect = CIF_ISP10_IE_C_SEL;
		break;
	case V4L2_COLORFX_NONE:
	default:
		effect = CIF_ISP10_IE_NONE;
		break;
	}

	if (effect < CIF_ISP10_IE_NONE) {
		cifisp_iowrite32OR(CIF_ICCL_IE_CLK, CIF_ICCL);
		cifisp_iowrite32(CIF_IMG_EFF_CTRL_ENABLE |
			effect << 1, CIF_IMG_EFF_CTRL);
		cifisp_iowrite32OR(CIF_IMG_EFF_CTRL_CFG_UPD, CIF_IMG_EFF_CTRL);
	} else if (effect == CIF_ISP10_IE_NONE) {
		cifisp_iowrite32AND(~CIF_IMG_EFF_CTRL_ENABLE, CIF_IMG_EFF_CTRL);
		cifisp_iowrite32AND(~CIF_ICCL_IE_CLK, CIF_ICCL);
	}
}

static void cifisp_ie_end(const struct cif_isp10_isp_dev *isp_dev)
{
	/* Disable measurement */
	cifisp_iowrite32AND(~CIF_IMG_EFF_CTRL_ENABLE, CIF_IMG_EFF_CTRL);
	cifisp_iowrite32AND(~CIF_ICCL_IE_CLK, CIF_ICCL);
}

static void cifisp_csm_config(const struct cif_isp10_isp_dev *isp_dev,
			      enum cif_isp10_pix_fmt_quantization quantization)
{
	unsigned int curr_id_ie =
		isp_dev->other_cfgs.log[CIFISP_IE_ID].curr_id;
	const struct cifisp_ie_config *ie_pconfig =
		&isp_dev->other_cfgs.cfgs[curr_id_ie].ie_config;
	unsigned int curr_id_cproc =
		isp_dev->other_cfgs.log[CIFISP_CPROC_ID].curr_id;
	unsigned int curr_cproc_en =
		isp_dev->other_cfgs.cfgs[curr_id_cproc].module_ens;

	if ((quantization != CIF_ISP10_QUANTIZATION_FULL_RANGE) ||
	    ((ie_pconfig->effect != V4L2_COLORFX_NONE) &&
		CIFISP_MODULE_IS_EN(
		curr_cproc_en,
		CIFISP_MODULE_CPROC))) {
		/* Limit range conversion */
		cifisp_iowrite32(0x21, CIF_ISP_CC_COEFF_0);
		cifisp_iowrite32(0x40, CIF_ISP_CC_COEFF_1);
		cifisp_iowrite32(0xd, CIF_ISP_CC_COEFF_2);
		cifisp_iowrite32(0x1ed, CIF_ISP_CC_COEFF_3);
		cifisp_iowrite32(0x1db, CIF_ISP_CC_COEFF_4);
		cifisp_iowrite32(0x38, CIF_ISP_CC_COEFF_5);
		cifisp_iowrite32(0x38, CIF_ISP_CC_COEFF_6);
		cifisp_iowrite32(0x1d1, CIF_ISP_CC_COEFF_7);
		cifisp_iowrite32(0x1f7, CIF_ISP_CC_COEFF_8);
		cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA,
				    CIF_ISP_CTRL);
		cifisp_iowrite32AND(~CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA,
				    CIF_ISP_CTRL);
	} else {
		cifisp_iowrite32(0x26, CIF_ISP_CC_COEFF_0);
		cifisp_iowrite32(0x4b, CIF_ISP_CC_COEFF_1);
		cifisp_iowrite32(0xf, CIF_ISP_CC_COEFF_2);
		cifisp_iowrite32(0x1ea, CIF_ISP_CC_COEFF_3);
		cifisp_iowrite32(0x1d6, CIF_ISP_CC_COEFF_4);
		cifisp_iowrite32(0x40, CIF_ISP_CC_COEFF_5);
		cifisp_iowrite32(0x40, CIF_ISP_CC_COEFF_6);
		cifisp_iowrite32(0x1ca, CIF_ISP_CC_COEFF_7);
		cifisp_iowrite32(0x1f6, CIF_ISP_CC_COEFF_8);
		cifisp_iowrite32OR(CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA,
				   CIF_ISP_CTRL);
		cifisp_iowrite32OR(CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA,
				   CIF_ISP_CTRL);
	}
}

/* DPF */
/*****************************************************************************/
static void cifisp_dpf_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_DPF_ID].new_id;
	const struct cifisp_dpf_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].dpf_config;
	unsigned int isp_dpf_mode;
	unsigned int i;
	unsigned int spatial_coeff;

	isp_dpf_mode =
		cifisp_ioread32(CIF_ISP_DPF_MODE) & CIFISP_DPF_MODE_EN;

	switch (pconfig->gain.mode) {
	case CIFISP_DPF_GAIN_USAGE_DISABLED:
		break;
	case CIFISP_DPF_GAIN_USAGE_NF_GAINS:
		isp_dpf_mode |= CIFISP_DPF_MODE_USE_NF_GAIN |
			CIFISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_LSC_GAINS:
		isp_dpf_mode |= CIFISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_NF_LSC_GAINS:
		isp_dpf_mode |= CIFISP_DPF_MODE_USE_NF_GAIN |
			CIFISP_DPF_MODE_AWB_GAIN_COMP |
			CIFISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_AWB_GAINS:
		isp_dpf_mode |= CIFISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_AWB_LSC_GAINS:
		isp_dpf_mode |= CIFISP_DPF_MODE_LSC_GAIN_COMP |
			CIFISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	default:
		break;
	}

	isp_dpf_mode |=
	CIFISP_DPF_MODE_NLL_SEGMENTATION(pconfig->nll.scale_mode);
	isp_dpf_mode |=
	CIFISP_DPF_MODE_RB_FLTSIZE(pconfig->rb_flt.fltsize);

	isp_dpf_mode |= (pconfig->rb_flt.r_enable) ?
		CIFISP_DPF_MODE_R_FLT_EN : CIFISP_DPF_MODE_R_FLT_DIS;
	isp_dpf_mode |= (pconfig->rb_flt.b_enable) ?
		CIFISP_DPF_MODE_B_FLT_EN : CIFISP_DPF_MODE_B_FLT_DIS;
	isp_dpf_mode |= (pconfig->g_flt.gb_enable) ?
		CIFISP_DPF_MODE_GB_FLT_EN : CIFISP_DPF_MODE_GB_FLT_DIS;
	isp_dpf_mode |= (pconfig->g_flt.gr_enable) ?
		CIFISP_DPF_MODE_GR_FLT_EN : CIFISP_DPF_MODE_GR_FLT_DIS;

	cifisp_iowrite32(isp_dpf_mode, CIF_ISP_DPF_MODE);
	cifisp_iowrite32(pconfig->gain.nf_b_gain, CIF_ISP_DPF_NF_GAIN_B);
	cifisp_iowrite32(pconfig->gain.nf_r_gain, CIF_ISP_DPF_NF_GAIN_R);
	cifisp_iowrite32(pconfig->gain.nf_gb_gain, CIF_ISP_DPF_NF_GAIN_GB);
	cifisp_iowrite32(pconfig->gain.nf_gr_gain, CIF_ISP_DPF_NF_GAIN_GR);

	for (i = 0; i < CIFISP_DPF_MAX_NLF_COEFFS; i++) {
		cifisp_iowrite32(pconfig->nll.coeff[i],
				 CIF_ISP_DPF_NULL_COEFF_0 + i * 4);
	}

	spatial_coeff = pconfig->g_flt.spatial_coeff[0] |
		((unsigned int)pconfig->g_flt.spatial_coeff[1] << 8) |
		((unsigned int)pconfig->g_flt.spatial_coeff[2] << 16) |
		((unsigned int)pconfig->g_flt.spatial_coeff[3] << 24);
	cifisp_iowrite32(spatial_coeff, CIF_ISP_DPF_S_WEIGHT_G_1_4);
	spatial_coeff = pconfig->g_flt.spatial_coeff[4] |
		((unsigned int)pconfig->g_flt.spatial_coeff[5] << 8);
	cifisp_iowrite32(spatial_coeff, CIF_ISP_DPF_S_WEIGHT_G_5_6);
	spatial_coeff = pconfig->rb_flt.spatial_coeff[0] |
		((unsigned int)pconfig->rb_flt.spatial_coeff[1] << 8) |
		((unsigned int)pconfig->rb_flt.spatial_coeff[2] << 16) |
		((unsigned int)pconfig->rb_flt.spatial_coeff[3] << 24);
	cifisp_iowrite32(spatial_coeff, CIF_ISP_DPF_S_WEIGHT_RB_1_4);
	spatial_coeff = pconfig->rb_flt.spatial_coeff[4] |
		((unsigned int)pconfig->rb_flt.spatial_coeff[5] << 8);
	cifisp_iowrite32(spatial_coeff, CIF_ISP_DPF_S_WEIGHT_RB_5_6);
}

static void cifisp_dpf_strength_config(const struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int new_id =
		isp_dev->other_cfgs.log[CIFISP_DPF_STRENGTH_ID].new_id;
	const struct cifisp_dpf_strength_config *pconfig =
		&isp_dev->other_cfgs.cfgs[new_id].dpf_strength_config;

	cifisp_iowrite32(pconfig->b, CIF_ISP_DPF_STRENGTH_B);
	cifisp_iowrite32(pconfig->g, CIF_ISP_DPF_STRENGTH_G);
	cifisp_iowrite32(pconfig->r, CIF_ISP_DPF_STRENGTH_R);
}

static void cifisp_dpf_en(struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32OR(CIFISP_DPF_MODE_EN,
			   CIF_ISP_DPF_MODE);
}

static void cifisp_dpf_end(struct cif_isp10_isp_dev *isp_dev)
{
	cifisp_iowrite32AND(~CIFISP_DPF_MODE_EN,
			    CIF_ISP_DPF_MODE);
}

/* ================== IOCTL implementation ========================= */
static int cifisp_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: p->type %d p->count %d\n",
		      ISP_VDEV_NAME, __func__, p->type, p->count);
	return vb2_ioctl_reqbufs(file, priv, p);
}

static int cifisp_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: p->type %d p->index %d\n",
		      ISP_VDEV_NAME, __func__, p->type, p->index);
	return vb2_ioctl_querybuf(file, priv, p);
}

static int cifisp_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: p->type %d p->index %d\n",
		      ISP_VDEV_NAME, __func__, p->type, p->index);
	return vb2_ioctl_qbuf(file, priv, p);
}

/* ========================================================== */

static int cifisp_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: p->type %d p->index %d\n",
		      ISP_VDEV_NAME, __func__, p->type, p->index);
	return vb2_ioctl_dqbuf(file, priv, p);
}

static int cifisp_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cif_isp10_isp_dev *isp_dev =
		video_get_drvdata(video_devdata(file));

	int ret = vb2_ioctl_streamon(file, priv, i);

	if (ret == 0)
		isp_dev->streamon = true;

	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: ret %d\n", ISP_VDEV_NAME, __func__, ret);

	return ret;
}

/* ========================================================== */
static int cifisp_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	int ret;
	struct cif_isp10_isp_dev *isp_dev =
		video_get_drvdata(video_devdata(file));

	ret = vb2_ioctl_streamoff(file, priv, i);
	if (ret == 0)
		isp_dev->streamon = false;

	CIFISP_DPRINT(CIFISP_DEBUG,
		      " %s: %s: ret %d\n", ISP_VDEV_NAME, __func__, ret);

	return ret;
}

static int cifisp_g_ctrl(struct file *file, void *priv, struct v4l2_control *vc)
{
	int ret;

	struct cif_isp10_isp_dev *isp_dev =
		video_get_drvdata(video_devdata(file));

	switch (vc->id) {
	case V4L2_CID_CIFISP_DPCC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_DPCC_ID);
		break;
	case V4L2_CID_CIFISP_BLS:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_BLS_ID);
		break;
	case V4L2_CID_CIFISP_SDG:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_SDG_ID);
		break;
	case V4L2_CID_CIFISP_LSC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_LSC_ID);
		break;
	case V4L2_CID_CIFISP_AWB_MEAS:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_AWB_ID);
		break;
	case V4L2_CID_CIFISP_AWB_GAIN:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_AWB_GAIN_ID);
		break;
	case V4L2_CID_CIFISP_FLT:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_FLT_ID);
		break;
	case V4L2_CID_CIFISP_BDM:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_BDM_ID);
		break;
	case V4L2_CID_CIFISP_CTK:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_CTK_ID);
		break;
	case V4L2_CID_CIFISP_GOC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_GOC_ID);
		break;
	case V4L2_CID_CIFISP_HST:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_HST_ID);
		break;
	case V4L2_CID_CIFISP_AEC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_AEC_ID);
		break;
	case V4L2_CID_CIFISP_CPROC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_CPROC_ID);
		break;
	case V4L2_CID_CIFISP_AFC:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_AFC_ID);
		break;
	case V4L2_CID_CIFISP_IE:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_IE_ID);
		break;
	case V4L2_CID_CIFISP_DPF:
		ret = cifisp_module_enable(
			isp_dev,
			_GET_,
			&vc->value,
			CIFISP_DPF_ID);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cifisp_s_ctrl(struct file *file, void *priv, struct v4l2_control *vc)
{
	struct cif_isp10_isp_dev *isp_dev =
		video_get_drvdata(video_devdata(file));

	return cifisp_module_enable(
		isp_dev,
		_SET_,
		&vc->value,
		vc->id - V4L2_CID_PRIVATE_BASE);
}

static long cifisp_ioctl_default(struct file *file, void *fh,
				 bool valid_prio, unsigned int cmd, void *arg)
{
	struct cif_isp10_isp_dev *isp = video_get_drvdata(video_devdata(file));
	long ret;

	switch (cmd) {
	case CIFISP_IOC_G_DPCC:
		ret = cifisp_dpcc_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_DPCC:
		ret = cifisp_dpcc_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_BLS:
		ret = cifisp_bls_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_BLS:
		ret = cifisp_bls_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_SDG:
		ret = cifisp_sdg_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_SDG:
		ret = cifisp_sdg_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_LSC:
		ret = cifisp_lsc_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_LSC:
		ret = cifisp_lsc_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_AWB_MEAS:
		ret = cifisp_awb_meas_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_AWB_MEAS:
		ret = cifisp_awb_meas_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_AWB_GAIN:
		ret = cifisp_awb_gain_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_AWB_GAIN:
		ret = cifisp_awb_gain_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_FLT:
		ret = cifisp_flt_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_FLT:
		ret = cifisp_flt_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_BDM:
		ret = cifisp_bdm_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_BDM:
		ret = cifisp_bdm_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_CTK:
		ret = cifisp_ctk_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_CTK:
		ret = cifisp_ctk_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_GOC:
		ret = cifisp_goc_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_GOC:
		ret = cifisp_goc_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_HST:
		ret = cifisp_hst_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_HST:
		ret = cifisp_hst_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_AEC:
		ret = cifisp_aec_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_AEC:
		ret = cifisp_aec_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_CPROC:
		ret = cifisp_cproc_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_CPROC:
		ret = cifisp_cproc_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_AFC:
		ret = cifisp_afc_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_AFC:
		ret = cifisp_afc_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_IE:
		ret = cifisp_ie_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_IE:
		ret = cifisp_ie_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_DPF:
		ret = cifisp_dpf_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_DPF:
		ret = cifisp_dpf_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_DPF_STRENGTH:
		ret = cifisp_dpf_strength_param(isp, _GET_, arg);
		break;
	case CIFISP_IOC_S_DPF_STRENGTH:
		ret = cifisp_dpf_strength_param(isp, _SET_, arg);
		break;
	case CIFISP_IOC_G_LAST_CONFIG:
		ret = cifisp_last_capture_config(arg);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cifisp_g_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	/*
	 * Dummy function needed to allow allocation of
	 * buffers on this device
	 */
	return 0;
}

static int cifisp_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct cif_isp10_isp_dev *isp_dev = video_get_drvdata(vdev);

	strcpy(cap->driver, DRIVER_NAME);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:" DRIVER_NAME "-%03i",
		 *isp_dev->dev_id);

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_DEVICE_CAPS;
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops cifisp_ioctl = {
	.vidioc_reqbufs = cifisp_reqbufs,
	.vidioc_querybuf = cifisp_querybuf,
	.vidioc_qbuf = cifisp_qbuf,
	.vidioc_dqbuf = cifisp_dqbuf,
	.vidioc_streamon = cifisp_streamon,
	.vidioc_streamoff = cifisp_streamoff,
	.vidioc_g_ctrl = cifisp_g_ctrl,
	.vidioc_s_ctrl = cifisp_s_ctrl,
	.vidioc_default = cifisp_ioctl_default,
	.vidioc_g_fmt_vid_cap = cifisp_g_fmt_vid_cap,
	.vidioc_querycap = cifisp_querycap
};

/* ======================================================== */

static unsigned int cifisp_poll(struct file *file,
				struct poll_table_struct *wait)
{
	unsigned int ret;

	ret = vb2_fop_poll(file, wait);

	CIFISP_DPRINT(CIFISP_DEBUG,
		      "Polling on vbq_stat buffer %d\n", ret);

	return ret;
}

/* ======================================================== */
static int cifisp_mmap(struct file *file, struct vm_area_struct *vma)
{
	return vb2_fop_mmap(file, vma);
}

/* ddl@rock-chips.com: v1.0.8 */
static int cifisp_reset(struct file *file)
{
	struct cif_isp10_isp_dev *isp_dev =
		video_get_drvdata(video_devdata(file));

	memset(&isp_dev->other_cfgs, 0, sizeof(struct cifisp_isp_other_cfg));
	memset(&isp_dev->meas_cfgs, 0, sizeof(struct cifisp_isp_meas_cfg));
	isp_dev->other_cfgs.module_updates = 0;

	isp_dev->meas_cfgs.module_updates = 0;
	isp_dev->active_lsc_width = 0;
	isp_dev->active_lsc_height = 0;

	isp_dev->streamon = false;
	isp_dev->active_meas = 0;
	isp_dev->frame_id = 0;
	isp_dev->cif_ism_cropping = false;

	isp_dev->meas_send_alone = CIFISP_MEAS_SEND_ALONE;
	isp_dev->awb_meas_ready = false;
	isp_dev->afm_meas_ready = false;
	isp_dev->aec_meas_ready = false;
	isp_dev->hst_meas_ready = false;

	isp_dev->meta_info.write_id = 0;
	isp_dev->meta_info.read_id = 0;
	return 0;
}

static int cifisp_open(struct file *file)
{
	CIFISP_DPRINT(CIFISP_DEBUG, "cifisp_open\n");

	cifisp_reset(file);

	return 0;
}

static int cifisp_close(struct file *file)
{
	CIFISP_DPRINT(CIFISP_DEBUG, "cifisp_close\n");

	vb2_fop_release(file);

	/* cifisp_reset(file); */
	return 0;
}

static int cifisp_meas_queue_work(struct cif_isp10_isp_dev *isp_dev, unsigned int send_meas)
{
	unsigned int module_updates = 0;
	struct cif_isp10_isp_readout_work *work;

	if (send_meas & CIF_ISP_AWB_DONE)
		module_updates |= CIFISP_MODULE_AWB;
	if (send_meas & CIF_ISP_AFM_FIN)
		module_updates |= CIFISP_MODULE_AFC;
	if (send_meas & CIF_ISP_EXP_END)
		module_updates |= CIFISP_MODULE_AEC;
	if (send_meas & CIF_ISP_HIST_MEASURE_RDY)
		module_updates |= CIFISP_MODULE_HST;

	if ((isp_dev->meas_cfgs.module_updates & module_updates) == 0 &&
		(isp_dev->active_meas & send_meas)) {
		work = kmalloc(sizeof(*work), GFP_ATOMIC);
		if (work) {
			INIT_WORK((struct work_struct *)work,
				cifisp_isp_readout_work);
			work->readout = CIF_ISP10_ISP_READOUT_MEAS;
			work->isp_dev = isp_dev;
			work->frame_id = isp_dev->frame_id;
			work->active_meas = send_meas;
			work->vs_t = isp_dev->vs_t;
			work->fi_t = isp_dev->fi_t;

			if (!queue_work(isp_dev->readout_wq,
				(struct work_struct *)work)) {
				CIFISP_DPRINT(CIFISP_ERROR,
					"Could not schedule work\n");
				kfree((void *)work);
			}

			CIFISP_DPRINT(CIFISP_DEBUG,
				"Send 0x%x Packet\n", send_meas);
		} else {
			CIFISP_DPRINT(CIFISP_ERROR,
				"Could not allocate work\n");
		}
	}
	return 0;
}

struct v4l2_file_operations cifisp_fops = {
	.mmap = cifisp_mmap,
	.unlocked_ioctl = video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = video_ioctl2,
#endif
	.poll = cifisp_poll,
	.open = cifisp_open,
	.release = cifisp_close
};

static void cifisp_release(struct video_device *vdev)
{
	struct cif_isp10_isp_dev *isp_dev = video_get_drvdata(vdev);

	CIFISP_DPRINT(CIFISP_DEBUG, "cifisp_release\n");
	video_device_release(vdev);
	destroy_workqueue(isp_dev->readout_wq);
}

/************************************************************/
static int cif_isp10_vb2_queue_setup(struct vb2_queue *vq,
				     const void *parg,
				     unsigned int *count,
				     unsigned int *num_planes,
				     unsigned int sizes[],
				     void *alloc_ctxs[])
{
	sizes[0] = sizeof(struct cifisp_stat_buffer);
	*num_planes = 1;

	if (!*count)
		*count = 2;

	return 0;
}

static void cif_isp10_vb2_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cif_isp10_buffer *ispbuf = to_cif_isp10_vb(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct cif_isp10_isp_dev *isp_dev = vq->drv_priv;
	unsigned long flags;

	CIFISP_DPRINT(CIFISP_DEBUG, "Queueing stat buffer!\n");
	spin_lock_irqsave(&isp_dev->irq_lock, flags);
	list_add_tail(&ispbuf->queue, &isp_dev->stat);
	spin_unlock_irqrestore(&isp_dev->irq_lock, flags);
}

static void cif_isp10_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct cif_isp10_isp_dev *isp_dev = vq->drv_priv;
	struct cif_isp10_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&isp_dev->irq_lock, flags);
	list_for_each_entry_safe(buf, tmp, &isp_dev->stat, queue) {
		list_del_init(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&isp_dev->irq_lock, flags);
}

static struct vb2_ops cif_isp10_vb2_ops = {
	.queue_setup	= cif_isp10_vb2_queue_setup,
	.buf_queue	= cif_isp10_vb2_queue,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.stop_streaming	= cif_isp10_vb2_stop_streaming,
};

static int cif_isp10_init_vb2_queue(struct vb2_queue *q,
	struct cif_isp10_isp_dev *isp_dev)
{
	memset(q, 0, sizeof(*q));

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = isp_dev;
	q->ops = &cif_isp10_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct cif_isp10_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(q);
}

/************************************************************/
int register_cifisp_device(struct cif_isp10_isp_dev *isp_dev,
			   struct video_device *vdev_cifisp,
			   struct v4l2_device *v4l2_dev,
			   void __iomem *cif_reg_baseaddress)
{
	isp_dev->base_addr = cif_reg_baseaddress;
	WARN_ON(!(isp_dev->base_addr));

	INIT_LIST_HEAD(&isp_dev->stat);
	spin_lock_init(&isp_dev->irq_lock);
	spin_lock_init(&isp_dev->config_lock);
	strlcpy(vdev_cifisp->name, ISP_VDEV_NAME, sizeof(vdev_cifisp->name));
	vdev_cifisp->vfl_type = V4L2_CAP_VIDEO_CAPTURE;
	video_set_drvdata(vdev_cifisp, isp_dev);
	vdev_cifisp->ioctl_ops = &cifisp_ioctl;
	vdev_cifisp->fops = &cifisp_fops;

	/*
	 * This might not release all resources,
	 * but unregistering is anyway not going to happen.
	 */
	vdev_cifisp->release = cifisp_release;
	mutex_init(&isp_dev->mutex);
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev_cifisp->lock = &isp_dev->mutex;
	vdev_cifisp->v4l2_dev = v4l2_dev;

	cif_isp10_init_vb2_queue(&isp_dev->vb2_vidq, isp_dev);

	vdev_cifisp->queue = &isp_dev->vb2_vidq;

	if (video_register_device(vdev_cifisp, VFL_TYPE_GRABBER, -1) < 0) {
		dev_err(&vdev_cifisp->dev,
			"could not register Video for Linux device\n");
		return -ENODEV;
	} else {
		dev_info(&vdev_cifisp->dev,
			 "successfully registered video device for cifisp(video%d)\n",
			 vdev_cifisp->minor);
	}

	CIFISP_DPRINT(CIFISP_DEBUG,
		      "%s: CIFISP vdev minor =  %d\n",
		      __func__, vdev_cifisp->minor);

	isp_dev->readout_wq =
		alloc_workqueue("measurement_queue",
				WQ_UNBOUND | WQ_MEM_RECLAIM, 1);

	if (!isp_dev->readout_wq)
		return -ENOMEM;

	isp_dev->v_blanking_us = CIFISP_MODULE_DEFAULT_VBLANKING_TIME;

	return 0;
}

void unregister_cifisp_device(struct video_device *vdev_cifisp)
{
	if (!IS_ERR_OR_NULL(vdev_cifisp))
		video_unregister_device(vdev_cifisp);
}

static void cifisp_dump_reg(struct cif_isp10_isp_dev *isp_dev, int level)
{
#ifdef CIFISP_DEBUG_REG
	if (isp_dev->dpcc_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_DPCC, level);

	if (isp_dev->lsc_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_LSC, level);

	if (isp_dev->bls_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_BLS, level);

	if (isp_dev->sdg_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_SDG, level);

	if (isp_dev->goc_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_GOC, level);

	if (isp_dev->bdm_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_BDM, level);

	if (isp_dev->flt_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_FLT, level);

	if (isp_dev->awb_meas_en || isp_dev->awb_gain_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_AWB, level);

	if (isp_dev->aec_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_AEC, level);

	if (isp_dev->ctk_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_CTK, level);

	if (isp_dev->cproc_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_CPROC, level);

	if (isp_dev->afc_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_AFC, level);

	if (isp_dev->hst_en)
		cifisp_reg_dump(isp_dev, CIFISP_MODULE_HST, level);
#endif
}

static void cifisp_set_isp_modules_actives(struct cif_isp10_isp_dev *isp_dev,
	enum cif_isp10_pix_fmt in_pix_fmt)
{
	isp_dev->other_cfgs.module_actives = 0;
	isp_dev->meas_cfgs.module_actives = 0;
	if (CIF_ISP10_PIX_FMT_IS_RAW_BAYER(in_pix_fmt)) {
		/* unlimited */
	} else if (CIF_ISP10_PIX_FMT_IS_Y_ONLY(in_pix_fmt)) {
		CIFISP_MODULE_UNACTIVE(
			isp_dev->other_cfgs.module_actives,
			CIFISP_MODULE_LSC |
			CIFISP_MODULE_AWB_GAIN |
			CIFISP_MODULE_BDM |
			CIFISP_MODULE_CTK);

		CIFISP_MODULE_UNACTIVE(
			isp_dev->meas_cfgs.module_actives,
			CIFISP_MODULE_AWB);
	} else {
		CIFISP_MODULE_UNACTIVE(
			isp_dev->other_cfgs.module_actives,
			CIFISP_MODULE_DPCC |
			CIFISP_MODULE_BLS |
			CIFISP_MODULE_SDG |
			CIFISP_MODULE_LSC |
			CIFISP_MODULE_FLT |
			CIFISP_MODULE_BDM |
			CIFISP_MODULE_CTK |
			CIFISP_MODULE_GOC |
			CIFISP_MODULE_DPF);

		CIFISP_MODULE_UNACTIVE(
			isp_dev->meas_cfgs.module_actives,
			CIFISP_MODULE_HST |
			CIFISP_MODULE_AFC |
			CIFISP_MODULE_AWB |
			CIFISP_MODULE_AEC);
	}
}

/* Not called when the camera active, thus not isr protection. */
void cifisp_configure_isp(
	struct cif_isp10_isp_dev *isp_dev,
	enum cif_isp10_pix_fmt in_pix_fmt,
	enum cif_isp10_pix_fmt_quantization quantization)
{
	unsigned int time_left = 3000;
	unsigned int i, curr_id;

	CIFISP_DPRINT(CIFISP_DEBUG, "%s\n", __func__);

	mutex_lock(&isp_dev->mutex);
	spin_lock(&isp_dev->config_lock);

	isp_dev->quantization = quantization;
	cifisp_set_isp_modules_actives(isp_dev, in_pix_fmt);
	/*
	 * Must config isp, Hardware may has been reset.
	 */
	for (i = 0; i < CIFISP_MEAS_ID; i++) {
		if (CIFISP_MODULE_IS_UNACTIVE(
			isp_dev->other_cfgs.module_actives,
			(1 << i)))
			continue;

		if (CIFISP_MODULE_IS_UPDATE(
			isp_dev->other_cfgs.module_updates,
			(1 << i)))
			continue;

		curr_id = isp_dev->other_cfgs.log[i].curr_id;
		if (CIFISP_MODULE_IS_EN(
			isp_dev->other_cfgs.cfgs[curr_id].module_ens,
			(1 << i))) {
			isp_dev->other_cfgs.log[i].new_id = curr_id;
			CIFISP_MODULE_UPDATE(
				isp_dev->other_cfgs.module_updates,
				(1 << i));

			if (i == CIFISP_DPF_ID) {
				isp_dev->other_cfgs.log[CIFISP_DPF_STRENGTH_ID].new_id = curr_id;
				CIFISP_MODULE_UPDATE(
					isp_dev->other_cfgs.module_updates,
					(1 << CIFISP_DPF_STRENGTH_ID));
			}
		}
	}
	for (i = CIFISP_MEAS_ID; i < CIFISP_MODULE_MAX; i++) {
		if (CIFISP_MODULE_IS_UNACTIVE(
			isp_dev->meas_cfgs.module_actives,
			(1 << i)))
			continue;

		if (CIFISP_MODULE_IS_UPDATE(
			isp_dev->meas_cfgs.module_updates,
			(1 << i)))
			continue;

		curr_id = isp_dev->meas_cfgs.log[i].curr_id;
		if (CIFISP_MODULE_IS_EN(
			isp_dev->meas_cfgs.cfgs[curr_id].module_ens,
			(1 << i))) {
			isp_dev->meas_cfgs.log[i].new_id = curr_id;
			CIFISP_MODULE_UPDATE(
				isp_dev->meas_cfgs.module_updates,
				(1 << i));
		}
	}
	cifisp_isp_isr_other_config(isp_dev, &time_left);
	cifisp_csm_config(isp_dev, quantization);
	cifisp_isp_isr_meas_config(isp_dev, &time_left);

	cifisp_dump_reg(isp_dev, CIFISP_DEBUG);

	spin_unlock(&isp_dev->config_lock);
	mutex_unlock(&isp_dev->mutex);
}

void cifisp_frame_in(
	struct cif_isp10_isp_dev *isp_dev,
	const struct timeval *fi_t)
{
	unsigned int write_id;
	/* Called in an interrupt context. */
	isp_dev->fi_t = *fi_t;

	write_id = isp_dev->meta_info.write_id;
	isp_dev->meta_info.fi_t[write_id] = *fi_t;
	isp_dev->meta_info.write_id = (write_id + 1) % CIF_ISP10_META_INFO_NUM;
}

void cifisp_v_start(
	struct cif_isp10_isp_dev *isp_dev,
	const struct timeval *vs_t)
{
	unsigned int write_id;
	/* Called in an interrupt context. */
	isp_dev->frame_id += 2;
	isp_dev->vs_t = *vs_t;

	write_id = isp_dev->meta_info.write_id;
	isp_dev->meta_info.frame_id[write_id] = isp_dev->frame_id;
	isp_dev->meta_info.vs_t[write_id] = *vs_t;
}

void cifisp_frame_id_reset(
	struct cif_isp10_isp_dev *isp_dev)
{
	unsigned int i;

	isp_dev->frame_id = 0;
	for (i = 0; i < CIFISP_MEAS_ID; i++) {
		memset(isp_dev->other_cfgs.log[i].s_frame_id,
		       0x00,
		       sizeof(isp_dev->other_cfgs.log[i].s_frame_id));
	}
	for (i = CIFISP_MEAS_ID; i < CIFISP_MODULE_MAX; i++) {
		memset(isp_dev->meas_cfgs.log[i].s_frame_id,
		       0x00,
		       sizeof(isp_dev->meas_cfgs.log[i].s_frame_id));
	}

	isp_dev->meta_info.read_id = 0;
	isp_dev->meta_info.write_id = 0;
}

/* Not called when the camera active, thus not isr protection. */
void cifisp_disable_isp(struct cif_isp10_isp_dev *isp_dev)
{
	CIFISP_DPRINT(CIFISP_DEBUG, "%s\n", __func__);

	mutex_lock(&isp_dev->mutex);

	cifisp_dpcc_end(isp_dev);
	cifisp_lsc_end(isp_dev);
	cifisp_bls_end(isp_dev);
	cifisp_sdg_end(isp_dev);
	cifisp_goc_end(isp_dev);
	cifisp_bdm_end(isp_dev);
	cifisp_flt_end(isp_dev);
	cifisp_awb_meas_end(isp_dev);
	cifisp_aec_end(isp_dev);
	cifisp_ctk_end(isp_dev);
	cifisp_cproc_end(isp_dev);
	cifisp_hst_end(isp_dev);
	cifisp_afc_end(isp_dev);
	cifisp_ie_end(isp_dev);
	cifisp_dpf_end(isp_dev);

	/*
	 * Isp isn't active, isp interrupt isn't enabled, spin_lock is enough;
	 */
	spin_lock(&isp_dev->config_lock);

	memset(&isp_dev->other_cfgs, 0, sizeof(struct cifisp_isp_other_cfg));
	memset(&isp_dev->meas_cfgs, 0, sizeof(struct cifisp_isp_meas_cfg));
	isp_dev->other_cfgs.module_updates = 0;
	isp_dev->meas_cfgs.module_updates = 0;
	spin_unlock(&isp_dev->config_lock);
	mutex_unlock(&isp_dev->mutex);
}

static void cifisp_send_measurement(
	struct cif_isp10_isp_dev *isp_dev,
	struct cif_isp10_isp_readout_work *meas_work)
{
	unsigned long lock_flags = 0;
	struct cif_isp10_buffer *buf = NULL;
	struct vb2_buffer *vb = NULL;
	void *mem_addr;
	unsigned int active_meas = meas_work->active_meas;
	struct cifisp_stat_buffer *stat_buf;
	struct cif_isp10_device *cif_dev =
		container_of(isp_dev, struct cif_isp10_device, isp_dev);
	struct pltfrm_cam_vcm_tim vcm_tim;
	long ret;

	spin_lock_irqsave(&isp_dev->irq_lock, lock_flags);
	if (!list_empty(&isp_dev->stat)) {
		buf = list_first_entry(&isp_dev->stat,
				       struct cif_isp10_buffer, queue);

		vb = &buf->vb.vb2_buf;
	} else {
		spin_unlock_irqrestore(&isp_dev->irq_lock, lock_flags);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "Not enought measurement bufs\n");
		goto end;
	}
	spin_unlock_irqrestore(&isp_dev->irq_lock, lock_flags);
	vb->state = VB2_BUF_STATE_ACTIVE;

	mem_addr = vb->vb2_queue->mem_ops->vaddr(
				vb->planes[0].mem_priv);
	stat_buf = (struct cifisp_stat_buffer *)mem_addr;
	memset(stat_buf, 0x00, sizeof(struct cifisp_stat_buffer));

	spin_lock_irqsave(&isp_dev->irq_lock, lock_flags);
	list_del(&buf->queue);
	spin_unlock_irqrestore(&isp_dev->irq_lock, lock_flags);

	stat_buf->meas_type = 0;
	if (active_meas & CIF_ISP_AWB_DONE) {
		memcpy(&stat_buf->params.awb,
		       &isp_dev->meas_stats.stat.params.awb,
		       sizeof(struct cifisp_awb_stat));
		stat_buf->meas_type |= CIFISP_STAT_AWB;
	}
	if (active_meas & CIF_ISP_AFM_FIN) {
		memcpy(&stat_buf->params.af,
		       &isp_dev->meas_stats.stat.params.af,
		       sizeof(struct cifisp_af_stat));
		stat_buf->meas_type |= CIFISP_STAT_AFM_FIN;
	}
	if (active_meas & CIF_ISP_EXP_END) {
		memcpy(&stat_buf->params.ae,
		       &isp_dev->meas_stats.stat.params.ae,
		       sizeof(struct cifisp_ae_stat));

		cif_isp10_sensor_mode_data_sync(cif_dev,
			meas_work->frame_id,
			&stat_buf->sensor_mode);

		stat_buf->meas_type |= CIFISP_STAT_AUTOEXP;
	}
	if (active_meas & CIF_ISP_HIST_MEASURE_RDY) {
		memcpy(&stat_buf->params.hist,
		       &isp_dev->meas_stats.stat.params.hist,
		       sizeof(struct cifisp_hist_stat));
		stat_buf->meas_type |= CIFISP_STAT_HIST;
	}

	ret = cif_isp10_img_src_ioctl(cif_dev->img_src,
				      PLTFRM_CIFCAM_GET_VCM_MOVE_RES,
				      &vcm_tim);
	if (ret == 0) {
		stat_buf->subdev_stat.vcm.vcm_start_t = vcm_tim.vcm_start_t;
		stat_buf->subdev_stat.vcm.vcm_end_t = vcm_tim.vcm_end_t;
	}

	stat_buf->vs_t = meas_work->vs_t;
	stat_buf->fi_t = meas_work->fi_t;

	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	wake_up(&vb->vb2_queue->done_wq);

	CIFISP_DPRINT(CIFISP_DEBUG,
		      "Measurement done\n");
	vb = NULL;
end:

	if (vb && (vb->state == VB2_BUF_STATE_ACTIVE))
		vb->state = VB2_BUF_STATE_QUEUED;
}

static int cifisp_s_vb_metadata(
	struct cif_isp10_isp_dev *isp_dev,
	struct cif_isp10_isp_readout_work *readout_work)
{
	struct cif_isp10_device *cif_dev =
		container_of(isp_dev, struct cif_isp10_device, isp_dev);
	struct cifisp_isp_metadata *isp_metadata =
		readout_work->isp_metadata;
	struct cifisp_isp_other_cfg *other_cfg =
		&isp_metadata->other_cfg;
	struct cifisp_isp_meas_cfg *meas_cfg =
		&isp_metadata->meas_cfg;
	struct cifisp_stat_buffer *stat_new =
		&isp_metadata->meas_stat;
	unsigned int i, j, match_id;
	unsigned long int lock_flags;

	cif_isp10_sensor_mode_data_sync(cif_dev,
		readout_work->frame_id,
		&isp_dev->meas_stats.stat.sensor_mode);

	spin_lock_irqsave(&isp_dev->config_lock, lock_flags);
	other_cfg->module_ens = 0;
	for (i = 0; i < CIFISP_MEAS_ID; i++) {
		match_id = 0xff;
		for (j = 0; j < 3; j++) {
			if (readout_work->frame_id >=
				isp_dev->other_cfgs.log[i].s_frame_id[j]) {
				if (match_id == 0xff)
					match_id = j;
				else if (isp_dev->other_cfgs.log[i].s_frame_id[match_id] <
					isp_dev->other_cfgs.log[i].s_frame_id[j])
					match_id = j;
			}
		}

		if (match_id == 0xff) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"%s: FrameID:%d isp other config haven't found! s_frame_id:%d, %d,%d\n",
				__func__,
				readout_work->frame_id,
				isp_dev->other_cfgs.log[i].s_frame_id[0],
				isp_dev->other_cfgs.log[i].s_frame_id[1],
				isp_dev->other_cfgs.log[i].s_frame_id[2]);
			match_id = isp_dev->other_cfgs.log[i].curr_id;
		}

		other_cfg->module_ens |=
			(isp_dev->other_cfgs.cfgs[match_id].module_ens &
			(1 << i));

		switch (i) {
		case CIFISP_DPCC_ID:
			memcpy(&other_cfg->dpcc_config,
			       &isp_dev->other_cfgs.cfgs[match_id].dpcc_config,
			       sizeof(other_cfg->dpcc_config));
			break;
		case CIFISP_BLS_ID:
			memcpy(&other_cfg->bls_config,
			       &isp_dev->other_cfgs.cfgs[match_id].bls_config,
			       sizeof(other_cfg->bls_config));
			break;
		case CIFISP_SDG_ID:
			memcpy(&other_cfg->sdg_config,
			       &isp_dev->other_cfgs.cfgs[match_id].sdg_config,
			       sizeof(other_cfg->sdg_config));
			break;
		case CIFISP_LSC_ID:
			memcpy(&other_cfg->lsc_config,
			       &isp_dev->other_cfgs.cfgs[match_id].lsc_config,
			       sizeof(other_cfg->lsc_config));
			break;
		case CIFISP_AWB_GAIN_ID:
			if (CIFISP_MODULE_IS_EN(other_cfg->module_ens,
						CIFISP_MODULE_AWB_GAIN)) {
				memcpy(&other_cfg->awb_gain_config,
				       &isp_dev->other_cfgs.cfgs[match_id].awb_gain_config,
				       sizeof(other_cfg->awb_gain_config));
			} else {
				unsigned int reg = cifisp_ioread32(CIF_ISP_AWB_GAIN_RB);

				other_cfg->awb_gain_config.gain_red =
					(unsigned short)CIFISP_AWB_GAIN_R_READ(reg);
				other_cfg->awb_gain_config.gain_blue =
					(unsigned short)CIFISP_AWB_GAIN_B_READ(reg);
				reg = cifisp_ioread32(CIF_ISP_AWB_GAIN_RB);
				other_cfg->awb_gain_config.gain_green_r =
					(unsigned short)CIFISP_AWB_GAIN_R_READ(reg);
				other_cfg->awb_gain_config.gain_green_b =
					(unsigned short)CIFISP_AWB_GAIN_B_READ(reg);
			}
			break;
		case CIFISP_FLT_ID:
			memcpy(&other_cfg->flt_config,
			       &isp_dev->other_cfgs.cfgs[match_id].flt_config,
			       sizeof(other_cfg->flt_config));
			break;
		case CIFISP_BDM_ID:
			memcpy(&other_cfg->bdm_config,
			       &isp_dev->other_cfgs.cfgs[match_id].bdm_config,
			       sizeof(other_cfg->bdm_config));
			break;
		case CIFISP_CTK_ID:
			if (CIFISP_MODULE_IS_EN(other_cfg->module_ens,
						CIFISP_MODULE_CTK)) {
				memcpy(&other_cfg->ctk_config,
				       &isp_dev->other_cfgs.cfgs[match_id].ctk_config,
				       sizeof(other_cfg->ctk_config));
			} else {
				other_cfg->ctk_config.coeff0 = cifisp_ioread32(CIF_ISP_CT_COEFF_0);
				other_cfg->ctk_config.coeff1 = cifisp_ioread32(CIF_ISP_CT_COEFF_1);
				other_cfg->ctk_config.coeff2 = cifisp_ioread32(CIF_ISP_CT_COEFF_2);
				other_cfg->ctk_config.coeff3 = cifisp_ioread32(CIF_ISP_CT_COEFF_3);
				other_cfg->ctk_config.coeff4 = cifisp_ioread32(CIF_ISP_CT_COEFF_4);
				other_cfg->ctk_config.coeff5 = cifisp_ioread32(CIF_ISP_CT_COEFF_5);
				other_cfg->ctk_config.coeff6 = cifisp_ioread32(CIF_ISP_CT_COEFF_6);
				other_cfg->ctk_config.coeff7 = cifisp_ioread32(CIF_ISP_CT_COEFF_7);
				other_cfg->ctk_config.coeff8 = cifisp_ioread32(CIF_ISP_CT_COEFF_8);
			}
			break;
		case CIFISP_GOC_ID:
			memcpy(&other_cfg->goc_config,
			       &isp_dev->other_cfgs.cfgs[match_id].goc_config,
			       sizeof(other_cfg->goc_config));
			break;
		case CIFISP_CPROC_ID:
			memcpy(&other_cfg->cproc_config,
			       &isp_dev->other_cfgs.cfgs[match_id].cproc_config,
			       sizeof(other_cfg->cproc_config));
			break;
		case CIFISP_IE_ID:
			memcpy(&other_cfg->ie_config,
			       &isp_dev->other_cfgs.cfgs[match_id].ie_config,
			       sizeof(other_cfg->ie_config));
			break;
		case CIFISP_DPF_ID:
			memcpy(&other_cfg->dpf_config,
			       &isp_dev->other_cfgs.cfgs[match_id].dpf_config,
			       sizeof(other_cfg->dpf_config));
			break;
		case CIFISP_DPF_STRENGTH_ID:
			memcpy(&other_cfg->dpf_strength_config,
			       &isp_dev->other_cfgs.cfgs[match_id].dpf_strength_config,
			       sizeof(other_cfg->dpf_strength_config));
			break;
		default:
			break;
		}
	}

	meas_cfg->module_ens = 0;
	for (i = CIFISP_MEAS_ID; i < CIFISP_MODULE_MAX; i++) {
		match_id = 0xff;
		for (j = 0; j < 3; j++) {
			if (readout_work->frame_id >=
				isp_dev->meas_cfgs.log[i].s_frame_id[j]) {
				if (match_id == 0xff)
					match_id = j;
				else if (isp_dev->meas_cfgs.log[i].s_frame_id[match_id] <
					 isp_dev->meas_cfgs.log[i].s_frame_id[j])
					match_id = j;
			}
		}

		if (match_id == 0xff) {
			CIFISP_DPRINT(CIFISP_ERROR,
				"%s: FrameID:%d isp meas config haven't found! s_frame_id:%d, %d,%d\n",
				__func__,
				readout_work->frame_id,
				isp_dev->meas_cfgs.log[i].s_frame_id[0],
				isp_dev->meas_cfgs.log[i].s_frame_id[1],
				isp_dev->meas_cfgs.log[i].s_frame_id[2]);
			match_id = isp_dev->meas_cfgs.log[i].curr_id;
		}
		switch (i) {
		case CIFISP_AFC_ID:
			memcpy(&meas_cfg->afc_config,
			       &isp_dev->meas_cfgs.cfgs[match_id].afc_config,
			       sizeof(meas_cfg->afc_config));
			break;
		case CIFISP_AEC_ID:
			memcpy(&meas_cfg->aec_config,
			       &isp_dev->meas_cfgs.cfgs[match_id].aec_config,
			       sizeof(meas_cfg->aec_config));
			break;
		case CIFISP_AWB_ID:
			memcpy(&meas_cfg->awb_meas_config,
			       &isp_dev->meas_cfgs.cfgs[match_id].awb_meas_config,
			       sizeof(meas_cfg->awb_meas_config));
			break;
		case CIFISP_HST_ID:
			memcpy(&meas_cfg->hst_config,
			       &isp_dev->meas_cfgs.cfgs[match_id].hst_config,
			       sizeof(meas_cfg->hst_config));
			break;
		default:
			break;
		}

		meas_cfg->module_ens |=
			(isp_dev->meas_cfgs.cfgs[match_id].module_ens &
			(1 << i));
	}

	if (isp_dev->meas_stats.g_frame_id >=
		readout_work->frame_id) {
		memcpy(stat_new,
		       &isp_dev->meas_stats.stat,
		       sizeof(*stat_new));
	}
	spin_unlock_irqrestore(&isp_dev->config_lock, lock_flags);

	cif_isp10_s_vb_metadata(cif_dev, readout_work);
	return 0;
}

void cifisp_isp_readout_work(struct work_struct *work)
{
	struct cif_isp10_isp_readout_work *readout_work =
		(struct cif_isp10_isp_readout_work *)work;
	struct cif_isp10_isp_dev *isp_dev =
		readout_work->isp_dev;

	if (!isp_dev->streamon)
		return;

	switch (readout_work->readout) {
	case CIF_ISP10_ISP_READOUT_MEAS:
		cifisp_send_measurement(isp_dev, readout_work);
		break;

	case CIF_ISP10_ISP_READOUT_META: {
		cifisp_s_vb_metadata(isp_dev, readout_work);
		break;
	}

	default:
		break;
	}

	kfree((void *)work);
}

static bool cifisp_isp_isr_other_config(
	struct cif_isp10_isp_dev *isp_dev,
	unsigned int *time_left)
{
	unsigned int time_in = *time_left;
	bool config_chk;
	unsigned int new_id;
	unsigned int i, j;
	unsigned int *ens;
	unsigned int *actives = &isp_dev->other_cfgs.module_actives;

	for (i = 0; i < CIFISP_MEAS_ID; i++) {
		if (CIFISP_MODULE_IS_UNACTIVE(*actives, (1 << i)))
			continue;

		if (CIFISP_MODULE_IS_UPDATE(
			isp_dev->other_cfgs.module_updates,
			(1 << i))) {
			new_id = isp_dev->other_cfgs.log[i].new_id;
			ens = &isp_dev->other_cfgs.cfgs[new_id].module_ens;

			switch (i) {
			case CIFISP_DPCC_ID:
				/*update dpc config */
				cifisp_dpcc_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_dpcc_en(isp_dev);
				else
					cifisp_dpcc_end(isp_dev);
				*time_left -= CIFISP_MODULE_DPCC_PROC_TIME;
				break;
			case CIFISP_BLS_ID:
				/*update bls config */
				cifisp_bls_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_bls_en(isp_dev);
				else
					cifisp_bls_end(isp_dev);
				*time_left -= CIFISP_MODULE_BLS_PROC_TIME;
				break;
			case CIFISP_SDG_ID:
				/*update sdg config */
				cifisp_sdg_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_sdg_en(isp_dev);
				else
					cifisp_sdg_end(isp_dev);
				*time_left -= CIFISP_MODULE_SDG_PROC_TIME;
				break;
			case CIFISP_LSC_ID: {
				bool res = true;

				if (CIFISP_MODULE_IS_EN(*ens, (1 << i))) {
					if (!cifisp_lsc_config(isp_dev))
						res = false;
				} else {
					cifisp_lsc_end(isp_dev);
				}
				*time_left -= CIFISP_MODULE_LSC_PROC_TIME;
				break;
			}
			case CIFISP_AWB_GAIN_ID:
				/*update awb gains */
				cifisp_awb_gain_config(isp_dev);
				*time_left -= CIFISP_MODULE_AWB_GAIN_PROC_TIME;
				break;
			case CIFISP_BDM_ID:
				/*update bdm config */
				cifisp_bdm_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_bdm_en(isp_dev);
				else
					cifisp_bdm_end(isp_dev);
				*time_left -= CIFISP_MODULE_BDM_PROC_TIME;
				break;
			case CIFISP_FLT_ID:
				/*update filter config */
				cifisp_flt_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_flt_en(isp_dev);
				else
					cifisp_flt_end(isp_dev);
				*time_left -= CIFISP_MODULE_FLT_PROC_TIME;
				break;
			case CIFISP_CTK_ID:
				/*update ctk config */
				cifisp_ctk_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_ctk_en(isp_dev);
				else
					cifisp_ctk_end(isp_dev);
				*time_left -= CIFISP_MODULE_CTK_PROC_TIME;
				break;
			case CIFISP_GOC_ID:
				/*update goc config */
				cifisp_goc_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_goc_en(isp_dev);
				else
					cifisp_goc_end(isp_dev);
				*time_left -= CIFISP_MODULE_GOC_PROC_TIME;
				break;
			case CIFISP_CPROC_ID:
				/*update cprc config */
				cifisp_cproc_config(
					isp_dev,
					isp_dev->quantization);
				cifisp_csm_config(
					isp_dev,
					isp_dev->quantization);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_cproc_en(isp_dev);
				else
					cifisp_cproc_end(isp_dev);

				*time_left -= CIFISP_MODULE_CPROC_PROC_TIME;
				break;
			case CIFISP_IE_ID:
				/*update ie config */
				cifisp_ie_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_ie_en(isp_dev);
				else
					cifisp_ie_end(isp_dev);
				*time_left -= CIFISP_MODULE_IE_PROC_TIME;
				break;
			case CIFISP_DPF_ID:
				/*update dpf  config */
				cifisp_dpf_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_dpf_en(isp_dev);
				else
					cifisp_dpf_end(isp_dev);
				*time_left -= CIFISP_MODULE_DPF_TIME;
				break;
			case CIFISP_DPF_STRENGTH_ID:
				/*update dpf strength config */
				cifisp_dpf_strength_config(isp_dev);
				*time_left -= CIFISP_MODULE_DPF_STRENGTH_TIME;
				break;
			default:
				break;
			}

			isp_dev->other_cfgs.log[i].s_frame_id[new_id] =
				isp_dev->frame_id;
			isp_dev->other_cfgs.log[i].curr_id = new_id;
			new_id = 0;
			for (j = 0; j < 3; j++) {
				if (isp_dev->other_cfgs.log[i].s_frame_id[new_id] >
				    isp_dev->other_cfgs.log[i].s_frame_id[j])
					new_id = j;
			}
			isp_dev->other_cfgs.log[i].new_id = new_id;
			CIFISP_MODULE_CLR_UPDATE(
				isp_dev->other_cfgs.module_updates,
				(1 << i));
		}
	}

	config_chk = time_in > *time_left;

	return config_chk;
}

static bool cifisp_isp_isr_meas_config(
	struct cif_isp10_isp_dev *isp_dev,
	unsigned int *time_left)
{
	unsigned int time_in = *time_left;
	bool config_chk;
	unsigned int new_id;
	unsigned int i, j;
	unsigned int *ens;
	unsigned int *actives = &isp_dev->meas_cfgs.module_actives;

	for (i = CIFISP_MEAS_ID; i < CIFISP_MODULE_MAX; i++) {
		if (CIFISP_MODULE_IS_UNACTIVE(*actives, (1 << i)))
			continue;

		if (CIFISP_MODULE_IS_UPDATE(
			isp_dev->meas_cfgs.module_updates,
			(1 << i))) {
			new_id = isp_dev->meas_cfgs.log[i].new_id;
			ens = &isp_dev->meas_cfgs.cfgs[new_id].module_ens;

			switch (i) {
			case CIFISP_AWB_ID:
				/*update dpc config */
				cifisp_awb_meas_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_awb_meas_en(isp_dev);
				else
					cifisp_awb_meas_end(isp_dev);
				*time_left -= CIFISP_MODULE_AWB_PROC_TIME;
				break;
			case CIFISP_AEC_ID:
				/*update aec config */
				cifisp_aec_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_aec_en(isp_dev);
				else
					cifisp_aec_end(isp_dev);
				*time_left -= CIFISP_MODULE_AEC_PROC_TIME;
				break;
			case CIFISP_AFC_ID:
				/*update afc config */
				cifisp_afc_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_afc_en(isp_dev);
				else
					cifisp_afc_end(isp_dev);
				*time_left -= CIFISP_MODULE_AFC_PROC_TIME;
				break;
			case CIFISP_HST_ID:
				/*update hst config */
				cifisp_hst_config(isp_dev);
				if (CIFISP_MODULE_IS_EN(*ens, (1 << i)))
					cifisp_hst_en(isp_dev);
				else
					cifisp_hst_end(isp_dev);
				*time_left -= CIFISP_MODULE_HST_PROC_TIME;
				break;
			default:
				break;
			}

			isp_dev->meas_cfgs.log[i].s_frame_id[new_id] =
				isp_dev->frame_id;
			isp_dev->meas_cfgs.log[i].curr_id = new_id;
			new_id = 0;
			for (j = 0; j < 3; j++) {
				if (isp_dev->meas_cfgs.log[i].s_frame_id[new_id] >
					isp_dev->meas_cfgs.log[i].s_frame_id[j])
					new_id = j;
			}
			isp_dev->meas_cfgs.log[i].new_id = new_id;
			CIFISP_MODULE_CLR_UPDATE(
				isp_dev->meas_cfgs.module_updates,
				(1 << i));
		}
	}

	config_chk = time_in > *time_left;

	return config_chk;
}

int cifisp_isp_isr(struct cif_isp10_isp_dev *isp_dev, u32 isp_mis)
{
	unsigned int isp_mis_tmp = 0;
	unsigned int time_left = isp_dev->v_blanking_us;
	unsigned int active_meas = 0;

#ifdef LOG_ISR_EXE_TIME
	ktime_t in_t = ktime_get();
#endif
	if (isp_mis & (CIF_ISP_DATA_LOSS | CIF_ISP_PIC_SIZE_ERROR))
		return 0;

	cifisp_iowrite32(
		(isp_mis & (CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN |
		CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY)),
		CIF_ISP_ICR);
	isp_mis_tmp = cifisp_ioread32(CIF_ISP_MIS);
	if (isp_mis_tmp &
		(isp_mis & (CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN |
		CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY)))
		CIFISP_DPRINT(CIFISP_ERROR,
			"isp icr 3A info err: 0x%x\n",
			isp_mis_tmp);

	if (isp_mis & CIF_ISP_AWB_DONE) {
		isp_dev->awb_meas_ready = true;
		cifisp_get_awb_meas(isp_dev, &isp_dev->meas_stats.stat);
	}
	if (isp_mis & CIF_ISP_AFM_FIN) {
		isp_dev->afm_meas_ready = true;
		cifisp_get_afc_meas(isp_dev, &isp_dev->meas_stats.stat);
	}
	if (isp_mis & CIF_ISP_EXP_END) {
		isp_dev->aec_meas_ready = true;
		cifisp_get_aec_meas(isp_dev, &isp_dev->meas_stats.stat);
		cifisp_bls_get_meas(isp_dev, &isp_dev->meas_stats.stat);
		isp_dev->meas_stats.g_frame_id = isp_dev->frame_id;
	}

	if (isp_mis & CIF_ISP_HIST_MEASURE_RDY) {
		isp_dev->hst_meas_ready = true;
		cifisp_get_hst_meas(isp_dev, &isp_dev->meas_stats.stat);
	}

	if ((isp_dev->meas_send_alone & CIF_ISP_AWB_DONE) && isp_dev->awb_meas_ready) {
		isp_dev->awb_meas_ready = false;
		cifisp_meas_queue_work(isp_dev, CIF_ISP_AWB_DONE);
	}
	if ((isp_dev->meas_send_alone & CIF_ISP_AFM_FIN) && isp_dev->afm_meas_ready) {
		isp_dev->afm_meas_ready = false;
		cifisp_meas_queue_work(isp_dev, CIF_ISP_AFM_FIN);
	}
	if ((isp_dev->meas_send_alone & (CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY)) &&
		isp_dev->aec_meas_ready && isp_dev->hst_meas_ready) {
		isp_dev->aec_meas_ready = false;
		isp_dev->hst_meas_ready = false;
		cifisp_meas_queue_work(isp_dev, CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY);
	}

	if (isp_mis & CIF_ISP_FRAME) {
		active_meas = isp_dev->active_meas;
		active_meas &= ~isp_dev->meas_send_alone;

		if (!(isp_dev->meas_send_alone & CIF_ISP_AWB_DONE))
			isp_dev->awb_meas_ready = false;
		if (!(isp_dev->meas_send_alone & CIF_ISP_AFM_FIN))
			isp_dev->afm_meas_ready = false;
		if (!(isp_dev->meas_send_alone & CIF_ISP_EXP_END))
			isp_dev->aec_meas_ready = false;
		if (!(isp_dev->meas_send_alone & CIF_ISP_HIST_MEASURE_RDY))
			isp_dev->hst_meas_ready = false;

		cifisp_meas_queue_work(isp_dev, active_meas);

		/*
		 * Then update  changed configs. Some of them involve
		 * lot of register writes. Do those only one per frame.
		 * Do the updates in the order of the processing flow.
		 */
		spin_lock(&isp_dev->config_lock);
		if (!cifisp_isp_isr_other_config(isp_dev, &time_left))
			cifisp_isp_isr_meas_config(isp_dev, &time_left);
		spin_unlock(&isp_dev->config_lock);

		cifisp_dump_reg(isp_dev, CIFISP_DEBUG);
	}
#ifdef LOG_ISR_EXE_TIME
	if (isp_mis & (CIF_ISP_EXP_END | CIF_ISP_AWB_DONE |
		CIF_ISP_FRAME | CIF_ISP_HIST_MEASURE_RDY)) {
		unsigned int diff_us =
		    ktime_to_us(ktime_sub(ktime_get(), in_t));

		if (diff_us > g_longest_isr_time)
			g_longest_isr_time = diff_us;

		pr_info("isp_isr time %d %d\n", diff_us, g_longest_isr_time);
	}
#endif

	return 0;
}

void cifisp_clr_readout_wq(struct cif_isp10_isp_dev *isp_dev)
{
	drain_workqueue(isp_dev->readout_wq);
}

static void cifisp_param_dump(const void *config, unsigned int module)
{
#ifdef CIFISP_DEBUG_PARAM
	switch (module) {
	case CIFISP_MODULE_AWB_GAIN:{
		struct cifisp_awb_gain_config *pconfig =
		    (struct cifisp_awb_gain_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AWB Gain Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, "g_g: %d\n",
			      pconfig->gain_green_r);
		CIFISP_DPRINT(CIFISP_DEBUG, "g_b: %d\n",
			      pconfig->gain_green_b);
		CIFISP_DPRINT(CIFISP_DEBUG, "r: %d\n",
			      pconfig->gain_red);
		CIFISP_DPRINT(CIFISP_DEBUG, "b: %d\n",
			      pconfig->gain_blue);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AWB Gain Parameters - END ####\n",
			      ISP_VDEV_NAME);
		}
		break;
	case CIFISP_MODULE_DPCC:{
		}
		break;

	case CIFISP_MODULE_BLS:{
		struct cifisp_bls_config *pconfig =
		    (struct cifisp_bls_config *)config;
		struct cifisp_bls_fixed_val *pval = &pconfig->fixed_val;

		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: BLS Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " enable_auto: %d\n",
			      pconfig->enable_auto);
		CIFISP_DPRINT(CIFISP_DEBUG, " en_windows: %d\n",
			      pconfig->en_windows);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window1.h_offs: %d\n",
			      pconfig->bls_window1.h_offs);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window1.v_offs: %d\n",
			      pconfig->bls_window1.v_offs);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window1.h_size: %d\n",
			      pconfig->bls_window1.h_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window1.v_size: %d\n",
			      pconfig->bls_window1.v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window2.h_offs: %d\n",
			      pconfig->bls_window2.h_offs);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window2.v_offs: %d\n",
			      pconfig->bls_window2.v_offs);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window2.h_size: %d\n",
			      pconfig->bls_window2.h_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " bls_window2.v_size: %d\n",
			      pconfig->bls_window2.v_size);
		CIFISP_DPRINT(CIFISP_DEBUG, " bls_samples: %d\n",
			      pconfig->bls_samples);
		CIFISP_DPRINT(CIFISP_DEBUG, " fixed_A: %d\n",
			      pval->fixed_a);
		CIFISP_DPRINT(CIFISP_DEBUG, " fixed_B: %d\n",
			      pval->fixed_b);
		CIFISP_DPRINT(CIFISP_DEBUG, " fixed_C: %d\n",
			      pval->fixed_c);
		CIFISP_DPRINT(CIFISP_DEBUG, " fixed_D: %d\n",
			      pval->fixed_d);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: BLS Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;
	case CIFISP_MODULE_LSC:{
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### LSC Parameters - BEGIN ####\n");
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### LSC Parameters - END ####\n");
		}
		break;
	case CIFISP_MODULE_FLT:{
		struct cifisp_flt_config *pconfig =
		    (struct cifisp_flt_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: FLT Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_mask_sharp0: %d\n",
			      pconfig->flt_mask_sharp0);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_mask_sharp1: %d\n",
			      pconfig->flt_mask_sharp1);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_mask_diag: %d\n",
			      pconfig->flt_mask_diag);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_mask_blur_max: %d\n",
			      pconfig->flt_mask_blur_max);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_mask_blur: %d\n",
			      pconfig->flt_mask_blur);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_mask_lin: %d\n",
			      pconfig->flt_mask_lin);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_mask_orth: %d\n",
			      pconfig->flt_mask_orth);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_mask_v_diag: %d\n",
			      pconfig->flt_mask_v_diag);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_mask_h_diag: %d\n",
			      pconfig->flt_mask_h_diag);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_lum_weight: %d\n",
			      pconfig->flt_lum_weight);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_blur_th0: %d\n",
			      pconfig->flt_blur_th0);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_blur_th1: %d\n",
			      pconfig->flt_blur_th1);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_sharp0_th: %d\n",
			      pconfig->flt_sharp0_th);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_sharp1_th: %d\n",
			      pconfig->flt_sharp1_th);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_chrom_h_mode: %d\n",
			      pconfig->flt_chrom_h_mode);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_chrom_v_mode: %d\n",
			      pconfig->flt_chrom_v_mode);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " flt_diag_sharp_mode: %d\n",
			      pconfig->flt_diag_sharp_mode);
		CIFISP_DPRINT(CIFISP_DEBUG, " flt_mode: %d\n",
			      pconfig->flt_mode);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: FLT Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_BDM:{
		struct cifisp_bdm_config *pconfig =
		    (struct cifisp_bdm_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: BDM Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " demosaic_th: %d\n",
			      pconfig->demosaic_th);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: BDM Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_SDG:{
		struct cifisp_sdg_config *pconfig =
		    (struct cifisp_sdg_config *)config;
		unsigned int i;

		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: SDG Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " RED -Curve parameters\n");
		for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
			CIFISP_DPRINT(CIFISP_DEBUG,
				      " gamma_y[%d]: %d\n",
				      pconfig->curve_r.gamma_y[i]);
		}
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " GREEN -Curve parameters\n");
		for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
			CIFISP_DPRINT(CIFISP_DEBUG,
				      " gamma_y[%d]: %d\n",
				p      config->curve_g.gamma_y[i]);
		}
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " BLUE -Curve parameters\n");
		for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
			CIFISP_DPRINT(CIFISP_DEBUG,
				      " gamma_y[%d]: %d\n",
				      pconfig->curve_b.gamma_y[i]);
		}
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: SDG Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_GOC:{
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: GOC Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: GOC Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_CTK:{
		struct cifisp_ctk_config *pconfig =
			(struct cifisp_ctk_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: CTK Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff0: %d\n",
			      pconfig->coeff0);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff1: %d\n",
			      pconfig->coeff1);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff2: %d\n",
			      pconfig->coeff2);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff3: %d\n",
			      pconfig->coeff3);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff4: %d\n",
			      pconfig->coeff4);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff5: %d\n",
			      pconfig->coeff5);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff6: %d\n",
			      pconfig->coeff6);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff7: %d\n",
			      pconfig->coeff7);
		CIFISP_DPRINT(CIFISP_DEBUG, " coeff8: %d\n",
			      pconfig->coeff8);
		CIFISP_DPRINT(CIFISP_DEBUG, " ct_offset_r: %d\n",
			      pconfig->ct_offset_r);
		CIFISP_DPRINT(CIFISP_DEBUG, " ct_offset_g: %d\n",
			      pconfig->ct_offset_g);
		CIFISP_DPRINT(CIFISP_DEBUG, " ct_offset_b: %d\n",
			      pconfig->ct_offset_b);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: CTK Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_AWB:{
		struct cifisp_awb_meas_config *pconfig =
			(struct cifisp_awb_meas_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AWB Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " awb_mode: %d\n",
			      pconfig->awb_mode);
		CIFISP_DPRINT(CIFISP_DEBUG, " max_y: %d\n",
			      pconfig->max_y);
		CIFISP_DPRINT(CIFISP_DEBUG, " min_y: %d\n",
			      pconfig->min_y);
		CIFISP_DPRINT(CIFISP_DEBUG, " max_csum: %d\n",
			      pconfig->max_csum);
		CIFISP_DPRINT(CIFISP_DEBUG, " min_c: %d\n",
			      pconfig->min_c);
		CIFISP_DPRINT(CIFISP_DEBUG, " frames: %d\n",
			      pconfig->frames);
		CIFISP_DPRINT(CIFISP_DEBUG, " awb_ref_cr: %d\n",
			      pconfig->awb_ref_cr);
		CIFISP_DPRINT(CIFISP_DEBUG, " awb_ref_cb: %d\n",
			      pconfig->awb_ref_cb);
		CIFISP_DPRINT(CIFISP_DEBUG, " gb_sat: %d\n",
			      pconfig->gb_sat);
		CIFISP_DPRINT(CIFISP_DEBUG, " gr_sat: %d\n",
			      pconfig->gr_sat);
		CIFISP_DPRINT(CIFISP_DEBUG, " r_sat: %d\n",
			      pconfig->b_sat);
		CIFISP_DPRINT(CIFISP_DEBUG, " grid_h_dim: %d\n",
			      pconfig->grid_h_dim);
		CIFISP_DPRINT(CIFISP_DEBUG, " grid_v_dim: %d\n",
			      pconfig->grid_v_dim);
		CIFISP_DPRINT(CIFISP_DEBUG, " grid_h_dist: %d\n",
			      pconfig->grid_h_dist);
		CIFISP_DPRINT(CIFISP_DEBUG, " grid_v_dist: %d\n",
			      pconfig->grid_v_dist);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " enable_ymax_cmp: %d\n",
			      pconfig->enable_ymax_cmp);
		CIFISP_DPRINT(CIFISP_DEBUG, " rgb_meas_pnt: %d\n",
			      pconfig->rgb_meas_pnt);
		CIFISP_DPRINT(CIFISP_DEBUG, " AWB Window size\n");
		CIFISP_DPRINT(CIFISP_DEBUG, " h_offs: %d\n",
			      pconfig->awb_wnd.h_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_offs: %d\n",
			      pconfig->awb_wnd.v_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " h_size: %d\n",
			      pconfig->awb_wnd.h_size);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_size: %d\n",
			      pconfig->awb_wnd.v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AWB Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_HST:{
		struct cifisp_hst_config *pconfig =
			(struct cifisp_hst_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: HST Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " mode: %d\n",
			      pconfig->mode);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " histogram_predivider: %d\n",
			      pconfig->histogram_predivider);
		CIFISP_DPRINT(CIFISP_DEBUG, " HST Window size\n");
		CIFISP_DPRINT(CIFISP_DEBUG, " h_offs: %d\n",
			      pconfig->meas_window.h_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_offs: %d\n",
			      pconfig->meas_window.v_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " h_size: %d\n",
			      pconfig->meas_window.h_size);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_size: %d\n",
			      pconfig->meas_window.v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: HST Parameters - END ####\n",
			      ISP_VDEV_NAME);

		} break;

	case CIFISP_MODULE_AEC:{
		struct cifisp_aec_config *pconfig =
			(struct cifisp_aec_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AEC Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " autostop: %d\n",
			      pconfig->autostop);
		CIFISP_DPRINT(CIFISP_DEBUG, " AEC Window size\n");
		CIFISP_DPRINT(CIFISP_DEBUG, " h_offs: %d\n",
			      pconfig->meas_window.h_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_offs: %d\n",
			      pconfig->meas_window.v_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " h_size: %d\n",
			      pconfig->meas_window.h_size);
		CIFISP_DPRINT(CIFISP_DEBUG, " v_size: %d\n",
			      pconfig->meas_window.v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AEC Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;

	case CIFISP_MODULE_CPROC:{
		struct cifisp_cproc_config *pconfig =
			(struct cifisp_cproc_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: CPROC Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " contrast: %d\n",
			      pconfig->contrast);
		CIFISP_DPRINT(CIFISP_DEBUG, " hue: %d\n",
			      pconfig->hue);
		CIFISP_DPRINT(CIFISP_DEBUG, " sat: %d\n",
			      pconfig->sat);
		CIFISP_DPRINT(CIFISP_DEBUG, " brightness: %d\n",
			      pconfig->brightness);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: CPROC Parameters - END ####\n",
			      ISP_VDEV_NAME);
		} break;
	case CIFISP_MODULE_YCFLT:{
		struct cifisp_ycflt_config *pconfig =
			(struct cifisp_ycflt_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: YCFLT Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG, " ctrl: %d\n",
			      pconfig->ctrl);
		CIFISP_DPRINT(CIFISP_DEBUG, " chr_ss_ctrl: %d\n",
			      pconfig->chr_ss_ctrl);
		CIFISP_DPRINT(CIFISP_DEBUG, " chr_ss_fac: %d\n",
			      pconfig->chr_ss_fac);
		CIFISP_DPRINT(CIFISP_DEBUG, " chr_ss_offs: %d\n",
			      pconfig->chr_ss_offs);
		CIFISP_DPRINT(CIFISP_DEBUG, " chr_nr_ctrl: %d\n",
			      pconfig->chr_nr_ctrl);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_edge_gain: %d\n",
			      pconfig->lum_eenr_edge_gain);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_corner_gain: %d\n",
			      pconfig->lum_eenr_corner_gain);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_fc_crop_neg: %d\n",
			      pconfig->lum_eenr_fc_crop_neg);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_fc_crop_pos: %d\n",
			      pconfig->lum_eenr_fc_crop_pos);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_fc_gain_neg: %d\n",
			      pconfig->lum_eenr_fc_gain_neg);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " lum_eenr_fc_gain_pos: %d\n",
			      pconfig->lum_eenr_fc_gain_pos);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: YCFLT Parameters - END ####\n",
			      ISP_VDEV_NAME);
		break;
		}
	case CIFISP_MODULE_AFC:{
		struct cifisp_afc_config *pconfig =
			(struct cifisp_afc_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "#### %s: AFC Parameters - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " window A %d %d %d %d\n",
			      pconfig->afm_win[0].h_offs,
			      pconfig->afm_win[0].v_offs,
			      pconfig->afm_win[0].h_size,
			      pconfig->afm_win[0].v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " window B %d %d %d %d\n",
			      pconfig->afm_win[1].h_offs,
			      pconfig->afm_win[1].v_offs,
			      pconfig->afm_win[1].h_size,
			      pconfig->afm_win[1].v_size);
		CIFISP_DPRINT(CIFISP_DEBUG,
			      " window C %d %d %d %d\n",
			      pconfig->afm_win[2].h_offs,
			      pconfig->afm_win[2].v_offs,
			      pconfig->afm_win[2].h_size,
			      pconfig->afm_win[2].v_size);
		CIFISP_DPRINT(CIFISP_DEBUG, " thres: %d\n",
			      pconfig->thres);
		CIFISP_DPRINT(CIFISP_DEBUG, " var_shift: %d\n",
			      pconfig->var_shift);
		break;
		}
	case CIFISP_MODULE_IE: {
		struct cifisp_ie_config *pconfig =
			(struct cifisp_ie_config *)config;
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "effect %d, %x, %x, %x, %x, %x, %x %d\n",
			      pconfig->effect, pconfig->color_sel,
			      pconfig->eff_mat_1, pconfig->eff_mat_2,
			      pconfig->eff_mat_3, pconfig->eff_mat_4,
			      pconfig->eff_mat_5, pconfig->eff_tint);
		break;
		}
	default:
		CIFISP_DPRINT(CIFISP_DEBUG,
			      "####%s: Invalid Module ID ####\n", ISP_VDEV_NAME);
		break;
	}
#endif
}

#ifdef LOG_CAPTURE_PARAMS
static void cifisp_reg_dump_capture(const struct cif_isp10_isp_dev *isp_dev)
{
	memset(&g_last_capture_config, 0, sizeof(g_last_capture_config));

	if (isp_dev->bls_en) {
		g_last_capture_config.bls.fixed_val.fixed_a =
		    cifisp_ioread32(CIF_ISP_BLS_A_FIXED);
		g_last_capture_config.bls.fixed_val.fixed_b =
		    cifisp_ioread32(CIF_ISP_BLS_B_FIXED);
		g_last_capture_config.bls.fixed_val.fixed_c =
		    cifisp_ioread32(CIF_ISP_BLS_C_FIXED);
		g_last_capture_config.bls.fixed_val.fixed_d =
		    cifisp_ioread32(CIF_ISP_BLS_D_FIXED);
	}

	if (isp_dev->lsc_en)
		cifisp_lsc_config_read(isp_dev, &g_last_capture_config.lsc);

	if (isp_dev->flt_en)
		cifisp_flt_config_read(isp_dev, &g_last_capture_config.flt);

	if (isp_dev->bdm_en)
		g_last_capture_config.bdm.demosaic_th =
		    cifisp_ioread32(CIF_ISP_DEMOSAIC);

	if (isp_dev->sdg_en)
		cifisp_sdg_config_read(isp_dev, &g_last_capture_config.sdg);

	if (isp_dev->goc_en)
		cifisp_goc_config_read(isp_dev, &g_last_capture_config.goc);

	if (isp_dev->ctk_en)
		cifisp_ctk_config_read(isp_dev, &g_last_capture_config.ctk);

	if (isp_dev->awb_meas_en)
		cifisp_awb_meas_config_read(isp_dev,
					    &g_last_capture_config.awb_meas);

	if (isp_dev->awb_gain_en)
		cifisp_awb_gain_config_read(isp_dev,
					    &g_last_capture_config.awb_gain);

	if (isp_dev->cproc_en)
		cifisp_cproc_config_read(isp_dev, &g_last_capture_config.cproc);
}
#endif

#ifdef CIFISP_DEBUG_REG
static void cifisp_reg_dump(const struct cif_isp10_isp_dev *isp_dev,
			    unsigned int module, int level)
{
	switch (module) {
	case CIFISP_MODULE_DPCC:
		CIFISP_DPRINT(level, "#### BPC Registers - BEGIN ####\n");
		CIFISP_DPRINT(level, "#### BPC Registers - END ####\n");
		break;
	case CIFISP_MODULE_BLS:
		CIFISP_DPRINT(level, "#### BLS Registers - BEGIN ####\n");
		CIFISP_DPRINT(level, " CIF_ISP_BLS_CTRL: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_CTRL));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_SAMPLES: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_SAMPLES));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_H1_START: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_H1_START));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_H1_STOP: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_H1_STOP));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_H1_START: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_H1_START));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_V1_START: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_V1_START));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_V1_STOP: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_V1_STOP));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_H2_START: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_H2_START));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_H2_STOP: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_H2_STOP));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_V2_START: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_V2_START));
		CIFISP_DPRINT(level, " CIF_ISP_BLS_V2_STOP: %d\n",
			      cifisp_ioread32(CIF_ISP_BLS_V2_STOP));
		CIFISP_DPRINT(level, "#### BLS Registers - END ####\n");
		break;
	case CIFISP_MODULE_LSC:
		CIFISP_DPRINT(level, "#### LSC Registers - BEGIN ####\n");
		CIFISP_DPRINT(level, "#### LSC Registers - END ####\n");
		break;
	case CIFISP_MODULE_FLT:
		CIFISP_DPRINT(level,
			      "#### %s: FLT Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level,
			      " CIF_ISP_FILT_MODE: %d\n",
			      cifisp_ioread32(CIF_ISP_FILT_MODE));
		CIFISP_DPRINT(level,
			      " CIF_ISP_FILT_LUM_WEIGHT: %d\n",
			      cifisp_ioread32(CIF_ISP_FILT_LUM_WEIGHT));
		CIFISP_DPRINT(level,
			      "#### %s: FLT Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_BDM:
		CIFISP_DPRINT(level,
			      "#### %s: BDM Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_DEMOSAIC: %d\n",
			      cifisp_ioread32(CIF_ISP_DEMOSAIC));
		CIFISP_DPRINT(level,
			      "#### %s: BDM Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_SDG:
		CIFISP_DPRINT(level,
			      "#### %s: SDG Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_DX_LO: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_DX_LO));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_DX_HI: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_DX_HI));

		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y0: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y0));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y1: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y1));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y2: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y2));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y3: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y3));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y4: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y4));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y5: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y5));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y6: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y6));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y7: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y7));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y8: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y8));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y9: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y9));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y10: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y10));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y11: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y11));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y12: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y12));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y13: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y13));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y14: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y14));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y15: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y15));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_R_Y16: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_R_Y16));

		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y0: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y0));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y1: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y1));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y2: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y2));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y3: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y3));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y4: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y4));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y5: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y5));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y6: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y6));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y7: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y7));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y8: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y8));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y9: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y9));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y10: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y10));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y11: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y11));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y12: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y12));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y13: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y13));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y14: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y14));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y15: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y15));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_G_Y16: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_G_Y16));

		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y0: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y0));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y1: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y1));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y2: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y2));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y3: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y3));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y4: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y4));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y5: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y5));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y6: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y6));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y7: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y7));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y8: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y8));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y9: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y9));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y10: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y10));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y11: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y11));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y12: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y12));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y13: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y13));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y14: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y14));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y15: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y15));
		CIFISP_DPRINT(level, " CIF_ISP_GAMMA_B_Y16: %d\n",
			      cifisp_ioread32(CIF_ISP_GAMMA_B_Y16));
		CIFISP_DPRINT(level,
			      "#### %s: SDG Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_GOC:
		CIFISP_DPRINT(level,
			      "#### %s: GOC Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level,
			      "#### %s: GOC registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_CTK:
		CIFISP_DPRINT(level,
			      "#### %s: CTK Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_0: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_0));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_1: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_1));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_2: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_2));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_3: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_3));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_4: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_4));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_5: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_5));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_6: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_6));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_7: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_7));
		CIFISP_DPRINT(level, " CIF_ISP_CT_COEFF_8: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_COEFF_8));
		CIFISP_DPRINT(level, " CIF_ISP_CT_OFFSET_R: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_OFFSET_R));
		CIFISP_DPRINT(level, " CIF_ISP_CT_OFFSET_G: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_OFFSET_G));
		CIFISP_DPRINT(level, " CIF_ISP_CT_OFFSET_B: %d\n",
			      cifisp_ioread32(CIF_ISP_CT_OFFSET_B));
		CIFISP_DPRINT(level,
			      "#### %s: CTK Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_AWB:
		CIFISP_DPRINT(level,
			      "#### %s: AWB Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_AWB_PROP: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_PROP));
		CIFISP_DPRINT(level, " CIF_ISP_AWB_GAIN_G: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_GAIN_G));
		CIFISP_DPRINT(level, " CIF_ISP_AWB_GAIN_RB: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_GAIN_RB));
		CIFISP_DPRINT(level, " CIF_ISP_AWB_REF: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_REF));
		CIFISP_DPRINT(level, " CIF_ISP_AWB_GAIN_RB: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_PROP));
		CIFISP_DPRINT(level, " CIF_ISP_AWB_FRAMES: %x\n",
			      cifisp_ioread32(CIF_ISP_AWB_FRAMES));
		CIFISP_DPRINT(level,
			      "#### %s: AWB Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_HST:
		CIFISP_DPRINT(level,
			      "#### %s: HST Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_HIST_PROP: %d\n",
			      cifisp_ioread32(CIF_ISP_HIST_PROP));
		CIFISP_DPRINT(level, " CIF_ISP_HIST_H_OFFS: %d\n",
			      cifisp_ioread32(CIF_ISP_HIST_H_OFFS));
		CIFISP_DPRINT(level, " CIF_ISP_HIST_H_SIZE: %d\n",
			      cifisp_ioread32(CIF_ISP_HIST_H_SIZE));
		CIFISP_DPRINT(level, " CIF_ISP_HIST_V_OFFS: %d\n",
			      cifisp_ioread32(CIF_ISP_HIST_V_OFFS));
		CIFISP_DPRINT(level, " CIF_ISP_HIST_V_SIZE: %d\n",
			      cifisp_ioread32(CIF_ISP_HIST_V_SIZE));
		CIFISP_DPRINT(level,
			      "#### %s: HST Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_AEC:
		CIFISP_DPRINT(level,
			      "#### %s: AEC Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " CIF_ISP_EXP_CTRL: %d\n",
			      cifisp_ioread32(CIF_ISP_EXP_CTRL));
		CIFISP_DPRINT(level, " CIF_ISP_EXP_H_OFFSET: %d\n",
			      cifisp_ioread32(CIF_ISP_EXP_H_OFFSET));
		CIFISP_DPRINT(level, " CIF_ISP_EXP_V_OFFSET: %d\n",
			      cifisp_ioread32(CIF_ISP_EXP_V_OFFSET));
		CIFISP_DPRINT(level, " CIF_ISP_EXP_H_SIZE: %d\n",
			      cifisp_ioread32(CIF_ISP_EXP_H_SIZE));
		CIFISP_DPRINT(level, " CIF_ISP_EXP_V_SIZE: %d\n",
			      cifisp_ioread32(CIF_ISP_EXP_V_SIZE));
		CIFISP_DPRINT(level,
			      "#### %s: AEC Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;

	case CIFISP_MODULE_CPROC:
		CIFISP_DPRINT(level,
			      "#### %s: CPROC Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " ctrl: %d\n",
			      cifisp_ioread32(CIF_C_PROC_CTRL));
		CIFISP_DPRINT(level, " contrast: %d\n",
			      cifisp_ioread32(CIF_C_PROC_CONTRAST));
		CIFISP_DPRINT(level, " hue: %d\n",
			      cifisp_ioread32(CIF_C_PROC_HUE));
		CIFISP_DPRINT(level, " sat: %d\n",
			      cifisp_ioread32(CIF_C_PROC_SATURATION));
		CIFISP_DPRINT(level, " brightness: %d\n",
			      cifisp_ioread32(CIF_C_PROC_BRIGHTNESS));
		CIFISP_DPRINT(level,
			      "#### %s: CPROC Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;
	case CIFISP_MODULE_AFC:
		CIFISP_DPRINT(level,
			      "#### %s: AFC Registers - BEGIN ####\n",
			      ISP_VDEV_NAME);
		CIFISP_DPRINT(level, " afm_ctr: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_CTRL));
		CIFISP_DPRINT(level, " afm_lt_a: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_LT_A));
		CIFISP_DPRINT(level, " afm_rb_a: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_RB_A));
		CIFISP_DPRINT(level, " afm_lt_b: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_LT_B));
		CIFISP_DPRINT(level, " afm_rb_b: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_RB_B));
		CIFISP_DPRINT(level, " afm_lt_c: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_LT_C));
		CIFISP_DPRINT(level, " afm_rb_c: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_RB_C));
		CIFISP_DPRINT(level, " afm_thres: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_THRES));
		CIFISP_DPRINT(level, " afm_var_shift: %d\n",
			      cifisp_ioread32(CIF_ISP_AFM_VAR_SHIFT));
		CIFISP_DPRINT(level,
			      "#### %s: YCFLT Registers - END ####\n",
			      ISP_VDEV_NAME);
		break;
	default:
		CIFISP_DPRINT(level, "####%s: Invalid Module ID ####\n",
			      ISP_VDEV_NAME);
		break;
	}
}
#endif
