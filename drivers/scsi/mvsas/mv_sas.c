/*
 * Marvell 88SE64xx/88SE94xx main function
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

static int mvs_find_tag(struct mvs_info *mvi, struct sas_task *task, u32 *tag)
{
	if (task->lldd_task) {
		struct mvs_slot_info *slot;
		slot = task->lldd_task;
		*tag = slot->slot_tag;
		return 1;
	}
	return 0;
}

void mvs_tag_clear(struct mvs_info *mvi, u32 tag)
{
	void *bitmap = mvi->tags;
	clear_bit(tag, bitmap);
}

void mvs_tag_free(struct mvs_info *mvi, u32 tag)
{
	mvs_tag_clear(mvi, tag);
}

void mvs_tag_set(struct mvs_info *mvi, unsigned int tag)
{
	void *bitmap = mvi->tags;
	set_bit(tag, bitmap);
}

inline int mvs_tag_alloc(struct mvs_info *mvi, u32 *tag_out)
{
	unsigned int index, tag;
	void *bitmap = mvi->tags;

	index = find_first_zero_bit(bitmap, mvi->tags_num);
	tag = index;
	if (tag >= mvi->tags_num)
		return -SAS_QUEUE_FULL;
	mvs_tag_set(mvi, tag);
	*tag_out = tag;
	return 0;
}

void mvs_tag_init(struct mvs_info *mvi)
{
	int i;
	for (i = 0; i < mvi->tags_num; ++i)
		mvs_tag_clear(mvi, i);
}

static struct mvs_info *mvs_find_dev_mvi(struct domain_device *dev)
{
	unsigned long i = 0, j = 0, hi = 0;
	struct sas_ha_struct *sha = dev->port->ha;
	struct mvs_info *mvi = NULL;
	struct asd_sas_phy *phy;

	while (sha->sas_port[i]) {
		if (sha->sas_port[i] == dev->port) {
			phy =  container_of(sha->sas_port[i]->phy_list.next,
				struct asd_sas_phy, port_phy_el);
			j = 0;
			while (sha->sas_phy[j]) {
				if (sha->sas_phy[j] == phy)
					break;
				j++;
			}
			break;
		}
		i++;
	}
	hi = j/((struct mvs_prv_info *)sha->lldd_ha)->n_phy;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[hi];

	return mvi;

}

static int mvs_find_dev_phyno(struct domain_device *dev, int *phyno)
{
	unsigned long i = 0, j = 0, n = 0, num = 0;
	struct mvs_device *mvi_dev = (struct mvs_device *)dev->lldd_dev;
	struct mvs_info *mvi = mvi_dev->mvi_info;
	struct sas_ha_struct *sha = dev->port->ha;

	while (sha->sas_port[i]) {
		if (sha->sas_port[i] == dev->port) {
			struct asd_sas_phy *phy;
			list_for_each_entry(phy,
				&sha->sas_port[i]->phy_list, port_phy_el) {
				j = 0;
				while (sha->sas_phy[j]) {
					if (sha->sas_phy[j] == phy)
						break;
					j++;
				}
				phyno[n] = (j >= mvi->chip->n_phy) ?
					(j - mvi->chip->n_phy) : j;
				num++;
				n++;
			}
			break;
		}
		i++;
	}
	return num;
}

struct mvs_device *mvs_find_dev_by_reg_set(struct mvs_info *mvi,
						u8 reg_set)
{
	u32 dev_no;
	for (dev_no = 0; dev_no < MVS_MAX_DEVICES; dev_no++) {
		if (mvi->devices[dev_no].taskfileset == MVS_ID_NOT_MAPPED)
			continue;

		if (mvi->devices[dev_no].taskfileset == reg_set)
			return &mvi->devices[dev_no];
	}
	return NULL;
}

static inline void mvs_free_reg_set(struct mvs_info *mvi,
				struct mvs_device *dev)
{
	if (!dev) {
		mv_printk("device has been free.\n");
		return;
	}
	if (dev->taskfileset == MVS_ID_NOT_MAPPED)
		return;
	MVS_CHIP_DISP->free_reg_set(mvi, &dev->taskfileset);
}

static inline u8 mvs_assign_reg_set(struct mvs_info *mvi,
				struct mvs_device *dev)
{
	if (dev->taskfileset != MVS_ID_NOT_MAPPED)
		return 0;
	return MVS_CHIP_DISP->assign_reg_set(mvi, &dev->taskfileset);
}

void mvs_phys_reset(struct mvs_info *mvi, u32 phy_mask, int hard)
{
	u32 no;
	for_each_phy(phy_mask, phy_mask, no) {
		if (!(phy_mask & 1))
			continue;
		MVS_CHIP_DISP->phy_reset(mvi, no, hard);
	}
}

int mvs_phy_control(struct asd_sas_phy *sas_phy, enum phy_func func,
			void *funcdata)
{
	int rc = 0, phy_id = sas_phy->id;
	u32 tmp, i = 0, hi;
	struct sas_ha_struct *sha = sas_phy->ha;
	struct mvs_info *mvi = NULL;

	while (sha->sas_phy[i]) {
		if (sha->sas_phy[i] == sas_phy)
			break;
		i++;
	}
	hi = i/((struct mvs_prv_info *)sha->lldd_ha)->n_phy;
	mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[hi];

	switch (func) {
	case PHY_FUNC_SET_LINK_RATE:
		MVS_CHIP_DISP->phy_set_link_rate(mvi, phy_id, funcdata);
		break;

	case PHY_FUNC_HARD_RESET:
		tmp = MVS_CHIP_DISP->read_phy_ctl(mvi, phy_id);
		if (tmp & PHY_RST_HARD)
			break;
		MVS_CHIP_DISP->phy_reset(mvi, phy_id, MVS_HARD_RESET);
		break;

	case PHY_FUNC_LINK_RESET:
		MVS_CHIP_DISP->phy_enable(mvi, phy_id);
		MVS_CHIP_DISP->phy_reset(mvi, phy_id, MVS_SOFT_RESET);
		break;

	case PHY_FUNC_DISABLE:
		MVS_CHIP_DISP->phy_disable(mvi, phy_id);
		break;
	case PHY_FUNC_RELEASE_SPINUP_HOLD:
	default:
		rc = -ENOSYS;
	}
	msleep(200);
	return rc;
}

void mvs_set_sas_addr(struct mvs_info *mvi, int port_id, u32 off_lo,
		      u32 off_hi, u64 sas_addr)
{
	u32 lo = (u32)sas_addr;
	u32 hi = (u32)(sas_addr>>32);

	MVS_CHIP_DISP->write_port_cfg_addr(mvi, port_id, off_lo);
	MVS_CHIP_DISP->write_port_cfg_data(mvi, port_id, lo);
	MVS_CHIP_DISP->write_port_cfg_addr(mvi, port_id, off_hi);
	MVS_CHIP_DISP->write_port_cfg_data(mvi, port_id, hi);
}

static void mvs_bytes_dmaed(struct mvs_info *mvi, int i)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;
	struct sas_ha_struct *sas_ha;
	if (!phy->phy_attached)
		return;

	if (!(phy->att_dev_info & PORT_DEV_TRGT_MASK)
		&& phy->phy_type & PORT_TYPE_SAS) {
		return;
	}

	sas_ha = mvi->sas;
	sas_ha->notify_phy_event(sas_phy, PHYE_OOB_DONE);

	if (sas_phy->phy) {
		struct sas_phy *sphy = sas_phy->phy;

		sphy->negotiated_linkrate = sas_phy->linkrate;
		sphy->minimum_linkrate = phy->minimum_linkrate;
		sphy->minimum_linkrate_hw = SAS_LINK_RATE_1_5_GBPS;
		sphy->maximum_linkrate = phy->maximum_linkrate;
		sphy->maximum_linkrate_hw = MVS_CHIP_DISP->phy_max_link_rate();
	}

	if (phy->phy_type & PORT_TYPE_SAS) {
		struct sas_identify_frame *id;

		id = (struct sas_identify_frame *)phy->frame_rcvd;
		id->dev_type = phy->identify.device_type;
		id->initiator_bits = SAS_PROTOCOL_ALL;
		id->target_bits = phy->identify.target_port_protocols;

		/* direct attached SAS device */
		if (phy->att_dev_info & PORT_SSP_TRGT_MASK) {
			MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_PHY_STAT);
			MVS_CHIP_DISP->write_port_cfg_data(mvi, i, 0x00);
		}
	} else if (phy->phy_type & PORT_TYPE_SATA) {
		/*Nothing*/
	}
	mv_dprintk("phy %d byte dmaded.\n", i + mvi->id * mvi->chip->n_phy);

	sas_phy->frame_rcvd_size = phy->frame_rcvd_size;

	mvi->sas->notify_port_event(sas_phy,
				   PORTE_BYTES_DMAED);
}

void mvs_scan_start(struct Scsi_Host *shost)
{
	int i, j;
	unsigned short core_nr;
	struct mvs_info *mvi;
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct mvs_prv_info *mvs_prv = sha->lldd_ha;

	core_nr = ((struct mvs_prv_info *)sha->lldd_ha)->n_host;

	for (j = 0; j < core_nr; j++) {
		mvi = ((struct mvs_prv_info *)sha->lldd_ha)->mvi[j];
		for (i = 0; i < mvi->chip->n_phy; ++i)
			mvs_bytes_dmaed(mvi, i);
	}
	mvs_prv->scan_finished = 1;
}

int mvs_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct sas_ha_struct *sha = SHOST_TO_SAS_HA(shost);
	struct mvs_prv_info *mvs_prv = sha->lldd_ha;

	if (mvs_prv->scan_finished == 0)
		return 0;

	sas_drain_work(sha);
	return 1;
}

static int mvs_task_prep_smp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei)
{
	int elem, rc, i;
	struct sas_ha_struct *sha = mvi->sas;
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct domain_device *dev = task->dev;
	struct asd_sas_port *sas_port = dev->port;
	struct sas_phy *sphy = dev->phy;
	struct asd_sas_phy *sas_phy = sha->sas_phy[sphy->number];
	struct scatterlist *sg_req, *sg_resp;
	u32 req_len, resp_len, tag = tei->tag;
	void *buf_tmp;
	u8 *buf_oaf;
	dma_addr_t buf_tmp_dma;
	void *buf_prd;
	struct mvs_slot_info *slot = &mvi->slot_info[tag];
	u32 flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);

	/*
	 * DMA-map SMP request, response buffers
	 */
	sg_req = &task->smp_task.smp_req;
	elem = dma_map_sg(mvi->dev, sg_req, 1, PCI_DMA_TODEVICE);
	if (!elem)
		return -ENOMEM;
	req_len = sg_dma_len(sg_req);

	sg_resp = &task->smp_task.smp_resp;
	elem = dma_map_sg(mvi->dev, sg_resp, 1, PCI_DMA_FROMDEVICE);
	if (!elem) {
		rc = -ENOMEM;
		goto err_out;
	}
	resp_len = SB_RFB_MAX;

	/* must be in dwords */
	if ((req_len & 0x3) || (resp_len & 0x3)) {
		rc = -EINVAL;
		goto err_out_2;
	}

	/*
	 * arrange MVS_SLOT_BUF_SZ-sized DMA buffer according to our needs
	 */

	/* region 1: command table area (MVS_SSP_CMD_SZ bytes) ***** */
	buf_tmp = slot->buf;
	buf_tmp_dma = slot->buf_dma;

	hdr->cmd_tbl = cpu_to_le64(sg_dma_address(sg_req));

	/* region 2: open address frame area (MVS_OAF_SZ bytes) ********* */
	buf_oaf = buf_tmp;
	hdr->open_frame = cpu_to_le64(buf_tmp_dma);

	buf_tmp += MVS_OAF_SZ;
	buf_tmp_dma += MVS_OAF_SZ;

	/* region 3: PRD table *********************************** */
	buf_prd = buf_tmp;
	if (tei->n_elem)
		hdr->prd_tbl = cpu_to_le64(buf_tmp_dma);
	else
		hdr->prd_tbl = 0;

	i = MVS_CHIP_DISP->prd_size() * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);
	if (mvi->flags & MVF_FLAG_SOC)
		hdr->reserved[0] = 0;

	/*
	 * Fill in TX ring and command slot header
	 */
	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32((TXQ_CMD_SMP << TXQ_CMD_SHIFT) |
					TXQ_MODE_I | tag |
					(MVS_PHY_ID << TXQ_PHY_SHIFT));

	hdr->flags |= flags;
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | ((req_len - 4) / 4));
	hdr->tags = cpu_to_le32(tag);
	hdr->data_len = 0;

	/* generate open address frame hdr (first 12 bytes) */
	/* initiator, SMP, ftype 1h */
	buf_oaf[0] = (1 << 7) | (PROTOCOL_SMP << 4) | 0x01;
	buf_oaf[1] = min(sas_port->linkrate, dev->linkrate) & 0xf;
	*(u16 *)(buf_oaf + 2) = 0xFFFF;		/* SAS SPEC */
	memcpy(buf_oaf + 4, dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	MVS_CHIP_DISP->make_prd(task->scatter, tei->n_elem, buf_prd);

	return 0;

err_out_2:
	dma_unmap_sg(mvi->dev, &tei->task->smp_task.smp_resp, 1,
		     PCI_DMA_FROMDEVICE);
err_out:
	dma_unmap_sg(mvi->dev, &tei->task->smp_task.smp_req, 1,
		     PCI_DMA_TODEVICE);
	return rc;
}

static u32 mvs_get_ncq_tag(struct sas_task *task, u32 *tag)
{
	struct ata_queued_cmd *qc = task->uldd_task;

	if (qc) {
		if (qc->tf.command == ATA_CMD_FPDMA_WRITE ||
		    qc->tf.command == ATA_CMD_FPDMA_READ ||
		    qc->tf.command == ATA_CMD_FPDMA_RECV ||
		    qc->tf.command == ATA_CMD_FPDMA_SEND ||
		    qc->tf.command == ATA_CMD_NCQ_NON_DATA) {
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
	struct mvs_device *mvi_dev = dev->lldd_dev;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_slot_info *slot;
	void *buf_prd;
	u32 tag = tei->tag, hdr_tag;
	u32 flags, del_q;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf;
	dma_addr_t buf_tmp_dma;
	u32 i, req_len, resp_len;
	const u32 max_resp_len = SB_RFB_MAX;

	if (mvs_assign_reg_set(mvi, mvi_dev) == MVS_ID_NOT_MAPPED) {
		mv_dprintk("Have not enough regiset for dev %d.\n",
			mvi_dev->device_id);
		return -EBUSY;
	}
	slot = &mvi->slot_info[tag];
	slot->tx = mvi->tx_prod;
	del_q = TXQ_MODE_I | tag |
		(TXQ_CMD_STP << TXQ_CMD_SHIFT) |
		((sas_port->phy_mask & TXQ_PHY_MASK) << TXQ_PHY_SHIFT) |
		(mvi_dev->taskfileset << TXQ_SRS_SHIFT);
	mvi->tx[mvi->tx_prod] = cpu_to_le32(del_q);

	if (task->data_dir == DMA_FROM_DEVICE)
		flags = (MVS_CHIP_DISP->prd_count() << MCH_PRD_LEN_SHIFT);
	else
		flags = (tei->n_elem << MCH_PRD_LEN_SHIFT);

	if (task->ata_task.use_ncq)
		flags |= MCH_FPDMA;
	if (dev->sata_dev.class == ATA_DEV_ATAPI) {
		if (task->ata_task.fis.command != ATA_CMD_ID_ATAPI)
			flags |= MCH_ATAPI;
	}

	hdr->flags = cpu_to_le32(flags);

	if (task->ata_task.use_ncq && mvs_get_ncq_tag(task, &hdr_tag))
		task->ata_task.fis.sector_count |= (u8) (hdr_tag << 3);
	else
		hdr_tag = tag;

	hdr->tags = cpu_to_le32(hdr_tag);

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
	i = MVS_CHIP_DISP->prd_size() * MVS_CHIP_DISP->prd_count();

	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);
	if (mvi->flags & MVF_FLAG_SOC)
		hdr->reserved[0] = 0;

	req_len = sizeof(struct host_to_dev_fis);
	resp_len = MVS_SLOT_BUF_SZ - MVS_ATA_CMD_SZ -
	    sizeof(struct mvs_err_info) - i;

	/* request, response lengths */
	resp_len = min(resp_len, max_resp_len);
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	if (likely(!task->ata_task.device_control_reg_update))
		task->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	/* fill in command FIS and ATAPI CDB */
	memcpy(buf_cmd, &task->ata_task.fis, sizeof(struct host_to_dev_fis));
	if (dev->sata_dev.class == ATA_DEV_ATAPI)
		memcpy(buf_cmd + STP_ATAPI_CMD,
			task->ata_task.atapi_packet, 16);

	/* generate open address frame hdr (first 12 bytes) */
	/* initiator, STP, ftype 1h */
	buf_oaf[0] = (1 << 7) | (PROTOCOL_STP << 4) | 0x1;
	buf_oaf[1] = min(sas_port->linkrate, dev->linkrate) & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(mvi_dev->device_id + 1);
	memcpy(buf_oaf + 4, dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in PRD (scatter/gather) table, if any */
	MVS_CHIP_DISP->make_prd(task->scatter, tei->n_elem, buf_prd);

	if (task->data_dir == DMA_FROM_DEVICE)
		MVS_CHIP_DISP->dma_fix(mvi, sas_port->phy_mask,
				TRASH_BUCKET_SIZE, tei->n_elem, buf_prd);

	return 0;
}

static int mvs_task_prep_ssp(struct mvs_info *mvi,
			     struct mvs_task_exec_info *tei, int is_tmf,
			     struct mvs_tmf_task *tmf)
{
	struct sas_task *task = tei->task;
	struct mvs_cmd_hdr *hdr = tei->hdr;
	struct mvs_port *port = tei->port;
	struct domain_device *dev = task->dev;
	struct mvs_device *mvi_dev = dev->lldd_dev;
	struct asd_sas_port *sas_port = dev->port;
	struct mvs_slot_info *slot;
	void *buf_prd;
	struct ssp_frame_hdr *ssp_hdr;
	void *buf_tmp;
	u8 *buf_cmd, *buf_oaf, fburst = 0;
	dma_addr_t buf_tmp_dma;
	u32 flags;
	u32 resp_len, req_len, i, tag = tei->tag;
	const u32 max_resp_len = SB_RFB_MAX;
	u32 phy_mask;

	slot = &mvi->slot_info[tag];

	phy_mask = ((port->wide_port_phymap) ? port->wide_port_phymap :
		sas_port->phy_mask) & TXQ_PHY_MASK;

	slot->tx = mvi->tx_prod;
	mvi->tx[mvi->tx_prod] = cpu_to_le32(TXQ_MODE_I | tag |
				(TXQ_CMD_SSP << TXQ_CMD_SHIFT) |
				(phy_mask << TXQ_PHY_SHIFT));

	flags = MCH_RETRY;
	if (task->ssp_task.enable_first_burst) {
		flags |= MCH_FBURST;
		fburst = (1 << 7);
	}
	if (is_tmf)
		flags |= (MCH_SSP_FR_TASK << MCH_SSP_FR_TYPE_SHIFT);
	else
		flags |= (MCH_SSP_FR_CMD << MCH_SSP_FR_TYPE_SHIFT);

	hdr->flags = cpu_to_le32(flags | (tei->n_elem << MCH_PRD_LEN_SHIFT));
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

	i = MVS_CHIP_DISP->prd_size() * tei->n_elem;
	buf_tmp += i;
	buf_tmp_dma += i;

	/* region 4: status buffer (larger the PRD, smaller this buf) ****** */
	slot->response = buf_tmp;
	hdr->status_buf = cpu_to_le64(buf_tmp_dma);
	if (mvi->flags & MVF_FLAG_SOC)
		hdr->reserved[0] = 0;

	resp_len = MVS_SLOT_BUF_SZ - MVS_SSP_CMD_SZ - MVS_OAF_SZ -
	    sizeof(struct mvs_err_info) - i;
	resp_len = min(resp_len, max_resp_len);

	req_len = sizeof(struct ssp_frame_hdr) + 28;

	/* request, response lengths */
	hdr->lens = cpu_to_le32(((resp_len / 4) << 16) | (req_len / 4));

	/* generate open address frame hdr (first 12 bytes) */
	/* initiator, SSP, ftype 1h */
	buf_oaf[0] = (1 << 7) | (PROTOCOL_SSP << 4) | 0x1;
	buf_oaf[1] = min(sas_port->linkrate, dev->linkrate) & 0xf;
	*(u16 *)(buf_oaf + 2) = cpu_to_be16(mvi_dev->device_id + 1);
	memcpy(buf_oaf + 4, dev->sas_addr, SAS_ADDR_SIZE);

	/* fill in SSP frame header (Command Table.SSP frame header) */
	ssp_hdr = (struct ssp_frame_hdr *)buf_cmd;

	if (is_tmf)
		ssp_hdr->frame_type = SSP_TASK;
	else
		ssp_hdr->frame_type = SSP_COMMAND;

	memcpy(ssp_hdr->hashed_dest_addr, dev->hashed_sas_addr,
	       HASHED_SAS_ADDR_SIZE);
	memcpy(ssp_hdr->hashed_src_addr,
	       dev->hashed_sas_addr, HASHED_SAS_ADDR_SIZE);
	ssp_hdr->tag = cpu_to_be16(tag);

	/* fill in IU for TASK and Command Frame */
	buf_cmd += sizeof(*ssp_hdr);
	memcpy(buf_cmd, &task->ssp_task.LUN, 8);

	if (ssp_hdr->frame_type != SSP_TASK) {
		buf_cmd[9] = fburst | task->ssp_task.task_attr |
				(task->ssp_task.task_prio << 3);
		memcpy(buf_cmd + 12, task->ssp_task.cmd->cmnd,
		       task->ssp_task.cmd->cmd_len);
	} else{
		buf_cmd[10] = tmf->tmf;
		switch (tmf->tmf) {
		case TMF_ABORT_TASK:
		case TMF_QUERY_TASK:
			buf_cmd[12] =
				(tmf->tag_of_task_to_be_managed >> 8) & 0xff;
			buf_cmd[13] =
				tmf->tag_of_task_to_be_managed & 0xff;
			break;
		default:
			break;
		}
	}
	/* fill in PRD (scatter/gather) table, if any */
	MVS_CHIP_DISP->make_prd(task->scatter, tei->n_elem, buf_prd);
	return 0;
}

#define	DEV_IS_GONE(mvi_dev)	((!mvi_dev || (mvi_dev->dev_type == SAS_PHY_UNUSED)))
static int mvs_task_prep(struct sas_task *task, struct mvs_info *mvi, int is_tmf,
				struct mvs_tmf_task *tmf, int *pass)
{
	struct domain_device *dev = task->dev;
	struct mvs_device *mvi_dev = dev->lldd_dev;
	struct mvs_task_exec_info tei;
	struct mvs_slot_info *slot;
	u32 tag = 0xdeadbeef, n_elem = 0;
	int rc = 0;

	if (!dev->port) {
		struct task_status_struct *tsm = &task->task_status;

		tsm->resp = SAS_TASK_UNDELIVERED;
		tsm->stat = SAS_PHY_DOWN;
		/*
		 * libsas will use dev->port, should
		 * not call task_done for sata
		 */
		if (dev->dev_type != SAS_SATA_DEV)
			task->task_done(task);
		return rc;
	}

	if (DEV_IS_GONE(mvi_dev)) {
		if (mvi_dev)
			mv_dprintk("device %d not ready.\n",
				mvi_dev->device_id);
		else
			mv_dprintk("device %016llx not ready.\n",
				SAS_ADDR(dev->sas_addr));

		rc = SAS_PHY_DOWN;
		return rc;
	}
	tei.port = dev->port->lldd_port;
	if (tei.port && !tei.port->port_attached && !tmf) {
		if (sas_protocol_ata(task->task_proto)) {
			struct task_status_struct *ts = &task->task_status;
			mv_dprintk("SATA/STP port %d does not attach"
					"device.\n", dev->port->id);
			ts->resp = SAS_TASK_COMPLETE;
			ts->stat = SAS_PHY_DOWN;

			task->task_done(task);

		} else {
			struct task_status_struct *ts = &task->task_status;
			mv_dprintk("SAS port %d does not attach"
				"device.\n", dev->port->id);
			ts->resp = SAS_TASK_UNDELIVERED;
			ts->stat = SAS_PHY_DOWN;
			task->task_done(task);
		}
		return rc;
	}

	if (!sas_protocol_ata(task->task_proto)) {
		if (task->num_scatter) {
			n_elem = dma_map_sg(mvi->dev,
					    task->scatter,
					    task->num_scatter,
					    task->data_dir);
			if (!n_elem) {
				rc = -ENOMEM;
				goto prep_out;
			}
		}
	} else {
		n_elem = task->num_scatter;
	}

	rc = mvs_tag_alloc(mvi, &tag);
	if (rc)
		goto err_out;

	slot = &mvi->slot_info[tag];

	task->lldd_task = NULL;
	slot->n_elem = n_elem;
	slot->slot_tag = tag;

	slot->buf = pci_pool_alloc(mvi->dma_pool, GFP_ATOMIC, &slot->buf_dma);
	if (!slot->buf) {
		rc = -ENOMEM;
		goto err_out_tag;
	}
	memset(slot->buf, 0, MVS_SLOT_BUF_SZ);

	tei.task = task;
	tei.hdr = &mvi->slot[tag];
	tei.tag = tag;
	tei.n_elem = n_elem;
	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		rc = mvs_task_prep_smp(mvi, &tei);
		break;
	case SAS_PROTOCOL_SSP:
		rc = mvs_task_prep_ssp(mvi, &tei, is_tmf, tmf);
		break;
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
		rc = mvs_task_prep_ata(mvi, &tei);
		break;
	default:
		dev_printk(KERN_ERR, mvi->dev,
			"unknown sas_task proto: 0x%x\n",
			task->task_proto);
		rc = -EINVAL;
		break;
	}

	if (rc) {
		mv_dprintk("rc is %x\n", rc);
		goto err_out_slot_buf;
	}
	slot->task = task;
	slot->port = tei.port;
	task->lldd_task = slot;
	list_add_tail(&slot->entry, &tei.port->list);
	spin_lock(&task->task_state_lock);
	task->task_state_flags |= SAS_TASK_AT_INITIATOR;
	spin_unlock(&task->task_state_lock);

	mvi_dev->running_req++;
	++(*pass);
	mvi->tx_prod = (mvi->tx_prod + 1) & (MVS_CHIP_SLOT_SZ - 1);

	return rc;

err_out_slot_buf:
	pci_pool_free(mvi->dma_pool, slot->buf, slot->buf_dma);
err_out_tag:
	mvs_tag_free(mvi, tag);
err_out:

	dev_printk(KERN_ERR, mvi->dev, "mvsas prep failed[%d]!\n", rc);
	if (!sas_protocol_ata(task->task_proto))
		if (n_elem)
			dma_unmap_sg(mvi->dev, task->scatter, n_elem,
				     task->data_dir);
prep_out:
	return rc;
}

static int mvs_task_exec(struct sas_task *task, gfp_t gfp_flags,
				struct completion *completion, int is_tmf,
				struct mvs_tmf_task *tmf)
{
	struct mvs_info *mvi = NULL;
	u32 rc = 0;
	u32 pass = 0;
	unsigned long flags = 0;

	mvi = ((struct mvs_device *)task->dev->lldd_dev)->mvi_info;

	spin_lock_irqsave(&mvi->lock, flags);
	rc = mvs_task_prep(task, mvi, is_tmf, tmf, &pass);
	if (rc)
		dev_printk(KERN_ERR, mvi->dev, "mvsas exec failed[%d]!\n", rc);

	if (likely(pass))
			MVS_CHIP_DISP->start_delivery(mvi, (mvi->tx_prod - 1) &
				(MVS_CHIP_SLOT_SZ - 1));
	spin_unlock_irqrestore(&mvi->lock, flags);

	return rc;
}

int mvs_queue_command(struct sas_task *task, gfp_t gfp_flags)
{
	return mvs_task_exec(task, gfp_flags, NULL, 0, NULL);
}

static void mvs_slot_free(struct mvs_info *mvi, u32 rx_desc)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	mvs_tag_clear(mvi, slot_idx);
}

static void mvs_slot_task_free(struct mvs_info *mvi, struct sas_task *task,
			  struct mvs_slot_info *slot, u32 slot_idx)
{
	if (!slot)
		return;
	if (!slot->task)
		return;
	if (!sas_protocol_ata(task->task_proto))
		if (slot->n_elem)
			dma_unmap_sg(mvi->dev, task->scatter,
				     slot->n_elem, task->data_dir);

	switch (task->task_proto) {
	case SAS_PROTOCOL_SMP:
		dma_unmap_sg(mvi->dev, &task->smp_task.smp_resp, 1,
			     PCI_DMA_FROMDEVICE);
		dma_unmap_sg(mvi->dev, &task->smp_task.smp_req, 1,
			     PCI_DMA_TODEVICE);
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SSP:
	default:
		/* do nothing */
		break;
	}

	if (slot->buf) {
		pci_pool_free(mvi->dma_pool, slot->buf, slot->buf_dma);
		slot->buf = NULL;
	}
	list_del_init(&slot->entry);
	task->lldd_task = NULL;
	slot->task = NULL;
	slot->port = NULL;
	slot->slot_tag = 0xFFFFFFFF;
	mvs_slot_free(mvi, slot_idx);
}

static void mvs_update_wideport(struct mvs_info *mvi, int phy_no)
{
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct mvs_port *port = phy->port;
	int j, no;

	for_each_phy(port->wide_port_phymap, j, no) {
		if (j & 1) {
			MVS_CHIP_DISP->write_port_cfg_addr(mvi, no,
						PHYR_WIDE_PORT);
			MVS_CHIP_DISP->write_port_cfg_data(mvi, no,
						port->wide_port_phymap);
		} else {
			MVS_CHIP_DISP->write_port_cfg_addr(mvi, no,
						PHYR_WIDE_PORT);
			MVS_CHIP_DISP->write_port_cfg_data(mvi, no,
						0);
		}
	}
}

static u32 mvs_is_phy_ready(struct mvs_info *mvi, int i)
{
	u32 tmp;
	struct mvs_phy *phy = &mvi->phy[i];
	struct mvs_port *port = phy->port;

	tmp = MVS_CHIP_DISP->read_phy_ctl(mvi, i);
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

	MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_SATA_SIG3);
	s[3] = cpu_to_le32(MVS_CHIP_DISP->read_port_cfg_data(mvi, i));

	MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_SATA_SIG2);
	s[2] = cpu_to_le32(MVS_CHIP_DISP->read_port_cfg_data(mvi, i));

	MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_SATA_SIG1);
	s[1] = cpu_to_le32(MVS_CHIP_DISP->read_port_cfg_data(mvi, i));

	MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_SATA_SIG0);
	s[0] = cpu_to_le32(MVS_CHIP_DISP->read_port_cfg_data(mvi, i));

	if (((s[1] & 0x00FFFFFF) == 0x00EB1401) && (*(u8 *)&s[3] == 0x01))
		s[1] = 0x00EB1401 | (*((u8 *)&s[1] + 3) & 0x10);

	return s;
}

static u32 mvs_is_sig_fis_received(u32 irq_status)
{
	return irq_status & PHYEV_SIG_FIS;
}

static void mvs_sig_remove_timer(struct mvs_phy *phy)
{
	if (phy->timer.function)
		del_timer(&phy->timer);
	phy->timer.function = NULL;
}

void mvs_update_phyinfo(struct mvs_info *mvi, int i, int get_st)
{
	struct mvs_phy *phy = &mvi->phy[i];
	struct sas_identify_frame *id;

	id = (struct sas_identify_frame *)phy->frame_rcvd;

	if (get_st) {
		phy->irq_status = MVS_CHIP_DISP->read_port_irq_stat(mvi, i);
		phy->phy_status = mvs_is_phy_ready(mvi, i);
	}

	if (phy->phy_status) {
		int oob_done = 0;
		struct asd_sas_phy *sas_phy = &mvi->phy[i].sas_phy;

		oob_done = MVS_CHIP_DISP->oob_done(mvi, i);

		MVS_CHIP_DISP->fix_phy_info(mvi, i, id);
		if (phy->phy_type & PORT_TYPE_SATA) {
			phy->identify.target_port_protocols = SAS_PROTOCOL_STP;
			if (mvs_is_sig_fis_received(phy->irq_status)) {
				mvs_sig_remove_timer(phy);
				phy->phy_attached = 1;
				phy->att_dev_sas_addr =
					i + mvi->id * mvi->chip->n_phy;
				if (oob_done)
					sas_phy->oob_mode = SATA_OOB_MODE;
				phy->frame_rcvd_size =
				    sizeof(struct dev_to_host_fis);
				mvs_get_d2h_reg(mvi, i, id);
			} else {
				u32 tmp;
				dev_printk(KERN_DEBUG, mvi->dev,
					"Phy%d : No sig fis\n", i);
				tmp = MVS_CHIP_DISP->read_port_irq_mask(mvi, i);
				MVS_CHIP_DISP->write_port_irq_mask(mvi, i,
						tmp | PHYEV_SIG_FIS);
				phy->phy_attached = 0;
				phy->phy_type &= ~PORT_TYPE_SATA;
				goto out_done;
			}
		}	else if (phy->phy_type & PORT_TYPE_SAS
			|| phy->att_dev_info & PORT_SSP_INIT_MASK) {
			phy->phy_attached = 1;
			phy->identify.device_type =
				phy->att_dev_info & PORT_DEV_TYPE_MASK;

			if (phy->identify.device_type == SAS_END_DEVICE)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SSP;
			else if (phy->identify.device_type != SAS_PHY_UNUSED)
				phy->identify.target_port_protocols =
							SAS_PROTOCOL_SMP;
			if (oob_done)
				sas_phy->oob_mode = SAS_OOB_MODE;
			phy->frame_rcvd_size =
			    sizeof(struct sas_identify_frame);
		}
		memcpy(sas_phy->attached_sas_addr,
			&phy->att_dev_sas_addr, SAS_ADDR_SIZE);

		if (MVS_CHIP_DISP->phy_work_around)
			MVS_CHIP_DISP->phy_work_around(mvi, i);
	}
	mv_dprintk("phy %d attach dev info is %x\n",
		i + mvi->id * mvi->chip->n_phy, phy->att_dev_info);
	mv_dprintk("phy %d attach sas addr is %llx\n",
		i + mvi->id * mvi->chip->n_phy, phy->att_dev_sas_addr);
out_done:
	if (get_st)
		MVS_CHIP_DISP->write_port_irq_stat(mvi, i, phy->irq_status);
}

static void mvs_port_notify_formed(struct asd_sas_phy *sas_phy, int lock)
{
	struct sas_ha_struct *sas_ha = sas_phy->ha;
	struct mvs_info *mvi = NULL; int i = 0, hi;
	struct mvs_phy *phy = sas_phy->lldd_phy;
	struct asd_sas_port *sas_port = sas_phy->port;
	struct mvs_port *port;
	unsigned long flags = 0;
	if (!sas_port)
		return;

	while (sas_ha->sas_phy[i]) {
		if (sas_ha->sas_phy[i] == sas_phy)
			break;
		i++;
	}
	hi = i/((struct mvs_prv_info *)sas_ha->lldd_ha)->n_phy;
	mvi = ((struct mvs_prv_info *)sas_ha->lldd_ha)->mvi[hi];
	if (i >= mvi->chip->n_phy)
		port = &mvi->port[i - mvi->chip->n_phy];
	else
		port = &mvi->port[i];
	if (lock)
		spin_lock_irqsave(&mvi->lock, flags);
	port->port_attached = 1;
	phy->port = port;
	sas_port->lldd_port = port;
	if (phy->phy_type & PORT_TYPE_SAS) {
		port->wide_port_phymap = sas_port->phy_mask;
		mv_printk("set wide port phy map %x\n", sas_port->phy_mask);
		mvs_update_wideport(mvi, sas_phy->id);

		/* direct attached SAS device */
		if (phy->att_dev_info & PORT_SSP_TRGT_MASK) {
			MVS_CHIP_DISP->write_port_cfg_addr(mvi, i, PHYR_PHY_STAT);
			MVS_CHIP_DISP->write_port_cfg_data(mvi, i, 0x04);
		}
	}
	if (lock)
		spin_unlock_irqrestore(&mvi->lock, flags);
}

static void mvs_port_notify_deformed(struct asd_sas_phy *sas_phy, int lock)
{
	struct domain_device *dev;
	struct mvs_phy *phy = sas_phy->lldd_phy;
	struct mvs_info *mvi = phy->mvi;
	struct asd_sas_port *port = sas_phy->port;
	int phy_no = 0;

	while (phy != &mvi->phy[phy_no]) {
		phy_no++;
		if (phy_no >= MVS_MAX_PHYS)
			return;
	}
	list_for_each_entry(dev, &port->dev_list, dev_list_node)
		mvs_do_release_task(phy->mvi, phy_no, dev);

}


void mvs_port_formed(struct asd_sas_phy *sas_phy)
{
	mvs_port_notify_formed(sas_phy, 1);
}

void mvs_port_deformed(struct asd_sas_phy *sas_phy)
{
	mvs_port_notify_deformed(sas_phy, 1);
}

static struct mvs_device *mvs_alloc_dev(struct mvs_info *mvi)
{
	u32 dev;
	for (dev = 0; dev < MVS_MAX_DEVICES; dev++) {
		if (mvi->devices[dev].dev_type == SAS_PHY_UNUSED) {
			mvi->devices[dev].device_id = dev;
			return &mvi->devices[dev];
		}
	}

	if (dev == MVS_MAX_DEVICES)
		mv_printk("max support %d devices, ignore ..\n",
			MVS_MAX_DEVICES);

	return NULL;
}

static void mvs_free_dev(struct mvs_device *mvi_dev)
{
	u32 id = mvi_dev->device_id;
	memset(mvi_dev, 0, sizeof(*mvi_dev));
	mvi_dev->device_id = id;
	mvi_dev->dev_type = SAS_PHY_UNUSED;
	mvi_dev->dev_status = MVS_DEV_NORMAL;
	mvi_dev->taskfileset = MVS_ID_NOT_MAPPED;
}

static int mvs_dev_found_notify(struct domain_device *dev, int lock)
{
	unsigned long flags = 0;
	int res = 0;
	struct mvs_info *mvi = NULL;
	struct domain_device *parent_dev = dev->parent;
	struct mvs_device *mvi_device;

	mvi = mvs_find_dev_mvi(dev);

	if (lock)
		spin_lock_irqsave(&mvi->lock, flags);

	mvi_device = mvs_alloc_dev(mvi);
	if (!mvi_device) {
		res = -1;
		goto found_out;
	}
	dev->lldd_dev = mvi_device;
	mvi_device->dev_status = MVS_DEV_NORMAL;
	mvi_device->dev_type = dev->dev_type;
	mvi_device->mvi_info = mvi;
	mvi_device->sas_device = dev;
	if (parent_dev && DEV_IS_EXPANDER(parent_dev->dev_type)) {
		int phy_id;
		u8 phy_num = parent_dev->ex_dev.num_phys;
		struct ex_phy *phy;
		for (phy_id = 0; phy_id < phy_num; phy_id++) {
			phy = &parent_dev->ex_dev.ex_phy[phy_id];
			if (SAS_ADDR(phy->attached_sas_addr) ==
				SAS_ADDR(dev->sas_addr)) {
				mvi_device->attached_phy = phy_id;
				break;
			}
		}

		if (phy_id == phy_num) {
			mv_printk("Error: no attached dev:%016llx"
				"at ex:%016llx.\n",
				SAS_ADDR(dev->sas_addr),
				SAS_ADDR(parent_dev->sas_addr));
			res = -1;
		}
	}

found_out:
	if (lock)
		spin_unlock_irqrestore(&mvi->lock, flags);
	return res;
}

int mvs_dev_found(struct domain_device *dev)
{
	return mvs_dev_found_notify(dev, 1);
}

static void mvs_dev_gone_notify(struct domain_device *dev)
{
	unsigned long flags = 0;
	struct mvs_device *mvi_dev = dev->lldd_dev;
	struct mvs_info *mvi;

	if (!mvi_dev) {
		mv_dprintk("found dev has gone.\n");
		return;
	}

	mvi = mvi_dev->mvi_info;

	spin_lock_irqsave(&mvi->lock, flags);

	mv_dprintk("found dev[%d:%x] is gone.\n",
		mvi_dev->device_id, mvi_dev->dev_type);
	mvs_release_task(mvi, dev);
	mvs_free_reg_set(mvi, mvi_dev);
	mvs_free_dev(mvi_dev);

	dev->lldd_dev = NULL;
	mvi_dev->sas_device = NULL;

	spin_unlock_irqrestore(&mvi->lock, flags);
}


void mvs_dev_gone(struct domain_device *dev)
{
	mvs_dev_gone_notify(dev);
}

static void mvs_task_done(struct sas_task *task)
{
	if (!del_timer(&task->slow_task->timer))
		return;
	complete(&task->slow_task->completion);
}

static void mvs_tmf_timedout(unsigned long data)
{
	struct sas_task *task = (struct sas_task *)data;

	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	complete(&task->slow_task->completion);
}

#define MVS_TASK_TIMEOUT 20
static int mvs_exec_internal_tmf_task(struct domain_device *dev,
			void *parameter, u32 para_len, struct mvs_tmf_task *tmf)
{
	int res, retry;
	struct sas_task *task = NULL;

	for (retry = 0; retry < 3; retry++) {
		task = sas_alloc_slow_task(GFP_KERNEL);
		if (!task)
			return -ENOMEM;

		task->dev = dev;
		task->task_proto = dev->tproto;

		memcpy(&task->ssp_task, parameter, para_len);
		task->task_done = mvs_task_done;

		task->slow_task->timer.data = (unsigned long) task;
		task->slow_task->timer.function = mvs_tmf_timedout;
		task->slow_task->timer.expires = jiffies + MVS_TASK_TIMEOUT*HZ;
		add_timer(&task->slow_task->timer);

		res = mvs_task_exec(task, GFP_KERNEL, NULL, 1, tmf);

		if (res) {
			del_timer(&task->slow_task->timer);
			mv_printk("executing internal task failed:%d\n", res);
			goto ex_err;
		}

		wait_for_completion(&task->slow_task->completion);
		res = TMF_RESP_FUNC_FAILED;
		/* Even TMF timed out, return direct. */
		if ((task->task_state_flags & SAS_TASK_STATE_ABORTED)) {
			if (!(task->task_state_flags & SAS_TASK_STATE_DONE)) {
				mv_printk("TMF task[%x] timeout.\n", tmf->tmf);
				goto ex_err;
			}
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		    task->task_status.stat == SAM_STAT_GOOD) {
			res = TMF_RESP_FUNC_COMPLETE;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		      task->task_status.stat == SAS_DATA_UNDERRUN) {
			/* no error, but return the number of bytes of
			 * underrun */
			res = task->task_status.residual;
			break;
		}

		if (task->task_status.resp == SAS_TASK_COMPLETE &&
		      task->task_status.stat == SAS_DATA_OVERRUN) {
			mv_dprintk("blocked task error.\n");
			res = -EMSGSIZE;
			break;
		} else {
			mv_dprintk(" task to dev %016llx response: 0x%x "
				    "status 0x%x\n",
				    SAS_ADDR(dev->sas_addr),
				    task->task_status.resp,
				    task->task_status.stat);
			sas_free_task(task);
			task = NULL;

		}
	}
ex_err:
	BUG_ON(retry == 3 && task != NULL);
	sas_free_task(task);
	return res;
}

static int mvs_debug_issue_ssp_tmf(struct domain_device *dev,
				u8 *lun, struct mvs_tmf_task *tmf)
{
	struct sas_ssp_task ssp_task;
	if (!(dev->tproto & SAS_PROTOCOL_SSP))
		return TMF_RESP_FUNC_ESUPP;

	memcpy(ssp_task.LUN, lun, 8);

	return mvs_exec_internal_tmf_task(dev, &ssp_task,
				sizeof(ssp_task), tmf);
}


/*  Standard mandates link reset for ATA  (type 0)
    and hard reset for SSP (type 1) , only for RECOVERY */
static int mvs_debug_I_T_nexus_reset(struct domain_device *dev)
{
	int rc;
	struct sas_phy *phy = sas_get_local_phy(dev);
	int reset_type = (dev->dev_type == SAS_SATA_DEV ||
			(dev->tproto & SAS_PROTOCOL_STP)) ? 0 : 1;
	rc = sas_phy_reset(phy, reset_type);
	sas_put_local_phy(phy);
	msleep(2000);
	return rc;
}

/* mandatory SAM-3 */
int mvs_lu_reset(struct domain_device *dev, u8 *lun)
{
	unsigned long flags;
	int rc = TMF_RESP_FUNC_FAILED;
	struct mvs_tmf_task tmf_task;
	struct mvs_device * mvi_dev = dev->lldd_dev;
	struct mvs_info *mvi = mvi_dev->mvi_info;

	tmf_task.tmf = TMF_LU_RESET;
	mvi_dev->dev_status = MVS_DEV_EH;
	rc = mvs_debug_issue_ssp_tmf(dev, lun, &tmf_task);
	if (rc == TMF_RESP_FUNC_COMPLETE) {
		spin_lock_irqsave(&mvi->lock, flags);
		mvs_release_task(mvi, dev);
		spin_unlock_irqrestore(&mvi->lock, flags);
	}
	/* If failed, fall-through I_T_Nexus reset */
	mv_printk("%s for device[%x]:rc= %d\n", __func__,
			mvi_dev->device_id, rc);
	return rc;
}

int mvs_I_T_nexus_reset(struct domain_device *dev)
{
	unsigned long flags;
	int rc = TMF_RESP_FUNC_FAILED;
    struct mvs_device * mvi_dev = (struct mvs_device *)dev->lldd_dev;
	struct mvs_info *mvi = mvi_dev->mvi_info;

	if (mvi_dev->dev_status != MVS_DEV_EH)
		return TMF_RESP_FUNC_COMPLETE;
	else
		mvi_dev->dev_status = MVS_DEV_NORMAL;
	rc = mvs_debug_I_T_nexus_reset(dev);
	mv_printk("%s for device[%x]:rc= %d\n",
		__func__, mvi_dev->device_id, rc);

	spin_lock_irqsave(&mvi->lock, flags);
	mvs_release_task(mvi, dev);
	spin_unlock_irqrestore(&mvi->lock, flags);

	return rc;
}
/* optional SAM-3 */
int mvs_query_task(struct sas_task *task)
{
	u32 tag;
	struct scsi_lun lun;
	struct mvs_tmf_task tmf_task;
	int rc = TMF_RESP_FUNC_FAILED;

	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd * cmnd = (struct scsi_cmnd *)task->uldd_task;
		struct domain_device *dev = task->dev;
		struct mvs_device *mvi_dev = (struct mvs_device *)dev->lldd_dev;
		struct mvs_info *mvi = mvi_dev->mvi_info;

		int_to_scsilun(cmnd->device->lun, &lun);
		rc = mvs_find_tag(mvi, task, &tag);
		if (rc == 0) {
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}

		tmf_task.tmf = TMF_QUERY_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		rc = mvs_debug_issue_ssp_tmf(dev, lun.scsi_lun, &tmf_task);
		switch (rc) {
		/* The task is still in Lun, release it then */
		case TMF_RESP_FUNC_SUCC:
		/* The task is not in Lun or failed, reset the phy */
		case TMF_RESP_FUNC_FAILED:
		case TMF_RESP_FUNC_COMPLETE:
			break;
		}
	}
	mv_printk("%s:rc= %d\n", __func__, rc);
	return rc;
}

/*  mandatory SAM-3, still need free task/slot info */
int mvs_abort_task(struct sas_task *task)
{
	struct scsi_lun lun;
	struct mvs_tmf_task tmf_task;
	struct domain_device *dev = task->dev;
	struct mvs_device *mvi_dev = (struct mvs_device *)dev->lldd_dev;
	struct mvs_info *mvi;
	int rc = TMF_RESP_FUNC_FAILED;
	unsigned long flags;
	u32 tag;

	if (!mvi_dev) {
		mv_printk("Device has removed\n");
		return TMF_RESP_FUNC_FAILED;
	}

	mvi = mvi_dev->mvi_info;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		rc = TMF_RESP_FUNC_COMPLETE;
		goto out;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);
	mvi_dev->dev_status = MVS_DEV_EH;
	if (task->lldd_task && task->task_proto & SAS_PROTOCOL_SSP) {
		struct scsi_cmnd * cmnd = (struct scsi_cmnd *)task->uldd_task;

		int_to_scsilun(cmnd->device->lun, &lun);
		rc = mvs_find_tag(mvi, task, &tag);
		if (rc == 0) {
			mv_printk("No such tag in %s\n", __func__);
			rc = TMF_RESP_FUNC_FAILED;
			return rc;
		}

		tmf_task.tmf = TMF_ABORT_TASK;
		tmf_task.tag_of_task_to_be_managed = cpu_to_le16(tag);

		rc = mvs_debug_issue_ssp_tmf(dev, lun.scsi_lun, &tmf_task);

		/* if successful, clear the task and callback forwards.*/
		if (rc == TMF_RESP_FUNC_COMPLETE) {
			u32 slot_no;
			struct mvs_slot_info *slot;

			if (task->lldd_task) {
				slot = task->lldd_task;
				slot_no = (u32) (slot - mvi->slot_info);
				spin_lock_irqsave(&mvi->lock, flags);
				mvs_slot_complete(mvi, slot_no, 1);
				spin_unlock_irqrestore(&mvi->lock, flags);
			}
		}

	} else if (task->task_proto & SAS_PROTOCOL_SATA ||
		task->task_proto & SAS_PROTOCOL_STP) {
		if (SAS_SATA_DEV == dev->dev_type) {
			struct mvs_slot_info *slot = task->lldd_task;
			u32 slot_idx = (u32)(slot - mvi->slot_info);
			mv_dprintk("mvs_abort_task() mvi=%p task=%p "
				   "slot=%p slot_idx=x%x\n",
				   mvi, task, slot, slot_idx);
			task->task_state_flags |= SAS_TASK_STATE_ABORTED;
			mvs_slot_task_free(mvi, task, slot, slot_idx);
			rc = TMF_RESP_FUNC_COMPLETE;
			goto out;
		}

	}
out:
	if (rc != TMF_RESP_FUNC_COMPLETE)
		mv_printk("%s:rc= %d\n", __func__, rc);
	return rc;
}

int mvs_abort_task_set(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct mvs_tmf_task tmf_task;

	tmf_task.tmf = TMF_ABORT_TASK_SET;
	rc = mvs_debug_issue_ssp_tmf(dev, lun, &tmf_task);

	return rc;
}

int mvs_clear_aca(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct mvs_tmf_task tmf_task;

	tmf_task.tmf = TMF_CLEAR_ACA;
	rc = mvs_debug_issue_ssp_tmf(dev, lun, &tmf_task);

	return rc;
}

int mvs_clear_task_set(struct domain_device *dev, u8 *lun)
{
	int rc = TMF_RESP_FUNC_FAILED;
	struct mvs_tmf_task tmf_task;

	tmf_task.tmf = TMF_CLEAR_TASK_SET;
	rc = mvs_debug_issue_ssp_tmf(dev, lun, &tmf_task);

	return rc;
}

static int mvs_sata_done(struct mvs_info *mvi, struct sas_task *task,
			u32 slot_idx, int err)
{
	struct mvs_device *mvi_dev = task->dev->lldd_dev;
	struct task_status_struct *tstat = &task->task_status;
	struct ata_task_resp *resp = (struct ata_task_resp *)tstat->buf;
	int stat = SAM_STAT_GOOD;


	resp->frame_len = sizeof(struct dev_to_host_fis);
	memcpy(&resp->ending_fis[0],
	       SATA_RECEIVED_D2H_FIS(mvi_dev->taskfileset),
	       sizeof(struct dev_to_host_fis));
	tstat->buf_valid_size = sizeof(*resp);
	if (unlikely(err)) {
		if (unlikely(err & CMD_ISS_STPD))
			stat = SAS_OPEN_REJECT;
		else
			stat = SAS_PROTO_RESPONSE;
       }

	return stat;
}

static void mvs_set_sense(u8 *buffer, int len, int d_sense,
		int key, int asc, int ascq)
{
	memset(buffer, 0, len);

	if (d_sense) {
		/* Descriptor format */
		if (len < 4) {
			mv_printk("Length %d of sense buffer too small to "
				"fit sense %x:%x:%x", len, key, asc, ascq);
		}

		buffer[0] = 0x72;		/* Response Code	*/
		if (len > 1)
			buffer[1] = key;	/* Sense Key */
		if (len > 2)
			buffer[2] = asc;	/* ASC	*/
		if (len > 3)
			buffer[3] = ascq;	/* ASCQ	*/
	} else {
		if (len < 14) {
			mv_printk("Length %d of sense buffer too small to "
				"fit sense %x:%x:%x", len, key, asc, ascq);
		}

		buffer[0] = 0x70;		/* Response Code	*/
		if (len > 2)
			buffer[2] = key;	/* Sense Key */
		if (len > 7)
			buffer[7] = 0x0a;	/* Additional Sense Length */
		if (len > 12)
			buffer[12] = asc;	/* ASC */
		if (len > 13)
			buffer[13] = ascq; /* ASCQ */
	}

	return;
}

static void mvs_fill_ssp_resp_iu(struct ssp_response_iu *iu,
				u8 key, u8 asc, u8 asc_q)
{
	iu->datapres = 2;
	iu->response_data_len = 0;
	iu->sense_data_len = 17;
	iu->status = 02;
	mvs_set_sense(iu->sense_data, 17, 0,
			key, asc, asc_q);
}

static int mvs_slot_err(struct mvs_info *mvi, struct sas_task *task,
			 u32 slot_idx)
{
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	int stat;
	u32 err_dw0 = le32_to_cpu(*(u32 *)slot->response);
	u32 err_dw1 = le32_to_cpu(*((u32 *)slot->response + 1));
	u32 tfs = 0;
	enum mvs_port_type type = PORT_TYPE_SAS;

	if (err_dw0 & CMD_ISS_STPD)
		MVS_CHIP_DISP->issue_stop(mvi, type, tfs);

	MVS_CHIP_DISP->command_active(mvi, slot_idx);

	stat = SAM_STAT_CHECK_CONDITION;
	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
	{
		stat = SAS_ABORTED_TASK;
		if ((err_dw0 & NO_DEST) || err_dw1 & bit(31)) {
			struct ssp_response_iu *iu = slot->response +
				sizeof(struct mvs_err_info);
			mvs_fill_ssp_resp_iu(iu, NOT_READY, 0x04, 01);
			sas_ssp_task_response(mvi->dev, task, iu);
			stat = SAM_STAT_CHECK_CONDITION;
		}
		if (err_dw1 & bit(31))
			mv_printk("reuse same slot, retry command.\n");
		break;
	}
	case SAS_PROTOCOL_SMP:
		stat = SAM_STAT_CHECK_CONDITION;
		break;

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP:
	{
		task->ata_task.use_ncq = 0;
		stat = SAS_PROTO_RESPONSE;
		mvs_sata_done(mvi, task, slot_idx, err_dw0);
	}
		break;
	default:
		break;
	}

	return stat;
}

int mvs_slot_complete(struct mvs_info *mvi, u32 rx_desc, u32 flags)
{
	u32 slot_idx = rx_desc & RXQ_SLOT_MASK;
	struct mvs_slot_info *slot = &mvi->slot_info[slot_idx];
	struct sas_task *task = slot->task;
	struct mvs_device *mvi_dev = NULL;
	struct task_status_struct *tstat;
	struct domain_device *dev;
	u32 aborted;

	void *to;
	enum exec_status sts;

	if (unlikely(!task || !task->lldd_task || !task->dev))
		return -1;

	tstat = &task->task_status;
	dev = task->dev;
	mvi_dev = dev->lldd_dev;

	spin_lock(&task->task_state_lock);
	task->task_state_flags &=
		~(SAS_TASK_STATE_PENDING | SAS_TASK_AT_INITIATOR);
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	/* race condition*/
	aborted = task->task_state_flags & SAS_TASK_STATE_ABORTED;
	spin_unlock(&task->task_state_lock);

	memset(tstat, 0, sizeof(*tstat));
	tstat->resp = SAS_TASK_COMPLETE;

	if (unlikely(aborted)) {
		tstat->stat = SAS_ABORTED_TASK;
		if (mvi_dev && mvi_dev->running_req)
			mvi_dev->running_req--;
		if (sas_protocol_ata(task->task_proto))
			mvs_free_reg_set(mvi, mvi_dev);

		mvs_slot_task_free(mvi, task, slot, slot_idx);
		return -1;
	}

	/* when no device attaching, go ahead and complete by error handling*/
	if (unlikely(!mvi_dev || flags)) {
		if (!mvi_dev)
			mv_dprintk("port has not device.\n");
		tstat->stat = SAS_PHY_DOWN;
		goto out;
	}

	/*
	 * error info record present; slot->response is 32 bit aligned but may
	 * not be 64 bit aligned, so check for zero in two 32 bit reads
	 */
	if (unlikely((rx_desc & RXQ_ERR)
		     && (*((u32 *)slot->response)
			 || *(((u32 *)slot->response) + 1)))) {
		mv_dprintk("port %d slot %d rx_desc %X has error info"
			"%016llX.\n", slot->port->sas_port.id, slot_idx,
			 rx_desc, get_unaligned_le64(slot->response));
		tstat->stat = mvs_slot_err(mvi, task, slot_idx);
		tstat->resp = SAS_TASK_COMPLETE;
		goto out;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SSP:
		/* hw says status == 0, datapres == 0 */
		if (rx_desc & RXQ_GOOD) {
			tstat->stat = SAM_STAT_GOOD;
			tstat->resp = SAS_TASK_COMPLETE;
		}
		/* response frame present */
		else if (rx_desc & RXQ_RSP) {
			struct ssp_response_iu *iu = slot->response +
						sizeof(struct mvs_err_info);
			sas_ssp_task_response(mvi->dev, task, iu);
		} else
			tstat->stat = SAM_STAT_CHECK_CONDITION;
		break;

	case SAS_PROTOCOL_SMP: {
			struct scatterlist *sg_resp = &task->smp_task.smp_resp;
			tstat->stat = SAM_STAT_GOOD;
			to = kmap_atomic(sg_page(sg_resp));
			memcpy(to + sg_resp->offset,
				slot->response + sizeof(struct mvs_err_info),
				sg_dma_len(sg_resp));
			kunmap_atomic(to);
			break;
		}

	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
	case SAS_PROTOCOL_SATA | SAS_PROTOCOL_STP: {
			tstat->stat = mvs_sata_done(mvi, task, slot_idx, 0);
			break;
		}

	default:
		tstat->stat = SAM_STAT_CHECK_CONDITION;
		break;
	}
	if (!slot->port->port_attached) {
		mv_dprintk("port %d has removed.\n", slot->port->sas_port.id);
		tstat->stat = SAS_PHY_DOWN;
	}


out:
	if (mvi_dev && mvi_dev->running_req) {
		mvi_dev->running_req--;
		if (sas_protocol_ata(task->task_proto) && !mvi_dev->running_req)
			mvs_free_reg_set(mvi, mvi_dev);
	}
	mvs_slot_task_free(mvi, task, slot, slot_idx);
	sts = tstat->stat;

	spin_unlock(&mvi->lock);
	if (task->task_done)
		task->task_done(task);

	spin_lock(&mvi->lock);

	return sts;
}

void mvs_do_release_task(struct mvs_info *mvi,
		int phy_no, struct domain_device *dev)
{
	u32 slot_idx;
	struct mvs_phy *phy;
	struct mvs_port *port;
	struct mvs_slot_info *slot, *slot2;

	phy = &mvi->phy[phy_no];
	port = phy->port;
	if (!port)
		return;
	/* clean cmpl queue in case request is already finished */
	mvs_int_rx(mvi, false);



	list_for_each_entry_safe(slot, slot2, &port->list, entry) {
		struct sas_task *task;
		slot_idx = (u32) (slot - mvi->slot_info);
		task = slot->task;

		if (dev && task->dev != dev)
			continue;

		mv_printk("Release slot [%x] tag[%x], task [%p]:\n",
			slot_idx, slot->slot_tag, task);
		MVS_CHIP_DISP->command_active(mvi, slot_idx);

		mvs_slot_complete(mvi, slot_idx, 1);
	}
}

void mvs_release_task(struct mvs_info *mvi,
		      struct domain_device *dev)
{
	int i, phyno[WIDE_PORT_MAX_PHY], num;
	num = mvs_find_dev_phyno(dev, phyno);
	for (i = 0; i < num; i++)
		mvs_do_release_task(mvi, phyno[i], dev);
}

static void mvs_phy_disconnected(struct mvs_phy *phy)
{
	phy->phy_attached = 0;
	phy->att_dev_info = 0;
	phy->att_dev_sas_addr = 0;
}

static void mvs_work_queue(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct mvs_wq *mwq = container_of(dw, struct mvs_wq, work_q);
	struct mvs_info *mvi = mwq->mvi;
	unsigned long flags;
	u32 phy_no = (unsigned long) mwq->data;
	struct sas_ha_struct *sas_ha = mvi->sas;
	struct mvs_phy *phy = &mvi->phy[phy_no];
	struct asd_sas_phy *sas_phy = &phy->sas_phy;

	spin_lock_irqsave(&mvi->lock, flags);
	if (mwq->handler & PHY_PLUG_EVENT) {

		if (phy->phy_event & PHY_PLUG_OUT) {
			u32 tmp;
			struct sas_identify_frame *id;
			id = (struct sas_identify_frame *)phy->frame_rcvd;
			tmp = MVS_CHIP_DISP->read_phy_ctl(mvi, phy_no);
			phy->phy_event &= ~PHY_PLUG_OUT;
			if (!(tmp & PHY_READY_MASK)) {
				sas_phy_disconnected(sas_phy);
				mvs_phy_disconnected(phy);
				sas_ha->notify_phy_event(sas_phy,
					PHYE_LOSS_OF_SIGNAL);
				mv_dprintk("phy%d Removed Device\n", phy_no);
			} else {
				MVS_CHIP_DISP->detect_porttype(mvi, phy_no);
				mvs_update_phyinfo(mvi, phy_no, 1);
				mvs_bytes_dmaed(mvi, phy_no);
				mvs_port_notify_formed(sas_phy, 0);
				mv_dprintk("phy%d Attached Device\n", phy_no);
			}
		}
	} else if (mwq->handler & EXP_BRCT_CHG) {
		phy->phy_event &= ~EXP_BRCT_CHG;
		sas_ha->notify_port_event(sas_phy,
				PORTE_BROADCAST_RCVD);
		mv_dprintk("phy%d Got Broadcast Change\n", phy_no);
	}
	list_del(&mwq->entry);
	spin_unlock_irqrestore(&mvi->lock, flags);
	kfree(mwq);
}

static int mvs_handle_event(struct mvs_info *mvi, void *data, int handler)
{
	struct mvs_wq *mwq;
	int ret = 0;

	mwq = kmalloc(sizeof(struct mvs_wq), GFP_ATOMIC);
	if (mwq) {
		mwq->mvi = mvi;
		mwq->data = data;
		mwq->handler = handler;
		MV_INIT_DELAYED_WORK(&mwq->work_q, mvs_work_queue, mwq);
		list_add_tail(&mwq->entry, &mvi->wq_list);
		schedule_delayed_work(&mwq->work_q, HZ * 2);
	} else
		ret = -ENOMEM;

	return ret;
}

static void mvs_sig_time_out(unsigned long tphy)
{
	struct mvs_phy *phy = (struct mvs_phy *)tphy;
	struct mvs_info *mvi = phy->mvi;
	u8 phy_no;

	for (phy_no = 0; phy_no < mvi->chip->n_phy; phy_no++) {
		if (&mvi->phy[phy_no] == phy) {
			mv_dprintk("Get signature time out, reset phy %d\n",
				phy_no+mvi->id*mvi->chip->n_phy);
			MVS_CHIP_DISP->phy_reset(mvi, phy_no, MVS_HARD_RESET);
		}
	}
}

void mvs_int_port(struct mvs_info *mvi, int phy_no, u32 events)
{
	u32 tmp;
	struct mvs_phy *phy = &mvi->phy[phy_no];

	phy->irq_status = MVS_CHIP_DISP->read_port_irq_stat(mvi, phy_no);
	MVS_CHIP_DISP->write_port_irq_stat(mvi, phy_no, phy->irq_status);
	mv_dprintk("phy %d ctrl sts=0x%08X.\n", phy_no+mvi->id*mvi->chip->n_phy,
		MVS_CHIP_DISP->read_phy_ctl(mvi, phy_no));
	mv_dprintk("phy %d irq sts = 0x%08X\n", phy_no+mvi->id*mvi->chip->n_phy,
		phy->irq_status);

	/*
	* events is port event now ,
	* we need check the interrupt status which belongs to per port.
	*/

	if (phy->irq_status & PHYEV_DCDR_ERR) {
		mv_dprintk("phy %d STP decoding error.\n",
		phy_no + mvi->id*mvi->chip->n_phy);
	}

	if (phy->irq_status & PHYEV_POOF) {
		mdelay(500);
		if (!(phy->phy_event & PHY_PLUG_OUT)) {
			int dev_sata = phy->phy_type & PORT_TYPE_SATA;
			int ready;
			mvs_do_release_task(mvi, phy_no, NULL);
			phy->phy_event |= PHY_PLUG_OUT;
			MVS_CHIP_DISP->clear_srs_irq(mvi, 0, 1);
			mvs_handle_event(mvi,
				(void *)(unsigned long)phy_no,
				PHY_PLUG_EVENT);
			ready = mvs_is_phy_ready(mvi, phy_no);
			if (ready || dev_sata) {
				if (MVS_CHIP_DISP->stp_reset)
					MVS_CHIP_DISP->stp_reset(mvi,
							phy_no);
				else
					MVS_CHIP_DISP->phy_reset(mvi,
							phy_no, MVS_SOFT_RESET);
				return;
			}
		}
	}

	if (phy->irq_status & PHYEV_COMWAKE) {
		tmp = MVS_CHIP_DISP->read_port_irq_mask(mvi, phy_no);
		MVS_CHIP_DISP->write_port_irq_mask(mvi, phy_no,
					tmp | PHYEV_SIG_FIS);
		if (phy->timer.function == NULL) {
			phy->timer.data = (unsigned long)phy;
			phy->timer.function = mvs_sig_time_out;
			phy->timer.expires = jiffies + 5*HZ;
			add_timer(&phy->timer);
		}
	}
	if (phy->irq_status & (PHYEV_SIG_FIS | PHYEV_ID_DONE)) {
		phy->phy_status = mvs_is_phy_ready(mvi, phy_no);
		mv_dprintk("notify plug in on phy[%d]\n", phy_no);
		if (phy->phy_status) {
			mdelay(10);
			MVS_CHIP_DISP->detect_porttype(mvi, phy_no);
			if (phy->phy_type & PORT_TYPE_SATA) {
				tmp = MVS_CHIP_DISP->read_port_irq_mask(
						mvi, phy_no);
				tmp &= ~PHYEV_SIG_FIS;
				MVS_CHIP_DISP->write_port_irq_mask(mvi,
							phy_no, tmp);
			}
			mvs_update_phyinfo(mvi, phy_no, 0);
			if (phy->phy_type & PORT_TYPE_SAS) {
				MVS_CHIP_DISP->phy_reset(mvi, phy_no, MVS_PHY_TUNE);
				mdelay(10);
			}

			mvs_bytes_dmaed(mvi, phy_no);
			/* whether driver is going to handle hot plug */
			if (phy->phy_event & PHY_PLUG_OUT) {
				mvs_port_notify_formed(&phy->sas_phy, 0);
				phy->phy_event &= ~PHY_PLUG_OUT;
			}
		} else {
			mv_dprintk("plugin interrupt but phy%d is gone\n",
				phy_no + mvi->id*mvi->chip->n_phy);
		}
	} else if (phy->irq_status & PHYEV_BROAD_CH) {
		mv_dprintk("phy %d broadcast change.\n",
			phy_no + mvi->id*mvi->chip->n_phy);
		mvs_handle_event(mvi, (void *)(unsigned long)phy_no,
				EXP_BRCT_CHG);
	}
}

int mvs_int_rx(struct mvs_info *mvi, bool self_clear)
{
	u32 rx_prod_idx, rx_desc;
	bool attn = false;

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
	if (unlikely(mvi->rx_cons == rx_prod_idx))
		mvi->rx_cons = MVS_CHIP_DISP->rx_update(mvi) & RX_RING_SZ_MASK;

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
		} else if (rx_desc & RXQ_ERR) {
			if (!(rx_desc & RXQ_DONE))
				mvs_slot_complete(mvi, rx_desc, 0);
		} else if (rx_desc & RXQ_SLOT_RESET) {
			mvs_slot_free(mvi, rx_desc);
		}
	}

	if (attn && self_clear)
		MVS_CHIP_DISP->int_full(mvi);
	return 0;
}

int mvs_gpio_write(struct sas_ha_struct *sha, u8 reg_type, u8 reg_index,
			u8 reg_count, u8 *write_data)
{
	struct mvs_prv_info *mvs_prv = sha->lldd_ha;
	struct mvs_info *mvi = mvs_prv->mvi[0];

	if (MVS_CHIP_DISP->gpio_write) {
		return MVS_CHIP_DISP->gpio_write(mvs_prv, reg_type,
			reg_index, reg_count, write_data);
	}

	return -ENOSYS;
}
