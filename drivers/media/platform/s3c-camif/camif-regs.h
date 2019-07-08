/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register definition file for s3c24xx/s3c64xx SoC CAMIF driver
 *
 * Copyright (C) 2012 Sylwester Nawrocki <sylvester.nawrocki@gmail.com>
 * Copyright (C) 2012 Tomasz Figa <tomasz.figa@gmail.com>
*/

#ifndef CAMIF_REGS_H_
#define CAMIF_REGS_H_

#include "camif-core.h"
#include <media/drv-intf/s3c_camif.h>

/*
 * The id argument indicates the processing path:
 * id = 0 - codec (FIMC C), 1 - preview (FIMC P).
 */

/* Camera input format */
#define S3C_CAMIF_REG_CISRCFMT			0x00
#define  CISRCFMT_ITU601_8BIT			(1 << 31)
#define  CISRCFMT_ITU656_8BIT			(0 << 31)
#define  CISRCFMT_ORDER422_YCBYCR		(0 << 14)
#define  CISRCFMT_ORDER422_YCRYCB		(1 << 14)
#define  CISRCFMT_ORDER422_CBYCRY		(2 << 14)
#define  CISRCFMT_ORDER422_CRYCBY		(3 << 14)
#define  CISRCFMT_ORDER422_MASK			(3 << 14)
#define  CISRCFMT_SIZE_CAM_MASK			(0x1fff << 16 | 0x1fff)

/* Window offset */
#define S3C_CAMIF_REG_CIWDOFST			0x04
#define  CIWDOFST_WINOFSEN			(1 << 31)
#define  CIWDOFST_CLROVCOFIY			(1 << 30)
#define  CIWDOFST_CLROVRLB_PR			(1 << 28)
/* #define  CIWDOFST_CLROVPRFIY			(1 << 27) */
#define  CIWDOFST_CLROVCOFICB			(1 << 15)
#define  CIWDOFST_CLROVCOFICR			(1 << 14)
#define  CIWDOFST_CLROVPRFICB			(1 << 13)
#define  CIWDOFST_CLROVPRFICR			(1 << 12)
#define  CIWDOFST_OFST_MASK			(0x7ff << 16 | 0x7ff)

/* Window offset 2 */
#define S3C_CAMIF_REG_CIWDOFST2			0x14
#define  CIWDOFST2_OFST2_MASK			(0xfff << 16 | 0xfff)

/* Global control */
#define S3C_CAMIF_REG_CIGCTRL			0x08
#define  CIGCTRL_SWRST				(1 << 31)
#define  CIGCTRL_CAMRST				(1 << 30)
#define  CIGCTRL_TESTPATTERN_NORMAL		(0 << 27)
#define  CIGCTRL_TESTPATTERN_COLOR_BAR		(1 << 27)
#define  CIGCTRL_TESTPATTERN_HOR_INC		(2 << 27)
#define  CIGCTRL_TESTPATTERN_VER_INC		(3 << 27)
#define  CIGCTRL_TESTPATTERN_MASK		(3 << 27)
#define  CIGCTRL_INVPOLPCLK			(1 << 26)
#define  CIGCTRL_INVPOLVSYNC			(1 << 25)
#define  CIGCTRL_INVPOLHREF			(1 << 24)
#define  CIGCTRL_IRQ_OVFEN			(1 << 22)
#define  CIGCTRL_HREF_MASK			(1 << 21)
#define  CIGCTRL_IRQ_LEVEL			(1 << 20)
/* IRQ_CLR_C, IRQ_CLR_P */
#define  CIGCTRL_IRQ_CLR(id)			(1 << (19 - (id)))
#define  CIGCTRL_FIELDMODE			(1 << 2)
#define  CIGCTRL_INVPOLFIELD			(1 << 1)
#define  CIGCTRL_CAM_INTERLACE			(1 << 0)

/* Y DMA output frame start address. n = 0..3. */
#define S3C_CAMIF_REG_CIYSA(id, n)		(0x18 + (id) * 0x54 + (n) * 4)
/* Cb plane output DMA start address. n = 0..3. Only codec path. */
#define S3C_CAMIF_REG_CICBSA(id, n)		(0x28 + (id) * 0x54 + (n) * 4)
/* Cr plane output DMA start address. n = 0..3. Only codec path. */
#define S3C_CAMIF_REG_CICRSA(id, n)		(0x38 + (id) * 0x54 + (n) * 4)

/* CICOTRGFMT, CIPRTRGFMT - Target format */
#define S3C_CAMIF_REG_CITRGFMT(id, _offs)	(0x48 + (id) * (0x34 + (_offs)))
#define  CITRGFMT_IN422				(1 << 31) /* only for s3c24xx */
#define  CITRGFMT_OUT422			(1 << 30) /* only for s3c24xx */
#define  CITRGFMT_OUTFORMAT_YCBCR420		(0 << 29) /* only for s3c6410 */
#define  CITRGFMT_OUTFORMAT_YCBCR422		(1 << 29) /* only for s3c6410 */
#define  CITRGFMT_OUTFORMAT_YCBCR422I		(2 << 29) /* only for s3c6410 */
#define  CITRGFMT_OUTFORMAT_RGB			(3 << 29) /* only for s3c6410 */
#define  CITRGFMT_OUTFORMAT_MASK		(3 << 29) /* only for s3c6410 */
#define  CITRGFMT_TARGETHSIZE(x)		((x) << 16)
#define  CITRGFMT_FLIP_NORMAL			(0 << 14)
#define  CITRGFMT_FLIP_X_MIRROR			(1 << 14)
#define  CITRGFMT_FLIP_Y_MIRROR			(2 << 14)
#define  CITRGFMT_FLIP_180			(3 << 14)
#define  CITRGFMT_FLIP_MASK			(3 << 14)
/* Preview path only */
#define  CITRGFMT_ROT90_PR			(1 << 13)
#define  CITRGFMT_TARGETVSIZE(x)		((x) << 0)
#define  CITRGFMT_TARGETSIZE_MASK		((0x1fff << 16) | 0x1fff)

/* CICOCTRL, CIPRCTRL. Output DMA control. */
#define S3C_CAMIF_REG_CICTRL(id, _offs)		(0x4c + (id) * (0x34 + (_offs)))
#define  CICTRL_BURST_MASK			(0xfffff << 4)
/* xBURSTn - 5-bits width */
#define  CICTRL_YBURST1(x)			((x) << 19)
#define  CICTRL_YBURST2(x)			((x) << 14)
#define  CICTRL_RGBBURST1(x)			((x) << 19)
#define  CICTRL_RGBBURST2(x)			((x) << 14)
#define  CICTRL_CBURST1(x)			((x) << 9)
#define  CICTRL_CBURST2(x)			((x) << 4)
#define  CICTRL_LASTIRQ_ENABLE			(1 << 2)
#define  CICTRL_ORDER422_MASK			(3 << 0)

/* CICOSCPRERATIO, CIPRSCPRERATIO. Pre-scaler control 1. */
#define S3C_CAMIF_REG_CISCPRERATIO(id, _offs)	(0x50 + (id) * (0x34 + (_offs)))

/* CICOSCPREDST, CIPRSCPREDST. Pre-scaler control 2. */
#define S3C_CAMIF_REG_CISCPREDST(id, _offs)	(0x54 + (id) * (0x34 + (_offs)))

/* CICOSCCTRL, CIPRSCCTRL. Main scaler control. */
#define S3C_CAMIF_REG_CISCCTRL(id, _offs)	(0x58 + (id) * (0x34 + (_offs)))
#define  CISCCTRL_SCALERBYPASS			(1 << 31)
/* s3c244x preview path only, s3c64xx both */
#define  CIPRSCCTRL_SAMPLE			(1 << 31)
/* 0 - 16-bit RGB, 1 - 24-bit RGB */
#define  CIPRSCCTRL_RGB_FORMAT_24BIT		(1 << 30) /* only for s3c244x */
#define  CIPRSCCTRL_SCALEUP_H			(1 << 29) /* only for s3c244x */
#define  CIPRSCCTRL_SCALEUP_V			(1 << 28) /* only for s3c244x */
/* s3c64xx */
#define  CISCCTRL_SCALEUP_H			(1 << 30)
#define  CISCCTRL_SCALEUP_V			(1 << 29)
#define  CISCCTRL_SCALEUP_MASK			(0x3 << 29)
#define  CISCCTRL_CSCR2Y_WIDE			(1 << 28)
#define  CISCCTRL_CSCY2R_WIDE			(1 << 27)
#define  CISCCTRL_LCDPATHEN_FIFO		(1 << 26)
#define  CISCCTRL_INTERLACE			(1 << 25)
#define  CISCCTRL_SCALERSTART			(1 << 15)
#define  CISCCTRL_INRGB_FMT_RGB565		(0 << 13)
#define  CISCCTRL_INRGB_FMT_RGB666		(1 << 13)
#define  CISCCTRL_INRGB_FMT_RGB888		(2 << 13)
#define  CISCCTRL_INRGB_FMT_MASK		(3 << 13)
#define  CISCCTRL_OUTRGB_FMT_RGB565		(0 << 11)
#define  CISCCTRL_OUTRGB_FMT_RGB666		(1 << 11)
#define  CISCCTRL_OUTRGB_FMT_RGB888		(2 << 11)
#define  CISCCTRL_OUTRGB_FMT_MASK		(3 << 11)
#define  CISCCTRL_EXTRGB_EXTENSION		(1 << 10)
#define  CISCCTRL_ONE2ONE			(1 << 9)
#define  CISCCTRL_MAIN_RATIO_MASK		(0x1ff << 16 | 0x1ff)

/* CICOTAREA, CIPRTAREA. Target area for DMA (Hsize x Vsize). */
#define S3C_CAMIF_REG_CITAREA(id, _offs)	(0x5c + (id) * (0x34 + (_offs)))
#define CITAREA_MASK				0xfffffff

/* Codec (id = 0) or preview (id = 1) path status. */
#define S3C_CAMIF_REG_CISTATUS(id, _offs)	(0x64 + (id) * (0x34 + (_offs)))
#define  CISTATUS_OVFIY_STATUS			(1 << 31)
#define  CISTATUS_OVFICB_STATUS			(1 << 30)
#define  CISTATUS_OVFICR_STATUS			(1 << 29)
#define  CISTATUS_OVF_MASK			(0x7 << 29)
#define  CIPRSTATUS_OVF_MASK			(0x3 << 30)
#define  CISTATUS_VSYNC_STATUS			(1 << 28)
#define  CISTATUS_FRAMECNT_MASK			(3 << 26)
#define  CISTATUS_FRAMECNT(__reg)		(((__reg) >> 26) & 0x3)
#define  CISTATUS_WINOFSTEN_STATUS		(1 << 25)
#define  CISTATUS_IMGCPTEN_STATUS		(1 << 22)
#define  CISTATUS_IMGCPTENSC_STATUS		(1 << 21)
#define  CISTATUS_VSYNC_A_STATUS		(1 << 20)
#define  CISTATUS_FRAMEEND_STATUS		(1 << 19) /* 17 on s3c64xx */

/* Image capture enable */
#define S3C_CAMIF_REG_CIIMGCPT(_offs)		(0xa0 + (_offs))
#define  CIIMGCPT_IMGCPTEN			(1 << 31)
#define  CIIMGCPT_IMGCPTEN_SC(id)		(1 << (30 - (id)))
/* Frame control: 1 - one-shot, 0 - free run */
#define  CIIMGCPT_CPT_FREN_ENABLE(id)		(1 << (25 - (id)))
#define  CIIMGCPT_CPT_FRMOD_ENABLE		(0 << 18)
#define  CIIMGCPT_CPT_FRMOD_CNT			(1 << 18)

/* Capture sequence */
#define S3C_CAMIF_REG_CICPTSEQ			0xc4

/* Image effects */
#define S3C_CAMIF_REG_CIIMGEFF(_offs)		(0xb0 + (_offs))
#define  CIIMGEFF_IE_ENABLE(id)			(1 << (30 + (id)))
#define  CIIMGEFF_IE_ENABLE_MASK		(3 << 30)
/* Image effect: 1 - after scaler, 0 - before scaler */
#define  CIIMGEFF_IE_AFTER_SC			(1 << 29)
#define  CIIMGEFF_FIN_MASK			(7 << 26)
#define  CIIMGEFF_FIN_BYPASS			(0 << 26)
#define  CIIMGEFF_FIN_ARBITRARY			(1 << 26)
#define  CIIMGEFF_FIN_NEGATIVE			(2 << 26)
#define  CIIMGEFF_FIN_ARTFREEZE			(3 << 26)
#define  CIIMGEFF_FIN_EMBOSSING			(4 << 26)
#define  CIIMGEFF_FIN_SILHOUETTE		(5 << 26)
#define  CIIMGEFF_PAT_CBCR_MASK			((0xff << 13) | 0xff)
#define  CIIMGEFF_PAT_CB(x)			((x) << 13)
#define  CIIMGEFF_PAT_CR(x)			(x)

/* MSCOY0SA, MSPRY0SA. Y/Cb/Cr frame start address for input DMA. */
#define S3C_CAMIF_REG_MSY0SA(id)		(0xd4 + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCB0SA(id)		(0xd8 + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCR0SA(id)		(0xdc + ((id) * 0x2c))

/* MSCOY0END, MSCOY0END. Y/Cb/Cr frame end address for input DMA. */
#define S3C_CAMIF_REG_MSY0END(id)		(0xe0 + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCB0END(id)		(0xe4 + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCR0END(id)		(0xe8 + ((id) * 0x2c))

/* MSPRYOFF, MSPRYOFF. Y/Cb/Cr offset. n: 0 - codec, 1 - preview. */
#define S3C_CAMIF_REG_MSYOFF(id)		(0x118 + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCBOFF(id)		(0x11c + ((id) * 0x2c))
#define S3C_CAMIF_REG_MSCROFF(id)		(0x120 + ((id) * 0x2c))

/* Real input DMA data size. n = 0 - codec, 1 - preview. */
#define S3C_CAMIF_REG_MSWIDTH(id)		(0xf8 + (id) * 0x2c)
#define  AUTOLOAD_ENABLE			(1 << 31)
#define  ADDR_CH_DIS				(1 << 30)
#define  MSHEIGHT(x)				(((x) & 0x3ff) << 16)
#define  MSWIDTH(x)				((x) & 0x3ff)

/* Input DMA control. n = 0 - codec, 1 - preview */
#define S3C_CAMIF_REG_MSCTRL(id)		(0xfc + (id) * 0x2c)
#define  MSCTRL_ORDER422_M_YCBYCR		(0 << 4)
#define  MSCTRL_ORDER422_M_YCRYCB		(1 << 4)
#define  MSCTRL_ORDER422_M_CBYCRY		(2 << 4)
#define  MSCTRL_ORDER422_M_CRYCBY		(3 << 4)
/* 0 - camera, 1 - DMA */
#define  MSCTRL_SEL_DMA_CAM			(1 << 3)
#define  MSCTRL_INFORMAT_M_YCBCR420		(0 << 1)
#define  MSCTRL_INFORMAT_M_YCBCR422		(1 << 1)
#define  MSCTRL_INFORMAT_M_YCBCR422I		(2 << 1)
#define  MSCTRL_INFORMAT_M_RGB			(3 << 1)
#define  MSCTRL_ENVID_M				(1 << 0)

/* CICOSCOSY, CIPRSCOSY. Scan line Y/Cb/Cr offset. */
#define S3C_CAMIF_REG_CISSY(id)			(0x12c + (id) * 0x0c)
#define S3C_CAMIF_REG_CISSCB(id)		(0x130 + (id) * 0x0c)
#define S3C_CAMIF_REG_CISSCR(id)		(0x134 + (id) * 0x0c)
#define S3C_CISS_OFFS_INITIAL(x)		((x) << 16)
#define S3C_CISS_OFFS_LINE(x)			((x) << 0)

/* ------------------------------------------------------------------ */

void camif_hw_reset(struct camif_dev *camif);
void camif_hw_clear_pending_irq(struct camif_vp *vp);
void camif_hw_clear_fifo_overflow(struct camif_vp *vp);
void camif_hw_set_lastirq(struct camif_vp *vp, int enable);
void camif_hw_set_input_path(struct camif_vp *vp);
void camif_hw_enable_scaler(struct camif_vp *vp, bool on);
void camif_hw_enable_capture(struct camif_vp *vp);
void camif_hw_disable_capture(struct camif_vp *vp);
void camif_hw_set_camera_bus(struct camif_dev *camif);
void camif_hw_set_source_format(struct camif_dev *camif);
void camif_hw_set_camera_crop(struct camif_dev *camif);
void camif_hw_set_scaler(struct camif_vp *vp);
void camif_hw_set_flip(struct camif_vp *vp);
void camif_hw_set_output_dma(struct camif_vp *vp);
void camif_hw_set_target_format(struct camif_vp *vp);
void camif_hw_set_test_pattern(struct camif_dev *camif, unsigned int pattern);
void camif_hw_set_effect(struct camif_dev *camif, unsigned int effect,
			unsigned int cr, unsigned int cb);
void camif_hw_set_output_addr(struct camif_vp *vp, struct camif_addr *paddr,
			      int index);
void camif_hw_dump_regs(struct camif_dev *camif, const char *label);

static inline u32 camif_hw_get_status(struct camif_vp *vp)
{
	return readl(vp->camif->io_base + S3C_CAMIF_REG_CISTATUS(vp->id,
								vp->offset));
}

#endif /* CAMIF_REGS_H_ */
