#ifndef _HDMI_TX_REG_H
#define _HDMI_TX_REG_H

//wait for pll lock
//must wait first (100us+) then polling lock bit to check
/*
#define M6_PLL_WAIT_FOR_LOCK(pll) \
	do{\
		__udelay(1000);\
	}while((aml_read_reg32(pll)&0x80000000)==0);
*/
#ifdef CONFIG_ARCH_MESON6
//M6 PLL control value 
#define M6_PLL_CNTL_CST2 (0x814d3928)
#define M6_PLL_CNTL_CST3 (0x6b425012)
#define M6_PLL_CNTL_CST4 (0x110)

//VID PLL
#define M6_VID_PLL_CNTL_2 (M6_PLL_CNTL_CST2)
#define M6_VID_PLL_CNTL_3 (M6_PLL_CNTL_CST3)
#define M6_VID_PLL_CNTL_4 (M6_PLL_CNTL_CST4)
#endif
unsigned int hdmi_rd_reg(unsigned int addr);

#define hdmi_wr_only_reg(addr, data)   hdmi_wr_reg(addr, data)

void hdmi_wr_reg(unsigned int addr, unsigned int data);

#define hdmi_set_reg_bits(reg, val, start, len) \
  hdmi_wr_reg(reg, (hdmi_rd_reg(reg) & ~(((1L<<(len))-1)<<(start)))|((unsigned int)(val) << (start)))

typedef struct {
    unsigned short cbus_addr;
    unsigned char gate_bit;
}Hdmi_Gate_s;


//inside chip
// tx base addr  : 0x00000 ~ 0x03fff
// rx base addr  : 0x04000 ~ 0x07fff
// sim base addr : 0x08000 ~ 0x0bfff
// cec0 base addr : 0x0c000 ~ 0x0c0ff
//external module
// tx base addr  : 0x10000 ~ 0x13fff
// rx base addr  : 0x14000 ~ 0x17fff
// sim base addr : 0x18000 ~ 0x1bfff
// cec0 base addr : 0x1c000 ~ 0x1c0ff
// cec1 base addr : 0x1c100 ~ 0x1c1ff

#define TX_BASE_ADDR     0x00000        //inside chip

#define CEC0_BASE_ADDR    0x0c000       //inside chip 
#define CEC1_BASE_ADDR    0x1c100       //outside chip 

#define OTHER_BASE_ADDR  0x08000        //inside chip

//********** OTHER BASE related **********//
#define HDMI_OTHER_CTRL0            0x0
#define HDMI_OTHER_CTRL1            0x1
#define HDMI_OTHER_STATUS0          0x2
#define HDMI_OTHER_CTRL2            0x3
#define HDMI_OTHER_INTR_MASKN       0x4
#define HDMI_OTHER_INTR_STAT        0x5
#define HDMI_OTHER_INTR_STAT_CLR    0x6

//********** TX related **********//
#define TX_RX_EDID_OFFSET               TX_BASE_ADDR+0x600 
#define TX_HDCP_SHADOW_OFFSET           TX_BASE_ADDR+0x100 
#define TX_HDCP_BKSV_SHADOW             TX_HDCP_SHADOW_OFFSET
#define TX_HDCP_AKSV_SHADOW             TX_HDCP_SHADOW_OFFSET + 0x10

#define TX_IEC60958_SUB1_OFFSET         TX_BASE_ADDR+0x0B0 
#define TX_IEC60958_SUB2_OFFSET         TX_BASE_ADDR+0x0C8 

#define TX_IEC60958_ST_SUB1_OFFSET      TX_BASE_ADDR+0x1B0 
#define TX_IEC60958_ST_SUB2_OFFSET      TX_BASE_ADDR+0x1C8 

// System config 0
#define TX_SYS0_AFE_SIGNAL        TX_BASE_ADDR+0x000 
#define TX_SYS0_AFE_LOOP          TX_BASE_ADDR+0x001 
#define TX_SYS0_ACR_CTS_0         TX_BASE_ADDR+0x002 
#define TX_SYS0_ACR_CTS_1         TX_BASE_ADDR+0x003 
#define TX_SYS0_ACR_CTS_2         TX_BASE_ADDR+0x004 
#define TX_SYS0_BIST_CONTROL      TX_BASE_ADDR+0x005 
#define TX_SYS0_BIST_DATA_0       TX_BASE_ADDR+0x006 
#define TX_SYS0_BIST_DATA_1       TX_BASE_ADDR+0x007 
#define TX_SYS0_BIST_DATA_2       TX_BASE_ADDR+0x008 
#define TX_SYS0_BIST_DATA_3       TX_BASE_ADDR+0x009 
#define TX_SYS0_BIST_DATA_4       TX_BASE_ADDR+0x00A 
#define TX_SYS0_BIST_DATA_5       TX_BASE_ADDR+0x00B 
#define TX_SYS0_BIST_DATA_6       TX_BASE_ADDR+0x00C 
#define TX_SYS0_BIST_DATA_7       TX_BASE_ADDR+0x00D 
#define TX_SYS0_BIST_DATA_8       TX_BASE_ADDR+0x00E 
#define TX_SYS0_BIST_DATA_9       TX_BASE_ADDR+0x00F 
// system config 1
#define TX_HDMI_PHY_CONFIG0       TX_BASE_ADDR+0x010
    #define HDMI_COMMON_b7_b0       0
#define TX_HDMI_PHY_CONFIG1       TX_BASE_ADDR+0x011
    #define HDMI_CTL_REG_b3_b0      4
    #define HDMI_COMMON_b11_b8      0
#define TX_HDMI_PHY_CONFIG2        TX_BASE_ADDR+0x012 
    #define HDMI_CTL_REG_b11_b4     0
#define TX_HDMI_PHY_CONFIG3        TX_BASE_ADDR+0x013 
    #define HDMI_MDR_PU             4
    #define HDMI_L2H_CTL            0
#define TX_HDMI_PHY_CONFIG4        TX_BASE_ADDR+0x014 
    #define HDMI_PREM_CTL           4
    #define HDMI_MODE_P               2   //0:narmal mode  1:clk chan(ch3) equals ch0  2:alternate high/low  3:alternate low/high
    #define HDMI_PHY_CLK_EN         1   //1:enable serialzer clock
    #define HDMI_LF_PD              0
#define TX_HDMI_PHY_CONFIG5        TX_BASE_ADDR+0x015 
    #define HDMI_VCM_CTL            5
    #define HDMI_PREFCTL            0
#define TX_HDMI_PHY_CONFIG6         TX_BASE_ADDR+0x016 
    #define HDMI_SWING_CTL          4
    #define HDMI_RTERM_CTL          0
//#define TX_SYS1_AFE_TEST          TX_BASE_ADDR+0x017 
//#define TX_SYS1_PLL               TX_BASE_ADDR+0x018 
//#define TX_SYS1_TUNE              TX_BASE_ADDR+0x019 
//#define TX_SYS1_AFE_CONNECT       TX_BASE_ADDR+0x01A 
#define TX_SYS1_ACR_N_0           TX_BASE_ADDR+0x01C 
#define TX_SYS1_ACR_N_1           TX_BASE_ADDR+0x01D 
#define TX_SYS1_ACR_N_2           TX_BASE_ADDR+0x01E 
#define TX_SYS1_PRBS_DATA         TX_BASE_ADDR+0x01F 
// system config 4
#define TX_SYS4_TX_CKI_DDR        TX_BASE_ADDR+0x0A0 
#define TX_SYS4_TX_CKO_DDR        TX_BASE_ADDR+0x0A1 
#define TX_SYS4_RX_CKI_DDR        TX_BASE_ADDR+0x0A2 
#define TX_SYS4_RX_CKO_DDR        TX_BASE_ADDR+0x0A3 
#define TX_SYS4_CONNECT_SEL_0     TX_BASE_ADDR+0x0A4 
#define TX_SYS4_CONNECT_SEL_1     TX_BASE_ADDR+0x0A5 
#define TX_SYS4_CONNECT_SEL_2     TX_BASE_ADDR+0x0A6 
#define TX_SYS4_CONNECT_SEL_3     TX_BASE_ADDR+0x0A7 
#define TX_SYS4_CK_INV_VIDEO      TX_BASE_ADDR+0x0A8 
#define TX_SYS4_CK_INV_AUDIO      TX_BASE_ADDR+0x0A9 
#define TX_SYS4_CK_INV_AFE        TX_BASE_ADDR+0x0AA 
#define TX_SYS4_CK_INV_CH01       TX_BASE_ADDR+0x0AB 
#define TX_SYS4_CK_INV_CH2        TX_BASE_ADDR+0x0AC 
#define TX_SYS4_CK_CEC            TX_BASE_ADDR+0x0AD 
#define TX_SYS4_CK_SOURCE_1       TX_BASE_ADDR+0x0AE 
#define TX_SYS4_CK_SOURCE_2       TX_BASE_ADDR+0x0AF 
// system config 5
#define TX_SYS5_TX_SOFT_RESET_1   TX_BASE_ADDR+0x0E0 
#define TX_SYS5_TX_SOFT_RESET_2   TX_BASE_ADDR+0x0E1 
#define TX_SYS5_RX_SOFT_RESET_1   TX_BASE_ADDR+0x0E2 
#define TX_SYS5_RX_SOFT_RESET_2   TX_BASE_ADDR+0x0E3 
#define TX_SYS5_RX_SOFT_RESET_3   TX_BASE_ADDR+0x0E4 
#define TX_SYS5_SSTL_BIDIR_IN     TX_BASE_ADDR+0x0E5 
#define TX_SYS5_SSTL_IN           TX_BASE_ADDR+0x0E6 
#define TX_SYS5_SSTL_DIFF_IN      TX_BASE_ADDR+0x0E7 
#define TX_SYS5_FIFO_CONFIG       TX_BASE_ADDR+0x0E8 
#define TX_SYS5_FIFO_SAMP01_CFG   TX_BASE_ADDR+0x0E9 
#define TX_SYS5_FIFO_SAMP23_CFG   TX_BASE_ADDR+0x0EA 
#define TX_SYS5_CONNECT_FIFO_CFG  TX_BASE_ADDR+0x0EB 
#define TX_SYS5_IO_CALIB_CONTROL  TX_BASE_ADDR+0x0EC 
#define TX_SYS5_SSTL_BIDIR_OUT    TX_BASE_ADDR+0x0ED 
#define TX_SYS5_SSTL_OUT          TX_BASE_ADDR+0x0EE 
#define TX_SYS5_SSTL_DIFF_OUT     TX_BASE_ADDR+0x0EF 

// HDCP CONFIG
#define TX_HDCP_ECC_CONFIG        TX_BASE_ADDR+0x024 
#define TX_HDCP_CRC_CONFIG        TX_BASE_ADDR+0x025 
#define TX_HDCP_EDID_CONFIG       TX_BASE_ADDR+0x026 
#define TX_HDCP_MEM_CONFIG        TX_BASE_ADDR+0x027 
#define TX_HDCP_HPD_FILTER_L      TX_BASE_ADDR+0x028 
#define TX_HDCP_HPD_FILTER_H      TX_BASE_ADDR+0x029 
#define TX_HDCP_ENCRYPT_BYTE      TX_BASE_ADDR+0x02A 
#define TX_HDCP_CONFIG0           TX_BASE_ADDR+0x02B 
#define TX_HDCP_CONFIG1           TX_BASE_ADDR+0x02C 
#define TX_HDCP_CONFIG2           TX_BASE_ADDR+0x02D 
#define TX_HDCP_CONFIG3           TX_BASE_ADDR+0x02E 
#define TX_HDCP_MODE              TX_BASE_ADDR+0x02F 

// Video config, part 1
#define TX_VIDEO_ACTIVE_PIXELS_0  TX_BASE_ADDR+0x030 
#define TX_VIDEO_ACTIVE_PIXELS_1  TX_BASE_ADDR+0x031 
#define TX_VIDEO_FRONT_PIXELS     TX_BASE_ADDR+0x032 
#define TX_VIDEO_HSYNC_PIXELS     TX_BASE_ADDR+0x033 
#define TX_VIDEO_BACK_PIXELS      TX_BASE_ADDR+0x034 
#define TX_VIDEO_ACTIVE_LINES_0   TX_BASE_ADDR+0x035 
#define TX_VIDEO_ACTIVE_LINES_1   TX_BASE_ADDR+0x036 
#define TX_VIDEO_EOF_LINES        TX_BASE_ADDR+0x037 
#define TX_VIDEO_VSYNC_LINES      TX_BASE_ADDR+0x038 
#define TX_VIDEO_SOF_LINES        TX_BASE_ADDR+0x039 
#define TX_VIDEO_DTV_TIMING       TX_BASE_ADDR+0x03A 
#define TX_VIDEO_DTV_MODE         TX_BASE_ADDR+0x03B 
#define TX_VIDEO_DTV_FORMAT0      TX_BASE_ADDR+0x03C 
#define TX_VIDEO_DTV_FORMAT1      TX_BASE_ADDR+0x03D 
#define TX_VIDEO_PIXEL_PACK       TX_BASE_ADDR+0x03F 
// video config, part 2
#define TX_VIDEO_CSC_COEFF_B0     TX_BASE_ADDR+0x040 
#define TX_VIDEO_CSC_COEFF_B1     TX_BASE_ADDR+0x041 
#define TX_VIDEO_CSC_COEFF_R0     TX_BASE_ADDR+0x042 
#define TX_VIDEO_CSC_COEFF_R1     TX_BASE_ADDR+0x043 
#define TX_VIDEO_CSC_COEFF_CB0    TX_BASE_ADDR+0x044 
#define TX_VIDEO_CSC_COEFF_CB1    TX_BASE_ADDR+0x045 
#define TX_VIDEO_CSC_COEFF_CR0    TX_BASE_ADDR+0x046 
#define TX_VIDEO_CSC_COEFF_CR1    TX_BASE_ADDR+0x047 
#define TX_VIDEO_DTV_OPTION_L     TX_BASE_ADDR+0x048 
#define TX_VIDEO_DTV_OPTION_H     TX_BASE_ADDR+0x049 
#define TX_VIDEO_DTV_FILTER       TX_BASE_ADDR+0x04A 
#define TX_VIDEO_DTV_DITHER       TX_BASE_ADDR+0x04B 
#define TX_VIDEO_DTV_DEDITHER     TX_BASE_ADDR+0x04C 
#define TX_VIDEO_PROC_CONFIG0     TX_BASE_ADDR+0x04E 
#define TX_VIDEO_PROC_CONFIG1     TX_BASE_ADDR+0x04F 

// Audio config
#define TX_AUDIO_FORMAT           TX_BASE_ADDR+0x058 
#define TX_AUDIO_SPDIF            TX_BASE_ADDR+0x059 
#define TX_AUDIO_I2S              TX_BASE_ADDR+0x05A 
#define TX_AUDIO_FIFO             TX_BASE_ADDR+0x05B 
#define TX_AUDIO_LIPSYNC          TX_BASE_ADDR+0x05C 
#define TX_AUDIO_CONTROL          TX_BASE_ADDR+0x05D 
#define TX_AUDIO_HEADER           TX_BASE_ADDR+0x05E 
#define TX_AUDIO_SAMPLE           TX_BASE_ADDR+0x05F 
#define TX_AUDIO_VALID            TX_BASE_ADDR+0x060 
#define TX_AUDIO_USER             TX_BASE_ADDR+0x061 
#define TX_AUDIO_PACK             TX_BASE_ADDR+0x062 
#define TX_AUDIO_CONTROL_MORE     TX_BASE_ADDR+0x064

// tmds config
#define TX_TMDS_MODE              TX_BASE_ADDR+0x068 
#define TX_TMDS_CONFIG0           TX_BASE_ADDR+0x06C 
#define TX_TMDS_CONFIG1           TX_BASE_ADDR+0x06D 

// packet config
#define TX_PACKET_ALLOC_ACTIVE_1  TX_BASE_ADDR+0x078 
#define TX_PACKET_ALLOC_ACTIVE_2  TX_BASE_ADDR+0x079 
#define TX_PACKET_ALLOC_EOF_1     TX_BASE_ADDR+0x07A 
#define TX_PACKET_ALLOC_EOF_2     TX_BASE_ADDR+0x07B 
#define TX_PACKET_ALLOC_SOF_1     TX_BASE_ADDR+0x07C 
#define TX_PACKET_ALLOC_SOF_2     TX_BASE_ADDR+0x07D 
#define TX_PACKET_CONTROL_1       TX_BASE_ADDR+0x07E 
#define TX_PACKET_CONTROL_2       TX_BASE_ADDR+0x07F 

// core config
#define TX_CORE_DATA_CAPTURE_1    TX_BASE_ADDR+0x0F0 
#define TX_CORE_DATA_CAPTURE_2    TX_BASE_ADDR+0x0F1 
#define TX_CORE_DATA_MONITOR_1    TX_BASE_ADDR+0x0F2 
#define TX_CORE_DATA_MONITOR_2    TX_BASE_ADDR+0x0F3 
#define TX_CORE_CALIB_MODE        TX_BASE_ADDR+0x0F4 
#define TX_CORE_CALIB_SAMPLE_DELAY  TX_BASE_ADDR+0x0F5 
#define TX_CORE_CALIB_VALUE_AUTO  TX_BASE_ADDR+0x0F6 
#define TX_CORE_CALIB_VALUE       TX_BASE_ADDR+0x0F7 

#define TX_CORE_EDID_CONFIG_MORE  TX_BASE_ADDR+0x080

// HDCP shadow register
#define TX_HDCP_SHW_BKSV_0        TX_BASE_ADDR+0x100 
#define TX_HDCP_SHW_BKSV_1        TX_BASE_ADDR+0x101 
#define TX_HDCP_SHW_BKSV_2        TX_BASE_ADDR+0x102 
#define TX_HDCP_SHW_BKSV_3        TX_BASE_ADDR+0x103 
#define TX_HDCP_SHW_BKSV_4        TX_BASE_ADDR+0x104 
#define TX_HDCP_SHW_RI1_0         TX_BASE_ADDR+0x108 
#define TX_HDCP_SHW_RI1_1         TX_BASE_ADDR+0x109 
#define TX_HDCP_SHW_PJ1           TX_BASE_ADDR+0x10A 
#define TX_HDCP_SHW_AKSV_0        TX_BASE_ADDR+0x110 
#define TX_HDCP_SHW_AKSV_1        TX_BASE_ADDR+0x111 
#define TX_HDCP_SHW_AKSV_2        TX_BASE_ADDR+0x112 
#define TX_HDCP_SHW_AKSV_3        TX_BASE_ADDR+0x113 
#define TX_HDCP_SHW_AKSV_4        TX_BASE_ADDR+0x114 
#define TX_HDCP_SHW_AINFO         TX_BASE_ADDR+0x115 
#define TX_HDCP_SHW_AN_0          TX_BASE_ADDR+0x118 
#define TX_HDCP_SHW_AN_1          TX_BASE_ADDR+0x119 
#define TX_HDCP_SHW_AN_2          TX_BASE_ADDR+0x11A 
#define TX_HDCP_SHW_AN_3          TX_BASE_ADDR+0x11B 
#define TX_HDCP_SHW_AN_4          TX_BASE_ADDR+0x11C 
#define TX_HDCP_SHW_AN_5          TX_BASE_ADDR+0x11D 
#define TX_HDCP_SHW_AN_6          TX_BASE_ADDR+0x11E 
#define TX_HDCP_SHW_AN_7          TX_BASE_ADDR+0x11F 
#define TX_HDCP_SHW_V1_H0_0       TX_BASE_ADDR+0x120 
#define TX_HDCP_SHW_V1_H0_1       TX_BASE_ADDR+0x121 
#define TX_HDCP_SHW_V1_H0_2       TX_BASE_ADDR+0x122 
#define TX_HDCP_SHW_V1_H0_3       TX_BASE_ADDR+0x123 
#define TX_HDCP_SHW_V1_H1_0       TX_BASE_ADDR+0x124 
#define TX_HDCP_SHW_V1_H1_1       TX_BASE_ADDR+0x125 
#define TX_HDCP_SHW_V1_H1_2       TX_BASE_ADDR+0x126 
#define TX_HDCP_SHW_V1_H1_3       TX_BASE_ADDR+0x127 
#define TX_HDCP_SHW_V1_H2_0       TX_BASE_ADDR+0x128 
#define TX_HDCP_SHW_V1_H2_1       TX_BASE_ADDR+0x129 
#define TX_HDCP_SHW_V1_H2_2       TX_BASE_ADDR+0x12A 
#define TX_HDCP_SHW_V1_H2_3       TX_BASE_ADDR+0x12B 
#define TX_HDCP_SHW_V1_H3_0       TX_BASE_ADDR+0x12C 
#define TX_HDCP_SHW_V1_H3_1       TX_BASE_ADDR+0x12D 
#define TX_HDCP_SHW_V1_H3_2       TX_BASE_ADDR+0x12E 
#define TX_HDCP_SHW_V1_H3_3       TX_BASE_ADDR+0x12F 
#define TX_HDCP_SHW_V1_H4_0       TX_BASE_ADDR+0x130 
#define TX_HDCP_SHW_V1_H4_1       TX_BASE_ADDR+0x131 
#define TX_HDCP_SHW_V1_H4_2       TX_BASE_ADDR+0x132 
#define TX_HDCP_SHW_V1_H4_3       TX_BASE_ADDR+0x133 
#define TX_HDCP_SHW_BCAPS         TX_BASE_ADDR+0x140 
#define TX_HDCP_SHW_BSTATUS_0     TX_BASE_ADDR+0x141 
#define TX_HDCP_SHW_BSTATUS_1     TX_BASE_ADDR+0x142 
#define TX_HDCP_SHW_KSV_FIFO      TX_BASE_ADDR+0x143 

// system status 0
#define TX_SYSST0_CONNECT_FIFO    TX_BASE_ADDR+0x180 
#define TX_SYSST0_PLL_MONITOR     TX_BASE_ADDR+0x181 
#define TX_SYSST0_AFE_FIFO        TX_BASE_ADDR+0x182 
#define TX_SYSST0_ROM_STATUS      TX_BASE_ADDR+0x18F 

// system status 1
#define TX_SYSST1_CALIB_BIT_RESULT_0     TX_BASE_ADDR+0x1E0 
#define TX_SYSST1_CALIB_BIT_RESULT_1     TX_BASE_ADDR+0x1E1 
//HDMI_STATUS_OUT[7:0]
#define TX_HDMI_PHY_READBACK_0           TX_BASE_ADDR+0x1E2 
//HDMI_COMP_OUT[4]
//HDMI_STATUS_OUT[11:8]
#define TX_HDMI_PHY_READBACK_1           TX_BASE_ADDR+0x1E3 
#define TX_SYSST1_CALIB_BIT_RESULT_4     TX_BASE_ADDR+0x1E4 
#define TX_SYSST1_CALIB_BIT_RESULT_5     TX_BASE_ADDR+0x1E5 
#define TX_SYSST1_CALIB_BIT_RESULT_6     TX_BASE_ADDR+0x1E6 
#define TX_SYSST1_CALIB_BIT_RESULT_7     TX_BASE_ADDR+0x1E7 
#define TX_SYSST1_CALIB_BUS_RESULT_0     TX_BASE_ADDR+0x1E8 
#define TX_SYSST1_CALIB_BUS_RESULT_1     TX_BASE_ADDR+0x1E9 
#define TX_SYSST1_CALIB_BUS_RESULT_2     TX_BASE_ADDR+0x1EA 
#define TX_SYSST1_CALIB_BUS_RESULT_3     TX_BASE_ADDR+0x1EB 
#define TX_SYSST1_CALIB_BUS_RESULT_4     TX_BASE_ADDR+0x1EC 
#define TX_SYSST1_CALIB_BUS_RESULT_5     TX_BASE_ADDR+0x1ED 
#define TX_SYSST1_CALIB_BUS_RESULT_6     TX_BASE_ADDR+0x1EE 
#define TX_SYSST1_CALIB_BUS_RESULT_7     TX_BASE_ADDR+0x1EF 

// hdcp status
#define TX_HDCP_ST_AUTHENTICATION        TX_BASE_ADDR+0x190 
#define TX_HDCP_ST_FRAME_COUNT           TX_BASE_ADDR+0x191 
#define TX_HDCP_ST_STATUS_0              TX_BASE_ADDR+0x192 
#define TX_HDCP_ST_STATUS_1              TX_BASE_ADDR+0x193 
#define TX_HDCP_ST_STATUS_2              TX_BASE_ADDR+0x194 
#define TX_HDCP_ST_STATUS_3              TX_BASE_ADDR+0x195 
#define TX_HDCP_ST_EDID_STATUS           TX_BASE_ADDR+0x196 
#define TX_HDCP_ST_MEM_STATUS            TX_BASE_ADDR+0x197 
#define TX_HDCP_ST_ST_MODE               TX_BASE_ADDR+0x19F 

// video status
#define TX_VIDEO_ST_ACTIVE_PIXELS_1      TX_BASE_ADDR+0x1A0 
#define TX_VIDEO_ST_ACTIVE_PIXELS_2      TX_BASE_ADDR+0x1A1 
#define TX_VIDEO_ST_FRONT_PIXELS         TX_BASE_ADDR+0x1A2 
#define TX_VIDEO_ST_HSYNC_PIXELS         TX_BASE_ADDR+0x1A3 
#define TX_VIDEO_ST_BACK_PIXELS          TX_BASE_ADDR+0x1A4 
#define TX_VIDEO_ST_ACTIVE_LINES_1       TX_BASE_ADDR+0x1A5 
#define TX_VIDEO_ST_ACTIVE_LINES_2       TX_BASE_ADDR+0x1A6 
#define TX_VIDEO_ST_EOF_LINES            TX_BASE_ADDR+0x1A7 
#define TX_VIDEO_ST_VSYNC_LINES          TX_BASE_ADDR+0x1A8 
#define TX_VIDEO_ST_SOF_LINES            TX_BASE_ADDR+0x1A9 
#define TX_VIDEO_ST_DTV_TIMING           TX_BASE_ADDR+0x1AA 
#define TX_VIDEO_ST_DTV_MODE             TX_BASE_ADDR+0x1AB 
// audio status
#define TX_VIDEO_ST_AUDIO_STATUS         TX_BASE_ADDR+0x1AC 
#define TX_AFE_STATUS_0                  TX_BASE_ADDR+0x1AE 
#define TX_AFE_STATUS_1                  TX_BASE_ADDR+0x1AF 

// Packet status
#define TX_PACKET_ST_REQUEST_STATUS_1    TX_BASE_ADDR+0x1F0 
#define TX_PACKET_ST_REQUEST_STATUS_2    TX_BASE_ADDR+0x1F1 
#define TX_PACKET_ST_REQUEST_MISSED_1    TX_BASE_ADDR+0x1F2 
#define TX_PACKET_ST_REQUEST_MISSED_2    TX_BASE_ADDR+0x1F3 
#define TX_PACKET_ST_ENCODE_STATUS_0     TX_BASE_ADDR+0x1F4 
#define TX_PACKET_ST_ENCODE_STATUS_1     TX_BASE_ADDR+0x1F5 
#define TX_PACKET_ST_ENCODE_STATUS_2     TX_BASE_ADDR+0x1F6 
#define TX_PACKET_ST_TIMER_STATUS        TX_BASE_ADDR+0x1F7 

// tmds status
#define TX_TMDS_ST_CLOCK_METER_1         TX_BASE_ADDR+0x1F8 
#define TX_TMDS_ST_CLOCK_METER_2         TX_BASE_ADDR+0x1F9 
#define TX_TMDS_ST_CLOCK_METER_3         TX_BASE_ADDR+0x1FA 
#define TX_TMDS_ST_TMDS_STATUS_1         TX_BASE_ADDR+0x1FC 
#define TX_TMDS_ST_TMDS_STATUS_2         TX_BASE_ADDR+0x1FD 
#define TX_TMDS_ST_TMDS_STATUS_3         TX_BASE_ADDR+0x1FE 
#define TX_TMDS_ST_TMDS_STATUS_4         TX_BASE_ADDR+0x1FF 


// Packet register
#define TX_PKT_REG_SPD_INFO_BASE_ADDR     TX_BASE_ADDR+0x200 
#define TX_PKT_REG_VEND_INFO_BASE_ADDR    TX_BASE_ADDR+0x220 
#define TX_PKT_REG_MPEG_INFO_BASE_ADDR    TX_BASE_ADDR+0x240 
#define TX_PKT_REG_AVI_INFO_BASE_ADDR     TX_BASE_ADDR+0x260 
#define TX_PKT_REG_AUDIO_INFO_BASE_ADDR   TX_BASE_ADDR+0x280 
#define TX_PKT_REG_ACP_INFO_BASE_ADDR     TX_BASE_ADDR+0x2A0 
#define TX_PKT_REG_ISRC1_BASE_ADDR        TX_BASE_ADDR+0x2C0 
#define TX_PKT_REG_ISRC2_BASE_ADDR        TX_BASE_ADDR+0x2E0 
#define TX_PKT_REG_EXCEPT0_BASE_ADDR      TX_BASE_ADDR+0x300 
#define TX_PKT_REG_EXCEPT1_BASE_ADDR      TX_BASE_ADDR+0x320 
#define TX_PKT_REG_EXCEPT2_BASE_ADDR      TX_BASE_ADDR+0x340 
#define TX_PKT_REG_EXCEPT3_BASE_ADDR      TX_BASE_ADDR+0x360 
#define TX_PKT_REG_EXCEPT4_BASE_ADDR      TX_BASE_ADDR+0x380 
#define TX_PKT_REG_GAMUT_P0_BASE_ADDR     TX_BASE_ADDR+0x3A0 
#define TX_PKT_REG_GAMUT_P1_1_BASE_ADDR   TX_BASE_ADDR+0x3C0 
#define TX_PKT_REG_GAMUT_P1_2_BASE_ADDR   TX_BASE_ADDR+0x3E0 



//********** CEC related **********//
//read/write
#define CEC_TX_MSG_0_HEADER        0x00 
#define CEC_TX_MSG_1_OPCODE        0x01 
#define CEC_TX_MSG_2_OP1           0x02 
#define CEC_TX_MSG_3_OP2           0x03 
#define CEC_TX_MSG_4_OP3           0x04 
#define CEC_TX_MSG_5_OP4           0x05 
#define CEC_TX_MSG_6_OP5           0x06 
#define CEC_TX_MSG_7_OP6           0x07 
#define CEC_TX_MSG_8_OP7           0x08 
#define CEC_TX_MSG_9_OP8           0x09 
#define CEC_TX_MSG_A_OP9           0x0A 
#define CEC_TX_MSG_B_OP10          0x0B 
#define CEC_TX_MSG_C_OP11          0x0C 
#define CEC_TX_MSG_D_OP12          0x0D 
#define CEC_TX_MSG_E_OP13          0x0E 
#define CEC_TX_MSG_F_OP14          0x0F 

//read only
#define CEC_TX_MSG_LENGTH          0x10 
#define CEC_TX_MSG_CMD             0x11 
#define CEC_TX_WRITE_BUF           0x12 
#define CEC_TX_CLEAR_BUF           0x13 
#define CEC_RX_MSG_CMD             0x14 
#define CEC_RX_CLEAR_BUF           0x15 
#define CEC_LOGICAL_ADDR0          0x16 
#define CEC_LOGICAL_ADDR1          0x17 
#define CEC_LOGICAL_ADDR2          0x18 
#define CEC_LOGICAL_ADDR3          0x19 
#define CEC_LOGICAL_ADDR4          0x1A 
#define CEC_CLOCK_DIV_H            0x1B 
#define CEC_CLOCK_DIV_L            0x1C 

//read/write
#define CEC_RX_MSG_0_HEADER        0x80 
#define CEC_RX_MSG_1_OPCODE        0x81 
#define CEC_RX_MSG_2_OP1           0x82 
#define CEC_RX_MSG_3_OP2           0x83 
#define CEC_RX_MSG_4_OP3           0x84 
#define CEC_RX_MSG_5_OP4           0x85 
#define CEC_RX_MSG_6_OP5           0x86 
#define CEC_RX_MSG_7_OP6           0x87 
#define CEC_RX_MSG_8_OP7           0x88 
#define CEC_RX_MSG_9_OP8           0x89 
#define CEC_RX_MSG_A_OP9           0x8A 
#define CEC_RX_MSG_B_OP10          0x8B 
#define CEC_RX_MSG_C_OP11          0x8C 
#define CEC_RX_MSG_D_OP12          0x8D 
#define CEC_RX_MSG_E_OP13          0x8E 
#define CEC_RX_MSG_F_OP14          0x8F 

//read only
#define CEC_RX_MSG_LENGTH          0x90 
#define CEC_RX_MSG_STATUS          0x91 
#define CEC_RX_NUM_MSG             0x92 
#define CEC_TX_MSG_STATUS          0x93 
#define CEC_TX_NUM_MSG             0x94 

// tx_msg_cmd definition
#define TX_NO_OP                0  // No transaction
#define TX_REQ_CURRENT          1  // Transmit earliest message in buffer
#define TX_ABORT                2  // Abort transmitting earliest message
#define TX_REQ_NEXT             3  // Overwrite earliest message in buffer and transmit next message

// tx_msg_status definition
#define TX_IDLE                 0  // No transaction
#define TX_BUSY                 1  // Transmitter is busy
#define TX_DONE                 2  // Message has been successfully transmitted
#define TX_ERROR                3  // Message has been transmitted with error

// rx_msg_cmd
#define RX_NO_OP                0  // No transaction
#define RX_ACK_CURRENT          1  // Read earliest message in buffer
#define RX_DISABLE              2  // Disable receiving latest message
#define RX_ACK_NEXT             3  // Clear earliest message from buffer and read next message

// rx_msg_status
#define RX_IDLE                 0  // No transaction
#define RX_BUSY                 1  // Receiver is busy
#define RX_DONE                 2  // Message has been received successfully
#define RX_ERROR                3  // Message has been received with error

#endif

