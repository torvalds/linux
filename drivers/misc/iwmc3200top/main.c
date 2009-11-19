/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/main.c
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio.h>

#include "iwmc3200top.h"
#include "log.h"
#include "fw-msg.h"
#include "debugfs.h"


#define DRIVER_DESCRIPTION "Intel(R) IWMC 3200 Top Driver"
#define DRIVER_COPYRIGHT "Copyright (c) 2008 Intel Corporation."

#define DRIVER_VERSION  "0.1.62"

MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_COPYRIGHT);
MODULE_FIRMWARE(FW_NAME(FW_API_VER));

/*
 * This workers main task is to wait for OP_OPR_ALIVE
 * from TOP FW until ALIVE_MSG_TIMOUT timeout is elapsed.
 * When OP_OPR_ALIVE received it will issue
 * a call to "bus_rescan_devices".
 */
static void iwmct_rescan_worker(struct work_struct *ws)
{
	struct iwmct_priv *priv;
	int ret;

	priv = container_of(ws, struct iwmct_priv, bus_rescan_worker);

	LOG_INFO(priv, FW_MSG, "Calling bus_rescan\n");

	ret = bus_rescan_devices(priv->func->dev.bus);
	if (ret < 0)
		LOG_INFO(priv, FW_DOWNLOAD, "bus_rescan_devices FAILED!!!\n");
}

static void op_top_message(struct iwmct_priv *priv, struct top_msg *msg)
{
	switch (msg->hdr.opcode) {
	case OP_OPR_ALIVE:
		LOG_INFO(priv, FW_MSG, "Got ALIVE from device, wake rescan\n");
		queue_work(priv->bus_rescan_wq, &priv->bus_rescan_worker);
		break;
	default:
		LOG_INFO(priv, FW_MSG, "Received msg opcode 0x%X\n",
			msg->hdr.opcode);
		break;
	}
}


static void handle_top_message(struct iwmct_priv *priv, u8 *buf, int len)
{
	struct top_msg *msg;

	msg = (struct top_msg *)buf;

	if (msg->hdr.type != COMM_TYPE_D2H) {
		LOG_ERROR(priv, FW_MSG,
			"Message from TOP with invalid message type 0x%X\n",
			msg->hdr.type);
		return;
	}

	if (len < sizeof(msg->hdr)) {
		LOG_ERROR(priv, FW_MSG,
			"Message from TOP is too short for message header "
			"received %d bytes, expected at least %zd bytes\n",
			len, sizeof(msg->hdr));
		return;
	}

	if (len < le16_to_cpu(msg->hdr.length) + sizeof(msg->hdr)) {
		LOG_ERROR(priv, FW_MSG,
			"Message length (%d bytes) is shorter than "
			"in header (%d bytes)\n",
			len, le16_to_cpu(msg->hdr.length));
		return;
	}

	switch (msg->hdr.category) {
	case COMM_CATEGORY_OPERATIONAL:
		op_top_message(priv, (struct top_msg *)buf);
		break;

	case COMM_CATEGORY_DEBUG:
	case COMM_CATEGORY_TESTABILITY:
	case COMM_CATEGORY_DIAGNOSTICS:
		iwmct_log_top_message(priv, buf, len);
		break;

	default:
		LOG_ERROR(priv, FW_MSG,
			"Message from TOP with unknown category 0x%X\n",
			msg->hdr.category);
		break;
	}
}

int iwmct_send_hcmd(struct iwmct_priv *priv, u8 *cmd, u16 len)
{
	int ret;
	u8 *buf;

	LOG_INFOEX(priv, FW_MSG, "Sending hcmd:\n");

	/* add padding to 256 for IWMC */
	((struct top_msg *)cmd)->hdr.flags |= CMD_FLAG_PADDING_256;

	LOG_HEXDUMP(FW_MSG, cmd, len);

	if (len > FW_HCMD_BLOCK_SIZE) {
		LOG_ERROR(priv, FW_MSG, "size %d exceeded hcmd max size %d\n",
			  len, FW_HCMD_BLOCK_SIZE);
		return -1;
	}

	buf = kzalloc(FW_HCMD_BLOCK_SIZE, GFP_KERNEL);
	if (!buf) {
		LOG_ERROR(priv, FW_MSG, "kzalloc error, buf size %d\n",
			  FW_HCMD_BLOCK_SIZE);
		return -1;
	}

	memcpy(buf, cmd, len);

	sdio_claim_host(priv->func);
	ret = sdio_memcpy_toio(priv->func, IWMC_SDIO_DATA_ADDR, buf,
			       FW_HCMD_BLOCK_SIZE);
	sdio_release_host(priv->func);

	kfree(buf);
	return ret;
}

int iwmct_tx(struct iwmct_priv *priv, unsigned int addr,
	void *src, int count)
{
	int ret;

	sdio_claim_host(priv->func);
	ret = sdio_memcpy_toio(priv->func, addr, src, count);
	sdio_release_host(priv->func);

	return ret;
}

static void iwmct_irq_read_worker(struct work_struct *ws)
{
	struct iwmct_priv *priv;
	struct iwmct_work_struct *read_req;
	__le32 *buf = NULL;
	int ret;
	int iosize;
	u32 barker;
	bool is_barker;

	priv = container_of(ws, struct iwmct_priv, isr_worker);

	LOG_INFO(priv, IRQ, "enter iwmct_irq_read_worker %p\n", ws);

	/* --------------------- Handshake with device -------------------- */
	sdio_claim_host(priv->func);

	/* all list manipulations have to be protected by
	 * sdio_claim_host/sdio_release_host */
	if (list_empty(&priv->read_req_list)) {
		LOG_ERROR(priv, IRQ, "read_req_list empty in read worker\n");
		goto exit_release;
	}

	read_req = list_entry(priv->read_req_list.next,
			      struct iwmct_work_struct, list);

	list_del(&read_req->list);
	iosize = read_req->iosize;
	kfree(read_req);

	buf = kzalloc(iosize, GFP_KERNEL);
	if (!buf) {
		LOG_ERROR(priv, IRQ, "kzalloc error, buf size %d\n", iosize);
		goto exit_release;
	}

	LOG_INFO(priv, IRQ, "iosize=%d, buf=%p, func=%d\n",
				iosize, buf, priv->func->num);

	/* read from device */
	ret = sdio_memcpy_fromio(priv->func, buf, IWMC_SDIO_DATA_ADDR, iosize);
	if (ret) {
		LOG_ERROR(priv, IRQ, "error %d reading buffer\n", ret);
		goto exit_release;
	}

	LOG_HEXDUMP(IRQ, (u8 *)buf, iosize);

	barker = le32_to_cpu(buf[0]);

	/* Verify whether it's a barker and if not - treat as regular Rx */
	if (barker == IWMC_BARKER_ACK ||
	    (barker & BARKER_DNLOAD_BARKER_MSK) == IWMC_BARKER_REBOOT) {

		/* Valid Barker is equal on first 4 dwords */
		is_barker = (buf[1] == buf[0]) &&
			    (buf[2] == buf[0]) &&
			    (buf[3] == buf[0]);

		if (!is_barker) {
			LOG_WARNING(priv, IRQ,
				"Potentially inconsistent barker "
				"%08X_%08X_%08X_%08X\n",
				le32_to_cpu(buf[0]), le32_to_cpu(buf[1]),
				le32_to_cpu(buf[2]), le32_to_cpu(buf[3]));
		}
	} else {
		is_barker = false;
	}

	/* Handle Top CommHub message */
	if (!is_barker) {
		sdio_release_host(priv->func);
		handle_top_message(priv, (u8 *)buf, iosize);
		goto exit;
	} else if (barker == IWMC_BARKER_ACK) { /* Handle barkers */
		if (atomic_read(&priv->dev_sync) == 0) {
			LOG_ERROR(priv, IRQ,
				  "ACK barker arrived out-of-sync\n");
			goto exit_release;
		}

		/* Continuing to FW download (after Sync is completed)*/
		atomic_set(&priv->dev_sync, 0);
		LOG_INFO(priv, IRQ, "ACK barker arrived "
				"- starting FW download\n");
	} else { /* REBOOT barker */
		LOG_INFO(priv, IRQ, "Recieved reboot barker: %x\n", barker);
		priv->barker = barker;

		if (barker & BARKER_DNLOAD_SYNC_MSK) {
			/* Send the same barker back */
			ret = sdio_memcpy_toio(priv->func, IWMC_SDIO_DATA_ADDR,
					       buf, iosize);
			if (ret) {
				LOG_ERROR(priv, IRQ,
					 "error %d echoing barker\n", ret);
				goto exit_release;
			}
			LOG_INFO(priv, IRQ, "Echoing barker to device\n");
			atomic_set(&priv->dev_sync, 1);
			goto exit_release;
		}

		/* Continuing to FW download (without Sync) */
		LOG_INFO(priv, IRQ, "No sync requested "
				    "- starting FW download\n");
	}

	sdio_release_host(priv->func);


	LOG_INFO(priv, IRQ, "barker download request 0x%x is:\n", priv->barker);
	LOG_INFO(priv, IRQ, "*******  Top FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_TOP_MSK) ? "was" : "not");
	LOG_INFO(priv, IRQ, "*******  GPS FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_GPS_MSK) ? "was" : "not");
	LOG_INFO(priv, IRQ, "*******  BT FW %s requested ********\n",
			(priv->barker & BARKER_DNLOAD_BT_MSK) ? "was" : "not");

	if (priv->dbg.fw_download)
		iwmct_fw_load(priv);
	else
		LOG_ERROR(priv, IRQ, "FW download not allowed\n");

	goto exit;

exit_release:
	sdio_release_host(priv->func);
exit:
	kfree(buf);
	LOG_INFO(priv, IRQ, "exit iwmct_irq_read_worker\n");
}

static void iwmct_irq(struct sdio_func *func)
{
	struct iwmct_priv *priv;
	int val, ret;
	int iosize;
	int addr = IWMC_SDIO_INTR_GET_SIZE_ADDR;
	struct iwmct_work_struct *read_req;

	priv = sdio_get_drvdata(func);

	LOG_INFO(priv, IRQ, "enter iwmct_irq\n");

	/* read the function's status register */
	val = sdio_readb(func, IWMC_SDIO_INTR_STATUS_ADDR, &ret);

	LOG_INFO(priv, IRQ, "iir value = %d, ret=%d\n", val, ret);

	if (!val) {
		LOG_ERROR(priv, IRQ, "iir = 0, exiting ISR\n");
		goto exit_clear_intr;
	}


	/*
	 * read 2 bytes of the transaction size
	 * IMPORTANT: sdio transaction size has to be read before clearing
	 * sdio interrupt!!!
	 */
	val = sdio_readb(priv->func, addr++, &ret);
	iosize = val;
	val = sdio_readb(priv->func, addr++, &ret);
	iosize += val << 8;

	LOG_INFO(priv, IRQ, "READ size %d\n", iosize);

	if (iosize == 0) {
		LOG_ERROR(priv, IRQ, "READ size %d, exiting ISR\n", iosize);
		goto exit_clear_intr;
	}

	/* allocate a work structure to pass iosize to the worker */
	read_req = kzalloc(sizeof(struct iwmct_work_struct), GFP_KERNEL);
	if (!read_req) {
		LOG_ERROR(priv, IRQ, "failed to allocate read_req, exit ISR\n");
		goto exit_clear_intr;
	}

	INIT_LIST_HEAD(&read_req->list);
	read_req->iosize = iosize;

	list_add_tail(&priv->read_req_list, &read_req->list);

	/* clear the function's interrupt request bit (write 1 to clear) */
	sdio_writeb(func, 1, IWMC_SDIO_INTR_CLEAR_ADDR, &ret);

	queue_work(priv->wq, &priv->isr_worker);

	LOG_INFO(priv, IRQ, "exit iwmct_irq\n");

	return;

exit_clear_intr:
	/* clear the function's interrupt request bit (write 1 to clear) */
	sdio_writeb(func, 1, IWMC_SDIO_INTR_CLEAR_ADDR, &ret);
}


static int blocks;
module_param(blocks, int, 0604);
MODULE_PARM_DESC(blocks, "max_blocks_to_send");

static int dump;
module_param(dump, bool, 0604);
MODULE_PARM_DESC(dump, "dump_hex_content");

static int jump = 1;
module_param(jump, bool, 0604);

static int direct = 1;
module_param(direct, bool, 0604);

static int checksum = 1;
module_param(checksum, bool, 0604);

static int fw_download = 1;
module_param(fw_download, bool, 0604);

static int block_size = IWMC_SDIO_BLK_SIZE;
module_param(block_size, int, 0404);

static int download_trans_blks = IWMC_DEFAULT_TR_BLK;
module_param(download_trans_blks, int, 0604);

static int rubbish_barker;
module_param(rubbish_barker, bool, 0604);

#ifdef CONFIG_IWMC3200TOP_DEBUG
static int log_level[LOG_SRC_MAX];
static unsigned int log_level_argc;
module_param_array(log_level, int, &log_level_argc, 0604);
MODULE_PARM_DESC(log_level, "log_level");

static int log_level_fw[FW_LOG_SRC_MAX];
static unsigned int log_level_fw_argc;
module_param_array(log_level_fw, int, &log_level_fw_argc, 0604);
MODULE_PARM_DESC(log_level_fw, "log_level_fw");
#endif

void iwmct_dbg_init_params(struct iwmct_priv *priv)
{
#ifdef CONFIG_IWMC3200TOP_DEBUG
	int i;

	for (i = 0; i < log_level_argc; i++) {
		dev_notice(&priv->func->dev, "log_level[%d]=0x%X\n",
						i, log_level[i]);
		iwmct_log_set_filter((log_level[i] >> 8) & 0xFF,
			       log_level[i] & 0xFF);
	}
	for (i = 0; i < log_level_fw_argc; i++) {
		dev_notice(&priv->func->dev, "log_level_fw[%d]=0x%X\n",
						i, log_level_fw[i]);
		iwmct_log_set_fw_filter((log_level_fw[i] >> 8) & 0xFF,
				  log_level_fw[i] & 0xFF);
	}
#endif

	priv->dbg.blocks = blocks;
	LOG_INFO(priv, INIT, "blocks=%d\n", blocks);
	priv->dbg.dump = (bool)dump;
	LOG_INFO(priv, INIT, "dump=%d\n", dump);
	priv->dbg.jump = (bool)jump;
	LOG_INFO(priv, INIT, "jump=%d\n", jump);
	priv->dbg.direct = (bool)direct;
	LOG_INFO(priv, INIT, "direct=%d\n", direct);
	priv->dbg.checksum = (bool)checksum;
	LOG_INFO(priv, INIT, "checksum=%d\n", checksum);
	priv->dbg.fw_download = (bool)fw_download;
	LOG_INFO(priv, INIT, "fw_download=%d\n", fw_download);
	priv->dbg.block_size = block_size;
	LOG_INFO(priv, INIT, "block_size=%d\n", block_size);
	priv->dbg.download_trans_blks = download_trans_blks;
	LOG_INFO(priv, INIT, "download_trans_blks=%d\n", download_trans_blks);
}

/*****************************************************************************
 *
 * sysfs attributes
 *
 *****************************************************************************/
static ssize_t show_iwmct_fw_version(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	struct iwmct_priv *priv = dev_get_drvdata(d);
	return sprintf(buf, "%s\n", priv->dbg.label_fw);
}
static DEVICE_ATTR(cc_label_fw, S_IRUGO, show_iwmct_fw_version, NULL);

#ifdef CONFIG_IWMC3200TOP_DEBUG
static DEVICE_ATTR(log_level, S_IWUSR | S_IRUGO,
		   show_iwmct_log_level, store_iwmct_log_level);
static DEVICE_ATTR(log_level_fw, S_IWUSR | S_IRUGO,
		   show_iwmct_log_level_fw, store_iwmct_log_level_fw);
#endif

static struct attribute *iwmct_sysfs_entries[] = {
	&dev_attr_cc_label_fw.attr,
#ifdef CONFIG_IWMC3200TOP_DEBUG
	&dev_attr_log_level.attr,
	&dev_attr_log_level_fw.attr,
#endif
	NULL
};

static struct attribute_group iwmct_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = iwmct_sysfs_entries,
};


static int iwmct_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct iwmct_priv *priv;
	int ret;
	int val = 1;
	int addr = IWMC_SDIO_INTR_ENABLE_ADDR;

	dev_dbg(&func->dev, "enter iwmct_probe\n");

	dev_dbg(&func->dev, "IRQ polling period id %u msecs, HZ is %d\n",
		jiffies_to_msecs(2147483647), HZ);

	priv = kzalloc(sizeof(struct iwmct_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&func->dev, "kzalloc error\n");
		return -ENOMEM;
	}
	priv->func = func;
	sdio_set_drvdata(func, priv);


	/* create drivers work queue */
	priv->wq = create_workqueue(DRV_NAME "_wq");
	priv->bus_rescan_wq = create_workqueue(DRV_NAME "_rescan_wq");
	INIT_WORK(&priv->bus_rescan_worker, iwmct_rescan_worker);
	INIT_WORK(&priv->isr_worker, iwmct_irq_read_worker);

	init_waitqueue_head(&priv->wait_q);

	sdio_claim_host(func);
	/* FIXME: Remove after it is fixed in the Boot ROM upgrade */
	func->enable_timeout = 10;

	/* In our HW, setting the block size also wakes up the boot rom. */
	ret = sdio_set_block_size(func, priv->dbg.block_size);
	if (ret) {
		LOG_ERROR(priv, INIT,
			"sdio_set_block_size() failure: %d\n", ret);
		goto error_sdio_enable;
	}

	ret = sdio_enable_func(func);
	if (ret) {
		LOG_ERROR(priv, INIT, "sdio_enable_func() failure: %d\n", ret);
		goto error_sdio_enable;
	}

	/* init reset and dev_sync states */
	atomic_set(&priv->reset, 0);
	atomic_set(&priv->dev_sync, 0);

	/* init read req queue */
	INIT_LIST_HEAD(&priv->read_req_list);

	/* process configurable parameters */
	iwmct_dbg_init_params(priv);
	ret = sysfs_create_group(&func->dev.kobj, &iwmct_attribute_group);
	if (ret) {
		LOG_ERROR(priv, INIT, "Failed to register attributes and "
			 "initialize module_params\n");
		goto error_dev_attrs;
	}

	iwmct_dbgfs_register(priv, DRV_NAME);

	if (!priv->dbg.direct && priv->dbg.download_trans_blks > 8) {
		LOG_INFO(priv, INIT,
			 "Reducing transaction to 8 blocks = 2K (from %d)\n",
			 priv->dbg.download_trans_blks);
		priv->dbg.download_trans_blks = 8;
	}
	priv->trans_len = priv->dbg.download_trans_blks * priv->dbg.block_size;
	LOG_INFO(priv, INIT, "Transaction length = %d\n", priv->trans_len);

	ret = sdio_claim_irq(func, iwmct_irq);
	if (ret) {
		LOG_ERROR(priv, INIT, "sdio_claim_irq() failure: %d\n", ret);
		goto error_claim_irq;
	}


	/* Enable function's interrupt */
	sdio_writeb(priv->func, val, addr, &ret);
	if (ret) {
		LOG_ERROR(priv, INIT, "Failure writing to "
			  "Interrupt Enable Register (%d): %d\n", addr, ret);
		goto error_enable_int;
	}

	sdio_release_host(func);

	LOG_INFO(priv, INIT, "exit iwmct_probe\n");

	return ret;

error_enable_int:
	sdio_release_irq(func);
error_claim_irq:
	sdio_disable_func(func);
error_dev_attrs:
	iwmct_dbgfs_unregister(priv->dbgfs);
	sysfs_remove_group(&func->dev.kobj, &iwmct_attribute_group);
error_sdio_enable:
	sdio_release_host(func);
	return ret;
}

static void iwmct_remove(struct sdio_func *func)
{
	struct iwmct_work_struct *read_req;
	struct iwmct_priv *priv = sdio_get_drvdata(func);

	priv = sdio_get_drvdata(func);

	LOG_INFO(priv, INIT, "enter\n");

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_release_host(func);

	/* Safely destroy osc workqueue */
	destroy_workqueue(priv->bus_rescan_wq);
	destroy_workqueue(priv->wq);

	sdio_claim_host(func);
	sdio_disable_func(func);
	sysfs_remove_group(&func->dev.kobj, &iwmct_attribute_group);
	iwmct_dbgfs_unregister(priv->dbgfs);
	sdio_release_host(func);

	/* free read requests */
	while (!list_empty(&priv->read_req_list)) {
		read_req = list_entry(priv->read_req_list.next,
			struct iwmct_work_struct, list);

		list_del(&read_req->list);
		kfree(read_req);
	}

	kfree(priv);
}


static const struct sdio_device_id iwmct_ids[] = {
	/* Intel Wireless MultiCom 3200 Top Driver */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_INTEL, 0x1404)},
	{ },	/* Terminating entry */
};

MODULE_DEVICE_TABLE(sdio, iwmct_ids);

static struct sdio_driver iwmct_driver = {
	.probe		= iwmct_probe,
	.remove		= iwmct_remove,
	.name		= DRV_NAME,
	.id_table	= iwmct_ids,
};

static int __init iwmct_init(void)
{
	int rc;

	/* Default log filter settings */
	iwmct_log_set_filter(LOG_SRC_ALL, LOG_SEV_FILTER_RUNTIME);
	iwmct_log_set_filter(LOG_SRC_FW_MSG, LOG_SEV_FILTER_ALL);
	iwmct_log_set_fw_filter(LOG_SRC_ALL, FW_LOG_SEV_FILTER_RUNTIME);

	rc = sdio_register_driver(&iwmct_driver);

	return rc;
}

static void __exit iwmct_exit(void)
{
	sdio_unregister_driver(&iwmct_driver);
}

module_init(iwmct_init);
module_exit(iwmct_exit);

