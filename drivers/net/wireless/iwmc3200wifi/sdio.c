/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

/*
 * This is the SDIO bus specific hooks for iwm.
 * It also is the module's entry point.
 *
 * Interesting code paths:
 * iwm_sdio_probe() (Called by an SDIO bus scan)
 *  -> iwm_if_alloc() (netdev.c)
 *      -> iwm_wdev_alloc() (cfg80211.c, allocates and register our wiphy)
 *          -> wiphy_new()
 *          -> wiphy_register()
 *      -> alloc_netdev_mq()
 *      -> register_netdev()
 *
 * iwm_sdio_remove()
 *  -> iwm_if_free() (netdev.c)
 *      -> unregister_netdev()
 *      -> iwm_wdev_free() (cfg80211.c)
 *          -> wiphy_unregister()
 *          -> wiphy_free()
 *
 * iwm_sdio_isr() (called in process context from the SDIO core code)
 *  -> queue_work(.., isr_worker)
 *      -- [async] --> iwm_sdio_isr_worker()
 *                      -> iwm_rx_handle()
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/debugfs.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>

#include "iwm.h"
#include "debug.h"
#include "bus.h"
#include "sdio.h"

static void iwm_sdio_isr_worker(struct work_struct *work)
{
	struct iwm_sdio_priv *hw;
	struct iwm_priv *iwm;
	struct iwm_rx_info *rx_info;
	struct sk_buff *skb;
	u8 *rx_buf;
	unsigned long rx_size;

	hw = container_of(work, struct iwm_sdio_priv, isr_worker);
	iwm = hw_to_iwm(hw);

	while (!skb_queue_empty(&iwm->rx_list)) {
		skb = skb_dequeue(&iwm->rx_list);
		rx_info = skb_to_rx_info(skb);
		rx_size = rx_info->rx_size;
		rx_buf = skb->data;

		IWM_HEXDUMP(iwm, DBG, SDIO, "RX: ", rx_buf, rx_size);
		if (iwm_rx_handle(iwm, rx_buf, rx_size) < 0)
			IWM_WARN(iwm, "RX error\n");

		kfree_skb(skb);
	}
}

static void iwm_sdio_isr(struct sdio_func *func)
{
	struct iwm_priv *iwm;
	struct iwm_sdio_priv *hw;
	struct iwm_rx_info *rx_info;
	struct sk_buff *skb;
	unsigned long buf_size, read_size;
	int ret;
	u8 val;

	hw = sdio_get_drvdata(func);
	iwm = hw_to_iwm(hw);

	buf_size = hw->blk_size;

	/* We're checking the status */
	val = sdio_readb(func, IWM_SDIO_INTR_STATUS_ADDR, &ret);
	if (val == 0 || ret < 0) {
		IWM_ERR(iwm, "Wrong INTR_STATUS\n");
		return;
	}

	/* See if we have free buffers */
	if (skb_queue_len(&iwm->rx_list) > IWM_RX_LIST_SIZE) {
		IWM_ERR(iwm, "No buffer for more Rx frames\n");
		return;
	}

	/* We first read the transaction size */
	read_size = sdio_readb(func, IWM_SDIO_INTR_GET_SIZE_ADDR + 1, &ret);
	read_size = read_size << 8;

	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't read the xfer size\n");
		return;
	}

	/* We need to clear the INT register */
	sdio_writeb(func, 1, IWM_SDIO_INTR_CLEAR_ADDR, &ret);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't clear the INT register\n");
		return;
	}

	while (buf_size < read_size)
		buf_size <<= 1;

	skb = dev_alloc_skb(buf_size);
	if (!skb) {
		IWM_ERR(iwm, "Couldn't alloc RX skb\n");
		return;
	}
	rx_info = skb_to_rx_info(skb);
	rx_info->rx_size = read_size;
	rx_info->rx_buf_size = buf_size;

	/* Now we can read the actual buffer */
	ret = sdio_memcpy_fromio(func, skb_put(skb, read_size),
				 IWM_SDIO_DATA_ADDR, read_size);

	/* The skb is put on a driver's specific Rx SKB list */
	skb_queue_tail(&iwm->rx_list, skb);

	/* We can now schedule the actual worker */
	queue_work(hw->isr_wq, &hw->isr_worker);
}

static void iwm_sdio_rx_free(struct iwm_sdio_priv *hw)
{
	struct iwm_priv *iwm = hw_to_iwm(hw);

	flush_workqueue(hw->isr_wq);

	skb_queue_purge(&iwm->rx_list);
}

/* Bus ops */
static int if_sdio_enable(struct iwm_priv *iwm)
{
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);
	int ret;

	sdio_claim_host(hw->func);

	ret = sdio_enable_func(hw->func);
	if (ret) {
		IWM_ERR(iwm, "Couldn't enable the device: is TOP driver "
			"loaded and functional?\n");
		goto release_host;
	}

	iwm_reset(iwm);

	ret = sdio_claim_irq(hw->func, iwm_sdio_isr);
	if (ret) {
		IWM_ERR(iwm, "Failed to claim irq: %d\n", ret);
		goto release_host;
	}

	sdio_writeb(hw->func, 1, IWM_SDIO_INTR_ENABLE_ADDR, &ret);
	if (ret < 0) {
		IWM_ERR(iwm, "Couldn't enable INTR: %d\n", ret);
		goto release_irq;
	}

	sdio_release_host(hw->func);

	IWM_DBG_SDIO(iwm, INFO, "IWM SDIO enable\n");

	return 0;

 release_irq:
	sdio_release_irq(hw->func);
 release_host:
	sdio_release_host(hw->func);

	return ret;
}

static int if_sdio_disable(struct iwm_priv *iwm)
{
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);
	int ret;

	sdio_claim_host(hw->func);
	sdio_writeb(hw->func, 0, IWM_SDIO_INTR_ENABLE_ADDR, &ret);
	if (ret < 0)
		IWM_WARN(iwm, "Couldn't disable INTR: %d\n", ret);

	sdio_release_irq(hw->func);
	sdio_disable_func(hw->func);
	sdio_release_host(hw->func);

	iwm_sdio_rx_free(hw);

	iwm_reset(iwm);

	IWM_DBG_SDIO(iwm, INFO, "IWM SDIO disable\n");

	return 0;
}

static int if_sdio_send_chunk(struct iwm_priv *iwm, u8 *buf, int count)
{
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);
	int aligned_count = ALIGN(count, hw->blk_size);
	int ret;

	if ((unsigned long)buf & 0x3) {
		IWM_ERR(iwm, "buf <%p> is not dword aligned\n", buf);
		/* TODO: Is this a hardware limitation? use get_unligned */
		return -EINVAL;
	}

	sdio_claim_host(hw->func);
	ret = sdio_memcpy_toio(hw->func, IWM_SDIO_DATA_ADDR, buf,
			       aligned_count);
	sdio_release_host(hw->func);

	return ret;
}

/* debugfs hooks */
static int iwm_debugfs_sdio_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t iwm_debugfs_sdio_read(struct file *filp, char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct iwm_priv *iwm = filp->private_data;
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);
	char *buf;
	u8 cccr;
	int buf_len = 4096, ret;
	size_t len = 0;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	sdio_claim_host(hw->func);

	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_IOEx, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_IOEx\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_IOEx:  0x%x\n", cccr);

	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_IORx, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_IORx\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_IORx:  0x%x\n", cccr);


	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_IENx, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_IENx\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_IENx:  0x%x\n", cccr);


	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_INTx, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_INTx\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_INTx:  0x%x\n", cccr);


	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_ABORT, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_ABORTx\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_ABORT: 0x%x\n", cccr);

	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_IF, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_IF\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_IF:    0x%x\n", cccr);


	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_CAPS, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_CAPS\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_CAPS:  0x%x\n", cccr);

	cccr =  sdio_f0_readb(hw->func, SDIO_CCCR_CIS, &ret);
	if (ret) {
		IWM_ERR(iwm, "Could not read SDIO_CCCR_CIS\n");
		goto err;
	}
	len += snprintf(buf + len, buf_len - len, "CCCR_CIS:   0x%x\n", cccr);

	ret = simple_read_from_buffer(buffer, len, ppos, buf, buf_len);
err:
	sdio_release_host(hw->func);

	kfree(buf);

	return ret;
}

static const struct file_operations iwm_debugfs_sdio_fops = {
	.owner =	THIS_MODULE,
	.open =		iwm_debugfs_sdio_open,
	.read =		iwm_debugfs_sdio_read,
	.llseek =	default_llseek,
};

static void if_sdio_debugfs_init(struct iwm_priv *iwm, struct dentry *parent_dir)
{
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);

	hw->cccr_dentry = debugfs_create_file("cccr", 0200,
					      parent_dir, iwm,
					      &iwm_debugfs_sdio_fops);
}

static void if_sdio_debugfs_exit(struct iwm_priv *iwm)
{
	struct iwm_sdio_priv *hw = iwm_to_if_sdio(iwm);

	debugfs_remove(hw->cccr_dentry);
}

static struct iwm_if_ops if_sdio_ops = {
	.enable = if_sdio_enable,
	.disable = if_sdio_disable,
	.send_chunk = if_sdio_send_chunk,
	.debugfs_init = if_sdio_debugfs_init,
	.debugfs_exit = if_sdio_debugfs_exit,
	.umac_name = "iwmc3200wifi-umac-sdio.bin",
	.calib_lmac_name = "iwmc3200wifi-calib-sdio.bin",
	.lmac_name = "iwmc3200wifi-lmac-sdio.bin",
};
MODULE_FIRMWARE("iwmc3200wifi-umac-sdio.bin");
MODULE_FIRMWARE("iwmc3200wifi-calib-sdio.bin");
MODULE_FIRMWARE("iwmc3200wifi-lmac-sdio.bin");

static int iwm_sdio_probe(struct sdio_func *func,
			  const struct sdio_device_id *id)
{
	struct iwm_priv *iwm;
	struct iwm_sdio_priv *hw;
	struct device *dev = &func->dev;
	int ret;

	/* check if TOP has already initialized the card */
	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	if (ret) {
		dev_err(dev, "wait for TOP to enable the device\n");
		sdio_release_host(func);
		return ret;
	}

	ret = sdio_set_block_size(func, IWM_SDIO_BLK_SIZE);

	sdio_disable_func(func);
	sdio_release_host(func);

	if (ret < 0) {
		dev_err(dev, "Failed to set block size: %d\n", ret);
		return ret;
	}

	iwm = iwm_if_alloc(sizeof(struct iwm_sdio_priv), dev, &if_sdio_ops);
	if (IS_ERR(iwm)) {
		dev_err(dev, "allocate SDIO interface failed\n");
		return PTR_ERR(iwm);
	}

	hw = iwm_private(iwm);
	hw->iwm = iwm;

	iwm_debugfs_init(iwm);

	sdio_set_drvdata(func, hw);

	hw->func = func;
	hw->blk_size = IWM_SDIO_BLK_SIZE;

	hw->isr_wq = create_singlethread_workqueue(KBUILD_MODNAME "_sdio");
	if (!hw->isr_wq) {
		ret = -ENOMEM;
		goto debugfs_exit;
	}

	INIT_WORK(&hw->isr_worker, iwm_sdio_isr_worker);

	ret = iwm_if_add(iwm);
	if (ret) {
		dev_err(dev, "add SDIO interface failed\n");
		goto destroy_wq;
	}

	dev_info(dev, "IWM SDIO probe\n");

	return 0;

 destroy_wq:
	destroy_workqueue(hw->isr_wq);
 debugfs_exit:
	iwm_debugfs_exit(iwm);
	iwm_if_free(iwm);
	return ret;
}

static void iwm_sdio_remove(struct sdio_func *func)
{
	struct iwm_sdio_priv *hw = sdio_get_drvdata(func);
	struct iwm_priv *iwm = hw_to_iwm(hw);
	struct device *dev = &func->dev;

	iwm_if_remove(iwm);
	destroy_workqueue(hw->isr_wq);
	iwm_debugfs_exit(iwm);
	iwm_if_free(iwm);

	sdio_set_drvdata(func, NULL);

	dev_info(dev, "IWM SDIO remove\n");
}

static const struct sdio_device_id iwm_sdio_ids[] = {
	/* Global/AGN SKU */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_INTEL, 0x1403) },
	/* BGN SKU */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_INTEL, 0x1408) },
	{ /* end: all zeroes */	},
};
MODULE_DEVICE_TABLE(sdio, iwm_sdio_ids);

static struct sdio_driver iwm_sdio_driver = {
	.name		= "iwm_sdio",
	.id_table	= iwm_sdio_ids,
	.probe		= iwm_sdio_probe,
	.remove		= iwm_sdio_remove,
};

static int __init iwm_sdio_init_module(void)
{
	return sdio_register_driver(&iwm_sdio_driver);
}

static void __exit iwm_sdio_exit_module(void)
{
	sdio_unregister_driver(&iwm_sdio_driver);
}

module_init(iwm_sdio_init_module);
module_exit(iwm_sdio_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(IWM_COPYRIGHT " " IWM_AUTHOR);
