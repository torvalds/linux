/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *  (Based on NXP driver for lpc 31xx)
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 */

#ifndef _DW_MMC_H_
#define _DW_MMC_H_

#include <linux/scatterlist.h>
#include <linux/mmc/core.h>
#include <linux/dmaengine.h>
#include <linux/reset.h>
#include <linux/fault-inject.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

enum dw_mci_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
	STATE_SENDING_CMD11,
	STATE_WAITING_CMD11_DONE,
};

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
};

enum dw_mci_cookie {
	COOKIE_UNMAPPED,
	COOKIE_PRE_MAPPED,	/* mapped by pre_req() of dwmmc */
	COOKIE_MAPPED,		/* mapped by prepare_data() of dwmmc */
};

struct mmc_data;

enum {
	TRANS_MODE_PIO = 0,
	TRANS_MODE_IDMAC,
	TRANS_MODE_EDMAC
};

struct dw_mci_dma_slave {
	struct dma_chan *ch;
	enum dma_transfer_direction direction;
};

/**
 * struct dw_mci - MMC controller state shared between all slots
 * @lock: Spinlock protecting the queue and associated data.
 * @irq_lock: Spinlock protecting the INTMASK setting.
 * @regs: Pointer to MMIO registers.
 * @fifo_reg: Pointer to MMIO registers for data FIFO
 * @sg: Scatterlist entry currently being processed by PIO code, if any.
 * @sg_miter: PIO mapping scatterlist iterator.
 * @mrq: The request currently being processed on @slot,
 *	or NULL if the controller is idle.
 * @cmd: The command currently being sent to the card, or NULL.
 * @data: The data currently being transferred, or NULL if no data
 *	transfer is in progress.
 * @stop_abort: The command currently prepared for stoping transfer.
 * @prev_blksz: The former transfer blksz record.
 * @timing: Record of current ios timing.
 * @use_dma: Which DMA channel is in use for the current transfer, zero
 *	denotes PIO mode.
 * @using_dma: Whether DMA is in use for the current transfer.
 * @dma_64bit_address: Whether DMA supports 64-bit address mode or not.
 * @sg_dma: Bus address of DMA buffer.
 * @sg_cpu: Virtual address of DMA buffer.
 * @dma_ops: Pointer to platform-specific DMA callbacks.
 * @cmd_status: Snapshot of SR taken upon completion of the current
 * @ring_size: Buffer size for idma descriptors.
 *	command. Only valid when EVENT_CMD_COMPLETE is pending.
 * @dms: structure of slave-dma private data.
 * @phy_regs: physical address of controller's register map
 * @data_status: Snapshot of SR taken upon completion of the current
 *	data transfer. Only valid when EVENT_DATA_COMPLETE or
 *	EVENT_DATA_ERROR is pending.
 * @stop_cmdr: Value to be loaded into CMDR when the stop command is
 *	to be sent.
 * @dir_status: Direction of current transfer.
 * @bh_work: Work running the request state machine.
 * @pending_events: Bitmask of events flagged by the interrupt handler
 *	to be processed by bh work.
 * @completed_events: Bitmask of events which the state machine has
 *	processed.
 * @state: BH work state.
 * @queue: List of slots waiting for access to the controller.
 * @bus_hz: The rate of @mck in Hz. This forms the basis for MMC bus
 *	rate and timeout calculations.
 * @current_speed: Configured rate of the controller.
 * @minimum_speed: Stored minimum rate of the controller.
 * @fifoth_val: The value of FIFOTH register.
 * @verid: Denote Version ID.
 * @dev: Device associated with the MMC controller.
 * @pdata: Platform data associated with the MMC controller.
 * @drv_data: Driver specific data for identified variant of the controller
 * @priv: Implementation defined private data.
 * @biu_clk: Pointer to bus interface unit clock instance.
 * @ciu_clk: Pointer to card interface unit clock instance.
 * @slot: Slots sharing this MMC controller.
 * @fifo_depth: depth of FIFO.
 * @data_addr_override: override fifo reg offset with this value.
 * @wm_aligned: force fifo watermark equal with data length in PIO mode.
 *	Set as true if alignment is needed.
 * @data_shift: log2 of FIFO item size.
 * @part_buf_start: Start index in part_buf.
 * @part_buf_count: Bytes of partial data in part_buf.
 * @part_buf: Simple buffer for partial fifo reads/writes.
 * @push_data: Pointer to FIFO push function.
 * @pull_data: Pointer to FIFO pull function.
 * @quirks: Set of quirks that apply to specific versions of the IP.
 * @vqmmc_enabled: Status of vqmmc, should be true or false.
 * @irq_flags: The flags to be passed to request_irq.
 * @irq: The irq value to be passed to request_irq.
 * @sdio_id0: Number of slot0 in the SDIO interrupt registers.
 * @cmd11_timer: Timer for SD3.0 voltage switch over scheme.
 * @cto_timer: Timer for broken command transfer over scheme.
 * @dto_timer: Timer for broken data transfer over scheme.
 *
 * Locking
 * =======
 *
 * @lock is a softirq-safe spinlock protecting @queue as well as
 * @slot, @mrq and @state. These must always be updated
 * at the same time while holding @lock.
 * The @mrq field of struct dw_mci_slot is also protected by @lock,
 * and must always be written at the same time as the slot is added to
 * @queue.
 *
 * @irq_lock is an irq-safe spinlock protecting the INTMASK register
 * to allow the interrupt handler to modify it directly.  Held for only long
 * enough to read-modify-write INTMASK and no other locks are grabbed when
 * holding this one.
 *
 * @pending_events and @completed_events are accessed using atomic bit
 * operations, so they don't need any locking.
 *
 * None of the fields touched by the interrupt handler need any
 * locking. However, ordering is important: Before EVENT_DATA_ERROR or
 * EVENT_DATA_COMPLETE is set in @pending_events, all data-related
 * interrupts must be disabled and @data_status updated with a
 * snapshot of SR. Similarly, before EVENT_CMD_COMPLETE is set, the
 * CMDRDY interrupt must be disabled and @cmd_status updated with a
 * snapshot of SR, and before EVENT_XFER_COMPLETE can be set, the
 * bytes_xfered field of @data must be written. This is ensured by
 * using barriers.
 */
struct dw_mci {
	spinlock_t		lock;
	spinlock_t		irq_lock;
	void __iomem		*regs;
	void __iomem		*fifo_reg;
	u32			data_addr_override;
	bool			wm_aligned;

	struct scatterlist	*sg;
	struct sg_mapping_iter	sg_miter;

	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	struct mmc_command	stop_abort;
	unsigned int		prev_blksz;
	unsigned char		timing;

	/* DMA interface members*/
	int			use_dma;
	int			using_dma;
	int			dma_64bit_address;

	dma_addr_t		sg_dma;
	void			*sg_cpu;
	const struct dw_mci_dma_ops	*dma_ops;
	/* For idmac */
	unsigned int		ring_size;

	/* For edmac */
	struct dw_mci_dma_slave *dms;
	/* Registers's physical base address */
	resource_size_t		phy_regs;

	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;
	u32			dir_status;
	struct work_struct	bh_work;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum dw_mci_state	state;
	struct list_head	queue;

	u32			bus_hz;
	u32			current_speed;
	u32			minimum_speed;
	u32			fifoth_val;
	u16			verid;
	struct device		*dev;
	struct dw_mci_board	*pdata;
	const struct dw_mci_drv_data	*drv_data;
	void			*priv;
	struct clk		*biu_clk;
	struct clk		*ciu_clk;
	struct dw_mci_slot	*slot;

	/* FIFO push and pull */
	int			fifo_depth;
	int			data_shift;
	u8			part_buf_start;
	u8			part_buf_count;
	union {
		u16		part_buf16;
		u32		part_buf32;
		u64		part_buf;
	};
	void (*push_data)(struct dw_mci *host, void *buf, int cnt);
	void (*pull_data)(struct dw_mci *host, void *buf, int cnt);

	u32			quirks;
	bool			vqmmc_enabled;
	unsigned long		irq_flags; /* IRQ flags */
	int			irq;

	int			sdio_id0;

	struct timer_list       cmd11_timer;
	struct timer_list       cto_timer;
	struct timer_list       dto_timer;

#ifdef CONFIG_FAULT_INJECTION
	struct fault_attr	fail_data_crc;
	struct hrtimer		fault_timer;
#endif
};

/* DMA ops for Internal/External DMAC interface */
struct dw_mci_dma_ops {
	/* DMA Ops */
	int (*init)(struct dw_mci *host);
	int (*start)(struct dw_mci *host, unsigned int sg_len);
	void (*complete)(void *host);
	void (*stop)(struct dw_mci *host);
	void (*cleanup)(struct dw_mci *host);
	void (*exit)(struct dw_mci *host);
};

struct dma_pdata;

/* Board platform data */
struct dw_mci_board {
	unsigned int bus_hz; /* Clock speed at the cclk_in pad */

	u32 caps;	/* Capabilities */
	u32 caps2;	/* More capabilities */
	u32 pm_caps;	/* PM capabilities */
	/*
	 * Override fifo depth. If 0, autodetect it from the FIFOTH register,
	 * but note that this may not be reliable after a bootloader has used
	 * it.
	 */
	unsigned int fifo_depth;

	/* delay in mS before detecting cards after interrupt */
	u32 detect_delay_ms;

	struct reset_control *rstc;
	struct dw_mci_dma_ops *dma_ops;
	struct dma_pdata *data;
};

/* Support for longer data read timeout */
#define DW_MMC_QUIRK_EXTENDED_TMOUT            BIT(0)
/* Force 32-bit access to the FIFO */
#define DW_MMC_QUIRK_FIFO64_32                 BIT(1)

#define DW_MMC_240A		0x240a
#define DW_MMC_280A		0x280a

#define SDMMC_CTRL		0x000
#define SDMMC_PWREN		0x004
#define SDMMC_CLKDIV		0x008
#define SDMMC_CLKSRC		0x00c
#define SDMMC_CLKENA		0x010
#define SDMMC_TMOUT		0x014
#define SDMMC_CTYPE		0x018
#define SDMMC_BLKSIZ		0x01c
#define SDMMC_BYTCNT		0x020
#define SDMMC_INTMASK		0x024
#define SDMMC_CMDARG		0x028
#define SDMMC_CMD		0x02c
#define SDMMC_RESP0		0x030
#define SDMMC_RESP1		0x034
#define SDMMC_RESP2		0x038
#define SDMMC_RESP3		0x03c
#define SDMMC_MINTSTS		0x040
#define SDMMC_RINTSTS		0x044
#define SDMMC_STATUS		0x048
#define SDMMC_FIFOTH		0x04c
#define SDMMC_CDETECT		0x050
#define SDMMC_WRTPRT		0x054
#define SDMMC_GPIO		0x058
#define SDMMC_TCBCNT		0x05c
#define SDMMC_TBBCNT		0x060
#define SDMMC_DEBNCE		0x064
#define SDMMC_USRID		0x068
#define SDMMC_VERID		0x06c
#define SDMMC_HCON		0x070
#define SDMMC_UHS_REG		0x074
#define SDMMC_RST_N		0x078
#define SDMMC_BMOD		0x080
#define SDMMC_PLDMND		0x084
#define SDMMC_DBADDR		0x088
#define SDMMC_IDSTS		0x08c
#define SDMMC_IDINTEN		0x090
#define SDMMC_DSCADDR		0x094
#define SDMMC_BUFADDR		0x098
#define SDMMC_CDTHRCTL		0x100
#define SDMMC_UHS_REG_EXT	0x108
#define SDMMC_DDR_REG		0x10c
#define SDMMC_ENABLE_SHIFT	0x110
#define SDMMC_DATA(x)		(x)
/*
 * Registers to support idmac 64-bit address mode
 */
#define SDMMC_DBADDRL		0x088
#define SDMMC_DBADDRU		0x08c
#define SDMMC_IDSTS64		0x090
#define SDMMC_IDINTEN64		0x094
#define SDMMC_DSCADDRL		0x098
#define SDMMC_DSCADDRU		0x09c
#define SDMMC_BUFADDRL		0x0A0
#define SDMMC_BUFADDRU		0x0A4

/*
 * Data offset is difference according to Version
 * Lower than 2.40a : data register offest is 0x100
 */
#define DATA_OFFSET		0x100
#define DATA_240A_OFFSET	0x200

/* shift bit field */
#define _SBF(f, v)		((v) << (f))

/* Control register defines */
#define SDMMC_CTRL_USE_IDMAC		BIT(25)
#define SDMMC_CTRL_CEATA_INT_EN		BIT(11)
#define SDMMC_CTRL_SEND_AS_CCSD		BIT(10)
#define SDMMC_CTRL_SEND_CCSD		BIT(9)
#define SDMMC_CTRL_ABRT_READ_DATA	BIT(8)
#define SDMMC_CTRL_SEND_IRQ_RESP	BIT(7)
#define SDMMC_CTRL_READ_WAIT		BIT(6)
#define SDMMC_CTRL_DMA_ENABLE		BIT(5)
#define SDMMC_CTRL_INT_ENABLE		BIT(4)
#define SDMMC_CTRL_DMA_RESET		BIT(2)
#define SDMMC_CTRL_FIFO_RESET		BIT(1)
#define SDMMC_CTRL_RESET		BIT(0)
/* Clock Enable register defines */
#define SDMMC_CLKEN_LOW_PWR		BIT(16)
#define SDMMC_CLKEN_ENABLE		BIT(0)
/* time-out register defines */
#define SDMMC_TMOUT_DATA(n)		_SBF(8, (n))
#define SDMMC_TMOUT_DATA_MSK		0xFFFFFF00
#define SDMMC_TMOUT_RESP(n)		((n) & 0xFF)
#define SDMMC_TMOUT_RESP_MSK		0xFF
/* card-type register defines */
#define SDMMC_CTYPE_8BIT		BIT(16)
#define SDMMC_CTYPE_4BIT		BIT(0)
#define SDMMC_CTYPE_1BIT		0
/* Interrupt status & mask register defines */
#define SDMMC_INT_SDIO(n)		BIT(16 + (n))
#define SDMMC_INT_EBE			BIT(15)
#define SDMMC_INT_ACD			BIT(14)
#define SDMMC_INT_SBE			BIT(13)
#define SDMMC_INT_HLE			BIT(12)
#define SDMMC_INT_FRUN			BIT(11)
#define SDMMC_INT_HTO			BIT(10)
#define SDMMC_INT_VOLT_SWITCH		BIT(10) /* overloads bit 10! */
#define SDMMC_INT_DRTO			BIT(9)
#define SDMMC_INT_RTO			BIT(8)
#define SDMMC_INT_DCRC			BIT(7)
#define SDMMC_INT_RCRC			BIT(6)
#define SDMMC_INT_RXDR			BIT(5)
#define SDMMC_INT_TXDR			BIT(4)
#define SDMMC_INT_DATA_OVER		BIT(3)
#define SDMMC_INT_CMD_DONE		BIT(2)
#define SDMMC_INT_RESP_ERR		BIT(1)
#define SDMMC_INT_CD			BIT(0)
#define SDMMC_INT_ERROR			0xbfc2
/* Command register defines */
#define SDMMC_CMD_START			BIT(31)
#define SDMMC_CMD_USE_HOLD_REG	BIT(29)
#define SDMMC_CMD_VOLT_SWITCH		BIT(28)
#define SDMMC_CMD_CCS_EXP		BIT(23)
#define SDMMC_CMD_CEATA_RD		BIT(22)
#define SDMMC_CMD_UPD_CLK		BIT(21)
#define SDMMC_CMD_INIT			BIT(15)
#define SDMMC_CMD_STOP			BIT(14)
#define SDMMC_CMD_PRV_DAT_WAIT		BIT(13)
#define SDMMC_CMD_SEND_STOP		BIT(12)
#define SDMMC_CMD_STRM_MODE		BIT(11)
#define SDMMC_CMD_DAT_WR		BIT(10)
#define SDMMC_CMD_DAT_EXP		BIT(9)
#define SDMMC_CMD_RESP_CRC		BIT(8)
#define SDMMC_CMD_RESP_LONG		BIT(7)
#define SDMMC_CMD_RESP_EXP		BIT(6)
#define SDMMC_CMD_INDX(n)		((n) & 0x1F)
/* Status register defines */
#define SDMMC_GET_FCNT(x)		(((x)>>17) & 0x1FFF)
#define SDMMC_STATUS_DMA_REQ		BIT(31)
#define SDMMC_STATUS_BUSY		BIT(9)
/* FIFOTH register defines */
#define SDMMC_SET_FIFOTH(m, r, t)	(((m) & 0x7) << 28 | \
					 ((r) & 0xFFF) << 16 | \
					 ((t) & 0xFFF))
/* HCON register defines */
#define DMA_INTERFACE_IDMA		(0x0)
#define DMA_INTERFACE_DWDMA		(0x1)
#define DMA_INTERFACE_GDMA		(0x2)
#define DMA_INTERFACE_NODMA		(0x3)
#define SDMMC_GET_TRANS_MODE(x)		(((x)>>16) & 0x3)
#define SDMMC_GET_SLOT_NUM(x)		((((x)>>1) & 0x1F) + 1)
#define SDMMC_GET_HDATA_WIDTH(x)	(((x)>>7) & 0x7)
#define SDMMC_GET_ADDR_CONFIG(x)	(((x)>>27) & 0x1)
/* Internal DMAC interrupt defines */
#define SDMMC_IDMAC_INT_AI		BIT(9)
#define SDMMC_IDMAC_INT_NI		BIT(8)
#define SDMMC_IDMAC_INT_CES		BIT(5)
#define SDMMC_IDMAC_INT_DU		BIT(4)
#define SDMMC_IDMAC_INT_FBE		BIT(2)
#define SDMMC_IDMAC_INT_RI		BIT(1)
#define SDMMC_IDMAC_INT_TI		BIT(0)
/* Internal DMAC bus mode bits */
#define SDMMC_IDMAC_ENABLE		BIT(7)
#define SDMMC_IDMAC_FB			BIT(1)
#define SDMMC_IDMAC_SWRESET		BIT(0)
/* H/W reset */
#define SDMMC_RST_HWACTIVE		0x1
/* Version ID register define */
#define SDMMC_GET_VERID(x)		((x) & 0xFFFF)
/* Card read threshold */
#define SDMMC_SET_THLD(v, x)		(((v) & 0xFFF) << 16 | (x))
#define SDMMC_CARD_WR_THR_EN		BIT(2)
#define SDMMC_CARD_RD_THR_EN		BIT(0)
/* UHS-1 register defines */
#define SDMMC_UHS_DDR			BIT(16)
#define SDMMC_UHS_18V			BIT(0)
/* DDR register defines */
#define SDMMC_DDR_HS400			BIT(31)
/* Enable shift register defines */
#define SDMMC_ENABLE_PHASE		BIT(0)
/* All ctrl reset bits */
#define SDMMC_CTRL_ALL_RESET_FLAGS \
	(SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET)

/* FIFO register access macros. These should not change the data endian-ness
 * as they are written to memory to be dealt with by the upper layers
 */
#define mci_fifo_readw(__reg)	__raw_readw(__reg)
#define mci_fifo_readl(__reg)	__raw_readl(__reg)
#define mci_fifo_readq(__reg)	__raw_readq(__reg)

#define mci_fifo_writew(__value, __reg)	__raw_writew(__reg, __value)
#define mci_fifo_writel(__value, __reg)	__raw_writel(__reg, __value)
#define mci_fifo_writeq(__value, __reg)	__raw_writeq(__reg, __value)

/*
 * Some dw_mmc devices have 64-bit FIFOs, but expect them to be
 * accessed using two 32-bit accesses. If such controller is used
 * with a 64-bit kernel, this has to be done explicitly.
 */
static inline u64 mci_fifo_l_readq(void __iomem *addr)
{
	u64 ans;
	u32 proxy[2];

	proxy[0] = mci_fifo_readl(addr);
	proxy[1] = mci_fifo_readl(addr + 4);
	memcpy(&ans, proxy, 8);
	return ans;
}

static inline void mci_fifo_l_writeq(void __iomem *addr, u64 value)
{
	u32 proxy[2];

	memcpy(proxy, &value, 8);
	mci_fifo_writel(addr, proxy[0]);
	mci_fifo_writel(addr + 4, proxy[1]);
}

/* Register access macros */
#define mci_readl(dev, reg)			\
	readl_relaxed((dev)->regs + SDMMC_##reg)
#define mci_writel(dev, reg, value)			\
	writel_relaxed((value), (dev)->regs + SDMMC_##reg)

/* 16-bit FIFO access macros */
#define mci_readw(dev, reg)			\
	readw_relaxed((dev)->regs + SDMMC_##reg)
#define mci_writew(dev, reg, value)			\
	writew_relaxed((value), (dev)->regs + SDMMC_##reg)

/* 64-bit FIFO access macros */
#ifdef readq
#define mci_readq(dev, reg)			\
	readq_relaxed((dev)->regs + SDMMC_##reg)
#define mci_writeq(dev, reg, value)			\
	writeq_relaxed((value), (dev)->regs + SDMMC_##reg)
#else
/*
 * Dummy readq implementation for architectures that don't define it.
 *
 * We would assume that none of these architectures would configure
 * the IP block with a 64bit FIFO width, so this code will never be
 * executed on those machines. Defining these macros here keeps the
 * rest of the code free from ifdefs.
 */
#define mci_readq(dev, reg)			\
	(*(volatile u64 __force *)((dev)->regs + SDMMC_##reg))
#define mci_writeq(dev, reg, value)			\
	(*(volatile u64 __force *)((dev)->regs + SDMMC_##reg) = (value))

#define __raw_writeq(__value, __reg) \
	(*(volatile u64 __force *)(__reg) = (__value))
#define __raw_readq(__reg) (*(volatile u64 __force *)(__reg))
#endif

extern int dw_mci_probe(struct dw_mci *host);
extern void dw_mci_remove(struct dw_mci *host);
#ifdef CONFIG_PM
extern int dw_mci_runtime_suspend(struct device *device);
extern int dw_mci_runtime_resume(struct device *device);
#else
static inline int dw_mci_runtime_suspend(struct device *device) { return -EOPNOTSUPP; }
static inline int dw_mci_runtime_resume(struct device *device) { return -EOPNOTSUPP; }
#endif

/**
 * struct dw_mci_slot - MMC slot state
 * @mmc: The mmc_host representing this slot.
 * @host: The MMC controller this slot is using.
 * @ctype: Card type for this slot.
 * @mrq: mmc_request currently being processed or waiting to be
 *	processed, or NULL when the slot is idle.
 * @queue_node: List node for placing this node in the @queue list of
 *	&struct dw_mci.
 * @clock: Clock rate configured by set_ios(). Protected by host->lock.
 * @__clk_old: The last clock value that was requested from core.
 *	Keeping track of this helps us to avoid spamming the console.
 * @flags: Random state bits associated with the slot.
 * @id: Number of this slot.
 * @sdio_id: Number of this slot in the SDIO interrupt registers.
 */
struct dw_mci_slot {
	struct mmc_host		*mmc;
	struct dw_mci		*host;

	u32			ctype;

	struct mmc_request	*mrq;
	struct list_head	queue_node;

	unsigned int		clock;
	unsigned int		__clk_old;

	unsigned long		flags;
#define DW_MMC_CARD_PRESENT	0
#define DW_MMC_CARD_NEED_INIT	1
#define DW_MMC_CARD_NO_LOW_PWR	2
#define DW_MMC_CARD_NO_USE_HOLD 3
#define DW_MMC_CARD_NEEDS_POLL	4
	int			id;
	int			sdio_id;
};

/**
 * dw_mci driver data - dw-mshc implementation specific driver data.
 * @caps: mmc subsystem specified capabilities of the controller(s).
 * @num_caps: number of capabilities specified by @caps.
 * @common_caps: mmc subsystem specified capabilities applicable to all of
 *	the controllers
 * @init: early implementation specific initialization.
 * @set_ios: handle bus specific extensions.
 * @parse_dt: parse implementation specific device tree properties.
 * @execute_tuning: implementation specific tuning procedure.
 * @set_data_timeout: implementation specific timeout.
 * @get_drto_clks: implementation specific cycle count for data read timeout.
 * @hw_reset: implementation specific HW reset.
 *
 * Provide controller implementation specific extensions. The usage of this
 * data structure is fully optional and usage of each member in this structure
 * is optional as well.
 */
struct dw_mci_drv_data {
	unsigned long	*caps;
	u32		num_caps;
	u32		common_caps;
	int		(*init)(struct dw_mci *host);
	void		(*set_ios)(struct dw_mci *host, struct mmc_ios *ios);
	int		(*parse_dt)(struct dw_mci *host);
	int		(*execute_tuning)(struct dw_mci_slot *slot, u32 opcode);
	int		(*prepare_hs400_tuning)(struct dw_mci *host,
						struct mmc_ios *ios);
	int		(*switch_voltage)(struct mmc_host *mmc,
					  struct mmc_ios *ios);
	void		(*set_data_timeout)(struct dw_mci *host,
					  unsigned int timeout_ns);
	u32		(*get_drto_clks)(struct dw_mci *host);
	void		(*hw_reset)(struct dw_mci *host);
};
#endif /* _DW_MMC_H_ */
