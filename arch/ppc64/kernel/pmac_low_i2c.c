/*
 *  arch/ppc/platforms/pmac_low_i2c.c
 *
 *  Copyright (C) 2003 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  This file contains some low-level i2c access routines that
 *  need to be used by various bits of the PowerMac platform code
 *  at times where the real asynchronous & interrupt driven driver
 *  cannot be used. The API borrows some semantics from the darwin
 *  driver in order to ease the implementation of the platform
 *  properties parser
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <asm/keylargo.h>
#include <asm/uninorth.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/pmac_low_i2c.h>

#define MAX_LOW_I2C_HOST	4

#ifdef DEBUG
#define DBG(x...) do {\
		printk(KERN_DEBUG "KW:" x);	\
	} while(0)
#else
#define DBG(x...)
#endif

struct low_i2c_host;

typedef int (*low_i2c_func_t)(struct low_i2c_host *host, u8 addr, u8 sub, u8 *data, int len);

struct low_i2c_host
{
	struct device_node	*np;		/* OF device node */
	struct semaphore	mutex;		/* Access mutex for use by i2c-keywest */
	low_i2c_func_t		func;		/* Access function */
	unsigned int		is_open : 1;	/* Poor man's access control */
	int			mode;		/* Current mode */
	int			channel;	/* Current channel */
	int			num_channels;	/* Number of channels */
	void __iomem		*base;		/* For keywest-i2c, base address */
	int			bsteps;		/* And register stepping */
	int			speed;		/* And speed */
};

static struct low_i2c_host	low_i2c_hosts[MAX_LOW_I2C_HOST];

/* No locking is necessary on allocation, we are running way before
 * anything can race with us
 */
static struct low_i2c_host *find_low_i2c_host(struct device_node *np)
{
	int i;

	for (i = 0; i < MAX_LOW_I2C_HOST; i++)
		if (low_i2c_hosts[i].np == np)
			return &low_i2c_hosts[i];
	return NULL;
}

/*
 *
 * i2c-keywest implementation (UniNorth, U2, U3, Keylargo's)
 *
 */

/*
 * Keywest i2c definitions borrowed from drivers/i2c/i2c-keywest.h,
 * should be moved somewhere in include/asm-ppc/
 */
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

static inline u8 __kw_read_reg(struct low_i2c_host *host, reg_t reg)
{
	return readb(host->base + (((unsigned int)reg) << host->bsteps));
}

static inline void __kw_write_reg(struct low_i2c_host *host, reg_t reg, u8 val)
{
	writeb(val, host->base + (((unsigned)reg) << host->bsteps));
	(void)__kw_read_reg(host, reg_subaddr);
}

#define kw_write_reg(reg, val)	__kw_write_reg(host, reg, val) 
#define kw_read_reg(reg)	__kw_read_reg(host, reg) 


/* Don't schedule, the g5 fan controller is too
 * timing sensitive
 */
static u8 kw_wait_interrupt(struct low_i2c_host* host)
{
	int i, j;
	u8 isr;
	
	for (i = 0; i < 100000; i++) {
		isr = kw_read_reg(reg_isr) & KW_I2C_IRQ_MASK;
		if (isr != 0)
			return isr;

		/* This code is used with the timebase frozen, we cannot rely
		 * on udelay ! For now, just use a bogus loop
		 */
		for (j = 1; j < 10000; j++)
			mb();
	}
	return isr;
}

static int kw_handle_interrupt(struct low_i2c_host *host, int state, int rw, int *rc, u8 **data, int *len, u8 isr)
{
	u8 ack;

	DBG("kw_handle_interrupt(%s, isr: %x)\n", __kw_state_names[state], isr);

	if (isr == 0) {
		if (state != state_stop) {
			DBG("KW: Timeout !\n");
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
			DBG("KW: NAK on address\n");
			return state_stop;		     
		} else {
			if (rw) {
				state = state_read;
				if (*len > 1)
					kw_write_reg(reg_control, KW_I2C_CTL_AAK);
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
				DBG("KW: nack on data write\n");
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

static int keywest_low_i2c_func(struct low_i2c_host *host, u8 addr, u8 subaddr, u8 *data, int len)
{
	u8 mode_reg = host->speed;
	int state = state_addr;
	int rc = 0;

	/* Setup mode & subaddress if any */
	switch(host->mode) {
	case pmac_low_i2c_mode_dumb:
		printk(KERN_ERR "low_i2c: Dumb mode not supported !\n");
		return -EINVAL;
	case pmac_low_i2c_mode_std:
		mode_reg |= KW_I2C_MODE_STANDARD;
		break;
	case pmac_low_i2c_mode_stdsub:
		mode_reg |= KW_I2C_MODE_STANDARDSUB;
		break;
	case pmac_low_i2c_mode_combined:
		mode_reg |= KW_I2C_MODE_COMBINED;
		break;
	}

	/* Setup channel & clear pending irqs */
	kw_write_reg(reg_isr, kw_read_reg(reg_isr));
	kw_write_reg(reg_mode, mode_reg | (host->channel << 4));
	kw_write_reg(reg_status, 0);

	/* Set up address and r/w bit */
	kw_write_reg(reg_addr, addr);

	/* Set up the sub address */
	if ((mode_reg & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_STANDARDSUB
	    || (mode_reg & KW_I2C_MODE_MODE_MASK) == KW_I2C_MODE_COMBINED)
		kw_write_reg(reg_subaddr, subaddr);

	/* Start sending address & disable interrupt*/
	kw_write_reg(reg_ier, 0 /*KW_I2C_IRQ_MASK*/);
	kw_write_reg(reg_control, KW_I2C_CTL_XADDR);

	/* State machine, to turn into an interrupt handler */
	while(state != state_idle) {
		u8 isr = kw_wait_interrupt(host);
		state = kw_handle_interrupt(host, state, addr & 1, &rc, &data, &len, isr);
	}

	return rc;
}

static void keywest_low_i2c_add(struct device_node *np)
{
	struct low_i2c_host	*host = find_low_i2c_host(NULL);
	u32			*psteps, *prate, steps, aoffset = 0;
	struct device_node	*parent;

	if (host == NULL) {
		printk(KERN_ERR "low_i2c: Can't allocate host for %s\n",
		       np->full_name);
		return;
	}
	memset(host, 0, sizeof(*host));

	init_MUTEX(&host->mutex);
	host->np = of_node_get(np);	
	psteps = (u32 *)get_property(np, "AAPL,address-step", NULL);
	steps = psteps ? (*psteps) : 0x10;
	for (host->bsteps = 0; (steps & 0x01) == 0; host->bsteps++)
		steps >>= 1;
	parent = of_get_parent(np);
	host->num_channels = 1;
	if (parent && parent->name[0] == 'u') {
		host->num_channels = 2;
		aoffset = 3;
	}
	/* Select interface rate */
	host->speed = KW_I2C_MODE_100KHZ;
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

	host->mode = pmac_low_i2c_mode_std;
	host->base = ioremap(np->addrs[0].address + aoffset,
						np->addrs[0].size);
	host->func = keywest_low_i2c_func;
}

/*
 *
 * PMU implementation
 *
 */


#ifdef CONFIG_ADB_PMU

static int pmu_low_i2c_func(struct low_i2c_host *host, u8 addr, u8 sub, u8 *data, int len)
{
	// TODO
	return -ENODEV;
}

static void pmu_low_i2c_add(struct device_node *np)
{
	struct low_i2c_host	*host = find_low_i2c_host(NULL);

	if (host == NULL) {
		printk(KERN_ERR "low_i2c: Can't allocate host for %s\n",
		       np->full_name);
		return;
	}
	memset(host, 0, sizeof(*host));

	init_MUTEX(&host->mutex);
	host->np = of_node_get(np);	
	host->num_channels = 3;
	host->mode = pmac_low_i2c_mode_std;
	host->func = pmu_low_i2c_func;
}

#endif /* CONFIG_ADB_PMU */

void __init pmac_init_low_i2c(void)
{
	struct device_node *np;

	/* Probe keywest-i2c busses */
	np = of_find_compatible_node(NULL, "i2c", "keywest-i2c");
	while(np) {
		keywest_low_i2c_add(np);
		np = of_find_compatible_node(np, "i2c", "keywest-i2c");
	}

#ifdef CONFIG_ADB_PMU
	/* Probe PMU busses */
	np = of_find_node_by_name(NULL, "via-pmu");
	if (np)
		pmu_low_i2c_add(np);
#endif /* CONFIG_ADB_PMU */

	/* TODO: Add CUDA support as well */
}

int pmac_low_i2c_lock(struct device_node *np)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;
	down(&host->mutex);
	return 0;
}
EXPORT_SYMBOL(pmac_low_i2c_lock);

int pmac_low_i2c_unlock(struct device_node *np)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;
	up(&host->mutex);
	return 0;
}
EXPORT_SYMBOL(pmac_low_i2c_unlock);


int pmac_low_i2c_open(struct device_node *np, int channel)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;

	if (channel >= host->num_channels)
		return -EINVAL;

	down(&host->mutex);
	host->is_open = 1;
	host->channel = channel;

	return 0;
}
EXPORT_SYMBOL(pmac_low_i2c_open);

int pmac_low_i2c_close(struct device_node *np)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;

	host->is_open = 0;
	up(&host->mutex);

	return 0;
}
EXPORT_SYMBOL(pmac_low_i2c_close);

int pmac_low_i2c_setmode(struct device_node *np, int mode)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;
	WARN_ON(!host->is_open);
	host->mode = mode;

	return 0;
}
EXPORT_SYMBOL(pmac_low_i2c_setmode);

int pmac_low_i2c_xfer(struct device_node *np, u8 addrdir, u8 subaddr, u8 *data, int len)
{
	struct low_i2c_host *host = find_low_i2c_host(np);

	if (!host)
		return -ENODEV;
	WARN_ON(!host->is_open);

	return host->func(host, addrdir, subaddr, data, len);
}
EXPORT_SYMBOL(pmac_low_i2c_xfer);

