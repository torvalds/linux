#ifndef DW_SPI_HEADER_H
#define DW_SPI_HEADER_H

#include <linux/io.h>
#include <linux/scatterlist.h>
#include <linux/dmaengine.h>


#if 1
#define DBG_SPI(x...)  if(atomic_read(&dws->debug_flag) == 1) printk(KERN_DEBUG x)
#else
#define DBG_SPI(x...)
#endif

/* SPI register offsets */
#define SPIM_CTRLR0				0x0000
#define SPIM_CTRLR1				0x0004
#define SPIM_SSIENR				0x0008
#define SPIM_SER				0x000c
#define SPIM_BAUDR				0x0010
#define SPIM_TXFTLR				0x0014
#define SPIM_RXFTLR				0x0018
#define SPIM_TXFLR				0x001c
#define SPIM_RXFLR				0x0020
#define SPIM_SR					0x0024
#define SPIM_IPR                0x0028
#define SPIM_IMR				0x002c
#define SPIM_ISR				0x0030
#define SPIM_RISR				0x0034
#define SPIM_ICR				0x0038
#define SPIM_DMACR				0x003c
#define SPIM_DMATDLR			0x0040
#define SPIM_DMARDLR			0x0044
#define SPIM_TXDR				0x0400
#define SPIM_RXDR               0x0800

/* --------Bit fields in CTRLR0--------begin */

#define SPI_DFS_OFFSET			0                  /* Data Frame Size */
#define SPI_DFS_4BIT            0x00
#define SPI_DFS_8BIT            0x01
#define SPI_DFS_16BIT           0x02
#define SPI_DFS_RESV            0x03

#define SPI_FRF_OFFSET			16                 /* Frame Format */
#define SPI_FRF_SPI			    0x00               /* motorola spi */
#define SPI_FRF_SSP			    0x01               /* Texas Instruments SSP*/
#define SPI_FRF_MICROWIRE		0x02               /*  National Semiconductors Microwire */
#define SPI_FRF_RESV			0x03

#define SPI_MODE_OFFSET		    6                 /* SCPH & SCOL */

#define SPI_SCPH_OFFSET			6                  /* Serial Clock Phase */
#define SPI_SCPH_TOGMID         0                  /* Serial clock toggles in middle of first data bit */
#define SPI_SCPH_TOGSTA         1                  /* Serial clock toggles at start of first data bit */

#define SPI_SCOL_OFFSET			7                  /* Serial Clock Polarity */

#define SPI_OPMOD_OFFSET	    20
#define SPI_OPMOD_MASTER        0
#define SPI_OPMOD_SLAVE         1

#define SPI_TMOD_OFFSET			18                 /* Transfer Mode */
//#define SPI_TMOD_MASK			(0x3 << SPI_TMOD_OFFSET)
#define	SPI_TMOD_TR			    0x00		       /* xmit & recv */
#define SPI_TMOD_TO			    0x01		       /* xmit only */
#define SPI_TMOD_RO			    0x02		       /* recv only */
#define SPI_TMOD_RESV		    0x03

#define SPI_CFS_OFFSET			2                  /* Control Frame Size */

#define SPI_CSM_OFFSET          8                  /* Chip Select Mode */
#define SPI_CSM_KEEP            0x00               /* ss_n keep low after every frame data is transferred */
#define SPI_CSM_HALF            0x01               /* ss_n be high for half sclk_out cycles after every frame data is transferred */
#define SPI_CSM_ONE             0x02               /* ss_n be high for one sclk_out cycle after every frame data is transferred */

#define SPI_SSN_DELAY_OFFSET    10
#define SPI_SSN_DELAY_HALF      0x00
#define SPI_SSN_DELAY_ONE       0x01

#define SPI_HALF_WORLD_TX_OFFSET       13
#define SPI_HALF_WORLD_ON       0x00
#define SPI_HALF_WORLD_OFF      0x01


/* --------Bit fields in CTRLR0--------end */


/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_FULL		    (1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_EMPT		    (1 << 3)
#define SR_RF_FULL			(1 << 4)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define SPI_INT_TXEI			(1 << 0)
#define SPI_INT_TXOI			(1 << 1)
#define SPI_INT_RXUI			(1 << 2)
#define SPI_INT_RXOI			(1 << 3)
#define SPI_INT_RXFI			(1 << 4)

/* Bit fields in DMACR */
#define SPI_DMACR_TX_ENABLE     (1 << 1)
#define SPI_DMACR_RX_ENABLE     (1 << 0)

/* Bit fields in ICR */
#define SPI_CLEAR_INT_ALL       (1<< 0)
#define SPI_CLEAR_INT_RXUI      (1 << 1)
#define SPI_CLEAR_INT_RXOI      (1 << 2)
#define SPI_CLEAR_INT_TXOI      (1 << 3)

#define SUSPND    (1<<0)
#define SPIBUSY   (1<<1)
#define RXBUSY    (1<<2)
#define TXBUSY    (1<<3)


enum dw_ssi_type {
	SSI_MOTO_SPI = 0,
	SSI_TI_SSP,
	SSI_NS_MICROWIRE,
};

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
	char			name[16];

	struct clk          *clk_spi;
	struct clk          *pclk_spi;

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
	int				dma_mapped;
	dma_addr_t		rx_dma;
	dma_addr_t		tx_dma;
	size_t			rx_map_len;
	size_t			tx_map_len;
	u8			n_bytes;	/* current is a 1/2 bytes op */
	u8			max_bits_per_word;	/* maxim is 16b */
	u32			dma_width;
	int			dmatdlr;
	int			dmardlr;
	int			cs_change;
	void			*tx_buffer;
	void			*rx_buffer;
	dma_addr_t		rx_dma_init;
	dma_addr_t		tx_dma_init;
	dma_cookie_t		rx_cookie;
	dma_cookie_t		tx_cookie;
	int 			state;
	struct completion	xfer_completion;
	irqreturn_t		(*transfer_handler)(struct dw_spi *dws);
	void			(*cs_control)(struct dw_spi *dws, u32 cs, u8 flag);

	/* Dma info */
	int			dma_inited;
	struct dma_chan		*txchan;
	struct scatterlist	tx_sgl;
	struct dma_chan		*rxchan;
	struct scatterlist	rx_sgl;
	int			dma_chan_done;
	struct device		*dma_dev;
	dma_addr_t		tx_dma_addr; /* phy address of the Data register */	
	dma_addr_t		rx_dma_addr; /* phy address of the Data register */
	struct dw_spi_dma_ops	*dma_ops;
	void			*dma_priv; /* platform relate info */
	
	//struct pci_dev		*dmac;
	atomic_t		debug_flag;

	/* Bus interface info */
	void			*priv;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
};

static inline u32 dw_readl(struct dw_spi *dws, u32 offset)
{
	return __raw_readl(dws->regs + offset);
}

static inline void dw_writel(struct dw_spi *dws, u32 offset, u32 val)
{
	__raw_writel(val, dws->regs + offset);
}

static inline u16 dw_readw(struct dw_spi *dws, u32 offset)
{
	return __raw_readw(dws->regs + offset);
}

static inline void dw_writew(struct dw_spi *dws, u32 offset, u16 val)
{
	__raw_writew(val, dws->regs + offset);
}

static inline void spi_enable_chip(struct dw_spi *dws, int enable)
{
	dw_writel(dws, SPIM_SSIENR, (enable ? 1 : 0));
}

static inline void spi_set_clk(struct dw_spi *dws, u16 div)
{
	dw_writel(dws, SPIM_BAUDR, div);
}

static inline void spi_chip_sel(struct dw_spi *dws, u16 cs)
{
	if (cs > dws->num_cs)
		return;

	if (dws->cs_control)
		dws->cs_control(dws, cs, 1);

	dw_writel(dws, SPIM_SER, 1 << cs);

	DBG_SPI("%s:cs=%d\n",__func__,cs);
}

static  inline void spi_cs_control(struct dw_spi *dws, u32 cs, u8 flag)
{
	if (flag)
		dw_writel(dws, SPIM_SER, 1 << cs);
	else 		
		dw_writel(dws, SPIM_SER, 0);
	
	return;
}


/* Disable IRQ bits */
static inline void spi_mask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, SPIM_IMR) & ~mask;
	dw_writel(dws, SPIM_IMR, new_mask);
}

/* Enable IRQ bits */
static inline void spi_umask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, SPIM_IMR) | mask;
	dw_writel(dws, SPIM_IMR, new_mask);
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
	void (*cs_control)(struct dw_spi *dws, u32 cs, u8 flag);
};

extern int dw_spi_add_host(struct dw_spi *dws);
extern void dw_spi_remove_host(struct dw_spi *dws);
extern int dw_spi_suspend_host(struct dw_spi *dws);
extern int dw_spi_resume_host(struct dw_spi *dws);
extern void dw_spi_xfer_done(struct dw_spi *dws);

/* platform related setup */
extern int dw_spi_dma_init(struct dw_spi *dws); /* Intel MID platforms */
#endif /* SPIM_HEADER_H */
