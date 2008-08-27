/*
 * sbus.h:  Defines for the Sun SBus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SBUS_H
#define _SPARC_SBUS_H

#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/of_device.h>

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/scatterlist.h>

/* We scan which devices are on the SBus using the PROM node device
 * tree.  SBus devices are described in two different ways.  You can
 * either get an absolute address at which to access the device, or
 * you can get a SBus 'slot' number and an offset within that slot.
 */

struct sbus_bus;

/* Linux SBUS device tables */
struct sbus_dev {
	struct of_device	ofdev;
	struct sbus_bus		*bus;
	struct sbus_dev		*next;
	struct sbus_dev		*child;
	struct sbus_dev		*parent;
	int prom_node;
	char prom_name[64];
	int slot;
};
#define to_sbus_device(d) container_of(d, struct sbus_dev, ofdev.dev)

/* This struct describes the SBus(s) found on this machine. */
struct sbus_bus {
	struct of_device	ofdev;
	struct sbus_dev		*devices;	/* Link to devices on this SBus */
	struct sbus_bus		*next;		/* next SBus, if more than one SBus */
	int			prom_node;	/* PROM device tree node for this SBus */
	char			prom_name[64];  /* Usually "sbus" or "sbi" */
	int			clock_freq;
};
#define to_sbus(d) container_of(d, struct sbus_bus, ofdev.dev)

static inline int
sbus_is_slave(struct sbus_dev *dev)
{
	/* XXX Have to write this for sun4c's */
	return 0;
}

/* These yield IOMMU mappings in consistent mode. */
void prom_adjust_ranges(struct linux_prom_ranges *, int,
			struct linux_prom_ranges *, int);

extern void sbus_setup_iommu(struct sbus_bus *, struct device_node *);
extern int sbus_arch_preinit(void);
extern void sbus_arch_postinit(void);

#endif /* !(_SPARC_SBUS_H) */
