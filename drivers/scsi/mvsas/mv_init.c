/*
	mv_init.c - Marvell 88SE6440 SAS/SATA init support

	Copyright 2007 Red Hat, Inc.
	Copyright 2008 Marvell. <kewei@marvell.com>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation; either version 2,
	or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty
	of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public
	License along with this program; see the file COPYING.	If not,
	write to the Free Software Foundation, 675 Mass Ave, Cambridge,
	MA 02139, USA.

 */

#include "mv_sas.h"
#include "mv_64xx.h"
#include "mv_chips.h"

static struct scsi_transport_template *mvs_stt;

static const struct mvs_chip_info mvs_chips[] = {
	[chip_6320] =		{ 2, 16, 9  },
	[chip_6440] =		{ 4, 16, 9  },
	[chip_6480] =		{ 8, 32, 10 },
};

static struct scsi_host_template mvs_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= mvs_slave_configure,
	.slave_destroy		= sas_slave_destroy,
	.scan_finished		= mvs_scan_finished,
	.scan_start		= mvs_scan_start,
	.change_queue_depth	= sas_change_queue_depth,
	.change_queue_type	= sas_change_queue_type,
	.bios_param		= sas_bios_param,
	.can_queue		= 1,
	.cmd_per_lun		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= SCSI_DEFAULT_MAX_SECTORS,
	.use_clustering		= ENABLE_CLUSTERING,
	.eh_device_reset_handler	= sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.slave_alloc		= sas_slave_alloc,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
};

static struct sas_domain_function_template mvs_transport_ops = {
	.lldd_execute_task	= mvs_task_exec,
	.lldd_control_phy	= mvs_phy_control,
	.lldd_abort_task	= mvs_task_abort,
	.lldd_port_formed	= mvs_port_formed,
	.lldd_I_T_nexus_reset	= mvs_I_T_nexus_reset,
};

static void __devinit mvs_phy_init(struct mvs_info *mvi, int phy_id)
{
	struct mvs_phy *phy = &mvi->phy[phy_id];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	sas_phy->enabled = (phy_id < mvi->chip->n_phy) ? 1 : 0;
	sas_phy->class = SAS;
	sas_phy->iproto = SAS_PROTOCOL_ALL;
	sas_phy->tproto = 0;
	sas_phy->type = PHY_TYPE_PHYSICAL;
	sas_phy->role = PHY_ROLE_INITIATOR;
	sas_phy->oob_mode = OOB_NOT_CONNECTED;
	sas_phy->linkrate = SAS_LINK_RATE_UNKNOWN;

	sas_phy->id = phy_id;
	sas_phy->sas_addr = &mvi->sas_addr[0];
	sas_phy->frame_rcvd = &phy->frame_rcvd[0];
	sas_phy->ha = &mvi->sas;
	sas_phy->lldd_phy = phy;
}

static void mvs_free(struct mvs_info *mvi)
{
	int i;

	if (!mvi)
		return;

	for (i = 0; i < MVS_SLOTS; i++) {
		struct mvs_slot_info *slot = &mvi->slot_info[i];

		if (slot->buf)
			dma_free_coherent(&mvi->pdev->dev, MVS_SLOT_BUF_SZ,
					  slot->buf, slot->buf_dma);
	}

	if (mvi->tx)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				  mvi->tx, mvi->tx_dma);
	if (mvi->rx_fis)
		dma_free_coherent(&mvi->pdev->dev, MVS_RX_FISL_SZ,
				  mvi->rx_fis, mvi->rx_fis_dma);
	if (mvi->rx)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				  mvi->rx, mvi->rx_dma);
	if (mvi->slot)
		dma_free_coherent(&mvi->pdev->dev,
				  sizeof(*mvi->slot) * MVS_SLOTS,
				  mvi->slot, mvi->slot_dma);
#ifdef MVS_ENABLE_PERI
	if (mvi->peri_regs)
		iounmap(mvi->peri_regs);
#endif
	if (mvi->regs)
		iounmap(mvi->regs);
	if (mvi->shost)
		scsi_host_put(mvi->shost);
	kfree(mvi->sas.sas_port);
	kfree(mvi->sas.sas_phy);
	kfree(mvi);
}

#ifdef MVS_USE_TASKLET
static void mvs_tasklet(unsigned long data)
{
	struct mvs_info *mvi = (struct mvs_info *) data;
	unsigned long flags;

	spin_lock_irqsave(&mvi->lock, flags);

#ifdef MVS_DISABLE_MSI
	mvs_int_full(mvi);
#else
	mvs_int_rx(mvi, true);
#endif
	spin_unlock_irqrestore(&mvi->lock, flags);
}
#endif

static irqreturn_t mvs_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;
	void __iomem *regs = mvi->regs;
	u32 stat;

	stat = mr32(GBL_INT_STAT);

	if (stat == 0 || stat == 0xffffffff)
		return IRQ_NONE;

	/* clear CMD_CMPLT ASAP */
	mw32_f(INT_STAT, CINT_DONE);

#ifndef MVS_USE_TASKLET
	spin_lock(&mvi->lock);

	mvs_int_full(mvi);

	spin_unlock(&mvi->lock);
#else
	tasklet_schedule(&mvi->tasklet);
#endif
	return IRQ_HANDLED;
}

static struct mvs_info *__devinit mvs_alloc(struct pci_dev *pdev,
					    const struct pci_device_id *ent)
{
	struct mvs_info *mvi;
	unsigned long res_start, res_len, res_flag;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	const struct mvs_chip_info *chip = &mvs_chips[ent->driver_data];
	int i;

	/*
	 * alloc and init our per-HBA mvs_info struct
	 */

	mvi = kzalloc(sizeof(*mvi), GFP_KERNEL);
	if (!mvi)
		return NULL;

	spin_lock_init(&mvi->lock);
#ifdef MVS_USE_TASKLET
	tasklet_init(&mvi->tasklet, mvs_tasklet, (unsigned long)mvi);
#endif
	mvi->pdev = pdev;
	mvi->chip = chip;

	if (pdev->device == 0x6440 && pdev->revision == 0)
		mvi->flags |= MVF_PHY_PWR_FIX;

	/*
	 * alloc and init SCSI, SAS glue
	 */

	mvi->shost = scsi_host_alloc(&mvs_sht, sizeof(void *));
	if (!mvi->shost)
		goto err_out;

	arr_phy = kcalloc(MVS_MAX_PHYS, sizeof(void *), GFP_KERNEL);
	arr_port = kcalloc(MVS_MAX_PHYS, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		goto err_out;

	for (i = 0; i < MVS_MAX_PHYS; i++) {
		mvs_phy_init(mvi, i);
		arr_phy[i] = &mvi->phy[i].sas_phy;
		arr_port[i] = &mvi->port[i].sas_port;
		mvi->port[i].taskfileset = MVS_ID_NOT_MAPPED;
		mvi->port[i].wide_port_phymap = 0;
		mvi->port[i].port_attached = 0;
		INIT_LIST_HEAD(&mvi->port[i].list);
	}

	SHOST_TO_SAS_HA(mvi->shost) = &mvi->sas;
	mvi->shost->transportt = mvs_stt;
	mvi->shost->max_id = 21;
	mvi->shost->max_lun = ~0;
	mvi->shost->max_channel = 0;
	mvi->shost->max_cmd_len = 16;

	mvi->sas.sas_ha_name = DRV_NAME;
	mvi->sas.dev = &pdev->dev;
	mvi->sas.lldd_module = THIS_MODULE;
	mvi->sas.sas_addr = &mvi->sas_addr[0];
	mvi->sas.sas_phy = arr_phy;
	mvi->sas.sas_port = arr_port;
	mvi->sas.num_phys = chip->n_phy;
	mvi->sas.lldd_max_execute_num = 1;
	mvi->sas.lldd_queue_size = MVS_QUEUE_SIZE;
	mvi->shost->can_queue = MVS_CAN_QUEUE;
	mvi->shost->cmd_per_lun = MVS_SLOTS / mvi->sas.num_phys;
	mvi->sas.lldd_ha = mvi;
	mvi->sas.core.shost = mvi->shost;

	mvs_tag_init(mvi);

	/*
	 * ioremap main and peripheral registers
	 */

#ifdef MVS_ENABLE_PERI
	res_start = pci_resource_start(pdev, 2);
	res_len = pci_resource_len(pdev, 2);
	if (!res_start || !res_len)
		goto err_out;

	mvi->peri_regs = ioremap_nocache(res_start, res_len);
	if (!mvi->peri_regs)
		goto err_out;
#endif

	res_start = pci_resource_start(pdev, 4);
	res_len = pci_resource_len(pdev, 4);
	if (!res_start || !res_len)
		goto err_out;

	res_flag = pci_resource_flags(pdev, 4);
	if (res_flag & IORESOURCE_CACHEABLE)
		mvi->regs = ioremap(res_start, res_len);
	else
		mvi->regs = ioremap_nocache(res_start, res_len);

	if (!mvi->regs)
		goto err_out;

	/*
	 * alloc and init our DMA areas
	 */

	mvi->tx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				     &mvi->tx_dma, GFP_KERNEL);
	if (!mvi->tx)
		goto err_out;
	memset(mvi->tx, 0, sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ);

	mvi->rx_fis = dma_alloc_coherent(&pdev->dev, MVS_RX_FISL_SZ,
					 &mvi->rx_fis_dma, GFP_KERNEL);
	if (!mvi->rx_fis)
		goto err_out;
	memset(mvi->rx_fis, 0, MVS_RX_FISL_SZ);

	mvi->rx = dma_alloc_coherent(&pdev->dev,
				     sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				     &mvi->rx_dma, GFP_KERNEL);
	if (!mvi->rx)
		goto err_out;
	memset(mvi->rx, 0, sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1));

	mvi->rx[0] = cpu_to_le32(0xfff);
	mvi->rx_cons = 0xfff;

	mvi->slot = dma_alloc_coherent(&pdev->dev,
				       sizeof(*mvi->slot) * MVS_SLOTS,
				       &mvi->slot_dma, GFP_KERNEL);
	if (!mvi->slot)
		goto err_out;
	memset(mvi->slot, 0, sizeof(*mvi->slot) * MVS_SLOTS);

	for (i = 0; i < MVS_SLOTS; i++) {
		struct mvs_slot_info *slot = &mvi->slot_info[i];

		slot->buf = dma_alloc_coherent(&pdev->dev, MVS_SLOT_BUF_SZ,
					       &slot->buf_dma, GFP_KERNEL);
		if (!slot->buf)
			goto err_out;
		memset(slot->buf, 0, MVS_SLOT_BUF_SZ);
	}

	/* finally, read NVRAM to get our SAS address */
	if (mvs_nvram_read(mvi, NVR_SAS_ADDR, &mvi->sas_addr, 8))
		goto err_out;
	return mvi;

err_out:
	mvs_free(mvi);
	return NULL;
}

/* move to PCI layer or libata core? */
static int pci_go_64(struct pci_dev *pdev)
{
	int rc;

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (rc) {
			rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
			if (rc) {
				dev_printk(KERN_ERR, &pdev->dev,
					   "64-bit DMA enable failed\n");
				return rc;
			}
		}
	} else {
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit DMA enable failed\n");
			return rc;
		}
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "32-bit consistent DMA enable failed\n");
			return rc;
		}
	}

	return rc;
}

static int __devinit mvs_pci_init(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int rc;
	struct mvs_info *mvi;
	irq_handler_t irq_handler = mvs_interrupt;

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable;

	rc = pci_go_64(pdev);
	if (rc)
		goto err_out_regions;

	mvi = mvs_alloc(pdev, ent);
	if (!mvi) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	rc = mvs_hw_init(mvi);
	if (rc)
		goto err_out_mvi;

#ifndef MVS_DISABLE_MSI
	if (!pci_enable_msi(pdev)) {
		u32 tmp;
		void __iomem *regs = mvi->regs;
		mvi->flags |= MVF_MSI;
		irq_handler = mvs_msi_interrupt;
		tmp = mr32(PCS);
		mw32(PCS, tmp | PCS_SELF_CLEAR);
	}
#endif

	rc = request_irq(pdev->irq, irq_handler, IRQF_SHARED, DRV_NAME, mvi);
	if (rc)
		goto err_out_msi;

	rc = scsi_add_host(mvi->shost, &pdev->dev);
	if (rc)
		goto err_out_irq;

	rc = sas_register_ha(&mvi->sas);
	if (rc)
		goto err_out_shost;

	pci_set_drvdata(pdev, mvi);

	mvs_print_info(mvi);

	mvs_hba_interrupt_enable(mvi);

	scsi_scan_host(mvi->shost);

	return 0;

err_out_shost:
	scsi_remove_host(mvi->shost);
err_out_irq:
	free_irq(pdev->irq, mvi);
err_out_msi:
	if (mvi->flags |= MVF_MSI)
		pci_disable_msi(pdev);
err_out_mvi:
	mvs_free(mvi);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
	return rc;
}

static void __devexit mvs_pci_remove(struct pci_dev *pdev)
{
	struct mvs_info *mvi = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);

	if (mvi) {
		sas_unregister_ha(&mvi->sas);
		mvs_hba_interrupt_disable(mvi);
		sas_remove_host(mvi->shost);
		scsi_remove_host(mvi->shost);

		free_irq(pdev->irq, mvi);
		if (mvi->flags & MVF_MSI)
			pci_disable_msi(pdev);
		mvs_free(mvi);
		pci_release_regions(pdev);
	}
	pci_disable_device(pdev);
}

static struct pci_device_id __devinitdata mvs_pci_table[] = {
	{ PCI_VDEVICE(MARVELL, 0x6320), chip_6320 },
	{ PCI_VDEVICE(MARVELL, 0x6340), chip_6440 },
	{
		.vendor 	= PCI_VENDOR_ID_MARVELL,
		.device 	= 0x6440,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x6480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_6480,
	},
	{ PCI_VDEVICE(MARVELL, 0x6440), chip_6440 },
	{ PCI_VDEVICE(MARVELL, 0x6480), chip_6480 },

	{ }	/* terminate list */
};

static struct pci_driver mvs_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= mvs_pci_table,
	.probe		= mvs_pci_init,
	.remove		= __devexit_p(mvs_pci_remove),
};

static int __init mvs_init(void)
{
	int rc;

	mvs_stt = sas_domain_attach_transport(&mvs_transport_ops);
	if (!mvs_stt)
		return -ENOMEM;

	rc = pci_register_driver(&mvs_pci_driver);
	if (rc)
		goto err_out;

	return 0;

err_out:
	sas_release_transport(mvs_stt);
	return rc;
}

static void __exit mvs_exit(void)
{
	pci_unregister_driver(&mvs_pci_driver);
	sas_release_transport(mvs_stt);
}

module_init(mvs_init);
module_exit(mvs_exit);

MODULE_AUTHOR("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION("Marvell 88SE6440 SAS/SATA controller driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, mvs_pci_table);
