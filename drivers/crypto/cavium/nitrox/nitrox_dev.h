/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_DEV_H
#define __NITROX_DEV_H

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#define VERSION_LEN 32

struct nitrox_cmdq {
	/* command queue lock */
	spinlock_t cmdq_lock;
	/* response list lock */
	spinlock_t response_lock;
	/* backlog list lock */
	spinlock_t backlog_lock;

	/* request submitted to chip, in progress */
	struct list_head response_head;
	/* hw queue full, hold in backlog list */
	struct list_head backlog_head;

	/* doorbell address */
	u8 __iomem *dbell_csr_addr;
	/* base address of the queue */
	u8 *head;

	struct nitrox_device *ndev;
	/* flush pending backlog commands */
	struct work_struct backlog_qflush;

	/* requests posted waiting for completion */
	atomic_t pending_count;
	/* requests in backlog queues */
	atomic_t backlog_count;

	/* command size 32B/64B */
	u8 instr_size;
	u8 qno;
	u32 qsize;

	/* unaligned addresses */
	u8 *head_unaligned;
	dma_addr_t dma_unaligned;
	/* dma address of the base */
	dma_addr_t dma;
};

struct nitrox_hw {
	/* firmware version */
	char fw_name[VERSION_LEN];

	u16 vendor_id;
	u16 device_id;
	u8 revision_id;

	/* CNN55XX cores */
	u8 se_cores;
	u8 ae_cores;
	u8 zip_cores;
};

#define MAX_MSIX_VECTOR_NAME	20
/**
 * vectors for queues (64 AE, 64 SE and 64 ZIP) and
 * error condition/mailbox.
 */
#define MAX_MSIX_VECTORS	192

struct nitrox_msix {
	struct msix_entry *entries;
	char **names;
	DECLARE_BITMAP(irqs, MAX_MSIX_VECTORS);
	u32 nr_entries;
};

struct bh_data {
	/* slc port completion count address */
	u8 __iomem *completion_cnt_csr_addr;

	struct nitrox_cmdq *cmdq;
	struct tasklet_struct resp_handler;
};

struct nitrox_bh {
	struct bh_data *slc;
};

/* NITROX-5 driver state */
#define NITROX_UCODE_LOADED	0
#define NITROX_READY		1

/* command queue size */
#define DEFAULT_CMD_QLEN 2048
/* command timeout in milliseconds */
#define CMD_TIMEOUT 2000

#define DEV(ndev) ((struct device *)(&(ndev)->pdev->dev))
#define PF_MODE 0

#define NITROX_CSR_ADDR(ndev, offset) \
	((ndev)->bar_addr + (offset))

/**
 * struct nitrox_device - NITROX Device Information.
 * @list: pointer to linked list of devices
 * @bar_addr: iomap address
 * @pdev: PCI device information
 * @status: NITROX status
 * @timeout: Request timeout in jiffies
 * @refcnt: Device usage count
 * @idx: device index (0..N)
 * @node: NUMA node id attached
 * @qlen: Command queue length
 * @nr_queues: Number of command queues
 * @ctx_pool: DMA pool for crypto context
 * @pkt_cmdqs: SE Command queues
 * @msix: MSI-X information
 * @bh: post processing work
 * @hw: hardware information
 * @debugfs_dir: debugfs directory
 */
struct nitrox_device {
	struct list_head list;

	u8 __iomem *bar_addr;
	struct pci_dev *pdev;

	unsigned long status;
	unsigned long timeout;
	refcount_t refcnt;

	u8 idx;
	int node;
	u16 qlen;
	u16 nr_queues;

	struct dma_pool *ctx_pool;
	struct nitrox_cmdq *pkt_cmdqs;

	struct nitrox_msix msix;
	struct nitrox_bh bh;

	struct nitrox_hw hw;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *debugfs_dir;
#endif
};

/**
 * nitrox_read_csr - Read from device register
 * @ndev: NITROX device
 * @offset: offset of the register to read
 *
 * Returns: value read
 */
static inline u64 nitrox_read_csr(struct nitrox_device *ndev, u64 offset)
{
	return readq(ndev->bar_addr + offset);
}

/**
 * nitrox_write_csr - Write to device register
 * @ndev: NITROX device
 * @offset: offset of the register to write
 * @value: value to write
 */
static inline void nitrox_write_csr(struct nitrox_device *ndev, u64 offset,
				    u64 value)
{
	writeq(value, (ndev->bar_addr + offset));
}

static inline int nitrox_ready(struct nitrox_device *ndev)
{
	return test_bit(NITROX_READY, &ndev->status);
}

#endif /* __NITROX_DEV_H */
