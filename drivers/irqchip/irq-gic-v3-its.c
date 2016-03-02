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

#include <linux/bitmap.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
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

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/exception.h>

#include "irq-gic-common.h"

#define ITS_FLAGS_CMDQ_NEEDS_FLUSHING		(1ULL << 0)
#define ITS_FLAGS_WORKAROUND_CAVIUM_22375	(1ULL << 1)

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
 * The ITS structure - contains most of the infrastructure, with the
 * top-level MSI domain, the command queue, the collections, and the
 * list of devices writing to it.
 */
struct its_node {
	raw_spinlock_t		lock;
	struct list_head	entry;
	void __iomem		*base;
	unsigned long		phys_base;
	struct its_cmd_block	*cmd_base;
	struct its_cmd_block	*cmd_write;
	struct {
		void		*base;
		u32		order;
	} tables[GITS_BASER_NR_REGS];
	struct its_collection	*collections;
	struct list_head	its_device_list;
	u64			flags;
	u32			ite_size;
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
static struct device_node *gic_root_node;
static struct rdists *gic_rdists;

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
		} its_mapvi_cmd;

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

static void its_encode_cmd(struct its_cmd_block *cmd, u8 cmd_nr)
{
	cmd->raw_cmd[0] &= ~0xffUL;
	cmd->raw_cmd[0] |= cmd_nr;
}

static void its_encode_devid(struct its_cmd_block *cmd, u32 devid)
{
	cmd->raw_cmd[0] &= BIT_ULL(32) - 1;
	cmd->raw_cmd[0] |= ((u64)devid) << 32;
}

static void its_encode_event_id(struct its_cmd_block *cmd, u32 id)
{
	cmd->raw_cmd[1] &= ~0xffffffffUL;
	cmd->raw_cmd[1] |= id;
}

static void its_encode_phys_id(struct its_cmd_block *cmd, u32 phys_id)
{
	cmd->raw_cmd[1] &= 0xffffffffUL;
	cmd->raw_cmd[1] |= ((u64)phys_id) << 32;
}

static void its_encode_size(struct its_cmd_block *cmd, u8 size)
{
	cmd->raw_cmd[1] &= ~0x1fUL;
	cmd->raw_cmd[1] |= size & 0x1f;
}

static void its_encode_itt(struct its_cmd_block *cmd, u64 itt_addr)
{
	cmd->raw_cmd[2] &= ~0xffffffffffffUL;
	cmd->raw_cmd[2] |= itt_addr & 0xffffffffff00UL;
}

static void its_encode_valid(struct its_cmd_block *cmd, int valid)
{
	cmd->raw_cmd[2] &= ~(1UL << 63);
	cmd->raw_cmd[2] |= ((u64)!!valid) << 63;
}

static void its_encode_target(struct its_cmd_block *cmd, u64 target_addr)
{
	cmd->raw_cmd[2] &= ~(0xffffffffUL << 16);
	cmd->raw_cmd[2] |= (target_addr & (0xffffffffUL << 16));
}

static void its_encode_collection(struct its_cmd_block *cmd, u16 col)
{
	cmd->raw_cmd[2] &= ~0xffffUL;
	cmd->raw_cmd[2] |= col;
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

static struct its_collection *its_build_mapvi_cmd(struct its_cmd_block *cmd,
						  struct its_cmd_desc *desc)
{
	struct its_collection *col;

	col = dev_event_to_col(desc->its_mapvi_cmd.dev,
			       desc->its_mapvi_cmd.event_id);

	its_encode_cmd(cmd, GITS_CMD_MAPVI);
	its_encode_devid(cmd, desc->its_mapvi_cmd.dev->device_id);
	its_encode_event_id(cmd, desc->its_mapvi_cmd.event_id);
	its_encode_phys_id(cmd, desc->its_mapvi_cmd.phys_id);
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
		__flush_dcache_area(cmd, sizeof(*cmd));
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

static void its_send_mapvi(struct its_device *dev, u32 irq_id, u32 id)
{
	struct its_cmd_desc desc;

	desc.its_mapvi_cmd.dev = dev;
	desc.its_mapvi_cmd.phys_id = irq_id;
	desc.its_mapvi_cmd.event_id = id;

	its_send_single_command(dev->its, its_build_mapvi_cmd, &desc);
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
		__flush_dcache_area(cfg, sizeof(*cfg));
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
	unsigned int cpu = cpumask_any_and(mask_val, cpu_online_mask);
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_collection *target_col;
	u32 id = its_get_event_id(d);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	target_col = &its_dev->its->collections[cpu];
	its_send_movi(its_dev, target_col, id);
	its_dev->event_map.col_map[id] = cpu;

	return IRQ_SET_MASK_OK_DONE;
}

static void its_irq_compose_msi_msg(struct irq_data *d, struct msi_msg *msg)
{
	struct its_device *its_dev = irq_data_get_irq_chip_data(d);
	struct its_node *its;
	u64 addr;

	its = its_dev->its;
	addr = its->phys_base + GITS_TRANSLATER;

	msg->address_lo		= addr & ((1UL << 32) - 1);
	msg->address_hi		= addr >> 32;
	msg->data		= its_get_event_id(d);
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

static unsigned long *lpi_bitmap;
static u32 lpi_chunks;
static DEFINE_SPINLOCK(lpi_lock);

static int its_lpi_to_chunk(int lpi)
{
	return (lpi - 8192) >> IRQS_PER_CHUNK_SHIFT;
}

static int its_chunk_to_lpi(int chunk)
{
	return (chunk << IRQS_PER_CHUNK_SHIFT) + 8192;
}

static int its_lpi_init(u32 id_bits)
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
 * We allocate 64kB for PROPBASE. That gives us at most 64K LPIs to
 * deal with (one configuration byte per interrupt). PENDBASE has to
 * be 64kB aligned (one bit per LPI, plus 8192 bits for SPI/PPI/SGI).
 */
#define LPI_PROPBASE_SZ		SZ_64K
#define LPI_PENDBASE_SZ		(LPI_PROPBASE_SZ / 8 + SZ_1K)

/*
 * This is how many bits of ID we need, including the useless ones.
 */
#define LPI_NRBITS		ilog2(LPI_PROPBASE_SZ + SZ_8K)

#define LPI_PROP_DEFAULT_PRIO	0xa0

static int __init its_alloc_lpi_tables(void)
{
	phys_addr_t paddr;

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
	__flush_dcache_area(page_address(gic_rdists->prop_page), LPI_PROPBASE_SZ);

	return 0;
}

static const char *its_base_type_string[] = {
	[GITS_BASER_TYPE_DEVICE]	= "Devices",
	[GITS_BASER_TYPE_VCPU]		= "Virtual CPUs",
	[GITS_BASER_TYPE_CPU]		= "Physical CPUs",
	[GITS_BASER_TYPE_COLLECTION]	= "Interrupt Collections",
	[GITS_BASER_TYPE_RESERVED5] 	= "Reserved (5)",
	[GITS_BASER_TYPE_RESERVED6] 	= "Reserved (6)",
	[GITS_BASER_TYPE_RESERVED7] 	= "Reserved (7)",
};

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

static int its_alloc_tables(const char *node_name, struct its_node *its)
{
	int err;
	int i;
	int psz = SZ_64K;
	u64 shr = GITS_BASER_InnerShareable;
	u64 cache;
	u64 typer;
	u32 ids;

	if (its->flags & ITS_FLAGS_WORKAROUND_CAVIUM_22375) {
		/*
		 * erratum 22375: only alloc 8MB table size
		 * erratum 24313: ignore memory access type
		 */
		cache	= 0;
		ids	= 0x14;			/* 20 bits, 8MB */
	} else {
		cache	= GITS_BASER_WaWb;
		typer	= readq_relaxed(its->base + GITS_TYPER);
		ids	= GITS_TYPER_DEVBITS(typer);
	}

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		u64 val = readq_relaxed(its->base + GITS_BASER + i * 8);
		u64 type = GITS_BASER_TYPE(val);
		u64 entry_size = GITS_BASER_ENTRY_SIZE(val);
		int order = get_order(psz);
		int alloc_pages;
		u64 tmp;
		void *base;

		if (type == GITS_BASER_TYPE_NONE)
			continue;

		/*
		 * Allocate as many entries as required to fit the
		 * range of device IDs that the ITS can grok... The ID
		 * space being incredibly sparse, this results in a
		 * massive waste of memory.
		 *
		 * For other tables, only allocate a single page.
		 */
		if (type == GITS_BASER_TYPE_DEVICE) {
			/*
			 * 'order' was initialized earlier to the default page
			 * granule of the the ITS.  We can't have an allocation
			 * smaller than that.  If the requested allocation
			 * is smaller, round up to the default page granule.
			 */
			order = max(get_order((1UL << ids) * entry_size),
				    order);
			if (order >= MAX_ORDER) {
				order = MAX_ORDER - 1;
				pr_warn("%s: Device Table too large, reduce its page order to %u\n",
					node_name, order);
			}
		}

retry_alloc_baser:
		alloc_pages = (PAGE_ORDER_TO_SIZE(order) / psz);
		if (alloc_pages > GITS_BASER_PAGES_MAX) {
			alloc_pages = GITS_BASER_PAGES_MAX;
			order = get_order(GITS_BASER_PAGES_MAX * psz);
			pr_warn("%s: Device Table too large, reduce its page order to %u (%u pages)\n",
				node_name, order, alloc_pages);
		}

		base = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
		if (!base) {
			err = -ENOMEM;
			goto out_free;
		}

		its->tables[i].base = base;
		its->tables[i].order = order;

retry_baser:
		val = (virt_to_phys(base) 				 |
		       (type << GITS_BASER_TYPE_SHIFT)			 |
		       ((entry_size - 1) << GITS_BASER_ENTRY_SIZE_SHIFT) |
		       cache						 |
		       shr						 |
		       GITS_BASER_VALID);

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

		val |= alloc_pages - 1;

		writeq_relaxed(val, its->base + GITS_BASER + i * 8);
		tmp = readq_relaxed(its->base + GITS_BASER + i * 8);

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
				__flush_dcache_area(base, PAGE_ORDER_TO_SIZE(order));
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
			its->tables[i].base = NULL;

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
			pr_err("ITS: %s: GITS_BASER%d doesn't stick: %lx %lx\n",
			       node_name, i,
			       (unsigned long) val, (unsigned long) tmp);
			err = -ENXIO;
			goto out_free;
		}

		pr_info("ITS: allocated %d %s @%lx (psz %dK, shr %d)\n",
			(int)(PAGE_ORDER_TO_SIZE(order) / entry_size),
			its_base_type_string[type],
			(unsigned long)virt_to_phys(base),
			psz / SZ_1K, (int)shr >> GITS_BASER_SHAREABILITY_SHIFT);
	}

	return 0;

out_free:
	its_free_tables(its);

	return err;
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
					get_order(max(LPI_PENDBASE_SZ, SZ_64K)));
		if (!pend_page) {
			pr_err("Failed to allocate PENDBASE for CPU%d\n",
			       smp_processor_id());
			return;
		}

		/* Make sure the GIC will observe the zero-ed page */
		__flush_dcache_area(page_address(pend_page), LPI_PENDBASE_SZ);

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
	       GICR_PROPBASER_WaWb |
	       ((LPI_NRBITS - 1) & GICR_PROPBASER_IDBITS_MASK));

	writeq_relaxed(val, rbase + GICR_PROPBASER);
	tmp = readq_relaxed(rbase + GICR_PROPBASER);

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
			writeq_relaxed(val, rbase + GICR_PROPBASER);
		}
		pr_info_once("GIC: using cache flushing for LPI property table\n");
		gic_rdists->flags |= RDIST_FLAGS_PROPBASE_NEEDS_FLUSHING;
	}

	/* set PENDBASE */
	val = (page_to_phys(pend_page) |
	       GICR_PENDBASER_InnerShareable |
	       GICR_PENDBASER_WaWb);

	writeq_relaxed(val, rbase + GICR_PENDBASER);
	tmp = readq_relaxed(rbase + GICR_PENDBASER);

	if (!(tmp & GICR_PENDBASER_SHAREABILITY_MASK)) {
		/*
		 * The HW reports non-shareable, we must remove the
		 * cacheability attributes as well.
		 */
		val &= ~(GICR_PENDBASER_SHAREABILITY_MASK |
			 GICR_PENDBASER_CACHEABILITY_MASK);
		val |= GICR_PENDBASER_nC;
		writeq_relaxed(val, rbase + GICR_PENDBASER);
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

		/*
		 * We now have to bind each collection to its target
		 * redistributor.
		 */
		if (readq_relaxed(its->base + GITS_TYPER) & GITS_TYPER_PTA) {
			/*
			 * This ITS wants the physical address of the
			 * redistributor.
			 */
			target = gic_data_rdist()->phys_base;
		} else {
			/*
			 * This ITS wants a linear CPU number.
			 */
			target = readq_relaxed(gic_data_rdist_rd_base() + GICR_TYPER);
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

	__flush_dcache_area(itt, sz);

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

	/* Bind the LPI to the first possible CPU */
	its_dev->event_map.col_map[event] = cpumask_first(cpu_online_mask);

	/* Map the GIC IRQ and event to the device */
	its_send_mapvi(its_dev, d->hwirq, event);
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
	if (val & GITS_CTLR_QUIESCENT)
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

static const struct gic_quirk its_quirks[] = {
#ifdef CONFIG_CAVIUM_ERRATUM_22375
	{
		.desc	= "ITS: Cavium errata 22375, 24313",
		.iidr	= 0xa100034c,	/* ThunderX pass 1.x */
		.mask	= 0xffff0fff,
		.init	= its_enable_quirk_cavium_22375,
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

static int its_probe(struct device_node *node, struct irq_domain *parent)
{
	struct resource res;
	struct its_node *its;
	void __iomem *its_base;
	struct irq_domain *inner_domain;
	u32 val;
	u64 baser, tmp;
	int err;

	err = of_address_to_resource(node, 0, &res);
	if (err) {
		pr_warn("%s: no regs?\n", node->full_name);
		return -ENXIO;
	}

	its_base = ioremap(res.start, resource_size(&res));
	if (!its_base) {
		pr_warn("%s: unable to map registers\n", node->full_name);
		return -ENOMEM;
	}

	val = readl_relaxed(its_base + GITS_PIDR2) & GIC_PIDR2_ARCH_MASK;
	if (val != 0x30 && val != 0x40) {
		pr_warn("%s: no ITS detected, giving up\n", node->full_name);
		err = -ENODEV;
		goto out_unmap;
	}

	err = its_force_quiescent(its_base);
	if (err) {
		pr_warn("%s: failed to quiesce, giving up\n",
			node->full_name);
		goto out_unmap;
	}

	pr_info("ITS: %s\n", node->full_name);

	its = kzalloc(sizeof(*its), GFP_KERNEL);
	if (!its) {
		err = -ENOMEM;
		goto out_unmap;
	}

	raw_spin_lock_init(&its->lock);
	INIT_LIST_HEAD(&its->entry);
	INIT_LIST_HEAD(&its->its_device_list);
	its->base = its_base;
	its->phys_base = res.start;
	its->ite_size = ((readl_relaxed(its_base + GITS_TYPER) >> 4) & 0xf) + 1;

	its->cmd_base = kzalloc(ITS_CMD_QUEUE_SZ, GFP_KERNEL);
	if (!its->cmd_base) {
		err = -ENOMEM;
		goto out_free_its;
	}
	its->cmd_write = its->cmd_base;

	its_enable_quirks(its);

	err = its_alloc_tables(node->full_name, its);
	if (err)
		goto out_free_cmd;

	err = its_alloc_collections(its);
	if (err)
		goto out_free_tables;

	baser = (virt_to_phys(its->cmd_base)	|
		 GITS_CBASER_WaWb		|
		 GITS_CBASER_InnerShareable	|
		 (ITS_CMD_QUEUE_SZ / SZ_4K - 1)	|
		 GITS_CBASER_VALID);

	writeq_relaxed(baser, its->base + GITS_CBASER);
	tmp = readq_relaxed(its->base + GITS_CBASER);

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
			writeq_relaxed(baser, its->base + GITS_CBASER);
		}
		pr_info("ITS: using cache flushing for cmd queue\n");
		its->flags |= ITS_FLAGS_CMDQ_NEEDS_FLUSHING;
	}

	writeq_relaxed(0, its->base + GITS_CWRITER);
	writel_relaxed(GITS_CTLR_ENABLE, its->base + GITS_CTLR);

	if (of_property_read_bool(node, "msi-controller")) {
		struct msi_domain_info *info;

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			err = -ENOMEM;
			goto out_free_tables;
		}

		inner_domain = irq_domain_add_tree(node, &its_domain_ops, its);
		if (!inner_domain) {
			err = -ENOMEM;
			kfree(info);
			goto out_free_tables;
		}

		inner_domain->parent = parent;
		inner_domain->bus_token = DOMAIN_BUS_NEXUS;
		info->ops = &its_msi_domain_ops;
		info->data = its;
		inner_domain->host_data = info;
	}

	spin_lock(&its_lock);
	list_add(&its->entry, &its_nodes);
	spin_unlock(&its_lock);

	return 0;

out_free_tables:
	its_free_tables(its);
out_free_cmd:
	kfree(its->cmd_base);
out_free_its:
	kfree(its);
out_unmap:
	iounmap(its_base);
	pr_err("ITS: failed probing %s (%d)\n", node->full_name, err);
	return err;
}

static bool gic_rdists_supports_plpis(void)
{
	return !!(readl_relaxed(gic_data_rdist_rd_base() + GICR_TYPER) & GICR_TYPER_PLPIS);
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

static struct of_device_id its_device_id[] = {
	{	.compatible	= "arm,gic-v3-its",	},
	{},
};

int its_init(struct device_node *node, struct rdists *rdists,
	     struct irq_domain *parent_domain)
{
	struct device_node *np;

	for (np = of_find_matching_node(node, its_device_id); np;
	     np = of_find_matching_node(np, its_device_id)) {
		its_probe(np, parent_domain);
	}

	if (list_empty(&its_nodes)) {
		pr_warn("ITS: No ITS available, not enabling LPIs\n");
		return -ENXIO;
	}

	gic_rdists = rdists;
	gic_root_node = node;

	its_alloc_lpi_tables();
	its_lpi_init(rdists->id_bits);

	return 0;
}
