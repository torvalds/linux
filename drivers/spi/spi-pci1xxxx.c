// SPDX-License-Identifier: GPL-2.0
// PCI1xxxx SPI driver
// Copyright (C) 2022 Microchip Technology Inc.
// Authors: Tharun Kumar P <tharunkumar.pasumarthi@microchip.com>
//          Kumaravel Thiagarajan <Kumaravel.Thiagarajan@microchip.com>


#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci_regs.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>
#include "internals.h"

#define DRV_NAME "spi-pci1xxxx"

#define	SYS_FREQ_DEFAULT		(62500000)

#define	PCI1XXXX_SPI_MAX_CLOCK_HZ	(30000000)
#define	PCI1XXXX_SPI_CLK_20MHZ		(20000000)
#define	PCI1XXXX_SPI_CLK_15MHZ		(15000000)
#define	PCI1XXXX_SPI_CLK_12MHZ		(12000000)
#define	PCI1XXXX_SPI_CLK_10MHZ		(10000000)
#define	PCI1XXXX_SPI_MIN_CLOCK_HZ	(2000000)

#define	PCI1XXXX_SPI_BUFFER_SIZE	(320)

#define	SPI_MST_CTL_DEVSEL_MASK		(GENMASK(27, 25))
#define	SPI_MST_CTL_CMD_LEN_MASK	(GENMASK(16, 8))
#define	SPI_MST_CTL_SPEED_MASK		(GENMASK(7, 5))
#define	SPI_MSI_VECTOR_SEL_MASK		(GENMASK(4, 4))

#define	SPI_MST_CTL_FORCE_CE		(BIT(4))
#define	SPI_MST_CTL_MODE_SEL		(BIT(2))
#define	SPI_MST_CTL_GO			(BIT(0))

#define SPI_PERI_ADDR_BASE		(0x160000)
#define SPI_SYSTEM_ADDR_BASE		(0x2000)
#define	SPI_MST1_ADDR_BASE		(0x800)

#define DEV_REV_REG			(SPI_SYSTEM_ADDR_BASE + 0x00)
#define SPI_SYSLOCK_REG			(SPI_SYSTEM_ADDR_BASE + 0xA0)
#define SPI_CONFIG_PERI_ENABLE_REG	(SPI_SYSTEM_ADDR_BASE + 0x108)

#define SPI_PERI_ENBLE_PF_MASK		(GENMASK(17, 16))
#define DEV_REV_MASK			(GENMASK(7, 0))

#define SPI_SYSLOCK			BIT(4)
#define SPI0				(0)
#define SPI1				(1)

/* DMA Related Registers */
#define SPI_DMA_ADDR_BASE		(0x1000)
#define SPI_DMA_GLOBAL_WR_ENGINE_EN	(SPI_DMA_ADDR_BASE + 0x0C)
#define SPI_DMA_WR_DOORBELL_REG		(SPI_DMA_ADDR_BASE + 0x10)
#define SPI_DMA_GLOBAL_RD_ENGINE_EN	(SPI_DMA_ADDR_BASE + 0x2C)
#define SPI_DMA_RD_DOORBELL_REG		(SPI_DMA_ADDR_BASE + 0x30)
#define SPI_DMA_INTR_WR_STS		(SPI_DMA_ADDR_BASE + 0x4C)
#define SPI_DMA_WR_INT_MASK		(SPI_DMA_ADDR_BASE + 0x54)
#define SPI_DMA_INTR_WR_CLR		(SPI_DMA_ADDR_BASE + 0x58)
#define SPI_DMA_ERR_WR_STS		(SPI_DMA_ADDR_BASE + 0x5C)
#define SPI_DMA_INTR_IMWR_WDONE_LOW	(SPI_DMA_ADDR_BASE + 0x60)
#define SPI_DMA_INTR_IMWR_WDONE_HIGH	(SPI_DMA_ADDR_BASE + 0x64)
#define SPI_DMA_INTR_IMWR_WABORT_LOW	(SPI_DMA_ADDR_BASE + 0x68)
#define SPI_DMA_INTR_IMWR_WABORT_HIGH	(SPI_DMA_ADDR_BASE + 0x6C)
#define SPI_DMA_INTR_WR_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0x70)
#define SPI_DMA_INTR_RD_STS		(SPI_DMA_ADDR_BASE + 0xA0)
#define SPI_DMA_RD_INT_MASK		(SPI_DMA_ADDR_BASE + 0xA8)
#define SPI_DMA_INTR_RD_CLR		(SPI_DMA_ADDR_BASE + 0xAC)
#define SPI_DMA_ERR_RD_STS		(SPI_DMA_ADDR_BASE + 0xB8)
#define SPI_DMA_INTR_IMWR_RDONE_LOW	(SPI_DMA_ADDR_BASE + 0xCC)
#define SPI_DMA_INTR_IMWR_RDONE_HIGH	(SPI_DMA_ADDR_BASE + 0xD0)
#define SPI_DMA_INTR_IMWR_RABORT_LOW	(SPI_DMA_ADDR_BASE + 0xD4)
#define SPI_DMA_INTR_IMWR_RABORT_HIGH	(SPI_DMA_ADDR_BASE + 0xD8)
#define SPI_DMA_INTR_RD_IMWR_DATA	(SPI_DMA_ADDR_BASE + 0xDC)

#define SPI_DMA_CH0_WR_BASE		(SPI_DMA_ADDR_BASE + 0x200)
#define SPI_DMA_CH0_RD_BASE		(SPI_DMA_ADDR_BASE + 0x300)
#define SPI_DMA_CH1_WR_BASE		(SPI_DMA_ADDR_BASE + 0x400)
#define SPI_DMA_CH1_RD_BASE		(SPI_DMA_ADDR_BASE + 0x500)

#define SPI_DMA_CH_CTL1_OFFSET		(0x00)
#define SPI_DMA_CH_XFER_LEN_OFFSET	(0x08)
#define SPI_DMA_CH_SAR_LO_OFFSET	(0x0C)
#define SPI_DMA_CH_SAR_HI_OFFSET	(0x10)
#define SPI_DMA_CH_DAR_LO_OFFSET	(0x14)
#define SPI_DMA_CH_DAR_HI_OFFSET	(0x18)

#define SPI_DMA_CH0_DONE_INT		BIT(0)
#define SPI_DMA_CH1_DONE_INT		BIT(1)
#define SPI_DMA_CH0_ABORT_INT		BIT(16)
#define SPI_DMA_CH1_ABORT_INT		BIT(17)
#define SPI_DMA_DONE_INT_MASK		(SPI_DMA_CH0_DONE_INT | SPI_DMA_CH1_DONE_INT)
#define SPI_DMA_ABORT_INT_MASK		(SPI_DMA_CH0_ABORT_INT | SPI_DMA_CH1_ABORT_INT)
#define DMA_CH_CONTROL_LIE		BIT(3)
#define DMA_CH_CONTROL_RIE		BIT(4)
#define DMA_INTR_EN			(DMA_CH_CONTROL_RIE | DMA_CH_CONTROL_LIE)

/* x refers to SPI Host Controller HW instance id in the below macros - 0 or 1 */

#define	SPI_MST_CMD_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x00)
#define	SPI_MST_RSP_BUF_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x200)
#define	SPI_MST_CTL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x400)
#define	SPI_MST_EVENT_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x420)
#define	SPI_MST_EVENT_MASK_REG_OFFSET(x)	(((x) * SPI_MST1_ADDR_BASE) + 0x424)
#define	SPI_MST_PAD_CTL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x460)
#define	SPIALERT_MST_DB_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x464)
#define	SPIALERT_MST_VAL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x468)
#define	SPI_PCI_CTRL_REG_OFFSET(x)		(((x) * SPI_MST1_ADDR_BASE) + 0x480)

#define PCI1XXXX_IRQ_FLAGS			(IRQF_NO_SUSPEND | IRQF_TRIGGER_NONE)
#define SPI_MAX_DATA_LEN			320

#define PCI1XXXX_SPI_TIMEOUT			(msecs_to_jiffies(100))
#define SYSLOCK_RETRY_CNT			(1000)
#define SPI_DMA_ENGINE_EN			(0x1)
#define SPI_DMA_ENGINE_DIS			(0x0)

#define SPI_INTR		BIT(8)
#define SPI_FORCE_CE		BIT(4)

#define SPI_CHIP_SEL_COUNT 7
#define VENDOR_ID_MCHP 0x1055

#define SPI_SUSPEND_CONFIG 0x101
#define SPI_RESUME_CONFIG 0x203

struct pci1xxxx_spi_internal {
	u8 hw_inst;
	u8 clkdiv;
	int irq;
	int mode;
	bool spi_xfer_in_progress;
	void *rx_buf;
	bool dma_aborted_rd;
	u32 bytes_recvd;
	u32 tx_sgl_len;
	u32 rx_sgl_len;
	struct scatterlist *tx_sgl, *rx_sgl;
	bool dma_aborted_wr;
	struct completion spi_xfer_done;
	struct spi_controller *spi_host;
	struct pci1xxxx_spi *parent;
	struct spi_transfer *xfer;
	struct {
		unsigned int dev_sel : 3;
		unsigned int msi_vector_sel : 1;
	} prev_val;
};

struct pci1xxxx_spi {
	struct pci_dev *dev;
	u8 total_hw_instances;
	u8 dev_rev;
	void __iomem *reg_base;
	void __iomem *dma_offset_bar;
	/* lock to safely access the DMA registers in isr */
	spinlock_t dma_reg_lock;
	bool can_dma;
	struct pci1xxxx_spi_internal *spi_int[] __counted_by(total_hw_instances);
};

static const struct pci_device_id pci1xxxx_spi_pci_id_table[] = {
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa004, PCI_ANY_ID, 0x0001), 0, 0, 0x02},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa004, PCI_ANY_ID, 0x0002), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa004, PCI_ANY_ID, 0x0003), 0, 0, 0x11},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa004, PCI_ANY_ID, PCI_ANY_ID), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa014, PCI_ANY_ID, 0x0001), 0, 0, 0x02},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa014, PCI_ANY_ID, 0x0002), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa014, PCI_ANY_ID, 0x0003), 0, 0, 0x11},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa014, PCI_ANY_ID, PCI_ANY_ID), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa024, PCI_ANY_ID, 0x0001), 0, 0, 0x02},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa024, PCI_ANY_ID, 0x0002), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa024, PCI_ANY_ID, 0x0003), 0, 0, 0x11},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa024, PCI_ANY_ID, PCI_ANY_ID), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa034, PCI_ANY_ID, 0x0001), 0, 0, 0x02},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa034, PCI_ANY_ID, 0x0002), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa034, PCI_ANY_ID, 0x0003), 0, 0, 0x11},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa034, PCI_ANY_ID, PCI_ANY_ID), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa044, PCI_ANY_ID, 0x0001), 0, 0, 0x02},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa044, PCI_ANY_ID, 0x0002), 0, 0, 0x01},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa044, PCI_ANY_ID, 0x0003), 0, 0, 0x11},
	{ PCI_DEVICE_SUB(VENDOR_ID_MCHP, 0xa044, PCI_ANY_ID, PCI_ANY_ID), 0, 0, 0x01},
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, pci1xxxx_spi_pci_id_table);

static int pci1xxxx_set_sys_lock(struct pci1xxxx_spi *par)
{
	writel(SPI_SYSLOCK, par->reg_base + SPI_SYSLOCK_REG);
	return readl(par->reg_base + SPI_SYSLOCK_REG);
}

static int pci1xxxx_acquire_sys_lock(struct pci1xxxx_spi *par)
{
	u32 regval;

	return readx_poll_timeout(pci1xxxx_set_sys_lock, par, regval,
			   (regval & SPI_SYSLOCK), 100,
			   SYSLOCK_RETRY_CNT * 100);
}

static void pci1xxxx_release_sys_lock(struct pci1xxxx_spi *par)
{
	writel(0x0, par->reg_base + SPI_SYSLOCK_REG);
}

static int pci1xxxx_check_spi_can_dma(struct pci1xxxx_spi *spi_bus, int irq)
{
	struct pci_dev *pdev = spi_bus->dev;
	u32 pf_num;
	u32 regval;
	int ret;

	/*
	 * DEV REV Registers is a system register, HW Syslock bit
	 * should be acquired before accessing the register
	 */
	ret = pci1xxxx_acquire_sys_lock(spi_bus);
	if (ret) {
		dev_err(&pdev->dev, "Error failed to acquire syslock\n");
		return ret;
	}

	regval = readl(spi_bus->reg_base + DEV_REV_REG);
	spi_bus->dev_rev = regval & DEV_REV_MASK;
	if (spi_bus->dev_rev >= 0xC0) {
		regval = readl(spi_bus->reg_base +
			       SPI_CONFIG_PERI_ENABLE_REG);
		pf_num = regval & SPI_PERI_ENBLE_PF_MASK;
	}

	pci1xxxx_release_sys_lock(spi_bus);

	/*
	 * DMA is supported only from C0 and SPI can use DMA only if
	 * it is mapped to PF0
	 */
	if (spi_bus->dev_rev < 0xC0 || pf_num)
		return -EOPNOTSUPP;

	/*
	 * DMA Supported only with MSI Interrupts
	 * One of the SPI instance's MSI vector address and data
	 * is used for DMA Interrupt
	 */
	if (!irq_get_msi_desc(irq)) {
		dev_warn(&pdev->dev, "Error MSI Interrupt not supported, will operate in PIO mode\n");
		return -EOPNOTSUPP;
	}

	spi_bus->dma_offset_bar = pcim_iomap(pdev, 2, pci_resource_len(pdev, 2));
	if (!spi_bus->dma_offset_bar) {
		dev_warn(&pdev->dev, "Error failed to map dma bar, will operate in PIO mode\n");
		return -EOPNOTSUPP;
	}

	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		dev_warn(&pdev->dev, "Error failed to set DMA mask, will operate in PIO mode\n");
		pcim_iounmap(pdev, spi_bus->dma_offset_bar);
		spi_bus->dma_offset_bar = NULL;
		return -EOPNOTSUPP;
	}

	return 0;
}

static int pci1xxxx_spi_dma_init(struct pci1xxxx_spi *spi_bus, int irq)
{
	struct msi_msg msi;
	int ret;

	ret = pci1xxxx_check_spi_can_dma(spi_bus, irq);
	if (ret)
		return ret;

	spin_lock_init(&spi_bus->dma_reg_lock);
	get_cached_msi_msg(irq, &msi);
	writel(SPI_DMA_ENGINE_EN, spi_bus->dma_offset_bar + SPI_DMA_GLOBAL_WR_ENGINE_EN);
	writel(SPI_DMA_ENGINE_EN, spi_bus->dma_offset_bar + SPI_DMA_GLOBAL_RD_ENGINE_EN);
	writel(msi.address_hi, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_WDONE_HIGH);
	writel(msi.address_hi, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_WABORT_HIGH);
	writel(msi.address_hi, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_RDONE_HIGH);
	writel(msi.address_hi, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_RABORT_HIGH);
	writel(msi.address_lo, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_WDONE_LOW);
	writel(msi.address_lo, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_WABORT_LOW);
	writel(msi.address_lo, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_RDONE_LOW);
	writel(msi.address_lo, spi_bus->dma_offset_bar + SPI_DMA_INTR_IMWR_RABORT_LOW);
	writel(msi.data, spi_bus->dma_offset_bar + SPI_DMA_INTR_WR_IMWR_DATA);
	writel(msi.data, spi_bus->dma_offset_bar + SPI_DMA_INTR_RD_IMWR_DATA);
	dma_set_max_seg_size(&spi_bus->dev->dev, PCI1XXXX_SPI_BUFFER_SIZE);
	spi_bus->can_dma = true;
	return 0;
}

static void pci1xxxx_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct pci1xxxx_spi_internal *p = spi_controller_get_devdata(spi->controller);
	struct pci1xxxx_spi *par = p->parent;
	u32 regval;

	/* Set the DEV_SEL bits of the SPI_MST_CTL_REG */
	regval = readl(par->reg_base + SPI_MST_CTL_REG_OFFSET(p->hw_inst));
	if (!enable) {
		regval |= SPI_FORCE_CE;
		regval &= ~SPI_MST_CTL_DEVSEL_MASK;
		regval |= (spi_get_chipselect(spi, 0) << 25);
	} else {
		regval &= ~SPI_FORCE_CE;
	}
	writel(regval, par->reg_base + SPI_MST_CTL_REG_OFFSET(p->hw_inst));
}

static u8 pci1xxxx_get_clock_div(u32 hz)
{
	u8 val = 0;

	if (hz >= PCI1XXXX_SPI_MAX_CLOCK_HZ)
		val = 2;
	else if ((hz < PCI1XXXX_SPI_MAX_CLOCK_HZ) && (hz >= PCI1XXXX_SPI_CLK_20MHZ))
		val = 3;
	else if ((hz < PCI1XXXX_SPI_CLK_20MHZ) && (hz >= PCI1XXXX_SPI_CLK_15MHZ))
		val = 4;
	else if ((hz < PCI1XXXX_SPI_CLK_15MHZ) && (hz >= PCI1XXXX_SPI_CLK_12MHZ))
		val = 5;
	else if ((hz < PCI1XXXX_SPI_CLK_12MHZ) && (hz >= PCI1XXXX_SPI_CLK_10MHZ))
		val = 6;
	else if ((hz < PCI1XXXX_SPI_CLK_10MHZ) && (hz >= PCI1XXXX_SPI_MIN_CLOCK_HZ))
		val = 7;
	else
		val = 2;

	return val;
}

static void pci1xxxx_spi_setup_dma_to_io(struct pci1xxxx_spi_internal *p,
					 dma_addr_t dma_addr, u32 len)
{
	void __iomem *base;

	if (!p->hw_inst)
		base = p->parent->dma_offset_bar + SPI_DMA_CH0_RD_BASE;
	else
		base = p->parent->dma_offset_bar + SPI_DMA_CH1_RD_BASE;

	writel(DMA_INTR_EN, base + SPI_DMA_CH_CTL1_OFFSET);
	writel(len, base + SPI_DMA_CH_XFER_LEN_OFFSET);
	writel(lower_32_bits(dma_addr), base + SPI_DMA_CH_SAR_LO_OFFSET);
	writel(upper_32_bits(dma_addr), base + SPI_DMA_CH_SAR_HI_OFFSET);
	/* Updated SPI Command Registers */
	writel(lower_32_bits(SPI_PERI_ADDR_BASE + SPI_MST_CMD_BUF_OFFSET(p->hw_inst)),
	       base + SPI_DMA_CH_DAR_LO_OFFSET);
	writel(upper_32_bits(SPI_PERI_ADDR_BASE + SPI_MST_CMD_BUF_OFFSET(p->hw_inst)),
	       base + SPI_DMA_CH_DAR_HI_OFFSET);
}

static void pci1xxxx_spi_setup_dma_from_io(struct pci1xxxx_spi_internal *p,
					   dma_addr_t dma_addr, u32 len)
{
	void *base;

	if (!p->hw_inst)
		base = p->parent->dma_offset_bar + SPI_DMA_CH0_WR_BASE;
	else
		base = p->parent->dma_offset_bar + SPI_DMA_CH1_WR_BASE;

	writel(DMA_INTR_EN, base + SPI_DMA_CH_CTL1_OFFSET);
	writel(len, base + SPI_DMA_CH_XFER_LEN_OFFSET);
	writel(lower_32_bits(dma_addr), base + SPI_DMA_CH_DAR_LO_OFFSET);
	writel(upper_32_bits(dma_addr), base + SPI_DMA_CH_DAR_HI_OFFSET);
	writel(lower_32_bits(SPI_PERI_ADDR_BASE + SPI_MST_RSP_BUF_OFFSET(p->hw_inst)),
	       base + SPI_DMA_CH_SAR_LO_OFFSET);
	writel(upper_32_bits(SPI_PERI_ADDR_BASE + SPI_MST_RSP_BUF_OFFSET(p->hw_inst)),
	       base + SPI_DMA_CH_SAR_HI_OFFSET);
}

static void pci1xxxx_spi_setup(struct pci1xxxx_spi *par, u8 hw_inst, u32 mode,
			       u8 clkdiv, u32 len)
{
	u32 regval;

	regval = readl(par->reg_base + SPI_MST_CTL_REG_OFFSET(hw_inst));
	regval &= ~(SPI_MST_CTL_MODE_SEL | SPI_MST_CTL_CMD_LEN_MASK |
		    SPI_MST_CTL_SPEED_MASK);

	if (mode == SPI_MODE_3)
		regval |= SPI_MST_CTL_MODE_SEL;

	regval |= FIELD_PREP(SPI_MST_CTL_CMD_LEN_MASK, len);
	regval |= FIELD_PREP(SPI_MST_CTL_SPEED_MASK, clkdiv);
	writel(regval, par->reg_base + SPI_MST_CTL_REG_OFFSET(hw_inst));
}

static void pci1xxxx_start_spi_xfer(struct pci1xxxx_spi_internal *p, u8 hw_inst)
{
	u32 regval;

	regval = readl(p->parent->reg_base + SPI_MST_CTL_REG_OFFSET(hw_inst));
	regval |= SPI_MST_CTL_GO;
	writel(regval, p->parent->reg_base + SPI_MST_CTL_REG_OFFSET(hw_inst));
}

static int pci1xxxx_spi_transfer_with_io(struct spi_controller *spi_ctlr,
					 struct spi_device *spi, struct spi_transfer *xfer)
{
	struct pci1xxxx_spi_internal *p = spi_controller_get_devdata(spi_ctlr);
	struct pci1xxxx_spi *par = p->parent;
	int len, loop_iter, transfer_len;
	unsigned long bytes_transfered;
	unsigned long bytes_recvd;
	unsigned long loop_count;
	u8 *rx_buf, result;
	const u8 *tx_buf;
	u32 regval;
	u8 clkdiv;

	p->spi_xfer_in_progress = true;
	p->bytes_recvd = 0;
	clkdiv = pci1xxxx_get_clock_div(xfer->speed_hz);
	tx_buf = xfer->tx_buf;
	rx_buf = xfer->rx_buf;
	transfer_len = xfer->len;
	regval = readl(par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	writel(regval, par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));

	if (tx_buf) {
		bytes_transfered = 0;
		bytes_recvd = 0;
		loop_count = transfer_len / SPI_MAX_DATA_LEN;
		if (transfer_len % SPI_MAX_DATA_LEN != 0)
			loop_count += 1;

		for (loop_iter = 0; loop_iter < loop_count; loop_iter++) {
			len = SPI_MAX_DATA_LEN;
			if ((transfer_len % SPI_MAX_DATA_LEN != 0) &&
			    (loop_iter == loop_count - 1))
				len = transfer_len % SPI_MAX_DATA_LEN;

			reinit_completion(&p->spi_xfer_done);
			memcpy_toio(par->reg_base + SPI_MST_CMD_BUF_OFFSET(p->hw_inst),
				    &tx_buf[bytes_transfered], len);
			bytes_transfered += len;
			pci1xxxx_spi_setup(par, p->hw_inst, spi->mode, clkdiv, len);
			pci1xxxx_start_spi_xfer(p, p->hw_inst);

			/* Wait for DMA_TERM interrupt */
			result = wait_for_completion_timeout(&p->spi_xfer_done,
							     PCI1XXXX_SPI_TIMEOUT);
			if (!result)
				return -ETIMEDOUT;

			if (rx_buf) {
				memcpy_fromio(&rx_buf[bytes_recvd], par->reg_base +
					      SPI_MST_RSP_BUF_OFFSET(p->hw_inst), len);
				bytes_recvd += len;
			}
		}
	}
	p->spi_xfer_in_progress = false;

	return 0;
}

static int pci1xxxx_spi_transfer_with_dma(struct spi_controller *spi_ctlr,
					  struct spi_device *spi,
					  struct spi_transfer *xfer)
{
	struct pci1xxxx_spi_internal *p = spi_controller_get_devdata(spi_ctlr);
	struct pci1xxxx_spi *par = p->parent;
	dma_addr_t rx_dma_addr = 0;
	dma_addr_t tx_dma_addr = 0;
	int ret = 0;
	u32 regval;

	p->spi_xfer_in_progress = true;
	p->tx_sgl = xfer->tx_sg.sgl;
	p->rx_sgl = xfer->rx_sg.sgl;
	p->rx_buf = xfer->rx_buf;
	regval = readl(par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	writel(regval, par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));

	if (!xfer->tx_buf || !p->tx_sgl) {
		ret = -EINVAL;
		goto error;
	}
	p->xfer = xfer;
	p->mode = spi->mode;
	p->clkdiv = pci1xxxx_get_clock_div(xfer->speed_hz);
	p->bytes_recvd = 0;
	p->rx_buf = xfer->rx_buf;
	regval = readl(par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	writel(regval, par->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));

	tx_dma_addr = sg_dma_address(p->tx_sgl);
	rx_dma_addr = sg_dma_address(p->rx_sgl);
	p->tx_sgl_len = sg_dma_len(p->tx_sgl);
	p->rx_sgl_len = sg_dma_len(p->rx_sgl);
	pci1xxxx_spi_setup(par, p->hw_inst, p->mode, p->clkdiv, p->tx_sgl_len);
	pci1xxxx_spi_setup_dma_to_io(p, (tx_dma_addr), p->tx_sgl_len);
	if (rx_dma_addr)
		pci1xxxx_spi_setup_dma_from_io(p, rx_dma_addr, p->rx_sgl_len);
	writel(p->hw_inst, par->dma_offset_bar + SPI_DMA_RD_DOORBELL_REG);

	reinit_completion(&p->spi_xfer_done);
	/* Wait for DMA_TERM interrupt */
	ret = wait_for_completion_timeout(&p->spi_xfer_done, PCI1XXXX_SPI_TIMEOUT);
	if (!ret) {
		ret = -ETIMEDOUT;
		if (p->dma_aborted_rd) {
			writel(SPI_DMA_ENGINE_DIS,
			       par->dma_offset_bar + SPI_DMA_GLOBAL_RD_ENGINE_EN);
			/*
			 * DMA ENGINE reset takes time if any TLP
			 * completeion in progress, should wait
			 * till DMA Engine reset is completed.
			 */
			ret = readl_poll_timeout(par->dma_offset_bar +
						 SPI_DMA_GLOBAL_RD_ENGINE_EN, regval,
						 (regval == 0x0), 0, USEC_PER_MSEC);
			if (ret) {
				ret = -ECANCELED;
				goto error;
			}
			writel(SPI_DMA_ENGINE_EN,
			       par->dma_offset_bar + SPI_DMA_GLOBAL_RD_ENGINE_EN);
			p->dma_aborted_rd = false;
			ret = -ECANCELED;
		}
		if (p->dma_aborted_wr) {
			writel(SPI_DMA_ENGINE_DIS,
			       par->dma_offset_bar + SPI_DMA_GLOBAL_WR_ENGINE_EN);

			/*
			 * DMA ENGINE reset takes time if any TLP
			 * completeion in progress, should wait
			 * till DMA Engine reset is completed.
			 */
			ret = readl_poll_timeout(par->dma_offset_bar +
						 SPI_DMA_GLOBAL_WR_ENGINE_EN, regval,
						 (regval == 0x0), 0, USEC_PER_MSEC);
			if (ret) {
				ret = -ECANCELED;
				goto error;
			}

			writel(SPI_DMA_ENGINE_EN,
			       par->dma_offset_bar + SPI_DMA_GLOBAL_WR_ENGINE_EN);
			p->dma_aborted_wr = false;
			ret = -ECANCELED;
		}
		goto error;
	}
	ret = 0;

error:
	p->spi_xfer_in_progress = false;

	return ret;
}

static int pci1xxxx_spi_transfer_one(struct spi_controller *spi_ctlr,
				     struct spi_device *spi, struct spi_transfer *xfer)
{
	if (spi_xfer_is_dma_mapped(spi_ctlr, spi, xfer))
		return pci1xxxx_spi_transfer_with_dma(spi_ctlr, spi, xfer);
	else
		return pci1xxxx_spi_transfer_with_io(spi_ctlr, spi, xfer);
}

static irqreturn_t pci1xxxx_spi_isr_io(int irq, void *dev)
{
	struct pci1xxxx_spi_internal *p = dev;
	irqreturn_t spi_int_fired = IRQ_NONE;
	u32 regval;

	/* Clear the SPI GO_BIT Interrupt */
	regval = readl(p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	if (regval & SPI_INTR) {
		/* Clear xfer_done */
		if (p->parent->can_dma && p->rx_buf)
			writel(p->hw_inst, p->parent->dma_offset_bar +
			       SPI_DMA_WR_DOORBELL_REG);
		else
			complete(&p->parent->spi_int[p->hw_inst]->spi_xfer_done);
		spi_int_fired = IRQ_HANDLED;
	}
	writel(regval, p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	return spi_int_fired;
}

static void pci1xxxx_spi_setup_next_dma_transfer(struct pci1xxxx_spi_internal *p)
{
	dma_addr_t tx_dma_addr = 0;
	dma_addr_t rx_dma_addr = 0;
	u32 prev_len;

	p->tx_sgl = sg_next(p->tx_sgl);
	if (p->rx_sgl)
		p->rx_sgl = sg_next(p->rx_sgl);
	if (!p->tx_sgl) {
		/* Clear xfer_done */
		complete(&p->spi_xfer_done);
	} else {
		tx_dma_addr = sg_dma_address(p->tx_sgl);
		prev_len = p->tx_sgl_len;
		p->tx_sgl_len = sg_dma_len(p->tx_sgl);
		if (prev_len != p->tx_sgl_len)
			pci1xxxx_spi_setup(p->parent,
					   p->hw_inst, p->mode, p->clkdiv, p->tx_sgl_len);
		pci1xxxx_spi_setup_dma_to_io(p, tx_dma_addr, p->tx_sgl_len);
		if (p->rx_sgl) {
			rx_dma_addr = sg_dma_address(p->rx_sgl);
			p->rx_sgl_len = sg_dma_len(p->rx_sgl);
			pci1xxxx_spi_setup_dma_from_io(p, rx_dma_addr, p->rx_sgl_len);
		}
		writel(p->hw_inst, p->parent->dma_offset_bar + SPI_DMA_RD_DOORBELL_REG);
	}
}

static irqreturn_t pci1xxxx_spi_isr_dma(int irq, void *dev)
{
	struct pci1xxxx_spi_internal *p = dev;
	irqreturn_t spi_int_fired = IRQ_NONE;
	unsigned long flags;
	u32 regval;

	spin_lock_irqsave(&p->parent->dma_reg_lock, flags);
	/* Clear the DMA RD INT and start spi xfer*/
	regval = readl(p->parent->dma_offset_bar + SPI_DMA_INTR_RD_STS);
	if (regval & SPI_DMA_DONE_INT_MASK) {
		if (regval & SPI_DMA_CH0_DONE_INT)
			pci1xxxx_start_spi_xfer(p, SPI0);
		if (regval & SPI_DMA_CH1_DONE_INT)
			pci1xxxx_start_spi_xfer(p, SPI1);
		spi_int_fired = IRQ_HANDLED;
	}
	if (regval & SPI_DMA_ABORT_INT_MASK) {
		p->dma_aborted_rd = true;
		spi_int_fired = IRQ_HANDLED;
	}
	writel(regval, p->parent->dma_offset_bar + SPI_DMA_INTR_RD_CLR);

	/* Clear the DMA WR INT */
	regval = readl(p->parent->dma_offset_bar + SPI_DMA_INTR_WR_STS);
	if (regval & SPI_DMA_DONE_INT_MASK) {
		if (regval & SPI_DMA_CH0_DONE_INT)
			pci1xxxx_spi_setup_next_dma_transfer(p->parent->spi_int[SPI0]);

		if (regval & SPI_DMA_CH1_DONE_INT)
			pci1xxxx_spi_setup_next_dma_transfer(p->parent->spi_int[SPI1]);

		spi_int_fired = IRQ_HANDLED;
	}
	if (regval & SPI_DMA_ABORT_INT_MASK) {
		p->dma_aborted_wr = true;
		spi_int_fired = IRQ_HANDLED;
	}
	writel(regval, p->parent->dma_offset_bar + SPI_DMA_INTR_WR_CLR);
	spin_unlock_irqrestore(&p->parent->dma_reg_lock, flags);

	/* Clear the SPI GO_BIT Interrupt */
	regval = readl(p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	if (regval & SPI_INTR) {
		writel(p->hw_inst, p->parent->dma_offset_bar + SPI_DMA_WR_DOORBELL_REG);
		spi_int_fired = IRQ_HANDLED;
	}
	writel(regval, p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	return spi_int_fired;
}

static irqreturn_t pci1xxxx_spi_isr(int irq, void *dev)
{
	struct pci1xxxx_spi_internal *p = dev;

	if (p->spi_host->can_dma(p->spi_host, NULL, p->xfer))
		return pci1xxxx_spi_isr_dma(irq, dev);
	else
		return pci1xxxx_spi_isr_io(irq, dev);
}

static bool pci1xxxx_spi_can_dma(struct spi_controller *host,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	struct pci1xxxx_spi_internal *p = spi_controller_get_devdata(host);
	struct pci1xxxx_spi *par = p->parent;

	return par->can_dma;
}

static int pci1xxxx_spi_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	u8 hw_inst_cnt, iter, start, only_sec_inst;
	struct pci1xxxx_spi_internal *spi_sub_ptr;
	struct device *dev = &pdev->dev;
	struct pci1xxxx_spi *spi_bus;
	struct spi_controller *spi_host;
	u32 regval;
	int ret;

	hw_inst_cnt = ent->driver_data & 0x0f;
	start = (ent->driver_data & 0xf0) >> 4;
	if (start == 1)
		only_sec_inst = 1;
	else
		only_sec_inst = 0;

	spi_bus = devm_kzalloc(&pdev->dev,
			       struct_size(spi_bus, spi_int, hw_inst_cnt),
			       GFP_KERNEL);
	if (!spi_bus)
		return -ENOMEM;

	spi_bus->dev = pdev;
	spi_bus->total_hw_instances = hw_inst_cnt;
	pci_set_master(pdev);

	for (iter = 0; iter < hw_inst_cnt; iter++) {
		spi_bus->spi_int[iter] = devm_kzalloc(&pdev->dev,
						      sizeof(struct pci1xxxx_spi_internal),
						      GFP_KERNEL);
		if (!spi_bus->spi_int[iter])
			return -ENOMEM;
		spi_sub_ptr = spi_bus->spi_int[iter];
		spi_sub_ptr->spi_host = devm_spi_alloc_host(dev, sizeof(struct spi_controller));
		if (!spi_sub_ptr->spi_host)
			return -ENOMEM;

		spi_sub_ptr->parent = spi_bus;
		spi_sub_ptr->spi_xfer_in_progress = false;

		if (!iter) {
			ret = pcim_enable_device(pdev);
			if (ret)
				return -ENOMEM;

			ret = pci_request_regions(pdev, DRV_NAME);
			if (ret)
				return -ENOMEM;

			spi_bus->reg_base = pcim_iomap(pdev, 0, pci_resource_len(pdev, 0));
			if (!spi_bus->reg_base) {
				ret = -EINVAL;
				goto error;
			}

			ret = pci_alloc_irq_vectors(pdev, hw_inst_cnt, hw_inst_cnt,
						    PCI_IRQ_ALL_TYPES);
			if (ret < 0) {
				dev_err(&pdev->dev, "Error allocating MSI vectors\n");
				goto error;
			}

			init_completion(&spi_sub_ptr->spi_xfer_done);
			/* Initialize Interrupts - SPI_INT */
			regval = readl(spi_bus->reg_base +
				       SPI_MST_EVENT_MASK_REG_OFFSET(spi_sub_ptr->hw_inst));
			regval &= ~SPI_INTR;
			writel(regval, spi_bus->reg_base +
			       SPI_MST_EVENT_MASK_REG_OFFSET(spi_sub_ptr->hw_inst));
			spi_sub_ptr->irq = pci_irq_vector(pdev, 0);

			ret = devm_request_irq(&pdev->dev, spi_sub_ptr->irq,
					       pci1xxxx_spi_isr, PCI1XXXX_IRQ_FLAGS,
					       pci_name(pdev), spi_sub_ptr);
			if (ret < 0) {
				dev_err(&pdev->dev, "Unable to request irq : %d",
					spi_sub_ptr->irq);
				ret = -ENODEV;
				goto error;
			}

			ret = pci1xxxx_spi_dma_init(spi_bus, spi_sub_ptr->irq);
			if (ret && ret != -EOPNOTSUPP)
				goto error;

			/* This register is only applicable for 1st instance */
			regval = readl(spi_bus->reg_base + SPI_PCI_CTRL_REG_OFFSET(0));
			if (!only_sec_inst)
				regval |= (BIT(4));
			else
				regval &= ~(BIT(4));

			writel(regval, spi_bus->reg_base + SPI_PCI_CTRL_REG_OFFSET(0));
		}

		spi_sub_ptr->hw_inst = start++;

		if (iter == 1) {
			init_completion(&spi_sub_ptr->spi_xfer_done);
			/* Initialize Interrupts - SPI_INT */
			regval = readl(spi_bus->reg_base +
			       SPI_MST_EVENT_MASK_REG_OFFSET(spi_sub_ptr->hw_inst));
			regval &= ~SPI_INTR;
			writel(regval, spi_bus->reg_base +
			       SPI_MST_EVENT_MASK_REG_OFFSET(spi_sub_ptr->hw_inst));
			spi_sub_ptr->irq = pci_irq_vector(pdev, iter);
			ret = devm_request_irq(&pdev->dev, spi_sub_ptr->irq,
					       pci1xxxx_spi_isr, PCI1XXXX_IRQ_FLAGS,
					       pci_name(pdev), spi_sub_ptr);
			if (ret < 0) {
				dev_err(&pdev->dev, "Unable to request irq : %d",
					spi_sub_ptr->irq);
				ret = -ENODEV;
				goto error;
			}
		}

		spi_host = spi_sub_ptr->spi_host;
		spi_host->num_chipselect = SPI_CHIP_SEL_COUNT;
		spi_host->mode_bits = SPI_MODE_0 | SPI_MODE_3 | SPI_RX_DUAL |
				      SPI_TX_DUAL | SPI_LOOP;
		spi_host->can_dma = pci1xxxx_spi_can_dma;
		spi_host->transfer_one = pci1xxxx_spi_transfer_one;

		spi_host->set_cs = pci1xxxx_spi_set_cs;
		spi_host->bits_per_word_mask = SPI_BPW_MASK(8);
		spi_host->max_speed_hz = PCI1XXXX_SPI_MAX_CLOCK_HZ;
		spi_host->min_speed_hz = PCI1XXXX_SPI_MIN_CLOCK_HZ;
		spi_host->flags = SPI_CONTROLLER_MUST_TX;
		spi_controller_set_devdata(spi_host, spi_sub_ptr);
		ret = devm_spi_register_controller(dev, spi_host);
		if (ret)
			goto error;
	}
	pci_set_drvdata(pdev, spi_bus);

	return 0;

error:
	pci_release_regions(pdev);
	return ret;
}

static void store_restore_config(struct pci1xxxx_spi *spi_ptr,
				 struct pci1xxxx_spi_internal *spi_sub_ptr,
				 u8 inst, bool store)
{
	u32 regval;

	if (store) {
		regval = readl(spi_ptr->reg_base +
			       SPI_MST_CTL_REG_OFFSET(spi_sub_ptr->hw_inst));
		regval &= SPI_MST_CTL_DEVSEL_MASK;
		spi_sub_ptr->prev_val.dev_sel = (regval >> 25) & 7;
		regval = readl(spi_ptr->reg_base +
			       SPI_PCI_CTRL_REG_OFFSET(spi_sub_ptr->hw_inst));
		regval &= SPI_MSI_VECTOR_SEL_MASK;
		spi_sub_ptr->prev_val.msi_vector_sel = (regval >> 4) & 1;
	} else {
		regval = readl(spi_ptr->reg_base + SPI_MST_CTL_REG_OFFSET(inst));
		regval &= ~SPI_MST_CTL_DEVSEL_MASK;
		regval |= (spi_sub_ptr->prev_val.dev_sel << 25);
		writel(regval,
		       spi_ptr->reg_base + SPI_MST_CTL_REG_OFFSET(inst));
		writel((spi_sub_ptr->prev_val.msi_vector_sel << 4),
			spi_ptr->reg_base + SPI_PCI_CTRL_REG_OFFSET(inst));
	}
}

static int pci1xxxx_spi_resume(struct device *dev)
{
	struct pci1xxxx_spi *spi_ptr = dev_get_drvdata(dev);
	struct pci1xxxx_spi_internal *spi_sub_ptr;
	u32 regval = SPI_RESUME_CONFIG;
	u8 iter;

	for (iter = 0; iter < spi_ptr->total_hw_instances; iter++) {
		spi_sub_ptr = spi_ptr->spi_int[iter];
		spi_controller_resume(spi_sub_ptr->spi_host);
		writel(regval, spi_ptr->reg_base +
		       SPI_MST_EVENT_MASK_REG_OFFSET(iter));

		/* Restore config at resume */
		store_restore_config(spi_ptr, spi_sub_ptr, iter, 0);
	}

	return 0;
}

static int pci1xxxx_spi_suspend(struct device *dev)
{
	struct pci1xxxx_spi *spi_ptr = dev_get_drvdata(dev);
	struct pci1xxxx_spi_internal *spi_sub_ptr;
	u32 reg1 = SPI_SUSPEND_CONFIG;
	u8 iter;

	for (iter = 0; iter < spi_ptr->total_hw_instances; iter++) {
		spi_sub_ptr = spi_ptr->spi_int[iter];

		while (spi_sub_ptr->spi_xfer_in_progress)
			msleep(20);

		/* Store existing config before suspend */
		store_restore_config(spi_ptr, spi_sub_ptr, iter, 1);
		spi_controller_suspend(spi_sub_ptr->spi_host);
		writel(reg1, spi_ptr->reg_base +
		       SPI_MST_EVENT_MASK_REG_OFFSET(iter));
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(spi_pm_ops, pci1xxxx_spi_suspend,
				pci1xxxx_spi_resume);

static struct pci_driver pci1xxxx_spi_driver = {
	.name		= DRV_NAME,
	.id_table	= pci1xxxx_spi_pci_id_table,
	.probe		= pci1xxxx_spi_probe,
	.driver		=	{
		.pm = pm_sleep_ptr(&spi_pm_ops),
	},
};

module_pci_driver(pci1xxxx_spi_driver);

MODULE_DESCRIPTION("Microchip Technology Inc. pci1xxxx SPI bus driver");
MODULE_AUTHOR("Tharun Kumar P<tharunkumar.pasumarthi@microchip.com>");
MODULE_AUTHOR("Kumaravel Thiagarajan<kumaravel.thiagarajan@microchip.com>");
MODULE_LICENSE("GPL v2");
