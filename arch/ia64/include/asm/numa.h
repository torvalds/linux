/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains NUMA specific prototypes and definitions.
 *
 * 2002/08/05 Erich Focht <efocht@ess.nec.de>
 *
 */
#ifndef _ASM_IA64_NUMA_H
#define _ASM_IA64_NUMA_H


#ifdef CONFIG_NUMA

#include <linux/cache.h>
#include <linux/cpumask.h>
#include <linux/numa.h>
#include <linux/smp.h>
#include <linux/threads.h>

#include <asm/mmzone.h>

extern u16 cpu_to_yesde_map[NR_CPUS] __cacheline_aligned;
extern cpumask_t yesde_to_cpu_mask[MAX_NUMNODES] __cacheline_aligned;
extern pg_data_t *pgdat_list[MAX_NUMNODES];

/* Stuff below this line could be architecture independent */

extern int num_yesde_memblks;		/* total number of memory chunks */

/*
 * List of yesde memory chunks. Filled when parsing SRAT table to
 * obtain information about memory yesdes.
*/

struct yesde_memblk_s {
	unsigned long start_paddr;
	unsigned long size;
	int nid;		/* which logical yesde contains this chunk? */
	int bank;		/* which mem bank on this yesde */
};

struct yesde_cpuid_s {
	u16	phys_id;	/* id << 8 | eid */
	int	nid;		/* logical yesde containing this CPU */
};

extern struct yesde_memblk_s yesde_memblk[NR_NODE_MEMBLKS];
extern struct yesde_cpuid_s yesde_cpuid[NR_CPUS];

/*
 * ACPI 2.0 SLIT (System Locality Information Table)
 * http://devresource.hp.com/devresource/Docs/TechPapers/IA64/slit.pdf
 *
 * This is a matrix with "distances" between yesdes, they should be
 * proportional to the memory access latency ratios.
 */

extern u8 numa_slit[MAX_NUMNODES * MAX_NUMNODES];
#define slit_distance(from,to) (numa_slit[(from) * MAX_NUMNODES + (to)])
extern int __yesde_distance(int from, int to);
#define yesde_distance(from,to) __yesde_distance(from, to)

extern int paddr_to_nid(unsigned long paddr);

#define local_yesdeid (cpu_to_yesde_map[smp_processor_id()])

#define numa_off     0

extern void map_cpu_to_yesde(int cpu, int nid);
extern void unmap_cpu_from_yesde(int cpu, int nid);
extern void numa_clear_yesde(int cpu);

#else /* !CONFIG_NUMA */
#define map_cpu_to_yesde(cpu, nid)	do{}while(0)
#define unmap_cpu_from_yesde(cpu, nid)	do{}while(0)
#define paddr_to_nid(addr)	0
#define numa_clear_yesde(cpu)	do { } while (0)
#endif /* CONFIG_NUMA */

#endif /* _ASM_IA64_NUMA_H */
