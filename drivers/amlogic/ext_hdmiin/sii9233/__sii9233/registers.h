//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef __REGISTERS_H__
#define __REGISTERS_H__



//------------------------------------------------------------------------------
// NOTE: Register addresses are 16 bit values with page and offset combined.
//
// Examples:  0x005 = page 0, offset 0x05
//            0x1B6 = page 1, offset 0xB6
//------------------------------------------------------------------------------



//------------------------------------------------------------------------------
// Registers in Page 0
//------------------------------------------------------------------------------

//SYS Device ID Low uint8_t Register
#define REG__IDL_RX			0x02
#define REG__IDH_RX			0X03
//SYS Device Revision Register
#define DEV_REV_RX			0x04
#define VAL__REV_1_2		0x03

// Software Reset Register
#define REG__SRST           0x005
#define BIT__SWRST_AUTO     0x10    // Auto SW Reset
#define BIT__ACRRST         0x04
#define BIT__AACRST         0x20
#define BIT__HDCPRST        0x08
#define BIT__FIFORST        0x02
#define BIT__SWRST        	0x01


// System Status Register
#define REG__STATE          0x006
#define BIT__PWR5V          0x08
#define BIT__SCDT           0x01
#define BIT__PCLK_STABLE	0x10
#define BIT__PWD_STAT		0x20

// Software Reset Register #2
#define REG__SRST2              0x007
#define BIT__DCFIFO_RST         0x80
#define BIT__AUDIO_FIFO_AUTO    0x40

// System Control Register #1
#define REG__SYS_CTRL1      0x008
#define BIT__OCKINV         0x02    // ODCK invert
#define BIT__PD             0x01    // Power down mode


// Port Switch Register
#define REG__PORT_SWTCH      0x009
//#define BIT__DDCDLY_EN      0x80
#define BIT__DDC3_EN        0x80
#define BIT__DDC2_EN        0x40
#define BIT__DDC1_EN        0x20
#define BIT__DDC0_EN        0x10
#define VAL__DDC_DISABLED 0x00

// Port Switch Register	2
#define REG__PORT_SWTCH2      0x00A
#define VAL__PORT3_EN         0x03
#define VAL__PORT2_EN         0x02
#define VAL__PORT1_EN         0x01
#define VAL__PORT0_EN         0x00
#define MSK__PORT_EN         0x03

// System Software Reset 2 Register
#define REG__C0_SRST2		0x00B
#define BIT__CEC_SRST		0x40

// System Pclk Stop Register
#define REG__SYS_PSTOP		0x00F
#define BIT__PSTOP_EN		0x01
#define MSK__PCLK_MAX		0xFE

//Hot plug Control Register
#define REG__HP_CTRL      0x010
#define VAL__HP_PORT0	  0x01
#define VAL__HP_PORT1	  0x04
#define VAL__HP_PORT2	  0x10
#define VAL__HP_PORT3	  0x40
#define VAL__HP_PORT_ALL  0x55
#define VAL__HP_PORT_NONE 0x00

//CEC configuration
#define REG__CEC_CONFIG	  0x011
#define VAL__CEC_PWR_ON	  0x20
#define VAL__CEC_INTR_EN	  0x01


//Programmable slave Address 4 mapping to page #A
#define REG__SLAVE_ADDR_XVYCC      0x015
#define REG__SLAVE_ADDR_EDID  0x019
#define REG__SLAVE_ADDR_CEC	  0x018


// HDCP Debug Register
#define REG__HDCPCTRL       0x031
#define BIT__CLEAR_RI       0x80

// Video Input H Resolution
#define REG__VID_H_RES1				0x03A
#define REG__VID_H_RES2				0x03B
#define MSK__VID_H_RES_BIT8_12		0x1F

// Video Input V Resolution
#define REG__VID_V_RES1				0x03C
#define REG__VID_V_RES2				0x03D
#define MSK__VID_V_RES_BIT8_10		0x07

// Video Input DE PIXEL
#define REG__VID_DE_PIXEL1			0x04E
#define REG__VID_DE_PIXEL2			0x04F
#define MSK__VID_DE_PIXEL_BIT8_11	0x0F

// Video DE Lines
#define REG__VID_DE_LINE1			0x050
#define REG__VID_DE_LINE2			0x051
#define MSK__VID_DE_LINE_BIT8_10	0x07

// Video V Sync to Active Video Lines
#define REG__VID_VS_AVT				0x052
#define MSK__VID_VS_AVT_BIT0_5		0x3F

// Video V Front Porch
#define REG__VID_V_FP				0x053
#define MSK__VID_V_FP_BIT0_5		0x3F

// Video H Front Porch
#define REG__VID_H_FP1				0x059
#define REG__VID_H_FP2				0x05A
#define MSK__VID_H_FP_BIT8_9		0x03

// Video H Sync Width
#define REG__VID_HS_WIDTH1			0x05B
#define REG__VID_HS_WIDTH2			0x05C
#define MSK__VID_HS_WIDTH_BIT8_9	0x03

// Video Control Register
#define REG__VID_CTRL       0x048
#define BIT__IVS            0x80    // Invert VSYNC
#define BIT__IHS            0x40    // Invert HSYNC

// Video Mode #2 Register
#define REG__VID_MODE2          0x049
#define MSK__DITHER_MODE        0xC0    // Output color depth
#define VAL__DITHER_8BITS       0x00    // 8 bit  color output
#define VAL__DITHER_10BITS      0x40    // 10 bit color output
#define VAL__DITHER_12BITS      0x80    // 12 bit color output
#define BIT__EVENPOL            0x20    // EVNODD pin invert

// Auto Output Format Register
#define REG__VID_AOF                        0x05F
#define VAL__RGB                            0x00    // RGB 4:4:4
#define VAL__YC444                          0x80    // YCbCr 4:4:4
#define VAL__YC422_8BIT                     0xC0    // YCbCr 4:2:2 8  bit
#define VAL__YC422_10BIT                    0xC8    // YCbCr 4:2:2 10 bit
#define VAL__YC422_MUX_8BIT                 0xE0    // YCbCr 4:2:2 8  bit multiplexed
#define VAL__YC422_MUX_10BIT                0xE8    // YCbCr 4:2:2 10 bit multiplexed
#define VAL__YC422_MUX_8BIT_EMBED_SYNC      0xF0    // YCbCr 4:2:2 8  bit multiplexed with embedded sync
#define VAL__YC422_MUX_10BIT_EMBEDD_SYNC    0xF8    // YCbCr 4:2:2 10 bit multiplexed with embedded sync
#define BIT__MUXYC                          0x20    // Set for any YC multiplexed mode

// Deep Color Status Register
#define REG__DC_STAT        0x061
#define MSK__PIXEL_DEPTH    0x03
#define VAL__DEPTH_8BPP     0x00
#define VAL__DEPTH_10BPP    0x01
#define VAL__DEPTH_12BPP    0x02

//Video Channel PCLK Count Base Register
#define REG__VIDA_XPCLK_BASE	0x0069

//XCLK to PCLK update Register
#define REG__VIDA_XPCNT_EN	0x006A
#define BIT__VIDA_XPCNT_EN	0x01

//Pixel Clock Timing Low Register
#define REG__VIDA_XPCNT0	0x006E

//Pixel Clock Timing High Register
#define REG__VIDA_XPCNT1	0x006F
#define MSK__VIDA_XPCNT1_BIT8_11	0x0F

// Interrupt State Register
#define REG__INTR_STATE     0x070
#define BIT__INTR           0x01
#define BIT__INTR_GROUP0	0x02
#define BIT__INTR_GROUP1	0x04

// Interrupt Status #1 Register
#define REG__INTR1          0x071
#define BIT__AUDIO_READY	0x04
#define BIT__PWRSTAT_CHANGE 0x08

// Interrupt Status #2 Register
#define REG__INTR2          0x072
#define BIT__HDMIMODE       0x80
#define BIT__CKDT_CHG       0x10
#define BIT__SCDT_CHG       0x08	 
#define BIT__GOTAUD         0x02
#define BIT__GOTCTS         0x04
#define BIT__VCLK_CHG		0x01

//////////////////////////////////////////
// Interrupt Status #3 Register			//
//////////////////////////////////////////
#define REG__INTR3              0x073
#define REG__INTR3_NEW_INFOFR   0x073
/* bit defenitions */
#define BIT__NEW_AVI_INF       	0x01
#define BIT__NEW_SPD_INF       	0x02
#define BIT__NEW_AUD_INF       	0x04
#define BIT__NEW_MPEG_INF       0x08
#define BIT__NEW_UNREQ_INF      0x10
#define BIT__NEW_CP_INF       	0x80


// Interrupt Status #4 Register
#define REG__INTR4          0x074
#define BIT__HDCPERR        0x40
#define BIT__CTS_DROP       0x08
#define BIT__CTS_REUSE      0x04
#define BIT__FIFO_OVER      0x02
#define BIT__FIFO_UNDER     0x01
#define BIT__NO_AVI_INF    	0x10
#define MSK__CTS_ERROR      (BIT__CTS_DROP  | BIT__CTS_REUSE)
#define MSK__FIFO_ERROR     (BIT__FIFO_OVER | BIT__FIFO_UNDER)

// Interrupt Unmask Registers
#define REG__INTR1_UNMASK   0x075

// Interrupt Unmask Registers
#define REG__INTR3_UNMASK   0x077

// Interrupt Info Frame Control

#define REG__INTR_IF_CTRL	0x07A

#define BIT__NEW_AVI_CTRL_INF	0x01
#define BIT__NEW_AUD_CTRL_INF	0x04
#define BIT__NEW_ACP_CTRL_INF	0x20
#define BIT__NEW_GMT_CTRL_INF	0x40


// Interrupt Status #5 Register
#define REG__INTR5              0x07B
#define BIT__FNCHG              0x80
#define BIT__AACDONE            0x40
#define BIT__AULERR             0x20
#define BIT__VRCHG              0x10
#define BIT__HRCHG              0x08
#define BIT__FSCHG              0x01
#define MSK__AUDIO_INTR         (BIT__FNCHG | BIT__AACDONE | BIT__AULERR | BIT__FSCHG)
// Interrupt Status #6 Register
#define REG__INTR6              0x07C
#define BIT__CABLE_UNPLUG       0x01
#define BIT__NEW_ACP_PKT		0x04

// Interrupt Unmask Registers
#define REG__INTR5_UNMASK       0x07D
#define REG__INTR6_UNMASK       0x07E

// TMDS Analog Control #2 Register
#define REG__TMDS_CCTRL2        0x081
#define BIT__OFFSET_COMP_EN		0x20
#define MSK__DC_CTL             0x0F
#define VAL__DC_CTL_8BPP_1X     0x00
#define VAL__DC_CTL_8BPP_2X     0x02
#define VAL__DC_CTL_10BPP_1X    0x04
#define VAL__DC_CTL_12BPP_1X    0x05
#define VAL__DC_CTL_10BPP_2X    0x06
#define VAL__DC_CTL_12BPP_2X    0x07

// TMDS Termination Control Register
#define REG__TMDS_TERMCTRL              0x082
//#define VAL__TERM_SEL1_NORMAL           0x00
//#define VAL__TERM_SEL1_UNTERMINATED     0x60
//#define VAL__TERM_SEL1_UNPLUGTERMINATED     0x40
#define VAL__TERM_CNTL_60               0x00
#define VAL__TERM_CNTL_55               0x04
#define VAL__TERM_CNTL_50               0x08
#define VAL__TERM_CNTL_45               0x0C
#define VAL__TERM_CNTL_40               0x10
//#define VAL__TERM_SEL0_NORMAL           0x00
#define VAL__TERM_SEL0_UNTERMINATED     0x03
//#define VAL__TERM_SEL0_UNPLUGTERMINATED     0x02


// TMDS Termination Control Register
#define REG__TMDS_TERMCTRL0              			0x082
#define VAL__TERM_SEL0_NORMAL           			0xFC
#define VAL__TERM_SEL1_NORMAL          			 	0xF3
#define VAL__TERM_SEL2_NORMAL    				    0xCF
#define VAL__TERM_SEL3_NORMAL           			0x3F
#define VAL__TERM_SEL0_UNPLUGTERMINATED           	0xFE //1111 1110
#define VAL__TERM_SEL1_UNPLUGTERMINATED           	0xFB //1111 1011
#define VAL__TERM_SEL2_UNPLUGTERMINATED           	0xEF //1110 1111
#define VAL__TERM_SEL3_UNPLUGTERMINATED           	0xBF //1011 1111
#define VAL__TERM_ALL_UNTERMINATED           		0xFF


//Termination Control Register #2
#define REG__TERMCTRL2			0x083
#define VAL__45OHM				0x60


// ACR Configuration Registers
#define REG__AACR_CFG1          0x088
#define REG__AACR_CFG2          0x089

// Interrupt Status #7 Register
#define REG__INTR7              0x090
#define BIT__VIDEO_READY       	0x20
#define BIT__PCLK_STOP       	0x04

//////////////////////////////////////////
// Interrupt Status #8 Register			//
//////////////////////////////////////////
#define REG__INTR8              0x091
#define REG__INTR8_NO_INFOFR    0x091
/* bit defenitions */
#define BIT__NO_SPD_INF       	0x02
#define BIT__NO_AUD_INF       	0x04
#define BIT__NO_MPEG_INF       	0x08
#define BIT__NO_UNREQ_INF      	0x10
#define BIT__NO_ACP_INF      	0x20
#define BIT__NO_GDB_INF       	0x40
#define BIT__NEW_GDB_INF       	0x80
/* REG__INTR8_NO_INFOFR end bit defenitions */

// Interrupt Unmask Registers
#define REG__INTR7_UNMASK       0x092
#define REG__INTR8_UNMASK       0x093

#define REG__INFM_CLR          0x094
#define BIT__CLR_MPEG		   0x08
#define BIT__CLR_GBD		   0x40
#define BIT__CLR_ACP		   0x20

				 
// Auto Audio Unmute Control 
#define REG__AEC1_CTL		0x0B4
#define BIT__ASC2_EN		0x01

// Auto Exception Control
#define REG__AEC_CTRL       0x0B5
#define BIT__AAC_OE         0x20        // Let AAC Control Audio Output Enables
#define BIT__AVC_EN         0x04        // Enable Auto Video Configuration (AVC)
#define BIT__AAC_EN         0x01        // Enable Auto Audio Control (AAC)
#define BIT__AAC_ALL        0x02        // Enable Auto Audio unmute (AAC_ALL)
#define BIT__CTRL_ACR_EN    0x80

// AEC Exception Enable Registers (3 uint8_ts total)
#define REG__AEC_EN1                0x0B6
#define BIT__CABLE_UNPLUG           0x01
#define BIT__CKDT_DETECT            0x80   
#define BIT__SYNC_DETECT            0x40

#define BIT__HDMI_MODE_CHANGED      0x01
#define BIT__AUDIO_FIFO_UNDERUN     0x02
#define BIT__AUDIO_FIFO_OVERRUN     0x04
#define BIT__CTS_REUSED             0x08   
#define BIT__FS_CHANGED             0x10
#define BIT__H_RES_CHANGED          0x80

#define BIT__V_RES_CHANGED          0x01

#define REG__AVC_EN2			    0x0BA
#define BIT__AUTO_DC_CONF			0x80
#define BIT__AUTO_CLK_DIVIDER		0x40

#define REG__ECC_CTRL				0x0BB
#define BIT__CAPCNT					0x01
#define REG__ECC_HDCP_THRES			0x0C5


//------------------------------------------------------------------------------
// Registers in Page 1
//------------------------------------------------------------------------------

// ACR Control Register #1
#define REG__ACR_CTRL1      0x100
#define BIT__FS_SEL		    0x02  
#define BIT__ACR_INIT       0x01

// ACR Audio Frequency Register
#define REG__FREQ_SVAL      0x102
#define VAL__SWMCLK_128     0x00
#define VAL__SWMCLK_256     0x50
#define VAL__SWMCLK_384     0xA0
#define VAL__SWMCLK_512     0xF0
#define MSK__SWMCLK         0xF0  
#define MSK__SWFS           0x0F

#define VAL__FS_48K     	0x02
#define VAL__FS_192K     	0x0E
#define VAL__FS_768K     	0x09


// ACR PLL Lock Value Registers
#define REG__LKTHRESH1      0x114

#define REG__TCLK_FS          0x117 
#define MSK__TCLKFS           0x0F


// ACR Control #3 Register
#define REG__ACR_CTRL3          0x118
#define MSK__CTS_THRESH			0xF0    
#define VAL__CTS_THRESH(x)      ((x) << 3)  // CTS change threshold

// Audio Output Formatting Registers
#define REG__I2S_CTRL1          0x126
#define BIT__SCK                0x40

// Audio Out I2S Control Register #2
#define REG__I2S_CTRL2          0x127
#define BIT__SD3                0x80    // I2S output enables
#define BIT__SD2                0x40
#define BIT__SD1                0x20
#define BIT__SD0                0x10
#define BIT__MCLKEN             0x08    // MCLK enable

// Audio Out Control Register
#define REG__AUD_CTRL           0x129
#define BIT__MUTE_MODE			0x20    // Soft mute enable
#define BIT__PSERR		        0x10    // Pass S/PDIF error
#define BIT__PAERR		        0x08    // Pass Audio error
#define BIT__I2SMODE			0x04
#define BIT__SPEN				0x01    // S/PDIF enable

// Audio In S/PDIF Channel Status #4
#define REG__AUD_CHST4			0X130
#define BIT__AUD_FS				0x0F 	// AUD_FS, sampling frequency

// HDMI Audio Status Register
#define REG__AUDP_STAT          0x134
#define BIT__HBRA_ON            0x40
#define BIT__DSD_STATUS         0x20
#define BIT__HDMI_LO            0x08  // Audio Layout - ignore unused MSB
#define BIT__HDMI_DET           0x01

// HDMI Mute Register
#define REG__HDMI_MUTE          0x137
#define BIT__VIDM_STATUS		0x40
#define BIT__AUDM               0x02
#define BIT__VIDM               0x01

//System Power Down Register
#define REG__CH0_PD_SYS			0x13F
#define BIT__PD_XTAL		    0x01

//////////////////////////////////////////
// Avi info frame registers				//
//////////////////////////////////////////
#define REG__AVI_TYPE           0x140
//////////////////////////////////////////
// Sud info frame registers				//
//////////////////////////////////////////
#define REG__SPD_TYPE           0x160
#define REG__SPD_DECODE         0x17F

//////////////////////////////////////////
// Aud info frame registers				//
//////////////////////////////////////////
#define REG__AUD_TYPE           0x180
//////////////////////////////////////////
// Mpeg info frame registers			//
//////////////////////////////////////////
#define REG__MPEG_TYPE           0x1A0
#define REG__MPEG_DECODE         0x1BF
//////////////////////////////////////////
// Unreq info frame registers			//
//////////////////////////////////////////
#define REG__UNREQ_TYPE          0x1C0
//////////////////////////////////////////
// Acp info frame registers				//
//////////////////////////////////////////
#define REG__ACP_TYPE           0x1E0
#define REG__ACP_DECODE         0x1FF

//ACP0 Packet byte#2 Register
#define REG__ACP_BYTE2			0x1E1
//////////////////////////////////////////
// Gamut info frame registers			//
//////////////////////////////////////////
#define REG__GMT_TYPE          0xA61

//------------------------------------------------------------------------------
// Registers in Page 8
//------------------------------------------------------------------------------
#define REG__CEC_DEBUG_2		0x886

#define	REG__CEC_DEBUG_3		0x887
#define BIT_SNOOP_EN            0x01
#define BIT_FLUSH_TX_FIFO       0x80

#define REG__CEC_TX_INIT		0x889
#define BIT_SEND_POLL       	0x80

#define REG__CEC_TX_DEST		0x889

#define REG__CEC_CONFIG_CPI		0x88E
#define BIT__CEC_PASS_THROUGH	0x10

#define REG__CEC_TX_COMMAND		0x88F

#define REG__CEC_TRANSMIT_DATA  0x89F
#define	BIT__TX_AUTO_CALC		0x20

#define REG__CEC_CAPTURE_ID0    0x8A2

#define REG__CEC_INT_ENABLE_1	0x8A5

// 0xA6 CPI Interrupt Status Register (R/W)
#define REG__CEC_INT_STATUS_0   0x8A6
#define BIT_CEC_LINE_STATE      0x80
#define BIT_TX_MESSAGE_SENT     0x20
#define BIT_TX_HOTPLUG          0x10
#define BIT_POWER_STAT_CHANGE   0x08
#define BIT_TX_FIFO_EMPTY       0x04
#define BIT_RX_MSG_RECEIVED     0x02
#define BIT_CMD_RECEIVED        0x01
// 0xA7 CPI Interrupt Status Register (R/W)
#define BIT_RX_FIFO_OVERRUN     0x08
#define BIT_SHORT_PULSE_DET     0x04
#define BIT_FRAME_RETRANSM_OV   0x02
#define BIT_START_IRREGULAR     0x01

#define REG__CEC_RX_CONTROL 0x8AC
// CEC  CEC_RX_CONTROL bits
#define BIT_CLR_RX_FIFO_CUR 0x01
#define BIT_CLR_RX_FIFO_ALL 0x02

#define REG__CEC_RX_COUNT   0x8AD
#define BIT_MSG_ERROR       0x80


#define REG__CEC_RX_CMD_HEADER		0x8AE
//#define CEC_RX_OPCODE_ADDR  0xAF
#define REG__CEC_RX_OPERAND_0 		0x8B0


#define REG__CEC_RX_COUNT   	0x8AD


#define REG__EN_EDID			0x901
#define VAL__EN_EDID_NONE		0x00 
#define VAL__EN_EDID_ALL		0x0F

#define REG__EDID_FIFO_ADDR		0x902
#define VAL__FIFO_ADDR_00		0x00

#define REG__EDID_FIFO_DATA		0x903
#define REG__EDID_FIFO_SEL		0x904
#define BIT__SEL_EXTRA			0x10
#define BIT__SEL_EDID0			0x01

#define REG__NVM_COMMAND		0x905
#define VAL__PRG_EXTRA			0x04
#define VAL__PRG_EDID			0x03
#define VAL__COPY_EXTRA			0x06

#define REG__NVM_COMMAND_DONE	0x907
#define BIT__NVM_COMMAND_DONE	0x01

#define REG__BSM_INIT			0x908
#define BIT__BSM_INIT			0x01

#define REG__BSM_STAT			0x909
#define	BIT__BOOT_DONE			0x04
#define	BIT__BOOT_ERROR			0x03

#define REG__NVM_STAT			0x910
#define VAL__NVM_VALID			0x03

#define REG__HPD_HW_CTRL		0x913
#define MSK__INVALIDATE_ALL		0xF0

#define REG__CECPA_ADDR			0x91A

#define REG__RPI_AUTO_CONFIG	0x940
#define BIT__CHECKSUM_EN		0x01
#define BIT__V_UNMUTE_EN		0x02
#define BIT__HCDP_EN			0x04
#define BIT__TERM_EN			0x08

#define REG__WAIT_CYCLE			0x941

#define REG__CLKDETECT_STATUS	0x9D0
#define BIT__CKDT_0				0x01
#define BIT__CKDT_1				0x02
#define BIT__CKDT_2				0x04
#define BIT__CKDT_3				0x08

#define REG__PWR5V_STATUS		0x9D1
#define BIT__PWR5V_0			0x01
#define BIT__PWR5V_1			0x02
#define BIT__PWR5V_2			0x04
#define BIT__PWR5V_3			0x08

#define REG__CBUS_PAD_SC		0x9D4
#define VAL__SC_CONF			0xFF

#define REG__DRIVE_CNTL			0x9E9
#define MSK__ODCK_STRENGTH		0x70
#define VAL__ODCK_STRENGTH		0x60

//------------------------------------------------------------------------------
// Factory Configuration Registers 
//------------------------------------------------------------------------------
#define REG__FACTORY_00E		0x00E

#define REG__FACTORY_9E5		0x9E5

#define REG__FACTORY_A81		0xA81
#define REG__FACTORY_A87		0xA87
#define REG__FACTORY_A88		0xA88 
#define REG__FACTORY_A89		0xA89
#define REG__FACTORY_A92		0xA92 

#define REG__FACTORY_AB5		0xAB5
#define REG__FACTORY_ABB		0xABB


//------------------------------------------------------------------------------
// Virtual registers which are used to group info frame interrupts
// for convenient processing
//------------------------------------------------------------------------------
#define BIT__VIRT_NO_AVI_INF	BIT__NEW_GDB_INF
#define BIT__VIRT_NEW_ACP_INF	0x40
#define BIT__VIRT_NEW_GDB_INF	BIT__NEW_GDB_INF



#endif  // __REGISTERS_H__
