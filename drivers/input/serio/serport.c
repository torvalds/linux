// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input device TTY line discipline
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This is a module that converts a tty line into a much simpler
 * 'serial io port' abstraction that the input device drivers use.
 */


#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/tty.h>
#include <linux/compat.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input device TTY line discipline");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_MOUSE);

#define SERPORT_BUSY	1
#define SERPORT_ACTIVE	2
#define SERPORT_DEAD	3

struct serport {
	struct tty_struct *tty;
	wait_queue_head_t wait;
	struct serio *serio;
	struct serio_device_id id;
	spinlock_t lock;
	unsigned long flags;
};

/*
 * Callback functions from the serio code.
 */

static int serport_serio_write(struct serio *serio, unsigned char data)
{
	struct serport *serport = serio->port_data;
	return -(serport->tty->ops->write(serport->tty, &data, 1) != 1);
}

static int serport_serio_open(struct serio *serio)
{
	struct serport *serport = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&serport->lock, flags);
	set_bit(SERPORT_ACTIVE, &serport->flags);
	spin_unlock_irqrestore(&serport->lock, flags);

	return 0;
}


static void serport_serio_close(struct serio *serio)
{
	struct serport *serport = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&serport->lock, flags);
	clear_bit(SERPORT_ACTIVE, &serport->flags);
	spin_unlock_irqrestore(&serport->lock, flags);
}

/*
 * serport_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty. It prepares the serio struct.
 */

static int serport_ldisc_open(struct tty_struct *tty)
{
	struct serport *serport;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	serport = kzalloc(sizeof(struct serport), GFP_KERNEL);
	if (!serport)
		return -ENOMEM;

	serport->tty = tty;
	spin_lock_init(&serport->lock);
	init_waitqueue_head(&serport->wait);

	tty->disc_data = serport;
	tty->receive_room = 256;
	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	return 0;
}

/*
 * serport_ldisc_close() is the opposite of serport_ldisc_open()
 */

static void serport_ldisc_close(struct tty_struct *tty)
{
	struct serport *serport = (struct serport *) tty->disc_data;

	kfree(serport);
}

/*
 * serport_ldisc_receive() is called by the low level tty driver when characters
 * are ready for us. We forward the characters and flags, one by one to the
 * 'interrupt' routine.
 */

static void serport_ldisc_receive(struct tty_struct *tty,
		const unsigned char *cp, const char *fp, int count)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	unsigned long flags;
	unsigned int ch_flags = 0;
	int i;

	spin_lock_irqsave(&serport->lock, flags);

	if (!test_bit(SERPORT_ACTIVE, &serport->flags))
		goto out;

	for (i = 0; i < count; i++) {
		if (fp) {
			switch (fp[i]) {
			case TTY_FRAME:
				ch_flags = SERIO_FRAME;
				break;

			case TTY_PARITY:
				ch_flags = SERIO_PARITY;
				break;

			default:
				ch_flags = 0;
				break;
			}
		}

		serio_interrupt(serport->serio, cp[i], ch_flags);
	}

out:
	spin_unlock_irqrestore(&serport->lock, flags);
}

/*
 * serport_ldisc_read() just waits indefinitely if everything goes well.
 * However, when the serio driver closes the serio port, it finishes,
 * returning 0 characters.
 */

static ssize_t serport_ldisc_read(struct tty_struct * tty, struct file * file,
				  unsigned char *kbuf, size_t nr,
				  void **cookie, unsigned long offset)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	struct serio *serio;

	if (test_and_set_bit(SERPORT_BUSY, &serport->flags))
		return -EBUSY;

	serport->serio = serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	strlcpy(serio->name, "Serial port", sizeof(serio->name));
	snprintf(serio->phys, sizeof(serio->phys), "%s/serio0", tty_name(tty));
	serio->id = serport->id;
	serio->id.type = SERIO_RS232;
	serio->write = serport_serio_write;
	serio->open = serport_serio_open;
	serio->close = serport_serio_close;
	serio->port_data = serport;
	serio->dev.parent = tty->dev;

	serio_register_port(serport->serio);
	printk(KERN_INFO "serio: Serial port %s\n", tty_name(tty));

	wait_event_interruptible(serport->wait, test_bit(SERPORT_DEAD, &serport->flags));
	serio_unregister_port(serport->serio);
	serport->serio = NULL;

	clear_bit(SERPORT_DEAD, &serport->flags);
	clear_bit(SERPORT_BUSY, &serport->flags);

	return 0;
}

static void serport_set_type(struct tty_struct *tty, unsigned long type)
{
	struct serport *serport = tty->disc_data;

	serport->id.proto = type & 0x000000ff;
	serport->id.id    = (type & 0x0000ff00) >> 8;
	serport->id.extra = (type & 0x00ff0000) >> 16;
}

/*
 * serport_ldisc_ioctl() allows to set the port protocol, and device ID
 */

static int serport_ldisc_ioctl(struct tty_struct *tty, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	if (cmd == SPIOCSTYPE) {
		unsigned long type;

		if (get_user(type, (unsigned long __user *) arg))
			return -EFAULT;

		serport_set_type(tty, type);
		return 0;
	}

	return -EINVAL;
}

#ifdef CONFIG_COMPAT
#define COMPAT_SPIOCSTYPE	_IOW('q', 0x01, compat_ulong_t)
static int serport_ldisc_compat_ioctl(struct tty_struct *tty,
				       struct file *file,
				       unsigned int cmd, unsigned long arg)
{
	if (cmd == COMPAT_SPIOCSTYPE) {
		void __user *uarg = compat_ptr(arg);
		compat_ulong_t compat_type;

		if (get_user(compat_type, (compat_ulong_t __user *)uarg))
			return -EFAULT;

		serport_set_type(tty, compat_type);
		return 0;
	}

	return -EINVAL;
}
#endif

static int serport_ldisc_hangup(struct tty_struct *tty)
{
	struct serport *serport = (struct serport *) tty->disc_data;
	unsigned long flags;

	spin_lock_irqsave(&serport->lock, flags);
	set_bit(SERPORT_DEAD, &serport->flags);
	spin_unlock_irqrestore(&serport->lock, flags);

	wake_up_interruptible(&serport->wait);
	return 0;
}

static void serport_ldisc_write_wakeup(struct tty_struct * tty)
{
	struct serport *serport = (struct serport *) tty->disc_data;
	unsigned long flags;

	spin_lock_irqsave(&serport->lock, flags);
	if (test_bit(SERPORT_ACTIVE, &serport->flags))
		serio_drv_write_wakeup(serport->serio);
	spin_unlock_irqrestore(&serport->lock, flags);
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc_ops serport_ldisc = {
	.owner =	THIS_MODULE,
	.num =		N_MOUSE,
	.name =		"input",
	.open =		serport_ldisc_open,
	.close =	serport_ldisc_close,
	.read =		serport_ldisc_read,
	.ioctl =	serport_ldisc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	serport_ldisc_compat_ioctl,
#endif
	.receive_buf =	serport_ldisc_receive,
	.hangup =	serport_ldisc_hangup,
	.write_wakeup =	serport_ldisc_write_wakeup
};

/*
 * The functions for insering/removing us as a module.
 */

static int __init serport_init(void)
{
	int retval;
	retval = tty_register_ldisc(&serport_ldisc);
	if (retval)
		printk(KERN_ERR "serport.c: Error registering line discipline.\n");

	return  retval;
}

static void __exit serport_exit(void)
{
	tty_unregister_ldisc(&serport_ldisc);
}

module_init(serport_init);
module_exit(serport_exit);
