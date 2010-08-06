/*
 * adummy.c: a dummy ATM driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include <linux/atmdev.h>
#include <linux/atm.h>
#include <linux/sonet.h>

/* version definition */

#define DRV_VERSION "1.0"

#define DEV_LABEL "adummy"

#define ADUMMY_DEV(dev) ((struct adummy_dev *) (dev)->dev_data)

struct adummy_dev {
	struct atm_dev *atm_dev;

	struct list_head entry;
};

/* globals */

static LIST_HEAD(adummy_devs);

static ssize_t __set_signal(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	struct atm_dev *atm_dev = container_of(dev, struct atm_dev, class_dev);
	int signal;

	if (sscanf(buf, "%d", &signal) == 1) {

		if (signal < ATM_PHY_SIG_LOST || signal > ATM_PHY_SIG_FOUND)
			signal = ATM_PHY_SIG_UNKNOWN;

		atm_dev_signal_change(atm_dev, signal);
		return 1;
	}
	return -EINVAL;
}

static ssize_t __show_signal(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct atm_dev *atm_dev = container_of(dev, struct atm_dev, class_dev);
	return sprintf(buf, "%d\n", atm_dev->signal);
}
static DEVICE_ATTR(signal, 0644, __show_signal, __set_signal);

static struct attribute *adummy_attrs[] = {
	&dev_attr_signal.attr,
	NULL
};

static struct attribute_group adummy_group_attrs = {
	.name = NULL, /* We want them in dev's root folder */
	.attrs = adummy_attrs
};

static int __init
adummy_start(struct atm_dev *dev)
{
	dev->ci_range.vpi_bits = 4;
	dev->ci_range.vci_bits = 12;

	return 0;
}

static int
adummy_open(struct atm_vcc *vcc)
{
	short vpi = vcc->vpi;
	int vci = vcc->vci;

	if (vci == ATM_VCI_UNSPEC || vpi == ATM_VPI_UNSPEC)
		return 0;

	set_bit(ATM_VF_ADDR, &vcc->flags);
	set_bit(ATM_VF_READY, &vcc->flags);

	return 0;
}

static void
adummy_close(struct atm_vcc *vcc)
{
	clear_bit(ATM_VF_READY, &vcc->flags);
	clear_bit(ATM_VF_ADDR, &vcc->flags);
}

static int
adummy_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	if (vcc->pop)
		vcc->pop(vcc, skb);
	else
		dev_kfree_skb_any(skb);
	atomic_inc(&vcc->stats->tx);

	return 0;
}

static int
adummy_proc_read(struct atm_dev *dev, loff_t *pos, char *page)
{
	int left = *pos;

	if (!left--)
		return sprintf(page, "version %s\n", DRV_VERSION);

	return 0;
}

static struct atmdev_ops adummy_ops =
{
	.open =		adummy_open,
	.close =	adummy_close,	
	.send =		adummy_send,
	.proc_read =	adummy_proc_read,
	.owner =	THIS_MODULE
};

static int __init adummy_init(void)
{
	struct atm_dev *atm_dev;
	struct adummy_dev *adummy_dev;
	int err = 0;

	printk(KERN_ERR "adummy: version %s\n", DRV_VERSION);

	adummy_dev = kzalloc(sizeof(struct adummy_dev),
						   GFP_KERNEL);
	if (!adummy_dev) {
		printk(KERN_ERR DEV_LABEL ": kzalloc() failed\n");
		err = -ENOMEM;
		goto out;
	}
	atm_dev = atm_dev_register(DEV_LABEL, &adummy_ops, -1, NULL);
	if (!atm_dev) {
		printk(KERN_ERR DEV_LABEL ": atm_dev_register() failed\n");
		err = -ENODEV;
		goto out_kfree;
	}

	adummy_dev->atm_dev = atm_dev;
	atm_dev->dev_data = adummy_dev;

	if (sysfs_create_group(&atm_dev->class_dev.kobj, &adummy_group_attrs))
		dev_err(&atm_dev->class_dev, "Could not register attrs for adummy\n");

	if (adummy_start(atm_dev)) {
		printk(KERN_ERR DEV_LABEL ": adummy_start() failed\n");
		err = -ENODEV;
		goto out_unregister;
	}

	list_add(&adummy_dev->entry, &adummy_devs);
out:
	return err;

out_unregister:
	atm_dev_deregister(atm_dev);
out_kfree:
	kfree(adummy_dev);
	goto out;
}

static void __exit adummy_cleanup(void)
{
	struct adummy_dev *adummy_dev, *next;

	list_for_each_entry_safe(adummy_dev, next, &adummy_devs, entry) {
		atm_dev_deregister(adummy_dev->atm_dev);
		kfree(adummy_dev);
	}
}

module_init(adummy_init);
module_exit(adummy_cleanup);

MODULE_AUTHOR("chas williams <chas@cmf.nrl.navy.mil>");
MODULE_DESCRIPTION("dummy ATM driver");
MODULE_LICENSE("GPL");
