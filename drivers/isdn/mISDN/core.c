// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mISDNif.h>
#include "core.h"

static u_int debug;

MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL");
module_param(debug, uint, S_IRUGO | S_IWUSR);

static u64		device_ids;
#define MAX_DEVICE_ID	63

static LIST_HEAD(Bprotocols);
static DEFINE_RWLOCK(bp_lock);

static void mISDN_dev_release(struct device *dev)
{
	/* nothing to do: the device is part of its parent's data structure */
}

static ssize_t id_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return -ENODEV;
	return sprintf(buf, "%d\n", mdev->id);
}
static DEVICE_ATTR_RO(id);

static ssize_t nrbchan_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return -ENODEV;
	return sprintf(buf, "%d\n", mdev->nrbchan);
}
static DEVICE_ATTR_RO(nrbchan);

static ssize_t d_protocols_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return -ENODEV;
	return sprintf(buf, "%d\n", mdev->Dprotocols);
}
static DEVICE_ATTR_RO(d_protocols);

static ssize_t b_protocols_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return -ENODEV;
	return sprintf(buf, "%d\n", mdev->Bprotocols | get_all_Bprotocols());
}
static DEVICE_ATTR_RO(b_protocols);

static ssize_t protocol_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return -ENODEV;
	return sprintf(buf, "%d\n", mdev->D.protocol);
}
static DEVICE_ATTR_RO(protocol);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	strcpy(buf, dev_name(dev));
	return strlen(buf);
}
static DEVICE_ATTR_RO(name);

#if 0 /* hangs */
static ssize_t name_set(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int err = 0;
	char *out = kmalloc(count + 1, GFP_KERNEL);

	if (!out)
		return -ENOMEM;

	memcpy(out, buf, count);
	if (count && out[count - 1] == '\n')
		out[--count] = 0;
	if (count)
		err = device_rename(dev, out);
	kfree(out);

	return (err < 0) ? err : count;
}
static DEVICE_ATTR_RW(name);
#endif

static ssize_t channelmap_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);
	char *bp = buf;
	int i;

	for (i = 0; i <= mdev->nrbchan; i++)
		*bp++ = test_channelmap(i, mdev->channelmap) ? '1' : '0';

	return bp - buf;
}
static DEVICE_ATTR_RO(channelmap);

static struct attribute *mISDN_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_d_protocols.attr,
	&dev_attr_b_protocols.attr,
	&dev_attr_protocol.attr,
	&dev_attr_channelmap.attr,
	&dev_attr_nrbchan.attr,
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mISDN);

static int mISDN_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return 0;

	if (add_uevent_var(env, "nchans=%d", mdev->nrbchan))
		return -ENOMEM;

	return 0;
}

static void mISDN_class_release(struct class *cls)
{
	/* do nothing, it's static */
}

static struct class mISDN_class = {
	.name = "mISDN",
	.owner = THIS_MODULE,
	.dev_uevent = mISDN_uevent,
	.dev_groups = mISDN_groups,
	.dev_release = mISDN_dev_release,
	.class_release = mISDN_class_release,
};

static int
_get_mdevice(struct device *dev, const void *id)
{
	struct mISDNdevice *mdev = dev_to_mISDN(dev);

	if (!mdev)
		return 0;
	if (mdev->id != *(const u_int *)id)
		return 0;
	return 1;
}

struct mISDNdevice
*get_mdevice(u_int id)
{
	return dev_to_mISDN(class_find_device(&mISDN_class, NULL, &id,
					      _get_mdevice));
}

static int
_get_mdevice_count(struct device *dev, void *cnt)
{
	*(int *)cnt += 1;
	return 0;
}

int
get_mdevice_count(void)
{
	int cnt = 0;

	class_for_each_device(&mISDN_class, NULL, &cnt, _get_mdevice_count);
	return cnt;
}

static int
get_free_devid(void)
{
	u_int	i;

	for (i = 0; i <= MAX_DEVICE_ID; i++)
		if (!test_and_set_bit(i, (u_long *)&device_ids))
			break;
	if (i > MAX_DEVICE_ID)
		return -EBUSY;
	return i;
}

int
mISDN_register_device(struct mISDNdevice *dev,
		      struct device *parent, char *name)
{
	int	err;

	err = get_free_devid();
	if (err < 0)
		return err;
	dev->id = err;

	device_initialize(&dev->dev);
	if (name && name[0])
		dev_set_name(&dev->dev, "%s", name);
	else
		dev_set_name(&dev->dev, "mISDN%d", dev->id);
	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_register %s %d\n",
		       dev_name(&dev->dev), dev->id);
	dev->dev.class = &mISDN_class;

	err = create_stack(dev);
	if (err)
		goto error1;

	dev->dev.platform_data = dev;
	dev->dev.parent = parent;
	dev_set_drvdata(&dev->dev, dev);

	err = device_add(&dev->dev);
	if (err)
		goto error3;
	return 0;

error3:
	delete_stack(dev);
error1:
	put_device(&dev->dev);
	return err;

}
EXPORT_SYMBOL(mISDN_register_device);

void
mISDN_unregister_device(struct mISDNdevice *dev) {
	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_unregister %s %d\n",
		       dev_name(&dev->dev), dev->id);
	/* sysfs_remove_link(&dev->dev.kobj, "device"); */
	device_del(&dev->dev);
	dev_set_drvdata(&dev->dev, NULL);

	test_and_clear_bit(dev->id, (u_long *)&device_ids);
	delete_stack(dev);
	put_device(&dev->dev);
}
EXPORT_SYMBOL(mISDN_unregister_device);

u_int
get_all_Bprotocols(void)
{
	struct Bprotocol	*bp;
	u_int	m = 0;

	read_lock(&bp_lock);
	list_for_each_entry(bp, &Bprotocols, list)
		m |= bp->Bprotocols;
	read_unlock(&bp_lock);
	return m;
}

struct Bprotocol *
get_Bprotocol4mask(u_int m)
{
	struct Bprotocol	*bp;

	read_lock(&bp_lock);
	list_for_each_entry(bp, &Bprotocols, list)
		if (bp->Bprotocols & m) {
			read_unlock(&bp_lock);
			return bp;
		}
	read_unlock(&bp_lock);
	return NULL;
}

struct Bprotocol *
get_Bprotocol4id(u_int id)
{
	u_int	m;

	if (id < ISDN_P_B_START || id > 63) {
		printk(KERN_WARNING "%s id not in range  %d\n",
		       __func__, id);
		return NULL;
	}
	m = 1 << (id & ISDN_P_B_MASK);
	return get_Bprotocol4mask(m);
}

int
mISDN_register_Bprotocol(struct Bprotocol *bp)
{
	u_long			flags;
	struct Bprotocol	*old;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "%s: %s/%x\n", __func__,
		       bp->name, bp->Bprotocols);
	old = get_Bprotocol4mask(bp->Bprotocols);
	if (old) {
		printk(KERN_WARNING
		       "register duplicate protocol old %s/%x new %s/%x\n",
		       old->name, old->Bprotocols, bp->name, bp->Bprotocols);
		return -EBUSY;
	}
	write_lock_irqsave(&bp_lock, flags);
	list_add_tail(&bp->list, &Bprotocols);
	write_unlock_irqrestore(&bp_lock, flags);
	return 0;
}
EXPORT_SYMBOL(mISDN_register_Bprotocol);

void
mISDN_unregister_Bprotocol(struct Bprotocol *bp)
{
	u_long	flags;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "%s: %s/%x\n", __func__, bp->name,
		       bp->Bprotocols);
	write_lock_irqsave(&bp_lock, flags);
	list_del(&bp->list);
	write_unlock_irqrestore(&bp_lock, flags);
}
EXPORT_SYMBOL(mISDN_unregister_Bprotocol);

static const char *msg_no_channel = "<no channel>";
static const char *msg_no_stack = "<no stack>";
static const char *msg_no_stackdev = "<no stack device>";

const char *mISDNDevName4ch(struct mISDNchannel *ch)
{
	if (!ch)
		return msg_no_channel;
	if (!ch->st)
		return msg_no_stack;
	if (!ch->st->dev)
		return msg_no_stackdev;
	return dev_name(&ch->st->dev->dev);
};
EXPORT_SYMBOL(mISDNDevName4ch);

static int
mISDNInit(void)
{
	int	err;

	printk(KERN_INFO "Modular ISDN core version %d.%d.%d\n",
	       MISDN_MAJOR_VERSION, MISDN_MINOR_VERSION, MISDN_RELEASE);
	mISDN_init_clock(&debug);
	mISDN_initstack(&debug);
	err = class_register(&mISDN_class);
	if (err)
		goto error1;
	err = mISDN_inittimer(&debug);
	if (err)
		goto error2;
	err = Isdnl1_Init(&debug);
	if (err)
		goto error3;
	err = Isdnl2_Init(&debug);
	if (err)
		goto error4;
	err = misdn_sock_init(&debug);
	if (err)
		goto error5;
	return 0;

error5:
	Isdnl2_cleanup();
error4:
	Isdnl1_cleanup();
error3:
	mISDN_timer_cleanup();
error2:
	class_unregister(&mISDN_class);
error1:
	return err;
}

static void mISDN_cleanup(void)
{
	misdn_sock_cleanup();
	Isdnl2_cleanup();
	Isdnl1_cleanup();
	mISDN_timer_cleanup();
	class_unregister(&mISDN_class);

	printk(KERN_DEBUG "mISDNcore unloaded\n");
}

module_init(mISDNInit);
module_exit(mISDN_cleanup);
