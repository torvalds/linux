/* Copyright (c) 2007 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoechr.c
 * AoE character device driver
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include "aoe.h"

enum {
	//MINOR_STAT = 1, (moved to sysfs)
	MINOR_ERR = 2,
	MINOR_DISCOVER,
	MINOR_INTERFACES,
	MINOR_REVALIDATE,
	MINOR_FLUSH,
	MSGSZ = 2048,
	NMSG = 100,		/* message backlog to retain */
};

struct aoe_chardev {
	ulong minor;
	char name[32];
};

enum { EMFL_VALID = 1 };

struct ErrMsg {
	short flags;
	short len;
	char *msg;
};

static struct ErrMsg emsgs[NMSG];
static int emsgs_head_idx, emsgs_tail_idx;
static struct semaphore emsgs_sema;
static spinlock_t emsgs_lock;
static int nblocked_emsgs_readers;
static struct class *aoe_class;
static struct aoe_chardev chardevs[] = {
	{ MINOR_ERR, "err" },
	{ MINOR_DISCOVER, "discover" },
	{ MINOR_INTERFACES, "interfaces" },
	{ MINOR_REVALIDATE, "revalidate" },
	{ MINOR_FLUSH, "flush" },
};

static int
discover(void)
{
	aoecmd_cfg(0xffff, 0xff);
	return 0;
}

static int
interfaces(const char __user *str, size_t size)
{
	if (set_aoe_iflist(str, size)) {
		printk(KERN_ERR
			"aoe: could not set interface list: too many interfaces\n");
		return -EINVAL;
	}
	return 0;
}

static int
revalidate(const char __user *str, size_t size)
{
	int major, minor, n;
	ulong flags;
	struct aoedev *d;
	struct sk_buff *skb;
	char buf[16];

	if (size >= sizeof buf)
		return -EINVAL;
	buf[sizeof buf - 1] = '\0';
	if (copy_from_user(buf, str, size))
		return -EFAULT;

	/* should be e%d.%d format */
	n = sscanf(buf, "e%d.%d", &major, &minor);
	if (n != 2) {
		printk(KERN_ERR "aoe: invalid device specification\n");
		return -EINVAL;
	}
	d = aoedev_by_aoeaddr(major, minor);
	if (!d)
		return -EINVAL;
	spin_lock_irqsave(&d->lock, flags);
	aoecmd_cleanslate(d);
loop:
	skb = aoecmd_ata_id(d);
	spin_unlock_irqrestore(&d->lock, flags);
	/* try again if we are able to sleep a bit,
	 * otherwise give up this revalidation
	 */
	if (!skb && !msleep_interruptible(200)) {
		spin_lock_irqsave(&d->lock, flags);
		goto loop;
	}
	aoenet_xmit(skb);
	aoecmd_cfg(major, minor);
	return 0;
}

void
aoechr_error(char *msg)
{
	struct ErrMsg *em;
	char *mp;
	ulong flags, n;

	n = strlen(msg);

	spin_lock_irqsave(&emsgs_lock, flags);

	em = emsgs + emsgs_tail_idx;
	if ((em->flags & EMFL_VALID)) {
bail:		spin_unlock_irqrestore(&emsgs_lock, flags);
		return;
	}

	mp = kmalloc(n, GFP_ATOMIC);
	if (mp == NULL) {
		printk(KERN_ERR "aoe: allocation failure, len=%ld\n", n);
		goto bail;
	}

	memcpy(mp, msg, n);
	em->msg = mp;
	em->flags |= EMFL_VALID;
	em->len = n;

	emsgs_tail_idx++;
	emsgs_tail_idx %= ARRAY_SIZE(emsgs);

	spin_unlock_irqrestore(&emsgs_lock, flags);

	if (nblocked_emsgs_readers)
		up(&emsgs_sema);
}

static ssize_t
aoechr_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offp)
{
	int ret = -EINVAL;

	switch ((unsigned long) filp->private_data) {
	default:
		printk(KERN_INFO "aoe: can't write to that file.\n");
		break;
	case MINOR_DISCOVER:
		ret = discover();
		break;
	case MINOR_INTERFACES:
		ret = interfaces(buf, cnt);
		break;
	case MINOR_REVALIDATE:
		ret = revalidate(buf, cnt);
		break;
	case MINOR_FLUSH:
		ret = aoedev_flush(buf, cnt);
	}
	if (ret == 0)
		ret = cnt;
	return ret;
}

static int
aoechr_open(struct inode *inode, struct file *filp)
{
	int n, i;

	n = iminor(inode);
	filp->private_data = (void *) (unsigned long) n;

	for (i = 0; i < ARRAY_SIZE(chardevs); ++i)
		if (chardevs[i].minor == n)
			return 0;
	return -EINVAL;
}

static int
aoechr_rel(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t
aoechr_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	unsigned long n;
	char *mp;
	struct ErrMsg *em;
	ssize_t len;
	ulong flags;

	n = (unsigned long) filp->private_data;
	if (n != MINOR_ERR)
		return -EFAULT;

	spin_lock_irqsave(&emsgs_lock, flags);

	for (;;) {
		em = emsgs + emsgs_head_idx;
		if ((em->flags & EMFL_VALID) != 0)
			break;
		if (filp->f_flags & O_NDELAY) {
			spin_unlock_irqrestore(&emsgs_lock, flags);
			return -EAGAIN;
		}
		nblocked_emsgs_readers++;

		spin_unlock_irqrestore(&emsgs_lock, flags);

		n = down_interruptible(&emsgs_sema);

		spin_lock_irqsave(&emsgs_lock, flags);

		nblocked_emsgs_readers--;

		if (n) {
			spin_unlock_irqrestore(&emsgs_lock, flags);
			return -ERESTARTSYS;
		}
	}
	if (em->len > cnt) {
		spin_unlock_irqrestore(&emsgs_lock, flags);
		return -EAGAIN;
	}
	mp = em->msg;
	len = em->len;
	em->msg = NULL;
	em->flags &= ~EMFL_VALID;

	emsgs_head_idx++;
	emsgs_head_idx %= ARRAY_SIZE(emsgs);

	spin_unlock_irqrestore(&emsgs_lock, flags);

	n = copy_to_user(buf, mp, len);
	kfree(mp);
	return n == 0 ? len : -EFAULT;
}

static const struct file_operations aoe_fops = {
	.write = aoechr_write,
	.read = aoechr_read,
	.open = aoechr_open,
	.release = aoechr_rel,
	.owner = THIS_MODULE,
};

int __init
aoechr_init(void)
{
	int n, i;

	n = register_chrdev(AOE_MAJOR, "aoechr", &aoe_fops);
	if (n < 0) { 
		printk(KERN_ERR "aoe: can't register char device\n");
		return n;
	}
	sema_init(&emsgs_sema, 0);
	spin_lock_init(&emsgs_lock);
	aoe_class = class_create(THIS_MODULE, "aoe");
	if (IS_ERR(aoe_class)) {
		unregister_chrdev(AOE_MAJOR, "aoechr");
		return PTR_ERR(aoe_class);
	}
	for (i = 0; i < ARRAY_SIZE(chardevs); ++i)
		device_create(aoe_class, NULL,
			      MKDEV(AOE_MAJOR, chardevs[i].minor), chardevs[i].name);

	return 0;
}

void
aoechr_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(chardevs); ++i)
		device_destroy(aoe_class, MKDEV(AOE_MAJOR, chardevs[i].minor));
	class_destroy(aoe_class);
	unregister_chrdev(AOE_MAJOR, "aoechr");
}

