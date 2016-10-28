/*
 * arch/powerpc/platforms/powermac/low_i2c.c
 *
 *  Copyright (C) 2003-2005 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * The linux i2c layer isn't completely suitable for our needs for various
 * reasons ranging from too late initialisation to semantics not perfectly
 * matching some requirements of the apple platform functions etc...
 *
 * This file thus provides a simple low level unified i2c interface for
 * powermac that covers the various types of i2c busses used in Apple machines.
 * For now, keywest, PMU and SMU, though we could add Cuda, or other bit
 * banging busses found on older chipsets in earlier machines if we ever need
 * one of them.
 *
 * The drivers in this file are synchronous/blocking. In addition, the
 * keywest one is fairly slow due to the use of msleep instead of interrupts
 * as the interrupt is currently used by i2c-keywest. In the long run, we
 * might want to get rid of those high-level interfaces to linux i2c layer
 * either completely (converting all drivers) or replacing them all with a
 * single stub driver on top of this one. Once done, the interrupt will be
 * available for our use.
 */

#undef DEBUG
#undef DEBUG_LOW

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/smu.h>
#include <asm/pmac_pfunc.h>
#include <asm/pmac_low_i2c.h>

#ifdef DEBUG
#define DBG(x...) do {\
		printk(KERN_DEBUG "low_i2c:" x);	\
	} while(0)
#else
#define DBG(x...)
#endif

#ifdef DEBUG_LOW
#define DBG_LOW(x...) do {\
		printk(KERN_DEBUG "low_i2c:" x);	\
	} while(0)
#else
#define DBG_LOW(x...)
#endif


static int pmac_i2c_force_poll = 1;

/*
 * A bus structure. Each bus in the system has such a structure associated.
 */
struct pmac_i2c_bus
{
	struct list_head	link;
	struct device_node	*controller;
	struct device_node	*busnode;
	int			type;
	int			flags;
	struct i2c_adapter	adapter;
	void			*hostdata;
	int			channel;	/* some hosts have multiple */
	int			mode;		/* current mode */
	struct mutex		mutex;
	int			opened;
	int			polled;		/* open mode */
	struct platform_device	*platform_dev;

	/* ops */
	int (*open)(struct pmac_i2c_bus *bus);
	void (*close)(struct pmac_i2c_bus *bus);
	int (*xfer)(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
		    u32 subaddr, u8 *data, int len);
};

static LIST_HEAD(pmac_i2c_busses);

/*
 * Keywest implementation
 */

struct pmac_i2c_host_kw
{
	struct mutex		mutex;		/* Access mutex for use by
						 * i2c-keywest */
	void __iomem		*base;		/* register base address */
	int			bsteps;		/* register stepping */
	int			speed;		/* speed */
	int			irq;
	u8			*data;
	unsigned		len;
	int			state;
	int			rw;
	int			polled;
	int			result;
	struct completion	complete;
	spinlock_t		lock;
	struct timer_list	timeout_timer;
};

/* Register indices */
typedef enum {
	reg_mode = 0,
	reg_control,
	reg_status,
	reg_isr,
	reg_ier,
	reg_addr,
	reg_subaddr,
	reg_data
} reg_t;

/* The Tumbler audio equalizer can be really slow sometimes */
#define KW_POLL_TIMEOUT		(2*HZ)

/* Mode register */
#define KW_I2C_MODE_100KHZ	0x00
#define KW_I2C_MODE_50KHZ	0x01
#define KW_I2C_MODE_25KHZ	0x02
#define KW_I2C_MODE_DUMB	0x00
#define KW_I2C_MODE_STANDARD	0x04
#define KW_I2C_MODE_STANDARDSUB	0x08
#define KW_I2C_MODE_COMBINED	0x0C
#define KW_I2C_MODE_MODE_MASK	0x0C
#define KW_I2C_MODE_CHAN_MASK	0xF0

/* Control register */
#define KW_I2C_CTL_AAK		0x01
#define KW_I2C_CTL_XADDR	0x02
#define KW_I2C_CTL_STOP		0x04
#define KW_I2C_CTL_START	0x08

/* Status register */
#define KW_I2C_STAT_BUSY	0x01
#define KW_I2C_STAT_LAST_AAK	0x02
#define KW_I2C_STAT_LAST_RW	0x04
#define KW_I2C_STAT_SDA		0x08
#define KW_I2C_STAT_SCL		0x10

/* IER & ISR registers */
#define KW_I2C_IRQ_DATA		0x01
#define KW_I2C_IRQ_ADDR		0x02
#define KW_I2C_IRQ_STOP		0x04
#define KW_I2C_IRQ_START	0x08
#define KW_I2C_IRQ_MASK		0x0F

/* State machine states */
enum {
	state_idle,
	state_addr,
	state_read,
	state_write,
	state_stop,
	state_dead
};

#define WRONG_STATE(name) do {\
		printk(KERN_DEBUG "KW: wrong state. Got %s, state: %s " \
		       "(isr: %02x)\n",	\
		       name, __kw_state_names[host->state], isr); \
	} while(0)

static const char *__kw_state_names[] = {
	"state_idle",
	"state_addr",
	"state_read",
	"state_write",
	"state_stop",
	"state_dead"
};

static inline u8 __kw_read_reg(struct pmac_i2c_host_kw *host, reg_t reg)
{
	return readb(host->base + (((unsigned int)reg) << host->bsteps));
}

static inline void __kw_write_reg(struct pmac_i2c_host_kw *host,
				  reg_t reg, u8 val)
{
	writeb(val, host->base + (((unsigned)reg) << host->bsteps));
	(void)__kw_read_reg(host, reg_subaddr);
}

#define kw_write_reg(reg, val)	__kw_write_reg(host, reg, val)
#define kw_read_reg(reg)	__kw_read_reg(host, reg)

static u8 kw_i2c_wait_interrupt(struct pmac_i2c_host_kw *host)
{
	int i, j;
	u8 isr;
	
	for (i = 0; i < 1000; i++) {
		isr = kw_read_reg(reg_isr) & KW_I2C_IRQ_MASK;
		if (isr != 0)
			return isr;

		/* This code is used with the timebase frozen, we cannot rely
		 * on udelay nor schedule when in polled mode !
		 * For now, just use a bogus loop....
		 */
		if (host->polled) {
			for (j = 1; j < 100000; j++)
				mb();
		} else
			msleep(1);
	}
	return isr;
}

static void kw_i2c_do_stop(struct pmac_i2c_host_kw *host, int result)
{
	kw_write_reg(reg_control, KW_I2C_CTL_STOP);
	host->state = state_stop;
	host->result = result;
}


static void kw_i2c_handle_interrupt(struct pmac_i2c_host_kw *host, u8 isr)
{
	u8 ack;

	DBG_LOW("kw_handle_interrupt(%s, isr: %x)\n",
		__kw_state_names[host->state], isr);

	if (host->state == state_idle) {
		printk(KERN_WARNING "low_i2c: Keywest got an out of state"
		       " interrupt, ignoring\n");
		kw_write_reg(reg_isr, isr);
		return;
	}

	if (isr == 0) {
		printk(KERN_WARNING "low_i2c: Timeout in i2c transfer"
		       " on keywest !\n");
		if (host->state != state_stop) {
			kw_i2c_do_stop(host, -EIO);
			return;
		}
		ack = kw_read_reg(reg_status);
		if (ack & KW_I2C_STAT_BUSY)
			kw_write_reg(reg_status, 0);
		host->state = state_idle;
		kw_write_reg(reg_ier, 0x00);
		if (!host->polled)
			complete(&host->complete);
		return;
	}

	if (isr & KW_I2C_IRQ_ADDR) {
		ack = kw_read_reg(reg_status);
		if (host->state != state_addr) {
			WRONG_STATE("KW_I2C_IRQ_ADDR"); 
			kw_i2c_do_stop(host, -EIO);
		}
		if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
			host->result = -ENXIO;
			host->state = state_stop;
			DBG_LOW("KW: NAK on address\n");
		} else {
			if (host->len == 0)
				kw_i2c_do_stop(host, 0);
			else if (host->rw) {
				host->state = state_read;
				if (host->len > 1)
					kw_write_reg(reg_control,
						     KW_I2C_CTL_AAK);
			} else {
				host->state = state_write;
				kw_write_reg(reg_data, *(host->data++));
				host->len--;
			}
		}
		kw_write_reg(reg_isr, KW_I2C_IRQ_ADDR);
	}

	if (isr & KW_I2C_IRQ_DATA) {
		if (host->state == state_read) {
			*(host->data++) = kw_read_reg(reg_data);
			host->len--;
			kw_write_reg(reg_isr, KW_I2C_IRQ_DATA);
			if (host->len == 0)
				host->state = state_stop;
			else if (host->len == 1)
				kw_write_reg(reg_control, 0);
		} else if (host->state == state_write) {
			ack = kw_read_reg(reg_status);
			if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
				DBG_LOW("KW: nack on data write\n");
				host->result = -EFBIG;
				host->state = state_stop;
			} else if (host->len) {
				kw_write_reg(reg_data, *(host->data++));
				host->len--;
			} else
				kw_i2c_do_stop(host, 0);
		} else {
			WRONG_STATE("KW_I2C_IRQ_DATA"); 
			if (host->state != state_stop)
				kw_i2c_do_stop(host, -EIO);
		}
		kw_write_reg(reg_isr, KW_I2C_IRQ_DATA);
	}

	if (isr & KW_I2C_IRQ_STOP) {
		kw_write_reg(reg_isr, KW_I2C_IRQ_STOP);
		if (host->state != state_stop) {
			WRONG_STATE("KW_I2C_IRQ_STOP");
			host->result = -EIO;
		}
		host->state = state_idle;
		if (!host->polled)
			complete(&host->complete);
	}

	/* Below should only happen in manual mode which we don't use ... */
	if (isr & KW_I2C_IRQ_START)
		kw_write_reg(reg_isr, KW_I2C_IRQ_START);

}

/* Interrupt handler */
static irqreturn_t kw_i2c_irq(int irq, void *dev_id)
{
	struct pmac_i2c_host_kw *host = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	del_timer(&host->timeout_timer);
	kw_i2c_handle_interrupt(host, kw_read_reg(reg_isr));
	if (host->state != state_idle) {
		host->timeout_timer.expires = jiffies + KW_POLL_TIMEOUT;
		add_timer(&host->timeout_timer);
	}
	spin_unlock_irqrestore(&host->lock, flags);
	return IRQ_HANDLED;
}

static void kw_i2c_timeout(unsigned long data)
{
	struct pmac_i2c_host_kw *host = (struct pmac_i2c_host_kw *)data;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * If the timer is pending, that means we raced with the
	 * irq, in which case we just return
	 */
	if (timer_pending(&host->timeout_timer))
		goto skip;

	kw_i2c_handle_interrupt(host, kw_read_reg(reg_isr));
	if (host->state != state_idle) {
		host->timeout_timer.expires = jiffies + KW_POLL_TIMEOUT;
		add_timer(&host->timeout_timer);
	}
 skip:
	spin_unlock_irqrestore(&host->lock, flags);
}

static int kw_i2c_open(struct pmac_i2c_bus *bus)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	mutex_lock(&host->mutex);
	return 0;
}

static void kw_i2c_close(struct pmac_i2c_bus *bus)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	mutex_unlock(&host->mutex);
}

static int kw_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
		       u32 subaddr, u8 *data, int len)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	u8 mode_reg = host->speed;
	int use_irq = host->irq && !bus->polled;

	/* Setup mode & subaddress if any */
	switch(bus->mode) {
	case pmac_i2c_mode_dumb:
		return -EINVAL;
	case pmac_i2c_mode_std:
		mode_reg |= KW_I2C_MODE_STANDARD;
		if (subsize != 0)
			return -EINVAL;
		break;
	case pmac_i2c_mode_stdsub:
		mode_reg |= KW_I2C_MODE_STANDARDSUB;
		if (subsize != 1)
			return -EINVAL;
		break;
	case pmac_i2c_mode_combined:
		mode_reg |= KW_I2C_MODE_COMBINED;
		if (subsize != 1)
			return -EINVAL;
		break;
	}

	/* Setup channel & clear pending irqs */
	kw_write_reg(reg_isr, kw_read_reg(reg_isr));
	kw_write_reg(reg_mode, mode_reg | (bus->channel << 4));
	kw_write_reg(reg_status, 0);

	/* Set up address and r/w bit, strip possible stale bus number from
	 * address top bits
	 */
	kw_write_reg(reg_addr, addrdir & 0xff);

	/* Set up the sub address */
	if ((mode_reg & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_STANDARDSUB
	    || (mode_reg & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_COMBINED)
		kw_write_reg(reg_subaddr, subaddr);

	/* Prepare for async operations */
	host->data = data;
	host->len = len;
	host->state = state_addr;
	host->result = 0;
	host->rw = (addrdir & 1);
	host->polled = bus->polled;

	/* Enable interrupt if not using polled mode and interrupt is
	 * available
	 */
	if (use_irq) {
		/* Clear completion */
		reinit_completion(&host->complete);
		/* Ack stale interrupts */
		kw_write_reg(reg_isr, kw_read_reg(reg_isr));
		/* Arm timeout */
		host->timeout_timer.expires = jiffies + KW_POLL_TIMEOUT;
		add_timer(&host->timeout_timer);
		/* Enable emission */
		kw_write_reg(reg_ier, KW_I2C_IRQ_MASK);
	}

	/* Start sending address */
	kw_write_reg(reg_control, KW_I2C_CTL_XADDR);

	/* Wait for completion */
	if (use_irq)
		wait_for_completion(&host->complete);
	else {
		while(host->state != state_idle) {
			unsigned long flags;

			u8 isr = kw_i2c_wait_interrupt(host);
			spin_lock_irqsave(&host->lock, flags);
			kw_i2c_handle_interrupt(host, isr);
			spin_unlock_irqrestore(&host->lock, flags);
		}
	}

	/* Disable emission */
	kw_write_reg(reg_ier, 0);

	return host->result;
}

static struct pmac_i2c_host_kw *__init kw_i2c_host_init(struct device_node *np)
{
	struct pmac_i2c_host_kw *host;
	const u32		*psteps, *prate, *addrp;
	u32			steps;

	host = kzalloc(sizeof(struct pmac_i2c_host_kw), GFP_KERNEL);
	if (host == NULL) {
		printk(KERN_ERR "low_i2c: Can't allocate host for %s\n",
		       np->full_name);
		return NULL;
	}

	/* Apple is kind enough to provide a valid AAPL,address property
	 * on all i2c keywest nodes so far ... we would have to fallback
	 * to macio parsing if that wasn't the case
	 */
	addrp = of_get_property(np, "AAPL,address", NULL);
	if (addrp == NULL) {
		printk(KERN_ERR "low_i2c: Can't find address for %s\n",
		       np->full_name);
		kfree(host);
		return NULL;
	}
	mutex_init(&host->mutex);
	init_completion(&host->complete);
	spin_lock_init(&host->lock);
	init_timer(&host->timeout_timer);
	host->timeout_timer.function = kw_i2c_timeout;
	host->timeout_timer.data = (unsigned long)host;

	psteps = of_get_property(np, "AAPL,address-step", NULL);
	steps = psteps ? (*psteps) : 0x10;
	for (host->bsteps = 0; (steps & 0x01) == 0; host->bsteps++)
		steps >>= 1;
	/* Select interface rate */
	host->speed = KW_I2C_MODE_25KHZ;
	prate = of_get_property(np, "AAPL,i2c-rate", NULL);
	if (prate) switch(*prate) {
	case 100:
		host->speed = KW_I2C_MODE_100KHZ;
		break;
	case 50:
		host->speed = KW_I2C_MODE_50KHZ;
		break;
	case 25:
		host->speed = KW_I2C_MODE_25KHZ;
		break;
	}	
	host->irq = irq_of_parse_and_map(np, 0);
	if (!host->irq)
		printk(KERN_WARNING
		       "low_i2c: Failed to map interrupt for %s\n",
		       np->full_name);

	host->base = ioremap((*addrp), 0x1000);
	if (host->base == NULL) {
		printk(KERN_ERR "low_i2c: Can't map registers for %s\n",
		       np->full_name);
		kfree(host);
		return NULL;
	}

	/* Make sure IRQ is disabled */
	kw_write_reg(reg_ier, 0);

	/* Request chip interrupt. We set IRQF_NO_SUSPEND because we don't
	 * want that interrupt disabled between the 2 passes of driver
	 * suspend or we'll have issues running the pfuncs
	 */
	if (request_irq(host->irq, kw_i2c_irq, IRQF_NO_SUSPEND,
			"keywest i2c", host))
		host->irq = 0;

	printk(KERN_INFO "KeyWest i2c @0x%08x irq %d %s\n",
	       *addrp, host->irq, np->full_name);

	return host;
}


static void __init kw_i2c_add(struct pmac_i2c_host_kw *host,
			      struct device_node *controller,
			      struct device_node *busnode,
			      int channel)
{
	struct pmac_i2c_bus *bus;

	bus = kzalloc(sizeof(struct pmac_i2c_bus), GFP_KERNEL);
	if (bus == NULL)
		return;

	bus->controller = of_node_get(controller);
	bus->busnode = of_node_get(busnode);
	bus->type = pmac_i2c_bus_keywest;
	bus->hostdata = host;
	bus->channel = channel;
	bus->mode = pmac_i2c_mode_std;
	bus->open = kw_i2c_open;
	bus->close = kw_i2c_close;
	bus->xfer = kw_i2c_xfer;
	mutex_init(&bus->mutex);
	if (controller == busnode)
		bus->flags = pmac_i2c_multibus;
	list_add(&bus->link, &pmac_i2c_busses);

	printk(KERN_INFO " channel %d bus %s\n", channel,
	       (controller == busnode) ? "<multibus>" : busnode->full_name);
}

static void __init kw_i2c_probe(void)
{
	struct device_node *np, *child, *parent;

	/* Probe keywest-i2c busses */
	for_each_compatible_node(np, "i2c","keywest-i2c") {
		struct pmac_i2c_host_kw *host;
		int multibus;

		/* Found one, init a host structure */
		host = kw_i2c_host_init(np);
		if (host == NULL)
			continue;

		/* Now check if we have a multibus setup (old style) or if we
		 * have proper bus nodes. Note that the "new" way (proper bus
		 * nodes) might cause us to not create some busses that are
		 * kept hidden in the device-tree. In the future, we might
		 * want to work around that by creating busses without a node
		 * but not for now
		 */
		child = of_get_next_child(np, NULL);
		multibus = !child || strcmp(child->name, "i2c-bus");
		of_node_put(child);

		/* For a multibus setup, we get the bus count based on the
		 * parent type
		 */
		if (multibus) {
			int chans, i;

			parent = of_get_parent(np);
			if (parent == NULL)
				continue;
			chans = parent->name[0] == 'u' ? 2 : 1;
			for (i = 0; i < chans; i++)
				kw_i2c_add(host, np, np, i);
		} else {
			for (child = NULL;
			     (child = of_get_next_child(np, child)) != NULL;) {
				const u32 *reg = of_get_property(child,
						"reg", NULL);
				if (reg == NULL)
					continue;
				kw_i2c_add(host, np, child, *reg);
			}
		}
	}
}


/*
 *
 * PMU implementation
 *
 */

#ifdef CONFIG_ADB_PMU

/*
 * i2c command block to the PMU
 */
struct pmu_i2c_hdr {
	u8	bus;
	u8	mode;
	u8	bus2;
	u8	address;
	u8	sub_addr;
	u8	comb_addr;
	u8	count;
	u8	data[];
};

static void pmu_i2c_complete(struct adb_request *req)
{
	complete(req->arg);
}

static int pmu_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
			u32 subaddr, u8 *data, int len)
{
	struct adb_request *req = bus->hostdata;
	struct pmu_i2c_hdr *hdr = (struct pmu_i2c_hdr *)&req->data[1];
	struct completion comp;
	int read = addrdir & 1;
	int retry;
	int rc = 0;

	/* For now, limit ourselves to 16 bytes transfers */
	if (len > 16)
		return -EINVAL;

	init_completion(&comp);

	for (retry = 0; retry < 16; retry++) {
		memset(req, 0, sizeof(struct adb_request));
		hdr->bus = bus->channel;
		hdr->count = len;

		switch(bus->mode) {
		case pmac_i2c_mode_std:
			if (subsize != 0)
				return -EINVAL;
			hdr->address = addrdir;
			hdr->mode = PMU_I2C_MODE_SIMPLE;
			break;
		case pmac_i2c_mode_stdsub:
		case pmac_i2c_mode_combined:
			if (subsize != 1)
				return -EINVAL;
			hdr->address = addrdir & 0xfe;
			hdr->comb_addr = addrdir;
			hdr->sub_addr = subaddr;
			if (bus->mode == pmac_i2c_mode_stdsub)
				hdr->mode = PMU_I2C_MODE_STDSUB;
			else
				hdr->mode = PMU_I2C_MODE_COMBINED;
			break;
		default:
			return -EINVAL;
		}

		reinit_completion(&comp);
		req->data[0] = PMU_I2C_CMD;
		req->reply[0] = 0xff;
		req->nbytes = sizeof(struct pmu_i2c_hdr) + 1;
		req->done = pmu_i2c_complete;
		req->arg = &comp;
		if (!read && len) {
			memcpy(hdr->data, data, len);
			req->nbytes += len;
		}
		rc = pmu_queue_request(req);
		if (rc)
			return rc;
		wait_for_completion(&comp);
		if (req->reply[0] == PMU_I2C_STATUS_OK)
			break;
		msleep(15);
	}
	if (req->reply[0] != PMU_I2C_STATUS_OK)
		return -EIO;

	for (retry = 0; retry < 16; retry++) {
		memset(req, 0, sizeof(struct adb_request));

		/* I know that looks like a lot, slow as hell, but darwin
		 * does it so let's be on the safe side for now
		 */
		msleep(15);

		hdr->bus = PMU_I2C_BUS_STATUS;

		reinit_completion(&comp);
		req->data[0] = PMU_I2C_CMD;
		req->reply[0] = 0xff;
		req->nbytes = 2;
		req->done = pmu_i2c_complete;
		req->arg = &comp;
		rc = pmu_queue_request(req);
		if (rc)
			return rc;
		wait_for_completion(&comp);

		if (req->reply[0] == PMU_I2C_STATUS_OK && !read)
			return 0;
		if (req->reply[0] == PMU_I2C_STATUS_DATAREAD && read) {
			int rlen = req->reply_len - 1;

			if (rlen != len) {
				printk(KERN_WARNING "low_i2c: PMU returned %d"
				       " bytes, expected %d !\n", rlen, len);
				return -EIO;
			}
			if (len)
				memcpy(data, &req->reply[1], len);
			return 0;
		}
	}
	return -EIO;
}

static void __init pmu_i2c_probe(void)
{
	struct pmac_i2c_bus *bus;
	struct device_node *busnode;
	int channel, sz;

	if (!pmu_present())
		return;

	/* There might or might not be a "pmu-i2c" node, we use that
	 * or via-pmu itself, whatever we find. I haven't seen a machine
	 * with separate bus nodes, so we assume a multibus setup
	 */
	busnode = of_find_node_by_name(NULL, "pmu-i2c");
	if (busnode == NULL)
		busnode = of_find_node_by_name(NULL, "via-pmu");
	if (busnode == NULL)
		return;

	printk(KERN_INFO "PMU i2c %s\n", busnode->full_name);

	/*
	 * We add bus 1 and 2 only for now, bus 0 is "special"
	 */
	for (channel = 1; channel <= 2; channel++) {
		sz = sizeof(struct pmac_i2c_bus) + sizeof(struct adb_request);
		bus = kzalloc(sz, GFP_KERNEL);
		if (bus == NULL)
			return;

		bus->controller = busnode;
		bus->busnode = busnode;
		bus->type = pmac_i2c_bus_pmu;
		bus->channel = channel;
		bus->mode = pmac_i2c_mode_std;
		bus->hostdata = bus + 1;
		bus->xfer = pmu_i2c_xfer;
		mutex_init(&bus->mutex);
		bus->flags = pmac_i2c_multibus;
		list_add(&bus->link, &pmac_i2c_busses);

		printk(KERN_INFO " channel %d bus <multibus>\n", channel);
	}
}

#endif /* CONFIG_ADB_PMU */


/*
 *
 * SMU implementation
 *
 */

#ifdef CONFIG_PMAC_SMU

static void smu_i2c_complete(struct smu_i2c_cmd *cmd, void *misc)
{
	complete(misc);
}

static int smu_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
			u32 subaddr, u8 *data, int len)
{
	struct smu_i2c_cmd *cmd = bus->hostdata;
	struct completion comp;
	int read = addrdir & 1;
	int rc = 0;

	if ((read && len > SMU_I2C_READ_MAX) ||
	    ((!read) && len > SMU_I2C_WRITE_MAX))
		return -EINVAL;

	memset(cmd, 0, sizeof(struct smu_i2c_cmd));
	cmd->info.bus = bus->channel;
	cmd->info.devaddr = addrdir;
	cmd->info.datalen = len;

	switch(bus->mode) {
	case pmac_i2c_mode_std:
		if (subsize != 0)
			return -EINVAL;
		cmd->info.type = SMU_I2C_TRANSFER_SIMPLE;
		break;
	case pmac_i2c_mode_stdsub:
	case pmac_i2c_mode_combined:
		if (subsize > 3 || subsize < 1)
			return -EINVAL;
		cmd->info.sublen = subsize;
		/* that's big-endian only but heh ! */
		memcpy(&cmd->info.subaddr, ((char *)&subaddr) + (4 - subsize),
		       subsize);
		if (bus->mode == pmac_i2c_mode_stdsub)
			cmd->info.type = SMU_I2C_TRANSFER_STDSUB;
		else
			cmd->info.type = SMU_I2C_TRANSFER_COMBINED;
		break;
	default:
		return -EINVAL;
	}
	if (!read && len)
		memcpy(cmd->info.data, data, len);

	init_completion(&comp);
	cmd->done = smu_i2c_complete;
	cmd->misc = &comp;
	rc = smu_queue_i2c(cmd);
	if (rc < 0)
		return rc;
	wait_for_completion(&comp);
	rc = cmd->status;

	if (read && len)
		memcpy(data, cmd->info.data, len);
	return rc < 0 ? rc : 0;
}

static void __init smu_i2c_probe(void)
{
	struct device_node *controller, *busnode;
	struct pmac_i2c_bus *bus;
	const u32 *reg;
	int sz;

	if (!smu_present())
		return;

	controller = of_find_node_by_name(NULL, "smu-i2c-control");
	if (controller == NULL)
		controller = of_find_node_by_name(NULL, "smu");
	if (controller == NULL)
		return;

	printk(KERN_INFO "SMU i2c %s\n", controller->full_name);

	/* Look for childs, note that they might not be of the right
	 * type as older device trees mix i2c busses and other things
	 * at the same level
	 */
	for (busnode = NULL;
	     (busnode = of_get_next_child(controller, busnode)) != NULL;) {
		if (strcmp(busnode->type, "i2c") &&
		    strcmp(busnode->type, "i2c-bus"))
			continue;
		reg = of_get_property(busnode, "reg", NULL);
		if (reg == NULL)
			continue;

		sz = sizeof(struct pmac_i2c_bus) + sizeof(struct smu_i2c_cmd);
		bus = kzalloc(sz, GFP_KERNEL);
		if (bus == NULL)
			return;

		bus->controller = controller;
		bus->busnode = of_node_get(busnode);
		bus->type = pmac_i2c_bus_smu;
		bus->channel = *reg;
		bus->mode = pmac_i2c_mode_std;
		bus->hostdata = bus + 1;
		bus->xfer = smu_i2c_xfer;
		mutex_init(&bus->mutex);
		bus->flags = 0;
		list_add(&bus->link, &pmac_i2c_busses);

		printk(KERN_INFO " channel %x bus %s\n",
		       bus->channel, busnode->full_name);
	}
}

#endif /* CONFIG_PMAC_SMU */

/*
 *
 * Core code
 *
 */


struct pmac_i2c_bus *pmac_i2c_find_bus(struct device_node *node)
{
	struct device_node *p = of_node_get(node);
	struct device_node *prev = NULL;
	struct pmac_i2c_bus *bus;

	while(p) {
		list_for_each_entry(bus, &pmac_i2c_busses, link) {
			if (p == bus->busnode) {
				if (prev && bus->flags & pmac_i2c_multibus) {
					const u32 *reg;
					reg = of_get_property(prev, "reg",
								NULL);
					if (!reg)
						continue;
					if (((*reg) >> 8) != bus->channel)
						continue;
				}
				of_node_put(p);
				of_node_put(prev);
				return bus;
			}
		}
		of_node_put(prev);
		prev = p;
		p = of_get_parent(p);
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(pmac_i2c_find_bus);

u8 pmac_i2c_get_dev_addr(struct device_node *device)
{
	const u32 *reg = of_get_property(device, "reg", NULL);

	if (reg == NULL)
		return 0;

	return (*reg) & 0xff;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_dev_addr);

struct device_node *pmac_i2c_get_controller(struct pmac_i2c_bus *bus)
{
	return bus->controller;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_controller);

struct device_node *pmac_i2c_get_bus_node(struct pmac_i2c_bus *bus)
{
	return bus->busnode;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_bus_node);

int pmac_i2c_get_type(struct pmac_i2c_bus *bus)
{
	return bus->type;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_type);

int pmac_i2c_get_flags(struct pmac_i2c_bus *bus)
{
	return bus->flags;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_flags);

int pmac_i2c_get_channel(struct pmac_i2c_bus *bus)
{
	return bus->channel;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_channel);


struct i2c_adapter *pmac_i2c_get_adapter(struct pmac_i2c_bus *bus)
{
	return &bus->adapter;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_adapter);

struct pmac_i2c_bus *pmac_i2c_adapter_to_bus(struct i2c_adapter *adapter)
{
	struct pmac_i2c_bus *bus;

	list_for_each_entry(bus, &pmac_i2c_busses, link)
		if (&bus->adapter == adapter)
			return bus;
	return NULL;
}
EXPORT_SYMBOL_GPL(pmac_i2c_adapter_to_bus);

int pmac_i2c_match_adapter(struct device_node *dev, struct i2c_adapter *adapter)
{
	struct pmac_i2c_bus *bus = pmac_i2c_find_bus(dev);

	if (bus == NULL)
		return 0;
	return (&bus->adapter == adapter);
}
EXPORT_SYMBOL_GPL(pmac_i2c_match_adapter);

int pmac_low_i2c_lock(struct device_node *np)
{
	struct pmac_i2c_bus *bus, *found = NULL;

	list_for_each_entry(bus, &pmac_i2c_busses, link) {
		if (np == bus->controller) {
			found = bus;
			break;
		}
	}
	if (!found)
		return -ENODEV;
	return pmac_i2c_open(bus, 0);
}
EXPORT_SYMBOL_GPL(pmac_low_i2c_lock);

int pmac_low_i2c_unlock(struct device_node *np)
{
	struct pmac_i2c_bus *bus, *found = NULL;

	list_for_each_entry(bus, &pmac_i2c_busses, link) {
		if (np == bus->controller) {
			found = bus;
			break;
		}
	}
	if (!found)
		return -ENODEV;
	pmac_i2c_close(bus);
	return 0;
}
EXPORT_SYMBOL_GPL(pmac_low_i2c_unlock);


int pmac_i2c_open(struct pmac_i2c_bus *bus, int polled)
{
	int rc;

	mutex_lock(&bus->mutex);
	bus->polled = polled || pmac_i2c_force_poll;
	bus->opened = 1;
	bus->mode = pmac_i2c_mode_std;
	if (bus->open && (rc = bus->open(bus)) != 0) {
		bus->opened = 0;
		mutex_unlock(&bus->mutex);
		return rc;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(pmac_i2c_open);

void pmac_i2c_close(struct pmac_i2c_bus *bus)
{
	WARN_ON(!bus->opened);
	if (bus->close)
		bus->close(bus);
	bus->opened = 0;
	mutex_unlock(&bus->mutex);
}
EXPORT_SYMBOL_GPL(pmac_i2c_close);

int pmac_i2c_setmode(struct pmac_i2c_bus *bus, int mode)
{
	WARN_ON(!bus->opened);

	/* Report me if you see the error below as there might be a new
	 * "combined4" mode that I need to implement for the SMU bus
	 */
	if (mode < pmac_i2c_mode_dumb || mode > pmac_i2c_mode_combined) {
		printk(KERN_ERR "low_i2c: Invalid mode %d requested on"
		       " bus %s !\n", mode, bus->busnode->full_name);
		return -EINVAL;
	}
	bus->mode = mode;

	return 0;
}
EXPORT_SYMBOL_GPL(pmac_i2c_setmode);

int pmac_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
		  u32 subaddr, u8 *data, int len)
{
	int rc;

	WARN_ON(!bus->opened);

	DBG("xfer() chan=%d, addrdir=0x%x, mode=%d, subsize=%d, subaddr=0x%x,"
	    " %d bytes, bus %s\n", bus->channel, addrdir, bus->mode, subsize,
	    subaddr, len, bus->busnode->full_name);

	rc = bus->xfer(bus, addrdir, subsize, subaddr, data, len);

#ifdef DEBUG
	if (rc)
		DBG("xfer error %d\n", rc);
#endif
	return rc;
}
EXPORT_SYMBOL_GPL(pmac_i2c_xfer);

/* some quirks for platform function decoding */
enum {
	pmac_i2c_quirk_invmask = 0x00000001u,
	pmac_i2c_quirk_skip = 0x00000002u,
};

static void pmac_i2c_devscan(void (*callback)(struct device_node *dev,
					      int quirks))
{
	struct pmac_i2c_bus *bus;
	struct device_node *np;
	static struct whitelist_ent {
		char *name;
		char *compatible;
		int quirks;
	} whitelist[] = {
		/* XXX Study device-tree's & apple drivers are get the quirks
		 * right !
		 */
		/* Workaround: It seems that running the clockspreading
		 * properties on the eMac will cause lockups during boot.
		 * The machine seems to work fine without that. So for now,
		 * let's make sure i2c-hwclock doesn't match about "imic"
		 * clocks and we'll figure out if we really need to do
		 * something special about those later.
		 */
		{ "i2c-hwclock", "imic5002", pmac_i2c_quirk_skip },
		{ "i2c-hwclock", "imic5003", pmac_i2c_quirk_skip },
		{ "i2c-hwclock", NULL, pmac_i2c_quirk_invmask },
		{ "i2c-cpu-voltage", NULL, 0},
		{  "temp-monitor", NULL, 0 },
		{  "supply-monitor", NULL, 0 },
		{ NULL, NULL, 0 },
	};

	/* Only some devices need to have platform functions instanciated
	 * here. For now, we have a table. Others, like 9554 i2c GPIOs used
	 * on Xserve, if we ever do a driver for them, will use their own
	 * platform function instance
	 */
	list_for_each_entry(bus, &pmac_i2c_busses, link) {
		for (np = NULL;
		     (np = of_get_next_child(bus->busnode, np)) != NULL;) {
			struct whitelist_ent *p;
			/* If multibus, check if device is on that bus */
			if (bus->flags & pmac_i2c_multibus)
				if (bus != pmac_i2c_find_bus(np))
					continue;
			for (p = whitelist; p->name != NULL; p++) {
				if (strcmp(np->name, p->name))
					continue;
				if (p->compatible &&
				    !of_device_is_compatible(np, p->compatible))
					continue;
				if (p->quirks & pmac_i2c_quirk_skip)
					break;
				callback(np, p->quirks);
				break;
			}
		}
	}
}

#define MAX_I2C_DATA	64

struct pmac_i2c_pf_inst
{
	struct pmac_i2c_bus	*bus;
	u8			addr;
	u8			buffer[MAX_I2C_DATA];
	u8			scratch[MAX_I2C_DATA];
	int			bytes;
	int			quirks;
};

static void* pmac_i2c_do_begin(struct pmf_function *func, struct pmf_args *args)
{
	struct pmac_i2c_pf_inst *inst;
	struct pmac_i2c_bus	*bus;

	bus = pmac_i2c_find_bus(func->node);
	if (bus == NULL) {
		printk(KERN_ERR "low_i2c: Can't find bus for %s (pfunc)\n",
		       func->node->full_name);
		return NULL;
	}
	if (pmac_i2c_open(bus, 0)) {
		printk(KERN_ERR "low_i2c: Can't open i2c bus for %s (pfunc)\n",
		       func->node->full_name);
		return NULL;
	}

	/* XXX might need GFP_ATOMIC when called during the suspend process,
	 * but then, there are already lots of issues with suspending when
	 * near OOM that need to be resolved, the allocator itself should
	 * probably make GFP_NOIO implicit during suspend
	 */
	inst = kzalloc(sizeof(struct pmac_i2c_pf_inst), GFP_KERNEL);
	if (inst == NULL) {
		pmac_i2c_close(bus);
		return NULL;
	}
	inst->bus = bus;
	inst->addr = pmac_i2c_get_dev_addr(func->node);
	inst->quirks = (int)(long)func->driver_data;
	return inst;
}

static void pmac_i2c_do_end(struct pmf_function *func, void *instdata)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	if (inst == NULL)
		return;
	pmac_i2c_close(inst->bus);
	kfree(inst);
}

static int pmac_i2c_do_read(PMF_STD_ARGS, u32 len)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	inst->bytes = len;
	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_read, 0, 0,
			     inst->buffer, len);
}

static int pmac_i2c_do_write(PMF_STD_ARGS, u32 len, const u8 *data)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_write, 0, 0,
			     (u8 *)data, len);
}

/* This function is used to do the masking & OR'ing for the "rmw" type
 * callbacks. Ze should apply the mask and OR in the values in the
 * buffer before writing back. The problem is that it seems that
 * various darwin drivers implement the mask/or differently, thus
 * we need to check the quirks first
 */
static void pmac_i2c_do_apply_rmw(struct pmac_i2c_pf_inst *inst,
				  u32 len, const u8 *mask, const u8 *val)
{
	int i;

	if (inst->quirks & pmac_i2c_quirk_invmask) {
		for (i = 0; i < len; i ++)
			inst->scratch[i] = (inst->buffer[i] & mask[i]) | val[i];
	} else {
		for (i = 0; i < len; i ++)
			inst->scratch[i] = (inst->buffer[i] & ~mask[i])
				| (val[i] & mask[i]);
	}
}

static int pmac_i2c_do_rmw(PMF_STD_ARGS, u32 masklen, u32 valuelen,
			   u32 totallen, const u8 *maskdata,
			   const u8 *valuedata)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	if (masklen > inst->bytes || valuelen > inst->bytes ||
	    totallen > inst->bytes || valuelen > masklen)
		return -EINVAL;

	pmac_i2c_do_apply_rmw(inst, masklen, maskdata, valuedata);

	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_write, 0, 0,
			     inst->scratch, totallen);
}

static int pmac_i2c_do_read_sub(PMF_STD_ARGS, u8 subaddr, u32 len)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	inst->bytes = len;
	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_read, 1, subaddr,
			     inst->buffer, len);
}

static int pmac_i2c_do_write_sub(PMF_STD_ARGS, u8 subaddr, u32 len,
				     const u8 *data)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_write, 1,
			     subaddr, (u8 *)data, len);
}

static int pmac_i2c_do_set_mode(PMF_STD_ARGS, int mode)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	return pmac_i2c_setmode(inst->bus, mode);
}

static int pmac_i2c_do_rmw_sub(PMF_STD_ARGS, u8 subaddr, u32 masklen,
			       u32 valuelen, u32 totallen, const u8 *maskdata,
			       const u8 *valuedata)
{
	struct pmac_i2c_pf_inst *inst = instdata;

	if (masklen > inst->bytes || valuelen > inst->bytes ||
	    totallen > inst->bytes || valuelen > masklen)
		return -EINVAL;

	pmac_i2c_do_apply_rmw(inst, masklen, maskdata, valuedata);

	return pmac_i2c_xfer(inst->bus, inst->addr | pmac_i2c_write, 1,
			     subaddr, inst->scratch, totallen);
}

static int pmac_i2c_do_mask_and_comp(PMF_STD_ARGS, u32 len,
				     const u8 *maskdata,
				     const u8 *valuedata)
{
	struct pmac_i2c_pf_inst *inst = instdata;
	int i, match;

	/* Get return value pointer, it's assumed to be a u32 */
	if (!args || !args->count || !args->u[0].p)
		return -EINVAL;

	/* Check buffer */
	if (len > inst->bytes)
		return -EINVAL;

	for (i = 0, match = 1; match && i < len; i ++)
		if ((inst->buffer[i] & maskdata[i]) != valuedata[i])
			match = 0;
	*args->u[0].p = match;
	return 0;
}

static int pmac_i2c_do_delay(PMF_STD_ARGS, u32 duration)
{
	msleep((duration + 999) / 1000);
	return 0;
}


static struct pmf_handlers pmac_i2c_pfunc_handlers = {
	.begin			= pmac_i2c_do_begin,
	.end			= pmac_i2c_do_end,
	.read_i2c		= pmac_i2c_do_read,
	.write_i2c		= pmac_i2c_do_write,
	.rmw_i2c		= pmac_i2c_do_rmw,
	.read_i2c_sub		= pmac_i2c_do_read_sub,
	.write_i2c_sub		= pmac_i2c_do_write_sub,
	.rmw_i2c_sub		= pmac_i2c_do_rmw_sub,
	.set_i2c_mode		= pmac_i2c_do_set_mode,
	.mask_and_compare	= pmac_i2c_do_mask_and_comp,
	.delay			= pmac_i2c_do_delay,
};

static void __init pmac_i2c_dev_create(struct device_node *np, int quirks)
{
	DBG("dev_create(%s)\n", np->full_name);

	pmf_register_driver(np, &pmac_i2c_pfunc_handlers,
			    (void *)(long)quirks);
}

static void __init pmac_i2c_dev_init(struct device_node *np, int quirks)
{
	DBG("dev_create(%s)\n", np->full_name);

	pmf_do_functions(np, NULL, 0, PMF_FLAGS_ON_INIT, NULL);
}

static void pmac_i2c_dev_suspend(struct device_node *np, int quirks)
{
	DBG("dev_suspend(%s)\n", np->full_name);
	pmf_do_functions(np, NULL, 0, PMF_FLAGS_ON_SLEEP, NULL);
}

static void pmac_i2c_dev_resume(struct device_node *np, int quirks)
{
	DBG("dev_resume(%s)\n", np->full_name);
	pmf_do_functions(np, NULL, 0, PMF_FLAGS_ON_WAKE, NULL);
}

void pmac_pfunc_i2c_suspend(void)
{
	pmac_i2c_devscan(pmac_i2c_dev_suspend);
}

void pmac_pfunc_i2c_resume(void)
{
	pmac_i2c_devscan(pmac_i2c_dev_resume);
}

/*
 * Initialize us: probe all i2c busses on the machine, instantiate
 * busses and platform functions as needed.
 */
/* This is non-static as it might be called early by smp code */
int __init pmac_i2c_init(void)
{
	static int i2c_inited;

	if (i2c_inited)
		return 0;
	i2c_inited = 1;

	/* Probe keywest-i2c busses */
	kw_i2c_probe();

#ifdef CONFIG_ADB_PMU
	/* Probe PMU i2c busses */
	pmu_i2c_probe();
#endif

#ifdef CONFIG_PMAC_SMU
	/* Probe SMU i2c busses */
	smu_i2c_probe();
#endif

	/* Now add plaform functions for some known devices */
	pmac_i2c_devscan(pmac_i2c_dev_create);

	return 0;
}
machine_arch_initcall(powermac, pmac_i2c_init);

/* Since pmac_i2c_init can be called too early for the platform device
 * registration, we need to do it at a later time. In our case, subsys
 * happens to fit well, though I agree it's a bit of a hack...
 */
static int __init pmac_i2c_create_platform_devices(void)
{
	struct pmac_i2c_bus *bus;
	int i = 0;

	/* In the case where we are initialized from smp_init(), we must
	 * not use the timer (and thus the irq). It's safe from now on
	 * though
	 */
	pmac_i2c_force_poll = 0;

	/* Create platform devices */
	list_for_each_entry(bus, &pmac_i2c_busses, link) {
		bus->platform_dev =
			platform_device_alloc("i2c-powermac", i++);
		if (bus->platform_dev == NULL)
			return -ENOMEM;
		bus->platform_dev->dev.platform_data = bus;
		bus->platform_dev->dev.of_node = bus->busnode;
		platform_device_add(bus->platform_dev);
	}

	/* Now call platform "init" functions */
	pmac_i2c_devscan(pmac_i2c_dev_init);

	return 0;
}
machine_subsys_initcall(powermac, pmac_i2c_create_platform_devices);
