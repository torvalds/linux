/*
 *  drivers/mmc/host/via-sdmmc.c - VIA SD/MMC Card Reader driver
 *  Copyright (c) 2008, VIA Technologies Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <linux/mmc/host.h>

#define DRV_NAME	"via_sdmmc"

#define PCI_DEVICE_ID_VIA_9530	0x9530

#define VIA_CRDR_SDC_OFF	0x200
#define VIA_CRDR_DDMA_OFF	0x400
#define VIA_CRDR_PCICTRL_OFF	0x600

#define VIA_CRDR_MIN_CLOCK	375000
#define VIA_CRDR_MAX_CLOCK	48000000

/*
 * PCI registers
 */

#define VIA_CRDR_PCI_WORK_MODE	0x40
#define VIA_CRDR_PCI_DBG_MODE	0x41

/*
 * SDC MMIO Registers
 */

#define VIA_CRDR_SDCTRL			0x0
#define VIA_CRDR_SDCTRL_START		0x01
#define VIA_CRDR_SDCTRL_WRITE		0x04
#define VIA_CRDR_SDCTRL_SINGLE_WR	0x10
#define VIA_CRDR_SDCTRL_SINGLE_RD	0x20
#define VIA_CRDR_SDCTRL_MULTI_WR	0x30
#define VIA_CRDR_SDCTRL_MULTI_RD	0x40
#define VIA_CRDR_SDCTRL_STOP		0x70

#define VIA_CRDR_SDCTRL_RSP_NONE	0x0
#define VIA_CRDR_SDCTRL_RSP_R1		0x10000
#define VIA_CRDR_SDCTRL_RSP_R2		0x20000
#define VIA_CRDR_SDCTRL_RSP_R3		0x30000
#define VIA_CRDR_SDCTRL_RSP_R1B		0x90000

#define VIA_CRDR_SDCARG 	0x4

#define VIA_CRDR_SDBUSMODE	0x8
#define VIA_CRDR_SDMODE_4BIT	0x02
#define VIA_CRDR_SDMODE_CLK_ON	0x40

#define VIA_CRDR_SDBLKLEN	0xc
/*
 * Bit 0 -Bit 10 : Block length. So, the maximum block length should be 2048.
 * Bit 11 - Bit 13 : Reserved.
 * GPIDET : Select GPI pin to detect card, GPI means CR_CD# in top design.
 * INTEN : Enable SD host interrupt.
 * Bit 16 - Bit 31 : Block count. So, the maximun block count should be 65536.
 */
#define VIA_CRDR_SDBLKLEN_GPIDET	0x2000
#define VIA_CRDR_SDBLKLEN_INTEN		0x8000
#define VIA_CRDR_MAX_BLOCK_COUNT	65536
#define VIA_CRDR_MAX_BLOCK_LENGTH	2048

#define VIA_CRDR_SDRESP0	0x10
#define VIA_CRDR_SDRESP1	0x14
#define VIA_CRDR_SDRESP2	0x18
#define VIA_CRDR_SDRESP3	0x1c

#define VIA_CRDR_SDCURBLKCNT	0x20

#define VIA_CRDR_SDINTMASK	0x24
/*
 * MBDIE : Multiple Blocks transfer Done Interrupt Enable
 * BDDIE : Block Data transfer Done Interrupt Enable
 * CIRIE : Card Insertion or Removal Interrupt Enable
 * CRDIE : Command-Response transfer Done Interrupt Enable
 * CRTOIE : Command-Response response TimeOut Interrupt Enable
 * ASCRDIE : Auto Stop Command-Response transfer Done Interrupt Enable
 * DTIE : Data access Timeout Interrupt Enable
 * SCIE : reSponse CRC error Interrupt Enable
 * RCIE : Read data CRC error Interrupt Enable
 * WCIE : Write data CRC error Interrupt Enable
 */
#define VIA_CRDR_SDINTMASK_MBDIE	0x10
#define VIA_CRDR_SDINTMASK_BDDIE	0x20
#define VIA_CRDR_SDINTMASK_CIRIE	0x80
#define VIA_CRDR_SDINTMASK_CRDIE	0x200
#define VIA_CRDR_SDINTMASK_CRTOIE	0x400
#define VIA_CRDR_SDINTMASK_ASCRDIE	0x800
#define VIA_CRDR_SDINTMASK_DTIE		0x1000
#define VIA_CRDR_SDINTMASK_SCIE		0x2000
#define VIA_CRDR_SDINTMASK_RCIE		0x4000
#define VIA_CRDR_SDINTMASK_WCIE		0x8000

#define VIA_CRDR_SDACTIVE_INTMASK \
	(VIA_CRDR_SDINTMASK_MBDIE | VIA_CRDR_SDINTMASK_CIRIE \
	| VIA_CRDR_SDINTMASK_CRDIE | VIA_CRDR_SDINTMASK_CRTOIE \
	| VIA_CRDR_SDINTMASK_DTIE | VIA_CRDR_SDINTMASK_SCIE \
	| VIA_CRDR_SDINTMASK_RCIE | VIA_CRDR_SDINTMASK_WCIE)

#define VIA_CRDR_SDSTATUS	0x28
/*
 * CECC : Reserved
 * WP : SD card Write Protect status
 * SLOTD : Reserved
 * SLOTG : SD SLOT status(Gpi pin status)
 * MBD : Multiple Blocks transfer Done interrupt status
 * BDD : Block Data transfer Done interrupt status
 * CD : Reserved
 * CIR : Card Insertion or Removal interrupt detected on GPI pin
 * IO : Reserved
 * CRD : Command-Response transfer Done interrupt status
 * CRTO : Command-Response response TimeOut interrupt status
 * ASCRDIE : Auto Stop Command-Response transfer Done interrupt status
 * DT : Data access Timeout interrupt status
 * SC : reSponse CRC error interrupt status
 * RC : Read data CRC error interrupt status
 * WC : Write data CRC error interrupt status
 */
#define VIA_CRDR_SDSTS_CECC		0x01
#define VIA_CRDR_SDSTS_WP		0x02
#define VIA_CRDR_SDSTS_SLOTD		0x04
#define VIA_CRDR_SDSTS_SLOTG		0x08
#define VIA_CRDR_SDSTS_MBD		0x10
#define VIA_CRDR_SDSTS_BDD		0x20
#define VIA_CRDR_SDSTS_CD		0x40
#define VIA_CRDR_SDSTS_CIR		0x80
#define VIA_CRDR_SDSTS_IO		0x100
#define VIA_CRDR_SDSTS_CRD		0x200
#define VIA_CRDR_SDSTS_CRTO		0x400
#define VIA_CRDR_SDSTS_ASCRDIE		0x800
#define VIA_CRDR_SDSTS_DT		0x1000
#define VIA_CRDR_SDSTS_SC		0x2000
#define VIA_CRDR_SDSTS_RC		0x4000
#define VIA_CRDR_SDSTS_WC		0x8000

#define VIA_CRDR_SDSTS_IGN_MASK\
	(VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_IO)
#define VIA_CRDR_SDSTS_INT_MASK \
	(VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_CD \
	| VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_IO | VIA_CRDR_SDSTS_CRD \
	| VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT \
	| VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTS_W1C_MASK \
	(VIA_CRDR_SDSTS_CECC | VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD \
	| VIA_CRDR_SDSTS_CD | VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_CRD \
	| VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT \
	| VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define  VIA_CRDR_SDSTS_CMD_MASK \
	(VIA_CRDR_SDSTS_CRD | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_SC)
#define  VIA_CRDR_SDSTS_DATA_MASK\
	(VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_DT \
	| VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)

#define VIA_CRDR_SDSTATUS2	0x2a
/*
 * CFE : Enable SD host automatic Clock FReezing
 */
#define VIA_CRDR_SDSTS_CFE		0x80

#define VIA_CRDR_SDRSPTMO	0x2C

#define VIA_CRDR_SDCLKSEL	0x30

#define VIA_CRDR_SDEXTCTRL	0x34
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SD	0x01
#define VIS_CRDR_SDEXTCTRL_SHIFT_9	0x02
#define VIS_CRDR_SDEXTCTRL_MMC_8BIT	0x04
#define VIS_CRDR_SDEXTCTRL_RELD_BLK	0x08
#define VIS_CRDR_SDEXTCTRL_BAD_CMDA	0x10
#define VIS_CRDR_SDEXTCTRL_BAD_DATA	0x20
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SPI	0x40
#define VIA_CRDR_SDEXTCTRL_HISPD	0x80
/* 0x38-0xFF reserved */

/*
 * Data DMA Control Registers
 */

#define VIA_CRDR_DMABASEADD	0x0
#define VIA_CRDR_DMACOUNTER	0x4

#define VIA_CRDR_DMACTRL	0x8
/*
 * DIR :Transaction Direction
 * 0 : From card to memory
 * 1 : From memory to card
 */
#define VIA_CRDR_DMACTRL_DIR		0x100
#define VIA_CRDR_DMACTRL_ENIRQ		0x10000
#define VIA_CRDR_DMACTRL_SFTRST		0x1000000

#define VIA_CRDR_DMASTS		0xc

#define VIA_CRDR_DMASTART	0x10
/*0x14-0xFF reserved*/

/*
 * PCI Control Registers
 */

/*0x0 - 0x1 reserved*/
#define VIA_CRDR_PCICLKGATT	0x2
/*
 * SFTRST :
 * 0 : Soft reset all the controller and it will be de-asserted automatically
 * 1 : Soft reset is de-asserted
 */
#define VIA_CRDR_PCICLKGATT_SFTRST	0x01
/*
 * 3V3 : Pad power select
 * 0 : 1.8V
 * 1 : 3.3V
 * NOTE : No mater what the actual value should be, this bit always
 * read as 0. This is a hardware bug.
 */
#define VIA_CRDR_PCICLKGATT_3V3	0x10
/*
 * PAD_PWRON : Pad Power on/off select
 * 0 : Power off
 * 1 : Power on
  * NOTE : No mater what the actual value should be, this bit always
 * read as 0. This is a hardware bug.
 */
#define VIA_CRDR_PCICLKGATT_PAD_PWRON	0x20

#define VIA_CRDR_PCISDCCLK	0x5

#define VIA_CRDR_PCIDMACLK	0x7
#define VIA_CRDR_PCIDMACLK_SDC		0x2

#define VIA_CRDR_PCIINTCTRL	0x8
#define VIA_CRDR_PCIINTCTRL_SDCIRQEN	0x04

#define VIA_CRDR_PCIINTSTATUS	0x9
#define VIA_CRDR_PCIINTSTATUS_SDC	0x04

#define  VIA_CRDR_PCITMOCTRL	0xa
#define VIA_CRDR_PCITMOCTRL_NO		0x0
#define VIA_CRDR_PCITMOCTRL_32US	0x1
#define VIA_CRDR_PCITMOCTRL_256US	0x2
#define VIA_CRDR_PCITMOCTRL_1024US	0x3
#define VIA_CRDR_PCITMOCTRL_256MS	0x4
#define VIA_CRDR_PCITMOCTRL_512MS	0x5
#define VIA_CRDR_PCITMOCTRL_1024MS	0x6

/*0xB-0xFF reserved*/

enum PCI_HOST_CLK_CONTROL {
	PCI_CLK_375K = 0x03,
	PCI_CLK_8M = 0x04,
	PCI_CLK_12M = 0x00,
	PCI_CLK_16M = 0x05,
	PCI_CLK_24M = 0x01,
	PCI_CLK_33M = 0x06,
	PCI_CLK_48M = 0x02
};

struct sdhcreg {
	u32 sdcontrol_reg;
	u32 sdcmdarg_reg;
	u32 sdbusmode_reg;
	u32 sdblklen_reg;
	u32 sdresp_reg[4];
	u32 sdcurblkcnt_reg;
	u32 sdintmask_reg;
	u32 sdstatus_reg;
	u32 sdrsptmo_reg;
	u32 sdclksel_reg;
	u32 sdextctrl_reg;
};

struct pcictrlreg {
	u8 reserve[2];
	u8 pciclkgat_reg;
	u8 pcinfcclk_reg;
	u8 pcimscclk_reg;
	u8 pcisdclk_reg;
	u8 pcicaclk_reg;
	u8 pcidmaclk_reg;
	u8 pciintctrl_reg;
	u8 pciintstatus_reg;
	u8 pcitmoctrl_reg;
	u8 Resv;
};

struct via_crdr_mmc_host {
	struct mmc_host *mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;

	void __iomem *mmiobase;
	void __iomem *sdhc_mmiobase;
	void __iomem *ddma_mmiobase;
	void __iomem *pcictrl_mmiobase;

	struct pcictrlreg pm_pcictrl_reg;
	struct sdhcreg pm_sdhc_reg;

	struct work_struct carddet_work;
	struct tasklet_struct finish_tasklet;

	struct timer_list timer;
	spinlock_t lock;
	u8 power;
	int reject;
	unsigned int quirks;
};

/* some devices need a very long delay for power to stabilize */
#define VIA_CRDR_QUIRK_300MS_PWRDELAY	0x0001

static const struct pci_device_id via_ids[] = {
	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_9530,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0,},
	{0,}
};

MODULE_DEVICE_TABLE(pci, via_ids);

static void via_print_sdchc(struct via_crdr_mmc_host *host)
{
	void __iomem *addrbase = host->sdhc_mmiobase;

	pr_debug("SDC MMIO Registers:\n");
	pr_debug("SDCONTROL=%08x, SDCMDARG=%08x, SDBUSMODE=%08x\n",
		 readl(addrbase + VIA_CRDR_SDCTRL),
		 readl(addrbase + VIA_CRDR_SDCARG),
		 readl(addrbase + VIA_CRDR_SDBUSMODE));
	pr_debug("SDBLKLEN=%08x, SDCURBLKCNT=%08x, SDINTMASK=%08x\n",
		 readl(addrbase + VIA_CRDR_SDBLKLEN),
		 readl(addrbase + VIA_CRDR_SDCURBLKCNT),
		 readl(addrbase + VIA_CRDR_SDINTMASK));
	pr_debug("SDSTATUS=%08x, SDCLKSEL=%08x, SDEXTCTRL=%08x\n",
		 readl(addrbase + VIA_CRDR_SDSTATUS),
		 readl(addrbase + VIA_CRDR_SDCLKSEL),
		 readl(addrbase + VIA_CRDR_SDEXTCTRL));
}

static void via_print_pcictrl(struct via_crdr_mmc_host *host)
{
	void __iomem *addrbase = host->pcictrl_mmiobase;

	pr_debug("PCI Control Registers:\n");
	pr_debug("PCICLKGATT=%02x, PCISDCCLK=%02x, PCIDMACLK=%02x\n",
		 readb(addrbase + VIA_CRDR_PCICLKGATT),
		 readb(addrbase + VIA_CRDR_PCISDCCLK),
		 readb(addrbase + VIA_CRDR_PCIDMACLK));
	pr_debug("PCIINTCTRL=%02x, PCIINTSTATUS=%02x\n",
		 readb(addrbase + VIA_CRDR_PCIINTCTRL),
		 readb(addrbase + VIA_CRDR_PCIINTSTATUS));
}

static void via_save_pcictrlreg(struct via_crdr_mmc_host *host)
{
	struct pcictrlreg *pm_pcictrl_reg;
	void __iomem *addrbase;

	pm_pcictrl_reg = &(host->pm_pcictrl_reg);
	addrbase = host->pcictrl_mmiobase;

	pm_pcictrl_reg->pciclkgat_reg = readb(addrbase + VIA_CRDR_PCICLKGATT);
	pm_pcictrl_reg->pciclkgat_reg |=
		VIA_CRDR_PCICLKGATT_3V3 | VIA_CRDR_PCICLKGATT_PAD_PWRON;
	pm_pcictrl_reg->pcisdclk_reg = readb(addrbase + VIA_CRDR_PCISDCCLK);
	pm_pcictrl_reg->pcidmaclk_reg = readb(addrbase + VIA_CRDR_PCIDMACLK);
	pm_pcictrl_reg->pciintctrl_reg = readb(addrbase + VIA_CRDR_PCIINTCTRL);
	pm_pcictrl_reg->pciintstatus_reg =
		readb(addrbase + VIA_CRDR_PCIINTSTATUS);
	pm_pcictrl_reg->pcitmoctrl_reg = readb(addrbase + VIA_CRDR_PCITMOCTRL);
}

static void via_restore_pcictrlreg(struct via_crdr_mmc_host *host)
{
	struct pcictrlreg *pm_pcictrl_reg;
	void __iomem *addrbase;

	pm_pcictrl_reg = &(host->pm_pcictrl_reg);
	addrbase = host->pcictrl_mmiobase;

	writeb(pm_pcictrl_reg->pciclkgat_reg, addrbase + VIA_CRDR_PCICLKGATT);
	writeb(pm_pcictrl_reg->pcisdclk_reg, addrbase + VIA_CRDR_PCISDCCLK);
	writeb(pm_pcictrl_reg->pcidmaclk_reg, addrbase + VIA_CRDR_PCIDMACLK);
	writeb(pm_pcictrl_reg->pciintctrl_reg, addrbase + VIA_CRDR_PCIINTCTRL);
	writeb(pm_pcictrl_reg->pciintstatus_reg,
		addrbase + VIA_CRDR_PCIINTSTATUS);
	writeb(pm_pcictrl_reg->pcitmoctrl_reg, addrbase + VIA_CRDR_PCITMOCTRL);
}

static void via_save_sdcreg(struct via_crdr_mmc_host *host)
{
	struct sdhcreg *pm_sdhc_reg;
	void __iomem *addrbase;

	pm_sdhc_reg = &(host->pm_sdhc_reg);
	addrbase = host->sdhc_mmiobase;

	pm_sdhc_reg->sdcontrol_reg = readl(addrbase + VIA_CRDR_SDCTRL);
	pm_sdhc_reg->sdcmdarg_reg = readl(addrbase + VIA_CRDR_SDCARG);
	pm_sdhc_reg->sdbusmode_reg = readl(addrbase + VIA_CRDR_SDBUSMODE);
	pm_sdhc_reg->sdblklen_reg = readl(addrbase + VIA_CRDR_SDBLKLEN);
	pm_sdhc_reg->sdcurblkcnt_reg = readl(addrbase + VIA_CRDR_SDCURBLKCNT);
	pm_sdhc_reg->sdintmask_reg = readl(addrbase + VIA_CRDR_SDINTMASK);
	pm_sdhc_reg->sdstatus_reg = readl(addrbase + VIA_CRDR_SDSTATUS);
	pm_sdhc_reg->sdrsptmo_reg = readl(addrbase + VIA_CRDR_SDRSPTMO);
	pm_sdhc_reg->sdclksel_reg = readl(addrbase + VIA_CRDR_SDCLKSEL);
	pm_sdhc_reg->sdextctrl_reg = readl(addrbase + VIA_CRDR_SDEXTCTRL);
}

static void via_restore_sdcreg(struct via_crdr_mmc_host *host)
{
	struct sdhcreg *pm_sdhc_reg;
	void __iomem *addrbase;

	pm_sdhc_reg = &(host->pm_sdhc_reg);
	addrbase = host->sdhc_mmiobase;

	writel(pm_sdhc_reg->sdcontrol_reg, addrbase + VIA_CRDR_SDCTRL);
	writel(pm_sdhc_reg->sdcmdarg_reg, addrbase + VIA_CRDR_SDCARG);
	writel(pm_sdhc_reg->sdbusmode_reg, addrbase + VIA_CRDR_SDBUSMODE);
	writel(pm_sdhc_reg->sdblklen_reg, addrbase + VIA_CRDR_SDBLKLEN);
	writel(pm_sdhc_reg->sdcurblkcnt_reg, addrbase + VIA_CRDR_SDCURBLKCNT);
	writel(pm_sdhc_reg->sdintmask_reg, addrbase + VIA_CRDR_SDINTMASK);
	writel(pm_sdhc_reg->sdstatus_reg, addrbase + VIA_CRDR_SDSTATUS);
	writel(pm_sdhc_reg->sdrsptmo_reg, addrbase + VIA_CRDR_SDRSPTMO);
	writel(pm_sdhc_reg->sdclksel_reg, addrbase + VIA_CRDR_SDCLKSEL);
	writel(pm_sdhc_reg->sdextctrl_reg, addrbase + VIA_CRDR_SDEXTCTRL);
}

static void via_pwron_sleep(struct via_crdr_mmc_host *sdhost)
{
	if (sdhost->quirks & VIA_CRDR_QUIRK_300MS_PWRDELAY)
		msleep(300);
	else
		msleep(3);
}

static void via_set_ddma(struct via_crdr_mmc_host *host,
			 dma_addr_t dmaaddr, u32 count, int dir, int enirq)
{
	void __iomem *addrbase;
	u32 ctrl_data = 0;

	if (enirq)
		ctrl_data |= VIA_CRDR_DMACTRL_ENIRQ;

	if (dir)
		ctrl_data |= VIA_CRDR_DMACTRL_DIR;

	addrbase = host->ddma_mmiobase;

	writel(dmaaddr, addrbase + VIA_CRDR_DMABASEADD);
	writel(count, addrbase + VIA_CRDR_DMACOUNTER);
	writel(ctrl_data, addrbase + VIA_CRDR_DMACTRL);
	writel(0x01, addrbase + VIA_CRDR_DMASTART);

	/* It seems that our DMA can not work normally with 375kHz clock */
	/* FIXME: don't brute-force 8MHz but use PIO at 375kHz !! */
	addrbase = host->pcictrl_mmiobase;
	if (readb(addrbase + VIA_CRDR_PCISDCCLK) == PCI_CLK_375K) {
		dev_info(host->mmc->parent, "forcing card speed to 8MHz\n");
		writeb(PCI_CLK_8M, addrbase + VIA_CRDR_PCISDCCLK);
	}
}

static void via_sdc_preparedata(struct via_crdr_mmc_host *host,
				struct mmc_data *data)
{
	void __iomem *addrbase;
	u32 blk_reg;
	int count;

	WARN_ON(host->data);

	/* Sanity checks */
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > host->mmc->max_blk_count);

	host->data = data;

	count = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
		((data->flags & MMC_DATA_READ) ?
		PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE));
	BUG_ON(count != 1);

	via_set_ddma(host, sg_dma_address(data->sg), sg_dma_len(data->sg),
		(data->flags & MMC_DATA_WRITE) ? 1 : 0, 1);

	addrbase = host->sdhc_mmiobase;

	blk_reg = data->blksz - 1;
	blk_reg |= VIA_CRDR_SDBLKLEN_GPIDET | VIA_CRDR_SDBLKLEN_INTEN;
	blk_reg |= (data->blocks) << 16;

	writel(blk_reg, addrbase + VIA_CRDR_SDBLKLEN);
}

static void via_sdc_get_response(struct via_crdr_mmc_host *host,
				 struct mmc_command *cmd)
{
	void __iomem *addrbase = host->sdhc_mmiobase;
	u32 dwdata0 = readl(addrbase + VIA_CRDR_SDRESP0);
	u32 dwdata1 = readl(addrbase + VIA_CRDR_SDRESP1);
	u32 dwdata2 = readl(addrbase + VIA_CRDR_SDRESP2);
	u32 dwdata3 = readl(addrbase + VIA_CRDR_SDRESP3);

	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = ((u8) (dwdata1)) |
		    (((u8) (dwdata0 >> 24)) << 8) |
		    (((u8) (dwdata0 >> 16)) << 16) |
		    (((u8) (dwdata0 >> 8)) << 24);

		cmd->resp[1] = ((u8) (dwdata2)) |
		    (((u8) (dwdata1 >> 24)) << 8) |
		    (((u8) (dwdata1 >> 16)) << 16) |
		    (((u8) (dwdata1 >> 8)) << 24);

		cmd->resp[2] = ((u8) (dwdata3)) |
		    (((u8) (dwdata2 >> 24)) << 8) |
		    (((u8) (dwdata2 >> 16)) << 16) |
		    (((u8) (dwdata2 >> 8)) << 24);

		cmd->resp[3] = 0xff |
		    ((((u8) (dwdata3 >> 24))) << 8) |
		    (((u8) (dwdata3 >> 16)) << 16) |
		    (((u8) (dwdata3 >> 8)) << 24);
	} else {
		dwdata0 >>= 8;
		cmd->resp[0] = ((dwdata0 & 0xff) << 24) |
		    (((dwdata0 >> 8) & 0xff) << 16) |
		    (((dwdata0 >> 16) & 0xff) << 8) | (dwdata1 & 0xff);

		dwdata1 >>= 8;
		cmd->resp[1] = ((dwdata1 & 0xff) << 24) |
		    (((dwdata1 >> 8) & 0xff) << 16) |
		    (((dwdata1 >> 16) & 0xff) << 8);
	}
}

static void via_sdc_send_command(struct via_crdr_mmc_host *host,
				 struct mmc_command *cmd)
{
	void __iomem *addrbase;
	struct mmc_data *data;
	u32 cmdctrl = 0;

	WARN_ON(host->cmd);

	data = cmd->data;
	mod_timer(&host->timer, jiffies + HZ);
	host->cmd = cmd;

	/*Command index*/
	cmdctrl = cmd->opcode << 8;

	/*Response type*/
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		cmdctrl |= VIA_CRDR_SDCTRL_RSP_NONE;
		break;
	case MMC_RSP_R1:
		cmdctrl |= VIA_CRDR_SDCTRL_RSP_R1;
		break;
	case MMC_RSP_R1B:
		cmdctrl |= VIA_CRDR_SDCTRL_RSP_R1B;
		break;
	case MMC_RSP_R2:
		cmdctrl |= VIA_CRDR_SDCTRL_RSP_R2;
		break;
	case MMC_RSP_R3:
		cmdctrl |= VIA_CRDR_SDCTRL_RSP_R3;
		break;
	default:
		pr_err("%s: cmd->flag is not valid\n", mmc_hostname(host->mmc));
		break;
	}

	if (!(cmd->data))
		goto nodata;

	via_sdc_preparedata(host, data);

	/*Command control*/
	if (data->blocks > 1) {
		if (data->flags & MMC_DATA_WRITE) {
			cmdctrl |= VIA_CRDR_SDCTRL_WRITE;
			cmdctrl |= VIA_CRDR_SDCTRL_MULTI_WR;
		} else {
			cmdctrl |= VIA_CRDR_SDCTRL_MULTI_RD;
		}
	} else {
		if (data->flags & MMC_DATA_WRITE) {
			cmdctrl |= VIA_CRDR_SDCTRL_WRITE;
			cmdctrl |= VIA_CRDR_SDCTRL_SINGLE_WR;
		} else {
			cmdctrl |= VIA_CRDR_SDCTRL_SINGLE_RD;
		}
	}

nodata:
	if (cmd == host->mrq->stop)
		cmdctrl |= VIA_CRDR_SDCTRL_STOP;

	cmdctrl |= VIA_CRDR_SDCTRL_START;

	addrbase = host->sdhc_mmiobase;
	writel(cmd->arg, addrbase + VIA_CRDR_SDCARG);
	writel(cmdctrl, addrbase + VIA_CRDR_SDCTRL);
}

static void via_sdc_finish_data(struct via_crdr_mmc_host *host)
{
	struct mmc_data *data;

	BUG_ON(!host->data);

	data = host->data;
	host->data = NULL;

	if (data->error)
		data->bytes_xfered = 0;
	else
		data->bytes_xfered = data->blocks * data->blksz;

	dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
		((data->flags & MMC_DATA_READ) ?
		PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE));

	if (data->stop)
		via_sdc_send_command(host, data->stop);
	else
		tasklet_schedule(&host->finish_tasklet);
}

static void via_sdc_finish_command(struct via_crdr_mmc_host *host)
{
	via_sdc_get_response(host, host->cmd);

	host->cmd->error = 0;

	if (!host->cmd->data)
		tasklet_schedule(&host->finish_tasklet);

	host->cmd = NULL;
}

static void via_sdc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	void __iomem *addrbase;
	struct via_crdr_mmc_host *host;
	unsigned long flags;
	u16 status;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	addrbase = host->pcictrl_mmiobase;
	writeb(VIA_CRDR_PCIDMACLK_SDC, addrbase + VIA_CRDR_PCIDMACLK);

	status = readw(host->sdhc_mmiobase + VIA_CRDR_SDSTATUS);
	status &= VIA_CRDR_SDSTS_W1C_MASK;
	writew(status, host->sdhc_mmiobase + VIA_CRDR_SDSTATUS);

	WARN_ON(host->mrq != NULL);
	host->mrq = mrq;

	status = readw(host->sdhc_mmiobase + VIA_CRDR_SDSTATUS);
	if (!(status & VIA_CRDR_SDSTS_SLOTG) || host->reject) {
		host->mrq->cmd->error = -ENOMEDIUM;
		tasklet_schedule(&host->finish_tasklet);
	} else {
		via_sdc_send_command(host, mrq->cmd);
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static void via_sdc_set_power(struct via_crdr_mmc_host *host,
			      unsigned short power, unsigned int on)
{
	unsigned long flags;
	u8 gatt;

	spin_lock_irqsave(&host->lock, flags);

	host->power = (1 << power);

	gatt = readb(host->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	if (host->power == MMC_VDD_165_195)
		gatt &= ~VIA_CRDR_PCICLKGATT_3V3;
	else
		gatt |= VIA_CRDR_PCICLKGATT_3V3;
	if (on)
		gatt |= VIA_CRDR_PCICLKGATT_PAD_PWRON;
	else
		gatt &= ~VIA_CRDR_PCICLKGATT_PAD_PWRON;
	writeb(gatt, host->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	via_pwron_sleep(host);
}

static void via_sdc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct via_crdr_mmc_host *host;
	unsigned long flags;
	void __iomem *addrbase;
	u32 org_data, sdextctrl;
	u8 clock;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	addrbase = host->sdhc_mmiobase;
	org_data = readl(addrbase + VIA_CRDR_SDBUSMODE);
	sdextctrl = readl(addrbase + VIA_CRDR_SDEXTCTRL);

	if (ios->bus_width == MMC_BUS_WIDTH_1)
		org_data &= ~VIA_CRDR_SDMODE_4BIT;
	else
		org_data |= VIA_CRDR_SDMODE_4BIT;

	if (ios->power_mode == MMC_POWER_OFF)
		org_data &= ~VIA_CRDR_SDMODE_CLK_ON;
	else
		org_data |= VIA_CRDR_SDMODE_CLK_ON;

	if (ios->timing == MMC_TIMING_SD_HS)
		sdextctrl |= VIA_CRDR_SDEXTCTRL_HISPD;
	else
		sdextctrl &= ~VIA_CRDR_SDEXTCTRL_HISPD;

	writel(org_data, addrbase + VIA_CRDR_SDBUSMODE);
	writel(sdextctrl, addrbase + VIA_CRDR_SDEXTCTRL);

	if (ios->clock >= 48000000)
		clock = PCI_CLK_48M;
	else if (ios->clock >= 33000000)
		clock = PCI_CLK_33M;
	else if (ios->clock >= 24000000)
		clock = PCI_CLK_24M;
	else if (ios->clock >= 16000000)
		clock = PCI_CLK_16M;
	else if (ios->clock >= 12000000)
		clock = PCI_CLK_12M;
	else if (ios->clock >=  8000000)
		clock = PCI_CLK_8M;
	else
		clock = PCI_CLK_375K;

	addrbase = host->pcictrl_mmiobase;
	if (readb(addrbase + VIA_CRDR_PCISDCCLK) != clock)
		writeb(clock, addrbase + VIA_CRDR_PCISDCCLK);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	if (ios->power_mode != MMC_POWER_OFF)
		via_sdc_set_power(host, ios->vdd, 1);
	else
		via_sdc_set_power(host, ios->vdd, 0);
}

static int via_sdc_get_ro(struct mmc_host *mmc)
{
	struct via_crdr_mmc_host *host;
	unsigned long flags;
	u16 status;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	status = readw(host->sdhc_mmiobase + VIA_CRDR_SDSTATUS);

	spin_unlock_irqrestore(&host->lock, flags);

	return !(status & VIA_CRDR_SDSTS_WP);
}

static const struct mmc_host_ops via_sdc_ops = {
	.request = via_sdc_request,
	.set_ios = via_sdc_set_ios,
	.get_ro = via_sdc_get_ro,
};

static void via_reset_pcictrl(struct via_crdr_mmc_host *host)
{
	unsigned long flags;
	u8 gatt;

	spin_lock_irqsave(&host->lock, flags);

	via_save_pcictrlreg(host);
	via_save_sdcreg(host);

	spin_unlock_irqrestore(&host->lock, flags);

	gatt = VIA_CRDR_PCICLKGATT_PAD_PWRON;
	if (host->power == MMC_VDD_165_195)
		gatt &= VIA_CRDR_PCICLKGATT_3V3;
	else
		gatt |= VIA_CRDR_PCICLKGATT_3V3;
	writeb(gatt, host->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	via_pwron_sleep(host);
	gatt |= VIA_CRDR_PCICLKGATT_SFTRST;
	writeb(gatt, host->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	msleep(3);

	spin_lock_irqsave(&host->lock, flags);

	via_restore_pcictrlreg(host);
	via_restore_sdcreg(host);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static void via_sdc_cmd_isr(struct via_crdr_mmc_host *host, u16 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->cmd) {
		pr_err("%s: Got command interrupt 0x%x even "
		       "though no command operation was in progress.\n",
		       mmc_hostname(host->mmc), intmask);
		return;
	}

	if (intmask & VIA_CRDR_SDSTS_CRTO)
		host->cmd->error = -ETIMEDOUT;
	else if (intmask & VIA_CRDR_SDSTS_SC)
		host->cmd->error = -EILSEQ;

	if (host->cmd->error)
		tasklet_schedule(&host->finish_tasklet);
	else if (intmask & VIA_CRDR_SDSTS_CRD)
		via_sdc_finish_command(host);
}

static void via_sdc_data_isr(struct via_crdr_mmc_host *host, u16 intmask)
{
	BUG_ON(intmask == 0);

	if (intmask & VIA_CRDR_SDSTS_DT)
		host->data->error = -ETIMEDOUT;
	else if (intmask & (VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC))
		host->data->error = -EILSEQ;

	via_sdc_finish_data(host);
}

static irqreturn_t via_sdc_isr(int irq, void *dev_id)
{
	struct via_crdr_mmc_host *sdhost = dev_id;
	void __iomem *addrbase;
	u8 pci_status;
	u16 sd_status;
	irqreturn_t result;

	if (!sdhost)
		return IRQ_NONE;

	spin_lock(&sdhost->lock);

	addrbase = sdhost->pcictrl_mmiobase;
	pci_status = readb(addrbase + VIA_CRDR_PCIINTSTATUS);
	if (!(pci_status & VIA_CRDR_PCIINTSTATUS_SDC)) {
		result = IRQ_NONE;
		goto out;
	}

	addrbase = sdhost->sdhc_mmiobase;
	sd_status = readw(addrbase + VIA_CRDR_SDSTATUS);
	sd_status &= VIA_CRDR_SDSTS_INT_MASK;
	sd_status &= ~VIA_CRDR_SDSTS_IGN_MASK;
	if (!sd_status) {
		result = IRQ_NONE;
		goto out;
	}

	if (sd_status & VIA_CRDR_SDSTS_CIR) {
		writew(sd_status & VIA_CRDR_SDSTS_CIR,
			addrbase + VIA_CRDR_SDSTATUS);

		schedule_work(&sdhost->carddet_work);
	}

	sd_status &= ~VIA_CRDR_SDSTS_CIR;
	if (sd_status & VIA_CRDR_SDSTS_CMD_MASK) {
		writew(sd_status & VIA_CRDR_SDSTS_CMD_MASK,
			addrbase + VIA_CRDR_SDSTATUS);
		via_sdc_cmd_isr(sdhost, sd_status & VIA_CRDR_SDSTS_CMD_MASK);
	}
	if (sd_status & VIA_CRDR_SDSTS_DATA_MASK) {
		writew(sd_status & VIA_CRDR_SDSTS_DATA_MASK,
			addrbase + VIA_CRDR_SDSTATUS);
		via_sdc_data_isr(sdhost, sd_status & VIA_CRDR_SDSTS_DATA_MASK);
	}

	sd_status &= ~(VIA_CRDR_SDSTS_CMD_MASK | VIA_CRDR_SDSTS_DATA_MASK);
	if (sd_status) {
		pr_err("%s: Unexpected interrupt 0x%x\n",
		       mmc_hostname(sdhost->mmc), sd_status);
		writew(sd_status, addrbase + VIA_CRDR_SDSTATUS);
	}

	result = IRQ_HANDLED;

	mmiowb();
out:
	spin_unlock(&sdhost->lock);

	return result;
}

static void via_sdc_timeout(struct timer_list *t)
{
	struct via_crdr_mmc_host *sdhost;
	unsigned long flags;

	sdhost = from_timer(sdhost, t, timer);

	spin_lock_irqsave(&sdhost->lock, flags);

	if (sdhost->mrq) {
		pr_err("%s: Timeout waiting for hardware interrupt."
		       "cmd:0x%x\n", mmc_hostname(sdhost->mmc),
		       sdhost->mrq->cmd->opcode);

		if (sdhost->data) {
			writel(VIA_CRDR_DMACTRL_SFTRST,
				sdhost->ddma_mmiobase + VIA_CRDR_DMACTRL);
			sdhost->data->error = -ETIMEDOUT;
			via_sdc_finish_data(sdhost);
		} else {
			if (sdhost->cmd)
				sdhost->cmd->error = -ETIMEDOUT;
			else
				sdhost->mrq->cmd->error = -ETIMEDOUT;
			tasklet_schedule(&sdhost->finish_tasklet);
		}
	}

	mmiowb();
	spin_unlock_irqrestore(&sdhost->lock, flags);
}

static void via_sdc_tasklet_finish(unsigned long param)
{
	struct via_crdr_mmc_host *host;
	unsigned long flags;
	struct mmc_request *mrq;

	host = (struct via_crdr_mmc_host *)param;

	spin_lock_irqsave(&host->lock, flags);

	del_timer(&host->timer);
	mrq = host->mrq;
	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	spin_unlock_irqrestore(&host->lock, flags);

	mmc_request_done(host->mmc, mrq);
}

static void via_sdc_card_detect(struct work_struct *work)
{
	struct via_crdr_mmc_host *host;
	void __iomem *addrbase;
	unsigned long flags;
	u16 status;

	host = container_of(work, struct via_crdr_mmc_host, carddet_work);

	addrbase = host->ddma_mmiobase;
	writel(VIA_CRDR_DMACTRL_SFTRST, addrbase + VIA_CRDR_DMACTRL);

	spin_lock_irqsave(&host->lock, flags);

	addrbase = host->pcictrl_mmiobase;
	writeb(VIA_CRDR_PCIDMACLK_SDC, addrbase + VIA_CRDR_PCIDMACLK);

	addrbase = host->sdhc_mmiobase;
	status = readw(addrbase + VIA_CRDR_SDSTATUS);
	if (!(status & VIA_CRDR_SDSTS_SLOTG)) {
		if (host->mrq) {
			pr_err("%s: Card removed during transfer!\n",
			       mmc_hostname(host->mmc));
			host->mrq->cmd->error = -ENOMEDIUM;
			tasklet_schedule(&host->finish_tasklet);
		}

		mmiowb();
		spin_unlock_irqrestore(&host->lock, flags);

		via_reset_pcictrl(host);

		spin_lock_irqsave(&host->lock, flags);
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	via_print_pcictrl(host);
	via_print_sdchc(host);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));
}

static void via_init_mmc_host(struct via_crdr_mmc_host *host)
{
	struct mmc_host *mmc = host->mmc;
	void __iomem *addrbase;
	u32 lenreg;
	u32 status;

	timer_setup(&host->timer, via_sdc_timeout, 0);

	spin_lock_init(&host->lock);

	mmc->f_min = VIA_CRDR_MIN_CLOCK;
	mmc->f_max = VIA_CRDR_MAX_CLOCK;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED;
	mmc->ops = &via_sdc_ops;

	/*Hardware cannot do scatter lists*/
	mmc->max_segs = 1;

	mmc->max_blk_size = VIA_CRDR_MAX_BLOCK_LENGTH;
	mmc->max_blk_count = VIA_CRDR_MAX_BLOCK_COUNT;

	mmc->max_seg_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_req_size = mmc->max_seg_size;

	INIT_WORK(&host->carddet_work, via_sdc_card_detect);

	tasklet_init(&host->finish_tasklet, via_sdc_tasklet_finish,
		     (unsigned long)host);

	addrbase = host->sdhc_mmiobase;
	writel(0x0, addrbase + VIA_CRDR_SDINTMASK);
	msleep(1);

	lenreg = VIA_CRDR_SDBLKLEN_GPIDET | VIA_CRDR_SDBLKLEN_INTEN;
	writel(lenreg, addrbase + VIA_CRDR_SDBLKLEN);

	status = readw(addrbase + VIA_CRDR_SDSTATUS);
	status &= VIA_CRDR_SDSTS_W1C_MASK;
	writew(status, addrbase + VIA_CRDR_SDSTATUS);

	status = readw(addrbase + VIA_CRDR_SDSTATUS2);
	status |= VIA_CRDR_SDSTS_CFE;
	writew(status, addrbase + VIA_CRDR_SDSTATUS2);

	writeb(0x0, addrbase + VIA_CRDR_SDEXTCTRL);

	writel(VIA_CRDR_SDACTIVE_INTMASK, addrbase + VIA_CRDR_SDINTMASK);
	msleep(1);
}

static int via_sd_probe(struct pci_dev *pcidev,
				    const struct pci_device_id *id)
{
	struct mmc_host *mmc;
	struct via_crdr_mmc_host *sdhost;
	u32 base, len;
	u8  gatt;
	int ret;

	pr_info(DRV_NAME
		": VIA SDMMC controller found at %s [%04x:%04x] (rev %x)\n",
		pci_name(pcidev), (int)pcidev->vendor, (int)pcidev->device,
		(int)pcidev->revision);

	ret = pci_enable_device(pcidev);
	if (ret)
		return ret;

	ret = pci_request_regions(pcidev, DRV_NAME);
	if (ret)
		goto disable;

	pci_write_config_byte(pcidev, VIA_CRDR_PCI_WORK_MODE, 0);
	pci_write_config_byte(pcidev, VIA_CRDR_PCI_DBG_MODE, 0);

	mmc = mmc_alloc_host(sizeof(struct via_crdr_mmc_host), &pcidev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto release;
	}

	sdhost = mmc_priv(mmc);
	sdhost->mmc = mmc;
	dev_set_drvdata(&pcidev->dev, sdhost);

	len = pci_resource_len(pcidev, 0);
	base = pci_resource_start(pcidev, 0);
	sdhost->mmiobase = ioremap_nocache(base, len);
	if (!sdhost->mmiobase) {
		ret = -ENOMEM;
		goto free_mmc_host;
	}

	sdhost->sdhc_mmiobase =
		sdhost->mmiobase + VIA_CRDR_SDC_OFF;
	sdhost->ddma_mmiobase =
		sdhost->mmiobase + VIA_CRDR_DDMA_OFF;
	sdhost->pcictrl_mmiobase =
		sdhost->mmiobase + VIA_CRDR_PCICTRL_OFF;

	sdhost->power = MMC_VDD_165_195;

	gatt = VIA_CRDR_PCICLKGATT_3V3 | VIA_CRDR_PCICLKGATT_PAD_PWRON;
	writeb(gatt, sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	via_pwron_sleep(sdhost);
	gatt |= VIA_CRDR_PCICLKGATT_SFTRST;
	writeb(gatt, sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	msleep(3);

	via_init_mmc_host(sdhost);

	ret =
	    request_irq(pcidev->irq, via_sdc_isr, IRQF_SHARED, DRV_NAME,
			sdhost);
	if (ret)
		goto unmap;

	writeb(VIA_CRDR_PCIINTCTRL_SDCIRQEN,
	       sdhost->pcictrl_mmiobase + VIA_CRDR_PCIINTCTRL);
	writeb(VIA_CRDR_PCITMOCTRL_1024MS,
	       sdhost->pcictrl_mmiobase + VIA_CRDR_PCITMOCTRL);

	/* device-specific quirks */
	if (pcidev->subsystem_vendor == PCI_VENDOR_ID_LENOVO &&
	    pcidev->subsystem_device == 0x3891)
		sdhost->quirks = VIA_CRDR_QUIRK_300MS_PWRDELAY;

	mmc_add_host(mmc);

	return 0;

unmap:
	iounmap(sdhost->mmiobase);
free_mmc_host:
	dev_set_drvdata(&pcidev->dev, NULL);
	mmc_free_host(mmc);
release:
	pci_release_regions(pcidev);
disable:
	pci_disable_device(pcidev);

	return ret;
}

static void via_sd_remove(struct pci_dev *pcidev)
{
	struct via_crdr_mmc_host *sdhost = pci_get_drvdata(pcidev);
	unsigned long flags;
	u8 gatt;

	spin_lock_irqsave(&sdhost->lock, flags);

	/* Ensure we don't accept more commands from mmc layer */
	sdhost->reject = 1;

	/* Disable generating further interrupts */
	writeb(0x0, sdhost->pcictrl_mmiobase + VIA_CRDR_PCIINTCTRL);
	mmiowb();

	if (sdhost->mrq) {
		pr_err("%s: Controller removed during "
			"transfer\n", mmc_hostname(sdhost->mmc));

		/* make sure all DMA is stopped */
		writel(VIA_CRDR_DMACTRL_SFTRST,
			sdhost->ddma_mmiobase + VIA_CRDR_DMACTRL);
		mmiowb();
		sdhost->mrq->cmd->error = -ENOMEDIUM;
		if (sdhost->mrq->stop)
			sdhost->mrq->stop->error = -ENOMEDIUM;
		tasklet_schedule(&sdhost->finish_tasklet);
	}
	spin_unlock_irqrestore(&sdhost->lock, flags);

	mmc_remove_host(sdhost->mmc);

	free_irq(pcidev->irq, sdhost);

	del_timer_sync(&sdhost->timer);

	tasklet_kill(&sdhost->finish_tasklet);

	/* switch off power */
	gatt = readb(sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	gatt &= ~VIA_CRDR_PCICLKGATT_PAD_PWRON;
	writeb(gatt, sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);

	iounmap(sdhost->mmiobase);
	dev_set_drvdata(&pcidev->dev, NULL);
	mmc_free_host(sdhost->mmc);
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);

	pr_info(DRV_NAME
		": VIA SDMMC controller at %s [%04x:%04x] has been removed\n",
		pci_name(pcidev), (int)pcidev->vendor, (int)pcidev->device);
}

#ifdef CONFIG_PM

static void via_init_sdc_pm(struct via_crdr_mmc_host *host)
{
	struct sdhcreg *pm_sdhcreg;
	void __iomem *addrbase;
	u32 lenreg;
	u16 status;

	pm_sdhcreg = &(host->pm_sdhc_reg);
	addrbase = host->sdhc_mmiobase;

	writel(0x0, addrbase + VIA_CRDR_SDINTMASK);

	lenreg = VIA_CRDR_SDBLKLEN_GPIDET | VIA_CRDR_SDBLKLEN_INTEN;
	writel(lenreg, addrbase + VIA_CRDR_SDBLKLEN);

	status = readw(addrbase + VIA_CRDR_SDSTATUS);
	status &= VIA_CRDR_SDSTS_W1C_MASK;
	writew(status, addrbase + VIA_CRDR_SDSTATUS);

	status = readw(addrbase + VIA_CRDR_SDSTATUS2);
	status |= VIA_CRDR_SDSTS_CFE;
	writew(status, addrbase + VIA_CRDR_SDSTATUS2);

	writel(pm_sdhcreg->sdcontrol_reg, addrbase + VIA_CRDR_SDCTRL);
	writel(pm_sdhcreg->sdcmdarg_reg, addrbase + VIA_CRDR_SDCARG);
	writel(pm_sdhcreg->sdintmask_reg, addrbase + VIA_CRDR_SDINTMASK);
	writel(pm_sdhcreg->sdrsptmo_reg, addrbase + VIA_CRDR_SDRSPTMO);
	writel(pm_sdhcreg->sdclksel_reg, addrbase + VIA_CRDR_SDCLKSEL);
	writel(pm_sdhcreg->sdextctrl_reg, addrbase + VIA_CRDR_SDEXTCTRL);

	via_print_pcictrl(host);
	via_print_sdchc(host);
}

static int via_sd_suspend(struct pci_dev *pcidev, pm_message_t state)
{
	struct via_crdr_mmc_host *host;

	host = pci_get_drvdata(pcidev);

	via_save_pcictrlreg(host);
	via_save_sdcreg(host);

	pci_save_state(pcidev);
	pci_enable_wake(pcidev, pci_choose_state(pcidev, state), 0);
	pci_disable_device(pcidev);
	pci_set_power_state(pcidev, pci_choose_state(pcidev, state));

	return 0;
}

static int via_sd_resume(struct pci_dev *pcidev)
{
	struct via_crdr_mmc_host *sdhost;
	int ret = 0;
	u8 gatt;

	sdhost = pci_get_drvdata(pcidev);

	gatt = VIA_CRDR_PCICLKGATT_PAD_PWRON;
	if (sdhost->power == MMC_VDD_165_195)
		gatt &= ~VIA_CRDR_PCICLKGATT_3V3;
	else
		gatt |= VIA_CRDR_PCICLKGATT_3V3;
	writeb(gatt, sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	via_pwron_sleep(sdhost);
	gatt |= VIA_CRDR_PCICLKGATT_SFTRST;
	writeb(gatt, sdhost->pcictrl_mmiobase + VIA_CRDR_PCICLKGATT);
	msleep(3);

	msleep(100);

	pci_set_power_state(pcidev, PCI_D0);
	pci_restore_state(pcidev);
	ret = pci_enable_device(pcidev);
	if (ret)
		return ret;

	via_restore_pcictrlreg(sdhost);
	via_init_sdc_pm(sdhost);

	return ret;
}

#else /* CONFIG_PM */

#define via_sd_suspend NULL
#define via_sd_resume NULL

#endif /* CONFIG_PM */

static struct pci_driver via_sd_driver = {
	.name = DRV_NAME,
	.id_table = via_ids,
	.probe = via_sd_probe,
	.remove = via_sd_remove,
	.suspend = via_sd_suspend,
	.resume = via_sd_resume,
};

module_pci_driver(via_sd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VIA Technologies Inc.");
MODULE_DESCRIPTION("VIA SD/MMC Card Interface driver");
