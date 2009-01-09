/* sb1000.c: A General Instruments SB1000 driver for linux. */
/*
	Written 1998 by Franco Venturi.

	Copyright 1998 by Franco Venturi.
	Copyright 1994,1995 by Donald Becker.
	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This driver is for the General Instruments SB1000 (internal SURFboard)

	The author may be reached as fventuri@mediaone.net

	This program is free software; you can redistribute it
	and/or  modify it under  the terms of  the GNU General
	Public  License as  published  by  the  Free  Software
	Foundation;  either  version 2 of the License, or  (at
	your option) any later version.

	Changes:

	981115 Steven Hirsch <shirsch@adelphia.net>

	Linus changed the timer interface.  Should work on all recent
	development kernels.

	980608 Steven Hirsch <shirsch@adelphia.net>

	Small changes to make it work with 2.1.x kernels. Hopefully,
	nothing major will change before official release of Linux 2.2.

	Merged with 2.2 - Alan Cox
*/

static char version[] = "sb1000.c:v1.1.2 6/01/98 (fventuri@mediaone.net)\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/if_cablemodem.h> /* for SIOGCM/SIOSCM stuff */
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/delay.h>	/* for udelay() */
#include <linux/etherdevice.h>
#include <linux/pnp.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#ifdef SB1000_DEBUG
static int sb1000_debug = SB1000_DEBUG;
#else
static const int sb1000_debug = 1;
#endif

static const int SB1000_IO_EXTENT = 8;
/* SB1000 Maximum Receive Unit */
static const int SB1000_MRU = 1500; /* octects */

#define NPIDS 4
struct sb1000_private {
	struct sk_buff *rx_skb[NPIDS];
	short rx_dlen[NPIDS];
	unsigned int rx_frames;
	short rx_error_count;
	short rx_error_dpc_count;
	unsigned char rx_session_id[NPIDS];
	unsigned char rx_frame_id[NPIDS];
	unsigned char rx_pkt_type[NPIDS];
};

/* prototypes for Linux interface */
extern int sb1000_probe(struct net_device *dev);
static int sb1000_open(struct net_device *dev);
static int sb1000_dev_ioctl (struct net_device *dev, struct ifreq *ifr, int cmd);
static int sb1000_start_xmit(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t sb1000_interrupt(int irq, void *dev_id);
static int sb1000_close(struct net_device *dev);


/* SB1000 hardware routines to be used during open/configuration phases */
static int card_wait_for_busy_clear(const int ioaddr[],
	const char* name);
static int card_wait_for_ready(const int ioaddr[], const char* name,
	unsigned char in[]);
static int card_send_command(const int ioaddr[], const char* name,
	const unsigned char out[], unsigned char in[]);

/* SB1000 hardware routines to be used during frame rx interrupt */
static int sb1000_wait_for_ready(const int ioaddr[], const char* name);
static int sb1000_wait_for_ready_clear(const int ioaddr[],
	const char* name);
static void sb1000_send_command(const int ioaddr[], const char* name,
	const unsigned char out[]);
static void sb1000_read_status(const int ioaddr[], unsigned char in[]);
static void sb1000_issue_read_command(const int ioaddr[],
	const char* name);

/* SB1000 commands for open/configuration */
static int sb1000_reset(const int ioaddr[], const char* name);
static int sb1000_check_CRC(const int ioaddr[], const char* name);
static inline int sb1000_start_get_set_command(const int ioaddr[],
	const char* name);
static int sb1000_end_get_set_command(const int ioaddr[],
	const char* name);
static int sb1000_activate(const int ioaddr[], const char* name);
static int sb1000_get_firmware_version(const int ioaddr[],
	const char* name, unsigned char version[], int do_end);
static int sb1000_get_frequency(const int ioaddr[], const char* name,
	int* frequency);
static int sb1000_set_frequency(const int ioaddr[], const char* name,
	int frequency);
static int sb1000_get_PIDs(const int ioaddr[], const char* name,
	short PID[]);
static int sb1000_set_PIDs(const int ioaddr[], const char* name,
	const short PID[]);

/* SB1000 commands for frame rx interrupt */
static int sb1000_rx(struct net_device *dev);
static void sb1000_error_dpc(struct net_device *dev);

static const struct pnp_device_id sb1000_pnp_ids[] = {
	{ "GIC1000", 0 },
	{ "", 0 }
};
MODULE_DEVICE_TABLE(pnp, sb1000_pnp_ids);

static const struct net_device_ops sb1000_netdev_ops = {
	.ndo_open		= sb1000_open,
	.ndo_start_xmit		= sb1000_start_xmit,
	.ndo_do_ioctl		= sb1000_dev_ioctl,
	.ndo_stop		= sb1000_close,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static int
sb1000_probe_one(struct pnp_dev *pdev, const struct pnp_device_id *id)
{
	struct net_device *dev;
	unsigned short ioaddr[2], irq;
	unsigned int serial_number;
	int error = -ENODEV;

	if (pnp_device_attach(pdev) < 0)
		return -ENODEV;
	if (pnp_activate_dev(pdev) < 0)
		goto out_detach;

	if (!pnp_port_valid(pdev, 0) || !pnp_port_valid(pdev, 1))
		goto out_disable;
	if (!pnp_irq_valid(pdev, 0))
		goto out_disable;

	serial_number = pdev->card->serial;

	ioaddr[0] = pnp_port_start(pdev, 0);
	ioaddr[1] = pnp_port_start(pdev, 0);

	irq = pnp_irq(pdev, 0);

	if (!request_region(ioaddr[0], 16, "sb1000"))
		goto out_disable;
	if (!request_region(ioaddr[1], 16, "sb1000"))
		goto out_release_region0;

	dev = alloc_etherdev(sizeof(struct sb1000_private));
	if (!dev) {
		error = -ENOMEM;
		goto out_release_regions;
	}


	dev->base_addr = ioaddr[0];
	/* mem_start holds the second I/O address */
	dev->mem_start = ioaddr[1];
	dev->irq = irq;

	if (sb1000_debug > 0)
		printk(KERN_NOTICE "%s: sb1000 at (%#3.3lx,%#3.3lx), "
			"S/N %#8.8x, IRQ %d.\n", dev->name, dev->base_addr,
			dev->mem_start, serial_number, dev->irq);

	/*
	 * The SB1000 is an rx-only cable modem device.  The uplink is a modem
	 * and we do not want to arp on it.
	 */
	dev->flags = IFF_POINTOPOINT|IFF_NOARP;

	SET_NETDEV_DEV(dev, &pdev->dev);

	if (sb1000_debug > 0)
		printk(KERN_NOTICE "%s", version);

	dev->netdev_ops	= &sb1000_netdev_ops;

	/* hardware address is 0:0:serial_number */
	dev->dev_addr[2]	= serial_number >> 24 & 0xff;
	dev->dev_addr[3]	= serial_number >> 16 & 0xff;
	dev->dev_addr[4]	= serial_number >>  8 & 0xff;
	dev->dev_addr[5]	= serial_number >>  0 & 0xff;

	pnp_set_drvdata(pdev, dev);

	error = register_netdev(dev);
	if (error)
		goto out_free_netdev;
	return 0;

 out_free_netdev:
	free_netdev(dev);
 out_release_regions:
	release_region(ioaddr[1], 16);
 out_release_region0:
	release_region(ioaddr[0], 16);
 out_disable:
	pnp_disable_dev(pdev);
 out_detach:
	pnp_device_detach(pdev);
	return error;
}

static void
sb1000_remove_one(struct pnp_dev *pdev)
{
	struct net_device *dev = pnp_get_drvdata(pdev);

	unregister_netdev(dev);
	release_region(dev->base_addr, 16);
	release_region(dev->mem_start, 16);
	free_netdev(dev);
}

static struct pnp_driver sb1000_driver = {
	.name		= "sb1000",
	.id_table	= sb1000_pnp_ids,
	.probe		= sb1000_probe_one,
	.remove		= sb1000_remove_one,
};


/*
 * SB1000 hardware routines to be used during open/configuration phases
 */

static const int TimeOutJiffies = (875 * HZ) / 100;

/* Card Wait For Busy Clear (cannot be used during an interrupt) */
static int
card_wait_for_busy_clear(const int ioaddr[], const char* name)
{
	unsigned char a;
	unsigned long timeout;

	a = inb(ioaddr[0] + 7);
	timeout = jiffies + TimeOutJiffies;
	while (a & 0x80 || a & 0x40) {
		/* a little sleep */
		yield();

		a = inb(ioaddr[0] + 7);
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: card_wait_for_busy_clear timeout\n",
				name);
			return -ETIME;
		}
	}

	return 0;
}

/* Card Wait For Ready (cannot be used during an interrupt) */
static int
card_wait_for_ready(const int ioaddr[], const char* name, unsigned char in[])
{
	unsigned char a;
	unsigned long timeout;

	a = inb(ioaddr[1] + 6);
	timeout = jiffies + TimeOutJiffies;
	while (a & 0x80 || !(a & 0x40)) {
		/* a little sleep */
		yield();

		a = inb(ioaddr[1] + 6);
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: card_wait_for_ready timeout\n",
				name);
			return -ETIME;
		}
	}

	in[1] = inb(ioaddr[0] + 1);
	in[2] = inb(ioaddr[0] + 2);
	in[3] = inb(ioaddr[0] + 3);
	in[4] = inb(ioaddr[0] + 4);
	in[0] = inb(ioaddr[0] + 5);
	in[6] = inb(ioaddr[0] + 6);
	in[5] = inb(ioaddr[1] + 6);
	return 0;
}

/* Card Send Command (cannot be used during an interrupt) */
static int
card_send_command(const int ioaddr[], const char* name,
	const unsigned char out[], unsigned char in[])
{
	int status, x;

	if ((status = card_wait_for_busy_clear(ioaddr, name)))
		return status;
	outb(0xa0, ioaddr[0] + 6);
	outb(out[2], ioaddr[0] + 1);
	outb(out[3], ioaddr[0] + 2);
	outb(out[4], ioaddr[0] + 3);
	outb(out[5], ioaddr[0] + 4);
	outb(out[1], ioaddr[0] + 5);
	outb(0xa0, ioaddr[0] + 6);
	outb(out[0], ioaddr[0] + 7);
	if (out[0] != 0x20 && out[0] != 0x30) {
		if ((status = card_wait_for_ready(ioaddr, name, in)))
			return status;
		inb(ioaddr[0] + 7);
		if (sb1000_debug > 3)
			printk(KERN_DEBUG "%s: card_send_command "
				"out: %02x%02x%02x%02x%02x%02x  "
				"in: %02x%02x%02x%02x%02x%02x%02x\n", name,
				out[0], out[1], out[2], out[3], out[4], out[5],
				in[0], in[1], in[2], in[3], in[4], in[5], in[6]);
	} else {
		if (sb1000_debug > 3)
			printk(KERN_DEBUG "%s: card_send_command "
				"out: %02x%02x%02x%02x%02x%02x\n", name,
				out[0], out[1], out[2], out[3], out[4], out[5]);
	}

	if (out[1] == 0x1b) {
		x = (out[2] == 0x02);
	} else {
		if (out[0] >= 0x80 && in[0] != (out[1] | 0x80))
			return -EIO;
	}
	return 0;
}


/*
 * SB1000 hardware routines to be used during frame rx interrupt
 */
static const int Sb1000TimeOutJiffies = 7 * HZ;

/* Card Wait For Ready (to be used during frame rx) */
static int
sb1000_wait_for_ready(const int ioaddr[], const char* name)
{
	unsigned long timeout;

	timeout = jiffies + Sb1000TimeOutJiffies;
	while (inb(ioaddr[1] + 6) & 0x80) {
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: sb1000_wait_for_ready timeout\n",
				name);
			return -ETIME;
		}
	}
	timeout = jiffies + Sb1000TimeOutJiffies;
	while (!(inb(ioaddr[1] + 6) & 0x40)) {
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: sb1000_wait_for_ready timeout\n",
				name);
			return -ETIME;
		}
	}
	inb(ioaddr[0] + 7);
	return 0;
}

/* Card Wait For Ready Clear (to be used during frame rx) */
static int
sb1000_wait_for_ready_clear(const int ioaddr[], const char* name)
{
	unsigned long timeout;

	timeout = jiffies + Sb1000TimeOutJiffies;
	while (inb(ioaddr[1] + 6) & 0x80) {
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: sb1000_wait_for_ready_clear timeout\n",
				name);
			return -ETIME;
		}
	}
	timeout = jiffies + Sb1000TimeOutJiffies;
	while (inb(ioaddr[1] + 6) & 0x40) {
		if (time_after_eq(jiffies, timeout)) {
			printk(KERN_WARNING "%s: sb1000_wait_for_ready_clear timeout\n",
				name);
			return -ETIME;
		}
	}
	return 0;
}

/* Card Send Command (to be used during frame rx) */
static void
sb1000_send_command(const int ioaddr[], const char* name,
	const unsigned char out[])
{
	outb(out[2], ioaddr[0] + 1);
	outb(out[3], ioaddr[0] + 2);
	outb(out[4], ioaddr[0] + 3);
	outb(out[5], ioaddr[0] + 4);
	outb(out[1], ioaddr[0] + 5);
	outb(out[0], ioaddr[0] + 7);
	if (sb1000_debug > 3)
		printk(KERN_DEBUG "%s: sb1000_send_command out: %02x%02x%02x%02x"
			"%02x%02x\n", name, out[0], out[1], out[2], out[3], out[4], out[5]);
	return;
}

/* Card Read Status (to be used during frame rx) */
static void
sb1000_read_status(const int ioaddr[], unsigned char in[])
{
	in[1] = inb(ioaddr[0] + 1);
	in[2] = inb(ioaddr[0] + 2);
	in[3] = inb(ioaddr[0] + 3);
	in[4] = inb(ioaddr[0] + 4);
	in[0] = inb(ioaddr[0] + 5);
	return;
}

/* Issue Read Command (to be used during frame rx) */
static void
sb1000_issue_read_command(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x20, 0x00, 0x00, 0x01, 0x00, 0x00};

	sb1000_wait_for_ready_clear(ioaddr, name);
	outb(0xa0, ioaddr[0] + 6);
	sb1000_send_command(ioaddr, name, Command0);
	return;
}


/*
 * SB1000 commands for open/configuration
 */
/* reset SB1000 card */
static int
sb1000_reset(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x80, 0x16, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int port, status;

	port = ioaddr[1] + 6;
	outb(0x4, port);
	inb(port);
	udelay(1000);
	outb(0x0, port);
	inb(port);
	ssleep(1);
	outb(0x4, port);
	inb(port);
	udelay(1000);
	outb(0x0, port);
	inb(port);
	udelay(0);

	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	if (st[3] != 0xf0)
		return -EIO;
	return 0;
}

/* check SB1000 firmware CRC */
static int
sb1000_check_CRC(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x80, 0x1f, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int crc, status;

	/* check CRC */
	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	if (st[1] != st[3] || st[2] != st[4])
		return -EIO;
	crc = st[1] << 8 | st[2];
	return 0;
}

static inline int
sb1000_start_get_set_command(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x80, 0x1b, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];

	return card_send_command(ioaddr, name, Command0, st);
}

static int
sb1000_end_get_set_command(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x80, 0x1b, 0x02, 0x00, 0x00, 0x00};
	static const unsigned char Command1[6] = {0x20, 0x00, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int status;

	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	return card_send_command(ioaddr, name, Command1, st);
}

static int
sb1000_activate(const int ioaddr[], const char* name)
{
	static const unsigned char Command0[6] = {0x80, 0x11, 0x00, 0x00, 0x00, 0x00};
	static const unsigned char Command1[6] = {0x80, 0x16, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int status;

	ssleep(1);
	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	if ((status = card_send_command(ioaddr, name, Command1, st)))
		return status;
	if (st[3] != 0xf1) {
    	if ((status = sb1000_start_get_set_command(ioaddr, name)))
			return status;
		return -EIO;
	}
	udelay(1000);
    return sb1000_start_get_set_command(ioaddr, name);
}

/* get SB1000 firmware version */
static int
sb1000_get_firmware_version(const int ioaddr[], const char* name,
	unsigned char version[], int do_end)
{
	static const unsigned char Command0[6] = {0x80, 0x23, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int status;

	if ((status = sb1000_start_get_set_command(ioaddr, name)))
		return status;
	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	if (st[0] != 0xa3)
		return -EIO;
	version[0] = st[1];
	version[1] = st[2];
	if (do_end)
		return sb1000_end_get_set_command(ioaddr, name);
	else
		return 0;
}

/* get SB1000 frequency */
static int
sb1000_get_frequency(const int ioaddr[], const char* name, int* frequency)
{
	static const unsigned char Command0[6] = {0x80, 0x44, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int status;

	udelay(1000);
	if ((status = sb1000_start_get_set_command(ioaddr, name)))
		return status;
	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	*frequency = ((st[1] << 8 | st[2]) << 8 | st[3]) << 8 | st[4];
	return sb1000_end_get_set_command(ioaddr, name);
}

/* set SB1000 frequency */
static int
sb1000_set_frequency(const int ioaddr[], const char* name, int frequency)
{
	unsigned char st[7];
	int status;
	unsigned char Command0[6] = {0x80, 0x29, 0x00, 0x00, 0x00, 0x00};

	const int FrequencyLowerLimit = 57000;
	const int FrequencyUpperLimit = 804000;

	if (frequency < FrequencyLowerLimit || frequency > FrequencyUpperLimit) {
		printk(KERN_ERR "%s: frequency chosen (%d kHz) is not in the range "
			"[%d,%d] kHz\n", name, frequency, FrequencyLowerLimit,
			FrequencyUpperLimit);
		return -EINVAL;
	}
	udelay(1000);
	if ((status = sb1000_start_get_set_command(ioaddr, name)))
		return status;
	Command0[5] = frequency & 0xff;
	frequency >>= 8;
	Command0[4] = frequency & 0xff;
	frequency >>= 8;
	Command0[3] = frequency & 0xff;
	frequency >>= 8;
	Command0[2] = frequency & 0xff;
	return card_send_command(ioaddr, name, Command0, st);
}

/* get SB1000 PIDs */
static int
sb1000_get_PIDs(const int ioaddr[], const char* name, short PID[])
{
	static const unsigned char Command0[6] = {0x80, 0x40, 0x00, 0x00, 0x00, 0x00};
	static const unsigned char Command1[6] = {0x80, 0x41, 0x00, 0x00, 0x00, 0x00};
	static const unsigned char Command2[6] = {0x80, 0x42, 0x00, 0x00, 0x00, 0x00};
	static const unsigned char Command3[6] = {0x80, 0x43, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	int status;

	udelay(1000);
	if ((status = sb1000_start_get_set_command(ioaddr, name)))
		return status;

	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;
	PID[0] = st[1] << 8 | st[2];

	if ((status = card_send_command(ioaddr, name, Command1, st)))
		return status;
	PID[1] = st[1] << 8 | st[2];

	if ((status = card_send_command(ioaddr, name, Command2, st)))
		return status;
	PID[2] = st[1] << 8 | st[2];

	if ((status = card_send_command(ioaddr, name, Command3, st)))
		return status;
	PID[3] = st[1] << 8 | st[2];

	return sb1000_end_get_set_command(ioaddr, name);
}

/* set SB1000 PIDs */
static int
sb1000_set_PIDs(const int ioaddr[], const char* name, const short PID[])
{
	static const unsigned char Command4[6] = {0x80, 0x2e, 0x00, 0x00, 0x00, 0x00};

	unsigned char st[7];
	short p;
	int status;
	unsigned char Command0[6] = {0x80, 0x31, 0x00, 0x00, 0x00, 0x00};
	unsigned char Command1[6] = {0x80, 0x32, 0x00, 0x00, 0x00, 0x00};
	unsigned char Command2[6] = {0x80, 0x33, 0x00, 0x00, 0x00, 0x00};
	unsigned char Command3[6] = {0x80, 0x34, 0x00, 0x00, 0x00, 0x00};

	udelay(1000);
	if ((status = sb1000_start_get_set_command(ioaddr, name)))
		return status;

	p = PID[0];
	Command0[3] = p & 0xff;
	p >>= 8;
	Command0[2] = p & 0xff;
	if ((status = card_send_command(ioaddr, name, Command0, st)))
		return status;

	p = PID[1];
	Command1[3] = p & 0xff;
	p >>= 8;
	Command1[2] = p & 0xff;
	if ((status = card_send_command(ioaddr, name, Command1, st)))
		return status;

	p = PID[2];
	Command2[3] = p & 0xff;
	p >>= 8;
	Command2[2] = p & 0xff;
	if ((status = card_send_command(ioaddr, name, Command2, st)))
		return status;

	p = PID[3];
	Command3[3] = p & 0xff;
	p >>= 8;
	Command3[2] = p & 0xff;
	if ((status = card_send_command(ioaddr, name, Command3, st)))
		return status;

	if ((status = card_send_command(ioaddr, name, Command4, st)))
		return status;
	return sb1000_end_get_set_command(ioaddr, name);
}


static void
sb1000_print_status_buffer(const char* name, unsigned char st[],
	unsigned char buffer[], int size)
{
	int i, j, k;

	printk(KERN_DEBUG "%s: status: %02x %02x\n", name, st[0], st[1]);
	if (buffer[24] == 0x08 && buffer[25] == 0x00 && buffer[26] == 0x45) {
		printk(KERN_DEBUG "%s: length: %d protocol: %d from: %d.%d.%d.%d:%d "
			"to %d.%d.%d.%d:%d\n", name, buffer[28] << 8 | buffer[29],
			buffer[35], buffer[38], buffer[39], buffer[40], buffer[41],
            buffer[46] << 8 | buffer[47],
			buffer[42], buffer[43], buffer[44], buffer[45],
            buffer[48] << 8 | buffer[49]);
	} else {
		for (i = 0, k = 0; i < (size + 7) / 8; i++) {
			printk(KERN_DEBUG "%s: %s", name, i ? "       " : "buffer:");
			for (j = 0; j < 8 && k < size; j++, k++)
				printk(" %02x", buffer[k]);
			printk("\n");
		}
	}
	return;
}

/*
 * SB1000 commands for frame rx interrupt
 */
/* receive a single frame and assemble datagram
 * (this is the heart of the interrupt routine)
 */
static int
sb1000_rx(struct net_device *dev)
{

#define FRAMESIZE 184
	unsigned char st[2], buffer[FRAMESIZE], session_id, frame_id;
	short dlen;
	int ioaddr, ns;
	unsigned int skbsize;
	struct sk_buff *skb;
	struct sb1000_private *lp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;

	/* SB1000 frame constants */
	const int FrameSize = FRAMESIZE;
	const int NewDatagramHeaderSkip = 8;
	const int NewDatagramHeaderSize = NewDatagramHeaderSkip + 18;
	const int NewDatagramDataSize = FrameSize - NewDatagramHeaderSize;
	const int ContDatagramHeaderSkip = 7;
	const int ContDatagramHeaderSize = ContDatagramHeaderSkip + 1;
	const int ContDatagramDataSize = FrameSize - ContDatagramHeaderSize;
	const int TrailerSize = 4;

	ioaddr = dev->base_addr;

	insw(ioaddr, (unsigned short*) st, 1);
#ifdef XXXDEBUG
printk("cm0: received: %02x %02x\n", st[0], st[1]);
#endif /* XXXDEBUG */
	lp->rx_frames++;

	/* decide if it is a good or bad frame */
	for (ns = 0; ns < NPIDS; ns++) {
		session_id = lp->rx_session_id[ns];
		frame_id = lp->rx_frame_id[ns];
		if (st[0] == session_id) {
			if (st[1] == frame_id || (!frame_id && (st[1] & 0xf0) == 0x30)) {
				goto good_frame;
			} else if ((st[1] & 0xf0) == 0x30 && (st[0] & 0x40)) {
				goto skipped_frame;
			} else {
				goto bad_frame;
			}
		} else if (st[0] == (session_id | 0x40)) {
			if ((st[1] & 0xf0) == 0x30) {
				goto skipped_frame;
			} else {
				goto bad_frame;
			}
		}
	}
	goto bad_frame;

skipped_frame:
	stats->rx_frame_errors++;
	skb = lp->rx_skb[ns];
	if (sb1000_debug > 1)
		printk(KERN_WARNING "%s: missing frame(s): got %02x %02x "
			"expecting %02x %02x\n", dev->name, st[0], st[1],
			skb ? session_id : session_id | 0x40, frame_id);
	if (skb) {
		dev_kfree_skb(skb);
		skb = NULL;
	}

good_frame:
	lp->rx_frame_id[ns] = 0x30 | ((st[1] + 1) & 0x0f);
	/* new datagram */
	if (st[0] & 0x40) {
		/* get data length */
		insw(ioaddr, buffer, NewDatagramHeaderSize / 2);
#ifdef XXXDEBUG
printk("cm0: IP identification: %02x%02x  fragment offset: %02x%02x\n", buffer[30], buffer[31], buffer[32], buffer[33]);
#endif /* XXXDEBUG */
		if (buffer[0] != NewDatagramHeaderSkip) {
			if (sb1000_debug > 1)
				printk(KERN_WARNING "%s: new datagram header skip error: "
					"got %02x expecting %02x\n", dev->name, buffer[0],
					NewDatagramHeaderSkip);
			stats->rx_length_errors++;
			insw(ioaddr, buffer, NewDatagramDataSize / 2);
			goto bad_frame_next;
		}
		dlen = ((buffer[NewDatagramHeaderSkip + 3] & 0x0f) << 8 |
			buffer[NewDatagramHeaderSkip + 4]) - 17;
		if (dlen > SB1000_MRU) {
			if (sb1000_debug > 1)
				printk(KERN_WARNING "%s: datagram length (%d) greater "
					"than MRU (%d)\n", dev->name, dlen, SB1000_MRU);
			stats->rx_length_errors++;
			insw(ioaddr, buffer, NewDatagramDataSize / 2);
			goto bad_frame_next;
		}
		lp->rx_dlen[ns] = dlen;
		/* compute size to allocate for datagram */
		skbsize = dlen + FrameSize;
		if ((skb = alloc_skb(skbsize, GFP_ATOMIC)) == NULL) {
			if (sb1000_debug > 1)
				printk(KERN_WARNING "%s: can't allocate %d bytes long "
					"skbuff\n", dev->name, skbsize);
			stats->rx_dropped++;
			insw(ioaddr, buffer, NewDatagramDataSize / 2);
			goto dropped_frame;
		}
		skb->dev = dev;
		skb_reset_mac_header(skb);
		skb->protocol = (unsigned short) buffer[NewDatagramHeaderSkip + 16];
		insw(ioaddr, skb_put(skb, NewDatagramDataSize),
			NewDatagramDataSize / 2);
		lp->rx_skb[ns] = skb;
	} else {
		/* continuation of previous datagram */
		insw(ioaddr, buffer, ContDatagramHeaderSize / 2);
		if (buffer[0] != ContDatagramHeaderSkip) {
			if (sb1000_debug > 1)
				printk(KERN_WARNING "%s: cont datagram header skip error: "
					"got %02x expecting %02x\n", dev->name, buffer[0],
					ContDatagramHeaderSkip);
			stats->rx_length_errors++;
			insw(ioaddr, buffer, ContDatagramDataSize / 2);
			goto bad_frame_next;
		}
		skb = lp->rx_skb[ns];
		insw(ioaddr, skb_put(skb, ContDatagramDataSize),
			ContDatagramDataSize / 2);
		dlen = lp->rx_dlen[ns];
	}
	if (skb->len < dlen + TrailerSize) {
		lp->rx_session_id[ns] &= ~0x40;
		return 0;
	}

	/* datagram completed: send to upper level */
	skb_trim(skb, dlen);
	netif_rx(skb);
	stats->rx_bytes+=dlen;
	stats->rx_packets++;
	lp->rx_skb[ns] = NULL;
	lp->rx_session_id[ns] |= 0x40;
	return 0;

bad_frame:
	insw(ioaddr, buffer, FrameSize / 2);
	if (sb1000_debug > 1)
		printk(KERN_WARNING "%s: frame error: got %02x %02x\n",
			dev->name, st[0], st[1]);
	stats->rx_frame_errors++;
bad_frame_next:
	if (sb1000_debug > 2)
		sb1000_print_status_buffer(dev->name, st, buffer, FrameSize);
dropped_frame:
	stats->rx_errors++;
	if (ns < NPIDS) {
		if ((skb = lp->rx_skb[ns])) {
			dev_kfree_skb(skb);
			lp->rx_skb[ns] = NULL;
		}
		lp->rx_session_id[ns] |= 0x40;
	}
	return -1;
}

static void
sb1000_error_dpc(struct net_device *dev)
{
	static const unsigned char Command0[6] = {0x80, 0x26, 0x00, 0x00, 0x00, 0x00};

	char *name;
	unsigned char st[5];
	int ioaddr[2];
	struct sb1000_private *lp = netdev_priv(dev);
	const int ErrorDpcCounterInitialize = 200;

	ioaddr[0] = dev->base_addr;
	/* mem_start holds the second I/O address */
	ioaddr[1] = dev->mem_start;
	name = dev->name;

	sb1000_wait_for_ready_clear(ioaddr, name);
	sb1000_send_command(ioaddr, name, Command0);
	sb1000_wait_for_ready(ioaddr, name);
	sb1000_read_status(ioaddr, st);
	if (st[1] & 0x10)
		lp->rx_error_dpc_count = ErrorDpcCounterInitialize;
	return;
}


/*
 * Linux interface functions
 */
static int
sb1000_open(struct net_device *dev)
{
	char *name;
	int ioaddr[2], status;
	struct sb1000_private *lp = netdev_priv(dev);
	const unsigned short FirmwareVersion[] = {0x01, 0x01};

	ioaddr[0] = dev->base_addr;
	/* mem_start holds the second I/O address */
	ioaddr[1] = dev->mem_start;
	name = dev->name;

	/* initialize sb1000 */
	if ((status = sb1000_reset(ioaddr, name)))
		return status;
	ssleep(1);
	if ((status = sb1000_check_CRC(ioaddr, name)))
		return status;

	/* initialize private data before board can catch interrupts */
	lp->rx_skb[0] = NULL;
	lp->rx_skb[1] = NULL;
	lp->rx_skb[2] = NULL;
	lp->rx_skb[3] = NULL;
	lp->rx_dlen[0] = 0;
	lp->rx_dlen[1] = 0;
	lp->rx_dlen[2] = 0;
	lp->rx_dlen[3] = 0;
	lp->rx_frames = 0;
	lp->rx_error_count = 0;
	lp->rx_error_dpc_count = 0;
	lp->rx_session_id[0] = 0x50;
	lp->rx_session_id[0] = 0x48;
	lp->rx_session_id[0] = 0x44;
	lp->rx_session_id[0] = 0x42;
	lp->rx_frame_id[0] = 0;
	lp->rx_frame_id[1] = 0;
	lp->rx_frame_id[2] = 0;
	lp->rx_frame_id[3] = 0;
	if (request_irq(dev->irq, &sb1000_interrupt, 0, "sb1000", dev)) {
		return -EAGAIN;
	}

	if (sb1000_debug > 2)
		printk(KERN_DEBUG "%s: Opening, IRQ %d\n", name, dev->irq);

	/* Activate board and check firmware version */
	udelay(1000);
	if ((status = sb1000_activate(ioaddr, name)))
		return status;
	udelay(0);
	if ((status = sb1000_get_firmware_version(ioaddr, name, version, 0)))
		return status;
	if (version[0] != FirmwareVersion[0] || version[1] != FirmwareVersion[1])
		printk(KERN_WARNING "%s: found firmware version %x.%02x "
			"(should be %x.%02x)\n", name, version[0], version[1],
			FirmwareVersion[0], FirmwareVersion[1]);


	netif_start_queue(dev);
	return 0;					/* Always succeed */
}

static int sb1000_dev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	char* name;
	unsigned char version[2];
	short PID[4];
	int ioaddr[2], status, frequency;
	unsigned int stats[5];
	struct sb1000_private *lp = netdev_priv(dev);

	if (!(dev && dev->flags & IFF_UP))
		return -ENODEV;

	ioaddr[0] = dev->base_addr;
	/* mem_start holds the second I/O address */
	ioaddr[1] = dev->mem_start;
	name = dev->name;

	switch (cmd) {
	case SIOCGCMSTATS:		/* get statistics */
		stats[0] = dev->stats.rx_bytes;
		stats[1] = lp->rx_frames;
		stats[2] = dev->stats.rx_packets;
		stats[3] = dev->stats.rx_errors;
		stats[4] = dev->stats.rx_dropped;
		if(copy_to_user(ifr->ifr_data, stats, sizeof(stats)))
			return -EFAULT;
		status = 0;
		break;

	case SIOCGCMFIRMWARE:		/* get firmware version */
		if ((status = sb1000_get_firmware_version(ioaddr, name, version, 1)))
			return status;
		if(copy_to_user(ifr->ifr_data, version, sizeof(version)))
			return -EFAULT;
		break;

	case SIOCGCMFREQUENCY:		/* get frequency */
		if ((status = sb1000_get_frequency(ioaddr, name, &frequency)))
			return status;
		if(put_user(frequency, (int __user *) ifr->ifr_data))
			return -EFAULT;
		break;

	case SIOCSCMFREQUENCY:		/* set frequency */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if(get_user(frequency, (int __user *) ifr->ifr_data))
			return -EFAULT;
		if ((status = sb1000_set_frequency(ioaddr, name, frequency)))
			return status;
		break;

	case SIOCGCMPIDS:			/* get PIDs */
		if ((status = sb1000_get_PIDs(ioaddr, name, PID)))
			return status;
		if(copy_to_user(ifr->ifr_data, PID, sizeof(PID)))
			return -EFAULT;
		break;

	case SIOCSCMPIDS:			/* set PIDs */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if(copy_from_user(PID, ifr->ifr_data, sizeof(PID)))
			return -EFAULT;
		if ((status = sb1000_set_PIDs(ioaddr, name, PID)))
			return status;
		/* set session_id, frame_id and pkt_type too */
		lp->rx_session_id[0] = 0x50 | (PID[0] & 0x0f);
		lp->rx_session_id[1] = 0x48;
		lp->rx_session_id[2] = 0x44;
		lp->rx_session_id[3] = 0x42;
		lp->rx_frame_id[0] = 0;
		lp->rx_frame_id[1] = 0;
		lp->rx_frame_id[2] = 0;
		lp->rx_frame_id[3] = 0;
		break;

	default:
		status = -EINVAL;
		break;
	}
	return status;
}

/* transmit function: do nothing since SB1000 can't send anything out */
static int
sb1000_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	printk(KERN_WARNING "%s: trying to transmit!!!\n", dev->name);
	/* sb1000 can't xmit datagrams */
	dev_kfree_skb(skb);
	return 0;
}

/* SB1000 interrupt handler. */
static irqreturn_t sb1000_interrupt(int irq, void *dev_id)
{
	static const unsigned char Command0[6] = {0x80, 0x2c, 0x00, 0x00, 0x00, 0x00};
	static const unsigned char Command1[6] = {0x80, 0x2e, 0x00, 0x00, 0x00, 0x00};

	char *name;
	unsigned char st;
	int ioaddr[2];
	struct net_device *dev = dev_id;
	struct sb1000_private *lp = netdev_priv(dev);

	const int MaxRxErrorCount = 6;

	ioaddr[0] = dev->base_addr;
	/* mem_start holds the second I/O address */
	ioaddr[1] = dev->mem_start;
	name = dev->name;

	/* is it a good interrupt? */
	st = inb(ioaddr[1] + 6);
	if (!(st & 0x08 && st & 0x20)) {
		return IRQ_NONE;
	}

	if (sb1000_debug > 3)
		printk(KERN_DEBUG "%s: entering interrupt\n", dev->name);

	st = inb(ioaddr[0] + 7);
	if (sb1000_rx(dev))
		lp->rx_error_count++;
#ifdef SB1000_DELAY
	udelay(SB1000_DELAY);
#endif /* SB1000_DELAY */
	sb1000_issue_read_command(ioaddr, name);
	if (st & 0x01) {
		sb1000_error_dpc(dev);
		sb1000_issue_read_command(ioaddr, name);
	}
	if (lp->rx_error_dpc_count && !(--lp->rx_error_dpc_count)) {
		sb1000_wait_for_ready_clear(ioaddr, name);
		sb1000_send_command(ioaddr, name, Command0);
		sb1000_wait_for_ready(ioaddr, name);
		sb1000_issue_read_command(ioaddr, name);
	}
	if (lp->rx_error_count >= MaxRxErrorCount) {
		sb1000_wait_for_ready_clear(ioaddr, name);
		sb1000_send_command(ioaddr, name, Command1);
		sb1000_wait_for_ready(ioaddr, name);
		sb1000_issue_read_command(ioaddr, name);
		lp->rx_error_count = 0;
	}

	return IRQ_HANDLED;
}

static int sb1000_close(struct net_device *dev)
{
	int i;
	int ioaddr[2];
	struct sb1000_private *lp = netdev_priv(dev);

	if (sb1000_debug > 2)
		printk(KERN_DEBUG "%s: Shutting down sb1000.\n", dev->name);

	netif_stop_queue(dev);

	ioaddr[0] = dev->base_addr;
	/* mem_start holds the second I/O address */
	ioaddr[1] = dev->mem_start;

	free_irq(dev->irq, dev);
	/* If we don't do this, we can't re-insmod it later. */
	release_region(ioaddr[1], SB1000_IO_EXTENT);
	release_region(ioaddr[0], SB1000_IO_EXTENT);

	/* free rx_skb's if needed */
	for (i=0; i<4; i++) {
		if (lp->rx_skb[i]) {
			dev_kfree_skb(lp->rx_skb[i]);
		}
	}
	return 0;
}

MODULE_AUTHOR("Franco Venturi <fventuri@mediaone.net>");
MODULE_DESCRIPTION("General Instruments SB1000 driver");
MODULE_LICENSE("GPL");

static int __init
sb1000_init(void)
{
	return pnp_register_driver(&sb1000_driver);
}

static void __exit
sb1000_exit(void)
{
	pnp_unregister_driver(&sb1000_driver);
}

module_init(sb1000_init);
module_exit(sb1000_exit);
