/*
* linux/drivers/mmc/host/mshci.h
* Mobile Storage Host Controller Interface driver
*
* Copyright (c) 2011 Samsung Electronics Co., Ltd.
*		http://www.samsung.com
*
* Based on linux/drivers/mmc/host/sdhci.h
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
*/

#include <linux/scatterlist.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/io.h>

/*
 * Controller registers
 */
/*****************************************************/
/*      MSHC Internal Registers       */
/*****************************************************/

#define MSHCI_CTRL	0x00	/* Control */
#define MSHCI_PWREN	0x04	/* Power-enable */
#define MSHCI_CLKDIV	0x08	/* Clock divider */
#define MSHCI_CLKSRC	0x0C	/* Clock source */
#define MSHCI_CLKENA	0x10	/* Clock enable */
#define MSHCI_TMOUT	0x14	/* Timeout */
#define MSHCI_CTYPE	0x18	/* Card type */
#define MSHCI_BLKSIZ	0x1C	/* Block Size */
#define MSHCI_BYTCNT	0x20	/* Byte count */
#define MSHCI_INTMSK	0x24	/* Interrupt Mask */
#define MSHCI_CMDARG	0x28	/* Command Argument */
#define MSHCI_CMD	0x2C	/* Command */
#define MSHCI_RESP0	0x30	/* Response 0 */
#define MSHCI_RESP1	0x34	/* Response 1 */
#define MSHCI_RESP2	0x38	/* Response 2 */
#define MSHCI_RESP3	0x3C	/* Response 3 */
#define MSHCI_MINTSTS	0x40	/* Masked interrupt status */
#define MSHCI_RINTSTS	0x44	/* Raw interrupt status */
#define MSHCI_STATUS	0x48	/* Status */
#define MSHCI_FIFOTH	0x4C	/* FIFO threshold */
#define MSHCI_CDETECT	0x50	/* Card detect */
#define MSHCI_WRTPRT	0x54	/* Write protect */
#define MSHCI_GPIO	0x58	/* General Purpose IO */
#define MSHCI_TCBCNT	0x5C	/* Transferred CIU byte count */
#define MSHCI_TBBCNT	0x60	/* Transferred host/DMA to/from byte count */
#define MSHCI_DEBNCE	0x64	/* Card detect debounce */
#define MSHCI_USRID	0x68	/* User ID */
#define MSHCI_VERID	0x6C	/* Version ID */
#define MSHCI_HCON	0x70	/* Hardware Configuration */
#define MSHCI_UHS_REG	0x74	/* UHS and DDR setting */
#define MSHCI_BMOD	0x80    /* Bus mode register */
#define MSHCI_PLDMND	0x84    /* Poll demand */
#define MSHCI_DBADDR	0x88    /* Descriptor list base address */
#define MSHCI_IDSTS	0x8C    /* Internal DMAC status */
#define MSHCI_IDINTEN	0x90    /* Internal DMAC interrupt enable */
#define MSHCI_DSCADDR	0x94    /* Current host descriptor address */
#define MSHCI_BUFADDR	0x98    /* Current host buffer address */
#define MSHCI_CLKSEL	0x9C    /* Clock Selection Register */
#define MSHCI_WAKEUPCON	0xA0    /* Wakeup control register */
#define MSHCI_CLOCKCON	0xA4    /* Clock (delay) control register */
#define MSHCI_FIFODAT	0x100	/* FIFO data read write */

/*****************************************************
 *  Control Register  Register
 *  MSHCI_CTRL - offset 0x00
 *****************************************************/

#define CTRL_RESET	(0x1<<0) /* Reset DWC_mobile_storage controller */
#define FIFO_RESET	(0x1<<1) /* Reset FIFO */
#define DMA_RESET	(0x1<<2) /* Reset DMA interface */
#define INT_ENABLE	(0x1<<4) /* Global interrupt enable/disable bit */
#define DMA_ENABLE	(0x1<<5) /* DMA transfer mode enable/disable bit */
#define READ_WAIT	(0x1<<6) /* For sending read-wait to SDIO cards */
#define SEND_IRQ_RESP	(0x1<<7) /* Send auto IRQ response */
#define ABRT_READ_DATA	(0x1<<8)
#define SEND_CCSD	(0x1<<9)
#define SEND_AS_CCSD	(0x1<<10)
#define CEATA_INTSTAT	(0x1<<11)
#define CARD_VOLA	(0xF<<16)
#define CARD_VOLB	(0xF<<20)
#define ENABLE_OD_PULLUP (0x1<<24)
#define ENABLE_IDMAC    (0x1<<25)

#define MSHCI_RESET_ALL (0x1)

/*****************************************************
 *  Power Enable Register
 *  MSHCI_PWREN - offset 0x04
 *****************************************************/
#define POWER_ENABLE	(0x1<<0)

/*****************************************************
 *  Clock Divider Register
 *  MSHCI_CLKDIV - offset 0x08
 *****************************************************/
#define CLK_DIVIDER0	(0xFF<<0)
#define CLK_DIVIDER1	(0xFF<<8)
#define	CLK_DIVIDER2	(0xFF<<16)
#define CLK_DIVIDER3	(0xFF<<24)

/*****************************************************
 *  Clock Enable Register
 *  MSHCI_CLKENA - offset 0x10
 *****************************************************/
#define CLK_SDMMC_MAX	(48000000) /* 96Mhz. it SHOULDBE optimized */
#define CLK_ENABLE		(0x1<<0)
#define CLK_DISABLE		(0x0<<0)

/*****************************************************
 *  Timeout  Register
 *  MSHCI_TMOUT - offset 0x14
 *****************************************************/
#define RSP_TIMEOUT	(0xFF<<0)
#define DATA_TIMEOUT	(0xFFFFFF<<8)

/*****************************************************
 *  Card Type Register
 *  MSHCI_CTYPE - offset 0x18
 *****************************************************/
#define CARD_WIDTH4	(0xFFFF<<0)
#define CARD_WIDTH8	(0xFFFF<<16)

/*****************************************************
 *  Block Size Register
 *  MSHCI_BLKSIZ - offset 0x1C
 *****************************************************/
#define BLK_SIZ		(0xFFFF<<0)

/*****************************************************
 *  Interrupt Mask Register
 *  MSHCI_INTMSK - offset 0x24
 *****************************************************/
#define INT_MASK	(0xFFFF<<0)
#define	SDIO_INT_MASK	(0xFFFF<<16)
#define SDIO_INT_ENABLE (0x1<<16)

/* interrupt bits */
#define INTMSK_ALL	0xFFFFFFFF
#define INTMSK_CDETECT	(0x1<<0)
#define INTMSK_RE	(0x1<<1)
#define INTMSK_CDONE	(0x1<<2)
#define INTMSK_DTO	(0x1<<3)
#define INTMSK_TXDR	(0x1<<4)
#define INTMSK_RXDR	(0x1<<5)
#define INTMSK_RCRC	(0x1<<6)
#define INTMSK_DCRC	(0x1<<7)
#define INTMSK_RTO	(0x1<<8)
#define INTMSK_DRTO	(0x1<<9)
#define INTMSK_HTO	(0x1<<10)
#define INTMSK_FRUN	(0x1<<11)
#define INTMSK_HLE	(0x1<<12)
#define INTMSK_SBE	(0x1<<13)
#define INTMSK_ACD	(0x1<<14)
#define INTMSK_EBE	(0x1<<15)
#define INTMSK_DMA	(INTMSK_ACD|INTMSK_RXDR|INTMSK_TXDR)

#define INT_SRC_IDMAC   (0x0)
#define INT_SRC_MINT    (0x1)


/*****************************************************
 *  Command Register
 *  MSHCI_CMD - offset 0x2C
 *****************************************************/

#define CMD_RESP_EXP_BIT	(0x1<<6)
#define CMD_RESP_LENGTH_BIT	(0x1<<7)
#define CMD_CHECK_CRC_BIT	(0x1<<8)
#define CMD_DATA_EXP_BIT	(0x1<<9)
#define CMD_RW_BIT		(0x1<<10)
#define CMD_TRANSMODE_BIT	(0x1<<11)
#define CMD_SENT_AUTO_STOP_BIT	(0x1<<12)
#define CMD_WAIT_PRV_DAT_BIT	(0x1<<13)
#define CMD_ABRT_CMD_BIT	(0x1<<14)
#define CMD_SEND_INIT_BIT	(0x1<<15)
#define CMD_CARD_NUM_BITS	(0x1F<<16)
#define CMD_SEND_CLK_ONLY	(0x1<<21)
#define CMD_READ_CEATA		(0x1<<22)
#define CMD_CCS_EXPECTED	(0x1<<23)
#define CMD_USE_HOLD_REG	(0x1<<29)
#define CMD_STRT_BIT		(0x1<<31)
#define CMD_ONLY_CLK		(CMD_STRT_BIT | CMD_SEND_CLK_ONLY | \
						CMD_WAIT_PRV_DAT_BIT)

/*****************************************************
 *  Masked Interrupt Status Register
 *  MSHCI_MINTSTS - offset 0x40
 *****************************************************/
/*****************************************************
 *  Raw Interrupt Register
 *  MSHCI_RINTSTS - offset 0x44
 *****************************************************/
#define INT_STATUS	(0xFFFF<<0)
#define SDIO_INTR	(0xFFFF<<16)
#define DATA_ERR	(INTMSK_EBE|INTMSK_SBE|INTMSK_HLE|INTMSK_FRUN|\
					INTMSK_EBE|INTMSK_DCRC)
#define DATA_TOUT	(INTMSK_HTO|INTMSK_DRTO)
#define DATA_STATUS	(DATA_ERR|DATA_TOUT|INTMSK_RXDR|INTMSK_TXDR|INTMSK_DTO)
#define CMD_STATUS	(INTMSK_RTO|INTMSK_RCRC|INTMSK_CDONE|INTMSK_RE)
#define CMD_ERROR       (INTMSK_RCRC|INTMSK_RTO|INTMSK_RE)

/*****************************************************
 *  Status Register
 *  MSHCI_STATUS - offset 0x48
 *****************************************************/
#define FIFO_RXWTRMARK	(0x1<<0)
#define FIFO_TXWTRMARK	(0x1<<1)
#define FIFO_EMPTY	(0x1<<2)
#define FIFO_FULL	(0x1<<3)
#define CMD_FSMSTAT	(0xF<<4)
#define	DATA_3STATUS	(0x1<<8)
#define DATA_BUSY	(0x1<<9)
#define DATA_MCBUSY	(0x1<<10)
#define RSP_INDEX	(0x3F<<11)
#define FIFO_COUNT	(0x1FFF<<17)
#define	DMA_ACK		(0x1<<30)
#define	DMA_REQ		(0x1<<31)
#define FIFO_WIDTH	(0x4)
#define FIFO_DEPTH	(0x20)

/*Command FSM status */
#define FSM_IDLE		(0<<4)
#define FSM_SEND_INIT_SEQ	(1<<4)
#define FSM_TX_CMD_STARTBIT	(2<<4)
#define FSM_TX_CMD_TXBIT	(3<<4)
#define	FSM_TX_CMD_INDEX_ARG	(4<<4)
#define	FSM_TX_CMD_CRC7		(5<<4)
#define FSM_TX_CMD_ENDBIT	(6<<4)
#define	FSM_RX_RESP_STARTBIT	(7<<4)
#define FSM_RX_RESP_IRQRESP	(8<<4)
#define FSM_RX_RESP_TXBIT	(9<<4)
#define FSM_RX_RESP_CMDIDX	(10<<4)
#define FSM_RX_RESP_DATA	(11<<4)
#define FSM_RX_RESP_CRC7	(12<<4)
#define FSM_RX_RESP_ENDBIT	(13<<4)
#define FSM_CMD_PATHWAITNCC	(14<<4)
#define FSM_WAIT		(15<<4)

/*****************************************************
 *  FIFO Threshold Watermark Register
 *  MSHCI_FIFOTH - offset 0x4C
 *****************************************************/
#define TX_WMARK		(0xFFF<<0)
#define RX_WMARK		(0xFFF<<16)
#define MSIZE_MASK		(0x7<<28)

/* DW DMA Mutiple Transaction Size */
#define MSIZE_1			(0<<28)
#define MSIZE_4			(1<<28)
#define MSIZE_8			(2<<28)
#define MSIZE_16		(3<<28)
#define MSIZE_32		(4<<28)
#define MSIZE_64		(5<<28)
#define MSIZE_128		(6<<28)
#define MSIZE_256		(7<<28)

/*****************************************************
 *  FIFO Threshold Watermark Register
 *  MSHCI_FIFOTH - offset 0x4C
 *****************************************************/
#define GPI			(0xFF<<0)
#define	GPO			(0xFFFF<<8)


/*****************************************************
 *  Card Detect Register
 *  MSHCI_CDETECT - offset 0x50
 *  It assumes there is only one SD slot
 *****************************************************/
#define CARD_PRESENT		(0x1<<0)

/*****************************************************
 *  Write Protect Register
 *  MSHCI_WRTPRT - offset 0x54
 *  It assumes there is only one SD slot
 *****************************************************/
#define WRTPRT_ON		(0x1<<0)

/*****************************************************
 *  Bus Mode Register
 *  MSHCI_BMOD - offset 0x80
 *****************************************************/
#define BMOD_IDMAC_RESET        (0x1<<0)
#define BMOD_IDMAC_FB           (0x1<<1)
#define BMOD_IDMAC_ENABLE       (0x1<<7)

/*****************************************************
 *  Hardware Configuration  Register
 *  MSHCI_HCON - offset 0x70
 *****************************************************/
#define CARD_TYPE		(0x1<<0)
#define NUM_CARDS		(0x1F<<1)
#define H_BUS_TYPE		(0x1<<6)
#define H_DATA_WIDTH		(0x7<<7)
#define H_ADDR_WIDTH		(0x3F<<10)
#define DMA_INTERFACE		(0x3<<16)
#define	GE_DMA_DATA_WIDTH	(0x7<<18)
#define FIFO_RAM_INSIDE		(0x1<<21)
#define UMPLEMENT_HOLD_REG	(0x1<<22)
#define SET_CLK_FALSE_PATH	(0x1<<23)
#define NUM_CLK_DIVIDER		(0x3<<24)

/*****************************************************
 *  Hardware Configuration  Register
 *  MSHCI_IDSTS - offset 0x8c
 *****************************************************/
#define IDSTS_FSM               (0xf<<13)
#define IDSTS_EB                (0x7<<10)
#define IDSTS_AIS               (0x1<<9)
#define IDSTS_NIS               (0x1<<8)
#define IDSTS_CES               (0x1<<5)
#define IDSTS_DU                (0x1<<4)
#define IDSTS_FBE               (0x1<<2)
#define IDSTS_RI                (0x1<<1)
#define IDSTS_TI                (0x1<<0)

struct mshci_ops;

struct mshci_idmac {
	u32	des0;
	u32	des1;
	u32	des2;
	u32	des3;
#define MSHCI_IDMAC_OWN         (1<<31)
#define MSHCI_IDMAC_ER          (1<<5)
#define MSHCI_IDMAC_CH          (1<<4)
#define MSHCI_IDMAC_FS          (1<<3)
#define MSHCI_IDMAC_LD          (1<<2)
#define MSHCI_IDMAC_DIC         (1<<1)
#define INTMSK_IDMAC_ALL        (0x337)
#define INTMSK_IDMAC_ERROR      (0x214)
};

struct mshci_host {
	/* Data set by hardware interface driver */
	const char		*hw_name;	/* Hardware bus name */

	unsigned int		quirks;		/* Deviations from spec. */
/* Controller has no write-protect pin connected with SD card */
#define MSHCI_QUIRK_NO_WP_BIT                           (1<<0)
#define MSHCI_QUIRK_BROKEN_CARD_DETECTION               (1<<1)
#define MSHCI_QUIRK_BROKEN_PRESENT_BIT					(1<<2)

	int			irq;		/* Device IRQ */
	void __iomem		*ioaddr;	/* Mapped address */

	const struct mshci_ops	*ops;		/* Low level hw interface */

	/* Internal data */
	struct mmc_host		*mmc;		/* MMC structure */
	u64			dma_mask;	/* custom DMA mask */

	spinlock_t		lock;		/* Mutex */

	int			flags;		/* Host attributes */
#define MSHCI_USE_IDMA		(1<<1)		/* Host is ADMA capable */
#define MSHCI_REQ_USE_DMA	(1<<2)		/* Use DMA for this req. */
#define MSHCI_DEVICE_DEAD	(1<<3)		/* Device unresponsive */

	unsigned int		version;	/* SDHCI spec. version */

	unsigned int		max_clk;	/* Max possible freq (MHz) */
	unsigned int		timeout_clk;	/* Timeout freq (KHz) */

	unsigned int		clock;		/* Current clock (MHz) */
	unsigned int		clock_to_restore; /* Saved clock for dynamic clock gating (MHz) */
	u8			pwr;		/* Current voltage */

	struct mmc_request	*mrq;		/* Current request */
	struct mmc_command	*cmd;		/* Current command */
	struct mmc_data		*data;		/* Current data request */
	unsigned int		data_early:1;	/* Data finished before cmd */

	struct sg_mapping_iter	sg_miter;	/* SG state for PIO */
	unsigned int		blocks;		/* remaining PIO blocks */

	int			sg_count;	/* Mapped sg entries */

	u8			*idma_desc;	/* ADMA descriptor table */
	u8			*align_buffer;	/* Bounce buffer */

	dma_addr_t		idma_addr;	/* Mapped ADMA descr. table */
	dma_addr_t		align_addr;	/* Mapped bounce buffer */

	struct tasklet_struct	card_tasklet;	/* Tasklet structures */
	struct tasklet_struct	finish_tasklet;

	struct timer_list	timer;		/* Timer for timeouts */

	u32			fifo_depth;
	u32			fifo_threshold;
	u32			data_transfered;

	/* IP version control */
	u32			data_addr;
	u32			hold_bit;

	u32			error_state;

	unsigned long		sl_flags;
	unsigned long		private[0] ____cacheline_aligned;
};

struct mshci_ops {
	void	(*set_clock)(struct mshci_host *host, unsigned int clock);

	int		(*enable_dma)(struct mshci_host *host);
	unsigned int	(*get_max_clock)(struct mshci_host *host);
	unsigned int	(*get_min_clock)(struct mshci_host *host);
	unsigned int	(*get_timeout_clock)(struct mshci_host *host);
	void            (*set_ios)(struct mshci_host *host,
				   struct mmc_ios *ios);
	int             (*get_ro) (struct mmc_host *mmc);
	void		(*init_issue_cmd)(struct mshci_host *host);
	void		(*init_card)(struct mshci_host *host);

	int		(*dma_map_sg)(struct mshci_host *host,
				struct device *dev,
				struct scatterlist *sg,
				int nents, enum dma_data_direction dir,
				int flush_type);
	void		(*dma_unmap_sg)(struct mshci_host *host,
				struct device *dev,
				struct scatterlist *sg,
				int nents, enum dma_data_direction dir,
				int flush_type);
	int		(*get_fifo_depth)(struct mshci_host *host);
};

static inline void mshci_writel(struct mshci_host *host, u32 val, int reg)
{
	__raw_writel(val, host->ioaddr + reg);
}

static inline u32 mshci_readl(struct mshci_host *host, int reg)
{
	return readl(host->ioaddr + reg);
}

extern struct mshci_host *mshci_alloc_host(struct device *dev,
	size_t priv_size);
extern void mshci_free_host(struct mshci_host *host);

static inline void *mshci_priv(struct mshci_host *host)
{
	return (void *)host->private;
}

extern int mshci_add_host(struct mshci_host *host);
extern void mshci_remove_host(struct mshci_host *host, int dead);

#ifdef CONFIG_PM
extern int mshci_suspend_host(struct mshci_host *host, pm_message_t state);
extern int mshci_resume_host(struct mshci_host *host);
#endif
