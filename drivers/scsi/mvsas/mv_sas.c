/*
	mv_sas.c - Marvell 88SE6440 SAS/SATA support

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

	---------------------------------------------------------------

	Random notes:
	* hardware supports controlling the endian-ness of data
	  structures.  this permits elimination of all the le32_to_cpu()
	  and cpu_to_le32() conversions.

 */

#include "mv_sas.h"
#include "mv_64xx.h"
#include "mv_chips.h"

/* offset for D2H FIS in the Received FIS List Structure */
#define SATA_RECEIVED_D2H_FIS(reg_set)	\
	((void *) mvi->rx_fis + 0x400 + 0x100 * reg_set + 0x40)
#define SATA_RECEIVED_PIO_FIS(reg_set)	\
	((void *) mvi->rx_fis + 0x400 + 0x100 * reg_set + 0x20)
#define UNASSOC_D2H_FIS(id)		\
	((void *) mvi->rx_fis + 0x100 * id)

struct mvs_task_exec_info {
	struct sas_task *task;
	struct mvs_cmd_hdr *hdr;
	struct mvs_port *port;
	u32 tag;
	int n_elem;
};

static void mvs_release_task(struct mvs_info *mvi, int phy_no);
static u32 mvs_is_phy_ready(struct mvs_info *mvi, int i);
static void mvs_update_phyinfo(struct mvs_info *mvi, int i,
					int get_st);
static int mvs_int_rx(struct mvs_info *mvi, bool self_clear);
static void mvs_slot_reset(struct mvs_info *mvi, struct sas_task *task,
				u32 slot_idx);

static int mvs_find_tag(struct mvs_info *mvi, struct sas_task *task, u32 *tag)
{
	if (task->lldd_task) {
		struct mvs_slot_info *slot;
		slot = (struct mvs_slot_info *) task->lldd_task;
		*tag = slot - mvi->slot_info;
		return 1;
	}
	return 0;
}

static void mvs_tag_clear(struct mvs_info *mvi, u32 tag)
{
	void *bitmap = (void *) &mvi->tags;
	clear_bit(tag, bitmap);
}

static void mvs_tag_free(struct mvs_info *mvi, u32 tag)
{
	mvs_tag_clear(mvi, tag);
}

static void mvs_tag_set(struct mvs_info *mvi, unsigned int tag)
{
	void *bitmap = (void *) &mvi->tags;
	set_bit(tag, bitmap);
}

static int mvs_tag_alloc(struct mvs_info *mvi, u32 *tag_out)
{
	unsigned int index, tag;
	void *bitmap = (void *) &mvi->tags;

	index = find_first_zero_bit(bitmap, MVS_SLOTS);
	tag = index;
	if (tag >= MVS_SLOTS)
		return -SAS_QUEUE_FULL;
	mvs_tag_set(mvi, tag);
	*tag_out = tag;
	return 0;
}

void mvs_tag_init(struct mvs_info *mvi)
{
	int i;
	for (i = 0; i < MVS_SLOTS; ++i)
		mvs_tag_clear(mvi, i);
}

static void mvs_hexdump(u32 size, u8 *data, u32 baseaddr)
{
	u32 i;
	u32 run;
	u32 offset;

	offset = 0;
	while (size) {
		printk("%08X : ", baseaddr + offset);
		if (size >= 16)
			run = 16;
		else
			run = size;
		size -= run;
		for (i = 0; i < 16; i++) {
			if (i < run)
				printk("%02X ", (u32)data[i]);
			else
				printk("   ");
		}
		printk(": ");
		for (i = 0; i < run; i++)
			printk("%c", isalnum(data[i]) ? data[i] : '.');
		printk("\n");
		data = &data[16];
		offset += run;
	}
	printk("\n");
}

#if _MV_DUMP
static void mvs_hba_sb_dump(struct mvs_info *mvi, u32 tag,
				   enum sas_protocol proto)
{
	u32 offset;
	struct pci_dev *pdev = mvi->pdev;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];

	offset = slot->cmd_size + MVS_OAF_SZ +
	    sizeof(struct mvs_prd) * slot->n_elem;
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Status buffer[%d] :\n",
			tag);
	mvs_hexdump(32, (u8 *) slot->response,
		    (u32) slot->buf_dma + offset);
}
#endif

static void mvs_hba_memory_dump(struct mvs_info *mvi, u32 tag,
				enum sas_protocol proto)
{
#if _MV_DUMP
	u32 sz, w_ptr;
	u64 addr;
	void __iomem *regs = mvi->regs;
	struct pci_dev *pdev = mvi->pdev;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];

	/*Delivery Queue */
	sz = mr32(TX_CFG) & TX_RING_SZ_MASK;
	w_ptr = slot->tx;
	addr = mr32(TX_HI) << 16 << 16 | mr32(TX_LO);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Delivery Queue Size=%04d , WRT_PTR=%04X\n", sz, w_ptr);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Delivery Queue Base Address=0x%llX (PA)"
		"(tx_dma=0x%llX), Entry=%04d\n",
		addr, mvi->tx_dma, w_ptr);
	mvs_hexdump(sizeof(u32), (u8 *)(&mvi->tx[mvi->tx_prod]),
			(u32) mvi->tx_dma + sizeof(u32) * w_ptr);
	/*Command List */
	addr = mvi->slot_dma;
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Command List Base Address=0x%llX (PA)"
		"(slot_dma=0x%llX), Header=%03d\n",
		addr, slot->buf_dma, tag);
	dev_printk(KERN_DEBUG, &pdev->dev, "Command Header[%03d]:\n", tag);
	/*mvs_cmd_hdr */
	mvs_hexdump(sizeof(struct mvs_cmd_hdr), (u8 *)(&mvi->slot[tag]),
		(u32) mvi->slot_dma + tag * sizeof(struct mvs_cmd_hdr));
	/*1.command table area */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Command Table :\n");
	mvs_hexdump(slot->cmd_size, (u8 *) slot->buf, (u32) slot->buf_dma);
	/*2.open address frame area */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->Open Address Frame :\n");
	mvs_hexdump(MVS_OAF_SZ, (u8 *) slot->buf + slot->cmd_size,
				(u32) slot->buf_dma + slot->cmd_size);
	/*3.status buffer */
	mvs_hba_sb_dump(mvi, tag, proto);
	/*4.PRD table */
	dev_printk(KERN_DEBUG, &pdev->dev, "+---->PRD table :\n");
	mvs_hexdump(sizeof(struct mvs_prd) * slot->n_elem,
		(u8 *) slot->buf + slot->cmd_size + MVS_OAF_SZ,
		(u32) slot->buf_dma + slot->cmd_size + MVS_OAF_SZ);
#endif
}

static void mvs_hba_cq_dump(struct mvs_info *mvi)
{
#if (_MV_DUMP > 2)
	u64 addr;
	void __iomem *regs = mvi->regs;
	struct pci_dev *pdev = mvi->pdev;
	u32 entry = mvi->rx_cons + 1;
	u32 rx_desc = le32_to_cpu(mvi->rx[entry]);

	/*Completion Queue */
	addr = mr32(RX_HI) << 16 << 16 | mr32(RX_LO);
	dev_printk(KERN_DEBUG, &pdev->dev, "Completion Task = 0x%p\n",
		   mvi->slot_info[rx_desc & RXQ_SLOT_MASK].task);
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Completion List Base Address=0x%llX (PA), "
		"CQ_Entry=%04d, CQ_WP=0x%08X\n",
		addr, entry - 1, mvi->rx[0]);
	mvs_hexdump(sizeof(u32), (u8 *)(&rx_desc),
		    mvi->rx_dma + sizeof(u32) * entry);
#endif
}

/* FIXME: locking? */
int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func, void *funcdata)
{
	struct mvs_info *mvi = sas_phy->ha->lldd_ha;
	int rc = 0, phy_id = sas_phy->id;
	u32 tmp;

	tmp = mvs_read_phy_ctl(mvi, phy_id);

	switch (func) {
	case PHY_FUNC_SET_LINK_RATE:{
			struct sas_phy_linkrates *rates = funcdata;
			u32 lrmin = 0, lrmax = 0;

			lrmin = (rates->minimum_linkrate << 8);
			lrmax = (rates->maximum_linkrate << 12);

			if (lrmin) {
				tmp &= ~(0xf << 8);
				tmp |= lrmin;
			}
			if (lrmax) {
				tmp &= ~(0xf << 12);
				tmp |= lrmax;
			}
			mvs_write_phy_ctl(mvi, phy_id, tmp);
			break;
		}

	case PHY_FUNC_HARD_RESET:
		if (tmp & PHY_RST_HARD)
			break;
		mvs_write_phy_ctl(mvi, phy_id, tmp | PHY_RST_HARD);
		break;

	case PHY_FUNC_LINK_RESET:
		mvs_write_phy_ctl(mvi, phy_id, tmp | PHY_RST);
		break;

	case PHY_FUNC_DISABLE:
	case PHY_FUNC_RELEASE_SPINUP_HOLD:
	default:
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static void mvs_bytes_dmaed(struct mvs_info *mvi, int i)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct asd_sas_phy *sas_phy = mvi->sas.sas_phy[i];

	if (!phy->phy_attached)
		return;

	if (sas_phy->phy) {
		struct sas_phy *sphy = sas_phy->phy;

		sphy->negotiated_linkrate = sas_phy->linkrate;
		sphy->minimum_linkrate = phy->minimum_linkrate;
		sphy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		sphy->maximum_linkrate = phy->maximum_linkrate;
		sphy->maximum_linkrate_hw = SAS_LINK_RATE_3_0_GBPS;
	}

	if (phy->phy_type & PORT_TYPE_SAS) {
		struct sas_identify_frame *id;

		id = (struct sas_identify_frame *)phy->frame_rcvd;
		id->dev_type = phy->identify.device_type;
		id->initiator_bits = SAS_PROTOCOL_ALL;
		id->target_bits = phy->identify.target_port_protocols;
	} else if (phy->phy_type & PORT_TYPE_SATA) {
		/* TODO */
	}
	mvi->sas.sas_phy[i]->frame_rcvd_size = phy->frame_rcvd_size;
	mvi->sas.notify_port_event(mvi->sas.sas_phy[i],
				   PORTE_BYTES_DMAED);
}

int mvs_slave_configure(struct scsi_device *sdev)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);
	int ret = sas_slave_configure(sdev);

	if (ret)
		return ret;

	if (dev_is_sata(dev)) {
		/* struct ata_port *ap = dev->sata_dev.ap; */
		/* struct ata_device *adev = ap->link.device; */

		/* clamp at no NCQ for the time being */
		/* adev->flags |= ATA_DFLAG_NCQ_OFF; */
		scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, 1);
	}
	return 0;
}

void mvs_scan_start(struct Scsi_Host *shost)
{
	int i;
	struct mvs_info *mvi = SHOST_TO_SAS_HA(shost)->lldd_ha;

	for (i = 0; i < mvi->chip->n_phy; ++i) {
		mvs_bytes_dmaed(mvi, i);
	}
}

int mvs_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	/* give the phy enabling interrupt event time to come in (1s
	 * is empirically about all it takes) */
	if (time < HZ)
		return 0;
	/* Wait for discovery to finish */
	scsi_flush_work(shost);
	return 1;
}

static int mvs_task_prep_smp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	int elem, rc, i;
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct scatterlist *sg_req, *sg_resp;
	u32 req_len, resp_len, tag = tei->tag;
	void *buf_tmp;
	u8 *buf_oaf;
	dma_addr_t buf_tmp_dma;
	struct mvs_prd *buf_prd;
	struct scatterlist *sg;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];
	struct asd_sas_port *sas_port = task->dev->port;
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);
#if _MV_DUMP
	u8 *buf_cmd;
	void *from;
#endif
	/*
	 * DMA-map SMP request, response buffers
	 */
	sg_req = &task->smp_task.smp_req;
	elem = pci_map_sg(mvi->pdev, sg_req, 1, PCI_DMA_TODEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);

	sg_resp = &task->smp_task.smp_resp;
	elem = pci_map_sg(mvi->pdev, sg_resp, 1, PCI_DMA_FROMDEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out;
	}
	resp_len = sg_dma_len(sg_resp);

	/* must be in dwords */
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_2;
	}

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ************** */
	buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

#if _MV_DUMP
	buf_cmd = buf_tmp;
	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);
	buf_tmp += req_len;
	buf_tmp_dma += req_len;
	slot->cmd_size = req_len;
#else
	hdr->cmd_tbl = cpu_to_le64(sg_dma_address(sg_req));
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	/*
	 * Fill in TX ring and command slot header
	 */
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32((TXQ_CMD_SMP << TXQ_CMD_SHIFT) |
					TXQ_MODE_I | tag |
					(sas_port->phy_mask << TXQ_PHY_SHIFT));

	hdr->flags |= flags;
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | ((req_len - 4) / 4));
	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = 0;

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (0 << 4) | 0x01; /* initiator, SMP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = 0xFFFF;		/* SAS SPEC */
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

#if _MV_DUMP
	/* copy cmd table */
	from = kmap_atomic(sg_page(sg_req), KM_IRQ0);
	memcpy(buf_cmd, from + sg_req->offset, req_len);
	kunmap_atomic(from, KM_IRQ0);
#endif
	return 0;

err_out_2:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_resp, 1,
		     PCI_DMA_FROMDEVICE);
err_out:
	pci_unmap_sg(mvi->pdev, &tei->task->smp_task.smp_req, 1,
		     PCI_DMA_TODEVICE);
	return rc;
}

static u32 mvs_get_ncq_tag(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
			qc->tf.command == ATA_CMD_FPDMA_READ) {
			*tag = qc->tag;
			return 1;
		}
	}

	return 0;
}

static int mvs_task_prep_ata(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct domain_device *dev = task->dev;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_slot_info *slot;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd;
	struct mvs_port *port = tei->port;
	u32 tag = tei->tag;
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf;
	dma_addr_t buf_tmp_dma;
	u32 i, req_len, resp_len;
	const u32 max_resp_len = SB_RFB_MAX;

	if (mvs_assign_reg_set(mvi, port) == MVS_ID_NOT_MAPPED)
		return -EBUSY;

	slot = &mvi->slot_info[tag];
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | tag |
					(TXQ_CMD_STP << TXQ_CMD_SHIFT) |
					(sas_port->phy_mask << TXQ_PHY_SHIFT) |
					(port->taskfileset << TXQ_SRS_SHIFT));

	if (task->ata_task.use_ncq)
		flags |= MCH_FPDMA;
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET) {
		if (task->ata_task.fis.command != ATA_CMD_ID_ATAPI)
			flags |= MCH_ATAPI;
	}

	/* FIXME: fill in port multiplier number */

	hdr->flags = cpu_to_le32(flags);

	/* FIXME: the low order order 5 bits for the TAG if enable NCQ */
	if (task->ata_task.use_ncq && mvs_get_ncq_tag(task, &hdr->tags))
		task->ata_task.fis.sector_count |= hdr->tags << 3;
	else
		hdr->tags = cpu_to_le32(tag);
	hdr->data_len = cpu_to_le32(task->total_xfer_len);

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_ATA_CMD_SZ bytes) ************** */
	buf_cmd = buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_ATA_CMD_SZ;
	buf_tmp_dma += MVS_ATA_CMD_SZ;
#if _MV_DUMP
	slot->cmd_size = MVS_ATA_CMD_SZ;
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	/* used for STP.  unused for SATA? */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	/* FIXME: probably unused, for SATA.  kept here just in case
	 * we get a STP/SATA error information record
	 */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	req_len = sizeof(struct host_to_dev_fis);
	resp_len = MVS_SLOT_BUF_SZ - MVS_ATA_CMD_SZ -
	    sizeof(struct mvs_err_info) - i;

	/* request, response lengths */
	resp_len = min(resp_len, max_resp_len);
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS and ATAPI CDB */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
	if (dev->sata_dev.command_set == ATAPI_COMMAND_SET)
		memcpy(buf_cmd + STP_ATAPI_CMD,
			task->ata_task.atapi_packet, 16);

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (2 << 4) | 0x1;	/* initiator, STP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(tag);
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

	return 0;
}

static int mvs_task_prep_ssp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct mvs_port *port = tei->port;
	struct mvs_slot_info *slot;
	struct scatterlist *sg;
	struct mvs_prd *buf_prd;
	struct ssp_frame_hdr *ssp_hdr;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf, fburst = 0;
	dma_addr_t buf_tmp_dma;
	u32 flags;
	u32 resp_len, req_len, i, tag = tei->tag;
	const u32 max_resp_len = SB_RFB_MAX;
	u8 phy_mask;

	slot = &mvi->slot_info[tag];

	phy_mask = (port->wide_port_phymap) ? port->wide_port_phymap :
		task->dev->port->phy_mask;
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | tag |
				(TXQ_CMD_SSP << TXQ_CMD_SHIFT) |
				(phy_mask << TXQ_PHY_SHIFT));

	flags = MCH_RETRY;
	if (task->ssp_task.enable_first_burst) {
		flags |= MCH_FBURST;
		fburst = (1 << 7);
	}
	hdr->flags = cpu_to_le32(flags |
				 (tei->n_elem << MCH_PRD_LEN_SHIFT) |
				 (MCH_SSP_FR_CMD << MCH_SSP_FR_TYPE_SHIFT));

	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = cpu_to_le32(task->total_xfer_len);

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ************** */
	buf_cmd = buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_SSP_CMD_SZ;
	buf_tmp_dma += MVS_SSP_CMD_SZ;
#if _MV_DUMP
	slot->cmd_size = MVS_SSP_CMD_SZ;
#endif

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table ********************************************* */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = sizeof(struct mvs_prd) * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);

	resp_len = MVS_SLOT_BUF_SZ - MVS_SSP_CMD_SZ - MVS_OAF_SZ -
	    sizeof(struct mvs_err_info) - i;
	resp_len = min(resp_len, max_resp_len);

	req_len = sizeof(struct ssp_frame_hdr) + 28;

	/* request, response lengths */
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	/* generate open address frame hdr (first 12 bytes) */
	buf_oaf[0] = (1 << 7) | (1 << 4) | 0x1;	/* initiator, SSP, ftype 1h */
	buf_oaf[1] = task->dev->linkrate & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(tag);
	memcpy(buf_oaf + 4, task->dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in SSP frame header (Command Table.SSP frame header) */
	ssp_hdr = (struct ssp_frame_hdr *)buf_cmd;
	ssp_hdr->frame_type = SSP_COMMAND;
	memcpy(ssp_hdr->hashed_dest_addr, task->dev->hashed_sas_addr,
	       HASHED_SAS_ADDR_SIZE);
	memcpy(ssp_hdr->hashed_src_addr,
	       task->dev->port->ha->hashed_sas_addr, HASHED_SAS_ADDR_SIZE);
	ssp_hdr->tag = cpu_to_be16(tag);

	/* fill in command frame IU */
	buf_cmd += sizeof(*ssp_hdr);
	memcpy(buf_cmd, &task->ssp_task.LUN, 8);
	buf_cmd[9] = fburst | task->ssp_task.task_attr |
			(task->ssp_task.task_prio << 3);
	memcpy(buf_cmd + 12, &task->ssp_task.cdb, 16);

	/* fill in PRD (scatter/gather) table, if any */
	for_each_sg(task->scatter, sg, tei->n_elem, i) {
		buf_prd->addr = cpu_to_le64(sg_dma_address(sg));
		buf_prd->len = cpu_to_le32(sg_dma_len(sg));
		buf_prd++;
	}

	return 0;
}

int mvs_task_exec(struct sas_task *task, const int num, gfp_t gfp_flags)
{
	struct domain_device *dev = task->dev;
	struct mvs_info *mvi = dev->port->ha->lldd_ha;
	struct pci_dev *pdev = mvi->pdev;
	void __iomem *regs = mvi->regs;
	struct mvs_task_exec_info tei;
	struct sas_task *t = task;
	struct mvs_slot_info *slot;
	u32 tag = 0xdeadbeef, rc, n_elem = 0;
	unsigned long flags;
	u32 n = num, pass = 0;

	spin_lock_irqsave(&mvi->lock, flags);
	do {
		dev = t->dev;
		tei.port = &mvi->port[dev->port->id];

		if (!tei.port->port_attached) {
			if (sas_protocol_ata(t->task_proto)) {
				rc = SAS_PHY_DOWN;
				goto out_done;
			} else {
				struct task_status_struct *ts = &t->task_status;
				ts->resp = SAS_TASK_UNDELIVERED;
				ts->stat = SAS_PHY_DOWN;
				t->task_done(t);
				if (n > 1)
					t = list_entry(t->list.next,
							struct sas_task, list);
				continue;
			}
		}

		if (!sas_protocol_ata(t->task_proto)) {
			if (t->num_scatter) {
				n_elem = pci_map_sg(mvi->pdev, t->scatter,
						    t->num_scatter,
						    t->data_dir);
				if (!n_elem) {
					rc = -ENOMEM;
					goto err_out;
				}
			}
		} else {
			n_elem = t->num_scatter;
		}

		rc = mvs_tag_alloc(mvi, &tag);
		if (rc)
			goto err_out;

		slot = &mvi->slot_info[tag];
		t->lldd_task = NULL;
		slot->n_elem = n_elem;
		memset(slot->buf, 0, MVS_SLOT_BUF_SZ);
		tei.task = t;
		tei.hdr = &mvi->slot[tag];
		tei.tag = tag;
		tei.n_elem = n_elem;

		switch (t->task_proto) {
		case SAS_PROTOCOL_SMP:
			rc = mvs_task_prep_smp(mvi, &tei);
			break;
		case SAS_PROTOCOL_SSP:
			rc = mvs_task_prep_ssp(mvi, &tei);
			break;
		case SAS_PROTOCOL_SATA:
		case SAS_PROTOCOL_STP:
		case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
			rc = mvs_task_prep_ata(mvi, &tei);
			break;
		default:
			dev_printk(KERN_ERR, &pdev->dev,
				"unknown sas_task proto: 0x%x\n",
				t->task_proto);
			rc = -EINVAL;
			break;
		}

		if (rc)
			goto err_out_tag;

		slot->task = t;
		slot->port = tei.port;
		t->lldd_task = (void *) slot;
		list_add_tail(&slot->list, &slot->port->list);
		/* TODO: select normal or high priority */

		spin_lock(&t->task_state_lock);
		t->task_state_flags |= SAS_TASK_AT_INITIATOR;
		spin_unlock(&t->task_state_lock);

		mvs_hba_memory_dump(mvi, tag, t->task_proto);

		++pass;
		mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_CHIP_SLOT_SZ - 1);
		if (n > 1)
			t = list_entry(t->list.next, struct sas_task, list);
	} while (--n);

	rc = 0;
	goto out_done;

err_out_tag:
	mvs_tag_free(mvi, tag);
err_out:
	dev_printk(KERN_ERR, &pdev->dev, "mvsas exec failed[%d]!\n", rc);
	if (!sas_protocol_ata(t->task_proto))
		if (n_elem)
			pci_unmap_sg(mvi->pdev, t->scatter, n_elem,
				     t->data_dir);
out_done:
	if (pass)
		mw32(TX_PROD_IDX, (mvi->tx_prod - 1) & (MVS_CHIP_SLOT_SZ - 1));
	spin_unlock_irqrestore(&mvi->lock, flags);
	return rc;
}

static void mvs_slot_free(struct mvs_info *mvi, u32 rx_desc)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	mvs_tag_clear(mvi, slot_idx);
}

static void mvs_slot_task_free(struct mvs_info *mvi, struct sas_task *task,
			  struct mvs_slot_info *slot, u32 slot_idx)
{
	if (!sas_protocol_ata(task->task_proto))
		if (slot->n_elem)
			pci_unmap_sg(mvi->pdev, task->scatter,
				     slot->n_elem, task->data_dir);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		pci_unmap_sg(mvi->pdev, &task->smp_task.smp_resp, 1,
			     PCI_DMA_FROMDEVICE);
		pci_unmap_sg(mvi->pdev, &task->smp_task.smp_req, 1,
			     PCI_DMA_TODEVICE);
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SSP:
	default:
		/* do nothing */
		break;
	}
	list_del(&slot->list);
	task->lldd_task = NULL;
	slot->task = NULL;
	slot->port = NULL;
}

static void mvs_update_wideport(struct mvs_info *mvi, int i)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct mvs_port *port = phy->port;
	int j, no;

	for_each_phy(port->wide_port_phymap, no, j, mvi->chip->n_phy)
		if (no & 1) {
			mvs_write_port_cfg_addr(mvi, no, PHYR_WIDE_PORT);
			mvs_write_port_cfg_data(mvi, no,
						port->wide_port_phymap);
		} else {
			mvs_write_port_cfg_addr(mvi, no, PHYR_WIDE_PORT);
			mvs_write_port_cfg_data(mvi, no, 0);
		}
}

static u32 mvs_is_phy_ready(struct mvs_info *mvi, int i)
{
	u32 tmp;
	struct mvs_phy *phy = &mvi->phy[i];
	struct mvs_port *port = phy->port;;

	tmp = mvs_read_phy_ctl(mvi, i);

	if ((tmp & PHY_READY_MASK) && !(phy->irq_status & PHYEV_POOF)) {
		if (!port)
			phy->phy_attached = 1;
		return tmp;
	}

	if (port) {
		if (phy->phy_type & PORT_TYPE_SAS) {
			port->wide_port_phymap &= ~(1U << i);
			if (!port->wide_port_phymap)
				port->port_attached = 0;
			mvs_update_wideport(mvi, i);
		} else if (phy->phy_type & PORT_TYPE_SATA)
			port->port_attached = 0;
		mvs_free_reg_set(mvi, phy->port);
		phy->port = NULL;
		phy->phy_attached = 0;
		phy->phy_type &= ~(PORT_TYPE_SAS | PORT_TYPE_SATA);
	}
	return 0;
}

static void *mvs_get_d2h_reg(struct mvs_info *mvi, int i, void *buf)
{
	u32 *s = (u32 *) buf;

	if (!s)
		return NULL;

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG3);
	s[3] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG2);
	s[2] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG1);
	s[1] = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_SATA_SIG0);
	s[0] = mvs_read_port_cfg_data(mvi, i);

	return (void *)s;
}

static u32 mvs_is_sig_fis_received(u32 irq_status)
{
	return irq_status & PHYEV_SIG_FIS;
}

static void mvs_update_phyinfo(struct mvs_info *mvi, int i,
					int get_st)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct pci_dev *pdev = mvi->pdev;
	u32 tmp;
	u64 tmp64;

	mvs_write_port_cfg_addr(mvi, i, PHYR_IDENTIFY);
	phy->dev_info = mvs_read_port_cfg_data(mvi, i);

	mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_HI);
	phy->dev_sas_addr = (u64) mvs_read_port_cfg_data(mvi, i) << 32;

	mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_LO);
	phy->dev_sas_addr |= mvs_read_port_cfg_data(mvi, i);

	if (get_st) {
		phy->irq_status = mvs_read_port_irq_stat(mvi, i);
		phy->phy_status = mvs_is_phy_ready(mvi, i);
	}

	if (phy->phy_status) {
		u32 phy_st;
		struct asd_sas_phy *sas_phy = mvi->sas.sas_phy[i];

		mvs_write_port_cfg_addr(mvi, i, PHYR_PHY_STAT);
		phy_st = mvs_read_port_cfg_data(mvi, i);

		sas_phy->linkrate =
			(phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
				PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET;
		phy->minimum_linkrate =
			(phy->phy_status &
				PHY_MIN_SPP_PHYS_LINK_RATE_MASK) >> 8;
		phy->maximum_linkrate =
			(phy->phy_status &
				PHY_MAX_SPP_PHYS_LINK_RATE_MASK) >> 12;

		if (phy->phy_type & PORT_TYPE_SAS) {
			/* Updated attached_sas_addr */
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_HI);
			phy->att_dev_sas_addr =
				(u64) mvs_read_port_cfg_data(mvi, i) << 32;
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_ADDR_LO);
			phy->att_dev_sas_addr |= mvs_read_port_cfg_data(mvi, i);
			mvs_write_port_cfg_addr(mvi, i, PHYR_ATT_DEV_INFO);
			phy->att_dev_info = mvs_read_port_cfg_data(mvi, i);
			phy->identify.device_type =
			    phy->att_dev_info & PORT_DEV_TYPE_MASK;

			if (phy->identify.device_type == SAS_END_DEV)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SSP;
			else if (phy->identify.device_type != NO_DEVICE)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SMP;
			if (phy_st & PHY_OOB_DTCTD)
				sas_phy->oob_mode = SAS_OOB_MODE;
			phy->frame_rcvd_size =
			    sizeof(struct sas_identify_frame);
		} else if (phy->phy_type & PORT_TYPE_SATA) {
			phy->identify.target_port_protocols = SAS_PROTOCOL_STP;
			if (mvs_is_sig_fis_received(phy->irq_status)) {
				phy->att_dev_sas_addr = i;	/* temp */
				if (phy_st & PHY_OOB_DTCTD)
					sas_phy->oob_mode = SATA_OOB_MODE;
				phy->frame_rcvd_size =
				    sizeof(struct dev_to_host_fis);
				mvs_get_d2h_reg(mvi, i,
						(void *)sas_phy->frame_rcvd);
			} else {
				dev_printk(KERN_DEBUG, &pdev->dev,
					"No sig fis\n");
				phy->phy_type &= ~(PORT_TYPE_SATA);
				goto out_done;
			}
		}
		tmp64 = cpu_to_be64(phy->att_dev_sas_addr);
		memcpy(sas_phy->attached_sas_addr, &tmp64, SAS_ADDR_SIZE);

		dev_printk(KERN_DEBUG, &pdev->dev,
			"phy[%d] Get Attached Address 0x%llX ,"
			" SAS Address 0x%llX\n",
			i,
			(unsigned long long)phy->att_dev_sas_addr,
			(unsigned long long)phy->dev_sas_addr);
		dev_printk(KERN_DEBUG, &pdev->dev,
			"Rate = %x , type = %d\n",
			sas_phy->linkrate, phy->phy_type);

		/* workaround for HW phy decoding error on 1.5g disk drive */
		mvs_write_port_vsr_addr(mvi, i, VSR_PHY_MODE6);
		tmp = mvs_read_port_vsr_data(mvi, i);
		if (((phy->phy_status & PHY_NEG_SPP_PHYS_LINK_RATE_MASK) >>
		     PHY_NEG_SPP_PHYS_LINK_RATE_MASK_OFFSET) ==
			SAS_LINK_RATE_1_5_GBPS)
			tmp &= ~PHY_MODE6_LATECLK;
		else
			tmp |= PHY_MODE6_LATECLK;
		mvs_write_port_vsr_data(mvi, i, tmp);

	}
out_done:
	if (get_st)
		mvs_write_port_irq_stat(mvi, i, phy->irq_status);
}

void mvs_port_formed(struct asd_sas_phy *sas_phy)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct mvs_info *mvi = sas_ha->lldd_ha;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct mvs_phy *phy = sas_phy->lldd_phy;
	struct mvs_port *port = &mvi->port[sas_port->id];
	unsigned long flags;

	spin_lock_irqsave(&mvi->lock, flags);
	port->port_attached = 1;
	phy->port = port;
	port->taskfileset = MVS_ID_NOT_MAPPED;
	if (phy->phy_type & PORT_TYPE_SAS) {
		port->wide_port_phymap = sas_port->phy_mask;
		mvs_update_wideport(mvi, sas_phy->id);
	}
	spin_unlock_irqrestore(&mvi->lock, flags);
}

int mvs_I_T_nexus_reset(struct domain_device *dev)
{
	return TMF_RESP_FUNC_FAILED;
}

static int mvs_sata_done(struct mvs_info *mvi, struct sas_task *task,
			u32 slot_idx, int err)
{
	struct mvs_port *port = mvi->slot_info[slot_idx].port;
	struct task_status_struct *tstat = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)tstat->buf;
	int stat = SAM_GOOD;

	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0],
	       SATA_RECEIVED_D2H_FIS(port->taskfileset),
	       sizeof(struct dev_to_host_fis));
	tstat->buf_valid_size = sizeof(*resp);
	if (unlikely(err))
		stat = SAS_PROTO_RESPONSE;
	return stat;
}

static int mvs_slot_err(struct mvs_info *mvi, struct sas_task *task,
			 u32 slot_idx)
{
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	u32 err_dw0 = le32_to_cpu(*(u32 *) (slot->response));
	u32 err_dw1 = le32_to_cpu(*(u32 *) (slot->response + 4));
	int stat = SAM_CHECK_COND;

	if (err_dw1 & SLOT_BSY_ERR) {
		stat = SAS_QUEUE_FULL;
		mvs_slot_reset(mvi, task, slot_idx);
	}
	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		break;
	case SAS_PROTOCOL_SMP:
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		if (err_dw0 & TFILE_ERR)
			stat = mvs_sata_done(mvi, task, slot_idx, 1);
		break;
	default:
		break;
	}

	mvs_hexdump(16, (u8 *) slot->response, 0);
	return stat;
}

static int mvs_slot_complete(struct mvs_info *mvi, u32 rx_desc, u32 flags)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	struct sas_task *task = slot->task;
	struct task_status_struct *tstat;
	struct mvs_port *port;
	bool aborted;
	void *to;

	if (unlikely(!task || !task->lldd_task))
		return -1;

	mvs_hba_cq_dump(mvi);

	spin_lock(&task->task_state_lock);
	aborted = task->task_state_flags & SAS_TASK_STATE_ABORTED;
	if (!aborted) {
		task->task_state_flags &=
		    ~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
		task->task_state_flags |= SAS_TASK_STATE_DONE;
	}
	spin_unlock(&task->task_state_lock);

	if (aborted) {
		mvs_slot_task_free(mvi, task, slot, slot_idx);
		mvs_slot_free(mvi, rx_desc);
		return -1;
	}

	port = slot->port;
	tstat = &task->task_status;
	memset(tstat, 0, sizeof(*tstat));
	tstat->resp = SAS_TASK_COMPLETE;

	if (unlikely(!port->port_attached || flags)) {
		mvs_slot_err(mvi, task, slot_idx);
		if (!sas_protocol_ata(task->task_proto))
			tstat->stat = SAS_PHY_DOWN;
		goto out;
	}

	/* error info record present */
	if (unlikely((rx_desc & RXQ_ERR) && (*(u64 *) slot->response))) {
		tstat->stat = mvs_slot_err(mvi, task, slot_idx);
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		/* hw says status == 0, datapres == 0 */
		if (rx_desc & RXQ_GOOD) {
			tstat->stat = SAM_GOOD;
			tstat->resp = SAS_TASK_COMPLETE;
		}
		/* response frame present */
		else if (rx_desc & RXQ_RSP) {
			struct ssp_response_iu *iu =
			    slot->response + sizeof(struct mvs_err_info);
			sas_ssp_task_response(&mvi->pdev->dev, task, iu);
		}

		/* should never happen? */
		else
			tstat->stat = SAM_CHECK_COND;
		break;

	case SAS_PROTOCOL_SMP: {
			struct scatterlist *sg_resp = &task->smp_task.smp_resp;
			tstat->stat = SAM_GOOD;
			to = kmap_atomic(sg_page(sg_resp), KM_IRQ0);
			memcpy(to + sg_resp->offset,
				slot->response + sizeof(struct mvs_err_info),
				sg_dma_len(sg_resp));
			kunmap_atomic(to, KM_IRQ0);
			break;
		}

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP: {
			tstat->stat = mvs_sata_done(mvi, task, slot_idx, 0);
			break;
		}

	default:
		tstat->stat = SAM_CHECK_COND;
		break;
	}

out:
	mvs_slot_task_free(mvi, task, slot, slot_idx);
	if (unlikely(tstat->stat != SAS_QUEUE_FULL))
		mvs_slot_free(mvi, rx_desc);

	spin_unlock(&mvi->lock);
	task->task_done(task);
	spin_lock(&mvi->lock);
	return tstat->stat;
}

static void mvs_release_task(struct mvs_info *mvi, int phy_no)
{
	struct list_head *pos, *n;
	struct mvs_slot_info *slot;
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct mvs_port *port = phy->port;
	u32 rx_desc;

	if (!port)
		return;

	list_for_each_safe(pos, n, &port->list) {
		slot = container_of(pos, struct mvs_slot_info, list);
		rx_desc = (u32) (slot - mvi->slot_info);
		mvs_slot_complete(mvi, rx_desc, 1);
	}
}

static void mvs_int_port(struct mvs_info *mvi, int phy_no, u32 events)
{
	struct pci_dev *pdev = mvi->pdev;
	struct sas_ha_struct *sas_ha = &mvi->sas;
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	phy->irq_status = mvs_read_port_irq_stat(mvi, phy_no);
	/*
	* events is port event now ,
	* we need check the interrupt status which belongs to per port.
	*/
	dev_printk(KERN_DEBUG, &pdev->dev,
		"Port %d Event = %X\n",
		phy_no, phy->irq_status);

	if (phy->irq_status & (PHYEV_POOF | PHYEV_DEC_ERR)) {
		mvs_release_task(mvi, phy_no);
		if (!mvs_is_phy_ready(mvi, phy_no)) {
			sas_phy_disconnected(sas_phy);
			sas_ha->notify_phy_event(sas_phy, PHYE_LOSS_OF_SIGNAL);
			dev_printk(KERN_INFO, &pdev->dev,
				"Port %d Unplug Notice\n", phy_no);

		} else
			mvs_phy_control(sas_phy, PHY_FUNC_LINK_RESET, NULL);
	}
	if (!(phy->irq_status & PHYEV_DEC_ERR)) {
		if (phy->irq_status & PHYEV_COMWAKE) {
			u32 tmp = mvs_read_port_irq_mask(mvi, phy_no);
			mvs_write_port_irq_mask(mvi, phy_no,
						tmp | PHYEV_SIG_FIS);
		}
		if (phy->irq_status & (PHYEV_SIG_FIS | PHYEV_ID_DONE)) {
			phy->phy_status = mvs_is_phy_ready(mvi, phy_no);
			if (phy->phy_status) {
				mvs_detect_porttype(mvi, phy_no);

				if (phy->phy_type & PORT_TYPE_SATA) {
					u32 tmp = mvs_read_port_irq_mask(mvi,
								phy_no);
					tmp &= ~PHYEV_SIG_FIS;
					mvs_write_port_irq_mask(mvi,
								phy_no, tmp);
				}

				mvs_update_phyinfo(mvi, phy_no, 0);
				sas_ha->notify_phy_event(sas_phy,
							PHYE_OOB_DONE);
				mvs_bytes_dmaed(mvi, phy_no);
			} else {
				dev_printk(KERN_DEBUG, &pdev->dev,
					"plugin interrupt but phy is gone\n");
				mvs_phy_control(sas_phy, PHY_FUNC_LINK_RESET,
							NULL);
			}
		} else if (phy->irq_status & PHYEV_BROAD_CH) {
			mvs_release_task(mvi, phy_no);
			sas_ha->notify_port_event(sas_phy,
						PORTE_BROADCAST_RCVD);
		}
	}
	mvs_write_port_irq_stat(mvi, phy_no, phy->irq_status);
}

static int mvs_int_rx(struct mvs_info *mvi, bool self_clear)
{
	void __iomem *regs = mvi->regs;
	u32 rx_prod_idx, rx_desc;
	bool attn = false;
	struct pci_dev *pdev = mvi->pdev;

	/* the first dword in the RX ring is special: it contains
	 * a mirror of the hardware's RX producer index, so that
	 * we don't have to stall the CPU reading that register.
	 * The actual RX ring is offset by one dword, due to this.
	 */
	rx_prod_idx = mvi->rx_cons;
	mvi->rx_cons = le32_to_cpu(mvi->rx[0]);
	if (mvi->rx_cons == 0xfff)	/* h/w hasn't touched RX ring yet */
		return 0;

	/* The CMPL_Q may come late, read from register and try again
	* note: if coalescing is enabled,
	* it will need to read from register every time for sure
	*/
	if (mvi->rx_cons == rx_prod_idx)
		mvi->rx_cons = mr32(RX_CONS_IDX) & RX_RING_SZ_MASK;

	if (mvi->rx_cons == rx_prod_idx)
		return 0;

	while (mvi->rx_cons != rx_prod_idx) {

		/* increment our internal RX consumer pointer */
		rx_prod_idx = (rx_prod_idx + 1) & (MVS_RX_RING_SZ - 1);

		rx_desc = le32_to_cpu(mvi->rx[rx_prod_idx + 1]);

		if (likely(rx_desc & RXQ_DONE))
			mvs_slot_complete(mvi, rx_desc, 0);
		if (rx_desc & RXQ_ATTN) {
			attn = true;
			dev_printk(KERN_DEBUG, &pdev->dev, "ATTN %X\n",
				rx_desc);
		} else if (rx_desc & RXQ_ERR) {
			if (!(rx_desc & RXQ_DONE))
				mvs_slot_complete(mvi, rx_desc, 0);
			dev_printk(KERN_DEBUG, &pdev->dev, "RXQ_ERR %X\n",
				rx_desc);
		} else if (rx_desc & RXQ_SLOT_RESET) {
			dev_printk(KERN_DEBUG, &pdev->dev, "Slot reset[%X]\n",
				rx_desc);
			mvs_slot_free(mvi, rx_desc);
		}
	}

	if (attn && self_clear)
		mvs_int_full(mvi);

	return 0;
}

#ifndef MVS_DISABLE_NVRAM
static int mvs_eep_read(void __iomem *regs, u32 addr, u32 *data)
{
	int timeout = 1000;

	if (addr & ~SPI_ADDR_MASK)
		return -EINVAL;

	writel(addr, regs + SPI_CMD);
	writel(TWSI_RD, regs + SPI_CTL);

	while (timeout-- > 0) {
		if (readl(regs + SPI_CTL) & TWSI_RDY) {
			*data = readl(regs + SPI_DATA);
			return 0;
		}

		udelay(10);
	}

	return -EBUSY;
}

static int mvs_eep_read_buf(void __iomem *regs, u32 addr,
			    void *buf, u32 buflen)
{
	u32 addr_end, tmp_addr, i, j;
	u32 tmp = 0;
	int rc;
	u8 *tmp8, *buf8 = buf;

	addr_end = addr + buflen;
	tmp_addr = ALIGN(addr, 4);
	if (addr > 0xff)
		return -EINVAL;

	j = addr & 0x3;
	if (j) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		tmp8 = (u8 *)&tmp;
		for (i = j; i < 4; i++)
			*buf8++ = tmp8[i];

		tmp_addr += 4;
	}

	for (j = ALIGN(addr_end, 4); tmp_addr < j; tmp_addr += 4) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		memcpy(buf8, &tmp, 4);
		buf8 += 4;
	}

	if (tmp_addr < addr_end) {
		rc = mvs_eep_read(regs, tmp_addr, &tmp);
		if (rc)
			return rc;

		tmp8 = (u8 *)&tmp;
		j = addr_end - tmp_addr;
		for (i = 0; i < j; i++)
			*buf8++ = tmp8[i];

		tmp_addr += 4;
	}

	return 0;
}
#endif

int mvs_nvram_read(struct mvs_info *mvi, u32 addr, void *buf, u32 buflen)
{
#ifndef MVS_DISABLE_NVRAM
	void __iomem *regs = mvi->regs;
	int rc, i;
	u32 sum;
	u8 hdr[2], *tmp;
	const char *msg;

	rc = mvs_eep_read_buf(regs, addr, &hdr, 2);
	if (rc) {
		msg = "nvram hdr read failed";
		goto err_out;
	}
	rc = mvs_eep_read_buf(regs, addr + 2, buf, buflen);
	if (rc) {
		msg = "nvram read failed";
		goto err_out;
	}

	if (hdr[0] != 0x5A) {
		/* entry id */
		msg = "invalid nvram entry id";
		rc = -ENOENT;
		goto err_out;
	}

	tmp = buf;
	sum = ((u32)hdr[0]) + ((u32)hdr[1]);
	for (i = 0; i < buflen; i++)
		sum += ((u32)tmp[i]);

	if (sum) {
		msg = "nvram checksum failure";
		rc = -EILSEQ;
		goto err_out;
	}

	return 0;

err_out:
	dev_printk(KERN_ERR, &mvi->pdev->dev, "%s", msg);
	return rc;
#else
	/* FIXME , For SAS target mode */
	memcpy(buf, "\x50\x05\x04\x30\x11\xab\x00\x00", 8);
	return 0;
#endif
}

static void mvs_int_sata(struct mvs_info *mvi)
{
	u32 tmp;
	void __iomem *regs = mvi->regs;
	tmp = mr32(INT_STAT_SRS);
	mw32(INT_STAT_SRS, tmp & 0xFFFF);
}

static void mvs_slot_reset(struct mvs_info *mvi, struct sas_task *task,
				u32 slot_idx)
{
	void __iomem *regs = mvi->regs;
	struct domain_device *dev = task->dev;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_port *port = mvi->slot_info[slot_idx].port;
	u32 reg_set, phy_mask;

	if (!sas_protocol_ata(task->task_proto)) {
		reg_set = 0;
		phy_mask = (port->wide_port_phymap) ? port->wide_port_phymap :
				sas_port->phy_mask;
	} else {
		reg_set = port->taskfileset;
		phy_mask = sas_port->phy_mask;
	}
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | slot_idx |
					(TXQ_CMD_SLOT_RESET << TXQ_CMD_SHIFT) |
					(phy_mask << TXQ_PHY_SHIFT) |
					(reg_set << TXQ_SRS_SHIFT));

	mw32(TX_PROD_IDX, mvi->tx_prod);
	mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_CHIP_SLOT_SZ - 1);
}

void mvs_int_full(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	u32 tmp, stat;
	int i;

	stat = mr32(INT_STAT);

	mvs_int_rx(mvi, false);

	for (i = 0; i < MVS_MAX_PORTS; i++) {
		tmp = (stat >> i) & (CINT_PORT | CINT_PORT_STOPPED);
		if (tmp)
			mvs_int_port(mvi, i, tmp);
	}

	if (stat & CINT_SRS)
		mvs_int_sata(mvi);

	mw32(INT_STAT, stat);
}

#ifndef MVS_DISABLE_MSI
static irqreturn_t mvs_msi_interrupt(int irq, void *opaque)
{
	struct mvs_info *mvi = opaque;

#ifndef MVS_USE_TASKLET
	spin_lock(&mvi->lock);

	mvs_int_rx(mvi, true);

	spin_unlock(&mvi->lock);
#else
	tasklet_schedule(&mvi->tasklet);
#endif
	return IRQ_HANDLED;
}
#endif

int mvs_task_abort(struct sas_task *task)
{
	int rc;
	unsigned long flags;
	struct mvs_info *mvi = task->dev->port->ha->lldd_ha;
	struct pci_dev *pdev = mvi->pdev;
	int tag;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		rc = TMF_RESP_FUNC_COMPLETE;
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		goto out_done;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		dev_printk(KERN_DEBUG, &pdev->dev, "SMP Abort! \n");
		break;
	case SAS_PROTOCOL_SSP:
		dev_printk(KERN_DEBUG, &pdev->dev, "SSP Abort! \n");
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:{
		dev_printk(KERN_DEBUG, &pdev->dev, "STP Abort! \n");
#if _MV_DUMP
		dev_printk(KERN_DEBUG, &pdev->dev, "Dump D2H FIS: \n");
		mvs_hexdump(sizeof(struct host_to_dev_fis),
				(void *)&task->ata_task.fis, 0);
		dev_printk(KERN_DEBUG, &pdev->dev, "Dump ATAPI Cmd : \n");
		mvs_hexdump(16, task->ata_task.atapi_packet, 0);
#endif
		spin_lock_irqsave(&task->task_state_lock, flags);
		if (task->task_state_flags & SAS_TASK_NEED_DEV_RESET) {
			/* TODO */
			;
		}
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		break;
	}
	default:
		break;
	}

	if (mvs_find_tag(mvi, task, &tag)) {
		spin_lock_irqsave(&mvi->lock, flags);
		mvs_slot_task_free(mvi, task, &mvi->slot_info[tag], tag);
		spin_unlock_irqrestore(&mvi->lock, flags);
	}
	if (!mvs_task_exec(task, 1, GFP_ATOMIC))
		rc = TMF_RESP_FUNC_COMPLETE;
	else
		rc = TMF_RESP_FUNC_FAILED;
out_done:
	return rc;
}

int __devinit mvs_hw_init(struct mvs_info *mvi)
{
	void __iomem *regs = mvi->regs;
	int i;
	u32 tmp, cctl;

	/* make sure interrupts are masked immediately (paranoia) */
	mw32(GBL_CTL, 0);
	tmp = mr32(GBL_CTL);

	/* Reset Controller */
	if (!(tmp & HBA_RST)) {
		if (mvi->flags & MVF_PHY_PWR_FIX) {
			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
			tmp &= ~PCTL_PWR_ON;
			tmp |= PCTL_OFF;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

			pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
			tmp &= ~PCTL_PWR_ON;
			tmp |= PCTL_OFF;
			pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);
		}

		/* global reset, incl. COMRESET/H_RESET_N (self-clearing) */
		mw32_f(GBL_CTL, HBA_RST);
	}

	/* wait for reset to finish; timeout is just a guess */
	i = 1000;
	while (i-- > 0) {
		msleep(10);

		if (!(mr32(GBL_CTL) & HBA_RST))
			break;
	}
	if (mr32(GBL_CTL) & HBA_RST) {
		dev_printk(KERN_ERR, &mvi->pdev->dev, "HBA reset failed\n");
		return -EBUSY;
	}

	/* Init Chip */
	/* make sure RST is set; HBA_RST /should/ have done that for us */
	cctl = mr32(CTL);
	if (cctl & CCTL_RST)
		cctl &= ~CCTL_RST;
	else
		mw32_f(CTL, cctl | CCTL_RST);

	/* write to device control _AND_ device status register? - A.C. */
	pci_read_config_dword(mvi->pdev, PCR_DEV_CTRL, &tmp);
	tmp &= ~PRD_REQ_MASK;
	tmp |= PRD_REQ_SIZE;
	pci_write_config_dword(mvi->pdev, PCR_DEV_CTRL, tmp);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL, tmp);

	pci_read_config_dword(mvi->pdev, PCR_PHY_CTL2, &tmp);
	tmp |= PCTL_PWR_ON;
	tmp &= ~PCTL_OFF;
	pci_write_config_dword(mvi->pdev, PCR_PHY_CTL2, tmp);

	mw32_f(CTL, cctl);

	/* reset control */
	mw32(PCS, 0);		/*MVS_PCS */

	mvs_phy_hacks(mvi);

	mw32(CMD_LIST_LO, mvi->slot_dma);
	mw32(CMD_LIST_HI, (mvi->slot_dma >> 16) >> 16);

	mw32(RX_FIS_LO, mvi->rx_fis_dma);
	mw32(RX_FIS_HI, (mvi->rx_fis_dma >> 16) >> 16);

	mw32(TX_CFG, MVS_CHIP_SLOT_SZ);
	mw32(TX_LO, mvi->tx_dma);
	mw32(TX_HI, (mvi->tx_dma >> 16) >> 16);

	mw32(RX_CFG, MVS_RX_RING_SZ);
	mw32(RX_LO, mvi->rx_dma);
	mw32(RX_HI, (mvi->rx_dma >> 16) >> 16);

	/* enable auto port detection */
	mw32(GBL_PORT_TYPE, MODE_AUTO_DET_EN);
	msleep(1100);
	/* init and reset phys */
	for (i = 0; i < mvi->chip->n_phy; i++) {
		u32 lo = be32_to_cpu(*(u32 *)&mvi->sas_addr[4]);
		u32 hi = be32_to_cpu(*(u32 *)&mvi->sas_addr[0]);

		mvs_detect_porttype(mvi, i);

		/* set phy local SAS address */
		mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_LO);
		mvs_write_port_cfg_data(mvi, i, lo);
		mvs_write_port_cfg_addr(mvi, i, PHYR_ADDR_HI);
		mvs_write_port_cfg_data(mvi, i, hi);

		/* reset phy */
		tmp = mvs_read_phy_ctl(mvi, i);
		tmp |= PHY_RST;
		mvs_write_phy_ctl(mvi, i, tmp);
	}

	msleep(100);

	for (i = 0; i < mvi->chip->n_phy; i++) {
		/* clear phy int status */
		tmp = mvs_read_port_irq_stat(mvi, i);
		tmp &= ~PHYEV_SIG_FIS;
		mvs_write_port_irq_stat(mvi, i, tmp);

		/* set phy int mask */
		tmp = PHYEV_RDY_CH | PHYEV_BROAD_CH | PHYEV_UNASSOC_FIS |
			PHYEV_ID_DONE | PHYEV_DEC_ERR;
		mvs_write_port_irq_mask(mvi, i, tmp);

		msleep(100);
		mvs_update_phyinfo(mvi, i, 1);
		mvs_enable_xmt(mvi, i);
	}

	/* FIXME: update wide port bitmaps */

	/* little endian for open address and command table, etc. */
	/* A.C.
	 * it seems that ( from the spec ) turning on big-endian won't
	 * do us any good on big-endian machines, need further confirmation
	 */
	cctl = mr32(CTL);
	cctl |= CCTL_ENDIAN_CMD;
	cctl |= CCTL_ENDIAN_DATA;
	cctl &= ~CCTL_ENDIAN_OPEN;
	cctl |= CCTL_ENDIAN_RSP;
	mw32_f(CTL, cctl);

	/* reset CMD queue */
	tmp = mr32(PCS);
	tmp |= PCS_CMD_RST;
	mw32(PCS, tmp);
	/* interrupt coalescing may cause missing HW interrput in some case,
	 * and the max count is 0x1ff, while our max slot is 0x200,
	 * it will make count 0.
	 */
	tmp = 0;
	mw32(INT_COAL, tmp);

	tmp = 0x100;
	mw32(INT_COAL_TMOUT, tmp);

	/* ladies and gentlemen, start your engines */
	mw32(TX_CFG, 0);
	mw32(TX_CFG, MVS_CHIP_SLOT_SZ | TX_EN);
	mw32(RX_CFG, MVS_RX_RING_SZ | RX_EN);
	/* enable CMD/CMPL_Q/RESP mode */
	mw32(PCS, PCS_SATA_RETRY | PCS_FIS_RX_EN | PCS_CMD_EN);

	/* enable completion queue interrupt */
	tmp = (CINT_PORT_MASK | CINT_DONE | CINT_MEM | CINT_SRS);
	mw32(INT_MASK, tmp);

	/* Enable SRS interrupt */
	mw32(INT_MASK_SRS, 0xFF);
	return 0;
}

void __devinit mvs_print_info(struct mvs_info *mvi)
{
	struct pci_dev *pdev = mvi->pdev;
	static int printed_version;

	if (!printed_version++)
		dev_printk(KERN_INFO, &pdev->dev, "version " DRV_VERSION "\n");

	dev_printk(KERN_INFO, &pdev->dev, "%u phys, addr %llx\n",
		   mvi->chip->n_phy, SAS_ADDR(mvi->sas_addr));
}

