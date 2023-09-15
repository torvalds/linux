/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_H
#define BCM_VK_H

#include <linux/atomic.h>
#include <linux/firmware.h>
#include <linux/irq.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/uaccess.h>
#include <uapi/linux/misc/bcm_vk.h>

#include "bcm_vk_msg.h"

#define DRV_MODULE_NAME		"bcm-vk"

/*
 * Load Image is completed in two stages:
 *
 * 1) When the VK device boot-up, M7 CPU runs and executes the BootROM.
 * The Secure Boot Loader (SBL) as part of the BootROM will run
 * to open up ITCM for host to push BOOT1 image.
 * SBL will authenticate the image before jumping to BOOT1 image.
 *
 * 2) Because BOOT1 image is a secured image, we also called it the
 * Secure Boot Image (SBI). At second stage, SBI will initialize DDR
 * and wait for host to push BOOT2 image to DDR.
 * SBI will authenticate the image before jumping to BOOT2 image.
 *
 */
/* Location of registers of interest in BAR0 */

/* Request register for Secure Boot Loader (SBL) download */
#define BAR_CODEPUSH_SBL		0x400
/* Start of ITCM */
#define CODEPUSH_BOOT1_ENTRY		0x00400000
#define CODEPUSH_MASK		        0xfffff000
#define CODEPUSH_BOOTSTART		BIT(0)

/* Boot Status register */
#define BAR_BOOT_STATUS			0x404

#define SRAM_OPEN			BIT(16)
#define DDR_OPEN			BIT(17)

/* Firmware loader progress status definitions */
#define FW_LOADER_ACK_SEND_MORE_DATA	BIT(18)
#define FW_LOADER_ACK_IN_PROGRESS	BIT(19)
#define FW_LOADER_ACK_RCVD_ALL_DATA	BIT(20)

/* Boot1/2 is running in standalone mode */
#define BOOT_STDALONE_RUNNING		BIT(21)

/* definitions for boot status register */
#define BOOT_STATE_MASK			(0xffffffff & \
					 ~(FW_LOADER_ACK_SEND_MORE_DATA | \
					   FW_LOADER_ACK_IN_PROGRESS | \
					   BOOT_STDALONE_RUNNING))

#define BOOT_ERR_SHIFT			4
#define BOOT_ERR_MASK			(0xf << BOOT_ERR_SHIFT)
#define BOOT_PROG_MASK			0xf

#define BROM_STATUS_NOT_RUN		0x2
#define BROM_NOT_RUN			(SRAM_OPEN | BROM_STATUS_NOT_RUN)
#define BROM_STATUS_COMPLETE		0x6
#define BROM_RUNNING			(SRAM_OPEN | BROM_STATUS_COMPLETE)
#define BOOT1_STATUS_COMPLETE		0x6
#define BOOT1_RUNNING			(DDR_OPEN | BOOT1_STATUS_COMPLETE)
#define BOOT2_STATUS_COMPLETE		0x6
#define BOOT2_RUNNING			(FW_LOADER_ACK_RCVD_ALL_DATA | \
					 BOOT2_STATUS_COMPLETE)

/* Boot request for Secure Boot Image (SBI) */
#define BAR_CODEPUSH_SBI		0x408
/* 64M mapped to BAR2 */
#define CODEPUSH_BOOT2_ENTRY		0x60000000

#define BAR_CARD_STATUS			0x410
/* CARD_STATUS definitions */
#define CARD_STATUS_TTYVK0_READY	BIT(0)
#define CARD_STATUS_TTYVK1_READY	BIT(1)

#define BAR_BOOT1_STDALONE_PROGRESS	0x420
#define BOOT1_STDALONE_SUCCESS		(BIT(13) | BIT(14))
#define BOOT1_STDALONE_PROGRESS_MASK	BOOT1_STDALONE_SUCCESS

#define BAR_METADATA_VERSION		0x440
#define BAR_OS_UPTIME			0x444
#define BAR_CHIP_ID			0x448
#define MAJOR_SOC_REV(_chip_id)		(((_chip_id) >> 20) & 0xf)

#define BAR_CARD_TEMPERATURE		0x45c
/* defines for all temperature sensor */
#define BCM_VK_TEMP_FIELD_MASK		0xff
#define BCM_VK_CPU_TEMP_SHIFT		0
#define BCM_VK_DDR0_TEMP_SHIFT		8
#define BCM_VK_DDR1_TEMP_SHIFT		16

#define BAR_CARD_VOLTAGE		0x460
/* defines for voltage rail conversion */
#define BCM_VK_VOLT_RAIL_MASK		0xffff
#define BCM_VK_3P3_VOLT_REG_SHIFT	16

#define BAR_CARD_ERR_LOG		0x464
/* Error log register bit definition - register for error alerts */
#define ERR_LOG_UECC			BIT(0)
#define ERR_LOG_SSIM_BUSY		BIT(1)
#define ERR_LOG_AFBC_BUSY		BIT(2)
#define ERR_LOG_HIGH_TEMP_ERR		BIT(3)
#define ERR_LOG_WDOG_TIMEOUT		BIT(4)
#define ERR_LOG_SYS_FAULT		BIT(5)
#define ERR_LOG_RAMDUMP			BIT(6)
#define ERR_LOG_COP_WDOG_TIMEOUT	BIT(7)
/* warnings */
#define ERR_LOG_MEM_ALLOC_FAIL		BIT(8)
#define ERR_LOG_LOW_TEMP_WARN		BIT(9)
#define ERR_LOG_ECC			BIT(10)
#define ERR_LOG_IPC_DWN			BIT(11)

/* Alert bit definitions detectd on host */
#define ERR_LOG_HOST_INTF_V_FAIL	BIT(13)
#define ERR_LOG_HOST_HB_FAIL		BIT(14)
#define ERR_LOG_HOST_PCIE_DWN		BIT(15)

#define BAR_CARD_ERR_MEM		0x468
/* defines for mem err, all fields have same width */
#define BCM_VK_MEM_ERR_FIELD_MASK	0xff
#define BCM_VK_ECC_MEM_ERR_SHIFT	0
#define BCM_VK_UECC_MEM_ERR_SHIFT	8
/* threshold of event occurrence and logs start to come out */
#define BCM_VK_ECC_THRESHOLD		10
#define BCM_VK_UECC_THRESHOLD		1

#define BAR_CARD_PWR_AND_THRE		0x46c
/* defines for power and temp threshold, all fields have same width */
#define BCM_VK_PWR_AND_THRE_FIELD_MASK	0xff
#define BCM_VK_LOW_TEMP_THRE_SHIFT	0
#define BCM_VK_HIGH_TEMP_THRE_SHIFT	8
#define BCM_VK_PWR_STATE_SHIFT		16

#define BAR_CARD_STATIC_INFO		0x470

#define BAR_INTF_VER			0x47c
#define BAR_INTF_VER_MAJOR_SHIFT	16
#define BAR_INTF_VER_MASK		0xffff
/*
 * major and minor semantic version numbers supported
 * Please update as required on interface changes
 */
#define SEMANTIC_MAJOR			1
#define SEMANTIC_MINOR			0

/*
 * first door bell reg, ie for queue = 0.  Only need the first one, as
 * we will use the queue number to derive the others
 */
#define VK_BAR0_REGSEG_DB_BASE		0x484
#define VK_BAR0_REGSEG_DB_REG_GAP	8 /*
					   * DB register gap,
					   * DB1 at 0x48c and DB2 at 0x494
					   */

/* reset register and specific values */
#define VK_BAR0_RESET_DB_NUM		3
#define VK_BAR0_RESET_DB_SOFT		0xffffffff
#define VK_BAR0_RESET_DB_HARD		0xfffffffd
#define VK_BAR0_RESET_RAMPDUMP		0xa0000000

#define VK_BAR0_Q_DB_BASE(q_num)	(VK_BAR0_REGSEG_DB_BASE + \
					 ((q_num) * VK_BAR0_REGSEG_DB_REG_GAP))
#define VK_BAR0_RESET_DB_BASE		(VK_BAR0_REGSEG_DB_BASE + \
					 (VK_BAR0_RESET_DB_NUM * VK_BAR0_REGSEG_DB_REG_GAP))

#define BAR_BOOTSRC_SELECT		0xc78
/* BOOTSRC definitions */
#define BOOTSRC_SOFT_ENABLE		BIT(14)

/* Card OS Firmware version size */
#define BAR_FIRMWARE_TAG_SIZE		50
#define FIRMWARE_STATUS_PRE_INIT_DONE	0x1f

/* VK MSG_ID defines */
#define VK_MSG_ID_BITMAP_SIZE		4096
#define VK_MSG_ID_BITMAP_MASK		(VK_MSG_ID_BITMAP_SIZE - 1)
#define VK_MSG_ID_OVERFLOW		0xffff

/*
 * BAR1
 */

/* BAR1 message q definition */

/* indicate if msgq ctrl in BAR1 is populated */
#define VK_BAR1_MSGQ_DEF_RDY		0x60c0
/* ready marker value for the above location, normal boot2 */
#define VK_BAR1_MSGQ_RDY_MARKER		0xbeefcafe
/* ready marker value for the above location, normal boot2 */
#define VK_BAR1_DIAG_RDY_MARKER		0xdeadcafe
/* number of msgqs in BAR1 */
#define VK_BAR1_MSGQ_NR			0x60c4
/* BAR1 queue control structure offset */
#define VK_BAR1_MSGQ_CTRL_OFF		0x60c8

/* BAR1 ucode and boot1 version tag */
#define VK_BAR1_UCODE_VER_TAG		0x6170
#define VK_BAR1_BOOT1_VER_TAG		0x61b0
#define VK_BAR1_VER_TAG_SIZE		64

/* Memory to hold the DMA buffer memory address allocated for boot2 download */
#define VK_BAR1_DMA_BUF_OFF_HI		0x61e0
#define VK_BAR1_DMA_BUF_OFF_LO		(VK_BAR1_DMA_BUF_OFF_HI + 4)
#define VK_BAR1_DMA_BUF_SZ		(VK_BAR1_DMA_BUF_OFF_HI + 8)

/* Scratch memory allocated on host for VK */
#define VK_BAR1_SCRATCH_OFF_HI		0x61f0
#define VK_BAR1_SCRATCH_OFF_LO		(VK_BAR1_SCRATCH_OFF_HI + 4)
#define VK_BAR1_SCRATCH_SZ_ADDR		(VK_BAR1_SCRATCH_OFF_HI + 8)
#define VK_BAR1_SCRATCH_DEF_NR_PAGES	32

/* BAR1 DAUTH info */
#define VK_BAR1_DAUTH_BASE_ADDR		0x6200
#define VK_BAR1_DAUTH_STORE_SIZE	0x48
#define VK_BAR1_DAUTH_VALID_SIZE	0x8
#define VK_BAR1_DAUTH_MAX		4
#define VK_BAR1_DAUTH_STORE_ADDR(x) \
		(VK_BAR1_DAUTH_BASE_ADDR + \
		 (x) * (VK_BAR1_DAUTH_STORE_SIZE + VK_BAR1_DAUTH_VALID_SIZE))
#define VK_BAR1_DAUTH_VALID_ADDR(x) \
		(VK_BAR1_DAUTH_STORE_ADDR(x) + VK_BAR1_DAUTH_STORE_SIZE)

/* BAR1 SOTP AUTH and REVID info */
#define VK_BAR1_SOTP_REVID_BASE_ADDR	0x6340
#define VK_BAR1_SOTP_REVID_SIZE		0x10
#define VK_BAR1_SOTP_REVID_MAX		2
#define VK_BAR1_SOTP_REVID_ADDR(x) \
		(VK_BAR1_SOTP_REVID_BASE_ADDR + (x) * VK_BAR1_SOTP_REVID_SIZE)

/* VK device supports a maximum of 3 bars */
#define MAX_BAR	3

/* default number of msg blk for inband SGL */
#define BCM_VK_DEF_IB_SGL_BLK_LEN	 16
#define BCM_VK_IB_SGL_BLK_MAX		 24

enum pci_barno {
	BAR_0 = 0,
	BAR_1,
	BAR_2
};

#ifdef CONFIG_BCM_VK_TTY
#define BCM_VK_NUM_TTY 2
#else
#define BCM_VK_NUM_TTY 0
#endif

struct bcm_vk_tty {
	struct tty_port port;
	u32 to_offset;	/* bar offset to use */
	u32 to_size;	/* to VK buffer size */
	u32 wr;		/* write offset shadow */
	u32 from_offset;	/* bar offset to use */
	u32 from_size;	/* from VK buffer size */
	u32 rd;		/* read offset shadow */
	pid_t pid;
	bool irq_enabled;
	bool is_opened;		/* tracks tty open/close */
};

/* VK device max power state, supports 3, full, reduced and low */
#define MAX_OPP 3
#define MAX_CARD_INFO_TAG_SIZE 64

struct bcm_vk_card_info {
	u32 version;
	char os_tag[MAX_CARD_INFO_TAG_SIZE];
	char cmpt_tag[MAX_CARD_INFO_TAG_SIZE];
	u32 cpu_freq_mhz;
	u32 cpu_scale[MAX_OPP];
	u32 ddr_freq_mhz;
	u32 ddr_size_MB;
	u32 video_core_freq_mhz;
};

/* DAUTH related info */
struct bcm_vk_dauth_key {
	char store[VK_BAR1_DAUTH_STORE_SIZE];
	char valid[VK_BAR1_DAUTH_VALID_SIZE];
};

struct bcm_vk_dauth_info {
	struct bcm_vk_dauth_key keys[VK_BAR1_DAUTH_MAX];
};

/*
 * Control structure of logging messages from the card.  This
 * buffer is for logmsg that comes from vk
 */
struct bcm_vk_peer_log {
	u32 rd_idx;
	u32 wr_idx;
	u32 buf_size;
	u32 mask;
	char data[];
};

/* max buf size allowed */
#define BCM_VK_PEER_LOG_BUF_MAX SZ_16K
/* max size per line of peer log */
#define BCM_VK_PEER_LOG_LINE_MAX  256

/*
 * single entry for processing type + utilization
 */
#define BCM_VK_PROC_TYPE_TAG_LEN 8
struct bcm_vk_proc_mon_entry_t {
	char tag[BCM_VK_PROC_TYPE_TAG_LEN];
	u32 used;
	u32 max; /**< max capacity */
};

/**
 * Structure for run time utilization
 */
#define BCM_VK_PROC_MON_MAX 8 /* max entries supported */
struct bcm_vk_proc_mon_info {
	u32 num; /**< no of entries */
	u32 entry_size; /**< per entry size */
	struct bcm_vk_proc_mon_entry_t entries[BCM_VK_PROC_MON_MAX];
};

struct bcm_vk_hb_ctrl {
	struct delayed_work work;
	u32 last_uptime;
	u32 lost_cnt;
};

struct bcm_vk_alert {
	u16 flags;
	u16 notfs;
};

/* some alert counters that the driver will keep track */
struct bcm_vk_alert_cnts {
	u16 ecc;
	u16 uecc;
};

struct bcm_vk {
	struct pci_dev *pdev;
	void __iomem *bar[MAX_BAR];
	int num_irqs;

	struct bcm_vk_card_info card_info;
	struct bcm_vk_proc_mon_info proc_mon_info;
	struct bcm_vk_dauth_info dauth_info;

	/* mutex to protect the ioctls */
	struct mutex mutex;
	struct miscdevice miscdev;
	int devid; /* dev id allocated */

#ifdef CONFIG_BCM_VK_TTY
	struct tty_driver *tty_drv;
	struct timer_list serial_timer;
	struct bcm_vk_tty tty[BCM_VK_NUM_TTY];
	struct workqueue_struct *tty_wq_thread;
	struct work_struct tty_wq_work;
#endif

	/* Reference-counting to handle file operations */
	struct kref kref;

	spinlock_t msg_id_lock; /* Spinlock for msg_id */
	u16 msg_id;
	DECLARE_BITMAP(bmap, VK_MSG_ID_BITMAP_SIZE);
	spinlock_t ctx_lock; /* Spinlock for component context */
	struct bcm_vk_ctx ctx[VK_CMPT_CTX_MAX];
	struct bcm_vk_ht_entry pid_ht[VK_PID_HT_SZ];
	pid_t reset_pid; /* process that issue reset */

	atomic_t msgq_inited; /* indicate if info has been synced with vk */
	struct bcm_vk_msg_chan to_v_msg_chan;
	struct bcm_vk_msg_chan to_h_msg_chan;

	struct workqueue_struct *wq_thread;
	struct work_struct wq_work; /* work queue for deferred job */
	unsigned long wq_offload[1]; /* various flags on wq requested */
	void *tdma_vaddr; /* test dma segment virtual addr */
	dma_addr_t tdma_addr; /* test dma segment bus addr */

	struct notifier_block panic_nb;
	u32 ib_sgl_size; /* size allocated for inband sgl insertion */

	/* heart beat mechanism control structure */
	struct bcm_vk_hb_ctrl hb_ctrl;
	/* house-keeping variable of error logs */
	spinlock_t host_alert_lock; /* protection to access host_alert struct */
	struct bcm_vk_alert host_alert;
	struct bcm_vk_alert peer_alert; /* bits set by the card */
	struct bcm_vk_alert_cnts alert_cnts;

	/* offset of the peer log control in BAR2 */
	u32 peerlog_off;
	struct bcm_vk_peer_log peerlog_info; /* record of peer log info */
	/* offset of processing monitoring info in BAR2 */
	u32 proc_mon_off;
};

/* wq offload work items bits definitions */
enum bcm_vk_wq_offload_flags {
	BCM_VK_WQ_DWNLD_PEND = 0,
	BCM_VK_WQ_DWNLD_AUTO = 1,
	BCM_VK_WQ_NOTF_PEND  = 2,
};

/* a macro to get an individual field with mask and shift */
#define BCM_VK_EXTRACT_FIELD(_field, _reg, _mask, _shift) \
		(_field = (((_reg) >> (_shift)) & (_mask)))

struct bcm_vk_entry {
	const u32 mask;
	const u32 exp_val;
	const char *str;
};

/* alerts that could be generated from peer */
#define BCM_VK_PEER_ERR_NUM 12
extern struct bcm_vk_entry const bcm_vk_peer_err[BCM_VK_PEER_ERR_NUM];
/* alerts detected by the host */
#define BCM_VK_HOST_ERR_NUM 3
extern struct bcm_vk_entry const bcm_vk_host_err[BCM_VK_HOST_ERR_NUM];

/*
 * check if PCIe interface is down on read.  Use it when it is
 * certain that _val should never be all ones.
 */
#define BCM_VK_INTF_IS_DOWN(val) ((val) == 0xffffffff)

static inline u32 vkread32(struct bcm_vk *vk, enum pci_barno bar, u64 offset)
{
	return readl(vk->bar[bar] + offset);
}

static inline void vkwrite32(struct bcm_vk *vk,
			     u32 value,
			     enum pci_barno bar,
			     u64 offset)
{
	writel(value, vk->bar[bar] + offset);
}

static inline u8 vkread8(struct bcm_vk *vk, enum pci_barno bar, u64 offset)
{
	return readb(vk->bar[bar] + offset);
}

static inline void vkwrite8(struct bcm_vk *vk,
			    u8 value,
			    enum pci_barno bar,
			    u64 offset)
{
	writeb(value, vk->bar[bar] + offset);
}

static inline bool bcm_vk_msgq_marker_valid(struct bcm_vk *vk)
{
	u32 rdy_marker = 0;
	u32 fw_status;

	fw_status = vkread32(vk, BAR_0, VK_BAR_FWSTS);

	if ((fw_status & VK_FWSTS_READY) == VK_FWSTS_READY)
		rdy_marker = vkread32(vk, BAR_1, VK_BAR1_MSGQ_DEF_RDY);

	return (rdy_marker == VK_BAR1_MSGQ_RDY_MARKER);
}

int bcm_vk_open(struct inode *inode, struct file *p_file);
ssize_t bcm_vk_read(struct file *p_file, char __user *buf, size_t count,
		    loff_t *f_pos);
ssize_t bcm_vk_write(struct file *p_file, const char __user *buf,
		     size_t count, loff_t *f_pos);
__poll_t bcm_vk_poll(struct file *p_file, struct poll_table_struct *wait);
int bcm_vk_release(struct inode *inode, struct file *p_file);
void bcm_vk_release_data(struct kref *kref);
irqreturn_t bcm_vk_msgq_irqhandler(int irq, void *dev_id);
irqreturn_t bcm_vk_notf_irqhandler(int irq, void *dev_id);
irqreturn_t bcm_vk_tty_irqhandler(int irq, void *dev_id);
int bcm_vk_msg_init(struct bcm_vk *vk);
void bcm_vk_msg_remove(struct bcm_vk *vk);
void bcm_vk_drain_msg_on_reset(struct bcm_vk *vk);
int bcm_vk_sync_msgq(struct bcm_vk *vk, bool force_sync);
void bcm_vk_blk_drv_access(struct bcm_vk *vk);
s32 bcm_to_h_msg_dequeue(struct bcm_vk *vk);
int bcm_vk_send_shutdown_msg(struct bcm_vk *vk, u32 shut_type,
			     const pid_t pid, const u32 q_num);
void bcm_to_v_q_doorbell(struct bcm_vk *vk, u32 q_num, u32 db_val);
int bcm_vk_auto_load_all_images(struct bcm_vk *vk);
void bcm_vk_hb_init(struct bcm_vk *vk);
void bcm_vk_hb_deinit(struct bcm_vk *vk);
void bcm_vk_handle_notf(struct bcm_vk *vk);
bool bcm_vk_drv_access_ok(struct bcm_vk *vk);
void bcm_vk_set_host_alert(struct bcm_vk *vk, u32 bit_mask);

#ifdef CONFIG_BCM_VK_TTY
int bcm_vk_tty_init(struct bcm_vk *vk, char *name);
void bcm_vk_tty_exit(struct bcm_vk *vk);
void bcm_vk_tty_terminate_tty_user(struct bcm_vk *vk);
void bcm_vk_tty_wq_exit(struct bcm_vk *vk);

static inline void bcm_vk_tty_set_irq_enabled(struct bcm_vk *vk, int index)
{
	vk->tty[index].irq_enabled = true;
}
#else
static inline int bcm_vk_tty_init(struct bcm_vk *vk, char *name)
{
	return 0;
}

static inline void bcm_vk_tty_exit(struct bcm_vk *vk)
{
}

static inline void bcm_vk_tty_terminate_tty_user(struct bcm_vk *vk)
{
}

static inline void bcm_vk_tty_wq_exit(struct bcm_vk *vk)
{
}

static inline void bcm_vk_tty_set_irq_enabled(struct bcm_vk *vk, int index)
{
}
#endif /* CONFIG_BCM_VK_TTY */

#endif
