#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/types.h>

#include "drx_driver.h"
#include "bsp_types.h"
#include "bsp_tuner.h"
#include "drx39xxj.h"

/* Dummy function to satisfy drxj.c */
DRXStatus_t DRXBSP_TUNER_Open(pTUNERInstance_t tuner)
{
	return DRX_STS_OK;
}

DRXStatus_t DRXBSP_TUNER_Close(pTUNERInstance_t tuner)
{
	return DRX_STS_OK;
}

DRXStatus_t DRXBSP_TUNER_SetFrequency(pTUNERInstance_t tuner,
				      TUNERMode_t mode,
				      s32 centerFrequency)
{
	return DRX_STS_OK;
}

DRXStatus_t
DRXBSP_TUNER_GetFrequency(pTUNERInstance_t tuner,
			  TUNERMode_t mode,
			  s32 *RFfrequency,
			  s32 *IFfrequency)
{
	return DRX_STS_OK;
}

DRXStatus_t DRXBSP_HST_Sleep(u32 n)
{
	msleep(n);
	return DRX_STS_OK;
}

u32 DRXBSP_HST_Clock(void)
{
	return jiffies_to_msecs(jiffies);
}

int DRXBSP_HST_Memcmp(void *s1, void *s2, u32 n)
{
	return (memcmp(s1, s2, (size_t) n));
}

void *DRXBSP_HST_Memcpy(void *to, void *from, u32 n)
{
	return (memcpy(to, from, (size_t) n));
}

DRXStatus_t DRXBSP_I2C_WriteRead(struct i2c_device_addr *wDevAddr,
				 u16 wCount,
				 u8 *wData,
				 struct i2c_device_addr *rDevAddr,
				 u16 rCount, u8 *rData)
{
	struct drx39xxj_state *state;
	struct i2c_msg msg[2];
	unsigned int num_msgs;

	if (wDevAddr == NULL) {
		/* Read only */
		state = rDevAddr->userData;
		msg[0].addr = rDevAddr->i2cAddr >> 1;
		msg[0].flags = I2C_M_RD;
		msg[0].buf = rData;
		msg[0].len = rCount;
		num_msgs = 1;
	} else if (rDevAddr == NULL) {
		/* Write only */
		state = wDevAddr->userData;
		msg[0].addr = wDevAddr->i2cAddr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = wCount;
		num_msgs = 1;
	} else {
		/* Both write and read */
		state = wDevAddr->userData;
		msg[0].addr = wDevAddr->i2cAddr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = wCount;
		msg[1].addr = rDevAddr->i2cAddr >> 1;
		msg[1].flags = I2C_M_RD;
		msg[1].buf = rData;
		msg[1].len = rCount;
		num_msgs = 2;
	}

	if (state->i2c == NULL) {
		printk("i2c was zero, aborting\n");
		return 0;
	}
	if (i2c_transfer(state->i2c, msg, num_msgs) != num_msgs) {
		printk(KERN_WARNING "drx3933: I2C write/read failed\n");
		return -EREMOTEIO;
	}

	return DRX_STS_OK;

#ifdef DJH_DEBUG
	struct drx39xxj_state *state = wDevAddr->userData;

	struct i2c_msg msg[2] = {
		{.addr = wDevAddr->i2cAddr,
		 .flags = 0,.buf = wData,.len = wCount},
		{.addr = rDevAddr->i2cAddr,
		 .flags = I2C_M_RD,.buf = rData,.len = rCount},
	};

	printk("drx3933 i2c operation addr=%x i2c=%p, wc=%x rc=%x\n",
	       wDevAddr->i2cAddr, state->i2c, wCount, rCount);

	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "drx3933: I2C write/read failed\n");
		return -EREMOTEIO;
	}
#endif
	return 0;
}
