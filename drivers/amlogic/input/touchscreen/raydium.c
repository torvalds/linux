/*
 * linux/drivers/input/touchscreen/raydium.c
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

#define DRIVER_NAME         "raydium"
#define DRIVER_VERSION   "1"

#define RAYDIUM_CMD_START    0
#define RAYDIUM_PACKET_SIZE  8

#define raydium_debug_info// printk
#define buf_to_short(buf)   ((*buf << 8) | *(buf+1))

int raydium_reset(struct device *dev);
int raydium_calibration(struct device *dev);
int raydium_get_event (struct device *dev, struct ts_event *event);

struct ts_chip raydium_chip = {
    .name = DRIVER_NAME,
    .version = DRIVER_VERSION,
    .reset = raydium_reset,
    .calibration = raydium_calibration,
    .get_event = raydium_get_event,
};


static int raydium_write_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
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


static int raydium_read_block(struct i2c_client *client, u8 addr, u8 *buf, int len)
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


int raydium_reset(struct device *dev)
{
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    struct ts_platform_data *pdata = dev->platform_data;

    if (pdata->data) {
        int reset_gpio = (int)pdata->data - 1;
        gpio_direction_output(reset_gpio, 0);
        msleep(20);
        gpio_direction_output(reset_gpio, 1);
        msleep(20);
    }

    return 0;
}
    

int raydium_calibration(struct device *dev)
{
	int ret = -1;
	char buf=0x03;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	ret = raydium_write_block(client,0x78,&buf,1);
	mdelay(500);
	return ret;
}


int raydium_get_event (struct device *dev, struct ts_event *event)
{
    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
    struct ts_platform_data *pdata = dev->platform_data;
    struct ts_info *info = &pdata->info;
    u8 buf[RAYDIUM_PACKET_SIZE];
    int event_num;
    int i, ba;

    memset(buf, 0, ARRAY_SIZE(buf));
    if (raydium_read_block(client, RAYDIUM_CMD_START,
            buf, RAYDIUM_PACKET_SIZE) < 0) {    
        /* i2c read failed */
        raydium_debug_info("raydium read i2c failed!\n");
        return -1;
    }
    raydium_debug_info("org data =%d, %d, %d, %d, ", buf[0], buf[1], buf[2], buf[3]);
    raydium_debug_info("%d, %d, %d, %d\n", buf[4], buf[5], buf[6], buf[7]);

    event_num = 0;
    for (i=0; i<2; i++) {
        ba = i*4;
        event->x = (buf[ba+1] << 8) | buf[ba];
        event->y = (buf[ba+3] << 8) | buf[ba+2];
        if (info->swap_xy) {
            swap(event->x, event->y);
        }
        if (info->x_pol) {
            event->x = info->xmax + info->xmin - event->x;
        }
        if (info->y_pol) {
            event->y = info->ymax + info->ymin - event->y;
        }
        if (event->x || event->y) {
            event->z = 1;
            event->w = 1;
            event++;
            event_num++;
        }
    }
    return event_num;
}


static int raydium_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	ret = capts_probe(&client->dev, &raydium_chip);
	if(ret == 0)
		raydium_calibration(&client->dev);
	return ret;
}


static int raydium_remove(struct i2c_client *client)
{
    return capts_remove(&client->dev);
}


static int raydium_suspend(struct i2c_client *client, pm_message_t msg)
{
    return capts_suspend(&client->dev, msg);
}


static int raydium_resume(struct i2c_client *client)
{
    return capts_resume(&client->dev);
}


static const struct i2c_device_id raydium_ids[] = {
   { DRIVER_NAME, 0 },
   { }
};

MODULE_DEVICE_TABLE(i2c, raydium_ids);

static struct i2c_driver raydium_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe = raydium_probe,
    .remove = raydium_remove,
    .suspend = raydium_suspend,
    .resume = raydium_resume,
    .id_table = raydium_ids,
};

static int __init raydium_init(void)
{
    return i2c_add_driver(&raydium_driver);
}

static void __exit raydium_exit(void)
{
    i2c_del_driver(&raydium_driver);
}
#ifdef CONFIG_DEFERRED_MODULE_INIT
deferred_module_init(raydium_init);
#else
module_init(raydium_init);
#endif
module_exit(raydium_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("raydium capacitive touch screen driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
