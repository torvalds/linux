/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _POWERNV_H
#define _POWERNV_H

/*
 * There's various hacks scattered throughout the generic powerpc arch code
 * that needs to call into powernv platform stuff. The prototypes for those
 * functions are in asm/powernv.h
 */
#include <asm/powernv.h>

#ifdef CONFIG_SMP
extern void pnv_smp_init(void);
#else
static inline void pnv_smp_init(void) { }
#endif

extern void pnv_platform_error_reboot(struct pt_regs *regs, const char *msg) __noreturn;

struct pci_dev;

#ifdef CONFIG_PCI
extern void pnv_pci_init(void);
extern void pnv_pci_shutdown(void);
#else
static inline void pnv_pci_init(void) { }
static inline void pnv_pci_shutdown(void) { }
#endif

extern u32 pnv_get_supported_cpuidle_states(void);

extern void pnv_lpc_init(void);

extern void opal_handle_events(void);
extern bool opal_have_pending_events(void);
extern void opal_event_shutdown(void);

bool cpu_core_split_required(void);

struct memcons;
ssize_t memcons_copy(struct memcons *mc, char *to, loff_t pos, size_t count);
u32 __init memcons_get_size(struct memcons *mc);
struct memcons *__init memcons_init(struct device_node *node, const char *mc_prop_name);

void pnv_rng_init(void);

#endif /* _POWERNV_H */
