// SPDX-License-Identifier: GPL-2.0
/*
 * TI K3 NAVSS Ring Accelerator subsystem driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sys_soc.h>
#include <linux/soc/ti/k3-ringacc.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <linux/soc/ti/ti_sci_inta_msi.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>

static LIST_HEAD(k3_ringacc_list);
static DEFINE_MUTEX(k3_ringacc_list_lock);

#define K3_RINGACC_CFG_RING_SIZE_ELCNT_MASK		GENMASK(19, 0)

/**
 * struct k3_ring_rt_regs - The RA realtime Control/Status Registers region
 *
 * @resv_16: Reserved
 * @db: Ring Doorbell Register
 * @resv_4: Reserved
 * @occ: Ring Occupancy Register
 * @indx: Ring Current Index Register
 * @hwocc: Ring Hardware Occupancy Register
 * @hwindx: Ring Hardware Current Index Register
 */
struct k3_ring_rt_regs {
	u32	resv_16[4];
	u32	db;
	u32	resv_4[1];
	u32	occ;
	u32	indx;
	u32	hwocc;
	u32	hwindx;
};

#define K3_RINGACC_RT_REGS_STEP	0x1000

/**
 * struct k3_ring_fifo_regs - The Ring Accelerator Queues Registers region
 *
 * @head_data: Ring Head Entry Data Registers
 * @tail_data: Ring Tail Entry Data Registers
 * @peek_head_data: Ring Peek Head Entry Data Regs
 * @peek_tail_data: Ring Peek Tail Entry Data Regs
 */
struct k3_ring_fifo_regs {
	u32	head_data[128];
	u32	tail_data[128];
	u32	peek_head_data[128];
	u32	peek_tail_data[128];
};

/**
 * struct k3_ringacc_proxy_gcfg_regs - RA Proxy Global Config MMIO Region
 *
 * @revision: Revision Register
 * @config: Config Register
 */
struct k3_ringacc_proxy_gcfg_regs {
	u32	revision;
	u32	config;
};

#define K3_RINGACC_PROXY_CFG_THREADS_MASK		GENMASK(15, 0)

/**
 * struct k3_ringacc_proxy_target_regs - Proxy Datapath MMIO Region
 *
 * @control: Proxy Control Register
 * @status: Proxy Status Register
 * @resv_512: Reserved
 * @data: Proxy Data Register
 */
struct k3_ringacc_proxy_target_regs {
	u32	control;
	u32	status;
	u8	resv_512[504];
	u32	data[128];
};

#define K3_RINGACC_PROXY_TARGET_STEP	0x1000
#define K3_RINGACC_PROXY_NOT_USED	(-1)

enum k3_ringacc_proxy_access_mode {
	PROXY_ACCESS_MODE_HEAD = 0,
	PROXY_ACCESS_MODE_TAIL = 1,
	PROXY_ACCESS_MODE_PEEK_HEAD = 2,
	PROXY_ACCESS_MODE_PEEK_TAIL = 3,
};

#define K3_RINGACC_FIFO_WINDOW_SIZE_BYTES  (512U)
#define K3_RINGACC_FIFO_REGS_STEP	0x1000
#define K3_RINGACC_MAX_DB_RING_CNT    (127U)

struct k3_ring_ops {
	int (*push_tail)(struct k3_ring *ring, void *elm);
	int (*push_head)(struct k3_ring *ring, void *elm);
	int (*pop_tail)(struct k3_ring *ring, void *elm);
	int (*pop_head)(struct k3_ring *ring, void *elm);
};

/**
 * struct k3_ring_state - Internal state tracking structure
 *
 * @free: Number of free entries
 * @occ: Occupancy
 * @windex: Write index
 * @rindex: Read index
 */
struct k3_ring_state {
	u32 free;
	u32 occ;
	u32 windex;
	u32 rindex;
};

/**
 * struct k3_ring - RA Ring descriptor
 *
 * @rt: Ring control/status registers
 * @fifos: Ring queues registers
 * @proxy: Ring Proxy Datapath registers
 * @ring_mem_dma: Ring buffer dma address
 * @ring_mem_virt: Ring buffer virt address
 * @ops: Ring operations
 * @size: Ring size in elements
 * @elm_size: Size of the ring element
 * @mode: Ring mode
 * @flags: flags
 * @ring_id: Ring Id
 * @parent: Pointer on struct @k3_ringacc
 * @use_count: Use count for shared rings
 * @proxy_id: RA Ring Proxy Id (only if @K3_RINGACC_RING_USE_PROXY)
 */
struct k3_ring {
	struct k3_ring_rt_regs __iomem *rt;
	struct k3_ring_fifo_regs __iomem *fifos;
	struct k3_ringacc_proxy_target_regs  __iomem *proxy;
	dma_addr_t	ring_mem_dma;
	void		*ring_mem_virt;
	struct k3_ring_ops *ops;
	u32		size;
	enum k3_ring_size elm_size;
	enum k3_ring_mode mode;
	u32		flags;
#define K3_RING_FLAG_BUSY	BIT(1)
#define K3_RING_FLAG_SHARED	BIT(2)
	struct k3_ring_state state;
	u32		ring_id;
	struct k3_ringacc	*parent;
	u32		use_count;
	int		proxy_id;
};

struct k3_ringacc_ops {
	int (*init)(struct platform_device *pdev, struct k3_ringacc *ringacc);
};

/**
 * struct k3_ringacc - Rings accelerator descriptor
 *
 * @dev: pointer on RA device
 * @proxy_gcfg: RA proxy global config registers
 * @proxy_target_base: RA proxy datapath region
 * @num_rings: number of ring in RA
 * @rings_inuse: bitfield for ring usage tracking
 * @rm_gp_range: general purpose rings range from tisci
 * @dma_ring_reset_quirk: DMA reset w/a enable
 * @num_proxies: number of RA proxies
 * @proxy_inuse: bitfield for proxy usage tracking
 * @rings: array of rings descriptors (struct @k3_ring)
 * @list: list of RAs in the system
 * @req_lock: protect rings allocation
 * @tisci: pointer ti-sci handle
 * @tisci_ring_ops: ti-sci rings ops
 * @tisci_dev_id: ti-sci device id
 * @ops: SoC specific ringacc operation
 */
struct k3_ringacc {
	struct device *dev;
	struct k3_ringacc_proxy_gcfg_regs __iomem *proxy_gcfg;
	void __iomem *proxy_target_base;
	u32 num_rings; /* number of rings in Ringacc module */
	unsigned long *rings_inuse;
	struct ti_sci_resource *rm_gp_range;

	bool dma_ring_reset_quirk;
	u32 num_proxies;
	unsigned long *proxy_inuse;

	struct k3_ring *rings;
	struct list_head list;
	struct mutex req_lock; /* protect rings allocation */

	const struct ti_sci_handle *tisci;
	const struct ti_sci_rm_ringacc_ops *tisci_ring_ops;
	u32 tisci_dev_id;

	const struct k3_ringacc_ops *ops;
};

/**
 * struct k3_ringacc - Rings accelerator SoC data
 *
 * @dma_ring_reset_quirk:  DMA reset w/a enable
 */
struct k3_ringacc_soc_data {
	unsigned dma_ring_reset_quirk:1;
};

static long k3_ringacc_ring_get_fifo_pos(struct k3_ring *ring)
{
	return K3_RINGACC_FIFO_WINDOW_SIZE_BYTES -
	       (4 << ring->elm_size);
}

static void *k3_ringacc_get_elm_addr(struct k3_ring *ring, u32 idx)
{
	return (ring->ring_mem_virt + idx * (4 << ring->elm_size));
}

static int k3_ringacc_ring_push_mem(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_pop_mem(struct k3_ring *ring, void *elem);

static struct k3_ring_ops k3_ring_mode_ring_ops = {
		.push_tail = k3_ringacc_ring_push_mem,
		.pop_head = k3_ringacc_ring_pop_mem,
};

static int k3_ringacc_ring_push_io(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_pop_io(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_push_head_io(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_pop_tail_io(struct k3_ring *ring, void *elem);

static struct k3_ring_ops k3_ring_mode_msg_ops = {
		.push_tail = k3_ringacc_ring_push_io,
		.push_head = k3_ringacc_ring_push_head_io,
		.pop_tail = k3_ringacc_ring_pop_tail_io,
		.pop_head = k3_ringacc_ring_pop_io,
};

static int k3_ringacc_ring_push_head_proxy(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_push_tail_proxy(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_pop_head_proxy(struct k3_ring *ring, void *elem);
static int k3_ringacc_ring_pop_tail_proxy(struct k3_ring *ring, void *elem);

static struct k3_ring_ops k3_ring_mode_proxy_ops = {
		.push_tail = k3_ringacc_ring_push_tail_proxy,
		.push_head = k3_ringacc_ring_push_head_proxy,
		.pop_tail = k3_ringacc_ring_pop_tail_proxy,
		.pop_head = k3_ringacc_ring_pop_head_proxy,
};

static void k3_ringacc_ring_dump(struct k3_ring *ring)
{
	struct device *dev = ring->parent->dev;

	dev_dbg(dev, "dump ring: %d\n", ring->ring_id);
	dev_dbg(dev, "dump mem virt %p, dma %pad\n", ring->ring_mem_virt,
		&ring->ring_mem_dma);
	dev_dbg(dev, "dump elmsize %d, size %d, mode %d, proxy_id %d\n",
		ring->elm_size, ring->size, ring->mode, ring->proxy_id);
	dev_dbg(dev, "dump flags %08X\n", ring->flags);

	dev_dbg(dev, "dump ring_rt_regs: db%08x\n", readl(&ring->rt->db));
	dev_dbg(dev, "dump occ%08x\n", readl(&ring->rt->occ));
	dev_dbg(dev, "dump indx%08x\n", readl(&ring->rt->indx));
	dev_dbg(dev, "dump hwocc%08x\n", readl(&ring->rt->hwocc));
	dev_dbg(dev, "dump hwindx%08x\n", readl(&ring->rt->hwindx));

	if (ring->ring_mem_virt)
		print_hex_dump_debug("dump ring_mem_virt ", DUMP_PREFIX_NONE,
				     16, 1, ring->ring_mem_virt, 16 * 8, false);
}

struct k3_ring *k3_ringacc_request_ring(struct k3_ringacc *ringacc,
					int id, u32 flags)
{
	int proxy_id = K3_RINGACC_PROXY_NOT_USED;

	mutex_lock(&ringacc->req_lock);

	if (id == K3_RINGACC_RING_ID_ANY) {
		/* Request for any general purpose ring */
		struct ti_sci_resource_desc *gp_rings =
						&ringacc->rm_gp_range->desc[0];
		unsigned long size;

		size = gp_rings->start + gp_rings->num;
		id = find_next_zero_bit(ringacc->rings_inuse, size,
					gp_rings->start);
		if (id == size)
			goto error;
	} else if (id < 0) {
		goto error;
	}

	if (test_bit(id, ringacc->rings_inuse) &&
	    !(ringacc->rings[id].flags & K3_RING_FLAG_SHARED))
		goto error;
	else if (ringacc->rings[id].flags & K3_RING_FLAG_SHARED)
		goto out;

	if (flags & K3_RINGACC_RING_USE_PROXY) {
		proxy_id = find_next_zero_bit(ringacc->proxy_inuse,
					      ringacc->num_proxies, 0);
		if (proxy_id == ringacc->num_proxies)
			goto error;
	}

	if (proxy_id != K3_RINGACC_PROXY_NOT_USED) {
		set_bit(proxy_id, ringacc->proxy_inuse);
		ringacc->rings[id].proxy_id = proxy_id;
		dev_dbg(ringacc->dev, "Giving ring#%d proxy#%d\n", id,
			proxy_id);
	} else {
		dev_dbg(ringacc->dev, "Giving ring#%d\n", id);
	}

	set_bit(id, ringacc->rings_inuse);
out:
	ringacc->rings[id].use_count++;
	mutex_unlock(&ringacc->req_lock);
	return &ringacc->rings[id];

error:
	mutex_unlock(&ringacc->req_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(k3_ringacc_request_ring);

int k3_ringacc_request_rings_pair(struct k3_ringacc *ringacc,
				  int fwd_id, int compl_id,
				  struct k3_ring **fwd_ring,
				  struct k3_ring **compl_ring)
{
	int ret = 0;

	if (!fwd_ring || !compl_ring)
		return -EINVAL;

	*fwd_ring = k3_ringacc_request_ring(ringacc, fwd_id, 0);
	if (!(*fwd_ring))
		return -ENODEV;

	*compl_ring = k3_ringacc_request_ring(ringacc, compl_id, 0);
	if (!(*compl_ring)) {
		k3_ringacc_ring_free(*fwd_ring);
		ret = -ENODEV;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_request_rings_pair);

static void k3_ringacc_ring_reset_sci(struct k3_ring *ring)
{
	struct k3_ringacc *ringacc = ring->parent;
	int ret;

	ret = ringacc->tisci_ring_ops->config(
			ringacc->tisci,
			TI_SCI_MSG_VALUE_RM_RING_COUNT_VALID,
			ringacc->tisci_dev_id,
			ring->ring_id,
			0,
			0,
			ring->size,
			0,
			0,
			0);
	if (ret)
		dev_err(ringacc->dev, "TISCI reset ring fail (%d) ring_idx %d\n",
			ret, ring->ring_id);
}

void k3_ringacc_ring_reset(struct k3_ring *ring)
{
	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return;

	memset(&ring->state, 0, sizeof(ring->state));

	k3_ringacc_ring_reset_sci(ring);
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_reset);

static void k3_ringacc_ring_reconfig_qmode_sci(struct k3_ring *ring,
					       enum k3_ring_mode mode)
{
	struct k3_ringacc *ringacc = ring->parent;
	int ret;

	ret = ringacc->tisci_ring_ops->config(
			ringacc->tisci,
			TI_SCI_MSG_VALUE_RM_RING_MODE_VALID,
			ringacc->tisci_dev_id,
			ring->ring_id,
			0,
			0,
			0,
			mode,
			0,
			0);
	if (ret)
		dev_err(ringacc->dev, "TISCI reconf qmode fail (%d) ring_idx %d\n",
			ret, ring->ring_id);
}

void k3_ringacc_ring_reset_dma(struct k3_ring *ring, u32 occ)
{
	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return;

	if (!ring->parent->dma_ring_reset_quirk)
		goto reset;

	if (!occ)
		occ = readl(&ring->rt->occ);

	if (occ) {
		u32 db_ring_cnt, db_ring_cnt_cur;

		dev_dbg(ring->parent->dev, "%s %u occ: %u\n", __func__,
			ring->ring_id, occ);
		/* TI-SCI ring reset */
		k3_ringacc_ring_reset_sci(ring);

		/*
		 * Setup the ring in ring/doorbell mode (if not already in this
		 * mode)
		 */
		if (ring->mode != K3_RINGACC_RING_MODE_RING)
			k3_ringacc_ring_reconfig_qmode_sci(
					ring, K3_RINGACC_RING_MODE_RING);
		/*
		 * Ring the doorbell 2**22 – ringOcc times.
		 * This will wrap the internal UDMAP ring state occupancy
		 * counter (which is 21-bits wide) to 0.
		 */
		db_ring_cnt = (1U << 22) - occ;

		while (db_ring_cnt != 0) {
			/*
			 * Ring the doorbell with the maximum count each
			 * iteration if possible to minimize the total
			 * of writes
			 */
			if (db_ring_cnt > K3_RINGACC_MAX_DB_RING_CNT)
				db_ring_cnt_cur = K3_RINGACC_MAX_DB_RING_CNT;
			else
				db_ring_cnt_cur = db_ring_cnt;

			writel(db_ring_cnt_cur, &ring->rt->db);
			db_ring_cnt -= db_ring_cnt_cur;
		}

		/* Restore the original ring mode (if not ring mode) */
		if (ring->mode != K3_RINGACC_RING_MODE_RING)
			k3_ringacc_ring_reconfig_qmode_sci(ring, ring->mode);
	}

reset:
	/* Reset the ring */
	k3_ringacc_ring_reset(ring);
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_reset_dma);

static void k3_ringacc_ring_free_sci(struct k3_ring *ring)
{
	struct k3_ringacc *ringacc = ring->parent;
	int ret;

	ret = ringacc->tisci_ring_ops->config(
			ringacc->tisci,
			TI_SCI_MSG_VALUE_RM_ALL_NO_ORDER,
			ringacc->tisci_dev_id,
			ring->ring_id,
			0,
			0,
			0,
			0,
			0,
			0);
	if (ret)
		dev_err(ringacc->dev, "TISCI ring free fail (%d) ring_idx %d\n",
			ret, ring->ring_id);
}

int k3_ringacc_ring_free(struct k3_ring *ring)
{
	struct k3_ringacc *ringacc;

	if (!ring)
		return -EINVAL;

	ringacc = ring->parent;

	dev_dbg(ring->parent->dev, "flags: 0x%08x\n", ring->flags);

	if (!test_bit(ring->ring_id, ringacc->rings_inuse))
		return -EINVAL;

	mutex_lock(&ringacc->req_lock);

	if (--ring->use_count)
		goto out;

	if (!(ring->flags & K3_RING_FLAG_BUSY))
		goto no_init;

	k3_ringacc_ring_free_sci(ring);

	dma_free_coherent(ringacc->dev,
			  ring->size * (4 << ring->elm_size),
			  ring->ring_mem_virt, ring->ring_mem_dma);
	ring->flags = 0;
	ring->ops = NULL;
	if (ring->proxy_id != K3_RINGACC_PROXY_NOT_USED) {
		clear_bit(ring->proxy_id, ringacc->proxy_inuse);
		ring->proxy = NULL;
		ring->proxy_id = K3_RINGACC_PROXY_NOT_USED;
	}

no_init:
	clear_bit(ring->ring_id, ringacc->rings_inuse);

out:
	mutex_unlock(&ringacc->req_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_free);

u32 k3_ringacc_get_ring_id(struct k3_ring *ring)
{
	if (!ring)
		return -EINVAL;

	return ring->ring_id;
}
EXPORT_SYMBOL_GPL(k3_ringacc_get_ring_id);

u32 k3_ringacc_get_tisci_dev_id(struct k3_ring *ring)
{
	if (!ring)
		return -EINVAL;

	return ring->parent->tisci_dev_id;
}
EXPORT_SYMBOL_GPL(k3_ringacc_get_tisci_dev_id);

int k3_ringacc_get_ring_irq_num(struct k3_ring *ring)
{
	int irq_num;

	if (!ring)
		return -EINVAL;

	irq_num = ti_sci_inta_msi_get_virq(ring->parent->dev, ring->ring_id);
	if (irq_num <= 0)
		irq_num = -EINVAL;
	return irq_num;
}
EXPORT_SYMBOL_GPL(k3_ringacc_get_ring_irq_num);

static int k3_ringacc_ring_cfg_sci(struct k3_ring *ring)
{
	struct k3_ringacc *ringacc = ring->parent;
	u32 ring_idx;
	int ret;

	if (!ringacc->tisci)
		return -EINVAL;

	ring_idx = ring->ring_id;
	ret = ringacc->tisci_ring_ops->config(
			ringacc->tisci,
			TI_SCI_MSG_VALUE_RM_ALL_NO_ORDER,
			ringacc->tisci_dev_id,
			ring_idx,
			lower_32_bits(ring->ring_mem_dma),
			upper_32_bits(ring->ring_mem_dma),
			ring->size,
			ring->mode,
			ring->elm_size,
			0);
	if (ret)
		dev_err(ringacc->dev, "TISCI config ring fail (%d) ring_idx %d\n",
			ret, ring_idx);

	return ret;
}

int k3_ringacc_ring_cfg(struct k3_ring *ring, struct k3_ring_cfg *cfg)
{
	struct k3_ringacc *ringacc;
	int ret = 0;

	if (!ring || !cfg)
		return -EINVAL;
	ringacc = ring->parent;

	if (cfg->elm_size > K3_RINGACC_RING_ELSIZE_256 ||
	    cfg->mode >= K3_RINGACC_RING_MODE_INVALID ||
	    cfg->size & ~K3_RINGACC_CFG_RING_SIZE_ELCNT_MASK ||
	    !test_bit(ring->ring_id, ringacc->rings_inuse))
		return -EINVAL;

	if (cfg->mode == K3_RINGACC_RING_MODE_MESSAGE &&
	    ring->proxy_id == K3_RINGACC_PROXY_NOT_USED &&
	    cfg->elm_size > K3_RINGACC_RING_ELSIZE_8) {
		dev_err(ringacc->dev,
			"Message mode must use proxy for %u element size\n",
			4 << ring->elm_size);
		return -EINVAL;
	}

	/*
	 * In case of shared ring only the first user (master user) can
	 * configure the ring. The sequence should be by the client:
	 * ring = k3_ringacc_request_ring(ringacc, ring_id, 0); # master user
	 * k3_ringacc_ring_cfg(ring, cfg); # master configuration
	 * k3_ringacc_request_ring(ringacc, ring_id, K3_RING_FLAG_SHARED);
	 * k3_ringacc_request_ring(ringacc, ring_id, K3_RING_FLAG_SHARED);
	 */
	if (ring->use_count != 1)
		return 0;

	ring->size = cfg->size;
	ring->elm_size = cfg->elm_size;
	ring->mode = cfg->mode;
	memset(&ring->state, 0, sizeof(ring->state));

	if (ring->proxy_id != K3_RINGACC_PROXY_NOT_USED)
		ring->proxy = ringacc->proxy_target_base +
			      ring->proxy_id * K3_RINGACC_PROXY_TARGET_STEP;

	switch (ring->mode) {
	case K3_RINGACC_RING_MODE_RING:
		ring->ops = &k3_ring_mode_ring_ops;
		break;
	case K3_RINGACC_RING_MODE_MESSAGE:
		if (ring->proxy)
			ring->ops = &k3_ring_mode_proxy_ops;
		else
			ring->ops = &k3_ring_mode_msg_ops;
		break;
	default:
		ring->ops = NULL;
		ret = -EINVAL;
		goto err_free_proxy;
	}

	ring->ring_mem_virt = dma_alloc_coherent(ringacc->dev,
					ring->size * (4 << ring->elm_size),
					&ring->ring_mem_dma, GFP_KERNEL);
	if (!ring->ring_mem_virt) {
		dev_err(ringacc->dev, "Failed to alloc ring mem\n");
		ret = -ENOMEM;
		goto err_free_ops;
	}

	ret = k3_ringacc_ring_cfg_sci(ring);

	if (ret)
		goto err_free_mem;

	ring->flags |= K3_RING_FLAG_BUSY;
	ring->flags |= (cfg->flags & K3_RINGACC_RING_SHARED) ?
			K3_RING_FLAG_SHARED : 0;

	k3_ringacc_ring_dump(ring);

	return 0;

err_free_mem:
	dma_free_coherent(ringacc->dev,
			  ring->size * (4 << ring->elm_size),
			  ring->ring_mem_virt,
			  ring->ring_mem_dma);
err_free_ops:
	ring->ops = NULL;
err_free_proxy:
	ring->proxy = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_cfg);

u32 k3_ringacc_ring_get_size(struct k3_ring *ring)
{
	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	return ring->size;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_get_size);

u32 k3_ringacc_ring_get_free(struct k3_ring *ring)
{
	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	if (!ring->state.free)
		ring->state.free = ring->size - readl(&ring->rt->occ);

	return ring->state.free;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_get_free);

u32 k3_ringacc_ring_get_occ(struct k3_ring *ring)
{
	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	return readl(&ring->rt->occ);
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_get_occ);

u32 k3_ringacc_ring_is_full(struct k3_ring *ring)
{
	return !k3_ringacc_ring_get_free(ring);
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_is_full);

enum k3_ringacc_access_mode {
	K3_RINGACC_ACCESS_MODE_PUSH_HEAD,
	K3_RINGACC_ACCESS_MODE_POP_HEAD,
	K3_RINGACC_ACCESS_MODE_PUSH_TAIL,
	K3_RINGACC_ACCESS_MODE_POP_TAIL,
	K3_RINGACC_ACCESS_MODE_PEEK_HEAD,
	K3_RINGACC_ACCESS_MODE_PEEK_TAIL,
};

#define K3_RINGACC_PROXY_MODE(x)	(((x) & 0x3) << 16)
#define K3_RINGACC_PROXY_ELSIZE(x)	(((x) & 0x7) << 24)
static int k3_ringacc_ring_cfg_proxy(struct k3_ring *ring,
				     enum k3_ringacc_proxy_access_mode mode)
{
	u32 val;

	val = ring->ring_id;
	val |= K3_RINGACC_PROXY_MODE(mode);
	val |= K3_RINGACC_PROXY_ELSIZE(ring->elm_size);
	writel(val, &ring->proxy->control);
	return 0;
}

static int k3_ringacc_ring_access_proxy(struct k3_ring *ring, void *elem,
					enum k3_ringacc_access_mode access_mode)
{
	void __iomem *ptr;

	ptr = (void __iomem *)&ring->proxy->data;

	switch (access_mode) {
	case K3_RINGACC_ACCESS_MODE_PUSH_HEAD:
	case K3_RINGACC_ACCESS_MODE_POP_HEAD:
		k3_ringacc_ring_cfg_proxy(ring, PROXY_ACCESS_MODE_HEAD);
		break;
	case K3_RINGACC_ACCESS_MODE_PUSH_TAIL:
	case K3_RINGACC_ACCESS_MODE_POP_TAIL:
		k3_ringacc_ring_cfg_proxy(ring, PROXY_ACCESS_MODE_TAIL);
		break;
	default:
		return -EINVAL;
	}

	ptr += k3_ringacc_ring_get_fifo_pos(ring);

	switch (access_mode) {
	case K3_RINGACC_ACCESS_MODE_POP_HEAD:
	case K3_RINGACC_ACCESS_MODE_POP_TAIL:
		dev_dbg(ring->parent->dev,
			"proxy:memcpy_fromio(x): --> ptr(%p), mode:%d\n", ptr,
			access_mode);
		memcpy_fromio(elem, ptr, (4 << ring->elm_size));
		ring->state.occ--;
		break;
	case K3_RINGACC_ACCESS_MODE_PUSH_TAIL:
	case K3_RINGACC_ACCESS_MODE_PUSH_HEAD:
		dev_dbg(ring->parent->dev,
			"proxy:memcpy_toio(x): --> ptr(%p), mode:%d\n", ptr,
			access_mode);
		memcpy_toio(ptr, elem, (4 << ring->elm_size));
		ring->state.free--;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(ring->parent->dev, "proxy: free%d occ%d\n", ring->state.free,
		ring->state.occ);
	return 0;
}

static int k3_ringacc_ring_push_head_proxy(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_proxy(ring, elem,
					    K3_RINGACC_ACCESS_MODE_PUSH_HEAD);
}

static int k3_ringacc_ring_push_tail_proxy(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_proxy(ring, elem,
					    K3_RINGACC_ACCESS_MODE_PUSH_TAIL);
}

static int k3_ringacc_ring_pop_head_proxy(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_proxy(ring, elem,
					    K3_RINGACC_ACCESS_MODE_POP_HEAD);
}

static int k3_ringacc_ring_pop_tail_proxy(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_proxy(ring, elem,
					    K3_RINGACC_ACCESS_MODE_POP_HEAD);
}

static int k3_ringacc_ring_access_io(struct k3_ring *ring, void *elem,
				     enum k3_ringacc_access_mode access_mode)
{
	void __iomem *ptr;

	switch (access_mode) {
	case K3_RINGACC_ACCESS_MODE_PUSH_HEAD:
	case K3_RINGACC_ACCESS_MODE_POP_HEAD:
		ptr = (void __iomem *)&ring->fifos->head_data;
		break;
	case K3_RINGACC_ACCESS_MODE_PUSH_TAIL:
	case K3_RINGACC_ACCESS_MODE_POP_TAIL:
		ptr = (void __iomem *)&ring->fifos->tail_data;
		break;
	default:
		return -EINVAL;
	}

	ptr += k3_ringacc_ring_get_fifo_pos(ring);

	switch (access_mode) {
	case K3_RINGACC_ACCESS_MODE_POP_HEAD:
	case K3_RINGACC_ACCESS_MODE_POP_TAIL:
		dev_dbg(ring->parent->dev,
			"memcpy_fromio(x): --> ptr(%p), mode:%d\n", ptr,
			access_mode);
		memcpy_fromio(elem, ptr, (4 << ring->elm_size));
		ring->state.occ--;
		break;
	case K3_RINGACC_ACCESS_MODE_PUSH_TAIL:
	case K3_RINGACC_ACCESS_MODE_PUSH_HEAD:
		dev_dbg(ring->parent->dev,
			"memcpy_toio(x): --> ptr(%p), mode:%d\n", ptr,
			access_mode);
		memcpy_toio(ptr, elem, (4 << ring->elm_size));
		ring->state.free--;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(ring->parent->dev, "free%d index%d occ%d index%d\n",
		ring->state.free, ring->state.windex, ring->state.occ,
		ring->state.rindex);
	return 0;
}

static int k3_ringacc_ring_push_head_io(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_io(ring, elem,
					 K3_RINGACC_ACCESS_MODE_PUSH_HEAD);
}

static int k3_ringacc_ring_push_io(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_io(ring, elem,
					 K3_RINGACC_ACCESS_MODE_PUSH_TAIL);
}

static int k3_ringacc_ring_pop_io(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_io(ring, elem,
					 K3_RINGACC_ACCESS_MODE_POP_HEAD);
}

static int k3_ringacc_ring_pop_tail_io(struct k3_ring *ring, void *elem)
{
	return k3_ringacc_ring_access_io(ring, elem,
					 K3_RINGACC_ACCESS_MODE_POP_HEAD);
}

static int k3_ringacc_ring_push_mem(struct k3_ring *ring, void *elem)
{
	void *elem_ptr;

	elem_ptr = k3_ringacc_get_elm_addr(ring, ring->state.windex);

	memcpy(elem_ptr, elem, (4 << ring->elm_size));

	ring->state.windex = (ring->state.windex + 1) % ring->size;
	ring->state.free--;
	writel(1, &ring->rt->db);

	dev_dbg(ring->parent->dev, "ring_push_mem: free%d index%d\n",
		ring->state.free, ring->state.windex);

	return 0;
}

static int k3_ringacc_ring_pop_mem(struct k3_ring *ring, void *elem)
{
	void *elem_ptr;

	elem_ptr = k3_ringacc_get_elm_addr(ring, ring->state.rindex);

	memcpy(elem, elem_ptr, (4 << ring->elm_size));

	ring->state.rindex = (ring->state.rindex + 1) % ring->size;
	ring->state.occ--;
	writel(-1, &ring->rt->db);

	dev_dbg(ring->parent->dev, "ring_pop_mem: occ%d index%d pos_ptr%p\n",
		ring->state.occ, ring->state.rindex, elem_ptr);
	return 0;
}

int k3_ringacc_ring_push(struct k3_ring *ring, void *elem)
{
	int ret = -EOPNOTSUPP;

	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	dev_dbg(ring->parent->dev, "ring_push: free%d index%d\n",
		ring->state.free, ring->state.windex);

	if (k3_ringacc_ring_is_full(ring))
		return -ENOMEM;

	if (ring->ops && ring->ops->push_tail)
		ret = ring->ops->push_tail(ring, elem);

	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_push);

int k3_ringacc_ring_push_head(struct k3_ring *ring, void *elem)
{
	int ret = -EOPNOTSUPP;

	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	dev_dbg(ring->parent->dev, "ring_push_head: free%d index%d\n",
		ring->state.free, ring->state.windex);

	if (k3_ringacc_ring_is_full(ring))
		return -ENOMEM;

	if (ring->ops && ring->ops->push_head)
		ret = ring->ops->push_head(ring, elem);

	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_push_head);

int k3_ringacc_ring_pop(struct k3_ring *ring, void *elem)
{
	int ret = -EOPNOTSUPP;

	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	if (!ring->state.occ)
		ring->state.occ = k3_ringacc_ring_get_occ(ring);

	dev_dbg(ring->parent->dev, "ring_pop: occ%d index%d\n", ring->state.occ,
		ring->state.rindex);

	if (!ring->state.occ)
		return -ENODATA;

	if (ring->ops && ring->ops->pop_head)
		ret = ring->ops->pop_head(ring, elem);

	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_pop);

int k3_ringacc_ring_pop_tail(struct k3_ring *ring, void *elem)
{
	int ret = -EOPNOTSUPP;

	if (!ring || !(ring->flags & K3_RING_FLAG_BUSY))
		return -EINVAL;

	if (!ring->state.occ)
		ring->state.occ = k3_ringacc_ring_get_occ(ring);

	dev_dbg(ring->parent->dev, "ring_pop_tail: occ%d index%d\n",
		ring->state.occ, ring->state.rindex);

	if (!ring->state.occ)
		return -ENODATA;

	if (ring->ops && ring->ops->pop_tail)
		ret = ring->ops->pop_tail(ring, elem);

	return ret;
}
EXPORT_SYMBOL_GPL(k3_ringacc_ring_pop_tail);

struct k3_ringacc *of_k3_ringacc_get_by_phandle(struct device_node *np,
						const char *property)
{
	struct device_node *ringacc_np;
	struct k3_ringacc *ringacc = ERR_PTR(-EPROBE_DEFER);
	struct k3_ringacc *entry;

	ringacc_np = of_parse_phandle(np, property, 0);
	if (!ringacc_np)
		return ERR_PTR(-ENODEV);

	mutex_lock(&k3_ringacc_list_lock);
	list_for_each_entry(entry, &k3_ringacc_list, list)
		if (entry->dev->of_node == ringacc_np) {
			ringacc = entry;
			break;
		}
	mutex_unlock(&k3_ringacc_list_lock);
	of_node_put(ringacc_np);

	return ringacc;
}
EXPORT_SYMBOL_GPL(of_k3_ringacc_get_by_phandle);

static int k3_ringacc_probe_dt(struct k3_ringacc *ringacc)
{
	struct device_node *node = ringacc->dev->of_node;
	struct device *dev = ringacc->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	if (!node) {
		dev_err(dev, "device tree info unavailable\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(node, "ti,num-rings", &ringacc->num_rings);
	if (ret) {
		dev_err(dev, "ti,num-rings read failure %d\n", ret);
		return ret;
	}

	ringacc->tisci = ti_sci_get_by_phandle(node, "ti,sci");
	if (IS_ERR(ringacc->tisci)) {
		ret = PTR_ERR(ringacc->tisci);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "ti,sci read fail %d\n", ret);
		ringacc->tisci = NULL;
		return ret;
	}

	ret = of_property_read_u32(node, "ti,sci-dev-id",
				   &ringacc->tisci_dev_id);
	if (ret) {
		dev_err(dev, "ti,sci-dev-id read fail %d\n", ret);
		return ret;
	}

	pdev->id = ringacc->tisci_dev_id;

	ringacc->rm_gp_range = devm_ti_sci_get_of_resource(ringacc->tisci, dev,
						ringacc->tisci_dev_id,
						"ti,sci-rm-range-gp-rings");
	if (IS_ERR(ringacc->rm_gp_range)) {
		dev_err(dev, "Failed to allocate MSI interrupts\n");
		return PTR_ERR(ringacc->rm_gp_range);
	}

	return ti_sci_inta_msi_domain_alloc_irqs(ringacc->dev,
						 ringacc->rm_gp_range);
}

static const struct k3_ringacc_soc_data k3_ringacc_soc_data_sr1 = {
	.dma_ring_reset_quirk = 1,
};

static const struct soc_device_attribute k3_ringacc_socinfo[] = {
	{ .family = "AM65X",
	  .revision = "SR1.0",
	  .data = &k3_ringacc_soc_data_sr1
	},
	{/* sentinel */}
};

static int k3_ringacc_init(struct platform_device *pdev,
			   struct k3_ringacc *ringacc)
{
	const struct soc_device_attribute *soc;
	void __iomem *base_fifo, *base_rt;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret, i;

	dev->msi_domain = of_msi_get_domain(dev, dev->of_node,
					    DOMAIN_BUS_TI_SCI_INTA_MSI);
	if (!dev->msi_domain) {
		dev_err(dev, "Failed to get MSI domain\n");
		return -EPROBE_DEFER;
	}

	ret = k3_ringacc_probe_dt(ringacc);
	if (ret)
		return ret;

	soc = soc_device_match(k3_ringacc_socinfo);
	if (soc && soc->data) {
		const struct k3_ringacc_soc_data *soc_data = soc->data;

		ringacc->dma_ring_reset_quirk = soc_data->dma_ring_reset_quirk;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rt");
	base_rt = devm_ioremap_resource(dev, res);
	if (IS_ERR(base_rt))
		return PTR_ERR(base_rt);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fifos");
	base_fifo = devm_ioremap_resource(dev, res);
	if (IS_ERR(base_fifo))
		return PTR_ERR(base_fifo);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "proxy_gcfg");
	ringacc->proxy_gcfg = devm_ioremap_resource(dev, res);
	if (IS_ERR(ringacc->proxy_gcfg))
		return PTR_ERR(ringacc->proxy_gcfg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "proxy_target");
	ringacc->proxy_target_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ringacc->proxy_target_base))
		return PTR_ERR(ringacc->proxy_target_base);

	ringacc->num_proxies = readl(&ringacc->proxy_gcfg->config) &
				     K3_RINGACC_PROXY_CFG_THREADS_MASK;

	ringacc->rings = devm_kzalloc(dev,
				      sizeof(*ringacc->rings) *
				      ringacc->num_rings,
				      GFP_KERNEL);
	ringacc->rings_inuse = devm_kcalloc(dev,
					    BITS_TO_LONGS(ringacc->num_rings),
					    sizeof(unsigned long), GFP_KERNEL);
	ringacc->proxy_inuse = devm_kcalloc(dev,
					    BITS_TO_LONGS(ringacc->num_proxies),
					    sizeof(unsigned long), GFP_KERNEL);

	if (!ringacc->rings || !ringacc->rings_inuse || !ringacc->proxy_inuse)
		return -ENOMEM;

	for (i = 0; i < ringacc->num_rings; i++) {
		ringacc->rings[i].rt = base_rt +
				       K3_RINGACC_RT_REGS_STEP * i;
		ringacc->rings[i].fifos = base_fifo +
					  K3_RINGACC_FIFO_REGS_STEP * i;
		ringacc->rings[i].parent = ringacc;
		ringacc->rings[i].ring_id = i;
		ringacc->rings[i].proxy_id = K3_RINGACC_PROXY_NOT_USED;
	}

	ringacc->tisci_ring_ops = &ringacc->tisci->ops.rm_ring_ops;

	dev_info(dev, "Ring Accelerator probed rings:%u, gp-rings[%u,%u] sci-dev-id:%u\n",
		 ringacc->num_rings,
		 ringacc->rm_gp_range->desc[0].start,
		 ringacc->rm_gp_range->desc[0].num,
		 ringacc->tisci_dev_id);
	dev_info(dev, "dma-ring-reset-quirk: %s\n",
		 ringacc->dma_ring_reset_quirk ? "enabled" : "disabled");
	dev_info(dev, "RA Proxy rev. %08x, num_proxies:%u\n",
		 readl(&ringacc->proxy_gcfg->revision), ringacc->num_proxies);

	return 0;
}

struct ringacc_match_data {
	struct k3_ringacc_ops ops;
};

static struct ringacc_match_data k3_ringacc_data = {
	.ops = {
		.init = k3_ringacc_init,
	},
};

/* Match table for of_platform binding */
static const struct of_device_id k3_ringacc_of_match[] = {
	{ .compatible = "ti,am654-navss-ringacc", .data = &k3_ringacc_data, },
	{},
};

static int k3_ringacc_probe(struct platform_device *pdev)
{
	const struct ringacc_match_data *match_data;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct k3_ringacc *ringacc;
	int ret;

	match = of_match_node(k3_ringacc_of_match, dev->of_node);
	if (!match)
		return -ENODEV;
	match_data = match->data;

	ringacc = devm_kzalloc(dev, sizeof(*ringacc), GFP_KERNEL);
	if (!ringacc)
		return -ENOMEM;

	ringacc->dev = dev;
	mutex_init(&ringacc->req_lock);
	ringacc->ops = &match_data->ops;

	ret = ringacc->ops->init(pdev, ringacc);
	if (ret)
		return ret;

	dev_set_drvdata(dev, ringacc);

	mutex_lock(&k3_ringacc_list_lock);
	list_add_tail(&ringacc->list, &k3_ringacc_list);
	mutex_unlock(&k3_ringacc_list_lock);

	return 0;
}

static struct platform_driver k3_ringacc_driver = {
	.probe		= k3_ringacc_probe,
	.driver		= {
		.name	= "k3-ringacc",
		.of_match_table = k3_ringacc_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(k3_ringacc_driver);
