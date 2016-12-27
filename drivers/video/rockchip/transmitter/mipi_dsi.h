/*
drivers/video/rockchip/transmitter/mipi_dsi.h
*/
#ifndef MIPI_DSI_H_
#define MIPI_DSI_H_

#ifdef CONFIG_MIPI_DSI_FT
#include "..\..\common\config.h"
#endif

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <dt-bindings/gpio/gpio.h>
#endif
#ifdef CONFIG_RK_3288_DSI_UBOOT
#include <linux/list.h>
#endif


//DSI DATA TYPE
#define DTYPE_DCS_SWRITE_0P		0x05 
#define DTYPE_DCS_SWRITE_1P		0x15 
#define DTYPE_DCS_LWRITE		0x39 
#define DTYPE_GEN_LWRITE		0x29 
#define DTYPE_GEN_SWRITE_2P		0x23 
#define DTYPE_GEN_SWRITE_1P		0x13
#define DTYPE_GEN_SWRITE_0P		0x03

//command transmit mode
#define HSDT			0x00
#define LPDT			0x01

//DSI DATA TYPE FLAG
#define DATA_TYPE_DCS			0x00
#define DATA_TYPE_GEN			0x01


//Video Mode
#define VM_NBMWSP		0x00  //Non burst mode with sync pulses
#define VM_NBMWSE		0x01  //Non burst mode with sync events
#define VM_BM			0x02  //Burst mode

//Video Pixel Format
#define VPF_16BPP		0x00
#define VPF_18BPP		0x01	 //packed
#define VPF_18BPPL		0x02     //loosely packed
#define VPF_24BPP		0x03

//Display Command Set
#define dcs_enter_idle_mode 		0x39
#define dcs_enter_invert_mode 		0x21
#define dcs_enter_normal_mode 		0x13
#define dcs_enter_partial_mode  	0x12
#define dcs_enter_sleep_mode  		0x10
#define dcs_exit_idle_mode  		0x38
#define dcs_exit_invert_mode  		0x20
#define dcs_exit_sleep_mode  		0x11
#define dcs_get_address_mode  		0x0b
#define dcs_get_blue_channel  		0x08
#define dcs_get_diagnostic_result  	0x0f
#define dcs_get_display_mode  		0x0d
#define dcs_get_green_channel  		0x07
#define dcs_get_pixel_format  		0x0c
#define dcs_get_power_mode  		0x0a
#define dcs_get_red_channel 		0x06
#define dcs_get_scanline 	 		0x45
#define dcs_get_signal_mode  		0x0e
#define dcs_nop				 		0x00
#define dcs_read_DDB_continue  		0xa8
#define dcs_read_DDB_start  		0xa1
#define dcs_read_memory_continue  	0x3e
#define dcs_read_memory_start  		0x2e
#define dcs_set_address_mode  		0x36
#define dcs_set_column_address  	0x2a
#define dcs_set_display_off  		0x28
#define dcs_set_display_on  		0x29
#define dcs_set_gamma_curve  		0x26
#define dcs_set_page_address  		0x2b
#define dcs_set_partial_area  		0x30
#define dcs_set_pixel_format  		0x3a
#define dcs_set_scroll_area  		0x33
#define dcs_set_scroll_start  		0x37
#define dcs_set_tear_off 	 		0x34
#define dcs_set_tear_on 	 		0x35
#define dcs_set_tear_scanline  		0x44
#define dcs_soft_reset 		 		0x01
#define dcs_write_LUT 		 		0x2d
#define dcs_write_memory_continue  	0x3c
#define dcs_write_memory_start 		0x2c

#ifndef MHz
#define MHz   1000000
#endif


#if 0
typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long s64;
typedef unsigned long u64;
#endif


//iomux
#define OLD_RK_IOMUX 0

struct spi_t {
	u32 cs;
#if OLD_RK_IOMUX	
	char* cs_mux_name;
#endif	
	int sck;
#if OLD_RK_IOMUX	
	char* sck_mux_name;
#endif	
	int miso;
#if OLD_RK_IOMUX	
	char* miso_mux_name;
#endif	
	int mosi;
#if OLD_RK_IOMUX	
	char* mosi_mux_name;
#endif	
};

struct power_t {
	int	enable_pin;    //gpio that control power
#if OLD_RK_IOMUX	
	char* mux_name;
	u32 mux_mode;
#endif	
	u32 effect_value;
	
	char *name;
	u32 voltage;
	int (*enable)(void *);
	int (*disable)(void *);
};

struct reset_t {
	int	reset_pin;    //gpio that control reset
#if OLD_RK_IOMUX	
	char* mux_name;
	u32 mux_mode;
#endif	
	u32 effect_value;
	
	u32 time_before_reset;    //ms
 	u32 time_after_reset;
	
	int (*do_reset)(void *);
};

struct tc358768_t {
	u32 id;
	struct reset_t reset;
	struct power_t vddc;
	struct power_t vddio;
	struct power_t vdd_mipi;
	struct i2c_client *client;
	int (*gpio_init)(void *);
	int (*gpio_deinit)(void *);
	int (*power_up)(void);
	int (*power_down)(void);
};


struct ssd2828_t {
	u32 id;
	struct reset_t reset;
	struct power_t shut;
	struct power_t vddio;
	struct power_t vdd_mipi;
	
	struct spi_t spi;
	int (*gpio_init)(void *);
	int (*gpio_deinit)(void *);
	int (*power_up)(void);
	int (*power_down)(void);
};

struct mipi_dsi_ops {
	u32 id;
	char name[32];
	void *dsi;
	int (*get_id)(void *);
	int (*dsi_init)(void *, u32 n);
	int (*dsi_set_regs)(void *, void *, u32 n);
	int (*dsi_enable_video_mode)(void *, u32 enable);
	int (*dsi_enable_command_mode)(void *, u32 enable);
	int (*dsi_enable_hs_clk)(void *, u32 enable);
	int (*dsi_send_dcs_packet)(void *, unsigned char *, u32 n);
	int (*dsi_read_dcs_packet)(void *, unsigned char *, u32 n);
	int (*dsi_send_packet)(void *, unsigned char *, u32 n);
    int (*dsi_is_enable)(void *, u32 enable);
	int (*dsi_is_active)(void *);
	int (*power_up)(void *);
	int (*power_down)(void *);
};

/* Screen description */
struct mipi_dsi_screen {

	u16 type;
	u16 face;
	u8 lcdc_id;    
	u8 screen_id; 

	/* Timing */
	u32 pixclock;
	u16 left_margin;
	u16 right_margin;
	u16 hsync_len;
	u16 upper_margin;
	u16 lower_margin;
	u16 vsync_len;
	
	/* Screen size */
	u16 x_res;
	u16 y_res;
	u16 width;
	u16 height;
	/* Pin polarity */
	u8 pin_hsync;
	u8 pin_vsync;
	u8 pin_den;
	u8 pin_dclk;

	/* MIPI DSI */
	u8 dsi_lane;
	u16 refresh_mode;
	u32 hs_tx_clk;
	struct rk_screen *screen;

	/* Operation function*/
	int (*init)(void);
	int (*standby)(u8 enable);

};

#define INVALID_GPIO        -1

struct dcs_cmd {
	u8 type;
	u8 dtype;
	u8 dsi_id;
	u8 cmd_len;
	int *cmds;
	int delay;
	char name[32];
};

struct mipi_dcs_cmd_ctr_list {
	struct list_head list;
	struct dcs_cmd dcs_cmd;
};

enum {
	VIDEO_MODE = 0,
	COMMAND_MODE,
};

struct mipi_screen
{
	u8 screen_init;
	u8 mipi_dsi_num;
	u8 lcd_rst_atv_val;
	u8 lcd_en_atv_val;
	u8 dsi_lane;
	u8 dsi_operate_mode;
	u32 hs_tx_clk;
	u32 lcd_en_gpio;
	u32 lcd_en_delay;
	u32 lcd_rst_gpio;
	u32 lcd_rst_delay;

	struct list_head cmdlist_head;
};

int register_dsi_ops(unsigned int id, struct mipi_dsi_ops *ops);
int del_dsi_ops(struct mipi_dsi_ops *ops);
int dsi_power_up(unsigned int id);
int dsi_power_off(unsigned int id);
int dsi_probe_current_chip(unsigned int id);
int dsi_init(unsigned int id, u32 n);
int dsi_is_active(unsigned int id);
int dsi_enable_video_mode(unsigned int id, u32 enable);
int dsi_enable_command_mode(unsigned int id, u32 enable);
int dsi_enable_hs_clk(unsigned int id, u32 enable);
int dsi_set_virtual_channel(unsigned int id, u32 channel);

int dsi_set_regs(unsigned int id, void *array, u32 n);
int dsi_send_dcs_packet(unsigned int id, unsigned char *packet, u32 n);
int dsi_read_dcs_packet(unsigned int id, unsigned char *packet, u32 n);
int dsi_send_packet(unsigned int id, unsigned char *packet, u32 n);
int dsi_is_enable(unsigned int id, u32 enable);

#endif /* end of MIPI_DSI_H_ */
