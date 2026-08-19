/*
 * drivers/input/serio/gscps2.c
 *
 * Copyright (c) 2004-2006 Helge Deller <deller@gmx.de>
 * Copyright (c) 2002 Laurent Canet <canetl@esiee.fr>
 * Copyright (c) 2002 Thibaut Varene <varenet@parisc-linux.org>
 *
 * Pieces of code based on linux-2.4's hp_mouse.c & hp_keyb.c
 *	Copyright (c) 1999 Alex deVries <alex@onefishtwo.ca>
 *	Copyright (c) 1999-2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (c) 2000 Xavier Debacker <debackex@esiee.fr>
 *	Copyright (c) 2000-2001 Thomas Marteau <marteaut@esiee.fr>
 *
 * HP GSC PS/2 port driver, found in PA/RISC Workstations
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * TODO:
 * - Dino testing (did HP ever shipped a machine on which this port
 *                 was usable/enabled ?)
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/property.h>
#include <linux/serio.h>

#include <asm/irq.h>
#include <asm/parisc-device.h>

MODULE_AUTHOR("Laurent Canet <canetl@esiee.fr>, Thibaut Varene <varenet@parisc-linux.org>, Helge Deller <deller@gmx.de>");
MODULE_DESCRIPTION("HP GSC PS2 port driver");
MODULE_LICENSE("GPL");

#define PFX "gscps2.c: "

/*
 * Driver constants
 */

/* various constants */
#define ENABLE			1
#define DISABLE			0

#define GSC_DINO_OFFSET		0x0800	/* offset for DINO controller versus LASI one */

/* PS/2 IO port offsets */
#define GSC_ID			0x00	/* device ID offset (see: GSC_ID_XXX) */
#define GSC_RESET		0x00	/* reset port offset */
#define GSC_RCVDATA		0x04	/* receive port offset */
#define GSC_XMTDATA		0x04	/* transmit port offset */
#define GSC_CONTROL		0x08	/* see: Control register bits */
#define GSC_STATUS		0x0C	/* see: Status register bits */

/* Control register bits */
#define GSC_CTRL_ENBL		0x01	/* enable interface */
#define GSC_CTRL_LPBXR		0x02	/* loopback operation */
#define GSC_CTRL_DIAG		0x20	/* directly control clock/data line */
#define GSC_CTRL_DATDIR		0x40	/* data line direct control */
#define GSC_CTRL_CLKDIR		0x80	/* clock line direct control */

/* Status register bits */
#define GSC_STAT_RBNE		0x01	/* Receive Buffer Not Empty */
#define GSC_STAT_TBNE		0x02	/* Transmit Buffer Not Empty */
#define GSC_STAT_TERR		0x04	/* Timeout Error */
#define GSC_STAT_PERR		0x08	/* Parity Error */
#define GSC_STAT_CMPINTR	0x10	/* Composite Interrupt = irq on any port */
#define GSC_STAT_DATSHD		0x40	/* Data Line Shadow */
#define GSC_STAT_CLKSHD		0x80	/* Clock Line Shadow */

/* IDs returned by GSC_ID port register */
#define GSC_ID_KEYBOARD		0	/* device ID values */
#define GSC_ID_MOUSE		1

#ifndef CONFIG_SERIO_GSCPS2_RDI_KEYCODES
# define CONFLICT(x, y) x
#else
# define CONFLICT(x, y) y
#endif

/*
 * Sadly RDI (Tadpole) decided to ship a different keyboard layout
 * than HP for their PS/2 laptop keyboard which leads to conflicting
 * keycodes between a normal HP PS/2 keyboard and a RDI PrecisionBook.
 *                       HP:            RDI:
 */
#define C_07	CONFLICT(KEY_F12,	KEY_F1)
#define C_11	CONFLICT(KEY_LEFTALT,	KEY_LEFTCTRL)
#define C_14	CONFLICT(KEY_LEFTCTRL,	KEY_CAPSLOCK)
#define C_58	CONFLICT(KEY_CAPSLOCK,	KEY_RIGHTCTRL)
#define C_61	CONFLICT(KEY_102ND,	KEY_LEFT)

/*
 * Special keycode value recognized by atkbd (ATKBD_KEY_NULL) to silently
 * discard scancodes without generating input events or "unknown key" warnings.
 */
#define KEY_NULL	255

#define KEYMAP_ENTRY(scancode, keycode) (((scancode) << 16) | (keycode))

static const u32 gscps2_keymap[] = {
	KEYMAP_ENTRY(0x01, KEY_F9),		KEYMAP_ENTRY(0x03, KEY_F5),
	KEYMAP_ENTRY(0x04, KEY_F3),		KEYMAP_ENTRY(0x05, KEY_F1),
	KEYMAP_ENTRY(0x06, KEY_F2),		KEYMAP_ENTRY(0x07, C_07),
	KEYMAP_ENTRY(0x08, KEY_ESC),		KEYMAP_ENTRY(0x09, KEY_F10),
	KEYMAP_ENTRY(0x0a, KEY_F8),		KEYMAP_ENTRY(0x0b, KEY_F6),
	KEYMAP_ENTRY(0x0c, KEY_F4),		KEYMAP_ENTRY(0x0d, KEY_TAB),
	KEYMAP_ENTRY(0x0e, KEY_GRAVE),		KEYMAP_ENTRY(0x0f, KEY_F2),
	KEYMAP_ENTRY(0x11, C_11),		KEYMAP_ENTRY(0x12, KEY_LEFTSHIFT),
	KEYMAP_ENTRY(0x14, C_14),		KEYMAP_ENTRY(0x15, KEY_Q),
	KEYMAP_ENTRY(0x16, KEY_1),		KEYMAP_ENTRY(0x17, KEY_F3),
	KEYMAP_ENTRY(0x19, KEY_LEFTALT),	KEYMAP_ENTRY(0x1a, KEY_Z),
	KEYMAP_ENTRY(0x1b, KEY_S),		KEYMAP_ENTRY(0x1c, KEY_A),
	KEYMAP_ENTRY(0x1d, KEY_W),		KEYMAP_ENTRY(0x1e, KEY_2),
	KEYMAP_ENTRY(0x1f, KEY_F4),		KEYMAP_ENTRY(0x21, KEY_C),
	KEYMAP_ENTRY(0x22, KEY_X),		KEYMAP_ENTRY(0x23, KEY_D),
	KEYMAP_ENTRY(0x24, KEY_E),		KEYMAP_ENTRY(0x25, KEY_4),
	KEYMAP_ENTRY(0x26, KEY_3),		KEYMAP_ENTRY(0x27, KEY_F5),
	KEYMAP_ENTRY(0x29, KEY_SPACE),		KEYMAP_ENTRY(0x2a, KEY_V),
	KEYMAP_ENTRY(0x2b, KEY_F),		KEYMAP_ENTRY(0x2c, KEY_T),
	KEYMAP_ENTRY(0x2d, KEY_R),		KEYMAP_ENTRY(0x2e, KEY_5),
	KEYMAP_ENTRY(0x2f, KEY_F6),		KEYMAP_ENTRY(0x31, KEY_N),
	KEYMAP_ENTRY(0x32, KEY_B),		KEYMAP_ENTRY(0x33, KEY_H),
	KEYMAP_ENTRY(0x34, KEY_G),		KEYMAP_ENTRY(0x35, KEY_Y),
	KEYMAP_ENTRY(0x36, KEY_6),		KEYMAP_ENTRY(0x37, KEY_F7),
	KEYMAP_ENTRY(0x39, KEY_RIGHTALT),	KEYMAP_ENTRY(0x3a, KEY_M),
	KEYMAP_ENTRY(0x3b, KEY_J),		KEYMAP_ENTRY(0x3c, KEY_U),
	KEYMAP_ENTRY(0x3d, KEY_7),		KEYMAP_ENTRY(0x3e, KEY_8),
	KEYMAP_ENTRY(0x3f, KEY_F8),		KEYMAP_ENTRY(0x41, KEY_COMMA),
	KEYMAP_ENTRY(0x42, KEY_K),		KEYMAP_ENTRY(0x43, KEY_I),
	KEYMAP_ENTRY(0x44, KEY_O),		KEYMAP_ENTRY(0x45, KEY_0),
	KEYMAP_ENTRY(0x46, KEY_9),		KEYMAP_ENTRY(0x47, KEY_F9),
	KEYMAP_ENTRY(0x49, KEY_DOT),		KEYMAP_ENTRY(0x4a, KEY_SLASH),
	KEYMAP_ENTRY(0x4b, KEY_L),		KEYMAP_ENTRY(0x4c, KEY_SEMICOLON),
	KEYMAP_ENTRY(0x4d, KEY_P),		KEYMAP_ENTRY(0x4e, KEY_MINUS),
	KEYMAP_ENTRY(0x4f, KEY_F10),		KEYMAP_ENTRY(0x52, KEY_APOSTROPHE),
	KEYMAP_ENTRY(0x54, KEY_LEFTBRACE),	KEYMAP_ENTRY(0x55, KEY_EQUAL),
	KEYMAP_ENTRY(0x56, KEY_F11),		KEYMAP_ENTRY(0x57, KEY_SYSRQ),
	KEYMAP_ENTRY(0x58, C_58),		KEYMAP_ENTRY(0x59, KEY_RIGHTSHIFT),
	KEYMAP_ENTRY(0x5a, KEY_ENTER),		KEYMAP_ENTRY(0x5b, KEY_RIGHTBRACE),
	KEYMAP_ENTRY(0x5c, KEY_BACKSLASH),	KEYMAP_ENTRY(0x5d, KEY_BACKSLASH),
	KEYMAP_ENTRY(0x5e, KEY_F12),		KEYMAP_ENTRY(0x5f, KEY_SCROLLLOCK),
	KEYMAP_ENTRY(0x60, KEY_DOWN),		KEYMAP_ENTRY(0x61, C_61),
	KEYMAP_ENTRY(0x62, KEY_PAUSE),		KEYMAP_ENTRY(0x63, KEY_UP),
	KEYMAP_ENTRY(0x64, KEY_DELETE),		KEYMAP_ENTRY(0x65, KEY_END),
	KEYMAP_ENTRY(0x66, KEY_BACKSPACE),	KEYMAP_ENTRY(0x67, KEY_INSERT),
	KEYMAP_ENTRY(0x69, KEY_KP1),		KEYMAP_ENTRY(0x6a, KEY_RIGHT),
	KEYMAP_ENTRY(0x6b, KEY_KP4),		KEYMAP_ENTRY(0x6c, KEY_KP7),
	KEYMAP_ENTRY(0x6d, KEY_PAGEDOWN),	KEYMAP_ENTRY(0x6e, KEY_HOME),
	KEYMAP_ENTRY(0x6f, KEY_PAGEUP),		KEYMAP_ENTRY(0x70, KEY_KP0),
	KEYMAP_ENTRY(0x71, KEY_KPDOT),		KEYMAP_ENTRY(0x72, KEY_KP2),
	KEYMAP_ENTRY(0x73, KEY_KP5),		KEYMAP_ENTRY(0x74, KEY_KP6),
	KEYMAP_ENTRY(0x75, KEY_KP8),		KEYMAP_ENTRY(0x76, KEY_ESC),
	KEYMAP_ENTRY(0x77, KEY_NUMLOCK),	KEYMAP_ENTRY(0x78, KEY_F11),
	KEYMAP_ENTRY(0x79, KEY_KPPLUS),		KEYMAP_ENTRY(0x7a, KEY_KP3),
	KEYMAP_ENTRY(0x7b, KEY_KPMINUS),	KEYMAP_ENTRY(0x7c, KEY_KPASTERISK),
	KEYMAP_ENTRY(0x7d, KEY_KP9),		KEYMAP_ENTRY(0x7e, KEY_SCROLLLOCK),
	KEYMAP_ENTRY(0x7f, KEY_102ND),		KEYMAP_ENTRY(0x91, KEY_RIGHTALT),
	KEYMAP_ENTRY(0x92, KEY_NULL),		KEYMAP_ENTRY(0x94, KEY_RIGHTCTRL),
	KEYMAP_ENTRY(0x9d, KEY_CAPSLOCK),	KEYMAP_ENTRY(0x9f, KEY_LEFTMETA),
	KEYMAP_ENTRY(0xa7, KEY_RIGHTMETA),	KEYMAP_ENTRY(0xaf, KEY_COMPOSE),
	KEYMAP_ENTRY(0xca, KEY_KPSLASH),	KEYMAP_ENTRY(0xda, KEY_KPENTER),
	KEYMAP_ENTRY(0xe9, KEY_END),		KEYMAP_ENTRY(0xeb, KEY_LEFT),
	KEYMAP_ENTRY(0xec, KEY_HOME),		KEYMAP_ENTRY(0xf0, KEY_INSERT),
	KEYMAP_ENTRY(0xf1, KEY_DELETE),		KEYMAP_ENTRY(0xf2, KEY_DOWN),
	KEYMAP_ENTRY(0xf4, KEY_RIGHT),		KEYMAP_ENTRY(0xf5, KEY_UP),
	KEYMAP_ENTRY(0xf7, KEY_PAUSE),		KEYMAP_ENTRY(0xfa, KEY_PAGEDOWN),
	KEYMAP_ENTRY(0xfc, KEY_SYSRQ),		KEYMAP_ENTRY(0xfd, KEY_PAGEUP),

	/* Escaped keycodes */
	KEYMAP_ENTRY(0x103, KEY_F7),		KEYMAP_ENTRY(0x10b, KEY_LEFTMETA),
	KEYMAP_ENTRY(0x10c, KEY_RIGHTMETA),	KEYMAP_ENTRY(0x111, KEY_RIGHTALT),
	KEYMAP_ENTRY(0x114, KEY_RIGHTCTRL),
};

static const struct property_entry gscps2_props[] = {
	PROPERTY_ENTRY_U32_ARRAY("linux,keymap", gscps2_keymap),
	{ }
};

static const struct software_node gscps2_keyboard_node = {
	.name = "gscps2-keyboard",
	.properties = gscps2_props,
};

static irqreturn_t gscps2_interrupt(int irq, void *dev);

#define BUFFER_SIZE 0x0f

/* GSC PS/2 port device struct */
struct gscps2port {
	struct list_head node;
	struct parisc_device *padev;
	struct serio *port;
	spinlock_t lock;
	char __iomem *addr;
	u8 act, append; /* position in buffer[] */
	struct {
		u8 data;
		u8 str;
	} buffer[BUFFER_SIZE+1];
	int id;
};

/*
 * Various HW level routines
 */

#define gscps2_readb_input(x)		readb((x)+GSC_RCVDATA)
#define gscps2_readb_control(x)		readb((x)+GSC_CONTROL)
#define gscps2_readb_status(x)		readb((x)+GSC_STATUS)
#define gscps2_writeb_control(x, y)	writeb((x), (y)+GSC_CONTROL)


/*
 * wait_TBE() - wait for Transmit Buffer Empty
 */

static int wait_TBE(char __iomem *addr)
{
	int timeout = 25000; /* device is expected to react within 250 msec */
	while (gscps2_readb_status(addr) & GSC_STAT_TBNE) {
		if (!--timeout)
			return 0;	/* This should not happen */
		udelay(10);
	}
	return 1;
}


/*
 * gscps2_flush() - flush the receive buffer
 */

static void gscps2_flush(struct gscps2port *ps2port)
{
	while (gscps2_readb_status(ps2port->addr) & GSC_STAT_RBNE)
		gscps2_readb_input(ps2port->addr);
	ps2port->act = ps2port->append = 0;
}

/*
 * gscps2_writeb_output() - write a byte to the port
 *
 * returns 1 on success, 0 on error
 */

static inline int gscps2_writeb_output(struct gscps2port *ps2port, u8 data)
{
	char __iomem *addr = ps2port->addr;

	if (!wait_TBE(addr)) {
		printk(KERN_DEBUG PFX "timeout - could not write byte %#x\n", data);
		return 0;
	}

	while (gscps2_readb_status(addr) & GSC_STAT_RBNE)
		/* wait */;

	scoped_guard(spinlock_irqsave, &ps2port->lock)
		writeb(data, addr+GSC_XMTDATA);

	/* this is ugly, but due to timing of the port it seems to be necessary. */
	mdelay(6);

	/* make sure any received data is returned as fast as possible */
	/* this is important e.g. when we set the LEDs on the keyboard */
	gscps2_interrupt(0, NULL);

	return 1;
}


/*
 * gscps2_enable() - enables or disables the port
 */

static void gscps2_enable(struct gscps2port *ps2port, int enable)
{
	u8 data;

	/* now enable/disable the port */
	scoped_guard(spinlock_irqsave, &ps2port->lock) {
		gscps2_flush(ps2port);
		data = gscps2_readb_control(ps2port->addr);
		if (enable)
			data |= GSC_CTRL_ENBL;
		else
			data &= ~GSC_CTRL_ENBL;
		gscps2_writeb_control(data, ps2port->addr);
	}

	wait_TBE(ps2port->addr);
	gscps2_flush(ps2port);
}

/*
 * gscps2_reset() - resets the PS/2 port
 */

static void gscps2_reset(struct gscps2port *ps2port)
{
	/* reset the interface */
	guard(spinlock_irqsave)(&ps2port->lock);
	gscps2_flush(ps2port);
	writeb(0xff, ps2port->addr + GSC_RESET);
	gscps2_flush(ps2port);
}

static LIST_HEAD(ps2port_list);

static void gscps2_read_data(struct gscps2port *ps2port)
{
	u8 status;

	do {
		status = gscps2_readb_status(ps2port->addr);
		if (!(status & GSC_STAT_RBNE))
			break;

		ps2port->buffer[ps2port->append].str = status;
		ps2port->buffer[ps2port->append].data =
				gscps2_readb_input(ps2port->addr);
		ps2port->append = (ps2port->append + 1) & BUFFER_SIZE;
	} while (true);
}

static bool gscps2_report_data(struct gscps2port *ps2port)
{
	unsigned int rxflags;
	u8 data, status;

	while (ps2port->act != ps2port->append) {
		/*
		 * Did new data arrived while we read existing data ?
		 * If yes, exit now and let the new irq handler start
		 * over again.
		 */
		if (gscps2_readb_status(ps2port->addr) & GSC_STAT_CMPINTR)
			return true;

		status = ps2port->buffer[ps2port->act].str;
		data   = ps2port->buffer[ps2port->act].data;

		ps2port->act = (ps2port->act + 1) & BUFFER_SIZE;
		rxflags = ((status & GSC_STAT_TERR) ? SERIO_TIMEOUT : 0 ) |
			  ((status & GSC_STAT_PERR) ? SERIO_PARITY  : 0 );

		serio_interrupt(ps2port->port, data, rxflags);
	}

	return false;
}

/**
 * gscps2_interrupt() - Interruption service routine
 * @irq: interrupt number which triggered (unused)
 * @dev: device pointer (unused)
 *
 * This function reads received PS/2 bytes and processes them on
 * all interfaces.
 * The problematic part here is, that the keyboard and mouse PS/2 port
 * share the same interrupt and it's not possible to send data if any
 * one of them holds input data. To solve this problem we try to receive
 * the data as fast as possible and handle the reporting to the upper layer
 * later.
 */

static irqreturn_t gscps2_interrupt(int irq, void *dev)
{
	struct gscps2port *ps2port;

	list_for_each_entry(ps2port, &ps2port_list, node) {
		guard(spinlock_irqsave)(&ps2port->lock);

		gscps2_read_data(ps2port);
	} /* list_for_each_entry */

	/* all data was read from the ports - now report the data to upper layer */
	list_for_each_entry(ps2port, &ps2port_list, node) {
		if (gscps2_report_data(ps2port)) {
			/* More data ready - break early to restart interrupt */
			break;
		}
	}

	return IRQ_HANDLED;
}


/*
 * gscps2_write() - send a byte out through the aux interface.
 */

static int gscps2_write(struct serio *port, unsigned char data)
{
	struct gscps2port *ps2port = port->port_data;

	if (!gscps2_writeb_output(ps2port, data)) {
		printk(KERN_DEBUG PFX "sending byte %#x failed.\n", data);
		return -1;
	}
	return 0;
}

/*
 * gscps2_open() is called when a port is opened by the higher layer.
 * It resets and enables the port.
 */

static int gscps2_open(struct serio *port)
{
	struct gscps2port *ps2port = port->port_data;

	gscps2_reset(ps2port);

	/* enable it */
	gscps2_enable(ps2port, ENABLE);

	gscps2_interrupt(0, NULL);

	return 0;
}

/*
 * gscps2_close() disables the port
 */

static void gscps2_close(struct serio *port)
{
	struct gscps2port *ps2port = port->port_data;
	gscps2_enable(ps2port, DISABLE);
}

/**
 * gscps2_probe() - Probes PS2 devices
 * @dev: pointer to parisc_device struct which will be probed
 *
 * @return: success/error report
 */

static int __init gscps2_probe(struct parisc_device *dev)
{
	struct gscps2port *ps2port;
	struct serio *serio;
	unsigned long hpa = dev->hpa.start;
	int ret;

	if (!dev->irq)
		return -ENODEV;

	/* Offset for DINO PS/2. Works with LASI even */
	if (dev->id.sversion == 0x96)
		hpa += GSC_DINO_OFFSET;

	ps2port = kzalloc_obj(*ps2port);
	serio = kzalloc_obj(*serio);
	if (!ps2port || !serio) {
		ret = -ENOMEM;
		goto fail_nomem;
	}

	dev_set_drvdata(&dev->dev, ps2port);

	ps2port->port = serio;
	ps2port->padev = dev;
	ps2port->addr = ioremap(hpa, GSC_STATUS + 4);
	if (!ps2port->addr) {
		ret = -ENOMEM;
		goto fail_nomem;
	}
	spin_lock_init(&ps2port->lock);

	gscps2_reset(ps2port);
	ps2port->id = readb(ps2port->addr + GSC_ID) & 0x0f;

	snprintf(serio->name, sizeof(serio->name), "gsc-ps2-%s",
		 (ps2port->id == GSC_ID_KEYBOARD) ? "keyboard" : "mouse");
	strscpy(serio->phys, dev_name(&dev->dev), sizeof(serio->phys));
	serio->id.type		= SERIO_8042;
	serio->write		= gscps2_write;
	serio->open		= gscps2_open;
	serio->close		= gscps2_close;
	serio->port_data	= ps2port;
	serio->dev.parent	= &dev->dev;

	ret = -EBUSY;
	if (request_irq(dev->irq, gscps2_interrupt, IRQF_SHARED, ps2port->port->name, ps2port))
		goto fail_miserably;

	if (ps2port->id != GSC_ID_KEYBOARD && ps2port->id != GSC_ID_MOUSE) {
		printk(KERN_WARNING PFX "Unsupported PS/2 port at 0x%08lx (id=%d) ignored\n",
				hpa, ps2port->id);
		ret = -ENODEV;
		goto fail;
	}

#if 0
	if (!request_mem_region(hpa, GSC_STATUS + 4, ps2port->port.name))
		goto fail;
#endif

	if (ps2port->id == GSC_ID_KEYBOARD) {
		ret = device_add_software_node(&serio->dev,
					       &gscps2_keyboard_node);
		if (ret) {
			dev_err(&dev->dev,
				"failed to add software node for keyboard: %d\n",
				ret);
			goto fail;
		}
	}

	pr_info("serio: %s port at 0x%08lx irq %d @ %s\n",
		ps2port->port->name,
		hpa,
		ps2port->padev->irq,
		ps2port->port->phys);

	serio_register_port(ps2port->port);

	list_add_tail(&ps2port->node, &ps2port_list);

	return 0;

fail:
	if (ps2port->id == GSC_ID_KEYBOARD)
		device_remove_software_node(&serio->dev);

	free_irq(dev->irq, ps2port);

fail_miserably:
	iounmap(ps2port->addr);
#if 0
	release_mem_region(dev->hpa.start, GSC_STATUS + 4);
#endif

fail_nomem:
	kfree(ps2port);
	kfree(serio);
	return ret;
}

/**
 * gscps2_remove() - Removes PS2 devices
 * @dev: pointer to parisc_device which shall be removed
 *
 * @return: success/error report
 */

static void __exit gscps2_remove(struct parisc_device *dev)
{
	struct gscps2port *ps2port = dev_get_drvdata(&dev->dev);

	if (ps2port->id == GSC_ID_KEYBOARD)
		device_remove_software_node(&ps2port->port->dev);

	serio_unregister_port(ps2port->port);
	free_irq(dev->irq, ps2port);
	gscps2_flush(ps2port);
	list_del(&ps2port->node);
	iounmap(ps2port->addr);
#if 0
	release_mem_region(dev->hpa, GSC_STATUS + 4);
#endif
	dev_set_drvdata(&dev->dev, NULL);
	kfree(ps2port);
}


static const struct parisc_device_id gscps2_device_tbl[] __initconst = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00084 }, /* LASI PS/2 */
#ifdef DINO_TESTED
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00096 }, /* DINO PS/2 */
#endif
	{ 0, }	/* 0 terminated list */
};
MODULE_DEVICE_TABLE(parisc, gscps2_device_tbl);

static struct parisc_driver parisc_ps2_driver __refdata = {
	.name		= "gsc_ps2",
	.id_table	= gscps2_device_tbl,
	.probe		= gscps2_probe,
	.remove		= __exit_p(gscps2_remove),
};

static int __init gscps2_init(void)
{
	int error;

	error = software_node_register(&gscps2_keyboard_node);
	if (error)
		return error;

	error = register_parisc_driver(&parisc_ps2_driver);
	if (error)
		software_node_unregister(&gscps2_keyboard_node);

	return error;
}

static void __exit gscps2_exit(void)
{
	unregister_parisc_driver(&parisc_ps2_driver);
	software_node_unregister(&gscps2_keyboard_node);
}


module_init(gscps2_init);
module_exit(gscps2_exit);
