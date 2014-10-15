#ifndef HDMIRX_H
#define HDMIRX_H

// Device ID to differentiate HDMIRX register access to TOP, DWC or PHY
#define HDMIRX_DEV_ID_TOP   0
#define HDMIRX_DEV_ID_DWC   1
#define HDMIRX_DEV_ID_PHY   2

#define HDMIRX_ADDR_PORT    0xc800e000  // TOP ADDR_PORT: 0xc800e000; DWC ADDR_PORT: 0xc800e010
#define HDMIRX_DATA_PORT    0xc800e004  // TOP DATA_PORT: 0xc800e004; DWC DATA_PORT: 0xc800e014
#define HDMIRX_CTRL_PORT    0xc800e008  // TOP CTRL_PORT: 0xc800e008; DWC CTRL_PORT: 0xc800e018

// Use the following functions to access the HDMIRX modules (TOP, DWC or PHY) by default
extern void             hdmirx_wr_only_reg  (unsigned char dev_id, unsigned long addr, unsigned long data);
extern unsigned long    hdmirx_rd_reg       (unsigned char dev_id, unsigned long addr);
extern void             hdmirx_rd_check_reg (unsigned char dev_id, unsigned long addr, unsigned long exp_data, unsigned long mask);
extern void             hdmirx_wr_reg       (unsigned char dev_id, unsigned long addr, unsigned long data);
extern void             hdmirx_poll_reg     (unsigned char dev_id, unsigned long addr, unsigned long exp_data, unsigned long mask);

// HDMIRX initialization
extern void             hdmirx_test_function (unsigned char acr_mode,                   // Select which ACR scheme: 0=Analog PLL based ACR; 1=Digital ACR.
                                              unsigned long manual_acr_cts,
                                              unsigned long manual_acr_n,
                                              unsigned char rx_8_channel,               // Audio channels: 0=2-channel; 1=4 x 2-channel.
                                              unsigned char edid_extension_flag,        // Number of 128-bytes blocks that following the basic block
                                              unsigned char edid_auto_cec_enable,       // 1=Automatic switch CEC ID depend on RX_PORT_SEL
                                              unsigned long edid_cec_id_addr,           // EDID address offsets for storing 2-byte of Physical Address
                                              unsigned long edid_cec_id_data,           // Physical Address: e.g. 0x1023 is 1.0.2.3
                                              unsigned char edid_auto_checksum_enable,  // Checksum byte selection: 0=Use data stored in MEM; 1=Use checksum calculated by HW.
                                              unsigned char edid_clk_divide_m1,         // EDID I2C clock = sysclk / (1+edid_clk_divide_m1).
                                              unsigned char hdcp_on,
                                              unsigned char hdcp_key_decrypt_en,
                                              unsigned char vic,                        // Video format identification code
                                              unsigned char pixel_repeat_hdmi,
                                              unsigned char interlace_mode,             // 0=Progressive; 1=Interlace.
                                              unsigned long front_porch,                // Number of pixels from DE Low to HSYNC high
                                              unsigned long back_porch,                 // Number of pixels from HSYNC low to DE high
                                              unsigned long hsync_pixels,               // Number of pixels of HSYNC pulse
                                              unsigned long hsync_polarity,             // TX HSYNC polarity: 0=low active; 1=high active.
                                              unsigned long sof_lines,                  // HSYNC count between VSYNC de-assertion and first line of active video
                                              unsigned long eof_lines,                  // HSYNC count between last line of active video and start of VSYNC
                                              unsigned long vsync_lines,                // HSYNC count of VSYNC assertion
                                              unsigned char vsync_polarity,             // TX VSYNC polarity: 0=low active; 1=high active.
                                              unsigned long total_pixels,               // Number of total pixels per line
                                              unsigned long total_lines,                // Number of total lines per frame
                                              unsigned char rx_input_color_format,      // Pixel format: 0=RGB444; 1=YCbCr422; 2=YCbCr444.
                                              unsigned char rx_input_color_depth,       // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
                                              unsigned char rx_hscale_half,             // 1=RX output video horizontally scaled by half, to reduce clock speed.
                                              unsigned long *curr_pdec_ien_maskn,
                                              unsigned long *curr_aud_clk_ien_maskn,
                                              unsigned long *curr_aud_fifo_ien_maskn,
                                              unsigned long *curr_md_ien_maskn,
                                              unsigned long *curr_hdmi_ien_maskn,
                                              unsigned long pdec_ien_maskn,
                                              unsigned long aud_clk_ien_maskn,
                                              unsigned long aud_fifo_ien_maskn,
                                              unsigned long md_ien_maskn,
                                              unsigned long hdmi_ien_maskn,
                                              unsigned char rx_port_sel,                // Select HDMI RX input port: 0=PortA; 1=PortB; 2=PortC, 3=PortD; others=invalid.
                                              unsigned char hdmi_arctx_en,              // Audio Return Channel (ARC) transmission block control:0=Disable; 1=Enable.
                                              unsigned char hdmi_arctx_mode,            // ARC transmission mode: 0=Single-ended mode; 1=Common mode.
                                              unsigned char *hdmi_pll_lock);

extern void             aocec_poll_reg_busy (unsigned char reg_busy);
extern void             aocec_wr_only_reg (unsigned long addr, unsigned long data);
extern unsigned long    aocec_rd_reg (unsigned long addr);
extern void             aocec_rd_check_reg (unsigned long addr, unsigned long exp_data, unsigned long mask);
extern void             aocec_wr_reg (unsigned long addr, unsigned long data);

// Internal functions:
void            hdmirx_wr_only_TOP  (unsigned long addr, unsigned long data);
void            hdmirx_wr_only_DWC  (unsigned long addr, unsigned long data);
void            hdmirx_wr_only_PHY  (unsigned long addr, unsigned long data);
unsigned long   hdmirx_rd_TOP       (unsigned long addr);
unsigned long   hdmirx_rd_DWC       (unsigned long addr);
unsigned long   hdmirx_rd_PHY       (unsigned long addr);
void            hdmirx_rd_check_TOP (unsigned long addr, unsigned long exp_data, unsigned long mask);
void            hdmirx_rd_check_DWC (unsigned long addr, unsigned long exp_data, unsigned long mask);
void            hdmirx_rd_check_PHY (unsigned long addr, unsigned long exp_data, unsigned long mask);
void            hdmirx_wr_TOP       (unsigned long addr, unsigned long data);
void            hdmirx_wr_DWC       (unsigned long addr, unsigned long data);
void            hdmirx_wr_PHY       (unsigned long addr, unsigned long data);
void            hdmirx_poll_TOP     (unsigned long addr, unsigned long exp_data, unsigned long mask);
void            hdmirx_poll_DWC     (unsigned long addr, unsigned long exp_data, unsigned long mask, unsigned long max_try);
void            hdmirx_poll_PHY     (unsigned long addr, unsigned long exp_data, unsigned long mask);
void            hdmirx_edid_setting (unsigned char edid_extension_flag);
void            hdmirx_key_setting  (unsigned char encrypt_en);
void            start_video_gen_ana (unsigned char  vic,                // Video format identification code
                                     unsigned char  pixel_repeat_hdmi,
                                     unsigned char  interlace_mode,     // 0=Progressive; 1=Interlace.
                                     unsigned long  front_porch,        // Number of pixels from DE Low to HSYNC high
                                     unsigned long  back_porch,         // Number of pixels from HSYNC low to DE high
                                     unsigned long  hsync_pixels,       // Number of pixels of HSYNC pulse
                                     unsigned long  hsync_polarity,     // TX HSYNC polarity: 0=low active; 1=high active.
                                     unsigned long  sof_lines,          // HSYNC count between VSYNC de-assertion and first line of active video
                                     unsigned long  eof_lines,          // HSYNC count between last line of active video and start of VSYNC
                                     unsigned long  vsync_lines,        // HSYNC count of VSYNC assertion
                                     unsigned long  vsync_polarity,     // TX VSYNC polarity: 0=low active; 1=high active.
                                     unsigned long  total_pixels,       // Number of total pixels per line
                                     unsigned long  total_lines);       // Number of total lines per frame

//------------------------------------------------------------------------------
// Defines for simulation
//------------------------------------------------------------------------------

////first set of test Device Private Keys (HDCP Table A-1, Receiver B1)
//const unsigned long hdmirx_hdcp_bksvs[2]    = {0x51, 0x1ef21acd}; // {high 8-bit, low32-bit}
//const unsigned long hdmirx_hdcp_keys[40*2]  = {
//    0xbc13e0, 0xc75bf0fd,   // key set  0: {high 24-bit, low 32-bit}
//    0xae0d2c, 0x7f76443b,   // key set  1: {high 24-bit, low 32-bit}
//    0x24bf21, 0x85a36c60,   // key set  2: {high 24-bit, low 32-bit}
//    0xf4bc6c, 0xbcd7a32f,   // key set  3: {high 24-bit, low 32-bit}
//    0xa72e69, 0xc5eb6388,   // key set  4: {high 24-bit, low 32-bit}
//    0x7fa2d2, 0x7a37d9f8,   // key set  5: {high 24-bit, low 32-bit}
//    0x32fd35, 0x29dea3d1,   // key set  6: {high 24-bit, low 32-bit}
//    0x485fc2, 0x40cc9bae,   // key set  7: {high 24-bit, low 32-bit}
//    0x3b9857, 0x797d5103,   // key set  8: {high 24-bit, low 32-bit}
//    0x0dd170, 0xbe615250,   // key set  9: {high 24-bit, low 32-bit}
//    0x1a748b, 0xe4866bb1,   // key set 10: {high 24-bit, low 32-bit}
//    0xf9606a, 0x7c348cca,   // key set 11: {high 24-bit, low 32-bit}
//    0x4bbb03, 0x7899eea1,   // key set 12: {high 24-bit, low 32-bit}
//    0x190ecf, 0x9cc095a9,   // key set 13: {high 24-bit, low 32-bit}
//    0xa821c4, 0x6897447f,   // key set 14: {high 24-bit, low 32-bit}
//    0x1a8a0b, 0xc4298a41,   // key set 15: {high 24-bit, low 32-bit}
//    0xaefc08, 0x53e62082,   // key set 16: {high 24-bit, low 32-bit}
//    0xf75d4a, 0x0c497ba4,   // key set 17: {high 24-bit, low 32-bit}
//    0xad6495, 0xfc8a06d8,   // key set 18: {high 24-bit, low 32-bit}
//    0x67c202, 0x0c2b2e02,   // key set 19: {high 24-bit, low 32-bit}
//    0x8f116b, 0x18f4ae8d,   // key set 20: {high 24-bit, low 32-bit}
//    0xe3053f, 0xa3e9fa69,   // key set 21: {high 24-bit, low 32-bit}
//    0x37d800, 0x2881c7d1,   // key set 22: {high 24-bit, low 32-bit}
//    0xc3a5fd, 0x1c15669c,   // key set 23: {high 24-bit, low 32-bit}
//    0x9e93d4, 0x1e0811f7,   // key set 24: {high 24-bit, low 32-bit}
//    0x2c4074, 0x509eec6c,   // key set 25: {high 24-bit, low 32-bit}
//    0x8b7fd8, 0x19279b61,   // key set 26: {high 24-bit, low 32-bit}
//    0xd7caad, 0xa0a06ce9,   // key set 27: {high 24-bit, low 32-bit}
//    0x9297dc, 0xa1f8c1db,   // key set 28: {high 24-bit, low 32-bit}
//    0x5d1aaa, 0x99dea489,   // key set 29: {high 24-bit, low 32-bit}
//    0x60cb56, 0xddbaa1d9,   // key set 30: {high 24-bit, low 32-bit}
//    0x85d4ad, 0x5e5ff2e0,   // key set 31: {high 24-bit, low 32-bit}
//    0x128016, 0x1221df6d,   // key set 32: {high 24-bit, low 32-bit}
//    0xca31a5, 0xf2406589,   // key set 33: {high 24-bit, low 32-bit}
//    0x1d30e8, 0xcb198e6f,   // key set 34: {high 24-bit, low 32-bit}
//    0xd1c18b, 0xed07d3fa,   // key set 35: {high 24-bit, low 32-bit}
//    0xcec7ec, 0x09245b43,   // key set 36: {high 24-bit, low 32-bit}
//    0xb08129, 0xefedd583,   // key set 37: {high 24-bit, low 32-bit}
//    0x2134cf, 0x4ce286e5,   // key set 38: {high 24-bit, low 32-bit}
//    0xedeef9, 0xd099b78c    // key set 39: {high 24-bit, low 32-bit}
//}; /* hdmirx_hdcp_keys */
//const unsigned long hdmirx_hdcp_key_decrypt_seed    = 0xA55A;
//const unsigned long hdmirx_hdcp_encrypt_keys[40*2]  = {
//    0xC0E0BD, 0x0AB26F9F,   // key set  0: {high 24-bit, low 32-bit}
//    0x0B90B3, 0xE9B2B75F,   // key set  1: {high 24-bit, low 32-bit}
//    0xBD00B5, 0xD15859EE,   // key set  2: {high 24-bit, low 32-bit}
//    0xD89597, 0x7578E44C,   // key set  3: {high 24-bit, low 32-bit}
//    0x4AFF12, 0xFCC45CA2,   // key set  4: {high 24-bit, low 32-bit}
//    0x36555B, 0xD5B12FAF,   // key set  5: {high 24-bit, low 32-bit}
//    0x8AE77F, 0x4EDFD419,   // key set  6: {high 24-bit, low 32-bit}
//    0x7AA3D0, 0x0FD2C60F,   // key set  7: {high 24-bit, low 32-bit}
//    0x79052E, 0xBD613745,   // key set  8: {high 24-bit, low 32-bit}
//    0xB67BB5, 0xE12AE0A6,   // key set  9: {high 24-bit, low 32-bit}
//    0x78B9DD, 0xF6629AC5,   // key set 10: {high 24-bit, low 32-bit}
//    0x61DEEE, 0x2BFE2F2F,   // key set 11: {high 24-bit, low 32-bit}
//    0x1A40B2, 0x1F63F998,   // key set 12: {high 24-bit, low 32-bit}
//    0x5A9AE6, 0xDE543C62,   // key set 13: {high 24-bit, low 32-bit}
//    0x65DF19, 0xA00E5744,   // key set 14: {high 24-bit, low 32-bit}
//    0x6C684F, 0x4B65A8BB,   // key set 15: {high 24-bit, low 32-bit}
//    0x7DA075, 0xB7F8D6CC,   // key set 16: {high 24-bit, low 32-bit}
//    0x1DE01C, 0xEEADFBC8,   // key set 17: {high 24-bit, low 32-bit}
//    0x06E607, 0xC4DC61C4,   // key set 18: {high 24-bit, low 32-bit}
//    0xA3BB1E, 0xD7510D5E,   // key set 19: {high 24-bit, low 32-bit}
//    0x02F495, 0xECEB5843,   // key set 20: {high 24-bit, low 32-bit}
//    0x80E13E, 0x57081DCB,   // key set 21: {high 24-bit, low 32-bit}
//    0x6FB563, 0x6F2E0EAB,   // key set 22: {high 24-bit, low 32-bit}
//    0x72439F, 0x4058074B,   // key set 23: {high 24-bit, low 32-bit}
//    0xB98261, 0xF21FBEEF,   // key set 24: {high 24-bit, low 32-bit}
//    0xC1EB77, 0x5AECDF3B,   // key set 25: {high 24-bit, low 32-bit}
//    0xF780A5, 0x5E975124,   // key set 26: {high 24-bit, low 32-bit}
//    0xE1DB09, 0x5E94F736,   // key set 27: {high 24-bit, low 32-bit}
//    0x8FFA7B, 0x82786B25,   // key set 28: {high 24-bit, low 32-bit}
//    0xE60823, 0x52B35574,   // key set 29: {high 24-bit, low 32-bit}
//    0x212A04, 0x82E7C09F,   // key set 30: {high 24-bit, low 32-bit}
//    0x38AF79, 0xC2A06F25,   // key set 31: {high 24-bit, low 32-bit}
//    0xFB17B5, 0x2A46ACA3,   // key set 32: {high 24-bit, low 32-bit}
//    0x2C2DE0, 0x1316DBC3,   // key set 33: {high 24-bit, low 32-bit}
//    0x5E5761, 0x758CCA16,   // key set 34: {high 24-bit, low 32-bit}
//    0x4D93A9, 0x09C6A332,   // key set 35: {high 24-bit, low 32-bit}
//    0xFA6BF7, 0x463357F5,   // key set 36: {high 24-bit, low 32-bit}
//    0x60B17C, 0xA1A5D7FA,   // key set 37: {high 24-bit, low 32-bit}
//    0x7BB35C, 0x605646D5,   // key set 38: {high 24-bit, low 32-bit}
//    0x28AAD1, 0x52893794    // key set 39: {high 24-bit, low 32-bit}
//}; /* hdmirx_hdcp_encrypt_keys */

#endif /* HDMIRX_H */
