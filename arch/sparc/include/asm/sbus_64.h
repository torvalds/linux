/* sbus.h: Defines for the Sun SBus.
 *
 * Copyright (C) 1996, 1999, 2007 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_SBUS_H
#define _SPARC64_SBUS_H

#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/of_device.h>

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/iommu.h>
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
};
#define to_sbus_device(d) container_of(d, struct sbus_dev, ofdev.dev)

/* This struct describes the SBus(s) found on this machine. */
struct sbus_bus {
	struct of_device	ofdev;
	struct sbus_dev		*devices;	/* Tree of SBUS devices	*/
	struct sbus_bus		*next;		/* Next SBUS in system	*/
};
#define to_sbus(d) container_of(d, struct sbus_bus, ofdev.dev)

extern void sbus_setup_iommu(struct sbus_bus *, struct device_node *);
extern int sbus_arch_preinit(void);
extern void sbus_arch_postinit(void);

#endif /* !(_SPARC64_SBUS_H) */
