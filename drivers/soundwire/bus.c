// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

/**
 * sdw_add_bus_master() - add a bus Master instance
 * @bus: bus instance
 *
 * Initializes the bus instance, read properties and create child
 * devices.
 */
int sdw_add_bus_master(struct sdw_bus *bus)
{
	int ret;

	if (!bus->dev) {
		pr_err("SoundWire bus has no device");
		return -ENODEV;
	}

	if (!bus->ops) {
		dev_err(bus->dev, "SoundWire Bus ops are not set");
		return -EINVAL;
	}

	mutex_init(&bus->msg_lock);
	mutex_init(&bus->bus_lock);
	INIT_LIST_HEAD(&bus->slaves);

	if (bus->ops->read_prop) {
		ret = bus->ops->read_prop(bus);
		if (ret < 0) {
			dev_err(bus->dev, "Bus read properties failed:%d", ret);
			return ret;
		}
	}

	/*
	 * Device numbers in SoundWire are 0 thru 15. Enumeration device
	 * number (0), Broadcast device number (15), Group numbers (12 and
	 * 13) and Master device number (14) are not used for assignment so
	 * mask these and other higher bits.
	 */

	/* Set higher order bits */
	*bus->assigned = ~GENMASK(SDW_BROADCAST_DEV_NUM, SDW_ENUM_DEV_NUM);

	/* Set enumuration device number and broadcast device number */
	set_bit(SDW_ENUM_DEV_NUM, bus->assigned);
	set_bit(SDW_BROADCAST_DEV_NUM, bus->assigned);

	/* Set group device numbers and master device number */
	set_bit(SDW_GROUP12_DEV_NUM, bus->assigned);
	set_bit(SDW_GROUP13_DEV_NUM, bus->assigned);
	set_bit(SDW_MASTER_DEV_NUM, bus->assigned);

	/*
	 * SDW is an enumerable bus, but devices can be powered off. So,
	 * they won't be able to report as present.
	 *
	 * Create Slave devices based on Slaves described in
	 * the respective firmware (ACPI/DT)
	 */
	if (IS_ENABLED(CONFIG_ACPI) && ACPI_HANDLE(bus->dev))
		ret = sdw_acpi_find_slaves(bus);
	else
		ret = -ENOTSUPP; /* No ACPI/DT so error out */

	if (ret) {
		dev_err(bus->dev, "Finding slaves failed:%d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sdw_add_bus_master);

static int sdw_delete_slave(struct device *dev, void *data)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_bus *bus = slave->bus;

	mutex_lock(&bus->bus_lock);

	if (slave->dev_num) /* clear dev_num if assigned */
		clear_bit(slave->dev_num, bus->assigned);

	list_del_init(&slave->node);
	mutex_unlock(&bus->bus_lock);

	device_unregister(dev);
	return 0;
}

/**
 * sdw_delete_bus_master() - delete the bus master instance
 * @bus: bus to be deleted
 *
 * Remove the instance, delete the child devices.
 */
void sdw_delete_bus_master(struct sdw_bus *bus)
{
	device_for_each_child(bus->dev, NULL, sdw_delete_slave);
}
EXPORT_SYMBOL(sdw_delete_bus_master);

/*
 * SDW IO Calls
 */

static inline int find_response_code(enum sdw_command_response resp)
{
	switch (resp) {
	case SDW_CMD_OK:
		return 0;

	case SDW_CMD_IGNORED:
		return -ENODATA;

	case SDW_CMD_TIMEOUT:
		return -ETIMEDOUT;

	default:
		return -EIO;
	}
}

static inline int do_transfer(struct sdw_bus *bus, struct sdw_msg *msg)
{
	int retry = bus->prop.err_threshold;
	enum sdw_command_response resp;
	int ret = 0, i;

	for (i = 0; i <= retry; i++) {
		resp = bus->ops->xfer_msg(bus, msg);
		ret = find_response_code(resp);

		/* if cmd is ok or ignored return */
		if (ret == 0 || ret == -ENODATA)
			return ret;
	}

	return ret;
}

static inline int do_transfer_defer(struct sdw_bus *bus,
			struct sdw_msg *msg, struct sdw_defer *defer)
{
	int retry = bus->prop.err_threshold;
	enum sdw_command_response resp;
	int ret = 0, i;

	defer->msg = msg;
	defer->length = msg->len;

	for (i = 0; i <= retry; i++) {
		resp = bus->ops->xfer_msg_defer(bus, msg, defer);
		ret = find_response_code(resp);
		/* if cmd is ok or ignored return */
		if (ret == 0 || ret == -ENODATA)
			return ret;
	}

	return ret;
}

static int sdw_reset_page(struct sdw_bus *bus, u16 dev_num)
{
	int retry = bus->prop.err_threshold;
	enum sdw_command_response resp;
	int ret = 0, i;

	for (i = 0; i <= retry; i++) {
		resp = bus->ops->reset_page_addr(bus, dev_num);
		ret = find_response_code(resp);
		/* if cmd is ok or ignored return */
		if (ret == 0 || ret == -ENODATA)
			return ret;
	}

	return ret;
}

/**
 * sdw_transfer() - Synchronous transfer message to a SDW Slave device
 * @bus: SDW bus
 * @msg: SDW message to be xfered
 */
int sdw_transfer(struct sdw_bus *bus, struct sdw_msg *msg)
{
	int ret;

	mutex_lock(&bus->msg_lock);

	ret = do_transfer(bus, msg);
	if (ret != 0 && ret != -ENODATA)
		dev_err(bus->dev, "trf on Slave %d failed:%d\n",
				msg->dev_num, ret);

	if (msg->page)
		sdw_reset_page(bus, msg->dev_num);

	mutex_unlock(&bus->msg_lock);

	return ret;
}

/**
 * sdw_transfer_defer() - Asynchronously transfer message to a SDW Slave device
 * @bus: SDW bus
 * @msg: SDW message to be xfered
 * @defer: Defer block for signal completion
 *
 * Caller needs to hold the msg_lock lock while calling this
 */
int sdw_transfer_defer(struct sdw_bus *bus, struct sdw_msg *msg,
				struct sdw_defer *defer)
{
	int ret;

	if (!bus->ops->xfer_msg_defer)
		return -ENOTSUPP;

	ret = do_transfer_defer(bus, msg, defer);
	if (ret != 0 && ret != -ENODATA)
		dev_err(bus->dev, "Defer trf on Slave %d failed:%d\n",
				msg->dev_num, ret);

	if (msg->page)
		sdw_reset_page(bus, msg->dev_num);

	return ret;
}


int sdw_fill_msg(struct sdw_msg *msg, struct sdw_slave *slave,
		u32 addr, size_t count, u16 dev_num, u8 flags, u8 *buf)
{
	memset(msg, 0, sizeof(*msg));
	msg->addr = addr; /* addr is 16 bit and truncated here */
	msg->len = count;
	msg->dev_num = dev_num;
	msg->flags = flags;
	msg->buf = buf;
	msg->ssp_sync = false;
	msg->page = false;

	if (addr < SDW_REG_NO_PAGE) { /* no paging area */
		return 0;
	} else if (addr >= SDW_REG_MAX) { /* illegal addr */
		pr_err("SDW: Invalid address %x passed\n", addr);
		return -EINVAL;
	}

	if (addr < SDW_REG_OPTIONAL_PAGE) { /* 32k but no page */
		if (slave && !slave->prop.paging_support)
			return 0;
		/* no need for else as that will fall thru to paging */
	}

	/* paging mandatory */
	if (dev_num == SDW_ENUM_DEV_NUM || dev_num == SDW_BROADCAST_DEV_NUM) {
		pr_err("SDW: Invalid device for paging :%d\n", dev_num);
		return -EINVAL;
	}

	if (!slave) {
		pr_err("SDW: No slave for paging addr\n");
		return -EINVAL;
	} else if (!slave->prop.paging_support) {
		dev_err(&slave->dev,
			"address %x needs paging but no support", addr);
		return -EINVAL;
	}

	msg->addr_page1 = (addr >> SDW_REG_SHIFT(SDW_SCP_ADDRPAGE1_MASK));
	msg->addr_page2 = (addr >> SDW_REG_SHIFT(SDW_SCP_ADDRPAGE2_MASK));
	msg->addr |= BIT(15);
	msg->page = true;

	return 0;
}

/**
 * sdw_nread() - Read "n" contiguous SDW Slave registers
 * @slave: SDW Slave
 * @addr: Register address
 * @count: length
 * @val: Buffer for values to be read
 */
int sdw_nread(struct sdw_slave *slave, u32 addr, size_t count, u8 *val)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, slave, addr, count,
			slave->dev_num, SDW_MSG_FLAG_READ, val);
	if (ret < 0)
		return ret;

	ret = pm_runtime_get_sync(slave->bus->dev);
	if (!ret)
		return ret;

	ret = sdw_transfer(slave->bus, &msg);
	pm_runtime_put(slave->bus->dev);

	return ret;
}
EXPORT_SYMBOL(sdw_nread);

/**
 * sdw_nwrite() - Write "n" contiguous SDW Slave registers
 * @slave: SDW Slave
 * @addr: Register address
 * @count: length
 * @val: Buffer for values to be read
 */
int sdw_nwrite(struct sdw_slave *slave, u32 addr, size_t count, u8 *val)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, slave, addr, count,
			slave->dev_num, SDW_MSG_FLAG_WRITE, val);
	if (ret < 0)
		return ret;

	ret = pm_runtime_get_sync(slave->bus->dev);
	if (!ret)
		return ret;

	ret = sdw_transfer(slave->bus, &msg);
	pm_runtime_put(slave->bus->dev);

	return ret;
}
EXPORT_SYMBOL(sdw_nwrite);

/**
 * sdw_read() - Read a SDW Slave register
 * @slave: SDW Slave
 * @addr: Register address
 */
int sdw_read(struct sdw_slave *slave, u32 addr)
{
	u8 buf;
	int ret;

	ret = sdw_nread(slave, addr, 1, &buf);
	if (ret < 0)
		return ret;
	else
		return buf;
}
EXPORT_SYMBOL(sdw_read);

/**
 * sdw_write() - Write a SDW Slave register
 * @slave: SDW Slave
 * @addr: Register address
 * @value: Register value
 */
int sdw_write(struct sdw_slave *slave, u32 addr, u8 value)
{
	return sdw_nwrite(slave, addr, 1, &value);

}
EXPORT_SYMBOL(sdw_write);

/*
 * SDW alert handling
 */

/* called with bus_lock held */
static struct sdw_slave *sdw_get_slave(struct sdw_bus *bus, int i)
{
	struct sdw_slave *slave = NULL;

	list_for_each_entry(slave, &bus->slaves, node) {
		if (slave->dev_num == i)
			return slave;
	}

	return NULL;
}

static int sdw_compare_devid(struct sdw_slave *slave, struct sdw_slave_id id)
{

	if ((slave->id.unique_id != id.unique_id) ||
	    (slave->id.mfg_id != id.mfg_id) ||
	    (slave->id.part_id != id.part_id) ||
	    (slave->id.class_id != id.class_id))
		return -ENODEV;

	return 0;
}

/* called with bus_lock held */
static int sdw_get_device_num(struct sdw_slave *slave)
{
	int bit;

	bit = find_first_zero_bit(slave->bus->assigned, SDW_MAX_DEVICES);
	if (bit == SDW_MAX_DEVICES) {
		bit = -ENODEV;
		goto err;
	}

	/*
	 * Do not update dev_num in Slave data structure here,
	 * Update once program dev_num is successful
	 */
	set_bit(bit, slave->bus->assigned);

err:
	return bit;
}

static int sdw_assign_device_num(struct sdw_slave *slave)
{
	int ret, dev_num;

	/* check first if device number is assigned, if so reuse that */
	if (!slave->dev_num) {
		mutex_lock(&slave->bus->bus_lock);
		dev_num = sdw_get_device_num(slave);
		mutex_unlock(&slave->bus->bus_lock);
		if (dev_num < 0) {
			dev_err(slave->bus->dev, "Get dev_num failed: %d",
								dev_num);
			return dev_num;
		}
	} else {
		dev_info(slave->bus->dev,
				"Slave already registered dev_num:%d",
				slave->dev_num);

		/* Clear the slave->dev_num to transfer message on device 0 */
		dev_num = slave->dev_num;
		slave->dev_num = 0;

	}

	ret = sdw_write(slave, SDW_SCP_DEVNUMBER, dev_num);
	if (ret < 0) {
		dev_err(&slave->dev, "Program device_num failed: %d", ret);
		return ret;
	}

	/* After xfer of msg, restore dev_num */
	slave->dev_num = dev_num;

	return 0;
}

void sdw_extract_slave_id(struct sdw_bus *bus,
			u64 addr, struct sdw_slave_id *id)
{
	dev_dbg(bus->dev, "SDW Slave Addr: %llx", addr);

	/*
	 * Spec definition
	 *   Register		Bit	Contents
	 *   DevId_0 [7:4]	47:44	sdw_version
	 *   DevId_0 [3:0]	43:40	unique_id
	 *   DevId_1		39:32	mfg_id [15:8]
	 *   DevId_2		31:24	mfg_id [7:0]
	 *   DevId_3		23:16	part_id [15:8]
	 *   DevId_4		15:08	part_id [7:0]
	 *   DevId_5		07:00	class_id
	 */
	id->sdw_version = (addr >> 44) & GENMASK(3, 0);
	id->unique_id = (addr >> 40) & GENMASK(3, 0);
	id->mfg_id = (addr >> 24) & GENMASK(15, 0);
	id->part_id = (addr >> 8) & GENMASK(15, 0);
	id->class_id = addr & GENMASK(7, 0);

	dev_dbg(bus->dev,
		"SDW Slave class_id %x, part_id %x, mfg_id %x, unique_id %x, version %x",
				id->class_id, id->part_id, id->mfg_id,
				id->unique_id, id->sdw_version);

}

static int sdw_program_device_num(struct sdw_bus *bus)
{
	u8 buf[SDW_NUM_DEV_ID_REGISTERS] = {0};
	struct sdw_slave *slave, *_s;
	struct sdw_slave_id id;
	struct sdw_msg msg;
	bool found = false;
	int count = 0, ret;
	u64 addr;

	/* No Slave, so use raw xfer api */
	ret = sdw_fill_msg(&msg, NULL, SDW_SCP_DEVID_0,
			SDW_NUM_DEV_ID_REGISTERS, 0, SDW_MSG_FLAG_READ, buf);
	if (ret < 0)
		return ret;

	do {
		ret = sdw_transfer(bus, &msg);
		if (ret == -ENODATA) { /* end of device id reads */
			ret = 0;
			break;
		}
		if (ret < 0) {
			dev_err(bus->dev, "DEVID read fail:%d\n", ret);
			break;
		}

		/*
		 * Construct the addr and extract. Cast the higher shift
		 * bits to avoid truncation due to size limit.
		 */
		addr = buf[5] | (buf[4] << 8) | (buf[3] << 16) |
			(buf[2] << 24) | ((unsigned long long)buf[1] << 32) |
			((unsigned long long)buf[0] << 40);

		sdw_extract_slave_id(bus, addr, &id);

		/* Now compare with entries */
		list_for_each_entry_safe(slave, _s, &bus->slaves, node) {
			if (sdw_compare_devid(slave, id) == 0) {
				found = true;

				/*
				 * Assign a new dev_num to this Slave and
				 * not mark it present. It will be marked
				 * present after it reports ATTACHED on new
				 * dev_num
				 */
				ret = sdw_assign_device_num(slave);
				if (ret) {
					dev_err(slave->bus->dev,
						"Assign dev_num failed:%d",
						ret);
					return ret;
				}

				break;
			}
		}

		if (found == false) {
			/* TODO: Park this device in Group 13 */
			dev_err(bus->dev, "Slave Entry not found");
		}

		count++;

		/*
		 * Check till error out or retry (count) exhausts.
		 * Device can drop off and rejoin during enumeration
		 * so count till twice the bound.
		 */

	} while (ret == 0 && count < (SDW_MAX_DEVICES * 2));

	return ret;
}

static void sdw_modify_slave_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	mutex_lock(&slave->bus->bus_lock);
	slave->status = status;
	mutex_unlock(&slave->bus->bus_lock);
}

static int sdw_initialize_slave(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int ret;
	u8 val;

	/*
	 * Set bus clash, parity and SCP implementation
	 * defined interrupt mask
	 * TODO: Read implementation defined interrupt mask
	 * from Slave property
	 */
	val = SDW_SCP_INT1_IMPL_DEF | SDW_SCP_INT1_BUS_CLASH |
					SDW_SCP_INT1_PARITY;

	/* Enable SCP interrupts */
	ret = sdw_update(slave, SDW_SCP_INTMASK1, val, val);
	if (ret < 0) {
		dev_err(slave->bus->dev,
				"SDW_SCP_INTMASK1 write failed:%d", ret);
		return ret;
	}

	/* No need to continue if DP0 is not present */
	if (!slave->prop.dp0_prop)
		return 0;

	/* Enable DP0 interrupts */
	val = prop->dp0_prop->device_interrupts;
	val |= SDW_DP0_INT_PORT_READY | SDW_DP0_INT_BRA_FAILURE;

	ret = sdw_update(slave, SDW_DP0_INTMASK, val, val);
	if (ret < 0) {
		dev_err(slave->bus->dev,
				"SDW_DP0_INTMASK read failed:%d", ret);
		return val;
	}

	return 0;
}
