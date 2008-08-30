/*
 * ebus.h: PCI to Ebus pseudo driver software state.
 *
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_EBUS_H
#define __SPARC64_EBUS_H

#include <linux/of_device.h>

#include <asm/oplib.h>
#include <asm/prom.h>

struct linux_ebus_child {
	struct linux_ebus_child		*next;
	struct linux_ebus_device	*parent;
	struct linux_ebus		*bus;
	struct device_node		*prom_node;
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};

struct linux_ebus_device {
	struct of_device		ofdev;
	struct linux_ebus_device	*next;
	struct linux_ebus_child		*children;
	struct linux_ebus		*bus;
	struct device_node		*prom_node;
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};
#define to_ebus_device(d) container_of(d, struct linux_ebus_device, ofdev.dev)

struct linux_ebus {
	struct of_device		ofdev;
	struct linux_ebus		*next;
	struct linux_ebus_device	*devices;
	struct pci_dev			*self;
	int				 index;
	int				 is_rio;
	struct device_node		*prom_node;
};
#define to_ebus(d) container_of(d, struct linux_ebus, ofdev.dev)

extern struct linux_ebus		*ebus_chain;

extern void ebus_init(void);

#define for_each_ebus(bus)						\
        for((bus) = ebus_chain; (bus); (bus) = (bus)->next)

#define for_each_ebusdev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#define for_each_edevchild(dev, child)					\
        for((child) = (dev)->children; (child); (child) = (child)->next)

#endif /* !(__SPARC64_EBUS_H) */
