// SPDX-License-Identifier: GPL-2.0
/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-i2c.c - flexcop internal 2Wire bus (I2C) and dvb i2c initialization
 * see flexcop.c for copyright information
 */
#include "flexcop.h"

#define FC_MAX_I2C_RETRIES 100000

static int flexcop_i2c_operation(struct flexcop_device *fc,
		flexcop_ibi_value *r100)
{
	int i;
	flexcop_ibi_value r;

	r100->tw_sm_c_100.working_start = 1;
	deb_i2c("r100 before: %08x\n",r100->raw);

	fc->write_ibi_reg(fc, tw_sm_c_100, ibi_zero);
	fc->write_ibi_reg(fc, tw_sm_c_100, *r100); /* initiating i2c operation */

	for (i = 0; i < FC_MAX_I2C_RETRIES; i++) {
		r = fc->read_ibi_reg(fc, tw_sm_c_100);

		if (!r.tw_sm_c_100.no_base_addr_ack_error) {
			if (r.tw_sm_c_100.st_done) {
				*r100 = r;
				deb_i2c("i2c success\n");
				return 0;
			}
		} else {
			deb_i2c("suffering from an i2c ack_error\n");
			return -EREMOTEIO;
		}
	}
	deb_i2c("tried %d times i2c operation, never finished or too many ack errors.\n",
		i);
	return -EREMOTEIO;
}

static int flexcop_i2c_read4(struct flexcop_i2c_adapter *i2c,
		flexcop_ibi_value r100, u8 *buf)
{
	flexcop_ibi_value r104;
	int len = r100.tw_sm_c_100.total_bytes,
		/* remember total_bytes is buflen-1 */
		ret;

	/* work-around to have CableStar2 and SkyStar2 rev 2.7 work
	 * correctly:
	 *
	 * the ITD1000 is behind an i2c-gate which closes automatically
	 * after an i2c-transaction the STV0297 needs 2 consecutive reads
	 * one with no_base_addr = 0 and one with 1
	 *
	 * those two work-arounds are conflictin: we check for the card
	 * type, it is set when probing the ITD1000 */
	if (i2c->fc->dev_type == FC_SKY_REV27)
		r100.tw_sm_c_100.no_base_addr_ack_error = i2c->no_base_addr;

	ret = flexcop_i2c_operation(i2c->fc, &r100);
	if (ret != 0) {
		deb_i2c("Retrying operation\n");
		r100.tw_sm_c_100.no_base_addr_ack_error = i2c->no_base_addr;
		ret = flexcop_i2c_operation(i2c->fc, &r100);
	}
	if (ret != 0) {
		deb_i2c("read failed. %d\n", ret);
		return ret;
	}

	buf[0] = r100.tw_sm_c_100.data1_reg;

	if (len > 0) {
		r104 = i2c->fc->read_ibi_reg(i2c->fc, tw_sm_c_104);
		deb_i2c("read: r100: %08x, r104: %08x\n", r100.raw, r104.raw);

		/* there is at least one more byte, otherwise we wouldn't be here */
		buf[1] = r104.tw_sm_c_104.data2_reg;
		if (len > 1) buf[2] = r104.tw_sm_c_104.data3_reg;
		if (len > 2) buf[3] = r104.tw_sm_c_104.data4_reg;
	}
	return 0;
}

static int flexcop_i2c_write4(struct flexcop_device *fc,
		flexcop_ibi_value r100, u8 *buf)
{
	flexcop_ibi_value r104;
	int len = r100.tw_sm_c_100.total_bytes; /* remember total_bytes is buflen-1 */
	r104.raw = 0;

	/* there is at least one byte, otherwise we wouldn't be here */
	r100.tw_sm_c_100.data1_reg = buf[0];
	r104.tw_sm_c_104.data2_reg = len > 0 ? buf[1] : 0;
	r104.tw_sm_c_104.data3_reg = len > 1 ? buf[2] : 0;
	r104.tw_sm_c_104.data4_reg = len > 2 ? buf[3] : 0;

	deb_i2c("write: r100: %08x, r104: %08x\n", r100.raw, r104.raw);

	/* write the additional i2c data before doing the actual i2c operation */
	fc->write_ibi_reg(fc, tw_sm_c_104, r104);
	return flexcop_i2c_operation(fc, &r100);
}

int flexcop_i2c_request(struct flexcop_i2c_adapter *i2c,
		flexcop_access_op_t op, u8 chipaddr, u8 addr, u8 *buf, u16 len)
{
	int ret;

#ifdef DUMP_I2C_MESSAGES
	int i;
#endif

	u16 bytes_to_transfer;
	flexcop_ibi_value r100;

	deb_i2c("op = %d\n",op);
	r100.raw = 0;
	r100.tw_sm_c_100.chipaddr = chipaddr;
	r100.tw_sm_c_100.twoWS_rw = op;
	r100.tw_sm_c_100.twoWS_port_reg = i2c->port;

#ifdef DUMP_I2C_MESSAGES
	printk(KERN_DEBUG "%d ", i2c->port);
	if (op == FC_READ)
		printk(KERN_CONT "rd(");
	else
		printk(KERN_CONT "wr(");
	printk(KERN_CONT "%02x): %02x ", chipaddr, addr);
#endif

	/* in that case addr is the only value ->
	 * we write it twice as baseaddr and val0
	 * BBTI is doing it like that for ISL6421 at least */
	if (i2c->no_base_addr && len == 0 && op == FC_WRITE) {
		buf = &addr;
		len = 1;
	}

	while (len != 0) {
		bytes_to_transfer = len > 4 ? 4 : len;

		r100.tw_sm_c_100.total_bytes = bytes_to_transfer - 1;
		r100.tw_sm_c_100.baseaddr = addr;

		if (op == FC_READ)
			ret = flexcop_i2c_read4(i2c, r100, buf);
		else
			ret = flexcop_i2c_write4(i2c->fc, r100, buf);

#ifdef DUMP_I2C_MESSAGES
		for (i = 0; i < bytes_to_transfer; i++)
			printk(KERN_CONT "%02x ", buf[i]);
#endif

		if (ret < 0)
			return ret;

		buf  += bytes_to_transfer;
		addr += bytes_to_transfer;
		len  -= bytes_to_transfer;
	}

#ifdef DUMP_I2C_MESSAGES
	printk(KERN_CONT "\n");
#endif

	return 0;
}
/* exported for PCI i2c */
EXPORT_SYMBOL(flexcop_i2c_request);

/* master xfer callback for demodulator */
static int flexcop_master_xfer(struct i2c_adapter *i2c_adap,
		struct i2c_msg msgs[], int num)
{
	struct flexcop_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	int i, ret = 0;

	/* Some drivers use 1 byte or 0 byte reads as probes, which this
	 * driver doesn't support.  These probes will always fail, so this
	 * hack makes them always succeed.  If one knew how, it would of
	 * course be better to actually do the read.  */
	if (num == 1 && msgs[0].flags == I2C_M_RD && msgs[0].len <= 1)
		return 1;

	if (mutex_lock_interruptible(&i2c->fc->i2c_mutex))
		return -ERESTARTSYS;

	for (i = 0; i < num; i++) {
		/* reading */
		if (i+1 < num && (msgs[i+1].flags == I2C_M_RD)) {
			ret = i2c->fc->i2c_request(i2c, FC_READ, msgs[i].addr,
					msgs[i].buf[0], msgs[i+1].buf,
					msgs[i+1].len);
			i++; /* skip the following message */
		} else /* writing */
			ret = i2c->fc->i2c_request(i2c, FC_WRITE, msgs[i].addr,
					msgs[i].buf[0], &msgs[i].buf[1],
					msgs[i].len - 1);
		if (ret < 0) {
			deb_i2c("i2c master_xfer failed");
			break;
		}
	}

	mutex_unlock(&i2c->fc->i2c_mutex);

	if (ret == 0)
		ret = num;
	return ret;
}

static u32 flexcop_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm flexcop_algo = {
	.master_xfer	= flexcop_master_xfer,
	.functionality	= flexcop_i2c_func,
};

int flexcop_i2c_init(struct flexcop_device *fc)
{
	int ret;
	mutex_init(&fc->i2c_mutex);

	fc->fc_i2c_adap[0].fc = fc;
	fc->fc_i2c_adap[1].fc = fc;
	fc->fc_i2c_adap[2].fc = fc;
	fc->fc_i2c_adap[0].port = FC_I2C_PORT_DEMOD;
	fc->fc_i2c_adap[1].port = FC_I2C_PORT_EEPROM;
	fc->fc_i2c_adap[2].port = FC_I2C_PORT_TUNER;

	strlcpy(fc->fc_i2c_adap[0].i2c_adap.name, "B2C2 FlexCop I2C to demod",
			sizeof(fc->fc_i2c_adap[0].i2c_adap.name));
	strlcpy(fc->fc_i2c_adap[1].i2c_adap.name, "B2C2 FlexCop I2C to eeprom",
			sizeof(fc->fc_i2c_adap[1].i2c_adap.name));
	strlcpy(fc->fc_i2c_adap[2].i2c_adap.name, "B2C2 FlexCop I2C to tuner",
			sizeof(fc->fc_i2c_adap[2].i2c_adap.name));

	i2c_set_adapdata(&fc->fc_i2c_adap[0].i2c_adap, &fc->fc_i2c_adap[0]);
	i2c_set_adapdata(&fc->fc_i2c_adap[1].i2c_adap, &fc->fc_i2c_adap[1]);
	i2c_set_adapdata(&fc->fc_i2c_adap[2].i2c_adap, &fc->fc_i2c_adap[2]);

	fc->fc_i2c_adap[0].i2c_adap.algo =
		fc->fc_i2c_adap[1].i2c_adap.algo =
		fc->fc_i2c_adap[2].i2c_adap.algo = &flexcop_algo;
	fc->fc_i2c_adap[0].i2c_adap.algo_data =
		fc->fc_i2c_adap[1].i2c_adap.algo_data =
		fc->fc_i2c_adap[2].i2c_adap.algo_data = NULL;
	fc->fc_i2c_adap[0].i2c_adap.dev.parent =
		fc->fc_i2c_adap[1].i2c_adap.dev.parent =
		fc->fc_i2c_adap[2].i2c_adap.dev.parent = fc->dev;

	ret = i2c_add_adapter(&fc->fc_i2c_adap[0].i2c_adap);
	if (ret < 0)
		return ret;

	ret = i2c_add_adapter(&fc->fc_i2c_adap[1].i2c_adap);
	if (ret < 0)
		goto adap_1_failed;

	ret = i2c_add_adapter(&fc->fc_i2c_adap[2].i2c_adap);
	if (ret < 0)
		goto adap_2_failed;

	fc->init_state |= FC_STATE_I2C_INIT;
	return 0;

adap_2_failed:
	i2c_del_adapter(&fc->fc_i2c_adap[1].i2c_adap);
adap_1_failed:
	i2c_del_adapter(&fc->fc_i2c_adap[0].i2c_adap);
	return ret;
}

void flexcop_i2c_exit(struct flexcop_device *fc)
{
	if (fc->init_state & FC_STATE_I2C_INIT) {
		i2c_del_adapter(&fc->fc_i2c_adap[2].i2c_adap);
		i2c_del_adapter(&fc->fc_i2c_adap[1].i2c_adap);
		i2c_del_adapter(&fc->fc_i2c_adap[0].i2c_adap);
	}
	fc->init_state &= ~FC_STATE_I2C_INIT;
}
