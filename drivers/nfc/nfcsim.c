/*
 * NFC hardware simulation driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/nfc.h>
#include <net/nfc/nfc.h>
#include <net/nfc/digital.h>

#define NFCSIM_ERR(d, fmt, args...) nfc_err(&d->nfc_digital_dev->nfc_dev->dev, \
					    "%s: " fmt, __func__, ## args)

#define NFCSIM_DBG(d, fmt, args...) dev_dbg(&d->nfc_digital_dev->nfc_dev->dev, \
					    "%s: " fmt, __func__, ## args)

#define NFCSIM_VERSION "0.2"

#define NFCSIM_MODE_NONE	0
#define NFCSIM_MODE_INITIATOR	1
#define NFCSIM_MODE_TARGET	2

#define NFCSIM_CAPABILITIES (NFC_DIGITAL_DRV_CAPS_IN_CRC   | \
			     NFC_DIGITAL_DRV_CAPS_TG_CRC)

struct nfcsim {
	struct nfc_digital_dev *nfc_digital_dev;

	struct work_struct recv_work;
	struct delayed_work send_work;

	struct nfcsim_link *link_in;
	struct nfcsim_link *link_out;

	bool up;
	u8 mode;
	u8 rf_tech;

	u16 recv_timeout;

	nfc_digital_cmd_complete_t cb;
	void *arg;
};

struct nfcsim_link {
	struct mutex lock;

	u8 rf_tech;
	u8 mode;

	u8 shutdown;

	struct sk_buff *skb;
	wait_queue_head_t recv_wait;
	u8 cond;
};

static struct nfcsim_link *nfcsim_link_new(void)
{
	struct nfcsim_link *link;

	link = kzalloc(sizeof(struct nfcsim_link), GFP_KERNEL);
	if (!link)
		return NULL;

	mutex_init(&link->lock);
	init_waitqueue_head(&link->recv_wait);

	return link;
}

static void nfcsim_link_free(struct nfcsim_link *link)
{
	dev_kfree_skb(link->skb);
	kfree(link);
}

static void nfcsim_link_recv_wake(struct nfcsim_link *link)
{
	link->cond = 1;
	wake_up_interruptible(&link->recv_wait);
}

static void nfcsim_link_set_skb(struct nfcsim_link *link, struct sk_buff *skb,
				u8 rf_tech, u8 mode)
{
	mutex_lock(&link->lock);

	dev_kfree_skb(link->skb);
	link->skb = skb;
	link->rf_tech = rf_tech;
	link->mode = mode;

	mutex_unlock(&link->lock);
}

static void nfcsim_link_recv_cancel(struct nfcsim_link *link)
{
	mutex_lock(&link->lock);

	link->mode = NFCSIM_MODE_NONE;

	mutex_unlock(&link->lock);

	nfcsim_link_recv_wake(link);
}

static void nfcsim_link_shutdown(struct nfcsim_link *link)
{
	mutex_lock(&link->lock);

	link->shutdown = 1;
	link->mode = NFCSIM_MODE_NONE;

	mutex_unlock(&link->lock);

	nfcsim_link_recv_wake(link);
}

static struct sk_buff *nfcsim_link_recv_skb(struct nfcsim_link *link,
					    int timeout, u8 rf_tech, u8 mode)
{
	int rc;
	struct sk_buff *skb;

	rc = wait_event_interruptible_timeout(link->recv_wait,
					      link->cond,
					      msecs_to_jiffies(timeout));

	mutex_lock(&link->lock);

	skb = link->skb;
	link->skb = NULL;

	if (!rc) {
		rc = -ETIMEDOUT;
		goto done;
	}

	if (!skb || link->rf_tech != rf_tech || link->mode == mode) {
		rc = -EINVAL;
		goto done;
	}

	if (link->shutdown) {
		rc = -ENODEV;
		goto done;
	}

done:
	mutex_unlock(&link->lock);

	if (rc < 0) {
		dev_kfree_skb(skb);
		skb = ERR_PTR(rc);
	}

	link->cond = 0;

	return skb;
}

static void nfcsim_send_wq(struct work_struct *work)
{
	struct nfcsim *dev = container_of(work, struct nfcsim, send_work.work);

	/*
	 * To effectively send data, the device just wake up its link_out which
	 * is the link_in of the peer device. The exchanged skb has already been
	 * stored in the dev->link_out through nfcsim_link_set_skb().
	 */
	nfcsim_link_recv_wake(dev->link_out);
}

static void nfcsim_recv_wq(struct work_struct *work)
{
	struct nfcsim *dev = container_of(work, struct nfcsim, recv_work);
	struct sk_buff *skb;

	skb = nfcsim_link_recv_skb(dev->link_in, dev->recv_timeout,
				   dev->rf_tech, dev->mode);

	if (!dev->up) {
		NFCSIM_ERR(dev, "Device is down\n");

		if (!IS_ERR(skb))
			dev_kfree_skb(skb);

		skb = ERR_PTR(-ENODEV);
	}

	dev->cb(dev->nfc_digital_dev, dev->arg, skb);
}

static int nfcsim_send(struct nfc_digital_dev *ddev, struct sk_buff *skb,
		       u16 timeout, nfc_digital_cmd_complete_t cb, void *arg)
{
	struct nfcsim *dev = nfc_digital_get_drvdata(ddev);
	u8 delay;

	if (!dev->up) {
		NFCSIM_ERR(dev, "Device is down\n");
		return -ENODEV;
	}

	dev->recv_timeout = timeout;
	dev->cb = cb;
	dev->arg = arg;

	schedule_work(&dev->recv_work);

	if (skb) {
		nfcsim_link_set_skb(dev->link_out, skb, dev->rf_tech,
				    dev->mode);

		/* Add random delay (between 3 and 10 ms) before sending data */
		get_random_bytes(&delay, 1);
		delay = 3 + (delay & 0x07);

		schedule_delayed_work(&dev->send_work, msecs_to_jiffies(delay));
	}

	return 0;
}

static void nfcsim_abort_cmd(struct nfc_digital_dev *ddev)
{
	struct nfcsim *dev = nfc_digital_get_drvdata(ddev);

	nfcsim_link_recv_cancel(dev->link_in);
}

static int nfcsim_switch_rf(struct nfc_digital_dev *ddev, bool on)
{
	struct nfcsim *dev = nfc_digital_get_drvdata(ddev);

	dev->up = on;

	return 0;
}

static int nfcsim_in_configure_hw(struct nfc_digital_dev *ddev,
					  int type, int param)
{
	struct nfcsim *dev = nfc_digital_get_drvdata(ddev);

	switch (type) {
	case NFC_DIGITAL_CONFIG_RF_TECH:
		dev->up = true;
		dev->mode = NFCSIM_MODE_INITIATOR;
		dev->rf_tech = param;
		break;

	case NFC_DIGITAL_CONFIG_FRAMING:
		break;

	default:
		NFCSIM_ERR(dev, "Invalid configuration type: %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int nfcsim_in_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return nfcsim_send(ddev, skb, timeout, cb, arg);
}

static int nfcsim_tg_configure_hw(struct nfc_digital_dev *ddev,
					  int type, int param)
{
	struct nfcsim *dev = nfc_digital_get_drvdata(ddev);

	switch (type) {
	case NFC_DIGITAL_CONFIG_RF_TECH:
		dev->up = true;
		dev->mode = NFCSIM_MODE_TARGET;
		dev->rf_tech = param;
		break;

	case NFC_DIGITAL_CONFIG_FRAMING:
		break;

	default:
		NFCSIM_ERR(dev, "Invalid configuration type: %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int nfcsim_tg_send_cmd(struct nfc_digital_dev *ddev,
			       struct sk_buff *skb, u16 timeout,
			       nfc_digital_cmd_complete_t cb, void *arg)
{
	return nfcsim_send(ddev, skb, timeout, cb, arg);
}

static int nfcsim_tg_listen(struct nfc_digital_dev *ddev, u16 timeout,
			    nfc_digital_cmd_complete_t cb, void *arg)
{
	return nfcsim_send(ddev, NULL, timeout, cb, arg);
}

static struct nfc_digital_ops nfcsim_digital_ops = {
	.in_configure_hw = nfcsim_in_configure_hw,
	.in_send_cmd = nfcsim_in_send_cmd,

	.tg_listen = nfcsim_tg_listen,
	.tg_configure_hw = nfcsim_tg_configure_hw,
	.tg_send_cmd = nfcsim_tg_send_cmd,

	.abort_cmd = nfcsim_abort_cmd,
	.switch_rf = nfcsim_switch_rf,
};

static struct dentry *nfcsim_debugfs_root;

static void nfcsim_debugfs_init(void)
{
	nfcsim_debugfs_root = debugfs_create_dir("nfcsim", NULL);

	if (!nfcsim_debugfs_root)
		pr_err("Could not create debugfs entry\n");

}

static void nfcsim_debugfs_remove(void)
{
	debugfs_remove_recursive(nfcsim_debugfs_root);
}

static void nfcsim_debugfs_init_dev(struct nfcsim *dev)
{
	struct dentry *dev_dir;
	char devname[5]; /* nfcX\0 */
	u32 idx;
	int n;

	if (!nfcsim_debugfs_root) {
		NFCSIM_ERR(dev, "nfcsim debugfs not initialized\n");
		return;
	}

	idx = dev->nfc_digital_dev->nfc_dev->idx;
	n = snprintf(devname, sizeof(devname), "nfc%d", idx);
	if (n >= sizeof(devname)) {
		NFCSIM_ERR(dev, "Could not compute dev name for dev %d\n", idx);
		return;
	}

	dev_dir = debugfs_create_dir(devname, nfcsim_debugfs_root);
	if (!dev_dir) {
		NFCSIM_ERR(dev, "Could not create debugfs entries for nfc%d\n",
			   idx);
		return;
	}
}

static struct nfcsim *nfcsim_device_new(struct nfcsim_link *link_in,
					struct nfcsim_link *link_out)
{
	struct nfcsim *dev;
	int rc;

	dev = kzalloc(sizeof(struct nfcsim), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	INIT_DELAYED_WORK(&dev->send_work, nfcsim_send_wq);
	INIT_WORK(&dev->recv_work, nfcsim_recv_wq);

	dev->nfc_digital_dev =
			nfc_digital_allocate_device(&nfcsim_digital_ops,
						    NFC_PROTO_NFC_DEP_MASK,
						    NFCSIM_CAPABILITIES,
						    0, 0);
	if (!dev->nfc_digital_dev) {
		kfree(dev);
		return ERR_PTR(-ENOMEM);
	}

	nfc_digital_set_drvdata(dev->nfc_digital_dev, dev);

	dev->link_in = link_in;
	dev->link_out = link_out;

	rc = nfc_digital_register_device(dev->nfc_digital_dev);
	if (rc) {
		pr_err("Could not register digital device (%d)\n", rc);
		nfc_digital_free_device(dev->nfc_digital_dev);
		kfree(dev);

		return ERR_PTR(rc);
	}

	nfcsim_debugfs_init_dev(dev);

	return dev;
}

static void nfcsim_device_free(struct nfcsim *dev)
{
	nfc_digital_unregister_device(dev->nfc_digital_dev);

	dev->up = false;

	nfcsim_link_shutdown(dev->link_in);

	cancel_delayed_work_sync(&dev->send_work);
	cancel_work_sync(&dev->recv_work);

	nfc_digital_free_device(dev->nfc_digital_dev);

	kfree(dev);
}

static struct nfcsim *dev0;
static struct nfcsim *dev1;

static int __init nfcsim_init(void)
{
	struct nfcsim_link *link0, *link1;
	int rc;

	link0 = nfcsim_link_new();
	link1 = nfcsim_link_new();
	if (!link0 || !link1) {
		rc = -ENOMEM;
		goto exit_err;
	}

	nfcsim_debugfs_init();

	dev0 = nfcsim_device_new(link0, link1);
	if (IS_ERR(dev0)) {
		rc = PTR_ERR(dev0);
		goto exit_err;
	}

	dev1 = nfcsim_device_new(link1, link0);
	if (IS_ERR(dev1)) {
		nfcsim_device_free(dev0);

		rc = PTR_ERR(dev1);
		goto exit_err;
	}

	pr_info("nfcsim " NFCSIM_VERSION " initialized\n");

	return 0;

exit_err:
	pr_err("Failed to initialize nfcsim driver (%d)\n", rc);

	nfcsim_link_free(link0);
	nfcsim_link_free(link1);

	return rc;
}

static void __exit nfcsim_exit(void)
{
	struct nfcsim_link *link0, *link1;

	link0 = dev0->link_in;
	link1 = dev0->link_out;

	nfcsim_device_free(dev0);
	nfcsim_device_free(dev1);

	nfcsim_link_free(link0);
	nfcsim_link_free(link1);

	nfcsim_debugfs_remove();
}

module_init(nfcsim_init);
module_exit(nfcsim_exit);

MODULE_DESCRIPTION("NFCSim driver ver " NFCSIM_VERSION);
MODULE_VERSION(NFCSIM_VERSION);
MODULE_LICENSE("GPL");
