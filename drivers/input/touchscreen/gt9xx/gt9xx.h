/* drivers/input/touchscreen/gt9xx.h
 * 
 * 2010 - 2013 Goodix Technology.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be a reference 
 * to you, when you are integrating the GOODiX's CTP IC into your system, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
 * General Public License for more details.
 * 
 */

#ifndef _GOODIX_GT9XX_H_
#define _GOODIX_GT9XX_H_

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include "../tp_suspend.h"


//#include <mach/gpio.h>
//#include <linux/earlysuspend.h>

#define CONFIG_8_9  0
#define DEBUG_SWITCH  0

//***************************PART1:ON/OFF define*******************************
#define GTP_CUSTOM_CFG        0
#if CONFIG_8_9
#define GTP_CHANGE_X2Y        1
#define GTP_X_REVERSE_ENABLE	0
#define GTP_Y_REVERSE_ENABLE	1
#else
#define GTP_CHANGE_X2Y        1
#define GTP_X_REVERSE_ENABLE	1
#define GTP_Y_REVERSE_ENABLE	0
#endif
#define GTP_DRIVER_SEND_CFG   1
#define GTP_HAVE_TOUCH_KEY    0
#define GTP_POWER_CTRL_SLEEP  0

#if defined(CONFIG_CHROME_PLATFORMS)
#define GTP_ICS_SLOT_REPORT   1
#else
#define GTP_ICS_SLOT_REPORT   0
#endif

#define GTP_AUTO_UPDATE       0    // auto update fw by .bin file as default
#define GTP_HEADER_FW_UPDATE  0    // auto update fw by gtp_default_FW in gt9xx_firmware.h, function together with GTP_AUTO_UPDATE
#define GTP_AUTO_UPDATE_CFG   0    // auto update config by .cfg file, function together with GTP_AUTO_UPDATE

#define GTP_COMPATIBLE_MODE   1    /* compatible with GT9XXF */

#define GTP_CREATE_WR_NODE    0
#define GTP_ESD_PROTECT       0    // esd protection with a cycle of 2 seconds

#define GTP_WITH_PEN          0
#define GTP_PEN_HAVE_BUTTON   0    // active pen has buttons, function together with GTP_WITH_PEN

#define GTP_GESTURE_WAKEUP    0    // gesture wakeup

#define GTP_DEBUG_ON          1
#define GTP_DEBUG_ARRAY_ON    0
#define GTP_DEBUG_FUNC_ON     0

/* init use fixed clk num */
/* if open, u8 p_main_clk[6] = {69,69,69,69,69,167}; */
#define GTP_USE_FIXED_CLK     1

#define PEN_DOWN 1
#define PEN_RELEASE 0
#define PEN_DOWN_UP 2 //fjp

#if GTP_COMPATIBLE_MODE
typedef enum
{
    CHIP_TYPE_GT9  = 0,
    CHIP_TYPE_GT9F = 1,
} CHIP_TYPE_T;
#endif

struct goodix_ts_data {
    spinlock_t irq_lock;
    struct i2c_client *client;
    struct input_dev  *input_dev;
    struct hrtimer timer;
    struct work_struct  work;
    //struct early_suspend early_suspend;
    s32 irq_is_disable;
    s32 use_irq;
    u16 abs_x_max;
    u16 abs_y_max;
    u8  max_touch_num;
    u8  int_trigger_type;
    u8  green_wake_mode;
    u8  enter_update;
    u8  gtp_is_suspend;
    u8  gtp_rawdiff_mode;
    u8  gtp_cfg_len;
    u8  fixed_cfg;
    u8  fw_error;
    u8  pnl_init_error;
	u8 cfg_file_num;
//add struct tp_device by Sam
    struct  tp_device  tp;

    //add by Daniel(yc)
    int irq;
    int irq_pin;
    int pwr_pin;
    int rst_pin;
    int tp_select_pin;
    int rst_val;
	u8 pendown;
    unsigned long irq_flags;
    
#if GTP_WITH_PEN
    struct input_dev *pen_dev;
#endif

#if GTP_ESD_PROTECT
    spinlock_t esd_lock;
    u8  esd_running;
    s32 clk_tick_cnt;
#endif

#if GTP_COMPATIBLE_MODE
    u16 bak_ref_len;
    s32 ref_chk_fs_times;
    s32 clk_chk_fs_times;
    CHIP_TYPE_T chip_type;
    u8 rqst_processing;
    u8 is_950;
#endif
    struct regulator *tp_regulator;

};

extern u16 show_len;
extern u16 total_len;


//*************************** PART2:TODO define **********************************
// STEP_1(REQUIRED): Define Configuration Information Group(s)
// Sensor_ID Map:
/* sensor_opt1 sensor_opt2 Sensor_ID
    GND         GND         0 
    VDDIO       GND         1 
    NC          GND         2 
    GND         NC/300K     3 
    VDDIO       NC/300K     4 
    NC          NC/300K     5 
*/
// TODO: define your own default or for Sensor_ID == 0 config here. 
// The predefined one is just a sample config, which is not suitable for your tp in most cases.
/*
#define CTP_CFG_GROUP1 {\
    0x46,0xE0,0x01,0x56,0x03,0x02,0xF1,0x01,0x02,0x44,\
    0x00,0x04,0x46,0x32,0x03,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x11,0x04,0x26,0x01,0x74,0x77,0x05,0x00,0x88,\
    0x64,0x0F,0xD0,0x07,0x05,0x07,0x00,0xDA,0x01,0x1D,\
    0x00,0x01,0x08,0x08,0x33,0x33,0x5D,0xAA,0x00,0x00,\
    0x00,0x32,0x96,0x54,0xC5,0x03,0x02,0x00,0x00,0x01,\
    0xC8,0x38,0x00,0xA0,0x45,0x00,0x91,0x57,0x00,0x80,\
    0x6C,0x00,0x61,0x87,0x00,0x61,0x10,0x0B,0x08,0x00,\
    0x51,0x40,0x30,0xFF,0xFF,0x00,0x04,0x00,0x00,0x1E,\
    0x0A,0x00,0x06,0x0B,0x09,0x0F,0x08,0x07,0x01,0x03,\
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0x03,0x02,0x05,0x04,0x07,0x06,0x09,\
    0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,\
    0x00,0x00,0x08,0x09,0x0A,0x0D,0x0E,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0xFF,0x0B,0x0C,0xFF,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,\
    0xFF,0xFF,0xFF,0xFF,0x6C,0xB2,0xB2,0x6C,0xFF,0x00,\
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x80,\
    0x8C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
    0x00,0x00,0x00,0x00,0x00,0x00,0xF6,0x01\
    }
*/

/*
//WGJ10187_GT9271_Config_20140623_104014_0X41.cfg
#define CTP_CFG_GROUP1 {\
    0x41,0xB0,0x04,0x80,0x07,0x05,0xF5,0x00,0x01,0x08,0x28,0x0F,0x64,0x32,0x03, \
    0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x16,0x19,0x1E,0x14,0x8F,0x2F,0x99, \
    0x41,0x43,0x15,0x0E,0x00,0x00,0x00,0x22,0x03,0x1D,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x06,0x00,0x2D,0x62,0x94,0xC5,0x02,0x07,0x17,0x00,0x04, \
    0x92,0x30,0x00,0x86,0x39,0x00,0x7F,0x42,0x00,0x79,0x4D,0x00,0x74,0x5A,0x00, \
    0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x17,0x16,0x15,0x14,0x11,0x10,0x0F,0x0E, \
    0x0D,0x0C,0x09,0x08,0x07,0x06,0x05,0x04,0x01,0x00,0xFF,0xFF,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x04,0x06,0x07,0x08,0x0A,0x0C, \
    0x0D,0x0F,0x10,0x11,0x12,0x13,0x14,0x29,0x28,0x27,0x26,0x25,0x24,0x23,0x22, \
    0x21,0x20,0x1F,0x1E,0x1C,0x1B,0x19,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x3D,0x01 \
    }
*/

//WGJ10187_GT9271_Config_20140623_104014_0X41.cfg
#define CTP_CFG_GROUP1 {\
    0x41,0x80,0x07,0xB0,0x04,0x0A,0x05,0x00,0x01,0x08,0x28,0x0F,0x50,0x32,0x03, \
    0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x8F,0x2F,0x99, \
    0x2B,0x2D,0x31,0x0D,0x00,0x00,0x00,0x01,0x03,0x1D,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x23,0x55,0x94,0xC5,0x02,0x07,0x00,0x00,0x00, \
    0x8C,0x26,0x00,0x7B,0x2D,0x00,0x6C,0x36,0x00,0x61,0x41,0x00,0x58,0x4E,0x00, \
    0x58,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x04,0x05,0x06,0x07,0x08,0x09, \
    0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x14,0x15,0x16,0x17,0xFF,0xFF,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x13,0x12,0x11,0x10,0x0F,0x0D,0x0C, \
    0x0A,0x08,0x07,0x06,0x04,0x02,0x00,0x19,0x1B,0x1C,0x1E,0x1F,0x20,0x21,0x22, \
    0x23,0x24,0x25,0x26,0x27,0x28,0x29,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00, \
    0x00,0x00,0x00,0x00,0xB5,0x01 \
    }

// TODO: define your config for Sensor_ID == 1 here, if needed
#define CTP_CFG_GROUP2 {\
    }

// TODO: define your config for Sensor_ID == 2 here, if needed
#define CTP_CFG_GROUP3 {\
    }

// TODO: define your config for Sensor_ID == 3 here, if needed
#define CTP_CFG_GROUP4 {\
}
// TODO: define your config for Sensor_ID == 4 here, if needed
#define CTP_CFG_GROUP5 {\
    }

// TODO: define your config for Sensor_ID == 5 here, if needed
#define CTP_CFG_GROUP6 {\
    }

// STEP_2(REQUIRED): Customize your I/O ports & I/O operations
/*
#define GTP_RST_PORT    S5PV210_GPJ3(6)
#define GTP_INT_PORT    S5PV210_GPH1(3)
#define GTP_INT_IRQ     gpio_to_irq(GTP_INT_PORT)
#define GTP_INT_CFG     S3C_GPIO_SFN(0xF)

#define GTP_GPIO_AS_INPUT(pin)          do{\
                                            gpio_direction_input(pin);\
                                            s3c_gpio_setpull(pin, S3C_GPIO_PULL_NONE);\
                                        }while(0)
#define GTP_GPIO_AS_INT(pin)            do{\
                                            GTP_GPIO_AS_INPUT(pin);\
                                            s3c_gpio_cfgpin(pin, GTP_INT_CFG);\
                                        }while(0)
*/                                        
#define GTP_GPIO_GET_VALUE(pin)         gpio_get_value(pin)
#define GTP_GPIO_OUTPUT(pin,level)      gpio_direction_output(pin,level)
#define GTP_GPIO_REQUEST(pin, label)    gpio_request(pin, label)
#define GTP_GPIO_FREE(pin)              gpio_free(pin)
#define GTP_IRQ_TAB                     {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_LEVEL_LOW, IRQ_TYPE_LEVEL_HIGH}

// STEP_3(optional): Specify your special config info if needed
#if GTP_CUSTOM_CFG
  #define GTP_MAX_HEIGHT   800
  #define GTP_MAX_WIDTH    480
  #define GTP_INT_TRIGGER  0            // 0: Rising 1: Falling
#else
  #define GTP_MAX_HEIGHT   4096
  #define GTP_MAX_WIDTH    4096
  #define GTP_INT_TRIGGER  1
#endif
#define GTP_MAX_TOUCH         10

// STEP_4(optional): If keys are available and reported as keys, config your key info here                             
#if GTP_HAVE_TOUCH_KEY
    #define GTP_KEY_TAB  {KEY_MENU, KEY_HOME, KEY_BACK}
#endif

//***************************PART3:OTHER define*********************************
#define GTP_DRIVER_VERSION          "V2.2<2014/01/14>"
#define GTP_I2C_NAME                "Goodix-TS"
#define GT91XX_CONFIG_PROC_FILE     "gt9xx_config"
#define GTP_POLL_TIME         10    
#define GTP_ADDR_LENGTH       2
#define GTP_CONFIG_MIN_LENGTH 186
#define GTP_CONFIG_MAX_LENGTH 240
#define FAIL                  0
#define SUCCESS               1
#define SWITCH_OFF            0
#define SWITCH_ON             1

//******************** For GT9XXF Start **********************//
#define GTP_REG_BAK_REF                 0x99D0
#define GTP_REG_MAIN_CLK                0x8020
#define GTP_REG_CHIP_TYPE               0x8000
#define GTP_REG_HAVE_KEY                0x804E
#define GTP_REG_MATRIX_DRVNUM           0x8069     
#define GTP_REG_MATRIX_SENNUM           0x806A

#define GTP_FL_FW_BURN              0x00
#define GTP_FL_ESD_RECOVERY         0x01
#define GTP_FL_READ_REPAIR          0x02

#define GTP_BAK_REF_SEND                0
#define GTP_BAK_REF_STORE               1
#define CFG_LOC_DRVA_NUM                29
#define CFG_LOC_DRVB_NUM                30
#define CFG_LOC_SENS_NUM                31

#define GTP_CHK_FW_MAX                  40
#define GTP_CHK_FS_MNT_MAX              300
#define GTP_BAK_REF_PATH                "/data/gtp_ref.bin"
#define GTP_MAIN_CLK_PATH               "/data/gtp_clk.bin"
#define GTP_RQST_CONFIG                 0x01
#define GTP_RQST_BAK_REF                0x02
#define GTP_RQST_RESET                  0x03
#define GTP_RQST_MAIN_CLOCK             0x04
#define GTP_RQST_RESPONDED              0x00
#define GTP_RQST_IDLE                   0xFF

//******************** For GT9XXF End **********************//
// Registers define
#define GTP_READ_COOR_ADDR    0x814E
#define GTP_REG_SLEEP         0x8040
#define GTP_REG_SENSOR_ID     0x814A
#define GTP_REG_CONFIG_DATA   0x8047
#define GTP_REG_VERSION       0x8140

#define RESOLUTION_LOC        3
#define TRIGGER_LOC           8

#define CFG_GROUP_LEN(p_cfg_grp)  (sizeof(p_cfg_grp) / sizeof(p_cfg_grp[0]))

// Log define
#define GTP_ERROR(fmt,arg...)          printk("<<-GTP-ERROR->> "fmt"\n",##arg)
#if DEBUG_SWITCH
#define GTP_INFO(fmt,arg...)           printk("<<-GTP-INFO->> "fmt"\n",##arg)
#define GTP_DEBUG(fmt,arg...)          do{\
                                         if(GTP_DEBUG_ON)\
                                         printk("<<-GTP-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
                                       }while(0)
#define GTP_DEBUG_ARRAY(array, num)    do{\
                                         s32 i;\
                                         u8* a = array;\
                                         if(GTP_DEBUG_ARRAY_ON)\
                                         {\
                                            printk("<<-GTP-DEBUG-ARRAY->>\n");\
                                            for (i = 0; i < (num); i++)\
                                            {\
                                                printk("%02x   ", (a)[i]);\
                                                if ((i + 1 ) %10 == 0)\
                                                {\
                                                    printk("\n");\
                                                }\
                                            }\
                                            printk("\n");\
                                        }\
                                       }while(0)
#define GTP_DEBUG_FUNC()               do{\
                                         if(GTP_DEBUG_FUNC_ON)\
                                         printk("     <<-GTP-FUNC->>       Func:%s@Line:%d\n",__func__,__LINE__);\
                                       }while(0)

#else
#define GTP_INFO(fmt,arg...)
#define GTP_DEBUG(fmt,arg...)
#define GTP_DEBUG_ARRAY(array, num)
#define GTP_DEBUG_FUNC()
#endif
#define GTP_SWAP(x, y)                 do{\
                                         typeof(x) z = x;\
                                         x = y;\
                                         y = z;\
                                       }while (0)

//*****************************End of Part III********************************
#define TRUE    1
#define FALSE   0

#endif /* _GOODIX_GT9XX_H_ */
