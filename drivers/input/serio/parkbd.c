/*
 *  Parallel port to Keyboard port adapter driver for Linux
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * To connect an AT or XT keyboard to the parallel port, a fairly simple adapter
 * can be made:
 * 
 *  Parallel port            Keyboard port
 *
 *     +5V --------------------- +5V (4)
 *  
 *                 ______
 *     +5V -------|______|--.
 *                          |
 *     ACK (10) ------------|
 *                          |--- KBD CLOCK (5)
 *     STROBE (1) ---|<|----'
 *     
 *                 ______
 *     +5V -------|______|--.
 *                          |
 *     BUSY (11) -----------|
 *                          |--- KBD DATA (1)
 *     AUTOFD (14) --|<|----'
 *
 *     GND (18-25) ------------- GND (3)
 *     
 * The diodes can be fairly any type, and the resistors should be somewhere
 * around 5 kOhm, but the adapter will likely work without the resistors,
 * too.
 *
 * The +5V source can be taken either from USB, from mouse or keyboard ports,
 * or from a joystick port. Unfortunately, the parallel port of a PC doesn't
 * have a +5V pin, and feeding the keyboard from signal pins is out of question
 * with 300 mA power reqirement of a typical AT keyboard.
 */

#include <linux/module.h>
#include <linux/parport.h>
#include <linux/init.h>
#include <linux/serio.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Parallel port to Keyboard port adapter driver");
MODULE_LICENSE("GPL");

static unsigned int parkbd_pp_no;
module_param_named(port, parkbd_pp_no, int, 0);
MODULE_PARM_DESC(port, "Parallel port the adapter is connected to (default is 0)");

static unsigned int parkbd_mode = SERIO_8042;
module_param_named(mode, parkbd_mode, uint, 0);
MODULE_PARM_DESC(mode, "Mode of operation: XT = 0/AT = 1 (default)");

#define PARKBD_CLOCK	0x01	/* Strobe & Ack */
#define PARKBD_DATA	0x02	/* AutoFd & Busy */

static int parkbd_buffer;
static int parkbd_counter;
static unsigned long parkbd_last;
static int parkbd_writing;
static unsigned long parkbd_start;

static struct pardevice *parkbd_dev;
static struct serio *parkbd_port;

static int parkbd_readlines(void)
{
	return (parport_read_status(parkbd_dev->port) >> 6) ^ 2;
}

static void parkbd_writelines(int data)
{
	parport_write_control(parkbd_dev->port, (~data & 3) | 0x10);
}

static int parkbd_write(struct serio *port, unsigned char c)
{
	unsigned char p;

	if (!parkbd_mode) return -1;

        p = c ^ (c >> 4);
	p = p ^ (p >> 2);
	p = p ^ (p >> 1);

	parkbd_counter = 0;
	parkbd_writing = 1;
	parkbd_buffer = c | (((int) (~p & 1)) << 8) | 0x600;

	parkbd_writelines(2);

	return 0;
}

static void parkbd_interrupt(int irq, void *dev_id)
{

	if (parkbd_writing) {

		if (parkbd_counter && ((parkbd_counter == 11) || time_after(jiffies, parkbd_last + HZ/100))) {
			parkbd_counter = 0;
			parkbd_buffer = 0;
			parkbd_writing = 0;
			parkbd_writelines(3);
			return;
		}

		parkbd_writelines(((parkbd_buffer >> parkbd_counter++) & 1) | 2);

		if (parkbd_counter == 11) {
			parkbd_counter = 0;
			parkbd_buffer = 0;
			parkbd_writing = 0;
			parkbd_writelines(3);
		}

	} else {

		if ((parkbd_counter == parkbd_mode + 10) || time_after(jiffies, parkbd_last + HZ/100)) {
			parkbd_counter = 0;
			parkbd_buffer = 0;
		}

		parkbd_buffer |= (parkbd_readlines() >> 1) << parkbd_counter++;

		if (parkbd_counter == parkbd_mode + 10)
			serio_interrupt(parkbd_port, (parkbd_buffer >> (2 - parkbd_mode)) & 0xff, 0);
	}

	parkbd_last = jiffies;
}

static int parkbd_getport(void)
{
	struct parport *pp;

	pp = parport_find_number(parkbd_pp_no);

	if (pp == NULL) {
		printk(KERN_ERR "parkbd: no such parport\n");
		return -ENODEV;
	}

	parkbd_dev = parport_register_device(pp, "parkbd", NULL, NULL, parkbd_interrupt, PARPORT_DEV_EXCL, NULL);
	parport_put_port(pp);

	if (!parkbd_dev)
		return -ENODEV;

	if (parport_claim(parkbd_dev)) {
		parport_unregister_device(parkbd_dev);
		return -EBUSY;
	}

	parkbd_start = jiffies;

	return 0;
}

static struct serio * __init parkbd_allocate_serio(void)
{
	struct serio *serio;

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (serio) {
		serio->id.type = parkbd_mode;
		serio->write = parkbd_write,
		strlcpy(serio->name, "PARKBD AT/XT keyboard adapter", sizeof(serio->name));
		snprintf(serio->phys, sizeof(serio->phys), "%s/serio0", parkbd_dev->port->name);
	}

	return serio;
}

static int __init parkbd_init(void)
{
	int err;

	err = parkbd_getport();
	if (err)
		return err;

	parkbd_port = parkbd_allocate_serio();
	if (!parkbd_port) {
		parport_release(parkbd_dev);
		return -ENOMEM;
	}

	parkbd_writelines(3);

	serio_register_port(parkbd_port);

	printk(KERN_INFO "serio: PARKBD %s adapter on %s\n",
                        parkbd_mode ? "AT" : "XT", parkbd_dev->port->name);

	return 0;
}

static void __exit parkbd_exit(void)
{
	parport_release(parkbd_dev);
	serio_unregister_port(parkbd_port);
	parport_unregister_device(parkbd_dev);
}

module_init(parkbd_init);
module_exit(parkbd_exit);
