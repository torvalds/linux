/*
 *  PS3 Platform spu routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mmzone.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

/* spu_management_ops */

/**
 * enum spe_type - Type of spe to create.
 * @spe_type_logical: Standard logical spe.
 *
 * For use with lv1_construct_logical_spe().  The current HV does not support
 * any types other than those listed.
 */

enum spe_type {
	SPE_TYPE_LOGICAL = 0,
};

/**
 * struct spe_shadow - logical spe shadow register area.
 *
 * Read-only shadow of spe registers.
 */

struct spe_shadow {
	u8 padding_0000[0x0140];
	u64 int_status_class0_RW;       /* 0x0140 */
	u64 int_status_class1_RW;       /* 0x0148 */
	u64 int_status_class2_RW;       /* 0x0150 */
	u8 padding_0158[0x0610-0x0158];
	u64 mfc_dsisr_RW;               /* 0x0610 */
	u8 padding_0618[0x0620-0x0618];
	u64 mfc_dar_RW;                 /* 0x0620 */
	u8 padding_0628[0x0800-0x0628];
	u64 mfc_dsipr_R;                /* 0x0800 */
	u8 padding_0808[0x0810-0x0808];
	u64 mfc_lscrr_R;                /* 0x0810 */
	u8 padding_0818[0x0c00-0x0818];
	u64 mfc_cer_R;                  /* 0x0c00 */
	u8 padding_0c08[0x0f00-0x0c08];
	u64 spe_execution_status;       /* 0x0f00 */
	u8 padding_0f08[0x1000-0x0f08];
} __attribute__ ((packed));


/**
 * enum spe_ex_state - Logical spe execution state.
 * @spe_ex_state_unexecutable: Uninitialized.
 * @spe_ex_state_executable: Enabled, not ready.
 * @spe_ex_state_executed: Ready for use.
 *
 * The execution state (status) of the logical spe as reported in
 * struct spe_shadow:spe_execution_status.
 */

enum spe_ex_state {
	SPE_EX_STATE_UNEXECUTABLE = 0,
	SPE_EX_STATE_EXECUTABLE = 2,
	SPE_EX_STATE_EXECUTED = 3,
};

/**
 * struct priv1_cache - Cached values of priv1 registers.
 * @masks[]: Array of cached spe interrupt masks, indexed by class.
 * @sr1: Cached mfc_sr1 register.
 * @tclass_id: Cached mfc_tclass_id register.
 */

struct priv1_cache {
	u64 masks[3];
	u64 sr1;
	u64 tclass_id;
};

/**
 * struct spu_pdata - Platform state variables.
 * @spe_id: HV spe id returned by lv1_construct_logical_spe().
 * @resource_id: HV spe resource id returned by
 * 	ps3_repository_read_spe_resource_id().
 * @priv2_addr: lpar address of spe priv2 area returned by
 * 	lv1_construct_logical_spe().
 * @shadow_addr: lpar address of spe register shadow area returned by
 * 	lv1_construct_logical_spe().
 * @shadow: Virtual (ioremap) address of spe register shadow area.
 * @cache: Cached values of priv1 registers.
 */

struct spu_pdata {
	u64 spe_id;
	u64 resource_id;
	u64 priv2_addr;
	u64 shadow_addr;
	struct spe_shadow __iomem *shadow;
	struct priv1_cache cache;
};

static struct spu_pdata *spu_pdata(struct spu *spu)
{
	return spu->pdata;
}

#define dump_areas(_a, _b, _c, _d, _e) \
	_dump_areas(_a, _b, _c, _d, _e, __func__, __LINE__)
static void _dump_areas(unsigned int spe_id, unsigned long priv2,
	unsigned long problem, unsigned long ls, unsigned long shadow,
	const char* func, int line)
{
	pr_debug("%s:%d: spe_id:  %xh (%u)\n", func, line, spe_id, spe_id);
	pr_debug("%s:%d: priv2:   %lxh\n", func, line, priv2);
	pr_debug("%s:%d: problem: %lxh\n", func, line, problem);
	pr_debug("%s:%d: ls:      %lxh\n", func, line, ls);
	pr_debug("%s:%d: shadow:  %lxh\n", func, line, shadow);
}

static unsigned long get_vas_id(void)
{
	unsigned long id;

	lv1_get_logical_ppe_id(&id);
	lv1_get_virtual_address_space_id_of_ppe(id, &id);

	return id;
}

static int __init construct_spu(struct spu *spu)
{
	int result;
	unsigned long unused;

	result = lv1_construct_logical_spe(PAGE_SHIFT, PAGE_SHIFT, PAGE_SHIFT,
		PAGE_SHIFT, PAGE_SHIFT, get_vas_id(), SPE_TYPE_LOGICAL,
		&spu_pdata(spu)->priv2_addr, &spu->problem_phys,
		&spu->local_store_phys, &unused,
		&spu_pdata(spu)->shadow_addr,
		&spu_pdata(spu)->spe_id);

	if (result) {
		pr_debug("%s:%d: lv1_construct_logical_spe failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	return result;
}

static int __init add_spu_pages(unsigned long start_addr, unsigned long size)
{
	int result;
	unsigned long start_pfn;
	unsigned long nr_pages;
	struct pglist_data *pgdata;
	struct zone *zone;

	BUG_ON(!mem_init_done);

	start_pfn = start_addr >> PAGE_SHIFT;
	nr_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	pgdata = NODE_DATA(0);
	zone = pgdata->node_zones;

	result = __add_pages(zone, start_pfn, nr_pages);

	if (result)
		pr_debug("%s:%d: __add_pages failed: (%d)\n",
			__func__, __LINE__, result);

	return result;
}

static void spu_unmap(struct spu *spu)
{
	iounmap(spu->priv2);
	iounmap(spu->problem);
	iounmap((__force u8 __iomem *)spu->local_store);
	iounmap(spu_pdata(spu)->shadow);
}

static int __init setup_areas(struct spu *spu)
{
	struct table {char* name; unsigned long addr; unsigned long size;};
	int result;

	/* setup pages */

	result = add_spu_pages(spu->local_store_phys, LS_SIZE);
	if (result)
		goto fail_add;

	result = add_spu_pages(spu->problem_phys, sizeof(struct spu_problem));
	if (result)
		goto fail_add;

	/* ioremap */

	spu_pdata(spu)->shadow = __ioremap(
		spu_pdata(spu)->shadow_addr, sizeof(struct spe_shadow),
		PAGE_READONLY | _PAGE_NO_CACHE | _PAGE_GUARDED);
	if (!spu_pdata(spu)->shadow) {
		pr_debug("%s:%d: ioremap shadow failed\n", __func__, __LINE__);
		goto fail_ioremap;
	}

	spu->local_store = ioremap(spu->local_store_phys, LS_SIZE);
	if (!spu->local_store) {
		pr_debug("%s:%d: ioremap local_store failed\n",
			__func__, __LINE__);
		goto fail_ioremap;
	}

	spu->problem = ioremap(spu->problem_phys,
		sizeof(struct spu_problem));
	if (!spu->problem) {
		pr_debug("%s:%d: ioremap problem failed\n", __func__, __LINE__);
		goto fail_ioremap;
	}

	spu->priv2 = ioremap(spu_pdata(spu)->priv2_addr,
		sizeof(struct spu_priv2));
	if (!spu->priv2) {
		pr_debug("%s:%d: ioremap priv2 failed\n", __func__, __LINE__);
		goto fail_ioremap;
	}

	dump_areas(spu_pdata(spu)->spe_id, spu_pdata(spu)->priv2_addr,
		spu->problem_phys, spu->local_store_phys,
		spu_pdata(spu)->shadow_addr);
	dump_areas(spu_pdata(spu)->spe_id, (unsigned long)spu->priv2,
		(unsigned long)spu->problem, (unsigned long)spu->local_store,
		(unsigned long)spu_pdata(spu)->shadow);

	return 0;

fail_ioremap:
	spu_unmap(spu);
fail_add:
	return result;
}

static int __init setup_interrupts(struct spu *spu)
{
	int result;

	result = ps3_alloc_spe_irq(spu_pdata(spu)->spe_id, 0,
		&spu->irqs[0]);

	if (result)
		goto fail_alloc_0;

	result = ps3_alloc_spe_irq(spu_pdata(spu)->spe_id, 1,
		&spu->irqs[1]);

	if (result)
		goto fail_alloc_1;

	result = ps3_alloc_spe_irq(spu_pdata(spu)->spe_id, 2,
		&spu->irqs[2]);

	if (result)
		goto fail_alloc_2;

	return result;

fail_alloc_2:
	ps3_free_spe_irq(spu->irqs[1]);
fail_alloc_1:
	ps3_free_spe_irq(spu->irqs[0]);
fail_alloc_0:
	spu->irqs[0] = spu->irqs[1] = spu->irqs[2] = NO_IRQ;
	return result;
}

static int __init enable_spu(struct spu *spu)
{
	int result;

	result = lv1_enable_logical_spe(spu_pdata(spu)->spe_id,
		spu_pdata(spu)->resource_id);

	if (result) {
		pr_debug("%s:%d: lv1_enable_logical_spe failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		goto fail_enable;
	}

	result = setup_areas(spu);

	if (result)
		goto fail_areas;

	result = setup_interrupts(spu);

	if (result)
		goto fail_interrupts;

	return 0;

fail_interrupts:
	spu_unmap(spu);
fail_areas:
	lv1_disable_logical_spe(spu_pdata(spu)->spe_id, 0);
fail_enable:
	return result;
}

static int ps3_destroy_spu(struct spu *spu)
{
	int result;

	pr_debug("%s:%d spu_%d\n", __func__, __LINE__, spu->number);

	result = lv1_disable_logical_spe(spu_pdata(spu)->spe_id, 0);
	BUG_ON(result);

	ps3_free_spe_irq(spu->irqs[2]);
	ps3_free_spe_irq(spu->irqs[1]);
	ps3_free_spe_irq(spu->irqs[0]);

	spu->irqs[0] = spu->irqs[1] = spu->irqs[2] = NO_IRQ;

	spu_unmap(spu);

	result = lv1_destruct_logical_spe(spu_pdata(spu)->spe_id);
	BUG_ON(result);

	kfree(spu->pdata);
	spu->pdata = NULL;

	return 0;
}

static int __init ps3_create_spu(struct spu *spu, void *data)
{
	int result;

	pr_debug("%s:%d spu_%d\n", __func__, __LINE__, spu->number);

	spu->pdata = kzalloc(sizeof(struct spu_pdata),
		GFP_KERNEL);

	if (!spu->pdata) {
		result = -ENOMEM;
		goto fail_malloc;
	}

	spu_pdata(spu)->resource_id = (unsigned long)data;

	/* Init cached reg values to HV defaults. */

	spu_pdata(spu)->cache.sr1 = 0x33;

	result = construct_spu(spu);

	if (result)
		goto fail_construct;

	/* For now, just go ahead and enable it. */

	result = enable_spu(spu);

	if (result)
		goto fail_enable;

	/* Make sure the spu is in SPE_EX_STATE_EXECUTED. */

	/* need something better here!!! */
	while (in_be64(&spu_pdata(spu)->shadow->spe_execution_status)
		!= SPE_EX_STATE_EXECUTED)
		(void)0;

	return result;

fail_enable:
fail_construct:
	ps3_destroy_spu(spu);
fail_malloc:
	return result;
}

static int __init ps3_enumerate_spus(int (*fn)(void *data))
{
	int result;
	unsigned int num_resource_id;
	unsigned int i;

	result = ps3_repository_read_num_spu_resource_id(&num_resource_id);

	pr_debug("%s:%d: num_resource_id %u\n", __func__, __LINE__,
		num_resource_id);

	/*
	 * For now, just create logical spus equal to the number
	 * of physical spus reserved for the partition.
	 */

	for (i = 0; i < num_resource_id; i++) {
		enum ps3_spu_resource_type resource_type;
		unsigned int resource_id;

		result = ps3_repository_read_spu_resource_id(i,
			&resource_type, &resource_id);

		if (result)
			break;

		if (resource_type == PS3_SPU_RESOURCE_TYPE_EXCLUSIVE) {
			result = fn((void*)(unsigned long)resource_id);

			if (result)
				break;
		}
	}

	if (result)
		printk(KERN_WARNING "%s:%d: Error initializing spus\n",
			__func__, __LINE__);

	return result;
}

const struct spu_management_ops spu_management_ps3_ops = {
	.enumerate_spus = ps3_enumerate_spus,
	.create_spu = ps3_create_spu,
	.destroy_spu = ps3_destroy_spu,
};

/* spu_priv1_ops */

static void int_mask_and(struct spu *spu, int class, u64 mask)
{
	u64 old_mask;

	/* are these serialized by caller??? */
	old_mask = spu_int_mask_get(spu, class);
	spu_int_mask_set(spu, class, old_mask & mask);
}

static void int_mask_or(struct spu *spu, int class, u64 mask)
{
	u64 old_mask;

	old_mask = spu_int_mask_get(spu, class);
	spu_int_mask_set(spu, class, old_mask | mask);
}

static void int_mask_set(struct spu *spu, int class, u64 mask)
{
	spu_pdata(spu)->cache.masks[class] = mask;
	lv1_set_spe_interrupt_mask(spu_pdata(spu)->spe_id, class,
		spu_pdata(spu)->cache.masks[class]);
}

static u64 int_mask_get(struct spu *spu, int class)
{
	return spu_pdata(spu)->cache.masks[class];
}

static void int_stat_clear(struct spu *spu, int class, u64 stat)
{
	/* Note that MFC_DSISR will be cleared when class1[MF] is set. */

	lv1_clear_spe_interrupt_status(spu_pdata(spu)->spe_id, class,
		stat, 0);
}

static u64 int_stat_get(struct spu *spu, int class)
{
	u64 stat;

	lv1_get_spe_interrupt_status(spu_pdata(spu)->spe_id, class, &stat);
	return stat;
}

static void cpu_affinity_set(struct spu *spu, int cpu)
{
	/* No support. */
}

static u64 mfc_dar_get(struct spu *spu)
{
	return in_be64(&spu_pdata(spu)->shadow->mfc_dar_RW);
}

static void mfc_dsisr_set(struct spu *spu, u64 dsisr)
{
	/* Nothing to do, cleared in int_stat_clear(). */
}

static u64 mfc_dsisr_get(struct spu *spu)
{
	return in_be64(&spu_pdata(spu)->shadow->mfc_dsisr_RW);
}

static void mfc_sdr_setup(struct spu *spu)
{
	/* Nothing to do. */
}

static void mfc_sr1_set(struct spu *spu, u64 sr1)
{
	/* Check bits allowed by HV. */

	static const u64 allowed = ~(MFC_STATE1_LOCAL_STORAGE_DECODE_MASK
		| MFC_STATE1_PROBLEM_STATE_MASK);

	BUG_ON((sr1 & allowed) != (spu_pdata(spu)->cache.sr1 & allowed));

	spu_pdata(spu)->cache.sr1 = sr1;
	lv1_set_spe_privilege_state_area_1_register(
		spu_pdata(spu)->spe_id,
		offsetof(struct spu_priv1, mfc_sr1_RW),
		spu_pdata(spu)->cache.sr1);
}

static u64 mfc_sr1_get(struct spu *spu)
{
	return spu_pdata(spu)->cache.sr1;
}

static void mfc_tclass_id_set(struct spu *spu, u64 tclass_id)
{
	spu_pdata(spu)->cache.tclass_id = tclass_id;
	lv1_set_spe_privilege_state_area_1_register(
		spu_pdata(spu)->spe_id,
		offsetof(struct spu_priv1, mfc_tclass_id_RW),
		spu_pdata(spu)->cache.tclass_id);
}

static u64 mfc_tclass_id_get(struct spu *spu)
{
	return spu_pdata(spu)->cache.tclass_id;
}

static void tlb_invalidate(struct spu *spu)
{
	/* Nothing to do. */
}

static void resource_allocation_groupID_set(struct spu *spu, u64 id)
{
	/* No support. */
}

static u64 resource_allocation_groupID_get(struct spu *spu)
{
	return 0; /* No support. */
}

static void resource_allocation_enable_set(struct spu *spu, u64 enable)
{
	/* No support. */
}

static u64 resource_allocation_enable_get(struct spu *spu)
{
	return 0; /* No support. */
}

const struct spu_priv1_ops spu_priv1_ps3_ops = {
	.int_mask_and = int_mask_and,
	.int_mask_or = int_mask_or,
	.int_mask_set = int_mask_set,
	.int_mask_get = int_mask_get,
	.int_stat_clear = int_stat_clear,
	.int_stat_get = int_stat_get,
	.cpu_affinity_set = cpu_affinity_set,
	.mfc_dar_get = mfc_dar_get,
	.mfc_dsisr_set = mfc_dsisr_set,
	.mfc_dsisr_get = mfc_dsisr_get,
	.mfc_sdr_setup = mfc_sdr_setup,
	.mfc_sr1_set = mfc_sr1_set,
	.mfc_sr1_get = mfc_sr1_get,
	.mfc_tclass_id_set = mfc_tclass_id_set,
	.mfc_tclass_id_get = mfc_tclass_id_get,
	.tlb_invalidate = tlb_invalidate,
	.resource_allocation_groupID_set = resource_allocation_groupID_set,
	.resource_allocation_groupID_get = resource_allocation_groupID_get,
	.resource_allocation_enable_set = resource_allocation_enable_set,
	.resource_allocation_enable_get = resource_allocation_enable_get,
};

void ps3_spu_set_platform(void)
{
	spu_priv1_ops = &spu_priv1_ps3_ops;
	spu_management_ops = &spu_management_ps3_ops;
}
