/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-i2c.c - flexcop internal 2Wire bus (I2C) and dvb i2c initialization
 *
 * see flexcop.c for copyright information.
 */
#include "flexcop.h"

#define FC_MAX_I2C_RETRIES 100000

static int flexcop_i2c_operation(struct flexcop_device *fc, flexcop_ibi_value *r100)
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
			if (r.tw_sm_c_100.st_done) {  /* && !r.tw_sm_c_100.working_start */
				*r100 = r;
				deb_i2c("i2c success\n");
				return 0;
			}
		} else {
			deb_i2c("suffering from an i2c ack_error\n");
			return -EREMOTEIO;
		}
	}
	deb_i2c("tried %d times i2c operation, never finished or too many ack errors.\n",i);
	return -EREMOTEIO;
}

static int flexcop_i2c_read4(struct flexcop_device *fc, flexcop_ibi_value r100, u8 *buf)
{
	flexcop_ibi_value r104;
	int len = r100.tw_sm_c_100.total_bytes, /* remember total_bytes is buflen-1 */
		ret;

	if ((ret = flexcop_i2c_operation(fc,&r100)) != 0) {
		/* The Cablestar needs a different kind of i2c-transfer (does not
		 * support "Repeat Start"):
		 * wait for the ACK failure,
		 * and do a subsequent read with the Bit 30 enabled
		 */
		r100.tw_sm_c_100.no_base_addr_ack_error = 1;
		if ((ret = flexcop_i2c_operation(fc,&r100)) != 0) {
			deb_i2c("no_base_addr read failed. %d\n",ret);
			return ret;
		}
	}

	buf[0] = r100.tw_sm_c_100.data1_reg;

	if (len > 0) {
		r104 = fc->read_ibi_reg(fc,tw_sm_c_104);
		deb_i2c("read: r100: %08x, r104: %08x\n",r100.raw,r104.raw);

		/* there is at least one more byte, otherwise we wouldn't be here */
		buf[1] = r104.tw_sm_c_104.data2_reg;
		if (len > 1) buf[2] = r104.tw_sm_c_104.data3_reg;
		if (len > 2) buf[3] = r104.tw_sm_c_104.data4_reg;
	}

	return 0;
}

static int flexcop_i2c_write4(struct flexcop_device *fc, flexcop_ibi_value r100, u8 *buf)
{
	flexcop_ibi_value r104;
	int len = r100.tw_sm_c_100.total_bytes; /* remember total_bytes is buflen-1 */
	r104.raw = 0;

	/* there is at least one byte, otherwise we wouldn't be here */
	r100.tw_sm_c_100.data1_reg = buf[0];

	r104.tw_sm_c_104.data2_reg = len > 0 ? buf[1] : 0;
	r104.tw_sm_c_104.data3_reg = len > 1 ? buf[2] : 0;
	r104.tw_sm_c_104.data4_reg = len > 2 ? buf[3] : 0;

	deb_i2c("write: r100: %08x, r104: %08x\n",r100.raw,r104.raw);

	/* write the additional i2c data before doing the actual i2c operation */
	fc->write_ibi_reg(fc,tw_sm_c_104,r104);
	return flexcop_i2c_operation(fc,&r100);
}

int flexcop_i2c_request(struct flexcop_device *fc, flexcop_access_op_t op,
		flexcop_i2c_port_t port, u8 chipaddr, u8 addr, u8 *buf, u16 len)
{
	int ret;
	u16 bytes_to_transfer;
	flexcop_ibi_value r100;

	deb_i2c("op = %d\n",op);
	r100.raw = 0;
	r100.tw_sm_c_100.chipaddr = chipaddr;
	r100.tw_sm_c_100.twoWS_rw = op;
	r100.tw_sm_c_100.twoWS_port_reg = port;

	while (len != 0) {
		bytes_to_transfer = len > 4 ? 4 : len;

		r100.tw_sm_c_100.total_bytes = bytes_to_transfer - 1;
		r100.tw_sm_c_100.baseaddr = addr;

		if (op == FC_READ)
			ret = flexcop_i2c_read4(fc, r100, buf);
		else
			ret = flexcop_i2c_write4(fc,r100, buf);

		if (ret < 0)
			return ret;

		buf  += bytes_to_transfer;
		addr += bytes_to_transfer;
		len  -= bytes_to_transfer;
	};

	return 0;
}
/* exported for PCI i2c */
EXPORT_SYMBOL(flexcop_i2c_request);

/* master xfer callback for demodulator */
static int flexcop_master_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msgs[], int num)
{
	struct flexcop_device *fc = i2c_get_adapdata(i2c_adap);
	int i, ret = 0;

	/* Some drivers use 1 byte or 0 byte reads as probes, which this
	 * driver doesn't support.  These probes will always fail, so this
	 * hack makes them always succeed.  If one knew how, it would of
	 * course be better to actually do the read.  */
	if (num == 1 && msgs[0].flags == I2C_M_RD && msgs[0].len <= 1)
		return 1;

	if (mutex_lock_interruptible(&fc->i2c_mutex))
		return -ERESTARTSYS;

	/* reading */
	if (num == 2 &&
		msgs[0].flags == 0 &&
		msgs[1].flags == I2C_M_RD &&
		msgs[0].buf != NULL &&
		msgs[1].buf != NULL) {

		ret = fc->i2c_request(fc, FC_READ, FC_I2C_PORT_DEMOD, msgs[0].addr, msgs[0].buf[0], msgs[1].buf, msgs[1].len);

	} else for (i = 0; i < num; i++) { /* writing command */
		if (msgs[i].flags != 0 || msgs[i].buf == NULL || msgs[i].len < 2) {
			ret = -EINVAL;
			break;
		}

		ret = fc->i2c_request(fc, FC_WRITE, FC_I2C_PORT_DEMOD, msgs[i].addr, msgs[i].buf[0], &msgs[i].buf[1], msgs[i].len - 1);
	}

	if (ret < 0)
		err("i2c master_xfer failed");
	else
		ret = num;

	mutex_unlock(&fc->i2c_mutex);

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

	memset(&fc->i2c_adap, 0, sizeof(struct i2c_adapter));
	strncpy(fc->i2c_adap.name, "B2C2 FlexCop device",
		sizeof(fc->i2c_adap.name));

	i2c_set_adapdata(&fc->i2c_adap,fc);

	fc->i2c_adap.class	    = I2C_CLASS_TV_DIGITAL;
	fc->i2c_adap.algo       = &flexcop_algo;
	fc->i2c_adap.algo_data  = NULL;
	fc->i2c_adap.dev.parent	= fc->dev;

	if ((ret = i2c_add_adapter(&fc->i2c_adap)) < 0)
		return ret;

	fc->init_state |= FC_STATE_I2C_INIT;
	return 0;
}

void flexcop_i2c_exit(struct flexcop_device *fc)
{
	if (fc->init_state & FC_STATE_I2C_INIT)
		i2c_del_adapter(&fc->i2c_adap);

	fc->init_state &= ~FC_STATE_I2C_INIT;
}
