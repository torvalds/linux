/*
 * IT660X I2C device driver
 *
 * Author: Y.S. Zhang <rain.zhang@amlogic.com>
 *
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* Standard Liniux Headers */
#include <linux/module.h>
#include <linux/i2c.h>

/* Amlogic Headers */
#include <linux/amlogic/tvin/tvin.h>

/* Local Headers */
#include "typedef.h"

#define IT660X_I2C_NAME         "it660x_i2c"

#define HDMIRXADR       (0x90>>1)
#define I2C_TRY_MAX_CNT 3

extern int it660x_debug_flag;
static struct i2c_client *it660x_i2c_client = NULL;

BYTE HDMIRX_ReadI2C_Byte(BYTE RegAddr)
{
    BYTE uc ;
    int  i2c_flag = -1;
    int i = 0;
    unsigned int i2c_try_cnt = I2C_TRY_MAX_CNT;
    unsigned char offset = RegAddr;
    struct i2c_msg msg[] = {
	    {
			.addr	= HDMIRXADR,
			.flags	= 0,    
			.len	= 1,
			.buf	= &offset,
		},
	    {
			.addr	= HDMIRXADR,
			.flags	= I2C_M_RD,    
			.len	= 1,
			.buf	= &uc,
		}

	};

repeat:
	i2c_flag = i2c_transfer(it660x_i2c_client->adapter, msg, 2);
    if (i2c_flag < 0) {
        pr_err("error in read it660x_i2c, %d byte(s) should be read,. \n", 1);
        if (i++ < i2c_try_cnt)
            goto repeat;
        return 0xff;
    }

    if(it660x_debug_flag&0x2){
        printk("[%s] %x: %x\n", __func__, RegAddr, uc);
    }
    return uc ;
}

SYS_STATUS HDMIRX_WriteI2C_Byte(BYTE RegAddr,BYTE val)
{
    
    int  i2c_flag = -1;
    int i = 0;
    unsigned int i2c_try_cnt = I2C_TRY_MAX_CNT;
    unsigned char offset = RegAddr;
    struct i2c_msg msg[] = {
	    {
			.addr	= HDMIRXADR,
			.flags	= 0,    
			.len	= 1,
			.buf	= &offset,
		},
	    {
			.addr	= HDMIRXADR,
			.flags	= I2C_M_NOSTART,    
			.len	= 1,
			.buf	= &val,
		}

	};

repeat:
	i2c_flag = i2c_transfer(it660x_i2c_client->adapter, msg, 2);
    if (i2c_flag < 0) {
        pr_err("error in writing it660x i2c, %d byte(s) should be writen,. \n", 1);
        if (i++ < i2c_try_cnt)
            goto repeat;
        return ER_FAIL;
    }
    else
    {
        if(it660x_debug_flag&0x2){
            printk("[%s] %x: %x\n", __func__, RegAddr, val);
        }
        return ER_SUCCESS;
    }
    
}


SYS_STATUS HDMIRX_ReadI2C_ByteN(BYTE RegAddr,BYTE *pData,int N)
{
    int  i2c_flag = -1;
    int i = 0;
    unsigned int i2c_try_cnt = I2C_TRY_MAX_CNT;
    unsigned char offset = RegAddr;
    struct i2c_msg msg[] = {
	    {
			.addr	= HDMIRXADR,
			.flags	= 0,    
			.len	= 1,
			.buf	= &offset,
		},
	    {
			.addr	= HDMIRXADR,
			.flags	= I2C_M_RD,    
			.len	= N,
			.buf	= pData,
		}

	};

repeat:
	i2c_flag = i2c_transfer(it660x_i2c_client->adapter, msg, 2);
    if (i2c_flag < 0) {
        pr_err("error in read it660x_i2c, %d byte(s) should be read,. \n", N);
        if (i++ < i2c_try_cnt)
            goto repeat;
        return ER_FAIL;
    }
    else
    {
        if(it660x_debug_flag&0x2){
            printk("[%s] %x:", __func__, RegAddr);
            for(i=0; i<N; i++){
                if((i%16)==0){
                    printk("\n");
                }
                printk("%x ", pData[i]);
            }
            printk("\n");
        }
        return ER_SUCCESS;
    }
}

SYS_STATUS HDMIRX_WriteI2C_ByteN(BYTE RegAddr,BYTE *pData,int N)
{
    int  i2c_flag = -1;
    int i = 0;
    unsigned int i2c_try_cnt = I2C_TRY_MAX_CNT;
    unsigned char offset = RegAddr;
    struct i2c_msg msg[] = {
	    {
			.addr	= HDMIRXADR,
			.flags	= 0,    
			.len	= 1,
			.buf	= &offset,
		},
	    {
			.addr	= HDMIRXADR,
			.flags	= I2C_M_NOSTART,    
			.len	= N,
			.buf	= pData,
		}

	};

repeat:
	i2c_flag = i2c_transfer(it660x_i2c_client->adapter, msg, 2);
    if (i2c_flag < 0) {
        pr_err("error in writing it660x i2c, %d byte(s) should be writen,. \n", N);
        if (i++ < i2c_try_cnt)
            goto repeat;
        return ER_FAIL;
    }
    else
    {
        if(it660x_debug_flag&0x2){
            printk("[%s] %x:", __func__, RegAddr);
            for(i=0; i<N; i++){
                if((i%16)==0){
                    printk("\n");
                }
                printk("%x ", pData[i]);
            }
            printk("\n");
        }
        return ER_SUCCESS;
    }
}

#ifdef _EDID_Parsing_
BOOL EDID_READ_BYTE( BYTE address, BYTE offset, BYTE byteno, BYTE *p_data, BYTE device)
{
    return i2c_read_byte(address, offset, byteno, p_data, device);

}
BOOL EDID_WRITE_BYTE( BYTE address, BYTE offset, BYTE byteno, BYTE *p_data, BYTE device )
{
    return i2c_write_byte(address, offset, byteno, p_data, device);
}
#endif

static int it660x_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

 if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
     pr_info("%s: functionality check failed\n", __FUNCTION__);
     return -ENODEV;
 }
 it660x_i2c_client = client;


 printk( " %s: it660x_i2c_client->addr = %x\n", __FUNCTION__, it660x_i2c_client->addr);

	return 0;
}

static int it660x_i2c_remove(struct i2c_client *client)
{
    pr_info("%s driver removed ok.\n", client->name);
	return 0;
}

static const struct i2c_device_id it660x_i2c_id[] = {
	{ IT660X_I2C_NAME, 0 },
	{ }
};


static struct i2c_driver it660x_i2c_driver = {
	.driver = {
        .owner  = THIS_MODULE,
		.name = IT660X_I2C_NAME,
	},
	.probe		= it660x_i2c_probe,
	.remove		= it660x_i2c_remove,
	.id_table	= it660x_i2c_id,
};


static int __init it660x_i2c_init(void)
{
    int ret = 0;
    pr_info( "%s . \n", __FUNCTION__ );

    ret = i2c_add_driver(&it660x_i2c_driver);

    if (ret < 0 /*|| it660x_i2c_client == NULL*/) {
        pr_err("it660x: failed to add i2c driver. \n");
        ret = -ENOTSUPP;
    }

	return ret;

}

static void __exit it660x_i2c_exit(void)
{
    pr_info( "%s . \n", __FUNCTION__ );

	i2c_del_driver(&it660x_i2c_driver);
}

MODULE_AUTHOR("Y.S Zhang <rain.zhang@amlogic.com>");
MODULE_DESCRIPTION("IT660X i2c device driver");
MODULE_LICENSE("GPL");

module_init(it660x_i2c_init);
module_exit(it660x_i2c_exit);

