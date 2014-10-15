//------------------------------------------------------------------------------
// Video
//------------------------------------------------------------------------------

#define VIC                 6                       // Video format identification code: 720(1440)x480i@59.94/60Hz
#define INTERLACE_MODE      1                       // 0=Progressive; 1=Interlace.
#define PIXEL_REPEAT_HDMI   (2-1)                   // Pixel repeat factor seen by HDMI TX

//#define MODE_3D             0                       // Define it to enable 3D mode: 1=3D frame-packing; 2=3D side-by-side; 3=3D top-and-bottom.
#define ACTIVE_SPACE        0                       // For 3D: Number of lines inserted between two active video regions.

#define ACTIVE_PIXELS       (720*(1+PIXEL_REPEAT_HDMI)) // Number of active pixels per line.
#define ACTIVE_LINES        (480/(1+INTERLACE_MODE))    // Number of active lines per field.

#define LINES_F0            262                     // Number of lines in the even field.
#define LINES_F1            263                     // Number of lines in the odd field.

#define FRONT_PORCH         38                      // Number of pixels from DE Low to HSYNC high. 
#define HSYNC_PIXELS        124                     // Number of pixels of HSYNC pulse. 
#define BACK_PORCH          114                     // Number of pixels from HSYNC low to DE high.

#define EOF_LINES           4                       // HSYNC count between last line of active video and start of VSYNC 
                                                    // a.k.a. End of Field (EOF). In interlaced mode,
                                                    // HSYNC count will be eof_lines at the end of even field  
                                                    // and eof_lines+1 at the end of odd field.
#define VSYNC_LINES         3                       // HSYNC count of VSYNC assertion
                                                    // In interlaced mode VSYNC will be in-phase with HSYNC in the even field and 
                                                    // out-of-phase with HSYNC in the odd field.
#define SOF_LINES           15                      // HSYNC count between VSYNC de-assertion and first line of active video

#define HSYNC_POLARITY      1                       // TX HSYNC polarity: 0=low active; 1=high active.
#define VSYNC_POLARITY      1                       // TX VSYNC polarity: 0=low active; 1=high active.

#define TOTAL_FRAMES        1                       // Number of frames to run in simulation
//#define VIU_DISPLAY_ON                              // Define it to enable viu display

#define TX_INPUT_COLOR_DEPTH    0                       // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
#define TX_OUTPUT_COLOR_DEPTH   0                       // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
#define RX_INPUT_COLOR_DEPTH    TX_OUTPUT_COLOR_DEPTH   // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.

#define TX_INPUT_COLOR_FORMAT   1                   // Pixel format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
#define TX_OUTPUT_COLOR_FORMAT  1                   // Pixel format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
#define RX_INPUT_COLOR_FORMAT   ((TX_OUTPUT_COLOR_FORMAT==1)? 2 : (TX_OUTPUT_COLOR_FORMAT==3)? 1 : 0)   // Pixel format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.

#define TX_INPUT_COLOR_RANGE    0                   // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
#define TX_OUTPUT_COLOR_RANGE   0                   // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.

#define RX_HSCALE_HALF          0                   // 1=RX output video horizontally scaled by half, to reduce clock speed.

//------------------------------------------------------------------------------
// Audio
//------------------------------------------------------------------------------

#define TX_I2S_SPDIF        1                       // 0=SPDIF; 1=I2S. Note: Must select I2S if CHIP_HAVE_HDMI_RX is defined.
#define TX_I2S_8_CHANNEL    1                       // 0=I2S 2-channel; 1=I2S 4 x 2-channel.

#define RX_AO_SEL           2                       // Select HDMIRX audio output format: 0=SAO SPDIF; 1=SAO I2S; 2=PAO; 3=Audin decode SPDIF; 4=Audin decode I2S.
#define RX_8_CHANNEL        TX_I2S_8_CHANNEL        // 0=I2S 2-channel; 1=I2S 4 x 2-channel.

#define AUDIO_SAMPLE_RATE   7                       // 0=8kHz; 1=11.025kHz; 2=12kHz; 3=16kHz; 4=22.05kHz; 5=24kHz; 6=32kHz; 7=44.1kHz; 8=48kHz; 9=88.2kHz; 10=96kHz; 11=192kHz; 12=768kHz; Other=48kHz.
#define AUDIO_PACKET_TYPE   0                       // 0=audio sample packet; 1=one bit audio; 2=HBR audio packet; 3=DST audio packet.
#define EXP_AUDIO_LENGTH    2304                    // exp/i2s_data.exp file length

// For Audio Clock Recovery
#define ACR_MODE            0                       // Select which ACR scheme:
                                                    // 0=Analog PLL based ACR;
                                                    // 1=Digital ACR.

#define MANUAL_ACR_N        6272
#define MANUAL_ACR_CTS      ((RX_INPUT_COLOR_DEPTH==0)? 30000 : (RX_INPUT_COLOR_DEPTH==1)? 30000*5/4 : (RX_INPUT_COLOR_DEPTH==2)? 30000*3/2 : 30000*2)
#define EXPECT_ACR_N        4096
#define EXPECT_ACR_CTS      19582

#define EXPECT_MEAS_RESULT  145057                  // = T(audio_master_clk) * meas_clk_cycles / T(hdmi_audmeas_ref_clk); where meas_clk_cycles=4096; T(hdmi_audmeas_ref_clk)=5 ns.

#define HDMI_ARCTX_EN       0                       // Audio Return Channel (ARC) transmission block control:0=Disable; 1=Enable.
#define HDMI_ARCTX_MODE     0                       // ARC transmission mode: 0=Single-ended mode; 1=Common mode.

//------------------------------------------------------------------------------
// EDID
//------------------------------------------------------------------------------

#define EDID_EXTENSION_FLAG         4               // Number of 128-bytes blocks that following the basic block
#define EDID_AUTO_CEC_ENABLE        1               // 1=Automatic switch CEC ID depend on RX_PORT_SEL
#define EDID_CEC_ID_ADDR            0x00990098      // EDID address offsets for storing 2-byte of Physical Address
#define EDID_CEC_ID_DATA            0x1023          // Physical Address: e.g. 0x1023 is 1.0.2.3
#define EDID_AUTO_CHECKSUM_ENABLE   1               // Checksum byte selection: 0=Use data stored in MEM; 1=Use checksum calculated by HW.
#define EDID_CLK_DIVIDE_M1          2               // EDID I2C clock = sysclk / (1+EDID_CLK_DIVIDE_M1).

//------------------------------------------------------------------------------
// HDCP
//------------------------------------------------------------------------------

#define HDCP_ON                 1
#define HDCP_KEY_DECRYPT_EN     0

//------------------------------------------------------------------------------
// Interrupt Mask
//------------------------------------------------------------------------------

#define HDMIRX_DWC_PDEC_IEN_BIT28_dviDet            (1<<28) // 0xf7c bit[28]
//#define HDMIRX_DWC_PDEC_IEN_BIT27_vsiCksChg         (1<<27) // 0xf7c bit[27]
//#define HDMIRX_DWC_PDEC_IEN_BIT26_gmdCksChg         (1<<26) // 0xf7c bit[26]
//#define HDMIRX_DWC_PDEC_IEN_BIT25_aifCksChg         (1<<25) // 0xf7c bit[25]
//#define HDMIRX_DWC_PDEC_IEN_BIT24_aviCksChg         (1<<24) // 0xf7c bit[24]
//#define HDMIRX_DWC_PDEC_IEN_BIT23_acrNChg           (1<<23) // 0xf7c bit[23]
//#define HDMIRX_DWC_PDEC_IEN_BIT22_acrCtsChg         (1<<22) // 0xf7c bit[22]
//#define HDMIRX_DWC_PDEC_IEN_BIT21_gcpAvmuteChg      (1<<21) // 0xf7c bit[21]
#define HDMIRX_DWC_PDEC_IEN_BIT20_gmdRcv            (1<<20) // 0xf7c bit[20]
#define HDMIRX_DWC_PDEC_IEN_BIT19_aifRcv            (1<<19) // 0xf7c bit[19]
#define HDMIRX_DWC_PDEC_IEN_BIT18_aviRcv            (1<<18) // 0xf7c bit[18]
//#define HDMIRX_DWC_PDEC_IEN_BIT17_acrRcv            (1<<17) // 0xf7c bit[17]
#define HDMIRX_DWC_PDEC_IEN_BIT16_gcpRcv            (1<<16) // 0xf7c bit[16]
#define HDMIRX_DWC_PDEC_IEN_BIT15_vsiRcv            (1<<15) // 0xf7c bit[15]
#define HDMIRX_DWC_PDEC_IEN_BIT08_pdFifoNewEntry    (1<<8)  // 0xf7c bit[08]
#define HDMIRX_DWC_PDEC_IEN_BIT04_pdFifoOverfl      (1<<4)  // 0xf7c bit[04]
#define HDMIRX_DWC_PDEC_IEN_BIT03_pdFifoUnderfl     (1<<3)  // 0xf7c bit[03]
//#define HDMIRX_DWC_PDEC_IEN_BIT02_pdFifoThStartPass (1<<2)  // 0xf7c bit[02]
#define HDMIRX_DWC_PDEC_IEN_BIT01_pdFifoThMaxPass   (1<<1)  // 0xf7c bit[01]
#define HDMIRX_DWC_PDEC_IEN_BIT00_pdFifoThMinPass   (1<<0)  // 0xf7c bit[00]

//#define HDMIRX_DWC_AUD_CLK_IEN_BIT22_wakeupctrl     (1<<22) // 0xf94 bit[22]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT21_errorFoll      (1<<21) // 0xf94 bit[21]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT20_errorInit      (1<<20) // 0xf94 bit[20]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT19_arblst         (1<<19) // 0xf94 bit[19]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT18_nack           (1<<18) // 0xf94 bit[18]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT17_eom            (1<<17) // 0xf94 bit[17]
//#define HDMIRX_DWC_AUD_CLK_IEN_BIT16_done           (1<<16) // 0xf94 bit[16]
#define HDMIRX_DWC_AUD_CLK_IEN_BIT01_sckStable      (1<<1)  // 0xf94 bit[01]
#define HDMIRX_DWC_AUD_CLK_IEN_BIT00_ctsnCnt        (1<<0)  // 0xf94 bit[00]

#define HDMIRX_DWC_AUD_FIFO_IEN_BIT04_afifOverfl    (1<<4) // 0xfac bit[04]
#define HDMIRX_DWC_AUD_FIFO_IEN_BIT03_afifUnderfl   (1<<3) // 0xfac bit[03]
//#define HDMIRX_DWC_AUD_FIFO_IEN_BIT02_afifThsPass   (1<<2) // 0xfac bit[02]
#define HDMIRX_DWC_AUD_FIFO_IEN_BIT01_afifThMax     (1<<1) // 0xfac bit[01]
#define HDMIRX_DWC_AUD_FIFO_IEN_BIT00_afifThMin     (1<<0) // 0xfac bit[00]

//#define HDMIRX_DWC_MD_IEN_BIT11_vofsLin             (1<<11) // 0xfc4 bit[11]
//#define HDMIRX_DWC_MD_IEN_BIT10_vtotLin             (1<<10) // 0xfc4 bit[10]
//#define HDMIRX_DWC_MD_IEN_BIT09_vactLin             (1<<9)  // 0xfc4 bit[09]
//#define HDMIRX_DWC_MD_IEN_BIT08_vsClk               (1<<8)  // 0xfc4 bit[08]
//#define HDMIRX_DWC_MD_IEN_BIT07_vtotClk             (1<<7)  // 0xfc4 bit[07]
//#define HDMIRX_DWC_MD_IEN_BIT06_hactPix             (1<<6)  // 0xfc4 bit[06]
//#define HDMIRX_DWC_MD_IEN_BIT05_hsClk               (1<<5)  // 0xfc4 bit[05]
//#define HDMIRX_DWC_MD_IEN_BIT04_htot32Clk           (1<<4)  // 0xfc4 bit[04]
#define HDMIRX_DWC_MD_IEN_BIT03_ilace               (1<<3)  // 0xfc4 bit[03]
//#define HDMIRX_DWC_MD_IEN_BIT02_deActivity          (1<<2)  // 0xfc4 bit[02]
//#define HDMIRX_DWC_MD_IEN_BIT01_vsAct               (1<<1)  // 0xfc4 bit[01]
//#define HDMIRX_DWC_MD_IEN_BIT00_hsAct               (1<<0)  // 0xfc4 bit[00]

//#define HDMIRX_DWC_HDMI_IEN_BIT30_i2cmpArblost      (1<<30) // 0xfdc bit[30]
//#define HDMIRX_DWC_HDMI_IEN_BIT29_i2cmpnack         (1<<29) // 0xfdc bit[29]
//#define HDMIRX_DWC_HDMI_IEN_BIT28_i2cmpdone         (1<<28) // 0xfdc bit[28]
//#define HDMIRX_DWC_HDMI_IEN_BIT25_aksvRcv           (1<<25) // 0xfdc bit[25]
//#define HDMIRX_DWC_HDMI_IEN_BIT24_pllClockGated     (1<<24) // 0xfdc bit[24]
//#define HDMIRX_DWC_HDMI_IEN_BIT16_dcmCurrentModeChg (1<<16) // 0xfdc bit[16]
//#define HDMIRX_DWC_HDMI_IEN_BIT15_dcmPhDiffCntOverfl    (1<<15) // 0xfdc bit[15]
//#define HDMIRX_DWC_HDMI_IEN_BIT14_dcmGcpZeroFieldsPass  (1<<14) // 0xfdc bit[14]
//#define HDMIRX_DWC_HDMI_IEN_BIT13_ctl3Change            (1<<13) // 0xfdc bit[13]
//#define HDMIRX_DWC_HDMI_IEN_BIT12_ctl2Change        (1<<12) // 0xfdc bit[12]
//#define HDMIRX_DWC_HDMI_IEN_BIT11_ctl1Change        (1<<11) // 0xfdc bit[11]
//#define HDMIRX_DWC_HDMI_IEN_BIT10_ctl0Change        (1<<10) // 0xfdc bit[10]
//#define HDMIRX_DWC_HDMI_IEN_BIT09_vsPolAdj          (1<<9)  // 0xfdc bit[09]
//#define HDMIRX_DWC_HDMI_IEN_BIT08_hsPolAdj          (1<<8)  // 0xfdc bit[08]
//#define HDMIRX_DWC_HDMI_IEN_BIT07_resOverload       (1<<7)  // 0xfdc bit[07]
//#define HDMIRX_DWC_HDMI_IEN_BIT06_clkChange         (1<<6)  // 0xfdc bit[06]
#define HDMIRX_DWC_HDMI_IEN_BIT05_pllLckChg         (1<<5)  // 0xfdc bit[05]
//#define HDMIRX_DWC_HDMI_IEN_BIT04_eqgainDone        (1<<4)  // 0xfdc bit[04]
//#define HDMIRX_DWC_HDMI_IEN_BIT03_offscalDone       (1<<3)  // 0xfdc bit[03]
//#define HDMIRX_DWC_HDMI_IEN_BIT02_rescalDone        (1<<2)  // 0xfdc bit[02]
//#define HDMIRX_DWC_HDMI_IEN_BIT01_actChange         (1<<1)  // 0xfdc bit[01]
//#define HDMIRX_DWC_HDMI_IEN_BIT00_stateReached      (1<<0)  // 0xfdc bit[00]

unsigned long pdec_ien_maskn    = ( 0
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT28_dviDet
                                    | HDMIRX_DWC_PDEC_IEN_BIT28_dviDet
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT27_vsiCksChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT27_vsiCksChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT26_gmdCksChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT26_gmdCksChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT25_aifCksChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT25_aifCksChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT24_aviCksChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT24_aviCksChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT23_acrNChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT23_acrNChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT22_acrCtsChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT22_acrCtsChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT21_gcpAvmuteChg
                                    | HDMIRX_DWC_PDEC_IEN_BIT21_gcpAvmuteChg
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT20_gmdRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT20_gmdRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT19_aifRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT19_aifRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT18_aviRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT18_aviRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT17_acrRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT17_acrRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT16_gcpRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT16_gcpRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT15_vsiRcv
                                    | HDMIRX_DWC_PDEC_IEN_BIT15_vsiRcv
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT08_pdFifoNewEntry
                                    | HDMIRX_DWC_PDEC_IEN_BIT08_pdFifoNewEntry
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT04_pdFifoOverfl
                                    | HDMIRX_DWC_PDEC_IEN_BIT04_pdFifoOverfl
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT03_pdFifoUnderfl
                                    | HDMIRX_DWC_PDEC_IEN_BIT03_pdFifoUnderfl
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT02_pdFifoThStartPass
                                    | HDMIRX_DWC_PDEC_IEN_BIT02_pdFifoThStartPass
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT01_pdFifoThMaxPass
                                    | HDMIRX_DWC_PDEC_IEN_BIT01_pdFifoThMaxPass
                                #endif
                                #ifdef HDMIRX_DWC_PDEC_IEN_BIT00_pdFifoThMinPass
                                    | HDMIRX_DWC_PDEC_IEN_BIT00_pdFifoThMinPass
                                #endif
                                    );

unsigned long aud_clk_ien_maskn = ( 0
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT22_wakeupctrl
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT22_wakeupctrl
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT21_errorFoll
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT21_errorFoll 
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT20_errorInit
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT20_errorInit 
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT19_arblst
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT19_arblst    
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT18_nack
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT18_nack      
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT17_eom
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT17_eom       
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT16_done
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT16_done      
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT01_sckStable
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT01_sckStable 
                                #endif
                                #ifdef HDMIRX_DWC_AUD_CLK_IEN_BIT00_ctsnCnt
                                    | HDMIRX_DWC_AUD_CLK_IEN_BIT00_ctsnCnt   
                                #endif
                                    );

unsigned long aud_fifo_ien_maskn= ( 0
                                #ifdef HDMIRX_DWC_AUD_FIFO_IEN_BIT04_afifOverfl
                                    | HDMIRX_DWC_AUD_FIFO_IEN_BIT04_afifOverfl 
                                #endif
                                #ifdef HDMIRX_DWC_AUD_FIFO_IEN_BIT03_afifUnderfl
                                    | HDMIRX_DWC_AUD_FIFO_IEN_BIT03_afifUnderfl
                                #endif
                                #ifdef HDMIRX_DWC_AUD_FIFO_IEN_BIT02_afifThsPass
                                    | HDMIRX_DWC_AUD_FIFO_IEN_BIT02_afifThsPass
                                #endif
                                #ifdef HDMIRX_DWC_AUD_FIFO_IEN_BIT01_afifThMax
                                    | HDMIRX_DWC_AUD_FIFO_IEN_BIT01_afifThMax  
                                #endif
                                #ifdef HDMIRX_DWC_AUD_FIFO_IEN_BIT00_afifThMin
                                    | HDMIRX_DWC_AUD_FIFO_IEN_BIT00_afifThMin  
                                #endif
                                    );

unsigned long md_ien_maskn      = ( 0
                                #ifdef HDMIRX_DWC_MD_IEN_BIT11_vofsLin
                                    | HDMIRX_DWC_MD_IEN_BIT11_vofsLin   
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT10_vtotLin
                                    | HDMIRX_DWC_MD_IEN_BIT10_vtotLin   
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT09_vactLin
                                    | HDMIRX_DWC_MD_IEN_BIT09_vactLin   
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT08_vsClk
                                    | HDMIRX_DWC_MD_IEN_BIT08_vsClk     
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT07_vtotClk
                                    | HDMIRX_DWC_MD_IEN_BIT07_vtotClk   
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT06_hactPix
                                    | HDMIRX_DWC_MD_IEN_BIT06_hactPix   
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT05_hsClk
                                    | HDMIRX_DWC_MD_IEN_BIT05_hsClk     
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT04_htot32Clk
                                    | HDMIRX_DWC_MD_IEN_BIT04_htot32Clk 
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT03_ilace
                                    | HDMIRX_DWC_MD_IEN_BIT03_ilace     
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT02_deActivity
                                    | HDMIRX_DWC_MD_IEN_BIT02_deActivity
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT01_vsAct
                                    | HDMIRX_DWC_MD_IEN_BIT01_vsAct     
                                #endif
                                #ifdef HDMIRX_DWC_MD_IEN_BIT00_hsAct
                                    | HDMIRX_DWC_MD_IEN_BIT00_hsAct     
                                #endif
                                    );

unsigned long hdmi_ien_maskn    = ( 0
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT30_i2cmpArblost
                                    | HDMIRX_DWC_HDMI_IEN_BIT30_i2cmpArblost     
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT29_i2cmpnack
                                    | HDMIRX_DWC_HDMI_IEN_BIT29_i2cmpnack        
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT28_i2cmpdone
                                    | HDMIRX_DWC_HDMI_IEN_BIT28_i2cmpdone        
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT25_aksvRcv
                                    | HDMIRX_DWC_HDMI_IEN_BIT25_aksvRcv          
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT24_pllClockGated
                                    | HDMIRX_DWC_HDMI_IEN_BIT24_pllClockGated    
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT16_dcmCurrentModeChg
                                    | HDMIRX_DWC_HDMI_IEN_BIT16_dcmCurrentModeChg
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT15_dcmPhDiffCntOverfl
                                    | HDMIRX_DWC_HDMI_IEN_BIT15_dcmPhDiffCntOverfl  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT14_dcmGcpZeroFieldsPass
                                    | HDMIRX_DWC_HDMI_IEN_BIT14_dcmGcpZeroFieldsPass
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT13_ctl3Change
                                    | HDMIRX_DWC_HDMI_IEN_BIT13_ctl3Change  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT12_ctl2Change
                                    | HDMIRX_DWC_HDMI_IEN_BIT12_ctl2Change  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT11_ctl1Change
                                    | HDMIRX_DWC_HDMI_IEN_BIT11_ctl1Change  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT10_ctl0Change
                                    | HDMIRX_DWC_HDMI_IEN_BIT10_ctl0Change  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT09_vsPolAdj
                                    | HDMIRX_DWC_HDMI_IEN_BIT09_vsPolAdj    
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT08_hsPolAdj
                                    | HDMIRX_DWC_HDMI_IEN_BIT08_hsPolAdj    
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT07_resOverload
                                    | HDMIRX_DWC_HDMI_IEN_BIT07_resOverload 
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT06_clkChange
                                    | HDMIRX_DWC_HDMI_IEN_BIT06_clkChange   
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT05_pllLckChg
                                    | HDMIRX_DWC_HDMI_IEN_BIT05_pllLckChg   
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT04_eqgainDone
                                    | HDMIRX_DWC_HDMI_IEN_BIT04_eqgainDone  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT03_offscalDone
                                    | HDMIRX_DWC_HDMI_IEN_BIT03_offscalDone 
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT02_rescalDone
                                    | HDMIRX_DWC_HDMI_IEN_BIT02_rescalDone  
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT01_actChange
                                    | HDMIRX_DWC_HDMI_IEN_BIT01_actChange   
                                #endif
                                #ifdef HDMIRX_DWC_HDMI_IEN_BIT00_stateReached
                                    | HDMIRX_DWC_HDMI_IEN_BIT00_stateReached
                                #endif
                                    );

//------------------------------------------------------------------------------
// The following parameters are mandatory only if CHIP_HAVE_HDMI_RX
//------------------------------------------------------------------------------
#define RX_PORT_SEL             0                   // Select from which input port HDMI RX is to receive: 0=PortA; 1=PortB; 2=PortC, 3=PortD; others=invalid.

//------------------------------------------------------------------------------
// The following parameters are not to be modified
//------------------------------------------------------------------------------

#define TOTAL_PIXELS        (FRONT_PORCH+HSYNC_PIXELS+BACK_PORCH+ACTIVE_PIXELS) // Number of total pixels per line.
#define TOTAL_LINES         (LINES_F0+(LINES_F1*INTERLACE_MODE))                // Number of total lines per frame.
