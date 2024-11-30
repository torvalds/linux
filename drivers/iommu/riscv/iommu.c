// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOMMU API for RISC-V IOMMU implementations.
 *
 * Copyright © 2022-2024 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 *
 * Authors
 *	Tomasz Jeznach <tjeznach@rivosinc.com>
 *	Nick Kossifidis <mick@ics.forth.gr>
 */

#define pr_fmt(fmt) "riscv-iommu: " fmt

#include <linux/compiler.h>
#include <linux/crash_dump.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "../iommu-pages.h"
#include "iommu-bits.h"
#include "iommu.h"

/* Timeouts in [us] */
#define RISCV_IOMMU_QCSR_TIMEOUT	150000
#define RISCV_IOMMU_QUEUE_TIMEOUT	150000
#define RISCV_IOMMU_DDTP_TIMEOUT	10000000
#define RISCV_IOMMU_IOTINVAL_TIMEOUT	90000000

/* Number of entries per CMD/FLT queue, should be <= INT_MAX */
#define RISCV_IOMMU_DEF_CQ_COUNT	8192
#define RISCV_IOMMU_DEF_FQ_COUNT	4096

/* RISC-V IOMMU PPN <> PHYS address conversions, PHYS <=> PPN[53:10] */
#define phys_to_ppn(pa)  (((pa) >> 2) & (((1ULL << 44) - 1) << 10))
#define ppn_to_phys(pn)	 (((pn) << 2) & (((1ULL << 44) - 1) << 12))

#define dev_to_iommu(dev) \
	iommu_get_iommu_dev(dev, struct riscv_iommu_device, iommu)

/* IOMMU PSCID allocation namespace. */
static DEFINE_IDA(riscv_iommu_pscids);
#define RISCV_IOMMU_MAX_PSCID		(BIT(20) - 1)

/* Device resource-managed allocations */
struct riscv_iommu_devres {
	void *addr;
	int order;
};

static void riscv_iommu_devres_pages_release(struct device *dev, void *res)
{
	struct riscv_iommu_devres *devres = res;

	iommu_free_pages(devres->addr, devres->order);
}

static int riscv_iommu_devres_pages_match(struct device *dev, void *res, void *p)
{
	struct riscv_iommu_devres *devres = res;
	struct riscv_iommu_devres *target = p;

	return devres->addr == target->addr;
}

static void *riscv_iommu_get_pages(struct riscv_iommu_device *iommu, int order)
{
	struct riscv_iommu_devres *devres;
	void *addr;

	addr = iommu_alloc_pages_node(dev_to_node(iommu->dev),
				      GFP_KERNEL_ACCOUNT, order);
	if (unlikely(!addr))
		return NULL;

	devres = devres_alloc(riscv_iommu_devres_pages_release,
			      sizeof(struct riscv_iommu_devres), GFP_KERNEL);

	if (unlikely(!devres)) {
		iommu_free_pages(addr, order);
		return NULL;
	}

	devres->addr = addr;
	devres->order = order;

	devres_add(iommu->dev, devres);

	return addr;
}

static void riscv_iommu_free_pages(struct riscv_iommu_device *iommu, void *addr)
{
	struct riscv_iommu_devres devres = { .addr = addr };

	devres_release(iommu->dev, riscv_iommu_devres_pages_release,
		       riscv_iommu_devres_pages_match, &devres);
}

/*
 * Hardware queue allocation and management.
 */

/* Setup queue base, control registers and default queue length */
#define RISCV_IOMMU_QUEUE_INIT(q, name) do {				\
	struct riscv_iommu_queue *_q = q;				\
	_q->qid = RISCV_IOMMU_INTR_ ## name;				\
	_q->qbr = RISCV_IOMMU_REG_ ## name ## B;			\
	_q->qcr = RISCV_IOMMU_REG_ ## name ## CSR;			\
	_q->mask = _q->mask ?: (RISCV_IOMMU_DEF_ ## name ## _COUNT) - 1;\
} while (0)

/* Note: offsets are the same for all queues */
#define Q_HEAD(q) ((q)->qbr + (RISCV_IOMMU_REG_CQH - RISCV_IOMMU_REG_CQB))
#define Q_TAIL(q) ((q)->qbr + (RISCV_IOMMU_REG_CQT - RISCV_IOMMU_REG_CQB))
#define Q_ITEM(q, index) ((q)->mask & (index))
#define Q_IPSR(q) BIT((q)->qid)

/*
 * Discover queue ring buffer hardware configuration, allocate in-memory
 * ring buffer or use fixed I/O memory location, configure queue base register.
 * Must be called before hardware queue is enabled.
 *
 * @queue - data structure, configured with RISCV_IOMMU_QUEUE_INIT()
 * @entry_size - queue single element size in bytes.
 */
static int riscv_iommu_queue_alloc(struct riscv_iommu_device *iommu,
				   struct riscv_iommu_queue *queue,
				   size_t entry_size)
{
	unsigned int logsz;
	u64 qb, rb;

	/*
	 * Use WARL base register property to discover maximum allowed
	 * number of entries and optional fixed IO address for queue location.
	 */
	riscv_iommu_writeq(iommu, queue->qbr, RISCV_IOMMU_QUEUE_LOG2SZ_FIELD);
	qb = riscv_iommu_readq(iommu, queue->qbr);

	/*
	 * Calculate and verify hardware supported queue length, as reported
	 * by the field LOG2SZ, where max queue length is equal to 2^(LOG2SZ + 1).
	 * Update queue size based on hardware supported value.
	 */
	logsz = ilog2(queue->mask);
	if (logsz > FIELD_GET(RISCV_IOMMU_QUEUE_LOG2SZ_FIELD, qb))
		logsz = FIELD_GET(RISCV_IOMMU_QUEUE_LOG2SZ_FIELD, qb);

	/*
	 * Use WARL base register property to discover an optional fixed IO
	 * address for queue ring buffer location. Otherwise allocate contiguous
	 * system memory.
	 */
	if (FIELD_GET(RISCV_IOMMU_PPN_FIELD, qb)) {
		const size_t queue_size = entry_size << (logsz + 1);

		queue->phys = pfn_to_phys(FIELD_GET(RISCV_IOMMU_PPN_FIELD, qb));
		queue->base = devm_ioremap(iommu->dev, queue->phys, queue_size);
	} else {
		do {
			const size_t queue_size = entry_size << (logsz + 1);
			const int order = get_order(queue_size);

			queue->base = riscv_iommu_get_pages(iommu, order);
			queue->phys = __pa(queue->base);
		} while (!queue->base && logsz-- > 0);
	}

	if (!queue->base)
		return -ENOMEM;

	qb = phys_to_ppn(queue->phys) |
	     FIELD_PREP(RISCV_IOMMU_QUEUE_LOG2SZ_FIELD, logsz);

	/* Update base register and read back to verify hw accepted our write */
	riscv_iommu_writeq(iommu, queue->qbr, qb);
	rb = riscv_iommu_readq(iommu, queue->qbr);
	if (rb != qb) {
		dev_err(iommu->dev, "queue #%u allocation failed\n", queue->qid);
		return -ENODEV;
	}

	/* Update actual queue mask */
	queue->mask = (2U << logsz) - 1;

	dev_dbg(iommu->dev, "queue #%u allocated 2^%u entries",
		queue->qid, logsz + 1);

	return 0;
}

/* Check interrupt queue status, IPSR */
static irqreturn_t riscv_iommu_queue_ipsr(int irq, void *data)
{
	struct riscv_iommu_queue *queue = (struct riscv_iommu_queue *)data;

	if (riscv_iommu_readl(queue->iommu, RISCV_IOMMU_REG_IPSR) & Q_IPSR(queue))
		return IRQ_WAKE_THREAD;

	return IRQ_NONE;
}

static int riscv_iommu_queue_vec(struct riscv_iommu_device *iommu, int n)
{
	/* Reuse ICVEC.CIV mask for all interrupt vectors mapping. */
	return (iommu->icvec >> (n * 4)) & RISCV_IOMMU_ICVEC_CIV;
}

/*
 * Enable queue processing in the hardware, register interrupt handler.
 *
 * @queue - data structure, already allocated with riscv_iommu_queue_alloc()
 * @irq_handler - threaded interrupt handler.
 */
static int riscv_iommu_queue_enable(struct riscv_iommu_device *iommu,
				    struct riscv_iommu_queue *queue,
				    irq_handler_t irq_handler)
{
	const unsigned int irq = iommu->irqs[riscv_iommu_queue_vec(iommu, queue->qid)];
	u32 csr;
	int rc;

	if (queue->iommu)
		return -EBUSY;

	/* Polling not implemented */
	if (!irq)
		return -ENODEV;

	queue->iommu = iommu;
	rc = request_threaded_irq(irq, riscv_iommu_queue_ipsr, irq_handler,
				  IRQF_ONESHOT | IRQF_SHARED,
				  dev_name(iommu->dev), queue);
	if (rc) {
		queue->iommu = NULL;
		return rc;
	}

	/*
	 * Enable queue with interrupts, clear any memory fault if any.
	 * Wait for the hardware to acknowledge request and activate queue
	 * processing.
	 * Note: All CSR bitfields are in the same offsets for all queues.
	 */
	riscv_iommu_writel(iommu, queue->qcr,
			   RISCV_IOMMU_QUEUE_ENABLE |
			   RISCV_IOMMU_QUEUE_INTR_ENABLE |
			   RISCV_IOMMU_QUEUE_MEM_FAULT);

	riscv_iommu_readl_timeout(iommu, queue->qcr,
				  csr, !(csr & RISCV_IOMMU_QUEUE_BUSY),
				  10, RISCV_IOMMU_QCSR_TIMEOUT);

	if (RISCV_IOMMU_QUEUE_ACTIVE != (csr & (RISCV_IOMMU_QUEUE_ACTIVE |
						RISCV_IOMMU_QUEUE_BUSY |
						RISCV_IOMMU_QUEUE_MEM_FAULT))) {
		/* Best effort to stop and disable failing hardware queue. */
		riscv_iommu_writel(iommu, queue->qcr, 0);
		free_irq(irq, queue);
		queue->iommu = NULL;
		dev_err(iommu->dev, "queue #%u failed to start\n", queue->qid);
		return -EBUSY;
	}

	/* Clear any pending interrupt flag. */
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_IPSR, Q_IPSR(queue));

	return 0;
}

/*
 * Disable queue. Wait for the hardware to acknowledge request and
 * stop processing enqueued requests. Report errors but continue.
 */
static void riscv_iommu_queue_disable(struct riscv_iommu_queue *queue)
{
	struct riscv_iommu_device *iommu = queue->iommu;
	u32 csr;

	if (!iommu)
		return;

	free_irq(iommu->irqs[riscv_iommu_queue_vec(iommu, queue->qid)], queue);
	riscv_iommu_writel(iommu, queue->qcr, 0);
	riscv_iommu_readl_timeout(iommu, queue->qcr,
				  csr, !(csr & RISCV_IOMMU_QUEUE_BUSY),
				  10, RISCV_IOMMU_QCSR_TIMEOUT);

	if (csr & (RISCV_IOMMU_QUEUE_ACTIVE | RISCV_IOMMU_QUEUE_BUSY))
		dev_err(iommu->dev, "fail to disable hardware queue #%u, csr 0x%x\n",
			queue->qid, csr);

	queue->iommu = NULL;
}

/*
 * Returns number of available valid queue entries and the first item index.
 * Update shadow producer index if necessary.
 */
static int riscv_iommu_queue_consume(struct riscv_iommu_queue *queue,
				     unsigned int *index)
{
	unsigned int head = atomic_read(&queue->head);
	unsigned int tail = atomic_read(&queue->tail);
	unsigned int last = Q_ITEM(queue, tail);
	int available = (int)(tail - head);

	*index = head;

	if (available > 0)
		return available;

	/* read hardware producer index, check reserved register bits are not set. */
	if (riscv_iommu_readl_timeout(queue->iommu, Q_TAIL(queue),
				      tail, (tail & ~queue->mask) == 0,
				      0, RISCV_IOMMU_QUEUE_TIMEOUT)) {
		dev_err_once(queue->iommu->dev,
			     "Hardware error: queue access timeout\n");
		return 0;
	}

	if (tail == last)
		return 0;

	/* update shadow producer index */
	return (int)(atomic_add_return((tail - last) & queue->mask, &queue->tail) - head);
}

/*
 * Release processed queue entries, should match riscv_iommu_queue_consume() calls.
 */
static void riscv_iommu_queue_release(struct riscv_iommu_queue *queue, int count)
{
	const unsigned int head = atomic_add_return(count, &queue->head);

	riscv_iommu_writel(queue->iommu, Q_HEAD(queue), Q_ITEM(queue, head));
}

/* Return actual consumer index based on hardware reported queue head index. */
static unsigned int riscv_iommu_queue_cons(struct riscv_iommu_queue *queue)
{
	const unsigned int cons = atomic_read(&queue->head);
	const unsigned int last = Q_ITEM(queue, cons);
	unsigned int head;

	if (riscv_iommu_readl_timeout(queue->iommu, Q_HEAD(queue), head,
				      !(head & ~queue->mask),
				      0, RISCV_IOMMU_QUEUE_TIMEOUT))
		return cons;

	return cons + ((head - last) & queue->mask);
}

/* Wait for submitted item to be processed. */
static int riscv_iommu_queue_wait(struct riscv_iommu_queue *queue,
				  unsigned int index,
				  unsigned int timeout_us)
{
	unsigned int cons = atomic_read(&queue->head);

	/* Already processed by the consumer */
	if ((int)(cons - index) > 0)
		return 0;

	/* Monitor consumer index */
	return readx_poll_timeout(riscv_iommu_queue_cons, queue, cons,
				 (int)(cons - index) > 0, 0, timeout_us);
}

/* Enqueue an entry and wait to be processed if timeout_us > 0
 *
 * Error handling for IOMMU hardware not responding in reasonable time
 * will be added as separate patch series along with other RAS features.
 * For now, only report hardware failure and continue.
 */
static unsigned int riscv_iommu_queue_send(struct riscv_iommu_queue *queue,
					   void *entry, size_t entry_size)
{
	unsigned int prod;
	unsigned int head;
	unsigned int tail;
	unsigned long flags;

	/* Do not preempt submission flow. */
	local_irq_save(flags);

	/* 1. Allocate some space in the queue */
	prod = atomic_inc_return(&queue->prod) - 1;
	head = atomic_read(&queue->head);

	/* 2. Wait for space availability. */
	if ((prod - head) > queue->mask) {
		if (readx_poll_timeout(atomic_read, &queue->head,
				       head, (prod - head) < queue->mask,
				       0, RISCV_IOMMU_QUEUE_TIMEOUT))
			goto err_busy;
	} else if ((prod - head) == queue->mask) {
		const unsigned int last = Q_ITEM(queue, head);

		if (riscv_iommu_readl_timeout(queue->iommu, Q_HEAD(queue), head,
					      !(head & ~queue->mask) && head != last,
					      0, RISCV_IOMMU_QUEUE_TIMEOUT))
			goto err_busy;
		atomic_add((head - last) & queue->mask, &queue->head);
	}

	/* 3. Store entry in the ring buffer */
	memcpy(queue->base + Q_ITEM(queue, prod) * entry_size, entry, entry_size);

	/* 4. Wait for all previous entries to be ready */
	if (readx_poll_timeout(atomic_read, &queue->tail, tail, prod == tail,
			       0, RISCV_IOMMU_QUEUE_TIMEOUT))
		goto err_busy;

	/*
	 * 5. Make sure the ring buffer update (whether in normal or I/O memory) is
	 *    completed and visible before signaling the tail doorbell to fetch
	 *    the next command. 'fence ow, ow'
	 */
	dma_wmb();
	riscv_iommu_writel(queue->iommu, Q_TAIL(queue), Q_ITEM(queue, prod + 1));

	/*
	 * 6. Make sure the doorbell write to the device has finished before updating
	 *    the shadow tail index in normal memory. 'fence o, w'
	 */
	mmiowb();
	atomic_inc(&queue->tail);

	/* 7. Complete submission and restore local interrupts */
	local_irq_restore(flags);

	return prod;

err_busy:
	local_irq_restore(flags);
	dev_err_once(queue->iommu->dev, "Hardware error: command enqueue failed\n");

	return prod;
}

/*
 * IOMMU Command queue chapter 3.1
 */

/* Command queue interrupt handler thread function */
static irqreturn_t riscv_iommu_cmdq_process(int irq, void *data)
{
	const struct riscv_iommu_queue *queue = (struct riscv_iommu_queue *)data;
	unsigned int ctrl;

	/* Clear MF/CQ errors, complete error recovery to be implemented. */
	ctrl = riscv_iommu_readl(queue->iommu, queue->qcr);
	if (ctrl & (RISCV_IOMMU_CQCSR_CQMF | RISCV_IOMMU_CQCSR_CMD_TO |
		    RISCV_IOMMU_CQCSR_CMD_ILL | RISCV_IOMMU_CQCSR_FENCE_W_IP)) {
		riscv_iommu_writel(queue->iommu, queue->qcr, ctrl);
		dev_warn(queue->iommu->dev,
			 "Queue #%u error; fault:%d timeout:%d illegal:%d fence_w_ip:%d\n",
			 queue->qid,
			 !!(ctrl & RISCV_IOMMU_CQCSR_CQMF),
			 !!(ctrl & RISCV_IOMMU_CQCSR_CMD_TO),
			 !!(ctrl & RISCV_IOMMU_CQCSR_CMD_ILL),
			 !!(ctrl & RISCV_IOMMU_CQCSR_FENCE_W_IP));
	}

	/* Placeholder for command queue interrupt notifiers */

	/* Clear command interrupt pending. */
	riscv_iommu_writel(queue->iommu, RISCV_IOMMU_REG_IPSR, Q_IPSR(queue));

	return IRQ_HANDLED;
}

/* Send command to the IOMMU command queue */
static void riscv_iommu_cmd_send(struct riscv_iommu_device *iommu,
				 struct riscv_iommu_command *cmd)
{
	riscv_iommu_queue_send(&iommu->cmdq, cmd, sizeof(*cmd));
}

/* Send IOFENCE.C command and wait for all scheduled commands to complete. */
static void riscv_iommu_cmd_sync(struct riscv_iommu_device *iommu,
				 unsigned int timeout_us)
{
	struct riscv_iommu_command cmd;
	unsigned int prod;

	riscv_iommu_cmd_iofence(&cmd);
	prod = riscv_iommu_queue_send(&iommu->cmdq, &cmd, sizeof(cmd));

	if (!timeout_us)
		return;

	if (riscv_iommu_queue_wait(&iommu->cmdq, prod, timeout_us))
		dev_err_once(iommu->dev,
			     "Hardware error: command execution timeout\n");
}

/*
 * IOMMU Fault/Event queue chapter 3.2
 */

static void riscv_iommu_fault(struct riscv_iommu_device *iommu,
			      struct riscv_iommu_fq_record *event)
{
	unsigned int err = FIELD_GET(RISCV_IOMMU_FQ_HDR_CAUSE, event->hdr);
	unsigned int devid = FIELD_GET(RISCV_IOMMU_FQ_HDR_DID, event->hdr);

	/* Placeholder for future fault handling implementation, report only. */
	if (err)
		dev_warn_ratelimited(iommu->dev,
				     "Fault %d devid: 0x%x iotval: %llx iotval2: %llx\n",
				     err, devid, event->iotval, event->iotval2);
}

/* Fault queue interrupt handler thread function */
static irqreturn_t riscv_iommu_fltq_process(int irq, void *data)
{
	struct riscv_iommu_queue *queue = (struct riscv_iommu_queue *)data;
	struct riscv_iommu_device *iommu = queue->iommu;
	struct riscv_iommu_fq_record *events;
	unsigned int ctrl, idx;
	int cnt, len;

	events = (struct riscv_iommu_fq_record *)queue->base;

	/* Clear fault interrupt pending and process all received fault events. */
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_IPSR, Q_IPSR(queue));

	do {
		cnt = riscv_iommu_queue_consume(queue, &idx);
		for (len = 0; len < cnt; idx++, len++)
			riscv_iommu_fault(iommu, &events[Q_ITEM(queue, idx)]);
		riscv_iommu_queue_release(queue, cnt);
	} while (cnt > 0);

	/* Clear MF/OF errors, complete error recovery to be implemented. */
	ctrl = riscv_iommu_readl(iommu, queue->qcr);
	if (ctrl & (RISCV_IOMMU_FQCSR_FQMF | RISCV_IOMMU_FQCSR_FQOF)) {
		riscv_iommu_writel(iommu, queue->qcr, ctrl);
		dev_warn(iommu->dev,
			 "Queue #%u error; memory fault:%d overflow:%d\n",
			 queue->qid,
			 !!(ctrl & RISCV_IOMMU_FQCSR_FQMF),
			 !!(ctrl & RISCV_IOMMU_FQCSR_FQOF));
	}

	return IRQ_HANDLED;
}

/* Lookup and initialize device context info structure. */
static struct riscv_iommu_dc *riscv_iommu_get_dc(struct riscv_iommu_device *iommu,
						 unsigned int devid)
{
	const bool base_format = !(iommu->caps & RISCV_IOMMU_CAPABILITIES_MSI_FLAT);
	unsigned int depth;
	unsigned long ddt, old, new;
	void *ptr;
	u8 ddi_bits[3] = { 0 };
	u64 *ddtp = NULL;

	/* Make sure the mode is valid */
	if (iommu->ddt_mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL ||
	    iommu->ddt_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_3LVL)
		return NULL;

	/*
	 * Device id partitioning for base format:
	 * DDI[0]: bits 0 - 6   (1st level) (7 bits)
	 * DDI[1]: bits 7 - 15  (2nd level) (9 bits)
	 * DDI[2]: bits 16 - 23 (3rd level) (8 bits)
	 *
	 * For extended format:
	 * DDI[0]: bits 0 - 5   (1st level) (6 bits)
	 * DDI[1]: bits 6 - 14  (2nd level) (9 bits)
	 * DDI[2]: bits 15 - 23 (3rd level) (9 bits)
	 */
	if (base_format) {
		ddi_bits[0] = 7;
		ddi_bits[1] = 7 + 9;
		ddi_bits[2] = 7 + 9 + 8;
	} else {
		ddi_bits[0] = 6;
		ddi_bits[1] = 6 + 9;
		ddi_bits[2] = 6 + 9 + 9;
	}

	/* Make sure device id is within range */
	depth = iommu->ddt_mode - RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL;
	if (devid >= (1 << ddi_bits[depth]))
		return NULL;

	/* Get to the level of the non-leaf node that holds the device context */
	for (ddtp = iommu->ddt_root; depth-- > 0;) {
		const int split = ddi_bits[depth];
		/*
		 * Each non-leaf node is 64bits wide and on each level
		 * nodes are indexed by DDI[depth].
		 */
		ddtp += (devid >> split) & 0x1FF;

		/*
		 * Check if this node has been populated and if not
		 * allocate a new level and populate it.
		 */
		do {
			ddt = READ_ONCE(*(unsigned long *)ddtp);
			if (ddt & RISCV_IOMMU_DDTE_V) {
				ddtp = __va(ppn_to_phys(ddt));
				break;
			}

			ptr = riscv_iommu_get_pages(iommu, 0);
			if (!ptr)
				return NULL;

			new = phys_to_ppn(__pa(ptr)) | RISCV_IOMMU_DDTE_V;
			old = cmpxchg_relaxed((unsigned long *)ddtp, ddt, new);

			if (old == ddt) {
				ddtp = (u64 *)ptr;
				break;
			}

			/* Race setting DDT detected, re-read and retry. */
			riscv_iommu_free_pages(iommu, ptr);
		} while (1);
	}

	/*
	 * Grab the node that matches DDI[depth], note that when using base
	 * format the device context is 4 * 64bits, and the extended format
	 * is 8 * 64bits, hence the (3 - base_format) below.
	 */
	ddtp += (devid & ((64 << base_format) - 1)) << (3 - base_format);

	return (struct riscv_iommu_dc *)ddtp;
}

/*
 * This is best effort IOMMU translation shutdown flow.
 * Disable IOMMU without waiting for hardware response.
 */
static void riscv_iommu_disable(struct riscv_iommu_device *iommu)
{
	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_CQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FQCSR, 0);
	riscv_iommu_writel(iommu, RISCV_IOMMU_REG_PQCSR, 0);
}

#define riscv_iommu_read_ddtp(iommu) ({ \
	u64 ddtp; \
	riscv_iommu_readq_timeout((iommu), RISCV_IOMMU_REG_DDTP, ddtp, \
				  !(ddtp & RISCV_IOMMU_DDTP_BUSY), 10, \
				  RISCV_IOMMU_DDTP_TIMEOUT); \
	ddtp; })

static int riscv_iommu_iodir_alloc(struct riscv_iommu_device *iommu)
{
	u64 ddtp;
	unsigned int mode;

	ddtp = riscv_iommu_read_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	/*
	 * It is optional for the hardware to report a fixed address for device
	 * directory root page when DDT.MODE is OFF or BARE.
	 */
	mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);
	if (mode == RISCV_IOMMU_DDTP_IOMMU_MODE_BARE ||
	    mode == RISCV_IOMMU_DDTP_IOMMU_MODE_OFF) {
		/* Use WARL to discover hardware fixed DDT PPN */
		riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP,
				   FIELD_PREP(RISCV_IOMMU_DDTP_IOMMU_MODE, mode));
		ddtp = riscv_iommu_read_ddtp(iommu);
		if (ddtp & RISCV_IOMMU_DDTP_BUSY)
			return -EBUSY;

		iommu->ddt_phys = ppn_to_phys(ddtp);
		if (iommu->ddt_phys)
			iommu->ddt_root = devm_ioremap(iommu->dev,
						       iommu->ddt_phys, PAGE_SIZE);
		if (iommu->ddt_root)
			memset(iommu->ddt_root, 0, PAGE_SIZE);
	}

	if (!iommu->ddt_root) {
		iommu->ddt_root = riscv_iommu_get_pages(iommu, 0);
		iommu->ddt_phys = __pa(iommu->ddt_root);
	}

	if (!iommu->ddt_root)
		return -ENOMEM;

	return 0;
}

/*
 * Discover supported DDT modes starting from requested value,
 * configure DDTP register with accepted mode and root DDT address.
 * Accepted iommu->ddt_mode is updated on success.
 */
static int riscv_iommu_iodir_set_mode(struct riscv_iommu_device *iommu,
				      unsigned int ddtp_mode)
{
	struct device *dev = iommu->dev;
	u64 ddtp, rq_ddtp;
	unsigned int mode, rq_mode = ddtp_mode;
	struct riscv_iommu_command cmd;

	ddtp = riscv_iommu_read_ddtp(iommu);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	/* Disallow state transition from xLVL to xLVL. */
	mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);
	if (mode != RISCV_IOMMU_DDTP_IOMMU_MODE_BARE &&
	    mode != RISCV_IOMMU_DDTP_IOMMU_MODE_OFF &&
	    rq_mode != RISCV_IOMMU_DDTP_IOMMU_MODE_BARE &&
	    rq_mode != RISCV_IOMMU_DDTP_IOMMU_MODE_OFF)
		return -EINVAL;

	do {
		rq_ddtp = FIELD_PREP(RISCV_IOMMU_DDTP_IOMMU_MODE, rq_mode);
		if (rq_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_BARE)
			rq_ddtp |= phys_to_ppn(iommu->ddt_phys);

		riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_DDTP, rq_ddtp);
		ddtp = riscv_iommu_read_ddtp(iommu);
		if (ddtp & RISCV_IOMMU_DDTP_BUSY) {
			dev_err(dev, "timeout when setting ddtp (ddt mode: %u, read: %llx)\n",
				rq_mode, ddtp);
			return -EBUSY;
		}

		/* Verify IOMMU hardware accepts new DDTP config. */
		mode = FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp);

		if (rq_mode == mode)
			break;

		/* Hardware mandatory DDTP mode has not been accepted. */
		if (rq_mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL && rq_ddtp != ddtp) {
			dev_err(dev, "DDTP update failed hw: %llx vs %llx\n",
				ddtp, rq_ddtp);
			return -EINVAL;
		}

		/*
		 * Mode field is WARL, an IOMMU may support a subset of
		 * directory table levels in which case if we tried to set
		 * an unsupported number of levels we'll readback either
		 * a valid xLVL or off/bare. If we got off/bare, try again
		 * with a smaller xLVL.
		 */
		if (mode < RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL &&
		    rq_mode > RISCV_IOMMU_DDTP_IOMMU_MODE_1LVL) {
			dev_dbg(dev, "DDTP hw mode %u vs %u\n", mode, rq_mode);
			rq_mode--;
			continue;
		}

		/*
		 * We tried all supported modes and IOMMU hardware failed to
		 * accept new settings, something went very wrong since off/bare
		 * and at least one xLVL must be supported.
		 */
		dev_err(dev, "DDTP hw mode %u, failed to set %u\n",
			mode, ddtp_mode);
		return -EINVAL;
	} while (1);

	iommu->ddt_mode = mode;
	if (mode != ddtp_mode)
		dev_dbg(dev, "DDTP hw mode %u, requested %u\n", mode, ddtp_mode);

	/* Invalidate device context cache */
	riscv_iommu_cmd_iodir_inval_ddt(&cmd);
	riscv_iommu_cmd_send(iommu, &cmd);

	/* Invalidate address translation cache */
	riscv_iommu_cmd_inval_vma(&cmd);
	riscv_iommu_cmd_send(iommu, &cmd);

	/* IOFENCE.C */
	riscv_iommu_cmd_sync(iommu, RISCV_IOMMU_IOTINVAL_TIMEOUT);

	return 0;
}

/* This struct contains protection domain specific IOMMU driver data. */
struct riscv_iommu_domain {
	struct iommu_domain domain;
	struct list_head bonds;
	spinlock_t lock;		/* protect bonds list updates. */
	int pscid;
	bool amo_enabled;
	int numa_node;
	unsigned int pgd_mode;
	unsigned long *pgd_root;
};

#define iommu_domain_to_riscv(iommu_domain) \
	container_of(iommu_domain, struct riscv_iommu_domain, domain)

/* Private IOMMU data for managed devices, dev_iommu_priv_* */
struct riscv_iommu_info {
	struct riscv_iommu_domain *domain;
};

/*
 * Linkage between an iommu_domain and attached devices.
 *
 * Protection domain requiring IOATC and DevATC translation cache invalidations,
 * should be linked to attached devices using a riscv_iommu_bond structure.
 * Devices should be linked to the domain before first use and unlinked after
 * the translations from the referenced protection domain can no longer be used.
 * Blocking and identity domains are not tracked here, as the IOMMU hardware
 * does not cache negative and/or identity (BARE mode) translations, and DevATC
 * is disabled for those protection domains.
 *
 * The device pointer and IOMMU data remain stable in the bond struct after
 * _probe_device() where it's attached to the managed IOMMU, up to the
 * completion of the _release_device() call. The release of the bond structure
 * is synchronized with the device release.
 */
struct riscv_iommu_bond {
	struct list_head list;
	struct rcu_head rcu;
	struct device *dev;
};

static int riscv_iommu_bond_link(struct riscv_iommu_domain *domain,
				 struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);
	struct riscv_iommu_bond *bond;
	struct list_head *bonds;

	bond = kzalloc(sizeof(*bond), GFP_KERNEL);
	if (!bond)
		return -ENOMEM;
	bond->dev = dev;

	/*
	 * List of devices attached to the domain is arranged based on
	 * managed IOMMU device.
	 */

	spin_lock(&domain->lock);
	list_for_each(bonds, &domain->bonds)
		if (dev_to_iommu(list_entry(bonds, struct riscv_iommu_bond, list)->dev) == iommu)
			break;
	list_add_rcu(&bond->list, bonds);
	spin_unlock(&domain->lock);

	/* Synchronize with riscv_iommu_iotlb_inval() sequence. See comment below. */
	smp_mb();

	return 0;
}

static void riscv_iommu_bond_unlink(struct riscv_iommu_domain *domain,
				    struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);
	struct riscv_iommu_bond *bond, *found = NULL;
	struct riscv_iommu_command cmd;
	int count = 0;

	if (!domain)
		return;

	spin_lock(&domain->lock);
	list_for_each_entry(bond, &domain->bonds, list) {
		if (found && count)
			break;
		else if (bond->dev == dev)
			found = bond;
		else if (dev_to_iommu(bond->dev) == iommu)
			count++;
	}
	if (found)
		list_del_rcu(&found->list);
	spin_unlock(&domain->lock);
	kfree_rcu(found, rcu);

	/*
	 * If this was the last bond between this domain and the IOMMU
	 * invalidate all cached entries for domain's PSCID.
	 */
	if (!count) {
		riscv_iommu_cmd_inval_vma(&cmd);
		riscv_iommu_cmd_inval_set_pscid(&cmd, domain->pscid);
		riscv_iommu_cmd_send(iommu, &cmd);

		riscv_iommu_cmd_sync(iommu, RISCV_IOMMU_IOTINVAL_TIMEOUT);
	}
}

/*
 * Send IOTLB.INVAL for whole address space for ranges larger than 2MB.
 * This limit will be replaced with range invalidations, if supported by
 * the hardware, when RISC-V IOMMU architecture specification update for
 * range invalidations update will be available.
 */
#define RISCV_IOMMU_IOTLB_INVAL_LIMIT	(2 << 20)

static void riscv_iommu_iotlb_inval(struct riscv_iommu_domain *domain,
				    unsigned long start, unsigned long end)
{
	struct riscv_iommu_bond *bond;
	struct riscv_iommu_device *iommu, *prev;
	struct riscv_iommu_command cmd;
	unsigned long len = end - start + 1;
	unsigned long iova;

	/*
	 * For each IOMMU linked with this protection domain (via bonds->dev),
	 * an IOTLB invaliation command will be submitted and executed.
	 *
	 * Possbile race with domain attach flow is handled by sequencing
	 * bond creation - riscv_iommu_bond_link(), and device directory
	 * update - riscv_iommu_iodir_update().
	 *
	 * PTE Update / IOTLB Inval           Device attach & directory update
	 * --------------------------         --------------------------
	 * update page table entries          add dev to the bond list
	 * FENCE RW,RW                        FENCE RW,RW
	 * For all IOMMUs: (can be empty)     Update FSC/PSCID
	 *   FENCE IOW,IOW                      FENCE IOW,IOW
	 *   IOTLB.INVAL                        IODIR.INVAL
	 *   IOFENCE.C
	 *
	 * If bond list is not updated with new device, directory context will
	 * be configured with already valid page table content. If an IOMMU is
	 * linked to the protection domain it will receive invalidation
	 * requests for updated page table entries.
	 */
	smp_mb();

	rcu_read_lock();

	prev = NULL;
	list_for_each_entry_rcu(bond, &domain->bonds, list) {
		iommu = dev_to_iommu(bond->dev);

		/*
		 * IOTLB invalidation request can be safely omitted if already sent
		 * to the IOMMU for the same PSCID, and with domain->bonds list
		 * arranged based on the device's IOMMU, it's sufficient to check
		 * last device the invalidation was sent to.
		 */
		if (iommu == prev)
			continue;

		riscv_iommu_cmd_inval_vma(&cmd);
		riscv_iommu_cmd_inval_set_pscid(&cmd, domain->pscid);
		if (len && len < RISCV_IOMMU_IOTLB_INVAL_LIMIT) {
			for (iova = start; iova < end; iova += PAGE_SIZE) {
				riscv_iommu_cmd_inval_set_addr(&cmd, iova);
				riscv_iommu_cmd_send(iommu, &cmd);
			}
		} else {
			riscv_iommu_cmd_send(iommu, &cmd);
		}
		prev = iommu;
	}

	prev = NULL;
	list_for_each_entry_rcu(bond, &domain->bonds, list) {
		iommu = dev_to_iommu(bond->dev);
		if (iommu == prev)
			continue;

		riscv_iommu_cmd_sync(iommu, RISCV_IOMMU_IOTINVAL_TIMEOUT);
		prev = iommu;
	}
	rcu_read_unlock();
}

#define RISCV_IOMMU_FSC_BARE 0

/*
 * Update IODIR for the device.
 *
 * During the execution of riscv_iommu_probe_device(), IODIR entries are
 * allocated for the device's identifiers.  Device context invalidation
 * becomes necessary only if one of the updated entries was previously
 * marked as valid, given that invalid device context entries are not
 * cached by the IOMMU hardware.
 * In this implementation, updating a valid device context while the
 * device is not quiesced might be disruptive, potentially causing
 * interim translation faults.
 */
static void riscv_iommu_iodir_update(struct riscv_iommu_device *iommu,
				     struct device *dev, u64 fsc, u64 ta)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct riscv_iommu_dc *dc;
	struct riscv_iommu_command cmd;
	bool sync_required = false;
	u64 tc;
	int i;

	for (i = 0; i < fwspec->num_ids; i++) {
		dc = riscv_iommu_get_dc(iommu, fwspec->ids[i]);
		tc = READ_ONCE(dc->tc);
		if (!(tc & RISCV_IOMMU_DC_TC_V))
			continue;

		WRITE_ONCE(dc->tc, tc & ~RISCV_IOMMU_DC_TC_V);

		/* Invalidate device context cached values */
		riscv_iommu_cmd_iodir_inval_ddt(&cmd);
		riscv_iommu_cmd_iodir_set_did(&cmd, fwspec->ids[i]);
		riscv_iommu_cmd_send(iommu, &cmd);
		sync_required = true;
	}

	if (sync_required)
		riscv_iommu_cmd_sync(iommu, RISCV_IOMMU_IOTINVAL_TIMEOUT);

	/*
	 * For device context with DC_TC_PDTV = 0, translation attributes valid bit
	 * is stored as DC_TC_V bit (both sharing the same location at BIT(0)).
	 */
	for (i = 0; i < fwspec->num_ids; i++) {
		dc = riscv_iommu_get_dc(iommu, fwspec->ids[i]);
		tc = READ_ONCE(dc->tc);
		tc |= ta & RISCV_IOMMU_DC_TC_V;

		WRITE_ONCE(dc->fsc, fsc);
		WRITE_ONCE(dc->ta, ta & RISCV_IOMMU_PC_TA_PSCID);
		/* Update device context, write TC.V as the last step. */
		dma_wmb();
		WRITE_ONCE(dc->tc, tc);

		/* Invalidate device context after update */
		riscv_iommu_cmd_iodir_inval_ddt(&cmd);
		riscv_iommu_cmd_iodir_set_did(&cmd, fwspec->ids[i]);
		riscv_iommu_cmd_send(iommu, &cmd);
	}

	riscv_iommu_cmd_sync(iommu, RISCV_IOMMU_IOTINVAL_TIMEOUT);
}

/*
 * IOVA page translation tree management.
 */

static void riscv_iommu_iotlb_flush_all(struct iommu_domain *iommu_domain)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);

	riscv_iommu_iotlb_inval(domain, 0, ULONG_MAX);
}

static void riscv_iommu_iotlb_sync(struct iommu_domain *iommu_domain,
				   struct iommu_iotlb_gather *gather)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);

	riscv_iommu_iotlb_inval(domain, gather->start, gather->end);
}

#define PT_SHIFT (PAGE_SHIFT - ilog2(sizeof(pte_t)))

#define _io_pte_present(pte)	((pte) & (_PAGE_PRESENT | _PAGE_PROT_NONE))
#define _io_pte_leaf(pte)	((pte) & _PAGE_LEAF)
#define _io_pte_none(pte)	((pte) == 0)
#define _io_pte_entry(pn, prot)	((_PAGE_PFN_MASK & ((pn) << _PAGE_PFN_SHIFT)) | (prot))

static void riscv_iommu_pte_free(struct riscv_iommu_domain *domain,
				 unsigned long pte, struct list_head *freelist)
{
	unsigned long *ptr;
	int i;

	if (!_io_pte_present(pte) || _io_pte_leaf(pte))
		return;

	ptr = (unsigned long *)pfn_to_virt(__page_val_to_pfn(pte));

	/* Recursively free all sub page table pages */
	for (i = 0; i < PTRS_PER_PTE; i++) {
		pte = READ_ONCE(ptr[i]);
		if (!_io_pte_none(pte) && cmpxchg_relaxed(ptr + i, pte, 0) == pte)
			riscv_iommu_pte_free(domain, pte, freelist);
	}

	if (freelist)
		list_add_tail(&virt_to_page(ptr)->lru, freelist);
	else
		iommu_free_page(ptr);
}

static unsigned long *riscv_iommu_pte_alloc(struct riscv_iommu_domain *domain,
					    unsigned long iova, size_t pgsize,
					    gfp_t gfp)
{
	unsigned long *ptr = domain->pgd_root;
	unsigned long pte, old;
	int level = domain->pgd_mode - RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39 + 2;
	void *addr;

	do {
		const int shift = PAGE_SHIFT + PT_SHIFT * level;

		ptr += ((iova >> shift) & (PTRS_PER_PTE - 1));
		/*
		 * Note: returned entry might be a non-leaf if there was
		 * existing mapping with smaller granularity. Up to the caller
		 * to replace and invalidate.
		 */
		if (((size_t)1 << shift) == pgsize)
			return ptr;
pte_retry:
		pte = READ_ONCE(*ptr);
		/*
		 * This is very likely incorrect as we should not be adding
		 * new mapping with smaller granularity on top
		 * of existing 2M/1G mapping. Fail.
		 */
		if (_io_pte_present(pte) && _io_pte_leaf(pte))
			return NULL;
		/*
		 * Non-leaf entry is missing, allocate and try to add to the
		 * page table. This might race with other mappings, retry.
		 */
		if (_io_pte_none(pte)) {
			addr = iommu_alloc_page_node(domain->numa_node, gfp);
			if (!addr)
				return NULL;
			old = pte;
			pte = _io_pte_entry(virt_to_pfn(addr), _PAGE_TABLE);
			if (cmpxchg_relaxed(ptr, old, pte) != old) {
				iommu_free_page(addr);
				goto pte_retry;
			}
		}
		ptr = (unsigned long *)pfn_to_virt(__page_val_to_pfn(pte));
	} while (level-- > 0);

	return NULL;
}

static unsigned long *riscv_iommu_pte_fetch(struct riscv_iommu_domain *domain,
					    unsigned long iova, size_t *pte_pgsize)
{
	unsigned long *ptr = domain->pgd_root;
	unsigned long pte;
	int level = domain->pgd_mode - RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39 + 2;

	do {
		const int shift = PAGE_SHIFT + PT_SHIFT * level;

		ptr += ((iova >> shift) & (PTRS_PER_PTE - 1));
		pte = READ_ONCE(*ptr);
		if (_io_pte_present(pte) && _io_pte_leaf(pte)) {
			*pte_pgsize = (size_t)1 << shift;
			return ptr;
		}
		if (_io_pte_none(pte))
			return NULL;
		ptr = (unsigned long *)pfn_to_virt(__page_val_to_pfn(pte));
	} while (level-- > 0);

	return NULL;
}

static int riscv_iommu_map_pages(struct iommu_domain *iommu_domain,
				 unsigned long iova, phys_addr_t phys,
				 size_t pgsize, size_t pgcount, int prot,
				 gfp_t gfp, size_t *mapped)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	size_t size = 0;
	unsigned long *ptr;
	unsigned long pte, old, pte_prot;
	int rc = 0;
	LIST_HEAD(freelist);

	if (!(prot & IOMMU_WRITE))
		pte_prot = _PAGE_BASE | _PAGE_READ;
	else if (domain->amo_enabled)
		pte_prot = _PAGE_BASE | _PAGE_READ | _PAGE_WRITE;
	else
		pte_prot = _PAGE_BASE | _PAGE_READ | _PAGE_WRITE | _PAGE_DIRTY;

	while (pgcount) {
		ptr = riscv_iommu_pte_alloc(domain, iova, pgsize, gfp);
		if (!ptr) {
			rc = -ENOMEM;
			break;
		}

		old = READ_ONCE(*ptr);
		pte = _io_pte_entry(phys_to_pfn(phys), pte_prot);
		if (cmpxchg_relaxed(ptr, old, pte) != old)
			continue;

		riscv_iommu_pte_free(domain, old, &freelist);

		size += pgsize;
		iova += pgsize;
		phys += pgsize;
		--pgcount;
	}

	*mapped = size;

	if (!list_empty(&freelist)) {
		/*
		 * In 1.0 spec version, the smallest scope we can use to
		 * invalidate all levels of page table (i.e. leaf and non-leaf)
		 * is an invalidate-all-PSCID IOTINVAL.VMA with AV=0.
		 * This will be updated with hardware support for
		 * capability.NL (non-leaf) IOTINVAL command.
		 */
		riscv_iommu_iotlb_inval(domain, 0, ULONG_MAX);
		iommu_put_pages_list(&freelist);
	}

	return rc;
}

static size_t riscv_iommu_unmap_pages(struct iommu_domain *iommu_domain,
				      unsigned long iova, size_t pgsize,
				      size_t pgcount,
				      struct iommu_iotlb_gather *gather)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	size_t size = pgcount << __ffs(pgsize);
	unsigned long *ptr, old;
	size_t unmapped = 0;
	size_t pte_size;

	while (unmapped < size) {
		ptr = riscv_iommu_pte_fetch(domain, iova, &pte_size);
		if (!ptr)
			return unmapped;

		/* partial unmap is not allowed, fail. */
		if (iova & (pte_size - 1))
			return unmapped;

		old = READ_ONCE(*ptr);
		if (cmpxchg_relaxed(ptr, old, 0) != old)
			continue;

		iommu_iotlb_gather_add_page(&domain->domain, gather, iova,
					    pte_size);

		iova += pte_size;
		unmapped += pte_size;
	}

	return unmapped;
}

static phys_addr_t riscv_iommu_iova_to_phys(struct iommu_domain *iommu_domain,
					    dma_addr_t iova)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	unsigned long pte_size;
	unsigned long *ptr;

	ptr = riscv_iommu_pte_fetch(domain, iova, &pte_size);
	if (_io_pte_none(*ptr) || !_io_pte_present(*ptr))
		return 0;

	return pfn_to_phys(__page_val_to_pfn(*ptr)) | (iova & (pte_size - 1));
}

static void riscv_iommu_free_paging_domain(struct iommu_domain *iommu_domain)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	const unsigned long pfn = virt_to_pfn(domain->pgd_root);

	WARN_ON(!list_empty(&domain->bonds));

	if ((int)domain->pscid > 0)
		ida_free(&riscv_iommu_pscids, domain->pscid);

	riscv_iommu_pte_free(domain, _io_pte_entry(pfn, _PAGE_TABLE), NULL);
	kfree(domain);
}

static bool riscv_iommu_pt_supported(struct riscv_iommu_device *iommu, int pgd_mode)
{
	switch (pgd_mode) {
	case RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39:
		return iommu->caps & RISCV_IOMMU_CAPABILITIES_SV39;

	case RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV48:
		return iommu->caps & RISCV_IOMMU_CAPABILITIES_SV48;

	case RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV57:
		return iommu->caps & RISCV_IOMMU_CAPABILITIES_SV57;
	}
	return false;
}

static int riscv_iommu_attach_paging_domain(struct iommu_domain *iommu_domain,
					    struct device *dev)
{
	struct riscv_iommu_domain *domain = iommu_domain_to_riscv(iommu_domain);
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);
	struct riscv_iommu_info *info = dev_iommu_priv_get(dev);
	u64 fsc, ta;

	if (!riscv_iommu_pt_supported(iommu, domain->pgd_mode))
		return -ENODEV;

	fsc = FIELD_PREP(RISCV_IOMMU_PC_FSC_MODE, domain->pgd_mode) |
	      FIELD_PREP(RISCV_IOMMU_PC_FSC_PPN, virt_to_pfn(domain->pgd_root));
	ta = FIELD_PREP(RISCV_IOMMU_PC_TA_PSCID, domain->pscid) |
	     RISCV_IOMMU_PC_TA_V;

	if (riscv_iommu_bond_link(domain, dev))
		return -ENOMEM;

	riscv_iommu_iodir_update(iommu, dev, fsc, ta);
	riscv_iommu_bond_unlink(info->domain, dev);
	info->domain = domain;

	return 0;
}

static const struct iommu_domain_ops riscv_iommu_paging_domain_ops = {
	.attach_dev = riscv_iommu_attach_paging_domain,
	.free = riscv_iommu_free_paging_domain,
	.map_pages = riscv_iommu_map_pages,
	.unmap_pages = riscv_iommu_unmap_pages,
	.iova_to_phys = riscv_iommu_iova_to_phys,
	.iotlb_sync = riscv_iommu_iotlb_sync,
	.flush_iotlb_all = riscv_iommu_iotlb_flush_all,
};

static struct iommu_domain *riscv_iommu_alloc_paging_domain(struct device *dev)
{
	struct riscv_iommu_domain *domain;
	struct riscv_iommu_device *iommu;
	unsigned int pgd_mode;
	dma_addr_t va_mask;
	int va_bits;

	iommu = dev_to_iommu(dev);
	if (iommu->caps & RISCV_IOMMU_CAPABILITIES_SV57) {
		pgd_mode = RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV57;
		va_bits = 57;
	} else if (iommu->caps & RISCV_IOMMU_CAPABILITIES_SV48) {
		pgd_mode = RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV48;
		va_bits = 48;
	} else if (iommu->caps & RISCV_IOMMU_CAPABILITIES_SV39) {
		pgd_mode = RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39;
		va_bits = 39;
	} else {
		dev_err(dev, "cannot find supported page table mode\n");
		return ERR_PTR(-ENODEV);
	}

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD_RCU(&domain->bonds);
	spin_lock_init(&domain->lock);
	domain->numa_node = dev_to_node(iommu->dev);
	domain->amo_enabled = !!(iommu->caps & RISCV_IOMMU_CAPABILITIES_AMO_HWAD);
	domain->pgd_mode = pgd_mode;
	domain->pgd_root = iommu_alloc_page_node(domain->numa_node,
						 GFP_KERNEL_ACCOUNT);
	if (!domain->pgd_root) {
		kfree(domain);
		return ERR_PTR(-ENOMEM);
	}

	domain->pscid = ida_alloc_range(&riscv_iommu_pscids, 1,
					RISCV_IOMMU_MAX_PSCID, GFP_KERNEL);
	if (domain->pscid < 0) {
		iommu_free_page(domain->pgd_root);
		kfree(domain);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Note: RISC-V Privilege spec mandates that virtual addresses
	 * need to be sign-extended, so if (VA_BITS - 1) is set, all
	 * bits >= VA_BITS need to also be set or else we'll get a
	 * page fault. However the code that creates the mappings
	 * above us (e.g. iommu_dma_alloc_iova()) won't do that for us
	 * for now, so we'll end up with invalid virtual addresses
	 * to map. As a workaround until we get this sorted out
	 * limit the available virtual addresses to VA_BITS - 1.
	 */
	va_mask = DMA_BIT_MASK(va_bits - 1);

	domain->domain.geometry.aperture_start = 0;
	domain->domain.geometry.aperture_end = va_mask;
	domain->domain.geometry.force_aperture = true;
	domain->domain.pgsize_bitmap = va_mask & (SZ_4K | SZ_2M | SZ_1G | SZ_512G);

	domain->domain.ops = &riscv_iommu_paging_domain_ops;

	return &domain->domain;
}

static int riscv_iommu_attach_blocking_domain(struct iommu_domain *iommu_domain,
					      struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);
	struct riscv_iommu_info *info = dev_iommu_priv_get(dev);

	/* Make device context invalid, translation requests will fault w/ #258 */
	riscv_iommu_iodir_update(iommu, dev, RISCV_IOMMU_FSC_BARE, 0);
	riscv_iommu_bond_unlink(info->domain, dev);
	info->domain = NULL;

	return 0;
}

static struct iommu_domain riscv_iommu_blocking_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev = riscv_iommu_attach_blocking_domain,
	}
};

static int riscv_iommu_attach_identity_domain(struct iommu_domain *iommu_domain,
					      struct device *dev)
{
	struct riscv_iommu_device *iommu = dev_to_iommu(dev);
	struct riscv_iommu_info *info = dev_iommu_priv_get(dev);

	riscv_iommu_iodir_update(iommu, dev, RISCV_IOMMU_FSC_BARE, RISCV_IOMMU_PC_TA_V);
	riscv_iommu_bond_unlink(info->domain, dev);
	info->domain = NULL;

	return 0;
}

static struct iommu_domain riscv_iommu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &(const struct iommu_domain_ops) {
		.attach_dev = riscv_iommu_attach_identity_domain,
	}
};

static struct iommu_group *riscv_iommu_device_group(struct device *dev)
{
	if (dev_is_pci(dev))
		return pci_device_group(dev);
	return generic_device_group(dev);
}

static int riscv_iommu_of_xlate(struct device *dev, const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static struct iommu_device *riscv_iommu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct riscv_iommu_device *iommu;
	struct riscv_iommu_info *info;
	struct riscv_iommu_dc *dc;
	u64 tc;
	int i;

	if (!fwspec || !fwspec->iommu_fwnode->dev || !fwspec->num_ids)
		return ERR_PTR(-ENODEV);

	iommu = dev_get_drvdata(fwspec->iommu_fwnode->dev);
	if (!iommu)
		return ERR_PTR(-ENODEV);

	/*
	 * IOMMU hardware operating in fail-over BARE mode will provide
	 * identity translation for all connected devices anyway...
	 */
	if (iommu->ddt_mode <= RISCV_IOMMU_DDTP_IOMMU_MODE_BARE)
		return ERR_PTR(-ENODEV);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	/*
	 * Allocate and pre-configure device context entries in
	 * the device directory. Do not mark the context valid yet.
	 */
	tc = 0;
	if (iommu->caps & RISCV_IOMMU_CAPABILITIES_AMO_HWAD)
		tc |= RISCV_IOMMU_DC_TC_SADE;
	for (i = 0; i < fwspec->num_ids; i++) {
		dc = riscv_iommu_get_dc(iommu, fwspec->ids[i]);
		if (!dc) {
			kfree(info);
			return ERR_PTR(-ENODEV);
		}
		if (READ_ONCE(dc->tc) & RISCV_IOMMU_DC_TC_V)
			dev_warn(dev, "already attached to IOMMU device directory\n");
		WRITE_ONCE(dc->tc, tc);
	}

	dev_iommu_priv_set(dev, info);

	return &iommu->iommu;
}

static void riscv_iommu_release_device(struct device *dev)
{
	struct riscv_iommu_info *info = dev_iommu_priv_get(dev);

	kfree_rcu_mightsleep(info);
}

static const struct iommu_ops riscv_iommu_ops = {
	.pgsize_bitmap = SZ_4K,
	.of_xlate = riscv_iommu_of_xlate,
	.identity_domain = &riscv_iommu_identity_domain,
	.blocked_domain = &riscv_iommu_blocking_domain,
	.release_domain = &riscv_iommu_blocking_domain,
	.domain_alloc_paging = riscv_iommu_alloc_paging_domain,
	.device_group = riscv_iommu_device_group,
	.probe_device = riscv_iommu_probe_device,
	.release_device	= riscv_iommu_release_device,
};

static int riscv_iommu_init_check(struct riscv_iommu_device *iommu)
{
	u64 ddtp;

	/*
	 * Make sure the IOMMU is switched off or in pass-through mode during
	 * regular boot flow and disable translation when we boot into a kexec
	 * kernel and the previous kernel left them enabled.
	 */
	ddtp = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_DDTP);
	if (ddtp & RISCV_IOMMU_DDTP_BUSY)
		return -EBUSY;

	if (FIELD_GET(RISCV_IOMMU_DDTP_IOMMU_MODE, ddtp) >
	     RISCV_IOMMU_DDTP_IOMMU_MODE_BARE) {
		if (!is_kdump_kernel())
			return -EBUSY;
		riscv_iommu_disable(iommu);
	}

	/* Configure accesses to in-memory data structures for CPU-native byte order. */
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
	    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE)) {
		if (!(iommu->caps & RISCV_IOMMU_CAPABILITIES_END))
			return -EINVAL;
		riscv_iommu_writel(iommu, RISCV_IOMMU_REG_FCTL,
				   iommu->fctl ^ RISCV_IOMMU_FCTL_BE);
		iommu->fctl = riscv_iommu_readl(iommu, RISCV_IOMMU_REG_FCTL);
		if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) !=
		    !!(iommu->fctl & RISCV_IOMMU_FCTL_BE))
			return -EINVAL;
	}

	/*
	 * Distribute interrupt vectors, always use first vector for CIV.
	 * At least one interrupt is required. Read back and verify.
	 */
	if (!iommu->irqs_count)
		return -EINVAL;

	iommu->icvec = FIELD_PREP(RISCV_IOMMU_ICVEC_FIV, 1 % iommu->irqs_count) |
		       FIELD_PREP(RISCV_IOMMU_ICVEC_PIV, 2 % iommu->irqs_count) |
		       FIELD_PREP(RISCV_IOMMU_ICVEC_PMIV, 3 % iommu->irqs_count);
	riscv_iommu_writeq(iommu, RISCV_IOMMU_REG_ICVEC, iommu->icvec);
	iommu->icvec = riscv_iommu_readq(iommu, RISCV_IOMMU_REG_ICVEC);
	if (max(max(FIELD_GET(RISCV_IOMMU_ICVEC_CIV, iommu->icvec),
		    FIELD_GET(RISCV_IOMMU_ICVEC_FIV, iommu->icvec)),
		max(FIELD_GET(RISCV_IOMMU_ICVEC_PIV, iommu->icvec),
		    FIELD_GET(RISCV_IOMMU_ICVEC_PMIV, iommu->icvec))) >= iommu->irqs_count)
		return -EINVAL;

	return 0;
}

void riscv_iommu_remove(struct riscv_iommu_device *iommu)
{
	iommu_device_unregister(&iommu->iommu);
	iommu_device_sysfs_remove(&iommu->iommu);
	riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_OFF);
	riscv_iommu_queue_disable(&iommu->cmdq);
	riscv_iommu_queue_disable(&iommu->fltq);
}

int riscv_iommu_init(struct riscv_iommu_device *iommu)
{
	int rc;

	RISCV_IOMMU_QUEUE_INIT(&iommu->cmdq, CQ);
	RISCV_IOMMU_QUEUE_INIT(&iommu->fltq, FQ);

	rc = riscv_iommu_init_check(iommu);
	if (rc)
		return dev_err_probe(iommu->dev, rc, "unexpected device state\n");

	rc = riscv_iommu_iodir_alloc(iommu);
	if (rc)
		return rc;

	rc = riscv_iommu_queue_alloc(iommu, &iommu->cmdq,
				     sizeof(struct riscv_iommu_command));
	if (rc)
		return rc;

	rc = riscv_iommu_queue_alloc(iommu, &iommu->fltq,
				     sizeof(struct riscv_iommu_fq_record));
	if (rc)
		return rc;

	rc = riscv_iommu_queue_enable(iommu, &iommu->cmdq, riscv_iommu_cmdq_process);
	if (rc)
		return rc;

	rc = riscv_iommu_queue_enable(iommu, &iommu->fltq, riscv_iommu_fltq_process);
	if (rc)
		goto err_queue_disable;

	rc = riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_MAX);
	if (rc)
		goto err_queue_disable;

	rc = iommu_device_sysfs_add(&iommu->iommu, NULL, NULL, "riscv-iommu@%s",
				    dev_name(iommu->dev));
	if (rc) {
		dev_err_probe(iommu->dev, rc, "cannot register sysfs interface\n");
		goto err_iodir_off;
	}

	rc = iommu_device_register(&iommu->iommu, &riscv_iommu_ops, iommu->dev);
	if (rc) {
		dev_err_probe(iommu->dev, rc, "cannot register iommu interface\n");
		goto err_remove_sysfs;
	}

	return 0;

err_remove_sysfs:
	iommu_device_sysfs_remove(&iommu->iommu);
err_iodir_off:
	riscv_iommu_iodir_set_mode(iommu, RISCV_IOMMU_DDTP_IOMMU_MODE_OFF);
err_queue_disable:
	riscv_iommu_queue_disable(&iommu->fltq);
	riscv_iommu_queue_disable(&iommu->cmdq);
	return rc;
}
