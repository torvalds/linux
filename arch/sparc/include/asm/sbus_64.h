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

/* The base address at which to calculate device OBIO addresses. */
#define SUN_SBUS_BVADDR        0x00000000
#define SBUS_OFF_MASK          0x0fffffff

/* These routines are used to calculate device address from slot
 * numbers + offsets, and vice versa.
 */

static inline unsigned long sbus_devaddr(int slotnum, unsigned long offset)
{
  return (unsigned long) (SUN_SBUS_BVADDR+((slotnum)<<28)+(offset));
}

static inline int sbus_dev_slot(unsigned long dev_addr)
{
  return (int) (((dev_addr)-SUN_SBUS_BVADDR)>>28);
}

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
	struct sbus_dev		*devices;	/* Tree of SBUS devices	*/
	struct sbus_bus		*next;		/* Next SBUS in system	*/
	int			prom_node;      /* OBP node of SBUS	*/
	char			prom_name[64];	/* Usually "sbus" or "sbi" */
	int			clock_freq;
	int portid;
};
#define to_sbus(d) container_of(d, struct sbus_bus, ofdev.dev)

extern struct sbus_bus *sbus_root;

/* Device probing routines could find these handy */
#define for_each_sbus(bus) \
        for((bus) = sbus_root; (bus); (bus)=(bus)->next)

#define for_each_sbusdev(device, bus) \
        for((device) = (bus)->devices; (device); (device)=(device)->next)

#define for_all_sbusdev(device, bus) \
	for ((bus) = sbus_root; (bus); (bus) = (bus)->next) \
		for ((device) = (bus)->devices; (device); (device) = (device)->next)

extern void sbus_setup_iommu(struct sbus_bus *, struct device_node *);
extern int sbus_arch_preinit(void);
extern void sbus_arch_postinit(void);

#endif /* !(_SPARC64_SBUS_H) */
