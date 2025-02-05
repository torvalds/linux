/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_PARISC_DEVICE_H_
#define _ASM_PARISC_PARISC_DEVICE_H_

#include <linux/device.h>

struct parisc_device {
	struct resource hpa;		/* Hard Physical Address */
	struct parisc_device_id id;
	struct parisc_driver *driver;	/* Driver for this device */
	char		name[80];	/* The hardware description */
	int		irq;
	int		aux_irq;	/* Some devices have a second IRQ */

	char		hw_path;        /* The module number on this bus */
	unsigned int	num_addrs;	/* some devices have additional address ranges. */
	unsigned long	*addr;          /* which will be stored here */
 
#ifdef CONFIG_64BIT
	/* parms for pdc_pat_cell_module() call */
	unsigned long	pcell_loc;	/* Physical Cell location */
	unsigned long	mod_index;	/* PAT specific - Misc Module info */

	/* generic info returned from pdc_pat_cell_module() */
	unsigned long	mod_info;	/* PAT specific - Misc Module info */
	unsigned long	pmod_loc;	/* physical Module location */
	unsigned long	mod0;
#endif
	u64		dma_mask;	/* DMA mask for I/O */
	struct device 	dev;
};

struct parisc_driver {
	struct parisc_driver *next;
	char *name; 
	const struct parisc_device_id *id_table;
	int (*probe)(struct parisc_device *dev); /* New device discovered */
	void (*remove)(struct parisc_device *dev);
	struct device_driver drv;
};


#define to_parisc_device(d)	container_of(d, struct parisc_device, dev)
#define to_parisc_driver(d)	container_of_const(d, struct parisc_driver, drv)
#define parisc_parent(d)	to_parisc_device(d->dev.parent)

static inline const char *parisc_pathname(struct parisc_device *d)
{
	return dev_name(&d->dev);
}

static inline void
parisc_set_drvdata(struct parisc_device *d, void *p)
{
	dev_set_drvdata(&d->dev, p);
}

static inline void *
parisc_get_drvdata(struct parisc_device *d)
{
	return dev_get_drvdata(&d->dev);
}

extern const struct bus_type parisc_bus_type;

int iosapic_serial_irq(struct parisc_device *dev);

#endif /*_ASM_PARISC_PARISC_DEVICE_H_*/
