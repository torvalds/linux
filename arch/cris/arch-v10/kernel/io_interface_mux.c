/* IO interface mux allocator for ETRAX100LX.
 * Copyright 2004, Axis Communications AB
 * $Id: io_interface_mux.c,v 1.2 2004/12/21 12:08:38 starvik Exp $
 */


/* C.f. ETRAX100LX Designer's Reference 20.9 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/arch/svinto.h>
#include <asm/io.h>
#include <asm/arch/io_interface_mux.h>


#define DBG(s)

/* Macro to access ETRAX 100 registers */
#define SETS(var, reg, field, val) var = (var & ~IO_MASK_(reg##_, field##_)) | \
					  IO_STATE_(reg##_, field##_, _##val)

enum io_if_group {
	group_a = (1<<0),
	group_b = (1<<1),
	group_c = (1<<2),
	group_d = (1<<3),
	group_e = (1<<4),
	group_f = (1<<5)
};

struct watcher
{
	void (*notify)(const unsigned int gpio_in_available,
		       const unsigned int gpio_out_available,
		       const unsigned char pa_available,
		       const unsigned char pb_available);
	struct watcher *next;
};


struct if_group
{
	enum io_if_group        group;
	unsigned char           used;
	enum cris_io_interface  owner;
};


struct interface
{
	enum cris_io_interface   ioif;
	unsigned char            groups;
	unsigned char            used;
	char                    *owner;
	unsigned int             gpio_g_in;
	unsigned int             gpio_g_out;
	unsigned char            gpio_b;
};

static struct if_group if_groups[6] = {
	{
		.group = group_a,
		.used = 0,
	},
	{
		.group = group_b,
		.used = 0,
	},
	{
		.group = group_c,
		.used = 0,
	},
	{
		.group = group_d,
		.used = 0,
	},
	{
		.group = group_e,
		.used = 0,
	},
	{
		.group = group_f,
		.used = 0,
	}
};

/* The order in the array must match the order of enum
 * cris_io_interface in io_interface_mux.h */
static struct interface interfaces[] = {
	/* Begin Non-multiplexed interfaces */
	{
		.ioif = if_eth,
		.groups = 0,
		.gpio_g_in = 0,
		.gpio_g_out = 0,
		.gpio_b = 0
	},
	{
		.ioif = if_serial_0,
		.groups = 0,
		.gpio_g_in = 0,
		.gpio_g_out = 0,
		.gpio_b = 0
	},
	/* End Non-multiplexed interfaces */
	{
		.ioif = if_serial_1,
		.groups = group_e,
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_serial_2,
		.groups = group_b,
		.gpio_g_in =  0x000000c0,
		.gpio_g_out = 0x000000c0,
		.gpio_b = 0x00
	},
	{
		.ioif = if_serial_3,
		.groups = group_c,
		.gpio_g_in =  0xc0000000,
		.gpio_g_out = 0xc0000000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_sync_serial_1,
		.groups = group_e | group_f, /* if_sync_serial_1 and if_sync_serial_3
					       can be used simultaneously */
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0x10
	},
	{
		.ioif = if_sync_serial_3,
		.groups = group_c | group_f,
		.gpio_g_in =  0xc0000000,
		.gpio_g_out = 0xc0000000,
		.gpio_b = 0x80
	},
	{
		.ioif = if_shared_ram,
		.groups = group_a,
		.gpio_g_in =  0x0000ff3e,
		.gpio_g_out = 0x0000ff38,
		.gpio_b = 0x00
	},
	{
		.ioif = if_shared_ram_w,
		.groups = group_a | group_d,
		.gpio_g_in =  0x00ffff3e,
		.gpio_g_out = 0x00ffff38,
		.gpio_b = 0x00
	},
	{
		.ioif = if_par_0,
		.groups = group_a,
		.gpio_g_in =  0x0000ff3e,
		.gpio_g_out = 0x0000ff3e,
		.gpio_b = 0x00
	},
	{
		.ioif = if_par_1,
		.groups = group_d,
		.gpio_g_in =  0x3eff0000,
		.gpio_g_out = 0x3eff0000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_par_w,
		.groups = group_a | group_d,
		.gpio_g_in =  0x00ffff3e,
		.gpio_g_out = 0x00ffff3e,
		.gpio_b = 0x00
	},
	{
		.ioif = if_scsi8_0,
		.groups = group_a | group_b | group_f, /* if_scsi8_0 and if_scsi8_1
							  can be used simultaneously */
		.gpio_g_in =  0x0000ffff,
		.gpio_g_out = 0x0000ffff,
		.gpio_b = 0x10
	},
	{
		.ioif = if_scsi8_1,
		.groups = group_c | group_d | group_f, /* if_scsi8_0 and if_scsi8_1
							  can be used simultaneously */
		.gpio_g_in =  0xffff0000,
		.gpio_g_out = 0xffff0000,
		.gpio_b = 0x80
	},
	{
		.ioif = if_scsi_w,
		.groups = group_a | group_b | group_d | group_f,
		.gpio_g_in =  0x01ffffff,
		.gpio_g_out = 0x07ffffff,
		.gpio_b = 0x80
	},
	{
		.ioif = if_ata,
		.groups = group_a | group_b | group_c | group_d,
		.gpio_g_in =  0xf9ffffff,
		.gpio_g_out = 0xffffffff,
		.gpio_b = 0x80
	},
	{
		.ioif = if_csp,
		.groups = group_f, /* if_csp and if_i2c can be used simultaneously */
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0xfc
	},
	{
		.ioif = if_i2c,
		.groups = group_f, /* if_csp and if_i2c can be used simultaneously */
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0x03
	},
	{
		.ioif = if_usb_1,
		.groups = group_e | group_f,
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0x2c
	},
	{
		.ioif = if_usb_2,
		.groups = group_d,
		.gpio_g_in =  0x0e000000,
		.gpio_g_out = 0x3c000000,
		.gpio_b = 0x00
	},
	/* GPIO pins */
	{
		.ioif = if_gpio_grp_a,
		.groups = group_a,
		.gpio_g_in =  0x0000ff3f,
		.gpio_g_out = 0x0000ff3f,
		.gpio_b = 0x00
	},
	{
		.ioif = if_gpio_grp_b,
		.groups = group_b,
		.gpio_g_in =  0x000000c0,
		.gpio_g_out = 0x000000c0,
		.gpio_b = 0x00
	},
	{
		.ioif = if_gpio_grp_c,
		.groups = group_c,
		.gpio_g_in =  0xc0000000,
		.gpio_g_out = 0xc0000000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_gpio_grp_d,
		.groups = group_d,
		.gpio_g_in =  0x3fff0000,
		.gpio_g_out = 0x3fff0000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_gpio_grp_e,
		.groups = group_e,
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0x00
	},
	{
		.ioif = if_gpio_grp_f,
		.groups = group_f,
		.gpio_g_in =  0x00000000,
		.gpio_g_out = 0x00000000,
		.gpio_b = 0xff
	}
	/* Array end */
};

static struct watcher *watchers = NULL;

static unsigned int gpio_in_pins =  0xffffffff;
static unsigned int gpio_out_pins = 0xffffffff;
static unsigned char gpio_pb_pins = 0xff;
static unsigned char gpio_pa_pins = 0xff;

static enum cris_io_interface gpio_pa_owners[8];
static enum cris_io_interface gpio_pb_owners[8];
static enum cris_io_interface gpio_pg_owners[32];

static int cris_io_interface_init(void);

static unsigned char clear_group_from_set(const unsigned char groups, struct if_group *group)
{
	return (groups & ~group->group);
}


static struct if_group *get_group(const unsigned char groups)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(if_groups); i++) {
		if (groups & if_groups[i].group) {
			return &if_groups[i];
		}
	}
	return NULL;
}


static void notify_watchers(void)
{
	struct watcher *w = watchers;

	DBG(printk("io_interface_mux: notifying watchers\n"));

	while (NULL != w) {
		w->notify((const unsigned int)gpio_in_pins,
			  (const unsigned int)gpio_out_pins,
			  (const unsigned char)gpio_pa_pins,
			  (const unsigned char)gpio_pb_pins);
		w = w->next;
	}
}


int cris_request_io_interface(enum cris_io_interface ioif, const char *device_id)
{
	int set_gen_config = 0;
	int set_gen_config_ii = 0;
	unsigned long int gens;
	unsigned long int gens_ii;
	struct if_group *grp;
	unsigned char group_set;
	unsigned long flags;

	(void)cris_io_interface_init();

	DBG(printk("cris_request_io_interface(%d, \"%s\")\n", ioif, device_id));

	if ((ioif >= if_max_interfaces) || (ioif < 0)) {
		printk(KERN_CRIT "cris_request_io_interface: Bad interface %u submitted for %s\n",
		       ioif,
		       device_id);
		return -EINVAL;
	}

	local_irq_save(flags);

	if (interfaces[ioif].used) {
		local_irq_restore(flags);
		printk(KERN_CRIT "cris_io_interface: Cannot allocate interface for %s, in use by %s\n",
		       device_id,
		       interfaces[ioif].owner);
		return -EBUSY;
	}

	/* Check that all required groups are free before allocating, */
	group_set = interfaces[ioif].groups;
	while (NULL != (grp = get_group(group_set))) {
		if (grp->used) {
			if (grp->group == group_f) {
				if ((if_sync_serial_1 ==  ioif) ||
				    (if_sync_serial_3 ==  ioif)) {
					if ((grp->owner != if_sync_serial_1) &&
					    (grp->owner != if_sync_serial_3)) {
						local_irq_restore(flags);
						return -EBUSY;
					}
				} else if ((if_scsi8_0 == ioif) ||
					   (if_scsi8_1 == ioif)) {
					if ((grp->owner != if_scsi8_0) &&
					    (grp->owner != if_scsi8_1)) {
						local_irq_restore(flags);
						return -EBUSY;
					}
				}
			} else {
				local_irq_restore(flags);
				return -EBUSY;
			}
		}
		group_set = clear_group_from_set(group_set, grp);
	}

	/* Are the required GPIO pins available too? */
	if (((interfaces[ioif].gpio_g_in & gpio_in_pins) != interfaces[ioif].gpio_g_in) ||
	    ((interfaces[ioif].gpio_g_out & gpio_out_pins) != interfaces[ioif].gpio_g_out) ||
	    ((interfaces[ioif].gpio_b & gpio_pb_pins) != interfaces[ioif].gpio_b)) {
		printk(KERN_CRIT "cris_request_io_interface: Could not get required pins for interface %u\n",
		       ioif);
		return -EBUSY;
	}

	/* All needed I/O pins and pin groups are free, allocate. */
	group_set = interfaces[ioif].groups;
	while (NULL != (grp = get_group(group_set))) {
		grp->used = 1;
		grp->owner = ioif;
		group_set = clear_group_from_set(group_set, grp);
	}

	gens = genconfig_shadow;
	gens_ii = gen_config_ii_shadow;

	set_gen_config = 1;
	switch (ioif)
	{
	/* Begin Non-multiplexed interfaces */
	case if_eth:
		/* fall through */
	case if_serial_0:
		set_gen_config = 0;
		break;
	/* End Non-multiplexed interfaces */
	case if_serial_1:
		set_gen_config_ii = 1;
		SETS(gens_ii, R_GEN_CONFIG_II, sermode1, async);
		break;
	case if_serial_2:
		SETS(gens, R_GEN_CONFIG, ser2, select);
		break;
	case if_serial_3:
		SETS(gens, R_GEN_CONFIG, ser3, select);
		set_gen_config_ii = 1;
		SETS(gens_ii, R_GEN_CONFIG_II, sermode3, async);
		break;
	case if_sync_serial_1:
		set_gen_config_ii = 1;
		SETS(gens_ii, R_GEN_CONFIG_II, sermode1, sync);
		break;
	case if_sync_serial_3:
		SETS(gens, R_GEN_CONFIG, ser3, select);
		set_gen_config_ii = 1;
		SETS(gens_ii, R_GEN_CONFIG_II, sermode3, sync);
		break;
	case if_shared_ram:
		SETS(gens, R_GEN_CONFIG, mio, select);
		break;
	case if_shared_ram_w:
		SETS(gens, R_GEN_CONFIG, mio_w, select);
		break;
	case if_par_0:
		SETS(gens, R_GEN_CONFIG, par0, select);
		break;
	case if_par_1:
		SETS(gens, R_GEN_CONFIG, par1, select);
		break;
	case if_par_w:
		SETS(gens, R_GEN_CONFIG, par0, select);
		SETS(gens, R_GEN_CONFIG, par_w, select);
		break;
	case if_scsi8_0:
		SETS(gens, R_GEN_CONFIG, scsi0, select);
		break;
	case if_scsi8_1:
		SETS(gens, R_GEN_CONFIG, scsi1, select);
		break;
	case if_scsi_w:
		SETS(gens, R_GEN_CONFIG, scsi0, select);
		SETS(gens, R_GEN_CONFIG, scsi0w, select);
		break;
	case if_ata:
		SETS(gens, R_GEN_CONFIG, ata, select);
		break;
	case if_csp:
		/* fall through */
	case if_i2c:
		set_gen_config = 0;
		break;
	case if_usb_1:
		SETS(gens, R_GEN_CONFIG, usb1, select);
		break;
	case if_usb_2:
		SETS(gens, R_GEN_CONFIG, usb2, select);
		break;
	case if_gpio_grp_a:
		/* GPIO groups are only accounted, don't do configuration changes. */
		/* fall through */
	case if_gpio_grp_b:
		/* fall through */
	case if_gpio_grp_c:
		/* fall through */
	case if_gpio_grp_d:
		/* fall through */
	case if_gpio_grp_e:
		/* fall through */
	case if_gpio_grp_f:
		set_gen_config = 0;
		break;
	default:
		panic("cris_request_io_interface: Bad interface %u submitted for %s\n",
		      ioif,
		      device_id);
	}

	interfaces[ioif].used = 1;
	interfaces[ioif].owner = (char*)device_id;

	if (set_gen_config) {
		volatile int i;
		genconfig_shadow = gens;
		*R_GEN_CONFIG = genconfig_shadow;
		/* Wait 12 cycles before doing any DMA command */
		for(i = 6; i > 0; i--)
			nop();
	}
	if (set_gen_config_ii) {
		gen_config_ii_shadow = gens_ii;
		*R_GEN_CONFIG_II = gen_config_ii_shadow;
	}

	DBG(printk("GPIO pins: available before: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   gpio_in_pins, gpio_out_pins, gpio_pb_pins));
	DBG(printk("grabbing pins: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   interfaces[ioif].gpio_g_in,
		   interfaces[ioif].gpio_g_out,
		   interfaces[ioif].gpio_b));

	gpio_in_pins &= ~interfaces[ioif].gpio_g_in;
	gpio_out_pins &= ~interfaces[ioif].gpio_g_out;
	gpio_pb_pins &= ~interfaces[ioif].gpio_b;

	DBG(printk("GPIO pins: available after: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   gpio_in_pins, gpio_out_pins, gpio_pb_pins));

	local_irq_restore(flags);

	notify_watchers();

	return 0;
}


void cris_free_io_interface(enum cris_io_interface ioif)
{
	struct if_group *grp;
	unsigned char group_set;
	unsigned long flags;

	(void)cris_io_interface_init();

	if ((ioif >= if_max_interfaces) || (ioif < 0)) {
		printk(KERN_CRIT "cris_free_io_interface: Bad interface %u\n",
		       ioif);
		return;
	}
	local_irq_save(flags);
	if (!interfaces[ioif].used) {
		printk(KERN_CRIT "cris_free_io_interface: Freeing free interface %u\n",
		       ioif);
		local_irq_restore(flags);
		return;
	}
	group_set = interfaces[ioif].groups;
	while (NULL != (grp = get_group(group_set))) {
		if (grp->group == group_f) {
			switch (ioif)
			{
			case if_sync_serial_1:
				if ((grp->owner == if_sync_serial_1) &&
				    interfaces[if_sync_serial_3].used) {
					grp->owner = if_sync_serial_3;
				} else
					grp->used = 0;
				break;
			case if_sync_serial_3:
				if ((grp->owner == if_sync_serial_3) &&
				    interfaces[if_sync_serial_1].used) {
					grp->owner = if_sync_serial_1;
				} else
					grp->used = 0;
				break;
			case if_scsi8_0:
				if ((grp->owner == if_scsi8_0) &&
				    interfaces[if_scsi8_1].used) {
					grp->owner = if_scsi8_1;
				} else
					grp->used = 0;
				break;
			case if_scsi8_1:
				if ((grp->owner == if_scsi8_1) &&
				    interfaces[if_scsi8_0].used) {
					grp->owner = if_scsi8_0;
				} else
					grp->used = 0;
				break;
			default:
				grp->used = 0;
			}
		} else {
			grp->used = 0;
		}
		group_set = clear_group_from_set(group_set, grp);
	}
	interfaces[ioif].used = 0;
	interfaces[ioif].owner = NULL;

	DBG(printk("GPIO pins: available before: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   gpio_in_pins, gpio_out_pins, gpio_pb_pins));
	DBG(printk("freeing pins: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   interfaces[ioif].gpio_g_in,
		   interfaces[ioif].gpio_g_out,
		   interfaces[ioif].gpio_b));

	gpio_in_pins |= interfaces[ioif].gpio_g_in;
	gpio_out_pins |= interfaces[ioif].gpio_g_out;
	gpio_pb_pins |= interfaces[ioif].gpio_b;

	DBG(printk("GPIO pins: available after: g_in=0x%08x g_out=0x%08x pb=0x%02x\n",
		   gpio_in_pins, gpio_out_pins, gpio_pb_pins));

	local_irq_restore(flags);

	notify_watchers();
}

/* Create a bitmask from bit 0 (inclusive) to bit stop_bit
   (non-inclusive).  stop_bit == 0 returns 0x0 */
static inline unsigned int create_mask(const unsigned stop_bit)
{
	/* Avoid overflow */
	if (stop_bit >= 32) {
		return 0xffffffff;
	}
	return (1<<stop_bit)-1;
}


/* port can be 'a', 'b' or 'g' */
int cris_io_interface_allocate_pins(const enum cris_io_interface ioif,
				    const char port,
				    const unsigned start_bit,
				    const unsigned stop_bit)
{
	unsigned int i;
	unsigned int mask = 0;
	unsigned int tmp_mask;
	unsigned long int flags;
	enum cris_io_interface *owners;

	(void)cris_io_interface_init();

	DBG(printk("cris_io_interface_allocate_pins: if=%d port=%c start=%u stop=%u\n",
		   ioif, port, start_bit, stop_bit));

	if (!((start_bit <= stop_bit) &&
	      ((((port == 'a') || (port == 'b')) && (stop_bit < 8)) ||
	       ((port == 'g') && (stop_bit < 32))))) {
		return -EINVAL;
	}

	mask = create_mask(stop_bit + 1);
	tmp_mask = create_mask(start_bit);
	mask &= ~tmp_mask;

	DBG(printk("cris_io_interface_allocate_pins: port=%c start=%u stop=%u mask=0x%08x\n",
		   port, start_bit, stop_bit, mask));

	local_irq_save(flags);

	switch (port) {
	case 'a':
		if ((gpio_pa_pins & mask) != mask) {
			local_irq_restore(flags);
			return -EBUSY;
		}
		owners = gpio_pa_owners;
		gpio_pa_pins &= ~mask;
		break;
	case 'b':
		if ((gpio_pb_pins & mask) != mask) {
			local_irq_restore(flags);
			return -EBUSY;
		}
		owners = gpio_pb_owners;
		gpio_pb_pins &= ~mask;
		break;
	case 'g':
		if (((gpio_in_pins & mask) != mask) ||
		    ((gpio_out_pins & mask) != mask)) {
			local_irq_restore(flags);
			return -EBUSY;
		}
		owners = gpio_pg_owners;
		gpio_in_pins &= ~mask;
		gpio_out_pins &= ~mask;
		break;
	default:
		local_irq_restore(flags);
		return -EINVAL;
	}

	for (i = start_bit; i <= stop_bit; i++) {
		owners[i] = ioif;
	}
	local_irq_restore(flags);

	notify_watchers();
	return 0;
}


/* port can be 'a', 'b' or 'g' */
int cris_io_interface_free_pins(const enum cris_io_interface ioif,
                                const char port,
                                const unsigned start_bit,
                                const unsigned stop_bit)
{
	unsigned int i;
	unsigned int mask = 0;
	unsigned int tmp_mask;
	unsigned long int flags;
	enum cris_io_interface *owners;

	(void)cris_io_interface_init();

	if (!((start_bit <= stop_bit) &&
	      ((((port == 'a') || (port == 'b')) && (stop_bit < 8)) ||
	       ((port == 'g') && (stop_bit < 32))))) {
		return -EINVAL;
	}

	mask = create_mask(stop_bit + 1);
	tmp_mask = create_mask(start_bit);
	mask &= ~tmp_mask;

	DBG(printk("cris_io_interface_free_pins: port=%c start=%u stop=%u mask=0x%08x\n",
		   port, start_bit, stop_bit, mask));

	local_irq_save(flags);

	switch (port) {
	case 'a':
		if ((~gpio_pa_pins & mask) != mask) {
			local_irq_restore(flags);
			printk(KERN_CRIT "cris_io_interface_free_pins: Freeing free pins");
		}
		owners = gpio_pa_owners;
		break;
	case 'b':
		if ((~gpio_pb_pins & mask) != mask) {
			local_irq_restore(flags);
			printk(KERN_CRIT "cris_io_interface_free_pins: Freeing free pins");
		}
		owners = gpio_pb_owners;
		break;
	case 'g':
		if (((~gpio_in_pins & mask) != mask) ||
		    ((~gpio_out_pins & mask) != mask)) {
			local_irq_restore(flags);
			printk(KERN_CRIT "cris_io_interface_free_pins: Freeing free pins");
		}
		owners = gpio_pg_owners;
		break;
	default:
		owners = NULL; /* Cannot happen. Shut up, gcc! */
	}

	for (i = start_bit; i <= stop_bit; i++) {
		if (owners[i] != ioif) {
			printk(KERN_CRIT "cris_io_interface_free_pins: Freeing unowned pins");
		}
	}

	/* All was ok, change data. */
	switch (port) {
	case 'a':
		gpio_pa_pins |= mask;
		break;
	case 'b':
		gpio_pb_pins |= mask;
		break;
	case 'g':
		gpio_in_pins |= mask;
		gpio_out_pins |= mask;
		break;
	}

	for (i = start_bit; i <= stop_bit; i++) {
		owners[i] = if_unclaimed;
	}
	local_irq_restore(flags);
	notify_watchers();

        return 0;
}


int cris_io_interface_register_watcher(void (*notify)(const unsigned int gpio_in_available,
                                                      const unsigned int gpio_out_available,
                                                      const unsigned char pa_available,
                                                      const unsigned char pb_available))
{
	struct watcher *w;

	(void)cris_io_interface_init();

	if (NULL == notify) {
		return -EINVAL;
	}
	w = kmalloc(sizeof(*w), GFP_KERNEL);
	if (!w) {
		return -ENOMEM;
	}
	w->notify = notify;
	w->next = watchers;
	watchers = w;

	w->notify((const unsigned int)gpio_in_pins,
		  (const unsigned int)gpio_out_pins,
		  (const unsigned char)gpio_pa_pins,
		  (const unsigned char)gpio_pb_pins);

	return 0;
}

void cris_io_interface_delete_watcher(void (*notify)(const unsigned int gpio_in_available,
						     const unsigned int gpio_out_available,
                                                     const unsigned char pa_available,
						     const unsigned char pb_available))
{
	struct watcher *w = watchers, *prev = NULL;

	(void)cris_io_interface_init();

	while ((NULL != w) && (w->notify != notify)){
		prev = w;
		w = w->next;
	}
	if (NULL != w) {
		if (NULL != prev) {
			prev->next = w->next;
		} else {
			watchers = w->next;
		}
		kfree(w);
		return;
	}
	printk(KERN_WARNING "cris_io_interface_delete_watcher: Deleting unknown watcher 0x%p\n", notify);
}


static int cris_io_interface_init(void)
{
	static int first = 1;
	int i;

	if (!first) {
		return 0;
	}
	first = 0;

	for (i = 0; i<8; i++) {
		gpio_pa_owners[i] = if_unclaimed;
		gpio_pb_owners[i] = if_unclaimed;
		gpio_pg_owners[i] = if_unclaimed;
	}
	for (; i<32; i++) {
		gpio_pg_owners[i] = if_unclaimed;
	}
	return 0;
}


module_init(cris_io_interface_init);


EXPORT_SYMBOL(cris_request_io_interface);
EXPORT_SYMBOL(cris_free_io_interface);
EXPORT_SYMBOL(cris_io_interface_allocate_pins);
EXPORT_SYMBOL(cris_io_interface_free_pins);
EXPORT_SYMBOL(cris_io_interface_register_watcher);
EXPORT_SYMBOL(cris_io_interface_delete_watcher);
