// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/module.h>

#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

static int dibx000_write_word(struct dibx000_i2c_master *mst, u16 reg, u16 val)
{
	int ret;

	if (mutex_lock_interruptible(&mst->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}

	mst->i2c_write_buffer[0] = (reg >> 8) & 0xff;
	mst->i2c_write_buffer[1] = reg & 0xff;
	mst->i2c_write_buffer[2] = (val >> 8) & 0xff;
	mst->i2c_write_buffer[3] = val & 0xff;

	memset(mst->msg, 0, sizeof(struct i2c_msg));
	mst->msg[0].addr = mst->i2c_addr;
	mst->msg[0].flags = 0;
	mst->msg[0].buf = mst->i2c_write_buffer;
	mst->msg[0].len = 4;

	ret = i2c_transfer(mst->i2c_adap, mst->msg, 1) != 1 ? -EREMOTEIO : 0;
	mutex_unlock(&mst->i2c_buffer_lock);

	return ret;
}

static u16 dibx000_read_word(struct dibx000_i2c_master *mst, u16 reg)
{
	u16 ret;

	if (mutex_lock_interruptible(&mst->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	mst->i2c_write_buffer[0] = reg >> 8;
	mst->i2c_write_buffer[1] = reg & 0xff;

	memset(mst->msg, 0, 2 * sizeof(struct i2c_msg));
	mst->msg[0].addr = mst->i2c_addr;
	mst->msg[0].flags = 0;
	mst->msg[0].buf = mst->i2c_write_buffer;
	mst->msg[0].len = 2;
	mst->msg[1].addr = mst->i2c_addr;
	mst->msg[1].flags = I2C_M_RD;
	mst->msg[1].buf = mst->i2c_read_buffer;
	mst->msg[1].len = 2;

	if (i2c_transfer(mst->i2c_adap, mst->msg, 2) != 2)
		dprintk("i2c read error on %d\n", reg);

	ret = (mst->i2c_read_buffer[0] << 8) | mst->i2c_read_buffer[1];
	mutex_unlock(&mst->i2c_buffer_lock);

	return ret;
}

static int dibx000_is_i2c_done(struct dibx000_i2c_master *mst)
{
	int i = 100;
	u16 status;

	while (((status = dibx000_read_word(mst, mst->base_reg + 2)) & 0x0100) == 0 && --i > 0)
		;

	/* i2c timed out */
	if (i == 0)
		return -EREMOTEIO;

	/* no acknowledge */
	if ((status & 0x0080) == 0)
		return -EREMOTEIO;

	return 0;
}

static int dibx000_master_i2c_write(struct dibx000_i2c_master *mst, struct i2c_msg *msg, u8 stop)
{
	u16 data;
	u16 da;
	u16 i;
	u16 txlen = msg->len, len;
	const u8 *b = msg->buf;

	while (txlen) {
		dibx000_read_word(mst, mst->base_reg + 2);

		len = txlen > 8 ? 8 : txlen;
		for (i = 0; i < len; i += 2) {
			data = *b++ << 8;
			if (i+1 < len)
				data |= *b++;
			dibx000_write_word(mst, mst->base_reg, data);
		}
		da = (((u8) (msg->addr))  << 9) |
			(1           << 8) |
			(1           << 7) |
			(0           << 6) |
			(0           << 5) |
			((len & 0x7) << 2) |
			(0           << 1) |
			(0           << 0);

		if (txlen == msg->len)
			da |= 1 << 5; /* start */

		if (txlen-len == 0 && stop)
			da |= 1 << 6; /* stop */

		dibx000_write_word(mst, mst->base_reg+1, da);

		if (dibx000_is_i2c_done(mst) != 0)
			return -EREMOTEIO;
		txlen -= len;
	}

	return 0;
}

static int dibx000_master_i2c_read(struct dibx000_i2c_master *mst, struct i2c_msg *msg)
{
	u16 da;
	u8 *b = msg->buf;
	u16 rxlen = msg->len, len;

	while (rxlen) {
		len = rxlen > 8 ? 8 : rxlen;
		da = (((u8) (msg->addr)) << 9) |
			(1           << 8) |
			(1           << 7) |
			(0           << 6) |
			(0           << 5) |
			((len & 0x7) << 2) |
			(1           << 1) |
			(0           << 0);

		if (rxlen == msg->len)
			da |= 1 << 5; /* start */

		if (rxlen-len == 0)
			da |= 1 << 6; /* stop */
		dibx000_write_word(mst, mst->base_reg+1, da);

		if (dibx000_is_i2c_done(mst) != 0)
			return -EREMOTEIO;

		rxlen -= len;

		while (len) {
			da = dibx000_read_word(mst, mst->base_reg);
			*b++ = (da >> 8) & 0xff;
			len--;
			if (len >= 1) {
				*b++ =  da   & 0xff;
				len--;
			}
		}
	}

	return 0;
}

int dibx000_i2c_set_speed(struct i2c_adapter *i2c_adap, u16 speed)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);

	if (mst->device_rev < DIB7000MC && speed < 235)
		speed = 235;
	return dibx000_write_word(mst, mst->base_reg + 3, (u16)(60000 / speed));

}
EXPORT_SYMBOL(dibx000_i2c_set_speed);

static u32 dibx000_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int dibx000_i2c_select_interface(struct dibx000_i2c_master *mst,
					enum dibx000_i2c_interface intf)
{
	if (mst->device_rev > DIB3000MC && mst->selected_interface != intf) {
		dprintk("selecting interface: %d\n", intf);
		mst->selected_interface = intf;
		return dibx000_write_word(mst, mst->base_reg + 4, intf);
	}
	return 0;
}

static int dibx000_i2c_master_xfer_gpio12(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);
	int msg_index;
	int ret = 0;

	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_GPIO_1_2);
	for (msg_index = 0; msg_index < num; msg_index++) {
		if (msg[msg_index].flags & I2C_M_RD) {
			ret = dibx000_master_i2c_read(mst, &msg[msg_index]);
			if (ret != 0)
				return 0;
		} else {
			ret = dibx000_master_i2c_write(mst, &msg[msg_index], 1);
			if (ret != 0)
				return 0;
		}
	}

	return num;
}

static int dibx000_i2c_master_xfer_gpio34(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);
	int msg_index;
	int ret = 0;

	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_GPIO_3_4);
	for (msg_index = 0; msg_index < num; msg_index++) {
		if (msg[msg_index].flags & I2C_M_RD) {
			ret = dibx000_master_i2c_read(mst, &msg[msg_index]);
			if (ret != 0)
				return 0;
		} else {
			ret = dibx000_master_i2c_write(mst, &msg[msg_index], 1);
			if (ret != 0)
				return 0;
		}
	}

	return num;
}

static const struct i2c_algorithm dibx000_i2c_master_gpio12_xfer_algo = {
	.master_xfer = dibx000_i2c_master_xfer_gpio12,
	.functionality = dibx000_i2c_func,
};

static const struct i2c_algorithm dibx000_i2c_master_gpio34_xfer_algo = {
	.master_xfer = dibx000_i2c_master_xfer_gpio34,
	.functionality = dibx000_i2c_func,
};

static int dibx000_i2c_gate_ctrl(struct dibx000_i2c_master *mst, u8 tx[4],
				 u8 addr, int onoff)
{
	u16 val;


	if (onoff)
		val = addr << 8;	// bit 7 = use master or not, if 0, the gate is open
	else
		val = 1 << 7;

	if (mst->device_rev > DIB7000)
		val <<= 1;

	tx[0] = (((mst->base_reg + 1) >> 8) & 0xff);
	tx[1] = ((mst->base_reg + 1) & 0xff);
	tx[2] = val >> 8;
	tx[3] = val & 0xff;

	return 0;
}

static int dibx000_i2c_gated_gpio67_xfer(struct i2c_adapter *i2c_adap,
					struct i2c_msg msg[], int num)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);
	int ret;

	if (num > 32) {
		dprintk("%s: too much I2C message to be transmitted (%i). Maximum is 32",
			__func__, num);
		return -ENOMEM;
	}

	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_GPIO_6_7);

	if (mutex_lock_interruptible(&mst->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}

	memset(mst->msg, 0, sizeof(struct i2c_msg) * (2 + num));

	/* open the gate */
	dibx000_i2c_gate_ctrl(mst, &mst->i2c_write_buffer[0], msg[0].addr, 1);
	mst->msg[0].addr = mst->i2c_addr;
	mst->msg[0].buf = &mst->i2c_write_buffer[0];
	mst->msg[0].len = 4;

	memcpy(&mst->msg[1], msg, sizeof(struct i2c_msg) * num);

	/* close the gate */
	dibx000_i2c_gate_ctrl(mst, &mst->i2c_write_buffer[4], 0, 0);
	mst->msg[num + 1].addr = mst->i2c_addr;
	mst->msg[num + 1].buf = &mst->i2c_write_buffer[4];
	mst->msg[num + 1].len = 4;

	ret = (i2c_transfer(mst->i2c_adap, mst->msg, 2 + num) == 2 + num ?
			num : -EIO);

	mutex_unlock(&mst->i2c_buffer_lock);
	return ret;
}

static const struct i2c_algorithm dibx000_i2c_gated_gpio67_algo = {
	.master_xfer = dibx000_i2c_gated_gpio67_xfer,
	.functionality = dibx000_i2c_func,
};

static int dibx000_i2c_gated_tuner_xfer(struct i2c_adapter *i2c_adap,
					struct i2c_msg msg[], int num)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);
	int ret;

	if (num > 32) {
		dprintk("%s: too much I2C message to be transmitted (%i). Maximum is 32",
			__func__, num);
		return -ENOMEM;
	}

	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_TUNER);

	if (mutex_lock_interruptible(&mst->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}
	memset(mst->msg, 0, sizeof(struct i2c_msg) * (2 + num));

	/* open the gate */
	dibx000_i2c_gate_ctrl(mst, &mst->i2c_write_buffer[0], msg[0].addr, 1);
	mst->msg[0].addr = mst->i2c_addr;
	mst->msg[0].buf = &mst->i2c_write_buffer[0];
	mst->msg[0].len = 4;

	memcpy(&mst->msg[1], msg, sizeof(struct i2c_msg) * num);

	/* close the gate */
	dibx000_i2c_gate_ctrl(mst, &mst->i2c_write_buffer[4], 0, 0);
	mst->msg[num + 1].addr = mst->i2c_addr;
	mst->msg[num + 1].buf = &mst->i2c_write_buffer[4];
	mst->msg[num + 1].len = 4;

	ret = (i2c_transfer(mst->i2c_adap, mst->msg, 2 + num) == 2 + num ?
			num : -EIO);
	mutex_unlock(&mst->i2c_buffer_lock);
	return ret;
}

static const struct i2c_algorithm dibx000_i2c_gated_tuner_algo = {
	.master_xfer = dibx000_i2c_gated_tuner_xfer,
	.functionality = dibx000_i2c_func,
};

struct i2c_adapter *dibx000_get_i2c_adapter(struct dibx000_i2c_master *mst,
						enum dibx000_i2c_interface intf,
						int gating)
{
	struct i2c_adapter *i2c = NULL;

	switch (intf) {
	case DIBX000_I2C_INTERFACE_TUNER:
		if (gating)
			i2c = &mst->gated_tuner_i2c_adap;
		break;
	case DIBX000_I2C_INTERFACE_GPIO_1_2:
		if (!gating)
			i2c = &mst->master_i2c_adap_gpio12;
		break;
	case DIBX000_I2C_INTERFACE_GPIO_3_4:
		if (!gating)
			i2c = &mst->master_i2c_adap_gpio34;
		break;
	case DIBX000_I2C_INTERFACE_GPIO_6_7:
		if (gating)
			i2c = &mst->master_i2c_adap_gpio67;
		break;
	default:
		pr_err("incorrect I2C interface selected\n");
		break;
	}

	return i2c;
}

EXPORT_SYMBOL(dibx000_get_i2c_adapter);

void dibx000_reset_i2c_master(struct dibx000_i2c_master *mst)
{
	/* initialize the i2c-master by closing the gate */
	u8 tx[4];
	struct i2c_msg m = {.addr = mst->i2c_addr,.buf = tx,.len = 4 };

	dibx000_i2c_gate_ctrl(mst, tx, 0, 0);
	i2c_transfer(mst->i2c_adap, &m, 1);
	mst->selected_interface = 0xff;	// the first time force a select of the I2C
	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_TUNER);
}

EXPORT_SYMBOL(dibx000_reset_i2c_master);

static int i2c_adapter_init(struct i2c_adapter *i2c_adap,
				const struct i2c_algorithm *algo, const char *name,
				struct dibx000_i2c_master *mst)
{
	strscpy(i2c_adap->name, name, sizeof(i2c_adap->name));
	i2c_adap->algo = algo;
	i2c_adap->algo_data = NULL;
	i2c_set_adapdata(i2c_adap, mst);
	if (i2c_add_adapter(i2c_adap) < 0)
		return -ENODEV;
	return 0;
}

int dibx000_init_i2c_master(struct dibx000_i2c_master *mst, u16 device_rev,
				struct i2c_adapter *i2c_adap, u8 i2c_addr)
{
	int ret;

	mutex_init(&mst->i2c_buffer_lock);
	if (mutex_lock_interruptible(&mst->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}
	memset(mst->msg, 0, sizeof(struct i2c_msg));
	mst->msg[0].addr = i2c_addr >> 1;
	mst->msg[0].flags = 0;
	mst->msg[0].buf = mst->i2c_write_buffer;
	mst->msg[0].len = 4;

	mst->device_rev = device_rev;
	mst->i2c_adap = i2c_adap;
	mst->i2c_addr = i2c_addr >> 1;

	if (device_rev == DIB7000P || device_rev == DIB8000)
		mst->base_reg = 1024;
	else
		mst->base_reg = 768;

	mst->gated_tuner_i2c_adap.dev.parent = mst->i2c_adap->dev.parent;
	if (i2c_adapter_init
			(&mst->gated_tuner_i2c_adap, &dibx000_i2c_gated_tuner_algo,
			 "DiBX000 tuner I2C bus", mst) != 0)
		pr_err("could not initialize the tuner i2c_adapter\n");

	mst->master_i2c_adap_gpio12.dev.parent = mst->i2c_adap->dev.parent;
	if (i2c_adapter_init
			(&mst->master_i2c_adap_gpio12, &dibx000_i2c_master_gpio12_xfer_algo,
			 "DiBX000 master GPIO12 I2C bus", mst) != 0)
		pr_err("could not initialize the master i2c_adapter\n");

	mst->master_i2c_adap_gpio34.dev.parent = mst->i2c_adap->dev.parent;
	if (i2c_adapter_init
			(&mst->master_i2c_adap_gpio34, &dibx000_i2c_master_gpio34_xfer_algo,
			 "DiBX000 master GPIO34 I2C bus", mst) != 0)
		pr_err("could not initialize the master i2c_adapter\n");

	mst->master_i2c_adap_gpio67.dev.parent = mst->i2c_adap->dev.parent;
	if (i2c_adapter_init
			(&mst->master_i2c_adap_gpio67, &dibx000_i2c_gated_gpio67_algo,
			 "DiBX000 master GPIO67 I2C bus", mst) != 0)
		pr_err("could not initialize the master i2c_adapter\n");

	/* initialize the i2c-master by closing the gate */
	dibx000_i2c_gate_ctrl(mst, mst->i2c_write_buffer, 0, 0);

	ret = (i2c_transfer(i2c_adap, mst->msg, 1) == 1);
	mutex_unlock(&mst->i2c_buffer_lock);

	return ret;
}

EXPORT_SYMBOL(dibx000_init_i2c_master);

void dibx000_exit_i2c_master(struct dibx000_i2c_master *mst)
{
	i2c_del_adapter(&mst->gated_tuner_i2c_adap);
	i2c_del_adapter(&mst->master_i2c_adap_gpio12);
	i2c_del_adapter(&mst->master_i2c_adap_gpio34);
	i2c_del_adapter(&mst->master_i2c_adap_gpio67);
}
EXPORT_SYMBOL(dibx000_exit_i2c_master);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Common function the DiBcom demodulator family");
MODULE_LICENSE("GPL");
