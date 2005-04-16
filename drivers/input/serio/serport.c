/*
 * Input device TTY line discipline
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This is a module that converts a tty line into a much simpler
 * 'serial io port' abstraction that the input device drivers use.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/tty.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input device TTY line discipline");
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_MOUSE);

#define SERPORT_BUSY	1

struct serport {
	struct tty_struct *tty;
	wait_queue_head_t wait;
	struct serio *serio;
	unsigned long flags;
};

/*
 * Callback functions from the serio code.
 */

static int serport_serio_write(struct serio *serio, unsigned char data)
{
	struct serport *serport = serio->port_data;
	return -(serport->tty->driver->write(serport->tty, &data, 1) != 1);
}

static void serport_serio_close(struct serio *serio)
{
	struct serport *serport = serio->port_data;

	serport->serio->id.type = 0;
	wake_up_interruptible(&serport->wait);
}

/*
 * serport_ldisc_open() is the routine that is called upon setting our line
 * discipline on a tty. It prepares the serio struct.
 */

static int serport_ldisc_open(struct tty_struct *tty)
{
	struct serport *serport;
	struct serio *serio;
	char name[64];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	serport = kmalloc(sizeof(struct serport), GFP_KERNEL);
	serio = kmalloc(sizeof(struct serio), GFP_KERNEL);
	if (unlikely(!serport || !serio)) {
		kfree(serport);
		kfree(serio);
		return -ENOMEM;
	}

	memset(serport, 0, sizeof(struct serport));
	serport->serio = serio;
	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	serport->tty = tty;
	tty->disc_data = serport;

	memset(serio, 0, sizeof(struct serio));
	strlcpy(serio->name, "Serial port", sizeof(serio->name));
	snprintf(serio->phys, sizeof(serio->phys), "%s/serio0", tty_name(tty, name));
	serio->id.type = SERIO_RS232;
	serio->write = serport_serio_write;
	serio->close = serport_serio_close;
	serio->port_data = serport;

	init_waitqueue_head(&serport->wait);

	return 0;
}

/*
 * serport_ldisc_close() is the opposite of serport_ldisc_open()
 */

static void serport_ldisc_close(struct tty_struct *tty)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	kfree(serport);
}

/*
 * serport_ldisc_receive() is called by the low level tty driver when characters
 * are ready for us. We forward the characters, one by one to the 'interrupt'
 * routine.
 *
 * FIXME: We should get pt_regs from the tty layer and forward them to
 *	  serio_interrupt here.
 */

static void serport_ldisc_receive(struct tty_struct *tty, const unsigned char *cp, char *fp, int count)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	int i;
	for (i = 0; i < count; i++)
		serio_interrupt(serport->serio, cp[i], 0, NULL);
}

/*
 * serport_ldisc_room() reports how much room we do have for receiving data.
 * Although we in fact have infinite room, we need to specify some value
 * here, and 256 seems to be reasonable.
 */

static int serport_ldisc_room(struct tty_struct *tty)
{
	return 256;
}

/*
 * serport_ldisc_read() just waits indefinitely if everything goes well.
 * However, when the serio driver closes the serio port, it finishes,
 * returning 0 characters.
 */

static ssize_t serport_ldisc_read(struct tty_struct * tty, struct file * file, unsigned char __user * buf, size_t nr)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	char name[64];

	if (test_and_set_bit(SERPORT_BUSY, &serport->flags))
		return -EBUSY;

	serio_register_port(serport->serio);
	printk(KERN_INFO "serio: Serial port %s\n", tty_name(tty, name));
	wait_event_interruptible(serport->wait, !serport->serio->id.type);
	serio_unregister_port(serport->serio);

	clear_bit(SERPORT_BUSY, &serport->flags);

	return 0;
}

/*
 * serport_ldisc_ioctl() allows to set the port protocol, and device ID
 */

static int serport_ldisc_ioctl(struct tty_struct * tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct serport *serport = (struct serport*) tty->disc_data;
	struct serio *serio = serport->serio;
	unsigned long type;

	if (cmd == SPIOCSTYPE) {
		if (get_user(type, (unsigned long __user *) arg))
			return -EFAULT;

		serio->id.proto	= type & 0x000000ff;
		serio->id.id	= (type & 0x0000ff00) >> 8;
		serio->id.extra	= (type & 0x00ff0000) >> 16;

		return 0;
	}

	return -EINVAL;
}

static void serport_ldisc_write_wakeup(struct tty_struct * tty)
{
	struct serport *sp = (struct serport *) tty->disc_data;

	serio_drv_write_wakeup(sp->serio);
}

/*
 * The line discipline structure.
 */

static struct tty_ldisc serport_ldisc = {
	.owner =	THIS_MODULE,
	.name =		"input",
	.open =		serport_ldisc_open,
	.close =	serport_ldisc_close,
	.read =		serport_ldisc_read,
	.ioctl =	serport_ldisc_ioctl,
	.receive_buf =	serport_ldisc_receive,
	.receive_room =	serport_ldisc_room,
	.write_wakeup =	serport_ldisc_write_wakeup
};

/*
 * The functions for insering/removing us as a module.
 */

static int __init serport_init(void)
{
	int retval;
	retval = tty_register_ldisc(N_MOUSE, &serport_ldisc);
	if (retval)
		printk(KERN_ERR "serport.c: Error registering line discipline.\n");

	return  retval;
}

static void __exit serport_exit(void)
{
	tty_register_ldisc(N_MOUSE, NULL);
}

module_init(serport_init);
module_exit(serport_exit);
