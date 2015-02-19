/*
 * AMD am53c974 driver.
 * Copyright (c) 2014 Hannes Reinecke, SUSE Linux GmbH
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi_host.h>

#include "esp_scsi.h"

#define DRV_MODULE_NAME "am53c974"
#define DRV_MODULE_VERSION "1.00"

static bool am53c974_debug;
static bool am53c974_fenab = true;

#define esp_dma_log(f, a...)						\
	do {								\
		if (am53c974_debug)					\
			shost_printk(KERN_DEBUG, esp->host, f, ##a);	\
	} while (0)

#define ESP_DMA_CMD 0x10
#define ESP_DMA_STC 0x11
#define ESP_DMA_SPA 0x12
#define ESP_DMA_WBC 0x13
#define ESP_DMA_WAC 0x14
#define ESP_DMA_STATUS 0x15
#define ESP_DMA_SMDLA 0x16
#define ESP_DMA_WMAC 0x17

#define ESP_DMA_CMD_IDLE 0x00
#define ESP_DMA_CMD_BLAST 0x01
#define ESP_DMA_CMD_ABORT 0x02
#define ESP_DMA_CMD_START 0x03
#define ESP_DMA_CMD_MASK  0x03
#define ESP_DMA_CMD_DIAG 0x04
#define ESP_DMA_CMD_MDL 0x10
#define ESP_DMA_CMD_INTE_P 0x20
#define ESP_DMA_CMD_INTE_D 0x40
#define ESP_DMA_CMD_DIR 0x80

#define ESP_DMA_STAT_PWDN 0x01
#define ESP_DMA_STAT_ERROR 0x02
#define ESP_DMA_STAT_ABORT 0x04
#define ESP_DMA_STAT_DONE 0x08
#define ESP_DMA_STAT_SCSIINT 0x10
#define ESP_DMA_STAT_BCMPLT 0x20

/* EEPROM is accessed with 16-bit values */
#define DC390_EEPROM_READ 0x80
#define DC390_EEPROM_LEN 0x40

/*
 * DC390 EEPROM
 *
 * 8 * 4 bytes of per-device options
 * followed by HBA specific options
 */

/* Per-device options */
#define DC390_EE_MODE1 0x00
#define DC390_EE_SPEED 0x01

/* HBA-specific options */
#define DC390_EE_ADAPT_SCSI_ID 0x40
#define DC390_EE_MODE2 0x41
#define DC390_EE_DELAY 0x42
#define DC390_EE_TAG_CMD_NUM 0x43

#define DC390_EE_MODE1_PARITY_CHK   0x01
#define DC390_EE_MODE1_SYNC_NEGO    0x02
#define DC390_EE_MODE1_EN_DISC      0x04
#define DC390_EE_MODE1_SEND_START   0x08
#define DC390_EE_MODE1_TCQ          0x10

#define DC390_EE_MODE2_MORE_2DRV    0x01
#define DC390_EE_MODE2_GREATER_1G   0x02
#define DC390_EE_MODE2_RST_SCSI_BUS 0x04
#define DC390_EE_MODE2_ACTIVE_NEGATION 0x08
#define DC390_EE_MODE2_NO_SEEK      0x10
#define DC390_EE_MODE2_LUN_CHECK    0x20

struct pci_esp_priv {
	struct esp *esp;
	u8 dma_status;
};

static void pci_esp_dma_drain(struct esp *esp);

static inline struct pci_esp_priv *pci_esp_get_priv(struct esp *esp)
{
	struct pci_dev *pdev = esp->dev;

	return pci_get_drvdata(pdev);
}

static void pci_esp_write8(struct esp *esp, u8 val, unsigned long reg)
{
	iowrite8(val, esp->regs + (reg * 4UL));
}

static u8 pci_esp_read8(struct esp *esp, unsigned long reg)
{
	return ioread8(esp->regs + (reg * 4UL));
}

static void pci_esp_write32(struct esp *esp, u32 val, unsigned long reg)
{
	return iowrite32(val, esp->regs + (reg * 4UL));
}

static dma_addr_t pci_esp_map_single(struct esp *esp, void *buf,
				     size_t sz, int dir)
{
	return pci_map_single(esp->dev, buf, sz, dir);
}

static int pci_esp_map_sg(struct esp *esp, struct scatterlist *sg,
			  int num_sg, int dir)
{
	return pci_map_sg(esp->dev, sg, num_sg, dir);
}

static void pci_esp_unmap_single(struct esp *esp, dma_addr_t addr,
				 size_t sz, int dir)
{
	pci_unmap_single(esp->dev, addr, sz, dir);
}

static void pci_esp_unmap_sg(struct esp *esp, struct scatterlist *sg,
			     int num_sg, int dir)
{
	pci_unmap_sg(esp->dev, sg, num_sg, dir);
}

static int pci_esp_irq_pending(struct esp *esp)
{
	struct pci_esp_priv *pep = pci_esp_get_priv(esp);

	pep->dma_status = pci_esp_read8(esp, ESP_DMA_STATUS);
	esp_dma_log("dma intr dreg[%02x]\n", pep->dma_status);

	if (pep->dma_status & (ESP_DMA_STAT_ERROR |
			       ESP_DMA_STAT_ABORT |
			       ESP_DMA_STAT_DONE |
			       ESP_DMA_STAT_SCSIINT))
		return 1;

	return 0;
}

static void pci_esp_reset_dma(struct esp *esp)
{
	/* Nothing to do ? */
}

static void pci_esp_dma_drain(struct esp *esp)
{
	u8 resid;
	int lim = 1000;


	if ((esp->sreg & ESP_STAT_PMASK) == ESP_DOP ||
	    (esp->sreg & ESP_STAT_PMASK) == ESP_DIP)
		/* Data-In or Data-Out, nothing to be done */
		return;

	while (--lim > 0) {
		resid = pci_esp_read8(esp, ESP_FFLAGS) & ESP_FF_FBYTES;
		if (resid <= 1)
			break;
		cpu_relax();
	}
	if (resid > 1) {
		/* FIFO not cleared */
		shost_printk(KERN_INFO, esp->host,
			     "FIFO not cleared, %d bytes left\n",
			     resid);
	}

	/*
	 * When there is a residual BCMPLT will never be set
	 * (obviously). But we still have to issue the BLAST
	 * command, otherwise the data will not being transferred.
	 * But we'll never know when the BLAST operation is
	 * finished. So check for some time and give up eventually.
	 */
	lim = 1000;
	pci_esp_write8(esp, ESP_DMA_CMD_DIR | ESP_DMA_CMD_BLAST, ESP_DMA_CMD);
	while (pci_esp_read8(esp, ESP_DMA_STATUS) & ESP_DMA_STAT_BCMPLT) {
		if (--lim == 0)
			break;
		cpu_relax();
	}
	pci_esp_write8(esp, ESP_DMA_CMD_DIR | ESP_DMA_CMD_IDLE, ESP_DMA_CMD);
	esp_dma_log("DMA blast done (%d tries, %d bytes left)\n", lim, resid);
	/* BLAST residual handling is currently untested */
	if (WARN_ON_ONCE(resid == 1)) {
		struct esp_cmd_entry *ent = esp->active_cmd;

		ent->flags |= ESP_CMD_FLAG_RESIDUAL;
	}
}

static void pci_esp_dma_invalidate(struct esp *esp)
{
	struct pci_esp_priv *pep = pci_esp_get_priv(esp);

	esp_dma_log("invalidate DMA\n");

	pci_esp_write8(esp, ESP_DMA_CMD_IDLE, ESP_DMA_CMD);
	pep->dma_status = 0;
}

static int pci_esp_dma_error(struct esp *esp)
{
	struct pci_esp_priv *pep = pci_esp_get_priv(esp);

	if (pep->dma_status & ESP_DMA_STAT_ERROR) {
		u8 dma_cmd = pci_esp_read8(esp, ESP_DMA_CMD);

		if ((dma_cmd & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_START)
			pci_esp_write8(esp, ESP_DMA_CMD_ABORT, ESP_DMA_CMD);

		return 1;
	}
	if (pep->dma_status & ESP_DMA_STAT_ABORT) {
		pci_esp_write8(esp, ESP_DMA_CMD_IDLE, ESP_DMA_CMD);
		pep->dma_status = pci_esp_read8(esp, ESP_DMA_CMD);
		return 1;
	}
	return 0;
}

static void pci_esp_send_dma_cmd(struct esp *esp, u32 addr, u32 esp_count,
				 u32 dma_count, int write, u8 cmd)
{
	struct pci_esp_priv *pep = pci_esp_get_priv(esp);
	u32 val = 0;

	BUG_ON(!(cmd & ESP_CMD_DMA));

	pep->dma_status = 0;

	/* Set DMA engine to IDLE */
	if (write)
		/* DMA write direction logic is inverted */
		val |= ESP_DMA_CMD_DIR;
	pci_esp_write8(esp, ESP_DMA_CMD_IDLE | val, ESP_DMA_CMD);

	pci_esp_write8(esp, (esp_count >> 0) & 0xff, ESP_TCLOW);
	pci_esp_write8(esp, (esp_count >> 8) & 0xff, ESP_TCMED);
	if (esp->config2 & ESP_CONFIG2_FENAB)
		pci_esp_write8(esp, (esp_count >> 16) & 0xff, ESP_TCHI);

	pci_esp_write32(esp, esp_count, ESP_DMA_STC);
	pci_esp_write32(esp, addr, ESP_DMA_SPA);

	esp_dma_log("start dma addr[%x] count[%d:%d]\n",
		    addr, esp_count, dma_count);

	scsi_esp_cmd(esp, cmd);
	/* Send DMA Start command */
	pci_esp_write8(esp, ESP_DMA_CMD_START | val, ESP_DMA_CMD);
}

static u32 pci_esp_dma_length_limit(struct esp *esp, u32 dma_addr, u32 dma_len)
{
	int dma_limit = 16;
	u32 base, end;

	/*
	 * If CONFIG2_FENAB is set we can
	 * handle up to 24 bit addresses
	 */
	if (esp->config2 & ESP_CONFIG2_FENAB)
		dma_limit = 24;

	if (dma_len > (1U << dma_limit))
		dma_len = (1U << dma_limit);

	/*
	 * Prevent crossing a 24-bit address boundary.
	 */
	base = dma_addr & ((1U << 24) - 1U);
	end = base + dma_len;
	if (end > (1U << 24))
		end = (1U <<24);
	dma_len = end - base;

	return dma_len;
}

static const struct esp_driver_ops pci_esp_ops = {
	.esp_write8	=	pci_esp_write8,
	.esp_read8	=	pci_esp_read8,
	.map_single	=	pci_esp_map_single,
	.map_sg		=	pci_esp_map_sg,
	.unmap_single	=	pci_esp_unmap_single,
	.unmap_sg	=	pci_esp_unmap_sg,
	.irq_pending	=	pci_esp_irq_pending,
	.reset_dma	=	pci_esp_reset_dma,
	.dma_drain	=	pci_esp_dma_drain,
	.dma_invalidate	=	pci_esp_dma_invalidate,
	.send_dma_cmd	=	pci_esp_send_dma_cmd,
	.dma_error	=	pci_esp_dma_error,
	.dma_length_limit =	pci_esp_dma_length_limit,
};

/*
 * Read DC-390 eeprom
 */
static void dc390_eeprom_prepare_read(struct pci_dev *pdev, u8 cmd)
{
	u8 carry_flag = 1, j = 0x80, bval;
	int i;

	for (i = 0; i < 9; i++) {
		if (carry_flag) {
			pci_write_config_byte(pdev, 0x80, 0x40);
			bval = 0xc0;
		} else
			bval = 0x80;

		udelay(160);
		pci_write_config_byte(pdev, 0x80, bval);
		udelay(160);
		pci_write_config_byte(pdev, 0x80, 0);
		udelay(160);

		carry_flag = (cmd & j) ? 1 : 0;
		j >>= 1;
	}
}

static u16 dc390_eeprom_get_data(struct pci_dev *pdev)
{
	int i;
	u16 wval = 0;
	u8 bval;

	for (i = 0; i < 16; i++) {
		wval <<= 1;

		pci_write_config_byte(pdev, 0x80, 0x80);
		udelay(160);
		pci_write_config_byte(pdev, 0x80, 0x40);
		udelay(160);
		pci_read_config_byte(pdev, 0x00, &bval);

		if (bval == 0x22)
			wval |= 1;
	}

	return wval;
}

static void dc390_read_eeprom(struct pci_dev *pdev, u16 *ptr)
{
	u8 cmd = DC390_EEPROM_READ, i;

	for (i = 0; i < DC390_EEPROM_LEN; i++) {
		pci_write_config_byte(pdev, 0xc0, 0);
		udelay(160);

		dc390_eeprom_prepare_read(pdev, cmd++);
		*ptr++ = dc390_eeprom_get_data(pdev);

		pci_write_config_byte(pdev, 0x80, 0);
		pci_write_config_byte(pdev, 0x80, 0);
		udelay(160);
	}
}

static void dc390_check_eeprom(struct esp *esp)
{
	u8 EEbuf[128];
	u16 *ptr = (u16 *)EEbuf, wval = 0;
	int i;

	dc390_read_eeprom((struct pci_dev *)esp->dev, ptr);

	for (i = 0; i < DC390_EEPROM_LEN; i++, ptr++)
		wval += *ptr;

	/* no Tekram EEprom found */
	if (wval != 0x1234) {
		struct pci_dev *pdev = esp->dev;
		dev_printk(KERN_INFO, &pdev->dev,
			   "No valid Tekram EEprom found\n");
		return;
	}
	esp->scsi_id = EEbuf[DC390_EE_ADAPT_SCSI_ID];
	esp->num_tags = 2 << EEbuf[DC390_EE_TAG_CMD_NUM];
	if (EEbuf[DC390_EE_MODE2] & DC390_EE_MODE2_ACTIVE_NEGATION)
		esp->config4 |= ESP_CONFIG4_RADE | ESP_CONFIG4_RAE;
}

static int pci_esp_probe_one(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct scsi_host_template *hostt = &scsi_esp_template;
	int err = -ENODEV;
	struct Scsi_Host *shost;
	struct esp *esp;
	struct pci_esp_priv *pep;

	if (pci_enable_device(pdev)) {
		dev_printk(KERN_INFO, &pdev->dev, "cannot enable device\n");
		return -ENODEV;
	}

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "failed to set 32bit DMA mask\n");
		goto fail_disable_device;
	}

	shost = scsi_host_alloc(hostt, sizeof(struct esp));
	if (!shost) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "failed to allocate scsi host\n");
		err = -ENOMEM;
		goto fail_disable_device;
	}

	pep = kzalloc(sizeof(struct pci_esp_priv), GFP_KERNEL);
	if (!pep) {
		dev_printk(KERN_INFO, &pdev->dev,
			   "failed to allocate esp_priv\n");
		err = -ENOMEM;
		goto fail_host_alloc;
	}

	esp = shost_priv(shost);
	esp->host = shost;
	esp->dev = pdev;
	esp->ops = &pci_esp_ops;
	/*
	 * The am53c974 HBA has a design flaw of generating
	 * spurious DMA completion interrupts when using
	 * DMA for command submission.
	 */
	esp->flags |= ESP_FLAG_USE_FIFO;
	/*
	 * Enable CONFIG2_FENAB to allow for large DMA transfers
	 */
	if (am53c974_fenab)
		esp->config2 |= ESP_CONFIG2_FENAB;

	pep->esp = esp;

	if (pci_request_regions(pdev, DRV_MODULE_NAME)) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "pci memory selection failed\n");
		goto fail_priv_alloc;
	}

	esp->regs = pci_iomap(pdev, 0, pci_resource_len(pdev, 0));
	if (!esp->regs) {
		dev_printk(KERN_ERR, &pdev->dev, "pci I/O map failed\n");
		err = -EINVAL;
		goto fail_release_regions;
	}
	esp->dma_regs = esp->regs;

	pci_set_master(pdev);

	esp->command_block = pci_alloc_consistent(pdev, 16,
						  &esp->command_block_dma);
	if (!esp->command_block) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to allocate command block\n");
		err = -ENOMEM;
		goto fail_unmap_regs;
	}

	err = request_irq(pdev->irq, scsi_esp_intr, IRQF_SHARED,
			  DRV_MODULE_NAME, esp);
	if (err < 0) {
		dev_printk(KERN_ERR, &pdev->dev, "failed to register IRQ\n");
		goto fail_unmap_command_block;
	}

	esp->scsi_id = 7;
	dc390_check_eeprom(esp);

	shost->this_id = esp->scsi_id;
	shost->max_id = 8;
	shost->irq = pdev->irq;
	shost->io_port = pci_resource_start(pdev, 0);
	shost->n_io_port = pci_resource_len(pdev, 0);
	shost->unique_id = shost->io_port;
	esp->scsi_id_mask = (1 << esp->scsi_id);
	/* Assume 40MHz clock */
	esp->cfreq = 40000000;

	pci_set_drvdata(pdev, pep);

	err = scsi_esp_register(esp, &pdev->dev);
	if (err)
		goto fail_free_irq;

	return 0;

fail_free_irq:
	free_irq(pdev->irq, esp);
fail_unmap_command_block:
	pci_free_consistent(pdev, 16, esp->command_block,
			    esp->command_block_dma);
fail_unmap_regs:
	pci_iounmap(pdev, esp->regs);
fail_release_regions:
	pci_release_regions(pdev);
fail_priv_alloc:
	kfree(pep);
fail_host_alloc:
	scsi_host_put(shost);
fail_disable_device:
	pci_disable_device(pdev);

	return err;
}

static void pci_esp_remove_one(struct pci_dev *pdev)
{
	struct pci_esp_priv *pep = pci_get_drvdata(pdev);
	struct esp *esp = pep->esp;

	scsi_esp_unregister(esp);
	free_irq(pdev->irq, esp);
	pci_free_consistent(pdev, 16, esp->command_block,
			    esp->command_block_dma);
	pci_iounmap(pdev, esp->regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(pep);

	scsi_host_put(esp->host);
}

static struct pci_device_id am53c974_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_SCSI,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ }
};
MODULE_DEVICE_TABLE(pci, am53c974_pci_tbl);

static struct pci_driver am53c974_driver = {
	.name           = DRV_MODULE_NAME,
	.id_table       = am53c974_pci_tbl,
	.probe          = pci_esp_probe_one,
	.remove         = pci_esp_remove_one,
};

static int __init am53c974_module_init(void)
{
	return pci_register_driver(&am53c974_driver);
}

static void __exit am53c974_module_exit(void)
{
	pci_unregister_driver(&am53c974_driver);
}

MODULE_DESCRIPTION("AM53C974 SCSI driver");
MODULE_AUTHOR("Hannes Reinecke <hare@suse.de>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);
MODULE_ALIAS("tmscsim");

module_param(am53c974_debug, bool, 0644);
MODULE_PARM_DESC(am53c974_debug, "Enable debugging");

module_param(am53c974_fenab, bool, 0444);
MODULE_PARM_DESC(am53c974_fenab, "Enable 24-bit DMA transfer sizes");

module_init(am53c974_module_init);
module_exit(am53c974_module_exit);
