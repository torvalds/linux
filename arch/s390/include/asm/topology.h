#ifndef _ASM_S390_TOPOLOGY_H
#define _ASM_S390_TOPOLOGY_H

#include <linux/cpumask.h>

struct sysinfo_15_1_x;
struct cpu;

#ifdef CONFIG_SCHED_BOOK

struct cpu_topology_s390 {
	unsigned short thread_id;
	unsigned short core_id;
	unsigned short socket_id;
	unsigned short book_id;
	cpumask_t thread_mask;
	cpumask_t core_mask;
	cpumask_t book_mask;
};

extern struct cpu_topology_s390 cpu_topology[NR_CPUS];

#define topology_physical_package_id(cpu)	(cpu_topology[cpu].socket_id)
#define topology_thread_id(cpu)			(cpu_topology[cpu].thread_id)
#define topology_thread_cpumask(cpu)		(&cpu_topology[cpu].thread_mask)
#define topology_core_id(cpu)			(cpu_topology[cpu].core_id)
#define topology_core_cpumask(cpu)		(&cpu_topology[cpu].core_mask)
#define topology_book_id(cpu)			(cpu_topology[cpu].book_id)
#define topology_book_cpumask(cpu)		(&cpu_topology[cpu].book_mask)

#define mc_capable() 1

int topology_cpu_init(struct cpu *);
int topology_set_cpu_management(int fc);
void topology_schedule_update(void);
void store_topology(struct sysinfo_15_1_x *info);
void topology_expect_change(void);
const struct cpumask *cpu_coregroup_mask(int cpu);

#else /* CONFIG_SCHED_BOOK */

static inline void topology_schedule_update(void) { }
static inline int topology_cpu_init(struct cpu *cpu) { return 0; }
static inline void topology_expect_change(void) { }

#endif /* CONFIG_SCHED_BOOK */

#define POLARIZATION_UNKNOWN	(-1)
#define POLARIZATION_HRZ	(0)
#define POLARIZATION_VL		(1)
#define POLARIZATION_VM		(2)
#define POLARIZATION_VH		(3)

#ifdef CONFIG_SCHED_BOOK
void s390_init_cpu_topology(void);
#else
static inline void s390_init_cpu_topology(void)
{
};
#endif

#include <asm-generic/topology.h>

#endif /* _ASM_S390_TOPOLOGY_H */
