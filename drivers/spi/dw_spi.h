#ifndef DW_SPI_HEADER_H
#define DW_SPI_HEADER_H

#include <linux/io.h>
#include <linux/scatterlist.h>

/* Bit fields in CTRLR0 */
#define SPI_DFS_OFFSET			0

#define SPI_FRF_OFFSET			4
#define SPI_FRF_SPI			0x0
#define SPI_FRF_SSP			0x1
#define SPI_FRF_MICROWIRE		0x2
#define SPI_FRF_RESV			0x3

#define SPI_MODE_OFFSET			6
#define SPI_SCPH_OFFSET			6
#define SPI_SCOL_OFFSET			7

#define SPI_TMOD_OFFSET			8
#define SPI_TMOD_MASK			(0x3 << SPI_TMOD_OFFSET)
#define	SPI_TMOD_TR			0x0		/* xmit & recv */
#define SPI_TMOD_TO			0x1		/* xmit only */
#define SPI_TMOD_RO			0x2		/* recv only */
#define SPI_TMOD_EPROMREAD		0x3		/* eeprom read mode */

#define SPI_SLVOE_OFFSET		10
#define SPI_SRL_OFFSET			11
#define SPI_CFS_OFFSET			12

/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_NOT_FULL			(1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_NOT_EMPT			(1 << 3)
#define SR_RF_FULL			(1 << 4)
#define SR_TX_ERR			(1 << 5)
#define SR_DCOL				(1 << 6)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define SPI_INT_TXEI			(1 << 0)
#define SPI_INT_TXOI			(1 << 1)
#define SPI_INT_RXUI			(1 << 2)
#define SPI_INT_RXOI			(1 << 3)
#define SPI_INT_RXFI			(1 << 4)
#define SPI_INT_MSTI			(1 << 5)

/* TX RX interrupt level threshold, max can be 256 */
#define SPI_INT_THRESHOLD		32

enum dw_ssi_type {
	SSI_MOTO_SPI = 0,
	SSI_TI_SSP,
	SSI_NS_MICROWIRE,
};

struct dw_spi_reg {
	u32	ctrl0;
	u32	ctrl1;
	u32	ssienr;
	u32	mwcr;
	u32	ser;
	u32	baudr;
	u32	txfltr;
	u32	rxfltr;
	u32	txflr;
	u32	rxflr;
	u32	sr;
	u32	imr;
	u32	isr;
	u32	risr;
	u32	txoicr;
	u32	rxoicr;
	u32	rxuicr;
	u32	msticr;
	u32	icr;
	u32	dmacr;
	u32	dmatdlr;
	u32	dmardlr;
	u32	idr;
	u32	version;
	u32	dr;		/* Currently oper as 32 bits,
				though only low 16 bits matters */
} __packed;

struct dw_spi;
struct dw_spi_dma_ops {
	int (*dma_init)(struct dw_spi *dws);
	void (*dma_exit)(struct dw_spi *dws);
	int (*dma_transfer)(struct dw_spi *dws, int cs_change);
};

struct dw_spi {
	struct spi_master	*master;
	struct spi_device	*cur_dev;
	struct device		*parent_dev;
	enum dw_ssi_type	type;

	void __iomem		*regs;
	unsigned long		paddr;
	u32			iolen;
	int			irq;
	u32			fifo_len;	/* depth of the FIFO buffer */
	u32			max_freq;	/* max bus freq supported */

	u16			bus_num;
	u16			num_cs;		/* supported slave numbers */

	/* Driver message queue */
	struct workqueue_struct	*workqueue;
	struct work_struct	pump_messages;
	spinlock_t		lock;
	struct list_head	queue;
	int			busy;
	int			run;

	/* Message Transfer pump */
	struct tasklet_struct	pump_transfers;

	/* Current message transfer state info */
	struct spi_message	*cur_msg;
	struct spi_transfer	*cur_transfer;
	struct chip_data	*cur_chip;
	struct chip_data	*prev_chip;
	size_t			len;
	void			*tx;
	void			*tx_end;
	void			*rx;
	void			*rx_end;
	int			dma_mapped;
	dma_addr_t		rx_dma;
	dma_addr_t		tx_dma;
	size_t			rx_map_len;
	size_t			tx_map_len;
	u8			n_bytes;	/* current is a 1/2 bytes op */
	u8			max_bits_per_word;	/* maxim is 16b */
	u32			dma_width;
	int			cs_change;
	int			(*write)(struct dw_spi *dws);
	int			(*read)(struct dw_spi *dws);
	irqreturn_t		(*transfer_handler)(struct dw_spi *dws);
	void			(*cs_control)(u32 command);

	/* Dma info */
	int			dma_inited;
	struct dma_chan		*txchan;
	struct scatterlist	tx_sgl;
	struct dma_chan		*rxchan;
	struct scatterlist	rx_sgl;
	int			dma_chan_done;
	struct device		*dma_dev;
	dma_addr_t		dma_addr; /* phy address of the Data register */
	struct dw_spi_dma_ops	*dma_ops;
	void			*dma_priv; /* platform relate info */
	struct pci_dev		*dmac;

	/* Bus interface info */
	void			*priv;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
};

#define dw_readl(dw, name) \
	__raw_readl(&(((struct dw_spi_reg *)dw->regs)->name))
#define dw_writel(dw, name, val) \
	__raw_writel((val), &(((struct dw_spi_reg *)dw->regs)->name))
#define dw_readw(dw, name) \
	__raw_readw(&(((struct dw_spi_reg *)dw->regs)->name))
#define dw_writew(dw, name, val) \
	__raw_writew((val), &(((struct dw_spi_reg *)dw->regs)->name))

static inline void spi_enable_chip(struct dw_spi *dws, int enable)
{
	dw_writel(dws, ssienr, (enable ? 1 : 0));
}

static inline void spi_set_clk(struct dw_spi *dws, u16 div)
{
	dw_writel(dws, baudr, div);
}

static inline void spi_chip_sel(struct dw_spi *dws, u16 cs)
{
	if (cs > dws->num_cs)
		return;

	if (dws->cs_control)
		dws->cs_control(1);

	dw_writel(dws, ser, 1 << cs);
}

/* Disable IRQ bits */
static inline void spi_mask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, imr) & ~mask;
	dw_writel(dws, imr, new_mask);
}

/* Enable IRQ bits */
static inline void spi_umask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, imr) | mask;
	dw_writel(dws, imr, new_mask);
}

/*
 * Each SPI slave device to work with dw_api controller should
 * has such a structure claiming its working mode (PIO/DMA etc),
 * which can be save in the "controller_data" member of the
 * struct spi_device
 */
struct dw_spi_chip {
	u8 poll_mode;	/* 0 for contoller polling mode */
	u8 type;	/* SPI/SSP/Micrwire */
	u8 enable_dma;
	void (*cs_control)(u32 command);
};

extern int dw_spi_add_host(struct dw_spi *dws);
extern void dw_spi_remove_host(struct dw_spi *dws);
extern int dw_spi_suspend_host(struct dw_spi *dws);
extern int dw_spi_resume_host(struct dw_spi *dws);
extern void dw_spi_xfer_done(struct dw_spi *dws);

/* platform related setup */
extern int dw_spi_mid_init(struct dw_spi *dws); /* Intel MID platforms */
#endif /* DW_SPI_HEADER_H */
