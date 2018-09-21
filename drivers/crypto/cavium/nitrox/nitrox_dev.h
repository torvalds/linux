/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_DEV_H
#define __NITROX_DEV_H

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/if.h>

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

	int write_idx;
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

/**
 * struct nitrox_hw - NITROX hardware information
 * @partname: partname ex: CNN55xxx-xxx
 * @fw_name: firmware version
 * @freq: NITROX frequency
 * @vendor_id: vendor ID
 * @device_id: device ID
 * @revision_id: revision ID
 * @se_cores: number of symmetric cores
 * @ae_cores: number of asymmetric cores
 * @zip_cores: number of zip cores
 */
struct nitrox_hw {
	char partname[IFNAMSIZ * 2];
	char fw_name[VERSION_LEN];

	int freq;
	u16 vendor_id;
	u16 device_id;
	u8 revision_id;

	u8 se_cores;
	u8 ae_cores;
	u8 zip_cores;
};

struct nitrox_stats {
	atomic64_t posted;
	atomic64_t completed;
	atomic64_t dropped;
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

/*
 * NITROX Device states
 */
enum ndev_state {
	__NDEV_NOT_READY,
	__NDEV_READY,
	__NDEV_IN_RESET,
};

/* NITROX support modes for VF(s) */
enum vf_mode {
	__NDEV_MODE_PF,
	__NDEV_MODE_VF16,
	__NDEV_MODE_VF32,
	__NDEV_MODE_VF64,
	__NDEV_MODE_VF128,
};

#define __NDEV_SRIOV_BIT 0

/* command queue size */
#define DEFAULT_CMD_QLEN 2048
/* command timeout in milliseconds */
#define CMD_TIMEOUT 2000

#define DEV(ndev) ((struct device *)(&(ndev)->pdev->dev))

#define NITROX_CSR_ADDR(ndev, offset) \
	((ndev)->bar_addr + (offset))

/**
 * struct nitrox_device - NITROX Device Information.
 * @list: pointer to linked list of devices
 * @bar_addr: iomap address
 * @pdev: PCI device information
 * @state: NITROX device state
 * @flags: flags to indicate device the features
 * @timeout: Request timeout in jiffies
 * @refcnt: Device usage count
 * @idx: device index (0..N)
 * @node: NUMA node id attached
 * @qlen: Command queue length
 * @nr_queues: Number of command queues
 * @mode: Device mode PF/VF
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

	atomic_t state;
	unsigned long flags;
	unsigned long timeout;
	refcount_t refcnt;

	u8 idx;
	int node;
	u16 qlen;
	u16 nr_queues;
	int num_vfs;
	enum vf_mode mode;

	struct dma_pool *ctx_pool;
	struct nitrox_cmdq *pkt_cmdqs;

	struct nitrox_msix msix;
	struct nitrox_bh bh;

	struct nitrox_stats stats;
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

static inline bool nitrox_ready(struct nitrox_device *ndev)
{
	return atomic_read(&ndev->state) == __NDEV_READY;
}

#endif /* __NITROX_DEV_H */
