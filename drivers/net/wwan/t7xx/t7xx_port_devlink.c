// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_pci_rescan.h"
#include "t7xx_port_devlink.h"
#include "t7xx_port_proxy.h"
#include "t7xx_state_monitor.h"
#include "t7xx_uevent.h"

static struct t7xx_devlink_region_info t7xx_devlink_region_list[T7XX_TOTAL_REGIONS] = {
	{"mr_dump", T7XX_MRDUMP_SIZE},
	{"lk_dump", T7XX_LKDUMP_SIZE},
};

static int t7xx_devlink_port_read(struct t7xx_port *port, char *buf, size_t count)
{
	int ret = 0, read_len;
	struct sk_buff *skb;

	spin_lock_irq(&port->rx_wq.lock);
	if (skb_queue_empty(&port->rx_skb_list)) {
		ret = wait_event_interruptible_locked_irq(port->rx_wq,
							  !skb_queue_empty(&port->rx_skb_list));
		if (ret == -ERESTARTSYS) {
			spin_unlock_irq(&port->rx_wq.lock);
			return -EINTR;
		}
	}
	skb = skb_dequeue(&port->rx_skb_list);
	spin_unlock_irq(&port->rx_wq.lock);

	read_len = count > skb->len ? skb->len : count;
	memcpy(buf, skb->data, read_len);
	dev_kfree_skb(skb);

	return ret ? ret : read_len;
}

static int t7xx_devlink_port_write(struct t7xx_port *port, const char *buf, size_t count)
{
	const struct t7xx_port_conf *port_conf = port->port_conf;
	size_t actual_count;
	struct sk_buff *skb;
	int ret, txq_mtu;

	txq_mtu = t7xx_get_port_mtu(port);
	if (txq_mtu < 0)
		return -EINVAL;

	actual_count = count > txq_mtu ? txq_mtu : count;
	skb = __dev_alloc_skb(actual_count, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, buf, actual_count);
	ret = t7xx_port_send_raw_skb(port, skb);
	if (ret) {
		dev_err(port->dev, "write error on %s, size: %zu, ret: %d\n",
			port_conf->name, actual_count, ret);
		dev_kfree_skb(skb);
		return ret;
	}

	return actual_count;
}

static int t7xx_devlink_fb_handle_response(struct t7xx_port *port, int *data)
{
	int ret = 0, index = 0, return_data = 0, read_bytes;
	char status[T7XX_FB_RESPONSE_SIZE + 1];

	while (index < T7XX_FB_RESP_COUNT) {
		index++;
		read_bytes = t7xx_devlink_port_read(port, status, T7XX_FB_RESPONSE_SIZE);
		if (read_bytes < 0) {
			dev_err(port->dev, "status read failed");
			ret = -EIO;
			break;
		}

		status[read_bytes] = '\0';
		if (!strncmp(status, T7XX_FB_RESP_INFO, strlen(T7XX_FB_RESP_INFO))) {
			break;
		} else if (!strncmp(status, T7XX_FB_RESP_OKAY, strlen(T7XX_FB_RESP_OKAY))) {
			break;
		} else if (!strncmp(status, T7XX_FB_RESP_FAIL, strlen(T7XX_FB_RESP_FAIL))) {
			ret = -EPROTO;
			break;
		} else if (!strncmp(status, T7XX_FB_RESP_DATA, strlen(T7XX_FB_RESP_DATA))) {
			if (data) {
				if (!kstrtoint(status + strlen(T7XX_FB_RESP_DATA), 16,
					       &return_data)) {
					*data = return_data;
				} else {
					dev_err(port->dev, "kstrtoint error!\n");
					ret = -EPROTO;
				}
			}
			break;
		}
	}

	return ret;
}

static int t7xx_devlink_fb_raw_command(char *cmd, struct t7xx_port *port, int *data)
{
	int ret, cmd_size = strlen(cmd);

	if (cmd_size > T7XX_FB_COMMAND_SIZE) {
		dev_err(port->dev, "command length %d is long\n", cmd_size);
		return -EINVAL;
	}

	if (cmd_size != t7xx_devlink_port_write(port, cmd, cmd_size)) {
		dev_err(port->dev, "raw command = %s write failed\n", cmd);
		return -EIO;
	}

	dev_dbg(port->dev, "raw command = %s written to the device\n", cmd);
	ret = t7xx_devlink_fb_handle_response(port, data);
	if (ret)
		dev_err(port->dev, "raw command = %s response FAILURE:%d\n", cmd, ret);

	return ret;
}

static int t7xx_devlink_fb_send_buffer(struct t7xx_port *port, const u8 *buf, size_t size)
{
	size_t remaining = size, offset = 0, len;
	int write_done;

	if (!size)
		return -EINVAL;

	while (remaining) {
		len = min_t(size_t, remaining, CLDMA_DEDICATED_Q_BUFF_SZ);
		write_done = t7xx_devlink_port_write(port, buf + offset, len);

		if (write_done < 0) {
			dev_err(port->dev, "write to device failed in %s", __func__);
			return -EIO;
		} else if (write_done != len) {
			dev_err(port->dev, "write Error. Only %d/%zu bytes written",
				write_done, len);
			return -EIO;
		}

		remaining -= len;
		offset += len;
	}

	return 0;
}

static int t7xx_devlink_fb_download_command(struct t7xx_port *port, size_t size)
{
	char download_command[T7XX_FB_COMMAND_SIZE];

	snprintf(download_command, sizeof(download_command), "%s:%08zx",
		 T7XX_FB_CMD_DOWNLOAD, size);
	return t7xx_devlink_fb_raw_command(download_command, port, NULL);
}

static int t7xx_devlink_fb_download(struct t7xx_port *port, const u8 *buf, size_t size)
{
	int ret;

	if (size <= 0 || size > SIZE_MAX) {
		dev_err(port->dev, "file is too large to download");
		return -EINVAL;
	}

	ret = t7xx_devlink_fb_download_command(port, size);
	if (ret)
		return ret;

	ret = t7xx_devlink_fb_send_buffer(port, buf, size);
	if (ret)
		return ret;

	return t7xx_devlink_fb_handle_response(port, NULL);
}

static int t7xx_devlink_fb_flash(const char *cmd, struct t7xx_port *port)
{
	char flash_command[T7XX_FB_COMMAND_SIZE];

	snprintf(flash_command, sizeof(flash_command), "%s:%s", T7XX_FB_CMD_FLASH, cmd);
	return t7xx_devlink_fb_raw_command(flash_command, port, NULL);
}

static int t7xx_devlink_fb_flash_partition(const char *partition, const u8 *buf,
					   struct t7xx_port *port, size_t size)
{
	int ret;

	ret = t7xx_devlink_fb_download(port, buf, size);
	if (ret)
		return ret;

	return t7xx_devlink_fb_flash(partition, port);
}

static int t7xx_devlink_fb_get_core(struct t7xx_port *port)
{
	struct t7xx_devlink_region_info *mrdump_region;
	char mrdump_complete_event[T7XX_FB_EVENT_SIZE];
	u32 mrd_mb = T7XX_MRDUMP_SIZE / (1024 * 1024);
	struct t7xx_devlink *dl = port->dl;
	int clen, dlen = 0, result = 0;
	unsigned long long zipsize = 0;
	char mcmd[T7XX_FB_MCMD_SIZE];
	size_t offset_dlen = 0;
	char *mdata;

	set_bit(T7XX_MRDUMP_STATUS, &dl->status);
	mdata = kmalloc(T7XX_FB_MDATA_SIZE, GFP_KERNEL);
	if (!mdata) {
		result = -ENOMEM;
		goto get_core_exit;
	}

	mrdump_region = dl->dl_region_info[T7XX_MRDUMP_INDEX];
	mrdump_region->dump = vmalloc(mrdump_region->default_size);
	if (!mrdump_region->dump) {
		kfree(mdata);
		result = -ENOMEM;
		goto get_core_exit;
	}

	result = t7xx_devlink_fb_raw_command(T7XX_FB_CMD_OEM_MRDUMP, port, NULL);
	if (result) {
		dev_err(port->dev, "%s command failed\n", T7XX_FB_CMD_OEM_MRDUMP);
		vfree(mrdump_region->dump);
		kfree(mdata);
		goto get_core_exit;
	}

	while (mrdump_region->default_size > offset_dlen) {
		clen = t7xx_devlink_port_read(port, mcmd, sizeof(mcmd));
		if (clen == strlen(T7XX_FB_CMD_RTS) &&
		    (!strncmp(mcmd, T7XX_FB_CMD_RTS, strlen(T7XX_FB_CMD_RTS)))) {
			memset(mdata, 0, T7XX_FB_MDATA_SIZE);
			dlen = 0;
			memset(mcmd, 0, sizeof(mcmd));
			clen = snprintf(mcmd, sizeof(mcmd), "%s", T7XX_FB_CMD_CTS);

			if (t7xx_devlink_port_write(port, mcmd, clen) != clen) {
				dev_err(port->dev, "write for _CTS failed:%d\n", clen);
				goto get_core_free_mem;
			}

			dlen = t7xx_devlink_port_read(port, mdata, T7XX_FB_MDATA_SIZE);
			if (dlen <= 0) {
				dev_err(port->dev, "read data error(%d)\n", dlen);
				goto get_core_free_mem;
			}

			zipsize += (unsigned long long)(dlen);
			memcpy(mrdump_region->dump + offset_dlen, mdata, dlen);
			offset_dlen += dlen;
			memset(mcmd, 0, sizeof(mcmd));
			clen = snprintf(mcmd, sizeof(mcmd), "%s", T7XX_FB_CMD_FIN);
			if (t7xx_devlink_port_write(port, mcmd, clen) != clen) {
				dev_err(port->dev, "%s: _FIN failed, (Read %05d:%05llu)\n",
					__func__, clen, zipsize);
				goto get_core_free_mem;
			}
		} else if ((clen == strlen(T7XX_FB_RESP_MRDUMP_DONE)) &&
			  (!strncmp(mcmd, T7XX_FB_RESP_MRDUMP_DONE,
				    strlen(T7XX_FB_RESP_MRDUMP_DONE)))) {
			dev_dbg(port->dev, "%s! size:%zd\n", T7XX_FB_RESP_MRDUMP_DONE, offset_dlen);
			mrdump_region->actual_size = offset_dlen;
			snprintf(mrdump_complete_event, sizeof(mrdump_complete_event),
				 "%s size=%zu", T7XX_UEVENT_MRDUMP_READY, offset_dlen);
			t7xx_uevent_send(dl->dev, mrdump_complete_event);
			kfree(mdata);
			result = 0;
			goto get_core_exit;
		} else {
			dev_err(port->dev, "getcore protocol error (read len %05d)\n", clen);
			goto get_core_free_mem;
		}
	}

	dev_err(port->dev, "mrdump exceeds %uMB size. Discarded!", mrd_mb);
	t7xx_uevent_send(port->dev, T7XX_UEVENT_MRD_DISCD);

get_core_free_mem:
	kfree(mdata);
	vfree(mrdump_region->dump);
	clear_bit(T7XX_MRDUMP_STATUS, &dl->status);
	return -EPROTO;

get_core_exit:
	clear_bit(T7XX_MRDUMP_STATUS, &dl->status);
	return result;
}

static int t7xx_devlink_fb_dump_log(struct t7xx_port *port)
{
	struct t7xx_devlink_region_info *lkdump_region;
	char lkdump_complete_event[T7XX_FB_EVENT_SIZE];
	struct t7xx_devlink *dl = port->dl;
	int dlen, datasize = 0, result;
	size_t offset_dlen = 0;
	u8 *data;

	set_bit(T7XX_LKDUMP_STATUS, &dl->status);
	result = t7xx_devlink_fb_raw_command(T7XX_FB_CMD_OEM_LKDUMP, port, &datasize);
	if (result) {
		dev_err(port->dev, "%s command returns failure\n", T7XX_FB_CMD_OEM_LKDUMP);
		goto lkdump_exit;
	}

	lkdump_region = dl->dl_region_info[T7XX_LKDUMP_INDEX];
	if (datasize > lkdump_region->default_size) {
		dev_err(port->dev, "lkdump size is more than %dKB. Discarded!",
			T7XX_LKDUMP_SIZE / 1024);
		t7xx_uevent_send(dl->dev, T7XX_UEVENT_LKD_DISCD);
		result = -EPROTO;
		goto lkdump_exit;
	}

	data = kzalloc(datasize, GFP_KERNEL);
	if (!data) {
		result = -ENOMEM;
		goto lkdump_exit;
	}

	lkdump_region->dump = vmalloc(lkdump_region->default_size);
	if (!lkdump_region->dump) {
		kfree(data);
		result = -ENOMEM;
		goto lkdump_exit;
	}

	while (datasize > 0) {
		dlen = t7xx_devlink_port_read(port, data, datasize);
		if (dlen <= 0) {
			dev_err(port->dev, "lkdump read error ret = %d", dlen);
			kfree(data);
			result = -EPROTO;
			goto lkdump_exit;
		}

		memcpy(lkdump_region->dump + offset_dlen, data, dlen);
		datasize -= dlen;
		offset_dlen += dlen;
	}

	dev_dbg(port->dev, "LKDUMP DONE! size:%zd\n", offset_dlen);
	lkdump_region->actual_size = offset_dlen;
	snprintf(lkdump_complete_event, sizeof(lkdump_complete_event), "%s size=%zu",
		 T7XX_UEVENT_LKDUMP_READY, offset_dlen);
	t7xx_uevent_send(dl->dev, lkdump_complete_event);
	kfree(data);
	clear_bit(T7XX_LKDUMP_STATUS, &dl->status);
	return t7xx_devlink_fb_handle_response(port, NULL);

lkdump_exit:
	clear_bit(T7XX_LKDUMP_STATUS, &dl->status);
	return result;
}

static int t7xx_devlink_flash_update(struct devlink *devlink,
				     struct devlink_flash_update_params *params,
				     struct netlink_ext_ack *extack)
{
	struct t7xx_devlink *dl = devlink_priv(devlink);
	const char *component = params->component;
	const struct firmware *fw = params->fw;
	char flash_event[T7XX_FB_EVENT_SIZE];
	struct t7xx_port *port;
	int ret;

	port = dl->port;
	if (port->dl->mode != T7XX_FB_DL_MODE) {
		dev_err(port->dev, "Modem is not in fastboot download mode!");
		ret = -EPERM;
		goto err_out;
	}

	if (dl->status != T7XX_DEVLINK_IDLE) {
		dev_err(port->dev, "Modem is busy!");
		ret = -EBUSY;
		goto err_out;
	}

	if (!component || !fw->data) {
		ret = -EINVAL;
		goto err_out;
	}

	set_bit(T7XX_FLASH_STATUS, &dl->status);
	dev_dbg(port->dev, "flash partition name:%s binary size:%zu\n", component, fw->size);
	ret = t7xx_devlink_fb_flash_partition(component, fw->data, port, fw->size);
	if (ret) {
		devlink_flash_update_status_notify(devlink, "flashing failure!",
						   params->component, 0, 0);
		snprintf(flash_event, sizeof(flash_event), "%s for [%s]",
			 T7XX_UEVENT_FLASHING_FAILURE, params->component);
	} else {
		devlink_flash_update_status_notify(devlink, "flashing success!",
						   params->component, 0, 0);
		snprintf(flash_event, sizeof(flash_event), "%s for [%s]",
			 T7XX_UEVENT_FLASHING_SUCCESS, params->component);
	}

	t7xx_uevent_send(dl->dev, flash_event);

err_out:
	clear_bit(T7XX_FLASH_STATUS, &dl->status);
	return ret;
}

static int t7xx_devlink_reload_down(struct devlink *devlink, bool netns_change,
				    enum devlink_reload_action action,
				    enum devlink_reload_limit limit,
				    struct netlink_ext_ack *extack)
{
	struct t7xx_devlink *dl = devlink_priv(devlink);

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		dl->set_fastboot_dl = 1;
		return 0;
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		return t7xx_devlink_fb_raw_command(T7XX_FB_CMD_REBOOT, dl->port, NULL);
	default:
		/* Unsupported action should not get to this function */
		return -EOPNOTSUPP;
	}
}

static int t7xx_devlink_reload_up(struct devlink *devlink,
				  enum devlink_reload_action action,
				  enum devlink_reload_limit limit,
				  u32 *actions_performed,
				  struct netlink_ext_ack *extack)
{
	struct t7xx_devlink *dl = devlink_priv(devlink);
	*actions_performed = BIT(action);
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		t7xx_rescan_queue_work(dl->mtk_dev->pdev);
		return 0;
	default:
		/* Unsupported action should not get to this function */
		return -EOPNOTSUPP;
	}
}

/* Call back function for devlink ops */
static const struct devlink_ops devlink_flash_ops = {
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT,
	.flash_update = t7xx_devlink_flash_update,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
			  BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_down = t7xx_devlink_reload_down,
	.reload_up = t7xx_devlink_reload_up,
};

static int t7xx_devlink_region_snapshot(struct devlink *dl, const struct devlink_region_ops *ops,
					struct netlink_ext_ack *extack, u8 **data)
{
	struct t7xx_devlink_region_info *region_info = ops->priv;
	struct t7xx_devlink *t7xx_dl = devlink_priv(dl);
	u8 *snapshot_mem;

	if (t7xx_dl->status != T7XX_DEVLINK_IDLE) {
		dev_err(t7xx_dl->dev, "Modem is busy!");
		return -EBUSY;
	}

	dev_dbg(t7xx_dl->dev, "accessed devlink region:%s index:%d", ops->name, region_info->entry);
	if (!strncmp(ops->name, "mr_dump", strlen("mr_dump"))) {
		if (!region_info->dump) {
			dev_err(t7xx_dl->dev, "devlink region:%s dump memory is not valid!",
				region_info->region_name);
			return -ENOMEM;
		}

		snapshot_mem = vmalloc(region_info->default_size);
		if (!snapshot_mem)
			return -ENOMEM;

		memcpy(snapshot_mem, region_info->dump, region_info->default_size);
		*data = snapshot_mem;
	} else if (!strncmp(ops->name, "lk_dump", strlen("lk_dump"))) {
		int ret;

		ret = t7xx_devlink_fb_dump_log(t7xx_dl->port);
		if (ret)
			return ret;

		*data = region_info->dump;
	}

	return 0;
}

/* To create regions for dump files */
static int t7xx_devlink_create_region(struct t7xx_devlink *dl)
{
	struct devlink_region_ops *region_ops;
	int rc, i;

	region_ops = dl->dl_region_ops;
	for (i = 0; i < T7XX_TOTAL_REGIONS; i++) {
		region_ops[i].name = t7xx_devlink_region_list[i].region_name;
		region_ops[i].snapshot = t7xx_devlink_region_snapshot;
		region_ops[i].destructor = vfree;
		dl->dl_region[i] =
		devlink_region_create(dl->dl_ctx, &region_ops[i], T7XX_MAX_SNAPSHOTS,
				      t7xx_devlink_region_list[i].default_size);

		if (IS_ERR(dl->dl_region[i])) {
			rc = PTR_ERR(dl->dl_region[i]);
			dev_err(dl->dev, "devlink region fail,err %d", rc);
			for ( ; i >= 0; i--)
				devlink_region_destroy(dl->dl_region[i]);

			return rc;
		}

		t7xx_devlink_region_list[i].entry = i;
		region_ops[i].priv = t7xx_devlink_region_list + i;
	}

	return 0;
}

/* To Destroy devlink regions */
static void t7xx_devlink_destroy_region(struct t7xx_devlink *dl)
{
	u8 i;

	for (i = 0; i < T7XX_TOTAL_REGIONS; i++)
		devlink_region_destroy(dl->dl_region[i]);
}

int t7xx_devlink_register(struct t7xx_pci_dev *t7xx_dev)
{
	struct devlink *dl_ctx;

	dl_ctx = devlink_alloc(&devlink_flash_ops, sizeof(struct t7xx_devlink),
			       &t7xx_dev->pdev->dev);
	if (!dl_ctx)
		return -ENOMEM;

	devlink_set_features(dl_ctx, DEVLINK_F_RELOAD);
	devlink_register(dl_ctx);
	t7xx_dev->dl = devlink_priv(dl_ctx);
	t7xx_dev->dl->dl_ctx = dl_ctx;

	return 0;
}

void t7xx_devlink_unregister(struct t7xx_pci_dev *t7xx_dev)
{
	struct devlink *dl_ctx = priv_to_devlink(t7xx_dev->dl);

	devlink_unregister(dl_ctx);
	devlink_free(dl_ctx);
}

/**
 * t7xx_devlink_region_init - Initialize/register devlink to t7xx driver
 * @port: Pointer to port structure
 * @dw: Pointer to devlink work structure
 * @wq: Pointer to devlink workqueue structure
 *
 * Returns: Pointer to t7xx_devlink on success and NULL on failure
 */
static struct t7xx_devlink *t7xx_devlink_region_init(struct t7xx_port *port,
						     struct t7xx_devlink_work *dw,
						     struct workqueue_struct *wq)
{
	struct t7xx_pci_dev *mtk_dev = port->t7xx_dev;
	struct t7xx_devlink *dl = mtk_dev->dl;
	int rc, i;

	dl->dl_ctx = mtk_dev->dl->dl_ctx;
	dl->mtk_dev = mtk_dev;
	dl->dev = &mtk_dev->pdev->dev;
	dl->mode = T7XX_FB_NO_MODE;
	dl->status = T7XX_DEVLINK_IDLE;
	dl->dl_work = dw;
	dl->dl_wq = wq;
	for (i = 0; i < T7XX_TOTAL_REGIONS; i++) {
		dl->dl_region_info[i] = &t7xx_devlink_region_list[i];
		dl->dl_region_info[i]->dump = NULL;
	}
	dl->port = port;
	port->dl = dl;

	rc = t7xx_devlink_create_region(dl);
	if (rc) {
		dev_err(dl->dev, "devlink region creation failed, rc %d", rc);
		return NULL;
	}

	return dl;
}

/**
 * t7xx_devlink_region_deinit - To unintialize the devlink from T7XX driver.
 * @dl:        Devlink instance
 */
static void t7xx_devlink_region_deinit(struct t7xx_devlink *dl)
{
	dl->mode = T7XX_FB_NO_MODE;
	t7xx_devlink_destroy_region(dl);
}

static void t7xx_devlink_work_handler(struct work_struct *data)
{
	struct t7xx_devlink_work *dl_work;

	dl_work = container_of(data, struct t7xx_devlink_work, work);
	t7xx_devlink_fb_get_core(dl_work->port);
}

static int t7xx_devlink_init(struct t7xx_port *port)
{
	struct t7xx_devlink_work *dl_work;
	struct workqueue_struct *wq;

	dl_work = kmalloc(sizeof(*dl_work), GFP_KERNEL);
	if (!dl_work)
		return -ENOMEM;

	wq = create_workqueue("t7xx_devlink");
	if (!wq) {
		kfree(dl_work);
		dev_err(port->dev, "create_workqueue failed\n");
		return -ENODATA;
	}

	INIT_WORK(&dl_work->work, t7xx_devlink_work_handler);
	dl_work->port = port;
	port->rx_length_th = T7XX_MAX_QUEUE_LENGTH;

	if (!t7xx_devlink_region_init(port, dl_work, wq))
		return -ENOMEM;

	return 0;
}

static void t7xx_devlink_uninit(struct t7xx_port *port)
{
	struct t7xx_devlink *dl = port->dl;
	struct sk_buff *skb;
	unsigned long flags;

	vfree(dl->dl_region_info[T7XX_MRDUMP_INDEX]->dump);
	if (dl->dl_wq)
		destroy_workqueue(dl->dl_wq);
	kfree(dl->dl_work);

	t7xx_devlink_region_deinit(port->dl);
	spin_lock_irqsave(&port->rx_skb_list.lock, flags);
	while ((skb = __skb_dequeue(&port->rx_skb_list)) != NULL)
		dev_kfree_skb(skb);
	spin_unlock_irqrestore(&port->rx_skb_list.lock, flags);
}

static int t7xx_devlink_enable_chl(struct t7xx_port *port)
{
	spin_lock(&port->port_update_lock);
	port->chan_enable = true;
	spin_unlock(&port->port_update_lock);

	if (port->dl->dl_wq && port->dl->mode == T7XX_FB_DUMP_MODE)
		queue_work(port->dl->dl_wq, &port->dl->dl_work->work);

	return 0;
}

static int t7xx_devlink_disable_chl(struct t7xx_port *port)
{
	spin_lock(&port->port_update_lock);
	port->chan_enable = false;
	spin_unlock(&port->port_update_lock);

	return 0;
}

struct port_ops devlink_port_ops = {
	.init = &t7xx_devlink_init,
	.recv_skb = &t7xx_port_enqueue_skb,
	.uninit = &t7xx_devlink_uninit,
	.enable_chl = &t7xx_devlink_enable_chl,
	.disable_chl = &t7xx_devlink_disable_chl,
};
