// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) KEBA Industrial Automation Gmbh 2024
 *
 * Driver for KEBA I2C controller FPGA IP core
 */

#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/misc/keba.h>

#define KI2C "i2c-keba"

#define KI2C_CAPABILITY_REG		0x02
#define   KI2C_CAPABILITY_CRYPTO	0x01
#define   KI2C_CAPABILITY_DC		0x02

#define KI2C_CONTROL_REG	0x04
#define   KI2C_CONTROL_MEN	0x01
#define   KI2C_CONTROL_MSTA	0x02
#define   KI2C_CONTROL_RSTA	0x04
#define   KI2C_CONTROL_MTX	0x08
#define   KI2C_CONTROL_TXAK	0x10
#define   KI2C_CONTROL_DISABLE	0x00

#define KI2C_CONTROL_DC_REG	0x05
#define   KI2C_CONTROL_DC_SDA	0x01
#define   KI2C_CONTROL_DC_SCL	0x02

#define KI2C_STATUS_REG		0x08
#define   KI2C_STATUS_IN_USE	0x01
#define   KI2C_STATUS_ACK_CYC	0x02
#define   KI2C_STATUS_RXAK	0x04
#define   KI2C_STATUS_MCF	0x08

#define KI2C_STATUS_DC_REG	0x09
#define   KI2C_STATUS_DC_SDA	0x01
#define   KI2C_STATUS_DC_SCL	0x02

#define KI2C_DATA_REG		0x0c

#define KI2C_INUSE_SLEEP_US	(2 * USEC_PER_MSEC)
#define KI2C_INUSE_TIMEOUT_US	(10 * USEC_PER_SEC)

#define KI2C_POLL_DELAY_US	5

struct ki2c {
	struct keba_i2c_auxdev *auxdev;
	void __iomem *base;
	struct i2c_adapter adapter;

	struct i2c_client **client;
	int client_size;
};

static int ki2c_inuse_lock(struct ki2c *ki2c)
{
	u8 sts;
	int ret;

	/*
	 * The I2C controller has an IN_USE bit for locking access to the
	 * controller. This enables the use of I2C controller by other none
	 * Linux processors.
	 *
	 * If the I2C controller is free, then the first read returns
	 * IN_USE == 0. After that the I2C controller is locked and further
	 * reads of IN_USE return 1.
	 *
	 * The I2C controller is unlocked by writing 1 into IN_USE.
	 *
	 * The IN_USE bit acts as a hardware semaphore for the I2C controller.
	 * Poll for semaphore, but sleep while polling to free the CPU.
	 */
	ret = readb_poll_timeout(ki2c->base + KI2C_STATUS_REG,
				 sts, (sts & KI2C_STATUS_IN_USE) == 0,
				 KI2C_INUSE_SLEEP_US, KI2C_INUSE_TIMEOUT_US);
	if (ret)
		dev_err(&ki2c->auxdev->auxdev.dev, "%s err!\n", __func__);

	return ret;
}

static void ki2c_inuse_unlock(struct ki2c *ki2c)
{
	/* unlock the controller by writing 1 into IN_USE */
	iowrite8(KI2C_STATUS_IN_USE, ki2c->base + KI2C_STATUS_REG);
}

static int ki2c_wait_for_bit(void __iomem *addr, u8 mask, unsigned long timeout)
{
	u8 val;

	return readb_poll_timeout(addr, val, (val & mask), KI2C_POLL_DELAY_US,
				  jiffies_to_usecs(timeout));
}

static int ki2c_wait_for_mcf(struct ki2c *ki2c)
{
	return ki2c_wait_for_bit(ki2c->base + KI2C_STATUS_REG, KI2C_STATUS_MCF,
				 ki2c->adapter.timeout);
}

static int ki2c_wait_for_data(struct ki2c *ki2c)
{
	int ret;

	ret = ki2c_wait_for_mcf(ki2c);
	if (ret < 0)
		return ret;

	return ki2c_wait_for_bit(ki2c->base + KI2C_STATUS_REG,
				 KI2C_STATUS_ACK_CYC,
				 ki2c->adapter.timeout);
}

static int ki2c_wait_for_data_ack(struct ki2c *ki2c)
{
	unsigned int reg;
	int ret;

	ret = ki2c_wait_for_data(ki2c);
	if (ret < 0)
		return ret;

	/* RXAK == 0 means ACK reveived */
	reg = ioread8(ki2c->base + KI2C_STATUS_REG);
	if (reg & KI2C_STATUS_RXAK)
		return -EIO;

	return 0;
}

static int ki2c_has_capability(struct ki2c *ki2c, unsigned int cap)
{
	unsigned int reg = ioread8(ki2c->base + KI2C_CAPABILITY_REG);

	return (reg & cap) != 0;
}

static int ki2c_get_scl(struct ki2c *ki2c)
{
	unsigned int reg = ioread8(ki2c->base + KI2C_STATUS_DC_REG);

	/* capability KI2C_CAPABILITY_DC required */
	return (reg & KI2C_STATUS_DC_SCL) != 0;
}

static int ki2c_get_sda(struct ki2c *ki2c)
{
	unsigned int reg = ioread8(ki2c->base + KI2C_STATUS_DC_REG);

	/* capability KI2C_CAPABILITY_DC required */
	return (reg & KI2C_STATUS_DC_SDA) != 0;
}

static void ki2c_set_scl(struct ki2c *ki2c, int val)
{
	u8 control_dc;

	/* capability KI2C_CAPABILITY_DC and KI2C_CONTROL_MEN = 0 reqired */
	control_dc = ioread8(ki2c->base + KI2C_CONTROL_DC_REG);
	if (val)
		control_dc |= KI2C_CONTROL_DC_SCL;
	else
		control_dc &= ~KI2C_CONTROL_DC_SCL;
	iowrite8(control_dc, ki2c->base + KI2C_CONTROL_DC_REG);
}

/*
 * Resetting bus bitwise is done by checking SDA and applying clock cycles as
 * long as SDA is low. 9 clock cycles are applied at most.
 *
 * Clock cycles are generated and udelay() determines the duration of clock
 * cycles. Generated clock rate is 100 KHz and so duration of both clock levels
 * is: delay in ns = (10^6 / 100) / 2
 */
#define KI2C_RECOVERY_CLK_CNT	(9 * 2)
#define KI2C_RECOVERY_UDELAY	5
static int ki2c_reset_bus_bitwise(struct ki2c *ki2c)
{
	int val = 1;
	int ret = 0;
	int i;

	/* disable I2C controller (MEN = 0) to get direct access to SCL/SDA */
	iowrite8(0, ki2c->base + KI2C_CONTROL_REG);

	/* generate clock cycles */
	ki2c_set_scl(ki2c, val);
	udelay(KI2C_RECOVERY_UDELAY);
	for (i = 0; i < KI2C_RECOVERY_CLK_CNT; i++) {
		if (val) {
			/* SCL shouldn't be low here */
			if (!ki2c_get_scl(ki2c)) {
				dev_err(&ki2c->auxdev->auxdev.dev,
					"SCL is stuck low!\n");
				ret = -EBUSY;
				break;
			}

			/* break if SDA is high */
			if (ki2c_get_sda(ki2c))
				break;
		}

		val = !val;
		ki2c_set_scl(ki2c, val);
		udelay(KI2C_RECOVERY_UDELAY);
	}

	if (!ki2c_get_sda(ki2c)) {
		dev_err(&ki2c->auxdev->auxdev.dev, "SDA is still low!\n");
		ret = -EBUSY;
	}

	/* reenable controller */
	iowrite8(KI2C_CONTROL_MEN, ki2c->base + KI2C_CONTROL_REG);

	return ret;
}

/*
 * Resetting bus bytewise is done by writing start bit, 9 data bits and stop
 * bit.
 *
 * This is not 100% safe. If target is an EEPROM and a write access was
 * interrupted during the ACK cycle, this approach might not be able to recover
 * the bus. The reason is, that after the 9 clock cycles the EEPROM will be in
 * ACK cycle again and will hold SDA low like it did before the start of the
 * routine. Furthermore the EEPROM might get written one additional byte with
 * 0xff into it. Thus, use bitwise approach whenever possible, especially when
 * EEPROMs are on the bus.
 */
static int ki2c_reset_bus_bytewise(struct ki2c *ki2c)
{
	int ret;

	/* hold data line high for 9 clock cycles */
	iowrite8(0xFF, ki2c->base + KI2C_DATA_REG);

	/* create start condition */
	iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MTX | KI2C_CONTROL_MSTA | KI2C_CONTROL_TXAK,
		 ki2c->base + KI2C_CONTROL_REG);
	ret = ki2c_wait_for_mcf(ki2c);
	if (ret < 0) {
		dev_err(&ki2c->auxdev->auxdev.dev, "Start condition failed\n");

		return ret;
	}

	/* create stop condition */
	iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MTX | KI2C_CONTROL_TXAK,
		 ki2c->base + KI2C_CONTROL_REG);
	ret = ki2c_wait_for_mcf(ki2c);
	if (ret < 0)
		dev_err(&ki2c->auxdev->auxdev.dev, "Stop condition failed\n");

	return ret;
}

static int ki2c_reset_bus(struct ki2c *ki2c)
{
	int ret;

	ret = ki2c_inuse_lock(ki2c);
	if (ret < 0)
		return ret;

	/*
	 * If the I2C controller is capable of direct control of SCL/SDA, then a
	 * bitwise reset is used. Otherwise fall back to bytewise reset.
	 */
	if (ki2c_has_capability(ki2c, KI2C_CAPABILITY_DC))
		ret = ki2c_reset_bus_bitwise(ki2c);
	else
		ret = ki2c_reset_bus_bytewise(ki2c);

	ki2c_inuse_unlock(ki2c);

	return ret;
}

static void ki2c_write_target_addr(struct ki2c *ki2c, struct i2c_msg *m)
{
	u8 addr;

	addr = m->addr << 1;
	/* Bit 0 signals RD/WR */
	if (m->flags & I2C_M_RD)
		addr |= 0x01;

	iowrite8(addr, ki2c->base + KI2C_DATA_REG);
}

static int ki2c_start_addr(struct ki2c *ki2c, struct i2c_msg *m)
{
	int ret;

	/*
	 * Store target address byte in the controller. This has to be done
	 * before sending START condition.
	 */
	ki2c_write_target_addr(ki2c, m);

	/* enable controller for TX */
	iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MTX,
		 ki2c->base + KI2C_CONTROL_REG);

	/* send START condition and target address byte */
	iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MTX | KI2C_CONTROL_MSTA,
		 ki2c->base + KI2C_CONTROL_REG);

	ret = ki2c_wait_for_data_ack(ki2c);
	if (ret < 0)
		/*
		 * For EEPROMs this is normal behavior during internal write
		 * operation.
		 */
		dev_dbg(&ki2c->auxdev->auxdev.dev,
			"%s wait for ACK err at 0x%02x!\n", __func__, m->addr);

	return ret;
}

static int ki2c_repstart_addr(struct ki2c *ki2c, struct i2c_msg *m)
{
	int ret;

	/* repeated start and write is not supported */
	if ((m->flags & I2C_M_RD) == 0) {
		dev_err(&ki2c->auxdev->auxdev.dev,
			"Repeated start not supported for writes\n");
		return -EINVAL;
	}

	/* send repeated start */
	iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MSTA | KI2C_CONTROL_RSTA,
		 ki2c->base + KI2C_CONTROL_REG);

	ret = ki2c_wait_for_mcf(ki2c);
	if (ret < 0) {
		dev_err(&ki2c->auxdev->auxdev.dev,
			"%s wait for MCF err at 0x%02x!\n", __func__, m->addr);
		return ret;
	}

	/* write target-address byte */
	ki2c_write_target_addr(ki2c, m);

	ret = ki2c_wait_for_data_ack(ki2c);
	if (ret < 0)
		dev_err(&ki2c->auxdev->auxdev.dev,
			"%s wait for ACK err at 0x%02x!\n", __func__, m->addr);

	return ret;
}

static void ki2c_stop(struct ki2c *ki2c)
{
	iowrite8(KI2C_CONTROL_MEN, ki2c->base + KI2C_CONTROL_REG);
	ki2c_wait_for_mcf(ki2c);
}

static int ki2c_write(struct ki2c *ki2c, const u8 *data, int len)
{
	int ret;
	int i;

	for (i = 0; i < len; i++) {
		/* write data byte */
		iowrite8(data[i], ki2c->base + KI2C_DATA_REG);

		ret = ki2c_wait_for_data_ack(ki2c);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ki2c_read(struct ki2c *ki2c, u8 *data, int len)
{
	u8 control;
	int ret;
	int i;

	if (len == 0)
		return 0;	/* nothing to do */

	control = KI2C_CONTROL_MEN | KI2C_CONTROL_MSTA;

	/* if just one byte => send tx-nack after transfer */
	if (len == 1)
		control |= KI2C_CONTROL_TXAK;

	iowrite8(control, ki2c->base + KI2C_CONTROL_REG);

	/* dummy read to start transfer on bus */
	ioread8(ki2c->base + KI2C_DATA_REG);

	for (i = 0; i < len; i++) {
		ret = ki2c_wait_for_data(ki2c);
		if (ret < 0)
			return ret;

		if (i == len - 2)
			/* send tx-nack after transfer of last byte */
			iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MSTA | KI2C_CONTROL_TXAK,
				 ki2c->base + KI2C_CONTROL_REG);
		else if (i == len - 1)
			/*
			 * switch to TX on last byte, so that reading DATA
			 * register does not trigger another read transfer
			 */
			iowrite8(KI2C_CONTROL_MEN | KI2C_CONTROL_MSTA | KI2C_CONTROL_MTX,
				 ki2c->base + KI2C_CONTROL_REG);

		/* read byte and start next transfer (if not last byte) */
		data[i] = ioread8(ki2c->base + KI2C_DATA_REG);
	}

	return len;
}

static int ki2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct ki2c *ki2c = i2c_get_adapdata(adap);
	int ret;
	int i;

	ret = ki2c_inuse_lock(ki2c);
	if (ret < 0)
		return ret;

	for (i = 0; i < num; i++) {
		struct i2c_msg *m = &msgs[i];

		if (i == 0)
			ret = ki2c_start_addr(ki2c, m);
		else
			ret = ki2c_repstart_addr(ki2c, m);
		if (ret < 0)
			break;

		if (m->flags & I2C_M_RD)
			ret = ki2c_read(ki2c, m->buf, m->len);
		else
			ret = ki2c_write(ki2c, m->buf, m->len);
		if (ret < 0)
			break;
	}

	ki2c_stop(ki2c);

	ki2c_inuse_unlock(ki2c);

	return ret < 0 ? ret : num;
}

static void ki2c_unregister_devices(struct ki2c *ki2c)
{
	int i;

	for (i = 0; i < ki2c->client_size; i++)
		i2c_unregister_device(ki2c->client[i]);
}

static int ki2c_register_devices(struct ki2c *ki2c)
{
	struct i2c_board_info *info = ki2c->auxdev->info;
	int i;

	/* register all known I2C devices */
	for (i = 0; i < ki2c->client_size; i++) {
		struct i2c_client *client;
		unsigned short const addr_list[2] = { info[i].addr,
						      I2C_CLIENT_END };

		client = i2c_new_scanned_device(&ki2c->adapter, &info[i],
						addr_list, NULL);
		if (!IS_ERR(client)) {
			ki2c->client[i] = client;
		} else if (PTR_ERR(client) != -ENODEV) {
			ki2c->client_size = i;
			ki2c_unregister_devices(ki2c);

			return PTR_ERR(client);
		}
	}

	return 0;
}

static u32 ki2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm ki2c_algo = {
	.xfer = ki2c_xfer,
	.functionality = ki2c_func,
};

static int ki2c_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;
	struct i2c_adapter *adap;
	struct ki2c *ki2c;
	int ret;

	ki2c = devm_kzalloc(dev, sizeof(*ki2c), GFP_KERNEL);
	if (!ki2c)
		return -ENOMEM;
	ki2c->auxdev = container_of(auxdev, struct keba_i2c_auxdev, auxdev);
	ki2c->client = devm_kcalloc(dev, ki2c->auxdev->info_size,
				    sizeof(*ki2c->client), GFP_KERNEL);
	if (!ki2c->client)
		return -ENOMEM;
	ki2c->client_size = ki2c->auxdev->info_size;
	auxiliary_set_drvdata(auxdev, ki2c);

	ki2c->base = devm_ioremap_resource(dev, &ki2c->auxdev->io);
	if (IS_ERR(ki2c->base))
		return PTR_ERR(ki2c->base);

	adap = &ki2c->adapter;
	strscpy(adap->name, "KEBA I2C adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	adap->algo = &ki2c_algo;
	adap->dev.parent = dev;

	i2c_set_adapdata(adap, ki2c);

	/* enable controller */
	iowrite8(KI2C_CONTROL_MEN, ki2c->base + KI2C_CONTROL_REG);

	/* reset bus before probing I2C devices */
	ret = ki2c_reset_bus(ki2c);
	if (ret)
		goto out;

	ret = devm_i2c_add_adapter(dev, adap);
	if (ret) {
		dev_err(dev, "Failed to add adapter (%d)!\n", ret);
		goto out;
	}

	ret = ki2c_register_devices(ki2c);
	if (ret) {
		dev_err(dev, "Failed to register devices (%d)!\n", ret);
		goto out;
	}

	return 0;

out:
	iowrite8(KI2C_CONTROL_DISABLE, ki2c->base + KI2C_CONTROL_REG);
	return ret;
}

static void ki2c_remove(struct auxiliary_device *auxdev)
{
	struct ki2c *ki2c = auxiliary_get_drvdata(auxdev);

	ki2c_unregister_devices(ki2c);

	/* disable controller */
	iowrite8(KI2C_CONTROL_DISABLE, ki2c->base + KI2C_CONTROL_REG);

	auxiliary_set_drvdata(auxdev, NULL);
}

static const struct auxiliary_device_id ki2c_devtype_aux[] = {
	{ .name = "keba.i2c" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, ki2c_devtype_aux);

static struct auxiliary_driver ki2c_driver_aux = {
	.name = KI2C,
	.id_table = ki2c_devtype_aux,
	.probe = ki2c_probe,
	.remove = ki2c_remove,
};
module_auxiliary_driver(ki2c_driver_aux);

MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA I2C bus controller driver");
MODULE_LICENSE("GPL");
