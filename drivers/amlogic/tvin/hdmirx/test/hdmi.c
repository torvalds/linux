#include "register.h"
#include "hdmi.h"
#include "hdmi_parameter.h"
#include "c_stimulus.h"
void hdmi_wr_only_reg(unsigned long addr, unsigned long data)
{
  *((volatile unsigned long *) HDMI_ADDR_PORT) = addr;
  *((volatile unsigned long *) HDMI_DATA_PORT) = data; 
}

void hdmi_wr_reg(unsigned long addr, unsigned long data)
{
    unsigned long rd_data;
    *((volatile unsigned long *) HDMI_ADDR_PORT) = addr;
    *((volatile unsigned long *) HDMI_DATA_PORT) = data; 
    if (addr < EXT_HDMI_TRX_ADDR_OFFSET) {  // only read back if it's chip internal device
        rd_data = hdmi_rd_reg (addr);
        if (rd_data != data) 
        {
            stimulus_print("Error: (addr) ");
            stimulus_print_num_hex(addr);
            stimulus_print_without_timestamp("(rdata) ");
            stimulus_print_num_hex(rd_data);
            stimulus_print_without_timestamp("(exp_data) ");
            stimulus_print_num_hex(data);
            stimulus_print_without_timestamp("\n");
            stimulus_finish_fail(10);
        }
    }
}

unsigned long hdmi_rd_reg(unsigned long addr)
{
  unsigned long data;
  *((volatile unsigned long *) HDMI_ADDR_PORT) = addr;
  data = *((volatile unsigned long *) HDMI_DATA_PORT); 
  return (data);
}

void hdmi_rd_check_reg(unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmi_rd_reg(addr);
    if ((rd_data | mask) != (exp_data | mask)) 
    {
        stimulus_print("Error: (addr) ");
        stimulus_print_num_hex(addr);
        stimulus_print_without_timestamp("(rd_data) ");
        stimulus_print_num_hex(rd_data);
        stimulus_print_without_timestamp("(exp_data) ");
        stimulus_print_num_hex(exp_data);
        stimulus_print_without_timestamp("(mask) ");
        stimulus_print_num_hex(mask);
        stimulus_print_without_timestamp("\n");
        stimulus_finish_fail(10);
    }
}

void hdmi_poll_reg(unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmi_rd_reg(addr);
    while ((rd_data | mask) != (exp_data | mask))
    {
        rd_data = hdmi_rd_reg(addr);
    }
}

void ext_hdmi_wr_only_reg(unsigned long addr, unsigned long data)
{
    hdmi_wr_only_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr, data);
}

void ext_hdmi_wr_reg(unsigned long addr, unsigned long data)
{
    //hdmi_wr_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr, data);
    hdmi_wr_only_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr, data);
}

unsigned long ext_hdmi_rd_reg(unsigned long addr)
{
    return ( hdmi_rd_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr) );
}

void ext_hdmi_rd_check_reg(unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    hdmi_rd_check_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr, exp_data, mask);
}

void ext_hdmi_poll_reg(unsigned long addr, unsigned long exp_data, unsigned long mask)
{
    unsigned long rd_data;
    rd_data = hdmi_rd_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr);
    while ((rd_data | mask) != (exp_data | mask))
    {
        rd_data = hdmi_rd_reg(EXT_HDMI_TRX_ADDR_OFFSET+addr);
    }
}

//------------------------------------------------------------------------------
// set_hdmi_audio_source(unsigned int src)
//
// Description:
// Select HDMI audio clock source, and I2S input data source.
//
// Parameters:
//  src -- 0=no audio clock to HDMI; 1=pcmout to HDMI; 2=Aiu I2S out to HDMI.
//------------------------------------------------------------------------------
void set_hdmi_audio_source (unsigned int src)
{
    unsigned long data32;
    unsigned int i;
    
    // Disable HDMI audio clock input and its I2S input
    data32  = 0;
    data32 |= 0     << 4;   // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= 0     << 0;   // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_HDMI_CLK_DATA_CTRL, data32);

    // Enable HDMI audio clock from the selected source
    data32  = 0;
    data32 |= 0      << 4;  // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_HDMI_CLK_DATA_CTRL, data32);
    
    // Wait until clock change is settled
    i = 0;
    while ( (((Rd(AIU_HDMI_CLK_DATA_CTRL))>>8)&0x3) != src ) {
        if (i > 255) {
            stimulus_print("[TEST.C] Error: set_hdmi_audio_source timeout!\n");
            stimulus_finish_fail(10);
        }
        i ++;
    }

    // Enable HDMI I2S input from the selected source
    data32  = 0;
    data32 |= src   << 4;   // [5:4]    hdmi_data_sel: 00=disable hdmi i2s input; 01=Select pcm data; 10=Select AIU I2S data; 11=Not allowed.
    data32 |= src   << 0;   // [1:0]    hdmi_clk_sel: 00=Disable hdmi audio clock input; 01=Select pcm clock; 10=Select AIU aoclk; 11=Not allowed.
    Wr(AIU_HDMI_CLK_DATA_CTRL, data32);

    // Wait until data change is settled
    while ( (((Rd(AIU_HDMI_CLK_DATA_CTRL))>>12)&0x3) != src ) {}
} /* set_hdmi_audio_source */

void hdmi_tx_key_setting (unsigned long   tx_dev_offset)  // 0x00000: Internal TX; 0x10000: External TX.
{
    const unsigned char a1_keys[] ={0x69,
                                    0x1e,
                                    0x13,
                                    0x8f,
                                    0x58,
                                    0xa4,
                                    0x4d,
                                    0x09,
                                    0x50,
                                    0xe6,
                                    0x58,
                                    0x35,
                                    0x82,
                                    0x1f,
                                    0x0d,
                                    0x98,
                                    0xb9,
                                    0xab,
                                    0x47,
                                    0x6a,
                                    0x8a,
                                    0xca,
                                    0xc5,
                                    0xcb,
                                    0x52,
                                    0x1b,
                                    0x18,
                                    0xf3,
                                    0xb4,
                                    0xd8,
                                    0x96,
                                    0x68,
                                    0x7f,
                                    0x14,
                                    0xfb,
                                    0x81,
                                    0x8f,
                                    0x48,
                                    0x78,
                                    0xc9,
                                    0x8b,
                                    0xe0,
                                    0x41,
                                    0x2c,
                                    0x11,
                                    0xc8,
                                    0x64,
                                    0xd0,
                                    0xa0,
                                    0x44,
                                    0x20,
                                    0x24,
                                    0x28,
                                    0x5a,
                                    0x9d,
                                    0xb3,
                                    0x6b,
                                    0x56,
                                    0xad,
                                    0xbd,
                                    0xb2,
                                    0x28,
                                    0xb9,
                                    0xf6,
                                    0xe4,
                                    0x6c,
                                    0x4a,
                                    0x7b,
                                    0xa4,
                                    0x91,
                                    0x58,
                                    0x9d,
                                    0x5e,
                                    0x20,
                                    0xf8,
                                    0x00,
                                    0x56,
                                    0xa0,
                                    0x3f,
                                    0xee,
                                    0x06,
                                    0xb7,
                                    0x7f,
                                    0x8c,
                                    0x28,
                                    0xbc,
                                    0x7c,
                                    0x9d,
                                    0x8c,
                                    0x2d,
                                    0xc0,
                                    0x05,
                                    0x9f,
                                    0x4b,
                                    0xe5,
                                    0x61,
                                    0x12,
                                    0x56,
                                    0xcb,
                                    0xc1,
                                    0xca,
                                    0x8c,
                                    0xde,
                                    0xf0,
                                    0x74,
                                    0x6a,
                                    0xdb,
                                    0xfc,
                                    0x0e,
                                    0xf6,
                                    0xb8,
                                    0x3b,
                                    0xd7,
                                    0x2f,
                                    0xb2,
                                    0x16,
                                    0xbb,
                                    0x2b,
                                    0xa0,
                                    0x98,
                                    0x54,
                                    0x78,
                                    0x46,
                                    0x8e,
                                    0x2f,
                                    0x48,
                                    0x38,
                                    0x47,
                                    0x27,
                                    0x62,
                                    0x25,
                                    0xae,
                                    0x66,
                                    0xf2,
                                    0xdd,
                                    0x23,
                                    0xa3,
                                    0x52,
                                    0x49,
                                    0x3d,
                                    0x54,
                                    0x3a,
                                    0x7b,
                                    0x76,
                                    0x31,
                                    0xd2,
                                    0xe2,
                                    0x25,
                                    0x61,
                                    0xe6,
                                    0xed,
                                    0x1a,
                                    0x58,
                                    0x4d,
                                    0xf7,
                                    0x22,
                                    0x7b,
                                    0xbf,
                                    0x82,
                                    0x60,
                                    0x32,
                                    0x6b,
                                    0xce,
                                    0x30,
                                    0x35,
                                    0x46,
                                    0x1b,
                                    0xf6,
                                    0x6b,
                                    0x97,
                                    0xd7,
                                    0xf0,
                                    0x09,
                                    0x04,
                                    0x36,
                                    0xf9,
                                    0x49,
                                    0x8d,
                                    0x61,
                                    0x05,
                                    0xe1,
                                    0xa1,
                                    0x06,
                                    0x34,
                                    0x05,
                                    0xd1,
                                    0x9d,
                                    0x8e,
                                    0xc9,
                                    0x90,
                                    0x61,
                                    0x42,
                                    0x94,
                                    0x67,
                                    0xc3,
                                    0x20,
                                    0xc3,
                                    0x4f,
                                    0xac,
                                    0xce,
                                    0x51,
                                    0x44,
                                    0x96,
                                    0x8a,
                                    0x8c,
                                    0xe1,
                                    0x04,
                                    0x45,
                                    0x90,
                                    0x3e,
                                    0xfc,
                                    0x2d,
                                    0x9c,
                                    0x57,
                                    0x10,
                                    0x00,
                                    0x29,
                                    0x80,
                                    0xb1,
                                    0xe5,
                                    0x69,
                                    0x3b,
                                    0x94,
                                    0xd7,
                                    0x43,
                                    0x7b,
                                    0xdd,
                                    0x5b,
                                    0xea,
                                    0xc7,
                                    0x54,
                                    0xba,
                                    0x90,
                                    0xc7,
                                    0x87,
                                    0x58,
                                    0xfb,
                                    0x74,
                                    0xe0,
                                    0x1d,
                                    0x4e,
                                    0x36,
                                    0xfa,
                                    0x5c,
                                    0x93,
                                    0xae,
                                    0x11,
                                    0x9a,
                                    0x15,
                                    0x5e,
                                    0x07,
                                    0x03,
                                    0x01,
                                    0xfb,
                                    0x78,
                                    0x8a,
                                    0x40,
                                    0xd3,
                                    0x05,
                                    0xb3,
                                    0x4d,
                                    0xa0,
                                    0xd7,
                                    0xa5,
                                    0x59,
                                    0x00,
                                    0x40,
                                    0x9e,
                                    0x2c,
                                    0x4a,
                                    0x63,
                                    0x3b,
                                    0x37,
                                    0x41,
                                    0x20,
                                    0x56,
                                    0xb4,
                                    0xbb,
                                    0x73,
                                    0x25,
                                    // ksv
                                    0x14,  
                                    0xf7,  
                                    0x61,  
                                    0x03,  
                                    0xb7,  
                                    // km   
                                    0xcc,  
                                    0xce,  
                                    0x2f,  
                                    0xd2,  
                                    0xc7,  
                                    0x09,  
                                    0x53};  

    int i, j, ram_addr, byte_num;
    unsigned int value;
    
    byte_num = sizeof(a1_keys)/sizeof(unsigned char);
    
    j = 0;
    for (i = 0; i < byte_num; i++)
    {
        value = a1_keys[i]; 
        ram_addr = TX_HDCP_DKEY_OFFSET+j;
        //printf("Tx Key value=%x was writen to address %x\n",value,ram_addr);
        //stimulus_display2("Tx Key value=%h was writen to address %h\n",value,ram_addr);
        hdmi_wr_reg(tx_dev_offset+ram_addr, value ^ 0xbe);
        j = ((i % 7) == 6) ? j + 2: j + 1;
    }
} /* hdmi_tx_key_setting */

void hdmitx_test_function ( unsigned long   tx_dev_offset,          // 0x00000: Internal TX; 0x10000: External TX.
                            unsigned char   hdcp_on,
                            unsigned char   vic,                    // Video format identification code
                            unsigned char   mode_3d,                // 0=2D; 1=3D frame-packing; 2=3D side-by-side; 3=3D top-and-bottom.
                            unsigned char   pixel_repeat_hdmi,
                            unsigned char   tx_input_color_depth,   // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
                            unsigned char   tx_input_color_format,  // Pixel format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
                            unsigned char   tx_input_color_range,   // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
                            unsigned char   tx_output_color_depth,  // Pixel bit width: 0=24-bit; 1=30-bit; 2=36-bit; 3=48-bit.
                            unsigned char   tx_output_color_format, // Pixel format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
                            unsigned char   tx_output_color_range,  // Pixel range: 0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
                            unsigned char   tx_i2s_spdif,           // 0=SPDIF; 1=I2S. Note: Must select I2S if CHIP_HAVE_HDMI_RX is defined.
                            unsigned char   tx_i2s_8_channel,       // 0=I2S 2-channel; 1=I2S 4 x 2-channel.
                            unsigned char   audio_packet_type)      // 0=audio sample packet; 1=one bit audio; 2=HBR audio packet; 3=DST audio packet.
{
    unsigned int tmp_add_data;
    
    stimulus_print("[TEST.C] Configure HDMITX\n");

    // Enable APB3 fail on error
    *((volatile unsigned long *) HDMI_CTRL_PORT) |= (1 << 15);        //APB3 err_en

    // Keep TX (except register I/F) in reset, while programming the registers:
    tmp_add_data  = 0;
    tmp_add_data |= 1   << 7; // [7] tx_pixel_rstn
    tmp_add_data |= 1   << 6; // [6] tx_tmds_rstn
    tmp_add_data |= 1   << 5; // [5] tx_audio_master_rstn
    tmp_add_data |= 1   << 4; // [4] tx_audio_sample_rstn
    tmp_add_data |= 1   << 3; // [3] tx_i2s_reset_rstn
    tmp_add_data |= 1   << 2; // [2] tx_dig_reset_n_ch2
    tmp_add_data |= 1   << 1; // [1] tx_dig_reset_n_ch1
    tmp_add_data |= 1   << 0; // [0] tx_dig_reset_n_ch0
    hdmi_wr_reg(tx_dev_offset+TX_SYS5_TX_SOFT_RESET_1, tmp_add_data); // 0xff

    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7] HDMI_CH3_RST_IN
    tmp_add_data |= 0   << 6; // [6] HDMI_CH2_RST_IN
    tmp_add_data |= 0   << 5; // [5] HDMI_CH1_RST_IN
    tmp_add_data |= 0   << 4; // [4] HDMI_CH0_RST_IN
    tmp_add_data |= 0   << 3; // [3] HDMI_SR_RST
    tmp_add_data |= 1   << 0; // [0] tx_dig_reset_n_ch3
    hdmi_wr_reg(tx_dev_offset+TX_SYS5_TX_SOFT_RESET_2, tmp_add_data); // 0x01

    // Enable software controlled DDC transaction
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7] forced_sys_trigger
    tmp_add_data |= 0   << 6; // [6] sys_trigger_config
    tmp_add_data |= 0   << 5; // [5] mem_acc_seq_mode
    tmp_add_data |= 0   << 4; // [4] mem_acc_seq_start
    tmp_add_data |= 1   << 3; // [3] forced_mem_copy_done
    tmp_add_data |= 1   << 2; // [2] mem_copy_done_config
    tmp_add_data |= 0   << 1; // [1] edid_int_forced_clear
    tmp_add_data |= 0   << 0; // [0] edid_int_auto_clear
    hdmi_wr_reg(tx_dev_offset+TX_HDCP_EDID_CONFIG, tmp_add_data); // 0x0e
    
    // Setting HDCP keys
    stimulus_print("[TEST.C] Setting HDMI TX HDCP keys with encryption\n");
    hdmi_tx_key_setting(tx_dev_offset);
    stimulus_print("[TEST.C] HDMI TX Key Setting is done\n");
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]   Force DTV timing (Auto)
    tmp_add_data |= 0   << 6; // [6]   Force Video Scan, only if [7]is set
    tmp_add_data |= 0   << 5; // [5]   Force Video field, only if [7]is set
    tmp_add_data |= 0   << 0; // [4:0] Rsrv
    hdmi_wr_reg(tx_dev_offset+TX_VIDEO_DTV_TIMING, tmp_add_data); // 0x00
    
    tmp_add_data  = 0;
    tmp_add_data |= 0                       << 7; // [7]   forced_default_phase
    tmp_add_data |= 0                       << 2; // [6:2] Rsrv
    tmp_add_data |= tx_output_color_depth   << 0; // [1:0] Color_depth:0=24-bit pixel; 1=30-bit pixel; 2=36-bit pixel; 3=48-bit pixel
    hdmi_wr_reg(tx_dev_offset+TX_VIDEO_DTV_MODE, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= 1                       << 7; // [7]   gc_pack_mode: 0=clear color_depth and pixel_phase when GC packet is transmitting AV_mute/clear info;
                                                  //                     1=do not clear.
    tmp_add_data |= 0                       << 0; // [6:0] forced_islands_per_period_active
    hdmi_wr_reg(tx_dev_offset+TX_PACKET_ALLOC_ACTIVE_1, tmp_add_data); // 0x80

    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]   Force packet timing
    tmp_add_data |= 0   << 6; // [6]   PACKET ALLOC MODE
    //tmp_add_data |= 47  << 0; // [5:0] PACKET_START_LATENCY
    tmp_add_data |= 58  << 0; // [5:0] PACKET_START_LATENCY
    hdmi_wr_reg(tx_dev_offset+TX_PACKET_CONTROL_1, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 6; // [7:6] audio_source_select[1:0]
    tmp_add_data |= 0   << 5; // [5]   external_packet_enable
    tmp_add_data |= 1   << 4; // [4]   internal_packet_enable
    tmp_add_data |= 0   << 2; // [3:2] afe_fifo_source_select_lane_1[1:0]
    tmp_add_data |= 0   << 0; // [1:0] afe_fifo_source_select_lane_0[1:0]
    hdmi_wr_reg(tx_dev_offset+TX_CORE_DATA_CAPTURE_2, tmp_add_data); // 0x10
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]   monitor_lane_1
    tmp_add_data |= 0   << 4; // [6:4] monitor_select_lane_1[2:0]
    tmp_add_data |= 1   << 3; // [3]   monitor_lane_0
    tmp_add_data |= 7   << 0; // [2:0] monitor_select_lane_0[2:0]
    hdmi_wr_reg(tx_dev_offset+TX_CORE_DATA_MONITOR_1, tmp_add_data); // 0x0f
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 3; // [7:3] Rsrv
    tmp_add_data |= 2   << 0; // [2:0] monitor_select[2:0]
    hdmi_wr_reg(tx_dev_offset+TX_CORE_DATA_MONITOR_2, tmp_add_data); // 0x02
    
    tmp_add_data  = 0;
    tmp_add_data |= 1   << 7; // [7]   forced_hdmi
    tmp_add_data |= 1   << 6; // [6]   hdmi_config
    tmp_add_data |= 0   << 4; // [5:4] Rsrv
    tmp_add_data |= 0   << 3; // [3]   bit_swap.
    tmp_add_data |= 0   << 0; // [2:0] channel_swap[2:0]
    hdmi_wr_reg(tx_dev_offset+TX_TMDS_MODE, tmp_add_data); // 0xc0
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]   Rsrv
    tmp_add_data |= 0   << 6; // [6]   TX_CONNECT_SEL: 0=use lower channel data[29:0]; 1=use upper channel data[59:30]
    tmp_add_data |= 0   << 0; // [5:0] Rsrv
    hdmi_wr_reg(tx_dev_offset+TX_SYS4_CONNECT_SEL_1, tmp_add_data); // 0x00
    
    // Normally it makes sense to synch 3 channel output with clock channel's rising edge,
    // as HDMI's serializer is LSB out first, invert tmds_clk pattern from "1111100000" to
    // "0000011111" actually enable data synch with clock rising edge.
    tmp_add_data = 1 << 4; // Set tmds_clk pattern to be "0000011111" before being sent to AFE clock channel
    hdmi_wr_reg(tx_dev_offset+TX_SYS4_CK_INV_VIDEO, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7] Rsrv
    tmp_add_data |= 0   << 6; // [6] TX_AFE_FIFO channel 2 bypass=0
    tmp_add_data |= 0   << 5; // [5] TX_AFE_FIFO channel 1 bypass=0
    tmp_add_data |= 0   << 4; // [4] TX_AFE_FIFO channel 0 bypass=0
    tmp_add_data |= 1   << 3; // [3] output enable of clk channel (channel 3)
    tmp_add_data |= 1   << 2; // [2] TX_AFE_FIFO channel 2 enable
    tmp_add_data |= 1   << 1; // [1] TX_AFE_FIFO channel 1 enable
    tmp_add_data |= 1   << 0; // [0] TX_AFE_FIFO channel 0 enable
    hdmi_wr_reg(tx_dev_offset+TX_SYS5_FIFO_CONFIG, tmp_add_data); // 0x0f
    
    tmp_add_data  = 0;
    tmp_add_data |= tx_output_color_format  << 6; // [7:6] output_color_format: 0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
    tmp_add_data |= tx_input_color_format   << 4; // [5:4] input_color_format:  0=RGB444; 1=YCbCr444; 2=Rsrv; 3=YCbCr422.
    tmp_add_data |= tx_output_color_depth   << 2; // [3:2] output_color_depth:  0=24-b; 1=30-b; 2=36-b; 3=48-b.
    tmp_add_data |= tx_input_color_depth    << 0; // [1:0] input_color_depth:   0=24-b; 1=30-b; 2=36-b; 3=48-b.
    hdmi_wr_reg(tx_dev_offset+TX_VIDEO_DTV_OPTION_L, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= 0                       << 4; // [7:4] Rsrv
    tmp_add_data |= tx_output_color_range   << 2; // [3:2] output_color_range:  0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
    tmp_add_data |= tx_input_color_range    << 0; // [1:0] input_color_range:   0=16-235/240; 1=16-240; 2=1-254; 3=0-255.
    hdmi_wr_reg(tx_dev_offset+TX_VIDEO_DTV_OPTION_H, tmp_add_data);

    tmp_add_data  = 0;
    tmp_add_data |= pixel_repeat_hdmi       << 4; // [7:4] pixel_repetition
    hdmi_wr_reg(tx_dev_offset+TX_VIDEO_PROC_CONFIG0, tmp_add_data);

    tmp_add_data  = 0;
    tmp_add_data |= tx_i2s_spdif    << 7; // [7]    I2S or SPDIF
    tmp_add_data |= tx_i2s_8_channel<< 6; // [6]    8 or 2ch
    tmp_add_data |= 2               << 4; // [5:4]  Serial Format: I2S format
    tmp_add_data |= 3               << 2; // [3:2]  Bit Width: 24-bit
    tmp_add_data |= 0               << 1; // [1]    WS Polarity: 0=WS low is left; 1=WS high is left
    tmp_add_data |= 1               << 0; // [0]    For I2S: 0=one-bit audio; 1=I2S;
                                          //        For SPDIF: 0= channel status from input data; 1=from register
    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_FORMAT, tmp_add_data);

    tmp_add_data  = 0;
    tmp_add_data |= 0x3 << 4; // [7:4]  FIFO Depth=256
    tmp_add_data |= 0x3 << 2; // [3:2]  Critical threshold=Depth/32
    tmp_add_data |= 0x1 << 0; // [1:0]  Normal threshold=Depth/8
    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_FIFO, tmp_add_data); // 0x3d

    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_LIPSYNC, 0); // [7:0] Normalized lip-sync param: 0 means S(lipsync) = S(total)/2

    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7]    forced_audio_fifo_clear
    tmp_add_data |= 1   << 6; // [6]    auto_audio_fifo_clear
    tmp_add_data |= audio_packet_type << 4; // [5:4]  audio_packet_type: 0=audio sample packet; 1=one bit audio; 2=HBR audio packet; 3=DST audio packet.
    tmp_add_data |= 0   << 3; // [3]    Rsrv
    tmp_add_data |= 0   << 2; // [2]    Audio sample packet's valid bit: 0=valid bit is 0 for I2S, is input data for SPDIF; 1=valid bit from register
    tmp_add_data |= 0   << 1; // [1]    Audio sample packet's user bit: 0=user bit is 0 for I2S, is input data for SPDIF; 1=user bit from register
    tmp_add_data |= 1   << 0; // [0]    0=Audio sample packet's sample_flat bit is 1; 1=sample_flat is 0.
    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_CONTROL, tmp_add_data);

    tmp_add_data  = 0;
    tmp_add_data |= tx_i2s_8_channel<< 7; // [7]    Audio sample packet's header layout bit: 0=layout0; 1=layout1
    tmp_add_data |= 0               << 6; // [6]    Set normal_double bit in DST packet header.
    tmp_add_data |= 0               << 0; // [5:0]  Rsrv
    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_HEADER, tmp_add_data);

    tmp_add_data  = tx_i2s_8_channel ? 0xff : 0x03;
    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_SAMPLE, tmp_add_data); // Channel valid for up to 8 channels, 1 bit per channel.

    hdmi_wr_reg(tx_dev_offset+TX_AUDIO_PACK, 0x01); // Enable audio sample packets

    hdmi_wr_reg(tx_dev_offset+TX_SYS0_ACR_CTS_0, 0x0a);
    hdmi_wr_reg(tx_dev_offset+TX_SYS0_ACR_CTS_1, 0x22);
    
    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7;    // [7]   Force ACR
    tmp_add_data |= 0   << 6;    // [6]   Force ACR Ready
    tmp_add_data |= 0x1 << 0;    // [3:0] CTS
    hdmi_wr_reg(tx_dev_offset+TX_SYS0_ACR_CTS_2, tmp_add_data); // 0x01
    
    // Set N = 4096 (N is not measured, N must be configured so as to be a reference to clock_meter)
    hdmi_wr_reg(tx_dev_offset+TX_SYS1_ACR_N_0, 0x00); // N[7:0]
    hdmi_wr_reg(tx_dev_offset+TX_SYS1_ACR_N_1, 0x10); // N[15:8]

    tmp_add_data  = 0;
    tmp_add_data |= 0xa << 4;    // [7:4] Meas Tolerance
    tmp_add_data |= 0x0 << 0;    // [3:0] N[19:16]
    hdmi_wr_reg(tx_dev_offset+TX_SYS1_ACR_N_2, tmp_add_data); // 0xa0

    tmp_add_data  = 0;
    tmp_add_data |= 0   << 7; // [7] cp_desired
    tmp_add_data |= 0   << 6; // [6] ess_config
    tmp_add_data |= 0   << 5; // [5] set_avmute
    tmp_add_data |= 1   << 4; // [4] clear_avmute
    tmp_add_data |= 0   << 3; // [3] hdcp_1_1
    tmp_add_data |= 0   << 2; // [2] Vsync/Hsync forced_polarity_select
    tmp_add_data |= 0   << 1; // [1] forced_vsync_polarity
    tmp_add_data |= 0   << 0; // [0] forced_hsync_polarity
    hdmi_wr_reg(tx_dev_offset+TX_HDCP_MODE, tmp_add_data); // 0x10

    // Audio Info frame
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x00, 0x01); // PB0: Checksum
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x04, tx_i2s_8_channel? 0xff : 0x03); // PB4: channel speaker allocation.
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x1B, 0x01); // PB27: Rsrv.
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x1C, 0x84); // HB0: Packet Type = 0x84
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x1E, 0x0a); // HB2: Payload length in bytes
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AUDIO_INFO_BASE_ADDR+0x1F, 0x80); // Enable audio info frame
   
    // TX Channel Status
    //0xB0 - 00000000;     //0xC8 - 00000000;
    //0xB1 - 00000000;     //0xC9 - 00000000;
    //0xB2 - 00011000;     //0xCA - 00101000;
    //0xB3 - 00000000;     //0xCB - 00000000;
    //0xB4 - 11111011;     //0xCC - 11111011;
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB1_OFFSET+0x00, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB1_OFFSET+0x01, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB1_OFFSET+0x02, 0x18);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB1_OFFSET+0x03, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB1_OFFSET+0x04, 0xfb);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB2_OFFSET+0x00, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB2_OFFSET+0x01, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB2_OFFSET+0x02, 0x28);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB2_OFFSET+0x03, 0x00);
    hdmi_wr_reg(tx_dev_offset+TX_IEC60958_SUB2_OFFSET+0x04, 0xfb);

    // AVI frame
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x00, 0xCC);              // PB0: Checksum
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x01, ((tx_output_color_format==1)? 2 : (tx_output_color_format==3)? 1 : 0)<<5);  // PB1 (Note: the value should be meaningful but is not!)
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x02, 0x34);              // PB2 (Note: the value should be meaningful but is not!)
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x03, 0x12);              // PB3 (Note: the value should be meaningful but is not!)
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x04, vic);               // PB4: [7]    Rsrv
                                                                                      //      [6:0]  VIC
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x05, pixel_repeat_hdmi); // PB5: [7:4]  Rsrv
                                                                                      //      [3:0]  PixelRepeat
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x1C, 0x82);              // HB0: packet type=0x82
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x1D, 0x02);              // HB1: packet version =0x02
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x1E, 0x0D);              // HB2: payload bytes=13
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_AVI_INFO_BASE_ADDR+0x1F, 0x80);              // Enable AVI packet generation

    // HDMI Vendor Specific Info (VSI) frame
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x00, 0xDD);                   // PB0: Checksum
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x01, 0x03);                   // PB1: 24-bit IEEE registration ID byte0
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x02, 0x0C);                   // PB2: 24-bit IEEE registration ID byte1
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x03, 0x00);                   // PB3: 24-bit IEEE registration ID byte2
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x04, ((mode_3d==0)?0:2)<<5);  // PB4: [7:5] HDMI_Video_Format
                                                                                            //      [4:0] Rsrv
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x05, ((mode_3d==1)?0:
                                                                    (mode_3d==2)?8:
                                                                    (mode_3d==3)?6:1)<<4);  // PB5: [7:4]  3D_Structure
                                                                                            //      [3:0]  Rsrv
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x1C, 0x81);                   // HB0: packet type=0x81
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x1D, 0x01);                   // HB1: packet version =0x01
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x1E, 0x06);                   // HB2: payload bytes=6
    hdmi_wr_reg(tx_dev_offset+TX_PKT_REG_VEND_INFO_BASE_ADDR+0x1F, 0x80);                   // Enable VSI packet generation

    tmp_add_data = 0xa; // time_divider[7:0] for DDC I2C bus clock
    hdmi_wr_reg(tx_dev_offset+TX_HDCP_CONFIG3, tmp_add_data);
    
    tmp_add_data  = 0;
    tmp_add_data |= hdcp_on << 7; // [7] cp_desired 
    tmp_add_data |= 1       << 6; // [6] ess_config 
    tmp_add_data |= 0       << 5; // [5] set_avmute 
    tmp_add_data |= 0       << 4; // [4] clear_avmute 
    tmp_add_data |= 1       << 3; // [3] hdcp_1_1 
    tmp_add_data |= 0       << 2; // [2] forced_polarity 
    tmp_add_data |= 0       << 1; // [1] forced_vsync_polarity 
    tmp_add_data |= 0       << 0; // [0] forced_hsync_polarity
    hdmi_wr_reg(tx_dev_offset+TX_HDCP_MODE, tmp_add_data); // 0xc8
    
    // Release TX out of reset
    hdmi_wr_reg(tx_dev_offset+TX_SYS5_TX_SOFT_RESET_1, 0x0);
    hdmi_wr_reg(tx_dev_offset+TX_SYS5_TX_SOFT_RESET_2, 0x0);
} /* hdmitx_test_function */

