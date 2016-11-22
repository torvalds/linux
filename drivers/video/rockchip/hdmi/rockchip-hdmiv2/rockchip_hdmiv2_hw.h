#ifndef _RK3288_HDMI_HW_H
#define _RK3288_HDMI_HW_H
#include <linux/interrupt.h>
#include "../rockchip-hdmi.h"

/*#define HDMI_INT_USE_POLL 1*/

enum PWR_MODE {
	NORMAL,
	LOWER_PWR,
};

enum {
	OUTPUT_DVI = 0,
	OUTPUT_HDMI,
};

/* Color Space Conversion Mode */
enum {
	CSC_BYPASS,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,	/* RGB 0-255 input to RGB
						 * 16-235 output that is 8bit
						 * clolor depth
						 */
	CSC_RGB_0_255_TO_RGB_16_235_10BIT,	/* RGB 0-255 input to RGB
						 * 16-235 output that is 8bit
						 * clolor depth
						 */
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,	/* RGB 0-255 input to YCbCr
						 * 16-235 output according
						 * BT601 that is 8bit clolor
						 * depth
						 */
	CSC_RGB_0_255_TO_ITU601_16_235_10BIT,	/* RGB 0-255 input to YCbCr
						 * 16-235 output according
						 * BT601 that is 10bit clolor
						 * depth
						 */
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,	/* RGB 0-255 input to YCbCr
						 * 16-235 output accroding
						 * BT709 that is 8bit clolor
						 * depth
						 */
	CSC_RGB_0_255_TO_ITU709_16_235_10BIT,	/* RGB 0-255 input to YCbCr
						 * 16-235 output accroding
						 * BT709 that is 10bit clolor
						 * depth
						 */
	CSC_ITU601_16_235_TO_RGB_0_255_8BIT,	/* YCbCr 16-235 input to RGB
						 * 0-255 output according
						 * BT601 that is 8bit clolor
						 * depth
						 */
	CSC_ITU709_16_235_TO_RGB_0_255_8BIT,	/* YCbCr 16-235 input to RGB
						 * 0-255 output according
						 * BT709 that is 8bit clolor
						 * depth
						 */
	CSC_ITU601_16_235_TO_RGB_16_235_8BIT,	/* YCbCr 16-235 input to RGB
						 * 16-235 output according
						 * BT601 that is 8bit clolor
						 * depth
						 */
	CSC_ITU709_16_235_TO_RGB_16_235_8BIT	/* YCbCr 16-235 input to RGB
						 * 16-235 output according
						 * BT709 that is 8bit clolor
						 * depth
						 */
};

#define HDMI_SCL_RATE		(100 * 1000)
#define DDC_I2C_EDID_ADDR	0x50	/* 0xA0/2 = 0x50 */
#define DDC_I2C_SEG_ADDR	0x30	/* 0x60/2 = 0x30 */
#define DDC_I2C_SCDC_ADDR	0x54	/* 0xa8/2 = 0x54 */

/* Register and Field Descriptions */
/* Identification Registers */
#define IDENTIFICATION_BASE		0x0000

#define DESIGN_ID			0x0000
#define REVISION_ID			0x0001
#define PRODUCT_ID0			0x0002
#define PRODUCT_ID1			0x0003

#define CONFIG0_ID			0x0004
#define m_PREPEN		BIT(7)
#define m_AUDSPDIF		BIT(5)
#define m_AUDI2S		BIT(4)
#define m_HDMI14		BIT(3)
#define m_CSC			BIT(2)
#define m_CEC			BIT(1)
#define m_HDCP			BIT(0)

#define CONFIG1_ID			0x0005
#define m_HDCP22		BIT(6)
#define m_HDMI20		BIT(5)
#define m_CONFAPB		BIT(1)

#define CONFIG2_ID			0x0006
enum PHYTYPE {
	HDMI_TX_PHY = 0x00,
	HDMI_MHL_WITH_HEAC_PHY = 0xb2,
	HDMI_MHL_PHY = 0xc2,
	HDMI_3D_TX_WITH_HEAC_PHY = 0xe2,
	HDMI_3D_TX_PHY = 0xf2,
	HDMI2_TX_PHY = 0xf3
};

#define CONFIG3_ID			0x0007
#define m_AHB_AUD_DMA		BIT(1)
#define m_GP_AUD		BIT(0)

/* Interrupt Registers */
#define INTERRUPT_BASE                  0x0100

#define IH_FC_STAT0			0x0100
#define m_AUD_INFOFRAME		BIT(7)
#define m_AUD_CONTENT_PROTECT	BIT(6)
#define m_AUD_HBR		BIT(5)
#define m_AUD_SAMPLE		BIT(2)
#define m_AUD_CLK_REGEN		BIT(1)
#define m_NULL_PACKET		BIT(0)

#define IH_FC_STAT1			0x0101
#define m_GMD			BIT(7)
#define m_ISCR1			BIT(6)
#define m_ISCR2			BIT(5)
#define m_VSD			BIT(4)
#define m_SPD			BIT(3)
#define m_AVI_INFOFRAME		BIT(1)
#define m_GCP			BIT(0)

#define v_AVI_INFOFRAME(n)	(((n) & 0x01) << 1)

#define IH_FC_STAT2			0x0102
#define m_LOWPRIO_OVERFLOW	BIT(1)
#define m_HIGHPRIO_OVERFLOW	BIT(0)

#define IH_AS_SATA0			0x0103
#define m_FIFO_UNDERRUN		BIT(4)
#define m_FIFO_OVERRUN		BIT(3)
#define m_AUD_FIFO_UDFLOW_THR	BIT(2)
#define m_AUD_FIFO_UDFLOW	BIT(1)
#define m_AUD_FIFO_OVERFLOW	BIT(0)

#define IH_PHY_STAT0			0x0104
#define m_RX_SENSE3		BIT(5)
#define m_RX_SENSE2		BIT(4)
#define m_RX_SENSE1		BIT(3)
#define m_RX_SENSE0		BIT(2)
#define m_TX_PHY_LOCK		BIT(1)
#define m_HPD			BIT(0)

#define IH_I2CM_STAT0			0x0105
#define m_SCDC_READREQ		BIT(2)
#define m_I2CM_DONE		BIT(1)
#define m_I2CM_ERROR		BIT(0)

#define IH_CEC_STAT0			0x0106
#define m_WAKEUP		BIT(6)
#define m_ERR_FOLLOW		BIT(5)
#define m_ERR_INITIATOR		BIT(4)
#define m_ARB_LOST		BIT(3)
#define m_NACK			BIT(2)
#define m_EOM			BIT(1)
#define m_DONE			BIT(0)

#define IH_VP_STAT0			0x0107
#define m_FIFOFULL_REPET	BIT(7)
#define m_FIFOEMPTY_REPET	BIT(6)
#define m_FIFOFULL_PACK		BIT(5)
#define m_FIFOEMPTY_PACK	BIT(4)
#define m_FIFOFULL_REMAP	BIT(3)
#define m_FIFOEMPTY_REMAP	BIT(2)
#define m_FIFOFULL_BYPASS	BIT(1)
#define m_FIFOEMPTY_BYPASS	BIT(0)

#define IH_I2CMPHY_STAT0		0x0108
#define m_I2CMPHY_DONE		BIT(1)
#define m_I2CMPHY_ERR		BIT(0)

#define IH_AHBDMAAUD_STAT0		0x0109
#define m_AUDDMA_INT_BUFOVERRUN	BIT(6)
#define m_AUDDMA_INT_ERR	BIT(5)
#define m_AUDDMA_INT_LOST	BIT(4)
#define m_AUDDMA_INT_RETRYSPLIT BIT(3)
#define m_AUDDMA_INT_DONE	BIT(2)
#define m_AUDDMA_INT_BUFFULL	BIT(1)
#define m_AUDDMA_INT_BUFEMPTY	BIT(0)

#define IH_DECODE			0x0170
#define m_IH_FC_STAT0		BIT(7)
#define m_IH_FC_STAT1		BIT(6)
#define m_IH_FC_STAT2_VP	BIT(5)
#define m_IH_AS_STAT0		BIT(4)
#define m_IH_PHY		BIT(3)
#define m_IH_I2CM_STAT0		BIT(2)
#define m_IH_CEC_STAT0		BIT(1)
#define m_IH_AHBDMAAUD_STAT0	BIT(0)

#define IH_MUTE_FC_STAT0		0x0180
#define m_AUDI_MUTE		BIT(7)
#define m_ACP_MUTE		BIT(6)
#define m_DST_MUTE		BIT(4)
#define m_OBA_MUTE		BIT(3)
#define m_AUDS_MUTE		BIT(2)
#define m_ACR_MUTE		BIT(1)
#define m_NULL_MUTE		BIT(0)

#define IH_MUTE_FC_STAT1		0x0181
#define m_GMD_MUTE		BIT(7)
#define m_ISCR1_MUTE		BIT(6)
#define m_ISCR2_MUTE		BIT(5)
#define m_VSD_MUTE		BIT(4)
#define m_SPD_MUTE		BIT(3)
#define m_AVI_MUTE		BIT(1)
#define m_GCP_MUTE		BIT(0)

#define IH_MUTE_FC_STAT2		0x0182
#define m_LPRIO_OVERFLOW_MUTE	BIT(1)
#define m_HPRIO_OVERFLOW_MUTE	BIT(0)

#define IH_MUTE_AS_STAT0		0x0183
#define m_FIFO_UNDERRUN_MUTE	BIT(4)
#define m_FIFO_OVERRUN_MUTE	BIT(3)
#define m_AUD_FIFO_UDF_THR_MUTE	BIT(2)
#define m_AUD_FIFO_UDF_MUTE	BIT(1)
#define m_AUD_FIFO_OVF_MUTE	BIT(0)

#define IH_MUTE_PHY_STAT0		0x0184
#define m_RX_SENSE3_MUTE	BIT(5)
#define m_RX_SENSE2_MUTE	BIT(4)
#define m_RX_SENSE1_MUTE	BIT(3)
#define m_RX_SENSE0_MUTE	BIT(2)
#define m_TX_PHY_LOCK_MUTE	BIT(1)
#define m_HPD_MUTE		BIT(0)

#define IH_MUTE_I2CM_STAT0		0x0185
#define m_SCDC_READREQ_MUTE	BIT(2)
#define v_SCDC_READREQ_MUTE(n)	(((n) & 0x01) << 2)
#define m_I2CM_DONE_MUTE	BIT(1)
#define v_I2CM_DONE_MUTE(n)	(((n) & 0x01) << 1)
#define m_I2CM_ERR_MUTE		BIT(0)
#define v_I2CM_ERR_MUTE(n)	(((n) & 0x01) << 0)

#define IH_MUTE_CEC_STAT0		0x0186
#define m_WAKEUP_MUTE		BIT(6)
#define m_ERR_FOLLOW_MUTE	BIT(5)
#define m_ERR_INITIATOR_MUTE	BIT(4)
#define m_ARB_LOST_MUTE		BIT(3)
#define m_NACK_MUTE		BIT(2)
#define m_EOM_MUTE		BIT(1)
#define m_DONE_MUTE		BIT(0)

#define IH_MUTE_VP_STAT0		0x0187
#define m_FIFOFULL_REP_MUTE	BIT(7)
#define m_FIFOEMPTY_REP_MUTE	BIT(6)
#define m_FIFOFULL_PACK_MUTE	BIT(5)
#define m_FIFOEMPTY_PACK_MUTE	BIT(4)
#define m_FIFOFULL_REMAP_MUTE	BIT(3)
#define m_FIFOEMPTY_REMAP_MUTE	BIT(2)
#define m_FIFOFULL_BYP_MUTE	BIT(1)
#define m_FIFOEMPTY_BYP_MUTE	BIT(0)

#define IH_MUTE_I2CMPHY_STAT0		0x0188
#define m_I2CMPHY_DONE_MUTE	BIT(1)
#define m_I2CMPHY_ERR_MUTE	BIT(0)

#define IH_MUTE_AHBDMAAUD_STAT0		0x0189
#define IH_MUTE				0x01ff

/* Video Sampler Registers */
#define VIDEO_SAMPLER_BASE		0x0200

#define TX_INVID0			0x0200
#define m_INTERNAL_DE_GEN	BIT(7)
#define v_INTERNAL_DE_GEN(n)	(((n) & 0x01) << 7)
enum VIDEO_MODE {
	VIDEO_RGB444_8BIT = 0x01,
	VIDEO_RGB444_10BIT = 0x03,
	VIDEO_RGB444_12BIT = 0x05,
	VIDEO_RGB444_16BIT = 0x07,
	VIDEO_YCBCR444_8BIT = 0x09,	/* or YCbCr420 */
	VIDEO_YCBCR444_10BIT = 0x0b,	/* or YCbCr420 */
	VIDEO_YCBCR444_12BIT = 0x0d,	/* or YCbCr420 */
	VIDEO_YCBCR444_16BIT = 0x0f,	/* or YCbCr420 */
	VIDEO_YCBCR422_12BIT = 0x12,
	VIDEO_YCBCR422_10BIT = 0x14,
	VIDEO_YCBCR422_8BIT = 0x16
};

#define m_VIDEO_MAPPING		(0x1f << 0)
#define v_VIDEO_MAPPING(n)	((n) & 0x1f)

#define TX_INSTUFFING			0x0201
#define m_BCBDATA_STUFF		BIT(2)
#define v_BCBDATA_STUFF(n)	(((n) & 0x01) << 2)
#define m_RCRDATA_STUFF		BIT(1)
#define v_RCRDATA_STUFF(n)	(((n) & 0x01) << 1)
#define m_GYDATA_STUFF		BIT(0)
#define v_GYDATA_STUFF(n)	(((n) & 0x01) << 0)

#define TX_GYDATA0			0x0202
#define TX_GYDATA1			0x0203
#define TX_RCRDATA0			0x0204
#define TX_RCRDATA1			0x0205
#define TX_BCBDATA0			0x0206
#define TX_BCBDATA1			0x0207

/* Video Packetizer Registers */
#define VIDEO_PACKETIZER_BASE		0x0800

#define VP_STATUS			0x0800
#define m_PACKING_PHASE		(0x0f << 0)

#define VP_PR_CD			0x0801
enum COLOR_DEPTH {
	COLOR_DEPTH_24BIT_DEFAULT = 0,
	COLOR_DEPTH_24BIT = 0x04,
	COLOR_DEPTH_30BIT,
	COLOR_DEPTH_36BIT,
	COLOR_DEPTH_48BIT
};

#define m_COLOR_DEPTH		(0x0f << 4)
#define v_COLOR_DEPTH(n)	(((n) & 0x0f) << 4)
enum PIXEL_REPET {
	NO_PIXEL_REPET = 0,
	PIXEL_SENT_2TIMES,
	PIXEL_SENT_3TIMES,
	PIXEL_SENT_4TIMES,
	PIXEL_SENT_5TIMES,
	PIXEL_SENT_6TIMES,
	PIXEL_SENT_7TIMES,
	PIXEL_SENT_8TIMES,
	PIXEL_SENT_9TIMES,
	PIXEL_SENT_10TIMES
};

#define m_DESIRED_PR_FACTOR	(0x0f << 0)
#define v_DESIRED_PR_FACTOR(n)	(((n) & 0x0f) << 0)

#define VP_STUFF			0x0802
#define m_IDEFAULT_PHASE	BIT(5)
#define v_IDEFAULT_PHASE(n)	(((n) & 0x01) << 5)
#define m_IFIX_PP_TO_LAST	BIT(4)
#define m_ICX_GOTO_P0_ST	BIT(3)
enum {
	DIRECT_MODE = 0,
	STUFFING_MODE
};

#define m_YCC422_STUFFING	BIT(2)
#define v_YCC422_STUFFING(n)	(((n) & 0x01) << 2)
#define m_PP_STUFFING		BIT(1)
#define v_PP_STUFFING(n)	(((n) & 0x01) << 1)
#define m_PR_STUFFING		BIT(0)
#define v_PR_STUFFING(n)	(((n) & 0x01) << 0)

#define VP_REMAP			0x0803
enum YCC422_SIZE {
	YCC422_16BIT = 0,
	YCC422_20BIT,
	YCC422_24BIT
};

#define m_YCC422_SIZE		(0x03 << 0)
#define v_YCC422_SIZE(n)	(((n) & 0x03) << 0)

#define VP_CONF				0x0804
#define m_BYPASS_EN		BIT(6)
#define v_BYPASS_EN(n)		(((n) & 0x01) << 6)
#define m_PIXEL_PACK_EN		BIT(5)
#define v_PIXEL_PACK_EN(n)	(((n) & 0x01) << 5)
#define m_PIXEL_REPET_EN	BIT(4)
#define v_PIXEL_REPET_EN(n)	(((n) & 0x01) << 4)
#define m_YCC422_EN		BIT(3)
#define v_YCC422_EN(n)		(((n) & 0x01) << 3)
#define m_BYPASS_SEL		BIT(2)
#define v_BYPASS_SEL(n)		(((n) & 0x01) << 2)
enum {
	OUT_FROM_PIXEL_PACKING = 0,
	OUT_FROM_YCC422_REMAP,
	OUT_FROM_8BIT_BYPASS
};

#define m_OUTPUT_SEL		(0x03 << 0)
#define v_OUTPUT_SEL(n)		(((n) & 0x03) << 0)

#define VP_MASK				0x0807
#define m_OINTFULL_REPET	BIT(7)
#define m_OINTEMPTY_REPET	BIT(6)
#define m_OINTFULL_PACK		BIT(5)
#define m_OINTEMPTY_PACK	BIT(4)
#define m_OINTFULL_REMAP	BIT(3)
#define m_OINTEMPTY_REMAP	BIT(2)
#define m_OINTFULL_BYPASS	BIT(1)
#define m_OINTEMPTY_BYPASS	BIT(0)

/* Frame Composer Registers */
#define FRAME_COMPOSER_BASE		0x1000

#define	FC_INVIDCONF			0x1000
#define m_FC_HDCP_KEEPOUT	BIT(7)
#define v_FC_HDCP_KEEPOUT(n)	(((n) & 0x01) << 7)
#define m_FC_VSYNC_POL		BIT(6)
#define v_FC_VSYNC_POL(n)	(((n) & 0x01) << 6)
#define m_FC_HSYNC_POL		BIT(5)
#define v_FC_HSYNC_POL(n)	(((n) & 0x01) << 5)
#define m_FC_DE_POL		BIT(4)
#define v_FC_DE_POL(n)		(((n) & 0x01) << 4)
#define m_FC_HDMI_DVI		BIT(3)
#define v_FC_HDMI_DVI(n)	(((n) & 0x01) << 3)
#define m_FC_VBLANK		BIT(1)
#define v_FC_VBLANK(n)		(((n) & 0x01) << 1)
#define m_FC_INTERLACE_MODE	BIT(0)
#define v_FC_INTERLACE_MODE(n)	(((n) & 0x01) << 0)

#define	FC_INHACTIV0			0x1001

#define	FC_INHACTIV1			0x1002
#define v_FC_HACTIVE1(n)	((n) & 0x3f)
#define m_FC_H_ACTIVE_13	BIT(5)
#define v_FC_H_ACTIVE_13(n)	(((n) & 0x01) << 5)
#define m_FC_H_ACTIVE_12	BIT(4)
#define v_FC_H_ACTIVE_12(n)	(((n) & 0x01) << 4)
#define m_FC_H_ACTIVE		(0x0f << 0)
#define v_FC_H_ACTIVE(n)	(((n) & 0x0f) << 0)

#define	FC_INHBLANK0			0x1003

#define	FC_INHBLANK1			0x1004
#define v_FC_HBLANK1(n)		((n) & 0x1f)
#define m_FC_H_BLANK_12_11	(0x07 << 2)
#define v_FC_H_BLANK_12_11(n)	(((n) & 0x07) << 2)
#define m_FC_H_BLANK		(0x03 << 0)
#define v_FC_H_BLANK(n)		(((n) & 0x03) << 0)

#define	FC_INVACTIV0			0x1005

#define	FC_INVACTIV1			0x1006
#define v_FC_VACTIVE1(n)	((n) & 0x1f)
#define m_FC_V_ACTIVE_12_11	(0x03 << 3)
#define v_FC_V_ACTIVE_12_11(n)	(((n) & 0x03) << 3)
#define m_FC_V_ACTIVE		(0x07 << 0)
#define v_FC_V_ACTIVE(n)	(((n) & 0x07) << 0)

#define	FC_INVBLANK			0x1007
#define	FC_HSYNCINDELAY0		0x1008

#define	FC_HSYNCINDELAY1		0x1009
#define v_FC_HSYNCINDEAY1(n)	((n) & 0x1f)
#define m_FC_H_SYNCFP_12_11	(0x03 << 3)
#define v_FC_H_SYNCFP_12_11(n)	(((n) & 0x03) << 3)
#define m_FC_H_SYNCFP		(0x07 << 0)
#define v_FC_H_SYNCFP(n)	(((n) & 0x07) << 0)

#define	FC_HSYNCINWIDTH0		0x100a

#define	FC_HSYNCINWIDTH1		0x100b
#define v_FC_HSYNCWIDTH1(n)	((n) & 0x03)
#define m_FC_HSYNC_9		BIT(1)
#define v_FC_HSYNC_9(n)		(((n) & 0x01) << 1)
#define m_FC_HSYNC		BIT(0)
#define v_FC_HSYNC(n)		(((n) & 0x01) << 0)

#define	FC_VSYNCINDELAY			0x100c
#define	FC_VSYNCINWIDTH			0x100d
#define	FC_INFREQ0			0x100e
#define	FC_INFREQ1			0x100f
#define	FC_INFREQ2			0x1010
#define	FC_CTRLDUR			0x1011
#define	FC_EXCTRLDUR			0x1012
#define	FC_EXCTRLSPAC			0x1013
#define	FC_CH0PREAM			0x1014
#define	FC_CH1PREAM			0x1015
#define	FC_CH2PREAM			0x1016

#define	FC_AVICONF3			0x1017
enum YCC_QUAN_RANGE {
	YQ_LIMITED_RANGE = 0,
	YQ_FULL_RANGE,
	RESERVED,
};

#define m_FC_YQ			(0x03 << 2)
#define v_FC_YQ(n)		(((n) & 0x03) << 2)
enum IT_CONTENT_TYPE {
	CN_GRAPHICS = 0,
	CN_PHOTO,
	CN_CINEMA,
	CN_GAME,
};

#define m_FC_CN			(0x03 << 0)
#define v_FC_CN(n)		(((n) & 0x03) << 0)

#define	FC_GCP				0x1018
#define m_FC_DEFAULT_PHASE	BIT(2)
#define v_FC_DEFAULT_PHASE(n)	(((n) & 0x01) << 2)
#define m_FC_SET_AVMUTE		BIT(1)
#define v_FC_SET_AVMUTE(n)	(((n) & 0x01) << 1)
#define m_FC_CLR_AVMUTE		BIT(0)
#define v_FC_CLR_AVMUTE(n)	(((n) & 0x01) << 0)

enum {
	AVI_COLOR_MODE_RGB = 0,
	AVI_COLOR_MODE_YCBCR422,
	AVI_COLOR_MODE_YCBCR444,
	AVI_COLOR_MODE_YCBCR420
};

enum {
	AVI_COLORIMETRY_NO_DATA = 0,
	AVI_COLORIMETRY_SMPTE_170M,
	AVI_COLORIMETRY_ITU709,
	AVI_COLORIMETRY_EXTENDED
};

enum {
	AVI_CODED_FRAME_ASPECT_NO_DATA,
	AVI_CODED_FRAME_ASPECT_4_3,
	AVI_CODED_FRAME_ASPECT_16_9
};

enum {
	ACTIVE_ASPECT_RATE_DEFAULT = 0x08,
	ACTIVE_ASPECT_RATE_4_3,
	ACTIVE_ASPECT_RATE_16_9,
	ACTIVE_ASPECT_RATE_14_9
};

enum {
	AVI_QUANTIZATION_RANGE_DEFAULT = 0,
	AVI_QUANTIZATION_RANGE_LIMITED,
	AVI_QUANTIZATION_RANGE_FULL
};

#define	FC_AVICONF0			0x1019
#define m_FC_RGC_YCC_2		BIT(7)	/* use for HDMI2.0 TX */
#define v_FC_RGC_YCC_2(n)	(((n) & 0x01) << 7)
#define m_FC_ACTIV_FORMAT	BIT(6)
#define v_FC_ACTIV_FORMAT(n)	(((n) & 0x01) << 6)
#define m_FC_SCAN_INFO		(0x03 << 4)
#define v_FC_SCAN_INFO(n)	(((n) & 0x03) << 4)
#define m_FC_BAR_FORMAT		(0x03 << 2)
#define v_FC_BAR_FORMAT(n)	(((n) & 0x03) << 2)
#define m_FC_RGC_YCC		(0x03 << 0)
#define v_FC_RGC_YCC(n)		(((n) & 0x03) << 0)

#define	FC_AVICONF1			0x101a
#define m_FC_COLORIMETRY	(0x03 << 6)
#define v_FC_COLORIMETRY(n)	(((n) & 0x03) << 6)
#define m_FC_PIC_ASPEC_RATIO	(0x03 << 4)
#define v_FC_PIC_ASPEC_RATIO(n)	(((n) & 0x03) << 4)
#define m_FC_ACT_ASPEC_RATIO	(0x0f << 0)
#define v_FC_ACT_ASPEC_RATIO(n)	(((n) & 0x0f) << 0)

#define	FC_AVICONF2			0x101b
#define m_FC_IT_CONTENT		BIT(7)
#define v_FC_IT_CONTENT(n)	(((n) & 0x01) << 7)
#define m_FC_EXT_COLORIMETRY	(0x07 << 4)
#define v_FC_EXT_COLORIMETRY(n)	(((n) & 0x07) << 4)
#define m_FC_QUAN_RANGE		(0x03 << 2)
#define v_FC_QUAN_RANGE(n)	(((n) & 0x03) << 2)
#define m_FC_NUN_PIC_SCALE	(0x03 << 0)
#define v_FC_NUN_PIC_SCALE(n)	(((n) & 0x03) << 0)

#define	FC_AVIVID			0x101c
#define m_FC_AVIVID_H		BIT(7)	/* use for HDMI2.0 TX */
#define v_FC_AVIVID_H(n)	(((n) & 0x01) << 7)
#define m_FC_AVIVID		(0x7f << 0)
#define v_FC_AVIVID(n)		(((n) & 0x7f) << 0)

#define	FC_AVIETB0			0x101d
#define	FC_AVIETB1			0x101e
#define	FC_AVISBB0			0x101f
#define	FC_AVISBB1			0x1020
#define	FC_AVIELB0			0x1021
#define	FC_AVIELB1			0x1022
#define	FC_AVISRB0			0x1023
#define	FC_AVISRB1			0x1024

#define	FC_AUDICONF0			0x1025
#define m_FC_CHN_CNT		(0x07 << 4)
#define v_FC_CHN_CNT(n)		(((n) & 0x07) << 4)
#define m_FC_CODING_TYPE	(0x0f << 0)
#define v_FC_CODING_TYPE(n)	(((n) & 0x0f) << 0)

#define	FC_AUDICONF1			0x1026
#define m_FC_SAMPLE_SIZE	(0x03 << 4)
#define v_FC_SAMPLE_SIZE(n)	(((n) & 0x03) << 4)
#define m_FC_SAMPLE_FREQ	(0x07 << 0)
#define v_FC_SAMPLE_FREQ(n)	(((n) & 0x07) << 0)

#define	FC_AUDICONF2			0x1027

#define	FC_AUDICONF3			0x1028
#define m_FC_LFE_PBL		(0x03 << 5)	/*only use for HDMI1.4 TX*/
#define v_FC_LFE_PBL(n)		(((n) & 0x03) << 5)
#define m_FC_DM_INH		BIT(4)
#define v_FC_DM_INH(n)		(((n) & 0x01) << 4)
#define m_FC_LSV		(0x0f << 0)
#define v_FC_LSV(n)		(((n) & 0x0f) << 0)

#define	FC_VSDIEEEID2			0x1029
#define	FC_VSDSIZE			0x102a
#define	FC_VSDIEEEID1			0x1030
#define	FC_VSDIEEEID0			0x1031
#define	FC_VSDPAYLOAD0			0x1032	/* 0~23 */
#define	FC_SPDVENDORNAME0		0x104a	/* 0~7 */
#define	FC_SPDPRODUCTNAME0		0x1052	/* 0~15 */
#define	FC_SPDDEVICEINF			0x1062

#define	FC_AUDSCONF			0x1063
#define m_AUD_PACK_SAMPFIT	(0x0f << 4)
#define v_AUD_PACK_SAMPFIT(n)	(((n) & 0x0f) << 4)
#define m_AUD_PACK_LAYOUT	BIT(0)
#define v_AUD_PACK_LAYOUT(n)	(((n) & 0x01) << 0)

#define	FC_AUDSSTAT			0x1064
#define	FC_AUDSV			0x1065
#define	FC_AUDSU			0x1066
#define	FC_AUDSCHNLS0			0x1067	/*0~8*/
#define	FC_AUDSCHNLS1			0x1068
#define	FC_AUDSCHNLS2			0x1069
#define	FC_AUDSCHNLS3			0x106a
#define	FC_AUDSCHNLS4			0x106b
#define	FC_AUDSCHNLS5			0x106c
#define	FC_AUDSCHNLS6			0x106d
#define	FC_AUDSCHNLS7			0x106e
#define	FC_AUDSCHNLS8			0x106f

enum {
	AUDIO_32K	= 0x3,
	AUDIO_441K	= 0x0,
	AUDIO_48K	= 0x2,
	AUDIO_882K	= 0x8,
	AUDIO_96K	= 0xa,
	AUDIO_1764K	= 0xc,
	AUDIO_192K	= 0xe,
	AUDIO_768K	= 0x9,
};

#define m_AUDIO_SAMPLE_RATE		(0x0f << 0)
#define v_AUDIO_SAMPLE_RATE(n)	(((n) & 0x0f) << 0)
#define m_AUDIO_ORI_SAMPLE_RATE		(0x0f << 4)
#define v_AUDIO_ORI_SAMPLE_RATE(n)	(((~(n)) & 0x0f) << 4)

#define m_AUDIO_WORD_LENGTH		(0x0f << 0)
#define v_AUDIO_WORD_LENGTH(n)	(((n) & 0x0f) << 0)

#define	FC_CTRLQHIGH			0x1073
#define	FC_CTRLQLOW			0x1074
#define	FC_ACP0				0x1075
#define	FC_ACP16			0x1082	/* 16~1 */
#define	FC_ISCR1_0			0x1092
#define	FC_ISCR1_16			0x1093	/* 16~1 */
#define	FC_ISCR2_15			0x10a3	/* 15~0 */

#define	FC_DATAUTO0			0x10b3
#define m_SPD_AUTO		BIT(4)
#define v_SPD_AUTO(n)		(((n) & 0x01) << 4)
#define m_VSD_AUTO		BIT(3)
#define v_VSD_AUTO(n)		(((n) & 0x01) << 3)
#define m_ISCR2_AUTO		BIT(2)
#define v_ISCR2_AUTO(n)		(((n) & 0x01) << 2)
#define m_ISCR1_AUTO		BIT(1)
#define v_ISCR1_AUTO(n)		(((n) & 0x01) << 1)
#define m_ACP_AUTO		BIT(0)
#define v_ACP_AUTO(n)		(((n) & 0x01) << 0)

#define	FC_DATAUTO1			0x10b4
#define	FC_DATAUTO2			0x10b5

#define	FC_DATMAN			0x10b6
#define m_SPD_MAN		BIT(4)
#define v_SPD_MAN(n)		(((n) & 0x01) << 4)
#define m_VSD_MAN		BIT(3)
#define v_VSD_MAN(n)		(((n) & 0x01) << 3)
#define m_ISCR2_MAN		BIT(2)
#define v_ISCR2_MAN(n)		(((n) & 0x01) << 2)
#define m_ISCR1_MAN		BIT(1)
#define v_ISCR1_MAN(n)		(((n) & 0x01) << 1)
#define m_ACP_MAN		BIT(0)
#define v_ACP_MAN(n)		(((n) & 0x01) << 0)

#define	FC_DATAUTO3			0x10b7
	#define m_AVI_AUTO		BIT(3)
	#define v_AVI_AUTO(n)		(((n) & 0x01) << 3)
	#define m_GCP_AUTO		BIT(2)
	#define v_GCP_AUTO(n)		(((n) & 0x01) << 2)
	#define m_AAI_AUTO		BIT(1)
	#define v_AAI_AUTO(n)		(((n) & 0x01) << 1)
	#define m_ACR_AUTO		BIT(0)
	#define v_ACR_AUTO(n)		(((n) & 0x01) << 0)
#define	FC_RDRB0			0x10b8
#define	FC_RDRB1			0x10b9
#define	FC_RDRB2			0x10ba
#define	FC_RDRB3			0x10bb
#define	FC_RDRB4			0x10bc
#define	FC_RDRB5			0x10bd
#define	FC_RDRB6			0x10be
#define	FC_RDRB7			0x10bf
	#define m_AVI_PACKETS_PER_FRAME		(0xf << 4)
	#define m_AVI_PACKERS_LINE_SPACING	(0xf)
	#define v_AVI_PACKETS_PER_FRAME(n)	(((n) & 0x0f) << 4)
	#define v_AVI_PACKERS_LINE_SPACING(n)	(((n) & 0x0f) << 0)
#define	FC_MASK0			0x10d2
#define	FC_MASK1			0x10d6
#define	FC_MASK2			0x10da

#define	FC_PRCONF			0x10e0
#define m_FC_PR_FACTOR		(0x0f << 4)
#define v_FC_PR_FACTOR(n)	(((n) & 0x0f) << 4)
#define m_FC_PR_FACTOR_OUT	(0x0f)
#define v_FC_PR_FACTOR_OUT(n)	((n) & 0x0f)

#define	FC_SCRAMBLER_CTRL		0x10e1
#define m_FC_SCRAMBLE_UCP	BIT(4)
#define v_FC_SCRAMBLE_UCP(n)	(((n) & 0x01) << 4)
#define m_FC_SCRAMBLE_EN	BIT(0)
#define v_FC_SCRAMBLE_EN(n)	(((n) & 0x01) << 0)

#define	FC_GMD_STAT			0x1100
#define	FC_GMD_EN			0x1101
#define	FC_GMD_UP			0x1102
#define	FC_GMD_CONF			0x1103
#define	FC_GMD_HB			0x1104
#define	FC_GMD_PB0			0x1105	/*0~27*/

#define	FC_PACK_TXE			0x10e3
	#define m_DRM_TXEN		BIT(7)
	#define v_DRM_TXEN(n)		(((n) & 0x01) << 7)
#define FC_DRM_UP			0x1167
	#define m_DRM_PUPD		BIT(0)
	#define v_DRM_PUPD(n)		(((n) & 0x01) << 0)
#define FC_DRM_HB			0x1168
#define FC_DRM_PB			0x116a
#define m_DRM_MASK			BIT(4)
#define v_DRM_MASK(n)			(((n) & 0x01) << 4)

#define FC_DBGFORCE			0x1200
#define m_FC_FORCEAUDIO		BIT(4)
#define v_FC_FORCEAUDIO(n)	(((n) & 0x01) << 4)
#define m_FC_FORCEVIDEO		BIT(0)
#define v_FC_FORCEVIDEO(n)	(((n) & 0x01) << 0)

#define	FC_DBGAUD0CH0			0x1201	/* aud0~aud2 ch0 */
#define	FC_DBGAUD0CH1			0x1204	/* aud0~aud2 ch1 */
#define	FC_DBGAUD0CH2			0x1207	/* aud0~aud2 ch2 */
#define	FC_DBGAUD0CH3			0x120a	/* aud0~aud2 ch3 */
#define	FC_DBGAUD0CH4			0x120d	/* aud0~aud2 ch4 */
#define	FC_DBGAUD0CH5			0x1210	/* aud0~aud2 ch5 */
#define	FC_DBGAUD0CH6			0x1213	/* aud0~aud2 ch6 */
#define	FC_DBGAUD0CH7			0x1216	/* aud0~aud2 ch7 */
#define	FC_DBGTMDS0			0x1219
#define	FC_DBGTMDS1			0x121a
#define	FC_DBGTMDS2			0x121b

/* HDMI Source PHY Registers */
#define HDMI_SOURCE_PHY_BASE		0x3000

#define PHY_CONF0			0x3000
#define m_POWER_DOWN_EN		BIT(7)/* no use */
#define v_POWER_DOWN_EN(n)	(((n) & 0x01) << 7)
#define m_TMDS_EN		BIT(6)/* no use */
#define v_TMDS_EN(n)		(((n) & 0x01) << 6)
#define	m_SVSRET_SIG		BIT(5)/* depend on PHY_MHL_COMB0=1 */
#define v_SVSRET_SIG(n)		(((n) & 0x01) << 5)
#define m_PDDQ_SIG		BIT(4)
/*1: power down phy; 0: power on phy */
#define v_PDDQ_SIG(n)		(((n) & 0x01) << 4)
#define m_TXPWRON_SIG		BIT(3)
/*1: power on transmitter; 0: power down transmitter */
#define v_TXPWRON_SIG(n)	(((n) & 0x01) << 3)
#define m_ENHPD_RXSENSE_SIG	BIT(2)
/*1: enable detect hdp & rx sense */
#define v_ENHPD_RXSENSE_SIG(n)	(((n) & 0x01) << 2)
#define m_SEL_DATAEN_POL	BIT(1)
#define v_SEL_DATAEN_POL(n)	(((n) & 0x01) << 1)
#define m_SEL_INTERFACE		BIT(0)
#define v_SEL_INTERFACE(n)	(((n) & 0x01) << 0)

#define PHY_TST0			0x3001
#define m_TEST_CLR_SIG		BIT(5)
#define m_TEST_EN_SIG		BIT(4)
#define m_TEST_CLK_SIG		BIT(0)

#define PHY_TST1			0x3002
#define PHY_TST2			0x3003
#define PHY_STAT0			0x3004
#define PHY_INI0			0x3005
#define PHY_MASK			0x3006
#define PHY_POL0			0x3007
#define m_PHY_RX_SENSE3		BIT(7)
#define v_PHY_TX_SENSE3(n)	(((n) & 0x01) << 7)
#define m_PHY_RX_SENSE2		BIT(6)
#define v_PHY_TX_SENSE2(n)      (((n) & 0x01) << 6)
#define m_PHY_RX_SENSE1		BIT(5)
#define v_PHY_TX_SENSE1(n)      (((n) & 0x01) << 5)
#define m_PHY_RX_SENSE0		BIT(4)
#define v_PHY_TX_SENSE0(n)      (((n) & 0x01) << 4)
#define m_PHY_HPD		BIT(1)
#define v_PHY_HPD		(((n) & 0x01) << 1)
#define m_PHY_LOCK		BIT(0)
#define v_PHY_LOCK(n)		(((n) & 0x01) << 0)

#define PHY_PCLFREQ0			0x3008
#define PHY_PCLFREQ1			0x3009
#define PHY_PLLCFGFREQ0			0x300a
#define PHY_PLLCFGFREQ1			0x300b
#define PHY_PLLCFGFREQ2			0x300c

/* I2C Master PHY Registers */
#define I2C_MASTER_PHY_BASE		0x3020

#define	PHY_I2CM_SLAVE			0x3020
#define PHY_GEN2_ADDR		0x69
#define PHY_HEAC_ADDR		0x49
#define PHY_I2C_SLAVE_ADDR	0x54

#define	PHY_I2CM_ADDRESS		0x3021
#define	PHY_I2CM_DATAO_1		0x3022
#define	PHY_I2CM_DATAO_0		0x3023
#define	PHY_I2CM_DATAI_1		0x3024
#define	PHY_I2CM_DATAI_0		0x3025

#define	PHY_I2CM_OPERATION		0x3026
#define m_PHY_I2CM_WRITE	BIT(4)
#define m_PHY_I2CM_READ		BIT(0)

#define	PHY_I2CM_INT			0x3027
#define m_PHY_I2CM_DONE_INT_POL	BIT(3)
#define v_PHY_I2CM_DONE_INT_POL(n) (((n) & 0x01) << 3)
#define m_PHY_I2CM_DONE_MASK	BIT(2)
#define v_PHY_I2CM_DONE_MASK(n)	(((n) & 0x01) << 2)
#define m_PHY_I2CM_DONE_INT	BIT(1)
#define m_PHY_I2CM_DONE_STATUS	BIT(0)

#define	PHY_I2CM_CTLINT			0x3028
#define m_PHY_I2CM_NACK_POL	BIT(7)
#define v_PHY_I2CM_NACK_POL(n)	(((n) & 0x01) << 7)
#define m_PHY_I2CM_NACK_MASK	BIT(6)
#define v_PHY_I2CM_NACK_MASK(n)	(((n) & 0x01) << 6)
#define m_PHY_I2CM_NACK_INT	BIT(5)
#define m_PHY_I2CM_NACK_STATUS	BIT(4)
#define m_PHY_I2CM_ARB_POL	BIT(3)
#define v_PHY_I2CM_ARB_POL(n)	(((n) & 0x01) << 3)
#define m_PHY_I2CM_ARB_MASK	BIT(2)
#define v_PHY_I2CM_ARB_MASK(n)	(((n) & 0x01) << 2)
#define m_PHY_I2CM_ARB_INT	BIT(1)
#define m_PHY_I2CM_ARB_STATUS	BIT(0)

#define	PHY_I2CM_DIV			0x3029
#define m_PHY_I2CM_FAST_STD	BIT(3)
#define v_PHY_I2CM_FAST_STD(n)	(((n) & 0x01) << 3)

#define	PHY_I2CM_SOFTRSTZ		0x302a
#define m_PHY_I2CM_SOFTRST	BIT(0)
#define v_PHY_I2CM_SOFTRST(n)	(((n) & 0x01) << 0)

#define	PHY_I2CM_SS_SCL_HCNT_1_ADDR	0x302b
#define	PHY_I2CM_SS_SCL_HCNT_0_ADDR	0x302c
#define	PHY_I2CM_SS_SCL_LCNT_1_ADDR	0x302d
#define	PHY_I2CM_SS_SCL_LCNT_0_ADDR	0x302e
#define	PHY_I2CM_FS_SCL_HCNT_1_ADDR	0x302f
#define	PHY_I2CM_FS_SCL_HCNT_0_ADDR	0x3030
#define	PHY_I2CM_FS_SCL_LCNT_1_ADDR	0x3031
#define	PHY_I2CM_FS_SCL_LCNT_0_ADDR	0x3032
#define	PHY_I2CM_SDA_HOLD		0x3033

/* Audio Sampler Registers */
#define AUDIO_SAMPLER_BASE		0x3100

#define AUD_CONF0			0x3100
#define m_SW_AUD_FIFO_RST	BIT(7)
#define v_SW_AUD_FIFO_RST(n)	(((n) & 0x01) << 7)
enum {
	AUDIO_SPDIF_GPA = 0,
	AUDIO_I2S
};

#define m_I2S_SEL		BIT(5)
#define v_I2S_SEL(n)		(((n) & 0x01) << 5)
enum {
	I2S_CHANNEL_1_2 = 1,
	I2S_CHANNEL_3_4 = 3,
	I2S_CHANNEL_5_6 = 7,
	I2S_CHANNEL_7_8 = 0xf
};

#define m_I2S_IN_EN		(0x0f << 0)
#define v_I2S_IN_EN(n)		(((n) & 0x0f) << 0)

#define AUD_CONF1			0x3101
enum I2S_MODE {
	I2S_STANDARD_MODE = 0,
	I2S_RIGHT_JUSTIFIED_MODE,
	I2S_LEFT_JUSTIFIED_MODE,
	I2S_BURST_1_MODE,
	I2S_BURST_2_MODE
};

#define m_I2S_MODE		(0x07 << 5)
#define v_I2S_MODE(n)		(((n) & 0x07) << 5)
enum I2S_WIDTH {
	I2S_16BIT_SAMPLE = 16,
	I2S_17BIT_SAMPLE,
	I2S_18BIT_SAMPLE,
	I2S_19BIT_SAMPLE,
	I2S_20BIT_SAMPLE,
	I2S_21BIT_SAMPLE,
	I2S_22BIT_SAMPLE,
	I2S_23BIT_SAMPLE,
	I2S_24BIT_SAMPLE,
};

#define m_I2S_WIDTH		(0x1f << 0)
#define v_I2S_WIDTH(n)		(((n) & 0x1f) << 0)

#define AUD_INT				0x3102
#define AUD_SPDIFINT			0x3302
#define m_FIFO_EMPTY_MASK	BIT(3)
#define v_FIFO_EMPTY_MASK(n)	(((n) & 0x01) << 3)
#define m_FIFO_FULL_MASK	BIT(2)
#define v_FIFO_FULL_MASK(n)	(((n) & 0x01) << 2)

#define AUD_CONF2			0x3103
#define m_NLPCM_EN		BIT(1)
#define v_NLPCM_EN(n)		(((n) & 0x01) << 1)
#define m_HBR_EN		BIT(0)
#define v_HBR_EN(n)		(((n) & 0x01) << 0)

#define AUD_INT1			0x3104
#define AUD_SPDIFINT1			0x3303
#define m_FIFO_OVERRUN_MASK	BIT(4)
#define v_FIFO_OVERRUN_MASK(n)	(((n) & 0x01) << 4)

/***************N-CTS Table**************/
/*	TMDS LOWCLK:	<=148.5M	*/
/*	TMDS MIDCLK:	297M		*/
/*	TMDS HIGHCLK:	594M		*/
#define N_32K_LOWCLK		0x1000
#define N_32K_MIDCLK		0x0c00
#define N_32K_HIGHCLK		0x0c00
#define N_441K_LOWCLK		0x1880
#define N_441K_MIDCLK		0x1260
#define N_441K_HIGHCLK		0x24c0
#define N_48K_LOWCLK		0x1800
#define N_48K_MIDCLK		0x1400
#define N_48K_HIGHCLK		0x1800
#define N_882K_LOWCLK		0x3100
#define N_882K_MIDCLK		0x24c0
#define N_882K_HIGHCLK		0x4980
#define N_96K_LOWCLK		0x3000
#define N_96K_MIDCLK		0x2800
#define N_96K_HIGHCLK		0x3000
#define N_1764K_LOWCLK		0x6200
#define N_1764K_MIDCLK		0x4980
#define N_1764K_HIGHCLK		0x9300
#define N_192K_LOWCLK		0x6000
#define N_192K_MIDCLK		0x5000
#define N_192K_HIGHCLK		0x6000

#define CALC_CTS(N, TMDSCLK, FS)	(((N) / 32) * (TMDSCLK) / ((FS) * 4))
/****************************************/

#define AUD_N1				0x3200
#define AUD_N2				0x3201

#define AUD_N3				0x3202
#define m_NCTS_ATOMIC_WR	BIT(7)
#define v_NCTS_ATOMIC_WR(n)	(((n) & 0x01) << 7)
#define m_AUD_N3		(0x0f << 0)
#define v_AUD_N3(n)		(((n) & 0x0f) << 0)

#define AUD_CTS1			0x3203
#define AUD_CTS2			0x3204

#define AUD_CTS3			0x3205
enum {
	N_SHIFT_1 = 0,
	N_SHIFT_16,
	N_SHIFT_32,
	N_SHIFT_64,
	N_SHIFT_128,
	N_SHIFT_256,
	N_SHIFT_OTHERS_128
};

#define m_N_SHIFT		(0x07 << 5)
#define v_N_SHIFT(n)		(((n) & 0x07) << 5)
#define m_CTS_MANUAL		BIT(4)
#define v_CTS_MANUAL(n)		(((n) & 0x01) << 4)
#define m_AUD_CTS3		(0x0f << 0)
#define v_AUD_CTS3(n)		(((n) & 0x0f) << 0)

#define AUD_INPUTCLKFS			0x3206
enum {
	FS_128 = 0,
	FS_256,
	FS_512,
	FS_64 = 4,
	FS_OTHERS_128
};

#define m_LFS_FACTOR		(0x07 << 0)
#define v_LFS_FACTOR(n)		(((n) & 0x07) << 0)

#define AUD_SPDIF0			0x3300
#define m_SW_SAUD_FIFO_RST	BIT(7)
#define v_SW_SAUD_FIFO_RST(n)	(((n) & 0x01) << 7)

#define AUD_SPDIF1			0x3301
enum {
	PCM_LINEAR = 0,
	PCM_NONLINEAR
};

#define m_SET_NLPCM		BIT(7)
#define v_SET_NLPCM(n)		(((n) & 0x01) << 7)
#define m_SPDIF_HBR_MODE	BIT(6)
#define v_SPDIF_HBR_MODE(n)	(((n) & 0x01) << 6)
#define m_SPDIF_WIDTH		(0x1f << 0)
#define v_SPDIF_WIDTH(n)	(((n) & 0x1f) << 0)

/* Generic Parallel Audio Interface Registers */
#define GP_AUDIO_INTERFACE_BASE		0x3500

#define	GP_CONF0			0x3500
#define	GP_CONF1			0x3501
#define	GP_CONF2			0x3502
#define	GP_MASK				0x3506

/* Audio DMA Registers */
#define AUDIO_DMA_BASE			0x3600

#define	AHB_DMA_CONF0			0x3600
#define	AHB_DMA_START			0x3601
#define	AHB_DMA_STOP			0x3602
#define	AHB_DMA_THRSLD			0x3603
#define	AHB_DMA_STRADDR_SET0_0		0x3604	/* 0~3 */
#define	AHB_DMA_STPADDR_SET0_0		0x3608	/* 0~3 */
#define	AHB_DMA_BSTADDR0		0x360c	/* 0~3 */
#define	AHB_DMA_MBLENGTH0		0x3610	/* 0~3 */
#define	AHB_DMA_MASK			0x3614
#define	AHB_DMA_CONF1			0x3616
#define	AHB_DMA_BUFFMASK		0x3619
#define	AHB_DMA_MASK1			0x361b
#define	AHB_DMA_STATUS			0x361c
#define	AHB_DMA_CONF2			0x361d
#define	AHB_DMA_STRADDR_SET1_0		0x3620	/* 0~3 */
#define	AHB_DMA_STPADDR_SET1_0		0x3624	/* 0~3 */

/* Main Controller Registers */
#define MAIN_CONTROLLER_BASE		0x4000

#define MC_CLKDIS			0x4001
#define m_HDCPCLK_DISABLE	BIT(6)
#define v_HDCPCLK_DISABLE(n)	(((n) & 0x01) << 6)
#define m_CECCLK_DISABLE	BIT(5)
#define v_CECCLK_DISABLE(n)	(((n) & 0x01) << 5)
#define m_CSCCLK_DISABLE	BIT(4)
#define v_CSCCLK_DISABLE(n)	(((n) & 0x01) << 4)
#define m_AUDCLK_DISABLE        BIT(3)
#define v_AUDCLK_DISABLE(n)     (((n) & 0x01) << 3)
#define m_PREPCLK_DISABLE	BIT(2)
#define v_PREPCLK_DISABLE(n)	(((n) & 0x01) << 2)
#define m_TMDSCLK_DISABLE	BIT(1)
#define v_TMDSCLK_DISABLE(n)	(((n) & 0x01) << 1)
#define m_PIXELCLK_DISABLE	BIT(0)
#define v_PIXELCLK_DISABLE(n)	(((n) & 0x01) << 0)

#define MC_SWRSTZREQ			0x4002
#define m_IGPA_SWRST		BIT(7)
#define v_IGPA_SWRST(n)		(((n) & 0x01) << 7)
#define m_CEC_SWRST		BIT(6)
#define v_CEC_SWRST(n)		(((n) & 0x01) << 6)
#define m_ISPDIF_SWRST          BIT(4)
#define v_ISPDIF_SWRST(n)       (((n) & 0x01) << 4)
#define m_II2S_SWRST            BIT(3)
#define v_II2S_SWRST(n)         (((n) & 0x01) << 3)
#define m_PREP_SWRST            BIT(2)
#define v_PREP_SWRST(n)         (((n) & 0x01) << 2)
#define m_TMDS_SWRST		BIT(1)
#define v_TMDS_SWRST(n)		(((n) & 0x01) << 1)
#define m_PIXEL_SWRST           BIT(0)
#define v_PIXEL_SWRST(n)        (((n) & 0x01) << 0)

#define MC_OPCTRL			0x4003
#define m_HDCP_BLOCK_BYP	BIT(0)
#define v_HDCP_BLOCK_BYP(n)	(((n) & 0x01) << 0)

#define MC_FLOWCTRL			0x4004
#define m_FEED_THROUGH_OFF      BIT(0)
#define v_FEED_THROUGH_OFF(n)   (((n) & 0x01) << 0)

#define MC_PHYRSTZ			0x4005
#define m_PHY_RSTZ		BIT(0)
#define v_PHY_RSTZ(n)		(((n) & 0x01) << 0)

#define MC_LOCKONCLOCK			0x4006
#define m_IGPACLK_ON		BIT(7)
#define v_IGPACLK_ON(n)		(((n) & 0x01) << 7)
#define m_PCLK_ON		BIT(6)
#define v_PCLK_ON(n)		(((n) & 0x01) << 6)
#define m_TMDSCLK_ON            BIT(5)
#define v_TMDSCLK_ON(n)         (((n) & 0x01) << 5)
#define m_PREPCLK_ON            BIT(4)
#define v_PREPCLK_ON(n)         (((n) & 0x01) << 4)
#define m_I2SCLK_ON		BIT(3)
#define v_I2SCLK_ON(n)		(((n) & 0x01) << 3)
#define m_SPDIFCLK_ON           BIT(2)
#define v_SPDIFCLK_ON(n)	(((n) & 0x01) << 2)
#define m_CECCLK_ON		BIT(0)
#define v_CECCLK_ON(n)		(((n) & 0x01) << 0)

#define MC_HEACPHY_RST			0x4007
#define m_HEAC_PHY_RST		BIT(0)
#define v_HEAC_PHY_RST(n)	(((n) & 0x01) << 0)

#define MC_LOCKONCLOCK_2		0x4009
#define m_AHB_AUD_DMA_CLK       BIT(0)
#define v_AHB_AUD_DMA_CLK(n)    (((n) & 0x01) << 0)

#define MC_SWRSTZREQ_2			0x400a
#define m_AHB_AUD_DMA_RST       BIT(7)
#define v_AHB_AUD_DMA_RST(n)    (((n) & 0x01) << 7)

/* Color Space Converter Registers */
#define COLOR_SPACE_CONVERTER_BASE	0x4100

#define	CSC_CFG				0x4100
#define m_CSC_INTPMODE		(0x03 << 4)
#define v_CSC_INTPMODE(n)	(((n) & 0x03) << 4)
#define m_CSC_DECIMODE		(0x03 << 0)
#define v_CSC_DECIMODE(n)	(((n) & 0x03) << 0)

#define	CSC_SCALE			0x4101
#define m_CSC_COLOR_DEPTH	(0x0f << 4)
#define v_CSC_COLOR_DEPTH(n)	(((n) & 0x0f) << 4)
#define m_CSC_SCALE		(0x03 << 0)
#define v_CSC_SCALE(n)		(((n) & 0x03) << 0)

#define	CSC_COEF_A1_MSB			0x4102
#define	CSC_COEF_A1_LSB			0x4103
#define	CSC_COEF_A2_MSB			0x4104
#define	CSC_COEF_A2_LSB			0x4105
#define	CSC_COEF_A3_MSB			0x4106
#define	CSC_COEF_A3_LSB			0x4107
#define	CSC_COEF_A4_MSB			0x4108
#define	CSC_COEF_A4_LSB			0x4109
#define	CSC_COEF_B1_MSB			0x410a
#define	CSC_COEF_B1_LSB			0x410b
#define	CSC_COEF_B2_MSB			0x410c
#define	CSC_COEF_B2_LSB			0x410d
#define	CSC_COEF_B3_MSB			0x410e
#define	CSC_COEF_B3_LSB			0x410f
#define	CSC_COEF_B4_MSB			0x4110
#define	CSC_COEF_B4_LSB			0x4111
#define	CSC_COEF_C1_MSB			0x4112
#define	CSC_COEF_C1_LSB			0x4113
#define	CSC_COEF_C2_MSB			0x4114
#define	CSC_COEF_C2_LSB			0x4115
#define	CSC_COEF_C3_MSB			0x4116
#define	CSC_COEF_C3_LSB			0x4117
#define	CSC_COEF_C4_MSB			0x4118
#define	CSC_COEF_C4_LSB			0x4119
#define	CSC_SPARE_1			0x411a
#define	CSC_SPARE_2			0x411b

/* HDCP Encryption Engine Registers */
#define HDCP_ENCRYPTION_ENGINE_BASE	0x5000

#define	A_HDCPCFG0			0x5000
#define m_HDCP_ENHANCE_LIKE	BIT(7)
#define v_HDCP_ENHANCE_LIKE(n)	(((n) & 0x01) << 7)
#define m_I2C_FAST_MODE		BIT(6)
#define v_I2C_FAST_MODE(n)	(((n) & 0x01) << 6)
#define m_ENCRYPT_BYPASS	BIT(5)
#define v_ENCRYPT_BYPASS(n)	(((n) & 0x01) << 5)
#define m_SYNC_RI_CHECK		BIT(4)
#define v_SYNC_RI_CHECK(n)	(((n) & 0x01) << 4)
#define m_AVMUTE		BIT(3)
#define m_RX_DETECT		BIT(2)
#define v_RX_DETECT(n)		(((n) & 0x01) << 2)
#define m_FEATURE11_EN		BIT(1)
#define v_FEATURE11_EN(n)	(((n) & 0x01) << 1)
#define m_HDMI_DVI		BIT(0)
#define v_HDMI_DVI(n)		(((n) & 0x01) << 0)

#define	A_HDCPCFG1			0x5001
#define m_HDCP_LOCK		BIT(4)
#define v_HDCP_LOCK(n)		(((n) & 0x01) << 4)
#define m_SHA1_CHECK_DISABLE	BIT(3)
#define v_SHA1_CHECK_DISBALE(n)	(((n) & 0x01) << 3)
#define m_PH2UPSHFTENC		BIT(2)
#define v_PH2UPSHFTENC(n)	(((n) & 0x01) << 2)
#define m_ENCRYPT_DISBALE	BIT(1)
#define v_ENCRYPT_DISBALE(n)	(((n) & 0x01) << 1)
#define m_HDCP_SW_RST		BIT(0)
#define v_HDCP_SW_RST(n)	(((n) & 0x01) << 0)

#define	A_HDCPOBS0			0x5002
#define m_STATE_AUTH		(0x0f << 4)
#define m_SUB_STATE_AUTH	(0x07 << 1)
#define m_STATE_HDCP_ENGAGED	BIT(0)

#define	A_HDCPOBS1			0x5003
#define m_STATE_OESS		(0x07 << 3)
#define m_STATE_REVO		(0x07 << 0)

#define	A_HDCPOBS2			0x5004
#define m_STATE_CIPHER		(0x07 << 3)
#define m_STATE_EESS		(0x07 << 0)

#define	A_HDCPOBS3			0x5005
#define m_BCAP_REPEATER		BIT(6)
#define m_BCAP_KSVFIFO_READY	BIT(5)
#define m_BCAP_FAST_I2C		BIT(4)
#define m_BCAP_HDMI_MODE	BIT(2)
#define m_BCAP_FEATURES11	BIT(1)
#define m_BCAP_FAST_REAUTH	BIT(0)

#define	A_APIINTCLR			0x5006
#define	A_APIINTSTAT			0x5007
#define	A_APIINTMSK			0x5008
#define m_HDCP_ENGAGED		BIT(7)
#define m_HDCP_FAILED		BIT(6)
#define m_HDCP_I2C_NOACK	BIT(4)
#define m_HDCP_LOST_ARBI	BIT(3)
#define m_KEEP_ERR_INT		BIT(2)
#define m_KSVSHA1_CALC_INT	BIT(1)
#define m_KSV_ACCESS_INT	BIT(0)
#define v_HDCP_ENGAGED(n)	(((n) & 0x01) << 7)
#define v_HDCP_FAILED(n)	(((n) & 0x01) << 6)
#define v_HDCP_I2C_NOACK(n)	(((n) & 0x01) << 4)
#define v_HDCP_LOST_ARBI(n)	(((n) & 0x01) << 3)
#define v_KEEP_ERR_INT(n)	(((n) & 0x01) << 1)
#define v_KSVSHA1_CALC_INT(n)	(((n) & 0x01) << 1)
#define v_KSV_ACCESS_INT(n)	(((n) & 0x01) << 0)

#define	A_VIDPOLCFG			0x5009
#define m_UNENCRYT_CONF		(0x03 << 5)
#define v_UNENCRYT_CONF(n)	(((n) & 0x03) << 5)
#define m_DATAEN_POL		BIT(4)
#define v_DATAEN_POL(n)		(((n) & 0x01) << 4)
#define m_VSYNC_POL		BIT(3)
#define v_VSYNC_POL(n)		(((n) & 0x01) << 3)
#define m_HSYNC_POL		BIT(1)
#define v_HSYNC_POL(n)		(((n) & 0x01) << 1)

#define	A_OESSWCFG			0x500a
#define	A_COREVERLSB			0x5014
#define	A_COREVERMSB			0x5015

#define	A_KSVMEMCTRL			0x5016
#define m_SHA1_FAIL		BIT(3)
#define v_SHA1_FAIL(n)		(((n) & 0x01) << 3)
#define m_KSV_UPDATE		BIT(2)
#define v_KSV_UPDATE(n)		(((n) & 0x01) << 2)
#define m_KSV_MEM_ACCESS	BIT(1)
#define m_KSV_MEM_REQ		BIT(0)
#define v_KSV_MEM_REQ(n)	(((n) & 0x01) << 0)

#define	HDCP_BSTATUS_0			0x5020
#define m_MAX_DEVS_EXCEEDED	BIT(7)
#define m_DEVICE_COUNT		(0x7f << 0)

#define	HDCP_BSTATUS_1			0x5021
#define	HDCP_M0_0			0x5022
#define	HDCP_M0_1			0x5023
#define	HDCP_M0_2			0x5024
#define	HDCP_M0_3			0x5025
#define	HDCP_M0_4			0x5026
#define	HDCP_M0_5			0x5027
#define	HDCP_M0_6			0x5028
#define	HDCP_M0_7			0x5029
#define	HDCP_KSV			0x502a	/* 0~634 */
#define	HDCP_VH				0x52a5	/* 0~19 */
#define	HDCP_REVOC_SIZE_0		0x52b9
#define	HDCP_REVOC_SIZE_1		0x52ba
#define	HDCP_REVOC_LIST			0x52bb	/* 0~5059 */

/* HDCP BKSV Registers */
#define HDCP_BKSV_BASE			0x7800

#define	HDCPREG_BKSV0			0x7800
#define	HDCPREG_BKSV1			0x7801
#define	HDCPREG_BKSV2			0x7802
#define	HDCPREG_BKSV3			0x7803
#define	HDCPREG_BKSV4			0x7804

/* HDCP AN Registers */
#define HDCP_AN_BASE			0x7805

#define	HDCPREG_ANCONF			0x7805
#define m_OAN_BYPASS		BIT(0)
#define v_OAN_BYPASS(n)		(((n) & 0x01) << 0)

#define	HDCPREG_AN0			0x7806
#define	HDCPREG_AN1			0x7807
#define	HDCPREG_AN2			0x7808
#define	HDCPREG_AN3			0x7809
#define	HDCPREG_AN4			0x780a
#define	HDCPREG_AN5			0x780b
#define	HDCPREG_AN6			0x780c
#define	HDCPREG_AN7			0x780d

/* Encrypted DPK Embedded Storage Registers */
#define ENCRYPTED_DPK_EMBEDDED_BASE	0x780e

#define	HDCPREG_RMCTL			0x780e
#define m_DPK_DECRYPT_EN	BIT(0)
#define v_DPK_DECRYPT_EN(n)	(((n) & 0x01) << 0)

#define	HDCPREG_RMSTS			0x780f
#define m_DPK_WR_OK_STS		BIT(6)
#define m_DPK_DATA_INDEX	(0x3f << 6)

#define	HDCPREG_SEED0			0x7810
#define	HDCPREG_SEED1			0x7811
#define	HDCPREG_DPK0			0x7812
#define	HDCPREG_DPK1			0x7813
#define	HDCPREG_DPK2			0x7814
#define	HDCPREG_DPK3			0x7815
#define	HDCPREG_DPK4			0x7816
#define	HDCPREG_DPK5			0x7817
#define	HDCPREG_DPK6			0x7818

#define HDCP2REG_BASE			0x7900
#define HDCP2REG_ID			0x7900
#define HDCP2REG_CTRL			0x7904
	#define m_HDCP2_HDP_OVR_VAL	BIT(5)
	#define m_HDCP2_HDP_OVR_EN	BIT(4)
	#define m_HDCP2_FORCE		BIT(2)
	#define m_HDCP2_OVR_EN		BIT(1)
	#define m_HDCP2_SWITCH_EN	BIT(0)

	#define v_HDCP2_HDP_OVR_VAL(n)	(((n) & 0x01) << 5)
	#define v_HDCP2_HDP_OVR_EN(n)	(((n) & 0x01) << 4)
	#define v_HDCP2_FORCE(n)	(((n) & 0x01) << 2)
	#define v_HDCP2_OVR_EN(n)	(((n) & 0x01) << 1)
	#define v_HDCP2_SWITCH_EN(n)	(((n) & 0x01) << 0)
#define HDCP2REG_CTRL1			0x7905
	#define m_HDCP2_CD_VAL		(0xf << 4)
	#define m_HDCP2_CD_EN		BIT(3)
	#define m_HDCP2_AVMUTE_OVR_VAL	BIT(1)
	#define m_HDCP2_AVMUTE_OVR_EN	BIT(0)

	#define v_HDCP2_CD_VAL(n)		(((n) & 0x0f) << 4)
	#define v_HDCP2_CD_EN(n)		(((n) & 0x01) << 3)
	#define v_HDCP2_AVMUTE_OVR_VAL(n)	(((n) & 0x01) << 1)
	#define v_HDCP2_AVMUTE_OVR_EN(n)	(((n) & 0x01) << 0)
#define HDCP2REG_STAS			0x7908
#define HDCP2REG_MASK			0x790c
#define HDCP2REG_STAT			0x790d
#define HDCP2REG_MUTE			0x790e
	#define m_HDCP2_CAPABLE		BIT(0)
	#define m_HDCP2_NOTCAPABLE	BIT(1)
	#define m_HDCP2_AUTH_LOST	BIT(2)
	#define m_HDCP2_AUTH_OK		BIT(3)
	#define m_HDCP2_AUTH_FAIL	BIT(4)
	#define m_HDCP2_DECRYPTED_CHG	BIT(5)

/* CEC Engine Registers */
#define CEC_ENGINE_BASE			0x7d00

#define	CEC_CTRL			0x7d00
	#define m_CEC_BC_S_NCK		BIT(5)	 /* Ignore ack of sending
						  * broadcast message.
						  */
	#define m_CEC_STANBY		BIT(4)
	#define m_CEC_BC_NCK		BIT(3)
	#define m_CEC_FRAME_TYPE	(3 << 1)
	#define m_CEC_SEND		BIT(0)
	#define v_CEC_BC_S_NCK(n)	(((n) & 0x1) << 4)
	#define v_CEC_STANBY(n)		((n & 0x1) << 4)
	#define v_CEC_BC_NCK(n)		((n & 0x1) << 3)
	#define v_CEC_FRAME_TYPE(n)	((n & 0x3) << 1)
	#define v_CEC_SEND(n)		(n & 0x1)
#define	CEC_MASK			0x7d02
#define	CEC_ADDR_L			0x7d05
#define	CEC_ADDR_H			0x7d06
#define	CEC_TX_CNT			0x7d07
#define	CEC_RX_CNT			0x7d08
#define	CEC_TX_DATA0			0x7d10	/* txdata0~txdata15 */
#define	CEC_RX_DATA0			0x7d20	/* rxdata0~rxdata15 */
#define CEC_LOCK			0x7d30
#define	CEC_WKUPCTRL			0x7d31

/* I2C Master Registers */
#define I2C_MASTER_BASE			0x7e00

#define	I2CM_SLAVE			0x7e00
#define	I2CM_ADDRESS			0x7e01
#define	I2CM_DATAO			0x7e02
#define	I2CM_DATAI			0x7e03

#define I2CM_OPERATION			0x7e04
#define m_I2CM_WR		BIT(4)
#define v_I2CM_WR(n)		(((n) & 0x01) << 4)
#define m_I2CM_RD8_EXT		BIT(3)
#define v_I2CM_RD8_EXT(n)	(((n) & 0x01) << 3)
#define m_I2CM_RD8		BIT(2)
#define v_I2CM_RD8(n)		(((n) & 0x01) << 2)
#define m_I2CM_RD_EXT		BIT(1)
#define v_I2CM_RD_EXT(n)	(((n) & 0x01) << 1)
#define m_I2CM_RD		BIT(0)
#define v_I2CM_RD(n)		(((n) & 0x01) << 0)

#define	I2CM_INT			0x7e05
#define m_I2CM_RD_REQ_MASK	BIT(6)
#define v_I2CM_RD_REQ_MASK(n)	(((n) & 0x01) << 6)
#define m_I2CM_DONE_MASK	BIT(2)
#define v_I2CM_DONE_MASK(n)	(((n) & 0x01) << 2)

#define	I2CM_CTLINT			0x7e06
#define m_I2CM_NACK_MASK	BIT(6)
#define v_I2CM_NACK_MASK(n)	(((n) & 0x01) << 6)
#define m_I2CM_ARB_MASK		BIT(2)
#define v_I2CM_ARB_MASK(n)	(((n) & 0x01) << 2)

#define	I2CM_DIV			0x7e07
enum {
	STANDARD_MODE = 0,
	FAST_MODE
};

#define m_I2CM_FAST_STD_MODE	BIT(3)
#define v_I2CM_FAST_STD_MODE(n)	(((n) & 0x01) << 3)

#define	I2CM_SEGADDR			0x7e08
#define m_I2CM_SEG_ADDR		(0x7f << 0)
#define v_I2CM_SEG_ADDR(n)	(((n) & 0x7f) << 0)

#define	I2CM_SOFTRSTZ			0x7e09
#define m_I2CM_SOFTRST		BIT(0)
#define v_I2CM_SOFTRST(n)	(((n) & 0x01) << 0)

#define	I2CM_SEGPTR			0x7e0a
#define	I2CM_SS_SCL_HCNT_1_ADDR		0x7e0b
#define	I2CM_SS_SCL_HCNT_0_ADDR		0x7e0c
#define	I2CM_SS_SCL_LCNT_1_ADDR		0x7e0d
#define	I2CM_SS_SCL_LCNT_0_ADDR		0x7e0e
#define	I2CM_FS_SCL_HCNT_1_ADDR		0x7e0f
#define	I2CM_FS_SCL_HCNT_0_ADDR		0x7e10
#define	I2CM_FS_SCL_LCNT_1_ADDR		0x7e11
#define	I2CM_FS_SCL_LCNT_0_ADDR		0x7e12
#define	I2CM_SDA_HOLD			0x7e13

#define	I2CM_SCDC_READ_UPDATE		0x7e14
#define m_I2CM_UPRD_VSYNC_EN	BIT(5)
#define v_I2CM_UPRD_VSYNC_EN(n) (((n) & 0x01) << 5)
#define m_I2CM_READ_REQ_EN	BIT(4)
#define v_I2CM_READ_REQ_EN(n)	(((n) & 0x01) << 4)
#define m_I2CM_READ_UPDATE	BIT(0)
#define v_I2CM_READ_UPDATE(n)	(((n) & 0x01) << 0)

#define	I2CM_READ_BUFF0			0x7e20	/* buff0~buff7 */
#define	I2CM_SCDC_UPDATE0		0x7e30
#define	I2CM_SCDC_UPDATE1		0x7e31

/*
* HDMI TX PHY Define Start
*/
#define PHYTX_OPMODE_PLLCFG		0x06
enum {
	PREP_DIV_BY_2 = 0,	/* 16 bits */
	PREP_DIV_BY_15,		/* 12 bits */
	PREP_DIV_BY_125,	/* 10 bits */
	PREP_DIV_BY_1,		/* 8 bits */
};

#define m_PREP_DIV		(0x03 << 13)
#define v_PREP_DIV(n)		(((n) & 0x03) << 13)
enum {
	TMDS_DIV_BY_1 = 0,
	TMDS_DIV_NOT_USED,
	TMDS_DIV_BY_3,
	TMDS_DIV_BY_4,
};

#define m_TMDS_CNTRL		(0x03 << 11)
#define v_TMDS_CNTRL(n)		(((n) & 0x03) << 11)
enum OPMODE {
	OP_HDMI_14 = 0,
	OP_HDMI_20,
};

#define m_OPMODE		(0x03 << 9)
#define v_OPMODE(n)		(((n) & 0x03) << 9)
enum {
	FBDIV2_BY_1 = 1,
	FBDIV2_BY_2,
	FBDIV2_BY_3,
	FBDIV2_BY_4,
	FBDIV2_BY_5,
	FBDIV2_BY_6,
};

#define m_FBDIV2_CNTRL		(0x07 << 6)
#define v_FBDIV2_CNTRL(n)	(((n) & 0x07) << 6)
enum {
	FBDIV1_BY_1 = 0,
	FBDIV1_BY_2,
	FBDIV1_BY_3,
	FBDIV1_BY_4,
};

#define m_FBDIV1_CNTRL		(0x03 << 4)
#define v_FBDIV1_CNTRL(n)	(((n) & 0x03) << 4)
enum {
	REF_DIV_BY_1 = 0,
	REF_DIV_BY_2,
	REF_DIV_NOT_USED,
	REF_DIV_BY_4,
};

#define m_REF_CNTRL		(0x03 << 2)
#define v_REF_CNTRL(n)		(((n) & 0x03) << 2)
#define m_MPLL_N_CNTRL		(0x03 << 0)
#define v_MPLL_N_CNTRL(n)	(((n) & 0x03) << 0)

#define PHYTX_CLKSYMCTRL		0x09
#define v_OVERRIDE(n)		(0x01 << 15)
#define m_SLOPEBOOST		(0x03 << 4)
#define v_SLOPEBOOST(n)		(((n) & 0x03) << 4)
#define m_TX_SYMON		(0x01 << 3)
#define v_TX_SYMON(n)		(((n) & 0x01) << 3)
#define m_PREEMPHASIS		(0x03 << 1)
#define v_PREEMPHASIS(n)	(((n) & 0x03) << 1)
#define m_CLK_SYMON		(0x01 << 0)
#define v_CLK_SYMON(n)		(((n) & 0x01) << 0)

#define PHYTX_VLEVCTRL			0x0e
#define m_SUP_TXLVL		(0x1f << 5)
#define v_SUP_TXLVL(n)		(((n) & 0x1f) << 5)
#define m_SUP_CLKLVL		(0x1f << 0)
#define v_SUP_CLKLVL(n)		(((n) & 0x1f) << 0)

#define PHYTX_PLLCURRCTRL		0x10
#define m_MPLL_PROP_CNTRL	(0x07 << 3)
#define v_MPLL_PROP_CNTRL(n)	(((n) & 0x07) << 3)
#define m_MPLL_INT_CNTRL	(0x07 << 0)
#define v_MPLL_INT_CNTRL(n)	(((n) & 0x07) << 0)

#define PHYTX_PLLGMPCTRL		0x15
#define m_MPLL_GMP_CNTRL	(0x03 << 0)
#define v_MPLL_GMP_CNTRL(n)	(((n) & 0x03) << 0)

enum TERM_RESIS {
	R50_OHMS = 0,
	R5714_OHMS,
	R6667_OHMS,
	R80_OHMS,
	R100_OHMS,
	R13333_OHMS,
	R200_OHMS,
	ROPEN_CIRCUIT,
};

#define PHYTX_TERM_RESIS		0x19
#define m_TX_TERM		(0x07 << 0)
#define v_TX_TERM(n)		(((n) & 0x07) << 0)

struct phy_mpll_config_tab {
	u32 pix_clock;
	u32 tmdsclock;
	u8 pix_repet;
	u8 color_depth;
	u16 prep_div;
	u16 tmdsmhl_cntrl;
	u16 opmode;
	u32 fbdiv2_cntrl;
	u16 fbdiv1_cntrl;
	u16 ref_cntrl;
	u16 n_cntrl;
	u32 prop_cntrl;
	u32 int_cntrl;
	u16 gmp_cntrl;
};

/* PHY Defined for RK322X */
#define EXT_PHY_CONTROL		0
	#define EXT_PHY_ANALOG_RESET_MASK		0x80
	#define EXT_PHY_DIGITAL_RESET_MASK		0x40
	#define EXT_PHY_PCLK_INVERT_MASK		0x08
	#define EXT_PHY_PREPCLK_INVERT_MASK		0x04
	#define EXT_PHY_TMDSCLK_INVERT_MASK		0x02
	#define EXT_PHY_SRC_SELECT_MASK			0x01

#define EXT_PHY_TERM_CAL		0x03
	#define EXT_PHY_TERM_CAL_EN_MASK		0x80
	#define EXT_PHY_TERM_CAL_DIV_H_MASK		0x7f

#define EXT_PHY_TERM_CAL_DIV_L		0x04

#define EXT_PHY_PLL_PRE_DIVIDER		0xe2
	#define EXT_PHY_PLL_FB_BIT8_MASK		0x80
	#define EXT_PHY_PLL_PCLK_DIV5_EN_MASK		0x20
	#define EXT_PHY_PLL_PRE_DIVIDER_MASK		0x1f

#define EXT_PHY_PLL_FB_DIVIDER		0xe3

#define EXT_PHY_PCLK_DIVIDER1		0xe4
	#define EXT_PHY_PCLK_DIVIDERB_MASK		0x60
	#define EXT_PHY_PCLK_DIVIDERA_MASK		0x1f

#define EXT_PHY_PCLK_DIVIDER2		0xe5
	#define EXT_PHY_PCLK_DIVIDERC_MASK		0x60
	#define EXT_PHY_PCLK_DIVIDERD_MASK		0x1f

#define EXT_PHY_TMDSCLK_DIVIDER		0xe6
	#define EXT_PHY_TMDSCLK_DIVIDERC_MASK		0x30
	#define EXT_PHY_TMDSCLK_DIVIDERA_MASK		0x0c
	#define EXT_PHY_TMDSCLK_DIVIDERB_MASK		0x03

#define EXT_PHY_PLL_BW			0xe7

#define EXT_PHY_PPLL_PRE_DIVIDER	0xe9
	#define EXT_PHY_PPLL_ENABLE_MASK		0xc0
	#define EXT_PHY_PPLL_PRE_DIVIDER_MASK		0x1f

#define EXT_PHY_PPLL_FB_DIVIDER		0xea

#define EXT_PHY_PPLL_POST_DIVIDER	0xeb
	#define EXT_PHY_PPLL_FB_DIVIDER_BIT8_MASK	0x80
	#define EXT_PHY_PPLL_POST_DIVIDER_MASK		0x30
	#define EXT_PHY_PPLL_LOCK_STATUS_MASK		0x01

#define EXT_PHY_PPLL_BW			0xec

#define EXT_PHY_SIGNAL_CTRL		0xee
	#define EXT_PHY_TRANSITION_CLK_EN_MASK		0x80
	#define EXT_PHY_TRANSITION_D0_EN_MASK		0x40
	#define EXT_PHY_TRANSITION_D1_EN_MASK		0x20
	#define EXT_PHY_TRANSITION_D2_EN_MASK		0x10
	#define EXT_PHY_LEVEL_CLK_EN_MASK		0x08
	#define EXT_PHY_LEVEL_D0_EN_MASK		0x04
	#define EXT_PHY_LEVEL_D1_EN_MASK		0x02
	#define EXT_PHY_LEVEL_D2_EN_MASK		0x01

#define EXT_PHY_SLOPEBOOST		0xef
	#define EXT_PHY_SLOPEBOOST_CLK_MASK		0x03
	#define EXT_PHY_SLOPEBOOST_D0_MASK		0x0c
	#define EXT_PHY_SLOPEBOOST_D1_MASK		0x30
	#define EXT_PHY_SLOPEBOOST_D2_MASK		0xc0

#define EXT_PHY_PREEMPHASIS		0xf0
	#define EXT_PHY_PREEMPHASIS_D0_MASK		0x03
	#define EXT_PHY_PREEMPHASIS_D1_MASK		0x0c
	#define EXT_PHY_PREEMPHASIS_D2_MASK		0x30

#define EXT_PHY_LEVEL1			0xf1
	#define EXT_PHY_LEVEL_CLK_MASK			0xf0
	#define EXT_PHY_LEVEL_D2_MASK			0x0f

#define EXT_PHY_LEVEL2			0xf2
	#define EXT_PHY_LEVEL_D1_MASK			0xf0
	#define EXT_PHY_LEVEL_D0_MASK			0x0f

#define EXT_PHY_TERM_RESIS_AUTO		0xf4
	#define EXT_PHY_AUTO_R50_OHMS			0
	#define EXT_PHY_AUTO_R75_OHMS			BIT(2)
	#define EXT_PHY_AUTO_R100_OHMS			(2 << 2)
	#define EXT_PHY_AUTO_ROPEN_CIRCUIT		(3 << 2)

#define EXT_PHY_TERM_RESIS_MANUAL_CLK	0xfb
#define EXT_PHY_TERM_RESIS_MANUAL_D2	0xfc
#define EXT_PHY_TERM_RESIS_MANUAL_D1	0xfd
#define EXT_PHY_TERM_RESIS_MANUAL_D0	0xfe

#define RK322X_DDC_MASK_EN	((3 << 13) | (3 << (13 + 16)))
#define RK322X_IO_3V_DOMAIN	((7 << 4) | (7 << (4 + 16)))
#define RK322X_PLL_POWER_DOWN	(BIT(12) | BIT(12 + 16))
#define RK322X_PLL_POWER_UP	BIT(12 + 16)
#define RK322X_PLL_PDATA_DEN	BIT(11 + 16)
#define RK322X_PLL_PDATA_EN	(BIT(11) | BIT(11 + 16))

#ifndef RK322X_GRF_SOC_CON2
#define RK322X_GRF_SOC_CON2	RK3228_GRF_SOC_CON2
#endif
#ifndef RK322X_GRF_SOC_CON6
#define RK322X_GRF_SOC_CON6	RK3228_GRF_SOC_CON6
#endif

struct ext_pll_config_tab {
	u32	pix_clock;
	u32	tmdsclock;
	u8	color_depth;
	u8	pll_nd;
	u16	pll_nf;
	u8	tmsd_divider_a;
	u8	tmsd_divider_b;
	u8	tmsd_divider_c;
	u8	pclk_divider_a;
	u8	pclk_divider_b;
	u8	pclk_divider_c;
	u8	pclk_divider_d;
	u8	vco_div_5;
	u8	ppll_nd;
	u16	ppll_nf;
	u8	ppll_no;
};

/*
* HDMI TX PHY Define End
*/

struct rockchip_hdmiv2_reg_table {
	int reg_base;
	int reg_end;
};

static inline u32 hdmi_readl(struct hdmi_dev *hdmi_dev, u16 offset)
{
	return readl_relaxed(hdmi_dev->regbase + (offset) * 0x04);
}

static inline int hdmi_writel(struct hdmi_dev *hdmi_dev, u16 offset, u32 val)
{
	int ret = 0;

	writel_relaxed(val, hdmi_dev->regbase + (offset) * 0x04);
	return ret;
}

static inline int hdmi_msk_reg(struct hdmi_dev *hdmi_dev,
			       u16 offset, u32 msk, u32 val)
{
	int ret = 0;
	u32 temp;

	temp = readl_relaxed(hdmi_dev->regbase +
			     (offset) * 0x04) & (0xFF - (msk));
	writel_relaxed(temp | ((val) & (msk)),
		       hdmi_dev->regbase + (offset) * 0x04);
	return ret;
}

irqreturn_t rockchip_hdmiv2_dev_irq(int irq, void *priv);
void rockchip_hdmiv2_dev_init_ops(struct hdmi_ops *ops);
void rockchip_hdmiv2_dev_initial(struct hdmi_dev *hdmi_dev);
void rockchip_hdmiv2_cec_init(struct hdmi *hdmi);
void rockchip_hdmiv2_cec_isr(struct hdmi_dev *hdmi_dev, char cec_int);
void rockchip_hdmiv2_hdcp_init(struct hdmi *hdmi);
void rockchip_hdmiv2_hdcp2_enable(int enable);
void rockchip_hdmiv2_hdcp_isr(struct hdmi_dev *hdmi_dev, int hdcp_int);
int rockchip_hdmiv2_write_phy(struct hdmi_dev *hdmi_dev,
			      int reg_addr, int val);
int rockchip_hdmiv2_read_phy(struct hdmi_dev *hdmi_dev,
			     int reg_addr);
#endif
