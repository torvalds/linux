#ifndef _RK3288_HDMI_HW_H
#define _RK3288_HDMI_HW_H
#include "../../rk_hdmi.h"

enum PWR_MODE{
	NORMAL,
	LOWER_PWR,
};
enum {
	OUTPUT_DVI = 0,
	OUTPUT_HDMI,
};


#define HDMI_SCL_RATE            (100*1000)

/*Register and Field Descriptions*/
/*Identification Registers*/
#define IDENTIFICATION_BASE		0x0000
enum IDENTIFICATION_REG{
	DESIGN_ID = IDENTIFICATION_BASE,
	REVISION_ID,
	PRODUCT_ID0,
	PRODUCT_ID1,
	CONFIG0_ID,
	CONFIG1_ID,
	CONFIG2_ID,
	CONFIG3_ID
};

//CONFIG0_ID
#define m_PREPEN		(1 << 7)
#define m_AUDSPDIF		(1 << 5)
#define m_AUDI2S		(1 << 4)
#define m_HDMI14		(1 << 3)
#define m_CSC			(1 << 2)
#define m_CEC			(1 << 1)
#define m_HDCP			(1 << 0)

//CONFIG1_ID
#define m_HDMI20		(1 << 5)
#define m_CONFAPB		(1 << 1)

//CONFIG2_ID
enum PHYTYPE {
	HDMI_TX_PHY = 0x00,
	HDMI_MHL_WITH_HEAC_PHY = 0xb2,
	HDMI_MHL_PHY = 0xc2,
	HDMI_3D_TX_WITH_HEAC_PHY = 0xe2,
	HDMI_3D_TX_PHY = 0xf2
};

//CONFIG3_ID
#define m_AHB_AUD_DMA		(1 << 1)
#define m_GP_AUD		(1 << 0)


/*Interrupt Registers*/
#define INTERRUPT_BASE                  0x0100
enum INTERRUPT_REG {
	IH_FC_STAT0 = INTERRUPT_BASE,
	IH_FC_STAT1,
	IH_FC_STAT2,
	IH_AS_STAT0,
	IH_PHY_STAT0,
	IH_I2CM_STAT0,
	IH_CEC_STAT0,
	IH_VP_STAT0,
	IH_I2CMPHY_STAT0,
	IH_AHBDMAAUD_STAT0,
	IH_DECODE = 0x0170,
	IH_MUTE_FC_STAT0 = 0x0180,
	IH_MUTE_FC_STAT1,
	IH_MUTE_FC_STAT2,
	IH_MUTE_AS_STAT0,
	IH_MUTE_PHY_STAT0,
	IH_MUTE_I2CM_STAT0,
	IH_MUTE_CEC_STAT0,
	IH_MUTE_VP_STAT0,
	IH_MUTE_I2CMPHY_STAT0,
	IH_MUTE_AHBDMAAUD_STAT0,
	IH_MUTE = 0x1ff
};

//IH_FC_STAT0
#define m_AUD_INFOFRAME		(1 << 7)
#define m_AUD_CONTENT_PROTECT	(1 << 6)
#define m_AUD_HBR		(1 << 5)
#define m_AUD_SAMPLE		(1 << 2)
#define m_AUD_CLK_REGEN		(1 << 1)
#define m_NULL_PACKET		(1 << 0)

//IH_FC_STAT1
#define m_GMD			(1 << 7)
#define m_ISCR1			(1 << 6)
#define m_ISCR2			(1 << 5)
#define m_VSD			(1 << 4)
#define m_SPD			(1 << 3)
#define m_AVI_INFOFRAME		(1 << 1)
#define m_GCP			(1 << 0)

//IH_FC_STAT2
#define m_LOWPRIO_OVERFLOW	(1 << 1)
#define m_HIGHPRIO_OVERFLOW	(1 << 0)

//IH_AS_SATA0
#define m_FIFO_UNDERRUN		(1 << 4)
#define m_FIFO_OVERRUN		(1 << 3)
#define m_AUD_FIFO_UDFLOW_THR	(1 << 2)
#define m_AUD_FIFO_UDFLOW	(1 << 1)
#define m_AUD_FIFO_OVERFLOW	(1 << 0)

//IH_PHY_STAT0
#define m_RX_SENSE3		(1 << 5)
#define m_RX_SENSE2		(1 << 4)
#define m_RX_SENSE1 		(1 << 3)
#define m_RX_SENSE0		(1 << 2)
#define m_TX_PHY_LOCK		(1 << 1)
#define m_HPD			(1 << 0)

//IH_I2CM_STAT0
#define m_SCDC_READREQ		(1 << 2)
#define m_I2CM_DONE		(1 << 1)
#define m_I2CM_ERROR		(1 << 0)

//IH_CEC_STAT0
#define m_WAKEUP		(1 << 6)
#define m_ERR_FOLLOW		(1 << 5)
#define m_ERR_INITIATOR		(1 << 4)
#define m_ARB_LOST		(1 << 3)
#define m_NACK			(1 << 2)
#define m_EOM			(1 << 1)
#define m_DONE			(1 << 0)

//IH_VP_STAT0
#define m_FIFOFULL_REPET	(1 << 7)
#define m_FIFOEMPTY_REPET	(1 << 6)
#define m_FIFOFULL_PACK		(1 << 5)
#define m_FIFOEMPTY_PACK	(1 << 4)
#define m_FIFOFULL_REMAP	(1 << 3)
#define m_FIFOEMPTY_REMAP	(1 << 2)
#define m_FIFOFULL_BYPASS	(1 << 1)
#define m_FIFOEMPTY_BYPASS	(1 << 0)

//IH_I2CMPHY_STAT0
#define m_I2CMPHY_DONE		(1 << 1)
#define m_I2CMPHY_ERR		(1 << 0)

//IH_AHBDMAAUD_STAT0
#define m_AUDDMA_INT_BUFOVERRUN	(1 << 6)
#define m_AUDDMA_INT_ERR	(1 << 5)
#define m_AUDDMA_INT_LOST	(1 << 4)
#define m_AUDDMA_INT_RETRYSPLIT (1 << 3)
#define m_AUDDMA_INT_DONE	(1 << 2)
#define m_AUDDMA_INT_BUFFULL	(1 << 1)
#define m_AUDDMA_INT_BUFEMPTY	(1 << 0)

//IH_DECODE
#define m_IH_FC_STAT0		(1 << 7)
#define m_IH_FC_STAT1		(1 << 6)
#define m_IH_FC_STAT2_VP	(1 << 5)
#define m_IH_AS_STAT0		(1 << 4)
#define m_IH_PHY		(1 << 3)
#define m_IH_I2CM_STAT0		(1 << 2)
#define m_IH_CEC_STAT0		(1 << 1)
#define m_IH_AHBDMAAUD_STAT0	(1 << 0)

//IH_MUTE_FC_STAT0
#define m_AUDI_MUTE		(1 << 7)
#define m_ACP_MUTE		(1 << 6)
#define m_DST_MUTE		(1 << 4)
#define m_OBA_MUTE		(1 << 3)
#define m_AUDS_MUTE		(1 << 2)
#define m_ACR_MUTE		(1 << 1)
#define m_NULL_MUTE		(1 << 0)

//Ih_MUTE_FC_STAT1
#define m_GMD_MUTE		(1 << 7)
#define m_ISCR1_MUTE		(1 << 6)
#define m_ISCR2_MUTE		(1 << 5)
#define m_VSD_MUTE		(1 << 4)
#define m_SPD_MUTE		(1 << 3)
#define m_AVI_MUTE		(1 << 1)
#define m_GCP_MUTE		(1 << 0)

//IH_MUTE_FC_STAT2
#define m_LPRIO_OVERFLOW_MUTE	(1 << 1)
#define m_HPRIO_OVERFLOW_MUTE	(1 << 0)

//IH_MUTE_AS_STAT0
#define m_FIFO_UNDERRUN_MUTE	(1 << 4)
#define m_FIFO_OVERRUN_MUTE	(1 << 3)
#define m_AUD_FIFO_UDF_THR_MUTE	(1 << 2)
#define m_AUD_FIFO_UDF_MUTE	(1 << 1)
#define m_AUD_FIFO_OVF_MUTE	(1 << 0)

//IH_MUTE_PHY_STAT0
#define m_RX_SENSE3_MUTE	(1 << 5)
#define m_RX_SENSE2_MUTE	(1 << 4)
#define m_RX_SENSE1_MUTE	(1 << 3)
#define m_RX_SENSE0_MUTE	(1 << 2)
#define m_TX_PHY_LOCK_MUTE	(1 << 1)
#define m_HPD_MUTE		(1 << 0)

//IH_MUTE_I2CM_STAT0
#define m_SCDC_READREQ_MUTE	(1 << 2)
#define m_I2CM_DONE_MUTE	(1 << 1)
#define m_I2CM_ERR_MUTE		(1 << 0)

//IH_MUTE_CEC_STAT0
#define m_WAKEUP_MUTE		(1 << 6)
#define m_ERR_FOLLOW_MUTE	(1 << 5)
#define m_ERR_INITIATOR_MUTE	(1 << 4)
#define m_ARB_LOST_MUTE		(1 << 3)
#define m_NACK_MUTE		(1 << 2)
#define m_EOM_MUTE		(1 << 1)
#define m_DONE_MUTE		(1 << 0)

//IH_MUTE_VP_STAT0
#define m_FIFOFULL_REP_MUTE	(1 << 7)
#define m_FIFOEMPTY_REP_MUTE	(1 << 6)
#define m_FIFOFULL_PACK_MUTE	(1 << 5)
#define m_FIFOEMPTY_PACK_MUTE	(1 << 4)
#define m_FIFOFULL_REMAP_MUTE	(1 << 3)
#define m_FIFOEMPTY_REMAP_MUTE	(1 << 2)
#define m_FIFOFULL_BYP_MUTE	(1 << 1)
#define m_FIFOEMPTY_BYP_MUTE	(1 << 0)

//IH_MUTE_I2CMPHY_STAT0
#define m_I2CMPHY_DONE_MUTE	(1 << 1)
#define m_I2CMPHY_ERR_MUTE	(1 << 0)


/*Video Sampler Registers*/
#define VIDEO_SAMPLER_BASE		0x0200
enum VIDEO_SAMPLER_REG {
	TX_INVID0 = VIDEO_SAMPLER_BASE,
	TX_INSTUFFING,
	TX_GYDATA0,
	TX_GYDATA1,
	TX_RCRDATA0,
	TX_RCRDATA1,
	TX_BCBDATA0,
	TX_BCBDATA1
};

//TX_INVID0
#define m_INTERNAL_DE_GEN	(1 << 7)
#define v_INTERNAL_DE_GEN(n)	(((n)&0x01) << 7)
enum VIDEO_MODE{
	VIDEO_RGB444_8BIT = 0x01,
	VIDEO_RGB444_10BIT = 0x03,
	VIDEO_RGB444_12BIT = 0x05,
	VIDEO_RGB444_16BIT = 0x07,
	VIDEO_YCBCR444_8BIT = 0x09,	//or YCbCr420
	VIDEO_YCBCR444_10BIT = 0x0b,	//or YCbCr420
	VIDEO_YCBCR444_12BIT = 0x0d,	//or YCbCr420
	VIDEO_YCBCR444_16BIT = 0x0f,	//or YCbCr420
	VIDEO_YCBCR422_12BIT = 0x12,
	VIDEO_YCBCR422_10BIT = 0x14,
	VIDEO_YCBCR422_8BIT = 0x16
};
#define m_VIDEO_MAPPING		(0x1f << 0)
#define v_VIDEO_MAPPING(n)	((n)&0x1f)

//TX_INSTUFFING
#define m_BCBDATA_STUFF		(1 << 2)
#define v_BCBDATA_STUFF(n)	(((n)&0x01) << 2)
#define m_RCRDATA_STUFF		(1 << 1)
#define v_RCRDATA_STUFF(n)	(((n)&0x01) << 1)
#define m_GYDATA_STUFF		(1 << 0)
#define v_GYDATA_STUFF(n)	(((n)&0x01) << 0)


/*Video Packetizer Registers*/
#define VIDEO_PACKETIZER_BASE		0x0800
enum VIDEO_PACKETIZER_REG {
	VP_STATUS = VIDEO_PACKETIZER_BASE,
	VP_PR_CD,
	VP_STUFF,
	VP_REMAP,
	VP_CONF,
	VP_MASK = 0x0807
};

//VP_STATUS
#define m_PACKING_PHASE		(0x0f << 0)

//VP_PR_CD
enum COLOR_DEPTH {
	COLOR_DEPTH_24BIT_DEFAULT = 0,
	COLOR_DEPTH_24BIT = 0x04,
	COLOR_DEPTH_30BIT,
	COLOR_DEPTH_36BIT,
	COLOR_DEPTH_48BIT
};
#define m_COLOR_DEPTH		(0x0f << 4)
#define v_COLOR_DEPTH(n)	(((n)&0x0f) << 4)
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
#define m_DESIRED_PR_FACTOR	(0x07 << 0)
#define v_DESIRED_PR_FACTOR(n)	(((n)&0x07) << 0)

//VP_STUFF
#define m_IDEFAULT_PHASE	(1 << 5)
#define v_IDEFAULT_PHASE(n)	(((n)&0x01) << 5)
#define m_IFIX_PP_TO_LAST	(1 << 4)
#define m_ICX_GOTO_P0_ST	(1 << 3)
enum {
	DIRECT_MODE = 0,
	STUFFING_MODE
};
#define m_YCC422_STUFFING	(1 << 2)
#define v_YCC422_STUFFING(n)	(((n)&0x01) << 1)
#define m_PR_STUFFING		(1 << 0)
#define v_PR_STUFFING(n)	(((n)&0x01) << 0)

//VP_REMAP
enum YCC422_SIZE{
	YCC422_16BIT = 0,
	YCC422_20BIT,
	YCC422_24BIT
};
#define m_YCC422_SIZE		(0x03 << 0)
#define v_YCC422_SIZE(n)	(((n)&0x03) << 0)

//VP_CONF
#define m_BYPASS_EN		(1 << 6)
#define v_BYPASS_EN(n)		(((n)&0x01) << 6)
#define m_PIXEL_PACK_EN		(1 << 5)
#define v_PIXEL_PACK_EN(n)	(((n)&0x01) << 5)
#define m_PIXEL_REPET_EN	(1 << 4)
#define v_PIXEL_REPET_EN(n)	(((n)&0x01) << 4)
#define m_YCC422_EN		(1 << 3)
#define v_YCC422_EN(n)		(((n)&0x01) << 3)
#define m_BYPASS_SEL		(1 << 2)
#define v_BYPASS_SEL(n)		(((n)&0x01) << 2)
enum {
	OUT_FROM_PIXEL_PACKING = 0,
	OUT_FROM_YCC422_REMAP,
	OUT_FROM_8BIT_BYPASS
};
#define m_OUTPUT_SEL		(0x03 << 0)
#define v_OUTPUT_SEL(n)		((n&0x03) << 0)

//VP_MASK
#define m_OINTFULL_REPET	(1 << 7)
#define m_OINTEMPTY_REPET	(1 << 6)
#define m_OINTFULL_PACK		(1 << 5)
#define m_OINTEMPTY_PACK	(1 << 4)
#define m_OINTFULL_REMAP	(1 << 3)
#define m_OINTEMPTY_REMAP	(1 << 2)
#define m_OINTFULL_BYPASS	(1 << 1)
#define m_OINTEMPTY_BYPASS	(1 << 0)


/*Frame Composer Registers*/
#define FRAME_COMPOSER_BASE		0x1000
enum FRAME_COMPOSER_REG {
	FC_INVIDCONF = FRAME_COMPOSER_BASE,
	FC_INHACTIV0,
	FC_INHACTIV1,
	FC_INHBLANK0,
	FC_INHBLANK1,
	FC_INVACTIV0,
	FC_INVACTIV1,
	FC_INVBLANK,
	FC_HSYNCINDELAY0,
	FC_HSYNCINDELAY1,
	FC_HSYNCINWIDTH0,
	FC_HSYNCINWIDTH1,
	FC_VSYNCINDELAY,
	FC_VSYNCINWIDTH,
	FC_INFREQ0,
	FC_INFREQ1,
	FC_INFREQ2,
	FC_CTRLDUR,
	FC_EXCTRLDUR,
	FC_EXCTRLSPAC,
	FC_CH0PREAM,
	FC_CH1PREAM,
	FC_CH2PREAM,
	FC_AVICONF3,
	FC_GCP,
	FC_AVICONF0,
	FC_AVICONF1,
	FC_AVICONF2,
	FC_AVIVID,
	FC_AVIETB0,
	FC_AVIETB1,
	FC_AVISBB0,
	FC_AVISBB1,
	FC_AVIELB0,
	FC_AVIELB1,
	FC_AVISRB0,
	FC_AVISRB1,
	FC_AUDICONF0,
	FC_AUDICONF1,
	FC_AUDICONF2,
	FC_AUDICONF3,
	FC_VSDIEEEID2,
	FC_VSDSIZE,
	FC_VSDIEEEID1,
	FC_VSDIEEEID0,
	FC_VSDPAYLOAD0 = 0x1032,	//0~23
	FC_SPDVENDORNAME0 = 0x104a,	//0~7
	FC_SPDPRODUCTNAME0 = 0x1052,	//0~15
	FC_SPDDEVICEINF	= 0x1062,
	FC_AUDSCONF,
	FC_AUDSSTAT,
	FC_AUDSV,
	FC_AUDSU,
	FC_AUDSCHNLS0,			//0~8
	FC_CTRLQHIGH = 0x1073,
	FC_CTRLQLOW,
	FC_ACP0,
	FC_ACP16 = 0x1082,		//16~1
	FC_ISCR1_0 = 0x1092,
	FC_ISCR1_16,			//16~1
	FC_ISCR2_15 = 0x10a3,		//15~0
	FC_DATAUTO0 = 0x10B3,
	FC_DATAUTO1,
	FC_DATAUTO2,
	FC_DATMAN,
	FC_DATAUTO3,
	FC_RDRB0,
	FC_RDRB1,
	FC_RDRB2,
	FC_RDRB3,
	FC_RDRB4,
	FC_RDRB5,
	FC_RDRB6,
	FC_RDRB7,
	FC_MASK0 = 0x10d2,
	FC_MASK1 = 0x10d6,
	FC_MASK2 = 0x10da,
	FC_PRCONF = 0x10e0,
	FC_SCRAMBLER_CTRL,
	FC_GMD_STAT,
	FC_GMD_EN,
	FC_GMD_UP,
	FC_GMD_CONF,
	FC_GMD_HB,
	FC_GMD_PB0,			//0~27
	FC_DBGFORCE = 0x1200,
	FC_DBGAUD0CH0,			//aud0~aud2 ch0
	FC_DBGAUD0CH1 = 0x1204,		//aud0~aud2 ch1
	FC_DBGAUD0CH2 = 0x1207,		//aud0~aud2 ch2
	FC_DBGAUD0CH3 = 0x120a,		//aud0~aud2 ch3
	FC_DBGAUD0CH4 = 0x120d,		//aud0~aud2 ch4	
	FC_DBGAUD0CH5 = 0x1210,		//aud0~aud2 ch5
	FC_DBGAUD0CH6 = 0x1213,		//aud0~aud2 ch6
	FC_DBGAUD0CH7 = 0x1216,		//aud0~aud2 ch7
	FC_DBGTMDS0 = 0x1219,
	FC_DBGTMDS1,
	FC_DBGTMDS2		
};

/*HDMI Source PHY Registers*/
#define HDMI_SOURCE_PHY_BASE		0x3000
enum HDMI_SOURCE_PHY_REG {
	PHY_CONF0 = HDMI_SOURCE_PHY_BASE,
	PHY_TST0,
	PHY_TST1,
	PHY_TST2,
	PHY_STAT0,
	PHY_INT0,
	PHY_MASK0,
	PHY_POL0,
	PHY_PCLFREQ0,
	PHY_PCLFREQ1,
	PHY_PLLCFGFREQ0,
	PHY_PLLCFGFREQ1,
	PHY_PLLCFGFREQ2
};

//PHY_CONF0
#define m_POWER_DOWN_EN		(1 << 7)		//enable depend on PHY_GEN2=0 and PHY_EXTERNAL=0
#define v_POWER_DOWN_EN(n)	(((n)&0x01) << 7)
#define m_TMDS_EN		(1 << 6)		//enable depend on PHY_GEN2=0 and PHY_EXTERNAL=0
#define v_TMDS_EN(n)		(((n)&0x01) << 6)
#define	m_SVSRET_SIG		(1 << 5)		//depend on PHY_MHL_COMB0=1
#define v_SVSRET_SIG(n)		(((n)&0x01) << 5)
#define m_PDDQ_SIG		(1 << 4)		//depend on PHY_GEN2=1 or PHY_EXTERNAL=1
#define v_PDDQ_SIG(n)		(((n)&0x01) << 4)
#define m_TXPWRON_SIG		(1 << 3)		//depend on PHY_GEN2=1 or PHY_EXTERNAL=1
#define v_TXPWRON_SIG(n)	(((n)&0x01) << 3)
#define m_ENHPD_RXSENSE_SIG	(1 << 2)		//depend on PHY_GEN2=1 or PHY_EXTERNAL=1
#define v_ENHPD_RXSENSE_SIG	(((n)&0x01) << 2)
#define m_SEL_DATAEN_POL	(1 << 1)
#define v_SEL_DATAEN_POL(n)	(((n)&0x01) << 1)
#define m_SEL_INTERFACE		(1 << 0)
#define v_SEL_INTERFACE(n)	(((n)&0x01) << 0)

//PHY_TST0
#define m_TEST_CLR_SIG		(1 << 5)
#define m_TEST_EN_SIG		(1 << 4)
#define m_TEST_CLK_SIG		(1 << 0)

//PHY_STAT0/PHY_INI0/PHY_MASK/PHY_POL0
#define m_PHY_RX_SENSE3		(1 << 7)
#define v_PHY_TX_SENSE3(n)	(((n)&0x01) << 7)
#define m_PHY_RX_SENSE2		(1 << 6)
#define v_PHY_TX_SENSE2(n)      (((n)&0x01) << 6)
#define m_PHY_RX_SENSE1		(1 << 5)
#define v_PHY_TX_SENSE1(n)      (((n)&0x01) << 5)
#define m_PHY_RX_SENSE0		(1 << 4)
#define v_PHY_TX_SENSE0(n)      (((n)&0x01) << 4)
#define m_PHY_HPD		(1 << 1)
#define v_PHY_HPD		(((n)&0x01) << 1)
#define m_PHY_LOCK		(1 << 0)
#define v_PHY_LOCK(n)		(((n)&0x01) << 0)


/*I2C Master PHY Registers*/
#define I2C_MASTER_PHY_BASE		0x3020
enum I2C_MASTER_PHY_REG {
	PHY_I2CM_SLAVE = I2C_MASTER_PHY_BASE,
	PHY_I2CM_ADDRESS,
	PHY_I2CM_DATAO_1,
	PHY_I2CM_DATAO_0,
	PHY_I2CM_DATAI_1,
	PHY_I2CM_DATAI_0,
	PHY_I2CM_OPERATION,
	PHY_I2CM_INT,
	PHY_I2CM_CTLINT,
	PHY_I2CM_DIV,
	PHY_I2CM_SOFTRSTZ,
	PHY_I2CM_SS_SCL_HCNT_1_ADDR,
	PHY_I2CM_SS_SCL_HCNT_0_ADDR,
	PHY_I2CM_SS_SCL_LCNT_1_ADDR,
	PHY_I2CM_SS_SCL_LCNT_0_ADDR,
	PHY_I2CM_FS_SCL_HCNT_1_ADDR,
	PHY_I2CM_FS_SCL_HCNT_0_ADDR,
	PHY_I2CM_FS_SCL_LCNT_1_ADDR,
	PHY_I2CM_FS_SCL_LCNT_0_ADDR,
	I2CM_PHY_SDA_HOLD
};

//PHY_I2CM_OPERATION
#define v_I2CM_WRITE		(1 << 4)
#define v_I2CM_READ		(1 << 0)

//PHY_I2CM_INT
#define m_I2CM_DONE_INT_POL	(1 << 3)
#define v_I2CM_DONE_INT_POL(n)	(((n)&0x01) << 3)
#define m_I2CM_DONE_MASK	(1 << 2)
#define v_I2CM_DONE_MASK(n)	(((n)&0x01) << 2)
#define m_I2CM_DONE_INT		(1 << 1)
#define m_I2CM_DONE_STATUS	(1 << 0)

//PHY_I2CM_CTLINT
#define m_I2CM_NACK_POL		(1 << 7)
#define v_I2CM_NACK_POL(n)	(((n)&0x01) << 7)
#define m_I2CM_NACK_MASK	(1 << 6)
#define v_I2CM_NACK_MASK(n)	(((n)&0x01) << 6)
#define m_I2CM_NACK_INT		(1 << 5)
#define m_I2CM_NACK_STATUS	(1 << 4)
#define m_I2CM_ARB_POL		(1 << 3)
#define v_I2CM_ARB_POL(n)	(((n)&0x01) << 3)
#define m_I2CM_ARB_MASK		(1 << 2)
#define v_I2CM_ARB_MASK(n)	(((n)&0x01) << 2)
#define m_I2CM_ARB_INT		(1 << 1)
#define m_I2CM_ARB_STATUS	(1 << 0)

//PHY_I2CM_DIV
enum {
	STANDARD_MODE = 0,
	FAST_MODE
};
#define m_I2CM_FAST_STD_MODE	(1 << 3)
#define v_I2CM_FAST_STD_MODE	(((n)&0x01) << 3)

//PHY_I2CM_SOFTRSTZ
#define m_I2CM_SOFTRST		(1 << 0)
#define v_I2CM_SOFTRST		(((n)&0x01) << 0)


/*Audio Sampler Registers*/
#define AUDIO_SAMPLER_BASE		0x3100
enum AUDIO_SAMPLER_REG {
	AUD_CONF0 = AUDIO_SAMPLER_BASE,
	AUD_CONF1,
	AUD_INT,
	AUD_CONF2,
	AUD_INT1,
	AUD_N1 = 0x3200,
	AUD_N2,
	AUD_N3,
	AUD_CTS1,
	AUD_CTS2,
	AUD_CTS3,
	AUD_INPUTCLKFS,
	AUD_SPDIF0 = 0x3300,
	AUD_SPDIF1,
	AUD_SPDIFINT,
	AUD_SPDIFINT1
};

//AUD_CONF0
#define m_SW_AUD_FIFO_RST	(1 << 7)
#define v_SW_AUD_FIFO_RST(n)	(((n)&0x01) << 7)
enum {
	AUDIO_SPDIF_GPA = 0,
	AUDIO_I2S
};
#define m_I2S_SEL		(1 << 5)
#define v_I2S_SEL(n)		(((n)&0x01) << 5)
#define m_I2S_IN_EN		(0x0f << 0)
#define v_I2S_IN_EN(v)		(((n)&0x0f) << 0)

//AUD_CONF1
enum I2S_MODE {
	I2S_STANDARD_MODE = 0,
	I2S_RIGHT_JUSTIFIED_MODE,
	I2S_LEFT_JUSTIFIED_MODE,
	I2S_BURST_1_MODE,
	I2S_BURST_2_MODE
};
#define m_I2S_MODE		(0x07 << 5)
#define v_I2S_MODE(n)		(((n)&0x07) << 5)
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
#define v_I2S_WIDTH(n)		(((n)&0x1f) << 0)

//AUD_INT/AUD_SPDIFINT
#define m_FIFO_EMPTY_MASK	(1 << 3)
#define v_FIFO_EMPTY_MASK(n)	(((n)&0x01) << 3)
#define m_FIFO_FULL_MASK	(1 << 2)
#define v_FIFO_FULL_MASK(n)	(((n)&0x01) << 2)

//AUD_CONF2
#define m_NLPCM_EN		(1 << 1)
#define v_NLPCM_EN(n)		(((n)&0x01) << 1)
#define m_HBR_EN		(1 << 0)
#define v_HBR_EN(n)		(((n)&0x01) << 0)

//AUD_INT1/AUD_SPDIFINT1
#define m_FIFO_OVERRUN_MASK	(1 << 4)
#define v_FIFO_OVERRUN_MASK(n)	(((n)&0x01) << 4)

//AUD_N3
#define m_NCTS_ATOMIC_WR	(1 << 7)
#define v_NCTS_ATOMIC_WR(n)	(((n)&0x01) << 7)
#define m_AUD_N3		(0x0f << 0)
#define v_AUD_N3(n)		(((n)&0x0f) << 0)

//AUD_CTS3
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
#define v_N_SHIFT(n)		(((n)&0x07) << 5)
#define m_CTS_MANUAL		(1 << 4)
#define v_CTS_MANUAL(n)		(((n)&0x01) << 4)
#define m_AUD_CTS3		(0x0f << 0)
#define v_AUD_CTS3(n)		(((n)&0x0f) << 0)

//AUD_INPUTCLKFS
enum {
	FS_128 = 0,
	FS_256,
	FS_512,
	FS_64 = 4,
	FS_OTHERS_128
};
#define m_LFS_FACTOR		(0x07 << 0)
#define v_LFS_FACTOR(n)		(((n)&0x07) << 0)

//AUD_SPDIF0
#define m_SW_SAUD_FIFO_RST	(1 << 7)
#define v_SW_SAUD_FIFO_RST	(((n)&0x01) << 7)

//AUD_SPDIF1
#define m_SET_NLPCM		(1 << 7)
#define v_SET_NLPCM		(((n)&0x01) << 7)
#define m_SPDIF_HBR_MODE	(1 << 6)
#define v_SPDIF_HBR_MODE(n)	(((n)&0x01) << 6)
#define m_SPDIF_WIDTH		(0x1f << 0)
#define v_SPDIF_WIDTH(n)	(((n)&0x1f) << 0)


/*Generic Parallel Audio Interface Registers*/
#define GP_AUDIO_INTERFACE_BASE		0x3500
enum GP_AUDIO_INTERFACE_REG {
	GP_CONF0 = GP_AUDIO_INTERFACE_BASE,
	GP_CONF1,
	GP_CONF2,
	GP_MASK = 0x3506
};

/*Audio DMA Registers*/
#define AUDIO_DMA_BASE			0x3600
enum AUDIO_DMA_REG {
	AHB_DMA_CONF0 = AUDIO_DMA_BASE,
	AHB_DMA_START,
	AHB_DMA_STOP,
	AHB_DMA_THRSLD,
	AHB_DMA_STRADDR_SET0_0,		//0~3
	AHB_DMA_STPADDR_SET0_0 = 0x3608,//0~3
	AHB_DMA_BSTADDR0 = 0x360c,	//0~3
	AHB_DMA_MBLENGTH0 = 0x3610,	//0~3,
	AHB_DMA_MASK = 0x3614,
	AHB_DMA_CONF1 = 0x3616,
	AHB_DMA_BUFFMASK = 0x3619,
	AHB_DMA_MASK1 = 0x361b,
	AHB_DMA_STATUS,
	AHB_DMA_CONF2,
	AHB_DMA_STRADDR_SET1_0 = 0x3620,//0~3
	AHB_DMA_STPADDR_SET1_0 = 0x3624 //0~3
};

/*Main Controller Registers*/
#define MAIN_CONTROLLER_BASE		0X4000
enum MAIN_CONTROLLER_REG {
	MC_CLKDIS = 0x4001,
	MC_SWRSTZREQ,
	MC_OPCTRL,
	MC_FLOWCTRL,
	MC_PHYRSTZ,
	MC_LOCKONCLOCK,
	MC_HEACPHY_RST,
	MC_LOCKONCLOCK2,
	MC_SWRSTZREQ_2
};

//MC_CLKDIS
#define m_HDCPCLK_DISABLE	(1 << 6)
#define v_HDCPCLK_DISABLE(n)	(((n)&0x01) << 6)
#define m_CECCLK_DISABLE	(1 << 5)
#define v_CECCLK_DISABLE(n)	(((n)&0x01) << 5)
#define m_CSCCLK_DISABLE	(1 << 4)
#define v_CSCCLK_DISABLE(n)	(((n)&0x01) << 4)
#define m_AUDCLK_DISABLE        (1 << 3)
#define v_AUDCLK_DISABLE(n)     (((n)&0x01) << 3)
#define m_PREPCLK_DISABLE	(1 << 2)
#define v_PREPCLK_DISABLE(n)	(((n)&0x01) << 2)
#define m_TMDSCLK_DISABLE	(1 << 1)
#define v_TMDSCLK_DISABLE(n)	(((n)&0x01) << 1)
#define m_PIXELCLK_DISABLE	(1 << 0)
#define v_PIXELCLK_DISABLE(n)	(((n)&0x01) << 0)

//MC_SWRSTZREQ
#define m_IGPA_SWRST		(1 << 7)
#define v_IGPA_SWRST(n)		(((n)&0x01) << 7)
#define m_CEC_SWRST		(1 << 6)
#define v_CEC_SWRST(n)		(((n)&0x01) << 6)
#define m_ISPDIF_SWRST          (1 << 4)
#define v_ISPDIF_SWRST(n)       (((n)&0x01) << 4)
#define m_II2S_SWRST            (1 << 3)
#define v_II2S_SWRST(n)         (((n)&0x01) << 3)
#define m_PREP_SWRST            (1 << 2)
#define v_PREP_SWRST(n)         (((n)&0x01) << 2)
#define m_TMDS_SWRST          	(1 << 1)
#define v_TMDS_SWRST(n)       	(((n)&0x01) << 1)
#define m_PIXEL_SWRST           (1 << 0)
#define v_PIXEL_SWRST(n)        (((n)&0x01) << 0)

//MC_OPCTRL
#define m_HDCP_BLOCK_BYP	(1 << 0)
#define v_HDCP_BLOCK_BYP(n)	(((n)&0x01) << 0)

//MC_FLOWCTRL
#define m_FEED_THROUGH_OFF      (1 << 0)
#define v_FEED_THROUGH_OFF(n)   (((n)&0x01) << 0)

//MC_PHYRSTZ
#define m_PHY_RSTZ		(1 << 0)
#define v_PHY_RSTZ(n)		(((n)&0x01) << 0)

//MC_LOCKONCLOCK
#define m_IGPACLK_ON		(1 << 7)
#define v_IGPACLK_ON(n)		(((n)&0x01) << 7)
#define m_PCLK_ON		(1 << 6)
#define v_PCLK_ON(n)		(((n)&0x01) << 6)
#define m_TMDSCLK_ON            (1 << 5)
#define v_TMDSCLK_ON(n)         (((n)&0x01) << 5)
#define m_PREPCLK_ON            (1 << 4)
#define v_PREPCLK_ON(n)         (((n)&0x01) << 4)
#define m_I2SCLK_ON            	(1 << 3)
#define v_I2SCLK_ON(n)         	(((n)&0x01) << 3)
#define m_SPDIFCLK_ON           (1 << 2)
#define v_SPDIFCLK_ON(n)       	(((n)&0x01) << 2)
#define m_CECCLK_ON            	(1 << 0)
#define v_CECCLK_ON(n)         	(((n)&0x01) << 0)

//MC_HEACPHY_RST
#define m_HEAC_PHY_RST		(1 << 0)
#define v_HEAC_PHY_RST(n)	(((n)&0x01) << 0)

//MC_LOCKONCLOCK_2
#define m_AHB_AUD_DMA_CLK       (1 << 0)
#define v_AHB_AUD_DMA_CLK(n)    (((n)&0x01) << 0)

//MC_SWRSTZREQ_2
#define m_AHB_AUD_DMA_RST       (1 << 7)
#define v_AHB_AUD_DMA_RST(n)    (((n)&0x01) << 7)


/*Color Space Converter Registers*/
#define COLOR_SPACE_CONVERTER_BASE	0x4100
enum COLOR_SPACE_CONVERTER_REG {
	CSC_CFG = COLOR_SPACE_CONVERTER_BASE,
	CSC_SCALE,
	CSC_COEF_A1_MSB,
	CSC_COEF_A1_LSB,
	CSC_COEF_A2_MSB,
	CSC_COEF_A2_LSB,
	CSC_COEF_A3_MSB,
	CSC_COEF_A3_LSB,
	CSC_COEF_A4_MSB,
	CSC_COEF_A4_LSB,
	CSC_COEF_B1_MSB,
	CSC_COEF_B1_LSB,
	CSC_COEF_B2_MSB,
	CSC_COEF_B2_LSB,
	CSC_COEF_B3_MSB,
	CSC_COEF_B3_LSB,
	CSC_COEF_B4_MSB,
	CSC_COEF_B4_LSB,
	CSC_COEF_C1_MSB,
	CSC_COEF_C1_LSB,
	CSC_COEF_C2_MSB,
	CSC_COEF_C2_LSB,
	CSC_COEF_C3_MSB,
	CSC_COEF_C3_LSB,
	CSC_COEF_C4_MSB,
	CSC_COEF_C4_LSB,
	CSC_SPARE_1,
	CSC_SPARE_2
};

//CSC_CFG
#define m_INTMODE		(0x03 << 4)
#define v_INTMODE(n)		(((n)&0x03) << 4)
#define m_DECMODE		(0x03 << 0)
#define v_DECMODE(n)		(((n)&0x03) << 0)

//CSC_SCALE
#define m_CSC_COLOR_DEPTH	(0x0f << 4)
#define v_CSC_COLOR_DEPTH	(((n)&0x0f) >> 4)
#define m_CSC_SCALE       	(0x03 << 0)
#define v_CSC_SCALE       	(((n)&0x03) >> 0)



/*HDCP Encryption Engine Registers*/
#define HDCP_ENCRYPTION_ENGINE_BASE	0x5000
enum HDCP_ENCRYPTION_REG {
	A_HDCPCFG0 = HDCP_ENCRYPTION_ENGINE_BASE,
	A_HDCPCFG1,
	A_HDCPOBS0,
	A_HDCPOBS1,
	A_HDCPOBS2,
	A_HDCPOBS3,
	A_APIINTCLR,
	A_APIINTSTAT,
	A_APIINTMSK,
	A_VIDPOLCFG,
	A_OESSWCFG,
	A_COREVERLSB = 0x5014,
	A_COREVERMSB,
	A_KSVMEMCTRL,
	HDCP_BSTATUS_0 = 0x5020,
	HDCP_BSTATUS_1,
	HDCP_M0_0,
	HDCP_M0_1,
	HDCP_M0_2,
	HDCP_M0_3,
	HDCP_M0_4,
	HDCP_M0_5,
	HDCP_M0_6,
	HDCP_M0_7,
	HDCP_KSV,			//0~634
	HDCP_VH = 0x52a5,		//0~19
	HDCP_REVOC_SIZE_0 = 0x52b9,
	HDCP_REVOC_SIZE_1,
	HDCP_REVOC_LIST,		//0~5059
};

/*HDCP BKSV Registers*/
#define HDCP_BKSV_BASE			0x7800
enum HDCP_BKSV_REG {
	HDCPREG_BKSV0 = HDCP_BKSV_BASE,
	HDCPREG_BKSV1,
	HDCPREG_BKSV2,
	HDCPREG_BKSV3,
	HDCPREG_BKSV4
};

/*HDCP AN Registers*/
#define HDCP_AN_BASE			0x7805
enum HDCP_AN_REG {
	HDCPREG_ANCONF = HDCP_AN_BASE,
	HDCPREG_AN0,
	HDCPREG_AN1,
	HDCPREG_AN2,
	HDCPREG_AN3,
	HDCPREG_AN4,
	HDCPREG_AN5,
	HDCPREG_AN6,
	HDCPREG_AN7
};

/*Encrypted DPK Embedded Storage Registers*/
#define ENCRYPTED_DPK_EMBEDDED_BASE	0x780e
enum ENCRYPTED_DPK_EMBEDDED_REG {
	HDCPREG_RMCTL = ENCRYPTED_DPK_EMBEDDED_BASE,
	HDCPREG_RMSTS,
	HDCPREG_SEED0,
	HDCPREG_SEED1,
	HDCPREG_DPK0,
	HDCPREG_DPK1,
	HDCPREG_DPK2,
	HDCPREG_DPK3,
	HDCPREG_DPK4,
	HDCPREG_DPK5,
	HDCPREG_DPK6
};

/*CEC Engine Registers*/
#define CEC_ENGINE_BASE			0x7d00
enum CEC_ENGINE_REG {
	CEC_CTRL = CEC_ENGINE_BASE,
	CEC_MASK = 0x7d02,
	CEC_ADDR_L = 0x7d05,
	CEC_ADDR_H,
	CEC_TX_CNT,
	CEC_RX_CNT,
	CEC_TX_DATA0 = 0x7d10,		//txdata0~txdata15
	CEC_RX_DATA0 = 0x7d20,		//rxdata0~rxdata15
	CEC_LOCK = 0x7d30,
	CEC_WKUPCTRL
};

/*I2C Master Registers*/
#define I2C_MASTER_BASE			0x7e00
enum I2C_MASTER_REG {
	I2CM_SLAVE = I2C_MASTER_BASE,
	I2CM_ADDRESS,
	I2CM_DATAO,
	I2CM_DATAI,
	I2CM_OPERATION,
	I2CM_INT,
	I2CM_CTLINT,
	I2CM_DIV,
	I2CM_SEGADDR,
	I2CM_SOFTRSTZ,
	I2CM_SEGPTR,
	I2CM_SS_SCL_HCNT_1_ADDR,
	I2CM_SS_SCL_HCNT_0_ADDR,
	I2CM_SS_SCL_LCNT_1_ADDR,
	I2CM_SS_SCL_LCNT_0_ADDR,
	I2CM_FS_SCL_HCNT_1_ADDR,
	I2CM_FS_SCL_HCNT_0_ADDR,
	I2CM_FS_SCL_LCNT_1_ADDR,
	I2CM_FS_SCL_LCNT_0_ADDR,
	I2CM_SDA_HOLD,
	I2CM_SCDC_READ_UPDATE,
	I2CM_READ_BUFF0 = 0x7e20,		//buff0~buff7
	I2CM_SCDC_UPDATE0 = 0x7e30,
	I2CM_SCDC_UPDATE1
};   


struct rk3288_hdmi_device {
	int			irq;
	void __iomem  		*regbase;
	int			regbase_phy;
	int			regsize_phy;
	int 			lcdc_id;
	struct device 		*dev;
	struct clk		*hclk;				//HDMI AHP clk
	struct hdmi 		driver;
        struct dentry           *debugfs_dir;
};


static inline int hdmi_readl(struct rk3288_hdmi_device *hdmi_dev, u16 offset, u32 *val)
{
        int ret = 0;
	*val = readl_relaxed(hdmi_dev->regbase + (offset) * 0x04);
        return ret;
}

static inline int hdmi_writel(struct rk3288_hdmi_device *hdmi_dev, u16 offset, u32 val)
{
        int ret = 0;
        writel_relaxed(val, hdmi_dev->regbase + (offset) * 0x04);
        return ret;
}

static inline int hdmi_msk_reg(struct rk3288_hdmi_device *hdmi_dev, u16 offset, u32 msk, u32 val)
{
        int ret = 0;
        u32 temp;
        temp = readl_relaxed(hdmi_dev->regbase + (offset) * 0x04) & (0xFF - (msk));
        writel_relaxed(temp | ( (val) & (msk) ),  hdmi_dev->regbase + (offset) * 0x04);
        return ret;
}

int rk3288_hdmi_initial(struct hdmi *hdmi_drv);


#endif
