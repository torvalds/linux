
#ifndef TC358768_H_
#define TC358768_H_

#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/rk_fb.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <linux/rk_screen.h>
#include <linux/ktime.h>


//DSI DATA TYPE
#define DTYPE_DCS_SWRITE_0P		0X05 
#define DTYPE_DCS_SWRITE_1P		0X15 
#define DTYPE_DCS_LWRITE		0X39 
#define DTYPE_GEN_LWRITE		0X29 
#define DTYPE_GEN_SWRITE_2P		0X23 
#define DTYPE_GEN_SWRITE_1P		0X13
#define DTYPE_GEN_SWRITE_0P		0X03

#define CONFIG_TC358768_I2C     1
#define CONFIG_TC358768_I2C_CLK     400*1000

#if !CONFIG_TC358768_I2C 
/* define spi write command and data interface function */
#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin
#define LCD_RST_PORT    gLcd_info->reset_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
#define LCD_RST_OUT(i)   gpio_direction_output(LCD_RST_PORT, i)
#define LCD_RST(i)      gpio_set_value(LCD_RST_PORT, i)
#endif


struct mipi_dsi_t {
	u32 id;
	int (*dsi_init)(void *, int n);
	int (*dsi_hs_start)(void *, int n);
	int (*dsi_send_dcs_packet)(unsigned char *, int n);
	int (*dsi_read_dcs_packet)(unsigned char *, int n);
	int (*dsi_send_packet)(void *, int n);

	void *chip;	
};

struct power_t {
	int	enable_pin;    //gpio that control power
	char* mux_name;
	u32 mux_mode;
	u32 effect_value;
	
	u32 min_voltage;
	u32 max_voltage;
	int (*enable)(void *);
	int (*disable)(void *);
};

struct reset_t {
	int	reset_pin;    //gpio that control reset
	char* mux_name;
	u32 mux_mode;
	u32 effect_value;
	
	u32 time_before_reset;    //ms
 	u32 time_after_reset;
	
	int (*do_reset)(void *);
};

struct tc358768_t {
	struct reset_t reset;
	struct power_t vddc;
	struct power_t vddio;
	struct power_t vdd_mipi;
	struct i2c_client *client;
	int (*gpio_init)(void *);
	int (*gpio_deinit)(void *);
	int (*power_up)(void *);
	int (*power_down)(void *);
};


int tc358768_init(struct mipi_dsi_t *pram);
u32 tc358768_wr_reg_32bits_delay(u32 delay, u32 data);

#endif /* end of TC358768_H_ */
