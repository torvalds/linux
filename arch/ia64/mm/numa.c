/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains NUMA specific variables and functions which can
 * be split away from DISCONTIGMEM and are used on NUMA machines with
 * contiguous memory.
 * 
 *                         2002/08/07 Erich Focht <efocht@ess.nec.de>
 */

#include <linux/config.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/mmzone.h>
#include <asm/numa.h>


/*
 * The following structures are usually initialized by ACPI or
 * similar mechanisms and describe the NUMA characteristics of the machine.
 */
int num_node_memblks;
struct node_memblk_s node_memblk[NR_NODE_MEMBLKS];
struct node_cpuid_s node_cpuid[NR_CPUS];
/*
 * This is a matrix with "distances" between nodes, they should be
 * proportional to the memory access latency ratios.
 */
u8 numa_slit[MAX_NUMNODES * MAX_NUMNODES];

/* Identify which cnode a physical address resides on */
int
paddr_to_nid(unsigned long paddr)
{
	int	i;

	for (i = 0; i < num_node_memblks; i++)
		if (paddr >= node_memblk[i].start_paddr &&
		    paddr < node_memblk[i].start_paddr + node_memblk[i].size)
			break;

	return (i < num_node_memblks) ? node_memblk[i].nid : (num_node_memblks ? -1 : 0);
}
