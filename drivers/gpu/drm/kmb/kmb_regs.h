/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright © 2018-2020 Intel Corporation
 */

#ifndef __KMB_REGS_H__
#define __KMB_REGS_H__

/***************************************************************************
 *		   LCD controller control register defines
 ***************************************************************************/
#define LCD_CONTROL				(0x4 * 0x000)
#define LCD_CTRL_PROGRESSIVE			  (0 << 0)
#define LCD_CTRL_INTERLACED			  BIT(0)
#define LCD_CTRL_ENABLE				  BIT(1)
#define LCD_CTRL_VL1_ENABLE			  BIT(2)
#define LCD_CTRL_VL2_ENABLE			  BIT(3)
#define LCD_CTRL_GL1_ENABLE			  BIT(4)
#define LCD_CTRL_GL2_ENABLE			  BIT(5)
#define LCD_CTRL_ALPHA_BLEND_VL1		  (0 << 6)
#define LCD_CTRL_ALPHA_BLEND_VL2		  BIT(6)
#define LCD_CTRL_ALPHA_BLEND_GL1		  (2 << 6)
#define LCD_CTRL_ALPHA_BLEND_GL2		  (3 << 6)
#define LCD_CTRL_ALPHA_TOP_VL1			  (0 << 8)
#define LCD_CTRL_ALPHA_TOP_VL2			  BIT(8)
#define LCD_CTRL_ALPHA_TOP_GL1			  (2 << 8)
#define LCD_CTRL_ALPHA_TOP_GL2			  (3 << 8)
#define LCD_CTRL_ALPHA_MIDDLE_VL1		  (0 << 10)
#define LCD_CTRL_ALPHA_MIDDLE_VL2		  BIT(10)
#define LCD_CTRL_ALPHA_MIDDLE_GL1		  (2 << 10)
#define LCD_CTRL_ALPHA_MIDDLE_GL2		  (3 << 10)
#define LCD_CTRL_ALPHA_BOTTOM_VL1		  (0 << 12)
#define LCD_CTRL_ALPHA_BOTTOM_VL2		  BIT(12)
#define LCD_CTRL_ALPHA_BOTTOM_GL1		  (2 << 12)
#define LCD_CTRL_ALPHA_BOTTOM_GL2		  (3 << 12)
#define LCD_CTRL_TIM_GEN_ENABLE			  BIT(14)
#define LCD_CTRL_CONTINUOUS			  (0 << 15)
#define LCD_CTRL_ONE_SHOT			  BIT(15)
#define LCD_CTRL_PWM0_EN			  BIT(16)
#define LCD_CTRL_PWM1_EN			  BIT(17)
#define LCD_CTRL_PWM2_EN			  BIT(18)
#define LCD_CTRL_OUTPUT_DISABLED		  (0 << 19)
#define LCD_CTRL_OUTPUT_ENABLED			  BIT(19)
#define LCD_CTRL_BPORCH_ENABLE			  BIT(21)
#define LCD_CTRL_FPORCH_ENABLE			  BIT(22)
#define LCD_CTRL_ALPHA_BLEND_BKGND_DISABLE	  BIT(23)
#define LCD_CTRL_PIPELINE_DMA			  BIT(28)
#define LCD_CTRL_VHSYNC_IDLE_LVL		  BIT(31)
#define LCD_CTRL_ALPHA_ALL			  (0xff << 6)

/* interrupts */
#define LCD_INT_STATUS				(0x4 * 0x001)
#define LCD_INT_EOF				  BIT(0)
#define LCD_INT_LINE_CMP			  BIT(1)
#define LCD_INT_VERT_COMP			  BIT(2)
#define LAYER0_DMA_DONE				  BIT(3)
#define LAYER0_DMA_IDLE				  BIT(4)
#define LAYER0_DMA_FIFO_OVERFLOW		  BIT(5)
#define LAYER0_DMA_FIFO_UNDERFLOW		  BIT(6)
#define LAYER0_DMA_CB_FIFO_OVERFLOW		  BIT(7)
#define LAYER0_DMA_CB_FIFO_UNDERFLOW		  BIT(8)
#define LAYER0_DMA_CR_FIFO_OVERFLOW		  BIT(9)
#define LAYER0_DMA_CR_FIFO_UNDERFLOW		  BIT(10)
#define LAYER1_DMA_DONE				  BIT(11)
#define LAYER1_DMA_IDLE				  BIT(12)
#define LAYER1_DMA_FIFO_OVERFLOW		  BIT(13)
#define LAYER1_DMA_FIFO_UNDERFLOW		  BIT(14)
#define LAYER1_DMA_CB_FIFO_OVERFLOW		  BIT(15)
#define LAYER1_DMA_CB_FIFO_UNDERFLOW		  BIT(16)
#define LAYER1_DMA_CR_FIFO_OVERFLOW		  BIT(17)
#define LAYER1_DMA_CR_FIFO_UNDERFLOW		  BIT(18)
#define LAYER2_DMA_DONE				  BIT(19)
#define LAYER2_DMA_IDLE				  BIT(20)
#define LAYER2_DMA_FIFO_OVERFLOW		  BIT(21)
#define LAYER2_DMA_FIFO_UNDERFLOW		  BIT(22)
#define LAYER3_DMA_DONE				  BIT(23)
#define LAYER3_DMA_IDLE				  BIT(24)
#define LAYER3_DMA_FIFO_OVERFLOW		  BIT(25)
#define LAYER3_DMA_FIFO_UNDERFLOW		  BIT(26)
#define LCD_INT_LAYER				  (0x07fffff8)
#define LCD_INT_ENABLE				(0x4 * 0x002)
#define LCD_INT_CLEAR				(0x4 * 0x003)
#define LCD_LINE_COUNT				(0x4 * 0x004)
#define LCD_LINE_COMPARE			(0x4 * 0x005)
#define LCD_VSTATUS				(0x4 * 0x006)

/*LCD_VSTATUS_COMPARE Vertcal interval in which to generate vertcal
 * interval interrupt
 */
/* BITS 13 and 14 */
#define LCD_VSTATUS_COMPARE			(0x4 * 0x007)
#define LCD_VSTATUS_VERTICAL_STATUS_MASK	  (3 << 13)
#define LCD_VSTATUS_COMPARE_VSYNC		  (0 << 13)
#define LCD_VSTATUS_COMPARE_BACKPORCH		  BIT(13)
#define LCD_VSTATUS_COMPARE_ACTIVE		  (2 << 13)
#define LCD_VSTATUS_COMPARE_FRONT_PORCH		  (3 << 13)

#define LCD_SCREEN_WIDTH			(0x4 * 0x008)
#define LCD_SCREEN_HEIGHT			(0x4 * 0x009)
#define LCD_FIELD_INT_CFG			(0x4 * 0x00a)
#define LCD_FIFO_FLUSH				(0x4 * 0x00b)
#define LCD_BG_COLOUR_LS			(0x4 * 0x00c)
#define LCD_BG_COLOUR_MS			(0x4 * 0x00d)
#define LCD_RAM_CFG			        (0x4 * 0x00e)

/****************************************************************************
 *		   LCD controller Layer config register
 ***************************************************************************/
#define LCD_LAYER0_CFG		        (0x4 * 0x100)
#define LCD_LAYERn_CFG(N)			(LCD_LAYER0_CFG + (0x400 * (N)))
#define LCD_LAYER_SCALE_H			BIT(1)
#define LCD_LAYER_SCALE_V			BIT(2)
#define LCD_LAYER_SCALE_H_V			(LCD_LAYER_SCALE_H | \
						      LCD_LAYER_SCALE_V)
#define LCD_LAYER_CSC_EN			BIT(3)
#define LCD_LAYER_ALPHA_STATIC			BIT(4)
#define LCD_LAYER_ALPHA_EMBED			BIT(5)
#define LCD_LAYER_ALPHA_COMBI			(LCD_LAYER_ALPHA_STATIC | \
						      LCD_LAYER_ALPHA_EMBED)
#define LCD_LAYER_ALPHA_DISABLED		~(LCD_LAYER_ALPHA_COMBI)
/* RGB multiplied with alpha */
#define LCD_LAYER_ALPHA_PREMULT			BIT(6)
#define LCD_LAYER_INVERT_COL			BIT(7)
#define LCD_LAYER_TRANSPARENT_EN		BIT(8)
#define LCD_LAYER_FORMAT_YCBCR444PLAN		(0 << 9)
#define LCD_LAYER_FORMAT_YCBCR422PLAN		BIT(9)
#define LCD_LAYER_FORMAT_YCBCR420PLAN		(2 << 9)
#define LCD_LAYER_FORMAT_RGB888PLAN		(3 << 9)
#define LCD_LAYER_FORMAT_YCBCR444LIN		(4 << 9)
#define LCD_LAYER_FORMAT_YCBCR422LIN		(5 << 9)
#define LCD_LAYER_FORMAT_RGB888			(6 << 9)
#define LCD_LAYER_FORMAT_RGBA8888		(7 << 9)
#define LCD_LAYER_FORMAT_RGBX8888		(8 << 9)
#define LCD_LAYER_FORMAT_RGB565			(9 << 9)
#define LCD_LAYER_FORMAT_RGBA1555		(0xa << 9)
#define LCD_LAYER_FORMAT_XRGB1555		(0xb << 9)
#define LCD_LAYER_FORMAT_RGB444			(0xc << 9)
#define LCD_LAYER_FORMAT_RGBA4444		(0xd << 9)
#define LCD_LAYER_FORMAT_RGBX4444		(0xe << 9)
#define LCD_LAYER_FORMAT_RGB332			(0xf << 9)
#define LCD_LAYER_FORMAT_RGBA3328		(0x10 << 9)
#define LCD_LAYER_FORMAT_RGBX3328		(0x11 << 9)
#define LCD_LAYER_FORMAT_CLUT			(0x12 << 9)
#define LCD_LAYER_FORMAT_NV12			(0x1c << 9)
#define LCD_LAYER_PLANAR_STORAGE		BIT(14)
#define LCD_LAYER_8BPP				(0 << 15)
#define LCD_LAYER_16BPP				BIT(15)
#define LCD_LAYER_24BPP				(2 << 15)
#define LCD_LAYER_32BPP				(3 << 15)
#define LCD_LAYER_Y_ORDER			BIT(17)
#define LCD_LAYER_CRCB_ORDER			BIT(18)
#define LCD_LAYER_BGR_ORDER			BIT(19)
#define LCD_LAYER_LUT_2ENT			(0 << 20)
#define LCD_LAYER_LUT_4ENT			BIT(20)
#define LCD_LAYER_LUT_16ENT			(2 << 20)
#define LCD_LAYER_NO_FLIP			(0 << 22)
#define LCD_LAYER_FLIP_V			BIT(22)
#define LCD_LAYER_FLIP_H			(2 << 22)
#define LCD_LAYER_ROT_R90			(3 << 22)
#define LCD_LAYER_ROT_L90			(4 << 22)
#define LCD_LAYER_ROT_180			(5 << 22)
#define LCD_LAYER_FIFO_00			(0 << 25)
#define LCD_LAYER_FIFO_25			BIT(25)
#define LCD_LAYER_FIFO_50			(2 << 25)
#define LCD_LAYER_FIFO_100			(3 << 25)
#define LCD_LAYER_INTERLEAVE_DIS		(0 << 27)
#define LCD_LAYER_INTERLEAVE_V			BIT(27)
#define LCD_LAYER_INTERLEAVE_H			(2 << 27)
#define LCD_LAYER_INTERLEAVE_CH			(3 << 27)
#define LCD_LAYER_INTERLEAVE_V_SUB		(4 << 27)
#define LCD_LAYER_INTERLEAVE_H_SUB		(5 << 27)
#define LCD_LAYER_INTERLEAVE_CH_SUB		(6 << 27)
#define LCD_LAYER_INTER_POS_EVEN		(0 << 30)
#define LCD_LAYER_INTER_POS_ODD			BIT(30)

#define LCD_LAYER0_COL_START		(0x4 * 0x101)
#define LCD_LAYERn_COL_START(N)		(LCD_LAYER0_COL_START + (0x400 * (N)))
#define LCD_LAYER0_ROW_START		(0x4 * 0x102)
#define LCD_LAYERn_ROW_START(N)		(LCD_LAYER0_ROW_START + (0x400 * (N)))
#define LCD_LAYER0_WIDTH		(0x4 * 0x103)
#define LCD_LAYERn_WIDTH(N)		(LCD_LAYER0_WIDTH + (0x400 * (N)))
#define LCD_LAYER0_HEIGHT		(0x4 * 0x104)
#define LCD_LAYERn_HEIGHT(N)		(LCD_LAYER0_HEIGHT + (0x400 * (N)))
#define LCD_LAYER0_SCALE_CFG		(0x4 * 0x105)
#define LCD_LAYERn_SCALE_CFG(N)		(LCD_LAYER0_SCALE_CFG + (0x400 * (N)))
#define LCD_LAYER0_ALPHA		(0x4 * 0x106)
#define LCD_LAYERn_ALPHA(N)		(LCD_LAYER0_ALPHA + (0x400 * (N)))
#define LCD_LAYER0_INV_COLOUR_LS	(0x4 * 0x107)
#define LCD_LAYERn_INV_COLOUR_LS(N)	(LCD_LAYER0_INV_COLOUR_LS + \
					(0x400 * (N)))
#define LCD_LAYER0_INV_COLOUR_MS	(0x4 * 0x108)
#define LCD_LAYERn_INV_COLOUR_MS(N)	(LCD_LAYER0_INV_COLOUR_MS + \
					(0x400 * (N)))
#define LCD_LAYER0_TRANS_COLOUR_LS	(0x4 * 0x109)
#define LCD_LAYERn_TRANS_COLOUR_LS(N)	(LCD_LAYER0_TRANS_COLOUR_LS + \
					(0x400 * (N)))
#define LCD_LAYER0_TRANS_COLOUR_MS	(0x4 * 0x10a)
#define LCD_LAYERn_TRANS_COLOUR_MS(N)	(LCD_LAYER0_TRANS_COLOUR_MS + \
					(0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF11		(0x4 * 0x10b)
#define LCD_LAYERn_CSC_COEFF11(N)	(LCD_LAYER0_CSC_COEFF11 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF12		(0x4 * 0x10c)
#define LCD_LAYERn_CSC_COEFF12(N)	(LCD_LAYER0_CSC_COEFF12 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF13		(0x4 * 0x10d)
#define LCD_LAYERn_CSC_COEFF13(N)	(LCD_LAYER0_CSC_COEFF13 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF21		(0x4 * 0x10e)
#define LCD_LAYERn_CSC_COEFF21(N)	(LCD_LAYER0_CSC_COEFF21 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF22		(0x4 * 0x10f)
#define LCD_LAYERn_CSC_COEFF22(N)	(LCD_LAYER0_CSC_COEFF22 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF23		(0x4 * 0x110)
#define LCD_LAYERn_CSC_COEFF23(N)	(LCD_LAYER0_CSC_COEFF23 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF31		(0x4 * 0x111)
#define LCD_LAYERn_CSC_COEFF31(N)	(LCD_LAYER0_CSC_COEFF31 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF32		(0x4 * 0x112)
#define LCD_LAYERn_CSC_COEFF32(N)	(LCD_LAYER0_CSC_COEFF32 + (0x400 * (N)))
#define LCD_LAYER0_CSC_COEFF33		(0x4 * 0x113)
#define LCD_LAYERn_CSC_COEFF33(N)	(LCD_LAYER0_CSC_COEFF33 + (0x400 * (N)))
#define LCD_LAYER0_CSC_OFF1		(0x4 * 0x114)
#define LCD_LAYERn_CSC_OFF1(N)		(LCD_LAYER0_CSC_OFF1 + (0x400 * (N)))
#define LCD_LAYER0_CSC_OFF2		(0x4 * 0x115)
#define LCD_LAYERn_CSC_OFF2(N)		(LCD_LAYER0_CSC_OFF2 + (0x400 * (N)))
#define LCD_LAYER0_CSC_OFF3		(0x4 * 0x116)
#define LCD_LAYERn_CSC_OFF3(N)		(LCD_LAYER0_CSC_OFF3 + (0x400 * (N)))

/* LCD controller Layer DMA config register */
#define LCD_LAYER0_DMA_CFG			(0x4 * 0x117)
#define LCD_LAYERn_DMA_CFG(N)			(LCD_LAYER0_DMA_CFG + \
						(0x400 * (N)))
#define LCD_DMA_LAYER_ENABLE			  BIT(0)
#define LCD_DMA_LAYER_STATUS			  BIT(1)
#define LCD_DMA_LAYER_AUTO_UPDATE		  BIT(2)
#define LCD_DMA_LAYER_CONT_UPDATE		  BIT(3)
#define LCD_DMA_LAYER_CONT_PING_PONG_UPDATE	  (LCD_DMA_LAYER_AUTO_UPDATE \
						| LCD_DMA_LAYER_CONT_UPDATE)
#define LCD_DMA_LAYER_FIFO_ADR_MODE		  BIT(4)
#define LCD_DMA_LAYER_AXI_BURST_1		  BIT(5)
#define LCD_DMA_LAYER_AXI_BURST_2		  (2 << 5)
#define LCD_DMA_LAYER_AXI_BURST_3		  (3 << 5)
#define LCD_DMA_LAYER_AXI_BURST_4		  (4 << 5)
#define LCD_DMA_LAYER_AXI_BURST_5		  (5 << 5)
#define LCD_DMA_LAYER_AXI_BURST_6		  (6 << 5)
#define LCD_DMA_LAYER_AXI_BURST_7		  (7 << 5)
#define LCD_DMA_LAYER_AXI_BURST_8		  (8 << 5)
#define LCD_DMA_LAYER_AXI_BURST_9		  (9 << 5)
#define LCD_DMA_LAYER_AXI_BURST_10		  (0xa << 5)
#define LCD_DMA_LAYER_AXI_BURST_11		  (0xb << 5)
#define LCD_DMA_LAYER_AXI_BURST_12		  (0xc << 5)
#define LCD_DMA_LAYER_AXI_BURST_13		  (0xd << 5)
#define LCD_DMA_LAYER_AXI_BURST_14		  (0xe << 5)
#define LCD_DMA_LAYER_AXI_BURST_15		  (0xf << 5)
#define LCD_DMA_LAYER_AXI_BURST_16		  (0x10 << 5)
#define LCD_DMA_LAYER_VSTRIDE_EN		  BIT(10)

#define LCD_LAYER0_DMA_START_ADR		(0x4 * 0x118)
#define LCD_LAYERn_DMA_START_ADDR(N)		(LCD_LAYER0_DMA_START_ADR \
						+ (0x400 * (N)))
#define LCD_LAYER0_DMA_START_SHADOW		(0x4 * 0x119)
#define LCD_LAYERn_DMA_START_SHADOW(N)		(LCD_LAYER0_DMA_START_SHADOW \
						+ (0x400 * (N)))
#define LCD_LAYER0_DMA_LEN			(0x4 * 0x11a)
#define LCD_LAYERn_DMA_LEN(N)			(LCD_LAYER0_DMA_LEN + \
						(0x400 * (N)))
#define LCD_LAYER0_DMA_LEN_SHADOW		(0x4 * 0x11b)
#define LCD_LAYERn_DMA_LEN_SHADOW(N)		(LCD_LAYER0_DMA_LEN_SHADOW + \
						(0x400 * (N)))
#define LCD_LAYER0_DMA_STATUS			(0x4 * 0x11c)
#define LCD_LAYERn_DMA_STATUS(N)		(LCD_LAYER0_DMA_STATUS + \
						(0x400 * (N)))
#define LCD_LAYER0_DMA_LINE_WIDTH		(0x4 * 0x11d)
#define LCD_LAYERn_DMA_LINE_WIDTH(N)		(LCD_LAYER0_DMA_LINE_WIDTH + \
						(0x400 * (N)))
#define LCD_LAYER0_DMA_LINE_VSTRIDE		(0x4 * 0x11e)
#define LCD_LAYERn_DMA_LINE_VSTRIDE(N)		(LCD_LAYER0_DMA_LINE_VSTRIDE +\
						(0x400 * (N)))
#define LCD_LAYER0_DMA_FIFO_STATUS		(0x4 * 0x11f)
#define LCD_LAYERn_DMA_FIFO_STATUS(N)		(LCD_LAYER0_DMA_FIFO_STATUS + \
						(0x400 * (N)))
#define LCD_LAYER0_CFG2				(0x4 * 0x120)
#define LCD_LAYERn_CFG2(N)			(LCD_LAYER0_CFG2 + (0x400 * (N)))
#define LCD_LAYER0_DMA_START_CB_ADR		(0x4 * 0x700)
#define LCD_LAYERn_DMA_START_CB_ADR(N)		(LCD_LAYER0_DMA_START_CB_ADR + \
						(0x20 * (N)))
#define LCD_LAYER0_DMA_START_CB_SHADOW		(0x4 * 0x701)
#define LCD_LAYERn_DMA_START_CB_SHADOW(N)	(LCD_LAYER0_DMA_START_CB_SHADOW\
						+ (0x20 * (N)))
#define LCD_LAYER0_DMA_CB_LINE_WIDTH		(0x4 * 0x702)
#define LCD_LAYERn_DMA_CB_LINE_WIDTH(N)		(LCD_LAYER0_DMA_CB_LINE_WIDTH +\
						(0x20 * (N)))
#define LCD_LAYER0_DMA_CB_LINE_VSTRIDE		(0x4 * 0x703)
#define LCD_LAYERn_DMA_CB_LINE_VSTRIDE(N)	(LCD_LAYER0_DMA_CB_LINE_VSTRIDE\
						+ (0x20 * (N)))
#define LCD_LAYER0_DMA_START_CR_ADR		(0x4 * 0x704)
#define LCD_LAYERn_DMA_START_CR_ADR(N)		(LCD_LAYER0_DMA_START_CR_ADR + \
						(0x20 * (N)))
#define LCD_LAYER0_DMA_START_CR_SHADOW		(0x4 * 0x705)
#define LCD_LAYERn_DMA_START_CR_SHADOW(N)	\
						(LCD_LAYER0_DMA_START_CR_SHADOW\
						 + (0x20 * (N)))
#define LCD_LAYER0_DMA_CR_LINE_WIDTH		(0x4 * 0x706)
#define LCD_LAYERn_DMA_CR_LINE_WIDTH(N)		(LCD_LAYER0_DMA_CR_LINE_WIDTH +\
						(0x20 * (N)))
#define LCD_LAYER0_DMA_CR_LINE_VSTRIDE		(0x4 * 0x707)
#define LCD_LAYERn_DMA_CR_LINE_VSTRIDE(N)	(LCD_LAYER0_DMA_CR_LINE_VSTRIDE\
						+ (0x20 * (N)))
#define LCD_LAYER1_DMA_START_CB_ADR		(0x4 * 0x708)
#define LCD_LAYER1_DMA_START_CB_SHADOW		(0x4 * 0x709)
#define LCD_LAYER1_DMA_CB_LINE_WIDTH		(0x4 * 0x70a)
#define LCD_LAYER1_DMA_CB_LINE_VSTRIDE		(0x4 * 0x70b)
#define LCD_LAYER1_DMA_START_CR_ADR		(0x4 * 0x70c)
#define LCD_LAYER1_DMA_START_CR_SHADOW		(0x4 * 0x70d)
#define LCD_LAYER1_DMA_CR_LINE_WIDTH		(0x4 * 0x70e)
#define LCD_LAYER1_DMA_CR_LINE_VSTRIDE		(0x4 * 0x70f)

/****************************************************************************
 *		   LCD controller output format register defines
 ***************************************************************************/
#define LCD_OUT_FORMAT_CFG			(0x4 * 0x800)
#define LCD_OUTF_FORMAT_RGB121212                 (0x00)
#define LCD_OUTF_FORMAT_RGB101010                 (0x01)
#define LCD_OUTF_FORMAT_RGB888                    (0x02)
#define LCD_OUTF_FORMAT_RGB666                    (0x03)
#define LCD_OUTF_FORMAT_RGB565                    (0x04)
#define LCD_OUTF_FORMAT_RGB444                    (0x05)
#define LCD_OUTF_FORMAT_MRGB121212                (0x10)
#define LCD_OUTF_FORMAT_MRGB101010                (0x11)
#define LCD_OUTF_FORMAT_MRGB888                   (0x12)
#define LCD_OUTF_FORMAT_MRGB666                   (0x13)
#define LCD_OUTF_FORMAT_MRGB565                   (0x14)
#define LCD_OUTF_FORMAT_YCBCR420_8B_LEGACY        (0x08)
#define LCD_OUTF_FORMAT_YCBCR420_8B_DCI           (0x09)
#define LCD_OUTF_FORMAT_YCBCR420_8B               (0x0A)
#define LCD_OUTF_FORMAT_YCBCR420_10B              (0x0B)
#define LCD_OUTF_FORMAT_YCBCR420_12B              (0x0C)
#define LCD_OUTF_FORMAT_YCBCR422_8B               (0x0D)
#define LCD_OUTF_FORMAT_YCBCR422_10B              (0x0E)
#define LCD_OUTF_FORMAT_YCBCR444                  (0x0F)
#define LCD_OUTF_FORMAT_MYCBCR420_8B_LEGACY       (0x18)
#define LCD_OUTF_FORMAT_MYCBCR420_8B_DCI          (0x19)
#define LCD_OUTF_FORMAT_MYCBCR420_8B              (0x1A)
#define LCD_OUTF_FORMAT_MYCBCR420_10B             (0x1B)
#define LCD_OUTF_FORMAT_MYCBCR420_12B             (0x1C)
#define LCD_OUTF_FORMAT_MYCBCR422_8B              (0x1D)
#define LCD_OUTF_FORMAT_MYCBCR422_10B             (0x1E)
#define LCD_OUTF_FORMAT_MYCBCR444                 (0x1F)
#define LCD_OUTF_BGR_ORDER			  BIT(5)
#define LCD_OUTF_Y_ORDER			  BIT(6)
#define LCD_OUTF_CRCB_ORDER			  BIT(7)
#define LCD_OUTF_SYNC_MODE			  BIT(11)
#define LCD_OUTF_RGB_CONV_MODE			  BIT(14)
#define LCD_OUTF_MIPI_RGB_MODE			  BIT(18)

#define LCD_HSYNC_WIDTH				(0x4 * 0x801)
#define LCD_H_BACKPORCH				(0x4 * 0x802)
#define LCD_H_ACTIVEWIDTH			(0x4 * 0x803)
#define LCD_H_FRONTPORCH			(0x4 * 0x804)
#define LCD_VSYNC_WIDTH				(0x4 * 0x805)
#define LCD_V_BACKPORCH				(0x4 * 0x806)
#define LCD_V_ACTIVEHEIGHT			(0x4 * 0x807)
#define LCD_V_FRONTPORCH			(0x4 * 0x808)
#define LCD_VSYNC_START				(0x4 * 0x809)
#define LCD_VSYNC_END				(0x4 * 0x80a)
#define LCD_V_BACKPORCH_EVEN			(0x4 * 0x80b)
#define LCD_VSYNC_WIDTH_EVEN			(0x4 * 0x80c)
#define LCD_V_ACTIVEHEIGHT_EVEN			(0x4 * 0x80d)
#define LCD_V_FRONTPORCH_EVEN			(0x4 * 0x80e)
#define LCD_VSYNC_START_EVEN			(0x4 * 0x80f)
#define LCD_VSYNC_END_EVEN			(0x4 * 0x810)
#define LCD_TIMING_GEN_TRIG			(0x4 * 0x811)
#define LCD_PWM0_CTRL				(0x4 * 0x812)
#define LCD_PWM0_RPT_LEADIN			(0x4 * 0x813)
#define LCD_PWM0_HIGH_LOW			(0x4 * 0x814)
#define LCD_PWM1_CTRL				(0x4 * 0x815)
#define LCD_PWM1_RPT_LEADIN			(0x4 * 0x816)
#define LCD_PWM1_HIGH_LOW			(0x4 * 0x817)
#define LCD_PWM2_CTRL				(0x4 * 0x818)
#define LCD_PWM2_RPT_LEADIN			(0x4 * 0x819)
#define LCD_PWM2_HIGH_LOW			(0x4 * 0x81a)
#define LCD_VIDEO0_DMA0_BYTES			(0x4 * 0xb00)
#define LCD_VIDEO0_DMA0_STATE			(0x4 * 0xb01)
#define LCD_DMA_STATE_ACTIVE			  BIT(3)
#define LCD_VIDEO0_DMA1_BYTES			(0x4 * 0xb02)
#define LCD_VIDEO0_DMA1_STATE			(0x4 * 0xb03)
#define LCD_VIDEO0_DMA2_BYTES			(0x4 * 0xb04)
#define LCD_VIDEO0_DMA2_STATE			(0x4 * 0xb05)
#define LCD_VIDEO1_DMA0_BYTES			(0x4 * 0xb06)
#define LCD_VIDEO1_DMA0_STATE			(0x4 * 0xb07)
#define LCD_VIDEO1_DMA1_BYTES			(0x4 * 0xb08)
#define LCD_VIDEO1_DMA1_STATE			(0x4 * 0xb09)
#define LCD_VIDEO1_DMA2_BYTES			(0x4 * 0xb0a)
#define LCD_VIDEO1_DMA2_STATE			(0x4 * 0xb0b)
#define LCD_GRAPHIC0_DMA_BYTES			(0x4 * 0xb0c)
#define LCD_GRAPHIC0_DMA_STATE			(0x4 * 0xb0d)
#define LCD_GRAPHIC1_DMA_BYTES			(0x4 * 0xb0e)
#define LCD_GRAPHIC1_DMA_STATE			(0x4 * 0xb0f)

/***************************************************************************
 *		   MIPI controller control register defines
 *************************************************************************/
#define MIPI0_HS_BASE_ADDR			(MIPI_BASE_ADDR + 0x400)
#define HS_OFFSET(M)				(((M) + 1) * 0x400)

#define MIPI_TX_HS_CTRL				(0x0)
#define   MIPI_TXm_HS_CTRL(M)			(MIPI_TX_HS_CTRL + HS_OFFSET(M))
#define   HS_CTRL_EN				BIT(0)
/* 1:CSI 0:DSI */
#define   HS_CTRL_CSIDSIN			BIT(2)
/* 1:LCD, 0:DMA */
#define   TX_SOURCE				BIT(3)
#define   ACTIVE_LANES(n)			((n) << 4)
#define   LCD_VC(ch)				((ch) << 8)
#define   DSI_EOTP_EN				BIT(11)
#define   DSI_CMD_HFP_EN			BIT(12)
#define   CRC_EN				BIT(14)
#define   HSEXIT_CNT(n)				((n) << 16)
#define   HSCLKIDLE_CNT				BIT(24)
#define MIPI_TX_HS_SYNC_CFG			(0x8)
#define   MIPI_TXm_HS_SYNC_CFG(M)		(MIPI_TX_HS_SYNC_CFG \
						+ HS_OFFSET(M))
#define   LINE_SYNC_PKT_ENABLE			BIT(0)
#define   FRAME_COUNTER_ACTIVE			BIT(1)
#define   LINE_COUNTER_ACTIVE			BIT(2)
#define   DSI_V_BLANKING			BIT(4)
#define   DSI_HSA_BLANKING			BIT(5)
#define   DSI_HBP_BLANKING			BIT(6)
#define   DSI_HFP_BLANKING			BIT(7)
#define   DSI_SYNC_PULSE_EVENTN			BIT(8)
#define   DSI_LPM_FIRST_VSA_LINE		BIT(9)
#define   DSI_LPM_LAST_VFP_LINE			BIT(10)
#define   WAIT_ALL_SECT				BIT(11)
#define   WAIT_TRIG_POS				BIT(15)
#define   ALWAYS_USE_HACT(f)			((f) << 19)
#define   FRAME_GEN_EN(f)			((f) << 23)
#define   HACT_WAIT_STOP(f)			((f) << 28)
#define MIPI_TX0_HS_FG0_SECT0_PH		(0x40)
#define   MIPI_TXm_HS_FGn_SECTo_PH(M, N, O)	(MIPI_TX0_HS_FG0_SECT0_PH + \
						HS_OFFSET(M) + (0x2C * (N)) \
						+ (8 * (O)))
#define   MIPI_TX_SECT_WC_MASK			(0xffff)
#define	  MIPI_TX_SECT_VC_MASK			(3)
#define   MIPI_TX_SECT_VC_SHIFT			(22)
#define   MIPI_TX_SECT_DT_MASK			(0x3f)
#define   MIPI_TX_SECT_DT_SHIFT			(16)
#define   MIPI_TX_SECT_DM_MASK			(3)
#define   MIPI_TX_SECT_DM_SHIFT			(24)
#define   MIPI_TX_SECT_DMA_PACKED		BIT(26)
#define MIPI_TX_HS_FG0_SECT_UNPACKED_BYTES0	(0x60)
#define MIPI_TX_HS_FG0_SECT_UNPACKED_BYTES1	(0x64)
#define   MIPI_TXm_HS_FGn_SECT_UNPACKED_BYTES0(M, N)	\
					(MIPI_TX_HS_FG0_SECT_UNPACKED_BYTES0 \
					+ HS_OFFSET(M) + (0x2C * (N)))
#define MIPI_TX_HS_FG0_SECT0_LINE_CFG		(0x44)
#define   MIPI_TXm_HS_FGn_SECTo_LINE_CFG(M, N, O)	\
				(MIPI_TX_HS_FG0_SECT0_LINE_CFG + HS_OFFSET(M) \
				+ (0x2C * (N)) + (8 * (O)))

#define MIPI_TX_HS_FG0_NUM_LINES		(0x68)
#define   MIPI_TXm_HS_FGn_NUM_LINES(M, N)	\
				(MIPI_TX_HS_FG0_NUM_LINES + HS_OFFSET(M) \
				+ (0x2C * (N)))
#define MIPI_TX_HS_VSYNC_WIDTHS0		(0x104)
#define   MIPI_TXm_HS_VSYNC_WIDTHn(M, N)		\
				(MIPI_TX_HS_VSYNC_WIDTHS0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_V_BACKPORCHES0		(0x16c)
#define   MIPI_TXm_HS_V_BACKPORCHESn(M, N)	\
				(MIPI_TX_HS_V_BACKPORCHES0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_V_FRONTPORCHES0		(0x174)
#define   MIPI_TXm_HS_V_FRONTPORCHESn(M, N)	\
				(MIPI_TX_HS_V_FRONTPORCHES0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_V_ACTIVE0			(0x17c)
#define   MIPI_TXm_HS_V_ACTIVEn(M, N)		\
				(MIPI_TX_HS_V_ACTIVE0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_HSYNC_WIDTH0			(0x10c)
#define   MIPI_TXm_HS_HSYNC_WIDTHn(M, N)		\
				(MIPI_TX_HS_HSYNC_WIDTH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_H_BACKPORCH0			(0x11c)
#define   MIPI_TXm_HS_H_BACKPORCHn(M, N)		\
				(MIPI_TX_HS_H_BACKPORCH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_H_FRONTPORCH0		(0x12c)
#define   MIPI_TXm_HS_H_FRONTPORCHn(M, N)	\
				(MIPI_TX_HS_H_FRONTPORCH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_H_ACTIVE0			(0x184)
#define   MIPI_TXm_HS_H_ACTIVEn(M, N)		\
				(MIPI_TX_HS_H_ACTIVE0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_LLP_HSYNC_WIDTH0		(0x13c)
#define   MIPI_TXm_HS_LLP_HSYNC_WIDTHn(M, N)	\
				(MIPI_TX_HS_LLP_HSYNC_WIDTH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_LLP_H_BACKPORCH0		(0x14c)
#define   MIPI_TXm_HS_LLP_H_BACKPORCHn(M, N)	\
				(MIPI_TX_HS_LLP_H_BACKPORCH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define MIPI_TX_HS_LLP_H_FRONTPORCH0		(0x15c)
#define   MIPI_TXm_HS_LLP_H_FRONTPORCHn(M, N)	\
				(MIPI_TX_HS_LLP_H_FRONTPORCH0 + HS_OFFSET(M) \
				+ (0x4 * (N)))

#define MIPI_TX_HS_MC_FIFO_CTRL_EN		(0x194)
#define   MIPI_TXm_HS_MC_FIFO_CTRL_EN(M)	\
				(MIPI_TX_HS_MC_FIFO_CTRL_EN + HS_OFFSET(M))

#define MIPI_TX_HS_MC_FIFO_CHAN_ALLOC0		(0x198)
#define MIPI_TX_HS_MC_FIFO_CHAN_ALLOC1		(0x19c)
#define   MIPI_TXm_HS_MC_FIFO_CHAN_ALLOCn(M, N)	\
			(MIPI_TX_HS_MC_FIFO_CHAN_ALLOC0 + HS_OFFSET(M) \
			+ (0x4 * (N)))
#define   SET_MC_FIFO_CHAN_ALLOC(dev, ctrl, vc, sz)	\
		kmb_write_bits_mipi(dev, \
				MIPI_TXm_HS_MC_FIFO_CHAN_ALLOCn(ctrl, \
				(vc) / 2), ((vc) % 2) * 16, 16, sz)
#define MIPI_TX_HS_MC_FIFO_RTHRESHOLD0		(0x1a0)
#define MIPI_TX_HS_MC_FIFO_RTHRESHOLD1		(0x1a4)
#define   MIPI_TXm_HS_MC_FIFO_RTHRESHOLDn(M, N)	\
				(MIPI_TX_HS_MC_FIFO_RTHRESHOLD0 + HS_OFFSET(M) \
				+ (0x4 * (N)))
#define   SET_MC_FIFO_RTHRESHOLD(dev, ctrl, vc, th)	\
	kmb_write_bits_mipi(dev, MIPI_TXm_HS_MC_FIFO_RTHRESHOLDn(ctrl, \
				(vc) / 2), ((vc) % 2) * 16, 16, th)
#define MIPI_TX_HS_DMA_CFG			(0x1a8)
#define MIPI_TX_HS_DMA_START_ADR_CHAN0		(0x1ac)
#define MIPI_TX_HS_DMA_LEN_CHAN0		(0x1b4)

/* MIPI IRQ */
#define MIPI_CTRL_IRQ_STATUS0				(0x00)
#define   MIPI_DPHY_ERR_IRQ				1
#define   MIPI_DPHY_ERR_MASK				0x7FE	/*bits 1-10 */
#define   MIPI_HS_IRQ					13
/* bits 13-22 */
#define   MIPI_HS_IRQ_MASK				0x7FE000
#define   MIPI_LP_EVENT_IRQ				25
#define   MIPI_GET_IRQ_STAT0(dev)		kmb_read_mipi(dev, \
						MIPI_CTRL_IRQ_STATUS0)
#define MIPI_CTRL_IRQ_STATUS1				(0x04)
#define   MIPI_HS_RX_EVENT_IRQ				0
#define   MIPI_GET_IRQ_STAT1(dev)		kmb_read_mipi(dev, \
						MIPI_CTRL_IRQ_STATUS1)
#define MIPI_CTRL_IRQ_ENABLE0				(0x08)
#define   SET_MIPI_CTRL_IRQ_ENABLE0(dev, M, N)	kmb_set_bit_mipi(dev, \
						MIPI_CTRL_IRQ_ENABLE0, \
						(M) + (N))
#define   MIPI_GET_IRQ_ENABLED0(dev)		kmb_read_mipi(dev, \
						MIPI_CTRL_IRQ_ENABLE0)
#define MIPI_CTRL_IRQ_ENABLE1				(0x0c)
#define   MIPI_GET_IRQ_ENABLED1(dev)		kmb_read_mipi(dev, \
						MIPI_CTRL_IRQ_ENABLE1)
#define MIPI_CTRL_IRQ_CLEAR0				(0x010)
#define   SET_MIPI_CTRL_IRQ_CLEAR0(dev, M, N)		\
		kmb_set_bit_mipi(dev, MIPI_CTRL_IRQ_CLEAR0, (M) + (N))
#define MIPI_CTRL_IRQ_CLEAR1				(0x014)
#define   SET_MIPI_CTRL_IRQ_CLEAR1(dev, M, N)		\
		kmb_set_bit_mipi(dev, MIPI_CTRL_IRQ_CLEAR1, (M) + (N))
#define MIPI_CTRL_DIG_LOOPBACK				(0x018)
#define MIPI_TX_HS_IRQ_STATUS				(0x01c)
#define   MIPI_TX_HS_IRQ_STATUSm(M)		(MIPI_TX_HS_IRQ_STATUS + \
						HS_OFFSET(M))
#define   GET_MIPI_TX_HS_IRQ_STATUS(dev, M)	kmb_read_mipi(dev, \
						MIPI_TX_HS_IRQ_STATUSm(M))
#define   MIPI_TX_HS_IRQ_LINE_COMPARE			BIT(1)
#define   MIPI_TX_HS_IRQ_FRAME_DONE_0			BIT(2)
#define   MIPI_TX_HS_IRQ_FRAME_DONE_1			BIT(3)
#define   MIPI_TX_HS_IRQ_FRAME_DONE_2			BIT(4)
#define   MIPI_TX_HS_IRQ_FRAME_DONE_3			BIT(5)
#define   MIPI_TX_HS_IRQ_DMA_DONE_0			BIT(6)
#define   MIPI_TX_HS_IRQ_DMA_IDLE_0			BIT(7)
#define   MIPI_TX_HS_IRQ_DMA_DONE_1			BIT(8)
#define   MIPI_TX_HS_IRQ_DMA_IDLE_1			BIT(9)
#define   MIPI_TX_HS_IRQ_DMA_DONE_2			BIT(10)
#define   MIPI_TX_HS_IRQ_DMA_IDLE_2			BIT(11)
#define   MIPI_TX_HS_IRQ_DMA_DONE_3			BIT(12)
#define   MIPI_TX_HS_IRQ_DMA_IDLE_3			BIT(13)
#define   MIPI_TX_HS_IRQ_MC_FIFO_UNDERFLOW		BIT(14)
#define   MIPI_TX_HS_IRQ_MC_FIFO_OVERFLOW		BIT(15)
#define   MIPI_TX_HS_IRQ_LLP_FIFO_EMPTY			BIT(16)
#define   MIPI_TX_HS_IRQ_LLP_REQUEST_QUEUE_FULL		BIT(17)
#define   MIPI_TX_HS_IRQ_LLP_REQUEST_QUEUE_ERROR	BIT(18)
#define   MIPI_TX_HS_IRQ_LLP_WORD_COUNT_ERROR		BIT(20)
#define   MIPI_TX_HS_IRQ_FRAME_DONE			\
				(MIPI_TX_HS_IRQ_FRAME_DONE_0 | \
				MIPI_TX_HS_IRQ_FRAME_DONE_1 | \
				MIPI_TX_HS_IRQ_FRAME_DONE_2 | \
				MIPI_TX_HS_IRQ_FRAME_DONE_3)

#define MIPI_TX_HS_IRQ_DMA_DONE				\
				(MIPI_TX_HS_IRQ_DMA_DONE_0 | \
				MIPI_TX_HS_IRQ_DMA_DONE_1 | \
				MIPI_TX_HS_IRQ_DMA_DONE_2 | \
				MIPI_TX_HS_IRQ_DMA_DONE_3)

#define MIPI_TX_HS_IRQ_DMA_IDLE				\
				(MIPI_TX_HS_IRQ_DMA_IDLE_0 | \
				MIPI_TX_HS_IRQ_DMA_IDLE_1 | \
				MIPI_TX_HS_IRQ_DMA_IDLE_2 | \
				MIPI_TX_HS_IRQ_DMA_IDLE_3)

#define MIPI_TX_HS_IRQ_ERROR				\
				(MIPI_TX_HS_IRQ_MC_FIFO_UNDERFLOW | \
				MIPI_TX_HS_IRQ_MC_FIFO_OVERFLOW | \
				MIPI_TX_HS_IRQ_LLP_FIFO_EMPTY | \
				MIPI_TX_HS_IRQ_LLP_REQUEST_QUEUE_FULL | \
				MIPI_TX_HS_IRQ_LLP_REQUEST_QUEUE_ERROR | \
				MIPI_TX_HS_IRQ_LLP_WORD_COUNT_ERROR)

#define MIPI_TX_HS_IRQ_ALL				\
				(MIPI_TX_HS_IRQ_FRAME_DONE | \
				MIPI_TX_HS_IRQ_DMA_DONE | \
				MIPI_TX_HS_IRQ_DMA_IDLE | \
				MIPI_TX_HS_IRQ_LINE_COMPARE | \
				MIPI_TX_HS_IRQ_ERROR)

#define MIPI_TX_HS_IRQ_ENABLE				(0x020)
#define	  GET_HS_IRQ_ENABLE(dev, M)		kmb_read_mipi(dev, \
						MIPI_TX_HS_IRQ_ENABLE \
						+ HS_OFFSET(M))
#define MIPI_TX_HS_IRQ_CLEAR				(0x024)

/* MIPI Test Pattern Generation */
#define MIPI_TX_HS_TEST_PAT_CTRL			(0x230)
#define   MIPI_TXm_HS_TEST_PAT_CTRL(M)			\
				(MIPI_TX_HS_TEST_PAT_CTRL + HS_OFFSET(M))
#define   TP_EN_VCm(M)					(1 << ((M) * 0x04))
#define   TP_SEL_VCm(M, N)				\
				((N) << (((M) * 0x04) + 1))
#define   TP_STRIPE_WIDTH(M)				((M) << 16)
#define MIPI_TX_HS_TEST_PAT_COLOR0			(0x234)
#define   MIPI_TXm_HS_TEST_PAT_COLOR0(M)		\
				(MIPI_TX_HS_TEST_PAT_COLOR0 + HS_OFFSET(M))
#define MIPI_TX_HS_TEST_PAT_COLOR1			(0x238)
#define   MIPI_TXm_HS_TEST_PAT_COLOR1(M)		\
				(MIPI_TX_HS_TEST_PAT_COLOR1 + HS_OFFSET(M))

/* D-PHY regs */
#define DPHY_ENABLE				(0x100)
#define DPHY_INIT_CTRL0				(0x104)
#define   SHUTDOWNZ				0
#define   RESETZ				12
#define DPHY_INIT_CTRL1				(0x108)
#define   PLL_CLKSEL_0				18
#define   PLL_SHADOW_CTRL			16
#define DPHY_INIT_CTRL2				(0x10c)
#define   SET_DPHY_INIT_CTRL0(dev, dphy, offset)	\
			kmb_set_bit_mipi(dev, DPHY_INIT_CTRL0, \
					((dphy) + (offset)))
#define   CLR_DPHY_INIT_CTRL0(dev, dphy, offset)	\
			kmb_clr_bit_mipi(dev, DPHY_INIT_CTRL0, \
					((dphy) + (offset)))
#define DPHY_INIT_CTRL2				(0x10c)
#define DPHY_PLL_OBS0				(0x110)
#define DPHY_PLL_OBS1				(0x114)
#define DPHY_PLL_OBS2				(0x118)
#define DPHY_FREQ_CTRL0_3			(0x11c)
#define DPHY_FREQ_CTRL4_7			(0x120)
#define   SET_DPHY_FREQ_CTRL0_3(dev, dphy, val)	\
			kmb_write_bits_mipi(dev, DPHY_FREQ_CTRL0_3 \
			+ (((dphy) / 4) * 4), (dphy % 4) * 8, 6, val)

#define DPHY_FORCE_CTRL0			(0x128)
#define DPHY_FORCE_CTRL1			(0x12C)
#define MIPI_DPHY_STAT0_3			(0x134)
#define MIPI_DPHY_STAT4_7			(0x138)
#define	  GET_STOPSTATE_DATA(dev, dphy)		\
			(((kmb_read_mipi(dev, MIPI_DPHY_STAT0_3 + \
					 ((dphy) / 4) * 4)) >> \
					 (((dphy % 4) * 8) + 4)) & 0x03)

#define MIPI_DPHY_ERR_STAT6_7			(0x14C)

#define DPHY_TEST_CTRL0				(0x154)
#define   SET_DPHY_TEST_CTRL0(dev, dphy)		\
			kmb_set_bit_mipi(dev, DPHY_TEST_CTRL0, (dphy))
#define   CLR_DPHY_TEST_CTRL0(dev, dphy)		\
			kmb_clr_bit_mipi(dev, DPHY_TEST_CTRL0, \
						(dphy))
#define DPHY_TEST_CTRL1				(0x158)
#define   SET_DPHY_TEST_CTRL1_CLK(dev, dphy)	\
			kmb_set_bit_mipi(dev, DPHY_TEST_CTRL1, (dphy))
#define   CLR_DPHY_TEST_CTRL1_CLK(dev, dphy)	\
			kmb_clr_bit_mipi(dev, DPHY_TEST_CTRL1, (dphy))
#define   SET_DPHY_TEST_CTRL1_EN(dev, dphy)	\
			kmb_set_bit_mipi(dev, DPHY_TEST_CTRL1, ((dphy) + 12))
#define   CLR_DPHY_TEST_CTRL1_EN(dev, dphy)	\
			kmb_clr_bit_mipi(dev, DPHY_TEST_CTRL1, ((dphy) + 12))
#define DPHY_TEST_DIN0_3			(0x15c)
#define   SET_TEST_DIN0_3(dev, dphy, val)		\
			kmb_write_mipi(dev, DPHY_TEST_DIN0_3 + \
			4, ((val) << (((dphy) % 4) * 8)))
#define DPHY_TEST_DOUT0_3			(0x168)
#define   GET_TEST_DOUT0_3(dev, dphy)		\
			(kmb_read_mipi(dev, DPHY_TEST_DOUT0_3) \
			>> (((dphy) % 4) * 8) & 0xff)
#define DPHY_TEST_DOUT4_7			(0x16C)
#define   GET_TEST_DOUT4_7(dev, dphy)		\
			(kmb_read_mipi(dev, DPHY_TEST_DOUT4_7) \
			>> (((dphy) % 4) * 8) & 0xff)
#define DPHY_TEST_DOUT8_9			(0x170)
#define DPHY_TEST_DIN4_7			(0x160)
#define DPHY_TEST_DIN8_9			(0x164)
#define DPHY_PLL_LOCK				(0x188)
#define   GET_PLL_LOCK(dev, dphy)		\
			(kmb_read_mipi(dev, DPHY_PLL_LOCK) \
			& (1 << ((dphy) - MIPI_DPHY6)))
#define DPHY_CFG_CLK_EN				(0x18c)

#define MSS_MIPI_CIF_CFG			(0x00)
#define MSS_LCD_MIPI_CFG			(0x04)
#define MSS_CAM_CLK_CTRL			(0x10)
#define MSS_LOOPBACK_CFG			(0x0C)
#define   LCD					BIT(1)
#define   MIPI_COMMON				BIT(2)
#define   MIPI_TX0				BIT(9)
#define MSS_CAM_RSTN_CTRL			(0x14)
#define MSS_CAM_RSTN_SET			(0x20)
#define MSS_CAM_RSTN_CLR			(0x24)

#define MSSCPU_CPR_CLK_EN			(0x0)
#define MSSCPU_CPR_RST_EN			(0x10)
#define BIT_MASK_16				(0xffff)
/* icam lcd qos */
#define LCD_QOS_PRIORITY			(0x8)
#define LCD_QOS_MODE				(0xC)
#define LCD_QOS_BW				(0x10)
#endif /* __KMB_REGS_H__ */
