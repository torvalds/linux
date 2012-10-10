/*
 * include/linux/goodix_touch.h
 *
 * Copyright (C) 2011 Goodix, Inc.
 *
 * Author: Felix
 * Date: 2011.04.28
 */

#ifndef 	_LINUX_GOODIX_TOUCH_H
#define		_LINUX_GOODIX_TOUCH_H

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

//*************************TouchScreen Work Part*****************************
#define GOODIX_I2C_NAME "gt818_ts"
#define GUITAR_GT80X
#define GT801_PLUS
#define GT801_NUVOTON
#define GUITAR_UPDATE_STATE 0x02
//#define NO_DEFAULT_ID                           //AUTO SET ADDRESS
//define resolution of the touchscreen
#define TOUCH_MAX_HEIGHT 	800//7680
#define TOUCH_MAX_WIDTH	480//	5120

//define resolution of the LCD
//#define SCREEN_MAX_HEIGHT	7680
//#define SCREEN_MAX_WIDTH	5120
// gpio base address
#define PIO_BASE_ADDRESS             (0x01c20800)
#define PIO_RANGE_SIZE               (0x400)

#define IRQ_EINT21                   (21)
#define IRQ_EINT29                   (29)

#define PHO_CFG2_OFFSET              (0X104)
#define PHO_DAT_OFFSET              (0X10C)
#define PHO_PULL1_OFFSET             (0X11C)

#define PIO_INT_STAT_OFFSET          (0x214)
#define PIO_INT_CTRL_OFFSET          (0x210)

#define SHUTDOWN_PORT                ()
#define INT_PORT                     (SW_INT_IRQNO_PIO)

#define 		TPD_DOWNLOADER_DEBUG  printk


//#define SHUTDOWN_PORT 	S3C64XX_GPF(3)			//SHUTDOWN管脚号
//#define INT_PORT  	S3C64XX_GPL(10)//S3C64XX_GPN(15)						//Int IO port
//#ifdef INT_PORT
//	#define TS_INT 		gpio_to_irq(INT_PORT)			//Interrupt Number,EINT18(119)
//	#define INT_CFG    	S3C_GPIO_SFN(3)//S3C_GPIO_SFN(2)					//IO configer as EINT
//#else
//	#define TS_INT	0
//#endif




// IC 类型
#define TPD_CHIP_VERSION_C_FIRMWARE_BASE 0x5A
#define TPD_CHIP_VERSION_D1_FIRMWARE_BASE 0x7A
#define TPD_CHIP_VERSION_E_FIRMWARE_BASE 0x9A
#define TPD_CHIP_VERSION_D2_FIRMWARE_BASE 0xBA
enum
{
    TPD_GT818_VERSION_B,
    TPD_GT818_VERSION_C,
    TPD_GT818_VERSION_D1,
    TPD_GT818_VERSION_E,
    TPD_GT818_VERSION_D2
};

#define FLAG_UP		0
#define FLAG_DOWN		1
//set GT801 PLUS trigger mode,只能设置0或1
#define INT_TRIGGER		1	   // 1=rising 0=falling
#define POLL_TIME		10	//actual query spacing interval:POLL_TIME+6

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM	5
#else
	#define MAX_FINGER_NUM	1
#endif


#if defined(INT_PORT)
	#if MAX_FINGER_NUM <= 3
	#define READ_BYTES_NUM 1+2+MAX_FINGER_NUM*5
	#elif MAX_FINGER_NUM == 4
	#define READ_BYTES_NUM 1+28
	#elif MAX_FINGER_NUM == 5
	#define READ_BYTES_NUM 1+34
	#endif
#else
	#define READ_BYTES_NUM 1+34
#endif

/*****************
enum finger_state {
#define FLAG_MASK 0x01
	FLAG_UP = 0,
	FLAG_DOWN = 1,
	FLAG_INVALID = 2,
};


struct point_node
{
	uint8_t id;
	//uint8_t retry;
	enum finger_state state;
	uint8_t pressure;
	unsigned int x;
	unsigned int y;
};
struct ts_event {
	u16	x1;
	u16	y1;
	u16	x2;
	u16	y2;
	u16	x3;
	u16	y3;
	u16	x4;
	u16	y4;
	u16	x5;
	u16	y5;
	u16	pressure;
    u8  touch_point;
};
********/
/* Notice: This definition used by platform_data.
 * It should be move this struct info to platform head file such as plat/ts.h.
 * If not used in client, it will be NULL in function of goodix_ts_probe.
 */
struct goodix_i2c_platform_data {
	uint32_t gpio_irq;			//IRQ port, use macro such as "gpio_to_irq" to get Interrupt Number.
	uint32_t irq_cfg;			//IRQ port config, must refer to master's Datasheet.
	uint32_t gpio_shutdown;		        //Shutdown port number
	uint32_t shutdown_cfg;		        //Shutdown port config
	uint32_t screen_width;		        //screen width
	uint32_t screen_height;		        //screen height
};



//#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

#define READ_TOUCH_ADDR_H 0x07
#define READ_TOUCH_ADDR_L 0x12
#define READ_KEY_ADDR_H 0x07
#define READ_KEY_ADDR_L 0x21
#define READ_COOR_ADDR_H 0x07
#define READ_COOR_ADDR_L 0x22
#define READ_ID_ADDR_H 0x00
#define READ_ID_ADDR_L 0xff
//****************************升级模块参数******************************************

//******************************************************************************
struct goodix_ts_data {
	uint16_t addr;
	uint8_t bad_data;
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_reset;		//use RESET flag
	int use_irq;		//use EINT flag
    int gpio_irq;
	int read_mode;		//read moudle mode,20110221 by andrew
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int retry;
       unsigned int version;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int (*power)(struct goodix_ts_data * ts, int on);
};

//*****************************End of Part I *********************************

//*************************Touchkey Surpport Part*****************************
//#define HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY
	const uint16_t touch_key_array[]={
									  KEY_MENU,				//MENU
									  KEY_BACK,				//HOME
									  KEY_SEND				//CALL
									 };
	#define MAX_KEY_NUM	 (sizeof(touch_key_array)/sizeof(touch_key_array[0]))
#endif
//*****************************End of Part II*********************************

//*************************Firmware Update part*******************************
#define AUTO_UPDATE_GT818             //如果定义了则上电会自动判断是否需要升级

#define CONFIG_TOUCHSCREEN_GOODIX_IAP
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data);
static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data );

#define PACK_SIZE 					64					//update file package size
//#define MAX_TIMEOUT					30000				//update time out conut
//#define MAX_I2C_RETRIES				10					//i2c retry times

//I2C buf address
//#define ADDR_CMD					80
//#define ADDR_STA					81
//#define ADDR_DAT					82


//write cmd
#define APK_UPDATE_TP               1
#define APK_READ_FUN                 10
#define APK_WRITE_CFG               11

//fun cmd
//#define CMD_DISABLE_TP             0
//#define CMD_ENABLE_TP              1
#define CMD_READ_VER               2
#define CMD_READ_RAW               3
#define CMD_READ_DIF               4
#define CMD_READ_CFG               5
#define CMD_READ_CHIP_TYPE         6

//#define CMD_SYS_REBOOT             101

//read mode
#define MODE_RD_VER                1
#define MODE_RD_RAW                2
#define MODE_RD_DIF                3
#define MODE_RD_CFG                4
#define MODE_RD_CHIP_TYPE          5
//error no
//#define ERROR_NO_FILE				2//ENOENT
//#define ERROR_FILE_READ				23//ENFILE
//#define ERROR_FILE_TYPE				21//EISDIR
//#define ERROR_GPIO_REQUEST			4//EINTR
//#define ERROR_I2C_TRANSFER			5//EIO
//#define ERROR_NO_RESPONSE			16//EBUSY
//#define ERROR_TIMEOUT				110//ETIMEDOUT

struct tpd_firmware_info_t
{
    int magic_number_1;
    int magic_number_2;
    unsigned short version;
    unsigned short length;
    unsigned short checksum;
    unsigned char data;
};

#define  NVRAM_LEN               0x0FF0   //	nvram total space
#define  NVRAM_BOOT_SECTOR_LEN	 0x0100	// boot sector
#define  NVRAM_UPDATE_START_ADDR 0x4100

#define  BIT_NVRAM_STROE	    0
#define  BIT_NVRAM_RECALL	    1
#define BIT_NVRAM_LOCK 2
#define  REG_NVRCS_H 0X12
#define  REG_NVRCS_L 0X01
//#define PACK_SIZE 					64					//update file package size
#define GT818_SET_INT_PIN( level )	{gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");gpio_write_one_pin_value(gpio_int_hdle,level , "ctp_int_port");} //gpio_direction_output(INT_PORT, level) //null macro now
#endif
//*****************************End of Part III********************************
struct goodix_i2c_rmi_platform_data {
	uint32_t version;	/* Use this entry for panels with */
	//reservation
};

#define RAW_DATA_READY          1
#define RAW_DATA_NON_ACTIVE     0xffffffff
#define RAW_DATA_ACTIVE         0

#define GT818_I2C_ADDR_1        0xba
#define GT818_I2C_ADDR_2        0x6e
#define GT818_I2C_ADDR_3        0x28
#define GT801_I2C_ADDR          0xaa


enum CHIP_TYPE
{
    GT800 = 1,
    GT800PLUS,
    GT800PLUS3,
    GT816,
    GT818,
    GT8105,
    GT8110,
    GT818PLUS
};


#endif /* _LINUX_GOODIX_TOUCH_H */
