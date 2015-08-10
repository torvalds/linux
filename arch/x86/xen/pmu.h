#ifndef __XEN_PMU_H
#define __XEN_PMU_H

#include <xen/interface/xenpmu.h>

irqreturn_t xen_pmu_irq_handler(int irq, void *dev_id);
void xen_pmu_init(int cpu);
void xen_pmu_finish(int cpu);
bool is_xen_pmu(int cpu);

#endif /* __XEN_PMU_H */
