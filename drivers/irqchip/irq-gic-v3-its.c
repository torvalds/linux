/*
 * Copyright (C) 2013, 2014 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitmap.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/dma-iommu.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v3.h>

#include <asm/cputype.h>
#include <asm/exception.h>

#include "irq-gic-common.h"

#define ITS_FLAGS_CMDQ_NEEDS_FLUSHING		(1ULL << 0)
#define ITS_FLAGS_WORKAROUND_CAVIUM_22375	(1ULL << 1)
#define ITS_FLAGS_WORKAROUND_CAVIUM_23144	(1ULL << 2)

#define RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING	(1 << 0)

/*
 * Collection structure - just an ID, and a redistributor address to
 * ping. We use one per CPU as a bag of interrupts assigned to this
 * CPU.
 */
struct its_collection {
	u64			target_address;
	u16			col_id;
};

/*
 * The ITS_BASER structure - contains memory information, cached
 * value of BASER register configuration and ITS page size.
 */
struct its_baser {
	void		*base;
	u64		val;
	u32		order;
	u32		psz;
};

/*
 * The ITS structure - contains most of the infrastructure, with the
 * top-level MSI domain, the command queue, the collections, and the
 * list of devices writing to it.
 */
struct its_node {
	raw_spinlock_t		lock;
	struct list_head	entry;
	void __iomem		*base;
	phys_addr_t		phys_base;
	struct its_cmd_block	*cmd_base;
	struct its_cmd_block	*cmd_write;
	struct its_baser	tables[GITS_BASER_NR_REGS];
	struct its_collection	*collections;
	struct list_head	its_device_list;
	u64			flags;
	u32			ite_size;
	u32			device_ids;
	int			numa_node;
};

#define ITS_ITT_ALIGN		SZ_256

/* Convert page order to size in bytes */
#define PAGE_ORDER_TO_SIZE(o)	(PAGE_SIZE << (o))

struct event_lpi_map {
	unsigned long		*lpi_map;
	u16			*col_map;
	irq_hw_number_t		lpi_base;
	int			nr_lpis;
};

/*
 * The ITS view of a device - belongs to an ITS, a collection, owns an
 * interrupt translation table, and a list of interrupts.
 */
struct its_device {
	struct list_head	entry;
	struct its_node		*its;
	struct event_lpi_map	event_map;
	void			*itt;
	u32			nr_ites;
	u32			device_id;
};

static LIST_HEAD(its_nodes);
static DEFINE_SPINLOCK(its_lock);
static struct rdists *gic_rdists;
static struct irq_domain *its_parent;

#define gic_data_rdist()		(raw_cpu_ptr(gic_rdists->rdist))
#define gic_data_rdist_rd_base()	(gic_data_rdist()->rd_base)

static struct its_collection *dev_event_to_col(struct its_device *its_dev,
					       u32 event)
{
	struct its_node *its = its_dev->its;

	return its->collections + its_dev->event_map.col_map[event];
}

/*
 * ITS command descriptors - parameters to be encoded in a command
 * block.
 */
struct its_cmd_desc {
	union {
		struct {
			struct its_device *dev;
			u32 event_id;
		} its_inv_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_int_cmd;

		struct {
			struct its_device *dev;
			int valid;
		} its_mapd_cmd;

		struct {
			struct its_collection *col;
			int valid;
		} its_mapc_cmd;

		struct {
			struct its_device *dev;
			u32 phys_id;
			u32 event_id;
		} its_mapti_cmd;

		struct {
			struct its_device *dev;
			struct its_collection *col;
			u32 event_id;
		} its_movi_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_discard_cmd;

		struct {
			struct its_collection *col;
		} its_invall_cmd;
	};
};

/*
 * The ITS command block, which is what the ITS actually parses.
 */
struct its_cmd_block {
	u64	raw_cmd[4];
};

#define ITS_CMD_QUEUE_SZ		SZ_64K
#define ITS_CMD_QUEUE_NR_ENTRIES	(ITS_CMD_QUEUE_SZ / sizeof(struct its_cmd_block))

typedef struct its_collection *(*its_cmd_builder_t)(struct its_cmd_block *,
						    struct its_cmd_desc *);

static void its_mask_encode(u64 *raw_cmd, u64 val, int h, int l)
{
	u64 mask = GENMASK_ULL(h, l);
	*raw_cmd &= ~mask;
	*raw_cmd |= (val << l) & mask;
}

static void its_encode_cmd(struct its_cmd_block *cmd, u8 cmd_nr)
{
	its_mask_encode(&cmd->raw_cmd[0], cmd_nr, 7, 0);
}

static void its_encode_devid(struct its_cmd_block *cmd, u32 devid)
{
	its_mask_encode(&cmd->raw_cmd[0], devid, 63, 32);
}

static void its_encode_event_id(struct its_cmd_block *cmd, u32 id)
{
	its_mask_encode(&cmd->raw_cmd[1], id, 31, 0);
}

static void its_encode_phys_id(struct its_cmd_block *cmd, u32 phys_id)
{
	its_mask_encode(&cmd->raw_cmd[1], phys_id, 63, 32);
}

static void its_encode_size(struct its_cmd_block *cmd, u8 size)
{
	its_mask_encode(&cmd->raw_cmd[1], size, 4, 0);
}

static void its_encode_itt(struct its_cmd_block *cmd, u64 itt_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], itt_addr >> 8, 50, 8);
}

static void its_encode_valid(struct its_cmd_block *cmd, int valid)
{
	its_mask_encode(&cmd->raw_cmd[2], !!valid, 63, 63);
}

static void its_encode_target(struct its_cmd_block *cmd, u64 target_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], target_addr >> 16, 50, 16);
}

static void its_encode_collection(struct its_cmd_block *cmd, u16 col)
{
	its_mask_encode(&cmd->raw_cmd[2], col, 15, 0);
}

static inline void its_fixup_cmd(struct its_cmd_block *cmd)
{
	/* Let's fixup BE commands */
	cmd->raw_cmd[0] = cpu_to_le64(cmd->raw_cmd[0]);
	cmd->raw_cmd[1] = cpu_to_le64(cmd->raw_cmd[1]);
	cmd->raw_cmd[2] = cpu_to_le64(cmd->raw_cmd[2]);
	cmd->raw_cmd[3] = cpu_to_le64(cmd->raw_cmd[3]);
}

static struct its_collection *its_build_mapd_cmd(struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	unsigned long itt_addr;
	u8 size = ilog2(desc->its_mapd_cmd.dev->nr_ites);

	itt_addr = virt_to_phys(desc->its_mapd_cmd.dev->itt);
	itt_addr = ALIGN(itt_addr, ITS_ITT_ALIGN);

	its_encode_cmd(cmd, GITS_CMD_MAPD);
	its_encode_devid(cmd, desc->its_mapd_cmd.dev->device_id);
	its_encode_size(cmd, size - 1);
	its_encode_itt(cmd, itt_addr);
	its_encode_valid(cmd, desc->its_mapd_cmd.valid);

	its_fixup_cmd(cmd);

	return NULL;
}

static struct its_collection *its_build_mapc_cmd(struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	its_encode_cmd(cmd, GITS_CMD_MAPC);
	its_encode_collection(cmd, desc->its_mapc_cmd.col->col_id);
	its_encode_target(cmd, desc->its_mapc_cmd.col->target_address);
	its_encode_valid(cmd, desc->its_mapc_cmd.valid);

	its_fixup_cmd(cmd);

	return desc->its_mapc_cmd.col;
}

static struct its_collection *its_build_mapti_cmd(struct its_cmd_block *cmd,
						  struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_mapti_cmd.dev,
			       desc->its_mapti_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_MAPTI);
	its_encode_devid(cmd, desc->its_mapti_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_mapti_cmd.event_id);
	its_encode_phys_id(cmd, desc->its_mapti_cmd.phys_id);
	its_encode_collection(cmd, col->col_id);

	its_fixup_cmd(cmd);

	return col;
}

static struct its_collection *its_build_movi_cmd(struct its_cmd_block *cmd,
						 struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_movi_cmd.dev,
			       desc->its_movi_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_MOVI);
	its_encode_devid(cmd, desc->its_movi_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_movi_cmd.event_id);
	its_encode_collection(cmd, desc->its_movi_cmd.col->col_id);

	its_fixup_cmd(cmd);

	return col;
}

static struct its_collection *its_build_discard_cmd(struct its_cmd_block *cmd,
						    struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_discard_cmd.dev,
			       desc->its_discard_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_DISCARD);
	its_encode_devid(cmd, desc->its_discard_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_discard_cmd.event_id);

	its_fixup_cmd(cmd);

	return col;
}

static struct its_collection *its_build_inv_cmd(struct its_cmd_block *cmd,
						struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_inv_cmd.dev,
			       desc->its_inv_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_INV);
	its_encode_devid(cmd, desc->its_inv_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_inv_cmd.event_id);

	its_fixup_cmd(cmd);

	return col;
}

static struct its_collection *its_build_invall_cmd(struct its_cmd_block *cmd,
						   struct its_cmd_desc *desc)
{
	its_encode_cmd(cmd, GITS_CMD_INVALL);
	its_encode_collection(cmd, desc->its_mapc_cmd.col->col_id);

	its_fixup_cmd(cmd);

	return NULL;
}

static u64 its_cmd_ptr_to_offset(struct its_node *its,
				 struct its_cmd_block *ptr)
{
	return (ptr - its->cmd_base) * sizeof(*ptr);
}

static int its_queue_full(struct its_node *its)
{
	int widx;
	int ridx;

	widx = its->cmd_write - its->cmd_base;
	ridx = readl_relaxed(its->base + GITS_CREADR) / sizeof(struct its_cmd_block);

	/* This is incredibly unlikely to happen, unless the ITS locks up. */
	if (((widx + 1) % ITS_CMD_QUEUE_NR_ENTRIES) == ridx)
		return 1;

	return 0;
}

static struct its_cmd_block *its_allocate_entry(struct its_node *its)
{
	struct its_cmd_block *cmd;
	u32 count = 1000000;	/* 1s! */

	while (its_queue_full(its)) {
		count--;
		if (!count) {
			pr_err_ratelimited("ITS queue not draining\n");
			return NULL;
		}
		cpu_relax();
		udelay(1);
	}

	cmd = its->cmd_write++;

	/* Handle queue wrapping */
	if (its->cmd_write == (its->cmd_base + ITS_CMD_QUEUE_NR_ENTRIES))
		its->cmd_write = its->cmd_base;

	/* Clear command  */
	cmd->raw_cmd[0] = 0;
	cmd->raw_cmd[1] = 0;
	cmd->raw_cmd[2] = 0;
	cmd->raw_cmd[3] = 0;

	return cmd;
}

static struct its_cmd_block *its_post_commands(struct its_node *its)
{
	u64 wr = its_cmd_ptr_to_offset(its, its->cmd_write);

	writel_relaxed(wr, its->base + GITS_CWRITER);

	return its->cmd_write;
}

static void its_flush_cmd(struct its_node *its, struct its_cmd_block *cmd)
{
	/*
	 * Make sure the commands written to memory are observable by
	 * the ITS.
	 */
	if (its->flags & ITS_FLAGS_CMDQ_NEEDS_FLUSHING)
		gic_flush_dcache_to_poc(cmd, sizeof(*cmd));
	else
		dsb(ishst);
}

static void its_wait_for_range_completion(struct its_node *its,
					  struct its_cmd_block *from,
					  struct its_cmd_block *to)
{
	u64 rd_idx, from_idx, to_idx;
	u32 count = 1000000;	/* 1s! */

	from_idx = its_cmd_ptr_to_offset(its, from);
	to_idx = its_cmd_ptr_to_offset(its, to);

	while (1) {
		rd_idx = readl_relaxed(its->base + GITS_CREADR);
		if (rd_idx >= to_idx || rd_idx < from_idx)
			break;

		count--;
		if (!count) {
			pr_err_ratelimited("ITS queue timeout\n");
			return;
		}
		cpu_relax();
		udelay(1);
	}
}

static void its_send_single_command(struct its_node *its,
				    its_cmd_builder_t builder,
				    struct its_cmd_desc *desc)
{
	struct its_cmd_block *cmd, *sync_cmd, *next_cmd;
	struct its_collection *sync_col;
	unsigned long flags;

	raw_spin_lock_irqsave(&its->lock, flags);

	cmd = its_allocate_entry(its);
	if (!cmd) {		/* We're soooooo screewed... */
		pr_err_ratelimited("ITS can't allocate, dropping command\n");
		raw_spin_unlock_irqrestore(&its->lock, flags);
		return;
	}
	sync_col = builder(cmd, desc);
	its_flush_cmd(its, cmd);

	if (sync_col) {
		sync_cmd = its_allocate_entry(its);
		if (!sync_cmd) {
			pr_err_ratelimited("ITS can't SYNC, skipping\n");
			goto post;
		}
		its_encode_cmd(sync_cmd, GITS_CMD_SYNC);
		its_encode_target(sync_cmd, sync_col->target_address);
		its_fixup_cmd(sync_cmd);
		its_flush_cmd(its, sync_cmd);
	}

post:
	next_cmd = its_post_commands(its);
	raw_spin_unlock_irqrestore(&its->lock, flags);

	its_wait_for_range_completion(its, cmd, next_cmd);
}

static void its_send_inv(struct its_device *dev, u32 event_id)
{
	struct its_cmd_desc desc;

	desc.its_inv_cmd.dev = dev;
	desc.its_inv_cmd.event_id = event_id;

	its_send_single_command(dev->its, its_build_inv_cmd, &desc);
}

static void its_send_mapd(struct its_device *dev, int valid)
{
	struct its_cmd_desc desc;

	desc.its_mapd_cmd.dev = dev;
	desc.its_mapd_cmd.valid = !!valid;

	its_send_single_command(dev->its, its_build_mapd_cmd, &desc);
}

static void its_send_mapc(struct its_node *its, struct its_collection *col,
			  int valid)
{
	struct its_cmd_desc desc;

	desc.its_mapc_cmd.col = col;
	desc.its_mapc_cmd.valid = !!valid;

	its_send_single_command(its, its_build_mapc_cmd, &desc);
}

static void its_send_mapti(struct its_device *dev, u32 irq_id, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_mapti_cmd.dev = dev;
	desc.its_mapti_cmd.phys_id = irq_id;
	desc.its_mapti_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_mapti_cmd, &desc);
}

static void its_send_movi(struct its_device *dev,
			  struct its_collection *col, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_movi_cmd.dev = dev;
	desc.its_movi_cmd.col = col;
	desc.its_movi_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_movi_cmd, &desc);
}

static void its_send_discard(struct its_device *dev, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_discard_cmd.dev = dev;
	desc.its_discard_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_discard_cmd, &desc);
}

static void its_send_invall(struct its_node *its, struct its_collection *col)
{
	struct its_cmd_desc desc;

	desc.its_invall_cmd.col = col;

	its_send_single_command(its, its_build_invall_cmd, &desc);
}

/*
 * irqchip functions - assumes MSI, mostly.
 */

static inline u32 its_get_event_id(struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	return d->hwirq - its_dev->event_map.lpi_base;
}

static void lpi_set_config(struct irq_data *d, bool enable)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = d->hwirq;
	u32 id = its_get_event_id(d);
	u8 *cfg = page_address(gic_rdists->prop_page) + hwirq - 8192;

	if (enable)
		*cfg |= LPI_PROP_ENABLED;
	else
		*cfg &= ~LPI_PROP_ENABLED;

	/*
	 * Make the above write visible to the redistributors.
	 * And yes, we're flushing exactly: One. Single. Byte.
	 * Humpf...
	 */
	if (gic_rdists->flags & RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING)
		gic_flush_dcache_to_poc(cfg, sizeof(*cfg));
	else
		dsb(ishst);
	its_send_inv(its_dev, id);
}

static void its_mask_irq(struct irq_data *d)
{
	lpi_set_config(d, false);
}

static void its_unmask_irq(struct irq_data *d)
{
	lpi_set_config(d, true);
}

static int its_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
			    bool force)
{
	unsigned int cpu;
	const struct cpumask *cpu_mask = cpu_online_mask;
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_collection *target_col;
	u32 id = its_get_event_id(d);

       /* lpi cannot be routed to a redistributor that is on a foreign node */
	if (its_dev->its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_23144) {
		if (its_dev->its->numa_node >= 0) {
			cpu_mask = cpumask_of_node(its_dev->its->numa_node);
			if (!cpumask_intersects(mask_val, cpu_mask))
				return -EINVAL;
		}
	}

	cpu = cpumask_any_and(mask_val, cpu_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	/* don't set the affinity when the target cpu is same as current one */
	if (cpu != its_dev->event_map.col_map[id]) {
		target_col = &its_dev->its->collections[cpu];
		its_send_movi(its_dev, target_col, id);
		its_dev->event_map.col_map[id] = cpu;
	}

	return IRQ_SET_MASK_OK_DONE;
}

static void its_irq_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_node *its;
	u64 addr;

	its = its_dev->its;
	addr = its->phys_base + GITS_TRANSLATER;

	msg->address_lo		= lower_32_bits(addr);
	msg->address_hi		= upper_32_bits(addr);
	msg->data		= its_get_event_id(d);

	iommu_dma_map_msi_msg(d->irq, msg);
}

static struct irq_chip its_irq_chip = {
	.name			= "ITS",
	.irq_mask		= its_mask_irq,
	.irq_unmask		= its_unmask_irq,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= its_set_affinity,
	.irq_compose_msi_msg	= its_irq_compose_msi_msg,
};

/*
 * How we allocate LPIs:
 *
 * The GIC has id_bits bits for interrupt identifiers. From there, we
 * must subtract 8192 which are reserved for SGIs/PPIs/SPIs. Then, as
 * we allocate LPIs by chunks of 32, we can shift the whole thing by 5
 * bits to the right.
 *
 * This gives us (((1UL << id_bits) - 8192) >> 5) possible allocations.
 */
#define IRQS_PER_CHUNK_SHIFT	5
#define IRQS_PER_CHUNK		(1 << IRQS_PER_CHUNK_SHIFT)
#define ITS_MAX_LPI_NRBITS	16 /* 64K LPIs */

static unsigned long *lpi_bitmap;
static u32 lpi_chunks;
static u32 lpi_id_bits;
static DEFINE_SPINLOCK(lpi_lock);

static int its_lpi_to_chunk(int lpi)
{
	return (lpi - 8192) >> IRQS_PER_CHUNK_SHIFT;
}

static int its_chunk_to_lpi(int chunk)
{
	return (chunk << IRQS_PER_CHUNK_SHIFT) + 8192;
}

static int __init its_lpi_init(u32 id_bits)
{
	lpi_chunks = its_lpi_to_chunk(1UL << id_bits);

	lpi_bitmap = kzalloc(BITS_TO_LONGS(lpi_chunks) * sizeof(long),
			     GFP_KERNEL);
	if (!lpi_bitmap) {
		lpi_chunks = 0;
		return -ENOMEM;
	}

	pr_info("ITS: Allocated %d chunks for LPIs\n", (int)lpi_chunks);
	return 0;
}

static unsigned long *its_lpi_alloc_chunks(int nr_irqs, int *base, int *nr_ids)
{
	unsigned long *bitmap = NULL;
	int chunk_id;
	int nr_chunks;
	int i;

	nr_chunks = DIV_ROUND_UP(nr_irqs, IRQS_PER_CHUNK);

	spin_lock(&lpi_lock);

	do {
		chunk_id = bitmap_find_next_zero_area(lpi_bitmap, lpi_chunks,
						      0, nr_chunks, 0);
		if (chunk_id < lpi_chunks)
			break;

		nr_chunks--;
	} while (nr_chunks > 0);

	if (!nr_chunks)
		goto out;

	bitmap = kzalloc(BITS_TO_LONGS(nr_chunks * IRQS_PER_CHUNK) * sizeof (long),
			 GFP_ATOMIC);
	if (!bitmap)
		goto out;

	for (i = 0; i < nr_chunks; i++)
		set_bit(chunk_id + i, lpi_bitmap);

	*base = its_chunk_to_lpi(chunk_id);
	*nr_ids = nr_chunks * IRQS_PER_CHUNK;

out:
	spin_unlock(&lpi_lock);

	if (!bitmap)
		*base = *nr_ids = 0;

	return bitmap;
}

static void its_lpi_free(struct event_lpi_map *map)
{
	int base = map->lpi_base;
	int nr_ids = map->nr_lpis;
	int lpi;

	spin_lock(&lpi_lock);

	for (lpi = base; lpi < (base + nr_ids); lpi += IRQS_PER_CHUNK) {
		int chunk = its_lpi_to_chunk(lpi);
		BUG_ON(chunk > lpi_chunks);
		if (test_bit(chunk, lpi_bitmap)) {
			clear_bit(chunk, lpi_bitmap);
		} else {
			pr_err("Bad LPI chunk %d\n", chunk);
		}
	}

	spin_unlock(&lpi_lock);

	kfree(map->lpi_map);
	kfree(map->col_map);
}

/*
 * We allocate memory for PROPBASE to cover 2 ^ lpi_id_bits LPIs to
 * deal with (one configuration byte per interrupt). PENDBASE has to
 * be 64kB aligned (one bit per LPI, plus 8192 bits for SPI/PPI/SGI).
 */
#define LPI_NRBITS		lpi_id_bits
#define LPI_PROPBASE_SZ		ALIGN(BIT(LPI_NRBITS), SZ_64K)
#define LPI_PENDBASE_SZ		ALIGN(BIT(LPI_NRBITS) / 8, SZ_64K)

#define LPI_PROP_DEFAULT_PRIO	0xa0

static int __init its_alloc_lpi_tables(void)
{
	phys_addr_t paddr;

	lpi_id_bits = min_t(u32, gic_rdists->id_bits, ITS_MAX_LPI_NRBITS);
	gic_rdists->prop_page = alloc_pages(GFP_NOWAIT,
					   get_order(LPI_PROPBASE_SZ));
	if (!gic_rdists->prop_page) {
		pr_err("Failed to allocate PROPBASE\n");
		return -ENOMEM;
	}

	paddr = page_to_phys(gic_rdists->prop_page);
	pr_info("GIC: using LPI property table @%pa\n", &paddr);

	/* Priority 0xa0, Group-1, disabled */
	memset(page_address(gic_rdists->prop_page),
	       LPI_PROP_DEFAULT_PRIO | LPI_PROP_GROUP1,
	       LPI_PROPBASE_SZ);

	/* Make sure the GIC will observe the written configuration */
	gic_flush_dcache_to_poc(page_address(gic_rdists->prop_page), LPI_PROPBASE_SZ);

	return its_lpi_init(lpi_id_bits);
}

static const char *its_base_type_string[] = {
	[GITS_BASER_TYPE_DEVICE]	= "Devices",
	[GITS_BASER_TYPE_VCPU]		= "Virtual CPUs",
	[GITS_BASER_TYPE_RESERVED3]	= "Reserved (3)",
	[GITS_BASER_TYPE_COLLECTION]	= "Interrupt Collections",
	[GITS_BASER_TYPE_RESERVED5] 	= "Reserved (5)",
	[GITS_BASER_TYPE_RESERVED6] 	= "Reserved (6)",
	[GITS_BASER_TYPE_RESERVED7] 	= "Reserved (7)",
};

static u64 its_read_baser(struct its_node *its, struct its_baser *baser)
{
	u32 idx = baser - its->tables;

	return gits_read_baser(its->base + GITS_BASER + (idx << 3));
}

static void its_write_baser(struct its_node *its, struct its_baser *baser,
			    u64 val)
{
	u32 idx = baser - its->tables;

	gits_write_baser(val, its->base + GITS_BASER + (idx << 3));
	baser->val = its_read_baser(its, baser);
}

static int its_setup_baser(struct its_node *its, struct its_baser *baser,
			   u64 cache, u64 shr, u32 psz, u32 order,
			   bool indirect)
{
	u64 val = its_read_baser(its, baser);
	u64 esz = GITS_BASER_ENTRY_SIZE(val);
	u64 type = GITS_BASER_TYPE(val);
	u32 alloc_pages;
	void *base;
	u64 tmp;

retry_alloc_baser:
	alloc_pages = (PAGE_ORDER_TO_SIZE(order) / psz);
	if (alloc_pages > GITS_BASER_PAGES_MAX) {
		pr_warn("ITS@%pa: %s too large, reduce ITS pages %u->%u\n",
			&its->phys_base, its_base_type_string[type],
			alloc_pages, GITS_BASER_PAGES_MAX);
		alloc_pages = GITS_BASER_PAGES_MAX;
		order = get_order(GITS_BASER_PAGES_MAX * psz);
	}

	base = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!base)
		return -ENOMEM;

retry_baser:
	val = (virt_to_phys(base)				 |
		(type << GITS_BASER_TYPE_SHIFT)			 |
		((esz - 1) << GITS_BASER_ENTRY_SIZE_SHIFT)	 |
		((alloc_pages - 1) << GITS_BASER_PAGES_SHIFT)	 |
		cache						 |
		shr						 |
		GITS_BASER_VALID);

	val |=	indirect ? GITS_BASER_INDIRECT : 0x0;

	switch (psz) {
	case SZ_4K:
		val |= GITS_BASER_PAGE_SIZE_4K;
		break;
	case SZ_16K:
		val |= GITS_BASER_PAGE_SIZE_16K;
		break;
	case SZ_64K:
		val |= GITS_BASER_PAGE_SIZE_64K;
		break;
	}

	its_write_baser(its, baser, val);
	tmp = baser->val;

	if ((val ^ tmp) & GITS_BASER_SHAREABILITY_MASK) {
		/*
		 * Shareability didn't stick. Just use
		 * whatever the read reported, which is likely
		 * to be the only thing this redistributor
		 * supports. If that's zero, make it
		 * non-cacheable as well.
		 */
		shr = tmp & GITS_BASER_SHAREABILITY_MASK;
		if (!shr) {
			cache = GITS_BASER_nC;
			gic_flush_dcache_to_poc(base, PAGE_ORDER_TO_SIZE(order));
		}
		goto retry_baser;
	}

	if ((val ^ tmp) & GITS_BASER_PAGE_SIZE_MASK) {
		/*
		 * Page size didn't stick. Let's try a smaller
		 * size and retry. If we reach 4K, then
		 * something is horribly wrong...
		 */
		free_pages((unsigned long)base, order);
		baser->base = NULL;

		switch (psz) {
		case SZ_16K:
			psz = SZ_4K;
			goto retry_alloc_baser;
		case SZ_64K:
			psz = SZ_16K;
			goto retry_alloc_baser;
		}
	}

	if (val != tmp) {
		pr_err("ITS@%pa: %s doesn't stick: %llx %llx\n",
		       &its->phys_base, its_base_type_string[type],
		       val, tmp);
		free_pages((unsigned long)base, order);
		return -ENXIO;
	}

	baser->order = order;
	baser->base = base;
	baser->psz = psz;
	tmp = indirect ? GITS_LVL1_ENTRY_SIZE : esz;

	pr_info("ITS@%pa: allocated %d %s @%lx (%s, esz %d, psz %dK, shr %d)\n",
		&its->phys_base, (int)(PAGE_ORDER_TO_SIZE(order) / (int)tmp),
		its_base_type_string[type],
		(unsigned long)virt_to_phys(base),
		indirect ? "indirect" : "flat", (int)esz,
		psz / SZ_1K, (int)shr >> GITS_BASER_SHAREABILITY_SHIFT);

	return 0;
}

static bool its_parse_baser_device(struct its_node *its, struct its_baser *baser,
				   u32 psz, u32 *order)
{
	u64 esz = GITS_BASER_ENTRY_SIZE(its_read_baser(its, baser));
	u64 val = GITS_BASER_InnerShareable | GITS_BASER_RaWaWb;
	u32 ids = its->device_ids;
	u32 new_order = *order;
	bool indirect = false;

	/* No need to enable Indirection if memory requirement < (psz*2)bytes */
	if ((esz << ids) > (psz * 2)) {
		/*
		 * Find out whether hw supports a single or two-level table by
		 * table by reading bit at offset '62' after writing '1' to it.
		 */
		its_write_baser(its, baser, val | GITS_BASER_INDIRECT);
		indirect = !!(baser->val & GITS_BASER_INDIRECT);

		if (indirect) {
			/*
			 * The size of the lvl2 table is equal to ITS page size
			 * which is 'psz'. For computing lvl1 table size,
			 * subtract ID bits that sparse lvl2 table from 'ids'
			 * which is reported by ITS hardware times lvl1 table
			 * entry size.
			 */
			ids -= ilog2(psz / (int)esz);
			esz = GITS_LVL1_ENTRY_SIZE;
		}
	}

	/*
	 * Allocate as many entries as required to fit the
	 * range of device IDs that the ITS can grok... The ID
	 * space being incredibly sparse, this results in a
	 * massive waste of memory if two-level device table
	 * feature is not supported by hardware.
	 */
	new_order = max_t(u32, get_order(esz << ids), new_order);
	if (new_order >= MAX_ORDER) {
		new_order = MAX_ORDER - 1;
		ids = ilog2(PAGE_ORDER_TO_SIZE(new_order) / (int)esz);
		pr_warn("ITS@%pa: Device Table too large, reduce ids %u->%u\n",
			&its->phys_base, its->device_ids, ids);
	}

	*order = new_order;

	return indirect;
}

static void its_free_tables(struct its_node *its)
{
	int i;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		if (its->tables[i].base) {
			free_pages((unsigned long)its->tables[i].base,
				   its->tables[i].order);
			its->tables[i].base = NULL;
		}
	}
}

static int its_alloc_tables(struct its_node *its)
{
	u64 typer = gic_read_typer(its->base + GITS_TYPER);
	u32 ids = GITS_TYPER_DEVBITS(typer);
	u64 shr = GITS_BASER_InnerShareable;
	u64 cache = GITS_BASER_RaWaWb;
	u32 psz = SZ_64K;
	int err, i;

	if (its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_22375) {
		/*
		* erratum 22375: only alloc 8MB table size
		* erratum 24313: ignore memory access type
		*/
		cache   = GITS_BASER_nCnB;
		ids     = 0x14;                 /* 20 bits, 8MB */
	}

	its->device_ids = ids;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		struct its_baser *baser = its->tables + i;
		u64 val = its_read_baser(its, baser);
		u64 type = GITS_BASER_TYPE(val);
		u32 order = get_order(psz);
		bool indirect = false;

		if (type == GITS_BASER_TYPE_NONE)
			continue;

		if (type == GITS_BASER_TYPE_DEVICE)
			indirect = its_parse_baser_device(its, baser, psz, &order);

		err = its_setup_baser(its, baser, cache, shr, psz, order, indirect);
		if (err < 0) {
			its_free_tables(its);
			return err;
		}

		/* Update settings which will be used for next BASERn */
		psz = baser->psz;
		cache = baser->val & GITS_BASER_CACHEABILITY_MASK;
		shr = baser->val & GITS_BASER_SHAREABILITY_MASK;
	}

	return 0;
}

static int its_alloc_collections(struct its_node *its)
{
	its->collections = kzalloc(nr_cpu_ids * sizeof(*its->collections),
				   GFP_KERNEL);
	if (!its->collections)
		return -ENOMEM;

	return 0;
}

static void its_cpu_init_lpis(void)
{
	void __iomem *rbase = gic_data_rdist_rd_base();
	struct page *pend_page;
	u64 val, tmp;

	/* If we didn't allocate the pending table yet, do it now */
	pend_page = gic_data_rdist()->pend_page;
	if (!pend_page) {
		phys_addr_t paddr;
		/*
		 * The pending pages have to be at least 64kB aligned,
		 * hence the 'max(LPI_PENDBASE_SZ, SZ_64K)' below.
		 */
		pend_page = alloc_pages(GFP_NOWAIT | __GFP_ZERO,
					get_order(max_t(u32, LPI_PENDBASE_SZ, SZ_64K)));
		if (!pend_page) {
			pr_err("Failed to allocate PENDBASE for CPU%d\n",
			       smp_processor_id());
			return;
		}

		/* Make sure the GIC will observe the zero-ed page */
		gic_flush_dcache_to_poc(page_address(pend_page), LPI_PENDBASE_SZ);

		paddr = page_to_phys(pend_page);
		pr_info("CPU%d: using LPI pending table @%pa\n",
			smp_processor_id(), &paddr);
		gic_data_rdist()->pend_page = pend_page;
	}

	/* Disable LPIs */
	val = readl_relaxed(rbase + GICR_CTLR);
	val &= ~GICR_CTLR_ENABLE_LPIS;
	writel_relaxed(val, rbase + GICR_CTLR);

	/*
	 * Make sure any change to the table is observable by the GIC.
	 */
	dsb(sy);

	/* set PROPBASE */
	val = (page_to_phys(gic_rdists->prop_page) |
	       GICR_PROPBASER_InnerShareable |
	       GICR_PROPBASER_RaWaWb |
	       ((LPI_NRBITS - 1) & GICR_PROPBASER_IDBITS_MASK));

	gicr_write_propbaser(val, rbase + GICR_PROPBASER);
	tmp = gicr_read_propbaser(rbase + GICR_PROPBASER);

	if ((tmp ^ val) & GICR_PROPBASER_SHAREABILITY_MASK) {
		if (!(tmp & GICR_PROPBASER_SHAREABILITY_MASK)) {
			/*
			 * The HW reports non-shareable, we must
			 * remove the cacheability attributes as
			 * well.
			 */
			val &= ~(GICR_PROPBASER_SHAREABILITY_MASK |
				 GICR_PROPBASER_CACHEABILITY_MASK);
			val |= GICR_PROPBASER_nC;
			gicr_write_propbaser(val, rbase + GICR_PROPBASER);
		}
		pr_info_once("GIC: using cache flushing for LPI property table\n");
		gic_rdists->flags |= RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING;
	}

	/* set PENDBASE */
	val = (page_to_phys(pend_page) |
	       GICR_PENDBASER_InnerShareable |
	       GICR_PENDBASER_RaWaWb);

	gicr_write_pendbaser(val, rbase + GICR_PENDBASER);
	tmp = gicr_read_pendbaser(rbase + GICR_PENDBASER);

	if (!(tmp & GICR_PENDBASER_SHAREABILITY_MASK)) {
		/*
		 * The HW reports non-shareable, we must remove the
		 * cacheability attributes as well.
		 */
		val &= ~(GICR_PENDBASER_SHAREABILITY_MASK |
			 GICR_PENDBASER_CACHEABILITY_MASK);
		val |= GICR_PENDBASER_nC;
		gicr_write_pendbaser(val, rbase + GICR_PENDBASER);
	}

	/* Enable LPIs */
	val = readl_relaxed(rbase + GICR_CTLR);
	val |= GICR_CTLR_ENABLE_LPIS;
	writel_relaxed(val, rbase + GICR_CTLR);

	/* Make sure the GIC has seen the above */
	dsb(sy);
}

static void its_cpu_init_collection(void)
{
	struct its_node *its;
	int cpu;

	spin_lock(&its_lock);
	cpu = smp_processor_id();

	list_for_each_entry(its, &its_nodes, entry) {
		u64 target;

		/* avoid cross node collections and its mapping */
		if (its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_23144) {
			struct device_node *cpu_node;

			cpu_node = of_get_cpu_node(cpu, NULL);
			if (its->numa_node != NUMA_NO_NODE &&
				its->numa_node != of_node_to_nid(cpu_node))
				continue;
		}

		/*
		 * We now have to bind each collection to its target
		 * redistributor.
		 */
		if (gic_read_typer(its->base + GITS_TYPER) & GITS_TYPER_PTA) {
			/*
			 * This ITS wants the physical address of the
			 * redistributor.
			 */
			target = gic_data_rdist()->phys_base;
		} else {
			/*
			 * This ITS wants a linear CPU number.
			 */
			target = gic_read_typer(gic_data_rdist_rd_base() + GICR_TYPER);
			target = GICR_TYPER_CPU_NUMBER(target) << 16;
		}

		/* Perform collection mapping */
		its->collections[cpu].target_address = target;
		its->collections[cpu].col_id = cpu;

		its_send_mapc(its, &its->collections[cpu], 1);
		its_send_invall(its, &its->collections[cpu]);
	}

	spin_unlock(&its_lock);
}

static struct its_device *its_find_device(struct its_node *its, u32 dev_id)
{
	struct its_device *its_dev = NULL, *tmp;
	unsigned long flags;

	raw_spin_lock_irqsave(&its->lock, flags);

	list_for_each_entry(tmp, &its->its_device_list, entry) {
		if (tmp->device_id == dev_id) {
			its_dev = tmp;
			break;
		}
	}

	raw_spin_unlock_irqrestore(&its->lock, flags);

	return its_dev;
}

static struct its_baser *its_get_baser(struct its_node *its, u32 type)
{
	int i;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		if (GITS_BASER_TYPE(its->tables[i].val) == type)
			return &its->tables[i];
	}

	return NULL;
}

static bool its_alloc_device_table(struct its_node *its, u32 dev_id)
{
	struct its_baser *baser;
	struct page *page;
	u32 esz, idx;
	__le64 *table;

	baser = its_get_baser(its, GITS_BASER_TYPE_DEVICE);

	/* Don't allow device id that exceeds ITS hardware limit */
	if (!baser)
		return (ilog2(dev_id) < its->device_ids);

	/* Don't allow device id that exceeds single, flat table limit */
	esz = GITS_BASER_ENTRY_SIZE(baser->val);
	if (!(baser->val & GITS_BASER_INDIRECT))
		return (dev_id < (PAGE_ORDER_TO_SIZE(baser->order) / esz));

	/* Compute 1st level table index & check if that exceeds table limit */
	idx = dev_id >> ilog2(baser->psz / esz);
	if (idx >= (PAGE_ORDER_TO_SIZE(baser->order) / GITS_LVL1_ENTRY_SIZE))
		return false;

	table = baser->base;

	/* Allocate memory for 2nd level table */
	if (!table[idx]) {
		page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(baser->psz));
		if (!page)
			return false;

		/* Flush Lvl2 table to PoC if hw doesn't support coherency */
		if (!(baser->val & GITS_BASER_SHAREABILITY_MASK))
			gic_flush_dcache_to_poc(page_address(page), baser->psz);

		table[idx] = cpu_to_le64(page_to_phys(page) | GITS_BASER_VALID);

		/* Flush Lvl1 entry to PoC if hw doesn't support coherency */
		if (!(baser->val & GITS_BASER_SHAREABILITY_MASK))
			gic_flush_dcache_to_poc(table + idx, GITS_LVL1_ENTRY_SIZE);

		/* Ensure updated table contents are visible to ITS hardware */
		dsb(sy);
	}

	return true;
}

static struct its_device *its_create_device(struct its_node *its, u32 dev_id,
					    int nvecs)
{
	struct its_device *dev;
	unsigned long *lpi_map;
	unsigned long flags;
	u16 *col_map = NULL;
	void *itt;
	int lpi_base;
	int nr_lpis;
	int nr_ites;
	int sz;

	if (!its_alloc_device_table(its, dev_id))
		return NULL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	/*
	 * At least one bit of EventID is being used, hence a minimum
	 * of two entries. No, the architecture doesn't let you
	 * express an ITT with a single entry.
	 */
	nr_ites = max(2UL, roundup_pow_of_two(nvecs));
	sz = nr_ites * its->ite_size;
	sz = max(sz, ITS_ITT_ALIGN) + ITS_ITT_ALIGN - 1;
	itt = kzalloc(sz, GFP_KERNEL);
	lpi_map = its_lpi_alloc_chunks(nvecs, &lpi_base, &nr_lpis);
	if (lpi_map)
		col_map = kzalloc(sizeof(*col_map) * nr_lpis, GFP_KERNEL);

	if (!dev || !itt || !lpi_map || !col_map) {
		kfree(dev);
		kfree(itt);
		kfree(lpi_map);
		kfree(col_map);
		return NULL;
	}

	gic_flush_dcache_to_poc(itt, sz);

	dev->its = its;
	dev->itt = itt;
	dev->nr_ites = nr_ites;
	dev->event_map.lpi_map = lpi_map;
	dev->event_map.col_map = col_map;
	dev->event_map.lpi_base = lpi_base;
	dev->event_map.nr_lpis = nr_lpis;
	dev->device_id = dev_id;
	INIT_LIST_HEAD(&dev->entry);

	raw_spin_lock_irqsave(&its->lock, flags);
	list_add(&dev->entry, &its->its_device_list);
	raw_spin_unlock_irqrestore(&its->lock, flags);

	/* Map device to its ITT */
	its_send_mapd(dev, 1);

	return dev;
}

static void its_free_device(struct its_device *its_dev)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&its_dev->its->lock, flags);
	list_del(&its_dev->entry);
	raw_spin_unlock_irqrestore(&its_dev->its->lock, flags);
	kfree(its_dev->itt);
	kfree(its_dev);
}

static int its_alloc_device_irq(struct its_device *dev, irq_hw_number_t *hwirq)
{
	int idx;

	idx = find_first_zero_bit(dev->event_map.lpi_map,
				  dev->event_map.nr_lpis);
	if (idx == dev->event_map.nr_lpis)
		return -ENOSPC;

	*hwirq = dev->event_map.lpi_base + idx;
	set_bit(idx, dev->event_map.lpi_map);

	return 0;
}

static int its_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *info)
{
	struct its_node *its;
	struct its_device *its_dev;
	struct msi_domain_info *msi_info;
	u32 dev_id;

	/*
	 * We ignore "dev" entierely, and rely on the dev_id that has
	 * been passed via the scratchpad. This limits this domain's
	 * usefulness to upper layers that definitely know that they
	 * are built on top of the ITS.
	 */
	dev_id = info->scratchpad[0].ul;

	msi_info = msi_get_domain_info(domain);
	its = msi_info->data;

	its_dev = its_find_device(its, dev_id);
	if (its_dev) {
		/*
		 * We already have seen this ID, probably through
		 * another alias (PCI bridge of some sort). No need to
		 * create the device.
		 */
		pr_debug("Reusing ITT for devID %x\n", dev_id);
		goto out;
	}

	its_dev = its_create_device(its, dev_id, nvec);
	if (!its_dev)
		return -ENOMEM;

	pr_debug("ITT %d entries, %d bits\n", nvec, ilog2(nvec));
out:
	info->scratchpad[0].ptr = its_dev;
	return 0;
}

static struct msi_domain_ops its_msi_domain_ops = {
	.msi_prepare	= its_msi_prepare,
};

static int its_irq_gic_domain_alloc(struct irq_domain *domain,
				    unsigned int virq,
				    irq_hw_number_t hwirq)
{
	struct irq_fwspec fwspec;

	if (irq_domain_get_of_node(domain->parent)) {
		fwspec.fwnode = domain->parent->fwnode;
		fwspec.param_count = 3;
		fwspec.param[0] = GIC_IRQ_TYPE_LPI;
		fwspec.param[1] = hwirq;
		fwspec.param[2] = IRQ_TYPE_EDGE_RISING;
	} else if (is_fwnode_irqchip(domain->parent->fwnode)) {
		fwspec.fwnode = domain->parent->fwnode;
		fwspec.param_count = 2;
		fwspec.param[0] = hwirq;
		fwspec.param[1] = IRQ_TYPE_EDGE_RISING;
	} else {
		return -EINVAL;
	}

	return irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
}

static int its_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *args)
{
	msi_alloc_info_t *info = args;
	struct its_device *its_dev = info->scratchpad[0].ptr;
	irq_hw_number_t hwirq;
	int err;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		err = its_alloc_device_irq(its_dev, &hwirq);
		if (err)
			return err;

		err = its_irq_gic_domain_alloc(domain, virq + i, hwirq);
		if (err)
			return err;

		irq_domain_set_hwirq_and_chip(domain, virq + i,
					      hwirq, &its_irq_chip, its_dev);
		pr_debug("ID:%d pID:%d vID:%d\n",
			 (int)(hwirq - its_dev->event_map.lpi_base),
			 (int) hwirq, virq + i);
	}

	return 0;
}

static void its_irq_domain_activate(struct irq_domain *domain,
				    struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);
	const struct cpumask *cpu_mask = cpu_online_mask;

	/* get the cpu_mask of local node */
	if (its_dev->its->numa_node >= 0)
		cpu_mask = cpumask_of_node(its_dev->its->numa_node);

	/* Bind the LPI to the first possible CPU */
	its_dev->event_map.col_map[event] = cpumask_first(cpu_mask);

	/* Map the GIC IRQ and event to the device */
	its_send_mapti(its_dev, d->hwirq, event);
}

static void its_irq_domain_deactivate(struct irq_domain *domain,
				      struct irq_data *d)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	u32 event = its_get_event_id(d);

	/* Stop the delivery of interrupts */
	its_send_discard(its_dev, event);
}

static void its_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *data = irq_domain_get_irq_data(domain,
								virq + i);
		u32 event = its_get_event_id(data);

		/* Mark interrupt index as unused */
		clear_bit(event, its_dev->event_map.lpi_map);

		/* Nuke the entry in the domain */
		irq_domain_reset_irq_data(data);
	}

	/* If all interrupts have been freed, start mopping the floor */
	if (bitmap_empty(its_dev->event_map.lpi_map,
			 its_dev->event_map.nr_lpis)) {
		its_lpi_free(&its_dev->event_map);

		/* Unmap device/itt */
		its_send_mapd(its_dev, 0);
		its_free_device(its_dev);
	}

	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static const struct irq_domain_ops its_domain_ops = {
	.alloc			= its_irq_domain_alloc,
	.free			= its_irq_domain_free,
	.activate		= its_irq_domain_activate,
	.deactivate		= its_irq_domain_deactivate,
};

static int its_force_quiescent(void __iomem *base)
{
	u32 count = 1000000;	/* 1s */
	u32 val;

	val = readl_relaxed(base + GITS_CTLR);
	/*
	 * GIC architecture specification requires the ITS to be both
	 * disabled and quiescent for writes to GITS_BASER<n> or
	 * GITS_CBASER to not have UNPREDICTABLE results.
	 */
	if ((val & GITS_CTLR_QUIESCENT) && !(val & GITS_CTLR_ENABLE))
		return 0;

	/* Disable the generation of all interrupts to this ITS */
	val &= ~GITS_CTLR_ENABLE;
	writel_relaxed(val, base + GITS_CTLR);

	/* Poll GITS_CTLR and wait until ITS becomes quiescent */
	while (1) {
		val = readl_relaxed(base + GITS_CTLR);
		if (val & GITS_CTLR_QUIESCENT)
			return 0;

		count--;
		if (!count)
			return -EBUSY;

		cpu_relax();
		udelay(1);
	}
}

static void __maybe_unused its_enable_quirk_cavium_22375(void *data)
{
	struct its_node *its = data;

	its->flags |= ITS_FLAGS_WORKAROUND_CAVIUM_22375;
}

static void __maybe_unused its_enable_quirk_cavium_23144(void *data)
{
	struct its_node *its = data;

	its->flags |= ITS_FLAGS_WORKAROUND_CAVIUM_23144;
}

static void __maybe_unused its_enable_quirk_qdf2400_e0065(void *data)
{
	struct its_node *its = data;

	/* On QDF2400, the size of the ITE is 16Bytes */
	its->ite_size = 16;
}

static const struct gic_quirk its_quirks[] = {
#ifdef CONFIG_CAVIUM_ERRATUM_22375
	{
		.desc	= "ITS: Cavium errata 22375, 24313",
		.iidr	= 0xa100034c,	/* ThunderX pass 1.x */
		.mask	= 0xffff0fff,
		.init	= its_enable_quirk_cavium_22375,
	},
#endif
#ifdef CONFIG_CAVIUM_ERRATUM_23144
	{
		.desc	= "ITS: Cavium erratum 23144",
		.iidr	= 0xa100034c,	/* ThunderX pass 1.x */
		.mask	= 0xffff0fff,
		.init	= its_enable_quirk_cavium_23144,
	},
#endif
#ifdef CONFIG_QCOM_QDF2400_ERRATUM_0065
	{
		.desc	= "ITS: QDF2400 erratum 0065",
		.iidr	= 0x00001070, /* QDF2400 ITS rev 1.x */
		.mask	= 0xffffffff,
		.init	= its_enable_quirk_qdf2400_e0065,
	},
#endif
	{
	}
};

static void its_enable_quirks(struct its_node *its)
{
	u32 iidr = readl_relaxed(its->base + GITS_IIDR);

	gic_enable_quirks(iidr, its_quirks, its);
}

static int its_init_domain(struct fwnode_handle *handle, struct its_node *its)
{
	struct irq_domain *inner_domain;
	struct msi_domain_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	inner_domain = irq_domain_create_tree(handle, &its_domain_ops, its);
	if (!inner_domain) {
		kfree(info);
		return -ENOMEM;
	}

	inner_domain->parent = its_parent;
	irq_domain_update_bus_token(inner_domain, DOMAIN_BUS_NEXUS);
	inner_domain->flags |= IRQ_DOMAIN_FLAG_MSI_REMAP;
	info->ops = &its_msi_domain_ops;
	info->data = its;
	inner_domain->host_data = info;

	return 0;
}

static int __init its_probe_one(struct resource *res,
				struct fwnode_handle *handle, int numa_node)
{
	struct its_node *its;
	void __iomem *its_base;
	u32 val;
	u64 baser, tmp;
	int err;

	its_base = ioremap(res->start, resource_size(res));
	if (!its_base) {
		pr_warn("ITS@%pa: Unable to map ITS registers\n", &res->start);
		return -ENOMEM;
	}

	val = readl_relaxed(its_base + GITS_PIDR2) & GIC_PIDR2_ARCH_MASK;
	if (val != 0x30 && val != 0x40) {
		pr_warn("ITS@%pa: No ITS detected, giving up\n", &res->start);
		err = -ENODEV;
		goto out_unmap;
	}

	err = its_force_quiescent(its_base);
	if (err) {
		pr_warn("ITS@%pa: Failed to quiesce, giving up\n", &res->start);
		goto out_unmap;
	}

	pr_info("ITS %pR\n", res);

	its = kzalloc(sizeof(*its), GFP_KERNEL);
	if (!its) {
		err = -ENOMEM;
		goto out_unmap;
	}

	raw_spin_lock_init(&its->lock);
	INIT_LIST_HEAD(&its->entry);
	INIT_LIST_HEAD(&its->its_device_list);
	its->base = its_base;
	its->phys_base = res->start;
	its->ite_size = ((gic_read_typer(its_base + GITS_TYPER) >> 4) & 0xf) + 1;
	its->numa_node = numa_node;

	its->cmd_base = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						get_order(ITS_CMD_QUEUE_SZ));
	if (!its->cmd_base) {
		err = -ENOMEM;
		goto out_free_its;
	}
	its->cmd_write = its->cmd_base;

	its_enable_quirks(its);

	err = its_alloc_tables(its);
	if (err)
		goto out_free_cmd;

	err = its_alloc_collections(its);
	if (err)
		goto out_free_tables;

	baser = (virt_to_phys(its->cmd_base)	|
		 GITS_CBASER_RaWaWb		|
		 GITS_CBASER_InnerShareable	|
		 (ITS_CMD_QUEUE_SZ / SZ_4K - 1)	|
		 GITS_CBASER_VALID);

	gits_write_cbaser(baser, its->base + GITS_CBASER);
	tmp = gits_read_cbaser(its->base + GITS_CBASER);

	if ((tmp ^ baser) & GITS_CBASER_SHAREABILITY_MASK) {
		if (!(tmp & GITS_CBASER_SHAREABILITY_MASK)) {
			/*
			 * The HW reports non-shareable, we must
			 * remove the cacheability attributes as
			 * well.
			 */
			baser &= ~(GITS_CBASER_SHAREABILITY_MASK |
				   GITS_CBASER_CACHEABILITY_MASK);
			baser |= GITS_CBASER_nC;
			gits_write_cbaser(baser, its->base + GITS_CBASER);
		}
		pr_info("ITS: using cache flushing for cmd queue\n");
		its->flags |= ITS_FLAGS_CMDQ_NEEDS_FLUSHING;
	}

	gits_write_cwriter(0, its->base + GITS_CWRITER);
	writel_relaxed(GITS_CTLR_ENABLE, its->base + GITS_CTLR);

	err = its_init_domain(handle, its);
	if (err)
		goto out_free_tables;

	spin_lock(&its_lock);
	list_add(&its->entry, &its_nodes);
	spin_unlock(&its_lock);

	return 0;

out_free_tables:
	its_free_tables(its);
out_free_cmd:
	free_pages((unsigned long)its->cmd_base, get_order(ITS_CMD_QUEUE_SZ));
out_free_its:
	kfree(its);
out_unmap:
	iounmap(its_base);
	pr_err("ITS@%pa: failed probing (%d)\n", &res->start, err);
	return err;
}

static bool gic_rdists_supports_plpis(void)
{
	return !!(gic_read_typer(gic_data_rdist_rd_base() + GICR_TYPER) & GICR_TYPER_PLPIS);
}

int its_cpu_init(void)
{
	if (!list_empty(&its_nodes)) {
		if (!gic_rdists_supports_plpis()) {
			pr_info("CPU%d: LPIs not supported\n", smp_processor_id());
			return -ENXIO;
		}
		its_cpu_init_lpis();
		its_cpu_init_collection();
	}

	return 0;
}

static const struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

static int __init its_of_probe(struct device_node *node)
{
	struct device_node *np;
	struct resource res;

	for (np = of_find_matching_node(node, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		if (!of_property_read_bool(np, "msi-controller")) {
			pr_warn("%s: no msi-controller property, ITS ignored\n",
				np->full_name);
			continue;
		}

		if (of_address_to_resource(np, 0, &res)) {
			pr_warn("%s: no regs?\n", np->full_name);
			continue;
		}

		its_probe_one(&res, &np->fwnode, of_node_to_nid(np));
	}
	return 0;
}

#ifdef CONFIG_ACPI

#define ACPI_GICV3_ITS_MEM_SIZE (SZ_128K)

#if defined(CONFIG_ACPI_NUMA) && (ACPI_CA_VERSION >= 0x20170531)
struct its_srat_map {
	/* numa node id */
	u32	numa_node;
	/* GIC ITS ID */
	u32	its_id;
};

static struct its_srat_map its_srat_maps[MAX_NUMNODES] __initdata;
static int its_in_srat __initdata;

static int __init acpi_get_its_numa_node(u32 its_id)
{
	int i;

	for (i = 0; i < its_in_srat; i++) {
		if (its_id == its_srat_maps[i].its_id)
			return its_srat_maps[i].numa_node;
	}
	return NUMA_NO_NODE;
}

static int __init gic_acpi_parse_srat_its(struct acpi_subtable_header *header,
			 const unsigned long end)
{
	int node;
	struct acpi_srat_gic_its_affinity *its_affinity;

	its_affinity = (struct acpi_srat_gic_its_affinity *)header;
	if (!its_affinity)
		return -EINVAL;

	if (its_affinity->header.length < sizeof(*its_affinity)) {
		pr_err("SRAT: Invalid header length %d in ITS affinity\n",
			its_affinity->header.length);
		return -EINVAL;
	}

	if (its_in_srat >= MAX_NUMNODES) {
		pr_err("SRAT: ITS affinity exceeding max count[%d]\n",
				MAX_NUMNODES);
		return -EINVAL;
	}

	node = acpi_map_pxm_to_node(its_affinity->proximity_domain);

	if (node == NUMA_NO_NODE || node >= MAX_NUMNODES) {
		pr_err("SRAT: Invalid NUMA node %d in ITS affinity\n", node);
		return 0;
	}

	its_srat_maps[its_in_srat].numa_node = node;
	its_srat_maps[its_in_srat].its_id = its_affinity->its_id;
	its_in_srat++;
	pr_info("SRAT: PXM %d -> ITS %d -> Node %d\n",
		its_affinity->proximity_domain, its_affinity->its_id, node);

	return 0;
}

static void __init acpi_table_parse_srat_its(void)
{
	acpi_table_parse_entries(ACPI_SIG_SRAT,
			sizeof(struct acpi_table_srat),
			ACPI_SRAT_TYPE_GIC_ITS_AFFINITY,
			gic_acpi_parse_srat_its, 0);
}
#else
static void __init acpi_table_parse_srat_its(void)	{ }
static int __init acpi_get_its_numa_node(u32 its_id) { return NUMA_NO_NODE; }
#endif

static int __init gic_acpi_parse_madt_its(struct acpi_subtable_header *header,
					  const unsigned long end)
{
	struct acpi_madt_generic_translator *its_entry;
	struct fwnode_handle *dom_handle;
	struct resource res;
	int err;

	its_entry = (struct acpi_madt_generic_translator *)header;
	memset(&res, 0, sizeof(res));
	res.start = its_entry->base_address;
	res.end = its_entry->base_address + ACPI_GICV3_ITS_MEM_SIZE - 1;
	res.flags = IORESOURCE_MEM;

	dom_handle = irq_domain_alloc_fwnode((void *)its_entry->base_address);
	if (!dom_handle) {
		pr_err("ITS@%pa: Unable to allocate GICv3 ITS domain token\n",
		       &res.start);
		return -ENOMEM;
	}

	err = iort_register_domain_token(its_entry->translation_id, dom_handle);
	if (err) {
		pr_err("ITS@%pa: Unable to register GICv3 ITS domain token (ITS ID %d) to IORT\n",
		       &res.start, its_entry->translation_id);
		goto dom_err;
	}

	err = its_probe_one(&res, dom_handle,
			acpi_get_its_numa_node(its_entry->translation_id));
	if (!err)
		return 0;

	iort_deregister_domain_token(its_entry->translation_id);
dom_err:
	irq_domain_free_fwnode(dom_handle);
	return err;
}

static void __init its_acpi_probe(void)
{
	acpi_table_parse_srat_its();
	acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_TRANSLATOR,
			      gic_acpi_parse_madt_its, 0);
}
#else
static void __init its_acpi_probe(void) { }
#endif

int __init its_init(struct fwnode_handle *handle, struct rdists *rdists,
		    struct irq_domain *parent_domain)
{
	struct device_node *of_node;

	its_parent = parent_domain;
	of_node = to_of_node(handle);
	if (of_node)
		its_of_probe(of_node);
	else
		its_acpi_probe();

	if (list_empty(&its_nodes)) {
		pr_warn("ITS: No ITS available, not enabling LPIs\n");
		return -ENXIO;
	}

	gic_rdists = rdists;
	return its_alloc_lpi_tables();
}
