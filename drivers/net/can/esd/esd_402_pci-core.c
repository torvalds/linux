// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2015 - 2016 Thomas Körper, esd electronic system design gmbh
 * Copyright (C) 2017 - 2023 Stefan Mätje, esd electronics gmbh
 */

#include <linux/can/dev.h>
#include <linux/can.h>
#include <linux/can/netlink.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "esdacc.h"

#define ESD_PCI_DEVICE_ID_PCIE402 0x0402

#define PCI402_FPGA_VER_MIN 0x003d
#define PCI402_MAX_CORES 6
#define PCI402_BAR 0
#define PCI402_IO_OV_OFFS 0
#define PCI402_IO_PCIEP_OFFS 0x10000
#define PCI402_IO_LEN_TOTAL 0x20000
#define PCI402_IO_LEN_CORE 0x2000
#define PCI402_PCICFG_MSICAP 0x50

#define PCI402_DMA_MASK DMA_BIT_MASK(32)
#define PCI402_DMA_SIZE ALIGN(0x10000, PAGE_SIZE)

#define PCI402_PCIEP_OF_INT_ENABLE 0x0050
#define PCI402_PCIEP_OF_BM_ADDR_LO 0x1000
#define PCI402_PCIEP_OF_BM_ADDR_HI 0x1004
#define PCI402_PCIEP_OF_MSI_ADDR_LO 0x1008
#define PCI402_PCIEP_OF_MSI_ADDR_HI 0x100c

struct pci402_card {
	/* Actually mapped io space, all other iomem derived from this */
	void __iomem *addr;
	void __iomem *addr_pciep;

	void *dma_buf;
	dma_addr_t dma_hnd;

	struct acc_ov ov;
	struct acc_core *cores;

	bool msi_enabled;
};

/* The BTR register capabilities described by the can_bittiming_const structures
 * below are valid since esdACC version 0x0032.
 */

/* Used if the esdACC FPGA is built as CAN-Classic version. */
static const struct can_bittiming_const pci402_bittiming_const = {
	.name = "esd_402",
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,
};

/* Used if the esdACC FPGA is built as CAN-FD version. */
static const struct can_bittiming_const pci402_bittiming_const_canfd = {
	.name = "esd_402fd",
	.tseg1_min = 1,
	.tseg1_max = 256,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const struct net_device_ops pci402_acc_netdev_ops = {
	.ndo_open = acc_open,
	.ndo_stop = acc_close,
	.ndo_start_xmit = acc_start_xmit,
	.ndo_change_mtu = can_change_mtu,
	.ndo_eth_ioctl = can_eth_ioctl_hwts,
};

static const struct ethtool_ops pci402_acc_ethtool_ops = {
	.get_ts_info = can_ethtool_op_get_ts_info_hwts,
};

static irqreturn_t pci402_interrupt(int irq, void *dev_id)
{
	struct pci_dev *pdev = dev_id;
	struct pci402_card *card = pci_get_drvdata(pdev);
	irqreturn_t irq_status;

	irq_status = acc_card_interrupt(&card->ov, card->cores);

	return irq_status;
}

static int pci402_set_msiconfig(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	u32 addr_lo_offs = 0;
	u32 addr_lo = 0;
	u32 addr_hi = 0;
	u32 data = 0;
	u16 csr = 0;
	int err;

	/* The FPGA hard IP PCIe core implements a 64-bit MSI Capability
	 * Register Format
	 */
	err = pci_read_config_word(pdev, PCI402_PCICFG_MSICAP + PCI_MSI_FLAGS, &csr);
	if (err)
		goto failed;

	err = pci_read_config_dword(pdev, PCI402_PCICFG_MSICAP + PCI_MSI_ADDRESS_LO,
				    &addr_lo);
	if (err)
		goto failed;
	err = pci_read_config_dword(pdev, PCI402_PCICFG_MSICAP + PCI_MSI_ADDRESS_HI,
				    &addr_hi);
	if (err)
		goto failed;

	err = pci_read_config_dword(pdev, PCI402_PCICFG_MSICAP + PCI_MSI_DATA_64,
				    &data);
	if (err)
		goto failed;

	addr_lo_offs = addr_lo & 0x0000ffff;
	addr_lo &= 0xffff0000;

	if (addr_hi)
		addr_lo |= 1; /* To enable 64-Bit addressing in PCIe endpoint */

	if (!(csr & PCI_MSI_FLAGS_ENABLE)) {
		err = -EINVAL;
		goto failed;
	}

	iowrite32(addr_lo, card->addr_pciep + PCI402_PCIEP_OF_MSI_ADDR_LO);
	iowrite32(addr_hi, card->addr_pciep + PCI402_PCIEP_OF_MSI_ADDR_HI);
	acc_ov_write32(&card->ov, ACC_OV_OF_MSI_ADDRESSOFFSET, addr_lo_offs);
	acc_ov_write32(&card->ov, ACC_OV_OF_MSI_DATA, data);

	return 0;

failed:
	pci_warn(pdev, "Error while setting MSI configuration:\n"
		 "CSR: 0x%.4x, addr: 0x%.8x%.8x, offs: 0x%.4x, data: 0x%.8x\n",
		 csr, addr_hi, addr_lo, addr_lo_offs, data);

	return err;
}

static int pci402_init_card(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);

	card->ov.addr = card->addr + PCI402_IO_OV_OFFS;
	card->addr_pciep = card->addr + PCI402_IO_PCIEP_OFFS;

	acc_reset_fpga(&card->ov);
	acc_init_ov(&card->ov, &pdev->dev);

	if (card->ov.version < PCI402_FPGA_VER_MIN) {
		pci_err(pdev,
			"esdACC version (0x%.4x) outdated, please update\n",
			card->ov.version);
		return -EINVAL;
	}

	if (card->ov.timestamp_frequency != ACC_TS_FREQ_80MHZ) {
		pci_err(pdev,
			"esdACC timestamp frequency of %uHz not supported by driver. Aborted.\n",
			card->ov.timestamp_frequency);
		return -EINVAL;
	}

	if (card->ov.active_cores > PCI402_MAX_CORES) {
		pci_err(pdev,
			"Card with %u active cores not supported by driver. Aborted.\n",
			card->ov.active_cores);
		return -EINVAL;
	}
	card->cores = devm_kcalloc(&pdev->dev, card->ov.active_cores,
				   sizeof(struct acc_core), GFP_KERNEL);
	if (!card->cores)
		return -ENOMEM;

	if (card->ov.features & ACC_OV_REG_FEAT_MASK_CANFD) {
		pci_warn(pdev,
			 "esdACC with CAN-FD feature detected. This driver doesn't support CAN-FD yet.\n");
	}

#ifdef __LITTLE_ENDIAN
	/* So card converts all busmastered data to LE for us: */
	acc_ov_set_bits(&card->ov, ACC_OV_OF_MODE,
			ACC_OV_REG_MODE_MASK_ENDIAN_LITTLE);
#endif

	return 0;
}

static int pci402_init_interrupt(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	int err;

	err = pci_enable_msi(pdev);
	if (!err) {
		err = pci402_set_msiconfig(pdev);
		if (!err) {
			card->msi_enabled = true;
			acc_ov_set_bits(&card->ov, ACC_OV_OF_MODE,
					ACC_OV_REG_MODE_MASK_MSI_ENABLE);
			pci_dbg(pdev, "MSI preparation done\n");
		}
	}

	err = devm_request_irq(&pdev->dev, pdev->irq, pci402_interrupt,
			       IRQF_SHARED, dev_name(&pdev->dev), pdev);
	if (err)
		goto failure_msidis;

	iowrite32(1, card->addr_pciep + PCI402_PCIEP_OF_INT_ENABLE);

	return 0;

failure_msidis:
	if (card->msi_enabled) {
		acc_ov_clear_bits(&card->ov, ACC_OV_OF_MODE,
				  ACC_OV_REG_MODE_MASK_MSI_ENABLE);
		pci_disable_msi(pdev);
		card->msi_enabled = false;
	}

	return err;
}

static void pci402_finish_interrupt(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);

	iowrite32(0, card->addr_pciep + PCI402_PCIEP_OF_INT_ENABLE);
	devm_free_irq(&pdev->dev, pdev->irq, pdev);

	if (card->msi_enabled) {
		acc_ov_clear_bits(&card->ov, ACC_OV_OF_MODE,
				  ACC_OV_REG_MODE_MASK_MSI_ENABLE);
		pci_disable_msi(pdev);
		card->msi_enabled = false;
	}
}

static int pci402_init_dma(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	int err;

	err = dma_set_coherent_mask(&pdev->dev, PCI402_DMA_MASK);
	if (err) {
		pci_err(pdev, "DMA set mask failed!\n");
		return err;
	}

	/* The esdACC DMA engine needs the DMA buffer aligned to a 64k
	 * boundary. The DMA API guarantees to align the returned buffer to the
	 * smallest PAGE_SIZE order which is greater than or equal to the
	 * requested size. With PCI402_DMA_SIZE == 64kB this suffices here.
	 */
	card->dma_buf = dma_alloc_coherent(&pdev->dev, PCI402_DMA_SIZE,
					   &card->dma_hnd, GFP_KERNEL);
	if (!card->dma_buf)
		return -ENOMEM;

	acc_init_bm_ptr(&card->ov, card->cores, card->dma_buf);

	iowrite32(card->dma_hnd,
		  card->addr_pciep + PCI402_PCIEP_OF_BM_ADDR_LO);
	iowrite32(0, card->addr_pciep + PCI402_PCIEP_OF_BM_ADDR_HI);

	pci_set_master(pdev);

	acc_ov_set_bits(&card->ov, ACC_OV_OF_MODE,
			ACC_OV_REG_MODE_MASK_BM_ENABLE);

	return 0;
}

static void pci402_finish_dma(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	int i;

	acc_ov_clear_bits(&card->ov, ACC_OV_OF_MODE,
			  ACC_OV_REG_MODE_MASK_BM_ENABLE);

	pci_clear_master(pdev);

	iowrite32(0, card->addr_pciep + PCI402_PCIEP_OF_BM_ADDR_LO);
	iowrite32(0, card->addr_pciep + PCI402_PCIEP_OF_BM_ADDR_HI);

	card->ov.bmfifo.messages = NULL;
	card->ov.bmfifo.irq_cnt = NULL;
	for (i = 0; i < card->ov.active_cores; i++) {
		struct acc_core *core = &card->cores[i];

		core->bmfifo.messages = NULL;
		core->bmfifo.irq_cnt = NULL;
	}

	dma_free_coherent(&pdev->dev, PCI402_DMA_SIZE, card->dma_buf,
			  card->dma_hnd);
	card->dma_buf = NULL;
}

static void pci402_unregister_core(struct acc_core *core)
{
	netdev_info(core->netdev, "unregister\n");
	unregister_candev(core->netdev);

	free_candev(core->netdev);
	core->netdev = NULL;
}

static int pci402_init_cores(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	int err;
	int i;

	for (i = 0; i < card->ov.active_cores; i++) {
		struct acc_core *core = &card->cores[i];
		struct acc_net_priv *priv;
		struct net_device *netdev;
		u32 fifo_config;

		core->addr = card->ov.addr + (i + 1) * PCI402_IO_LEN_CORE;

		fifo_config = acc_read32(core, ACC_CORE_OF_TXFIFO_CONFIG);
		core->tx_fifo_size = (fifo_config >> 24);
		if (core->tx_fifo_size <= 1) {
			pci_err(pdev, "Invalid tx_fifo_size!\n");
			err = -EINVAL;
			goto failure;
		}

		netdev = alloc_candev(sizeof(*priv), core->tx_fifo_size);
		if (!netdev) {
			err = -ENOMEM;
			goto failure;
		}
		core->netdev = netdev;

		netdev->flags |= IFF_ECHO;
		netdev->dev_port = i;
		netdev->netdev_ops = &pci402_acc_netdev_ops;
		netdev->ethtool_ops = &pci402_acc_ethtool_ops;
		SET_NETDEV_DEV(netdev, &pdev->dev);

		priv = netdev_priv(netdev);
		priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
			CAN_CTRLMODE_LISTENONLY |
			CAN_CTRLMODE_BERR_REPORTING |
			CAN_CTRLMODE_CC_LEN8_DLC;

		priv->can.clock.freq = card->ov.core_frequency;
		if (card->ov.features & ACC_OV_REG_FEAT_MASK_CANFD)
			priv->can.bittiming_const = &pci402_bittiming_const_canfd;
		else
			priv->can.bittiming_const = &pci402_bittiming_const;
		priv->can.do_set_bittiming = acc_set_bittiming;
		priv->can.do_set_mode = acc_set_mode;
		priv->can.do_get_berr_counter = acc_get_berr_counter;

		priv->core = core;
		priv->ov = &card->ov;

		err = register_candev(netdev);
		if (err) {
			free_candev(core->netdev);
			core->netdev = NULL;
			goto failure;
		}

		netdev_info(netdev, "registered\n");
	}

	return 0;

failure:
	for (i--; i >= 0; i--)
		pci402_unregister_core(&card->cores[i]);

	return err;
}

static void pci402_finish_cores(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);
	int i;

	for (i = 0; i < card->ov.active_cores; i++)
		pci402_unregister_core(&card->cores[i]);
}

static int pci402_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pci402_card *card = NULL;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card) {
		err = -ENOMEM;
		goto failure_disable_pci;
	}

	pci_set_drvdata(pdev, card);

	err = pci_request_regions(pdev, pci_name(pdev));
	if (err)
		goto failure_disable_pci;

	card->addr = pci_iomap(pdev, PCI402_BAR, PCI402_IO_LEN_TOTAL);
	if (!card->addr) {
		err = -ENOMEM;
		goto failure_release_regions;
	}

	err = pci402_init_card(pdev);
	if (err)
		goto failure_unmap;

	err = pci402_init_dma(pdev);
	if (err)
		goto failure_unmap;

	err = pci402_init_interrupt(pdev);
	if (err)
		goto failure_finish_dma;

	err = pci402_init_cores(pdev);
	if (err)
		goto failure_finish_interrupt;

	return 0;

failure_finish_interrupt:
	pci402_finish_interrupt(pdev);

failure_finish_dma:
	pci402_finish_dma(pdev);

failure_unmap:
	pci_iounmap(pdev, card->addr);

failure_release_regions:
	pci_release_regions(pdev);

failure_disable_pci:
	pci_disable_device(pdev);

	return err;
}

static void pci402_remove(struct pci_dev *pdev)
{
	struct pci402_card *card = pci_get_drvdata(pdev);

	pci402_finish_interrupt(pdev);
	pci402_finish_cores(pdev);
	pci402_finish_dma(pdev);
	pci_iounmap(pdev, card->addr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id pci402_tbl[] = {
	{
		.vendor = PCI_VENDOR_ID_ESDGMBH,
		.device = ESD_PCI_DEVICE_ID_PCIE402,
		.subvendor = PCI_VENDOR_ID_ESDGMBH,
		.subdevice = PCI_ANY_ID,
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci402_tbl);

static struct pci_driver pci402_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci402_tbl,
	.probe = pci402_probe,
	.remove = pci402_remove,
};
module_pci_driver(pci402_driver);

MODULE_DESCRIPTION("Socket-CAN driver for esd CAN 402 card family with esdACC core on PCIe");
MODULE_AUTHOR("Thomas Körper <socketcan@esd.eu>");
MODULE_AUTHOR("Stefan Mätje <stefan.maetje@esd.eu>");
MODULE_LICENSE("GPL");
