#ifndef __PLAT_BOARD_H
#define __PLAT_BOARD_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/rk_screen.h>

enum {
        I2C_IDLE = 0,
        I2C_SDA_LOW,
        I2C_SCL_LOW,
        BOTH_LOW,
};
struct rk30_i2c_platform_data {
	char *name;
	int bus_num;
#define I2C_RK29_ADAP   0
#define I2C_RK30_ADAP   1
	int adap_type;
	int is_div_from_arm;
	u32 flags;
	int (*io_init)(void);
	int (*io_deinit)(void);
        int (*check_idle)(void);
};

struct spi_cs_gpio {
	const char *name;
	unsigned int cs_gpio;
	char *cs_iomux_name;
	unsigned int cs_iomux_mode;
};

struct rk29xx_spi_platform_data {
	int (*io_init)(struct spi_cs_gpio*, int);
	int (*io_deinit)(struct spi_cs_gpio*, int);
	int (*io_fix_leakage_bug)(void);
	int (*io_resume_leakage_bug)(void);
	struct spi_cs_gpio *chipselect_gpios;
	u16 num_chipselect;
};

struct rk29_bl_info {
	u32 pwm_id;
	u32 bl_ref;
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*pwm_suspend)(void);
	int (*pwm_resume)(void);
	int min_brightness;	/* 0 ~ 255 */
	unsigned int delay_ms;	/* in milliseconds */
};

struct rk29_io_t {
    unsigned long io_addr;
    unsigned long enable;
    unsigned long disable;
    int (*io_init)(void);
};



struct rk29_fb_setting_info {
	u8 data_num;
	u8 vsync_en;
	u8 den_en;
	u8 mcu_fmk_en;
	u8 disp_on_en;
	u8 standby_en;
};

struct rk29fb_info {
	u32 fb_id;
	enum rk_disp_prop prop;		//display device property,like PRMRY,EXTEND
	u32 mcu_fmk_pin;
	struct rk29lcd_info *lcd_info;
	int (*io_init)(struct rk29_fb_setting_info *fb_setting);
	int (*io_deinit)(void);
	int (*io_enable)(void);
	int (*io_disable)(void);
	void (*set_screen_info)(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info );
};

struct rk29_sdmmc_platform_data {
	unsigned int host_caps;
	unsigned int host_ocr_avail;
	unsigned int use_dma:1;
	char dma_name[8];
	int (*io_init)(void);
	int (*io_deinit)(void);
	void (*set_iomux)(int device_id, unsigned int bus_width);//added by xbw at 2011-10-13
	int (*status)(struct device *);
	int (*register_status_notify)(void (*callback)(int card_present, void *dev_id), void *dev_id);
	int detect_irq;
	int enable_sd_wakeup;
	int write_prt;
	unsigned int sdio_INT_gpio; //add gpio INT for sdio interrupt.Modifed by xbw at 2012-08-09
};

struct gsensor_platform_data {
	u16 model;
	u16 swap_xy;
	u16 swap_xyz;
	signed char orientation[9];
	int (*get_pendown_state)(void);
	int (*init_platform_hw)(void);
	int (*gsensor_platform_sleep)(void);
	int (*gsensor_platform_wakeup)(void);
	void (*exit_platform_hw)(void);
};


struct akm8975_platform_data {
	short m_layout[4][3][3];
	char project_name[64];
	int gpio_DRDY;
};

struct sensor_platform_data {
	int type;
	int irq;
	int power_pin;
	int reset_pin;
	int irq_enable;         //if irq_enable=1 then use irq else use polling  
	int poll_delay_ms;      //polling
	int x_min;              //filter
	int y_min;
	int z_min;
	int factory;
	unsigned char address;
	signed char orientation[9];
	short m_layout[4][3][3];
	char project_name[64];
	int (*init_platform_hw)(void);
	void (*exit_platform_hw)(void);
	int (*power_on)(void);
	int (*power_off)(void);
};


struct goodix_platform_data {
	int model ;
	int rest_pin;
	int irq_pin ;
	int (*get_pendown_state)(void);
	int (*init_platform_hw)(void);
	int (*platform_sleep)(void);
	int (*platform_wakeup)(void);
	void (*exit_platform_hw)(void);
};

struct cm3217_platform_data {
	int irq_pin;
	int power_pin;
	int (*init_platform_hw)(void);
	void (*exit_platform_hw)(void);
};

struct irda_info {
	u32 intr_pin;
	int (*iomux_init)(void);
	int (*iomux_deinit)(void);
	int (*irda_pwr_ctl)(int en);
};

struct rk29_gpio_expander_info {
	unsigned int gpio_num;
	unsigned int pin_type;	//GPIO_IN or GPIO_OUT
	unsigned int pin_value;	//GPIO_HIGH or GPIO_LOW
};

/*vmac*/
struct rk29_vmac_platform_data {
	int (*vmac_register_set)(void);
	int (*rmii_io_init)(void);
	int (*rmii_io_deinit)(void);
	int (*rmii_power_control)(int enable);
};

#define BOOT_MODE_NORMAL		0
#define BOOT_MODE_FACTORY2		1
#define BOOT_MODE_RECOVERY		2
#define BOOT_MODE_CHARGE		3
#define BOOT_MODE_POWER_TEST		4
#define BOOT_MODE_OFFMODE_CHARGING	5
#define BOOT_MODE_REBOOT		6
#define BOOT_MODE_PANIC			7
int board_boot_mode(void);

/* for USB detection */
#ifdef CONFIG_USB_GADGET
int __init board_usb_detect_init(unsigned gpio);
#else
static int inline board_usb_detect_init(unsigned gpio) { return 0; }
#endif

#ifdef CONFIG_RK_EARLY_PRINTK
void __init rk29_setup_early_printk(void);
#else
static void inline rk29_setup_early_printk(void) {}
#endif

/* for wakeup Android */
void rk28_send_wakeup_key(void);

/* for reserved memory 
 * function: board_mem_reserve_add 
 * return value: start address of reserved memory */
phys_addr_t __init board_mem_reserve_add(char *name, size_t size);
void __init board_mem_reserved(void);

#endif
