#include "hdmi.h"
#include "hdmirx.h"
#include "hdmirx_parameter.h"
#include "test_prm.h"
void hdmi_tx_hpd_detect(void);

// -----------------------------------------------
// Global variables
// -----------------------------------------------
unsigned char   vdin0_field_n               = 0;
unsigned char   vdin1_field_n               = 0;
unsigned char   viu_field_n                 = 0;

unsigned char   vid_colour_depth_chg_1st    = 1;
unsigned char   hdmi_pll_lock               = 0;
unsigned char   hdmi_mode                   = 0;
unsigned long   recv_acr_packet_cnt         = 0;
unsigned char   vsAct_1st                   = 1;
unsigned char   vactLin_1st                 = 1;
unsigned char   vtotLin_1st                 = 1;
unsigned char   htot32_1st                  = 1;
unsigned char   gcp_rcv                     = 0;
unsigned long   hs_clk_cnt                  = 0;
unsigned long   hactPix_cnt                 = 0;
unsigned long   vofsLin_cnt                 = 0;
unsigned char   recv_avi                    = 0;
unsigned char   edid_addr_intr_num          = 0;

unsigned long   curr_pdec_ien_maskn         = 0;
unsigned long   curr_aud_clk_ien_maskn      = 0;
unsigned long   curr_aud_fifo_ien_maskn     = 0;
unsigned long   curr_md_ien_maskn           = 0;
unsigned long   curr_hdmi_ien_maskn         = 0;

unsigned char   aud_clk_stable              = 0;

// --------------------------------------------------------
//                     C_Entry
// --------------------------------------------------------
void test(void) 
{
    unsigned long   data32;
    unsigned char   divisor_i2s = 0;    // aoclk    = amclk / 1
    unsigned char   divisor_958 = 0;    // clk958   = amclk / 1

    // --------------------------------------------------------
    // Set Clocks
    // --------------------------------------------------------
    stimulus_print("[TEST.C] Set clock\n");

    // --------------------------------------------------------
    // Program core_pin_mux to enable HDMI pins
    // --------------------------------------------------------
	WRITE_CBUS_REG(PERIPHS_PIN_MUX_0 , READ_CBUS_REG(PERIPHS_PIN_MUX_0 )|
				( (1 << 27)   |   // pm_gpioW_0_hdmirx_5V_A  
				(1 << 26)   |   // pm_gpioW_1_hdmirx_HPD_A 
				(1 << 25)   |   // pm_gpioW_2_hdmirx_scl_A 
				(1 << 24)   |   // pm_gpioW_3_hdmirx_sda_A 
				(1 << 23)   |   // pm_gpioW_4_hdmirx_5V_B  
				(1 << 22)   |   // pm_gpioW_5_hdmirx_HPD_B 
				(1 << 21)   |   // pm_gpioW_6_hdmirx_scl_B 
				(1 << 20)   |   // pm_gpioW_7_hdmirx_sda_B 
				(1 << 19)   |   // pm_gpioW_8_hdmirx_5V_C  
				(1 << 18)   |   // pm_gpioW_9_hdmirx_HPD_C 
				(1 << 17)   |   // pm_gpioW_10_hdmirx_scl_C
				(1 << 16)   |   // pm_gpioW_11_hdmirx_sda_C
				(1 << 15)   |   // pm_gpioW_12_hdmirx_5V_D 
				(1 << 14)   |   // pm_gpioW_13_hdmirx_HPD_D
				(1 << 13)   |   // pm_gpioW_14_hdmirx_scl_D
				(1 << 12)   |   // pm_gpioW_15_hdmirx_sda_D
				(1 << 11)));     // pm_gpioW_16_hdmirx_cec  

	WRITE_CBUS_REG(PERIPHS_PIN_MUX_1 , READ_CBUS_REG(PERIPHS_PIN_MUX_1 )|
				( (1 << 2)    |   // pm_gpioW_17_hdmirx_tmds_clk
				(1 << 1)    |   // pm_gpioW_18_hdmirx_pix_clk 
				(1 << 0)));      // pm_gpioW_19_hdmirx_audmeas 
    
    // --------------------------------------------------------
    // Set up HDMI
    // --------------------------------------------------------
    hdmirx_test_function (  ACR_MODE,                   // Select which ACR scheme: 0=Analog PLL based ACR; 1=Digital ACR.
                            MANUAL_ACR_CTS,
                            MANUAL_ACR_N,
                            RX_8_CHANNEL,               // Audio channels: 0=2-channel; 1=4 x 2-channel.
                            EDID_EXTENSION_FLAG,        // Number of 128-bytes blocks that following the basic block
                            EDID_AUTO_CEC_ENABLE,       // 1=Automatic switch CEC ID depend on RX_PORT_SEL
                            EDID_CEC_ID_ADDR,           // EDID address offsets for storing 2-byte of Physical Address
                            EDID_CEC_ID_DATA,           // Physical Address: e.g. 0x1023 is 1.0.2.3
                            EDID_AUTO_CHECKSUM_ENABLE,  // Checksum byte selection: 0=Use data stored in MEM; 1=Use checksum calculated by HW.
                            EDID_CLK_DIVIDE_M1,         // EDID I2C clock = sysclk / (1+EDID_CLK_DIVIDE_M1).
                            HDCP_ON,
                            HDCP_KEY_DECRYPT_EN,
                            VIC,                        // Video format identification code
                            PIXEL_REPEAT_HDMI,
                            INTERLACE_MODE,             // 0=Progressive; 1=Interlace.
                            FRONT_PORCH,                // Number of pixels from DE Low to HSYNC high
                            BACK_PORCH,                 // Number of pixels from HSYNC low to DE high
                            HSYNC_PIXELS,               // Number of pixels of HSYNC pulse
                            HSYNC_POLARITY,             // TX HSYNC polarity: 0=low active; 1=high active.
                            SOF_LINES,                  // HSYNC count between VSYNC de-assertion and first line of active video
                            EOF_LINES,                  // HSYNC count between last line of active video and start of VSYNC
                            VSYNC_LINES,                // HSYNC count of VSYNC assertion
                            VSYNC_POLARITY,             // TX VSYNC polarity: 0=low active; 1=high active.
                            TOTAL_PIXELS,               // Number of total pixels per line
                            TOTAL_LINES,                // Number of total lines per frame
                            RX_INPUT_COLOR_FORMAT,      // Pixel format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.
                            RX_INPUT_COLOR_DEPTH,       // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
                            RX_HSCALE_HALF,             // 1=RX output video horizontally scaled by half, to reduce clock speed.
                            &curr_pdec_ien_maskn,
                            &curr_aud_clk_ien_maskn,
                            &curr_aud_fifo_ien_maskn,
                            &curr_md_ien_maskn,
                            &curr_hdmi_ien_maskn,
                            pdec_ien_maskn,
                            aud_clk_ien_maskn,
                            aud_fifo_ien_maskn,
                            md_ien_maskn,
                            hdmi_ien_maskn,
                            RX_PORT_SEL,                // Select HDMI RX input port: 0=PortA; 1=PortB; 2=PortC, 3=PortD; others=invalid.
                            HDMI_ARCTX_EN,              // Audio Return Channel (ARC) transmission block control:0=Disable; 1=Enable.
                            HDMI_ARCTX_MODE,            // ARC transmission mode: 0=Single-ended mode; 1=Common mode.
                            &hdmi_pll_lock);


    return;
}

