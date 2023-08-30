// SPDX-License-Identifier: GPL-2.0
// PCI1xxxx SPI driver
// Copyright (C) 2022 Microchip Technology Inc.
// Authors: Tharun Kumar P <tharunkumar.pasumarthi@microchip.com>
//          Kumaravel Thiagarajan <Kumaravel.Thiagarajan@microchip.com>


#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

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

#define	SPI_MST1_ADDR_BASE		(0x800)

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

#define SPI_INTR		BIT(8)
#define SPI_FORCE_CE		BIT(4)

#define SPI_CHIP_SEL_COUNT 7
#define VENDOR_ID_MCHP 0x1055

#define SPI_SUSPEND_CONFIG 0x101
#define SPI_RESUME_CONFIG 0x203

struct pci1xxxx_spi_internal {
	u8 hw_inst;
	bool spi_xfer_in_progress;
	int irq;
	struct completion spi_xfer_done;
	struct spi_master *spi_host;
	struct pci1xxxx_spi *parent;
	struct {
		unsigned int dev_sel : 3;
		unsigned int msi_vector_sel : 1;
	} prev_val;
};

struct pci1xxxx_spi {
	struct pci_dev *dev;
	u8 total_hw_instances;
	void __iomem *reg_base;
	struct pci1xxxx_spi_internal *spi_int[];
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

static int pci1xxxx_spi_transfer_one(struct spi_controller *spi_ctlr,
				     struct spi_device *spi, struct spi_transfer *xfer)
{
	struct pci1xxxx_spi_internal *p = spi_controller_get_devdata(spi_ctlr);
	int mode, len, loop_iter, transfer_len;
	struct pci1xxxx_spi *par = p->parent;
	unsigned long bytes_transfered;
	unsigned long bytes_recvd;
	unsigned long loop_count;
	u8 *rx_buf, result;
	const u8 *tx_buf;
	u32 regval;
	u8 clkdiv;

	p->spi_xfer_in_progress = true;
	mode = spi->mode;
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
			regval = readl(par->reg_base +
				       SPI_MST_CTL_REG_OFFSET(p->hw_inst));
			regval &= ~(SPI_MST_CTL_MODE_SEL | SPI_MST_CTL_CMD_LEN_MASK |
				    SPI_MST_CTL_SPEED_MASK);

			if (mode == SPI_MODE_3)
				regval |= SPI_MST_CTL_MODE_SEL;
			else
				regval &= ~SPI_MST_CTL_MODE_SEL;

			regval |= (clkdiv << 5);
			regval &= ~SPI_MST_CTL_CMD_LEN_MASK;
			regval |= (len << 8);
			writel(regval, par->reg_base +
			       SPI_MST_CTL_REG_OFFSET(p->hw_inst));
			regval = readl(par->reg_base +
				       SPI_MST_CTL_REG_OFFSET(p->hw_inst));
			regval |= SPI_MST_CTL_GO;
			writel(regval, par->reg_base +
			       SPI_MST_CTL_REG_OFFSET(p->hw_inst));

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

static irqreturn_t pci1xxxx_spi_isr(int irq, void *dev)
{
	struct pci1xxxx_spi_internal *p = dev;
	irqreturn_t spi_int_fired = IRQ_NONE;
	u32 regval;

	/* Clear the SPI GO_BIT Interrupt */
	regval = readl(p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));
	if (regval & SPI_INTR) {
		/* Clear xfer_done */
		complete(&p->spi_xfer_done);
		spi_int_fired = IRQ_HANDLED;
	}

	writel(regval, p->parent->reg_base + SPI_MST_EVENT_REG_OFFSET(p->hw_inst));

	return spi_int_fired;
}

static int pci1xxxx_spi_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	u8 hw_inst_cnt, iter, start, only_sec_inst;
	struct pci1xxxx_spi_internal *spi_sub_ptr;
	struct device *dev = &pdev->dev;
	struct pci1xxxx_spi *spi_bus;
	struct spi_master *spi_host;
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
		spi_sub_ptr = spi_bus->spi_int[iter];
		spi_sub_ptr->spi_host = devm_spi_alloc_master(dev, sizeof(struct spi_master));
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
		spi_host->transfer_one = pci1xxxx_spi_transfer_one;
		spi_host->set_cs = pci1xxxx_spi_set_cs;
		spi_host->bits_per_word_mask = SPI_BPW_MASK(8);
		spi_host->max_speed_hz = PCI1XXXX_SPI_MAX_CLOCK_HZ;
		spi_host->min_speed_hz = PCI1XXXX_SPI_MIN_CLOCK_HZ;
		spi_host->flags = SPI_MASTER_MUST_TX;
		spi_master_set_devdata(spi_host, spi_sub_ptr);
		ret = devm_spi_register_master(dev, spi_host);
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
		spi_master_resume(spi_sub_ptr->spi_host);
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
		spi_master_suspend(spi_sub_ptr->spi_host);
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
