/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPI_DW_H__
#define __SPI_DW_H__

#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/irqreturn.h>
#include <linux/io.h>
#include <linux/scatterlist.h>
#include <linux/spi/spi-mem.h>
#include <linux/bitfield.h>

/* Synopsys DW SSI IP-core virtual IDs */
#define DW_PSSI_ID			0
#define DW_HSSI_ID			1

/* Synopsys DW SSI component versions (FourCC sequence) */
#define DW_HSSI_102A			0x3130322a

/* DW SSI IP-core ID and version check helpers */
#define dw_spi_ip_is(_dws, _ip) \
	((_dws)->ip == DW_ ## _ip ## _ID)

#define __dw_spi_ver_cmp(_dws, _ip, _ver, _op) \
	(dw_spi_ip_is(_dws, _ip) && (_dws)->ver _op DW_ ## _ip ## _ ## _ver)

#define dw_spi_ver_is(_dws, _ip, _ver) __dw_spi_ver_cmp(_dws, _ip, _ver, ==)

#define dw_spi_ver_is_ge(_dws, _ip, _ver) __dw_spi_ver_cmp(_dws, _ip, _ver, >=)

/* DW SPI controller capabilities */
#define DW_SPI_CAP_CS_OVERRIDE		BIT(0)
#define DW_SPI_CAP_DFS32		BIT(1)

/* Register offsets (Generic for both DWC APB SSI and DWC SSI IP-cores) */
#define DW_SPI_CTRLR0			0x00
#define DW_SPI_CTRLR1			0x04
#define DW_SPI_SSIENR			0x08
#define DW_SPI_MWCR			0x0c
#define DW_SPI_SER			0x10
#define DW_SPI_BAUDR			0x14
#define DW_SPI_TXFTLR			0x18
#define DW_SPI_RXFTLR			0x1c
#define DW_SPI_TXFLR			0x20
#define DW_SPI_RXFLR			0x24
#define DW_SPI_SR			0x28
#define DW_SPI_IMR			0x2c
#define DW_SPI_ISR			0x30
#define DW_SPI_RISR			0x34
#define DW_SPI_TXOICR			0x38
#define DW_SPI_RXOICR			0x3c
#define DW_SPI_RXUICR			0x40
#define DW_SPI_MSTICR			0x44
#define DW_SPI_ICR			0x48
#define DW_SPI_DMACR			0x4c
#define DW_SPI_DMATDLR			0x50
#define DW_SPI_DMARDLR			0x54
#define DW_SPI_IDR			0x58
#define DW_SPI_VERSION			0x5c
#define DW_SPI_DR			0x60
#define DW_SPI_RX_SAMPLE_DLY		0xf0
#define DW_SPI_CS_OVERRIDE		0xf4

/* Bit fields in CTRLR0 (DWC APB SSI) */
#define DW_PSSI_CTRLR0_DFS_MASK			GENMASK(3, 0)
#define DW_PSSI_CTRLR0_DFS32_MASK		GENMASK(20, 16)

#define DW_PSSI_CTRLR0_FRF_MASK			GENMASK(5, 4)
#define DW_SPI_CTRLR0_FRF_MOTO_SPI		0x0
#define DW_SPI_CTRLR0_FRF_TI_SSP		0x1
#define DW_SPI_CTRLR0_FRF_NS_MICROWIRE		0x2
#define DW_SPI_CTRLR0_FRF_RESV			0x3

#define DW_PSSI_CTRLR0_MODE_MASK		GENMASK(7, 6)
#define DW_PSSI_CTRLR0_SCPHA			BIT(6)
#define DW_PSSI_CTRLR0_SCPOL			BIT(7)

#define DW_PSSI_CTRLR0_TMOD_MASK		GENMASK(9, 8)
#define DW_SPI_CTRLR0_TMOD_TR			0x0	/* xmit & recv */
#define DW_SPI_CTRLR0_TMOD_TO			0x1	/* xmit only */
#define DW_SPI_CTRLR0_TMOD_RO			0x2	/* recv only */
#define DW_SPI_CTRLR0_TMOD_EPROMREAD		0x3	/* eeprom read mode */

#define DW_PSSI_CTRLR0_SLV_OE			BIT(10)
#define DW_PSSI_CTRLR0_SRL			BIT(11)
#define DW_PSSI_CTRLR0_CFS			BIT(12)

/* Bit fields in CTRLR0 (DWC SSI with AHB interface) */
#define DW_HSSI_CTRLR0_DFS_MASK			GENMASK(4, 0)
#define DW_HSSI_CTRLR0_FRF_MASK			GENMASK(7, 6)
#define DW_HSSI_CTRLR0_SCPHA			BIT(8)
#define DW_HSSI_CTRLR0_SCPOL			BIT(9)
#define DW_HSSI_CTRLR0_TMOD_MASK		GENMASK(11, 10)
#define DW_HSSI_CTRLR0_SRL			BIT(13)
#define DW_HSSI_CTRLR0_MST			BIT(31)

/* Bit fields in CTRLR1 */
#define DW_SPI_NDF_MASK				GENMASK(15, 0)

/* Bit fields in SR, 7 bits */
#define DW_SPI_SR_MASK				GENMASK(6, 0)
#define DW_SPI_SR_BUSY				BIT(0)
#define DW_SPI_SR_TF_NOT_FULL			BIT(1)
#define DW_SPI_SR_TF_EMPT			BIT(2)
#define DW_SPI_SR_RF_NOT_EMPT			BIT(3)
#define DW_SPI_SR_RF_FULL			BIT(4)
#define DW_SPI_SR_TX_ERR			BIT(5)
#define DW_SPI_SR_DCOL				BIT(6)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define DW_SPI_INT_MASK				GENMASK(5, 0)
#define DW_SPI_INT_TXEI				BIT(0)
#define DW_SPI_INT_TXOI				BIT(1)
#define DW_SPI_INT_RXUI				BIT(2)
#define DW_SPI_INT_RXOI				BIT(3)
#define DW_SPI_INT_RXFI				BIT(4)
#define DW_SPI_INT_MSTI				BIT(5)

/* Bit fields in DMACR */
#define DW_SPI_DMACR_RDMAE			BIT(0)
#define DW_SPI_DMACR_TDMAE			BIT(1)

/* Mem/DMA operations helpers */
#define DW_SPI_WAIT_RETRIES			5
#define DW_SPI_BUF_SIZE \
	(sizeof_field(struct spi_mem_op, cmd.opcode) + \
	 sizeof_field(struct spi_mem_op, addr.val) + 256)
#define DW_SPI_GET_BYTE(_val, _idx) \
	((_val) >> (BITS_PER_BYTE * (_idx)) & 0xff)

/* Slave spi_transfer/spi_mem_op related */
struct dw_spi_cfg {
	u8 tmode;
	u8 dfs;
	u32 ndf;
	u32 freq;
};

struct dw_spi;
struct dw_spi_dma_ops {
	int (*dma_init)(struct device *dev, struct dw_spi *dws);
	void (*dma_exit)(struct dw_spi *dws);
	int (*dma_setup)(struct dw_spi *dws, struct spi_transfer *xfer);
	bool (*can_dma)(struct spi_controller *host, struct spi_device *spi,
			struct spi_transfer *xfer);
	int (*dma_transfer)(struct dw_spi *dws, struct spi_transfer *xfer);
	void (*dma_stop)(struct dw_spi *dws);
};

struct dw_spi {
	struct spi_controller	*host;

	u32			ip;		/* Synopsys DW SSI IP-core ID */
	u32			ver;		/* Synopsys component version */
	u32			caps;		/* DW SPI capabilities */

	void __iomem		*regs;
	unsigned long		paddr;
	int			irq;
	u32			fifo_len;	/* depth of the FIFO buffer */
	unsigned int		dfs_offset;     /* CTRLR0 DFS field offset */
	u32			max_mem_freq;	/* max mem-ops bus freq */
	u32			max_freq;	/* max bus freq supported */

	u32			reg_io_width;	/* DR I/O width in bytes */
	u32			num_cs;		/* chip select lines */
	u16			bus_num;
	void (*set_cs)(struct spi_device *spi, bool enable);

	/* Current message transfer state info */
	void			*tx;
	unsigned int		tx_len;
	void			*rx;
	unsigned int		rx_len;
	u8			buf[DW_SPI_BUF_SIZE];
	int			dma_mapped;
	u8			n_bytes;	/* current is a 1/2 bytes op */
	irqreturn_t		(*transfer_handler)(struct dw_spi *dws);
	u32			current_freq;	/* frequency in hz */
	u32			cur_rx_sample_dly;
	u32			def_rx_sample_dly_ns;

	/* Custom memory operations */
	struct spi_controller_mem_ops mem_ops;

	/* DMA info */
	struct dma_chan		*txchan;
	u32			txburst;
	struct dma_chan		*rxchan;
	u32			rxburst;
	u32			dma_sg_burst;
	u32			dma_addr_widths;
	unsigned long		dma_chan_busy;
	dma_addr_t		dma_addr; /* phy address of the Data register */
	const struct dw_spi_dma_ops *dma_ops;
	struct completion	dma_completion;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
	struct debugfs_regset32 regset;
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

static inline u32 dw_read_io_reg(struct dw_spi *dws, u32 offset)
{
	switch (dws->reg_io_width) {
	case 2:
		return readw_relaxed(dws->regs + offset);
	case 4:
	default:
		return readl_relaxed(dws->regs + offset);
	}
}

static inline void dw_write_io_reg(struct dw_spi *dws, u32 offset, u32 val)
{
	switch (dws->reg_io_width) {
	case 2:
		writew_relaxed(val, dws->regs + offset);
		break;
	case 4:
	default:
		writel_relaxed(val, dws->regs + offset);
		break;
	}
}

static inline void dw_spi_enable_chip(struct dw_spi *dws, int enable)
{
	dw_writel(dws, DW_SPI_SSIENR, (enable ? 1 : 0));
}

static inline void dw_spi_set_clk(struct dw_spi *dws, u16 div)
{
	dw_writel(dws, DW_SPI_BAUDR, div);
}

/* Disable IRQ bits */
static inline void dw_spi_mask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, DW_SPI_IMR) & ~mask;
	dw_writel(dws, DW_SPI_IMR, new_mask);
}

/* Enable IRQ bits */
static inline void dw_spi_umask_intr(struct dw_spi *dws, u32 mask)
{
	u32 new_mask;

	new_mask = dw_readl(dws, DW_SPI_IMR) | mask;
	dw_writel(dws, DW_SPI_IMR, new_mask);
}

/*
 * This disables the SPI controller, interrupts, clears the interrupts status
 * and CS, then re-enables the controller back. Transmit and receive FIFO
 * buffers are cleared when the device is disabled.
 */
static inline void dw_spi_reset_chip(struct dw_spi *dws)
{
	dw_spi_enable_chip(dws, 0);
	dw_spi_mask_intr(dws, 0xff);
	dw_readl(dws, DW_SPI_ICR);
	dw_writel(dws, DW_SPI_SER, 0);
	dw_spi_enable_chip(dws, 1);
}

static inline void dw_spi_shutdown_chip(struct dw_spi *dws)
{
	dw_spi_enable_chip(dws, 0);
	dw_spi_set_clk(dws, 0);
}

extern void dw_spi_set_cs(struct spi_device *spi, bool enable);
extern void dw_spi_update_config(struct dw_spi *dws, struct spi_device *spi,
				 struct dw_spi_cfg *cfg);
extern int dw_spi_check_status(struct dw_spi *dws, bool raw);
extern int dw_spi_add_host(struct device *dev, struct dw_spi *dws);
extern void dw_spi_remove_host(struct dw_spi *dws);
extern int dw_spi_suspend_host(struct dw_spi *dws);
extern int dw_spi_resume_host(struct dw_spi *dws);

#ifdef CONFIG_SPI_DW_DMA

extern void dw_spi_dma_setup_mfld(struct dw_spi *dws);
extern void dw_spi_dma_setup_generic(struct dw_spi *dws);

#else

static inline void dw_spi_dma_setup_mfld(struct dw_spi *dws) {}
static inline void dw_spi_dma_setup_generic(struct dw_spi *dws) {}

#endif /* !CONFIG_SPI_DW_DMA */

#endif /* __SPI_DW_H__ */
