/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright(c) 2004 - 2009 Intel Corporation. All rights reserved.
 */
#ifndef IOATDMA_H
#define IOATDMA_H

#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/dmapool.h>
#include <linux/cache.h>
#include <linux/pci_ids.h>
#include <linux/circ_buf.h>
#include <linux/interrupt.h>
#include "registers.h"
#include "hw.h"

#define IOAT_DMA_VERSION  "5.00"

#define IOAT_DMA_DCA_ANY_CPU		~0

#define to_ioatdma_device(dev) container_of(dev, struct ioatdma_device, dma_dev)
#define to_dev(ioat_chan) (&(ioat_chan)->ioat_dma->pdev->dev)
#define to_pdev(ioat_chan) ((ioat_chan)->ioat_dma->pdev)

#define chan_num(ch) ((int)((ch)->reg_base - (ch)->ioat_dma->reg_base) / 0x80)

/* ioat hardware assumes at least two sources for raid operations */
#define src_cnt_to_sw(x) ((x) + 2)
#define src_cnt_to_hw(x) ((x) - 2)
#define ndest_to_sw(x) ((x) + 1)
#define ndest_to_hw(x) ((x) - 1)
#define src16_cnt_to_sw(x) ((x) + 9)
#define src16_cnt_to_hw(x) ((x) - 9)

/*
 * workaround for IOAT ver.3.0 null descriptor issue
 * (channel returns error when size is 0)
 */
#define NULL_DESC_BUFFER_SIZE 1

enum ioat_irq_mode {
	IOAT_NOIRQ = 0,
	IOAT_MSIX,
	IOAT_MSI,
	IOAT_INTX
};

/**
 * struct ioatdma_device - internal representation of a IOAT device
 * @pdev: PCI-Express device
 * @reg_base: MMIO register space base address
 * @completion_pool: DMA buffers for completion ops
 * @sed_hw_pool: DMA super descriptor pools
 * @dma_dev: embedded struct dma_device
 * @version: version of ioatdma device
 * @msix_entries: irq handlers
 * @idx: per channel data
 * @dca: direct cache access context
 * @irq_mode: interrupt mode (INTX, MSI, MSIX)
 * @cap: read DMA capabilities register
 */
struct ioatdma_device {
	struct pci_dev *pdev;
	void __iomem *reg_base;
	struct dma_pool *completion_pool;
#define MAX_SED_POOLS	5
	struct dma_pool *sed_hw_pool[MAX_SED_POOLS];
	struct dma_device dma_dev;
	u8 version;
#define IOAT_MAX_CHANS 4
	struct msix_entry msix_entries[IOAT_MAX_CHANS];
	struct ioatdma_chan *idx[IOAT_MAX_CHANS];
	struct dca_provider *dca;
	enum ioat_irq_mode irq_mode;
	u32 cap;

	/* shadow version for CB3.3 chan reset errata workaround */
	u64 msixtba0;
	u64 msixdata0;
	u32 msixpba;
};

#define IOAT_MAX_ORDER 16
#define IOAT_MAX_DESCS (1 << IOAT_MAX_ORDER)
#define IOAT_CHUNK_SIZE (SZ_512K)
#define IOAT_DESCS_PER_CHUNK (IOAT_CHUNK_SIZE / IOAT_DESC_SZ)

struct ioat_descs {
	void *virt;
	dma_addr_t hw;
};

struct ioatdma_chan {
	struct dma_chan dma_chan;
	void __iomem *reg_base;
	dma_addr_t last_completion;
	spinlock_t cleanup_lock;
	unsigned long state;
	#define IOAT_CHAN_DOWN 0
	#define IOAT_COMPLETION_ACK 1
	#define IOAT_RESET_PENDING 2
	#define IOAT_KOBJ_INIT_FAIL 3
	#define IOAT_RUN 5
	#define IOAT_CHAN_ACTIVE 6
	struct timer_list timer;
	#define RESET_DELAY msecs_to_jiffies(100)
	struct ioatdma_device *ioat_dma;
	dma_addr_t completion_dma;
	u64 *completion;
	struct tasklet_struct cleanup_task;
	struct kobject kobj;

/* ioat v2 / v3 channel attributes
 * @xfercap_log; log2 of channel max transfer length (for fast division)
 * @head: allocated index
 * @issued: hardware notification point
 * @tail: cleanup index
 * @dmacount: identical to 'head' except for occasionally resetting to zero
 * @alloc_order: log2 of the number of allocated descriptors
 * @produce: number of descriptors to produce at submit time
 * @ring: software ring buffer implementation of hardware ring
 * @prep_lock: serializes descriptor preparation (producers)
 */
	size_t xfercap_log;
	u16 head;
	u16 issued;
	u16 tail;
	u16 dmacount;
	u16 alloc_order;
	u16 produce;
	struct ioat_ring_ent **ring;
	spinlock_t prep_lock;
	struct ioat_descs descs[IOAT_MAX_DESCS / IOAT_DESCS_PER_CHUNK];
	int desc_chunks;
	int intr_coalesce;
	int prev_intr_coalesce;
};

struct ioat_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct dma_chan *, char *);
	ssize_t (*store)(struct dma_chan *, const char *, size_t);
};

/**
 * struct ioat_sed_ent - wrapper around super extended hardware descriptor
 * @hw: hardware SED
 * @dma: dma address for the SED
 * @parent: point to the dma descriptor that's the parent
 * @hw_pool: descriptor pool index
 */
struct ioat_sed_ent {
	struct ioat_sed_raw_descriptor *hw;
	dma_addr_t dma;
	struct ioat_ring_ent *parent;
	unsigned int hw_pool;
};

/**
 * struct ioat_ring_ent - wrapper around hardware descriptor
 * @hw: hardware DMA descriptor (for memcpy)
 * @xor: hardware xor descriptor
 * @xor_ex: hardware xor extension descriptor
 * @pq: hardware pq descriptor
 * @pq_ex: hardware pq extension descriptor
 * @pqu: hardware pq update descriptor
 * @raw: hardware raw (un-typed) descriptor
 * @txd: the generic software descriptor for all engines
 * @len: total transaction length for unmap
 * @result: asynchronous result of validate operations
 * @id: identifier for debug
 * @sed: pointer to super extended descriptor sw desc
 */

struct ioat_ring_ent {
	union {
		struct ioat_dma_descriptor *hw;
		struct ioat_xor_descriptor *xor;
		struct ioat_xor_ext_descriptor *xor_ex;
		struct ioat_pq_descriptor *pq;
		struct ioat_pq_ext_descriptor *pq_ex;
		struct ioat_pq_update_descriptor *pqu;
		struct ioat_raw_descriptor *raw;
	};
	size_t len;
	struct dma_async_tx_descriptor txd;
	enum sum_check_flags *result;
	#ifdef DEBUG
	int id;
	#endif
	struct ioat_sed_ent *sed;
};

extern const struct sysfs_ops ioat_sysfs_ops;
extern struct ioat_sysfs_entry ioat_version_attr;
extern struct ioat_sysfs_entry ioat_cap_attr;
extern int ioat_pending_level;
extern int ioat_ring_alloc_order;
extern struct kobj_type ioat_ktype;
extern struct kmem_cache *ioat_cache;
extern int ioat_ring_max_alloc_order;
extern struct kmem_cache *ioat_sed_cache;

static inline struct ioatdma_chan *to_ioat_chan(struct dma_chan *c)
{
	return container_of(c, struct ioatdma_chan, dma_chan);
}

/* wrapper around hardware descriptor format + additional software fields */
#ifdef DEBUG
#define set_desc_id(desc, i) ((desc)->id = (i))
#define desc_id(desc) ((desc)->id)
#else
#define set_desc_id(desc, i)
#define desc_id(desc) (0)
#endif

static inline void
__dump_desc_dbg(struct ioatdma_chan *ioat_chan, struct ioat_dma_descriptor *hw,
		struct dma_async_tx_descriptor *tx, int id)
{
	struct device *dev = to_dev(ioat_chan);

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) cookie: %d flags: %#x"
		" ctl: %#10.8x (op: %#x int_en: %d compl: %d)\n", id,
		(unsigned long long) tx->phys,
		(unsigned long long) hw->next, tx->cookie, tx->flags,
		hw->ctl, hw->ctl_f.op, hw->ctl_f.int_en, hw->ctl_f.compl_write);
}

#define dump_desc_dbg(c, d) \
	({ if (d) __dump_desc_dbg(c, d->hw, &d->txd, desc_id(d)); 0; })

static inline struct ioatdma_chan *
ioat_chan_by_index(struct ioatdma_device *ioat_dma, int index)
{
	return ioat_dma->idx[index];
}

static inline u64 ioat_chansts(struct ioatdma_chan *ioat_chan)
{
	return readq(ioat_chan->reg_base + IOAT_CHANSTS_OFFSET);
}

static inline u64 ioat_chansts_to_addr(u64 status)
{
	return status & IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR;
}

static inline u32 ioat_chanerr(struct ioatdma_chan *ioat_chan)
{
	return readl(ioat_chan->reg_base + IOAT_CHANERR_OFFSET);
}

static inline void ioat_suspend(struct ioatdma_chan *ioat_chan)
{
	u8 ver = ioat_chan->ioat_dma->version;

	writeb(IOAT_CHANCMD_SUSPEND,
	       ioat_chan->reg_base + IOAT_CHANCMD_OFFSET(ver));
}

static inline void ioat_reset(struct ioatdma_chan *ioat_chan)
{
	u8 ver = ioat_chan->ioat_dma->version;

	writeb(IOAT_CHANCMD_RESET,
	       ioat_chan->reg_base + IOAT_CHANCMD_OFFSET(ver));
}

static inline bool ioat_reset_pending(struct ioatdma_chan *ioat_chan)
{
	u8 ver = ioat_chan->ioat_dma->version;
	u8 cmd;

	cmd = readb(ioat_chan->reg_base + IOAT_CHANCMD_OFFSET(ver));
	return (cmd & IOAT_CHANCMD_RESET) == IOAT_CHANCMD_RESET;
}

static inline bool is_ioat_active(unsigned long status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_ACTIVE);
}

static inline bool is_ioat_idle(unsigned long status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_DONE);
}

static inline bool is_ioat_halted(unsigned long status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_HALTED);
}

static inline bool is_ioat_suspended(unsigned long status)
{
	return ((status & IOAT_CHANSTS_STATUS) == IOAT_CHANSTS_SUSPENDED);
}

/* channel was fatally programmed */
static inline bool is_ioat_bug(unsigned long err)
{
	return !!err;
}


static inline u32 ioat_ring_size(struct ioatdma_chan *ioat_chan)
{
	return 1 << ioat_chan->alloc_order;
}

/* count of descriptors in flight with the engine */
static inline u16 ioat_ring_active(struct ioatdma_chan *ioat_chan)
{
	return CIRC_CNT(ioat_chan->head, ioat_chan->tail,
			ioat_ring_size(ioat_chan));
}

/* count of descriptors pending submission to hardware */
static inline u16 ioat_ring_pending(struct ioatdma_chan *ioat_chan)
{
	return CIRC_CNT(ioat_chan->head, ioat_chan->issued,
			ioat_ring_size(ioat_chan));
}

static inline u32 ioat_ring_space(struct ioatdma_chan *ioat_chan)
{
	return ioat_ring_size(ioat_chan) - ioat_ring_active(ioat_chan);
}

static inline u16
ioat_xferlen_to_descs(struct ioatdma_chan *ioat_chan, size_t len)
{
	u16 num_descs = len >> ioat_chan->xfercap_log;

	num_descs += !!(len & ((1 << ioat_chan->xfercap_log) - 1));
	return num_descs;
}

static inline struct ioat_ring_ent *
ioat_get_ring_ent(struct ioatdma_chan *ioat_chan, u16 idx)
{
	return ioat_chan->ring[idx & (ioat_ring_size(ioat_chan) - 1)];
}

static inline void
ioat_set_chainaddr(struct ioatdma_chan *ioat_chan, u64 addr)
{
	writel(addr & 0x00000000FFFFFFFF,
	       ioat_chan->reg_base + IOAT2_CHAINADDR_OFFSET_LOW);
	writel(addr >> 32,
	       ioat_chan->reg_base + IOAT2_CHAINADDR_OFFSET_HIGH);
}

/* IOAT Prep functions */
struct dma_async_tx_descriptor *
ioat_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_interrupt_lock(struct dma_chan *c, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
	       unsigned int src_cnt, size_t len, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_xor_val(struct dma_chan *chan, dma_addr_t *src,
		    unsigned int src_cnt, size_t len,
		    enum sum_check_flags *result, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_pq(struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
	      unsigned int src_cnt, const unsigned char *scf, size_t len,
	      unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_pq_val(struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		  unsigned int src_cnt, const unsigned char *scf, size_t len,
		  enum sum_check_flags *pqres, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_pqxor(struct dma_chan *chan, dma_addr_t dst, dma_addr_t *src,
		 unsigned int src_cnt, size_t len, unsigned long flags);
struct dma_async_tx_descriptor *
ioat_prep_pqxor_val(struct dma_chan *chan, dma_addr_t *src,
		     unsigned int src_cnt, size_t len,
		     enum sum_check_flags *result, unsigned long flags);

/* IOAT Operation functions */
irqreturn_t ioat_dma_do_interrupt(int irq, void *data);
irqreturn_t ioat_dma_do_interrupt_msix(int irq, void *data);
struct ioat_ring_ent **
ioat_alloc_ring(struct dma_chan *c, int order, gfp_t flags);
void ioat_start_null_desc(struct ioatdma_chan *ioat_chan);
void ioat_free_ring_ent(struct ioat_ring_ent *desc, struct dma_chan *chan);
int ioat_reset_hw(struct ioatdma_chan *ioat_chan);
enum dma_status
ioat_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate);
void ioat_cleanup_event(unsigned long data);
void ioat_timer_event(struct timer_list *t);
int ioat_check_space_lock(struct ioatdma_chan *ioat_chan, int num_descs);
void ioat_issue_pending(struct dma_chan *chan);

/* IOAT Init functions */
bool is_bwd_ioat(struct pci_dev *pdev);
struct dca_provider *ioat_dca_init(struct pci_dev *pdev, void __iomem *iobase);
void ioat_kobject_add(struct ioatdma_device *ioat_dma, struct kobj_type *type);
void ioat_kobject_del(struct ioatdma_device *ioat_dma);
int ioat_dma_setup_interrupts(struct ioatdma_device *ioat_dma);
void ioat_stop(struct ioatdma_chan *ioat_chan);
#endif /* IOATDMA_H */
