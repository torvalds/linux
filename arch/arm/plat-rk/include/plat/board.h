#ifndef __PLAT_BOARD_H
#define __PLAT_BOARD_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/rk_screen.h>
#include <plat/sram.h>

struct adc_platform_data {
        int ref_volt;
        int base_chn;
        int (*get_base_volt)(void);
};
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
        int (*check_idle)(int);
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
	int pre_div;
};

struct rk29_io_t {
    unsigned long io_addr;
    unsigned long enable;
    unsigned long disable;
    int (*io_init)(void);
};

enum {
	PMIC_TYPE_NONE =0,
	PMIC_TYPE_WM8326 =1,
	PMIC_TYPE_TPS65910 =2,
	PMIC_TYPE_ACT8931 =3,
	PMIC_TYPE_MAX,
};
extern __sramdata  int g_pmic_type;
#define pmic_is_wm8326()  (g_pmic_type == PMIC_TYPE_WM8326)
#define pmic_is_tps65910()  (g_pmic_type == PMIC_TYPE_TPS65910)
#define pmic_is_act8931()  (g_pmic_type == PMIC_TYPE_ACT8931)

struct  pmu_info {
	char		*name;
	int		min_uv;
	int		max_uv;   
	int          suspend_vol;
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

struct rksdmmc_iomux {
    char    *name;  //set the MACRO of gpio
    int     fgpio;
    int     fmux;
};

struct rksdmmc_gpio {
    int     io;                             //set the address of gpio
    char    name[64];   //
    int     enable;  // disable = !enable   //set the default value,i.e,GPIO_HIGH or GPIO_LOW
    struct rksdmmc_iomux  iomux;
};


struct rksdmmc_gpio_board {
    struct rksdmmc_gpio   clk_gpio;
    struct rksdmmc_gpio   cmd_gpio;
    struct rksdmmc_gpio   data0_gpio;
    struct rksdmmc_gpio   data1_gpio;    
    struct rksdmmc_gpio   data2_gpio;
    struct rksdmmc_gpio   data3_gpio;
   
    struct rksdmmc_gpio   detect_irq;    
    struct rksdmmc_gpio   power_en_gpio;   
    struct rksdmmc_gpio   write_prt;
    struct rksdmmc_gpio   sdio_irq_gpio;
};


struct rksdmmc_gpio_wifi_moudle {
    struct rksdmmc_gpio   power_n;  //PMU_EN  
    struct rksdmmc_gpio   reset_n;  //SYSRET_B, DAIRST 
    struct rksdmmc_gpio   vddio;    //power source
    struct rksdmmc_gpio   bgf_int_b;
    struct rksdmmc_gpio   wifi_int_b;
    struct rksdmmc_gpio   gps_sync;
    struct rksdmmc_gpio   ANTSEL2;  //pin5--ANTSEL2  
    struct rksdmmc_gpio   ANTSEL3;  //pin6--ANTSEL3 
    struct rksdmmc_gpio   GPS_LAN;  //pin33--GPS_LAN
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
	int insert_card_level;
    int power_en;
	int power_en_level;
	int enable_sd_wakeup;
	int write_prt;
	int write_prt_enalbe_level;
	unsigned int sdio_INT_gpio; 
	struct rksdmmc_gpio   det_pin_info;
        int (*sd_vcc_reset)(void);
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

struct ft5606_platform_data {
    int     (*get_pendown_state)(void);
    int     (*init_platform_hw)(void);
    int     (*platform_sleep)(void);
    int     (*platform_wakeup)(void);
    void    (*exit_platform_hw)(void);
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

/* Platform data for the board id */
struct board_id_platform_data {
	int gpio_pin[32];
	int num_gpio;
	int (*init_platform_hw)(void);	
	int (*exit_platform_hw)(void);
	int (*init_parameter)(int id);  
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

struct ct360_platform_data {
	u16		model;
	u16		x_max;
	u16		y_max;
	void 	(*hw_init)(void);
	void 	(*shutdown)(int);
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
        int(*rmii_speed_switch)(int speed);
};
//#if defined  CONFIG_BATTERY_RK30_ADC_FAC 
/* adc battery */
struct rk30_adc_battery_platform_data {
	int (*io_init)(void);
	int (*io_deinit)(void);
	int (*is_dc_charging)(void);
	int (*charging_ok)(void);

	int (*is_usb_charging)(void);
	int spport_usb_charging ;
	int (*control_usb_charging)(int);

	int is_reboot_charging;

	int dc_det_pin;
	int batt_low_pin;
	int charge_ok_pin;
	int charge_set_pin;

	int dc_det_level;
	int batt_low_level;
	int charge_ok_level;
	int charge_set_level;
	
      	int adc_channel;

	int dc_det_pin_pull;    //pull up/down enable/disbale
	int batt_low_pin_pull;
	int charge_ok_pin_pull;
	int charge_set_pin_pull;

	int low_voltage_protection; // low voltage protection

	int charging_sleep; // don't have lock,if chargeing_sleep = 0;else have lock
	

	int save_capacity;  //save capacity to /data/bat_last_capacity.dat,  suggested use

	int reference_voltage; // the rK2928 is 3300;RK3066 and rk29 are 2500;rk3066B is 1800;
	int pull_up_res;      //divider resistance ,  pull-up resistor
	int pull_down_res; //divider resistance , pull-down resistor

	int time_down_discharge; //the time of capactiy drop 1% --discharge
	int time_up_charge; //the time of capacity up 1% ---charging 

	int  use_board_table;
	int  table_size;
	int  *discharge_table;
	int  *charge_table;
	int  *property_tabel;
	int *board_batt_table;

};
//#endif


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
