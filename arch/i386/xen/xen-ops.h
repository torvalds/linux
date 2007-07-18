#ifndef XEN_OPS_H
#define XEN_OPS_H

#include <linux/init.h>
#include <linux/clocksource.h>

DECLARE_PER_CPU(struct vcpu_info *, xen_vcpu);
DECLARE_PER_CPU(unsigned long, xen_cr3);

extern struct start_info *xen_start_info;
extern struct shared_info *HYPERVISOR_shared_info;

char * __init xen_memory_setup(void);
void __init xen_arch_setup(void);
void __init xen_init_IRQ(void);

unsigned long xen_cpu_khz(void);
void __init xen_time_init(void);
unsigned long xen_get_wallclock(void);
int xen_set_wallclock(unsigned long time);
cycle_t xen_clocksource_read(void);

void xen_mark_init_mm_pinned(void);

DECLARE_PER_CPU(enum paravirt_lazy_mode, xen_lazy_mode);

static inline unsigned xen_get_lazy_mode(void)
{
	return x86_read_percpu(xen_lazy_mode);
}


#endif /* XEN_OPS_H */
