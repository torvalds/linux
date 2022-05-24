// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Software Defined Silicon driver
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "vsec.h"

#define ACCESS_TYPE_BARID		2
#define ACCESS_TYPE_LOCAL		3

#define SDSI_MIN_SIZE_DWORDS		276
#define SDSI_SIZE_CONTROL		8
#define SDSI_SIZE_MAILBOX		1024
#define SDSI_SIZE_REGS			72
#define SDSI_SIZE_CMD			sizeof(u64)

/*
 * Write messages are currently up to the size of the mailbox
 * while read messages are up to 4 times the size of the
 * mailbox, sent in packets
 */
#define SDSI_SIZE_WRITE_MSG		SDSI_SIZE_MAILBOX
#define SDSI_SIZE_READ_MSG		(SDSI_SIZE_MAILBOX * 4)

#define SDSI_ENABLED_FEATURES_OFFSET	16
#define SDSI_ENABLED			BIT(3)
#define SDSI_SOCKET_ID_OFFSET		64
#define SDSI_SOCKET_ID			GENMASK(3, 0)

#define SDSI_MBOX_CMD_SUCCESS		0x40
#define SDSI_MBOX_CMD_TIMEOUT		0x80

#define MBOX_TIMEOUT_US			2000
#define MBOX_TIMEOUT_ACQUIRE_US		1000
#define MBOX_POLLING_PERIOD_US		100
#define MBOX_ACQUIRE_NUM_RETRIES	5
#define MBOX_ACQUIRE_RETRY_DELAY_MS	500
#define MBOX_MAX_PACKETS		4

#define MBOX_OWNER_NONE			0x00
#define MBOX_OWNER_INBAND		0x01

#define CTRL_RUN_BUSY			BIT(0)
#define CTRL_READ_WRITE			BIT(1)
#define CTRL_SOM			BIT(2)
#define CTRL_EOM			BIT(3)
#define CTRL_OWNER			GENMASK(5, 4)
#define CTRL_COMPLETE			BIT(6)
#define CTRL_READY			BIT(7)
#define CTRL_STATUS			GENMASK(15, 8)
#define CTRL_PACKET_SIZE		GENMASK(31, 16)
#define CTRL_MSG_SIZE			GENMASK(63, 48)

#define DISC_TABLE_SIZE			12
#define DT_ACCESS_TYPE			GENMASK(3, 0)
#define DT_SIZE				GENMASK(27, 12)
#define DT_TBIR				GENMASK(2, 0)
#define DT_OFFSET(v)			((v) & GENMASK(31, 3))

enum sdsi_command {
	SDSI_CMD_PROVISION_AKC		= 0x04,
	SDSI_CMD_PROVISION_CAP		= 0x08,
	SDSI_CMD_READ_STATE		= 0x10,
};

struct sdsi_mbox_info {
	u64	*payload;
	void	*buffer;
	int	size;
};

struct disc_table {
	u32	access_info;
	u32	guid;
	u32	offset;
};

struct sdsi_priv {
	struct mutex		mb_lock;	/* Mailbox access lock */
	struct device		*dev;
	void __iomem		*control_addr;
	void __iomem		*mbox_addr;
	void __iomem		*regs_addr;
	u32			guid;
	bool			sdsi_enabled;
};

/* SDSi mailbox operations must be performed using 64bit mov instructions */
static __always_inline void
sdsi_memcpy64_toio(u64 __iomem *to, const u64 *from, size_t count_bytes)
{
	size_t count = count_bytes / sizeof(*to);
	int i;

	for (i = 0; i < count; i++)
		writeq(from[i], &to[i]);
}

static __always_inline void
sdsi_memcpy64_fromio(u64 *to, const u64 __iomem *from, size_t count_bytes)
{
	size_t count = count_bytes / sizeof(*to);
	int i;

	for (i = 0; i < count; i++)
		to[i] = readq(&from[i]);
}

static inline void sdsi_complete_transaction(struct sdsi_priv *priv)
{
	u64 control = FIELD_PREP(CTRL_COMPLETE, 1);

	lockdep_assert_held(&priv->mb_lock);
	writeq(control, priv->control_addr);
}

static int sdsi_status_to_errno(u32 status)
{
	switch (status) {
	case SDSI_MBOX_CMD_SUCCESS:
		return 0;
	case SDSI_MBOX_CMD_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static int sdsi_mbox_cmd_read(struct sdsi_priv *priv, struct sdsi_mbox_info *info,
			      size_t *data_size)
{
	struct device *dev = priv->dev;
	u32 total, loop, eom, status, message_size;
	u64 control;
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	/* Format and send the read command */
	control = FIELD_PREP(CTRL_EOM, 1) |
		  FIELD_PREP(CTRL_SOM, 1) |
		  FIELD_PREP(CTRL_RUN_BUSY, 1) |
		  FIELD_PREP(CTRL_PACKET_SIZE, info->size);
	writeq(control, priv->control_addr);

	/* For reads, data sizes that are larger than the mailbox size are read in packets. */
	total = 0;
	loop = 0;
	do {
		void *buf = info->buffer + (SDSI_SIZE_MAILBOX * loop);
		u32 packet_size;

		/* Poll on ready bit */
		ret = readq_poll_timeout(priv->control_addr, control, control & CTRL_READY,
					 MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_US);
		if (ret)
			break;

		eom = FIELD_GET(CTRL_EOM, control);
		status = FIELD_GET(CTRL_STATUS, control);
		packet_size = FIELD_GET(CTRL_PACKET_SIZE, control);
		message_size = FIELD_GET(CTRL_MSG_SIZE, control);

		ret = sdsi_status_to_errno(status);
		if (ret)
			break;

		/* Only the last packet can be less than the mailbox size. */
		if (!eom && packet_size != SDSI_SIZE_MAILBOX) {
			dev_err(dev, "Invalid packet size\n");
			ret = -EPROTO;
			break;
		}

		if (packet_size > SDSI_SIZE_MAILBOX) {
			dev_err(dev, "Packet size too large\n");
			ret = -EPROTO;
			break;
		}

		sdsi_memcpy64_fromio(buf, priv->mbox_addr, round_up(packet_size, SDSI_SIZE_CMD));

		total += packet_size;

		sdsi_complete_transaction(priv);
	} while (!eom && ++loop < MBOX_MAX_PACKETS);

	if (ret) {
		sdsi_complete_transaction(priv);
		return ret;
	}

	if (!eom) {
		dev_err(dev, "Exceeded read attempts\n");
		return -EPROTO;
	}

	/* Message size check is only valid for multi-packet transfers */
	if (loop && total != message_size)
		dev_warn(dev, "Read count %u differs from expected count %u\n",
			 total, message_size);

	*data_size = total;

	return 0;
}

static int sdsi_mbox_cmd_write(struct sdsi_priv *priv, struct sdsi_mbox_info *info)
{
	u64 control;
	u32 status;
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	/* Write rest of the payload */
	sdsi_memcpy64_toio(priv->mbox_addr + SDSI_SIZE_CMD, info->payload + 1,
			   info->size - SDSI_SIZE_CMD);

	/* Format and send the write command */
	control = FIELD_PREP(CTRL_EOM, 1) |
		  FIELD_PREP(CTRL_SOM, 1) |
		  FIELD_PREP(CTRL_RUN_BUSY, 1) |
		  FIELD_PREP(CTRL_READ_WRITE, 1) |
		  FIELD_PREP(CTRL_PACKET_SIZE, info->size);
	writeq(control, priv->control_addr);

	/* Poll on ready bit */
	ret = readq_poll_timeout(priv->control_addr, control, control & CTRL_READY,
				 MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_US);

	if (ret)
		goto release_mbox;

	status = FIELD_GET(CTRL_STATUS, control);
	ret = sdsi_status_to_errno(status);

release_mbox:
	sdsi_complete_transaction(priv);

	return ret;
}

static int sdsi_mbox_acquire(struct sdsi_priv *priv, struct sdsi_mbox_info *info)
{
	u64 control;
	u32 owner;
	int ret, retries = 0;

	lockdep_assert_held(&priv->mb_lock);

	/* Check mailbox is available */
	control = readq(priv->control_addr);
	owner = FIELD_GET(CTRL_OWNER, control);
	if (owner != MBOX_OWNER_NONE)
		return -EBUSY;

	/*
	 * If there has been no recent transaction and no one owns the mailbox,
	 * we should acquire it in under 1ms. However, if we've accessed it
	 * recently it may take up to 2.1 seconds to acquire it again.
	 */
	do {
		/* Write first qword of payload */
		writeq(info->payload[0], priv->mbox_addr);

		/* Check for ownership */
		ret = readq_poll_timeout(priv->control_addr, control,
			FIELD_GET(CTRL_OWNER, control) == MBOX_OWNER_INBAND,
			MBOX_POLLING_PERIOD_US, MBOX_TIMEOUT_ACQUIRE_US);

		if (FIELD_GET(CTRL_OWNER, control) == MBOX_OWNER_NONE &&
		    retries++ < MBOX_ACQUIRE_NUM_RETRIES) {
			msleep(MBOX_ACQUIRE_RETRY_DELAY_MS);
			continue;
		}

		/* Either we got it or someone else did. */
		break;
	} while (true);

	return ret;
}

static int sdsi_mbox_write(struct sdsi_priv *priv, struct sdsi_mbox_info *info)
{
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	ret = sdsi_mbox_acquire(priv, info);
	if (ret)
		return ret;

	return sdsi_mbox_cmd_write(priv, info);
}

static int sdsi_mbox_read(struct sdsi_priv *priv, struct sdsi_mbox_info *info, size_t *data_size)
{
	int ret;

	lockdep_assert_held(&priv->mb_lock);

	ret = sdsi_mbox_acquire(priv, info);
	if (ret)
		return ret;

	return sdsi_mbox_cmd_read(priv, info, data_size);
}

static ssize_t sdsi_provision(struct sdsi_priv *priv, char *buf, size_t count,
			      enum sdsi_command command)
{
	struct sdsi_mbox_info info;
	int ret;

	if (!priv->sdsi_enabled)
		return -EPERM;

	if (count > (SDSI_SIZE_WRITE_MSG - SDSI_SIZE_CMD))
		return -EOVERFLOW;

	/* Qword aligned message + command qword */
	info.size = round_up(count, SDSI_SIZE_CMD) + SDSI_SIZE_CMD;

	info.payload = kzalloc(info.size, GFP_KERNEL);
	if (!info.payload)
		return -ENOMEM;

	/* Copy message to payload buffer */
	memcpy(info.payload, buf, count);

	/* Command is last qword of payload buffer */
	info.payload[(info.size - SDSI_SIZE_CMD) / SDSI_SIZE_CMD] = command;

	ret = mutex_lock_interruptible(&priv->mb_lock);
	if (ret)
		goto free_payload;
	ret = sdsi_mbox_write(priv, &info);
	mutex_unlock(&priv->mb_lock);

free_payload:
	kfree(info.payload);

	if (ret)
		return ret;

	return count;
}

static ssize_t provision_akc_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf, loff_t off,
				   size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sdsi_priv *priv = dev_get_drvdata(dev);

	if (off)
		return -ESPIPE;

	return sdsi_provision(priv, buf, count, SDSI_CMD_PROVISION_AKC);
}
static BIN_ATTR_WO(provision_akc, SDSI_SIZE_WRITE_MSG);

static ssize_t provision_cap_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf, loff_t off,
				   size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sdsi_priv *priv = dev_get_drvdata(dev);

	if (off)
		return -ESPIPE;

	return sdsi_provision(priv, buf, count, SDSI_CMD_PROVISION_CAP);
}
static BIN_ATTR_WO(provision_cap, SDSI_SIZE_WRITE_MSG);

static long state_certificate_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf, loff_t off,
				   size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sdsi_priv *priv = dev_get_drvdata(dev);
	u64 command = SDSI_CMD_READ_STATE;
	struct sdsi_mbox_info info;
	size_t size;
	int ret;

	if (!priv->sdsi_enabled)
		return -EPERM;

	if (off)
		return 0;

	/* Buffer for return data */
	info.buffer = kmalloc(SDSI_SIZE_READ_MSG, GFP_KERNEL);
	if (!info.buffer)
		return -ENOMEM;

	info.payload = &command;
	info.size = sizeof(command);

	ret = mutex_lock_interruptible(&priv->mb_lock);
	if (ret)
		goto free_buffer;
	ret = sdsi_mbox_read(priv, &info, &size);
	mutex_unlock(&priv->mb_lock);
	if (ret < 0)
		goto free_buffer;

	if (size > count)
		size = count;

	memcpy(buf, info.buffer, size);

free_buffer:
	kfree(info.buffer);

	if (ret)
		return ret;

	return size;
}
static BIN_ATTR(state_certificate, 0400, state_certificate_read, NULL, SDSI_SIZE_READ_MSG);

static ssize_t registers_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr, char *buf, loff_t off,
			      size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct sdsi_priv *priv = dev_get_drvdata(dev);
	void __iomem *addr = priv->regs_addr;

	memcpy_fromio(buf, addr + off, count);

	return count;
}
static BIN_ATTR(registers, 0400, registers_read, NULL, SDSI_SIZE_REGS);

static struct bin_attribute *sdsi_bin_attrs[] = {
	&bin_attr_registers,
	&bin_attr_state_certificate,
	&bin_attr_provision_akc,
	&bin_attr_provision_cap,
	NULL
};

static ssize_t guid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sdsi_priv *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", priv->guid);
}
static DEVICE_ATTR_RO(guid);

static struct attribute *sdsi_attrs[] = {
	&dev_attr_guid.attr,
	NULL
};

static const struct attribute_group sdsi_group = {
	.attrs = sdsi_attrs,
	.bin_attrs = sdsi_bin_attrs,
};
__ATTRIBUTE_GROUPS(sdsi);

static int sdsi_map_mbox_registers(struct sdsi_priv *priv, struct pci_dev *parent,
				   struct disc_table *disc_table, struct resource *disc_res)
{
	u32 access_type = FIELD_GET(DT_ACCESS_TYPE, disc_table->access_info);
	u32 size = FIELD_GET(DT_SIZE, disc_table->access_info);
	u32 tbir = FIELD_GET(DT_TBIR, disc_table->offset);
	u32 offset = DT_OFFSET(disc_table->offset);
	u32 features_offset;
	struct resource res = {};

	/* Starting location of SDSi MMIO region based on access type */
	switch (access_type) {
	case ACCESS_TYPE_LOCAL:
		if (tbir) {
			dev_err(priv->dev, "Unsupported BAR index %u for access type %u\n",
				tbir, access_type);
			return -EINVAL;
		}

		/*
		 * For access_type LOCAL, the base address is as follows:
		 * base address = end of discovery region + base offset + 1
		 */
		res.start = disc_res->end + offset + 1;
		break;

	case ACCESS_TYPE_BARID:
		res.start = pci_resource_start(parent, tbir) + offset;
		break;

	default:
		dev_err(priv->dev, "Unrecognized access_type %u\n", access_type);
		return -EINVAL;
	}

	res.end = res.start + size * sizeof(u32) - 1;
	res.flags = IORESOURCE_MEM;

	priv->control_addr = devm_ioremap_resource(priv->dev, &res);
	if (IS_ERR(priv->control_addr))
		return PTR_ERR(priv->control_addr);

	priv->mbox_addr = priv->control_addr + SDSI_SIZE_CONTROL;
	priv->regs_addr = priv->mbox_addr + SDSI_SIZE_MAILBOX;

	features_offset = readq(priv->regs_addr + SDSI_ENABLED_FEATURES_OFFSET);
	priv->sdsi_enabled = !!(features_offset & SDSI_ENABLED);

	return 0;
}

static int sdsi_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	struct intel_vsec_device *intel_cap_dev = auxdev_to_ivdev(auxdev);
	struct disc_table disc_table;
	struct resource *disc_res;
	void __iomem *disc_addr;
	struct sdsi_priv *priv;
	int ret;

	priv = devm_kzalloc(&auxdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &auxdev->dev;
	mutex_init(&priv->mb_lock);
	auxiliary_set_drvdata(auxdev, priv);

	/* Get the SDSi discovery table */
	disc_res = &intel_cap_dev->resource[0];
	disc_addr = devm_ioremap_resource(&auxdev->dev, disc_res);
	if (IS_ERR(disc_addr))
		return PTR_ERR(disc_addr);

	memcpy_fromio(&disc_table, disc_addr, DISC_TABLE_SIZE);

	priv->guid = disc_table.guid;

	/* Map the SDSi mailbox registers */
	ret = sdsi_map_mbox_registers(priv, intel_cap_dev->pcidev, &disc_table, disc_res);
	if (ret)
		return ret;

	return 0;
}

static const struct auxiliary_device_id sdsi_aux_id_table[] = {
	{ .name = "intel_vsec.sdsi" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, sdsi_aux_id_table);

static struct auxiliary_driver sdsi_aux_driver = {
	.driver = {
		.dev_groups = sdsi_groups,
	},
	.id_table	= sdsi_aux_id_table,
	.probe		= sdsi_probe,
	/* No remove. All resources are handled under devm */
};
module_auxiliary_driver(sdsi_aux_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Software Defined Silicon driver");
MODULE_LICENSE("GPL");
