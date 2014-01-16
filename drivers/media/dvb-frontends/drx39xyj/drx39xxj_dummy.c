#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/types.h>

#include "drx_driver.h"
#include "drx39xxj.h"

/* Dummy function to satisfy drxj.c */
int drxbsp_tuner_open(struct tuner_instance *tuner)
{
	return DRX_STS_OK;
}

int drxbsp_tuner_close(struct tuner_instance *tuner)
{
	return DRX_STS_OK;
}

int drxbsp_tuner_set_frequency(struct tuner_instance *tuner,
				      u32 mode,
				      s32 center_frequency)
{
	return DRX_STS_OK;
}

int
drxbsp_tuner_get_frequency(struct tuner_instance *tuner,
			  u32 mode,
			  s32 *r_ffrequency,
			  s32 *i_ffrequency)
{
	return DRX_STS_OK;
}

int drxbsp_hst_sleep(u32 n)
{
	msleep(n);
	return DRX_STS_OK;
}

u32 drxbsp_hst_clock(void)
{
	return jiffies_to_msecs(jiffies);
}

int drxbsp_hst_memcmp(void *s1, void *s2, u32 n)
{
	return (memcmp(s1, s2, (size_t) n));
}

void *drxbsp_hst_memcpy(void *to, void *from, u32 n)
{
	return (memcpy(to, from, (size_t) n));
}

int drxbsp_i2c_write_read(struct i2c_device_addr *w_dev_addr,
				 u16 w_count,
				 u8 *wData,
				 struct i2c_device_addr *r_dev_addr,
				 u16 r_count, u8 *r_data)
{
	struct drx39xxj_state *state;
	struct i2c_msg msg[2];
	unsigned int num_msgs;

	if (w_dev_addr == NULL) {
		/* Read only */
		state = r_dev_addr->user_data;
		msg[0].addr = r_dev_addr->i2c_addr >> 1;
		msg[0].flags = I2C_M_RD;
		msg[0].buf = r_data;
		msg[0].len = r_count;
		num_msgs = 1;
	} else if (r_dev_addr == NULL) {
		/* Write only */
		state = w_dev_addr->user_data;
		msg[0].addr = w_dev_addr->i2c_addr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = w_count;
		num_msgs = 1;
	} else {
		/* Both write and read */
		state = w_dev_addr->user_data;
		msg[0].addr = w_dev_addr->i2c_addr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = w_count;
		msg[1].addr = r_dev_addr->i2c_addr >> 1;
		msg[1].flags = I2C_M_RD;
		msg[1].buf = r_data;
		msg[1].len = r_count;
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
	struct drx39xxj_state *state = w_dev_addr->user_data;

	struct i2c_msg msg[2] = {
		{.addr = w_dev_addr->i2c_addr,
		 .flags = 0, .buf = wData, .len = w_count},
		{.addr = r_dev_addr->i2c_addr,
		 .flags = I2C_M_RD, .buf = r_data, .len = r_count},
	};

	printk("drx3933 i2c operation addr=%x i2c=%p, wc=%x rc=%x\n",
	       w_dev_addr->i2c_addr, state->i2c, w_count, r_count);

	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "drx3933: I2C write/read failed\n");
		return -EREMOTEIO;
	}
#endif
	return 0;
}
