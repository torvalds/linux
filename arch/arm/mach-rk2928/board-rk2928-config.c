#if 1
#define CONFIG_ERR(v, name)     do { printk("%s: Invalid parameter: %s(%d)\n", __func__, (name), (v)); } while(0)
#else
#define CONFIG_ERR(v, name)
#endif
#include <mach/config.h>
int __sramdata g_pmic_type =  0;

struct pwm_io_config{
        int id;
        int gpio;
        char *mux_name;
        unsigned int io_mode;
        unsigned int pwm_mode;
};
static struct pwm_io_config pwm_cfg[] = {
        {
                .id = 0,
                .gpio = RK2928_PIN0_PD2,
                .mux_name = GPIO0D2_PWM_0_NAME,
                .io_mode = GPIO0D_GPIO0D2,
                .pwm_mode = GPIO0D_PWM_0,
        },
        {
                .id = 1,
                .gpio = RK2928_PIN0_PD3,
                .mux_name = GPIO0D3_PWM_1_NAME,
                .io_mode = GPIO0D_GPIO0D3,
                .pwm_mode = GPIO0D_PWM_1,
        },
        {
                .id = 2,
                .gpio = RK2928_PIN0_PD4,
                .mux_name = GPIO0D4_PWM_2_NAME,
                .io_mode = GPIO0D_GPIO0D4,
                .pwm_mode = GPIO0D_PWM_2,
        },
};

#define TPS65910_HOST_IRQ INVALID_GPIO
static struct pmu_info tps65910_dcdc_info[] = {
        {.name = "vdd_cpu"},
        {.name = "vdd2"},
        {.name = "vdd3"},
        {.name = "vio"},
};
static struct pmu_info tps65910_ldo_info[] = {
        {.name = "vdig1"},
        {.name = "vdig2"},
        {.name = "vaux1"},
        {.name = "vaux2"},
        {.name = "vaux33"},
        {.name = "vmmc"},
        {.name = "vdac"},
        {.name = "vpll"},
};
static struct pmu_info act8931_dcdc_info[] = {
        {.name = "act_dcdc1"},
        {.name = "act_dcdc2"},
        {.name = "vdd_cpu"},
};
static struct pmu_info act8931_ldo_info[] = {
        {.name = "act_ldo1"},
        {.name = "act_ldo2"},
        {.name = "act_ldo3"},
        {.name = "act_ldo4"},
};

/*************************************** parameter ******************************************/
/* keyboard */
uint key_adc = DEF_KEY_ADC;
uint key_val_size = 7;
uint key_val[] = {DEF_PLAY_KEY, DEF_VOLDN_KEY, DEF_VOLUP_KEY, DEF_MENU_KEY, DEF_ESC_KEY, DEF_HOME_KEY, DEF_CAM_KEY};
module_param_array(key_val, uint, &key_val_size, 0644);

static inline int check_key_param(void)
{
        return 0;
}
/* backlight */
static uint bl_pwm = DEF_BL_PWM;
module_param(bl_pwm, uint, 0644);
static uint bl_ref = DEF_BL_REF; 
module_param(bl_ref, uint, 0644);
static uint bl_min = DEF_BL_MIN;
module_param(bl_min, uint, 0644);
static int bl_en = DEF_BL_EN;
module_param(bl_en, int, 0644);

static inline int check_bl_param(void)
{
        if(bl_pwm < 0 || bl_pwm >= ARRAY_SIZE(pwm_cfg)){
                CONFIG_ERR(bl_pwm, "bl_pwm");
                return -EINVAL;
        }
        if(bl_ref != 0 && bl_ref != 1){
                CONFIG_ERR(bl_ref, "bl_ref");
                return -EINVAL;
        }
        if(bl_min > 100){
                CONFIG_ERR(bl_min, "bl_min");
                return -EINVAL;
        }
        return 0;
}
/* usb */
static int otg_drv = DEF_OTG_DRV;
module_param(otg_drv, int, 0644);
static int host_drv = DEF_HOST_DRV;
module_param(host_drv, int, 0644);
static inline int check_usb_param(void)
{
        return 0;
}
int inline otg_drv_init(int on)
{
        if(otg_drv == -1){
                return 0;
        }else if (get_port_config(otg_drv).gpio == RK2928_PIN3_PC1){
                rk30_mux_api_set(GPIO3C1_OTG_DRVVBUS_NAME, GPIO3C_OTG_DRVVBUS);    
                return 0;
        }else
                return port_output_init(otg_drv, on, "otg_drv");
}
void inline otg_drv_on(void)
{
        port_output_on(otg_drv);
}
void inline otg_drv_off(void)
{
        port_output_off(otg_drv);
}
int inline host_drv_init(int on)
{
        return port_output_init(host_drv, on, "host_drv");
}
void inline host_drv_on(void)
{
        port_output_on(host_drv);
}
void inline host_drv_off(void)
{
        port_output_off(host_drv);
}
/* lcd */
static int lcd_cabc = DEF_LCD_CABC;
module_param(lcd_cabc, int, 0644);
static int lcd_en = DEF_LCD_EN;
module_param(lcd_en, int, 0644);
static int lcd_std = DEF_LCD_STD;
module_param(lcd_std, int, 0644);

static inline int check_lcd_param(void)
{
        return 0;

}

/* gsensor */
static int gs_type = DEF_GS_TYPE;
module_param(gs_type, int, 0644);
static int gs_i2c = DEF_GS_I2C;
module_param(gs_i2c, int, 0644);
static int gs_addr = DEF_GS_ADDR;
module_param(gs_addr, int, 0644);
static int gs_irq = DEF_GS_IRQ;
module_param(gs_irq, int, 0644);
static int gs_pwr = DEF_GS_PWR;
module_param(gs_pwr, int, 0644);
static int gs_orig[9] = DEF_GS_ORIG;
module_param_array(gs_orig, int, NULL, 0644);

static inline int check_gs_param(void)
{
        int i;

        if(gs_type == GS_TYPE_NONE)
                return 0;
        if(gs_type < GS_TYPE_NONE || gs_type > GS_TYPE_MAX){
                CONFIG_ERR(gs_type, "gs_type");
                return -EINVAL;
        }
        if(gs_i2c < 0 || gs_i2c > 3){
                CONFIG_ERR(gs_i2c, "gs_i2c");
                return -EINVAL;
        }
        if(gs_addr < 0 || gs_addr > 0x7f){
                CONFIG_ERR(gs_i2c, "gs_addr");
                return -EINVAL;
        }
        for(i = 0; i < 9; i++){
                if(gs_orig[i] != 1 && gs_orig[i] != 0 && gs_orig[i] != -1){
                        CONFIG_ERR(gs_orig[i], "gs_orig[x]");
                        return -EINVAL;
                }
        }
        return 0;
}
/* lsensor */
static int ls_type = DEF_LS_TYPE;

static int ls_i2c = DEF_LS_I2C;
module_param(ls_i2c, int, 0644);
static int ls_addr = DEF_LS_ADDR;
module_param(ls_addr, int, 0644);
static int ls_irq = DEF_LS_IRQ;
module_param(ls_irq, int, 0644);
static int ls_pwr = DEF_LS_PWR;
module_param(ls_pwr, int, 0644);

static inline int check_ls_param(void)
{
        if(ls_type == LS_TYPE_NONE)
                return 0;
        if(ls_type < LS_TYPE_NONE || ls_type > LS_TYPE_MAX){
                CONFIG_ERR(ls_type, "ls_type");
                return -EINVAL;
        }
        if(ls_i2c < 0 || ls_i2c > 3){
                CONFIG_ERR(ls_i2c, "ls_i2c");
                return -EINVAL;
        }
        if(ls_addr < 0 || ls_addr > 0x7f){
                CONFIG_ERR(ls_i2c, "ls_addr");
                return -EINVAL;
        }
        return 0;
}


/* psensor */
static int ps_type = DEF_PS_TYPE;

static int ps_i2c = DEF_PS_I2C;
module_param(ps_i2c, int, 0644);
static int ps_addr = DEF_PS_ADDR;
module_param(ps_addr, int, 0644);
static int ps_irq = DEF_PS_IRQ;
module_param(ps_irq, int, 0644);
static int ps_pwr = DEF_PS_PWR;
module_param(ps_pwr, int, 0644);

static inline int check_ps_param(void)
{
        if(ps_type == PS_TYPE_NONE)
                return 0;
        if(ps_type < PS_TYPE_NONE || ps_type > PS_TYPE_MAX){
                CONFIG_ERR(ps_type, "ps_type");
                return -EINVAL;
        }
        if(ps_i2c < 0 || ps_i2c > 3){
                CONFIG_ERR(ps_i2c, "ps_i2c");
                return -EINVAL;
        }
        if(ps_addr < 0 || ps_addr > 0x7f){
                CONFIG_ERR(ps_i2c, "ps_addr");
                return -EINVAL;
        }
        return 0;
}
/* camera */
static int front_cam_i2c = DEF_FRONT_CAM_I2C;
module_param(front_cam_i2c, int, 0644);
static int front_cam_id = DEF_FRONT_CAM_ID;
module_param(front_cam_id, uint, 0644);
static int front_cam_pwr = DEF_FRONT_CAM_PWR;
module_param(front_cam_pwr, int, 0644);

static int back_cam_i2c = DEF_BACK_CAM_I2C;
module_param(back_cam_i2c, int, 0644);
static int back_cam_id = DEF_BACK_CAM_ID;
module_param(back_cam_id, uint, 0644);
static int back_cam_pwr = DEF_BACK_CAM_PWR;
module_param(back_cam_pwr, int, 0644);

static inline int check_cam_param(void)
{
        return 0;
}

/* pwm regulator */
static int __sramdata reg_pwm = DEF_REG_PWM;
module_param(reg_pwm, int, 0644);
static inline int check_reg_pwm_param(void)
{
        if(reg_pwm < 0 || reg_pwm >= ARRAY_SIZE(pwm_cfg)){  
                CONFIG_ERR(reg_pwm, "reg_pwm");
                return -EINVAL;
        }

        return 0;
}

/* pmic */
static uint pmic_type = DEF_PMIC_TYPE;
module_param(pmic_type, uint, 0644);
static __sramdata int pmic_slp = DEF_PMIC_SLP;
module_param(pmic_slp, int, 0644);
static int pmic_irq = DEF_PMIC_IRQ;
module_param(pmic_irq, int, 0644);
static int pmic_i2c = DEF_PMIC_I2C;
module_param(pmic_i2c, int, 0644);
static int pmic_addr = DEF_PMIC_ADDR;
module_param(pmic_addr, int, 0644);

static int tps65910_dcdc[] = DEF_TPS65910_DCDC;
module_param_array(tps65910_dcdc, int, NULL, 0644);
static int tps65910_ldo[] = DEF_TPS65910_LDO;
module_param_array(tps65910_ldo, int, NULL, 0644);

static int act8931_dcdc[] = DEF_ACT8931_DCDC;
module_param_array(act8931_dcdc, int, NULL, 0644);
static int act8931_ldo[] = DEF_ACT8931_LDO;
module_param_array(act8931_ldo, int, NULL, 0644);

static inline int check_pmic_param(void)
{
        if(pmic_type <= PMIC_TYPE_WM8326 || pmic_type >= PMIC_TYPE_MAX){
                CONFIG_ERR(pmic_type, "pmic_type");
                return -EINVAL;
        }
        if(pmic_i2c < 0 || pmic_i2c > 3){
                CONFIG_ERR(pmic_i2c, "pmic_i2c");
                return -EINVAL;
        }
        if(pmic_addr < 0 || pmic_addr > 0x7f){
                CONFIG_ERR(pmic_i2c, "pmic_addr");
                return -EINVAL;
        }

        g_pmic_type = pmic_type;

        return 0;
}
int pmic_dcdc_set(int index, int on)
{
        struct regulator *dcdc = NULL;

        if(index < 0)
                return -EINVAL;

        if(pmic_is_tps65910()) {
                dcdc = regulator_get(NULL, tps65910_dcdc_info[index].name); 
        }
        if(pmic_is_act8931()) {
                dcdc = regulator_get(NULL, act8931_dcdc_info[index].name); 
        }
        if(IS_ERR_OR_NULL(dcdc))
                return -EINVAL;
        if(on)
                regulator_enable(dcdc);
        else
                regulator_disable(dcdc);
        regulator_put(dcdc);
        return 0;

}
EXPORT_SYMBOL(pmic_dcdc_set);
int pmic_ldo_set(int index, int on)
{
        struct regulator *ldo = NULL;

        if(index < 0)
                return -EINVAL;

        if(pmic_is_tps65910()) {
                ldo = regulator_get(NULL, tps65910_ldo_info[index].name); 
        }
        if(pmic_is_act8931()) {
                ldo = regulator_get(NULL, act8931_ldo_info[index].name); 
        }
        if(IS_ERR_OR_NULL(ldo))
                return -EINVAL;
        if(on)
                regulator_enable(ldo);
        else
                regulator_disable(ldo);
        regulator_put(ldo);
        return 0;

}
EXPORT_SYMBOL(pmic_ldo_set);
/* ion */
static uint ion_size = DEF_ION_SIZE; 
module_param(ion_size, uint, 0644);

static inline int check_ion_param(void)
{
        return 0;
}
/* codec */
static int spk_ctl = DEF_SPK_CTL;
module_param(spk_ctl, int, 0644);
static int hp_det = DEF_HP_DET;
module_param(hp_det, int, 0644);
static inline int check_codec_param(void)
{
        return 0;
}
/* sdmmc */
static int sd_det = -1;
module_param(sd_det, int, 0644);
static inline int check_sdmmc_param(void)
{
        return 0;
}
/* wifi */
static int wifi_rst = DEF_WIFI_RST;
module_param(wifi_rst, int, 0644);
static int wifi_pwr = DEF_WIFI_PWR;
module_param(wifi_pwr, int, 0644);
static uint wifi_type = DEF_WIFI_TYPE;
module_param(wifi_type, uint, 0644);
static int wifi_ldo = DEF_WIFI_LDO;
module_param(wifi_ldo, int, 0644);
static inline int check_wifi_param(void)
{
        if(wifi_type != WIFI_NONE){
                if(wifi_type <= WIFI_USB_NONE || wifi_type >= WIFI_USB_MAX || wifi_type <= WIFI_SDIO_NONE || wifi_type >= WIFI_SDIO_MAX){
                        CONFIG_ERR(wifi_type, "wifi_type");
                        return -EINVAL;
                }
        }

        return 0;
        
}
/* rtc */
static int rtc_i2c = DEF_RTC_I2C;
module_param(rtc_i2c, int, 0644);
static int rtc_addr = DEF_RTC_ADDR;
module_param(rtc_addr, int, 0644);
static int rtc_irq = DEF_RTC_IRQ;
module_param(rtc_irq, int, 0644);
static inline int check_rtc_param(void)
{
        if(rtc_i2c < 0 || rtc_i2c > 3){
                CONFIG_ERR(rtc_i2c, "rtc_i2c");
                return -EINVAL;
        }
        if(rtc_addr < 0 || rtc_addr > 0x7f){
                CONFIG_ERR(rtc_i2c, "rtc_addr");
                return -EINVAL;
        }
        return 0;
}

/* charge */
static int chg_adc = DEF_CHG_ADC;
module_param(chg_adc, int, 0644);

static int dc_det = DEF_DC_DET;
module_param(dc_det, int, 0644);
static int bat_low = DEF_BAT_LOW;
module_param(bat_low, int, 0644);
static int chg_ok = DEF_CHG_OK;
module_param(chg_ok, int, 0644);
static int chg_set = DEF_CHG_SET;
module_param(chg_set, int, 0644);
static int chg_sel = DEF_CHG_SEL;
module_param(chg_sel, int, 0644);
static inline int check_chg_param(void)
{
        return 0;
}

/* dvfs */
static struct dvfs_arm_table dvfs_cpu_logic_table[] = {
//#if defined(RK2926_TB_DEFAULT_CONFIG)
#if 1
        {.frequency = 216 * 1000, .cpu_volt = 1200 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 312 * 1000, .cpu_volt = 1200 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 408 * 1000, .cpu_volt = 1200 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 504 * 1000, .cpu_volt = 1200 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 600 * 1000, .cpu_volt = 1250 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 696 * 1000, .cpu_volt = 1350 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 816 * 1000, .cpu_volt = 1400 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 912 * 1000, .cpu_volt = 1450 * 1000, .logic_volt = 1200 * 1000},
        {.frequency = 1008 * 1000,.cpu_volt = 1500 * 1000, .logic_volt = 1200 * 1000},
#else
	{ .frequency = 216 * 1000, .cpu_volt = 850 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 312 * 1000, .cpu_volt = 900 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 408 * 1000, .cpu_volt = 950 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 504 * 1000, .cpu_volt = 1000 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 600 * 1000, .cpu_volt = 1100 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 696 * 1000, .cpu_volt = 1175 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 816 * 1000, .cpu_volt = 1250 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency = 912 * 1000, .cpu_volt = 1350 * 1000, .logic_volt = 1200 * 1000 },
	{ .frequency =1008 * 1000, .cpu_volt = 1450 * 1000, .logic_volt = 1200 * 1000 },
#endif
	{ .frequency = CPUFREQ_TABLE_END },
};
static unsigned int dvfs_cpu_logic[ARRAY_SIZE(dvfs_cpu_logic_table) * 3];
static unsigned int dvfs_cpu_logic_num;
module_param_array(dvfs_cpu_logic, uint, &dvfs_cpu_logic_num, 0400);

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{ .frequency = 266 * 1000, .index = 1050 * 1000 },
	{ .frequency = 400 * 1000, .index = 1275 * 1000 },
	{ .frequency = CPUFREQ_TABLE_END },
};
static unsigned int dvfs_gpu[ARRAY_SIZE(dvfs_gpu_table) * 2];
static unsigned int dvfs_gpu_num;
module_param_array(dvfs_gpu, uint, &dvfs_gpu_num, 0400);

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 300 * 1000,	.index = 1050 * 1000},
	{.frequency = 400 * 1000,	.index = 1125 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};
static unsigned int dvfs_ddr[ARRAY_SIZE(dvfs_ddr_table) * 2];
static unsigned int dvfs_ddr_num;
module_param_array(dvfs_ddr, uint, &dvfs_ddr_num, 0400);


/* global */
static int is_phonepad = DEF_IS_PHONEPAD;
module_param(is_phonepad, int, 0644);
static int pwr_on = DEF_PWR_ON;
module_param(pwr_on, int, 0644);

static inline int rk2928_power_on(void)
{
        return port_output_init(pwr_on, 1, "pwr_on");
}
static inline void rk2928_power_off(void)
{
        port_output_off(pwr_on);
        port_deinit(pwr_on);
}

