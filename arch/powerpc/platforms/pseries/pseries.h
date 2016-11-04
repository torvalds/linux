/*
 * Copyright 2006 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _PSERIES_PSERIES_H
#define _PSERIES_PSERIES_H

#include <linux/interrupt.h>
#include <asm/rtas.h>

struct device_node;

extern void request_event_sources_irqs(struct device_node *np,
				       irq_handler_t handler, const char *name);

#include <linux/of.h>

struct pt_regs;

extern int pSeries_system_reset_exception(struct pt_regs *regs);
extern int pSeries_machine_check_exception(struct pt_regs *regs);

#ifdef CONFIG_SMP
extern void smp_init_pseries(void);
#else
static inline void smp_init_pseries(void) { };
#endif

extern void pseries_kexec_cpu_down(int crash_shutdown, int secondary);

extern void pSeries_final_fixup(void);

/* Poweron flag used for enabling auto ups restart */
extern unsigned long rtas_poweron_auto;

/* Provided by HVC VIO */
extern void hvc_vio_init_early(void);

/* Dynamic logical Partitioning/Mobility */
extern void dlpar_free_cc_nodes(struct device_node *);
extern void dlpar_free_cc_property(struct property *);
extern struct device_node *dlpar_configure_connector(__be32,
						struct device_node *);
extern int dlpar_attach_node(struct device_node *);
extern int dlpar_detach_node(struct device_node *);
extern int dlpar_acquire_drc(u32 drc_index);
extern int dlpar_release_drc(u32 drc_index);

void queue_hotplug_event(struct pseries_hp_errorlog *hp_errlog,
			 struct completion *hotplug_done, int *rc);
#ifdef CONFIG_MEMORY_HOTPLUG
int dlpar_memory(struct pseries_hp_errorlog *hp_elog);
#else
static inline int dlpar_memory(struct pseries_hp_errorlog *hp_elog)
{
	return -EOPNOTSUPP;
}
#endif

#ifdef CONFIG_HOTPLUG_CPU
int dlpar_cpu(struct pseries_hp_errorlog *hp_elog);
#else
static inline int dlpar_cpu(struct pseries_hp_errorlog *hp_elog)
{
	return -EOPNOTSUPP;
}
#endif

/* PCI root bridge prepare function override for pseries */
struct pci_host_bridge;
int pseries_root_bridge_prepare(struct pci_host_bridge *bridge);

extern struct pci_controller_ops pseries_pci_controller_ops;

unsigned long pseries_memory_block_size(void);

#endif /* _PSERIES_PSERIES_H */
