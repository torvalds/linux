/*
 * Marvell 88SE64xx/88SE94xx pci init
 *
 * Copyright 2007 Red Hat, Inc.
 * Copyright 2008 Marvell. <kewei@marvell.com>
 * Copyright 2009-2011 Marvell. <yuxiangl@marvell.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
*/


#include "mv_sas.h"

static int lldd_max_execute_num = 1;
module_param_named(collector, lldd_max_execute_num, int, S_IRUGO);
MODULE_PARM_DESC(collector, "\n"
	"\tIf greater than one, tells the SAS Layer to run in Task Collector\n"
	"\tMode.  If 1 or 0, tells the SAS Layer to run in Direct Mode.\n"
	"\tThe mvsas SAS LLDD supports both modes.\n"
	"\tDefault: 1 (Direct Mode).\n");

int interrupt_coalescing = 0x80;

static struct scsi_transport_template *mvs_stt;
struct kmem_cache *mvs_task_list_cache;
static const struct mvs_chip_info mvs_chips[] = {
	[chip_6320] =	{ 1, 2, 0x400, 17, 16, 6,  9, &mvs_64xx_dispatch, },
	[chip_6440] =	{ 1, 4, 0x400, 17, 16, 6,  9, &mvs_64xx_dispatch, },
	[chip_6485] =	{ 1, 8, 0x800, 33, 32, 6, 10, &mvs_64xx_dispatch, },
	[chip_9180] =	{ 2, 4, 0x800, 17, 64, 8,  9, &mvs_94xx_dispatch, },
	[chip_9480] =	{ 2, 4, 0x800, 17, 64, 8,  9, &mvs_94xx_dispatch, },
	[chip_9445] =	{ 1, 4, 0x800, 17, 64, 8, 11, &mvs_94xx_dispatch, },
	[chip_9485] =	{ 2, 4, 0x800, 17, 64, 8, 11, &mvs_94xx_dispatch, },
	[chip_1300] =	{ 1, 4, 0x400, 17, 16, 6,  9, &mvs_64xx_dispatch, },
	[chip_1320] =	{ 2, 4, 0x800, 17, 64, 8,  9, &mvs_94xx_dispatch, },
};

struct device_attribute *mvst_host_attrs[];

#define SOC_SAS_NUM 2

static struct scsi_host_template mvs_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= sas_queuecommand,
	.target_alloc		= sas_target_alloc,
	.slave_configure	= sas_slave_configure,
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
	.eh_device_reset_handler = sas_eh_device_reset_handler,
	.eh_bus_reset_handler	= sas_eh_bus_reset_handler,
	.target_destroy		= sas_target_destroy,
	.ioctl			= sas_ioctl,
	.shost_attrs		= mvst_host_attrs,
};

static struct sas_domain_function_template mvs_transport_ops = {
	.lldd_dev_found 	= mvs_dev_found,
	.lldd_dev_gone		= mvs_dev_gone,
	.lldd_execute_task	= mvs_queue_command,
	.lldd_control_phy	= mvs_phy_control,

	.lldd_abort_task	= mvs_abort_task,
	.lldd_abort_task_set    = mvs_abort_task_set,
	.lldd_clear_aca         = mvs_clear_aca,
	.lldd_clear_task_set    = mvs_clear_task_set,
	.lldd_I_T_nexus_reset	= mvs_I_T_nexus_reset,
	.lldd_lu_reset 		= mvs_lu_reset,
	.lldd_query_task	= mvs_query_task,
	.lldd_port_formed	= mvs_port_formed,
	.lldd_port_deformed     = mvs_port_deformed,

};

static void mvs_phy_init(struct mvs_info *mvi, int phy_id)
{
	struct mvs_phy *phy = &mvi->phy[phy_id];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	phy->mvi = mvi;
	phy->port = NULL;
	init_timer(&phy->timer);
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
	sas_phy->ha = (struct sas_ha_struct *)mvi->shost->hostdata;
	sas_phy->lldd_phy = phy;
}

static void mvs_free(struct mvs_info *mvi)
{
	struct mvs_wq *mwq;
	int slot_nr;

	if (!mvi)
		return;

	if (mvi->flags & MVF_FLAG_SOC)
		slot_nr = MVS_SOC_SLOTS;
	else
		slot_nr = MVS_CHIP_SLOT_SZ;

	if (mvi->dma_pool)
		pci_pool_destroy(mvi->dma_pool);

	if (mvi->tx)
		dma_free_coherent(mvi->dev,
				  sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				  mvi->tx, mvi->tx_dma);
	if (mvi->rx_fis)
		dma_free_coherent(mvi->dev, MVS_RX_FISL_SZ,
				  mvi->rx_fis, mvi->rx_fis_dma);
	if (mvi->rx)
		dma_free_coherent(mvi->dev,
				  sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				  mvi->rx, mvi->rx_dma);
	if (mvi->slot)
		dma_free_coherent(mvi->dev,
				  sizeof(*mvi->slot) * slot_nr,
				  mvi->slot, mvi->slot_dma);

	if (mvi->bulk_buffer)
		dma_free_coherent(mvi->dev, TRASH_BUCKET_SIZE,
				  mvi->bulk_buffer, mvi->bulk_buffer_dma);
	if (mvi->bulk_buffer1)
		dma_free_coherent(mvi->dev, TRASH_BUCKET_SIZE,
				  mvi->bulk_buffer1, mvi->bulk_buffer_dma1);

	MVS_CHIP_DISP->chip_iounmap(mvi);
	if (mvi->shost)
		scsi_host_put(mvi->shost);
	list_for_each_entry(mwq, &mvi->wq_list, entry)
		cancel_delayed_work(&mwq->work_q);
	kfree(mvi->tags);
	kfree(mvi);
}

#ifdef CONFIG_SCSI_MVSAS_TASKLET
static void mvs_tasklet(unsigned long opaque)
{
	u32 stat;
	u16 core_nr, i = 0;

	struct mvs_info *mvi;
	struct sas_ha_struct *sha = (struct sas_ha_struct *)opaque;

	core_nr = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[0];

	if (unlikely(!mvi))
		BUG_ON(1);

	stat = MVS_CHIP_DISP->isr_status(mvi, mvi->pdev->irq);
	if (!stat)
		goto out;

	for (i = 0; i < core_nr; i++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[i];
		MVS_CHIP_DISP->isr(mvi, mvi->pdev->irq, stat);
	}
out:
	MVS_CHIP_DISP->interrupt_enable(mvi);

}
#endif

static irqreturn_t mvs_interrupt(int irq, void *opaque)
{
	u32 core_nr;
	u32 stat;
	struct mvs_info *mvi;
	struct sas_ha_struct *sha = opaque;
#ifndef CONFIG_SCSI_MVSAS_TASKLET
	u32 i;
#endif

	core_nr = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[0];

	if (unlikely(!mvi))
		return IRQ_NONE;
#ifdef CONFIG_SCSI_MVSAS_TASKLET
	MVS_CHIP_DISP->interrupt_disable(mvi);
#endif

	stat = MVS_CHIP_DISP->isr_status(mvi, irq);
	if (!stat) {
	#ifdef CONFIG_SCSI_MVSAS_TASKLET
		MVS_CHIP_DISP->interrupt_enable(mvi);
	#endif
		return IRQ_NONE;
	}

#ifdef CONFIG_SCSI_MVSAS_TASKLET
	tasklet_schedule(&((struct mvs_prv_info *)sha->lldd_ha)->mv_tasklet);
#else
	for (i = 0; i < core_nr; i++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[i];
		MVS_CHIP_DISP->isr(mvi, irq, stat);
	}
#endif
	return IRQ_HANDLED;
}

static int mvs_alloc(struct mvs_info *mvi, struct Scsi_Host *shost)
{
	int i = 0, slot_nr;
	char pool_name[32];

	if (mvi->flags & MVF_FLAG_SOC)
		slot_nr = MVS_SOC_SLOTS;
	else
		slot_nr = MVS_CHIP_SLOT_SZ;

	spin_lock_init(&mvi->lock);
	for (i = 0; i < mvi->chip->n_phy; i++) {
		mvs_phy_init(mvi, i);
		mvi->port[i].wide_port_phymap = 0;
		mvi->port[i].port_attached = 0;
		INIT_LIST_HEAD(&mvi->port[i].list);
	}
	for (i = 0; i < MVS_MAX_DEVICES; i++) {
		mvi->devices[i].taskfileset = MVS_ID_NOT_MAPPED;
		mvi->devices[i].dev_type = SAS_PHY_UNUSED;
		mvi->devices[i].device_id = i;
		mvi->devices[i].dev_status = MVS_DEV_NORMAL;
		init_timer(&mvi->devices[i].timer);
	}

	/*
	 * alloc and init our DMA areas
	 */
	mvi->tx = dma_alloc_coherent(mvi->dev,
				     sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ,
				     &mvi->tx_dma, GFP_KERNEL);
	if (!mvi->tx)
		goto err_out;
	memset(mvi->tx, 0, sizeof(*mvi->tx) * MVS_CHIP_SLOT_SZ);
	mvi->rx_fis = dma_alloc_coherent(mvi->dev, MVS_RX_FISL_SZ,
					 &mvi->rx_fis_dma, GFP_KERNEL);
	if (!mvi->rx_fis)
		goto err_out;
	memset(mvi->rx_fis, 0, MVS_RX_FISL_SZ);

	mvi->rx = dma_alloc_coherent(mvi->dev,
				     sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1),
				     &mvi->rx_dma, GFP_KERNEL);
	if (!mvi->rx)
		goto err_out;
	memset(mvi->rx, 0, sizeof(*mvi->rx) * (MVS_RX_RING_SZ + 1));
	mvi->rx[0] = cpu_to_le32(0xfff);
	mvi->rx_cons = 0xfff;

	mvi->slot = dma_alloc_coherent(mvi->dev,
				       sizeof(*mvi->slot) * slot_nr,
				       &mvi->slot_dma, GFP_KERNEL);
	if (!mvi->slot)
		goto err_out;
	memset(mvi->slot, 0, sizeof(*mvi->slot) * slot_nr);

	mvi->bulk_buffer = dma_alloc_coherent(mvi->dev,
				       TRASH_BUCKET_SIZE,
				       &mvi->bulk_buffer_dma, GFP_KERNEL);
	if (!mvi->bulk_buffer)
		goto err_out;

	mvi->bulk_buffer1 = dma_alloc_coherent(mvi->dev,
				       TRASH_BUCKET_SIZE,
				       &mvi->bulk_buffer_dma1, GFP_KERNEL);
	if (!mvi->bulk_buffer1)
		goto err_out;

	sprintf(pool_name, "%s%d", "mvs_dma_pool", mvi->id);
	mvi->dma_pool = pci_pool_create(pool_name, mvi->pdev, MVS_SLOT_BUF_SZ, 16, 0);
	if (!mvi->dma_pool) {
			printk(KERN_DEBUG "failed to create dma pool %s.\n", pool_name);
			goto err_out;
	}
	mvi->tags_num = slot_nr;

	/* Initialize tags */
	mvs_tag_init(mvi);
	return 0;
err_out:
	return 1;
}


int mvs_ioremap(struct mvs_info *mvi, int bar, int bar_ex)
{
	unsigned long res_start, res_len, res_flag, res_flag_ex = 0;
	struct pci_dev *pdev = mvi->pdev;
	if (bar_ex != -1) {
		/*
		 * ioremap main and peripheral registers
		 */
		res_start = pci_resource_start(pdev, bar_ex);
		res_len = pci_resource_len(pdev, bar_ex);
		if (!res_start || !res_len)
			goto err_out;

		res_flag_ex = pci_resource_flags(pdev, bar_ex);
		if (res_flag_ex & IORESOURCE_MEM) {
			if (res_flag_ex & IORESOURCE_CACHEABLE)
				mvi->regs_ex = ioremap(res_start, res_len);
			else
				mvi->regs_ex = ioremap_nocache(res_start,
						res_len);
		} else
			mvi->regs_ex = (void *)res_start;
		if (!mvi->regs_ex)
			goto err_out;
	}

	res_start = pci_resource_start(pdev, bar);
	res_len = pci_resource_len(pdev, bar);
	if (!res_start || !res_len)
		goto err_out;

	res_flag = pci_resource_flags(pdev, bar);
	if (res_flag & IORESOURCE_CACHEABLE)
		mvi->regs = ioremap(res_start, res_len);
	else
		mvi->regs = ioremap_nocache(res_start, res_len);

	if (!mvi->regs) {
		if (mvi->regs_ex && (res_flag_ex & IORESOURCE_MEM))
			iounmap(mvi->regs_ex);
		mvi->regs_ex = NULL;
		goto err_out;
	}

	return 0;
err_out:
	return -1;
}

void mvs_iounmap(void __iomem *regs)
{
	iounmap(regs);
}

static struct mvs_info *mvs_pci_alloc(struct pci_dev *pdev,
				const struct pci_device_id *ent,
				struct Scsi_Host *shost, unsigned int id)
{
	struct mvs_info *mvi = NULL;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	mvi = kzalloc(sizeof(*mvi) +
		(1L << mvs_chips[ent->driver_data].slot_width) *
		sizeof(struct mvs_slot_info), GFP_KERNEL);
	if (!mvi)
		return NULL;

	mvi->pdev = pdev;
	mvi->dev = &pdev->dev;
	mvi->chip_id = ent->driver_data;
	mvi->chip = &mvs_chips[mvi->chip_id];
	INIT_LIST_HEAD(&mvi->wq_list);

	((struct mvs_prv_info *)sha->lldd_ha)->mvi[id] = mvi;
	((struct mvs_prv_info *)sha->lldd_ha)->n_phy = mvi->chip->n_phy;

	mvi->id = id;
	mvi->sas = sha;
	mvi->shost = shost;

	mvi->tags = kzalloc(MVS_CHIP_SLOT_SZ>>3, GFP_KERNEL);
	if (!mvi->tags)
		goto err_out;

	if (MVS_CHIP_DISP->chip_ioremap(mvi))
		goto err_out;
	if (!mvs_alloc(mvi, shost))
		return mvi;
err_out:
	mvs_free(mvi);
	return NULL;
}

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

static int mvs_prep_sas_ha_init(struct Scsi_Host *shost,
				const struct mvs_chip_info *chip_info)
{
	int phy_nr, port_nr; unsigned short core_nr;
	struct asd_sas_phy **arr_phy;
	struct asd_sas_port **arr_port;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);

	core_nr = chip_info->n_host;
	phy_nr  = core_nr * chip_info->n_phy;
	port_nr = phy_nr;

	memset(sha, 0x00, sizeof(struct sas_ha_struct));
	arr_phy  = kcalloc(phy_nr, sizeof(void *), GFP_KERNEL);
	arr_port = kcalloc(port_nr, sizeof(void *), GFP_KERNEL);
	if (!arr_phy || !arr_port)
		goto exit_free;

	sha->sas_phy = arr_phy;
	sha->sas_port = arr_port;
	sha->core.shost = shost;

	sha->lldd_ha = kzalloc(sizeof(struct mvs_prv_info), GFP_KERNEL);
	if (!sha->lldd_ha)
		goto exit_free;

	((struct mvs_prv_info *)sha->lldd_ha)->n_host = core_nr;

	shost->transportt = mvs_stt;
	shost->max_id = MVS_MAX_DEVICES;
	shost->max_lun = ~0;
	shost->max_channel = 1;
	shost->max_cmd_len = 16;

	return 0;
exit_free:
	kfree(arr_phy);
	kfree(arr_port);
	return -1;

}

static void  mvs_post_sas_ha_init(struct Scsi_Host *shost,
			const struct mvs_chip_info *chip_info)
{
	int can_queue, i = 0, j = 0;
	struct mvs_info *mvi = NULL;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	unsigned short nr_core = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;

	for (j = 0; j < nr_core; j++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[j];
		for (i = 0; i < chip_info->n_phy; i++) {
			sha->sas_phy[j * chip_info->n_phy  + i] =
				&mvi->phy[i].sas_phy;
			sha->sas_port[j * chip_info->n_phy + i] =
				&mvi->port[i].sas_port;
		}
	}

	sha->sas_ha_name = DRV_NAME;
	sha->dev = mvi->dev;
	sha->lldd_module = THIS_MODULE;
	sha->sas_addr = &mvi->sas_addr[0];

	sha->num_phys = nr_core * chip_info->n_phy;

	sha->lldd_max_execute_num = lldd_max_execute_num;

	if (mvi->flags & MVF_FLAG_SOC)
		can_queue = MVS_SOC_CAN_QUEUE;
	else
		can_queue = MVS_CHIP_SLOT_SZ;

	sha->lldd_queue_size = can_queue;
	shost->sg_tablesize = min_t(u16, SG_ALL, MVS_MAX_SG);
	shost->can_queue = can_queue;
	mvi->shost->cmd_per_lun = MVS_QUEUE_SIZE;
	sha->core.shost = mvi->shost;
}

static void mvs_init_sas_add(struct mvs_info *mvi)
{
	u8 i;
	for (i = 0; i < mvi->chip->n_phy; i++) {
		mvi->phy[i].dev_sas_addr = 0x5005043011ab0000ULL;
		mvi->phy[i].dev_sas_addr =
			cpu_to_be64((u64)(*(u64 *)&mvi->phy[i].dev_sas_addr));
	}

	memcpy(mvi->sas_addr, &mvi->phy[0].dev_sas_addr, SAS_ADDR_SIZE);
}

static int mvs_pci_init(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned int rc, nhost = 0;
	struct mvs_info *mvi;
	struct mvs_prv_info *mpi;
	irq_handler_t irq_handler = mvs_interrupt;
	struct Scsi_Host *shost = NULL;
	const struct mvs_chip_info *chip;

	dev_printk(KERN_INFO, &pdev->dev,
		"mvsas: driver version %s\n", DRV_VERSION);
	rc = pci_enable_device(pdev);
	if (rc)
		goto err_out_enable;

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out_disable;

	rc = pci_go_64(pdev);
	if (rc)
		goto err_out_regions;

	shost = scsi_host_alloc(&mvs_sht, sizeof(void *));
	if (!shost) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	chip = &mvs_chips[ent->driver_data];
	SHOST_TO_SAS_HA(shost) =
		kcalloc(1, sizeof(struct sas_ha_struct), GFP_KERNEL);
	if (!SHOST_TO_SAS_HA(shost)) {
		kfree(shost);
		rc = -ENOMEM;
		goto err_out_regions;
	}

	rc = mvs_prep_sas_ha_init(shost, chip);
	if (rc) {
		kfree(shost);
		rc = -ENOMEM;
		goto err_out_regions;
	}

	pci_set_drvdata(pdev, SHOST_TO_SAS_HA(shost));

	do {
		mvi = mvs_pci_alloc(pdev, ent, shost, nhost);
		if (!mvi) {
			rc = -ENOMEM;
			goto err_out_regions;
		}

		memset(&mvi->hba_info_param, 0xFF,
			sizeof(struct hba_info_page));

		mvs_init_sas_add(mvi);

		mvi->instance = nhost;
		rc = MVS_CHIP_DISP->chip_init(mvi);
		if (rc) {
			mvs_free(mvi);
			goto err_out_regions;
		}
		nhost++;
	} while (nhost < chip->n_host);
	mpi = (struct mvs_prv_info *)(SHOST_TO_SAS_HA(shost)->lldd_ha);
#ifdef CONFIG_SCSI_MVSAS_TASKLET
	tasklet_init(&(mpi->mv_tasklet), mvs_tasklet,
		     (unsigned long)SHOST_TO_SAS_HA(shost));
#endif

	mvs_post_sas_ha_init(shost, chip);

	rc = scsi_add_host(shost, &pdev->dev);
	if (rc)
		goto err_out_shost;

	rc = sas_register_ha(SHOST_TO_SAS_HA(shost));
	if (rc)
		goto err_out_shost;
	rc = request_irq(pdev->irq, irq_handler, IRQF_SHARED,
		DRV_NAME, SHOST_TO_SAS_HA(shost));
	if (rc)
		goto err_not_sas;

	MVS_CHIP_DISP->interrupt_enable(mvi);

	scsi_scan_host(mvi->shost);

	return 0;

err_not_sas:
	sas_unregister_ha(SHOST_TO_SAS_HA(shost));
err_out_shost:
	scsi_remove_host(mvi->shost);
err_out_regions:
	pci_release_regions(pdev);
err_out_disable:
	pci_disable_device(pdev);
err_out_enable:
	return rc;
}

static void mvs_pci_remove(struct pci_dev *pdev)
{
	unsigned short core_nr, i = 0;
	struct sas_ha_struct *sha = pci_get_drvdata(pdev);
	struct mvs_info *mvi = NULL;

	core_nr = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[0];

#ifdef CONFIG_SCSI_MVSAS_TASKLET
	tasklet_kill(&((struct mvs_prv_info *)sha->lldd_ha)->mv_tasklet);
#endif

	pci_set_drvdata(pdev, NULL);
	sas_unregister_ha(sha);
	sas_remove_host(mvi->shost);
	scsi_remove_host(mvi->shost);

	MVS_CHIP_DISP->interrupt_disable(mvi);
	free_irq(mvi->pdev->irq, sha);
	for (i = 0; i < core_nr; i++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[i];
		mvs_free(mvi);
	}
	kfree(sha->sas_phy);
	kfree(sha->sas_port);
	kfree(sha);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return;
}

static struct pci_device_id mvs_pci_table[] = {
	{ PCI_VDEVICE(MARVELL, 0x6320), chip_6320 },
	{ PCI_VDEVICE(MARVELL, 0x6340), chip_6440 },
	{
		.vendor 	= PCI_VENDOR_ID_MARVELL,
		.device 	= 0x6440,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x6480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_6485,
	},
	{ PCI_VDEVICE(MARVELL, 0x6440), chip_6440 },
	{ PCI_VDEVICE(MARVELL, 0x6485), chip_6485 },
	{ PCI_VDEVICE(MARVELL, 0x9480), chip_9480 },
	{ PCI_VDEVICE(MARVELL, 0x9180), chip_9180 },
	{ PCI_VDEVICE(ARECA, PCI_DEVICE_ID_ARECA_1300), chip_1300 },
	{ PCI_VDEVICE(ARECA, PCI_DEVICE_ID_ARECA_1320), chip_1320 },
	{ PCI_VDEVICE(ADAPTEC2, 0x0450), chip_6440 },
	{ PCI_VDEVICE(TTI, 0x2710), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2720), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2721), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2722), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2740), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2744), chip_9480 },
	{ PCI_VDEVICE(TTI, 0x2760), chip_9480 },
	{
		.vendor		= PCI_VENDOR_ID_MARVELL_EXT,
		.device		= 0x9480,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x9480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_9480,
	},
	{
		.vendor		= PCI_VENDOR_ID_MARVELL_EXT,
		.device		= 0x9445,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x9480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_9445,
	},
	{
		.vendor		= PCI_VENDOR_ID_MARVELL_EXT,
		.device		= 0x9485,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= 0x9480,
		.class		= 0,
		.class_mask	= 0,
		.driver_data	= chip_9485,
	},
	{ PCI_VDEVICE(OCZ, 0x1021), chip_9485}, /* OCZ RevoDrive3 */
	{ PCI_VDEVICE(OCZ, 0x1022), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1040), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1041), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1042), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1043), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1044), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1080), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1083), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */
	{ PCI_VDEVICE(OCZ, 0x1084), chip_9485}, /* OCZ RevoDrive3/zDriveR4 (exact model unknown) */

	{ }	/* terminate list */
};

static struct pci_driver mvs_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= mvs_pci_table,
	.probe		= mvs_pci_init,
	.remove		= mvs_pci_remove,
};

static ssize_t
mvs_show_driver_version(struct device *cdev,
		struct device_attribute *attr,  char *buffer)
{
	return snprintf(buffer, PAGE_SIZE, "%s\n", DRV_VERSION);
}

static DEVICE_ATTR(driver_version,
			 S_IRUGO,
			 mvs_show_driver_version,
			 NULL);

static ssize_t
mvs_store_interrupt_coalescing(struct device *cdev,
			struct device_attribute *attr,
			const char *buffer, size_t size)
{
	int val = 0;
	struct mvs_info *mvi = NULL;
	struct Scsi_Host *shost = class_to_shost(cdev);
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	u8 i, core_nr;
	if (buffer == NULL)
		return size;

	if (sscanf(buffer, "%d", &val) != 1)
		return -EINVAL;

	if (val >= 0x10000) {
		mv_dprintk("interrupt coalescing timer %d us is"
			"too long\n", val);
		return strlen(buffer);
	}

	interrupt_coalescing = val;

	core_nr = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[0];

	if (unlikely(!mvi))
		return -EINVAL;

	for (i = 0; i < core_nr; i++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[i];
		if (MVS_CHIP_DISP->tune_interrupt)
			MVS_CHIP_DISP->tune_interrupt(mvi,
				interrupt_coalescing);
	}
	mv_dprintk("set interrupt coalescing time to %d us\n",
		interrupt_coalescing);
	return strlen(buffer);
}

static ssize_t mvs_show_interrupt_coalescing(struct device *cdev,
			struct device_attribute *attr, char *buffer)
{
	return snprintf(buffer, PAGE_SIZE, "%d\n", interrupt_coalescing);
}

static DEVICE_ATTR(interrupt_coalescing,
			 S_IRUGO|S_IWUSR,
			 mvs_show_interrupt_coalescing,
			 mvs_store_interrupt_coalescing);

/* task handler */
struct task_struct *mvs_th;
static int __init mvs_init(void)
{
	int rc;
	mvs_stt = sas_domain_attach_transport(&mvs_transport_ops);
	if (!mvs_stt)
		return -ENOMEM;

	mvs_task_list_cache = kmem_cache_create("mvs_task_list", sizeof(struct mvs_task_list),
							 0, SLAB_HWCACHE_ALIGN, NULL);
	if (!mvs_task_list_cache) {
		rc = -ENOMEM;
		mv_printk("%s: mvs_task_list_cache alloc failed! \n", __func__);
		goto err_out;
	}

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
	kmem_cache_destroy(mvs_task_list_cache);
}

struct device_attribute *mvst_host_attrs[] = {
	&dev_attr_driver_version,
	&dev_attr_interrupt_coalescing,
	NULL,
};

module_init(mvs_init);
module_exit(mvs_exit);

MODULE_AUTHOR("Jeff Garzik <jgarzik@pobox.com>");
MODULE_DESCRIPTION("Marvell 88SE6440 SAS/SATA controller driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
#ifdef CONFIG_PCI
MODULE_DEVICE_TABLE(pci, mvs_pci_table);
#endif
