// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"
#include "sysfs_local.h"

static DEFINE_IDA(sdw_ida);

static int sdw_get_id(struct sdw_bus *bus)
{
	int rc = ida_alloc(&sdw_ida, GFP_KERNEL);

	if (rc < 0)
		return rc;

	bus->id = rc;
	return 0;
}

/**
 * sdw_bus_master_add() - add a bus Master instance
 * @bus: bus instance
 * @parent: parent device
 * @fwnode: firmware node handle
 *
 * Initializes the bus instance, read properties and create child
 * devices.
 */
int sdw_bus_master_add(struct sdw_bus *bus, struct device *parent,
		       struct fwnode_handle *fwnode)
{
	struct sdw_master_prop *prop = NULL;
	int ret;

	if (!parent) {
		pr_err("SoundWire parent device is not set\n");
		return -ENODEV;
	}

	ret = sdw_get_id(bus);
	if (ret) {
		dev_err(parent, "Failed to get bus id\n");
		return ret;
	}

	ret = sdw_master_device_add(bus, parent, fwnode);
	if (ret) {
		dev_err(parent, "Failed to add master device at link %d\n",
			bus->link_id);
		return ret;
	}

	if (!bus->ops) {
		dev_err(bus->dev, "SoundWire Bus ops are not set\n");
		return -EINVAL;
	}

	if (!bus->compute_params) {
		dev_err(bus->dev,
			"Bandwidth allocation not configured, compute_params no set\n");
		return -EINVAL;
	}

	mutex_init(&bus->msg_lock);
	mutex_init(&bus->bus_lock);
	INIT_LIST_HEAD(&bus->slaves);
	INIT_LIST_HEAD(&bus->m_rt_list);

	/*
	 * Initialize multi_link flag
	 * TODO: populate this flag by reading property from FW node
	 */
	bus->multi_link = false;
	if (bus->ops->read_prop) {
		ret = bus->ops->read_prop(bus);
		if (ret < 0) {
			dev_err(bus->dev,
				"Bus read properties failed:%d\n", ret);
			return ret;
		}
	}

	sdw_bus_debugfs_init(bus);

	/*
	 * Device numbers in SoundWire are 0 through 15. Enumeration device
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
	else if (IS_ENABLED(CONFIG_OF) && bus->dev->of_node)
		ret = sdw_of_find_slaves(bus);
	else
		ret = -ENOTSUPP; /* No ACPI/DT so error out */

	if (ret) {
		dev_err(bus->dev, "Finding slaves failed:%d\n", ret);
		return ret;
	}

	/*
	 * Initialize clock values based on Master properties. The max
	 * frequency is read from max_clk_freq property. Current assumption
	 * is that the bus will start at highest clock frequency when
	 * powered on.
	 *
	 * Default active bank will be 0 as out of reset the Slaves have
	 * to start with bank 0 (Table 40 of Spec)
	 */
	prop = &bus->prop;
	bus->params.max_dr_freq = prop->max_clk_freq * SDW_DOUBLE_RATE_FACTOR;
	bus->params.curr_dr_freq = bus->params.max_dr_freq;
	bus->params.curr_bank = SDW_BANK0;
	bus->params.next_bank = SDW_BANK1;

	return 0;
}
EXPORT_SYMBOL(sdw_bus_master_add);

static int sdw_delete_slave(struct device *dev, void *data)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_bus *bus = slave->bus;

	pm_runtime_disable(dev);

	sdw_slave_debugfs_exit(slave);

	mutex_lock(&bus->bus_lock);

	if (slave->dev_num) /* clear dev_num if assigned */
		clear_bit(slave->dev_num, bus->assigned);

	list_del_init(&slave->node);
	mutex_unlock(&bus->bus_lock);

	device_unregister(dev);
	return 0;
}

/**
 * sdw_bus_master_delete() - delete the bus master instance
 * @bus: bus to be deleted
 *
 * Remove the instance, delete the child devices.
 */
void sdw_bus_master_delete(struct sdw_bus *bus)
{
	device_for_each_child(bus->dev, NULL, sdw_delete_slave);
	sdw_master_device_del(bus);

	sdw_bus_debugfs_exit(bus);
	ida_free(&sdw_ida, bus->id);
}
EXPORT_SYMBOL(sdw_bus_master_delete);

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
				    struct sdw_msg *msg,
				    struct sdw_defer *defer)
{
	int retry = bus->prop.err_threshold;
	enum sdw_command_response resp;
	int ret = 0, i;

	defer->msg = msg;
	defer->length = msg->len;
	init_completion(&defer->complete);

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

static int sdw_transfer_unlocked(struct sdw_bus *bus, struct sdw_msg *msg)
{
	int ret;

	ret = do_transfer(bus, msg);
	if (ret != 0 && ret != -ENODATA)
		dev_err(bus->dev, "trf on Slave %d failed:%d\n",
			msg->dev_num, ret);

	if (msg->page)
		sdw_reset_page(bus, msg->dev_num);

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

	ret = sdw_transfer_unlocked(bus, msg);

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

	if (addr < SDW_REG_NO_PAGE) /* no paging area */
		return 0;

	if (addr >= SDW_REG_MAX) { /* illegal addr */
		pr_err("SDW: Invalid address %x passed\n", addr);
		return -EINVAL;
	}

	if (addr < SDW_REG_OPTIONAL_PAGE) { /* 32k but no page */
		if (slave && !slave->prop.paging_support)
			return 0;
		/* no need for else as that will fall-through to paging */
	}

	/* paging mandatory */
	if (dev_num == SDW_ENUM_DEV_NUM || dev_num == SDW_BROADCAST_DEV_NUM) {
		pr_err("SDW: Invalid device for paging :%d\n", dev_num);
		return -EINVAL;
	}

	if (!slave) {
		pr_err("SDW: No slave for paging addr\n");
		return -EINVAL;
	}

	if (!slave->prop.paging_support) {
		dev_err(&slave->dev,
			"address %x needs paging but no support\n", addr);
		return -EINVAL;
	}

	msg->addr_page1 = FIELD_GET(SDW_SCP_ADDRPAGE1_MASK, addr);
	msg->addr_page2 = FIELD_GET(SDW_SCP_ADDRPAGE2_MASK, addr);
	msg->addr |= BIT(15);
	msg->page = true;

	return 0;
}

/*
 * Read/Write IO functions.
 * no_pm versions can only be called by the bus, e.g. while enumerating or
 * handling suspend-resume sequences.
 * all clients need to use the pm versions
 */

static int
sdw_nread_no_pm(struct sdw_slave *slave, u32 addr, size_t count, u8 *val)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, slave, addr, count,
			   slave->dev_num, SDW_MSG_FLAG_READ, val);
	if (ret < 0)
		return ret;

	return sdw_transfer(slave->bus, &msg);
}

static int
sdw_nwrite_no_pm(struct sdw_slave *slave, u32 addr, size_t count, u8 *val)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, slave, addr, count,
			   slave->dev_num, SDW_MSG_FLAG_WRITE, val);
	if (ret < 0)
		return ret;

	return sdw_transfer(slave->bus, &msg);
}

int sdw_write_no_pm(struct sdw_slave *slave, u32 addr, u8 value)
{
	return sdw_nwrite_no_pm(slave, addr, 1, &value);
}
EXPORT_SYMBOL(sdw_write_no_pm);

static int
sdw_bread_no_pm(struct sdw_bus *bus, u16 dev_num, u32 addr)
{
	struct sdw_msg msg;
	u8 buf;
	int ret;

	ret = sdw_fill_msg(&msg, NULL, addr, 1, dev_num,
			   SDW_MSG_FLAG_READ, &buf);
	if (ret)
		return ret;

	ret = sdw_transfer(bus, &msg);
	if (ret < 0)
		return ret;

	return buf;
}

static int
sdw_bwrite_no_pm(struct sdw_bus *bus, u16 dev_num, u32 addr, u8 value)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, NULL, addr, 1, dev_num,
			   SDW_MSG_FLAG_WRITE, &value);
	if (ret)
		return ret;

	return sdw_transfer(bus, &msg);
}

int sdw_bread_no_pm_unlocked(struct sdw_bus *bus, u16 dev_num, u32 addr)
{
	struct sdw_msg msg;
	u8 buf;
	int ret;

	ret = sdw_fill_msg(&msg, NULL, addr, 1, dev_num,
			   SDW_MSG_FLAG_READ, &buf);
	if (ret)
		return ret;

	ret = sdw_transfer_unlocked(bus, &msg);
	if (ret < 0)
		return ret;

	return buf;
}
EXPORT_SYMBOL(sdw_bread_no_pm_unlocked);

int sdw_bwrite_no_pm_unlocked(struct sdw_bus *bus, u16 dev_num, u32 addr, u8 value)
{
	struct sdw_msg msg;
	int ret;

	ret = sdw_fill_msg(&msg, NULL, addr, 1, dev_num,
			   SDW_MSG_FLAG_WRITE, &value);
	if (ret)
		return ret;

	return sdw_transfer_unlocked(bus, &msg);
}
EXPORT_SYMBOL(sdw_bwrite_no_pm_unlocked);

int sdw_read_no_pm(struct sdw_slave *slave, u32 addr)
{
	u8 buf;
	int ret;

	ret = sdw_nread_no_pm(slave, addr, 1, &buf);
	if (ret < 0)
		return ret;
	else
		return buf;
}
EXPORT_SYMBOL(sdw_read_no_pm);

static int sdw_update_no_pm(struct sdw_slave *slave, u32 addr, u8 mask, u8 val)
{
	int tmp;

	tmp = sdw_read_no_pm(slave, addr);
	if (tmp < 0)
		return tmp;

	tmp = (tmp & ~mask) | val;
	return sdw_write_no_pm(slave, addr, tmp);
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
	int ret;

	ret = pm_runtime_get_sync(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(&slave->dev);
		return ret;
	}

	ret = sdw_nread_no_pm(slave, addr, count, val);

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put(&slave->dev);

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
	int ret;

	ret = pm_runtime_get_sync(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(&slave->dev);
		return ret;
	}

	ret = sdw_nwrite_no_pm(slave, addr, count, val);

	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put(&slave->dev);

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
	if (slave->id.mfg_id != id.mfg_id ||
	    slave->id.part_id != id.part_id ||
	    slave->id.class_id != id.class_id ||
	    (slave->id.unique_id != SDW_IGNORED_UNIQUE_ID &&
	     slave->id.unique_id != id.unique_id))
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
	bool new_device = false;

	/* check first if device number is assigned, if so reuse that */
	if (!slave->dev_num) {
		if (!slave->dev_num_sticky) {
			mutex_lock(&slave->bus->bus_lock);
			dev_num = sdw_get_device_num(slave);
			mutex_unlock(&slave->bus->bus_lock);
			if (dev_num < 0) {
				dev_err(slave->bus->dev, "Get dev_num failed: %d\n",
					dev_num);
				return dev_num;
			}
			slave->dev_num = dev_num;
			slave->dev_num_sticky = dev_num;
			new_device = true;
		} else {
			slave->dev_num = slave->dev_num_sticky;
		}
	}

	if (!new_device)
		dev_dbg(slave->bus->dev,
			"Slave already registered, reusing dev_num:%d\n",
			slave->dev_num);

	/* Clear the slave->dev_num to transfer message on device 0 */
	dev_num = slave->dev_num;
	slave->dev_num = 0;

	ret = sdw_write_no_pm(slave, SDW_SCP_DEVNUMBER, dev_num);
	if (ret < 0) {
		dev_err(&slave->dev, "Program device_num %d failed: %d\n",
			dev_num, ret);
		return ret;
	}

	/* After xfer of msg, restore dev_num */
	slave->dev_num = slave->dev_num_sticky;

	return 0;
}

void sdw_extract_slave_id(struct sdw_bus *bus,
			  u64 addr, struct sdw_slave_id *id)
{
	dev_dbg(bus->dev, "SDW Slave Addr: %llx\n", addr);

	id->sdw_version = SDW_VERSION(addr);
	id->unique_id = SDW_UNIQUE_ID(addr);
	id->mfg_id = SDW_MFG_ID(addr);
	id->part_id = SDW_PART_ID(addr);
	id->class_id = SDW_CLASS_ID(addr);

	dev_dbg(bus->dev,
		"SDW Slave class_id %x, part_id %x, mfg_id %x, unique_id %x, version %x\n",
				id->class_id, id->part_id, id->mfg_id,
				id->unique_id, id->sdw_version);
}

static int sdw_program_device_num(struct sdw_bus *bus)
{
	u8 buf[SDW_NUM_DEV_ID_REGISTERS] = {0};
	struct sdw_slave *slave, *_s;
	struct sdw_slave_id id;
	struct sdw_msg msg;
	bool found;
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
			dev_dbg(bus->dev, "No more devices to enumerate\n");
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
			((u64)buf[2] << 24) | ((u64)buf[1] << 32) |
			((u64)buf[0] << 40);

		sdw_extract_slave_id(bus, addr, &id);

		found = false;
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
						"Assign dev_num failed:%d\n",
						ret);
					return ret;
				}

				break;
			}
		}

		if (!found) {
			/* TODO: Park this device in Group 13 */

			/*
			 * add Slave device even if there is no platform
			 * firmware description. There will be no driver probe
			 * but the user/integration will be able to see the
			 * device, enumeration status and device number in sysfs
			 */
			sdw_slave_add(bus, &id, NULL);

			dev_err(bus->dev, "Slave Entry not found\n");
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

	dev_vdbg(&slave->dev,
		 "%s: changing status slave %d status %d new status %d\n",
		 __func__, slave->dev_num, slave->status, status);

	if (status == SDW_SLAVE_UNATTACHED) {
		dev_dbg(&slave->dev,
			"%s: initializing completion for Slave %d\n",
			__func__, slave->dev_num);

		init_completion(&slave->enumeration_complete);
		init_completion(&slave->initialization_complete);

	} else if ((status == SDW_SLAVE_ATTACHED) &&
		   (slave->status == SDW_SLAVE_UNATTACHED)) {
		dev_dbg(&slave->dev,
			"%s: signaling completion for Slave %d\n",
			__func__, slave->dev_num);

		complete(&slave->enumeration_complete);
	}
	slave->status = status;
	mutex_unlock(&slave->bus->bus_lock);
}

static enum sdw_clk_stop_mode sdw_get_clk_stop_mode(struct sdw_slave *slave)
{
	enum sdw_clk_stop_mode mode;

	/*
	 * Query for clock stop mode if Slave implements
	 * ops->get_clk_stop_mode, else read from property.
	 */
	if (slave->ops && slave->ops->get_clk_stop_mode) {
		mode = slave->ops->get_clk_stop_mode(slave);
	} else {
		if (slave->prop.clk_stop_mode1)
			mode = SDW_CLK_STOP_MODE1;
		else
			mode = SDW_CLK_STOP_MODE0;
	}

	return mode;
}

static int sdw_slave_clk_stop_callback(struct sdw_slave *slave,
				       enum sdw_clk_stop_mode mode,
				       enum sdw_clk_stop_type type)
{
	int ret;

	if (slave->ops && slave->ops->clk_stop) {
		ret = slave->ops->clk_stop(slave, mode, type);
		if (ret < 0) {
			dev_err(&slave->dev,
				"Clk Stop type =%d failed: %d\n", type, ret);
			return ret;
		}
	}

	return 0;
}

static int sdw_slave_clk_stop_prepare(struct sdw_slave *slave,
				      enum sdw_clk_stop_mode mode,
				      bool prepare)
{
	bool wake_en;
	u32 val = 0;
	int ret;

	wake_en = slave->prop.wake_capable;

	if (prepare) {
		val = SDW_SCP_SYSTEMCTRL_CLK_STP_PREP;

		if (mode == SDW_CLK_STOP_MODE1)
			val |= SDW_SCP_SYSTEMCTRL_CLK_STP_MODE1;

		if (wake_en)
			val |= SDW_SCP_SYSTEMCTRL_WAKE_UP_EN;
	} else {
		val = sdw_read_no_pm(slave, SDW_SCP_SYSTEMCTRL);

		val &= ~(SDW_SCP_SYSTEMCTRL_CLK_STP_PREP);
	}

	ret = sdw_write_no_pm(slave, SDW_SCP_SYSTEMCTRL, val);

	if (ret != 0)
		dev_err(&slave->dev,
			"Clock Stop prepare failed for slave: %d", ret);

	return ret;
}

static int sdw_bus_wait_for_clk_prep_deprep(struct sdw_bus *bus, u16 dev_num)
{
	int retry = bus->clk_stop_timeout;
	int val;

	do {
		val = sdw_bread_no_pm(bus, dev_num, SDW_SCP_STAT) &
			SDW_SCP_STAT_CLK_STP_NF;
		if (!val) {
			dev_info(bus->dev, "clock stop prep/de-prep done slave:%d",
				 dev_num);
			return 0;
		}

		usleep_range(1000, 1500);
		retry--;
	} while (retry);

	dev_err(bus->dev, "clock stop prep/de-prep failed slave:%d",
		dev_num);

	return -ETIMEDOUT;
}

/**
 * sdw_bus_prep_clk_stop: prepare Slave(s) for clock stop
 *
 * @bus: SDW bus instance
 *
 * Query Slave for clock stop mode and prepare for that mode.
 */
int sdw_bus_prep_clk_stop(struct sdw_bus *bus)
{
	enum sdw_clk_stop_mode slave_mode;
	bool simple_clk_stop = true;
	struct sdw_slave *slave;
	bool is_slave = false;
	int ret = 0;

	/*
	 * In order to save on transition time, prepare
	 * each Slave and then wait for all Slave(s) to be
	 * prepared for clock stop.
	 */
	list_for_each_entry(slave, &bus->slaves, node) {
		if (!slave->dev_num)
			continue;

		if (slave->status != SDW_SLAVE_ATTACHED &&
		    slave->status != SDW_SLAVE_ALERT)
			continue;

		/* Identify if Slave(s) are available on Bus */
		is_slave = true;

		slave_mode = sdw_get_clk_stop_mode(slave);
		slave->curr_clk_stop_mode = slave_mode;

		ret = sdw_slave_clk_stop_callback(slave, slave_mode,
						  SDW_CLK_PRE_PREPARE);
		if (ret < 0) {
			dev_err(&slave->dev,
				"pre-prepare failed:%d", ret);
			return ret;
		}

		ret = sdw_slave_clk_stop_prepare(slave,
						 slave_mode, true);
		if (ret < 0) {
			dev_err(&slave->dev,
				"pre-prepare failed:%d", ret);
			return ret;
		}

		if (slave_mode == SDW_CLK_STOP_MODE1)
			simple_clk_stop = false;
	}

	if (is_slave && !simple_clk_stop) {
		ret = sdw_bus_wait_for_clk_prep_deprep(bus,
						       SDW_BROADCAST_DEV_NUM);
		if (ret < 0)
			return ret;
	}

	/* Don't need to inform slaves if there is no slave attached */
	if (!is_slave)
		return ret;

	/* Inform slaves that prep is done */
	list_for_each_entry(slave, &bus->slaves, node) {
		if (!slave->dev_num)
			continue;

		if (slave->status != SDW_SLAVE_ATTACHED &&
		    slave->status != SDW_SLAVE_ALERT)
			continue;

		slave_mode = slave->curr_clk_stop_mode;

		if (slave_mode == SDW_CLK_STOP_MODE1) {
			ret = sdw_slave_clk_stop_callback(slave,
							  slave_mode,
							  SDW_CLK_POST_PREPARE);

			if (ret < 0) {
				dev_err(&slave->dev,
					"post-prepare failed:%d", ret);
			}
		}
	}

	return ret;
}
EXPORT_SYMBOL(sdw_bus_prep_clk_stop);

/**
 * sdw_bus_clk_stop: stop bus clock
 *
 * @bus: SDW bus instance
 *
 * After preparing the Slaves for clock stop, stop the clock by broadcasting
 * write to SCP_CTRL register.
 */
int sdw_bus_clk_stop(struct sdw_bus *bus)
{
	int ret;

	/*
	 * broadcast clock stop now, attached Slaves will ACK this,
	 * unattached will ignore
	 */
	ret = sdw_bwrite_no_pm(bus, SDW_BROADCAST_DEV_NUM,
			       SDW_SCP_CTRL, SDW_SCP_CTRL_CLK_STP_NOW);
	if (ret < 0) {
		if (ret == -ENODATA)
			dev_dbg(bus->dev,
				"ClockStopNow Broadcast msg ignored %d", ret);
		else
			dev_err(bus->dev,
				"ClockStopNow Broadcast msg failed %d", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sdw_bus_clk_stop);

/**
 * sdw_bus_exit_clk_stop: Exit clock stop mode
 *
 * @bus: SDW bus instance
 *
 * This De-prepares the Slaves by exiting Clock Stop Mode 0. For the Slaves
 * exiting Clock Stop Mode 1, they will be de-prepared after they enumerate
 * back.
 */
int sdw_bus_exit_clk_stop(struct sdw_bus *bus)
{
	enum sdw_clk_stop_mode mode;
	bool simple_clk_stop = true;
	struct sdw_slave *slave;
	bool is_slave = false;
	int ret;

	/*
	 * In order to save on transition time, de-prepare
	 * each Slave and then wait for all Slave(s) to be
	 * de-prepared after clock resume.
	 */
	list_for_each_entry(slave, &bus->slaves, node) {
		if (!slave->dev_num)
			continue;

		if (slave->status != SDW_SLAVE_ATTACHED &&
		    slave->status != SDW_SLAVE_ALERT)
			continue;

		/* Identify if Slave(s) are available on Bus */
		is_slave = true;

		mode = slave->curr_clk_stop_mode;

		if (mode == SDW_CLK_STOP_MODE1) {
			simple_clk_stop = false;
			continue;
		}

		ret = sdw_slave_clk_stop_callback(slave, mode,
						  SDW_CLK_PRE_DEPREPARE);
		if (ret < 0)
			dev_warn(&slave->dev,
				 "clk stop deprep failed:%d", ret);

		ret = sdw_slave_clk_stop_prepare(slave, mode,
						 false);

		if (ret < 0)
			dev_warn(&slave->dev,
				 "clk stop deprep failed:%d", ret);
	}

	if (is_slave && !simple_clk_stop)
		sdw_bus_wait_for_clk_prep_deprep(bus, SDW_BROADCAST_DEV_NUM);

	/*
	 * Don't need to call slave callback function if there is no slave
	 * attached
	 */
	if (!is_slave)
		return 0;

	list_for_each_entry(slave, &bus->slaves, node) {
		if (!slave->dev_num)
			continue;

		if (slave->status != SDW_SLAVE_ATTACHED &&
		    slave->status != SDW_SLAVE_ALERT)
			continue;

		mode = slave->curr_clk_stop_mode;
		sdw_slave_clk_stop_callback(slave, mode,
					    SDW_CLK_POST_DEPREPARE);
	}

	return 0;
}
EXPORT_SYMBOL(sdw_bus_exit_clk_stop);

int sdw_configure_dpn_intr(struct sdw_slave *slave,
			   int port, bool enable, int mask)
{
	u32 addr;
	int ret;
	u8 val = 0;

	if (slave->bus->params.s_data_mode != SDW_PORT_DATA_MODE_NORMAL) {
		dev_dbg(&slave->dev, "TEST FAIL interrupt %s\n",
			enable ? "on" : "off");
		mask |= SDW_DPN_INT_TEST_FAIL;
	}

	addr = SDW_DPN_INTMASK(port);

	/* Set/Clear port ready interrupt mask */
	if (enable) {
		val |= mask;
		val |= SDW_DPN_INT_PORT_READY;
	} else {
		val &= ~(mask);
		val &= ~SDW_DPN_INT_PORT_READY;
	}

	ret = sdw_update(slave, addr, (mask | SDW_DPN_INT_PORT_READY), val);
	if (ret < 0)
		dev_err(slave->bus->dev,
			"SDW_DPN_INTMASK write failed:%d\n", val);

	return ret;
}

static int sdw_slave_set_frequency(struct sdw_slave *slave)
{
	u32 mclk_freq = slave->bus->prop.mclk_freq;
	u32 curr_freq = slave->bus->params.curr_dr_freq >> 1;
	unsigned int scale;
	u8 scale_index;
	u8 base;
	int ret;

	/*
	 * frequency base and scale registers are required for SDCA
	 * devices. They may also be used for 1.2+/non-SDCA devices,
	 * but we will need a DisCo property to cover this case
	 */
	if (!slave->id.class_id)
		return 0;

	if (!mclk_freq) {
		dev_err(&slave->dev,
			"no bus MCLK, cannot set SDW_SCP_BUS_CLOCK_BASE\n");
		return -EINVAL;
	}

	/*
	 * map base frequency using Table 89 of SoundWire 1.2 spec.
	 * The order of the tests just follows the specification, this
	 * is not a selection between possible values or a search for
	 * the best value but just a mapping.  Only one case per platform
	 * is relevant.
	 * Some BIOS have inconsistent values for mclk_freq but a
	 * correct root so we force the mclk_freq to avoid variations.
	 */
	if (!(19200000 % mclk_freq)) {
		mclk_freq = 19200000;
		base = SDW_SCP_BASE_CLOCK_19200000_HZ;
	} else if (!(24000000 % mclk_freq)) {
		mclk_freq = 24000000;
		base = SDW_SCP_BASE_CLOCK_24000000_HZ;
	} else if (!(24576000 % mclk_freq)) {
		mclk_freq = 24576000;
		base = SDW_SCP_BASE_CLOCK_24576000_HZ;
	} else if (!(22579200 % mclk_freq)) {
		mclk_freq = 22579200;
		base = SDW_SCP_BASE_CLOCK_22579200_HZ;
	} else if (!(32000000 % mclk_freq)) {
		mclk_freq = 32000000;
		base = SDW_SCP_BASE_CLOCK_32000000_HZ;
	} else {
		dev_err(&slave->dev,
			"Unsupported clock base, mclk %d\n",
			mclk_freq);
		return -EINVAL;
	}

	if (mclk_freq % curr_freq) {
		dev_err(&slave->dev,
			"mclk %d is not multiple of bus curr_freq %d\n",
			mclk_freq, curr_freq);
		return -EINVAL;
	}

	scale = mclk_freq / curr_freq;

	/*
	 * map scale to Table 90 of SoundWire 1.2 spec - and check
	 * that the scale is a power of two and maximum 64
	 */
	scale_index = ilog2(scale);

	if (BIT(scale_index) != scale || scale_index > 6) {
		dev_err(&slave->dev,
			"No match found for scale %d, bus mclk %d curr_freq %d\n",
			scale, mclk_freq, curr_freq);
		return -EINVAL;
	}
	scale_index++;

	ret = sdw_write_no_pm(slave, SDW_SCP_BUS_CLOCK_BASE, base);
	if (ret < 0) {
		dev_err(&slave->dev,
			"SDW_SCP_BUS_CLOCK_BASE write failed:%d\n", ret);
		return ret;
	}

	/* initialize scale for both banks */
	ret = sdw_write_no_pm(slave, SDW_SCP_BUSCLOCK_SCALE_B0, scale_index);
	if (ret < 0) {
		dev_err(&slave->dev,
			"SDW_SCP_BUSCLOCK_SCALE_B0 write failed:%d\n", ret);
		return ret;
	}
	ret = sdw_write_no_pm(slave, SDW_SCP_BUSCLOCK_SCALE_B1, scale_index);
	if (ret < 0)
		dev_err(&slave->dev,
			"SDW_SCP_BUSCLOCK_SCALE_B1 write failed:%d\n", ret);

	dev_dbg(&slave->dev,
		"Configured bus base %d, scale %d, mclk %d, curr_freq %d\n",
		base, scale_index, mclk_freq, curr_freq);

	return ret;
}

static int sdw_initialize_slave(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int ret;
	u8 val;

	ret = sdw_slave_set_frequency(slave);
	if (ret < 0)
		return ret;

	/*
	 * Set SCP_INT1_MASK register, typically bus clash and
	 * implementation-defined interrupt mask. The Parity detection
	 * may not always be correct on startup so its use is
	 * device-dependent, it might e.g. only be enabled in
	 * steady-state after a couple of frames.
	 */
	val = slave->prop.scp_int1_mask;

	/* Enable SCP interrupts */
	ret = sdw_update_no_pm(slave, SDW_SCP_INTMASK1, val, val);
	if (ret < 0) {
		dev_err(slave->bus->dev,
			"SDW_SCP_INTMASK1 write failed:%d\n", ret);
		return ret;
	}

	/* No need to continue if DP0 is not present */
	if (!slave->prop.dp0_prop)
		return 0;

	/* Enable DP0 interrupts */
	val = prop->dp0_prop->imp_def_interrupts;
	val |= SDW_DP0_INT_PORT_READY | SDW_DP0_INT_BRA_FAILURE;

	ret = sdw_update_no_pm(slave, SDW_DP0_INTMASK, val, val);
	if (ret < 0)
		dev_err(slave->bus->dev,
			"SDW_DP0_INTMASK read failed:%d\n", ret);
	return ret;
}

static int sdw_handle_dp0_interrupt(struct sdw_slave *slave, u8 *slave_status)
{
	u8 clear = 0, impl_int_mask;
	int status, status2, ret, count = 0;

	status = sdw_read(slave, SDW_DP0_INT);
	if (status < 0) {
		dev_err(slave->bus->dev,
			"SDW_DP0_INT read failed:%d\n", status);
		return status;
	}

	do {
		if (status & SDW_DP0_INT_TEST_FAIL) {
			dev_err(&slave->dev, "Test fail for port 0\n");
			clear |= SDW_DP0_INT_TEST_FAIL;
		}

		/*
		 * Assumption: PORT_READY interrupt will be received only for
		 * ports implementing Channel Prepare state machine (CP_SM)
		 */

		if (status & SDW_DP0_INT_PORT_READY) {
			complete(&slave->port_ready[0]);
			clear |= SDW_DP0_INT_PORT_READY;
		}

		if (status & SDW_DP0_INT_BRA_FAILURE) {
			dev_err(&slave->dev, "BRA failed\n");
			clear |= SDW_DP0_INT_BRA_FAILURE;
		}

		impl_int_mask = SDW_DP0_INT_IMPDEF1 |
			SDW_DP0_INT_IMPDEF2 | SDW_DP0_INT_IMPDEF3;

		if (status & impl_int_mask) {
			clear |= impl_int_mask;
			*slave_status = clear;
		}

		/* clear the interrupt */
		ret = sdw_write(slave, SDW_DP0_INT, clear);
		if (ret < 0) {
			dev_err(slave->bus->dev,
				"SDW_DP0_INT write failed:%d\n", ret);
			return ret;
		}

		/* Read DP0 interrupt again */
		status2 = sdw_read(slave, SDW_DP0_INT);
		if (status2 < 0) {
			dev_err(slave->bus->dev,
				"SDW_DP0_INT read failed:%d\n", status2);
			return status2;
		}
		status &= status2;

		count++;

		/* we can get alerts while processing so keep retrying */
	} while (status != 0 && count < SDW_READ_INTR_CLEAR_RETRY);

	if (count == SDW_READ_INTR_CLEAR_RETRY)
		dev_warn(slave->bus->dev, "Reached MAX_RETRY on DP0 read\n");

	return ret;
}

static int sdw_handle_port_interrupt(struct sdw_slave *slave,
				     int port, u8 *slave_status)
{
	u8 clear = 0, impl_int_mask;
	int status, status2, ret, count = 0;
	u32 addr;

	if (port == 0)
		return sdw_handle_dp0_interrupt(slave, slave_status);

	addr = SDW_DPN_INT(port);
	status = sdw_read(slave, addr);
	if (status < 0) {
		dev_err(slave->bus->dev,
			"SDW_DPN_INT read failed:%d\n", status);

		return status;
	}

	do {
		if (status & SDW_DPN_INT_TEST_FAIL) {
			dev_err(&slave->dev, "Test fail for port:%d\n", port);
			clear |= SDW_DPN_INT_TEST_FAIL;
		}

		/*
		 * Assumption: PORT_READY interrupt will be received only
		 * for ports implementing CP_SM.
		 */
		if (status & SDW_DPN_INT_PORT_READY) {
			complete(&slave->port_ready[port]);
			clear |= SDW_DPN_INT_PORT_READY;
		}

		impl_int_mask = SDW_DPN_INT_IMPDEF1 |
			SDW_DPN_INT_IMPDEF2 | SDW_DPN_INT_IMPDEF3;

		if (status & impl_int_mask) {
			clear |= impl_int_mask;
			*slave_status = clear;
		}

		/* clear the interrupt */
		ret = sdw_write(slave, addr, clear);
		if (ret < 0) {
			dev_err(slave->bus->dev,
				"SDW_DPN_INT write failed:%d\n", ret);
			return ret;
		}

		/* Read DPN interrupt again */
		status2 = sdw_read(slave, addr);
		if (status2 < 0) {
			dev_err(slave->bus->dev,
				"SDW_DPN_INT read failed:%d\n", status2);
			return status2;
		}
		status &= status2;

		count++;

		/* we can get alerts while processing so keep retrying */
	} while (status != 0 && count < SDW_READ_INTR_CLEAR_RETRY);

	if (count == SDW_READ_INTR_CLEAR_RETRY)
		dev_warn(slave->bus->dev, "Reached MAX_RETRY on port read");

	return ret;
}

static int sdw_handle_slave_alerts(struct sdw_slave *slave)
{
	struct sdw_slave_intr_status slave_intr;
	u8 clear = 0, bit, port_status[15] = {0};
	int port_num, stat, ret, count = 0;
	unsigned long port;
	bool slave_notify = false;
	u8 buf, buf2[2], _buf, _buf2[2];
	bool parity_check;
	bool parity_quirk;

	sdw_modify_slave_status(slave, SDW_SLAVE_ALERT);

	ret = pm_runtime_get_sync(&slave->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err(&slave->dev, "Failed to resume device: %d\n", ret);
		pm_runtime_put_noidle(&slave->dev);
		return ret;
	}

	/* Read Intstat 1, Intstat 2 and Intstat 3 registers */
	ret = sdw_read(slave, SDW_SCP_INT1);
	if (ret < 0) {
		dev_err(slave->bus->dev,
			"SDW_SCP_INT1 read failed:%d\n", ret);
		goto io_err;
	}
	buf = ret;

	ret = sdw_nread(slave, SDW_SCP_INTSTAT2, 2, buf2);
	if (ret < 0) {
		dev_err(slave->bus->dev,
			"SDW_SCP_INT2/3 read failed:%d\n", ret);
		goto io_err;
	}

	do {
		/*
		 * Check parity, bus clash and Slave (impl defined)
		 * interrupt
		 */
		if (buf & SDW_SCP_INT1_PARITY) {
			parity_check = slave->prop.scp_int1_mask & SDW_SCP_INT1_PARITY;
			parity_quirk = !slave->first_interrupt_done &&
				(slave->prop.quirks & SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY);

			if (parity_check && !parity_quirk)
				dev_err(&slave->dev, "Parity error detected\n");
			clear |= SDW_SCP_INT1_PARITY;
		}

		if (buf & SDW_SCP_INT1_BUS_CLASH) {
			if (slave->prop.scp_int1_mask & SDW_SCP_INT1_BUS_CLASH)
				dev_err(&slave->dev, "Bus clash detected\n");
			clear |= SDW_SCP_INT1_BUS_CLASH;
		}

		/*
		 * When bus clash or parity errors are detected, such errors
		 * are unlikely to be recoverable errors.
		 * TODO: In such scenario, reset bus. Make this configurable
		 * via sysfs property with bus reset being the default.
		 */

		if (buf & SDW_SCP_INT1_IMPL_DEF) {
			if (slave->prop.scp_int1_mask & SDW_SCP_INT1_IMPL_DEF) {
				dev_dbg(&slave->dev, "Slave impl defined interrupt\n");
				slave_notify = true;
			}
			clear |= SDW_SCP_INT1_IMPL_DEF;
		}

		/* Check port 0 - 3 interrupts */
		port = buf & SDW_SCP_INT1_PORT0_3;

		/* To get port number corresponding to bits, shift it */
		port = FIELD_GET(SDW_SCP_INT1_PORT0_3, port);
		for_each_set_bit(bit, &port, 8) {
			sdw_handle_port_interrupt(slave, bit,
						  &port_status[bit]);
		}

		/* Check if cascade 2 interrupt is present */
		if (buf & SDW_SCP_INT1_SCP2_CASCADE) {
			port = buf2[0] & SDW_SCP_INTSTAT2_PORT4_10;
			for_each_set_bit(bit, &port, 8) {
				/* scp2 ports start from 4 */
				port_num = bit + 3;
				sdw_handle_port_interrupt(slave,
						port_num,
						&port_status[port_num]);
			}
		}

		/* now check last cascade */
		if (buf2[0] & SDW_SCP_INTSTAT2_SCP3_CASCADE) {
			port = buf2[1] & SDW_SCP_INTSTAT3_PORT11_14;
			for_each_set_bit(bit, &port, 8) {
				/* scp3 ports start from 11 */
				port_num = bit + 10;
				sdw_handle_port_interrupt(slave,
						port_num,
						&port_status[port_num]);
			}
		}

		/* Update the Slave driver */
		if (slave_notify && slave->ops &&
		    slave->ops->interrupt_callback) {
			slave_intr.control_port = clear;
			memcpy(slave_intr.port, &port_status,
			       sizeof(slave_intr.port));

			slave->ops->interrupt_callback(slave, &slave_intr);
		}

		/* Ack interrupt */
		ret = sdw_write(slave, SDW_SCP_INT1, clear);
		if (ret < 0) {
			dev_err(slave->bus->dev,
				"SDW_SCP_INT1 write failed:%d\n", ret);
			goto io_err;
		}

		/* at this point all initial interrupt sources were handled */
		slave->first_interrupt_done = true;

		/*
		 * Read status again to ensure no new interrupts arrived
		 * while servicing interrupts.
		 */
		ret = sdw_read(slave, SDW_SCP_INT1);
		if (ret < 0) {
			dev_err(slave->bus->dev,
				"SDW_SCP_INT1 read failed:%d\n", ret);
			goto io_err;
		}
		_buf = ret;

		ret = sdw_nread(slave, SDW_SCP_INTSTAT2, 2, _buf2);
		if (ret < 0) {
			dev_err(slave->bus->dev,
				"SDW_SCP_INT2/3 read failed:%d\n", ret);
			goto io_err;
		}

		/* Make sure no interrupts are pending */
		buf &= _buf;
		buf2[0] &= _buf2[0];
		buf2[1] &= _buf2[1];
		stat = buf || buf2[0] || buf2[1];

		/*
		 * Exit loop if Slave is continuously in ALERT state even
		 * after servicing the interrupt multiple times.
		 */
		count++;

		/* we can get alerts while processing so keep retrying */
	} while (stat != 0 && count < SDW_READ_INTR_CLEAR_RETRY);

	if (count == SDW_READ_INTR_CLEAR_RETRY)
		dev_warn(slave->bus->dev, "Reached MAX_RETRY on alert read\n");

io_err:
	pm_runtime_mark_last_busy(&slave->dev);
	pm_runtime_put_autosuspend(&slave->dev);

	return ret;
}

static int sdw_update_slave_status(struct sdw_slave *slave,
				   enum sdw_slave_status status)
{
	unsigned long time;

	if (!slave->probed) {
		/*
		 * the slave status update is typically handled in an
		 * interrupt thread, which can race with the driver
		 * probe, e.g. when a module needs to be loaded.
		 *
		 * make sure the probe is complete before updating
		 * status.
		 */
		time = wait_for_completion_timeout(&slave->probe_complete,
				msecs_to_jiffies(DEFAULT_PROBE_TIMEOUT));
		if (!time) {
			dev_err(&slave->dev, "Probe not complete, timed out\n");
			return -ETIMEDOUT;
		}
	}

	if (!slave->ops || !slave->ops->update_status)
		return 0;

	return slave->ops->update_status(slave, status);
}

/**
 * sdw_handle_slave_status() - Handle Slave status
 * @bus: SDW bus instance
 * @status: Status for all Slave(s)
 */
int sdw_handle_slave_status(struct sdw_bus *bus,
			    enum sdw_slave_status status[])
{
	enum sdw_slave_status prev_status;
	struct sdw_slave *slave;
	bool attached_initializing;
	int i, ret = 0;

	/* first check if any Slaves fell off the bus */
	for (i = 1; i <= SDW_MAX_DEVICES; i++) {
		mutex_lock(&bus->bus_lock);
		if (test_bit(i, bus->assigned) == false) {
			mutex_unlock(&bus->bus_lock);
			continue;
		}
		mutex_unlock(&bus->bus_lock);

		slave = sdw_get_slave(bus, i);
		if (!slave)
			continue;

		if (status[i] == SDW_SLAVE_UNATTACHED &&
		    slave->status != SDW_SLAVE_UNATTACHED)
			sdw_modify_slave_status(slave, SDW_SLAVE_UNATTACHED);
	}

	if (status[0] == SDW_SLAVE_ATTACHED) {
		dev_dbg(bus->dev, "Slave attached, programming device number\n");
		ret = sdw_program_device_num(bus);
		if (ret)
			dev_err(bus->dev, "Slave attach failed: %d\n", ret);
		/*
		 * programming a device number will have side effects,
		 * so we deal with other devices at a later time
		 */
		return ret;
	}

	/* Continue to check other slave statuses */
	for (i = 1; i <= SDW_MAX_DEVICES; i++) {
		mutex_lock(&bus->bus_lock);
		if (test_bit(i, bus->assigned) == false) {
			mutex_unlock(&bus->bus_lock);
			continue;
		}
		mutex_unlock(&bus->bus_lock);

		slave = sdw_get_slave(bus, i);
		if (!slave)
			continue;

		attached_initializing = false;

		switch (status[i]) {
		case SDW_SLAVE_UNATTACHED:
			if (slave->status == SDW_SLAVE_UNATTACHED)
				break;

			sdw_modify_slave_status(slave, SDW_SLAVE_UNATTACHED);
			break;

		case SDW_SLAVE_ALERT:
			ret = sdw_handle_slave_alerts(slave);
			if (ret)
				dev_err(bus->dev,
					"Slave %d alert handling failed: %d\n",
					i, ret);
			break;

		case SDW_SLAVE_ATTACHED:
			if (slave->status == SDW_SLAVE_ATTACHED)
				break;

			prev_status = slave->status;
			sdw_modify_slave_status(slave, SDW_SLAVE_ATTACHED);

			if (prev_status == SDW_SLAVE_ALERT)
				break;

			attached_initializing = true;

			ret = sdw_initialize_slave(slave);
			if (ret)
				dev_err(bus->dev,
					"Slave %d initialization failed: %d\n",
					i, ret);

			break;

		default:
			dev_err(bus->dev, "Invalid slave %d status:%d\n",
				i, status[i]);
			break;
		}

		ret = sdw_update_slave_status(slave, status[i]);
		if (ret)
			dev_err(slave->bus->dev,
				"Update Slave status failed:%d\n", ret);
		if (attached_initializing)
			complete(&slave->initialization_complete);
	}

	return ret;
}
EXPORT_SYMBOL(sdw_handle_slave_status);

void sdw_clear_slave_status(struct sdw_bus *bus, u32 request)
{
	struct sdw_slave *slave;
	int i;

	/* Check all non-zero devices */
	for (i = 1; i <= SDW_MAX_DEVICES; i++) {
		mutex_lock(&bus->bus_lock);
		if (test_bit(i, bus->assigned) == false) {
			mutex_unlock(&bus->bus_lock);
			continue;
		}
		mutex_unlock(&bus->bus_lock);

		slave = sdw_get_slave(bus, i);
		if (!slave)
			continue;

		if (slave->status != SDW_SLAVE_UNATTACHED) {
			sdw_modify_slave_status(slave, SDW_SLAVE_UNATTACHED);
			slave->first_interrupt_done = false;
		}

		/* keep track of request, used in pm_runtime resume */
		slave->unattach_request = request;
	}
}
EXPORT_SYMBOL(sdw_clear_slave_status);
