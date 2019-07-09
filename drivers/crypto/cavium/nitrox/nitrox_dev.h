/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_DEV_H
#define __NITROX_DEV_H

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/if.h>

#define VERSION_LEN 32
/* Maximum queues in PF mode */
#define MAX_PF_QUEUES	64
/* Maximum UCD Blocks */
#define CNN55XX_MAX_UCD_BLOCKS	8

/**
 * struct nitrox_cmdq - NITROX command queue
 * @cmd_qlock: command queue lock
 * @resp_qlock: response queue lock
 * @backlog_qlock: backlog queue lock
 * @ndev: NITROX device
 * @response_head: submitted request list
 * @backlog_head: backlog queue
 * @dbell_csr_addr: doorbell register address for this queue
 * @compl_cnt_csr_addr: completion count register address of the slc port
 * @base: command queue base address
 * @dma: dma address of the base
 * @pending_count: request pending at device
 * @backlog_count: backlog request count
 * @write_idx: next write index for the command
 * @instr_size: command size
 * @qno: command queue number
 * @qsize: command queue size
 * @unalign_base: unaligned base address
 * @unalign_dma: unaligned dma address
 */
struct nitrox_cmdq {
	spinlock_t cmd_qlock;
	spinlock_t resp_qlock;
	spinlock_t backlog_qlock;

	struct nitrox_device *ndev;
	struct list_head response_head;
	struct list_head backlog_head;

	u8 __iomem *dbell_csr_addr;
	u8 __iomem *compl_cnt_csr_addr;
	u8 *base;
	dma_addr_t dma;

	struct work_struct backlog_qflush;

	atomic_t pending_count;
	atomic_t backlog_count;

	int write_idx;
	u8 instr_size;
	u8 qno;
	u32 qsize;

	u8 *unalign_base;
	dma_addr_t unalign_dma;
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
	char fw_name[CNN55XX_MAX_UCD_BLOCKS][VERSION_LEN];

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

#define IRQ_NAMESZ	32

struct nitrox_q_vector {
	char name[IRQ_NAMESZ];
	bool valid;
	int ring;
	struct tasklet_struct resp_tasklet;
	union {
		struct nitrox_cmdq *cmdq;
		struct nitrox_device *ndev;
	};
};

/**
 * mbox_msg - Mailbox message data
 * @type: message type
 * @opcode: message opcode
 * @data: message data
 */
union mbox_msg {
	u64 value;
	struct {
		u64 type: 2;
		u64 opcode: 6;
		u64 data: 58;
	};
	struct {
		u64 type: 2;
		u64 opcode: 6;
		u64 chipid: 8;
		u64 vfid: 8;
	} id;
};

/**
 * nitrox_vfdev - NITROX VF device instance in PF
 * @state: VF device state
 * @vfno: VF number
 * @nr_queues: number of queues enabled in VF
 * @ring: ring to communicate with VF
 * @msg: Mailbox message data from VF
 * @mbx_resp: Mailbox counters
 */
struct nitrox_vfdev {
	atomic_t state;
	int vfno;
	int nr_queues;
	int ring;
	union mbox_msg msg;
	atomic64_t mbx_resp;
};

/**
 * struct nitrox_iov - SR-IOV information
 * @num_vfs: number of VF(s) enabled
 * @max_vf_queues: Maximum number of queues allowed for VF
 * @vfdev: VF(s) devices
 * @pf2vf_wq: workqueue for PF2VF communication
 * @msix: MSI-X entry for PF in SR-IOV case
 */
struct nitrox_iov {
	int num_vfs;
	int max_vf_queues;
	struct nitrox_vfdev *vfdev;
	struct workqueue_struct *pf2vf_wq;
	struct msix_entry msix;
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
 * @pkt_inq: Packet input rings
 * @qvec: MSI-X queue vectors information
 * @iov: SR-IOV informatin
 * @num_vecs: number of MSI-X vectors
 * @stats: request statistics
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
	enum vf_mode mode;

	struct dma_pool *ctx_pool;
	struct nitrox_cmdq *pkt_inq;

	struct nitrox_q_vector *qvec;
	struct nitrox_iov iov;
	int num_vecs;

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

static inline bool nitrox_vfdev_ready(struct nitrox_vfdev *vfdev)
{
	return atomic_read(&vfdev->state) == __NDEV_READY;
}

#endif /* __NITROX_DEV_H */
