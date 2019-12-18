// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/bitfield.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/peci.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>

/* Mask for getting minor revision number from DIB */
#define REVISION_NUM_MASK	GENMASK(15, 8)

/* CRC8 table for Assured Write Frame Check */
#define PECI_CRC8_POLYNOMIAL	0x07
DECLARE_CRC8_TABLE(peci_crc8_table);

static bool is_registered;

static DEFINE_MUTEX(core_lock);
static DEFINE_IDR(peci_adapter_idr);

struct peci_adapter *peci_get_adapter(int nr)
{
	struct peci_adapter *adapter;

	mutex_lock(&core_lock);
	adapter = idr_find(&peci_adapter_idr, nr);
	if (!adapter)
		goto out_unlock;

	if (try_module_get(adapter->owner))
		get_device(&adapter->dev);
	else
		adapter = NULL;

out_unlock:
	mutex_unlock(&core_lock);

	return adapter;
}
EXPORT_SYMBOL_GPL(peci_get_adapter);

void peci_put_adapter(struct peci_adapter *adapter)
{
	if (!adapter)
		return;

	put_device(&adapter->dev);
	module_put(adapter->owner);
}
EXPORT_SYMBOL_GPL(peci_put_adapter);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "%s\n", dev->type == &peci_client_type ?
		       to_peci_client(dev)->name : to_peci_adapter(dev)->name);
}
static DEVICE_ATTR_RO(name);

static void peci_client_dev_release(struct device *dev)
{
	struct peci_client *client = to_peci_client(dev);

	dev_dbg(dev, "%s: %s\n", __func__, client->name);
	peci_put_adapter(client->adapter);
	kfree(client);
}

static struct attribute *peci_device_attrs[] = {
	&dev_attr_name.attr,
	NULL
};
ATTRIBUTE_GROUPS(peci_device);

struct device_type peci_client_type = {
	.groups		= peci_device_groups,
	.release	= peci_client_dev_release,
};
EXPORT_SYMBOL_GPL(peci_client_type);

/**
 * peci_verify_client - return parameter as peci_client, or NULL
 * @dev: device, probably from some driver model iterator
 *
 * Return: pointer to peci_client on success, else NULL.
 */
struct peci_client *peci_verify_client(struct device *dev)
{
	return (dev->type == &peci_client_type)
			? to_peci_client(dev)
			: NULL;
}
EXPORT_SYMBOL_GPL(peci_verify_client);

/**
 * peci_get_xfer_msg() - get a DMA safe peci_xfer_msg for the given tx and rx
 *			 length
 * @tx_len: the length of tx_buf. May be 0 if tx_buf isn't needed.
 * @rx_len: the length of rx_buf. May be 0 if rx_buf isn't needed.
 *
 * Return: NULL if a DMA safe buffer was not obtained.
 *	   Or a valid pointer to be used with DMA. After use, release it by
 *	   calling peci_put_xfer_msg().
 *
 * This function must only be called from process context!
 */
struct peci_xfer_msg *peci_get_xfer_msg(u8 tx_len, u8 rx_len)
{
	struct peci_xfer_msg *msg;
	u8 *tx_buf, *rx_buf;

	if (tx_len) {
		tx_buf = kzalloc(tx_len, GFP_KERNEL);
		if (!tx_buf)
			return NULL;
	} else {
		tx_buf = NULL;
	}

	if (rx_len) {
		rx_buf = kzalloc(rx_len, GFP_KERNEL);
		if (!rx_buf)
			goto err_free_tx_buf;
	} else {
		rx_buf = NULL;
	}

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		goto err_free_tx_rx_buf;

	msg->tx_len = tx_len;
	msg->tx_buf = tx_buf;
	msg->rx_len = rx_len;
	msg->rx_buf = rx_buf;

	return msg;

err_free_tx_rx_buf:
	kfree(rx_buf);
err_free_tx_buf:
	kfree(tx_buf);

	return NULL;
}
EXPORT_SYMBOL_GPL(peci_get_xfer_msg);

/**
 * peci_put_xfer_msg - release a DMA safe peci_xfer_msg
 * @msg: the message obtained from peci_get_xfer_msg(). May be NULL.
 */
void peci_put_xfer_msg(struct peci_xfer_msg *msg)
{
	if (!msg)
		return;

	kfree(msg->rx_buf);
	kfree(msg->tx_buf);
	kfree(msg);
}
EXPORT_SYMBOL_GPL(peci_put_xfer_msg);

/* Calculate an Assured Write Frame Check Sequence byte */
static int peci_aw_fcs(struct peci_xfer_msg *msg, int len, u8 *aw_fcs)
{
	u8 *tmp_buf;

	/* Allocate a temporary buffer to use a contiguous byte array */
	tmp_buf = kmalloc(len, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	tmp_buf[0] = msg->addr;
	tmp_buf[1] = msg->tx_len;
	tmp_buf[2] = msg->rx_len;
	memcpy(&tmp_buf[3], msg->tx_buf, len - 3);

	*aw_fcs = crc8(peci_crc8_table, tmp_buf, (size_t)len, 0);

	kfree(tmp_buf);

	return 0;
}

static int __peci_xfer(struct peci_adapter *adapter, struct peci_xfer_msg *msg,
		       bool do_retry, bool has_aw_fcs)
{
	uint interval_us = PECI_DEV_RETRY_INTERVAL_MIN_USEC;
	ulong timeout = jiffies;
	u8 aw_fcs;
	int ret;

	/*
	 * In case if adapter uses DMA, check at here whether tx and rx buffers
	 * are DMA capable or not.
	 */
	if (IS_ENABLED(CONFIG_HAS_DMA) && adapter->use_dma) {
		if (is_vmalloc_addr(msg->tx_buf) ||
		    is_vmalloc_addr(msg->rx_buf)) {
			WARN_ONCE(1, "xfer msg is not dma capable\n");
			return -EAGAIN;
		} else if (object_is_on_stack(msg->tx_buf) ||
			   object_is_on_stack(msg->rx_buf)) {
			WARN_ONCE(1, "xfer msg is on stack\n");
			return -EAGAIN;
		}
	}

	/*
	 * For some commands, the PECI originator may need to retry a command if
	 * the processor PECI client responds with a 0x8x completion code. In
	 * each instance, the processor PECI client may have started the
	 * operation but not completed it yet. When the 'retry' bit is set, the
	 * PECI client will ignore a new request if it exactly matches a
	 * previous valid request. For better performance and for reducing
	 * retry traffic, the interval time will be increased exponentially.
	 */

	if (do_retry)
		timeout += PECI_DEV_RETRY_TIMEOUT;

	for (;;) {
		ret = adapter->xfer(adapter, msg);

		if (!do_retry || ret || !msg->rx_buf)
			break;

		/* Retry is needed when completion code is 0x8x */
		if ((msg->rx_buf[0] & PECI_DEV_CC_RETRY_CHECK_MASK) !=
		    PECI_DEV_CC_NEED_RETRY)
			break;

		/* Set the retry bit to indicate a retry attempt */
		msg->tx_buf[1] |= PECI_DEV_RETRY_BIT;

		/* Recalculate the AW FCS if it has one */
		if (has_aw_fcs) {
			ret = peci_aw_fcs(msg, 2 + msg->tx_len, &aw_fcs);
			if (ret)
				break;

			msg->tx_buf[msg->tx_len - 1] = 0x80 ^ aw_fcs;
		}

		/* Retry it for 'timeout' before returning an error. */
		if (time_after(jiffies, timeout)) {
			dev_dbg(&adapter->dev, "Timeout retrying xfer!\n");
			ret = -ETIMEDOUT;
			break;
		}

		usleep_range(interval_us, interval_us * 2);

		interval_us *= 2;
		if (interval_us > PECI_DEV_RETRY_INTERVAL_MAX_USEC)
			interval_us = PECI_DEV_RETRY_INTERVAL_MAX_USEC;
	}

	if (ret)
		dev_dbg(&adapter->dev, "xfer error: %d\n", ret);

	return ret;
}

static int peci_xfer(struct peci_adapter *adapter, struct peci_xfer_msg *msg)
{
	return __peci_xfer(adapter, msg, false, false);
}

static int peci_xfer_with_retries(struct peci_adapter *adapter,
				  struct peci_xfer_msg *msg,
				  bool has_aw_fcs)
{
	return __peci_xfer(adapter, msg, true, has_aw_fcs);
}

static int peci_scan_cmd_mask(struct peci_adapter *adapter)
{
	struct peci_xfer_msg *msg;
	u8 revision;
	int ret;
	u64 dib;

	/* Update command mask just once */
	if (adapter->cmd_mask & BIT(PECI_CMD_XFER))
		return 0;

	msg = peci_get_xfer_msg(PECI_GET_DIB_WR_LEN, PECI_GET_DIB_RD_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr      = PECI_BASE_ADDR;
	msg->tx_buf[0] = PECI_GET_DIB_CMD;

	ret = peci_xfer(adapter, msg);
	if (ret)
		return ret;

	dib = le64_to_cpup((__le64 *)msg->rx_buf);

	/* Check special case for Get DIB command */
	if (dib == 0) {
		dev_dbg(&adapter->dev, "DIB read as 0\n");
		ret = -EIO;
		goto out;
	}

	/*
	 * Setting up the supporting commands based on revision number.
	 * See PECI Spec Table 3-1.
	 */
	revision = FIELD_GET(REVISION_NUM_MASK, dib);
	if (revision >= 0x40) { /* Rev. 4.0 */
		adapter->cmd_mask |= BIT(PECI_CMD_RD_IA_MSREX);
		adapter->cmd_mask |= BIT(PECI_CMD_RD_END_PT_CFG);
		adapter->cmd_mask |= BIT(PECI_CMD_WR_END_PT_CFG);
		adapter->cmd_mask |= BIT(PECI_CMD_CRASHDUMP_DISC);
		adapter->cmd_mask |= BIT(PECI_CMD_CRASHDUMP_GET_FRAME);
	}
	if (revision >= 0x36) /* Rev. 3.6 */
		adapter->cmd_mask |= BIT(PECI_CMD_WR_IA_MSR);
	if (revision >= 0x35) /* Rev. 3.5 */
		adapter->cmd_mask |= BIT(PECI_CMD_WR_PCI_CFG);
	if (revision >= 0x34) /* Rev. 3.4 */
		adapter->cmd_mask |= BIT(PECI_CMD_RD_PCI_CFG);
	if (revision >= 0x33) { /* Rev. 3.3 */
		adapter->cmd_mask |= BIT(PECI_CMD_RD_PCI_CFG_LOCAL);
		adapter->cmd_mask |= BIT(PECI_CMD_WR_PCI_CFG_LOCAL);
	}
	if (revision >= 0x32) /* Rev. 3.2 */
		adapter->cmd_mask |= BIT(PECI_CMD_RD_IA_MSR);
	if (revision >= 0x31) { /* Rev. 3.1 */
		adapter->cmd_mask |= BIT(PECI_CMD_RD_PKG_CFG);
		adapter->cmd_mask |= BIT(PECI_CMD_WR_PKG_CFG);
	}

	adapter->cmd_mask |= BIT(PECI_CMD_XFER);
	adapter->cmd_mask |= BIT(PECI_CMD_GET_TEMP);
	adapter->cmd_mask |= BIT(PECI_CMD_GET_DIB);
	adapter->cmd_mask |= BIT(PECI_CMD_PING);

out:
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_check_cmd_support(struct peci_adapter *adapter,
				  enum peci_cmd cmd)
{
	if (!(adapter->cmd_mask & BIT(PECI_CMD_PING)) &&
	    peci_scan_cmd_mask(adapter) < 0) {
		dev_dbg(&adapter->dev, "Failed to scan command mask\n");
		return -EIO;
	}

	if (!(adapter->cmd_mask & BIT(cmd))) {
		dev_dbg(&adapter->dev, "Command %d is not supported\n", cmd);
		return -EINVAL;
	}

	return 0;
}

static int peci_cmd_xfer(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_xfer_msg *msg = vmsg;
	u8 aw_fcs;
	int ret;

	if (!msg->tx_len) {
		ret = peci_xfer(adapter, msg);
	} else {
		switch (msg->tx_buf[0]) {
		case PECI_GET_DIB_CMD:
		case PECI_GET_TEMP_CMD:
			ret = peci_xfer(adapter, msg);
			break;
		case PECI_WRPKGCFG_CMD:
		case PECI_WRIAMSR_CMD:
		case PECI_WRPCICFG_CMD:
		case PECI_WRPCICFGLOCAL_CMD:
		case PECI_WRENDPTCFG_CMD:
			/*
			 * The sender may not have supplied the AW FCS byte.
			 * Unconditionally add an Assured Write Frame Check
			 * Sequence byte
			 */
			ret = peci_aw_fcs(msg, 2 + msg->tx_len, &aw_fcs);
			if (ret)
				break;

			msg->tx_buf[msg->tx_len - 1] = 0x80 ^ aw_fcs;

			ret = peci_xfer_with_retries(adapter, msg, true);
			break;
		default:
			ret = peci_xfer_with_retries(adapter, msg, false);
			break;
		}
	}

	return ret;
}

static int peci_cmd_ping(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_ping_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	msg = peci_get_xfer_msg(0, 0);
	if (!msg)
		return -ENOMEM;

	msg->addr   = umsg->addr;

	ret = peci_xfer(adapter, msg);

	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_get_dib(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_get_dib_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	msg = peci_get_xfer_msg(PECI_GET_DIB_WR_LEN, PECI_GET_DIB_RD_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr      = umsg->addr;
	msg->tx_buf[0] = PECI_GET_DIB_CMD;

	ret = peci_xfer(adapter, msg);
	if (ret)
		goto out;

	umsg->dib = le64_to_cpup((__le64 *)msg->rx_buf);

out:
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_get_temp(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_get_temp_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	msg = peci_get_xfer_msg(PECI_GET_TEMP_WR_LEN, PECI_GET_TEMP_RD_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr      = umsg->addr;
	msg->tx_buf[0] = PECI_GET_TEMP_CMD;

	ret = peci_xfer(adapter, msg);
	if (ret)
		goto out;

	umsg->temp_raw = le16_to_cpup((__le16 *)msg->rx_buf);

out:
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_rd_pkg_cfg(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_pkg_cfg_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	/* Per the PECI spec, the read length must be a byte, word, or dword */
	if (umsg->rx_len != 1 && umsg->rx_len != 2 && umsg->rx_len != 4) {
		dev_dbg(&adapter->dev, "Invalid read length, rx_len: %d\n",
			umsg->rx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_RDPKGCFG_WRITE_LEN,
				PECI_RDPKGCFG_READ_LEN_BASE + umsg->rx_len);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_RDPKGCFG_CMD;
	msg->tx_buf[1] = 0;         /* request byte for Host ID | Retry bit */
				    /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = umsg->index;            /* RdPkgConfig index */
	msg->tx_buf[3] = (u8)umsg->param;        /* LSB - Config parameter */
	msg->tx_buf[4] = (u8)(umsg->param >> 8); /* MSB - Config parameter */

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->pkg_config, &msg->rx_buf[1], umsg->rx_len);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_wr_pkg_cfg(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_wr_pkg_cfg_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret, i;
	u8 aw_fcs;

	/* Per the PECI spec, the write length must be a dword */
	if (umsg->tx_len != 4) {
		dev_dbg(&adapter->dev, "Invalid write length, tx_len: %d\n",
			umsg->tx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_WRPKGCFG_WRITE_LEN_BASE + umsg->tx_len,
				PECI_WRPKGCFG_READ_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_WRPKGCFG_CMD;
	msg->tx_buf[1] = 0;         /* request byte for Host ID | Retry bit */
				   /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = umsg->index;            /* RdPkgConfig index */
	msg->tx_buf[3] = (u8)umsg->param;        /* LSB - Config parameter */
	msg->tx_buf[4] = (u8)(umsg->param >> 8); /* MSB - Config parameter */
	for (i = 0; i < umsg->tx_len; i++)
		msg->tx_buf[5 + i] = (u8)(umsg->value >> (i << 3));

	/* Add an Assured Write Frame Check Sequence byte */
	ret = peci_aw_fcs(msg, 8 + umsg->tx_len, &aw_fcs);
	if (ret)
		goto out;

	msg->tx_buf[5 + i] = 0x80 ^ aw_fcs;

	ret = peci_xfer_with_retries(adapter, msg, true);

out:
	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_rd_ia_msr(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_ia_msr_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	msg = peci_get_xfer_msg(PECI_RDIAMSR_WRITE_LEN, PECI_RDIAMSR_READ_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_RDIAMSR_CMD;
	msg->tx_buf[1] = 0;
	msg->tx_buf[2] = umsg->thread_id;
	msg->tx_buf[3] = (u8)umsg->address;
	msg->tx_buf[4] = (u8)(umsg->address >> 8);

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(&umsg->value, &msg->rx_buf[1], sizeof(uint64_t));

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_rd_ia_msrex(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_ia_msrex_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	msg = peci_get_xfer_msg(PECI_RDIAMSREX_WRITE_LEN,
				PECI_RDIAMSREX_READ_LEN);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_RDIAMSREX_CMD;
	msg->tx_buf[1] = 0;
	msg->tx_buf[2] = (u8)umsg->thread_id;
	msg->tx_buf[3] = (u8)(umsg->thread_id >> 8);
	msg->tx_buf[4] = (u8)umsg->address;
	msg->tx_buf[5] = (u8)(umsg->address >> 8);

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(&umsg->value, &msg->rx_buf[1], sizeof(uint64_t));

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_wr_ia_msr(struct peci_adapter *adapter, void *vmsg)
{
	return -ENOSYS; /* Not implemented yet */
}

static int peci_cmd_rd_pci_cfg(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_pci_cfg_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	u32 address;
	int ret;

	msg = peci_get_xfer_msg(PECI_RDPCICFG_WRITE_LEN,
				PECI_RDPCICFG_READ_LEN);
	if (!msg)
		return -ENOMEM;

	address = umsg->reg;                  /* [11:0]  - Register */
	address |= (u32)umsg->function << 12; /* [14:12] - Function */
	address |= (u32)umsg->device << 15;   /* [19:15] - Device   */
	address |= (u32)umsg->bus << 20;      /* [27:20] - Bus      */
					      /* [31:28] - Reserved */
	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_RDPCICFG_CMD;
	msg->tx_buf[1] = 0;         /* request byte for Host ID | Retry bit */
				   /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = (u8)address;         /* LSB - PCI Config Address */
	msg->tx_buf[3] = (u8)(address >> 8);  /* PCI Config Address */
	msg->tx_buf[4] = (u8)(address >> 16); /* PCI Config Address */
	msg->tx_buf[5] = (u8)(address >> 24); /* MSB - PCI Config Address */

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->pci_config, &msg->rx_buf[1], 4);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_wr_pci_cfg(struct peci_adapter *adapter, void *vmsg)
{
	return -ENOSYS; /* Not implemented yet */
}

static int peci_cmd_rd_pci_cfg_local(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_pci_cfg_local_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	u32 address;
	int ret;

	/* Per the PECI spec, the read length must be a byte, word, or dword */
	if (umsg->rx_len != 1 && umsg->rx_len != 2 && umsg->rx_len != 4) {
		dev_dbg(&adapter->dev, "Invalid read length, rx_len: %d\n",
			umsg->rx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_RDPCICFGLOCAL_WRITE_LEN,
				PECI_RDPCICFGLOCAL_READ_LEN_BASE +
				umsg->rx_len);
	if (!msg)
		return -ENOMEM;

	address = umsg->reg;                  /* [11:0]  - Register */
	address |= (u32)umsg->function << 12; /* [14:12] - Function */
	address |= (u32)umsg->device << 15;   /* [19:15] - Device   */
	address |= (u32)umsg->bus << 20;      /* [23:20] - Bus      */

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_RDPCICFGLOCAL_CMD;
	msg->tx_buf[1] = 0;         /* request byte for Host ID | Retry bit */
				    /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = (u8)address;      /* LSB - PCI Configuration Address */
	msg->tx_buf[3] = (u8)(address >> 8);  /* PCI Configuration Address */
	msg->tx_buf[4] = (u8)(address >> 16); /* PCI Configuration Address */

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->pci_config, &msg->rx_buf[1], umsg->rx_len);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_wr_pci_cfg_local(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_wr_pci_cfg_local_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	u32 address;
	int ret, i;
	u8 aw_fcs;

	/* Per the PECI spec, the write length must be a byte, word, or dword */
	if (umsg->tx_len != 1 && umsg->tx_len != 2 && umsg->tx_len != 4) {
		dev_dbg(&adapter->dev, "Invalid write length, tx_len: %d\n",
			umsg->tx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_WRPCICFGLOCAL_WRITE_LEN_BASE +
				umsg->tx_len, PECI_WRPCICFGLOCAL_READ_LEN);
	if (!msg)
		return -ENOMEM;

	address = umsg->reg;                  /* [11:0]  - Register */
	address |= (u32)umsg->function << 12; /* [14:12] - Function */
	address |= (u32)umsg->device << 15;   /* [19:15] - Device   */
	address |= (u32)umsg->bus << 20;      /* [23:20] - Bus      */

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_WRPCICFGLOCAL_CMD;
	msg->tx_buf[1] = 0;         /* request byte for Host ID | Retry bit */
				    /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = (u8)address;      /* LSB - PCI Configuration Address */
	msg->tx_buf[3] = (u8)(address >> 8);  /* PCI Configuration Address */
	msg->tx_buf[4] = (u8)(address >> 16); /* PCI Configuration Address */
	for (i = 0; i < umsg->tx_len; i++)
		msg->tx_buf[5 + i] = (u8)(umsg->value >> (i << 3));

	/* Add an Assured Write Frame Check Sequence byte */
	ret = peci_aw_fcs(msg, 8 + umsg->tx_len, &aw_fcs);
	if (ret)
		goto out;

	msg->tx_buf[5 + i] = 0x80 ^ aw_fcs;

	ret = peci_xfer_with_retries(adapter, msg, true);

out:
	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_rd_end_pt_cfg(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_rd_end_pt_cfg_msg *umsg = vmsg;
	struct peci_xfer_msg *msg = NULL;
	u32 address;
	u8 tx_size;
	int ret;

	switch (umsg->msg_type) {
	case PECI_ENDPTCFG_TYPE_LOCAL_PCI:
	case PECI_ENDPTCFG_TYPE_PCI:
		/*
		 * Per the PECI spec, the read length must be a byte, word,
		 * or dword
		 */
		if (umsg->rx_len != 1 && umsg->rx_len != 2 &&
		    umsg->rx_len != 4) {
			dev_dbg(&adapter->dev,
				"Invalid read length, rx_len: %d\n",
				umsg->rx_len);
			return -EINVAL;
		}

		msg = peci_get_xfer_msg(PECI_RDENDPTCFG_PCI_WRITE_LEN,
					PECI_RDENDPTCFG_READ_LEN_BASE +
					umsg->rx_len);
		if (!msg)
			return -ENOMEM;

		address = umsg->params.pci_cfg.reg; /* [11:0] - Register */
		address |= (u32)umsg->params.pci_cfg.function
			   << 12; /* [14:12] - Function */
		address |= (u32)umsg->params.pci_cfg.device
			   << 15; /* [19:15] - Device   */
		address |= (u32)umsg->params.pci_cfg.bus
			   << 20; /* [27:20] - Bus      */
				  /* [31:28] - Reserved */
		msg->addr = umsg->addr;
		msg->tx_buf[0] = PECI_RDENDPTCFG_CMD;
		msg->tx_buf[1] = 0x00; /* request byte for Host ID|Retry bit */
		msg->tx_buf[2] = umsg->msg_type;	   /* Message Type */
		msg->tx_buf[3] = 0x00;			   /* Endpoint ID */
		msg->tx_buf[4] = 0x00;			   /* Reserved */
		msg->tx_buf[5] = 0x00;			   /* Reserved */
		msg->tx_buf[6] = PECI_ENDPTCFG_ADDR_TYPE_PCI; /* Addr Type */
		msg->tx_buf[7] = umsg->params.pci_cfg.seg; /* PCI Segment */
		msg->tx_buf[8] = (u8)address; /* LSB - PCI Config Address */
		msg->tx_buf[9] = (u8)(address >> 8);   /* PCI Config Address */
		msg->tx_buf[10] = (u8)(address >> 16); /* PCI Config Address */
		msg->tx_buf[11] =
			(u8)(address >> 24); /* MSB - PCI Config Address */
		break;

	case PECI_ENDPTCFG_TYPE_MMIO:
		/*
		 * Per the PECI spec, the read length must be a byte, word,
		 * dword, or qword
		 */
		if (umsg->rx_len != 1 && umsg->rx_len != 2 &&
		    umsg->rx_len != 4 && umsg->rx_len != 8) {
			dev_dbg(&adapter->dev,
				"Invalid read length, rx_len: %d\n",
				umsg->rx_len);
			return -EINVAL;
		}
		/*
		 * Per the PECI spec, the address type must specify either DWORD
		 * or QWORD
		 */
		if (umsg->params.mmio.addr_type !=
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_D &&
		    umsg->params.mmio.addr_type !=
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q) {
			dev_dbg(&adapter->dev,
				"Invalid address type, addr_type: %d\n",
				umsg->params.mmio.addr_type);
			return -EINVAL;
		}

		if (umsg->params.mmio.addr_type ==
			PECI_ENDPTCFG_ADDR_TYPE_MMIO_D)
			tx_size = PECI_RDENDPTCFG_MMIO_D_WRITE_LEN;
		else
			tx_size = PECI_RDENDPTCFG_MMIO_Q_WRITE_LEN;
		msg = peci_get_xfer_msg(tx_size,
					PECI_RDENDPTCFG_READ_LEN_BASE +
					umsg->rx_len);
		if (!msg)
			return -ENOMEM;

		address = umsg->params.mmio.function; /* [2:0] - Function */
		address |= (u32)umsg->params.mmio.device
			   << 3; /* [7:3] - Device */

		msg->addr = umsg->addr;
		msg->tx_buf[0] = PECI_RDENDPTCFG_CMD;
		msg->tx_buf[1] = 0x00; /* request byte for Host ID|Retry bit */
		msg->tx_buf[2] = umsg->msg_type;	      /* Message Type */
		msg->tx_buf[3] = 0x00;			      /* Endpoint ID */
		msg->tx_buf[4] = 0x00;			      /* Reserved */
		msg->tx_buf[5] = umsg->params.mmio.bar;       /* BAR # */
		msg->tx_buf[6] = umsg->params.mmio.addr_type; /* Address Type */
		msg->tx_buf[7] = umsg->params.mmio.seg;       /* PCI Segment */
		msg->tx_buf[8] = (u8)address;	   /* Function/Device */
		msg->tx_buf[9] = umsg->params.mmio.bus; /* PCI Bus */
		msg->tx_buf[10] = (u8)umsg->params.mmio
					 .offset; /* LSB - Register Offset */
		msg->tx_buf[11] = (u8)(umsg->params.mmio.offset
				       >> 8); /* Register Offset */
		msg->tx_buf[12] = (u8)(umsg->params.mmio.offset
				       >> 16); /* Register Offset */
		msg->tx_buf[13] = (u8)(umsg->params.mmio.offset
				       >> 24); /* MSB - DWORD Register Offset */
		if (umsg->params.mmio.addr_type ==
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q) {
			msg->tx_buf[14] = (u8)(umsg->params.mmio.offset
					       >> 32); /* Register Offset */
			msg->tx_buf[15] = (u8)(umsg->params.mmio.offset
					       >> 40); /* Register Offset */
			msg->tx_buf[16] = (u8)(umsg->params.mmio.offset
					       >> 48); /* Register Offset */
			msg->tx_buf[17] =
				(u8)(umsg->params.mmio.offset
				     >> 56); /* MSB - QWORD Register Offset */
		}
		break;

	default:
		return -EINVAL;
	}

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->data, &msg->rx_buf[1], umsg->rx_len);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_wr_end_pt_cfg(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_wr_end_pt_cfg_msg *umsg = vmsg;
	struct peci_xfer_msg *msg = NULL;
	u8 tx_size, aw_fcs;
	int ret, i, idx;
	u32 address;

	switch (umsg->msg_type) {
	case PECI_ENDPTCFG_TYPE_LOCAL_PCI:
	case PECI_ENDPTCFG_TYPE_PCI:
		/*
		 * Per the PECI spec, the write length must be a byte, word,
		 * or dword
		 */
		if (umsg->tx_len != 1 && umsg->tx_len != 2 &&
		    umsg->tx_len != 4) {
			dev_dbg(&adapter->dev,
				"Invalid write length, tx_len: %d\n",
				umsg->tx_len);
			return -EINVAL;
		}

		msg = peci_get_xfer_msg(PECI_WRENDPTCFG_PCI_WRITE_LEN_BASE +
					umsg->tx_len, PECI_WRENDPTCFG_READ_LEN);
		if (!msg)
			return -ENOMEM;

		address = umsg->params.pci_cfg.reg; /* [11:0] - Register */
		address |= (u32)umsg->params.pci_cfg.function
			   << 12; /* [14:12] - Function */
		address |= (u32)umsg->params.pci_cfg.device
			   << 15; /* [19:15] - Device   */
		address |= (u32)umsg->params.pci_cfg.bus
			   << 20; /* [27:20] - Bus      */
				  /* [31:28] - Reserved */
		msg->addr = umsg->addr;
		msg->tx_buf[0] = PECI_WRENDPTCFG_CMD;
		msg->tx_buf[1] = 0x00; /* request byte for Host ID|Retry bit */
		msg->tx_buf[2] = umsg->msg_type;	   /* Message Type */
		msg->tx_buf[3] = 0x00;			   /* Endpoint ID */
		msg->tx_buf[4] = 0x00;			   /* Reserved */
		msg->tx_buf[5] = 0x00;			   /* Reserved */
		msg->tx_buf[6] = PECI_ENDPTCFG_ADDR_TYPE_PCI; /* Addr Type */
		msg->tx_buf[7] = umsg->params.pci_cfg.seg; /* PCI Segment */
		msg->tx_buf[8] = (u8)address; /* LSB - PCI Config Address */
		msg->tx_buf[9] = (u8)(address >> 8);   /* PCI Config Address */
		msg->tx_buf[10] = (u8)(address >> 16); /* PCI Config Address */
		msg->tx_buf[11] =
			(u8)(address >> 24); /* MSB - PCI Config Address */
		for (i = 0; i < umsg->tx_len; i++)
			msg->tx_buf[12 + i] = (u8)(umsg->value >> (i << 3));

		/* Add an Assured Write Frame Check Sequence byte */
		ret = peci_aw_fcs(msg, 15 + umsg->tx_len, &aw_fcs);
		if (ret)
			goto out;

		msg->tx_buf[12 + i] = 0x80 ^ aw_fcs;
		break;

	case PECI_ENDPTCFG_TYPE_MMIO:
		/*
		 * Per the PECI spec, the write length must be a byte, word,
		 * dword, or qword
		 */
		if (umsg->tx_len != 1 && umsg->tx_len != 2 &&
		    umsg->tx_len != 4 && umsg->tx_len != 8) {
			dev_dbg(&adapter->dev,
				"Invalid write length, tx_len: %d\n",
				umsg->tx_len);
			return -EINVAL;
		}
		/*
		 * Per the PECI spec, the address type must specify either DWORD
		 * or QWORD
		 */
		if (umsg->params.mmio.addr_type !=
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_D &&
		    umsg->params.mmio.addr_type !=
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q) {
			dev_dbg(&adapter->dev,
				"Invalid address type, addr_type: %d\n",
				umsg->params.mmio.addr_type);
			return -EINVAL;
		}

		if (umsg->params.mmio.addr_type ==
			PECI_ENDPTCFG_ADDR_TYPE_MMIO_D)
			tx_size = PECI_WRENDPTCFG_MMIO_D_WRITE_LEN_BASE +
				  umsg->tx_len;
		else
			tx_size = PECI_WRENDPTCFG_MMIO_Q_WRITE_LEN_BASE +
				  umsg->tx_len;
		msg = peci_get_xfer_msg(tx_size, PECI_WRENDPTCFG_READ_LEN);
		if (!msg)
			return -ENOMEM;

		address = umsg->params.mmio.function; /* [2:0] - Function */
		address |= (u32)umsg->params.mmio.device
			   << 3; /* [7:3] - Device */

		msg->addr = umsg->addr;
		msg->tx_buf[0] = PECI_WRENDPTCFG_CMD;
		msg->tx_buf[1] = 0x00; /* request byte for Host ID|Retry bit */
		msg->tx_buf[2] = umsg->msg_type;	      /* Message Type */
		msg->tx_buf[3] = 0x00;			      /* Endpoint ID */
		msg->tx_buf[4] = 0x00;			      /* Reserved */
		msg->tx_buf[5] = umsg->params.mmio.bar;       /* BAR # */
		msg->tx_buf[6] = umsg->params.mmio.addr_type; /* Address Type */
		msg->tx_buf[7] = umsg->params.mmio.seg;       /* PCI Segment */
		msg->tx_buf[8] = (u8)address;	   /* Function/Device */
		msg->tx_buf[9] = umsg->params.mmio.bus; /* PCI Bus */
		msg->tx_buf[10] = (u8)umsg->params.mmio
					 .offset; /* LSB - Register Offset */
		msg->tx_buf[11] = (u8)(umsg->params.mmio.offset
				       >> 8); /* Register Offset */
		msg->tx_buf[12] = (u8)(umsg->params.mmio.offset
				       >> 16); /* Register Offset */
		msg->tx_buf[13] = (u8)(umsg->params.mmio.offset
				       >> 24); /* MSB - DWORD Register Offset */
		if (umsg->params.mmio.addr_type ==
		    PECI_ENDPTCFG_ADDR_TYPE_MMIO_Q) {
			msg->tx_buf[14] = (u8)(umsg->params.mmio.offset
					       >> 32); /* Register Offset */
			msg->tx_buf[15] = (u8)(umsg->params.mmio.offset
					       >> 40); /* Register Offset */
			msg->tx_buf[16] = (u8)(umsg->params.mmio.offset
					       >> 48); /* Register Offset */
			msg->tx_buf[17] =
				(u8)(umsg->params.mmio.offset
				     >> 56); /* MSB - QWORD Register Offset */
			idx = 18;
		} else {
			idx = 14;
		}
		for (i = 0; i < umsg->tx_len; i++)
			msg->tx_buf[idx + i] = (u8)(umsg->value >> (i << 3));

		/* Add an Assured Write Frame Check Sequence byte */
		ret = peci_aw_fcs(msg, idx + 3 + umsg->tx_len, &aw_fcs);
		if (ret)
			goto out;

		msg->tx_buf[idx + i] = 0x80 ^ aw_fcs;
		break;

	default:
		return -EINVAL;
	}

	ret = peci_xfer_with_retries(adapter, msg, true);

out:
	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_crashdump_disc(struct peci_adapter *adapter, void *vmsg)
{
	struct peci_crashdump_disc_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	/* Per the EDS, the read length must be a byte, word, or qword */
	if (umsg->rx_len != 1 && umsg->rx_len != 2 && umsg->rx_len != 8) {
		dev_dbg(&adapter->dev, "Invalid read length, rx_len: %d\n",
			umsg->rx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_CRASHDUMP_DISC_WRITE_LEN,
				PECI_CRASHDUMP_DISC_READ_LEN_BASE +
				umsg->rx_len);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_CRASHDUMP_CMD;
	msg->tx_buf[1] = 0x00;        /* request byte for Host ID | Retry bit */
				      /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = PECI_CRASHDUMP_DISC_VERSION;
	msg->tx_buf[3] = PECI_CRASHDUMP_DISC_OPCODE;
	msg->tx_buf[4] = umsg->subopcode;
	msg->tx_buf[5] = umsg->param0;
	msg->tx_buf[6] = (u8)umsg->param1;
	msg->tx_buf[7] = (u8)(umsg->param1 >> 8);
	msg->tx_buf[8] = umsg->param2;

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->data, &msg->rx_buf[1], umsg->rx_len);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

static int peci_cmd_crashdump_get_frame(struct peci_adapter *adapter,
					void *vmsg)
{
	struct peci_crashdump_get_frame_msg *umsg = vmsg;
	struct peci_xfer_msg *msg;
	int ret;

	/* Per the EDS, the read length must be a qword or dqword */
	if (umsg->rx_len != 8 && umsg->rx_len != 16) {
		dev_dbg(&adapter->dev, "Invalid read length, rx_len: %d\n",
			umsg->rx_len);
		return -EINVAL;
	}

	msg = peci_get_xfer_msg(PECI_CRASHDUMP_GET_FRAME_WRITE_LEN,
				PECI_CRASHDUMP_GET_FRAME_READ_LEN_BASE +
				umsg->rx_len);
	if (!msg)
		return -ENOMEM;

	msg->addr = umsg->addr;
	msg->tx_buf[0] = PECI_CRASHDUMP_CMD;
	msg->tx_buf[1] = 0x00;        /* request byte for Host ID | Retry bit */
				      /* Host ID is 0 for PECI 3.0 */
	msg->tx_buf[2] = PECI_CRASHDUMP_GET_FRAME_VERSION;
	msg->tx_buf[3] = PECI_CRASHDUMP_GET_FRAME_OPCODE;
	msg->tx_buf[4] = (u8)umsg->param0;
	msg->tx_buf[5] = (u8)(umsg->param0 >> 8);
	msg->tx_buf[6] = (u8)umsg->param1;
	msg->tx_buf[7] = (u8)(umsg->param1 >> 8);
	msg->tx_buf[8] = (u8)umsg->param2;
	msg->tx_buf[9] = (u8)(umsg->param2 >> 8);

	ret = peci_xfer_with_retries(adapter, msg, false);
	if (!ret)
		memcpy(umsg->data, &msg->rx_buf[1], umsg->rx_len);

	umsg->cc = msg->rx_buf[0];
	peci_put_xfer_msg(msg);

	return ret;
}

typedef int (*peci_cmd_fn_type)(struct peci_adapter *, void *);

static const peci_cmd_fn_type peci_cmd_fn[PECI_CMD_MAX] = {
	peci_cmd_xfer,
	peci_cmd_ping,
	peci_cmd_get_dib,
	peci_cmd_get_temp,
	peci_cmd_rd_pkg_cfg,
	peci_cmd_wr_pkg_cfg,
	peci_cmd_rd_ia_msr,
	peci_cmd_wr_ia_msr,
	peci_cmd_rd_ia_msrex,
	peci_cmd_rd_pci_cfg,
	peci_cmd_wr_pci_cfg,
	peci_cmd_rd_pci_cfg_local,
	peci_cmd_wr_pci_cfg_local,
	peci_cmd_rd_end_pt_cfg,
	peci_cmd_wr_end_pt_cfg,
	peci_cmd_crashdump_disc,
	peci_cmd_crashdump_get_frame,
};

/**
 * peci_command - transfer function of a PECI command
 * @adapter: pointer to peci_adapter
 * @vmsg: pointer to PECI messages
 * Context: can sleep
 *
 * This performs a transfer of a PECI command using PECI messages parameter
 * which has various formats on each command.
 *
 * Return: zero on success, else a negative error code.
 */
int peci_command(struct peci_adapter *adapter, enum peci_cmd cmd, void *vmsg)
{
	int ret;

	if (cmd >= PECI_CMD_MAX || cmd < PECI_CMD_XFER)
		return -ENOTTY;

	dev_dbg(&adapter->dev, "%s, cmd=0x%02x\n", __func__, cmd);

	if (!peci_cmd_fn[cmd])
		return -EINVAL;

	mutex_lock(&adapter->bus_lock);

	ret = peci_check_cmd_support(adapter, cmd);
	if (!ret)
		ret = peci_cmd_fn[cmd](adapter, vmsg);

	mutex_unlock(&adapter->bus_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(peci_command);

static int peci_detect(struct peci_adapter *adapter, u8 addr)
{
	struct peci_ping_msg msg;

	msg.addr = addr;

	return peci_command(adapter, PECI_CMD_PING, &msg);
}

static const struct of_device_id *
peci_of_match_device(const struct of_device_id *matches,
		     struct peci_client *client)
{
#if IS_ENABLED(CONFIG_OF)
	if (!(client && matches))
		return NULL;

	return of_match_device(matches, &client->dev);
#else /* CONFIG_OF */
	return NULL;
#endif /* CONFIG_OF */
}

static const struct peci_device_id *
peci_match_id(const struct peci_device_id *id, struct peci_client *client)
{
	if (!(id && client))
		return NULL;

	while (id->name[0]) {
		if (!strncmp(client->name, id->name, PECI_NAME_SIZE))
			return id;
		id++;
	}

	return NULL;
}

static int peci_device_match(struct device *dev, struct device_driver *drv)
{
	struct peci_client *client = peci_verify_client(dev);
	struct peci_driver *driver;

	/* Attempt an OF style match */
	if (peci_of_match_device(drv->of_match_table, client))
		return 1;

	driver = to_peci_driver(drv);

	/* Finally an ID match */
	if (peci_match_id(driver->id_table, client))
		return 1;

	return 0;
}

static int peci_device_probe(struct device *dev)
{
	struct peci_client *client = peci_verify_client(dev);
	struct peci_driver *driver;
	int status = -EINVAL;

	if (!client)
		return 0;

	driver = to_peci_driver(dev->driver);

	if (!driver->id_table &&
	    !peci_of_match_device(dev->driver->of_match_table, client))
		return -ENODEV;

	dev_dbg(dev, "%s: name:%s\n", __func__, client->name);

	status = dev_pm_domain_attach(&client->dev, true);
	if (status == -EPROBE_DEFER)
		return status;

	if (driver->probe)
		status = driver->probe(client);
	else
		status = -EINVAL;

	if (status)
		goto err_detach_pm_domain;

	return 0;

err_detach_pm_domain:
	dev_pm_domain_detach(&client->dev, true);

	return status;
}

static void peci_device_remove(struct device *dev)
{
	struct peci_client *client = peci_verify_client(dev);
	struct peci_driver *driver;

	if (client && dev->driver) {

		driver = to_peci_driver(dev->driver);
		if (driver->remove) {
			dev_dbg(dev, "%s: name:%s\n", __func__, client->name);
			driver->remove(client);
		}

		dev_pm_domain_detach(&client->dev, true);
	}
}

static void peci_device_shutdown(struct device *dev)
{
	struct peci_client *client = peci_verify_client(dev);
	struct peci_driver *driver;

	if (!client || !dev->driver)
		return;

	dev_dbg(dev, "%s: name:%s\n", __func__, client->name);

	driver = to_peci_driver(dev->driver);
	if (driver->shutdown)
		driver->shutdown(client);
}

struct bus_type peci_bus_type = {
	.name		= "peci",
	.match		= peci_device_match,
	.probe		= peci_device_probe,
	.remove		= peci_device_remove,
	.shutdown	= peci_device_shutdown,
};
EXPORT_SYMBOL_GPL(peci_bus_type);

static int peci_check_addr_validity(u8 addr)
{
	if (addr < PECI_BASE_ADDR && addr > PECI_BASE_ADDR + PECI_OFFSET_MAX)
		return -EINVAL;

	return 0;
}

static int peci_check_client_busy(struct device *dev, void *client_new_p)
{
	struct peci_client *client = peci_verify_client(dev);
	struct peci_client *client_new = client_new_p;

	if (client && client->addr == client_new->addr)
		return -EBUSY;

	return 0;
}

/**
 * peci_get_cpu_id - read CPU ID from the Package Configuration Space of CPU
 * @adapter: pointer to peci_adapter
 * @addr: address of the PECI client CPU
 * @cpu_id: where the CPU ID will be stored
 * Context: can sleep
 *
 * Return: zero on success, else a negative error code.
 */
int peci_get_cpu_id(struct peci_adapter *adapter, u8 addr, u32 *cpu_id)
{
	struct peci_rd_pkg_cfg_msg msg;
	int ret;

	msg.addr = addr;
	msg.index = PECI_MBX_INDEX_CPU_ID;
	msg.param = PECI_PKG_ID_CPU_ID;
	msg.rx_len = 4;

	ret = peci_command(adapter, PECI_CMD_RD_PKG_CFG, &msg);
	if (msg.cc != PECI_DEV_CC_SUCCESS)
		ret = -EAGAIN;
	if (ret)
		return ret;

	*cpu_id = le32_to_cpup((__le32 *)msg.pkg_config);

	return 0;
}
EXPORT_SYMBOL_GPL(peci_get_cpu_id);

static struct peci_client *peci_new_device(struct peci_adapter *adapter,
					   struct peci_board_info const *info)
{
	struct peci_client *client;
	int ret;

	/* Increase reference count for the adapter assigned */
	if (!peci_get_adapter(adapter->nr))
		return NULL;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		goto err_put_adapter;

	client->adapter = adapter;
	client->addr = info->addr;
	strlcpy(client->name, info->type, sizeof(client->name));

	ret = peci_check_addr_validity(client->addr);
	if (ret) {
		dev_err(&adapter->dev, "Invalid PECI CPU address 0x%02hx\n",
			client->addr);
		goto err_free_client_silent;
	}

	/* Check online status of client */
	ret = peci_detect(adapter, client->addr);
	if (ret)
		goto err_free_client;

	ret = device_for_each_child(&adapter->dev, client,
				    peci_check_client_busy);
	if (ret)
		goto err_free_client;

	client->dev.parent = &client->adapter->dev;
	client->dev.bus = &peci_bus_type;
	client->dev.type = &peci_client_type;
	client->dev.of_node = of_node_get(info->of_node);
	dev_set_name(&client->dev, "%d-%02x", adapter->nr, client->addr);

	ret = device_register(&client->dev);
	if (ret)
		goto err_put_of_node;

	dev_dbg(&adapter->dev, "client [%s] registered with bus id %s\n",
		client->name, dev_name(&client->dev));

	return client;

err_put_of_node:
	of_node_put(info->of_node);
err_free_client:
	dev_err(&adapter->dev,
		"Failed to register peci client %s at 0x%02x (%d)\n",
		client->name, client->addr, ret);
err_free_client_silent:
	kfree(client);
err_put_adapter:
	peci_put_adapter(adapter);

	return NULL;
}

static void peci_unregister_device(struct peci_client *client)
{
	if (!client)
		return;

	if (client->dev.of_node) {
		of_node_clear_flag(client->dev.of_node, OF_POPULATED);
		of_node_put(client->dev.of_node);
	}

	device_unregister(&client->dev);
}

static int peci_unregister_client(struct device *dev, void *dummy)
{
	struct peci_client *client = peci_verify_client(dev);

	peci_unregister_device(client);

	return 0;
}

static void peci_adapter_dev_release(struct device *dev)
{
	struct peci_adapter *adapter = to_peci_adapter(dev);

	dev_dbg(dev, "%s: %s\n", __func__, adapter->name);
	mutex_destroy(&adapter->userspace_clients_lock);
	mutex_destroy(&adapter->bus_lock);
	kfree(adapter);
}

static ssize_t peci_sysfs_new_device(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct peci_adapter *adapter = to_peci_adapter(dev);
	struct peci_board_info info = {};
	struct peci_client *client;
	char *blank, end;
	short addr;
	int ret;

	/* Parse device type */
	blank = strchr(buf, ' ');
	if (!blank) {
		dev_err(dev, "%s: Missing parameters\n", "new_device");
		return -EINVAL;
	}
	if (blank - buf > PECI_NAME_SIZE - 1) {
		dev_err(dev, "%s: Invalid device type\n", "new_device");
		return -EINVAL;
	}
	memcpy(info.type, buf, blank - buf);

	/* Parse remaining parameters, reject extra parameters */
	ret = sscanf(++blank, "%hi%c", &addr, &end);
	if (ret < 1) {
		dev_err(dev, "%s: Can't parse client address\n", "new_device");
		return -EINVAL;
	}
	if (ret > 1 && end != '\n') {
		dev_err(dev, "%s: Extra parameters\n", "new_device");
		return -EINVAL;
	}

	info.addr = (u8)addr;
	client = peci_new_device(adapter, &info);
	if (!client)
		return -EINVAL;

	/* Keep track of the added device */
	mutex_lock(&adapter->userspace_clients_lock);
	list_add_tail(&client->detected, &adapter->userspace_clients);
	mutex_unlock(&adapter->userspace_clients_lock);
	dev_dbg(dev, "%s: Instantiated device %s at 0x%02hx\n", "new_device",
		info.type, info.addr);

	return count;
}
static DEVICE_ATTR(new_device, 0200, NULL, peci_sysfs_new_device);

static ssize_t peci_sysfs_delete_device(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct peci_adapter *adapter = to_peci_adapter(dev);
	struct peci_client *client, *next;
	struct peci_board_info info = {};
	char *blank, end;
	short addr;
	int ret;

	/* Parse device type */
	blank = strchr(buf, ' ');
	if (!blank) {
		dev_err(dev, "%s: Missing parameters\n", "delete_device");
		return -EINVAL;
	}
	if (blank - buf > PECI_NAME_SIZE - 1) {
		dev_err(dev, "%s: Invalid device type\n", "delete_device");
		return -EINVAL;
	}
	memcpy(info.type, buf, blank - buf);

	/* Parse remaining parameters, reject extra parameters */
	ret = sscanf(++blank, "%hi%c", &addr, &end);
	if (ret < 1) {
		dev_err(dev, "%s: Can't parse client address\n",
			"delete_device");
		return -EINVAL;
	}
	if (ret > 1 && end != '\n') {
		dev_err(dev, "%s: Extra parameters\n", "delete_device");
		return -EINVAL;
	}

	info.addr = (u8)addr;

	/* Make sure the device was added through sysfs */
	ret = -ENOENT;
	mutex_lock(&adapter->userspace_clients_lock);
	list_for_each_entry_safe(client, next, &adapter->userspace_clients,
				 detected) {
		if (client->addr == info.addr &&
		    !strncmp(client->name, info.type, PECI_NAME_SIZE)) {
			dev_dbg(dev, "%s: Deleting device %s at 0x%02hx\n",
				"delete_device", client->name, client->addr);
			list_del(&client->detected);
			peci_unregister_device(client);
			ret = count;
			break;
		}
	}
	mutex_unlock(&adapter->userspace_clients_lock);

	if (ret < 0)
		dev_dbg(dev, "%s: Can't find device in list\n",
			"delete_device");

	return ret;
}
static DEVICE_ATTR_IGNORE_LOCKDEP(delete_device, 0200, NULL,
				  peci_sysfs_delete_device);

static struct attribute *peci_adapter_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_new_device.attr,
	&dev_attr_delete_device.attr,
	NULL
};
ATTRIBUTE_GROUPS(peci_adapter);

struct device_type peci_adapter_type = {
	.groups		= peci_adapter_groups,
	.release	= peci_adapter_dev_release,
};
EXPORT_SYMBOL_GPL(peci_adapter_type);

/**
 * peci_verify_adapter - return parameter as peci_adapter, or NULL
 * @dev: device, probably from some driver model iterator
 *
 * Return: pointer to peci_adapter on success, else NULL.
 */
struct peci_adapter *peci_verify_adapter(struct device *dev)
{
	return (dev->type == &peci_adapter_type)
			? to_peci_adapter(dev)
			: NULL;
}
EXPORT_SYMBOL_GPL(peci_verify_adapter);

#if IS_ENABLED(CONFIG_OF)
static struct peci_client *peci_of_register_device(struct peci_adapter *adapter,
						   struct device_node *node)
{
	struct peci_board_info info = {};
	struct peci_client *client;
	u32 addr;
	int ret;

	dev_dbg(&adapter->dev, "register %pOF\n", node);

	ret = of_property_read_u32(node, "reg", &addr);
	if (ret) {
		dev_err(&adapter->dev, "invalid reg on %pOF\n", node);
		return ERR_PTR(ret);
	}

	info.addr = addr;
	info.of_node = node;

	client = peci_new_device(adapter, &info);
	if (!client)
		client = ERR_PTR(-EINVAL);

	return client;
}

static void peci_of_register_devices(struct peci_adapter *adapter)
{
	struct device_node *bus, *node;
	struct peci_client *client;

	/* Only register child devices if the adapter has a node pointer set */
	if (!adapter->dev.of_node)
		return;

	bus = of_get_child_by_name(adapter->dev.of_node, "peci-bus");
	if (!bus)
		bus = of_node_get(adapter->dev.of_node);

	for_each_available_child_of_node(bus, node) {
		if (of_node_test_and_set_flag(node, OF_POPULATED))
			continue;

		client = peci_of_register_device(adapter, node);
		if (IS_ERR(client)) {
			dev_warn(&adapter->dev,
				 "Failed to create PECI device for %pOF\n",
				 node);
			of_node_clear_flag(node, OF_POPULATED);
		}
	}

	of_node_put(bus);
}
#else /* CONFIG_OF */
static void peci_of_register_devices(struct peci_adapter *adapter) { }
#endif /* CONFIG_OF */

#if IS_ENABLED(CONFIG_OF_DYNAMIC)
static int peci_of_match_node(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

/* must call put_device() when done with returned peci_client device */
static struct peci_client *peci_of_find_device(struct device_node *node)
{
	struct peci_client *client;
	struct device *dev;

	dev = bus_find_device(&peci_bus_type, NULL, node, peci_of_match_node);
	if (!dev)
		return NULL;

	client = peci_verify_client(dev);
	if (!client)
		put_device(dev);

	return client;
}

/* must call put_device() when done with returned peci_adapter device */
static struct peci_adapter *peci_of_find_adapter(struct device_node *node)
{
	struct peci_adapter *adapter;
	struct device *dev;

	dev = bus_find_device(&peci_bus_type, NULL, node, peci_of_match_node);
	if (!dev)
		return NULL;

	adapter = peci_verify_adapter(dev);
	if (!adapter)
		put_device(dev);

	return adapter;
}

static int peci_of_notify(struct notifier_block *nb, ulong action, void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct peci_adapter *adapter;
	struct peci_client *client;

	switch (of_reconfig_get_state_change(action, rd)) {
	case OF_RECONFIG_CHANGE_ADD:
		adapter = peci_of_find_adapter(rd->dn->parent);
		if (!adapter)
			return NOTIFY_OK;	/* not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED)) {
			put_device(&adapter->dev);
			return NOTIFY_OK;
		}

		client = peci_of_register_device(adapter, rd->dn);
		put_device(&adapter->dev);

		if (IS_ERR(client)) {
			dev_err(&adapter->dev,
				"failed to create client for '%pOF'\n", rd->dn);
			of_node_clear_flag(rd->dn, OF_POPULATED);
			return notifier_from_errno(PTR_ERR(client));
		}
		break;
	case OF_RECONFIG_CHANGE_REMOVE:
		/* already depopulated? */
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* find our device by node */
		client = peci_of_find_device(rd->dn);
		if (!client)
			return NOTIFY_OK;	/* no? not meant for us */

		/* unregister takes one ref away */
		peci_unregister_device(client);

		/* and put the reference of the find */
		put_device(&client->dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block peci_of_notifier = {
	.notifier_call = peci_of_notify,
};
#else /* CONFIG_OF_DYNAMIC */
extern struct notifier_block peci_of_notifier;
#endif /* CONFIG_OF_DYNAMIC */

/**
 * peci_alloc_adapter - allocate a PECI adapter
 * @dev: the adapter, possibly using the platform_bus
 * @size: how much zeroed driver-private data to allocate; the pointer to this
 *	memory is in the driver_data field of the returned device,
 *	accessible with peci_get_adapdata().
 * Context: can sleep
 *
 * This call is used only by PECI adapter drivers, which are the only ones
 * directly touching chip registers.  It's how they allocate a peci_adapter
 * structure, prior to calling peci_add_adapter().
 *
 * This must be called from context that can sleep.
 *
 * The caller is responsible for initializing the adapter's methods before
 * calling peci_add_adapter(); and (after errors while adding the device)
 * calling put_device() to prevent a memory leak.
 *
 * Return: the peci_adapter structure on success, else NULL.
 */
struct peci_adapter *peci_alloc_adapter(struct device *dev, uint size)
{
	struct peci_adapter *adapter;

	if (!dev)
		return NULL;

	adapter = kzalloc(size + sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	device_initialize(&adapter->dev);
	adapter->dev.parent = dev;
	adapter->dev.bus = &peci_bus_type;
	adapter->dev.type = &peci_adapter_type;
	peci_set_adapdata(adapter, &adapter[1]);

	return adapter;
}
EXPORT_SYMBOL_GPL(peci_alloc_adapter);

static int peci_register_adapter(struct peci_adapter *adapter)
{
	int ret = -EINVAL;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered))
		goto err_free_idr;

	if (WARN(!adapter->name[0], "peci adapter has no name"))
		goto err_free_idr;

	if (WARN(!adapter->xfer, "peci adapter has no xfer function\n"))
		goto err_free_idr;

	mutex_init(&adapter->bus_lock);
	mutex_init(&adapter->userspace_clients_lock);
	INIT_LIST_HEAD(&adapter->userspace_clients);

	dev_set_name(&adapter->dev, "peci-%d", adapter->nr);

	ret = device_add(&adapter->dev);
	if (ret) {
		pr_err("adapter '%s': can't add device (%d)\n",
		       adapter->name, ret);
		goto err_free_idr;
	}

	dev_dbg(&adapter->dev, "adapter [%s] registered\n", adapter->name);

	pm_runtime_no_callbacks(&adapter->dev);
	pm_suspend_ignore_children(&adapter->dev, true);
	pm_runtime_enable(&adapter->dev);

	/* create pre-declared device nodes */
	peci_of_register_devices(adapter);

	return 0;

err_free_idr:
	mutex_lock(&core_lock);
	idr_remove(&peci_adapter_idr, adapter->nr);
	mutex_unlock(&core_lock);
	return ret;
}

static int peci_add_numbered_adapter(struct peci_adapter *adapter)
{
	int id;

	mutex_lock(&core_lock);
	id = idr_alloc(&peci_adapter_idr, adapter,
		       adapter->nr, adapter->nr + 1, GFP_KERNEL);
	mutex_unlock(&core_lock);
	if (WARN(id < 0, "couldn't get idr"))
		return id == -ENOSPC ? -EBUSY : id;

	return peci_register_adapter(adapter);
}

/**
 * peci_add_adapter - add a PECI adapter
 * @adapter: initialized adapter, originally from peci_alloc_adapter()
 * Context: can sleep
 *
 * PECI adapters connect to their drivers using some non-PECI bus,
 * such as the platform bus.  The final stage of probe() in that code
 * includes calling peci_add_adapter() to hook up to this PECI bus glue.
 *
 * This must be called from context that can sleep.
 *
 * It returns zero on success, else a negative error code (dropping the
 * adapter's refcount).  After a successful return, the caller is responsible
 * for calling peci_del_adapter().
 *
 * Return: zero on success, else a negative error code.
 */
int peci_add_adapter(struct peci_adapter *adapter)
{
	struct device *dev = &adapter->dev;
	int id;

	id = of_alias_get_id(dev->of_node, "peci");
	if (id >= 0) {
		adapter->nr = id;
		return peci_add_numbered_adapter(adapter);
	}

	mutex_lock(&core_lock);
	id = idr_alloc(&peci_adapter_idr, adapter, 0, 0, GFP_KERNEL);
	mutex_unlock(&core_lock);
	if (WARN(id < 0, "couldn't get idr"))
		return id;

	adapter->nr = id;

	return peci_register_adapter(adapter);
}
EXPORT_SYMBOL_GPL(peci_add_adapter);

/**
 * peci_del_adapter - delete a PECI adapter
 * @adapter: the adpater being deleted
 * Context: can sleep
 *
 * This call is used only by PECI adpater drivers, which are the only ones
 * directly touching chip registers.
 *
 * This must be called from context that can sleep.
 *
 * Note that this function also drops a reference to the adapter.
 */
void peci_del_adapter(struct peci_adapter *adapter)
{
	struct peci_client *client, *next;
	struct peci_adapter *found;
	int nr;

	/* First make sure that this adapter was ever added */
	mutex_lock(&core_lock);
	found = idr_find(&peci_adapter_idr, adapter->nr);
	mutex_unlock(&core_lock);

	if (found != adapter)
		return;

	/* Remove devices instantiated from sysfs */
	mutex_lock(&adapter->userspace_clients_lock);
	list_for_each_entry_safe(client, next, &adapter->userspace_clients,
				 detected) {
		dev_dbg(&adapter->dev, "Removing %s at 0x%x\n", client->name,
			client->addr);
		list_del(&client->detected);
		peci_unregister_device(client);
	}
	mutex_unlock(&adapter->userspace_clients_lock);

	/*
	 * Detach any active clients. This can't fail, thus we do not
	 * check the returned value.
	 */
	device_for_each_child(&adapter->dev, NULL, peci_unregister_client);

	/* device name is gone after device_unregister */
	dev_dbg(&adapter->dev, "adapter [%s] unregistered\n", adapter->name);

	pm_runtime_disable(&adapter->dev);
	nr = adapter->nr;
	device_unregister(&adapter->dev);

	/* free bus id */
	mutex_lock(&core_lock);
	idr_remove(&peci_adapter_idr, nr);
	mutex_unlock(&core_lock);
}
EXPORT_SYMBOL_GPL(peci_del_adapter);

int peci_for_each_dev(void *data, int (*fn)(struct device *, void *))
{
	int ret;

	mutex_lock(&core_lock);
	ret = bus_for_each_dev(&peci_bus_type, NULL, data, fn);
	mutex_unlock(&core_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(peci_for_each_dev);

/**
 * peci_register_driver - register a PECI driver
 * @owner: owner module of the driver being registered
 * @driver: the driver being registered
 * Context: can sleep
 *
 * Return: zero on success, else a negative error code.
 */
int peci_register_driver(struct module *owner, struct peci_driver *driver)
{
	int ret;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered))
		return -EAGAIN;

	/* add the driver to the list of peci drivers in the driver core */
	driver->driver.owner = owner;
	driver->driver.bus = &peci_bus_type;

	/*
	 * When registration returns, the driver core
	 * will have called probe() for all matching-but-unbound devices.
	 */
	ret = driver_register(&driver->driver);
	if (ret)
		return ret;

	pr_debug("driver [%s] registered\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL_GPL(peci_register_driver);

/**
 * peci_del_driver - unregister a PECI driver
 * @driver: the driver being unregistered
 * Context: can sleep
 */
void peci_del_driver(struct peci_driver *driver)
{
	driver_unregister(&driver->driver);
	pr_debug("driver [%s] unregistered\n", driver->driver.name);
}
EXPORT_SYMBOL_GPL(peci_del_driver);

static int __init peci_init(void)
{
	int ret;

	ret = bus_register(&peci_bus_type);
	if (ret < 0) {
		pr_err("peci: Failed to register PECI bus type!\n");
		return ret;
	}

	crc8_populate_msb(peci_crc8_table, PECI_CRC8_POLYNOMIAL);

	if (IS_ENABLED(CONFIG_OF_DYNAMIC))
		WARN_ON(of_reconfig_notifier_register(&peci_of_notifier));

	is_registered = true;

	return 0;
}

static void __exit peci_exit(void)
{
	if (IS_ENABLED(CONFIG_OF_DYNAMIC))
		WARN_ON(of_reconfig_notifier_unregister(&peci_of_notifier));

	bus_unregister(&peci_bus_type);
}

subsys_initcall(peci_init);
module_exit(peci_exit);

MODULE_AUTHOR("Jason M Biils <jason.m.bills@linux.intel.com>");
MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("PECI bus core module");
MODULE_LICENSE("GPL v2");
