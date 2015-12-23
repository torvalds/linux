#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

#include "../aeolia.h"

#define ICC_MAX_READ_DATA 0xff
#define ICC_MAX_WRITE_DATA 0xf8

/* This is actually multiple nested variable length structures, but since we
 * currently only support one op per transaction, we hardcode it. */
struct icc_i2c_msg {
	/* Header */
	u8 code;
	u16 length;
	u8 count;
	struct {
		u8 major;
		u8 length;
		u8 minor;
		u8 count;
		struct {
			u8 length;
			u8 slave_addr;
			u8 reg_addr;
			u8 data[ICC_MAX_WRITE_DATA];
		} xfer;
	} cmd;
} __packed;

static int icc_i2c_smbus_xfer(struct i2c_adapter *adapter, u16 addr,
			  unsigned short flags,
			  char read_write, u8 command, int size,
			  union i2c_smbus_data *data)
{
	struct apcie_dev *sc = i2c_get_adapdata(adapter);
	int ret;
	struct icc_i2c_msg msg;
	u8 resultbuf[8 + ICC_MAX_READ_DATA];

	msg.code = 4; /* Don't really know what this is */
	msg.count = 1;
	msg.cmd.count = 1;
	msg.cmd.xfer.slave_addr = addr << 1;
	msg.cmd.xfer.reg_addr = command;
	if (read_write == I2C_SMBUS_READ) {
		msg.cmd.major = 1;
		msg.cmd.minor = 1;
		msg.cmd.length = 8;
		msg.cmd.xfer.data[0] = 0; /* unknown */
	} else {
		msg.cmd.major = 2;
		msg.cmd.minor = 2;
	}

	switch (size) {
	case I2C_SMBUS_BYTE_DATA:
		msg.cmd.xfer.length = 1;
		if (read_write == I2C_SMBUS_WRITE) {
			msg.cmd.length = 8;
			msg.cmd.xfer.data[0] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		msg.cmd.xfer.length = 2;
		if (read_write == I2C_SMBUS_WRITE) {
			msg.cmd.length = 9;
			msg.cmd.xfer.data[0] = data->word & 0xff;
			msg.cmd.xfer.data[1] = data->word >> 8;
		}
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		msg.cmd.xfer.length = data->block[0];
		if (read_write == I2C_SMBUS_WRITE) {
			if (data->block[0] > ICC_MAX_WRITE_DATA) {
				sc_err("icc-i2c: transaction too large: %d\n",
				       data->block[0]);
				return -E2BIG;
			}
			msg.cmd.length = 7 + data->block[0];
			memcpy(msg.cmd.xfer.data, &data->block[1],
			       data->block[0]);
		}
		break;
	default:
		sc_err("icc-i2c: unsupported transaction %d\n", size);
		return -ENOTSUPP;
	}

	msg.length = msg.cmd.length + 4;
	ret = apcie_icc_cmd(0x10, 0x0, &msg, msg.length, resultbuf,
		      sizeof(resultbuf));
	if (ret < 2 || ret > sizeof(resultbuf)) {
		sc_err("icc-i2c: icc command failed: %d\n", ret);
		return -EIO;
	}
	if (resultbuf[0] != 0 || resultbuf[1] != 0) {
		sc_err("icc-i2c: i2c command failed: %d, %d\n",
		       resultbuf[0], resultbuf[1]);
		return -EIO;
	}

	if (read_write == I2C_SMBUS_READ)
		switch (size) {
		case I2C_SMBUS_BYTE_DATA:
			data->byte = resultbuf[8];
			break;
		case I2C_SMBUS_WORD_DATA:
			data->word = resultbuf[8] | (resultbuf[9] << 8);
			break;
		case I2C_SMBUS_I2C_BLOCK_DATA:
			memcpy(&data->block[1], &resultbuf[8],
			       data->block[0]);
			break;
		}

	return 0;
}

u32 icc_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

static const struct i2c_algorithm icc_i2c_algo = {
	.smbus_xfer   = &icc_i2c_smbus_xfer,
	.functionality = &icc_i2c_functionality,
};


int icc_i2c_init(struct apcie_dev *sc)
{
	struct i2c_adapter *i2c;
	int ret;

	i2c = &sc->icc.i2c;
	i2c->owner = THIS_MODULE;
	i2c->algo = &icc_i2c_algo;
	i2c->algo_data = NULL;
	i2c->dev.parent = &sc->pdev->dev;
	strlcpy(i2c->name, "icc", sizeof(i2c->name));
	i2c_set_adapdata(i2c, sc);
	ret = i2c_add_adapter(i2c);
	if (ret < 0) {
		sc_err("failed to add i2c adapter\n");
		return ret;
	}
	return 0;
}

void icc_i2c_remove(struct apcie_dev *sc)
{
	i2c_del_adapter(&sc->icc.i2c);
}
