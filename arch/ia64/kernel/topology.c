/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains NUMA specific variables and functions which can
 * be split away from DISCONTIGMEM and are used on NUMA machines with
 * contiguous memory.
 * 		2002/08/07 Erich Focht <efocht@ess.nec.de>
 * Populate cpu entries in sysfs for non-numa systems as well
 *  	Intel Corporation - Ashok Raj
 */

#include <linux/config.h>
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/nodemask.h>
#include <asm/mmzone.h>
#include <asm/numa.h>
#include <asm/cpu.h>

#ifdef CONFIG_NUMA
static struct node *sysfs_nodes;
#endif
static struct ia64_cpu *sysfs_cpus;

int arch_register_cpu(int num)
{
	struct node *parent = NULL;
	
#ifdef CONFIG_NUMA
	parent = &sysfs_nodes[cpu_to_node(num)];
#endif /* CONFIG_NUMA */

#ifdef CONFIG_ACPI
	/*
	 * If CPEI cannot be re-targetted, and this is
	 * CPEI target, then dont create the control file
	 */
	if (!can_cpei_retarget() && is_cpu_cpei_target(num))
		sysfs_cpus[num].cpu.no_control = 1;
#endif

	return register_cpu(&sysfs_cpus[num].cpu, num, parent);
}

#ifdef CONFIG_HOTPLUG_CPU

void arch_unregister_cpu(int num)
{
	struct node *parent = NULL;

#ifdef CONFIG_NUMA
	int node = cpu_to_node(num);
	parent = &sysfs_nodes[node];
#endif /* CONFIG_NUMA */

	return unregister_cpu(&sysfs_cpus[num].cpu, parent);
}
EXPORT_SYMBOL(arch_register_cpu);
EXPORT_SYMBOL(arch_unregister_cpu);
#endif /*CONFIG_HOTPLUG_CPU*/


static int __init topology_init(void)
{
	int i, err = 0;

#ifdef CONFIG_NUMA
	sysfs_nodes = kmalloc(sizeof(struct node) * MAX_NUMNODES, GFP_KERNEL);
	if (!sysfs_nodes) {
		err = -ENOMEM;
		goto out;
	}
	memset(sysfs_nodes, 0, sizeof(struct node) * MAX_NUMNODES);

	/* MCD - Do we want to register all ONLINE nodes, or all POSSIBLE nodes? */
	for_each_online_node(i)
		if ((err = register_node(&sysfs_nodes[i], i, 0)))
			goto out;
#endif

	sysfs_cpus = kmalloc(sizeof(struct ia64_cpu) * NR_CPUS, GFP_KERNEL);
	if (!sysfs_cpus) {
		err = -ENOMEM;
		goto out;
	}
	memset(sysfs_cpus, 0, sizeof(struct ia64_cpu) * NR_CPUS);

	for_each_present_cpu(i)
		if((err = arch_register_cpu(i)))
			goto out;
out:
	return err;
}

__initcall(topology_init);
