/*
 * linux/drivers/input/touchscreen/hx8520.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <mach/gpio.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/capts.h>

#define DRIVER_NAME         "hx8520"
#define DRIVER_VERSION   "1"

#define HX8520_CMD_START    0x86
#define HX8520_PACKET_SIZE  20

#define hx8520_debug_info// printk

int hx8520_reset(struct device *dev);
int hx8520_calibration(struct device *dev);
int hx8520_get_event (struct device *dev, struct ts_event *event);

struct ts_chip hx8520_chip = {
    .name = DRIVER_NAME,
    .version = DRIVER_VERSION,
    .reset = hx8520_reset,
    .calibration = hx8520_calibration,
    .get_event = hx8520_get_event,
};


static int hx8520_write_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
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
    int msg_num = (buf && len) ? ARRAY_SIZE(msg) : 1;
    return i2c_transfer(client->adapter, msg, msg_num);
}


static int hx8520_read_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
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


int hx8520_reset(struct device *dev)
{
    int ret = -1;
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    struct ts_platform_data *pdata = dev->platform_data;
    u8 buf[4];
    
    if (pdata->data) {
        int reset_gpio = (int)pdata->data - 1;
        gpio_direction_output(reset_gpio, 0);
        msleep(20);
        gpio_direction_output(reset_gpio, 1);
        msleep(20);
    }
    
    /* 1. IC internal power on */
    ret = hx8520_write_block(client, 0x81, 0, 0);
    if(ret < 0) return ret;

    /* 2. MCU power on */ 
    msleep(120);
    buf[0] = 0x02;
    ret = hx8520_write_block(client, 0x35, buf, 1);
    if(ret < 0) return ret;
    
    /* 3. flash power on */
    msleep(120);
    buf[0] = 0x01;
    ret = hx8520_write_block(client, 0x36, buf, 1);
    if(ret < 0) return ret;

    /* 4. Start touch panel sensing */
    msleep(120);
    ret = hx8520_write_block(client, 0x83, 0, 0);
    if(ret < 0) return ret;
    
    /*dummy read */
    msleep(200);
    hx8520_read_block(client, 0x31, buf, 3);

    return 0;
}
    

int hx8520_calibration(struct device *dev)
{
    int ret = -1;
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    
    return ret;
}


int hx8520_get_event (struct device *dev, struct ts_event *event)
{
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    struct ts_platform_data *pdata = dev->platform_data;
    struct ts_info *info = &pdata->info;
    u8 buf[HX8520_PACKET_SIZE];
    int event_num;
    int i;

    memset(buf, 0, ARRAY_SIZE(buf));
    if (hx8520_read_block(client, HX8520_CMD_START,
            buf, HX8520_PACKET_SIZE) < 0) {    
        /* i2c read failed */
        hx8520_debug_info("hx8520 read i2c failed!\n");
        return -1;
    }
    hx8520_debug_info("%d, %d, %d, %d\n", buf[0], buf[1], buf[2], buf[3]);
    hx8520_debug_info("%d, %d, %d, %d\n", buf[4], buf[5], buf[6], buf[7]);
    hx8520_debug_info("%d, %d, %d, %d\n", buf[8], buf[9], buf[10], buf[11]);
    hx8520_debug_info("%d, %d, %d, %d\n", buf[12], buf[13], buf[14], buf[15]);
    hx8520_debug_info("%d, %d, %d, %d\n", buf[16], buf[17], buf[18], buf[19]);
     
    event_num = 0;
    int zero_num = 0;
    for(i=0; i<4; i++) {
        event->x  = (buf[i*4] << 8) | buf[i*4+1];
        event->y  = (buf[i*4+2] << 8) | buf[i*4+3];
        if (info->swap_xy) {
            swap(event->x, event->y);
        }
        if ((event->x == 0) && (event->y == 0)) zero_num++;
        if ((event->x >= info->xmin) && (event->x < info->xmax)
        &&(event->y >= info->ymin) && (event->y < info->ymax)) {
            if (info->x_pol) {
                event->x = info->xmax + info->xmin - event->x;
            }
            if (info->y_pol) {
                event->y = info->ymax + info->ymin - event->y;
            }
            event->z = 1;
            event->w = 1;
            event->id = i;
            event++;
            event_num++;
        }
        else if ((event->x != -1) || (event->y != -1)) {
            hx8520_debug_info("hx8520 data%d error, %d, %d\n", i, event->x, event->y);            
            event_num = -1;
            break;
        }
    }
    
    if ((zero_num == 4) && (buf[16] == 0)) {
        return 0;
    }
    if ((event_num < 0) || (event_num > 2) || (event_num != buf[16])) {
        hx8520_debug_info("hx8520 stack error\n");
        hx8520_write_block(client, 0x88, 0, 0); // Execute CMD 0x88H to clear stack
        hx8520_read_block(client, HX8520_CMD_START, buf, HX8520_PACKET_SIZE);
        return -2;
    }
    
    return event_num;
}


static int hx8520_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    return capts_probe(&client->dev, &hx8520_chip);
}


static int hx8520_remove(struct i2c_client *client)
{
    return capts_remove(&client->dev);
}


static int hx8520_suspend(struct i2c_client *client, pm_message_t msg)
{
    return capts_suspend(&client->dev, msg);
}


static int hx8520_resume(struct i2c_client *client)
{
    return capts_resume(&client->dev);
}


static const struct i2c_device_id hx8520_ids[] = {
   { DRIVER_NAME, 0 },
   { }
};

MODULE_DEVICE_TABLE(i2c, hx8520_ids);

static struct i2c_driver hx8520_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe = hx8520_probe,
    .remove = hx8520_remove,
    .suspend = hx8520_suspend,
    .resume = hx8520_resume,
    .id_table = hx8520_ids,
};

static int __init hx8520_init(void)
{
    return i2c_add_driver(&hx8520_driver);
}

static void __exit hx8520_exit(void)
{
    i2c_del_driver(&hx8520_driver);
}

#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(hx8520_init);
#else
module_init(hx8520_init);
#endif
module_exit(hx8520_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("hx8520 capacitive touch screen driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
