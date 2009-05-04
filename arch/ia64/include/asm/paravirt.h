/******************************************************************************
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#ifndef __ASM_PARAVIRT_H
#define __ASM_PARAVIRT_H

#ifndef __ASSEMBLY__
/******************************************************************************
 * fsys related addresses
 */
struct pv_fsys_data {
	unsigned long *fsyscall_table;
	void *fsys_bubble_down;
};

extern struct pv_fsys_data pv_fsys_data;

unsigned long *paravirt_get_fsyscall_table(void);
char *paravirt_get_fsys_bubble_down(void);

/******************************************************************************
 * patchlist addresses for gate page
 */
enum pv_gate_patchlist {
	PV_GATE_START_FSYSCALL,
	PV_GATE_END_FSYSCALL,

	PV_GATE_START_BRL_FSYS_BUBBLE_DOWN,
	PV_GATE_END_BRL_FSYS_BUBBLE_DOWN,

	PV_GATE_START_VTOP,
	PV_GATE_END_VTOP,

	PV_GATE_START_MCKINLEY_E9,
	PV_GATE_END_MCKINLEY_E9,
};

struct pv_patchdata {
	unsigned long start_fsyscall_patchlist;
	unsigned long end_fsyscall_patchlist;
	unsigned long start_brl_fsys_bubble_down_patchlist;
	unsigned long end_brl_fsys_bubble_down_patchlist;
	unsigned long start_vtop_patchlist;
	unsigned long end_vtop_patchlist;
	unsigned long start_mckinley_e9_patchlist;
	unsigned long end_mckinley_e9_patchlist;

	void *gate_section;
};

extern struct pv_patchdata pv_patchdata;

unsigned long paravirt_get_gate_patchlist(enum pv_gate_patchlist type);
void *paravirt_get_gate_section(void);
#endif

#ifdef CONFIG_PARAVIRT_GUEST

#define PARAVIRT_HYPERVISOR_TYPE_DEFAULT	0
#define PARAVIRT_HYPERVISOR_TYPE_XEN		1

#ifndef __ASSEMBLY__

#include <asm/hw_irq.h>
#include <asm/meminit.h>

/******************************************************************************
 * general info
 */
struct pv_info {
	unsigned int kernel_rpl;
	int paravirt_enabled;
	const char *name;
};

extern struct pv_info pv_info;

static inline int paravirt_enabled(void)
{
	return pv_info.paravirt_enabled;
}

static inline unsigned int get_kernel_rpl(void)
{
	return pv_info.kernel_rpl;
}

/******************************************************************************
 * initialization hooks.
 */
struct rsvd_region;

struct pv_init_ops {
	void (*banner)(void);

	int (*reserve_memory)(struct rsvd_region *region);

	void (*arch_setup_early)(void);
	void (*arch_setup_console)(char **cmdline_p);
	int (*arch_setup_nomca)(void);

	void (*post_smp_prepare_boot_cpu)(void);

#ifdef ASM_SUPPORTED
	unsigned long (*patch_bundle)(void *sbundle, void *ebundle,
				      unsigned long type);
	unsigned long (*patch_inst)(unsigned long stag, unsigned long etag,
				    unsigned long type);
#endif
	void (*patch_branch)(unsigned long tag, unsigned long type);
};

extern struct pv_init_ops pv_init_ops;

static inline void paravirt_banner(void)
{
	if (pv_init_ops.banner)
		pv_init_ops.banner();
}

static inline int paravirt_reserve_memory(struct rsvd_region *region)
{
	if (pv_init_ops.reserve_memory)
		return pv_init_ops.reserve_memory(region);
	return 0;
}

static inline void paravirt_arch_setup_early(void)
{
	if (pv_init_ops.arch_setup_early)
		pv_init_ops.arch_setup_early();
}

static inline void paravirt_arch_setup_console(char **cmdline_p)
{
	if (pv_init_ops.arch_setup_console)
		pv_init_ops.arch_setup_console(cmdline_p);
}

static inline int paravirt_arch_setup_nomca(void)
{
	if (pv_init_ops.arch_setup_nomca)
		return pv_init_ops.arch_setup_nomca();
	return 0;
}

static inline void paravirt_post_smp_prepare_boot_cpu(void)
{
	if (pv_init_ops.post_smp_prepare_boot_cpu)
		pv_init_ops.post_smp_prepare_boot_cpu();
}

/******************************************************************************
 * replacement of iosapic operations.
 */

struct pv_iosapic_ops {
	void (*pcat_compat_init)(void);

	struct irq_chip *(*__get_irq_chip)(unsigned long trigger);

	unsigned int (*__read)(char __iomem *iosapic, unsigned int reg);
	void (*__write)(char __iomem *iosapic, unsigned int reg, u32 val);
};

extern struct pv_iosapic_ops pv_iosapic_ops;

static inline void
iosapic_pcat_compat_init(void)
{
	if (pv_iosapic_ops.pcat_compat_init)
		pv_iosapic_ops.pcat_compat_init();
}

static inline struct irq_chip*
iosapic_get_irq_chip(unsigned long trigger)
{
	return pv_iosapic_ops.__get_irq_chip(trigger);
}

static inline unsigned int
__iosapic_read(char __iomem *iosapic, unsigned int reg)
{
	return pv_iosapic_ops.__read(iosapic, reg);
}

static inline void
__iosapic_write(char __iomem *iosapic, unsigned int reg, u32 val)
{
	return pv_iosapic_ops.__write(iosapic, reg, val);
}

/******************************************************************************
 * replacement of irq operations.
 */

struct pv_irq_ops {
	void (*register_ipi)(void);

	int (*assign_irq_vector)(int irq);
	void (*free_irq_vector)(int vector);

	void (*register_percpu_irq)(ia64_vector vec,
				    struct irqaction *action);

	void (*resend_irq)(unsigned int vector);
};

extern struct pv_irq_ops pv_irq_ops;

static inline void
ia64_register_ipi(void)
{
	pv_irq_ops.register_ipi();
}

static inline int
assign_irq_vector(int irq)
{
	return pv_irq_ops.assign_irq_vector(irq);
}

static inline void
free_irq_vector(int vector)
{
	return pv_irq_ops.free_irq_vector(vector);
}

static inline void
register_percpu_irq(ia64_vector vec, struct irqaction *action)
{
	pv_irq_ops.register_percpu_irq(vec, action);
}

static inline void
ia64_resend_irq(unsigned int vector)
{
	pv_irq_ops.resend_irq(vector);
}

/******************************************************************************
 * replacement of time operations.
 */

extern struct itc_jitter_data_t itc_jitter_data;
extern volatile int time_keeper_id;

struct pv_time_ops {
	void (*init_missing_ticks_accounting)(int cpu);
	int (*do_steal_accounting)(unsigned long *new_itm);

	void (*clocksource_resume)(void);

	unsigned long long (*sched_clock)(void);
};

extern struct pv_time_ops pv_time_ops;

static inline void
paravirt_init_missing_ticks_accounting(int cpu)
{
	if (pv_time_ops.init_missing_ticks_accounting)
		pv_time_ops.init_missing_ticks_accounting(cpu);
}

static inline int
paravirt_do_steal_accounting(unsigned long *new_itm)
{
	return pv_time_ops.do_steal_accounting(new_itm);
}

static inline unsigned long long paravirt_sched_clock(void)
{
	return pv_time_ops.sched_clock();
}

#endif /* !__ASSEMBLY__ */

#else
/* fallback for native case */

#ifndef __ASSEMBLY__

#define paravirt_banner()				do { } while (0)
#define paravirt_reserve_memory(region)			0

#define paravirt_arch_setup_early()			do { } while (0)
#define paravirt_arch_setup_console(cmdline_p)		do { } while (0)
#define paravirt_arch_setup_nomca()			0
#define paravirt_post_smp_prepare_boot_cpu()		do { } while (0)

#define paravirt_init_missing_ticks_accounting(cpu)	do { } while (0)
#define paravirt_do_steal_accounting(new_itm)		0

#endif /* __ASSEMBLY__ */


#endif /* CONFIG_PARAVIRT_GUEST */

#endif /* __ASM_PARAVIRT_H */
