/* *
 * Copyright 2011 Pixtree Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @file        pix_i2c.c
 * @brief
 * @author      hun chan Yu(kingmst@pixtree.com)
 * @version     1.0.1
 * @history
 *   2011.11.14 : Create
 */


#include <linux/errno.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/map.h>

#include <asm/atomic.h>

#include "pix_i2c.h"

#if defined(CONFIG_BOARD_ODROID_A)
#error
#elif defined(CONFIG_BOARD_ODROID_A4)
#define PIN_SDA			EXYNOS4_GPX0(5)
#define PIN_SCL			EXYNOS4_GPX0(2)
#elif defined(CONFIG_BOARD_ODROID_PC)
#define PIN_SDA			EXYNOS4_GPC0(0)
#define PIN_SCL			EXYNOS4_GPC0(3)
#elif defined(CONFIG_BOARD_ETRI_RTU)
#define PIN_SDA			EXYNOS4_GPX0(5)
#define PIN_SCL			EXYNOS4_GPX0(0)
#elif defined(CONFIG_BOARD_ODROID_Q) || defined(CONFIG_BOARD_ODROID_Q2)
#define PIN_SDA			EXYNOS4_GPD0(2)
#define PIN_SCL			EXYNOS4_GPD0(3)
#else
#error
#endif
#define PIX_I2C_MINOR	(249)

#define SDA_IN			gpio_direction_input(PIN_SDA)
#define SDA_OUT			gpio_direction_input(PIN_SDA)
#define SDA_LOW			gpio_direction_output(PIN_SDA,0)
#define SDA_HIGH		gpio_direction_input(PIN_SDA)
#define SDA_DETECT		gpio_get_value(PIN_SDA)

#define SCL_LOW			gpio_direction_output(PIN_SCL,0)
#define SCL_HIGH		gpio_direction_input(PIN_SCL)
#define SCL_OUT			gpio_direction_input(PIN_SCL)

#define I2C_DELAY		udelay(2)
#define I2C_DELAY_LONG	udelay(50)

static atomic_t scull_available = ATOMIC_INIT(1);


int InitGPIO(void)
{
	gpio_request(PIN_SDA,"SDA");
	gpio_request(PIN_SCL,"SCL");
    

	SDA_HIGH;
	SCL_HIGH;

	return 0;
}

void ReleaseGPIO(void)
{
	gpio_free(PIN_SDA);
	gpio_free(PIN_SCL);


	return;
}

void I2c_start(void)
{
	SDA_HIGH;
	I2C_DELAY;
	I2C_DELAY;
	SCL_HIGH;
	I2C_DELAY;
	SDA_LOW;
	I2C_DELAY;
	SCL_LOW;
	I2C_DELAY;
}

void I2c_stop(void)
{
	SDA_LOW;
	I2C_DELAY;
	SCL_HIGH;
	I2C_DELAY;
	SDA_HIGH;
	I2C_DELAY_LONG;
}

unsigned char I2c_ack_detect(void)
{
	SDA_IN;	// SDA Input Mode
	I2C_DELAY;
	SCL_HIGH;
	I2C_DELAY;
	if (SDA_DETECT)
	{
		SDA_OUT;
		return ERROR_CODE_FALSE; // false
	}
	I2C_DELAY;
	SCL_LOW;
	SDA_OUT;
	return ERROR_CODE_TRUE; // true
}

void I2c_ack_send(void)
{
	SDA_OUT;
	SDA_LOW;
	I2C_DELAY;
	SCL_HIGH;
	I2C_DELAY;
	SCL_LOW;
	I2C_DELAY;
}

unsigned char I2c_write_byte(unsigned char data)
{
	int i;

	for(i = 0; i< 8; i++)
	{
		if( (data << i) & 0x80) SDA_HIGH;
		else SDA_LOW;
		I2C_DELAY;
		SCL_HIGH;
		I2C_DELAY;
		SCL_LOW;
		I2C_DELAY;
	}

	if(I2c_ack_detect()!=ERROR_CODE_TRUE) {
		return ERROR_CODE_FALSE;
	}
	return ERROR_CODE_TRUE;
}

unsigned char I2c_read_byte(void)
{
	int i;
	unsigned char data;

	data = 0;
	SDA_IN;
	for(i = 0; i< 8; i++){
		data <<= 1;
		I2C_DELAY;
		SCL_HIGH;
		I2C_DELAY;
		if (SDA_DETECT) data |= 0x01;
		SCL_LOW;
		I2C_DELAY;
	}
	I2c_ack_send();
	return data;
}

unsigned char I2c_write(unsigned char device_addr, unsigned char sub_addr, unsigned char *buff, int ByteNo)
{
	int i;

	I2c_start();
	I2C_DELAY;
	if(I2c_write_byte(device_addr)) {
		I2c_stop();
		return ERROR_CODE_WRITE_ADDR;
	}
	if(I2c_write_byte(sub_addr)) {
		I2c_stop();
		return ERROR_CODE_WRITE_ADDR;
	}
	for(i = 0; i<ByteNo; i++) {
		if(I2c_write_byte(buff[i])) {
			I2c_stop();
			return ERROR_CODE_WRITE_DATA;
		}
	}
	I2C_DELAY;
	I2c_stop();
	I2C_DELAY_LONG;
	return ERROR_CODE_TRUE;
}

unsigned char I2c_read(unsigned char device_addr, unsigned char sub_addr, unsigned char *buff, int ByteNo)
{
	int i;
	I2c_start();
	I2C_DELAY;
	if(I2c_write_byte(device_addr)) {
		I2c_stop();
		DPRINTK("Fail to write device addr\n");
		return ERROR_CODE_READ_ADDR;
	}
	if(I2c_write_byte(sub_addr)) {
		I2c_stop();
		DPRINTK("Fail to write sub addr\n");
		return ERROR_CODE_READ_ADDR;
	}
	I2c_start();
	I2C_DELAY;
	if(I2c_write_byte(device_addr+1)) {
		I2c_stop();
		DPRINTK("Fail to write device addr+1\n");
		return ERROR_CODE_READ_ADDR;
	}
	for(i = 0; i<ByteNo; i++) buff[i] = I2c_read_byte();
	I2C_DELAY;
	I2C_DELAY_LONG;
	I2c_stop();
	I2C_DELAY_LONG;
	return ERROR_CODE_TRUE;
}

int PixI2cWrite(unsigned char device_addr, unsigned char sub_addr, void *ptr_data, unsigned int ui_data_length)
{
	unsigned char uc_return;
	if((uc_return = I2c_write(device_addr, sub_addr, (unsigned char*)ptr_data, ui_data_length))!=ERROR_CODE_TRUE)
	{
		DPRINTK("[Write]error code = %d\n",uc_return);
		return -1;
	}
	return 0;
}

int PixI2cRead(unsigned char device_addr, unsigned char sub_addr, void *ptr_data, unsigned int ui_data_length)
{
	unsigned char uc_return;
	if((uc_return=I2c_read(device_addr, sub_addr, (unsigned char*)ptr_data, ui_data_length))!=ERROR_CODE_TRUE)
	{
		DPRINTK("[Read]error code = %d\n",uc_return);
		return -1;
	}
	return 0;
}

//static int PixI2c_Ioctl(struct inode *inode, struct file *fp, unsigned int cmd, unsigned long arg)
static int PixI2c_Ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	PPIX_IIC_DATA ptr_user_data;
	unsigned int ui_data_length;
	unsigned char uc_device_addr;
	unsigned char uc_sub_addr;
	void *ptr_data=NULL;
	int i_return=0;

	switch(cmd)
    {
    case IOCTL_PIX_I2C_INIT:
        InitGPIO();
        break;
    case IOCTL_PIX_I2C_TERMINATE:
        ReleaseGPIO();
        break;
    case IOCTL_PIX_I2C_READ:
		ptr_user_data = (PPIX_IIC_DATA)arg;
		if((i_return=get_user(uc_device_addr, &ptr_user_data->device_addr))<0)
		{
			goto ERROR_IOCTL;
		}
		if((i_return=get_user(uc_sub_addr, &ptr_user_data->sub_addr))<0)
		{
			goto ERROR_IOCTL;
		}
		if((i_return=get_user(ui_data_length, &ptr_user_data->ui_data_length))<0)
		{
			goto ERROR_IOCTL;
		}
		ptr_data = kmalloc(ui_data_length,GFP_KERNEL);
		if((i_return=PixI2cRead(uc_device_addr, uc_sub_addr, ptr_data, ui_data_length))<0)
			goto ERROR_IOCTL;
		if((i_return=copy_to_user((void*)ptr_user_data->ptr_data, ptr_data, ui_data_length))<0)
			goto ERROR_IOCTL;
		break;
	case IOCTL_PIX_I2C_WRITE:
		ptr_user_data = (PPIX_IIC_DATA)arg;
		if((i_return=get_user(uc_device_addr, &ptr_user_data->device_addr))<0)
			goto ERROR_IOCTL;
		if((i_return=get_user(uc_sub_addr, &ptr_user_data->sub_addr))<0)
			goto ERROR_IOCTL;
		if((i_return=get_user(ui_data_length, &ptr_user_data->ui_data_length))<0)
			goto ERROR_IOCTL;
		ptr_data = kmalloc(ui_data_length,GFP_KERNEL);
		if((i_return=copy_from_user(ptr_data, (const void*)ptr_user_data->ptr_data, ui_data_length))<0)
			goto ERROR_IOCTL;
		if((i_return=PixI2cWrite(uc_device_addr, uc_sub_addr, ptr_data, ui_data_length))<0)
			goto ERROR_IOCTL;
		break;
	default:	i_return=-EINVAL; break;
    }
ERROR_IOCTL:
	KERNEL_FREE(ptr_data);
	return i_return;
}

static int PixI2c_Open(struct inode *pInode, struct file *pFile)
{
	int i_return = -ENODEV;
	int iNum= MINOR(pInode->i_rdev);

	if(!atomic_dec_and_test(&scull_available))
	{
		atomic_inc(&scull_available);
		return -EBUSY;
	}
	if(!try_module_get(THIS_MODULE))
	{
		DPRINTK("Failed iic driver for pixtree\n");
		goto OUT;
	}
	i_return = 0;
    DPRINTK("open iic driver for pixtree minor : %d\n",iNum);
OUT:
	return i_return;
}

static int PixI2c_Release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
    DPRINTK("release iic driver for pixtree\n");
	atomic_inc(&scull_available);
	return 0;
}

static const struct file_operations pix_iic_fops=
{
  .owner    = THIS_MODULE,
  .open     = PixI2c_Open,
  .unlocked_ioctl    = PixI2c_Ioctl,
  .release	= PixI2c_Release,
};
static struct miscdevice pix_i2c_dev={
		.minor	= PIX_I2C_MINOR,
		.name	= "pix_i2c",
		.fops	= &pix_iic_fops,
};
int __init PixI2c_Init(void)
{
    int nResult;
    nResult = misc_register(&pix_i2c_dev);
    if(nResult)
	{
			DPRINTK("[init PixI2c_Init]fail to regist dev...................!!!!!!!!!!!!!!!!!\n");
			return nResult;
	}
	InitGPIO();

	s5p_gpio_set_pd_cfg(PIN_SDA, S3C_GPIO_SPECIAL(2));
	s5p_gpio_set_pd_cfg(PIN_SCL, S3C_GPIO_SPECIAL(2));

	s5p_gpio_set_pd_pull(PIN_SDA, S3C_GPIO_PULL_NONE);
   	s5p_gpio_set_pd_pull(PIN_SCL, S3C_GPIO_PULL_NONE);

    return 0;
}

void __exit PixI2c_Exit(void)
{
    misc_deregister(&pix_i2c_dev);
}

#ifndef MODULE
__initcall(PixI2c_Init);
#else
module_init(PixI2c_Init);
module_exit(PixI2c_Exit);
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Pixtree, Inc.");
MODULE_DESCRIPTION("for IIC communication");
