// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i2c support for Silicon Labs' CP2615 Digital Audio Bridge
 *
 * (c) 2021, Bence Csókás <bence98@sch.bme.hu>
 */

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/usb.h>

/** CP2615 I/O Protocol implementation */

#define CP2615_VID 0x10c4
#define CP2615_PID 0xeac1

#define IOP_EP_IN  0x82
#define IOP_EP_OUT 0x02
#define IOP_IFN 1
#define IOP_ALTSETTING 2

#define MAX_IOP_SIZE 64
#define MAX_IOP_PAYLOAD_SIZE (MAX_IOP_SIZE - 6)
#define MAX_I2C_SIZE (MAX_IOP_PAYLOAD_SIZE - 4)

enum cp2615_iop_msg_type {
	iop_GetAccessoryInfo = 0xD100,
	iop_AccessoryInfo = 0xA100,
	iop_GetPortConfiguration = 0xD203,
	iop_PortConfiguration = 0xA203,
	iop_DoI2cTransfer = 0xD400,
	iop_I2cTransferResult = 0xA400,
	iop_GetSerialState = 0xD501,
	iop_SerialState = 0xA501
};

struct __packed cp2615_iop_msg {
	__be16 preamble, length, msg;
	u8 data[MAX_IOP_PAYLOAD_SIZE];
};

#define PART_ID_A01 0x1400
#define PART_ID_A02 0x1500

struct __packed cp2615_iop_accessory_info {
	__be16 part_id, option_id, proto_ver;
};

struct __packed cp2615_i2c_transfer {
	u8 tag, i2caddr, read_len, write_len;
	u8 data[MAX_I2C_SIZE];
};

/* Possible values for struct cp2615_i2c_transfer_result.status */
enum cp2615_i2c_status {
	/* Writing to the internal EEPROM failed, because it is locked */
	CP2615_CFG_LOCKED = -6,
	/* read_len or write_len out of range */
	CP2615_INVALID_PARAM = -4,
	/* I2C slave did not ACK in time */
	CP2615_TIMEOUT,
	/* I2C bus busy */
	CP2615_BUS_BUSY,
	/* I2C bus error (ie. device NAK'd the request) */
	CP2615_BUS_ERROR,
	CP2615_SUCCESS
};

struct __packed cp2615_i2c_transfer_result {
	u8 tag, i2caddr;
	s8 status;
	u8 read_len;
	u8 data[MAX_I2C_SIZE];
};

static int cp2615_init_iop_msg(struct cp2615_iop_msg *ret, enum cp2615_iop_msg_type msg,
			const void *data, size_t data_len)
{
	if (data_len > MAX_IOP_PAYLOAD_SIZE)
		return -EFBIG;

	if (!ret)
		return -EINVAL;

	ret->preamble = 0x2A2A;
	ret->length = htons(data_len + 6);
	ret->msg = htons(msg);
	if (data && data_len)
		memcpy(&ret->data, data, data_len);
	return 0;
}

static int cp2615_init_i2c_msg(struct cp2615_iop_msg *ret, const struct cp2615_i2c_transfer *data)
{
	return cp2615_init_iop_msg(ret, iop_DoI2cTransfer, data, 4 + data->write_len);
}

/* Translates status codes to Linux errno's */
static int cp2615_check_status(enum cp2615_i2c_status status)
{
	switch (status) {
	case CP2615_SUCCESS:
			return 0;
	case CP2615_BUS_ERROR:
		return -ENXIO;
	case CP2615_BUS_BUSY:
		return -EAGAIN;
	case CP2615_TIMEOUT:
		return -ETIMEDOUT;
	case CP2615_INVALID_PARAM:
		return -EINVAL;
	case CP2615_CFG_LOCKED:
		return -EPERM;
	}
	/* Unknown error code */
	return -EPROTO;
}

/** Driver code */

static int
cp2615_i2c_send(struct usb_interface *usbif, struct cp2615_i2c_transfer *i2c_w)
{
	struct cp2615_iop_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	int res = cp2615_init_i2c_msg(msg, i2c_w);

	if (!res)
		res = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, IOP_EP_OUT),
				   msg, ntohs(msg->length), NULL, 0);
	kfree(msg);
	return res;
}

static int
cp2615_i2c_recv(struct usb_interface *usbif, unsigned char tag, void *buf)
{
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	struct cp2615_iop_msg *msg;
	struct cp2615_i2c_transfer_result *i2c_r;
	int res;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	res = usb_bulk_msg(usbdev, usb_rcvbulkpipe(usbdev, IOP_EP_IN), msg,
			   sizeof(struct cp2615_iop_msg), NULL, 0);
	if (res < 0) {
		kfree(msg);
		return res;
	}

	i2c_r = (struct cp2615_i2c_transfer_result *)&msg->data;
	if (msg->msg != htons(iop_I2cTransferResult) || i2c_r->tag != tag) {
		kfree(msg);
		return -EIO;
	}

	res = cp2615_check_status(i2c_r->status);
	if (!res)
		memcpy(buf, &i2c_r->data, i2c_r->read_len);

	kfree(msg);
	return res;
}

/* Checks if the IOP is functional by querying the part's ID */
static int cp2615_check_iop(struct usb_interface *usbif)
{
	struct cp2615_iop_msg *msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	struct cp2615_iop_accessory_info *info = (struct cp2615_iop_accessory_info *)&msg->data;
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	int res = cp2615_init_iop_msg(msg, iop_GetAccessoryInfo, NULL, 0);

	if (res)
		goto out;

	res = usb_bulk_msg(usbdev, usb_sndbulkpipe(usbdev, IOP_EP_OUT),
				   msg, ntohs(msg->length), NULL, 0);
	if (res)
		goto out;

	res = usb_bulk_msg(usbdev, usb_rcvbulkpipe(usbdev, IOP_EP_IN),
			       msg, sizeof(struct cp2615_iop_msg), NULL, 0);
	if (res)
		goto out;

	if (msg->msg != htons(iop_AccessoryInfo)) {
		res = -EIO;
		goto out;
	}

	switch (ntohs(info->part_id)) {
	case PART_ID_A01:
		dev_dbg(&usbif->dev, "Found A01 part. (WARNING: errata exists!)\n");
		break;
	case PART_ID_A02:
		dev_dbg(&usbif->dev, "Found good A02 part.\n");
		break;
	default:
		dev_warn(&usbif->dev, "Unknown part ID %04X\n", ntohs(info->part_id));
	}

out:
	kfree(msg);
	return res;
}

static int
cp2615_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct usb_interface *usbif = adap->algo_data;
	int i = 0, ret = 0;
	struct i2c_msg *msg;
	struct cp2615_i2c_transfer i2c_w = {0};

	dev_dbg(&usbif->dev, "Doing %d I2C transactions\n", num);

	for (; !ret && i < num; i++) {
		msg = &msgs[i];

		i2c_w.tag = 0xdd;
		i2c_w.i2caddr = i2c_8bit_addr_from_msg(msg);
		if (msg->flags & I2C_M_RD) {
			i2c_w.read_len = msg->len;
			i2c_w.write_len = 0;
		} else {
			i2c_w.read_len = 0;
			i2c_w.write_len = msg->len;
			memcpy(&i2c_w.data, msg->buf, i2c_w.write_len);
		}
		ret = cp2615_i2c_send(usbif, &i2c_w);
		if (ret)
			break;
		ret = cp2615_i2c_recv(usbif, i2c_w.tag, msg->buf);
	}
	if (ret < 0)
		return ret;
	return i;
}

static u32
cp2615_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm cp2615_i2c_algo = {
	.master_xfer	= cp2615_i2c_master_xfer,
	.functionality	= cp2615_i2c_func,
};

/*
 * This chip has some limitations: one is that the USB endpoint
 * can only receive 64 bytes/transfer, that leaves 54 bytes for
 * the I2C transfer. On top of that, EITHER read_len OR write_len
 * may be zero, but not both. If both are non-zero, the adapter
 * issues a write followed by a read. And the chip does not
 * support repeated START between the write and read phases.
 */
static struct i2c_adapter_quirks cp2615_i2c_quirks = {
	.max_write_len = MAX_I2C_SIZE,
	.max_read_len = MAX_I2C_SIZE,
	.flags = I2C_AQ_COMB_WRITE_THEN_READ | I2C_AQ_NO_ZERO_LEN | I2C_AQ_NO_REP_START,
	.max_comb_1st_msg_len = MAX_I2C_SIZE,
	.max_comb_2nd_msg_len = MAX_I2C_SIZE
};

static void
cp2615_i2c_remove(struct usb_interface *usbif)
{
	struct i2c_adapter *adap = usb_get_intfdata(usbif);

	usb_set_intfdata(usbif, NULL);
	i2c_del_adapter(adap);
}

static int
cp2615_i2c_probe(struct usb_interface *usbif, const struct usb_device_id *id)
{
	int ret = 0;
	struct i2c_adapter *adap;
	struct usb_device *usbdev = interface_to_usbdev(usbif);

	ret = usb_set_interface(usbdev, IOP_IFN, IOP_ALTSETTING);
	if (ret)
		return ret;

	ret = cp2615_check_iop(usbif);
	if (ret)
		return ret;

	adap = devm_kzalloc(&usbif->dev, sizeof(struct i2c_adapter), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	strscpy(adap->name, usbdev->serial, sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->dev.parent = &usbif->dev;
	adap->dev.of_node = usbif->dev.of_node;
	adap->timeout = HZ;
	adap->algo = &cp2615_i2c_algo;
	adap->quirks = &cp2615_i2c_quirks;
	adap->algo_data = usbif;

	ret = i2c_add_adapter(adap);
	if (ret)
		return ret;

	usb_set_intfdata(usbif, adap);
	return 0;
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(CP2615_VID, CP2615_PID, IOP_IFN) },
	{ }
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver cp2615_i2c_driver = {
	.name = "i2c-cp2615",
	.probe = cp2615_i2c_probe,
	.disconnect = cp2615_i2c_remove,
	.id_table = id_table,
};

module_usb_driver(cp2615_i2c_driver);

MODULE_AUTHOR("Bence Csókás <bence98@sch.bme.hu>");
MODULE_DESCRIPTION("CP2615 I2C bus driver");
MODULE_LICENSE("GPL");
