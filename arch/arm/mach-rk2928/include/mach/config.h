#ifndef __MACH_CONFIG_H
#define __MACH_CONFIG_H
#include <mach/board.h>

#define RK2926_TB_DEFAULT_CONFIG
//#define RK2928_TB_DEFAULT_CONFIG
//#define RK2926_V86_DEFAULT_CONFIG
//#define RK2926_SDK_DEFAULT_CONFIG
//#define RK2928_SDK_DEFAULT_CONFIG
//#define RK2928_PHONEPAD_DEFAULT_CONFIG


/* camera id */
#define BACK_SENSOR_0           NONE
#define BACK_SENSOR_0_ADDR      0x00
#define BACK_SENSOR_1           NONE
#define BACK_SENSOR_1_ADDR      0x00
#define BACK_SENSOR_2           NONE
#define BACK_SENSOR_2_ADDR      0x00

#define FRONT_SENSOR_0          RK29_CAM_SENSOR_GC0308
#define FRONT_SENSOR_0_ADDR     0x42
#define FRONT_SENSOR_1          RK29_CAM_SENSOR_OV2659
#define FRONT_SENSOR_1_ADDR     0x60
#define FRONT_SENSOR_2          RK29_CAM_SENSOR_HI704
#define FRONT_SENSOR_2_ADDR     0x60

enum {
        CAM_ID_BACK_SENSOR_0 = 0,
        CAM_ID_BACK_SENSOR_1,
        CAM_ID_BACK_SENSOR_2,
        CAM_ID_FRONT_SENSOR_0,
        CAM_ID_FRONT_SENSOR_1,
        CAM_ID_FRONT_SENSOR_2,
};

enum { 
        GS_TYPE_NONE = 0,
        GS_TYPE_MMA8452,
        GS_TYPE_MMA7660,
        GS_TYPE_KXTIK,
        GS_TYPE_MAX,
};
enum {
        LS_TYPE_NONE = 0,
        LS_TYPE_AP321XX,
        LS_TYPE_MAX,
};
enum {
        PS_TYPE_NONE = 0,
        PS_TYPE_AP321XX,
        PS_TYPE_MAX,
};
enum {
        WIFI_NONE = 0,
        WIFI_USB_NONE = 1<<4,
        //here: add usb wifi type
        WIFI_USB_MAX,
        WIFI_SDIO_NONE = 1<<8,
        //here: add sdio wifi type
        WIFI_SDIO_MAX,
};

int pmic_dcdc_set(int index, int on);
int pmic_ldo_set(int index, int on);

/****************************  rk2926 top board ******************************/
#if defined(RK2926_TB_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000101a4,
        DEF_VOLDN_KEY = 0x000102b6,
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 100 | (1<<31),
        DEF_ESC_KEY = 255  | (1<<31),
        DEF_HOME_KEY = 425 | (1<<31),
        DEF_CAM_KEY = 576  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 1,
        DEF_BL_MIN = 60,
        DEF_BL_EN = 0x000002c1,
};
/* usb */
enum {
        DEF_OTG_DRV = 0x000003c1,
        DEF_HOST_DRV = 0x000002b4,
};
/* lcd */
enum {
        DEF_LCD_CABC = 0x000002c3,
        DEF_LCD_EN = -1,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_LVDS, OUT_D888_P666, \
                        65000000, 300000000, \
                        10, 100, 1024, 210, \
                        10, 10, 768, 18, \
                        202, 102, \
                        1, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_MMA8452,
        DEF_GS_I2C = 0,
        DEF_GS_ADDR = 0x1d,
        DEF_GS_IRQ = 0x008001b2,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, 1, 0,-1, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};
/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_1,
        DEF_FRONT_CAM_I2C = 0,
        DEF_FRONT_CAM_PWR = 0x000003b3,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};
/* pwm regulator */
enum {
        DEF_REG_PWM = 1,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_TPS65910,
        DEF_PMIC_SLP = 0x000001a1,
        DEF_PMIC_IRQ = 0x000001b1,
        DEF_PMIC_I2C = 1,
        DEF_PMIC_ADDR = 0x2d, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1800000,1800000,0,0,0,0,0,0,0,0,0,0,1800000,1800000,2500000,2500000}
#define DEF_ACT8931_DCDC {0,0,0,0,0,0}
#define DEF_ACT8931_LDO {0,0,0,0,0,0,0,0}

/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000001a0,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000102a7,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = 5,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = -1,
};
/* charge */
enum {
        DEF_CHG_ADC = -1,
        DEF_DC_DET = -1,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = -1,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = -1,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 0,
        DEF_PWR_ON = 0x000001a2,
};
/****************************  rk2928 top board ******************************/
#elif defined(RK2928_TB_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000103c5,
        DEF_VOLDN_KEY = 0x000100d1,
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 135 | (1<<31),
        DEF_ESC_KEY = 334  | (1<<31),
        DEF_HOME_KEY = 550 | (1<<31),
        DEF_CAM_KEY = 700  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 1,
        DEF_BL_MIN = 60,
        DEF_BL_EN = 0x000003c4,
};
/* usb */
enum {
        DEF_OTG_DRV = 0x000003c1,
        DEF_HOST_DRV = 0x000001b2,
};
/* lcd */
enum {
        DEF_LCD_CABC = 0x000002d1,
        DEF_LCD_EN = -1,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_LVDS, OUT_D888_P666, \
                        65000000, 300000000, \
                        10, 100, 1024, 210, \
                        10, 10, 768, 18, \
                        202, 102, \
                        1, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_MMA8452,
        DEF_GS_I2C = 0,
        DEF_GS_ADDR = 0x1d,
        DEF_GS_IRQ = 0x008003d1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, 1, 0,-1, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_1,
        DEF_FRONT_CAM_I2C = 0,
        DEF_FRONT_CAM_PWR = 0x000003b3,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = 1,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_TPS65910,
        DEF_PMIC_SLP = 0x000003d2,
        DEF_PMIC_IRQ = 0x000003c6,
        DEF_PMIC_I2C = 1,
        DEF_PMIC_ADDR = 0x2d, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1800000,1800000,0,0,0,0,0,0,0,0,0,0,1800000,1800000,2500000,2500000}
#define DEF_ACT8931_DCDC {0,0,0,0,0,0}
#define DEF_ACT8931_LDO {0,0,0,0,0,0,0,0}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000003d4,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000101c1,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = 5,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = -1,
};
/* charge */
enum {
        DEF_CHG_ADC = -1,
        DEF_DC_DET = -1,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = -1,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = -1,
};

/* global */
enum {
        DEF_PWR_ON = 0x000001b4,
};

/****************************  rk2926 sdk(m713) ******************************/
#elif defined(RK2926_SDK_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000101a4,
        DEF_VOLDN_KEY = 512 | (1<<31),
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 0 | (1<<31),
        DEF_ESC_KEY = 0  | (1<<31),
        DEF_HOME_KEY = 0 | (1<<31),
        DEF_CAM_KEY = 0  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 0,
        DEF_BL_MIN = 80,
        DEF_BL_EN = -1,
};

/* usb */
enum {
        DEF_OTG_DRV = -1,
        DEF_HOST_DRV = -1,
};
/* lcd */
enum {
        DEF_LCD_CABC = -1,
        DEF_LCD_EN = 0x000101b3,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P666, \
                        33000000, 150000000, \
                        30, 10, 800, 210, \
                        13, 10, 480, 22, \
                        154, 85, \
                        1, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_MMA7660,
        DEF_GS_I2C = 1,
        DEF_GS_ADDR = 0x4c,
        DEF_GS_IRQ = 0x008001b2,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, -1, 0, 1, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_0,
        DEF_FRONT_CAM_I2C = 1,
        DEF_FRONT_CAM_PWR = 0x000003b3,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = 1,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_ACT8931,
        DEF_PMIC_SLP = 0x000001a1,
        DEF_PMIC_IRQ = 0x000001b1,
        DEF_PMIC_I2C = 0,
        DEF_PMIC_ADDR = 0x5b, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1500000,1500000,1200000,1200000,2800000,2800000,3300000,3300000,3300000,3300000,3300000,3300000,0,0,0,0}
#define DEF_ACT8931_DCDC {3300000,3300000,1500000,1500000,1200000,1200000}
#define DEF_ACT8931_LDO {2800000,2800000,1800000,1800000,3000000,3000000,3300000,3300000}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000001a0,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000102a7,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = 3,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = 0x008001a5,
};
/* charge */
enum {
        DEF_CHG_ADC = 0,
        DEF_DC_DET = -1,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = -1,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = 0x000001a1,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 0,
        DEF_PWR_ON = 0x000001a2,
};
/****************************  rk2926 sdk(v86) ******************************/
#elif defined(RK2926_V86_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000101a4,
        DEF_VOLDN_KEY = 512 | (1<<31),
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 0 | (1<<31),
        DEF_ESC_KEY = 0  | (1<<31),
        DEF_HOME_KEY = 0 | (1<<31),
        DEF_CAM_KEY = 0  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 0,
        DEF_BL_MIN = 80,
        DEF_BL_EN = 0x000003c1,
};

/* usb */
enum {
        DEF_OTG_DRV = -1,
        DEF_HOST_DRV = -1,
};
/* lcd */
enum {
        DEF_LCD_CABC = -1,
        DEF_LCD_EN = 0x000101b3,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P666, \
                        33000000, 15000000, \
                        30, 10, 800, 210, \
                        13, 10, 480, 22, \
                        154, 85, \
                        0, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_MMA7660,
        DEF_GS_I2C = 1,
        DEF_GS_ADDR = 0x4c,
        DEF_GS_IRQ = 0x008001b2,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {0, 1, 0, 0, 0, -1, 1, 0, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_0,
        DEF_FRONT_CAM_I2C = 1,
        DEF_FRONT_CAM_PWR = 0x000003b3,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = 1,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_TPS65910,
        DEF_PMIC_SLP = 0x000001a1,
        DEF_PMIC_IRQ = 0x000001b1,
        DEF_PMIC_I2C = 0,
        DEF_PMIC_ADDR = 0x2d, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1500000,1500000,1200000,1200000,2800000,2800000,3300000,3300000,3300000,3300000,3300000,3300000,0,0,0,0}
#define DEF_ACT8931_DCDC {3300000,3300000,1500000,1500000,1200000,1200000}
#define DEF_ACT8931_LDO {2800000,2800000,1800000,1800000,3000000,3000000,3300000,3300000}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000001a0,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000102a7,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = 5,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = -1,
};
/* charge */
enum {
        DEF_CHG_ADC = 0,
        DEF_DC_DET = 0x001101a5,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = -1,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = -1,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 0,
        DEF_PWR_ON = 0x000001a2,
};
/****************************  rk2928 sdk ******************************/
#elif defined(RK2928_SDK_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000100d1,
        DEF_VOLDN_KEY = 512 | (1<<31),
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 0 | (1<<31),
        DEF_ESC_KEY = 0  | (1<<31),
        DEF_HOME_KEY = 0 | (1<<31),
        DEF_CAM_KEY = 0  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 0,
        DEF_BL_MIN = 80,
        DEF_BL_EN = 0x000003c5,
};
/* usb */
enum {
        DEF_OTG_DRV = -1,
        DEF_HOST_DRV = -1,
};
/* lcd */
enum {
        DEF_LCD_CABC = -1,
        DEF_LCD_EN = 0x000103b3,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P666, \
                        33000000, 150000000, \
                        30, 10, 800, 210, \
                        13, 10, 480, 22, \
                        154, 85, \
                        1, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_MMA7660,
        DEF_GS_I2C = 1,
        DEF_GS_ADDR = 0x4c,
        DEF_GS_IRQ = 0x008003d1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, 1, 0, -1, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_0,
        DEF_FRONT_CAM_I2C = 1,
        DEF_FRONT_CAM_PWR = 0x000003d7,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = 2,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_TPS65910,
        DEF_PMIC_SLP = 0x000000d0,
        DEF_PMIC_IRQ = 0x000003c6,
        DEF_PMIC_I2C = 0,
        DEF_PMIC_ADDR = 0x5b, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1500000,1500000,1200000,1200000,2800000,2800000,3300000,3300000,3300000,3300000,3300000,3300000,0,0,0,0}
#define DEF_ACT8931_DCDC {3300000,3300000,1500000,1500000,1200000,1200000}
#define DEF_ACT8931_LDO {2800000,2800000,1800000,1800000,3000000,3000000,3300000,3300000}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000003d4,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000101c1,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = 3,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = 0x008001a5,
};
/* charge */
enum {
        DEF_CHG_ADC = 0,
        DEF_DC_DET = 0x000101b4,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = 0x000001a0,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = 0x000000d0,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 0,
        DEF_PWR_ON = 0x000001a1,
};
/****************************  rk2928 phonepad ******************************/
#elif defined(RK2928_PHONEPAD_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000101a4,
        DEF_VOLDN_KEY = 512 | (1<<31),
        DEF_VOLUP_KEY = 1  | (1<<31),
        DEF_MENU_KEY = 0 | (1<<31),
        DEF_ESC_KEY = 0  | (1<<31),
        DEF_HOME_KEY = 0 | (1<<31),
        DEF_CAM_KEY = 0  | (1<<31),
};
/* backlight */
enum{
        DEF_BL_PWM = 0,
        DEF_BL_REF = 1,
        DEF_BL_MIN = 80,
        DEF_BL_EN = 0x000001b0,
};
/* usb */
enum {
        DEF_OTG_DRV = 0x000003c1,
        DEF_HOST_DRV = -1,
};
/* lcd */
enum {
        DEF_LCD_CABC = -1,
        DEF_LCD_EN = 0x000100c3,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P666, \
                        30000000, 150000000, \
                        48, 88, 800, 40, \
                        3, 32, 480, 13, \
                        154, 85, \
                        1, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_KXTIK,
        DEF_GS_I2C = 1,
        DEF_GS_ADDR = 0x0f,
        DEF_GS_IRQ = 0x008003d1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, -1, 0, 1, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_AP321XX,
        DEF_LS_I2C = 1,
        DEF_LS_ADDR = 0x1e,
        DEF_LS_IRQ = 0x008000c6,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_AP321XX,
        DEF_PS_I2C = 1,
        DEF_PS_ADDR = 0x1e,
        DEF_PS_IRQ = 0x008000c6,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = CAM_ID_FRONT_SENSOR_2,
        DEF_FRONT_CAM_I2C = 1,
        DEF_FRONT_CAM_PWR = 0x000003b3,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = 2,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_TPS65910,
        DEF_PMIC_SLP = 0x000001a1,
        DEF_PMIC_IRQ = 0x000003c6,
        DEF_PMIC_I2C = 0,
        DEF_PMIC_ADDR = 0x2d, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {1200000,1200000,1200000,1200000,0,0,3300000,3300000}
#define DEF_TPS65910_LDO {1500000,1500000,1200000,1200000,2800000,2800000,3300000,3300000,3300000,3300000,3300000,3300000,0,0,0,0}
#define DEF_ACT8931_DCDC {0,0,0,0,0,0}
#define DEF_ACT8931_LDO {0,0,0,0,0,0,0,0}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = 0x000003d4,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = 0x000101c1,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = -1,
};
/* rtc */
enum {
        DEF_RTC_I2C = 0,
        DEF_RTC_ADDR = 0x51,
        DEF_RTC_IRQ = 0x008001a5,
};
/* charge */
enum {
        DEF_CHG_ADC = 0,
        DEF_DC_DET = 0x001101a5,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = 0x002001a0,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = -1,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 1,
        DEF_PWR_ON = 0x000001a2,
};


/****************************  other ******************************/
#else
/* keyboard */
enum{
        DEF_KEY_ADC = -1,
        DEF_PLAY_KEY = -1,
        DEF_VOLDN_KEY = -1,
        DEF_VOLUP_KEY = 0,
        DEF_MENU_KEY = 0,
        DEF_ESC_KEY = 0,
        DEF_HOME_KEY = 0,
        DEF_CAM_KEY = 0,
};
/* backlight */
enum{
        DEF_BL_PWM = -1,
        DEF_BL_REF = 0,
        DEF_BL_MIN = 0,
        DEF_BL_EN = -1,
};
/* usb */
enum {
        DEF_OTG_DRV = -1,
        DEF_HOST_DRV = -1,
};
/* lcd */
enum {
        DEF_LCD_CABC = -1,
        DEF_LCD_EN = -1,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {0, 0, \
                        0, 0, \
                        0, 0, 0, 0, \
                        0, 0, 0, 0, \
                        0, 0, \
                        0, 0 }
/* gsensor */
enum {
        DEF_GS_TYPE = GS_TYPE_NONE,
        DEF_GS_I2C = -1,
        DEF_GS_ADDR = -1,
        DEF_GS_IRQ = -1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {0, 0, 0, 0, 0, 0, 0, 0, 0}
/* lsensor */
enum {
        DEF_LS_TYPE = LS_TYPE_NONE,
        DEF_LS_I2C = -1,
        DEF_LS_ADDR = -1,
        DEF_LS_IRQ = -1,
        DEF_LS_PWR = -1,
};
/* psensor */
enum {
        DEF_PS_TYPE = PS_TYPE_NONE,
        DEF_PS_I2C = -1,
        DEF_PS_ADDR = -1,
        DEF_PS_IRQ = -1,
        DEF_PS_PWR = -1,
};

/* camera */
enum {
        DEF_FRONT_CAM_ID = -1,
        DEF_FRONT_CAM_I2C = -1,
        DEF_FRONT_CAM_PWR = -1,
};
enum {
        DEF_BACK_CAM_ID = -1,
        DEF_BACK_CAM_I2C = -1,
        DEF_BACK_CAM_PWR = -1,
};

/* pwm regulator */
enum {
        DEF_REG_PWM = -1,
};
/* pmic */
enum {
        DEF_PMIC_TYPE = PMIC_TYPE_NONE,
        DEF_PMIC_SLP = -1,
        DEF_PMIC_IRQ = -1,
        DEF_PMIC_I2C = -1,
        DEF_PMIC_ADDR = -1, 
};
/* tps656910_dcdc: vdd_cpu, vdd2, vdd3, vio 
 * tps65910_ldo: vdig1, vdig2, vaux1, vaux2, vaux33, vmmc, vdac, vpll 
 * act8931_dcdc: act_dcdc1, act_dcdc2, vdd_cpu
 * act8931_ldo: act_ldo1, act_ldo2, act_ldo3, act_ldo4
 */
#define DEF_TPS65910_DCDC {0,0,0,0,0,0,0,0}
#define DEF_TPS65910_LDO {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
#define DEF_ACT8931_DCDC {0,0,0,0,0,0}
#define DEF_ACT8931_LDO {0,0,0,0,0,0,0,0}
/* ion */
enum {
        DEF_ION_SIZE = 80 * 1024 * 1024,
};
/* codec */
enum {
        DEF_SPK_CTL = -1,
        DEF_HP_DET = -1,
};
/* sdmmc */
enum {
        DEF_SD_DET = -1,
};
/* wifi */
enum {
        DEF_WIFI_RST = -1,
        DEF_WIFI_PWR = -1,
        DEF_WIFI_TYPE = WIFI_NONE, 
        DEF_WIFI_LDO = -1,
};
/* rtc */
enum {
        DEF_RTC_I2C = -1,
        DEF_RTC_ADDR = -1,
        DEF_RTC_IRQ = -1,
};
/* charge */
enum {
        DEF_CHG_ADC = -1,
        DEF_DC_DET = -1,
        DEF_BAT_LOW = -1,
        DEF_CHG_OK = -1,
        DEF_CHG_SET = -1,
        DEF_CHG_SEL = -1,
};

/* global */
enum {
        DEF_IS_PHONEPAD = 0,
        DEF_PWR_ON = -1,
};
#endif

int inline otg_drv_init(int on);
void inline otg_drv_on(void);
void inline otg_drv_off(void);

int inline host_drv_init(int on);
void inline host_drv_on(void);
void inline host_drv_off(void);

#endif
