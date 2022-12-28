// SPDX-License-Identifier: GPL-2.0-only
/*
 * Designware HDMI CEC driver
 *
 * Copyright (C) 2015-2017 Russell King.
 */
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <drm/drm_edid.h>
#include <drm/bridge/dw_hdmi.h>

#include <media/cec.h>
#include <media/cec-notifier.h>

#include "dw-hdmi-cec.h"

enum {
	HDMI_IH_CEC_STAT0	= 0x0106,
	HDMI_IH_MUTE_CEC_STAT0	= 0x0186,
	HDMI_IH_MUTE		= 0x01ff,

	HDMI_CEC_CTRL		= 0x7d00,
	CEC_TRANS_MASK		= 0x7,
	CEC_CTRL_STANDBY	= BIT(4),
	CEC_CTRL_START		= BIT(0),
	CEC_CTRL_FRAME_TYP	= 3 << 1,
	CEC_CTRL_RETRY		= 0 << 1,
	CEC_CTRL_NORMAL		= 1 << 1,
	CEC_CTRL_IMMED		= 2 << 1,

	HDMI_CEC_STAT		= 0x7d01,
	CEC_STAT_DONE		= BIT(0),
	CEC_STAT_EOM		= BIT(1),
	CEC_STAT_NACK		= BIT(2),
	CEC_STAT_ARBLOST	= BIT(3),
	CEC_STAT_ERROR_INIT	= BIT(4),
	CEC_STAT_ERROR_FOLL	= BIT(5),
	CEC_STAT_WAKEUP		= BIT(6),

	HDMI_CEC_MASK		= 0x7d02,
	HDMI_CEC_POLARITY	= 0x7d03,
	HDMI_CEC_INT		= 0x7d04,
	HDMI_CEC_ADDR_L		= 0x7d05,
	HDMI_CEC_ADDR_H		= 0x7d06,
	HDMI_CEC_TX_CNT		= 0x7d07,
	HDMI_CEC_RX_CNT		= 0x7d08,
	HDMI_CEC_TX_DATA0	= 0x7d10,
	HDMI_CEC_RX_DATA0	= 0x7d20,
	HDMI_CEC_RX_DATA1	= 0x7d21,
	HDMI_CEC_LOCK		= 0x7d30,
	HDMI_CEC_WKUPCTRL	= 0x7d31,
};

struct dw_hdmi_cec {
	struct device *dev;
	struct dw_hdmi *hdmi;
	struct miscdevice misc_dev;
	const struct dw_hdmi_cec_ops *ops;
	u32 addresses;
	struct cec_adapter *adap;
	struct cec_msg rx_msg;
	unsigned int tx_status;
	bool tx_done;
	bool rx_done;
	struct cec_notifier *notify;
	struct input_dev *devinput;
	int irq;
	int wake_irq;
	bool wake_en;
	bool standby_en;
	struct mutex wake_lock;
};

static void dw_hdmi_write(struct dw_hdmi_cec *cec, u8 val, int offset)
{
	cec->ops->write(cec->hdmi, val, offset);
}

static u8 dw_hdmi_read(struct dw_hdmi_cec *cec, int offset)
{
	return cec->ops->read(cec->hdmi, offset);
}

static void dw_hdmi_mod(struct dw_hdmi_cec *cec, unsigned int offset, u8 mask, u8 val)
{
	cec->ops->mod(cec->hdmi, val, mask, offset);
}

static int dw_hdmi_cec_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		cec->addresses = 0;
	else
		cec->addresses |= BIT(logical_addr) | BIT(15);

	dw_hdmi_write(cec, cec->addresses & 255, HDMI_CEC_ADDR_L);
	dw_hdmi_write(cec, cec->addresses >> 8, HDMI_CEC_ADDR_H);

	return 0;
}

static int dw_hdmi_cec_transmit(struct cec_adapter *adap, u8 attempts,
				u32 signal_free_time, struct cec_msg *msg)
{
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);
	unsigned int i, ctrl;

	switch (signal_free_time) {
	case CEC_SIGNAL_FREE_TIME_RETRY:
		ctrl = CEC_CTRL_RETRY;
		break;
	case CEC_SIGNAL_FREE_TIME_NEW_INITIATOR:
	default:
		ctrl = CEC_CTRL_NORMAL;
		break;
	case CEC_SIGNAL_FREE_TIME_NEXT_XFER:
		ctrl = CEC_CTRL_IMMED;
		break;
	}

	for (i = 0; i < msg->len; i++)
		dw_hdmi_write(cec, msg->msg[i], HDMI_CEC_TX_DATA0 + i);

	dw_hdmi_write(cec, msg->len, HDMI_CEC_TX_CNT);
	dw_hdmi_mod(cec, HDMI_CEC_CTRL, CEC_TRANS_MASK, ctrl | CEC_CTRL_START);

	return 0;
}

static irqreturn_t dw_hdmi_cec_hardirq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);
	unsigned int stat = dw_hdmi_read(cec, HDMI_IH_CEC_STAT0);
	irqreturn_t ret = IRQ_HANDLED;

	if (stat == 0)
		return IRQ_NONE;

	dw_hdmi_write(cec, stat, HDMI_IH_CEC_STAT0);

	if (stat & CEC_STAT_ERROR_INIT) {
		cec->tx_status = CEC_TX_STATUS_ERROR;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_DONE) {
		cec->tx_status = CEC_TX_STATUS_OK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_NACK) {
		cec->tx_status = CEC_TX_STATUS_NACK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	}

	if (stat & CEC_STAT_EOM) {
		unsigned int len, i;

		len = dw_hdmi_read(cec, HDMI_CEC_RX_CNT);
		if (len > sizeof(cec->rx_msg.msg))
			len = sizeof(cec->rx_msg.msg);

		for (i = 0; i < len; i++)
			cec->rx_msg.msg[i] =
				dw_hdmi_read(cec, HDMI_CEC_RX_DATA0 + i);

		dw_hdmi_write(cec, 0, HDMI_CEC_LOCK);

		cec->rx_msg.len = len;
		smp_wmb();
		cec->rx_done = true;

		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t dw_hdmi_cec_thread(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);

	if (cec->tx_done) {
		cec->tx_done = false;
		cec_transmit_attempt_done(adap, cec->tx_status);
	}
	if (cec->rx_done) {
		cec->rx_done = false;
		smp_rmb();
		cec_received_msg(adap, &cec->rx_msg);
	}
	return IRQ_HANDLED;
}

static int dw_hdmi_cec_enable(struct cec_adapter *adap, bool enable)
{
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);

	if (!enable) {
		dw_hdmi_write(cec, 0, HDMI_CEC_POLARITY);

		if (cec->wake_en && cec->standby_en) {
			dw_hdmi_write(cec, 0xff, HDMI_IH_CEC_STAT0);
			dw_hdmi_mod(cec, HDMI_CEC_CTRL, CEC_CTRL_STANDBY, CEC_CTRL_STANDBY);
			dw_hdmi_write(cec, 0, HDMI_CEC_LOCK);
			dw_hdmi_write(cec, 0xff, HDMI_CEC_WKUPCTRL);
			dw_hdmi_write(cec, ~(1 << 6), HDMI_CEC_MASK);
			dw_hdmi_write(cec, ~(1 << 6), HDMI_IH_MUTE_CEC_STAT0);
			dw_hdmi_write(cec, 0x01, HDMI_IH_MUTE);
		} else {
			cec->ops->disable(cec->hdmi);
		}
	} else {
		unsigned int irqs;

		dw_hdmi_cec_log_addr(cec->adap, CEC_LOG_ADDR_INVALID);
		dw_hdmi_mod(cec, HDMI_CEC_CTRL, CEC_CTRL_STANDBY, 0);
		dw_hdmi_write(cec, 0x02, HDMI_IH_MUTE);
		dw_hdmi_write(cec, ~0, HDMI_IH_CEC_STAT0);
		dw_hdmi_write(cec, 0, HDMI_CEC_LOCK);

		cec->ops->enable(cec->hdmi);

		irqs = CEC_STAT_ERROR_INIT | CEC_STAT_NACK | CEC_STAT_EOM |
		       CEC_STAT_DONE;
		dw_hdmi_write(cec, irqs, HDMI_CEC_POLARITY);
		dw_hdmi_write(cec, ~irqs, HDMI_CEC_MASK);
		dw_hdmi_write(cec, ~irqs, HDMI_IH_MUTE_CEC_STAT0);
	}
	return 0;
}

static const struct cec_adap_ops dw_hdmi_cec_ops = {
	.adap_enable = dw_hdmi_cec_enable,
	.adap_log_addr = dw_hdmi_cec_log_addr,
	.adap_transmit = dw_hdmi_cec_transmit,
};

static void dw_hdmi_cec_del(void *data)
{
	struct dw_hdmi_cec *cec = data;

	cec_delete_adapter(cec->adap);
}

static irqreturn_t dw_hdmi_cec_wake_irq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);
	u8 cec_int;

	cec_int = dw_hdmi_read(cec, HDMI_IH_CEC_STAT0);
	if (!cec_int)
		return IRQ_NONE;

	dw_hdmi_write(cec, 0x02, HDMI_IH_MUTE);
	dw_hdmi_write(cec, cec_int, HDMI_IH_CEC_STAT0);
	dw_hdmi_write(cec, 0x00, HDMI_CEC_WKUPCTRL);

	if (!cec->wake_en)
		return IRQ_HANDLED;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t dw_hdmi_cec_wake_thread(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);

	mutex_lock(&cec->wake_lock);

	if (!cec->standby_en) {
		mutex_unlock(&cec->wake_lock);
		return IRQ_HANDLED;
	}
	cec->standby_en = false;

	dev_dbg(cec->dev, "wakeup opcode:0x%x\n", dw_hdmi_read(cec, HDMI_CEC_RX_DATA1));
	input_event(cec->devinput, EV_KEY, KEY_POWER, 1);
	input_sync(cec->devinput);
	input_event(cec->devinput, EV_KEY, KEY_POWER, 0);
	input_sync(cec->devinput);
	mutex_unlock(&cec->wake_lock);

	return IRQ_HANDLED;
}

static int rockchip_hdmi_cec_input_init(struct dw_hdmi_cec *cec)
{
	int err;

	cec->devinput = devm_input_allocate_device(cec->dev);
	if (!cec->devinput)
		return -EPERM;

	cec->devinput->name = "hdmi_cec_key";
	cec->devinput->phys = "hdmi_cec_key/input0";
	cec->devinput->id.bustype = BUS_HOST;
	cec->devinput->id.vendor = 0x0001;
	cec->devinput->id.product = 0x0001;
	cec->devinput->id.version = 0x0100;

	err = input_register_device(cec->devinput);
	if (err < 0) {
		input_free_device(cec->devinput);
		return err;
	}
	input_set_capability(cec->devinput, EV_KEY, KEY_POWER);

	return 0;
}

static long cec_standby(struct cec_adapter *adap, __u8 __user *parg)
{
	u8 en;
	int ret;
	struct dw_hdmi_cec *cec = cec_get_drvdata(adap);

	mutex_lock(&cec->wake_lock);
	if (copy_from_user(&en, parg, sizeof(en))) {
		mutex_unlock(&cec->wake_lock);
		return -EFAULT;
	}

	cec->standby_en = !en;
	ret = adap->ops->adap_enable(adap, en);
	mutex_unlock(&cec->wake_lock);

	return ret;
}

static long cec_func_en(struct dw_hdmi_cec *cec, int __user *parg)
{
	int en_mask;

	if (copy_from_user(&en_mask, parg, sizeof(en_mask)))
		return -EFAULT;

	cec->wake_en = (en_mask & CEC_EN) && (en_mask & CEC_WAKE);

	return 0;
}

static long dw_hdmi_cec_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct dw_hdmi_cec *cec;
	struct miscdevice *misc_dev;
	void __user *data;

	if (!f)
		return -EFAULT;

	misc_dev = f->private_data;
	cec = container_of(misc_dev, struct dw_hdmi_cec, misc_dev);
	data = (void __user *)arg;

	switch (cmd) {
	case CEC_STANDBY:
		return cec_standby(cec->adap, data);
	case CEC_FUNC_EN:
		return cec_func_en(cec, data);
	default:
		return -EINVAL;
	}

	return -ENOTTY;
}

static int dw_hdmi_cec_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int dw_hdmi_cec_release(struct inode *inode, struct file *f)
{
	return 0;
}

static const struct file_operations dw_hdmi_cec_file_operations = {
	.compat_ioctl = dw_hdmi_cec_ioctl,
	.unlocked_ioctl = dw_hdmi_cec_ioctl,
	.open = dw_hdmi_cec_open,
	.release = dw_hdmi_cec_release,
	.owner = THIS_MODULE,
};

static void dw_hdmi_cec_hpd_wake_up(struct platform_device *pdev)
{
	struct dw_hdmi_cec *cec = platform_get_drvdata(pdev);

	mutex_lock(&cec->wake_lock);

	if (!cec->standby_en) {
		mutex_unlock(&cec->wake_lock);
		return;
	}
	cec->standby_en = false;

	dw_hdmi_write(cec, 0x02, HDMI_IH_MUTE);

	input_event(cec->devinput, EV_KEY, KEY_POWER, 1);
	input_sync(cec->devinput);
	input_event(cec->devinput, EV_KEY, KEY_POWER, 0);
	input_sync(cec->devinput);
	mutex_unlock(&cec->wake_lock);
}

static const struct dw_hdmi_cec_wake_ops cec_ops = {
	.hpd_wake_up = dw_hdmi_cec_hpd_wake_up,
};

static int dw_hdmi_cec_probe(struct platform_device *pdev)
{
	struct dw_hdmi_cec_data *data = dev_get_platdata(&pdev->dev);
	struct dw_hdmi_cec *cec;
	int ret;

	if (!data)
		return -ENXIO;

	/*
	 * Our device is just a convenience - we want to link to the real
	 * hardware device here, so that userspace can see the association
	 * between the HDMI hardware and its associated CEC chardev.
	 */
	cec = devm_kzalloc(&pdev->dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	cec->dev = &pdev->dev;
	cec->irq = data->irq;
	cec->wake_irq = data->wake_irq;
	cec->ops = data->ops;
	cec->hdmi = data->hdmi;

	mutex_init(&cec->wake_lock);

	platform_set_drvdata(pdev, cec);

	dw_hdmi_write(cec, 0, HDMI_CEC_TX_CNT);
	dw_hdmi_write(cec, ~0, HDMI_CEC_MASK);
	dw_hdmi_write(cec, ~0, HDMI_IH_MUTE_CEC_STAT0);
	dw_hdmi_write(cec, 0, HDMI_CEC_POLARITY);

	cec->adap = cec_allocate_adapter(&dw_hdmi_cec_ops, cec, "dw_hdmi",
					 CEC_CAP_DEFAULTS |
					 CEC_CAP_CONNECTOR_INFO,
					 CEC_MAX_LOG_ADDRS);
	if (IS_ERR(cec->adap))
		return PTR_ERR(cec->adap);

	dw_hdmi_set_cec_adap(cec->hdmi, cec->adap);

	/* override the module pointer */
	cec->adap->owner = THIS_MODULE;

	ret = devm_add_action(&pdev->dev, dw_hdmi_cec_del, cec);
	if (ret) {
		cec_delete_adapter(cec->adap);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, cec->irq,
					dw_hdmi_cec_hardirq,
					dw_hdmi_cec_thread, IRQF_SHARED | IRQF_ONESHOT,
					"dw-hdmi-cec", cec->adap);
	if (ret < 0)
		return ret;

	if (cec->wake_irq > 0) {
		ret = devm_request_threaded_irq(&pdev->dev, cec->wake_irq,
						dw_hdmi_cec_wake_irq,
						dw_hdmi_cec_wake_thread,
						IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
						"cec-wakeup", cec->adap);
		if (ret) {
			dev_err(&pdev->dev,
				"hdmi_cec request_irq failed (%d).\n",
				ret);
			return ret;
		}
		device_init_wakeup(&pdev->dev, 1);
		enable_irq_wake(cec->wake_irq);
	}

	cec->notify = cec_notifier_cec_adap_register(pdev->dev.parent,
						     NULL, cec->adap);
	if (!cec->notify)
		return -ENOMEM;

	ret = cec_register_adapter(cec->adap, pdev->dev.parent);
	if (ret < 0) {
		cec_notifier_cec_adap_unregister(cec->notify, cec->adap);
		return ret;
	}

	/*
	 * CEC documentation says we must not call cec_delete_adapter
	 * after a successful call to cec_register_adapter().
	 */
	devm_remove_action(&pdev->dev, dw_hdmi_cec_del, cec);

	rockchip_hdmi_cec_input_init(cec);

	cec->misc_dev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "rk_cec");
	if (!cec->misc_dev.name)
		return -ENOMEM;
	cec->misc_dev.minor = MISC_DYNAMIC_MINOR;
	cec->misc_dev.fops = &dw_hdmi_cec_file_operations;
	cec->misc_dev.mode = 0666;

	ret = misc_register(&cec->misc_dev);

	dw_hdmi_cec_wake_ops_register(cec->hdmi, &cec_ops);

	return ret;
}

static int dw_hdmi_cec_remove(struct platform_device *pdev)
{
	struct dw_hdmi_cec *cec = platform_get_drvdata(pdev);

	cec_notifier_cec_adap_unregister(cec->notify, cec->adap);
	cec_unregister_adapter(cec->adap);
	misc_deregister(&cec->misc_dev);

	return 0;
}

static struct platform_driver dw_hdmi_cec_driver = {
	.probe	= dw_hdmi_cec_probe,
	.remove	= dw_hdmi_cec_remove,
	.driver = {
		.name = "dw-hdmi-cec",
	},
};
module_platform_driver(dw_hdmi_cec_driver);

MODULE_AUTHOR("Russell King <rmk+kernel@armlinux.org.uk>");
MODULE_DESCRIPTION("Synopsys Designware HDMI CEC driver for i.MX");
MODULE_LICENSE("GPL");
MODULE_ALIAS(PLATFORM_MODULE_PREFIX "dw-hdmi-cec");
