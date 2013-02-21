
#ifndef MIPI_DSI_H_
#define MIPI_DSI_H_

#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/rk_fb.h>
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

struct spi_t {
	int cs;
	char* cs_mux_name;
	int sck;
	char* sck_mux_name;
	int miso;
	char* miso_mux_name;
	int mosi;
	char* mosi_mux_name;
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
	char *name;
	int (*get_id)(void);
	int (*dsi_init)(void *, int n);
	int (*dsi_set_regs)(void *, int n);
	int (*dsi_send_dcs_packet)(unsigned char *, int n);
	int (*dsi_read_dcs_packet)(unsigned char *, int n);
	int (*dsi_send_packet)(void *, int n);
	int (*power_up)(void);
	int (*power_down)(void);	
};


int register_dsi_ops(struct mipi_dsi_ops *ops);
int del_dsi_ops(struct mipi_dsi_ops *ops);
int dsi_power_up(void);
int dsi_power_off(void);
int dsi_probe_current_chip(void);
int dsi_init(void *array, int n);
int dsi_set_regs(void *array, int n);
int dsi_send_dcs_packet(unsigned char *packet, int n);
int dsi_read_dcs_packet(unsigned char *packet, int n);
int dsi_send_packet(void *packet, int n);
#endif /* end of MIPI_DSI_H_ */
