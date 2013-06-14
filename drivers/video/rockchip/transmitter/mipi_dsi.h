
//drivers/video/display/transmitter/mipi_dsi.h

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

//Video Mode
#define VM_NBMWSP		0X00  //Non burst mode with sync pulses
#define VM_NBMWSE		0X01  //Non burst mode with sync events
#define VM_BM			0X02  //Burst mode

//Video Pixel Format
#define VPF_16BPP		0X00
#define VPF_18BPP		0X01	 //packed
#define VPF_18BPPL		0X02     //loosely packed
#define VPF_24BPP		0X03

//iomux
#define OLD_RK_IOMUX 0

struct spi_t {
	int cs;
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
	char *name;
	int (*get_id)(void);
	int (*dsi_init)(void *, u32 n);
	int (*dsi_set_regs)(void *, u32 n);
	int (*dsi_send_dcs_packet)(unsigned char *, u32 n);
	int (*dsi_read_dcs_packet)(unsigned char *, u32 n);
	int (*dsi_send_packet)(void *, u32 n);
	int (*power_up)(void);
	int (*power_down)(void);	
};


int register_dsi_ops(struct mipi_dsi_ops *ops);
int del_dsi_ops(struct mipi_dsi_ops *ops);
int dsi_power_up(void);
int dsi_power_off(void);
int dsi_probe_current_chip(void);
int dsi_init(void *array, u32 n);

int dsi_enable_video_mode(u32 enable);
int dsi_set_virtual_channel(u32 channel);

int dsi_set_regs(void *array, u32 n);
int dsi_send_dcs_packet(unsigned char *packet, u32 n);
int dsi_read_dcs_packet(unsigned char *packet, u32 n);
int dsi_send_packet(void *packet, u32 n);
#endif /* end of MIPI_DSI_H_ */
