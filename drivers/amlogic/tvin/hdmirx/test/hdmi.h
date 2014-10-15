#ifndef HDMI_H
#define HDMI_H
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
//#include <linux/amports/canvas.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <mach/regs.h>
#include <mach/clock.h>
#include <mach/register.h>
#include <mach/power_gate.h>

#define stimulus_print printk
#define stimulus_finish_fail(a)

#define HDMI_ADDR_PORT   0xc8002000
#define HDMI_DATA_PORT   0xc8002004
#define HDMI_CTRL_PORT   0xc8002008

// Use the following functions to access the on-chip HDMI modules by default
extern void hdmi_wr_reg(unsigned long addr, unsigned long data);
extern void hdmi_wr_only_reg(unsigned long addr, unsigned long data);
extern unsigned long hdmi_rd_reg(unsigned long addr);
extern void hdmi_rd_check_reg(unsigned long addr, unsigned long exp_data, unsigned long mask);
extern void hdmi_poll_reg(unsigned long addr, unsigned long exp_data, unsigned long mask);

// Use the following functions to access the off-chip HDMI modules by default, input address the same as on-chip HDMI's,
// the regisers accessed by these functions only exist in a simulation environment.
extern void ext_hdmi_wr_reg(unsigned long addr, unsigned long data);
extern void ext_hdmi_wr_only_reg(unsigned long addr, unsigned long data);
extern unsigned long ext_hdmi_rd_reg(unsigned long addr);
extern void ext_hdmi_rd_check_reg(unsigned long addr, unsigned long exp_data, unsigned long mask);
extern void ext_hdmi_poll_reg(unsigned long addr, unsigned long exp_data, unsigned long mask);

extern void set_hdmi_audio_source (unsigned int src);

extern void hdmitx_test_function (  unsigned long   tx_dev_offset,          // 0x00000: Internal TX; 0x10000: External TX.
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
                                    unsigned char   audio_packet_type);     // 0=audio sample packet; 1=one bit audio; 2=HBR audio packet; 3=DST audio packet.

// Internal functions:
void hdmi_tx_key_setting (unsigned long tx_dev_offset);

//------------------------------------------------------------------------------
// Defines to communicate from C code to verilog world
//------------------------------------------------------------------------------

#define STIMULUS_HDMI_UTIL_SET_VIC                  0x01000000
#define STIMULUS_HDMI_UTIL_SET_HSYNC_0              0x02000000
#define STIMULUS_HDMI_UTIL_SET_HSYNC_1              0x03000000
#define STIMULUS_HDMI_UTIL_SET_VSYNC_0              0x04000000
#define STIMULUS_HDMI_UTIL_SET_VSYNC_1              0x05000000
#define STIMULUS_HDMI_UTIL_SET_HV_TOTAL             0x06000000
#define STIMULUS_HDMI_UTIL_VANLYZ_RESET             0x07000000
#define STIMULUS_HDMI_UTIL_AANLYZ_EN                0x08000000
#define STIMULUS_HDMI_UTIL_SEL_RX_PORT              0x09000000
#define STIMULUS_HDMI_UTIL_SET_TOTAL_FRAMES         0x0a000000
#define STIMULUS_HDMI_UTIL_VGEN_RESET               0x0b000000
#define STIMULUS_HDMI_UTIL_AGEN_CTRL                0x0c000000
#define STIMULUS_HDMI_UTIL_AGEN_ENABLE              0x0d000000
#define STIMULUS_HDMI_UTIL_AIU_ANLYZ_EN             0x0e000000
#define STIMULUS_HDMI_UTIL_CEC1_CHK_REG_RD          0x0f000000
#define STIMULUS_HDMI_UTIL_AGEN_CTRL_2              0x10000000
#define STIMULUS_HDMI_UTIL_INVERT_HPD               0x11000000
#define STIMULUS_HDMI_UTIL_INIT_FIELD_NUM           0x12000000
#define STIMULUS_HDMI_UTIL_CALC_PLL_CONFIG          0x13000000
#define STIMULUS_HDMI_UTIL_VID_FORMAT               0x14000000
#define STIMULUS_HDMI_UTIL_ARCTX_MODE               0x15000000
#define STIMULUS_HDMI_UTIL_CHECK_HPD                0x16000000
#define STIMULUS_HDMI_UTIL_AOCEC_CHK_PARAM          0x17000000

#define STIMULUS_EVENT_EXT_HDMI_TX_PLL              30

#endif
