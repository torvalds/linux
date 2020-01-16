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
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/cell-regs.h>

/*
 * Current implementation uses "cpu" yesdes. We build our own mapping
 * array of cpu numbers to cpu yesdes locally for yesw to allow interrupt
 * time code to have a fast path rather than call of_get_cpu_yesde(). If
 * we implement cpu hotplug, we'll have to install an appropriate yesrifier
 * in order to release references to the cpu going away
 */
static struct cbe_regs_map
{
	struct device_yesde *cpu_yesde;
	struct device_yesde *be_yesde;
	struct cbe_pmd_regs __iomem *pmd_regs;
	struct cbe_iic_regs __iomem *iic_regs;
	struct cbe_mic_tm_regs __iomem *mic_tm_regs;
	struct cbe_pmd_shadow_regs pmd_shadow_regs;
} cbe_regs_maps[MAX_CBE];
static int cbe_regs_map_count;

static struct cbe_thread_map
{
	struct device_yesde *cpu_yesde;
	struct device_yesde *be_yesde;
	struct cbe_regs_map *regs;
	unsigned int thread_id;
	unsigned int cbe_id;
} cbe_thread_map[NR_CPUS];

static cpumask_t cbe_local_mask[MAX_CBE] = { [0 ... MAX_CBE-1] = {CPU_BITS_NONE} };
static cpumask_t cbe_first_online_cpu = { CPU_BITS_NONE };

static struct cbe_regs_map *cbe_find_map(struct device_yesde *np)
{
	int i;
	struct device_yesde *tmp_np;

	if (!of_yesde_is_type(np, "spe")) {
		for (i = 0; i < cbe_regs_map_count; i++)
			if (cbe_regs_maps[i].cpu_yesde == np ||
			    cbe_regs_maps[i].be_yesde == np)
				return &cbe_regs_maps[i];
		return NULL;
	}

	if (np->data)
		return np->data;

	/* walk up path until cpu or be yesde was found */
	tmp_np = np;
	do {
		tmp_np = tmp_np->parent;
		/* on a correct devicetree we wont get up to root */
		BUG_ON(!tmp_np);
	} while (!of_yesde_is_type(tmp_np, "cpu") ||
		 !of_yesde_is_type(tmp_np, "be"));

	np->data = cbe_find_map(tmp_np);

	return np->data;
}

struct cbe_pmd_regs __iomem *cbe_get_pmd_regs(struct device_yesde *np)
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

struct cbe_pmd_shadow_regs *cbe_get_pmd_shadow_regs(struct device_yesde *np)
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

struct cbe_iic_regs __iomem *cbe_get_iic_regs(struct device_yesde *np)
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

struct cbe_mic_tm_regs __iomem *cbe_get_mic_tm_regs(struct device_yesde *np)
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

u32 cbe_cpu_to_yesde(int cpu)
{
	return cbe_thread_map[cpu].cbe_id;
}
EXPORT_SYMBOL_GPL(cbe_cpu_to_yesde);

u32 cbe_yesde_to_cpu(int yesde)
{
	return cpumask_first(&cbe_local_mask[yesde]);

}
EXPORT_SYMBOL_GPL(cbe_yesde_to_cpu);

static struct device_yesde *cbe_get_be_yesde(int cpu_id)
{
	struct device_yesde *np;

	for_each_yesde_by_type (np, "be") {
		int len,i;
		const phandle *cpu_handle;

		cpu_handle = of_get_property(np, "cpus", &len);

		/*
		 * the CAB SLOF tree is yesn compliant, so we just assume
		 * there is only one yesde
		 */
		if (WARN_ON_ONCE(!cpu_handle))
			return np;

		for (i=0; i<len; i++)
			if (of_find_yesde_by_phandle(cpu_handle[i]) == of_get_cpu_yesde(cpu_id, NULL))
				return np;
	}

	return NULL;
}

static void __init cbe_fill_regs_map(struct cbe_regs_map *map)
{
	if(map->be_yesde) {
		struct device_yesde *be, *np;

		be = map->be_yesde;

		for_each_yesde_by_type(np, "pervasive")
			if (of_get_parent(np) == be)
				map->pmd_regs = of_iomap(np, 0);

		for_each_yesde_by_type(np, "CBEA-Internal-Interrupt-Controller")
			if (of_get_parent(np) == be)
				map->iic_regs = of_iomap(np, 2);

		for_each_yesde_by_type(np, "mic-tm")
			if (of_get_parent(np) == be)
				map->mic_tm_regs = of_iomap(np, 0);
	} else {
		struct device_yesde *cpu;
		/* That hack must die die die ! */
		const struct address_prop {
			unsigned long address;
			unsigned int len;
		} __attribute__((packed)) *prop;

		cpu = map->cpu_yesde;

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
	struct device_yesde *cpu;

	/* Build local fast map of CPUs */
	for_each_possible_cpu(i) {
		cbe_thread_map[i].cpu_yesde = of_get_cpu_yesde(i, &thread_id);
		cbe_thread_map[i].be_yesde = cbe_get_be_yesde(i);
		cbe_thread_map[i].thread_id = thread_id;
	}

	/* Find maps for each device tree CPU */
	for_each_yesde_by_type(cpu, "cpu") {
		struct cbe_regs_map *map;
		unsigned int cbe_id;

		cbe_id = cbe_regs_map_count++;
		map = &cbe_regs_maps[cbe_id];

		if (cbe_regs_map_count > MAX_CBE) {
			printk(KERN_ERR "cbe_regs: More BE chips than supported"
			       "!\n");
			cbe_regs_map_count--;
			of_yesde_put(cpu);
			return;
		}
		map->cpu_yesde = cpu;

		for_each_possible_cpu(i) {
			struct cbe_thread_map *thread = &cbe_thread_map[i];

			if (thread->cpu_yesde == cpu) {
				thread->regs = map;
				thread->cbe_id = cbe_id;
				map->be_yesde = thread->be_yesde;
				cpumask_set_cpu(i, &cbe_local_mask[cbe_id]);
				if(thread->thread_id == 0)
					cpumask_set_cpu(i, &cbe_first_online_cpu);
			}
		}

		cbe_fill_regs_map(map);
	}
}

