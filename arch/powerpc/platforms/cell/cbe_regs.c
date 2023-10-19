// SPDX-License-Identifier: GPL-2.0-only
/*
 * cbe_regs.c
 *
 * Accessor routines for the various MMIO register blocks of the CBE
 *
 * (c) 2006 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 */

#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/pgtable.h>

#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/cell-regs.h>

/*
 * Current implementation uses "cpu" nodes. We build our own mapping
 * array of cpu numbers to cpu nodes locally for now to allow interrupt
 * time code to have a fast path rather than call of_get_cpu_node(). If
 * we implement cpu hotplug, we'll have to install an appropriate notifier
 * in order to release references to the cpu going away
 */
static struct cbe_regs_map
{
	struct device_node *cpu_node;
	struct device_node *be_node;
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct cbe_iic_regs __iomem *iic_regs;
	struct cbe_mic_tm_regs __iomem *mic_tm_regs;
	struct cbe_pmd_shadow_regs pmd_shadow_regs;
} cbe_regs_maps[MAX_CBE];
static int cbe_regs_map_count;

static struct cbe_thread_map
{
	struct device_node *cpu_node;
	struct device_node *be_node;
	struct cbe_regs_map *regs;
	unsigned int thread_id;
	unsigned int cbe_id;
} cbe_thread_map[NR_CPUS];

static cpumask_t cbe_local_mask[MAX_CBE] = { [0 ... MAX_CBE-1] = {CPU_BITS_NONE} };
static cpumask_t cbe_first_online_cpu = { CPU_BITS_NONE };

static struct cbe_regs_map *cbe_find_map(struct device_node *np)
{
	int i;
	struct device_node *tmp_np;

	if (!of_node_is_type(np, "spe")) {
		for (i = 0; i < cbe_regs_map_count; i++)
			if (cbe_regs_maps[i].cpu_node == np ||
			    cbe_regs_maps[i].be_node == np)
				return &cbe_regs_maps[i];
		return NULL;
	}

	if (np->data)
		return np->data;

	/* walk up path until cpu or be node was found */
	tmp_np = np;
	do {
		tmp_np = tmp_np->parent;
		/* on a correct devicetree we wont get up to root */
		BUG_ON(!tmp_np);
	} while (!of_node_is_type(tmp_np, "cpu") ||
		 !of_node_is_type(tmp_np, "be"));

	np->data = cbe_find_map(tmp_np);

	return np->data;
}

struct cbe_pmd_regs __iomem *cbe_get_pmd_regs(struct device_node *np)
{
	struct cbe_regs_map *map = cbe_find_map(np);
	if (map == NULL)
		return NULL;
	return map->pmd_regs;
}
EXPORT_SYMBOL_GPL(cbe_get_pmd_regs);

struct cbe_pmd_regs __iomem *cbe_get_cpu_pmd_regs(int cpu)
{
	struct cbe_regs_map *map = cbe_thread_map[cpu].regs;
	if (map == NULL)
		return NULL;
	return map->pmd_regs;
}
EXPORT_SYMBOL_GPL(cbe_get_cpu_pmd_regs);

struct cbe_pmd_shadow_regs *cbe_get_pmd_shadow_regs(struct device_node *np)
{
	struct cbe_regs_map *map = cbe_find_map(np);
	if (map == NULL)
		return NULL;
	return &map->pmd_shadow_regs;
}

struct cbe_pmd_shadow_regs *cbe_get_cpu_pmd_shadow_regs(int cpu)
{
	struct cbe_regs_map *map = cbe_thread_map[cpu].regs;
	if (map == NULL)
		return NULL;
	return &map->pmd_shadow_regs;
}

struct cbe_iic_regs __iomem *cbe_get_iic_regs(struct device_node *np)
{
	struct cbe_regs_map *map = cbe_find_map(np);
	if (map == NULL)
		return NULL;
	return map->iic_regs;
}

struct cbe_iic_regs __iomem *cbe_get_cpu_iic_regs(int cpu)
{
	struct cbe_regs_map *map = cbe_thread_map[cpu].regs;
	if (map == NULL)
		return NULL;
	return map->iic_regs;
}

struct cbe_mic_tm_regs __iomem *cbe_get_mic_tm_regs(struct device_node *np)
{
	struct cbe_regs_map *map = cbe_find_map(np);
	if (map == NULL)
		return NULL;
	return map->mic_tm_regs;
}

struct cbe_mic_tm_regs __iomem *cbe_get_cpu_mic_tm_regs(int cpu)
{
	struct cbe_regs_map *map = cbe_thread_map[cpu].regs;
	if (map == NULL)
		return NULL;
	return map->mic_tm_regs;
}
EXPORT_SYMBOL_GPL(cbe_get_cpu_mic_tm_regs);

u32 cbe_get_hw_thread_id(int cpu)
{
	return cbe_thread_map[cpu].thread_id;
}
EXPORT_SYMBOL_GPL(cbe_get_hw_thread_id);

u32 cbe_cpu_to_node(int cpu)
{
	return cbe_thread_map[cpu].cbe_id;
}
EXPORT_SYMBOL_GPL(cbe_cpu_to_node);

u32 cbe_node_to_cpu(int node)
{
	return cpumask_first(&cbe_local_mask[node]);

}
EXPORT_SYMBOL_GPL(cbe_node_to_cpu);

static struct device_node *__init cbe_get_be_node(int cpu_id)
{
	struct device_node *np;

	for_each_node_by_type (np, "be") {
		int len,i;
		const phandle *cpu_handle;

		cpu_handle = of_get_property(np, "cpus", &len);

		/*
		 * the CAB SLOF tree is non compliant, so we just assume
		 * there is only one node
		 */
		if (WARN_ON_ONCE(!cpu_handle))
			return np;

		for (i = 0; i < len; i++) {
			struct device_node *ch_np = of_find_node_by_phandle(cpu_handle[i]);
			struct device_node *ci_np = of_get_cpu_node(cpu_id, NULL);

			of_node_put(ch_np);
			of_node_put(ci_np);

			if (ch_np == ci_np)
				return np;
		}
	}

	return NULL;
}

static void __init cbe_fill_regs_map(struct cbe_regs_map *map)
{
	if(map->be_node) {
		struct device_node *be, *np, *parent_np;

		be = map->be_node;

		for_each_node_by_type(np, "pervasive") {
			parent_np = of_get_parent(np);
			if (parent_np == be)
				map->pmd_regs = of_iomap(np, 0);
			of_node_put(parent_np);
		}

		for_each_node_by_type(np, "CBEA-Internal-Interrupt-Controller") {
			parent_np = of_get_parent(np);
			if (parent_np == be)
				map->iic_regs = of_iomap(np, 2);
			of_node_put(parent_np);
		}

		for_each_node_by_type(np, "mic-tm") {
			parent_np = of_get_parent(np);
			if (parent_np == be)
				map->mic_tm_regs = of_iomap(np, 0);
			of_node_put(parent_np);
		}
	} else {
		struct device_node *cpu;
		/* That hack must die die die ! */
		const struct address_prop {
			unsigned long address;
			unsigned int len;
		} __attribute__((packed)) *prop;

		cpu = map->cpu_node;

		prop = of_get_property(cpu, "pervasive", NULL);
		if (prop != NULL)
			map->pmd_regs = ioremap(prop->address, prop->len);

		prop = of_get_property(cpu, "iic", NULL);
		if (prop != NULL)
			map->iic_regs = ioremap(prop->address, prop->len);

		prop = of_get_property(cpu, "mic-tm", NULL);
		if (prop != NULL)
			map->mic_tm_regs = ioremap(prop->address, prop->len);
	}
}


void __init cbe_regs_init(void)
{
	int i;
	unsigned int thread_id;
	struct device_node *cpu;

	/* Build local fast map of CPUs */
	for_each_possible_cpu(i) {
		cbe_thread_map[i].cpu_node = of_get_cpu_node(i, &thread_id);
		cbe_thread_map[i].be_node = cbe_get_be_node(i);
		cbe_thread_map[i].thread_id = thread_id;
	}

	/* Find maps for each device tree CPU */
	for_each_node_by_type(cpu, "cpu") {
		struct cbe_regs_map *map;
		unsigned int cbe_id;

		cbe_id = cbe_regs_map_count++;
		map = &cbe_regs_maps[cbe_id];

		if (cbe_regs_map_count > MAX_CBE) {
			printk(KERN_ERR "cbe_regs: More BE chips than supported"
			       "!\n");
			cbe_regs_map_count--;
			of_node_put(cpu);
			return;
		}
		of_node_put(map->cpu_node);
		map->cpu_node = of_node_get(cpu);

		for_each_possible_cpu(i) {
			struct cbe_thread_map *thread = &cbe_thread_map[i];

			if (thread->cpu_node == cpu) {
				thread->regs = map;
				thread->cbe_id = cbe_id;
				map->be_node = thread->be_node;
				cpumask_set_cpu(i, &cbe_local_mask[cbe_id]);
				if(thread->thread_id == 0)
					cpumask_set_cpu(i, &cbe_first_online_cpu);
			}
		}

		cbe_fill_regs_map(map);
	}
}

