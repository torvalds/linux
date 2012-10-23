#ifndef __MACH_CONFIG_H
#define __MACH_CONFIG_H
#include <mach/board.h>

#define RK2926_TB_DEFAULT_CONFIG
//#define RK2928_TB_DEFAULT_CONFIG
//#define RK2926_SDK_DEFAULT_CONFIG
//#define RK2928_SDK_DEFAULT_CONFIG

enum {
        WIFI_NONE = 0,
        WIFI_USB_NONE = 1<<4,
        //here: add usb wifi type
        WIFI_USB_MAX,
        WIFI_SDIO_NONE = 1<<8,
        //here: add sdio wifi type
        WIFI_SDIO_MAX,
};
/* android paramter */
enum {
        DEF_AP_MDM = -1,
        DEF_AP_HAS_ALSA = -1,
        DEF_AP_MULTI_CARD = -1,
        DEF_AP_DATA_ONLY = -1,
};

/****************************  rk2926 top board ******************************/
#if defined(RK2926_TB_DEFAULT_CONFIG)
/* keyboard */
enum{
        DEF_KEY_ADC = 1,
        DEF_PLAY_KEY = 0x000101a4,
        DEF_VOLDN_KEY = 0x000102b4,
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
        DEF_BL_EN = 0x000002c1,
};
/* lcd */
enum {
        DEF_LCD_CABC = 0x000002c3,
        DEF_LCD_EN = -1,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_LVDS, OUT_D888_P666, \
                        65000000, 30000000, \
                        10, 100, 1024, 210, \
                        10, 10, 768, 18, \
                        202, 102, \
                        1, 0 }
/* gsensor */
enum { 
        GS_TYPE_NONE = 0,
        GS_TYPE_MMA8452,
        GS_TYPE_MMA7660,
        GS_TYPE_MAX,
};

enum {
        DEF_GS_TYPE = GS_TYPE_MMA8452,
        DEF_GS_I2C = 0,
        DEF_GS_ADDR = 0x1d,
        DEF_GS_IRQ = 0x008001b2,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, 1, 0,-1, 0}
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
/* lcd */
enum {
        DEF_LCD_CABC = 0x000002d1,
        DEF_LCD_EN = -1,
        DEF_LCD_STD = -1,
};

#define DEF_LCD_PARAM {SCREEN_LVDS, OUT_D888_P666, \
                        65000000, 30000000, \
                        10, 100, 1024, 210, \
                        10, 10, 768, 18, \
                        202, 102, \
                        1, 0 }
/* gsensor */
enum { 
        GS_TYPE_NONE = 0,
        GS_TYPE_MMA8452,
        GS_TYPE_MMA7660,
        GS_TYPE_MAX,
};

enum {
        DEF_GS_TYPE = GS_TYPE_MMA8452,
        DEF_GS_I2C = 0,
        DEF_GS_ADDR = 0x1d,
        DEF_GS_IRQ = 0x008003d1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, 1, 0,-1, 0}
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
        DEF_PWR_ON = 0x000003c5,
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
        DEF_BL_EN = 0x000001b0,
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
                        1, 0 }
/* gsensor */
enum { 
        GS_TYPE_NONE = 0,
        GS_TYPE_MMA8452,
        GS_TYPE_MMA7660,
        GS_TYPE_MAX,
};

enum {
        DEF_GS_TYPE = GS_TYPE_MMA7660,
        DEF_GS_I2C = 1,
        DEF_GS_ADDR = 0x4c,
        DEF_GS_IRQ = 0x008001b2,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {-1, 0, 0, 0, 0, -1, 0, 1, 0}
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
        DEF_CHG_SEL = -1,
};

/* global */
enum {
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
        GS_TYPE_NONE = 0,
        GS_TYPE_MMA8452,
        GS_TYPE_MMA7660,
        GS_TYPE_MAX,
};

enum {
        DEF_GS_TYPE = GS_TYPE_NONE,
        DEF_GS_I2C = -1,
        DEF_GS_ADDR = -1,
        DEF_GS_IRQ = -1,
        DEF_GS_PWR = -1,
};
#define DEF_GS_ORIG {0, 0, 0, 0, 0, 0, 0, 0, 0}
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
        DEF_PWR_ON = -1,
};

#endif

#endif
