#include <linux/i2c.h>

#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiBX000: "); printk(args); } } while (0)

static int dibx000_write_word(struct dibx000_i2c_master *mst, u16 reg, u16 val)
{
	u8 b[4] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg = {
		.addr = mst->i2c_addr,.flags = 0,.buf = b,.len = 4
	};
	return i2c_transfer(mst->i2c_adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
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

static u32 dibx000_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int dibx000_i2c_gated_tuner_xfer(struct i2c_adapter *i2c_adap,
					struct i2c_msg msg[], int num)
{
	struct dibx000_i2c_master *mst = i2c_get_adapdata(i2c_adap);
	struct i2c_msg m[2 + num];
	u8 tx_open[4], tx_close[4];

	memset(m, 0, sizeof(struct i2c_msg) * (2 + num));

	dibx000_i2c_select_interface(mst, DIBX000_I2C_INTERFACE_TUNER);

	dibx000_i2c_gate_ctrl(mst, tx_open, msg[0].addr, 1);
	m[0].addr = mst->i2c_addr;
	m[0].buf = tx_open;
	m[0].len = 4;

	memcpy(&m[1], msg, sizeof(struct i2c_msg) * num);

	dibx000_i2c_gate_ctrl(mst, tx_close, 0, 0);
	m[num + 1].addr = mst->i2c_addr;
	m[num + 1].buf = tx_close;
	m[num + 1].len = 4;

	return i2c_transfer(mst->i2c_adap, m, 2 + num) == 2 + num ? num : -EIO;
}

static struct i2c_algorithm dibx000_i2c_gated_tuner_algo = {
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
	default:
		printk(KERN_ERR "DiBX000: incorrect I2C interface selected\n");
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
			    struct i2c_algorithm *algo, const char *name,
			    struct dibx000_i2c_master *mst)
{
	strncpy(i2c_adap->name, name, sizeof(i2c_adap->name));
	i2c_adap->class = I2C_CLASS_TV_DIGITAL, i2c_adap->algo = algo;
	i2c_adap->algo_data = NULL;
	i2c_set_adapdata(i2c_adap, mst);
	if (i2c_add_adapter(i2c_adap) < 0)
		return -ENODEV;
	return 0;
}

int dibx000_init_i2c_master(struct dibx000_i2c_master *mst, u16 device_rev,
			    struct i2c_adapter *i2c_adap, u8 i2c_addr)
{
	u8 tx[4];
	struct i2c_msg m = {.addr = i2c_addr >> 1,.buf = tx,.len = 4 };

	mst->device_rev = device_rev;
	mst->i2c_adap = i2c_adap;
	mst->i2c_addr = i2c_addr >> 1;

	if (device_rev == DIB7000P || device_rev == DIB8000)
		mst->base_reg = 1024;
	else
		mst->base_reg = 768;

	if (i2c_adapter_init
	    (&mst->gated_tuner_i2c_adap, &dibx000_i2c_gated_tuner_algo,
	     "DiBX000 tuner I2C bus", mst) != 0)
		printk(KERN_ERR
		       "DiBX000: could not initialize the tuner i2c_adapter\n");

	/* initialize the i2c-master by closing the gate */
	dibx000_i2c_gate_ctrl(mst, tx, 0, 0);

	return i2c_transfer(i2c_adap, &m, 1) == 1;
}

EXPORT_SYMBOL(dibx000_init_i2c_master);

void dibx000_exit_i2c_master(struct dibx000_i2c_master *mst)
{
	i2c_del_adapter(&mst->gated_tuner_i2c_adap);
}

EXPORT_SYMBOL(dibx000_exit_i2c_master);

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Common function the DiBcom demodulator family");
MODULE_LICENSE("GPL");
