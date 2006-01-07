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
 * banging busses found on older chipstes in earlier machines if we ever need
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

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/smu.h>
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
	struct i2c_adapter	*adapter;
	void			*hostdata;
	int			channel;	/* some hosts have multiple */
	int			mode;		/* current mode */
	struct semaphore	sem;
	int			opened;
	int			polled;		/* open mode */

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
	struct semaphore	mutex;		/* Access mutex for use by
						 * i2c-keywest */
	void __iomem		*base;		/* register base address */
	int			bsteps;		/* register stepping */
	int			speed;		/* speed */
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
		printk(KERN_DEBUG "KW: wrong state. Got %s, state: %s (isr: %02x)\n", \
		       name, __kw_state_names[state], isr); \
	} while(0)

static const char *__kw_state_names[] = {
	"state_idle",
	"state_addr",
	"state_read",
	"state_write",
	"state_stop",
	"state_dead"
};

static inline u8 __kw_read_reg(struct pmac_i2c_bus *bus, reg_t reg)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	return readb(host->base + (((unsigned int)reg) << host->bsteps));
}

static inline void __kw_write_reg(struct pmac_i2c_bus *bus, reg_t reg, u8 val)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	writeb(val, host->base + (((unsigned)reg) << host->bsteps));
	(void)__kw_read_reg(bus, reg_subaddr);
}

#define kw_write_reg(reg, val)	__kw_write_reg(bus, reg, val)
#define kw_read_reg(reg)	__kw_read_reg(bus, reg)

static u8 kw_i2c_wait_interrupt(struct pmac_i2c_bus* bus)
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
		if (bus->polled) {
			for (j = 1; j < 1000000; j++)
				mb();
		} else
			msleep(1);
	}
	return isr;
}

static int kw_i2c_handle_interrupt(struct pmac_i2c_bus *bus, int state, int rw,
				   int *rc, u8 **data, int *len, u8 isr)
{
	u8 ack;

	DBG_LOW("kw_handle_interrupt(%s, isr: %x)\n",
		__kw_state_names[state], isr);

	if (isr == 0) {
		if (state != state_stop) {
			DBG_LOW("KW: Timeout !\n");
			*rc = -EIO;
			goto stop;
		}
		if (state == state_stop) {
			ack = kw_read_reg(reg_status);
			if (!(ack & KW_I2C_STAT_BUSY)) {
				state = state_idle;
				kw_write_reg(reg_ier, 0x00);
			}
		}
		return state;
	}

	if (isr & KW_I2C_IRQ_ADDR) {
		ack = kw_read_reg(reg_status);
		if (state != state_addr) {
			kw_write_reg(reg_isr, KW_I2C_IRQ_ADDR);
			WRONG_STATE("KW_I2C_IRQ_ADDR"); 
			*rc = -EIO;
			goto stop;
		}
		if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
			*rc = -ENODEV;
			DBG_LOW("KW: NAK on address\n");
			return state_stop;		     
		} else {
			if (rw) {
				state = state_read;
				if (*len > 1)
					kw_write_reg(reg_control,
						     KW_I2C_CTL_AAK);
			} else {
				state = state_write;
				kw_write_reg(reg_data, **data);
				(*data)++; (*len)--;
			}
		}
		kw_write_reg(reg_isr, KW_I2C_IRQ_ADDR);
	}

	if (isr & KW_I2C_IRQ_DATA) {
		if (state == state_read) {
			**data = kw_read_reg(reg_data);
			(*data)++; (*len)--;
			kw_write_reg(reg_isr, KW_I2C_IRQ_DATA);
			if ((*len) == 0)
				state = state_stop;
			else if ((*len) == 1)
				kw_write_reg(reg_control, 0);
		} else if (state == state_write) {
			ack = kw_read_reg(reg_status);
			if ((ack & KW_I2C_STAT_LAST_AAK) == 0) {
				DBG_LOW("KW: nack on data write\n");
				*rc = -EIO;
				goto stop;
			} else if (*len) {
				kw_write_reg(reg_data, **data);
				(*data)++; (*len)--;
			} else {
				kw_write_reg(reg_control, KW_I2C_CTL_STOP);
				state = state_stop;
				*rc = 0;
			}
			kw_write_reg(reg_isr, KW_I2C_IRQ_DATA);
		} else {
			kw_write_reg(reg_isr, KW_I2C_IRQ_DATA);
			WRONG_STATE("KW_I2C_IRQ_DATA"); 
			if (state != state_stop) {
				*rc = -EIO;
				goto stop;
			}
		}
	}

	if (isr & KW_I2C_IRQ_STOP) {
		kw_write_reg(reg_isr, KW_I2C_IRQ_STOP);
		if (state != state_stop) {
			WRONG_STATE("KW_I2C_IRQ_STOP");
			*rc = -EIO;
		}
		return state_idle;
	}

	if (isr & KW_I2C_IRQ_START)
		kw_write_reg(reg_isr, KW_I2C_IRQ_START);

	return state;

 stop:
	kw_write_reg(reg_control, KW_I2C_CTL_STOP);	
	return state_stop;
}

static int kw_i2c_open(struct pmac_i2c_bus *bus)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	down(&host->mutex);
	return 0;
}

static void kw_i2c_close(struct pmac_i2c_bus *bus)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	up(&host->mutex);
}

static int kw_i2c_xfer(struct pmac_i2c_bus *bus, u8 addrdir, int subsize,
		       u32 subaddr, u8 *data, int len)
{
	struct pmac_i2c_host_kw *host = bus->hostdata;
	u8 mode_reg = host->speed;
	int state = state_addr;
	int rc = 0;

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

	/* Start sending address & disable interrupt*/
	kw_write_reg(reg_ier, 0 /*KW_I2C_IRQ_MASK*/);
	kw_write_reg(reg_control, KW_I2C_CTL_XADDR);

	/* State machine, to turn into an interrupt handler in the future */
	while(state != state_idle) {
		u8 isr = kw_i2c_wait_interrupt(bus);
		state = kw_i2c_handle_interrupt(bus, state, addrdir & 1, &rc,
						&data, &len, isr);
	}

	return rc;
}

static struct pmac_i2c_host_kw *__init kw_i2c_host_init(struct device_node *np)
{
	struct pmac_i2c_host_kw *host;
	u32			*psteps, *prate, *addrp, steps;

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
	addrp = (u32 *)get_property(np, "AAPL,address", NULL);
	if (addrp == NULL) {
		printk(KERN_ERR "low_i2c: Can't find address for %s\n",
		       np->full_name);
		kfree(host);
		return NULL;
	}
	init_MUTEX(&host->mutex);
	psteps = (u32 *)get_property(np, "AAPL,address-step", NULL);
	steps = psteps ? (*psteps) : 0x10;
	for (host->bsteps = 0; (steps & 0x01) == 0; host->bsteps++)
		steps >>= 1;
	/* Select interface rate */
	host->speed = KW_I2C_MODE_25KHZ;
	prate = (u32 *)get_property(np, "AAPL,i2c-rate", NULL);
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

	printk(KERN_INFO "KeyWest i2c @0x%08x %s\n", *addrp, np->full_name);
	host->base = ioremap((*addrp), 0x1000);

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
	init_MUTEX(&bus->sem);
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
	for (np = NULL;
	     (np = of_find_compatible_node(np, "i2c","keywest-i2c")) != NULL;){
		struct pmac_i2c_host_kw *host;
		int multibus, chans, i;

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
			parent = of_get_parent(np);
			if (parent == NULL)
				continue;
			chans = parent->name[0] == 'u' ? 2 : 1;
			for (i = 0; i < chans; i++)
				kw_i2c_add(host, np, np, i);
		} else {
			for (child = NULL;
			     (child = of_get_next_child(np, child)) != NULL;) {
				u32 *reg =
					(u32 *)get_property(child, "reg", NULL);
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

		INIT_COMPLETION(comp);
		req->data[0] = PMU_I2C_CMD;
		req->reply[0] = 0xff;
		req->nbytes = sizeof(struct pmu_i2c_hdr) + 1;
		req->done = pmu_i2c_complete;
		req->arg = &comp;
		if (!read) {
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

		INIT_COMPLETION(comp);
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
		init_MUTEX(&bus->sem);
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
	if (!read)
		memcpy(cmd->info.data, data, len);

	init_completion(&comp);
	cmd->done = smu_i2c_complete;
	cmd->misc = &comp;
	rc = smu_queue_i2c(cmd);
	if (rc < 0)
		return rc;
	wait_for_completion(&comp);
	rc = cmd->status;

	if (read)
		memcpy(data, cmd->info.data, len);
	return rc < 0 ? rc : 0;
}

static void __init smu_i2c_probe(void)
{
	struct device_node *controller, *busnode;
	struct pmac_i2c_bus *bus;
	u32 *reg;
	int sz;

	if (!smu_present())
		return;

	controller = of_find_node_by_name(NULL, "smu_i2c_control");
	if (controller == NULL)
		controller = of_find_node_by_name(NULL, "smu");
	if (controller == NULL)
		return;

	printk(KERN_INFO "SMU i2c %s\n", controller->full_name);

	/* Look for childs, note that they might not be of the right
	 * type as older device trees mix i2c busses and other thigns
	 * at the same level
	 */
	for (busnode = NULL;
	     (busnode = of_get_next_child(controller, busnode)) != NULL;) {
		if (strcmp(busnode->type, "i2c") &&
		    strcmp(busnode->type, "i2c-bus"))
			continue;
		reg = (u32 *)get_property(busnode, "reg", NULL);
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
		init_MUTEX(&bus->sem);
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
					u32 *reg;
					reg = (u32 *)get_property(prev, "reg",
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
	u32 *reg = (u32 *)get_property(device, "reg", NULL);

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

void pmac_i2c_attach_adapter(struct pmac_i2c_bus *bus,
			     struct i2c_adapter *adapter)
{
	WARN_ON(bus->adapter != NULL);
	bus->adapter = adapter;
}
EXPORT_SYMBOL_GPL(pmac_i2c_attach_adapter);

void pmac_i2c_detach_adapter(struct pmac_i2c_bus *bus,
			     struct i2c_adapter *adapter)
{
	WARN_ON(bus->adapter != adapter);
	bus->adapter = NULL;
}
EXPORT_SYMBOL_GPL(pmac_i2c_detach_adapter);

struct i2c_adapter *pmac_i2c_get_adapter(struct pmac_i2c_bus *bus)
{
	return bus->adapter;
}
EXPORT_SYMBOL_GPL(pmac_i2c_get_adapter);

extern int pmac_i2c_match_adapter(struct device_node *dev,
				  struct i2c_adapter *adapter)
{
	struct pmac_i2c_bus *bus = pmac_i2c_find_bus(dev);

	if (bus == NULL)
		return 0;
	return (bus->adapter == adapter);
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

	down(&bus->sem);
	bus->polled = polled;
	bus->opened = 1;
	bus->mode = pmac_i2c_mode_std;
	if (bus->open && (rc = bus->open(bus)) != 0) {
		bus->opened = 0;
		up(&bus->sem);
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
	up(&bus->sem);
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

/*
 * Initialize us: probe all i2c busses on the machine and instantiate
 * busses.
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
	pmu_i2c_probe();
#endif

#ifdef CONFIG_PMAC_SMU
	smu_i2c_probe();
#endif

	return 0;
}
arch_initcall(pmac_i2c_init);

