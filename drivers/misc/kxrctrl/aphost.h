/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __APHOST_H__
#define __APHOST_H__

#include <linux/string.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <asm-generic/gpio.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/dma-buf.h>

#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/of_device.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/timekeeping.h>
#include <linux/kthread.h>

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/of_irq.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/ch11.h>
#include <linux/usb/hcd.h>
#include <linux/usb/phy.h>

#include <linux/regulator/consumer.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <uapi/linux/sched/types.h>

#include <linux/time.h>
#include <linux/timer.h>

#define MAX_PACK_SIZE 100 
#define MAX_DATA_SIZE 32

typedef struct {
	uint64_t ts;
	uint32_t size;
	uint8_t data[MAX_DATA_SIZE];
} d_packet_t; 


typedef struct {
volatile int8_t c_head;
volatile int8_t p_head;
volatile int8_t packDS;
d_packet_t  data[MAX_PACK_SIZE];
}cp_buffer_t;

typedef enum _requestType_t
{
	getMasterNordicVersionRequest = 1,
	setVibStateRequest,
	bondJoyStickRequest,
	disconnectJoyStickRequest,
	getJoyStickBondStateRequest,
	hostEnterDfuStateRequest,
	getLeftJoyStickProductNameRequest,
	getRightJoyStickProductNameRequest,
	getLeftJoyStickFwVersionRequest,
	getRightJoyStickFwVersionRequest,
	invalidRequest,
}requestType_t;

typedef struct _request_t
{
	struct _requestHead
	{
		unsigned char requestType:7;
		unsigned char needAck:1;  //1:need to ack 0:don't need to ack
	} requestHead;
	unsigned char requestData[3];
}request_t;

typedef struct _acknowledge_t
{
	struct _acknowledgeHead
	{
		unsigned char requestType:7;
		unsigned char ack:1;  //1:ack 0:not ack
	} acknowledgeHead;
	unsigned char acknowledgeData[3];
}acknowledge_t;
#endif //__APHOST_H__
