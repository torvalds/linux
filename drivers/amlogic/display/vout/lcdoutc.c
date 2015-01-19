/*
 * AMLOGIC lcd controller driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 * Modify:  Evoke Zhang <evoke.zhang@amlogic.com>
 * compatible dts
 *
 */
#include <linux/init.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/amlogic/logo/logo.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <mach/lcd_reg.h>
#include <mach/lcdoutc.h>
#include <linux/amlogic/vout/lcdoutc.h>
#include <linux/amlogic/vout/aml_lcd_common.h>
#include <linux/amlogic/vout/lcd_aml.h>
#include <mach/clock.h>
#include <asm/fiq.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/amlogic/aml_lcd_bl.h>
#include <linux/amlogic/vout/aml_lcd_extern.h>
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
#include <linux/amlogic/aml_pmu_common.h>
#endif

#define PANEL_NAME		"panel"

#ifdef LCD_DEBUG_INFO
unsigned int lcd_print_flag = 1;
#else
unsigned int lcd_print_flag = 0;
#endif
void lcd_print(const char *fmt, ...)
{
	va_list args;

	if (lcd_print_flag == 0)
		return;
	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

static const char* lcd_power_type_table[]={
	"cpu",
	"pmu",
	"signal",
	"init",
	"null",
};

static const char* lcd_power_pmu_gpio_table[]={
	"GPIO0",
	"GPIO1",
	"GPIO2",
	"GPIO3",
	"GPIO4",
	"null",
}; 

typedef struct {
	Lcd_Config_t *pConf;
	vinfo_t lcd_info;
} lcd_dev_t;

static lcd_dev_t *pDev = NULL;
static struct class *gamma_debug_class = NULL;
static Bool_t data_status = ON;
static int bl_status = ON;

static inline void lcd_mdelay(int n)
{
	mdelay(n);
}

#ifdef CONFIG_USE_OF
static void lcd_setup_gamma_table(Lcd_Config_t *pConf, unsigned int rgb_flag)
{
	int i;
	
	const unsigned short gamma_adjust[256] = {
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
		32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
		64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
		96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
		128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
		160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
		192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
		224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	};

	if (rgb_flag == 0) {	//r
		for (i=0; i<256; i++) {
			pConf->lcd_effect.GammaTableR[i] = gamma_adjust[i] << 2;
		}
	}
	else if (rgb_flag == 1) {	//g
		for (i=0; i<256; i++) {
			pConf->lcd_effect.GammaTableG[i] = gamma_adjust[i] << 2;
		}
	}
	else if (rgb_flag == 2) {	//b
		for (i=0; i<256; i++) {
			pConf->lcd_effect.GammaTableB[i] = gamma_adjust[i] << 2;
		}
	}
	else if (rgb_flag == 3) {	//rgb
		for (i=0; i<256; i++) {
			pConf->lcd_effect.GammaTableR[i] = gamma_adjust[i] << 2;
			pConf->lcd_effect.GammaTableG[i] = gamma_adjust[i] << 2;
			pConf->lcd_effect.GammaTableB[i] = gamma_adjust[i] << 2;
		}
	}
}

static void backlight_power_ctrl(Bool_t status)
{
	if( status == ON ){
		if ((data_status == OFF) || (bl_status == ON))
			return;
		bl_power_on(LCD_BL_FLAG);
	}
	else{
		if (bl_status == OFF)
			return;
		bl_power_off(LCD_BL_FLAG);
	}
	lcd_print("%s(%s): data_status=%s\n", __FUNCTION__, (status ? "ON" : "OFF"), (data_status ? "ON" : "OFF"));
	bl_status = status;
}

static int lcd_power_ctrl(Bool_t status)
{
	int i;
	int ret = 0;
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
	struct aml_pmu_driver *pmu_driver;
#endif
	struct aml_lcd_extern_driver_t *lcd_extern_driver;

	lcd_print("%s(): %s\n", __FUNCTION__, (status ? "ON" : "OFF"));
	if (status) {
		for (i=0; i<pDev->pConf->lcd_power_ctrl.power_on_step; i++) {
			lcd_print("%s %s step %d\n", __FUNCTION__, (status ? "ON" : "OFF"), i+1);
			switch (pDev->pConf->lcd_power_ctrl.power_on_config[i].type) {
				case LCD_POWER_TYPE_CPU:
					if (pDev->pConf->lcd_power_ctrl.power_on_config[i].value == LCD_POWER_GPIO_OUTPUT_LOW) {
						lcd_gpio_direction_output(pDev->pConf->lcd_power_ctrl.power_on_config[i].gpio, 0);
					}
					else if (pDev->pConf->lcd_power_ctrl.power_on_config[i].value == LCD_POWER_GPIO_OUTPUT_HIGH) {
						lcd_gpio_direction_output(pDev->pConf->lcd_power_ctrl.power_on_config[i].gpio, 1);
					}
					else if (pDev->pConf->lcd_power_ctrl.power_on_config[i].value == LCD_POWER_GPIO_INPUT) {
						lcd_gpio_direction_input(pDev->pConf->lcd_power_ctrl.power_on_config[i].gpio);
					}
					break;
				case LCD_POWER_TYPE_PMU:
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
					pmu_driver = aml_pmu_get_driver();
					if (pmu_driver == NULL) {
						printk("no pmu driver\n");
					}
					else if (pmu_driver->pmu_set_gpio) {
						if (pDev->pConf->lcd_power_ctrl.power_on_config[i].value == LCD_POWER_GPIO_OUTPUT_LOW) {
							pmu_driver->pmu_set_gpio(pDev->pConf->lcd_power_ctrl.power_on_config[i].gpio, 0);
						}
						else {
							pmu_driver->pmu_set_gpio(pDev->pConf->lcd_power_ctrl.power_on_config[i].gpio, 1);
						}
					}
#endif
					break;
				case LCD_POWER_TYPE_SIGNAL:
					if (pDev->pConf->lcd_power_ctrl.ports_ctrl == NULL)
						printk("no lcd_ports_ctrl\n");
					else
						pDev->pConf->lcd_power_ctrl.ports_ctrl(ON);
					break;
				case LCD_POWER_TYPE_INITIAL:
					lcd_extern_driver = aml_lcd_extern_get_driver();
					if (lcd_extern_driver == NULL) {
						printk("no lcd_extern driver\n");
					}
					else {
						if (lcd_extern_driver->power_on) {
							lcd_extern_driver->power_on();
							printk("%s power on init\n", lcd_extern_driver->name);
						}
					}
					break;
				default:
					printk("lcd power ctrl ON step %d is null.\n", i+1);
					break;
			}
			if (pDev->pConf->lcd_power_ctrl.power_on_config[i].delay > 0)
				lcd_mdelay(pDev->pConf->lcd_power_ctrl.power_on_config[i].delay);
		}
		if (pDev->pConf->lcd_power_ctrl.power_ctrl_video)
			ret = pDev->pConf->lcd_power_ctrl.power_ctrl_video(ON);
		data_status = status;
	}
	else {
		data_status = status;
		lcd_mdelay(30);
		if (pDev->pConf->lcd_power_ctrl.power_ctrl_video)
			ret = pDev->pConf->lcd_power_ctrl.power_ctrl_video(OFF);
		for (i=0; i<pDev->pConf->lcd_power_ctrl.power_off_step; i++) {
			lcd_print("%s %s step %d\n", __FUNCTION__, (status ? "ON" : "OFF"), i+1);
			switch (pDev->pConf->lcd_power_ctrl.power_off_config[i].type) {
				case LCD_POWER_TYPE_CPU:
					if (pDev->pConf->lcd_power_ctrl.power_off_config[i].value == LCD_POWER_GPIO_OUTPUT_LOW) {
						lcd_gpio_direction_output(pDev->pConf->lcd_power_ctrl.power_off_config[i].gpio, 0);
					}
					else if (pDev->pConf->lcd_power_ctrl.power_off_config[i].value == LCD_POWER_GPIO_OUTPUT_HIGH) {
						lcd_gpio_direction_output(pDev->pConf->lcd_power_ctrl.power_off_config[i].gpio, 1);
					}
					else if (pDev->pConf->lcd_power_ctrl.power_off_config[i].value == LCD_POWER_GPIO_INPUT) {
						lcd_gpio_direction_input(pDev->pConf->lcd_power_ctrl.power_off_config[i].gpio);
					}
					break;
				case LCD_POWER_TYPE_PMU:
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
					pmu_driver = aml_pmu_get_driver();
					if (pmu_driver == NULL) {
						printk("no pmu driver\n");
					}
					else if (pmu_driver->pmu_set_gpio) {
						if (pDev->pConf->lcd_power_ctrl.power_off_config[i].value == LCD_POWER_GPIO_OUTPUT_LOW) {
							pmu_driver->pmu_set_gpio(pDev->pConf->lcd_power_ctrl.power_off_config[i].gpio, 0);
						}
						else {
							pmu_driver->pmu_set_gpio(pDev->pConf->lcd_power_ctrl.power_off_config[i].gpio, 1);
						}
					}
#endif
					break;
				case LCD_POWER_TYPE_SIGNAL:
					if (pDev->pConf->lcd_power_ctrl.ports_ctrl == NULL)
						printk("no lcd_ports_ctrl\n");
					else
						pDev->pConf->lcd_power_ctrl.ports_ctrl(OFF);
					break;
				case LCD_POWER_TYPE_INITIAL:
					lcd_extern_driver = aml_lcd_extern_get_driver();
					if (lcd_extern_driver == NULL) {
						printk("no lcd_extern driver\n");
					}
					else {
						if (lcd_extern_driver->power_off) {
							lcd_extern_driver->power_off();
							printk("%s power off init\n", lcd_extern_driver->name);
						}
					}
					break;
				default:
					printk("lcd power ctrl OFF step %d is null.\n", i+1);
					break;
			}
			if (pDev->pConf->lcd_power_ctrl.power_off_config[i].delay > 0)
				lcd_mdelay(pDev->pConf->lcd_power_ctrl.power_off_config[i].delay);
		}
	}

	printk("%s(): %s finished.\n", __FUNCTION__, (status ? "ON" : "OFF"));
	return ret;
}
#endif

void _enable_backlight(void)
{
	backlight_power_ctrl(ON);
}
void _disable_backlight(void)
{
	backlight_power_ctrl(OFF);
}

static void _lcd_module_enable(void)
{
    pDev->pConf->lcd_misc_ctrl.module_enable();
}

static void _lcd_module_disable(void)
{
    pDev->pConf->lcd_misc_ctrl.module_disable();
}

static const vinfo_t *lcd_get_current_info(void)
{
    if (pDev == NULL) {
        printk("[error] no lcd device exist!\n");
        return NULL;
    }
    else 
        return &pDev->lcd_info;
}

DEFINE_MUTEX(lcd_vout_mutex);
static int lcd_set_current_vmode(vmode_t mode)
{
    mutex_lock(&lcd_vout_mutex);
    if (VMODE_LCD != (mode & VMODE_MODE_BIT_MASK)) {
        mutex_unlock(&lcd_vout_mutex);
        return -EINVAL;
    }

    pDev->pConf->lcd_misc_ctrl.vpp_sel = 0;
    WRITE_LCD_REG(VPP_POSTBLEND_H_SIZE, pDev->lcd_info.width);

    if( !(mode&VMODE_LOGO_BIT_MASK) ){
        _disable_backlight();
        _lcd_module_enable();
        _enable_backlight();
    }
    if (VMODE_INIT_NULL == pDev->lcd_info.mode)
        pDev->lcd_info.mode = VMODE_LCD;
    
    mutex_unlock(&lcd_vout_mutex);
    return 0;
}

#ifdef CONFIG_AM_TV_OUTPUT2
static int lcd_set_current_vmode2(vmode_t mode)
{
    mutex_lock(&lcd_vout_mutex);
    if (mode != VMODE_LCD) {
        mutex_unlock(&lcd_vout_mutex);
        return -EINVAL;
    }
    _disable_backlight();
    pDev->pConf->lcd_misc_ctrl.vpp_sel = 1;

    WRITE_LCD_REG(VPP2_POSTBLEND_H_SIZE, pDev->lcd_info.width);

    _lcd_module_enable();
    if (VMODE_INIT_NULL == pDev->lcd_info.mode)
        pDev->lcd_info.mode = VMODE_LCD;
    _enable_backlight();
    mutex_unlock(&lcd_vout_mutex);
    return 0;
}
#endif

static vmode_t lcd_validate_vmode(char *mode)
{
    if ((strncmp(mode, PANEL_NAME, strlen(PANEL_NAME))) == 0)
        return VMODE_LCD;
    
    return VMODE_MAX;
}
static int lcd_vmode_is_supported(vmode_t mode)
{
    mode&=VMODE_MODE_BIT_MASK;
    if(mode == VMODE_LCD )
    return true;
    return false;
}

static int lcd_vout_disable(vmode_t cur_vmod)
{
    mutex_lock(&lcd_vout_mutex);
    _disable_backlight();
    _lcd_module_disable();
    mutex_unlock(&lcd_vout_mutex);
    return 0;
}

#ifdef  CONFIG_PM
static int lcd_suspend(void)
{
    mutex_lock(&lcd_vout_mutex);
    BUG_ON(pDev==NULL);
    printk("lcd_suspend\n");
    _disable_backlight();
    _lcd_module_disable();
    mutex_unlock(&lcd_vout_mutex);
    return 0;
}
static int lcd_resume(void)
{
    mutex_lock(&lcd_vout_mutex);
    printk("lcd_resume\n");
    _lcd_module_enable();
    _enable_backlight();
    mutex_unlock(&lcd_vout_mutex);
    return 0;
}
#endif
static vout_server_t lcd_vout_server={
    .name = "lcd_vout_server",
    .op = {
        .get_vinfo = lcd_get_current_info,
        .set_vmode = lcd_set_current_vmode,
        .validate_vmode = lcd_validate_vmode,
        .vmode_is_supported=lcd_vmode_is_supported,
        .disable=lcd_vout_disable,
#ifdef  CONFIG_PM
        .vout_suspend=lcd_suspend,
        .vout_resume=lcd_resume,
#endif
    },
};

#ifdef CONFIG_AM_TV_OUTPUT2
static vout_server_t lcd_vout2_server={
    .name = "lcd_vout2_server",
    .op = {
        .get_vinfo = lcd_get_current_info,
        .set_vmode = lcd_set_current_vmode2,
        .validate_vmode = lcd_validate_vmode,
        .vmode_is_supported=lcd_vmode_is_supported,
        .disable=lcd_vout_disable,
#ifdef  CONFIG_PM  
        .vout_suspend=lcd_suspend,
        .vout_resume=lcd_resume,
#endif
    },
};
#endif

static void _init_vout(void)
{
    pDev->lcd_info.name = PANEL_NAME;
    pDev->lcd_info.mode = VMODE_LCD;
    pDev->lcd_info.width = pDev->pConf->lcd_basic.h_active;
    pDev->lcd_info.height = pDev->pConf->lcd_basic.v_active;
    pDev->lcd_info.field_height = pDev->pConf->lcd_basic.v_active;
    pDev->lcd_info.aspect_ratio_num = pDev->pConf->lcd_basic.screen_ratio_width;
    pDev->lcd_info.aspect_ratio_den = pDev->pConf->lcd_basic.screen_ratio_height;
    pDev->lcd_info.screen_real_width= pDev->pConf->lcd_basic.h_active_area;
    pDev->lcd_info.screen_real_height= pDev->pConf->lcd_basic.v_active_area;
    pDev->lcd_info.sync_duration_num = pDev->pConf->lcd_timing.sync_duration_num;
    pDev->lcd_info.sync_duration_den = pDev->pConf->lcd_timing.sync_duration_den;
    pDev->lcd_info.video_clk = pDev->pConf->lcd_timing.lcd_clk;
       
    //add lcd actual active area size
    printk("lcd actual active area size: %d %d (mm).\n", pDev->pConf->lcd_basic.h_active_area, pDev->pConf->lcd_basic.v_active_area);
    vout_register_server(&lcd_vout_server);
#ifdef CONFIG_AM_TV_OUTPUT2
    vout2_register_server(&lcd_vout2_server);
#endif
}

//*********************************************************
//gamma debug
//*********************************************************
#ifdef CONFIG_AML_GAMMA_DEBUG
static unsigned short gamma_adjust_r[256];
static unsigned short gamma_adjust_g[256];
static unsigned short gamma_adjust_b[256];
static unsigned short gamma_r_coeff, gamma_g_coeff, gamma_b_coeff;
static unsigned gamma_ctrl;

static void save_original_gamma(Lcd_Config_t *pConf)
{
    int i;

    for (i=0; i<256; i++) {
        gamma_adjust_r[i] = pConf->lcd_effect.GammaTableR[i];
        gamma_adjust_g[i] = pConf->lcd_effect.GammaTableG[i];
        gamma_adjust_b[i] = pConf->lcd_effect.GammaTableB[i];
    }
    gamma_ctrl = pConf->lcd_effect.gamma_ctrl;
    gamma_r_coeff = pConf->lcd_effect.gamma_r_coeff;
    gamma_g_coeff = pConf->lcd_effect.gamma_g_coeff;
    gamma_b_coeff = pConf->lcd_effect.gamma_b_coeff;
}

static void read_original_gamma_table(void)
{
    unsigned i;

    printk("original gamma: enable=%d, reverse=%d, r_coeff=%u%%, g_coeff=%u%%, b_coeff=%u%%\n", 
          ((gamma_ctrl >> GAMMA_CTRL_EN) & 1), ((gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1), gamma_r_coeff, gamma_g_coeff, gamma_b_coeff);
    printk("read original gamma table R:\n");
    for (i=0; i<256; i++) {
        printk("%u,", gamma_adjust_r[i]);
    }
    printk("\n\nread original gamma table G:\n");
    for (i=0; i<256; i++) {
        printk("%u,", gamma_adjust_g[i]);
    }
    printk("\n\nread original gamma table B:\n");
    for (i=0; i<256; i++) {
        printk("%u,", gamma_adjust_b[i]);
    }
    printk("\n");
}

static void read_current_gamma_table(Lcd_Config_t *pConf)
{
    unsigned i;

    printk("current gamma: enable=%d, reverse=%d, r_coeff=%u%%, g_coeff=%u%%, b_coeff=%u%%\n", 
          ((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_EN) & 1), ((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1), 
          pConf->lcd_effect.gamma_r_coeff, pConf->lcd_effect.gamma_g_coeff, pConf->lcd_effect.gamma_b_coeff);
    printk("read current gamma table R:\n");
    for (i=0; i<256; i++) {
        printk("%u ", pConf->lcd_effect.GammaTableR[i]);
    }
    printk("\n\nread current gamma table G:\n");
    for (i=0; i<256; i++) {
        printk("%u ", pConf->lcd_effect.GammaTableG[i]);
    }
    printk("\n\nread current gamma table B:\n");
    for (i=0; i<256; i++) {
        printk("%u ", pConf->lcd_effect.GammaTableB[i]);
    }
    printk("\n");
}

static int write_gamma_table(Lcd_Config_t *pConf)
{
    int ret = 0;

    if (pConf->lcd_effect.set_gamma_table == NULL) {
        printk("set gamma table function is null\n");
        ret = -1;
    }
    else {
        pConf->lcd_effect.set_gamma_table(1); //force enable gamma table
        printk("write gamma table ");
    }
    return ret;
}

static void set_gamma_coeff(Lcd_Config_t *pConf, unsigned r_coeff, unsigned g_coeff, unsigned b_coeff)
{
    pConf->lcd_effect.gamma_r_coeff = (unsigned short)(r_coeff);
    pConf->lcd_effect.gamma_g_coeff = (unsigned short)(g_coeff);
    pConf->lcd_effect.gamma_b_coeff = (unsigned short)(b_coeff);
    if (write_gamma_table(pConf) == 0)
        printk("with scale factor R:%u%%, G:%u%%, B:%u%%.\n", r_coeff, g_coeff, b_coeff);
}

static const char * usage_str =
{"Usage:\n"
"    echo coeff <R_coeff> <G_coeff> <B_coeff> > write ; set R,G,B gamma scale factor\n"
"    echo ctrl <enable> <reverse> > write; control gamma table enable and reverse\n"
"data format:\n"
"    <R/G/B_coeff>  : a number in Dec(0~100), means a percent value\n"
"    <enable>       : 0=disable, 1=enable\n"
"    <reverse>      : 0=normal, 1=reverse\n"
"\n"
"    echo [r|g|b] <step> <value> <value> <value> <value> <value> <value> <value> <value> > write ; input R/G/B gamma table\n"
"    echo w [0 | 8 | 10] > write ; apply the original/8bit/10bit gamma table\n"
"data format:\n"
"    <step>  : 0xX, 4bit in Hex, there are 8 steps(0~7, 8bit gamma) or 16 steps(0~f, 10bit gamma) for a single cycle\n"
"    <value> : 0xXXXXXXXX, 32bit in Hex, 2 or 4 gamma table values (8 or 10bit gamma) combia in one <value>\n"
"\n"
"    echo f[r | g | b | w] <level_value> > write ; write R/G/B/white gamma level with fixed level_value\n"
"data format:\n"
"    <level_value>  : a number in Dec(0~255)\n"
"\n"
"    echo [0 | 1] > read ; readback original/current gamma table\n"
};

static ssize_t gamma_help(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n",usage_str);
}

static ssize_t aml_lcd_gamma_read(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    if (buf[0] == '0')
        read_original_gamma_table();
    else
        read_current_gamma_table(pDev->pConf);

    return count;
}

static unsigned gamma_adjust_r_temp[128];
static unsigned gamma_adjust_g_temp[128];
static unsigned gamma_adjust_b_temp[128];
static ssize_t aml_lcd_gamma_debug(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned int ret;
    unsigned int i, j;
    unsigned t[8];

    switch (buf[0]) {
    case 'c':
        if (buf[1] == 'o') {
            t[0] = 100;
            t[1] = 100;
            t[2] = 100;
            ret = sscanf(buf, "coeff %u %u %u", &t[0], &t[1], &t[2]);
            set_gamma_coeff(pDev->pConf, t[0], t[1], t[2]);
        }
        else if (buf[1] == 't') {
            t[0] = 1;
            t[1] = 0;
            ret = sscanf(buf, "ctrl %u %u", &t[0], &t[1]);
            pDev->pConf->lcd_effect.gamma_ctrl = ((t[0] << GAMMA_CTRL_EN) | (t[1] << GAMMA_CTRL_REVERSE));
            if (write_gamma_table(pDev->pConf) == 0)
                printk(" finished.\n");
        }
        break;
    case 'r':
        ret = sscanf(buf, "r %x %x %x %x %x %x %x %x %x", &i, &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7]);
        if (i<16) {
            i =  i * 8;
            for (j=0; j<8; j++) {
                gamma_adjust_r_temp[i+j] = t[j];
            }
            printk("write R table: step %u.\n", i/8);
        }
        break;
    case 'g':
        ret = sscanf(buf, "g %x %x %x %x %x %x %x %x %x", &i, &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7]);
        if (i<16) {
            i =  i * 8;
            for (j=0; j<8; j++) {
                gamma_adjust_g_temp[i+j] = t[j];
            }
            printk("write G table: step %u.\n", i/8);
        }
        break;
    case 'b':
        ret = sscanf(buf, "b %x %x %x %x %x %x %x %x %x", &i, &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7]);
        if (i<16) {
            i =  i * 8;
            for (j=0; j<8; j++) {
                gamma_adjust_b_temp[i+j] = t[j];
            }
            printk("write B table: step %u.\n", i/8);
        }
        break;
    case 'w':
        i = 0;
        ret = sscanf(buf, "w %u", &i);
        if (i == 8) {
            for (i=0; i<64; i++) {
                for (j=0; j<4; j++){
                    pDev->pConf->lcd_effect.GammaTableR[i*4+j] = (unsigned short)(((gamma_adjust_r_temp[i] >> (24-j*8)) & 0xff) << 2);
                    pDev->pConf->lcd_effect.GammaTableG[i*4+j] = (unsigned short)(((gamma_adjust_g_temp[i] >> (24-j*8)) & 0xff) << 2);
                    pDev->pConf->lcd_effect.GammaTableB[i*4+j] = (unsigned short)(((gamma_adjust_b_temp[i] >> (24-j*8)) & 0xff) << 2);
                }
            }
            if (write_gamma_table(pDev->pConf) == 0)
                printk("8bit finished.\n");
        }
        else if (i == 10) {
            for (i=0; i<128; i++) {
                for (j=0; j<2; j++){
                    pDev->pConf->lcd_effect.GammaTableR[i*2+j] = (unsigned short)((gamma_adjust_r_temp[i] >> (16-j*16)) & 0xffff);
                    pDev->pConf->lcd_effect.GammaTableG[i*2+j] = (unsigned short)((gamma_adjust_g_temp[i] >> (16-j*16)) & 0xffff);
                    pDev->pConf->lcd_effect.GammaTableB[i*2+j] = (unsigned short)((gamma_adjust_b_temp[i] >> (16-j*16)) & 0xffff);
                }
            }
            if (write_gamma_table(pDev->pConf) == 0)
                printk("10bit finished.\n");
        }
        else {
            for (i=0; i<256; i++) {
                pDev->pConf->lcd_effect.GammaTableR[i] = gamma_adjust_r[i];
                pDev->pConf->lcd_effect.GammaTableG[i] = gamma_adjust_g[i];
                pDev->pConf->lcd_effect.GammaTableB[i] = gamma_adjust_b[i];
            }
            if (write_gamma_table(pDev->pConf) == 0)
                printk("to original.\n");
        }
        break;
    case 'f':
        i=255;
        if (buf[1] == 'r') {
            ret = sscanf(buf, "fr %u", &i);
            i &= 0xff;
            for (j=0; j<256; j++) {
                pDev->pConf->lcd_effect.GammaTableR[j] = i<<2;
            }
            set_gamma_coeff(pDev->pConf, 100, 0, 0);
            printk("with R fixed value %u finished.\n", i);
        }
        else if (buf[1] == 'g') {
            ret = sscanf(buf, "fg %u", &i);
            i &= 0xff; 
            for (j=0; j<256; j++) {
                pDev->pConf->lcd_effect.GammaTableG[j] = i<<2;
            }
            set_gamma_coeff(pDev->pConf, 0, 100, 0);
            printk("with G fixed value %u finished.\n", i);
        }
        else if (buf[1] == 'b') {
            ret = sscanf(buf, "fb %u", &i);
            i &= 0xff;
            for (j=0; j<256; j++) {
                pDev->pConf->lcd_effect.GammaTableB[j] = i<<2;
            }
            set_gamma_coeff(pDev->pConf, 0, 0, 100);
            printk("with B fixed value %u finished.\n", i);
        }
        else {
            ret = sscanf(buf, "fw %u", &i);
            i &= 0xff;
            for (j=0; j<256; j++) {
                pDev->pConf->lcd_effect.GammaTableR[j] = i<<2;
                pDev->pConf->lcd_effect.GammaTableG[j] = i<<2;
                pDev->pConf->lcd_effect.GammaTableB[j] = i<<2;
            }
            set_gamma_coeff(pDev->pConf, 100, 100, 100);
            printk("with fixed value %u finished.\n", i);
        }
        break;
    default:
            printk("wrong format of gamma table writing.\n");
    }

    if (ret != 1 || ret !=2)
        return -EINVAL;

    return count;
    //return 0;
}

static struct class_attribute aml_lcd_gamma_class_attrs[] = {
    __ATTR(write,  S_IRUGO | S_IWUSR, gamma_help, aml_lcd_gamma_debug),
    __ATTR(read,  S_IRUGO | S_IWUSR, gamma_help, aml_lcd_gamma_read),
    __ATTR(help,  S_IRUGO | S_IWUSR, gamma_help, NULL),
};

static int creat_lcd_gamma_attr(void)
{
    int i;

    gamma_debug_class = class_create(THIS_MODULE, "gamma");
    if(IS_ERR(gamma_debug_class)) {
        printk("create gamma debug class fail\n");
        return -1;
    }

    for(i=0;i<ARRAY_SIZE(aml_lcd_gamma_class_attrs);i++) {
        if (class_create_file(gamma_debug_class, &aml_lcd_gamma_class_attrs[i])) {
            printk("create gamma debug attribute %s fail\n", aml_lcd_gamma_class_attrs[i].attr.name);
        }
    }

    return 0;
}

static int remove_lcd_gamma_attr(void)
{
    int i;

    if (gamma_debug_class == NULL)
        return -1;

    for(i=0;i<ARRAY_SIZE(aml_lcd_gamma_class_attrs);i++) {
        class_remove_file(gamma_debug_class, &aml_lcd_gamma_class_attrs[i]);
    }
    class_destroy(gamma_debug_class);
    gamma_debug_class = NULL;

    return 0;
}
#endif
//*********************************************************

//*********************************************************
//LCD debug
//*********************************************************
static Lcd_Basic_t temp_lcd_basic;
static Lcd_Timing_t temp_lcd_timing;
static unsigned short temp_dith_user, temp_dith_ctrl;
static unsigned int temp_vadj_brightness, temp_vadj_contrast, temp_vadj_saturation;
static int temp_ttl_rb_swap, temp_ttl_bit_swap;
static int temp_lvds_repack, temp_pn_swap, temp_lvds_vswing;
static unsigned char temp_dsi_lane_num;
static unsigned temp_dsi_bit_rate_min, temp_dsi_bit_rate_max, temp_factor_denominator, temp_factor_numerator;
static unsigned char temp_edp_link_rate, temp_edp_lane_count, temp_edp_vswing, temp_edp_preemphasis;

static const char * lcd_common_usage_str =
{"Usage:\n"
"    echo 0/1 > status ; 0=disable lcd; 1=enable lcd\n"
"    cat status ; read current lcd status\n"
"\n"
"    echo 0/1 > print ; 0=disable debug print; 1=enable debug print\n"
"    cat print ; read current debug print flag\n"
"\n"
"    echo <cmd> ... > debug ; lcd common debug, use 'cat debug' for help\n"
"    cat debug ; print help information for debug command\n"
#ifdef CONFIG_LCD_IF_MIPI_VALID
"\n"
"    echo <cmd> ... > dsi ; mipi-dsi debug, use 'cat dsi' for help\n"
"    cat dsi ; print help information for dsi command\n"
#endif
#ifdef CONFIG_LCD_IF_EDP_VALID
"\n"
"    echo <cmd> ... > print ; edp debug, use 'cat edp' for help\n"
"    cat print ; print help information for edp command\n"
#endif
};

static const char * lcd_usage_str =
{"Usage:\n"
"    echo basic <h_active> <v_active> <h_period> <v_period> > debug ; write lcd basic config\n"
"    echo type <lcd_type> <lcd_bits> > debug ; write lcd type & bits\n"
"    echo clock <lcd_clk> <ss_level> <clk_pol> > debug ; write lcd clk (Hz)\n"
"    echo sync <hs_width> <hs_backporch> <hs_pol> <vs_width> <vs_backporch> <vs_pol> > debug ; write lcd sync timing\n"
"    echo valid <hvsync_valid> <de_valid> > debug ; enable lcd sync signals\n"
"data format:\n"
"    <lcd_type> : "
#ifdef CONFIG_LCD_IF_MIPI_VALID
"0=mipi, "
#endif
"1=lvds, "
#ifdef CONFIG_LCD_IF_EDP_VALID
"2=edp, "
#endif
"3=ttl\n"
"    <lcd_bits> : 6=6bit(RGB18bit), 8=8bit(RGB24bit)\n"
"    <ss_level> : lcd clock spread spectrum level, 0 for disable\n"
"    <xx_pol>   : 0=negative, 1=positive\n"
"    <xx_valid> : 0=disable, 1=enable\n"
"\n"
"    echo ttl <rb_swap> <bit_swap> > debug ; write ttl RGB swap config\n"
"    echo lvds <vswing_level> <lvds_repack> <pn_swap> > debug ; write lvds config\n"
#ifdef CONFIG_LCD_IF_MIPI_VALID
"    echo mdsi <lane_num> <bit_rate_max> <factor> > debug ; write mipi-dsi clock config\n"
"    echo mctl <init_mode> <disp_mode> <lp_clk_auto_stop> <transfer_switch> > debug ; write mipi-dsi control config\n"
#endif
#ifdef CONFIG_LCD_IF_EDP_VALID
"    echo edp <link_rate> <lane_count> <vswing_level> > debug ; write edp config\n"
#endif
"data format:\n"
"    <xx_swap>      : 0=normal, 1=swap\n"
"    <vswing_level> : lvds support level 0~4 (Default=1);"
#ifdef CONFIG_LCD_IF_EDP_VALID
" edp support level 0~3 (default=0)"
#endif
"\n"
"    <lvds_repack>  : 0=JEIDA mode, 1=VESA mode\n"
"    <pn_swap>      : 0=normal, 1=swap lvds p/n channels\n"
#ifdef CONFIG_LCD_IF_MIPI_VALID
"    <bit_rate_max> : unit in MHz\n"
"    <factor>:      : special adjust, 0 for default\n"
"    <xxxx_mode>    : 0=video mode, 1=command mode\n"
"    <lp_clk_auto_stop> : 0=disable, 1=enable\n"
"    <transfer_switch>  : 0=auto, 1=standard, 2=slow\n"
#endif
#ifdef CONFIG_LCD_IF_EDP_VALID
"    <link_rate>    : 0=1.62G, 1=2.7G\n"
#endif
"\n"
"    echo offset <h_sign> <h_offset> <v_sign> <v_offset> > debug ; write ttl display offset\n"
"    echo dither <dither_user> <dither_ctrl> > debug ; write user dither ctrl config\n"
"    echo vadj <brightness> <contrast> <saturation> > debug ; write video adjust config\n"
"data format:\n"
"    <xx_sign>     : 0=positive, 1=negative\n"
"    <dither_user> : 0=disable user control, 1=enable user control\n"
"    <dither_ctrl> : dither ctrl in Hex, such as 0x400 or 0x600\n"
"    <brightness>  : negative 0x1ff~0x101, positive 0x0~0xff, signed value in Hex, default is 0x0\n"
"    <contrast>    : 0x0~0xff, unsigned value in Hex, default is 0x80\n"
"    <saturation>  : 0x0~0x1ff, unsigned value in Hex, default is 0x100\n"
"\n"
"    echo write > debug ; update lcd driver\n"
"    echo reset > debug ; reset lcd config & driver\n"
"    echo read > debug ; read current lcd config\n"
"    echo test <num> > debug ; bist pattern test, 0=pattern off, 1~7=different pattern\n"
};

static ssize_t lcd_debug_common_help(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n",lcd_common_usage_str);
}

static ssize_t lcd_debug_help(struct class *class, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%s\n",lcd_usage_str);
}

static void read_current_lcd_config(Lcd_Config_t *pConf)
{
    unsigned lcd_clk;
    int h_adj, v_adj;

    lcd_clk = (pConf->lcd_timing.lcd_clk / 1000);
    h_adj = ((pConf->lcd_timing.h_offset >> 31) & 1);
    v_adj = ((pConf->lcd_timing.v_offset >> 31) & 1);

    pConf->lcd_misc_ctrl.print_version();
    printk("LCD mode: %s, %s %ubit, %ux%u@%u.%uHz\n"
           "lcd_clk           %u.%03uMHz\n"
           "ss_level          %d\n"
           "clk_pol           %d\n\n",
           pConf->lcd_basic.model_name, lcd_type_table[pConf->lcd_basic.lcd_type], pConf->lcd_basic.lcd_bits, pConf->lcd_basic.h_active, pConf->lcd_basic.v_active,
           (pConf->lcd_timing.sync_duration_num / 10), (pConf->lcd_timing.sync_duration_num % 10),
           (lcd_clk / 1000), (lcd_clk % 1000), ((pConf->lcd_timing.clk_ctrl >> CLK_CTRL_SS) & 0xf), ((pConf->lcd_timing.pol_ctrl >> POL_CTRL_CLK) & 1));

    printk("h_period          %d\n"
           "v_period          %d\n"
           "hs_width          %d\n"
           "hs_backporch      %d\n"
           "hs_pol            %d\n"
           "vs_width          %d\n"
           "vs_backporch      %d\n"
           "vs_pol            %d\n"
           "vs_h_phase        %s%d\n"
           "hvsync_valid      %d\n"
           "de_valid          %d\n"
           "h_offset          %s%d\n"
           "v_offset          %s%d\n\n",
           pConf->lcd_basic.h_period, pConf->lcd_basic.v_period,
           pConf->lcd_timing.hsync_width, pConf->lcd_timing.hsync_bp, ((pConf->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1),
           pConf->lcd_timing.vsync_width, pConf->lcd_timing.vsync_bp, ((pConf->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1),
           (((pConf->lcd_timing.vsync_h_phase >> 31) & 1) ? "-":""), (pConf->lcd_timing.vsync_h_phase & 0xffff), pConf->lcd_timing.hvsync_valid, pConf->lcd_timing.de_valid,
           (h_adj ? "-" : ""), (pConf->lcd_timing.h_offset & 0xffff), (v_adj ? "-" : ""), (pConf->lcd_timing.v_offset & 0xffff));

    switch (pConf->lcd_basic.lcd_type) {
        case LCD_DIGITAL_TTL:
            printk("rb_swap           %u\n"
                   "bit_swap          %u\n\n",
                   pConf->lcd_control.ttl_config->rb_swap, pConf->lcd_control.ttl_config->bit_swap);
            break;
        case LCD_DIGITAL_LVDS:
            printk("vswing_level      %u\n"
                   "lvds_repack       %u\n"
                   "pn_swap           %u\n\n",
                   pConf->lcd_control.lvds_config->lvds_vswing, pConf->lcd_control.lvds_config->lvds_repack, pConf->lcd_control.lvds_config->pn_swap);
            break;
        case LCD_DIGITAL_MIPI:
            printk("dsi_lane_num      %u\n"
                   "dsi_bit_rate      %u.%03uMHz\n"
                   "operation_mode    %u(%s), %u(%s)\n"
                   "transfer_ctrl     %u, %u\n\n",
                   pConf->lcd_control.mipi_config->lane_num, (pConf->lcd_control.mipi_config->bit_rate / 1000000), ((pConf->lcd_control.mipi_config->bit_rate % 1000000) / 1000),
                   ((pConf->lcd_control.mipi_config->operation_mode>>BIT_OPERATION_MODE_INIT) &1), (((pConf->lcd_control.mipi_config->operation_mode>>BIT_OPERATION_MODE_INIT) & 1) ? "COMMAND" : "VIDEO"),
                   ((pConf->lcd_control.mipi_config->operation_mode>>BIT_OPERATION_MODE_DISP) & 1), (((pConf->lcd_control.mipi_config->operation_mode>>BIT_OPERATION_MODE_DISP) & 1) ? "COMMAND" : "VIDEO"),
                   ((pConf->lcd_control.mipi_config->transfer_ctrl>>BIT_TRANS_CTRL_CLK) & 1), ((pConf->lcd_control.mipi_config->transfer_ctrl>>BIT_TRANS_CTRL_SWITCH) & 3));
            break;
        case LCD_DIGITAL_EDP:
            printk("link_rate         %s\n"
                   "lane_count        %u\n"
                   "link_adaptive     %u\n"
                   "vswing            %u\n"
                   "max_lane_count    %u\n"
                   "sync_clock_mode   %u\n\n",
                   ((pConf->lcd_control.edp_config->link_rate == 0) ? "1.62G":"2.7G"), pConf->lcd_control.edp_config->lane_count,
                   pConf->lcd_control.edp_config->link_adaptive, pConf->lcd_control.edp_config->vswing,
                   pConf->lcd_control.edp_config->max_lane_count, pConf->lcd_control.edp_config->sync_clock_mode);
            break;
        default:
            break;
    }

    if (pConf->lcd_effect.dith_user)
        printk("dither_ctrl       0x%x\n", pConf->lcd_effect.dith_cntl_addr);

    printk("pll_ctrl          0x%08x\n"
           "div_ctrl          0x%08x\n"
           "clk_ctrl          0x%08x\n"
           "video_on_pixel    %d\n"
           "video_on_line     %d\n\n", 
           pConf->lcd_timing.pll_ctrl, pConf->lcd_timing.div_ctrl, pConf->lcd_timing.clk_ctrl,
           pConf->lcd_timing.video_on_pixel, pConf->lcd_timing.video_on_line);
}

static void save_lcd_config(Lcd_Config_t *pConf)
{
	temp_lcd_basic.h_active = pConf->lcd_basic.h_active;
	temp_lcd_basic.v_active = pConf->lcd_basic.v_active;
	temp_lcd_basic.h_period = pConf->lcd_basic.h_period;
	temp_lcd_basic.v_period = pConf->lcd_basic.v_period;
	temp_lcd_basic.lcd_type = pConf->lcd_basic.lcd_type;
	temp_lcd_basic.lcd_bits = pConf->lcd_basic.lcd_bits;

	temp_lcd_timing.pll_ctrl = pConf->lcd_timing.pll_ctrl;
	temp_lcd_timing.div_ctrl = pConf->lcd_timing.div_ctrl;
	temp_lcd_timing.clk_ctrl = pConf->lcd_timing.clk_ctrl;
	temp_lcd_timing.lcd_clk = pConf->lcd_timing.lcd_clk;
	temp_lcd_timing.hsync_width = pConf->lcd_timing.hsync_width;
	temp_lcd_timing.hsync_bp = pConf->lcd_timing.hsync_bp;
	temp_lcd_timing.vsync_width = pConf->lcd_timing.vsync_width;
	temp_lcd_timing.vsync_bp = pConf->lcd_timing.vsync_bp;
	temp_lcd_timing.hvsync_valid = pConf->lcd_timing.hvsync_valid;
	//temp_lcd_timing.de_hstart = pConf->lcd_timing.de_hstart;
	//temp_lcd_timing.de_vstart = pConf->lcd_timing.de_vstart;
	temp_lcd_timing.de_valid = pConf->lcd_timing.de_valid;
	temp_lcd_timing.h_offset = pConf->lcd_timing.h_offset;
	temp_lcd_timing.v_offset = pConf->lcd_timing.v_offset;
	temp_lcd_timing.pol_ctrl = pConf->lcd_timing.pol_ctrl;
	
	switch (pConf->lcd_basic.lcd_type) {
		case LCD_DIGITAL_MIPI:
			temp_dsi_lane_num = pConf->lcd_control.mipi_config->lane_num;
			temp_dsi_bit_rate_min = pConf->lcd_control.mipi_config->bit_rate_min;
			temp_dsi_bit_rate_max = pConf->lcd_control.mipi_config->bit_rate_max;
			temp_factor_denominator = pConf->lcd_control.mipi_config->factor_denominator;
			temp_factor_numerator = pConf->lcd_control.mipi_config->factor_numerator;
			break;
		case LCD_DIGITAL_EDP:
			temp_edp_link_rate = pConf->lcd_control.edp_config->link_rate;
			temp_edp_lane_count = pConf->lcd_control.edp_config->lane_count;
			temp_edp_vswing = pConf->lcd_control.edp_config->vswing;
			temp_edp_preemphasis = pConf->lcd_control.edp_config->preemphasis;
			break;
		case LCD_DIGITAL_LVDS:
			temp_lvds_repack = pConf->lcd_control.lvds_config->lvds_repack;
			temp_pn_swap = pConf->lcd_control.lvds_config->pn_swap;
			temp_lvds_vswing = pConf->lcd_control.lvds_config->lvds_vswing;
			break;
		case LCD_DIGITAL_TTL:
			temp_ttl_rb_swap = pConf->lcd_control.ttl_config->rb_swap;
			temp_ttl_bit_swap = pConf->lcd_control.ttl_config->bit_swap;
			break;
		default:
			break;
	}
	
	temp_dith_user = pConf->lcd_effect.dith_user;
	temp_dith_ctrl = pConf->lcd_effect.dith_cntl_addr;
	temp_vadj_brightness = pConf->lcd_effect.vadj_brightness;
	temp_vadj_contrast = pConf->lcd_effect.vadj_contrast;
	temp_vadj_saturation = pConf->lcd_effect.vadj_saturation;
}

static void reset_lcd_config(Lcd_Config_t *pConf)
{
	printk("reset lcd config.\n");
	
	_disable_backlight();
	_lcd_module_disable();
	mdelay(200);
	
	pConf->lcd_basic.h_active = temp_lcd_basic.h_active;
	pConf->lcd_basic.v_active = temp_lcd_basic.v_active;
	pConf->lcd_basic.h_period = temp_lcd_basic.h_period;
	pConf->lcd_basic.v_period = temp_lcd_basic.v_period;
	pConf->lcd_basic.lcd_type = temp_lcd_basic.lcd_type;
	pConf->lcd_basic.lcd_bits = temp_lcd_basic.lcd_bits;

	pConf->lcd_timing.pll_ctrl = temp_lcd_timing.pll_ctrl;
	pConf->lcd_timing.div_ctrl = temp_lcd_timing.div_ctrl;
	pConf->lcd_timing.clk_ctrl = temp_lcd_timing.clk_ctrl;
	pConf->lcd_timing.lcd_clk = temp_lcd_timing.lcd_clk;
	pConf->lcd_timing.hsync_width = temp_lcd_timing.hsync_width;
	pConf->lcd_timing.hsync_bp = temp_lcd_timing.hsync_bp;
	pConf->lcd_timing.vsync_width = temp_lcd_timing.vsync_width;
	pConf->lcd_timing.vsync_bp = temp_lcd_timing.vsync_bp;
	pConf->lcd_timing.hvsync_valid = temp_lcd_timing.hvsync_valid;
	//pConf->lcd_timing.de_hstart = temp_lcd_timing.de_hstart;
	//pConf->lcd_timing.de_vstart = temp_lcd_timing.de_vstart;
	pConf->lcd_timing.de_valid = temp_lcd_timing.de_valid;
	pConf->lcd_timing.h_offset = temp_lcd_timing.h_offset;
	pConf->lcd_timing.v_offset = temp_lcd_timing.v_offset;
	pConf->lcd_timing.pol_ctrl = temp_lcd_timing.pol_ctrl;
	
	pConf->lcd_effect.dith_user = temp_dith_user;
	pConf->lcd_effect.dith_cntl_addr = temp_dith_ctrl;
	pConf->lcd_effect.vadj_brightness = temp_vadj_brightness;
	pConf->lcd_effect.vadj_contrast = temp_vadj_contrast;
	pConf->lcd_effect.vadj_saturation = temp_vadj_saturation;
	
	switch (pConf->lcd_basic.lcd_type) {
		case LCD_DIGITAL_MIPI:
			pConf->lcd_control.mipi_config->lane_num = temp_dsi_lane_num;
			pConf->lcd_control.mipi_config->bit_rate_min = temp_dsi_bit_rate_min;
			pConf->lcd_control.mipi_config->bit_rate_max = temp_dsi_bit_rate_max;
			pConf->lcd_control.mipi_config->factor_denominator = temp_factor_denominator;
			pConf->lcd_control.mipi_config->factor_numerator = temp_factor_numerator;
			break;
		case LCD_DIGITAL_EDP:
			pConf->lcd_control.edp_config->link_rate = temp_edp_link_rate;
			pConf->lcd_control.edp_config->lane_count = temp_edp_lane_count;
			pConf->lcd_control.edp_config->vswing = temp_edp_vswing;
			pConf->lcd_control.edp_config->preemphasis = temp_edp_preemphasis;
			break;
		case LCD_DIGITAL_LVDS:
			pConf->lcd_control.lvds_config->lvds_repack = temp_lvds_repack;
			pConf->lcd_control.lvds_config->pn_swap = temp_pn_swap;
			pConf->lcd_control.lvds_config->lvds_vswing = temp_lvds_vswing;
			break;
		case LCD_DIGITAL_TTL:
			pConf->lcd_control.ttl_config->rb_swap = temp_ttl_rb_swap;
			pConf->lcd_control.ttl_config->bit_swap = temp_ttl_bit_swap;
			break;
		default:
			break;
	}
	
	lcd_config_init(pConf);
	_init_vout();
	_lcd_module_enable();
	_enable_backlight();
}

static ssize_t lcd_debug(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret;
	unsigned t[6];
	
	switch (buf[0]) {
		case 'b':	//write basic config
			t[0] = 1024;
			t[1] = 768;
			t[2] = 1344;
			t[3] = 806;
			ret = sscanf(buf, "basic %d %d %d %d", &t[0], &t[1], &t[2], &t[3]);
			pDev->pConf->lcd_basic.h_active = t[0];
			pDev->pConf->lcd_basic.v_active = t[1];
			pDev->pConf->lcd_basic.h_period = t[2];
			pDev->pConf->lcd_basic.v_period = t[3];
			printk("h_active=%d, v_active=%d, h_period=%d, v_period=%d\n", t[0], t[1], t[2], t[3]);
			break;
		case 't':
			if (buf[1] == 'y') {//type
				t[0] = 1;
				t[1] = 6;
				ret = sscanf(buf, "type %d %d", &t[0], &t[1]);
				pDev->pConf->lcd_basic.lcd_type = t[0];
				pDev->pConf->lcd_basic.lcd_bits = t[1];
				printk("lcd_type: %s, lcd_bits: %d\n", lcd_type_table[t[0]], t[1]);
			}
			else if (buf[1] == 'e') {//test
				t[0] = 0;
				ret = sscanf(buf, "test %d", &t[0]);
				if (pDev->pConf->lcd_misc_ctrl.lcd_status == 0)
					printk("lcd is already OFF, can't display test pattern\n");
				else
					pDev->pConf->lcd_misc_ctrl.lcd_test(t[0]);
			}
			else if (buf[1] == 't') {//ttl
				t[0] = 0;
				t[1] = 0;
				ret = sscanf(buf, "ttl %d %d", &t[0], &t[1]);
				pDev->pConf->lcd_control.ttl_config->rb_swap = t[0];
				pDev->pConf->lcd_control.ttl_config->bit_swap = t[1];
				printk("ttl rb_swap: %s, bit_swap: %s\n", ((t[0] == 0) ? "disable" : "enable"), ((t[1] == 0) ? "disable" : "enable"));
			}
			break;
		case 'c':
			t[0] = 40000000;
			t[1] = 0;
			t[2] = 0;
			ret = sscanf(buf, "clock %d %d %d", &t[0], &t[1], &t[2]);
			pDev->pConf->lcd_timing.lcd_clk = t[0];
			pDev->pConf->lcd_timing.clk_ctrl = ((pDev->pConf->lcd_timing.clk_ctrl & ~((1 << CLK_CTRL_AUTO) | (0xf << CLK_CTRL_SS))) | ((1 << CLK_CTRL_AUTO) | (t[1] << CLK_CTRL_SS)));
			pDev->pConf->lcd_timing.pol_ctrl = ((pDev->pConf->lcd_timing.pol_ctrl & ~(1 << POL_CTRL_CLK)) | (t[2] << POL_CTRL_CLK));
			printk("lcd_clk=%dHz, ss_level=%d, clk_pol=%s\n", t[0], t[1], ((t[2] == 0) ? "negative" : "positive"));
			break;
		case 's'://sync
			t[0] = 10;
			t[1] = 60;
			t[2] = 0;
			t[3] = 3;
			t[4] = 20;
			t[5] = 0;
			ret = sscanf(buf, "sync %d %d %d %d %d %d", &t[0], &t[1], &t[2], &t[3], &t[4], &t[5]);
			pDev->pConf->lcd_timing.hsync_width = t[0];
			pDev->pConf->lcd_timing.hsync_bp = t[1];
			pDev->pConf->lcd_timing.vsync_width = t[3];
			pDev->pConf->lcd_timing.vsync_bp = t[4];
			pDev->pConf->lcd_timing.pol_ctrl = ((pDev->pConf->lcd_timing.pol_ctrl & ~((1 << POL_CTRL_HS) | (1 << POL_CTRL_VS))) | ((t[2] << POL_CTRL_HS) | (t[5] << POL_CTRL_VS)));
			printk("hs_width=%d, hs_bp=%d, hs_pol=%s, vs_width=%d, vs_bp=%d, vs_pol=%s\n", t[0], t[1], ((t[2] == 0) ? "negative" : "positive"), t[3], t[4], ((t[5] == 0) ? "negative" : "positive"));
			break;
		case 'v':
			if (buf[2] == 'l') { //valid
				t[0] = 0;
				t[1] = 0;
				t[2] = 1;
				ret = sscanf(buf, "valid %d %d", &t[0], &t[1]);
				pDev->pConf->lcd_timing.hvsync_valid = t[0];
				pDev->pConf->lcd_timing.de_valid = t[1];
				printk("hvsync: %s, de: %s\n", ((t[0] == 0) ? "disable" : "enable"), ((t[1] == 0) ? "disable" : "enable"));
			}
			else if (buf[2] == 'd') { //vadj
				t[0] = 0x0;
				t[1] = 0x80;
				t[2] = 0x100;
				ret = sscanf(buf, "vadj %d %d %d", &t[0], &t[1], &t[2]);
				pDev->pConf->lcd_effect.vadj_brightness = t[0];
				pDev->pConf->lcd_effect.vadj_contrast = t[1];
				pDev->pConf->lcd_effect.vadj_saturation = t[2];
				printk("video adjust: brightness=0x%x, contrast=0x%x, stauration=0x%x\n", t[0], t[1], t[2]);
			}
			break;
		case 'o':
			t[0] = 1;
			t[1] = 0;
			t[2] = 1;
			t[3] = 0;
			ret = sscanf(buf, "offset %d %d %d %d", &t[0], &t[1], &t[2], &t[3]);
			pDev->pConf->lcd_timing.h_offset = ((t[0] << 31) | ((t[1] & 0xffff) << 0));
			pDev->pConf->lcd_timing.v_offset = ((t[2] << 31) | ((t[3] & 0xffff) << 0));
			printk("h_offset = %s%u, v_offset = %s%u\n", (t[0] ? "+" : "-"), (t[1] & 0xffff), (t[2] ? "+" : "-"), (t[3] & 0xffff));
			break;
		case 'l':	//write lvds config		//lvds_repack, pn_swap
			t[0] = 1;
			t[1] = 1;
			t[2] = 0;
			ret = sscanf(buf, "lvds %d %d %d", &t[0], &t[1], &t[2]);
			pDev->pConf->lcd_control.lvds_config->lvds_vswing = t[0];
			pDev->pConf->lcd_control.lvds_config->lvds_repack = t[1];
			pDev->pConf->lcd_control.lvds_config->pn_swap = t[2];
			printk("vswing_level: %u, lvds_repack: %s, rb_swap: %s\n", t[0], ((t[1] == 1) ? "VESA mode" : "JEIDA mode"), ((t[2] == 0) ? "disable" : "enable"));
			break;
#ifdef CONFIG_LCD_IF_MIPI_VALID
		case 'm':	//write mipi config
			if (buf[1] == 'd') {
				t[0] = 0;
				t[1] = 4;
				t[2] = 0;
				ret = sscanf(buf, "mdsi %d %d %d", &t[0],&t[1],&t[2]);
				pDev->pConf->lcd_control.mipi_config->lane_num = (unsigned char)(t[0]);
				pDev->pConf->lcd_control.mipi_config->bit_rate_max = t[1]*1000;
				pDev->pConf->lcd_control.mipi_config->factor_numerator = t[2];
				pDev->pConf->lcd_control.mipi_config->factor_denominator=10;
				printk("dsi lane_num = %d, bit_rate max=%dMHz, factor=%d\n",t[0], t[1], pDev->pConf->lcd_control.mipi_config->factor_numerator);
			}
			else if (buf[1] == 'c') {
				t[0] = 1;
				t[1] = 0;
				t[2] = 0;
				t[3] = 0;
				ret = sscanf(buf, "mctl %d %d %d %d", &t[0],&t[1],&t[2],&t[3]);
				pDev->pConf->lcd_control.mipi_config->operation_mode = ((t[0] << BIT_OPERATION_MODE_INIT) | (t[1] << BIT_OPERATION_MODE_DISP));
				pDev->pConf->lcd_control.mipi_config->transfer_ctrl = ((t[2] << BIT_TRANS_CTRL_CLK) | (t[3] << BIT_TRANS_CTRL_SWITCH));
				printk("dsi operation mode init=%s(%d), display=%s(%d), lp_clk_auto_stop=%d, transfer_switch=%d\n",(t[0]? "command" : "video"), t[0], (t[1] ? "command" : "video"), t[1], t[2], t[3]);
			}
			break;
#endif
		case 'd':
			if (buf[2] == 't') {
				t[0] = 0;
				t[1] = 0x600;
				ret = sscanf(buf, "dither %d %x", &t[0], &t[1]);
				pDev->pConf->lcd_effect.dith_user = t[0];
				pDev->pConf->lcd_effect.dith_cntl_addr = t[1];
				printk("dither user_ctrl: %s, 0x%x\n", ((t[0] == 0) ? "disable" : "enable"), t[1]);
			}
			else {
				printk("power off lcd.\n");
				_disable_backlight();
				pDev->pConf->lcd_power_ctrl.power_ctrl(OFF);
			}
			break;
		case 'w':	//update display config
			if (pDev->pConf->lcd_basic.lcd_type == LCD_DIGITAL_MINILVDS) {
				printk("Don't support miniLVDS yet. Will reset to original lcd config.\n");
				reset_lcd_config(pDev->pConf);
			}
			else {
				_lcd_module_disable();
				mdelay(200);
				lcd_config_init(pDev->pConf);
				_init_vout();
				_lcd_module_enable();
			}
			break;
		case 'r':	
			if (buf[2] == 'a') { //read lcd config
				read_current_lcd_config(pDev->pConf);
			}
			else if (buf[2] == 's') { //reset lcd config
				reset_lcd_config(pDev->pConf);
			}
			break;
		case 'e':
			if (buf[1] == 'n') {
				printk("power on lcd.\n");
				_lcd_module_disable();
				mdelay(200);
				_lcd_module_enable();
				_enable_backlight();
			}
#ifdef CONFIG_LCD_IF_EDP_VALID
			else if (buf[1] == 'd') {
				t[0] = 1;
				t[1] = 4;
				t[2] = 0;
				ret = sscanf(buf, "edp %u %u %u", &t[0], &t[1], &t[2]);
				if (t[0] == 0)
					pDev->pConf->lcd_control.edp_config->link_rate = 0;
				else
					pDev->pConf->lcd_control.edp_config->link_rate = 1;
				switch (t[1]) {
					case 1:
					case 2:
						pDev->pConf->lcd_control.edp_config->lane_count = t[1];
						break;
					default:
						pDev->pConf->lcd_control.edp_config->lane_count = 4;
						break;
				}
				pDev->pConf->lcd_control.edp_config->vswing = t[2];
				printk("set edp link_rate = %s, lane_count = %u, vswing_level = %u\n", ((pDev->pConf->lcd_control.edp_config->link_rate == 0) ? "1.62G":"2.7G"), pDev->pConf->lcd_control.edp_config->lane_count, pDev->pConf->lcd_control.edp_config->vswing);
			}
#endif
			else {
				printk("wrong format of lcd debug command.\n");
			}
			break;
		default:
			printk("wrong format of lcd debug command.\n");
	}	
	
	if (ret != 1 || ret !=2)
		return -EINVAL;
	
	return count;
	//return 0;
}

static ssize_t lcd_status_read(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "read lcd status: %s\n", (pDev->pConf->lcd_misc_ctrl.lcd_status ? "ON":"OFF"));
}

static ssize_t lcd_status_write(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret;
	unsigned temp;

	temp = 1;
	ret = sscanf(buf, "%d", &temp);
	if (temp) {
		if (pDev->pConf->lcd_misc_ctrl.lcd_status == 0) {
			mutex_lock(&lcd_vout_mutex);
			_lcd_module_enable();
			_enable_backlight();
			mutex_unlock(&lcd_vout_mutex);
		}
		else {
			printk("lcd is already ON\n");
		}
	}
	else {
		if (pDev->pConf->lcd_misc_ctrl.lcd_status == 1) {
			mutex_lock(&lcd_vout_mutex);
			_disable_backlight();
			_lcd_module_disable();
			mutex_unlock(&lcd_vout_mutex);
		}
		else {
			printk("lcd is already OFF\n");
		}
	}

	if (ret != 1 || ret !=2)
		return -EINVAL;

	return count;
	//return 0;
}

static ssize_t lcd_print_read(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "lcd print flag: %u\n", lcd_print_flag);
}

static ssize_t lcd_print_write(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int ret;

	ret = sscanf(buf, "%u", &lcd_print_flag);
	printk("write lcd print flag: %u\n", lcd_print_flag);

	if (ret != 1 || ret !=2)
		return -EINVAL;

	return count;
	//return 0;
}

static struct class_attribute lcd_debug_class_attrs[] = {
	__ATTR(debug,  S_IRUGO | S_IWUSR, lcd_debug_help, lcd_debug),
	__ATTR(help,  S_IRUGO | S_IWUSR, lcd_debug_common_help, NULL),
	__ATTR(status,  S_IRUGO | S_IWUSR, lcd_status_read, lcd_status_write),
	__ATTR(print,  S_IRUGO | S_IWUSR, lcd_print_read, lcd_print_write),
};

static int creat_lcd_class(void)
{
    pDev->pConf->lcd_misc_ctrl.debug_class = class_create(THIS_MODULE, "lcd");
    if(IS_ERR(pDev->pConf->lcd_misc_ctrl.debug_class)) {
        printk("create lcd debug class fail\n");
        return -1;
    }
    return 0;
}

static int remove_lcd_class(void)
{
    if (pDev->pConf->lcd_misc_ctrl.debug_class == NULL)
        return -1;

    class_destroy(pDev->pConf->lcd_misc_ctrl.debug_class);
    pDev->pConf->lcd_misc_ctrl.debug_class = NULL;
    return 0;
}

static int creat_lcd_attr(void)
{
    int i;

    if(pDev->pConf->lcd_misc_ctrl.debug_class == NULL) {
        printk("no lcd debug class exist\n");
        return -1;
    }

    for(i=0;i<ARRAY_SIZE(lcd_debug_class_attrs);i++) {
        if (class_create_file(pDev->pConf->lcd_misc_ctrl.debug_class, &lcd_debug_class_attrs[i])) {
            printk("create lcd debug attribute %s fail\n", lcd_debug_class_attrs[i].attr.name);
        }
    }

    return 0;
}

static int remove_lcd_attr(void)
{
    int i;

    if (pDev->pConf->lcd_misc_ctrl.debug_class == NULL)
        return -1;

    for(i=0;i<ARRAY_SIZE(lcd_debug_class_attrs);i++) {
        class_remove_file(pDev->pConf->lcd_misc_ctrl.debug_class, &lcd_debug_class_attrs[i]);
    }

    return 0;
}
//****************************

static int lcd_reboot_notifier(struct notifier_block *nb, unsigned long state, void *cmd)
 {
	lcd_print("[%s]: %lu\n", __FUNCTION__, state);
	if (pDev->pConf->lcd_misc_ctrl.lcd_status == 0)
		return NOTIFY_DONE;
	
	_disable_backlight();
	_lcd_module_disable();

	return NOTIFY_OK;
}

static int amlogic_pmu_gpio_name_map_num(const char *name)
{
	int index;
	
	for(index = 0; index < LCD_POWER_PMU_GPIO_MAX; index++) {
		if(!strcasecmp(name, lcd_power_pmu_gpio_table[index]))
			break;
	}
	return index;
}

#ifdef CONFIG_USE_OF
#define LCD_MODEL_LEN_MAX    30
static int _get_lcd_model_timing(Lcd_Config_t *pConf, struct platform_device *pdev)
{
	int ret=0;
	const char *str;
	unsigned int val;
	//unsigned int lcd_para[100];
	unsigned int *lcd_para = (unsigned int *)kmalloc(sizeof(unsigned int)*100, GFP_KERNEL);
	int i, j;
	struct device_node *lcd_model_node;
	phandle fhandle;
	
	if (lcd_para == NULL) {
		printk("[_get_lcd_model_timing]: Not enough memory\n");
		return -1;
	}
	if (pdev->dev.of_node) {
		ret = of_property_read_u32(pdev->dev.of_node,"lcd_model_config",&fhandle);
		lcd_model_node = of_find_node_by_phandle(fhandle);
		ret = of_property_read_string(lcd_model_node,"model_name", &str);
		if(ret) {
			str = "none";
			printk("lcd: faild to get lcd_model_name!\n");
		}
		pConf->lcd_basic.model_name = (char *)kmalloc(sizeof(char)*LCD_MODEL_LEN_MAX, GFP_KERNEL);
		if (pConf->lcd_basic.model_name == NULL) {
			printk("[_get_lcd_model_timing]: Not enough memory\n");
		}
		else {
			memset(pConf->lcd_basic.model_name, 0, LCD_MODEL_LEN_MAX);
			strcpy(pConf->lcd_basic.model_name, str);
			printk("load lcd model in dtb: %s\n", pConf->lcd_basic.model_name);
		}
		
		ret = of_property_read_string(lcd_model_node, "interface", &str);
		if (ret) {
			printk("faild to get lcd_type!\n");
			str = "invalid";
		}	
		for(val = 0; val < LCD_TYPE_MAX; val++) {
			if(!strcasecmp(str, lcd_type_table[val]))
				break;
		}		
		pConf->lcd_basic.lcd_type = val;
		lcd_print("lcd_type= %s(%u)\n", lcd_type_table[pConf->lcd_basic.lcd_type], pConf->lcd_basic.lcd_type);
		ret = of_property_read_u32_array(lcd_model_node,"active_area",&lcd_para[0],2);
		if(ret){
			printk("faild to get active_area\n");
		}
		else {
			pConf->lcd_basic.h_active_area = lcd_para[0];
			pConf->lcd_basic.v_active_area = lcd_para[1];
			pConf->lcd_basic.screen_ratio_width = lcd_para[0];
			pConf->lcd_basic.screen_ratio_height = lcd_para[1];
		}
		lcd_print("h_active_area = %umm, v_active_area =%umm\n", pConf->lcd_basic.h_active_area, pConf->lcd_basic.v_active_area);
		ret = of_property_read_u32_array(lcd_model_node,"lcd_bits_option",&lcd_para[0],2);
		if(ret){
			printk("faild to get lcd_bits_option\n");
		}
		else {
			pConf->lcd_basic.lcd_bits = (unsigned short)(lcd_para[0]);
			pConf->lcd_basic.lcd_bits_option = (unsigned short)(lcd_para[1]);
		}
		lcd_print("lcd_bits = %u, lcd_bits_option = %u\n", pConf->lcd_basic.lcd_bits, pConf->lcd_basic.lcd_bits_option);
		ret = of_property_read_u32_array(lcd_model_node,"resolution", &lcd_para[0], 2);
		if(ret){
			printk("faild to get resolution\n");
		}
		else {
			pConf->lcd_basic.h_active = (unsigned short)(lcd_para[0]);
			pConf->lcd_basic.v_active = (unsigned short)(lcd_para[1]);
		}		
		ret = of_property_read_u32_array(lcd_model_node,"period",&lcd_para[0],2);
		if(ret){
			printk("faild to get period\n");
		}
		else {
			pConf->lcd_basic.h_period = (unsigned short)(lcd_para[0]);
			pConf->lcd_basic.v_period = (unsigned short)(lcd_para[1]);
		}
		lcd_print("h_active = %u, v_active =%u, h_period = %u, v_period = %u\n", pConf->lcd_basic.h_active, pConf->lcd_basic.v_active, pConf->lcd_basic.h_period, pConf->lcd_basic.v_period);
		ret = of_property_read_u32_array(lcd_model_node,"clock_hz_pol",&lcd_para[0], 2);
		if(ret){
			printk("faild to get clock_hz_pol\n");
		}
		else {
			pConf->lcd_timing.lcd_clk = lcd_para[0];
			pConf->lcd_timing.pol_ctrl = (lcd_para[1] << POL_CTRL_CLK);
		}
		lcd_print("pclk = %uHz, pol=%u\n", pConf->lcd_timing.lcd_clk, (pConf->lcd_timing.pol_ctrl >> POL_CTRL_CLK) & 1);
		ret = of_property_read_u32_array(lcd_model_node,"hsync_width_backporch",&lcd_para[0], 2);
		if(ret){
			printk("faild to get hsync_width_backporch\n");
		}
		else {
			pConf->lcd_timing.hsync_width = (unsigned short)(lcd_para[0]);
			pConf->lcd_timing.hsync_bp = (unsigned short)(lcd_para[1]);
		}
		lcd_print("hsync width = %u, backporch = %u\n", pConf->lcd_timing.hsync_width, pConf->lcd_timing.hsync_bp);
		ret = of_property_read_u32_array(lcd_model_node,"vsync_width_backporch",&lcd_para[0], 2);
		if(ret){
			printk("faild to get vsync_width_backporch\n");
		}
		else {
			pConf->lcd_timing.vsync_width = (unsigned short)(lcd_para[0]);
			pConf->lcd_timing.vsync_bp = (unsigned short)(lcd_para[1]);
		}
		lcd_print("vsync width = %u, backporch = %u\n", pConf->lcd_timing.vsync_width, pConf->lcd_timing.vsync_bp);
		ret = of_property_read_u32_array(lcd_model_node,"pol_hsync_vsync",&lcd_para[0], 2);
		if(ret){
			printk("faild to get pol_hsync_vsync\n");
		}
		else {
			pConf->lcd_timing.pol_ctrl = (pConf->lcd_timing.pol_ctrl & ~((1 << POL_CTRL_HS) | (1 << POL_CTRL_VS))) | ((lcd_para[0] << POL_CTRL_HS) | (lcd_para[1] << POL_CTRL_VS));
		}
		lcd_print("pol hsync = %u, vsync = %u\n", (pConf->lcd_timing.pol_ctrl >> POL_CTRL_HS) & 1, (pConf->lcd_timing.pol_ctrl >> POL_CTRL_VS) & 1);
		ret = of_property_read_u32_array(lcd_model_node,"vsync_horizontal_phase",&lcd_para[0], 2);
		if(ret){
			printk("faild to get vsync_horizontal_phase\n");
			pConf->lcd_timing.vsync_h_phase = 0;
		} else {
			pConf->lcd_timing.vsync_h_phase = ((lcd_para[0] << 31) | ((lcd_para[1] & 0xffff) << 0));
		}
		if (lcd_para[0] == 0)
			lcd_print("vsync_horizontal_phase= %d\n", lcd_para[1]);
		else
			lcd_print("vsync_horizontal_phase= -%d\n", lcd_para[1]);

        if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_MIPI) {
            ret = of_property_read_u32(lcd_model_node,"dsi_lane_num",&val);
            if(ret){
                printk("faild to get dsi_lane_num\n");
                pConf->lcd_control.mipi_config->lane_num = 4;
            }
            else {
                pConf->lcd_control.mipi_config->lane_num = (unsigned char)val;
            }
            lcd_print("dsi_lane_num= %d\n",  pConf->lcd_control.mipi_config->lane_num);
            ret = of_property_read_u32(lcd_model_node,"dsi_bit_rate_max",&val);
            if(ret){
                printk("faild to get dsi_bit_rate_max\n");
                pConf->lcd_control.mipi_config->bit_rate_max = 0;
            }
            else {
                pConf->lcd_control.mipi_config->bit_rate_max = val;
            }
            lcd_print("dsi bit_rate max = %dMHz\n", pConf->lcd_control.mipi_config->bit_rate_max);
            ret = of_property_read_u32(lcd_model_node,"pclk_lanebyteclk_factor",&val);
            if(ret){
                printk("faild to get pclk_lanebyteclk_factor\n");
                pConf->lcd_control.mipi_config->factor_numerator = 0;
            }
            else {
                pConf->lcd_control.mipi_config->factor_numerator = val;
            }
            pConf->lcd_control.mipi_config->factor_denominator = 10;
            lcd_print("pclk_lanebyteclk factor= %d\n", pConf->lcd_control.mipi_config->factor_numerator);
            ret = of_property_read_u32_array(lcd_model_node,"dsi_operation_mode",&lcd_para[0], 2);
            if(ret){
                printk("faild to get dsi_operation_mode\n");
                pConf->lcd_control.mipi_config->operation_mode = ((1 << BIT_OPERATION_MODE_INIT) | (0 << BIT_OPERATION_MODE_DISP));
            }
            else {
                pConf->lcd_control.mipi_config->operation_mode = ((lcd_para[0] << BIT_OPERATION_MODE_INIT) | (lcd_para[1] << BIT_OPERATION_MODE_DISP));
            }
            lcd_print("dsi_operation_mode init=%d, display=%d\n", (pConf->lcd_control.mipi_config->operation_mode >> BIT_OPERATION_MODE_INIT) & 1, (pConf->lcd_control.mipi_config->operation_mode >> BIT_OPERATION_MODE_DISP) & 1);
            ret = of_property_read_u32_array(lcd_model_node,"dsi_transfer_ctrl",&lcd_para[0], 2);
            if(ret){
                printk("faild to get dsi_transfer_ctrl\n");
                pConf->lcd_control.mipi_config->transfer_ctrl = ((0 << BIT_TRANS_CTRL_CLK) | (0 << BIT_TRANS_CTRL_SWITCH));
            }
            else {
                pConf->lcd_control.mipi_config->transfer_ctrl = ((lcd_para[0] << BIT_TRANS_CTRL_CLK) | (lcd_para[1] << BIT_TRANS_CTRL_SWITCH));
            }
            lcd_print("dsi_transfer_ctrl clk=%d, switch=%d\n", (pConf->lcd_control.mipi_config->transfer_ctrl >> BIT_TRANS_CTRL_CLK) & 1, (pConf->lcd_control.mipi_config->transfer_ctrl >> BIT_TRANS_CTRL_SWITCH) & 3);
            //detect dsi init on table
            if (pConf->lcd_control.mipi_config->dsi_init_on != NULL) {
                ret = of_property_read_u32_index(lcd_model_node,"dsi_init_on", 0, &lcd_para[0]);
                if (ret) {
                    printk("faild to get dsi_init_on\n");
                }
                else {
                    i = 0;
                    while (i < DSI_INIT_ON_MAX) {
                        ret = of_property_read_u32_index(lcd_model_node,"dsi_init_on", i, &val);
                        if (val == 0xff) {
                            ret = of_property_read_u32_index(lcd_model_node,"dsi_init_on", (i+1), &val);
                            i += 2;
                            if (val == 0xff)
                                break;
                        }
                        else {
                            ret = of_property_read_u32_index(lcd_model_node,"dsi_init_on", (i+2), &val);
                            i = i + 3 + val;
                        }
                    }
                    ret = of_property_read_u32_array(lcd_model_node,"dsi_init_on", &lcd_para[0], i);
                    if(ret){
                        printk("faild to get dsi_init_on\n");
                    }
                    else {
                        lcd_print("dsi_init_on: ");
                        for (j=0; j<i; j++) {
                            pConf->lcd_control.mipi_config->dsi_init_on[j] = (unsigned char)(lcd_para[j] & 0xff);
                            lcd_print("0x%02x ", pConf->lcd_control.mipi_config->dsi_init_on[j]);
                        }
                        lcd_print("\n");
                    }
                }
            }
            //detect dsi init off table
            if (pConf->lcd_control.mipi_config->dsi_init_off != NULL) {
                ret = of_property_read_u32_index(lcd_model_node,"dsi_init_off", 0, &lcd_para[0]);
                if (ret) {
                    printk("faild to get dsi_init_off\n");
                }
                else {
                    i = 0;
                    while (i < DSI_INIT_OFF_MAX) {
                        ret = of_property_read_u32_index(lcd_model_node,"dsi_init_off", i, &val);
                        if (val == 0xff) {
                            ret = of_property_read_u32_index(lcd_model_node,"dsi_init_off", (i+1), &val);
                            i += 2;
                            if (val == 0xff)
                                break;
                        }
                        else {
                            ret = of_property_read_u32_index(lcd_model_node,"dsi_init_off", (i+2), &val);
                            i = i + 3 + val;
                        }
                    }
                    ret = of_property_read_u32_array(lcd_model_node,"dsi_init_off", &lcd_para[0], i);
                    if(ret){
                        printk("faild to get dsi_init_off\n");
                    }
                    else {
                        lcd_print("dsi_init_off: ");
                        for (j=0; j<i; j++) {
                            pConf->lcd_control.mipi_config->dsi_init_off[j] = (unsigned char)(lcd_para[j] & 0xff);
                            lcd_print("0x%02x ", pConf->lcd_control.mipi_config->dsi_init_off[j]);
                        }
                        lcd_print("\n");
                    }
                }
            }
            ret = of_property_read_u32(lcd_model_node,"lcd_extern_init",&val);
            if(ret){
                printk("faild to get lcd_extern_init\n");
                pConf->lcd_control.mipi_config->lcd_extern_init =0;
            } else {
                pConf->lcd_control.mipi_config->lcd_extern_init =(unsigned char)(val);
            }
            lcd_print("lcd_extern_init = %d\n",  pConf->lcd_control.mipi_config->lcd_extern_init);
        }
        else if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_EDP) {
            ret = of_property_read_u32(lcd_model_node,"max_lane_count",&val);
            if(ret){
                printk("faild to get max_lane_count\n");
                pConf->lcd_control.edp_config->max_lane_count = 4;
            } else {
                pConf->lcd_control.edp_config->max_lane_count =(unsigned char)(val);
            }
            lcd_print("max_lane_count = %d\n", pConf->lcd_control.edp_config->max_lane_count);
        }
    }
    kfree(lcd_para);
    return ret;
}

static int _get_lcd_default_config(Lcd_Config_t *pConf, struct platform_device *pdev)
{
	int ret=0;
	unsigned int val;
	unsigned int lcd_para[5];
	//unsigned int gamma_temp[256];
	unsigned int *gamma_temp = (unsigned int *)kmalloc(sizeof(unsigned int)*256, GFP_KERNEL);
	int i;
	unsigned int lcd_gamma_multi = 0;
	
	if (gamma_temp == NULL) {
		printk("[_get_lcd_default_config]: Not enough memory\n");
		return -1;
	}
	//pdev->dev.of_node = of_find_node_by_name(NULL,"lcd");
	if (pdev->dev.of_node) {
		if (pConf->lcd_basic.lcd_bits_option == 1) {
			ret = of_property_read_u32(pdev->dev.of_node,"lcd_bits_user",&val);
			if(ret){
				printk("don't find to match lcd_bits_user, use panel typical setting.\n");
			}
			else {
				pConf->lcd_basic.lcd_bits = (unsigned short)(val);
				printk("lcd_bits = %u\n", pConf->lcd_basic.lcd_bits);
			}
		}
		//ttl & lvds config
		if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_TTL) {
			ret = of_property_read_u32_array(pdev->dev.of_node,"ttl_rb_bit_swap",&lcd_para[0], 2);
			if(ret){
				printk("don't find to match ttl_rb_bit_swap, use default setting.\n");
			}
			else {
				pConf->lcd_control.ttl_config->rb_swap = (unsigned char)(lcd_para[0]);
				pConf->lcd_control.ttl_config->bit_swap = (unsigned char)(lcd_para[1]);
				printk("ttl rb_swap = %u, bit_swap = %u\n", pConf->lcd_control.ttl_config->rb_swap, pConf->lcd_control.ttl_config->bit_swap);
			}
		}
		if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_LVDS) {
			ret = of_property_read_u32(pdev->dev.of_node,"lvds_channel_pn_swap",&val);
			if(ret){
				printk("don't find to match lvds_channel_pn_swap, use default setting.\n");
			}
			else {
				pConf->lcd_control.lvds_config->pn_swap = val;
				printk("lvds_pn_swap = %u\n", pConf->lcd_control.lvds_config->pn_swap);
			}
		}

		//recommend setting
		ret = of_property_read_u32_array(pdev->dev.of_node,"valid_hvsync_de",&lcd_para[0], 2);
		if(ret){
			printk("don't find to match valid_hvsync_de, use default setting.\n");
		}
		else {
			pConf->lcd_timing.hvsync_valid = (unsigned short)(lcd_para[0]);
			pConf->lcd_timing.de_valid = (unsigned short)(lcd_para[1]);
			lcd_print("valid hvsync = %u, de = %u\n", pConf->lcd_timing.hvsync_valid, pConf->lcd_timing.de_valid);
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"hsign_hoffset_vsign_voffset",&lcd_para[0], 4);
		if(ret){
			printk("don't find to match hsign_hoffset_vsign_voffset, use default setting.\n");
			pConf->lcd_timing.h_offset = 0;
			pConf->lcd_timing.v_offset = 0;
		}
		else {
			pConf->lcd_timing.h_offset = ((lcd_para[0] << 31) | ((lcd_para[1] & 0xffff) << 0));
			pConf->lcd_timing.v_offset = ((lcd_para[2] << 31) | ((lcd_para[3] & 0xffff) << 0));
			lcd_print("h_offset = %s%u, ", (((pConf->lcd_timing.h_offset >> 31) & 1) ? "-" : ""), (pConf->lcd_timing.h_offset & 0xffff));
			lcd_print("v_offset = %s%u\n", (((pConf->lcd_timing.v_offset >> 31) & 1) ? "-" : ""), (pConf->lcd_timing.v_offset & 0xffff));
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"dither_user_ctrl",&lcd_para[0], 2);
		if(ret){
			printk("don't find to match dither_user_ctrl, use default setting.\n");
			pConf->lcd_effect.dith_user = 0;
		}
		else {
			pConf->lcd_effect.dith_user = (unsigned short)(lcd_para[0]);
			pConf->lcd_effect.dith_cntl_addr = (unsigned short)(lcd_para[1]);
			lcd_print("dither_user = %u, dither_ctrl = 0x%x\n", pConf->lcd_effect.dith_user, pConf->lcd_effect.dith_cntl_addr);
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"vadj_brightness_contrast_saturation",&lcd_para[0], 3);
		if(ret){
			printk("don't find to match vadj_brightness_contrast_saturation, use default setting.\n");
		}
		else {
			pConf->lcd_effect.vadj_brightness = lcd_para[0];
			pConf->lcd_effect.vadj_contrast = lcd_para[1];
			pConf->lcd_effect.vadj_saturation = lcd_para[2];
			lcd_print("vadj_brightness = 0x%x, vadj_contrast = 0x%x, vadj_saturation = 0x%x\n", pConf->lcd_effect.vadj_brightness, pConf->lcd_effect.vadj_contrast, pConf->lcd_effect.vadj_saturation);
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_en_reverse",&lcd_para[0], 2);
		if(ret){
			printk("don't find to match gamma_en_reverse, use default setting.\n");
		}
		else {
			pConf->lcd_effect.gamma_ctrl = ((lcd_para[0] << GAMMA_CTRL_EN) | (lcd_para[1] << GAMMA_CTRL_REVERSE));
			lcd_print("gamma_en = %u, gamma_reverse=%u\n", ((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_EN) & 1), ((pConf->lcd_effect.gamma_ctrl >> GAMMA_CTRL_REVERSE) & 1));
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_multi_rgb_coeff",&lcd_para[0], 4);
		if(ret){
			printk("don't find to match gamma_multi_rgb_coeff, use default setting.\n");
		}
		else {
			lcd_gamma_multi = lcd_para[0];
			pConf->lcd_effect.gamma_r_coeff = (unsigned short)(lcd_para[1]);
			pConf->lcd_effect.gamma_g_coeff = (unsigned short)(lcd_para[2]);
			pConf->lcd_effect.gamma_b_coeff = (unsigned short)(lcd_para[3]);
			lcd_print("gamma_multi = %u, gamma_r_coeff = %u, gamma_g_coeff = %u, gamma_b_coeff = %u\n", lcd_gamma_multi, pConf->lcd_effect.gamma_r_coeff, pConf->lcd_effect.gamma_g_coeff, pConf->lcd_effect.gamma_b_coeff);
		}
		if (lcd_gamma_multi == 1) {
			ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_table_r",&gamma_temp[0], 256);
			if(ret){
				printk("don't find to match gamma_table_r, use default table.\n");
				lcd_setup_gamma_table(pConf, 0);
			}
			else {
				for (i=0; i<256; i++) {
					pConf->lcd_effect.GammaTableR[i] = (unsigned short)(gamma_temp[i] << 2);
				}
				lcd_print("load gamma_table_r.\n");
			}
			ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_table_g",&gamma_temp[0], 256);
			if(ret){
				printk("don't find to match gamma_table_g, use default table.\n");
				lcd_setup_gamma_table(pConf, 1);
			}
			else {
				for (i=0; i<256; i++) {
					pConf->lcd_effect.GammaTableG[i] = (unsigned short)(gamma_temp[i] << 2);
				}
				lcd_print("load gamma_table_g.\n");
			}
			ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_table_b",&gamma_temp[0], 256);
			if(ret){
				printk("don't find to match gamma_table_b, use default table.\n");
				lcd_setup_gamma_table(pConf, 2);
			}
			else {
				for (i=0; i<256; i++) {
					pConf->lcd_effect.GammaTableB[i] = (unsigned short)(gamma_temp[i] << 2);
				}
				lcd_print("load gamma_table_b.\n");
			}
		}
		else {
			ret = of_property_read_u32_array(pdev->dev.of_node,"gamma_table",&gamma_temp[0], 256);
			if(ret){
				printk("don't find to match gamma_table, use default table.\n");
				lcd_setup_gamma_table(pConf, 3);
			}
			else {
				for (i=0; i<256; i++) {
					pConf->lcd_effect.GammaTableR[i] = (unsigned short)(gamma_temp[i] << 2);
					pConf->lcd_effect.GammaTableG[i] = (unsigned short)(gamma_temp[i] << 2);
					pConf->lcd_effect.GammaTableB[i] = (unsigned short)(gamma_temp[i] << 2);
				}
				lcd_print("load gamma_table.\n");
			}
		}
		
		//default setting
		ret = of_property_read_u32(pdev->dev.of_node,"clock_spread_spectrum",&val);
		if(ret){
			printk("don't find to match clock_spread_spectrum, use default setting.\n");
		}
		else {
			pConf->lcd_timing.clk_ctrl = ((pConf->lcd_timing.clk_ctrl & ~(0xf << CLK_CTRL_SS)) | (val << CLK_CTRL_SS));
			lcd_print("lcd clock spread spectrum = %u\n", (pConf->lcd_timing.clk_ctrl >> CLK_CTRL_SS) & 0xf);
		}
		ret = of_property_read_u32(pdev->dev.of_node,"clock_auto_generation",&val);
		if(ret){
			printk("don't find to match clock_auto_generation, use default setting.\n");
		}
		else {
			pConf->lcd_timing.clk_ctrl = ((pConf->lcd_timing.clk_ctrl & ~(1 << CLK_CTRL_AUTO)) | (val << CLK_CTRL_AUTO));
			lcd_print("lcd clock auto_generation = %u\n", (pConf->lcd_timing.clk_ctrl >> CLK_CTRL_AUTO) & 0x1);
		}
		if (((pConf->lcd_timing.clk_ctrl >> CLK_CTRL_AUTO) & 0x1) == 0) {
			ret = of_property_read_u32_array(pdev->dev.of_node,"clk_pll_div_clk_ctrl",&lcd_para[0], 3);
			if(ret){
				printk("don't find to match clk_pll_div_clk_ctrl, use default setting.\n");
			}
			else {
				pConf->lcd_timing.pll_ctrl = lcd_para[0];
				pConf->lcd_timing.div_ctrl = lcd_para[1];
				pConf->lcd_timing.clk_ctrl = lcd_para[2];
				printk("pll_ctrl = 0x%x, div_ctrl = 0x%x, clk_ctrl=0x%x\n", pConf->lcd_timing.pll_ctrl, pConf->lcd_timing.div_ctrl, (pConf->lcd_timing.clk_ctrl & 0xffff));
			}
		}
		if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_LVDS) {
			ret = of_property_read_u32(pdev->dev.of_node,"lvds_vswing",&val);
			if(ret){
				printk("don't find to match lvds_vswing, use default setting.\n");
			}
			else {
				pConf->lcd_control.lvds_config->lvds_vswing = val;
				printk("lvds_vswing level = %u\n", pConf->lcd_control.lvds_config->lvds_vswing = val);
			}
			ret = of_property_read_u32_array(pdev->dev.of_node,"lvds_user_repack",&lcd_para[0], 2);
			if(ret){
				printk("don't find to match lvds_user_repack, use default setting.\n");
				pConf->lcd_control.lvds_config->lvds_repack_user = 0;
				pConf->lcd_control.lvds_config->lvds_repack = 1;
			}
			else {
				pConf->lcd_control.lvds_config->lvds_repack_user = lcd_para[0];
				pConf->lcd_control.lvds_config->lvds_repack = lcd_para[1];
				if (lcd_para[0] > 0) {
					printk("lvds_repack = %u\n", pConf->lcd_control.lvds_config->lvds_repack);
				}
				else {
					lcd_print("lvds_repack_user = %u, lvds_repack = %u\n", pConf->lcd_control.lvds_config->lvds_repack_user, pConf->lcd_control.lvds_config->lvds_repack);
				}
			}
		}
		if (pConf->lcd_basic.lcd_type == LCD_DIGITAL_EDP) {
			ret = of_property_read_u32_array(pdev->dev.of_node,"edp_user_link_rate_lane_count",&lcd_para[0], 3);
			if(ret){
				pConf->lcd_control.edp_config->link_user = 0;
				pConf->lcd_control.edp_config->link_rate = 1;
				pConf->lcd_control.edp_config->lane_count = 4;
				printk("don't find to match edp_user_link_rate_lane_count, use default setting.\n");
			}
			else {
				pConf->lcd_control.edp_config->link_user = (unsigned char)(lcd_para[0]);
				pConf->lcd_control.edp_config->link_rate = (unsigned char)(lcd_para[1]);
				pConf->lcd_control.edp_config->lane_count = (unsigned char)(lcd_para[2]);
				if (pConf->lcd_control.edp_config->link_user > 0) {
					printk("edp link_rate = %s, lane_count = %u\n", ((pConf->lcd_control.edp_config->link_rate == 0) ? "1.62G":"2.7G"), pConf->lcd_control.edp_config->lane_count);
				}
				else {
					lcd_print("edp user = %u, link_rate = %s, lane_count = %u\n", pConf->lcd_control.edp_config->link_user, ((pConf->lcd_control.edp_config->link_rate == 0) ? "1.62G":"2.7G"), pConf->lcd_control.edp_config->lane_count);
				}
			}
			ret = of_property_read_u32_array(pdev->dev.of_node,"edp_link_adaptive_vswing",&lcd_para[0], 2);
			if(ret){
				printk("don't find to match edp_link_adaptive_vswing, use default setting.\n");
				pConf->lcd_control.edp_config->link_adaptive = 0;
				pConf->lcd_control.edp_config->vswing = 0;
				pConf->lcd_control.edp_config->preemphasis = 0;
			}
			else {
				pConf->lcd_control.edp_config->link_adaptive = (unsigned char)(lcd_para[0]);
				pConf->lcd_control.edp_config->vswing = (unsigned char)(lcd_para[1]);
				pConf->lcd_control.edp_config->preemphasis = 0;
				if (pConf->lcd_control.edp_config->link_adaptive == 0) {
					printk("edp swing_level = %u\n", pConf->lcd_control.edp_config->vswing);
				}
				else {
					lcd_print("edp link_adaptive = %u, swing_level = %u\n", pConf->lcd_control.edp_config->link_adaptive, pConf->lcd_control.edp_config->vswing);
				}
			}
			ret = of_property_read_u32(pdev->dev.of_node,"edp_sync_clock_mode",&val);
			if(ret){
				printk("don't find to match edp_sync_clock_mode, use default setting.\n");
				pConf->lcd_control.edp_config->sync_clock_mode = 1;
			}
			else {
				pConf->lcd_control.edp_config->sync_clock_mode = (val & 1);
				printk("edp sync_clock_mode = %u\n", pConf->lcd_control.edp_config->sync_clock_mode);
			}
		}
		ret = of_property_read_u32_array(pdev->dev.of_node,"rgb_base_coeff",&lcd_para[0], 2);
		if(ret){
			printk("don't find to match rgb_base_coeff, use default setting.\n");
		}
		else {
			pConf->lcd_effect.rgb_base_addr = (unsigned short)(lcd_para[0]);
			pConf->lcd_effect.rgb_coeff_addr = (unsigned short)(lcd_para[1]);
			lcd_print("rgb_base = 0x%x, rgb_coeff = 0x%x\n", pConf->lcd_effect.rgb_base_addr, pConf->lcd_effect.rgb_coeff_addr);
		}
		// ret = of_property_read_u32_array(pdev->dev.of_node,"video_on_pixel_line",&lcd_para[0], 2);
		// if(ret){
			// printk("don't find to match video_on_pixel_line, use default setting.\n");
		// }
		// else {
			// pConf->lcd_timing.video_on_pixel = (unsigned short)(lcd_para[0]);
			// pConf->lcd_timing.video_on_line = (unsigned short)(lcd_para[1]);
			// lcd_print("video_on_pixel = %u, video_on_line = %u\n", pConf->lcd_timing.video_on_pixel, pConf->lcd_timing.video_on_line);
		// }
	}
	kfree(gamma_temp);
	return ret;
}

static int _get_lcd_power_config(Lcd_Config_t *pConf, struct platform_device *pdev)
{
	int ret=0;
	const char *str;
	unsigned char propname[20];
	int val;
	unsigned int lcd_para[LCD_POWER_CTRL_STEP_MAX];
	int i;
	int index;
	
	if (pdev->dev.of_node) {
		//lcd power on
		for (i=0; i < LCD_POWER_CTRL_STEP_MAX; i++) {
			//propname = kasprintf(GFP_KERNEL, "power_on_step_%d", i+1);
			sprintf(propname, "power_on_step_%d", i+1);
			ret = of_property_read_string_index(pdev->dev.of_node, propname, 0, &str);
			if (ret) {
				lcd_print("faild to get %s\n", propname);
				break;
			}
			else if ((strcasecmp(str, "null") == 0) || ((strcasecmp(str, "n") == 0))) {
				break;
			}
			else {
				lcd_print("%s 0: %s\n", propname, str);
				for(index = 0; index < LCD_POWER_TYPE_MAX; index++) {
					if(!strcasecmp(str, lcd_power_type_table[index]))
						break;
				}
				pConf->lcd_power_ctrl.power_on_config[i].type = index;
				
				if (pConf->lcd_power_ctrl.power_on_config[i].type < LCD_POWER_TYPE_SIGNAL) {
					ret = of_property_read_string_index(pdev->dev.of_node, propname, 1, &str);
					if (ret) {
						printk("faild to get %s index 1\n", propname);
					}
					else {
						if (pConf->lcd_power_ctrl.power_on_config[i].type == LCD_POWER_TYPE_CPU) {
							val = amlogic_gpio_name_map_num(str);
							ret = lcd_gpio_request(val);
							if (ret) {
							  printk("faild to alloc lcd power ctrl gpio (%s)\n", str);
							}
							pConf->lcd_power_ctrl.power_on_config[i].gpio = val;
						}
						else if (pConf->lcd_power_ctrl.power_on_config[i].type == LCD_POWER_TYPE_PMU) {
							pConf->lcd_power_ctrl.power_on_config[i].gpio = amlogic_pmu_gpio_name_map_num(str);
						}
					}
					ret = of_property_read_string_index(pdev->dev.of_node, propname, 2, &str);
					if (ret) {
						printk("faild to get %s\n", propname);
					}
					else {
						if ((strcasecmp(str, "output_low") == 0) || (strcasecmp(str, "0") == 0)) {
							pConf->lcd_power_ctrl.power_on_config[i].value = LCD_POWER_GPIO_OUTPUT_LOW;
						}
						else if ((strcasecmp(str, "output_high") == 0) || (strcasecmp(str, "1") == 0)) {
							pConf->lcd_power_ctrl.power_on_config[i].value = LCD_POWER_GPIO_OUTPUT_HIGH;
						}
						else if ((strcasecmp(str, "input") == 0) || (strcasecmp(str, "2") == 0)) {
							pConf->lcd_power_ctrl.power_on_config[i].value = LCD_POWER_GPIO_INPUT;
						}
					}
				}
			}
		}
		pConf->lcd_power_ctrl.power_on_step = i;
		lcd_print("lcd_power_on_step = %d\n", pConf->lcd_power_ctrl.power_on_step);
		
		ret = of_property_read_u32_array(pdev->dev.of_node,"power_on_delay",&lcd_para[0],pConf->lcd_power_ctrl.power_on_step);
		if (ret) {
			printk("faild to get power_on_delay\n");
		}
		else {
			for (i=0; i<pConf->lcd_power_ctrl.power_on_step; i++) {
				pConf->lcd_power_ctrl.power_on_config[i].delay = (unsigned short)(lcd_para[i]);
			}
		}
		//lcd power off
		for (i=0; i < LCD_POWER_CTRL_STEP_MAX; i++) {
			sprintf(propname, "power_off_step_%d", i+1);
			//propname = kasprintf(GFP_KERNEL, "power_off_step_%d", i+1);
			ret = of_property_read_string_index(pdev->dev.of_node, propname, 0, &str);
			if (ret) {
				lcd_print("faild to get %s index 0\n", propname);
				break;
			}
			else if ((strcasecmp(str, "null") == 0) || ((strcasecmp(str, "n") == 0))) {
				break;
			}
			else {
				lcd_print("%s 0: %s\n", propname, str);
				for(index = 0; index < LCD_POWER_TYPE_MAX; index++) {
					if(!strcasecmp(str, lcd_power_type_table[index]))
						break;
				}
				pConf->lcd_power_ctrl.power_off_config[i].type = index;
			
				if (pConf->lcd_power_ctrl.power_off_config[i].type < LCD_POWER_TYPE_SIGNAL) {
					ret = of_property_read_string_index(pdev->dev.of_node, propname, 1, &str);
					if (ret) {
						printk("faild to get %s index 1\n", propname);
					}
					else {
						if (pConf->lcd_power_ctrl.power_off_config[i].type == LCD_POWER_TYPE_CPU) {
							val = amlogic_gpio_name_map_num(str);
							pConf->lcd_power_ctrl.power_off_config[i].gpio = val;
						}
						else if (pConf->lcd_power_ctrl.power_off_config[i].type == LCD_POWER_TYPE_PMU) {
							pConf->lcd_power_ctrl.power_off_config[i].gpio = amlogic_pmu_gpio_name_map_num(str);
						}
					}
					ret = of_property_read_string_index(pdev->dev.of_node, propname, 2, &str);
					if (ret) {
						printk("faild to get %s index 2\n", propname);
					}
					else {
						if ((strcasecmp(str, "output_low") == 0) || (strcasecmp(str, "0") == 0)) {
							pConf->lcd_power_ctrl.power_off_config[i].value = LCD_POWER_GPIO_OUTPUT_LOW;
						}
						else if ((strcasecmp(str, "output_high") == 0) || (strcasecmp(str, "1") == 0)) {
							pConf->lcd_power_ctrl.power_off_config[i].value = LCD_POWER_GPIO_OUTPUT_HIGH;
						}
						else if ((strcasecmp(str, "input") == 0) || (strcasecmp(str, "2") == 0)) {
							pConf->lcd_power_ctrl.power_off_config[i].value = LCD_POWER_GPIO_INPUT;
						}
					}
				}
			}
		}
		pConf->lcd_power_ctrl.power_off_step = i;
		lcd_print("lcd_power_off_step = %d\n", pConf->lcd_power_ctrl.power_off_step);
		
		ret = of_property_read_u32_array(pdev->dev.of_node,"power_off_delay",&lcd_para[0],pConf->lcd_power_ctrl.power_off_step);
		if (ret) {
			printk("faild to get power_off_delay\n");
		}
		else {
			for (i=0; i<pConf->lcd_power_ctrl.power_off_step; i++) {
				pConf->lcd_power_ctrl.power_off_config[i].delay = (unsigned short)(lcd_para[i]);
			}
		}
		
		for (i=0; i<pConf->lcd_power_ctrl.power_on_step; i++) {
			lcd_print("power on step %d: type = %s(%d)\n", i+1, lcd_power_type_table[pConf->lcd_power_ctrl.power_on_config[i].type], pConf->lcd_power_ctrl.power_on_config[i].type);
			lcd_print("power on step %d: gpio = %d\n", i+1, pConf->lcd_power_ctrl.power_on_config[i].gpio);
			lcd_print("power on step %d: value = %d\n", i+1, pConf->lcd_power_ctrl.power_on_config[i].value);
			lcd_print("power on step %d: delay = %d\n", i+1, pConf->lcd_power_ctrl.power_on_config[i].delay);
		}
		
		for (i=0; i<pConf->lcd_power_ctrl.power_off_step; i++) {
			lcd_print("power off step %d: type = %s(%d)\n", i+1, lcd_power_type_table[pConf->lcd_power_ctrl.power_off_config[i].type], pConf->lcd_power_ctrl.power_off_config[i].type);
			lcd_print("power off step %d: gpio = %d\n", i+1, pConf->lcd_power_ctrl.power_off_config[i].gpio);
			lcd_print("power off step %d: value = %d\n", i+1, pConf->lcd_power_ctrl.power_off_config[i].value);
			lcd_print("power off step %d: delay = %d\n", i+1, pConf->lcd_power_ctrl.power_off_config[i].delay);
		}

		pConf->lcd_misc_ctrl.pin = devm_pinctrl_get(&pdev->dev);
		if (IS_ERR(pConf->lcd_misc_ctrl.pin))
			printk("get lcd ttl ports pinmux error.\n");
	}
	return ret;
}
#endif

#ifdef CONFIG_USE_OF
static const struct of_device_id lcd_dt_match[] = {
	{
		.compatible = "amlogic,lcd",
	},
	{},
};
#else
#define lcd_dt_match NULL
#endif

static void lcd_config_assign(Lcd_Config_t *pConf)
{
    pConf->lcd_power_ctrl.power_ctrl = lcd_power_ctrl;
}

static struct notifier_block lcd_reboot_nb;
static int lcd_probe(struct platform_device *pdev)
{
#ifndef CONFIG_USE_OF
	struct aml_lcd_platform *pdata;
#endif
	int ret = 0;
	
	pDev = (lcd_dev_t *)kmalloc(sizeof(lcd_dev_t), GFP_KERNEL);
	if (!pDev) {
		printk("[lcd probe]: Not enough memory.\n");
		return -ENOMEM;
	}
	
#ifdef CONFIG_USE_OF
	//pdata = lcd_get_driver_data(pdev);
	pDev->pConf = get_lcd_config();
	_get_lcd_model_timing(pDev->pConf, pdev);
	_get_lcd_default_config(pDev->pConf, pdev);
	_get_lcd_power_config(pDev->pConf, pdev);
#else
	pdata = pdev->dev.platform_data;
	pDev->pConf = (Lcd_Config_t *)(pdata->lcd_conf);
#endif
	
	creat_lcd_class();
	lcd_config_assign(pDev->pConf);
	lcd_config_probe(pDev->pConf);
	save_lcd_config(pDev->pConf);
	
	pDev->pConf->lcd_misc_ctrl.print_version();
	lcd_config_init(pDev->pConf);
	_init_vout();
	
	lcd_reboot_nb.notifier_call = lcd_reboot_notifier;
	ret = register_reboot_notifier(&lcd_reboot_nb);
	if (ret) {
		printk("notifier register lcd_reboot_notifier fail!\n");
	}
	
	ret = creat_lcd_attr();
#ifdef CONFIG_AML_GAMMA_DEBUG
	save_original_gamma(pDev->pConf);
	ret = creat_lcd_gamma_attr();
#endif
	
	printk("LCD probe ok\n");
	return 0;
}

static int lcd_remove(struct platform_device *pdev)
{
	unregister_reboot_notifier(&lcd_reboot_nb);
	
	lcd_config_remove(pDev->pConf);
	remove_lcd_attr();
	remove_lcd_class();
#ifdef CONFIG_AML_GAMMA_DEBUG
	remove_lcd_gamma_attr();
#endif
	
	if (pDev->pConf->lcd_basic.model_name)
		kfree(pDev->pConf->lcd_basic.model_name);
	if (pDev)
		kfree(pDev);

    return 0;
}

//device tree
static struct platform_driver lcd_driver = {
	.probe = lcd_probe,
	.remove = lcd_remove,
	.driver = {
		.name = "mesonlcd",
		.owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
		.of_match_table = lcd_dt_match,
#endif
	},
};

static int __init lcd_init(void)
{
    lcd_print("LCD driver init\n");
    if (platform_driver_register(&lcd_driver)) {
        printk("failed to register lcd driver module\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit lcd_exit(void)
{
    platform_driver_unregister(&lcd_driver);
}

subsys_initcall(lcd_init);
module_exit(lcd_exit);

MODULE_DESCRIPTION("Meson LCD Panel Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Amlogic, Inc.");
