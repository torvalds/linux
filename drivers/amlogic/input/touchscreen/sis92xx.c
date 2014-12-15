/*
 * linux/drivers/input/touchscreen/sis92xx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/capts.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
	static struct early_suspend sis_early_suspend;
#endif

#define DRIVER_NAME         "sis92xx"
#define DRIVER_VERSION   "1"

#define sis92xx_info printk

#define SIS92XX_CMD_NORMAL                       0x0
#define SIS92XX_CMD_WAITREADY                 0x4
#define SIS92XX_CMD_RESET                           0x82
#define SIS92XX_CMD_UPDATE_FW_INFO    0x83
#define SIS92XX_CMD_UPDATE_FW_DATA  0x84
#define SIS92XX_CMD_READ_MEM_DATA    0x86
#define SIS92XX_CMD_RECALIBRATE            0x87
#define SIS92XX_PACKET_SIZE            16

#define get_bits(val, start_bit, bit_num) ((val >> start_bit) & ((1 << bit_num) - 1))

#define buf_to_short(buf)   ((*buf << 8) +*(buf+1))

int sis92xx_reset(struct device *dev);
int sis92xx_calibration(struct device *dev);
int sis92xx_get_event (struct device *dev, struct ts_event *event);
#ifdef CONFIG_HAS_EARLYSUSPEND
static int sis92xx_suspend(struct early_suspend *handler);
static int sis92xx_resume(struct early_suspend *handler);
#endif
struct ts_chip sis92xx_chip = {
    .name = DRIVER_NAME,
    .version = DRIVER_VERSION,
    .reset = sis92xx_reset,
    .calibration = sis92xx_calibration,
    .get_event = sis92xx_get_event,
};

/*
* CRC16 implementation acording to CCITT standards
*/
static const unsigned short crc16tab[256]= {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
    0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
    0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
    0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
    0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
    0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
    0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
    0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
    0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
    0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
    0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
    0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
    0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
    0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
    0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
    0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
    0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
    0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
    0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
    0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
    0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
    0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
    0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
    0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
    0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
    0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
    0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
    0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
    0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
    0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
    0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

unsigned short crc16_ccitt(const unsigned char *buf, int len)
{
    int counter;
    unsigned short crc = 0;
    
    for( counter = 0; counter < len; counter++)
        crc = (crc<<8) ^ crc16tab[((crc>>8) ^ buf[counter]) & 0x00FF];
    return crc;
}


static int sis92xx_write_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
{ 
    struct i2c_msg msg[2] = {
        [0] = {
            .addr = client->addr,
            .flags = client->flags,
            .len = 1,
            .buf = &addr
        },
        [1] = {
            .addr = client->addr,
            .flags = client->flags | I2C_M_NOSTART,
            .len = len,
            .buf = buf
        },
    };
    int msg_num = len ? ARRAY_SIZE(msg) : 1;
    return i2c_transfer(client->adapter, msg, msg_num);
}


static int sis92xx_read_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
{ 
    struct i2c_msg msg[2] = {
        [0] = {
            .addr = client->addr,
            .flags = client->flags,
            .len = 1,
            .buf = &addr
        },
        [1] = {
            .addr = client->addr,
            .flags = client->flags | I2C_M_RD,
            .len = len,
            .buf = buf
        },
    };
    
    return i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
}

	
int sis92xx_reset(struct device *dev)
{
    int ret = -1;
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    u8 buf[6];
    int retry = 100;
    
    return 0;   
    while(retry && (sis92xx_read_block(client, SIS92XX_CMD_RESET, buf, 6) > 0)) {
        if ((buf[0] == 4) && (buf[1] == 0) && (buf[2] == 0x40)) {
            retry--;
            msleep(100);
        }
        else if ((buf[0] == 4) && (buf[1] == 0) && (buf[2] == 0x80)
        && (buf[3] == 0xff) && (buf[4] == 0xff)) {
            /* reset ok */
            ret = 0;
            break;
        }
    }
    
    return ret;
}
    

int sis92xx_calibration(struct device *dev)
{
    int ret = -1;
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    u8 buf[6];
    int retry = 100;
    
    while(retry && (sis92xx_read_block(client,SIS92XX_CMD_RECALIBRATE, buf, 6) > 0)) {
        if ((buf[0] == 4) && (buf[1] == 0) && (buf[2] == 0x40)) {
            retry--;
            msleep(100);
        }
        else if ((buf[0] == 4) && (buf[1] == 0) && (buf[2] == 0x80)
        && (buf[3] == 0xff) && (buf[4] == 0xff)) {
            /* calibration ok */
            ret = 0;
            break;
        }
    }
    
    return ret;
}

int sis92xx_get_event (struct device *dev, struct ts_event *event)
{
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    u8 buf[SIS92XX_PACKET_SIZE];
    u8 packet_size;
    u8 event_num = 0;
     int i;
	 
    memset(buf, 0, SIS92XX_PACKET_SIZE);   
    if (sis92xx_read_block(client, SIS92XX_CMD_NORMAL,
            buf, SIS92XX_PACKET_SIZE) < 0) {
        /* i2c read failed */
        return 0;
    }
    
    packet_size = buf[0];
    event_num = get_bits(buf[2], 0, 4);
    event_num %= EVENT_MAX;
    
//    /* check crc */
//    if (get_bits(buf[2], 4, 1)) {
//        u16 crc;
//        crc = buf_to_short(&buf[packet_size - 1]);
//        if (crc16_ccitt(buf, packet_size) != crc) {
//            /* check crc failed */
//            return 0;
//        }
//    }

    /* generate the X/Y points */
    int ba = 3;
    struct ts_platform_data *pdata = dev->platform_data;
    struct ts_info *info = &pdata->info;
    for(i=0; i<event_num; i++) {
        event->state = get_bits(buf[ba], 0, 4);
        //event->id = get_bits(buf[ba], 4, 4);
        event->id = i;
        event->x = buf_to_short(&buf[ ba+1]);
        event->y = buf_to_short(&buf[ ba+3]);
        event->z = event->state ? 0 : 1;
        event->w = event->state ? 0 : 1;

        if (info->swap_xy) {
            swap(event->x, event->y);
        }
        if (info->x_pol) {
            event->x = info->xmax + info->xmin - event->x;
        }
        if (info->y_pol) {
            event->y = info->ymax + info->ymin - event->y;
        }
        
        ba += 5;
        event++;
    }

    return event_num;
}


static int sis92xx_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct ts_platform_data *pdata = client->dev.platform_data;

	if(pdata&&pdata->power_off&&pdata->power_on){
		pdata->power_off();
		mdelay(50);
		pdata->power_on();
	}
	ret = capts_probe(&client->dev, &sis92xx_chip);
#ifdef CONFIG_HAS_EARLYSUSPEND
		sis_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
		sis_early_suspend.suspend = sis92xx_suspend;
		sis_early_suspend.resume = sis92xx_resume;
		sis_early_suspend.param = client;
		register_early_suspend(&sis_early_suspend);
#endif
    return ret;
}


static int sis92xx_remove(struct i2c_client *client)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&sis_early_suspend);
#endif
    return capts_remove(&client->dev);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static int sis92xx_suspend(struct early_suspend *handler)
{
	int ret = -1;
	pm_message_t msg={0};
	if(handler && handler->param) {
		struct i2c_client *client = (struct i2c_client *)handler->param;
		ret = capts_suspend(&client->dev, msg);
	}
	return ret;
}

static int sis92xx_resume(struct early_suspend *handler)
{
 	int ret = -1;
	if(handler && handler->param) {
		struct i2c_client *client = (struct i2c_client *)handler->param;
		ret = capts_resume(&client->dev);
	}
	return ret;
}
#else
static int sis92xx_suspend(struct i2c_client *client, pm_message_t msg)
{
    return capts_suspend(&client->dev, msg);
}

static int sis92xx_resume(struct i2c_client *client)
{
    return capts_resume(&client->dev);
}
#endif

static const struct i2c_device_id sis92xx_ids[] = {
   { DRIVER_NAME, 0 },
   { }
};

MODULE_DEVICE_TABLE(i2c, sis92xx_ids);

static struct i2c_driver sis92xx_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe = sis92xx_probe,
    .remove = sis92xx_remove,
    .suspend = sis92xx_suspend,
    .resume = sis92xx_resume,
    .id_table = sis92xx_ids,
};

static int __init sis92xx_init(void)
{
       return i2c_add_driver(&sis92xx_driver);
}

static void __exit sis92xx_exit(void)
{
       i2c_del_driver(&sis92xx_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(sis92xx_init);
#else
module_init(sis92xx_init);
#endif
module_exit(sis92xx_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("sis92xx capacitive touch screen driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
