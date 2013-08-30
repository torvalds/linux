#ifndef __MACH_CONFIG_H
#define __MACH_CONFIG_H
#include <mach/board.h>
//#include <mach/iomux-rk30.h>

//#include <mach/iomux.h>
#define BOARD_DEFAULT
//#define BOARD_RK3168_86V
//#define BOARD_RK3168_86V_RK616
//#define BOARD_RK3028_86V

#define INVALID_VALUE  -1
#define INVALID_GPIO -1



enum {         
	CODEC_TYPE_NONE = 0,        
	CODEC_TYPE_RT5631,
	CODEC_TYPE_ES8323,
	CODEC_TYPE_RK616,
	CODEC_TYPE_MAX,
};

enum {         
	TP_TYPE_NONE = 0,        
	TP_TYPE_GSLX680, 
	TP_TYPE_GT811_86V,
	TP_TYPE_GT8XX,
	TP_TYPE_MAX,
};

enum {         
	GS_TYPE_NONE = 0,              
	GS_TYPE_MMA7660,         
	GS_TYPE_LIS3DH,
	GS_TYPE_MXC6225,
	GS_TYPE_DMARAD10,
	GS_TYPE_MMA8452,
	GS_TYPE_LSM303D,
	GS_TYPE_MAX,
};

enum {         
	WIFI_TYPE_NONE = 0,              
	WIFI_TYPE_RTL8188CU,         
	WIFI_TYPE_RTL8188EU,
	WIFI_TYPE_MT7601,
	WIFI_TYPE_RTL8188ETV,
	WIFI_TYPE_MT5370,
	WIFI_TYPE_MAX,
};


enum {
        OUT_TYPE_INDEX = 0,
        OUT_FACE_INDEX,
        LVDS_FORMAT_INDEX,
        OUT_CLK_INDEX,
        LCDC_ACLK_INDEX,
        H_PW_INDEX,
        H_BP_INDEX,
        H_VD_INDEX,
        H_FP_INDEX,
        V_PW_INDEX,
        V_BP_INDEX,
        V_VD_INDEX,
        V_FP_INDEX,
        LCD_WIDTH_INDEX,
        LCD_HEIGHT_INDEX,
        DCLK_POL_INDEX,
        SWAP_RB_INDEX,
        LCD_PARAM_MAX,
};

#ifdef BOARD_DEFAULT
////////////////////////////////////////////////////////////
/*system*/
enum {               
	DEF_PWR_ON = -1,
};
////////////////////////////////////////////////////////////
/*lcd*/
enum {
	DEF_LCD_EN = -1,
	DEF_LCD_CS = -1,
	DEF_LCD_STD=-1,
};


#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P888, 0,\
						50000000, 500000000,\
						30, 10, 1024, 210,\
						13, 10, 600, 22,\
						154, 85,\
						1, 0 }
//////////////////////////////////////////////////////////////
/* backlight */

enum{
	DEF_BL_EN = -1,
	DEF_BL_PWMID =-1,
	DEF_BL_PWM_MOD=-1,
	DEF_BL_MOD = -1,
	DEF_BL_DIV = -1, 
	DEF_BL_REF = -1,
	DEF_BL_MIN = -1, 
	DEF_BL_MAX = -1,	
};
//////////////////////////////////////////////////////////////
/*tp*/
enum {
	DEF_TP_TYPE= -1,
	DEF_TP_IRQ = -1,
	DEF_TP_RST = -1,
	DEF_TP_I2C = -1, 
	DEF_TP_ADDR = -1,
	DEF_X_MAX = -1,
	DEF_Y_MAX = -1,
	DEF_FIRMVER = -1,
};
//////////////////////////////////////////////////////////////
/*key*/
enum{        
	DEF_KEY_ADC = -1,        
	DEF_PLAY_KEY = -1,        
	DEF_VOLDN_KEY = -1,        
	DEF_VOLUP_KEY = -1,        
	DEF_MENU_KEY = -1,        
	DEF_ESC_KEY = -1,        
	DEF_HOME_KEY = -1,        
	//DEF_CAM_KEY = -1
	};
//////////////////////////////////////////////////////////////
/* gsensor */
enum { 
	DEF_GS_TYPE = -1,
	DEF_GS_IRQ = -1,        	        
	DEF_GS_I2C = -1,        
	DEF_GS_ADDR = -1,        
	
};
#define DEF_GS_ORIG {-1, 0, 0, 0, -1, 0, 0, 0, 1} 
////////////////////////////////////////////////////////////////
/* codec */
enum {  
	DEF_CODEC_TYPE=-1,
	DEF_CODEC_POWER=-1,
	DEF_CODEC_RST=-1,
	DEF_CODEC_HDMI_IRQ=-1,
	DEF_SPK_CTL = -1,        
	DEF_HP_DET = -1,
	DEF_CODEC_I2C= -1,
	DEF_CODEC_ADDR=-1,
};
/////////////////////////////////////////////////////////////////
/* charge */
enum {                
	DEF_DC_DET = -1,        
	DEF_BAT_LOW = -1,        
	DEF_CHG_OK = -1,        
	DEF_CHG_SET = -1,        
	DEF_USB_DET = -1,
	DEF_REF_VOL = -1,
	DEF_UP_RES = -1,
	DEF_DOWN_RES = -1,
	DEF_ROOT_CHG = -1,
	DEF_SAVE_CAP = -1,
	DEF_LOW_VOL = -1,
	DEF_NUM = 0,
};
#define DEF_BAT_CHARGE {3600, 3700, 3760, 3810, 3870, 3910, 3960, 4020, 4080, 4130, 4180}; 
#define DEF_BAT_DISCHARGE {3495, 3570, 3630, 3700, 3740, 3790, 3825, 3865, 3920, 3980, 4050} ;
//////////////////////////////////////////////////////////////////
/*wifi*/
enum {        
	DEF_WIFI_TYPE = -1,        
	DEF_WIFI_POWER = -1,           	
};
#endif 

#ifdef BOARD_RK3168_86V
////////////////////////////////////////////////////////////
/*system*/
enum {               
	DEF_PWR_ON = 0x000000a0,
};
////////////////////////////////////////////////////////////
/*lcd*/
enum {
	DEF_LCD_EN = 0x000100b0,
	DEF_LCD_CS = 0x000003d4,
	DEF_LCD_STD=0x000102c6,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P888, 0,\
						50000000, 500000000, \
						30, 10, 1024, 210, \
						13, 10, 600, 22, \
						154, 85, \
						0, 0 }
						

//////////////////////////////////////////////////////////////
/* backlight */

enum{
	DEF_BL_EN = 0x000000a2,
	DEF_BL_PWMID = 3,
	DEF_BL_PWM_MOD=PWM3,
	DEF_BL_MOD = 1,
	DEF_BL_DIV = 20*1000, 
	DEF_BL_REF = 0,
	DEF_BL_MIN = 60, 
	DEF_BL_MAX = 255,	
};
//////////////////////////////////////////////////////////////
/*tp*/
enum {
	DEF_TP_TYPE= TP_TYPE_GSLX680,
	DEF_TP_IRQ = 0x000001b7,
	DEF_TP_RST = 0x000000b6,
	DEF_TP_I2C = 2, 
	DEF_TP_ADDR = 0x40,
	DEF_X_MAX = 1024,
	DEF_Y_MAX = 600,
	DEF_FIRMVER = 0,
};
//////////////////////////////////////////////////////////////
/*key*/
enum{        
	DEF_KEY_ADC = 1,        
	DEF_PLAY_KEY = 0x000100a4,        
	DEF_VOLDN_KEY = 0x80000096,        
	DEF_VOLUP_KEY = 0x80000001,        
	DEF_MENU_KEY = 0x80000000,        
	DEF_ESC_KEY = 0x80000000,        
	DEF_HOME_KEY = 0x80000000,        
	};
//////////////////////////////////////////////////////////////
/* gsensor */
enum { 
	DEF_GS_TYPE = GS_TYPE_MXC6225,
	DEF_GS_IRQ = 0x008000b7,        	        
	DEF_GS_I2C = 0,        
	DEF_GS_ADDR = 0x15,        
	
};
#define DEF_GS_ORIG {-1, 0, 0, 0, -1, 0, 0, 0, 1} 
////////////////////////////////////////////////////////////////
/* codec */
enum {  
	DEF_CODEC_TYPE=CODEC_TYPE_ES8323,
	DEF_CODEC_POWER=-1,
	DEF_CODEC_RST=-1,
	DEF_CODEC_HDMI_IRQ=-1,
	DEF_SPK_CTL = 0x000002d7,        
	DEF_HP_DET = 0x000000b5,
	DEF_CODEC_I2C= 4,
	DEF_CODEC_ADDR=0x10,
};
/////////////////////////////////////////////////////////////////
/* charge */
enum {                
	DEF_DC_DET = 0x000100b2,        
	DEF_BAT_LOW = INVALID_GPIO,        
	DEF_CHG_OK = 0x000000a6,        
	DEF_CHG_SET = INVALID_GPIO,        
	DEF_USB_DET = INVALID_GPIO,
	DEF_REF_VOL = 1800,
	DEF_UP_RES = 200,
	DEF_DOWN_RES = 120,
	DEF_ROOT_CHG = 1,
	DEF_SAVE_CAP = 1,
	DEF_LOW_VOL = 3600,
	DEF_NUM = 0,
};
#define DEF_BAT_CHARGE {3600, 3700, 3760, 3810, 3870, 3910, 3960, 4020, 4080, 4130, 4180}; 
#define DEF_BAT_DISCHARGE {3495, 3570, 3630, 3700, 3740, 3790, 3825, 3865, 3920, 3980, 4050};
//////////////////////////////////////////////////////////////////
/*wifi*/
enum {        
	DEF_WIFI_TYPE = WIFI_TYPE_RTL8188EU,        
	DEF_WIFI_POWER = 0x000100d5,           	
};
#endif 

#ifdef BOARD_RK3168_86V_RK616
////////////////////////////////////////////////////////////
/*system*/
enum {               
	DEF_PWR_ON = 0x000000a0,
};
////////////////////////////////////////////////////////////
/*lcd*/
enum {
	DEF_LCD_EN = 0x000100b0,
	DEF_LCD_CS = 0x000003d4,
	DEF_LCD_STD=0x000102c6,
};
/*
#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P888, \                        
						50000000, 500000000, \                        
						30, 10, 1024, 210, \                        
						13, 10, 600, 22, \                        
						154, 85, \                        
						1, 0 }
*/

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P888,0,\
						33000000, 150000000,\
						1,88,800,40,\
						3,29,480,13,\
						154, 85,\
						0, 0 }

//////////////////////////////////////////////////////////////
/* backlight */

enum{
	DEF_BL_EN = 0x000000a2,
	DEF_BL_PWMID = 3,
	DEF_BL_PWM_MOD=PWM3,
	DEF_BL_MOD = 1,
	DEF_BL_DIV = 20*1000, 
	DEF_BL_REF = 0,
	DEF_BL_MIN = 60, 
	DEF_BL_MAX = 255,	
};
//////////////////////////////////////////////////////////////
/*tp*/
enum {
	DEF_TP_TYPE= TP_TYPE_GSLX680,
	DEF_TP_IRQ = 0x000001b7,
	DEF_TP_RST = 0x000000b6,
	DEF_TP_I2C = 2, 
	DEF_TP_ADDR = 0x40,
	DEF_X_MAX = 1024,
	DEF_Y_MAX = 600,
	DEF_FIRMVER= 0,
};
//////////////////////////////////////////////////////////////
/*key*/
enum{        
	DEF_KEY_ADC = 1,        
	DEF_PLAY_KEY = 0x000100a4,        
	DEF_VOLDN_KEY = 0x80000096,        
	DEF_VOLUP_KEY = 0x80000001,        
	DEF_MENU_KEY = 0x80000000,        
	DEF_ESC_KEY = 0x80000000,        
	DEF_HOME_KEY = 0x80000000,        
	};
//////////////////////////////////////////////////////////////
/* gsensor */
enum { 
	DEF_GS_TYPE = GS_TYPE_MMA7660,
	DEF_GS_IRQ = 0x008000b7,        	        
	DEF_GS_I2C = 0,        
	DEF_GS_ADDR = 0x4c,        
	
};
#define DEF_GS_ORIG {1, 0, 0, 0, -1, 0, 0, 0, 1} 
////////////////////////////////////////////////////////////////
/* codec */
enum {  
	DEF_CODEC_TYPE=CODEC_TYPE_RK616,
	DEF_CODEC_POWER=0x000000a3,
	DEF_CODEC_RST=0x000003b2,
	DEF_CODEC_HDMI_IRQ=0x000002d6,
	DEF_SPK_CTL = 0x000002d7,        
	DEF_HP_DET = -1,
	DEF_CODEC_I2C= 4,
	DEF_CODEC_ADDR=0x50,
};
/////////////////////////////////////////////////////////////////
/* charge */
enum {                
	DEF_DC_DET = 0x000100b2,        
	DEF_BAT_LOW = INVALID_GPIO,        
	DEF_CHG_OK = 0x000000a6,        
	DEF_CHG_SET = INVALID_GPIO,        
	DEF_USB_DET = INVALID_GPIO,
	DEF_REF_VOL = 1800,
	DEF_UP_RES = 200,
	DEF_DOWN_RES = 120,
	DEF_ROOT_CHG = 1,
	DEF_SAVE_CAP = 1,
	DEF_LOW_VOL = 3600,
	DEF_NUM = 0,
};
#define DEF_BAT_CHARGE {3600, 3700, 3760, 3810, 3870, 3910, 3960, 4020, 4080, 4130, 4180}; 
#define DEF_BAT_DISCHARGE {3495, 3570, 3630, 3700, 3740, 3790, 3825, 3865, 3920, 3980, 4050} ;
//////////////////////////////////////////////////////////////////
/*wifi*/
enum {        
	DEF_WIFI_TYPE = WIFI_TYPE_RTL8188EU,        
	DEF_WIFI_POWER = 0x000100d5,           	
};
#endif 

#ifdef BOARD_RK3028_86V
////////////////////////////////////////////////////////////
/*system*/
enum {               
	DEF_PWR_ON = 0x000001a1,
};
////////////////////////////////////////////////////////////
/*lcd*/
enum {
	DEF_LCD_EN = 0x000103d2,
	DEF_LCD_CS = -1,
	DEF_LCD_STD=-1,
};

#define DEF_LCD_PARAM {SCREEN_RGB, OUT_P666, 0,\
						50000000, 500000000, \
						30, 10, 1024, 210, \
						13, 10, 600, 22, \
						154, 85, \
						0, 0 }
						

//////////////////////////////////////////////////////////////
/* backlight */

enum{
	DEF_BL_EN = 0x000003c5,
	DEF_BL_PWMID = 0,
	DEF_BL_PWM_MOD=PWM0,
	DEF_BL_MOD = 1,
	DEF_BL_DIV = 20*1000, 
	DEF_BL_REF = 0,
	DEF_BL_MIN = 60, 
	DEF_BL_MAX = 255,	
};
//////////////////////////////////////////////////////////////
/*tp*/
enum {
	DEF_TP_TYPE= TP_TYPE_GSLX680,
	DEF_TP_IRQ = 0x000103c7,
	DEF_TP_RST = 0x000003c3,
	DEF_TP_I2C = 2, 
	DEF_TP_ADDR = 0x40,
	DEF_X_MAX = 1024,
	DEF_Y_MAX = 600,
	DEF_FIRMVER = 0,
};
//////////////////////////////////////////////////////////////
/*key*/
enum{        
	DEF_KEY_ADC = 1,        
	DEF_PLAY_KEY = 0x000100a4,        
	DEF_VOLDN_KEY = 0x80000000,        
	DEF_VOLUP_KEY = 0x80000000,        
	DEF_MENU_KEY = 0x80000000,        
	DEF_ESC_KEY = 0x80000000,        
	DEF_HOME_KEY = 0x80000000,        
	};
//////////////////////////////////////////////////////////////
/* gsensor */
enum { 
	DEF_GS_TYPE = GS_TYPE_MXC6225,
	DEF_GS_IRQ = 0x008000b7,        	        
	DEF_GS_I2C = 0,        
	DEF_GS_ADDR = 0x15,        
	
};
#define DEF_GS_ORIG {-1, 0, 0, 0, -1, 0, 0, 0, 1} 
////////////////////////////////////////////////////////////////
/* codec */
enum {  
	DEF_CODEC_TYPE=CODEC_TYPE_ES8323,
	DEF_CODEC_POWER=-1,
	DEF_CODEC_RST=-1,
	DEF_CODEC_HDMI_IRQ=-1,
	DEF_SPK_CTL = 0x000002d7,        
	DEF_HP_DET = 0x000000b5,
	DEF_CODEC_I2C= 4,
	DEF_CODEC_ADDR=0x10,
};
/////////////////////////////////////////////////////////////////
/* charge */
enum {                
	DEF_DC_DET = 0x000101b4,        
	DEF_BAT_LOW = INVALID_GPIO,        
	DEF_CHG_OK = 0x000001a0,        
	DEF_CHG_SET = INVALID_GPIO,        
	DEF_USB_DET = INVALID_GPIO,
	DEF_REF_VOL = 3300,
	DEF_UP_RES = 200,
	DEF_DOWN_RES = 120,
	DEF_ROOT_CHG = 1,
	DEF_SAVE_CAP = 1,
	DEF_LOW_VOL = 3600,
	DEF_NUM = 0,
};
#define DEF_BAT_CHARGE {3600, 3700, 3760, 3810, 3870, 3910, 3960, 4020, 4080, 4130, 4180}; 
#define DEF_BAT_DISCHARGE {3495, 3570, 3630, 3700, 3740, 3790, 3825, 3865, 3920, 3980, 4050};
//////////////////////////////////////////////////////////////////
/*wifi*/
enum {        
	DEF_WIFI_TYPE = WIFI_TYPE_RTL8188EU,        
	DEF_WIFI_POWER = 0x000103d3,           	
};
#endif 

#endif




