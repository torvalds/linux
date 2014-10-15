#ifndef HDMIRX_PARAMETER_DEFINE_H
#define HDMIRX_PARAMETER_DEFINE_H

//------------------------------------------------------------------------------
// TOP-level wrapper registers addresses
//------------------------------------------------------------------------------

#define HDMIRX_TOP_SW_RESET                     0x000
#define HDMIRX_TOP_CLK_CNTL                     0x001
#define HDMIRX_TOP_HPD_PWR5V                    0x002
#define HDMIRX_TOP_PORT_SEL                     0x003
#define HDMIRX_TOP_EDID_GEN_CNTL                0x004
#define HDMIRX_TOP_EDID_ADDR_CEC                0x005
#define HDMIRX_TOP_EDID_DATA_CEC_PORT01         0x006
#define HDMIRX_TOP_EDID_DATA_CEC_PORT23         0x007
#define HDMIRX_TOP_EDID_GEN_STAT                0x008
#define HDMIRX_TOP_INTR_MASKN                   0x009
#define HDMIRX_TOP_INTR_STAT                    0x00A
#define HDMIRX_TOP_INTR_STAT_CLR                0x00B
#define HDMIRX_TOP_VID_CNTL                     0x00C
#define HDMIRX_TOP_VID_STAT                     0x00D
#define HDMIRX_TOP_ACR_CNTL_STAT                0x00E
#define HDMIRX_TOP_ACR_AUDFIFO                  0x00F
#define HDMIRX_TOP_ARCTX_CNTL                   0x010
#define HDMIRX_TOP_METER_HDMI_CNTL              0x011
#define HDMIRX_TOP_METER_HDMI_STAT              0x012

#define HDMIRX_TOP_EDID_OFFSET                  0x200

//------------------------------------------------------------------------------
// DWC_HDMI1.3_RX_Controller registers addresses
//------------------------------------------------------------------------------

#define HDMIRX_DWC_HDMI_SETUP_CTRL              0x0000
#define HDMIRX_DWC_HDMI_OVR_CTRL                0x0004
#define HDMIRX_DWC_HDMI_TIMER_CTRL              0x0008
#define HDMIRX_DWC_HDMI_RES_OVR                 0x0010
#define HDMIRX_DWC_HDMI_RES_STS                 0x0014
#define HDMIRX_DWC_HDMI_PLL_CTRL                0x0018
#define HDMIRX_DWC_HDMI_PLL_FRQSET1             0x001C
#define HDMIRX_DWC_HDMI_PLL_FRQSET2             0x0020
#define HDMIRX_DWC_HDMI_PLL_PAR1                0x0024
#define HDMIRX_DWC_HDMI_PLL_PAR2                0x0028
#define HDMIRX_DWC_HDMI_PLL_PAR3                0x002C
#define HDMIRX_DWC_HDMI_PLL_LCK_STS             0x0030
#define HDMIRX_DWC_HDMI_CLK_CTRL                0x0034
#define HDMIRX_DWC_HDMI_PCB_CTRL                0x0038
#define HDMIRX_DWC_HDMI_PHS_CTRL                0x0040
#define HDMIRX_DWC_HDMI_PHS_USD                 0x0044
#define HDMIRX_DWC_HDMI_MISC_CTRL               0x0048
#define HDMIRX_DWC_HDMI_EQOFF_CTRL              0x004C
#define HDMIRX_DWC_HDMI_EQGAIN_CTRL             0x0050
#define HDMIRX_DWC_HDMI_EQCAL_STS               0x0054
#define HDMIRX_DWC_HDMI_EQRESULT                0x0058
#define HDMIRX_DWC_HDMI_EQ_MEAS_CTRL            0x005C
#define HDMIRX_DWC_HDMI_MODE_RECOVER            0x0080
#define HDMIRX_DWC_HDMI_ERROR_PROTECT           0x0084
#define HDMIRX_DWC_HDMI_ERD_STS                 0x0088
#define HDMIRX_DWC_HDMI_SYNC_CTRL               0x0090
#define HDMIRX_DWC_HDMI_CKM_EVLTM               0x0094
#define HDMIRX_DWC_HDMI_CKM_F                   0x0098
#define HDMIRX_DWC_HDMI_CKM_RESULT              0x009C
#define HDMIRX_DWC_HDMI_RESMPL_CTRL             0x00A4
#define HDMIRX_DWC_HDMI_DCM_CTRL                0x00A8
#define HDMIRX_DWC_HDMI_VM_CFG_CH_0_1           0x00B0
#define HDMIRX_DWC_HDMI_VM_CFG_CH2              0x00B4
#define HDMIRX_DWC_HDMI_SPARE                   0x00B8
#define HDMIRX_DWC_HDMI_STS                     0x00BC
#define HDMIRX_DWC_HDCP_CTRL                    0x00C0
#define HDMIRX_DWC_HDCP_SETTINGS                0x00C4
#define HDMIRX_DWC_HDCP_SEED                    0x00C8
#define HDMIRX_DWC_HDCP_BKSV1                   0x00CC
#define HDMIRX_DWC_HDCP_BKSV0                   0x00D0
#define HDMIRX_DWC_HDCP_KIDX                    0x00D4
#define HDMIRX_DWC_HDCP_KEY1                    0x00D8
#define HDMIRX_DWC_HDCP_KEY0                    0x00DC
#define HDMIRX_DWC_HDCP_DBG                     0x00E0
#define HDMIRX_DWC_HDCP_AKSV1                   0x00E4
#define HDMIRX_DWC_HDCP_AKSV0                   0x00E8
#define HDMIRX_DWC_HDCP_AN1                     0x00EC
#define HDMIRX_DWC_HDCP_AN0                     0x00F0
#define HDMIRX_DWC_HDCP_EESS_WOO                0x00F4
#define HDMIRX_DWC_HDCP_I2C_TIMEOUT             0x00F8
#define HDMIRX_DWC_HDCP_STS                     0x00FC
#define HDMIRX_DWC_MD_HCTRL1                    0x0140
#define HDMIRX_DWC_MD_HCTRL2                    0x0144
#define HDMIRX_DWC_MD_HT0                       0x0148
#define HDMIRX_DWC_MD_HT1                       0x014C
#define HDMIRX_DWC_MD_HACT_PX                   0x0150
#define HDMIRX_DWC_MD_HACT_PXA                  0x0154
#define HDMIRX_DWC_MD_VCTRL                     0x0158
#define HDMIRX_DWC_MD_VSC                       0x015C
#define HDMIRX_DWC_MD_VTC                       0x0160
#define HDMIRX_DWC_MD_VOL                       0x0164
#define HDMIRX_DWC_MD_VAL                       0x0168
#define HDMIRX_DWC_MD_VTH                       0x016C
#define HDMIRX_DWC_MD_VTL                       0x0170
#define HDMIRX_DWC_MD_IL_CTRL                   0x0174
#define HDMIRX_DWC_MD_IL_SKEW                   0x0178
#define HDMIRX_DWC_MD_IL_POL                    0x017C
#define HDMIRX_DWC_MD_STS                       0x0180
#define HDMIRX_DWC_AUD_CTRL                     0x0200
#define HDMIRX_DWC_AUD_PLL_CTRL                 0x0208
#define HDMIRX_DWC_AUD_PLL_LOCK                 0x020C
#define HDMIRX_DWC_AUD_PLL_RESET                0x0210
#define HDMIRX_DWC_AUD_CLK_CTRL                 0x0214
#define HDMIRX_DWC_AUD_CLK_MASP                 0x0218
#define HDMIRX_DWC_AUD_CLK_MAUD                 0x021C
#define HDMIRX_DWC_AUD_FILT_CTRL1               0x0220
#define HDMIRX_DWC_AUD_FILT_CTRL2               0x0224
#define HDMIRX_DWC_AUD_CTS_MAN                  0x0228
#define HDMIRX_DWC_AUD_N_MAN                    0x022C
#define HDMIRX_DWC_AUD_CLK_STS                  0x023C
#define HDMIRX_DWC_AUD_FIFO_CTRL                0x0240
#define HDMIRX_DWC_AUD_FIFO_TH                  0x0244
#define HDMIRX_DWC_AUD_FIFO_FILL_S              0x0248
#define HDMIRX_DWC_AUD_FIFO_CLR_MM              0x024C
#define HDMIRX_DWC_AUD_FIFO_FILLSTS             0x0250
#define HDMIRX_DWC_AUD_CHEXTR_CTRL              0x0254
#define HDMIRX_DWC_AUD_MUTE_CTRL                0x0258
#define HDMIRX_DWC_AUD_SAO_CTRL                 0x0260
#define HDMIRX_DWC_AUD_PAO_CTRL                 0x0264
#define HDMIRX_DWC_AUD_SPARE                    0x0268
#define HDMIRX_DWC_AUD_FIFO_STS                 0x027C
#define HDMIRX_DWC_AUDPLL_GEN_CTS               0x0280
#define HDMIRX_DWC_AUDPLL_GEN_N                 0x0284
#define HDMIRX_DWC_AUDPLL_GEN_CTRL_RW1          0x0288
#define HDMIRX_DWC_AUDPLL_GEN_CTRL_RW2          0x028C
#define HDMIRX_DWC_AUDPLL_GEN_CTRL_W1           0x0298
#define HDMIRX_DWC_AUDPLL_GEN_STS_RO1           0x02A0
#define HDMIRX_DWC_AUDPLL_GEN_STS_RO2           0x02A4
#define HDMIRX_DWC_AUDPLL_SC_CTS                0x02AC
#define HDMIRX_DWC_AUDPLL_SC_N                  0x02B0
#define HDMIRX_DWC_AUDPLL_SC_CTRL               0x02B4
#define HDMIRX_DWC_AUDPLL_SC_STS1               0x02B8
#define HDMIRX_DWC_AUDPLL_SC_STS2               0x02BC
#define HDMIRX_DWC_SNPS_PHYG3_CTRL              0x02C0
#define HDMIRX_DWC_I2CM_PHYG3_SLAVE             0x02C4
#define HDMIRX_DWC_I2CM_PHYG3_ADDRESS           0x02C8
#define HDMIRX_DWC_I2CM_PHYG3_DATAO             0x02CC
#define HDMIRX_DWC_I2CM_PHYG3_DATAI             0x02D0
#define HDMIRX_DWC_I2CM_PHYG3_OPERATION         0x02D4
#define HDMIRX_DWC_I2CM_PHYG3_MODE              0x02D8
#define HDMIRX_DWC_I2CM_PHYG3_SOFTRST           0x02DC
#define HDMIRX_DWC_I2CM_PHYG3_SS_CNTS           0x02E0
#define HDMIRX_DWC_I2CM_PHYG3_FS_HCNT           0x02E4
#define HDMIRX_DWC_PDEC_CTRL                    0x0300
#define HDMIRX_DWC_PDEC_FIFO_CFG                0x0304
#define HDMIRX_DWC_PDEC_FIFO_STS                0x0308
#define HDMIRX_DWC_PDEC_FIFO_DATA               0x030C
#define HDMIRX_DWC_PDEC_DBG_CTRL                0x0310
#define HDMIRX_DWC_PDEC_DBG_TMAX                0x0314
#define HDMIRX_DWC_PDEC_DBG_CTS                 0x0318
#define HDMIRX_DWC_PDEC_DBG_ACP                 0x031C
#define HDMIRX_DWC_PDEC_DBG_ERR_CORR            0x0320
#define HDMIRX_DWC_PDEC_FIFO_STS1               0x0324
#define HDMIRX_DWC_PDEC_ACRM_CTRL               0x0330
#define HDMIRX_DWC_PDEC_ACRM_MAX                0x0334
#define HDMIRX_DWC_PDEC_ACRM_MIN                0x0338
#define HDMIRX_DWC_PDEC_ASP_CTRL                0x0340
#define HDMIRX_DWC_PDEC_ASP_ERR                 0x0344
#define HDMIRX_DWC_PDEC_STS                     0x0360
#define HDMIRX_DWC_PDEC_GCP_AVMUTE              0x0380
#define HDMIRX_DWC_PDEC_ACR_CTS                 0x0390
#define HDMIRX_DWC_PDEC_ACR_N                   0x0394
#define HDMIRX_DWC_PDEC_AVI_HB                  0x03A0
#define HDMIRX_DWC_PDEC_AVI_PB                  0x03A4
#define HDMIRX_DWC_PDEC_AVI_TBB                 0x03A8
#define HDMIRX_DWC_PDEC_AVI_LRB                 0x03AC
#define HDMIRX_DWC_PDEC_AIF_CTRL                0x03C0
#define HDMIRX_DWC_PDEC_AIF_HB                  0x03C4
#define HDMIRX_DWC_PDEC_AIF_PB0                 0x03C8
#define HDMIRX_DWC_PDEC_AIF_PB1                 0x03CC
#define HDMIRX_DWC_PDEC_GMD_HB                  0x03D0
#define HDMIRX_DWC_PDEC_GMD_PB                  0x03D4
#define HDMIRX_DWC_DUMMY_IP_REG                 0x0F00
#define HDMIRX_DWC_PDEC_IEN_CLR                 0x0F78
#define HDMIRX_DWC_PDEC_IEN_SET                 0x0F7C
#define HDMIRX_DWC_PDEC_ISTS                    0x0F80
#define HDMIRX_DWC_PDEC_IEN                     0x0F84
#define HDMIRX_DWC_PDEC_ICLR                    0x0F88
#define HDMIRX_DWC_PDEC_ISET                    0x0F8C
#define HDMIRX_DWC_AUD_CLK_IEN_CLR              0x0F90
#define HDMIRX_DWC_AUD_CLK_IEN_SET              0x0F94
#define HDMIRX_DWC_AUD_CLK_ISTS                 0x0F98
#define HDMIRX_DWC_AUD_CLK_IEN                  0x0F9C
#define HDMIRX_DWC_AUD_CLK_ICLR                 0x0FA0
#define HDMIRX_DWC_AUD_CLK_ISET                 0x0FA4
#define HDMIRX_DWC_AUD_FIFO_IEN_CLR             0x0FA8
#define HDMIRX_DWC_AUD_FIFO_IEN_SET             0x0FAC
#define HDMIRX_DWC_AUD_FIFO_ISTS                0x0FB0
#define HDMIRX_DWC_AUD_FIFO_IEN                 0x0FB4
#define HDMIRX_DWC_AUD_FIFO_ICLR                0x0FB8
#define HDMIRX_DWC_AUD_FIFO_ISET                0x0FBC
#define HDMIRX_DWC_MD_IEN_CLR                   0x0FC0
#define HDMIRX_DWC_MD_IEN_SET                   0x0FC4
#define HDMIRX_DWC_MD_ISTS                      0x0FC8
#define HDMIRX_DWC_MD_IEN                       0x0FCC
#define HDMIRX_DWC_MD_ICLR                      0x0FD0
#define HDMIRX_DWC_MD_ISET                      0x0FD4
#define HDMIRX_DWC_HDMI_IEN_CLR                 0x0FD8
#define HDMIRX_DWC_HDMI_IEN_SET                 0x0FDC
#define HDMIRX_DWC_HDMI_ISTS                    0x0FE0
#define HDMIRX_DWC_HDMI_IEN                     0x0FE4
#define HDMIRX_DWC_HDMI_ICLR                    0x0FE8
#define HDMIRX_DWC_HDMI_ISET                    0x0FEC
#define HDMIRX_DWC_DMI_SW_RST                   0x0FF0
#define HDMIRX_DWC_DMI_DISABLE_IF               0x0FF4
#define HDMIRX_DWC_DMI_MODULE_ID                0x0FFC
#define HDMIRX_DWC_SPARE_REGISTER_5             0x0600
#define HDMIRX_DWC_SPARE_REGISTER_4             0x0604
#define HDMIRX_DWC_SPARE_REGISTER_3             0x0608
#define HDMIRX_DWC_SPARE_REGISTER_2             0x060C
#define HDMIRX_DWC_SPARE_REGISTER_1             0x0610

// CEC Controller registers addresses
#define HDMIRX_DWC_CEC_CTRL                     0x1F00
#define HDMIRX_DWC_CEC_STAT                     0x1F04
#define HDMIRX_DWC_CEC_MASK                     0x1F08
#define HDMIRX_DWC_CEC_POLARITY                 0x1F0C
#define HDMIRX_DWC_CEC_INT                      0x1F10
#define HDMIRX_DWC_CEC_ADDR_L                   0x1F14
#define HDMIRX_DWC_CEC_ADDR_H                   0x1F18
#define HDMIRX_DWC_CEC_TX_CNT                   0x1F1C
#define HDMIRX_DWC_CEC_RX_CNT                   0x1F20
#define HDMIRX_DWC_CEC_TX_DATA0                 0x1F40
#define HDMIRX_DWC_CEC_TX_DATA1                 0x1F44
#define HDMIRX_DWC_CEC_TX_DATA2                 0x1F48
#define HDMIRX_DWC_CEC_TX_DATA3                 0x1F4C
#define HDMIRX_DWC_CEC_TX_DATA4                 0x1F50
#define HDMIRX_DWC_CEC_TX_DATA5                 0x1F54
#define HDMIRX_DWC_CEC_TX_DATA6                 0x1F58
#define HDMIRX_DWC_CEC_TX_DATA7                 0x1F5C
#define HDMIRX_DWC_CEC_TX_DATA8                 0x1F60
#define HDMIRX_DWC_CEC_TX_DATA9                 0x1F64
#define HDMIRX_DWC_CEC_TX_DATA10                0x1F68
#define HDMIRX_DWC_CEC_TX_DATA11                0x1F6C
#define HDMIRX_DWC_CEC_TX_DATA12                0x1F70
#define HDMIRX_DWC_CEC_TX_DATA13                0x1F74
#define HDMIRX_DWC_CEC_TX_DATA14                0x1F78
#define HDMIRX_DWC_CEC_TX_DATA15                0x1F7C
#define HDMIRX_DWC_CEC_RX_DATA0                 0x1F80
#define HDMIRX_DWC_CEC_RX_DATA1                 0x1F84
#define HDMIRX_DWC_CEC_RX_DATA2                 0x1F88
#define HDMIRX_DWC_CEC_RX_DATA3                 0x1F8C
#define HDMIRX_DWC_CEC_RX_DATA4                 0x1F90
#define HDMIRX_DWC_CEC_RX_DATA5                 0x1F94
#define HDMIRX_DWC_CEC_RX_DATA6                 0x1F98
#define HDMIRX_DWC_CEC_RX_DATA7                 0x1F9C
#define HDMIRX_DWC_CEC_RX_DATA8                 0x1FA0
#define HDMIRX_DWC_CEC_RX_DATA9                 0x1FA4
#define HDMIRX_DWC_CEC_RX_DATA10                0x1FA8
#define HDMIRX_DWC_CEC_RX_DATA11                0x1FAC
#define HDMIRX_DWC_CEC_RX_DATA12                0x1FB0
#define HDMIRX_DWC_CEC_RX_DATA13                0x1FB4
#define HDMIRX_DWC_CEC_RX_DATA14                0x1FB8
#define HDMIRX_DWC_CEC_RX_DATA15                0x1FBC
#define HDMIRX_DWC_CEC_LOCK                     0x1FC0
#define HDMIRX_DWC_CEC_WKUPCTRL                 0x1FC4

//// DWC_HDMI1.3_RX_Controller registers default valus
//#define DEFAULT_RX_HDMI_SETUP_CTRL      {6'd0, 1'b1, 1'b1, 6'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 6'd24, 1'b0, 1'b0, 4'd0, 1'b0}
//#define DEFAULT_RX_HDMI_OVR_CTRL        {1'b0, 3'd0, 2'd0, 1'b0, 3'd0, 1'b0, 1'b0, 3'd0, 1'b0, 2'd0, 3'd0, 3'd0, 7'd0, 1'b0}
//#define DEFAULT_RX_HDMI_TIMER_CTRL      {21'd0, 1'b0, 10'd632}
//#define DEFAULT_RX_HDMI_RES_OVR         {4'd0, 4'd5, 1'b0, 7'd0, 1'b0, 6'd0, 1'b0, 1'b0, 1'b0, 6'd0}
//#define DEFAULT_RX_HDMI_RES_STS         {1'b0, 7'd0, 1'b0, 6'd0, 1'b0, 8'd0, 8'd0}
//#define DEFAULT_RX_HDMI_PLL_CTRL        {8'd0, 8'd0, 4'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 2'd2, 1'b1, 1'b0, 2'd0, 1'b0}
//#define DEFAULT_RX_HDMI_PLL_FRQSET1     {8'd114, 8'd82, 8'd49, 8'd33}
//#define DEFAULT_RX_HDMI_PLL_FRQSET2     {1'b0, 3'd4, 4'd0, 8'd212, 8'd179, 8'd147}
//#define DEFAULT_RX_HDMI_PLL_PAR1        {4'd12, 4'd6, 4'd15, 4'd5, 4'd15, 4'd8, 4'd9, 4'd4}
//#define DEFAULT_RX_HDMI_PLL_PAR2        {4'd7, 4'd8, 4'd8, 4'd7, 4'd9, 4'd6, 4'd10, 4'd6}
//#define DEFAULT_RX_HDMI_PLL_PAR3        {1'b0, 3'd7, 1'b1, 3'd6, 1'b1, 3'd4, 1'b1, 3'd3, 1'b1, 3'd2, 1'b1, 3'd1, 1'b1, 3'd1, 1'b1, 3'd0}
//#define DEFAULT_RX_HDMI_PLL_LCK_STS     {18'd0, 4'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_CLK_CTRL        {11'd0, 5'd0, 2'd0, 5'd0, 3'd2, 3'd2, 2'd0, 1'b0}
//#define DEFAULT_RX_HDMI_PCB_CTRL        {6'd0, 1'b0, 1'b0, 4'd0, 2'd0, 1'b0, 1'b0, 3'd0, 1'b0, 1'b0, 3'd0, 3'd0, 1'b0, 1'b0, 3'd0}
//#define DEFAULT_RX_HDMI_PHS_CTRL        {3'd0, 3'd0, 3'd0, 1'b0, 6'd42, 2'd0, 2'd1, 1'b0, 1'b0, 1'b1, 1'b1, 3'd0, 1'b1, 2'd0, 2'd2}
//#define DEFAULT_RX_HDMI_PHS_USD         {5'd0, 1'b0, 1'b0, 1'b0, 2'd0, 6'd0, 2'd0, 6'd0, 2'd0, 6'd0}
//#define DEFAULT_RX_HDMI_MISC_CTRL       {2'd0, 3'd0, 2'd0, 1'b0, 7'd0, 1'b0, 4'd0, 1'b0, 1'b0, 1'b0, 1'b1, 7'd0, 1'b0}
//#define DEFAULT_RX_HDMI_EQOFF_CTRL      {13'd0, 3'd0, 2'd0, 1'b0, 4'd0, 4'd0, 4'd0, 1'b0}
//#define DEFAULT_RX_HDMI_EQGAIN_CTRL     {8'd0, 1'b0, 3'd4, 3'd4, 3'd4, 1'b0, 1'b0, 1'b0, 2'd1, 2'd2, 1'b0, 1'b0, 1'b0, 3'd4, 1'b0}
//#define DEFAULT_RX_HDMI_EQCAL_STS       {2'd0, 4'd0, 4'd0, 4'd0, 1'b0, 1'b0, 1'b0, 3'd0, 3'd0, 3'd0, 3'd0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_EQRESULT        {9'd0, 12'd0, 11'd0}
//#define DEFAULT_RX_HDMI_EQ_MEAS_CTRL    {12'd0, 1'b0, 1'b0, 1'b0, 1'b0, 16'd0}
//#define DEFAULT_RX_HDMI_MODE_RECOVER    {6'd0, 2'd0, 5'd0, 1'b0, 5'd8, 5'd8, 2'd0, 2'd0, 2'd0, 2'd0}
//#define DEFAULT_RX_HDMI_ERROR_PROTECT   {11'd0, 1'b0, 1'b0, 3'd0, 2'd0, 2'd0, 2'd0, 2'd0, 2'd0, 1'b0, 2'd0, 3'd0}
//#define DEFAULT_RX_HDMI_ERD_STS         {29'd0, 3'd0}
//#define DEFAULT_RX_HDMI_SYNC_CTRL       {27'd0, 2'd0, 2'd0, 1'b0}
//#define DEFAULT_RX_HDMI_CKM_EVLTM       {10'd0, 2'd1, 1'b0, 3'd0, 12'd4095, 3'd0, 1'b0}
//#define DEFAULT_RX_HDMI_CKM_F           {16'd63882, 16'd9009}
//#define DEFAULT_RX_HDMI_CKM_RESULT      {14'd0, 1'b0, 1'b0, 16'd0}
//#define DEFAULT_RX_HDMI_RESMPL_CTRL     {27'd0, 4'd0, 1'b1}
//#define DEFAULT_RX_HDMI_DCM_CTRL        {3'd0, 1'b0, 8'd0, 2'd0, 1'b0, 4'd0, 1'b0, 4'd4, 2'd0, 4'd5, 2'd0}
//#define DEFAULT_RX_HDMI_VM_CFG_CH_0_1   {16'd0, 16'd0}
//#define DEFAULT_RX_HDMI_VM_CFG_CH2      {15'd0,  1'b0, 16'd0}
//#define DEFAULT_RX_HDMI_SPARE           32'd0
//#define DEFAULT_RX_HDMI_STS             {4'd0, 4'd0, 8'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0}
//#define DEFAULT_RX_HDCP_CTRL            {17'd0, 1'b0, 1'b0, 1'b0, 2'd0, 2'd0, 2'd0, 3'd0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDCP_SETTINGS        {13'd0, 2'd1, 1'b1, 3'd0, 1'b1, 2'd0, 1'b1, 1'b1, 7'd58, 1'b0}
//#define DEFAULT_RX_HDCP_SEED            {16'd0, 16'd0}
//#define DEFAULT_RX_HDCP_BKSV1           {24'd0, 8'd0}
//#define DEFAULT_RX_HDCP_BKSV0           32'd0
//#define DEFAULT_RX_HDCP_KIDX            {26'd0, 6'd0}
//#define DEFAULT_RX_HDCP_KEY1            {8'd0, 24'd0}
//#define DEFAULT_RX_HDCP_KEY0            32'd0
//#define DEFAULT_RX_HDCP_DBG             {8'd0, 1'b0, 1'b0, 6'd0, 16'd0}
//#define DEFAULT_RX_HDCP_AKSV1           {24'd0, 8'd0}
//#define DEFAULT_RX_HDCP_AKSV0           32'd0
//#define DEFAULT_RX_HDCP_AN1             32'd0
//#define DEFAULT_RX_HDCP_AN0             32'd0
//#define DEFAULT_RX_HDCP_EESS_WOO        {6'd0, 10'd534, 6'd0, 10'd511}
//#define DEFAULT_RX_HDCP_I2C_TIMEOUT     32'd0
//#define DEFAULT_RX_HDCP_STS             {31'd0, 1'b1}
//#define DEFAULT_RX_MD_HCTRL1            {21'd0, 3'd1, 2'd0, 1'b0, 1'b1, 4'd0}
//#define DEFAULT_RX_MD_HCTRL2            {17'd0, 3'd2, 1'b0, 3'd1, 2'd0, 1'b0, 2'd0, 1'b0, 2'd2}
//#define DEFAULT_RX_MD_HT0               {16'd0, 7'd0, 9'd0}
//#define DEFAULT_RX_MD_HT1               {4'd0, 12'd0, 4'd0, 12'd0}
//#define DEFAULT_RX_MD_HACT_PX           {20'd0, 12'd0}
//#define DEFAULT_RX_MD_HACT_PXA          {20'd0, 12'd0}
//#define DEFAULT_RX_MD_VCTRL             {27'd0, 1'b0, 2'd0, 1'b1, 1'b0}
//#define DEFAULT_RX_MD_VSC               {16'd0, 16'd0}
//#define DEFAULT_RX_MD_VTC               {10'd0, 22'd0}
//#define DEFAULT_RX_MD_VOL               {21'd0, 11'd0}
//#define DEFAULT_RX_MD_VAL               {21'd0, 11'd0}
//#define DEFAULT_RX_MD_VTH               {20'd0, 2'd2, 2'd0, 2'd0, 3'd2, 3'd2}
//#define DEFAULT_RX_MD_VTL               {21'd0, 11'd0}
//#define DEFAULT_RX_MD_IL_CTRL           {31'd0, 1'b0}
//#define DEFAULT_RX_MD_IL_SKEW           {25'd0, 3'd0, 1'b0, 3'd0}
//#define DEFAULT_RX_MD_IL_POL            {29'd0, 1'b0, 2'd0}
//#define DEFAULT_RX_MD_STS               {28'd0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CTRL             {29'd0, 2'd3, 1'b0}
//#define DEFAULT_RX_AUD_PLL_CTRL         {1'b0, 3'd0, 4'd6, 4'd3, 4'd8, 1'b0, 1'b0, 1'b1, 1'b0, 12'd0}
//#define DEFAULT_RX_AUD_PLL_LOCK         {16'd0, 16'd128}
//#define DEFAULT_RX_AUD_PLL_RESET        {1'b1, 31'd14331160}
//#define DEFAULT_RX_AUD_CLK_CTRL         {24'd0, 1'b0, 1'b0, 1'b0, 1'b0, 2'd0, 1'b1, 1'b1}
//#define DEFAULT_RX_AUD_CLK_MASP         {12'd0, 20'd3855}
//#define DEFAULT_RX_AUD_CLK_MAUD         {12'd0, 20'd61440}
//#define DEFAULT_RX_AUD_FILT_CTRL1       {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FILT_CTRL2       {5'd0, 5'd0, 2'd3, 10'd0, 10'd0}
//#define DEFAULT_RX_AUD_CTS_MAN          {12'd0, 20'd0}
//#define DEFAULT_RX_AUD_N_MAN            {12'd0, 20'd0}
//#define DEFAULT_RX_AUD_CLK_STS          {31'd0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_CTRL        {13'd0, 2'd0, 1'b1, 15'd0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_TH          {5'd0, 9'd144, 9'd32, 9'd32}
//#define DEFAULT_RX_AUD_FIFO_FILL_S      32'd0
//#define DEFAULT_RX_AUD_FIFO_CLR_MM      {31'd0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_FILLSTS     {5'd0, 9'd0, 9'd0, 9'd0}
//#define DEFAULT_RX_AUD_CHEXTR_CTRL      {24'd0, 1'b0, 5'd0, 2'd0}
//#define DEFAULT_RX_AUD_MUTE_CTRL        {9'd0, 2'd0, 2'd0, 2'd0, 7'd48, 2'd0, 1'b1, 2'd3, 2'd3, 2'd0, 1'b0}
//#define DEFAULT_RX_AUD_SAO_CTRL         {21'd0, 1'b1, 1'b1, 4'd15, 4'd15, 1'b1}
//#define DEFAULT_RX_AUD_PAO_CTRL         {20'd0, 8'd176, 1'b0, 2'd0, 1'b0}
//#define DEFAULT_RX_AUD_SPARE            32'd0
//#define DEFAULT_RX_AUD_FIFO_STS         {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_CTRL            {23'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 3'd0, 1'b0}
//#define DEFAULT_RX_PDEC_FIFO_CFG        {2'd0, 10'd32, 10'd32, 10'd32}
//#define DEFAULT_RX_PDEC_FIFO_STS        {16'd0, 16'd0}
//#define DEFAULT_RX_PDEC_FIFO_DATA       32'd0
//#define DEFAULT_RX_PDEC_DBG_CTRL        {15'd0, 1'b0, 7'd0, 1'b0, 8'd0}
//#define DEFAULT_RX_PDEC_DBG_TMAX        {8'd0, 24'd0}
//#define DEFAULT_RX_PDEC_DBG_CTS         {16'd0, 8'd0, 8'd0}
//#define DEFAULT_RX_PDEC_DBG_ACP         {16'd0, 8'd0, 8'd0}
//#define DEFAULT_RX_PDEC_DBG_ERR_CORR    {8'd0, 8'd2, 7'd0, 1'b0, 8'd1}
//#define DEFAULT_RX_PDEC_FIFO_STS1       {16'd0, 16'd0}
//#define DEFAULT_RX_PDEC_ACRM_CTRL       {27'd0, 3'd0, 2'd1}
//#define DEFAULT_RX_PDEC_ACRM_MAX        {12'd0, 20'd0}
//#define DEFAULT_RX_PDEC_ACRM_MIN        {8'd0, 4'd0, 20'd0}
//#define DEFAULT_RX_PDEC_ASP_CTRL        {25'd0, 1'b1, 4'd0, 2'd0}
//#define DEFAULT_RX_PDEC_ASP_ERR         {16'd0, 16'd0}
//#define DEFAULT_RX_PDEC_STS             {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_GCP_AVMUTE      {19'd0, 1'b0, 4'd0, 4'd0, 2'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_ACR_CTS         {12'd0, 20'd0}
//#define DEFAULT_RX_PDEC_ACR_N           {12'd0, 20'd0}
//#define DEFAULT_RX_PDEC_AVI_HB          {4'd0, 4'd0, 8'd0, 3'd0, 5'd0, 8'd0}
//#define DEFAULT_RX_PDEC_AVI_PB          {1'b0, 7'd0, 1'b0, 3'd0, 2'd0, 2'd0, 2'd0, 2'd0, 4'd0, 1'b0, 2'd0, 1'b0, 2'd0, 2'd0}
//#define DEFAULT_RX_PDEC_AVI_TBB         {16'd0, 16'd0}
//#define DEFAULT_RX_PDEC_AVI_LRB         {16'd0, 16'd0}
//#define DEFAULT_RX_PDEC_AIF_CTRL        {23'd0, 1'b0, 8'd0}
//#define DEFAULT_RX_PDEC_AIF_HB          {8'd0, 8'd0, 3'd0, 5'd0, 8'd0}
//#define DEFAULT_RX_PDEC_AIF_PB0         {8'd0, 8'd0, 3'd0, 3'd0, 2'd0, 4'd0, 1'b0, 3'd0}
//#define DEFAULT_RX_PDEC_AIF_PB1         {24'd0, 1'b0, 4'd0, 3'd0}
//#define DEFAULT_RX_PDEC_GMD_HB          {16'd0, 1'b0, 1'b0, 2'd0, 4'd0, 1'b0, 3'd0, 4'd0}
//#define DEFAULT_RX_PDEC_GMD_PB          {8'd0, 8'd0, 16'd0}
//#define DEFAULT_RX_DUMMY_IP_REG         32'd0
//#define DEFAULT_RX_PDEC_IEN_CLR         {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_IEN_SET         {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_ISTS            {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_IEN             {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_ICLR            {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_PDEC_ISET            {3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 7'd0, 1'b0, 3'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_IEN_CLR      {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_IEN_SET      {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_ISTS         {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_IEN          {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_ICLR         {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_CLK_ISET         {30'd0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_IEN_CLR     {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_IEN_SET     {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_ISTS        {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_IEN         {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_ICLR        {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUD_FIFO_ISET        {27'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_IEN_CLR           {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_IEN_SET           {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_ISTS              {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_IEN               {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_ICLR              {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_MD_ISET              {20'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_IEN_CLR         {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_IEN_SET         {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_ISTS            {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_IEN             {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_ICLR            {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_HDMI_ISET            {1'b0, 6'd0, 1'b0, 7'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_DMI_SW_RST           {31'd0, 1'b0}
//#define DEFAULT_RX_DMI_DISABLE_IF       {31'd0, 1'b0}
//#define DEFAULT_RX_DMI_MODULE_ID        {16'd41154, 4'd5, 4'd0, 8'd0}
//
//#define DEFAULT_RX_AUDPLL_GEN_CTS       32'd0
//#define DEFAULT_RX_AUDPLL_GEN_N         32'd0
//#define DEFAULT_RX_AUDPLL_GEN_CTRL_RW1  32'd0
//#define DEFAULT_RX_AUDPLL_GEN_CTRL_RW2  32'd0
//#define DEFAULT_RX_AUDPLL_GEN_CTRL_W1   32'd0
//
//#define DEFAULT_RX_AUDPLL_SC_CTS        {12'd0, 20'd0}
//#define DEFAULT_RX_AUDPLL_SC_N          {12'd0, 20'd0}
//#define DEFAULT_RX_AUDPLL_SC_CTRL       {25'd0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0}
//#define DEFAULT_RX_AUDPLL_SC_STS1       {32'd0}
//#define DEFAULT_RX_AUDPLL_SC_STS2       {32'd0}
//
//#define DEFAULT_RX_SPARE_REGISTER_5   32'd0
//#define DEFAULT_RX_SPARE_REGISTER_4   32'd0
//#define DEFAULT_RX_SPARE_REGISTER_3   32'd0
//#define DEFAULT_RX_SPARE_REGISTER_2   32'd0
//#define DEFAULT_RX_SPARE_REGISTER_1   32'd0
//
//// CEC Controller registers default values
//#define  DEFAULT_RX_CEC_CTRL            {24'd0, 8'h02}
//#define  DEFAULT_RX_CEC_STAT            {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_MASK            {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_POLARITY        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_INT             {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_ADDR_L          {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_ADDR_H          {24'd0, 8'h80}
//#define  DEFAULT_RX_CEC_TX_CNT          {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_CNT          {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA0        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA1        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA2        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA3        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA4        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA5        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA6        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA7        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA8        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA9        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA10       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA11       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA12       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA13       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA14       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_TX_DATA15       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA0        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA1        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA2        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA3        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA4        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA5        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA6        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA7        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA8        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA9        {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA10       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA11       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA12       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA13       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA14       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_RX_DATA15       {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_LOCK            {24'd0, 8'h00}
//#define  DEFAULT_RX_CEC_WKUPCTRL        {24'd0, 8'hff}

#endif /* HDMIRX_PARAMETER_DEFINE_H */
