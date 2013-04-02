/*
* Filename: rsxx_priv.h
*
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __RSXX_PRIV_H__
#define __RSXX_PRIV_H__

#include <linux/version.h>
#include <linux/semaphore.h>

#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/ioctl.h>

#include "rsxx.h"
#include "rsxx_cfg.h"

struct proc_cmd;

#define PCI_DEVICE_ID_FS70_FLASH	0x04A9
#define PCI_DEVICE_ID_FS80_FLASH	0x04AA

#define RS70_PCI_REV_SUPPORTED	4

#define DRIVER_NAME "rsxx"
#define DRIVER_VERSION "4.0"

/* Block size is 4096 */
#define RSXX_HW_BLK_SHIFT		12
#define RSXX_HW_BLK_SIZE		(1 << RSXX_HW_BLK_SHIFT)
#define RSXX_HW_BLK_MASK		(RSXX_HW_BLK_SIZE - 1)

#define MAX_CREG_DATA8	32
#define LOG_BUF_SIZE8	128

#define RSXX_MAX_OUTSTANDING_CMDS	255
#define RSXX_CS_IDX_MASK		0xff

#define STATUS_BUFFER_SIZE8     4096
#define COMMAND_BUFFER_SIZE8    4096

#define RSXX_MAX_TARGETS	8

struct dma_tracker_list;

/* DMA Command/Status Buffer structure */
struct rsxx_cs_buffer {
	dma_addr_t	dma_addr;
	void		*buf;
	u32		idx;
};

struct rsxx_dma_stats {
	u32 crc_errors;
	u32 hard_errors;
	u32 soft_errors;
	u32 writes_issued;
	u32 writes_failed;
	u32 reads_issued;
	u32 reads_failed;
	u32 reads_retried;
	u32 discards_issued;
	u32 discards_failed;
	u32 done_rescheduled;
	u32 issue_rescheduled;
	u32 dma_sw_err;
	u32 dma_hw_fault;
	u32 dma_cancelled;
	u32 sw_q_depth;		/* Number of DMAs on the SW queue. */
	atomic_t hw_q_depth;	/* Number of DMAs queued to HW. */
};

struct rsxx_dma_ctrl {
	struct rsxx_cardinfo		*card;
	int				id;
	void				__iomem *regmap;
	struct rsxx_cs_buffer		status;
	struct rsxx_cs_buffer		cmd;
	u16				e_cnt;
	spinlock_t			queue_lock;
	struct list_head		queue;
	struct workqueue_struct		*issue_wq;
	struct work_struct		issue_dma_work;
	struct workqueue_struct		*done_wq;
	struct work_struct		dma_done_work;
	struct timer_list		activity_timer;
	struct dma_tracker_list		*trackers;
	struct rsxx_dma_stats		stats;
};

struct rsxx_cardinfo {
	struct pci_dev		*dev;
	unsigned int		halt;
	unsigned int		eeh_state;

	void			__iomem *regmap;
	spinlock_t		irq_lock;
	unsigned int		isr_mask;
	unsigned int		ier_mask;

	struct rsxx_card_cfg	config;
	int			config_valid;

	/* Embedded CPU Communication */
	struct {
		spinlock_t		lock;
		bool			active;
		struct creg_cmd		*active_cmd;
		struct work_struct	done_work;
		struct list_head	queue;
		unsigned int		q_depth;
		/* Cache the creg status to prevent ioreads */
		struct {
			u32		stat;
			u32		failed_cancel_timer;
			u32		creg_timeout;
		} creg_stats;
		struct timer_list	cmd_timer;
		struct mutex		reset_lock;
		int			reset;
	} creg_ctrl;

	struct {
		char tmp[MAX_CREG_DATA8];
		char buf[LOG_BUF_SIZE8]; /* terminated */
		int buf_len;
	} log;

	struct work_struct	event_work;
	unsigned int		state;
	u64			size8;

	/* Lock the device attach/detach function */
	struct mutex		dev_lock;

	/* Block Device Variables */
	bool			bdev_attached;
	int			disk_id;
	int			major;
	struct request_queue	*queue;
	struct gendisk		*gendisk;
	struct {
		/* Used to convert a byte address to a device address. */
		u64 lower_mask;
		u64 upper_shift;
		u64 upper_mask;
		u64 target_mask;
		u64 target_shift;
	} _stripe;
	unsigned int		dma_fault;

	int			scrub_hard;

	int			n_targets;
	struct rsxx_dma_ctrl	*ctrl;
};

enum rsxx_pci_regmap {
	HWID		= 0x00,	/* Hardware Identification Register */
	SCRATCH		= 0x04, /* Scratch/Debug Register */
	RESET		= 0x08, /* Reset Register */
	ISR		= 0x10, /* Interrupt Status Register */
	IER		= 0x14, /* Interrupt Enable Register */
	IPR		= 0x18, /* Interrupt Poll Register */
	CB_ADD_LO	= 0x20, /* Command Host Buffer Address [31:0] */
	CB_ADD_HI	= 0x24, /* Command Host Buffer Address [63:32]*/
	HW_CMD_IDX	= 0x28, /* Hardware Processed Command Index */
	SW_CMD_IDX	= 0x2C, /* Software Processed Command Index */
	SB_ADD_LO	= 0x30, /* Status Host Buffer Address [31:0] */
	SB_ADD_HI	= 0x34, /* Status Host Buffer Address [63:32] */
	HW_STATUS_CNT	= 0x38, /* Hardware Status Counter */
	SW_STATUS_CNT	= 0x3C, /* Deprecated */
	CREG_CMD	= 0x40, /* CPU Command Register */
	CREG_ADD	= 0x44, /* CPU Address Register */
	CREG_CNT	= 0x48, /* CPU Count Register */
	CREG_STAT	= 0x4C, /* CPU Status Register */
	CREG_DATA0	= 0x50, /* CPU Data Registers */
	CREG_DATA1	= 0x54,
	CREG_DATA2	= 0x58,
	CREG_DATA3	= 0x5C,
	CREG_DATA4	= 0x60,
	CREG_DATA5	= 0x64,
	CREG_DATA6	= 0x68,
	CREG_DATA7	= 0x6c,
	INTR_COAL	= 0x70, /* Interrupt Coalescing Register */
	HW_ERROR	= 0x74, /* Card Error Register */
	PCI_DEBUG0	= 0x78, /* PCI Debug Registers */
	PCI_DEBUG1	= 0x7C,
	PCI_DEBUG2	= 0x80,
	PCI_DEBUG3	= 0x84,
	PCI_DEBUG4	= 0x88,
	PCI_DEBUG5	= 0x8C,
	PCI_DEBUG6	= 0x90,
	PCI_DEBUG7	= 0x94,
	PCI_POWER_THROTTLE = 0x98,
	PERF_CTRL	= 0x9c,
	PERF_TIMER_LO	= 0xa0,
	PERF_TIMER_HI	= 0xa4,
	PERF_RD512_LO	= 0xa8,
	PERF_RD512_HI	= 0xac,
	PERF_WR512_LO	= 0xb0,
	PERF_WR512_HI	= 0xb4,
	PCI_RECONFIG	= 0xb8,
};

enum rsxx_intr {
	CR_INTR_DMA0	= 0x00000001,
	CR_INTR_CREG	= 0x00000002,
	CR_INTR_DMA1	= 0x00000004,
	CR_INTR_EVENT	= 0x00000008,
	CR_INTR_DMA2	= 0x00000010,
	CR_INTR_DMA3	= 0x00000020,
	CR_INTR_DMA4	= 0x00000040,
	CR_INTR_DMA5	= 0x00000080,
	CR_INTR_DMA6	= 0x00000100,
	CR_INTR_DMA7	= 0x00000200,
	CR_INTR_ALL_C	= 0x0000003f,
	CR_INTR_ALL_G	= 0x000003ff,
	CR_INTR_DMA_ALL = 0x000003f5,
	CR_INTR_ALL	= 0xffffffff,
};

static inline int CR_INTR_DMA(int N)
{
	static const unsigned int _CR_INTR_DMA[] = {
		CR_INTR_DMA0, CR_INTR_DMA1, CR_INTR_DMA2, CR_INTR_DMA3,
		CR_INTR_DMA4, CR_INTR_DMA5, CR_INTR_DMA6, CR_INTR_DMA7
	};
	return _CR_INTR_DMA[N];
}
enum rsxx_pci_reset {
	DMA_QUEUE_RESET		= 0x00000001,
};

enum rsxx_hw_fifo_flush {
	RSXX_FLUSH_BUSY		= 0x00000002,
	RSXX_FLUSH_TIMEOUT	= 0x00000004,
};

enum rsxx_pci_revision {
	RSXX_DISCARD_SUPPORT = 2,
	RSXX_EEH_SUPPORT     = 3,
};

enum rsxx_creg_cmd {
	CREG_CMD_TAG_MASK	= 0x0000FF00,
	CREG_OP_WRITE		= 0x000000C0,
	CREG_OP_READ		= 0x000000E0,
};

enum rsxx_creg_addr {
	CREG_ADD_CARD_CMD		= 0x80001000,
	CREG_ADD_CARD_STATE		= 0x80001004,
	CREG_ADD_CARD_SIZE		= 0x8000100c,
	CREG_ADD_CAPABILITIES		= 0x80001050,
	CREG_ADD_LOG			= 0x80002000,
	CREG_ADD_NUM_TARGETS		= 0x80003000,
	CREG_ADD_CONFIG			= 0xB0000000,
};

enum rsxx_creg_card_cmd {
	CARD_CMD_STARTUP		= 1,
	CARD_CMD_SHUTDOWN		= 2,
	CARD_CMD_LOW_LEVEL_FORMAT	= 3,
	CARD_CMD_FPGA_RECONFIG_BR	= 4,
	CARD_CMD_FPGA_RECONFIG_MAIN	= 5,
	CARD_CMD_BACKUP			= 6,
	CARD_CMD_RESET			= 7,
	CARD_CMD_deprecated		= 8,
	CARD_CMD_UNINITIALIZE		= 9,
	CARD_CMD_DSTROY_EMERGENCY	= 10,
	CARD_CMD_DSTROY_NORMAL		= 11,
	CARD_CMD_DSTROY_EXTENDED	= 12,
	CARD_CMD_DSTROY_ABORT		= 13,
};

enum rsxx_card_state {
	CARD_STATE_SHUTDOWN		= 0x00000001,
	CARD_STATE_STARTING		= 0x00000002,
	CARD_STATE_FORMATTING		= 0x00000004,
	CARD_STATE_UNINITIALIZED	= 0x00000008,
	CARD_STATE_GOOD			= 0x00000010,
	CARD_STATE_SHUTTING_DOWN	= 0x00000020,
	CARD_STATE_FAULT		= 0x00000040,
	CARD_STATE_RD_ONLY_FAULT	= 0x00000080,
	CARD_STATE_DSTROYING		= 0x00000100,
};

enum rsxx_led {
	LED_DEFAULT	= 0x0,
	LED_IDENTIFY	= 0x1,
	LED_SOAK	= 0x2,
};

enum rsxx_creg_flash_lock {
	CREG_FLASH_LOCK		= 1,
	CREG_FLASH_UNLOCK	= 2,
};

enum rsxx_card_capabilities {
	CARD_CAP_SUBPAGE_WRITES = 0x00000080,
};

enum rsxx_creg_stat {
	CREG_STAT_STATUS_MASK	= 0x00000003,
	CREG_STAT_SUCCESS	= 0x1,
	CREG_STAT_ERROR		= 0x2,
	CREG_STAT_CHAR_PENDING	= 0x00000004, /* Character I/O pending bit */
	CREG_STAT_LOG_PENDING	= 0x00000008, /* HW log message pending bit */
	CREG_STAT_TAG_MASK	= 0x0000ff00,
};

static inline unsigned int CREG_DATA(int N)
{
	return CREG_DATA0 + (N << 2);
}

/*----------------- Convenient Log Wrappers -------------------*/
#define CARD_TO_DEV(__CARD)	(&(__CARD)->dev->dev)

/***** config.c *****/
int rsxx_load_config(struct rsxx_cardinfo *card);

/***** core.c *****/
void rsxx_enable_ier(struct rsxx_cardinfo *card, unsigned int intr);
void rsxx_disable_ier(struct rsxx_cardinfo *card, unsigned int intr);
void rsxx_enable_ier_and_isr(struct rsxx_cardinfo *card,
				 unsigned int intr);
void rsxx_disable_ier_and_isr(struct rsxx_cardinfo *card,
				  unsigned int intr);

/***** dev.c *****/
int rsxx_attach_dev(struct rsxx_cardinfo *card);
void rsxx_detach_dev(struct rsxx_cardinfo *card);
int rsxx_setup_dev(struct rsxx_cardinfo *card);
void rsxx_destroy_dev(struct rsxx_cardinfo *card);
int rsxx_dev_init(void);
void rsxx_dev_cleanup(void);

/***** dma.c ****/
typedef void (*rsxx_dma_cb)(struct rsxx_cardinfo *card,
				void *cb_data,
				unsigned int status);
int rsxx_dma_setup(struct rsxx_cardinfo *card);
void rsxx_dma_destroy(struct rsxx_cardinfo *card);
int rsxx_dma_init(void);
void rsxx_dma_cleanup(void);
void rsxx_dma_queue_reset(struct rsxx_cardinfo *card);
int rsxx_dma_configure(struct rsxx_cardinfo *card);
int rsxx_dma_queue_bio(struct rsxx_cardinfo *card,
			   struct bio *bio,
			   atomic_t *n_dmas,
			   rsxx_dma_cb cb,
			   void *cb_data);
int rsxx_hw_buffers_init(struct pci_dev *dev, struct rsxx_dma_ctrl *ctrl);
int rsxx_eeh_save_issued_dmas(struct rsxx_cardinfo *card);
void rsxx_eeh_cancel_dmas(struct rsxx_cardinfo *card);
int rsxx_eeh_remap_dmas(struct rsxx_cardinfo *card);

/***** cregs.c *****/
int rsxx_creg_write(struct rsxx_cardinfo *card, u32 addr,
			unsigned int size8,
			void *data,
			int byte_stream);
int rsxx_creg_read(struct rsxx_cardinfo *card,
		       u32 addr,
		       unsigned int size8,
		       void *data,
		       int byte_stream);
int rsxx_read_hw_log(struct rsxx_cardinfo *card);
int rsxx_get_card_state(struct rsxx_cardinfo *card,
			    unsigned int *state);
int rsxx_get_card_size8(struct rsxx_cardinfo *card, u64 *size8);
int rsxx_get_num_targets(struct rsxx_cardinfo *card,
			     unsigned int *n_targets);
int rsxx_get_card_capabilities(struct rsxx_cardinfo *card,
				   u32 *capabilities);
int rsxx_issue_card_cmd(struct rsxx_cardinfo *card, u32 cmd);
int rsxx_creg_setup(struct rsxx_cardinfo *card);
void rsxx_creg_destroy(struct rsxx_cardinfo *card);
int rsxx_creg_init(void);
void rsxx_creg_cleanup(void);
int rsxx_reg_access(struct rsxx_cardinfo *card,
			struct rsxx_reg_access __user *ucmd,
			int read);
void rsxx_eeh_save_issued_creg(struct rsxx_cardinfo *card);
void rsxx_kick_creg_queue(struct rsxx_cardinfo *card);



#endif /* __DRIVERS_BLOCK_RSXX_H__ */
