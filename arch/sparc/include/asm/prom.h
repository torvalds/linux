#include <linux/of.h>	/* linux/of.h gets to determine #include ordering */
#ifndef _SPARC_PROM_H
#define _SPARC_PROM_H
#ifdef __KERNEL__

/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 * Updates for SPARC by David S. Miller
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/of_pdt.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT	2
#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT	1

#define of_compat_cmp(s1, s2, l)	strncmp((s1), (s2), (l))
#define of_prop_cmp(s1, s2)		strcasecmp((s1), (s2))
#define of_node_cmp(s1, s2)		strcmp((s1), (s2))

struct of_irq_controller {
	unsigned int	(*irq_build)(struct device_node *, unsigned int, void *);
	void		*data;
};

extern struct device_node *of_find_node_by_cpuid(int cpuid);
extern int of_set_property(struct device_node *node, const char *name, void *val, int len);
extern struct mutex of_set_property_mutex;
extern int of_getintprop_default(struct device_node *np,
				 const char *name,
				 int def);
extern int of_find_in_proplist(const char *list, const char *match, int len);
#ifdef CONFIG_NUMA
extern int of_node_to_nid(struct device_node *dp);
#define of_node_to_nid of_node_to_nid
#endif

extern void prom_build_devicetree(void);
extern void of_populate_present_mask(void);
extern void of_fill_in_cpu_data(void);

struct resource;
extern void __iomem *of_ioremap(struct resource *res, unsigned long offset, unsigned long size, char *name);
extern void of_iounmap(struct resource *res, void __iomem *base, unsigned long size);

/* These routines are here to provide compatibility with how powerpc
 * handles IRQ mapping for OF device nodes.  We precompute and permanently
 * register them in the platform_device objects, whereas powerpc computes them
 * on request.
 */
static inline void irq_dispose_mapping(unsigned int virq)
{
}

extern struct device_node *of_console_device;
extern char *of_console_path;
extern char *of_console_options;

extern void irq_trans_init(struct device_node *dp);
extern char *build_path_component(struct device_node *dp);

#endif /* __KERNEL__ */
#endif /* _SPARC_PROM_H */
