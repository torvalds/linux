// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <scsi/iscsi_if.h>
#include <linux/inet.h>
#include <net/arp.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/if_vlan.h>
#include <linux/cpu.h>
#include <linux/iscsi_boot_sysfs.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>

#include "qedi.h"
#include "qedi_gbl.h"
#include "qedi_iscsi.h"

static uint qedi_qed_debug;
module_param(qedi_qed_debug, uint, 0644);
MODULE_PARM_DESC(qedi_qed_debug, " QED debug level 0 (default)");

static uint qedi_fw_debug;
module_param(qedi_fw_debug, uint, 0644);
MODULE_PARM_DESC(qedi_fw_debug, " Firmware debug level 0(default) to 3");

uint qedi_dbg_log = QEDI_LOG_WARN | QEDI_LOG_SCSI_TM;
module_param(qedi_dbg_log, uint, 0644);
MODULE_PARM_DESC(qedi_dbg_log, " Default debug level");

uint qedi_io_tracing;
module_param(qedi_io_tracing, uint, 0644);
MODULE_PARM_DESC(qedi_io_tracing,
		 " Enable logging of SCSI requests/completions into trace buffer. (default off).");

static uint qedi_ll2_buf_size = 0x400;
module_param(qedi_ll2_buf_size, uint, 0644);
MODULE_PARM_DESC(qedi_ll2_buf_size,
		 "parameter to set ping packet size, default - 0x400, Jumbo packets - 0x2400.");

static uint qedi_flags_override;
module_param(qedi_flags_override, uint, 0644);
MODULE_PARM_DESC(qedi_flags_override, "Disable/Enable MFW error flags bits action.");

const struct qed_iscsi_ops *qedi_ops;
static struct scsi_transport_template *qedi_scsi_transport;
static struct pci_driver qedi_pci_driver;
static DEFINE_PER_CPU(struct qedi_percpu_s, qedi_percpu);
static LIST_HEAD(qedi_udev_list);
/* Static function declaration */
static int qedi_alloc_global_queues(struct qedi_ctx *qedi);
static void qedi_free_global_queues(struct qedi_ctx *qedi);
static struct qedi_cmd *qedi_get_cmd_from_tid(struct qedi_ctx *qedi, u32 tid);
static void qedi_reset_uio_rings(struct qedi_uio_dev *udev);
static void qedi_ll2_free_skbs(struct qedi_ctx *qedi);
static struct nvm_iscsi_block *qedi_get_nvram_block(struct qedi_ctx *qedi);
static void qedi_recovery_handler(struct work_struct *work);
static void qedi_schedule_hw_err_handler(void *dev,
					 enum qed_hw_err_type err_type);
static int qedi_suspend(struct pci_dev *pdev, pm_message_t state);

static int qedi_iscsi_event_cb(void *context, u8 fw_event_code, void *fw_handle)
{
	struct qedi_ctx *qedi;
	struct qedi_endpoint *qedi_ep;
	struct iscsi_eqe_data *data;
	int rval = 0;

	if (!context || !fw_handle) {
		QEDI_ERR(NULL, "Recv event with ctx NULL\n");
		return -EINVAL;
	}

	qedi = (struct qedi_ctx *)context;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Recv Event %d fw_handle %p\n", fw_event_code, fw_handle);

	data = (struct iscsi_eqe_data *)fw_handle;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "icid=0x%x conn_id=0x%x err-code=0x%x error-pdu-opcode-reserved=0x%x\n",
		   data->icid, data->conn_id, data->error_code,
		   data->error_pdu_opcode_reserved);

	qedi_ep = qedi->ep_tbl[data->icid];

	if (!qedi_ep) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Cannot process event, ep already disconnected, cid=0x%x\n",
			   data->icid);
		WARN_ON(1);
		return -ENODEV;
	}

	switch (fw_event_code) {
	case ISCSI_EVENT_TYPE_ASYN_CONNECT_COMPLETE:
		if (qedi_ep->state == EP_STATE_OFLDCONN_START)
			qedi_ep->state = EP_STATE_OFLDCONN_COMPL;

		wake_up_interruptible(&qedi_ep->tcp_ofld_wait);
		break;
	case ISCSI_EVENT_TYPE_ASYN_TERMINATE_DONE:
		qedi_ep->state = EP_STATE_DISCONN_COMPL;
		wake_up_interruptible(&qedi_ep->tcp_ofld_wait);
		break;
	case ISCSI_EVENT_TYPE_ISCSI_CONN_ERROR:
		qedi_process_iscsi_error(qedi_ep, data);
		break;
	case ISCSI_EVENT_TYPE_ASYN_ABORT_RCVD:
	case ISCSI_EVENT_TYPE_ASYN_SYN_RCVD:
	case ISCSI_EVENT_TYPE_ASYN_MAX_RT_TIME:
	case ISCSI_EVENT_TYPE_ASYN_MAX_RT_CNT:
	case ISCSI_EVENT_TYPE_ASYN_MAX_KA_PROBES_CNT:
	case ISCSI_EVENT_TYPE_ASYN_FIN_WAIT2:
	case ISCSI_EVENT_TYPE_TCP_CONN_ERROR:
		qedi_process_tcp_error(qedi_ep, data);
		break;
	default:
		QEDI_ERR(&qedi->dbg_ctx, "Recv Unknown Event %u\n",
			 fw_event_code);
	}

	return rval;
}

static int qedi_uio_open(struct uio_info *uinfo, struct inode *inode)
{
	struct qedi_uio_dev *udev = uinfo->priv;
	struct qedi_ctx *qedi = udev->qedi;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (udev->uio_dev != -1)
		return -EBUSY;

	rtnl_lock();
	udev->uio_dev = iminor(inode);
	qedi_reset_uio_rings(udev);
	set_bit(UIO_DEV_OPENED, &qedi->flags);
	rtnl_unlock();

	return 0;
}

static int qedi_uio_close(struct uio_info *uinfo, struct inode *inode)
{
	struct qedi_uio_dev *udev = uinfo->priv;
	struct qedi_ctx *qedi = udev->qedi;

	udev->uio_dev = -1;
	clear_bit(UIO_DEV_OPENED, &qedi->flags);
	qedi_ll2_free_skbs(qedi);
	return 0;
}

static void __qedi_free_uio_rings(struct qedi_uio_dev *udev)
{
	if (udev->uctrl) {
		free_page((unsigned long)udev->uctrl);
		udev->uctrl = NULL;
	}

	if (udev->ll2_ring) {
		free_page((unsigned long)udev->ll2_ring);
		udev->ll2_ring = NULL;
	}

	if (udev->ll2_buf) {
		free_pages((unsigned long)udev->ll2_buf, 2);
		udev->ll2_buf = NULL;
	}
}

static void __qedi_free_uio(struct qedi_uio_dev *udev)
{
	uio_unregister_device(&udev->qedi_uinfo);

	__qedi_free_uio_rings(udev);

	pci_dev_put(udev->pdev);
	kfree(udev);
}

static void qedi_free_uio(struct qedi_uio_dev *udev)
{
	if (!udev)
		return;

	list_del_init(&udev->list);
	__qedi_free_uio(udev);
}

static void qedi_reset_uio_rings(struct qedi_uio_dev *udev)
{
	struct qedi_ctx *qedi = NULL;
	struct qedi_uio_ctrl *uctrl = NULL;

	qedi = udev->qedi;
	uctrl = udev->uctrl;

	spin_lock_bh(&qedi->ll2_lock);
	uctrl->host_rx_cons = 0;
	uctrl->hw_rx_prod = 0;
	uctrl->hw_rx_bd_prod = 0;
	uctrl->host_rx_bd_cons = 0;

	memset(udev->ll2_ring, 0, udev->ll2_ring_size);
	memset(udev->ll2_buf, 0, udev->ll2_buf_size);
	spin_unlock_bh(&qedi->ll2_lock);
}

static int __qedi_alloc_uio_rings(struct qedi_uio_dev *udev)
{
	int rc = 0;

	if (udev->ll2_ring || udev->ll2_buf)
		return rc;

	/* Memory for control area.  */
	udev->uctrl = (void *)get_zeroed_page(GFP_KERNEL);
	if (!udev->uctrl)
		return -ENOMEM;

	/* Allocating memory for LL2 ring  */
	udev->ll2_ring_size = QEDI_PAGE_SIZE;
	udev->ll2_ring = (void *)get_zeroed_page(GFP_KERNEL | __GFP_COMP);
	if (!udev->ll2_ring) {
		rc = -ENOMEM;
		goto exit_alloc_ring;
	}

	/* Allocating memory for Tx/Rx pkt buffer */
	udev->ll2_buf_size = TX_RX_RING * qedi_ll2_buf_size;
	udev->ll2_buf_size = QEDI_PAGE_ALIGN(udev->ll2_buf_size);
	udev->ll2_buf = (void *)__get_free_pages(GFP_KERNEL | __GFP_COMP |
						 __GFP_ZERO, 2);
	if (!udev->ll2_buf) {
		rc = -ENOMEM;
		goto exit_alloc_buf;
	}
	return rc;

exit_alloc_buf:
	free_page((unsigned long)udev->ll2_ring);
	udev->ll2_ring = NULL;
exit_alloc_ring:
	return rc;
}

static int qedi_alloc_uio_rings(struct qedi_ctx *qedi)
{
	struct qedi_uio_dev *udev = NULL;
	int rc = 0;

	list_for_each_entry(udev, &qedi_udev_list, list) {
		if (udev->pdev == qedi->pdev) {
			udev->qedi = qedi;
			if (__qedi_alloc_uio_rings(udev)) {
				udev->qedi = NULL;
				return -ENOMEM;
			}
			qedi->udev = udev;
			return 0;
		}
	}

	udev = kzalloc(sizeof(*udev), GFP_KERNEL);
	if (!udev)
		goto err_udev;

	udev->uio_dev = -1;

	udev->qedi = qedi;
	udev->pdev = qedi->pdev;

	rc = __qedi_alloc_uio_rings(udev);
	if (rc)
		goto err_uctrl;

	list_add(&udev->list, &qedi_udev_list);

	pci_dev_get(udev->pdev);
	qedi->udev = udev;

	udev->tx_pkt = udev->ll2_buf;
	udev->rx_pkt = udev->ll2_buf + qedi_ll2_buf_size;
	return 0;

 err_uctrl:
	kfree(udev);
 err_udev:
	return -ENOMEM;
}

static int qedi_init_uio(struct qedi_ctx *qedi)
{
	struct qedi_uio_dev *udev = qedi->udev;
	struct uio_info *uinfo;
	int ret = 0;

	if (!udev)
		return -ENOMEM;

	uinfo = &udev->qedi_uinfo;

	uinfo->mem[0].addr = (unsigned long)udev->uctrl;
	uinfo->mem[0].size = sizeof(struct qedi_uio_ctrl);
	uinfo->mem[0].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[1].addr = (unsigned long)udev->ll2_ring;
	uinfo->mem[1].size = udev->ll2_ring_size;
	uinfo->mem[1].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[2].addr = (unsigned long)udev->ll2_buf;
	uinfo->mem[2].size = udev->ll2_buf_size;
	uinfo->mem[2].memtype = UIO_MEM_LOGICAL;

	uinfo->name = "qedi_uio";
	uinfo->version = QEDI_MODULE_VERSION;
	uinfo->irq = UIO_IRQ_CUSTOM;

	uinfo->open = qedi_uio_open;
	uinfo->release = qedi_uio_close;

	if (udev->uio_dev == -1) {
		if (!uinfo->priv) {
			uinfo->priv = udev;

			ret = uio_register_device(&udev->pdev->dev, uinfo);
			if (ret) {
				QEDI_ERR(&qedi->dbg_ctx,
					 "UIO registration failed\n");
			}
		}
	}

	return ret;
}

static int qedi_alloc_and_init_sb(struct qedi_ctx *qedi,
				  struct qed_sb_info *sb_info, u16 sb_id)
{
	struct status_block *sb_virt;
	dma_addr_t sb_phys;
	int ret;

	sb_virt = dma_alloc_coherent(&qedi->pdev->dev,
				     sizeof(struct status_block), &sb_phys,
				     GFP_KERNEL);
	if (!sb_virt) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Status block allocation failed for id = %d.\n",
			  sb_id);
		return -ENOMEM;
	}

	ret = qedi_ops->common->sb_init(qedi->cdev, sb_info, sb_virt, sb_phys,
				       sb_id, QED_SB_TYPE_STORAGE);
	if (ret) {
		dma_free_coherent(&qedi->pdev->dev, sizeof(*sb_virt), sb_virt, sb_phys);
		QEDI_ERR(&qedi->dbg_ctx,
			 "Status block initialization failed for id = %d.\n",
			  sb_id);
		return ret;
	}

	return 0;
}

static void qedi_free_sb(struct qedi_ctx *qedi)
{
	struct qed_sb_info *sb_info;
	int id;

	for (id = 0; id < MIN_NUM_CPUS_MSIX(qedi); id++) {
		sb_info = &qedi->sb_array[id];
		if (sb_info->sb_virt)
			dma_free_coherent(&qedi->pdev->dev,
					  sizeof(*sb_info->sb_virt),
					  (void *)sb_info->sb_virt,
					  sb_info->sb_phys);
	}
}

static void qedi_free_fp(struct qedi_ctx *qedi)
{
	kfree(qedi->fp_array);
	kfree(qedi->sb_array);
}

static void qedi_destroy_fp(struct qedi_ctx *qedi)
{
	qedi_free_sb(qedi);
	qedi_free_fp(qedi);
}

static int qedi_alloc_fp(struct qedi_ctx *qedi)
{
	int ret = 0;

	qedi->fp_array = kcalloc(MIN_NUM_CPUS_MSIX(qedi),
				 sizeof(struct qedi_fastpath), GFP_KERNEL);
	if (!qedi->fp_array) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "fastpath fp array allocation failed.\n");
		return -ENOMEM;
	}

	qedi->sb_array = kcalloc(MIN_NUM_CPUS_MSIX(qedi),
				 sizeof(struct qed_sb_info), GFP_KERNEL);
	if (!qedi->sb_array) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "fastpath sb array allocation failed.\n");
		ret = -ENOMEM;
		goto free_fp;
	}

	return ret;

free_fp:
	qedi_free_fp(qedi);
	return ret;
}

static void qedi_int_fp(struct qedi_ctx *qedi)
{
	struct qedi_fastpath *fp;
	int id;

	memset(qedi->fp_array, 0, MIN_NUM_CPUS_MSIX(qedi) *
	       sizeof(*qedi->fp_array));
	memset(qedi->sb_array, 0, MIN_NUM_CPUS_MSIX(qedi) *
	       sizeof(*qedi->sb_array));

	for (id = 0; id < MIN_NUM_CPUS_MSIX(qedi); id++) {
		fp = &qedi->fp_array[id];
		fp->sb_info = &qedi->sb_array[id];
		fp->sb_id = id;
		fp->qedi = qedi;
		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d",
			 "qedi", id);

		/* fp_array[i] ---- irq cookie
		 * So init data which is needed in int ctx
		 */
	}
}

static int qedi_prepare_fp(struct qedi_ctx *qedi)
{
	struct qedi_fastpath *fp;
	int id, ret = 0;

	ret = qedi_alloc_fp(qedi);
	if (ret)
		goto err;

	qedi_int_fp(qedi);

	for (id = 0; id < MIN_NUM_CPUS_MSIX(qedi); id++) {
		fp = &qedi->fp_array[id];
		ret = qedi_alloc_and_init_sb(qedi, fp->sb_info, fp->sb_id);
		if (ret) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "SB allocation and initialization failed.\n");
			ret = -EIO;
			goto err_init;
		}
	}

	return 0;

err_init:
	qedi_free_sb(qedi);
	qedi_free_fp(qedi);
err:
	return ret;
}

static int qedi_setup_cid_que(struct qedi_ctx *qedi)
{
	int i;

	qedi->cid_que.cid_que_base = kmalloc_array(qedi->max_active_conns,
						   sizeof(u32), GFP_KERNEL);
	if (!qedi->cid_que.cid_que_base)
		return -ENOMEM;

	qedi->cid_que.conn_cid_tbl = kmalloc_array(qedi->max_active_conns,
						   sizeof(struct qedi_conn *),
						   GFP_KERNEL);
	if (!qedi->cid_que.conn_cid_tbl) {
		kfree(qedi->cid_que.cid_que_base);
		qedi->cid_que.cid_que_base = NULL;
		return -ENOMEM;
	}

	qedi->cid_que.cid_que = (u32 *)qedi->cid_que.cid_que_base;
	qedi->cid_que.cid_q_prod_idx = 0;
	qedi->cid_que.cid_q_cons_idx = 0;
	qedi->cid_que.cid_q_max_idx = qedi->max_active_conns;
	qedi->cid_que.cid_free_cnt = qedi->max_active_conns;

	for (i = 0; i < qedi->max_active_conns; i++) {
		qedi->cid_que.cid_que[i] = i;
		qedi->cid_que.conn_cid_tbl[i] = NULL;
	}

	return 0;
}

static void qedi_release_cid_que(struct qedi_ctx *qedi)
{
	kfree(qedi->cid_que.cid_que_base);
	qedi->cid_que.cid_que_base = NULL;

	kfree(qedi->cid_que.conn_cid_tbl);
	qedi->cid_que.conn_cid_tbl = NULL;
}

static int qedi_init_id_tbl(struct qedi_portid_tbl *id_tbl, u16 size,
			    u16 start_id, u16 next)
{
	id_tbl->start = start_id;
	id_tbl->max = size;
	id_tbl->next = next;
	spin_lock_init(&id_tbl->lock);
	id_tbl->table = kcalloc(BITS_TO_LONGS(size), sizeof(long), GFP_KERNEL);
	if (!id_tbl->table)
		return -ENOMEM;

	return 0;
}

static void qedi_free_id_tbl(struct qedi_portid_tbl *id_tbl)
{
	kfree(id_tbl->table);
	id_tbl->table = NULL;
}

int qedi_alloc_id(struct qedi_portid_tbl *id_tbl, u16 id)
{
	int ret = -1;

	id -= id_tbl->start;
	if (id >= id_tbl->max)
		return ret;

	spin_lock(&id_tbl->lock);
	if (!test_bit(id, id_tbl->table)) {
		set_bit(id, id_tbl->table);
		ret = 0;
	}
	spin_unlock(&id_tbl->lock);
	return ret;
}

u16 qedi_alloc_new_id(struct qedi_portid_tbl *id_tbl)
{
	u16 id;

	spin_lock(&id_tbl->lock);
	id = find_next_zero_bit(id_tbl->table, id_tbl->max, id_tbl->next);
	if (id >= id_tbl->max) {
		id = QEDI_LOCAL_PORT_INVALID;
		if (id_tbl->next != 0) {
			id = find_first_zero_bit(id_tbl->table, id_tbl->next);
			if (id >= id_tbl->next)
				id = QEDI_LOCAL_PORT_INVALID;
		}
	}

	if (id < id_tbl->max) {
		set_bit(id, id_tbl->table);
		id_tbl->next = (id + 1) & (id_tbl->max - 1);
		id += id_tbl->start;
	}

	spin_unlock(&id_tbl->lock);

	return id;
}

void qedi_free_id(struct qedi_portid_tbl *id_tbl, u16 id)
{
	if (id == QEDI_LOCAL_PORT_INVALID)
		return;

	id -= id_tbl->start;
	if (id >= id_tbl->max)
		return;

	clear_bit(id, id_tbl->table);
}

static void qedi_cm_free_mem(struct qedi_ctx *qedi)
{
	kfree(qedi->ep_tbl);
	qedi->ep_tbl = NULL;
	qedi_free_id_tbl(&qedi->lcl_port_tbl);
}

static int qedi_cm_alloc_mem(struct qedi_ctx *qedi)
{
	u16 port_id;

	qedi->ep_tbl = kzalloc((qedi->max_active_conns *
				sizeof(struct qedi_endpoint *)), GFP_KERNEL);
	if (!qedi->ep_tbl)
		return -ENOMEM;
	port_id = get_random_u32_below(QEDI_LOCAL_PORT_RANGE);
	if (qedi_init_id_tbl(&qedi->lcl_port_tbl, QEDI_LOCAL_PORT_RANGE,
			     QEDI_LOCAL_PORT_MIN, port_id)) {
		qedi_cm_free_mem(qedi);
		return -ENOMEM;
	}

	return 0;
}

static struct qedi_ctx *qedi_host_alloc(struct pci_dev *pdev)
{
	struct Scsi_Host *shost;
	struct qedi_ctx *qedi = NULL;

	shost = iscsi_host_alloc(&qedi_host_template,
				 sizeof(struct qedi_ctx), 0);
	if (!shost) {
		QEDI_ERR(NULL, "Could not allocate shost\n");
		goto exit_setup_shost;
	}

	shost->max_id = QEDI_MAX_ISCSI_CONNS_PER_HBA - 1;
	shost->max_channel = 0;
	shost->max_lun = ~0;
	shost->max_cmd_len = 16;
	shost->transportt = qedi_scsi_transport;

	qedi = iscsi_host_priv(shost);
	memset(qedi, 0, sizeof(*qedi));
	qedi->shost = shost;
	qedi->dbg_ctx.host_no = shost->host_no;
	qedi->pdev = pdev;
	qedi->dbg_ctx.pdev = pdev;
	qedi->max_active_conns = ISCSI_MAX_SESS_PER_HBA;
	qedi->max_sqes = QEDI_SQ_SIZE;

	shost->nr_hw_queues = MIN_NUM_CPUS_MSIX(qedi);

	pci_set_drvdata(pdev, qedi);

exit_setup_shost:
	return qedi;
}

static int qedi_ll2_rx(void *cookie, struct sk_buff *skb, u32 arg1, u32 arg2)
{
	struct qedi_ctx *qedi = (struct qedi_ctx *)cookie;
	struct skb_work_list *work;
	struct ethhdr *eh;

	if (!qedi) {
		QEDI_ERR(NULL, "qedi is NULL\n");
		return -1;
	}

	if (!test_bit(UIO_DEV_OPENED, &qedi->flags)) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_UIO,
			  "UIO DEV is not opened\n");
		kfree_skb(skb);
		return 0;
	}

	eh = (struct ethhdr *)skb->data;
	/* Undo VLAN encapsulation */
	if (eh->h_proto == htons(ETH_P_8021Q)) {
		memmove((u8 *)eh + VLAN_HLEN, eh, ETH_ALEN * 2);
		eh = (struct ethhdr *)skb_pull(skb, VLAN_HLEN);
		skb_reset_mac_header(skb);
	}

	/* Filter out non FIP/FCoE frames here to free them faster */
	if (eh->h_proto != htons(ETH_P_ARP) &&
	    eh->h_proto != htons(ETH_P_IP) &&
	    eh->h_proto != htons(ETH_P_IPV6)) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_LL2,
			  "Dropping frame ethertype [0x%x] len [0x%x].\n",
			  eh->h_proto, skb->len);
		kfree_skb(skb);
		return 0;
	}

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_LL2,
		  "Allowed frame ethertype [0x%x] len [0x%x].\n",
		  eh->h_proto, skb->len);

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Could not allocate work so dropping frame.\n");
		kfree_skb(skb);
		return 0;
	}

	INIT_LIST_HEAD(&work->list);
	work->skb = skb;

	if (skb_vlan_tag_present(skb))
		work->vlan_id = skb_vlan_tag_get(skb);

	if (work->vlan_id)
		__vlan_insert_tag(work->skb, htons(ETH_P_8021Q), work->vlan_id);

	spin_lock_bh(&qedi->ll2_lock);
	list_add_tail(&work->list, &qedi->ll2_skb_list);
	spin_unlock_bh(&qedi->ll2_lock);

	wake_up_process(qedi->ll2_recv_thread);

	return 0;
}

/* map this skb to iscsiuio mmaped region */
static int qedi_ll2_process_skb(struct qedi_ctx *qedi, struct sk_buff *skb,
				u16 vlan_id)
{
	struct qedi_uio_dev *udev = NULL;
	struct qedi_uio_ctrl *uctrl = NULL;
	struct qedi_rx_bd rxbd;
	struct qedi_rx_bd *p_rxbd;
	u32 rx_bd_prod;
	void *pkt;
	int len = 0;
	u32 prod;

	if (!qedi) {
		QEDI_ERR(NULL, "qedi is NULL\n");
		return -1;
	}

	udev = qedi->udev;
	uctrl = udev->uctrl;

	++uctrl->hw_rx_prod_cnt;
	prod = (uctrl->hw_rx_prod + 1) % RX_RING;

	pkt = udev->rx_pkt + (prod * qedi_ll2_buf_size);
	len = min_t(u32, skb->len, (u32)qedi_ll2_buf_size);
	memcpy(pkt, skb->data, len);

	memset(&rxbd, 0, sizeof(rxbd));
	rxbd.rx_pkt_index = prod;
	rxbd.rx_pkt_len = len;
	rxbd.vlan_id = vlan_id;

	uctrl->hw_rx_bd_prod = (uctrl->hw_rx_bd_prod + 1) % QEDI_NUM_RX_BD;
	rx_bd_prod = uctrl->hw_rx_bd_prod;
	p_rxbd = (struct qedi_rx_bd *)udev->ll2_ring;
	p_rxbd += rx_bd_prod;

	memcpy(p_rxbd, &rxbd, sizeof(rxbd));

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_LL2,
		  "hw_rx_prod [%d] prod [%d] hw_rx_bd_prod [%d] rx_pkt_idx [%d] rx_len [%d].\n",
		  uctrl->hw_rx_prod, prod, uctrl->hw_rx_bd_prod,
		  rxbd.rx_pkt_index, rxbd.rx_pkt_len);
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_LL2,
		  "host_rx_cons [%d] hw_rx_bd_cons [%d].\n",
		  uctrl->host_rx_cons, uctrl->host_rx_bd_cons);

	uctrl->hw_rx_prod = prod;

	/* notify the iscsiuio about new packet */
	uio_event_notify(&udev->qedi_uinfo);

	return 0;
}

static void qedi_ll2_free_skbs(struct qedi_ctx *qedi)
{
	struct skb_work_list *work, *work_tmp;

	spin_lock_bh(&qedi->ll2_lock);
	list_for_each_entry_safe(work, work_tmp, &qedi->ll2_skb_list, list) {
		list_del(&work->list);
		kfree_skb(work->skb);
		kfree(work);
	}
	spin_unlock_bh(&qedi->ll2_lock);
}

static int qedi_ll2_recv_thread(void *arg)
{
	struct qedi_ctx *qedi = (struct qedi_ctx *)arg;
	struct skb_work_list *work, *work_tmp;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		spin_lock_bh(&qedi->ll2_lock);
		list_for_each_entry_safe(work, work_tmp, &qedi->ll2_skb_list,
					 list) {
			list_del(&work->list);
			qedi_ll2_process_skb(qedi, work->skb, work->vlan_id);
			kfree_skb(work->skb);
			kfree(work);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_bh(&qedi->ll2_lock);
		schedule();
	}

	__set_current_state(TASK_RUNNING);
	return 0;
}

static int qedi_set_iscsi_pf_param(struct qedi_ctx *qedi)
{
	u8 num_sq_pages;
	u32 log_page_size;
	int rval = 0;


	num_sq_pages = (MAX_OUTSTANDING_TASKS_PER_CON * 8) / QEDI_PAGE_SIZE;

	qedi->num_queues = MIN_NUM_CPUS_MSIX(qedi);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Number of CQ count is %d\n", qedi->num_queues);

	memset(&qedi->pf_params.iscsi_pf_params, 0,
	       sizeof(qedi->pf_params.iscsi_pf_params));

	qedi->p_cpuq = dma_alloc_coherent(&qedi->pdev->dev,
			qedi->num_queues * sizeof(struct qedi_glbl_q_params),
			&qedi->hw_p_cpuq, GFP_KERNEL);
	if (!qedi->p_cpuq) {
		QEDI_ERR(&qedi->dbg_ctx, "dma_alloc_coherent fail\n");
		rval = -1;
		goto err_alloc_mem;
	}

	rval = qedi_alloc_global_queues(qedi);
	if (rval) {
		QEDI_ERR(&qedi->dbg_ctx, "Global queue allocation failed.\n");
		rval = -1;
		goto err_alloc_mem;
	}

	qedi->pf_params.iscsi_pf_params.num_cons = QEDI_MAX_ISCSI_CONNS_PER_HBA;
	qedi->pf_params.iscsi_pf_params.num_tasks = QEDI_MAX_ISCSI_TASK;
	qedi->pf_params.iscsi_pf_params.half_way_close_timeout = 10;
	qedi->pf_params.iscsi_pf_params.num_sq_pages_in_ring = num_sq_pages;
	qedi->pf_params.iscsi_pf_params.num_r2tq_pages_in_ring = num_sq_pages;
	qedi->pf_params.iscsi_pf_params.num_uhq_pages_in_ring = num_sq_pages;
	qedi->pf_params.iscsi_pf_params.num_queues = qedi->num_queues;
	qedi->pf_params.iscsi_pf_params.debug_mode = qedi_fw_debug;
	qedi->pf_params.iscsi_pf_params.two_msl_timer = QED_TWO_MSL_TIMER_DFLT;
	qedi->pf_params.iscsi_pf_params.tx_sws_timer = QED_TX_SWS_TIMER_DFLT;
	qedi->pf_params.iscsi_pf_params.max_fin_rt = 2;

	for (log_page_size = 0 ; log_page_size < 32 ; log_page_size++) {
		if ((1 << log_page_size) == QEDI_PAGE_SIZE)
			break;
	}
	qedi->pf_params.iscsi_pf_params.log_page_size = log_page_size;

	qedi->pf_params.iscsi_pf_params.glbl_q_params_addr =
							   (u64)qedi->hw_p_cpuq;

	/* RQ BDQ initializations.
	 * rq_num_entries: suggested value for Initiator is 16 (4KB RQ)
	 * rqe_log_size: 8 for 256B RQE
	 */
	qedi->pf_params.iscsi_pf_params.rqe_log_size = 8;
	/* BDQ address and size */
	qedi->pf_params.iscsi_pf_params.bdq_pbl_base_addr[BDQ_ID_RQ] =
							qedi->bdq_pbl_list_dma;
	qedi->pf_params.iscsi_pf_params.bdq_pbl_num_entries[BDQ_ID_RQ] =
						qedi->bdq_pbl_list_num_entries;
	qedi->pf_params.iscsi_pf_params.rq_buffer_size = QEDI_BDQ_BUF_SIZE;

	/* cq_num_entries: num_tasks + rq_num_entries */
	qedi->pf_params.iscsi_pf_params.cq_num_entries = 2048;

	qedi->pf_params.iscsi_pf_params.gl_rq_pi = QEDI_PROTO_CQ_PROD_IDX;
	qedi->pf_params.iscsi_pf_params.gl_cmd_pi = 1;

err_alloc_mem:
	return rval;
}

/* Free DMA coherent memory for array of queue pointers we pass to qed */
static void qedi_free_iscsi_pf_param(struct qedi_ctx *qedi)
{
	size_t size = 0;

	if (qedi->p_cpuq) {
		size = qedi->num_queues * sizeof(struct qedi_glbl_q_params);
		dma_free_coherent(&qedi->pdev->dev, size, qedi->p_cpuq,
				    qedi->hw_p_cpuq);
	}

	qedi_free_global_queues(qedi);

	kfree(qedi->global_queues);
}

static void qedi_get_boot_tgt_info(struct nvm_iscsi_block *block,
				   struct qedi_boot_target *tgt, u8 index)
{
	u32 ipv6_en;

	ipv6_en = !!(block->generic.ctrl_flags &
		     NVM_ISCSI_CFG_GEN_IPV6_ENABLED);

	snprintf(tgt->iscsi_name, sizeof(tgt->iscsi_name), "%s",
		 block->target[index].target_name.byte);

	tgt->ipv6_en = ipv6_en;

	if (ipv6_en)
		snprintf(tgt->ip_addr, IPV6_LEN, "%pI6\n",
			 block->target[index].ipv6_addr.byte);
	else
		snprintf(tgt->ip_addr, IPV4_LEN, "%pI4\n",
			 block->target[index].ipv4_addr.byte);
}

static int qedi_find_boot_info(struct qedi_ctx *qedi,
			       struct qed_mfw_tlv_iscsi *iscsi,
			       struct nvm_iscsi_block *block)
{
	struct qedi_boot_target *pri_tgt = NULL, *sec_tgt = NULL;
	u32 pri_ctrl_flags = 0, sec_ctrl_flags = 0, found = 0;
	struct iscsi_cls_session *cls_sess;
	struct iscsi_cls_conn *cls_conn;
	struct qedi_conn *qedi_conn;
	struct iscsi_session *sess;
	struct iscsi_conn *conn;
	char ep_ip_addr[64];
	int i, ret = 0;

	pri_ctrl_flags = !!(block->target[0].ctrl_flags &
					NVM_ISCSI_CFG_TARGET_ENABLED);
	if (pri_ctrl_flags) {
		pri_tgt = kzalloc(sizeof(*pri_tgt), GFP_KERNEL);
		if (!pri_tgt)
			return -1;
		qedi_get_boot_tgt_info(block, pri_tgt, 0);
	}

	sec_ctrl_flags = !!(block->target[1].ctrl_flags &
					NVM_ISCSI_CFG_TARGET_ENABLED);
	if (sec_ctrl_flags) {
		sec_tgt = kzalloc(sizeof(*sec_tgt), GFP_KERNEL);
		if (!sec_tgt) {
			ret = -1;
			goto free_tgt;
		}
		qedi_get_boot_tgt_info(block, sec_tgt, 1);
	}

	for (i = 0; i < qedi->max_active_conns; i++) {
		qedi_conn = qedi_get_conn_from_id(qedi, i);
		if (!qedi_conn)
			continue;

		if (qedi_conn->ep->ip_type == TCP_IPV4)
			snprintf(ep_ip_addr, IPV4_LEN, "%pI4\n",
				 qedi_conn->ep->dst_addr);
		else
			snprintf(ep_ip_addr, IPV6_LEN, "%pI6\n",
				 qedi_conn->ep->dst_addr);

		cls_conn = qedi_conn->cls_conn;
		conn = cls_conn->dd_data;
		cls_sess = iscsi_conn_to_session(cls_conn);
		sess = cls_sess->dd_data;

		if (!iscsi_is_session_online(cls_sess))
			continue;

		if (!sess->targetname)
			continue;

		if (pri_ctrl_flags) {
			if (!strcmp(pri_tgt->iscsi_name, sess->targetname) &&
			    !strcmp(pri_tgt->ip_addr, ep_ip_addr)) {
				found = 1;
				break;
			}
		}

		if (sec_ctrl_flags) {
			if (!strcmp(sec_tgt->iscsi_name, sess->targetname) &&
			    !strcmp(sec_tgt->ip_addr, ep_ip_addr)) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		if (conn->hdrdgst_en) {
			iscsi->header_digest_set = true;
			iscsi->header_digest = 1;
		}

		if (conn->datadgst_en) {
			iscsi->data_digest_set = true;
			iscsi->data_digest = 1;
		}
		iscsi->boot_taget_portal_set = true;
		iscsi->boot_taget_portal = sess->tpgt;

	} else {
		ret = -1;
	}

	if (sec_ctrl_flags)
		kfree(sec_tgt);
free_tgt:
	if (pri_ctrl_flags)
		kfree(pri_tgt);

	return ret;
}

static void qedi_get_generic_tlv_data(void *dev, struct qed_generic_tlvs *data)
{
	struct qedi_ctx *qedi;

	if (!dev) {
		QEDI_INFO(NULL, QEDI_LOG_EVT,
			  "dev is NULL so ignoring get_generic_tlv_data request.\n");
		return;
	}
	qedi = (struct qedi_ctx *)dev;

	memset(data, 0, sizeof(struct qed_generic_tlvs));
	ether_addr_copy(data->mac[0], qedi->mac);
}

/*
 * Protocol TLV handler
 */
static void qedi_get_protocol_tlv_data(void *dev, void *data)
{
	struct qed_mfw_tlv_iscsi *iscsi = data;
	struct qed_iscsi_stats *fw_iscsi_stats;
	struct nvm_iscsi_block *block = NULL;
	u32 chap_en = 0, mchap_en = 0;
	struct qedi_ctx *qedi = dev;
	int rval = 0;

	fw_iscsi_stats = kmalloc(sizeof(*fw_iscsi_stats), GFP_KERNEL);
	if (!fw_iscsi_stats) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Could not allocate memory for fw_iscsi_stats.\n");
		goto exit_get_data;
	}

	mutex_lock(&qedi->stats_lock);
	/* Query firmware for offload stats */
	qedi_ops->get_stats(qedi->cdev, fw_iscsi_stats);
	mutex_unlock(&qedi->stats_lock);

	iscsi->rx_frames_set = true;
	iscsi->rx_frames = fw_iscsi_stats->iscsi_rx_packet_cnt;
	iscsi->rx_bytes_set = true;
	iscsi->rx_bytes = fw_iscsi_stats->iscsi_rx_bytes_cnt;
	iscsi->tx_frames_set = true;
	iscsi->tx_frames = fw_iscsi_stats->iscsi_tx_packet_cnt;
	iscsi->tx_bytes_set = true;
	iscsi->tx_bytes = fw_iscsi_stats->iscsi_tx_bytes_cnt;
	iscsi->frame_size_set = true;
	iscsi->frame_size = qedi->ll2_mtu;
	block = qedi_get_nvram_block(qedi);
	if (block) {
		chap_en = !!(block->generic.ctrl_flags &
			     NVM_ISCSI_CFG_GEN_CHAP_ENABLED);
		mchap_en = !!(block->generic.ctrl_flags &
			      NVM_ISCSI_CFG_GEN_CHAP_MUTUAL_ENABLED);

		iscsi->auth_method_set = (chap_en || mchap_en) ? true : false;
		iscsi->auth_method = 1;
		if (chap_en)
			iscsi->auth_method = 2;
		if (mchap_en)
			iscsi->auth_method = 3;

		iscsi->tx_desc_size_set = true;
		iscsi->tx_desc_size = QEDI_SQ_SIZE;
		iscsi->rx_desc_size_set = true;
		iscsi->rx_desc_size = QEDI_CQ_SIZE;

		/* tpgt, hdr digest, data digest */
		rval = qedi_find_boot_info(qedi, iscsi, block);
		if (rval)
			QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
				  "Boot target not set");
	}

	kfree(fw_iscsi_stats);
exit_get_data:
	return;
}

void qedi_schedule_hw_err_handler(void *dev,
				  enum qed_hw_err_type err_type)
{
	struct qedi_ctx *qedi = (struct qedi_ctx *)dev;
	unsigned long override_flags = qedi_flags_override;

	if (override_flags && test_bit(QEDI_ERR_OVERRIDE_EN, &override_flags))
		qedi->qedi_err_flags = qedi_flags_override;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "HW error handler scheduled, err=%d err_flags=0x%x\n",
		  err_type, qedi->qedi_err_flags);

	switch (err_type) {
	case QED_HW_ERR_FAN_FAIL:
		schedule_delayed_work(&qedi->board_disable_work, 0);
		break;
	case QED_HW_ERR_MFW_RESP_FAIL:
	case QED_HW_ERR_HW_ATTN:
	case QED_HW_ERR_DMAE_FAIL:
	case QED_HW_ERR_RAMROD_FAIL:
	case QED_HW_ERR_FW_ASSERT:
		/* Prevent HW attentions from being reasserted */
		if (test_bit(QEDI_ERR_ATTN_CLR_EN, &qedi->qedi_err_flags))
			qedi_ops->common->attn_clr_enable(qedi->cdev, true);

		if (err_type == QED_HW_ERR_RAMROD_FAIL &&
		    test_bit(QEDI_ERR_IS_RECOVERABLE, &qedi->qedi_err_flags))
			qedi_ops->common->recovery_process(qedi->cdev);

		break;
	default:
		break;
	}
}

static void qedi_schedule_recovery_handler(void *dev)
{
	struct qedi_ctx *qedi = dev;

	QEDI_ERR(&qedi->dbg_ctx, "Recovery handler scheduled.\n");

	if (test_and_set_bit(QEDI_IN_RECOVERY, &qedi->flags))
		return;

	atomic_set(&qedi->link_state, QEDI_LINK_DOWN);

	schedule_delayed_work(&qedi->recovery_work, 0);
}

static void qedi_set_conn_recovery(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct iscsi_conn *conn = session->leadconn;
	struct qedi_conn *qedi_conn = conn->dd_data;

	qedi_start_conn_recovery(qedi_conn->qedi, qedi_conn);
}

static void qedi_link_update(void *dev, struct qed_link_output *link)
{
	struct qedi_ctx *qedi = (struct qedi_ctx *)dev;

	if (link->link_up) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO, "Link Up event.\n");
		atomic_set(&qedi->link_state, QEDI_LINK_UP);
	} else {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "Link Down event.\n");
		atomic_set(&qedi->link_state, QEDI_LINK_DOWN);
		iscsi_host_for_each_session(qedi->shost, qedi_set_conn_recovery);
	}
}

static struct qed_iscsi_cb_ops qedi_cb_ops = {
	{
		.link_update =		qedi_link_update,
		.schedule_recovery_handler = qedi_schedule_recovery_handler,
		.schedule_hw_err_handler = qedi_schedule_hw_err_handler,
		.get_protocol_tlv_data = qedi_get_protocol_tlv_data,
		.get_generic_tlv_data = qedi_get_generic_tlv_data,
	}
};

static int qedi_queue_cqe(struct qedi_ctx *qedi, union iscsi_cqe *cqe,
			  u16 que_idx, struct qedi_percpu_s *p)
{
	struct qedi_work *qedi_work;
	struct qedi_conn *q_conn;
	struct qedi_cmd *qedi_cmd;
	u32 iscsi_cid;
	int rc = 0;

	iscsi_cid  = cqe->cqe_common.conn_id;
	q_conn = qedi->cid_que.conn_cid_tbl[iscsi_cid];
	if (!q_conn) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Session no longer exists for cid=0x%x!!\n",
			  iscsi_cid);
		return -1;
	}

	switch (cqe->cqe_common.cqe_type) {
	case ISCSI_CQE_TYPE_SOLICITED:
	case ISCSI_CQE_TYPE_SOLICITED_WITH_SENSE:
		qedi_cmd = qedi_get_cmd_from_tid(qedi, cqe->cqe_solicited.itid);
		if (!qedi_cmd) {
			rc = -1;
			break;
		}
		INIT_LIST_HEAD(&qedi_cmd->cqe_work.list);
		qedi_cmd->cqe_work.qedi = qedi;
		memcpy(&qedi_cmd->cqe_work.cqe, cqe, sizeof(union iscsi_cqe));
		qedi_cmd->cqe_work.que_idx = que_idx;
		qedi_cmd->cqe_work.is_solicited = true;
		list_add_tail(&qedi_cmd->cqe_work.list, &p->work_list);
		break;
	case ISCSI_CQE_TYPE_UNSOLICITED:
	case ISCSI_CQE_TYPE_DUMMY:
	case ISCSI_CQE_TYPE_TASK_CLEANUP:
		qedi_work = kzalloc(sizeof(*qedi_work), GFP_ATOMIC);
		if (!qedi_work) {
			rc = -1;
			break;
		}
		INIT_LIST_HEAD(&qedi_work->list);
		qedi_work->qedi = qedi;
		memcpy(&qedi_work->cqe, cqe, sizeof(union iscsi_cqe));
		qedi_work->que_idx = que_idx;
		qedi_work->is_solicited = false;
		list_add_tail(&qedi_work->list, &p->work_list);
		break;
	default:
		rc = -1;
		QEDI_ERR(&qedi->dbg_ctx, "FW Error cqe.\n");
	}
	return rc;
}

static bool qedi_process_completions(struct qedi_fastpath *fp)
{
	struct qedi_ctx *qedi = fp->qedi;
	struct qed_sb_info *sb_info = fp->sb_info;
	struct status_block *sb = sb_info->sb_virt;
	struct qedi_percpu_s *p = NULL;
	struct global_queue *que;
	u16 prod_idx;
	unsigned long flags;
	union iscsi_cqe *cqe;
	int cpu;
	int ret;

	/* Get the current firmware producer index */
	prod_idx = sb->pi_array[QEDI_PROTO_CQ_PROD_IDX];

	if (prod_idx >= QEDI_CQ_SIZE)
		prod_idx = prod_idx % QEDI_CQ_SIZE;

	que = qedi->global_queues[fp->sb_id];
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO,
		  "Before: global queue=%p prod_idx=%d cons_idx=%d, sb_id=%d\n",
		  que, prod_idx, que->cq_cons_idx, fp->sb_id);

	qedi->intr_cpu = fp->sb_id;
	cpu = smp_processor_id();
	p = &per_cpu(qedi_percpu, cpu);

	if (unlikely(!p->iothread))
		WARN_ON(1);

	spin_lock_irqsave(&p->p_work_lock, flags);
	while (que->cq_cons_idx != prod_idx) {
		cqe = &que->cq[que->cq_cons_idx];

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO,
			  "cqe=%p prod_idx=%d cons_idx=%d.\n",
			  cqe, prod_idx, que->cq_cons_idx);

		ret = qedi_queue_cqe(qedi, cqe, fp->sb_id, p);
		if (ret)
			QEDI_WARN(&qedi->dbg_ctx,
				  "Dropping CQE 0x%x for cid=0x%x.\n",
				  que->cq_cons_idx, cqe->cqe_common.conn_id);

		que->cq_cons_idx++;
		if (que->cq_cons_idx == QEDI_CQ_SIZE)
			que->cq_cons_idx = 0;
	}
	wake_up_process(p->iothread);
	spin_unlock_irqrestore(&p->p_work_lock, flags);

	return true;
}

static bool qedi_fp_has_work(struct qedi_fastpath *fp)
{
	struct qedi_ctx *qedi = fp->qedi;
	struct global_queue *que;
	struct qed_sb_info *sb_info = fp->sb_info;
	struct status_block *sb = sb_info->sb_virt;
	u16 prod_idx;

	barrier();

	/* Get the current firmware producer index */
	prod_idx = sb->pi_array[QEDI_PROTO_CQ_PROD_IDX];

	/* Get the pointer to the global CQ this completion is on */
	que = qedi->global_queues[fp->sb_id];

	/* prod idx wrap around uint16 */
	if (prod_idx >= QEDI_CQ_SIZE)
		prod_idx = prod_idx % QEDI_CQ_SIZE;

	return (que->cq_cons_idx != prod_idx);
}

/* MSI-X fastpath handler code */
static irqreturn_t qedi_msix_handler(int irq, void *dev_id)
{
	struct qedi_fastpath *fp = dev_id;
	struct qedi_ctx *qedi = fp->qedi;
	bool wake_io_thread = true;

	qed_sb_ack(fp->sb_info, IGU_INT_DISABLE, 0);

process_again:
	wake_io_thread = qedi_process_completions(fp);
	if (wake_io_thread) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
			  "process already running\n");
	}

	if (!qedi_fp_has_work(fp))
		qed_sb_update_sb_idx(fp->sb_info);

	/* Check for more work */
	rmb();

	if (!qedi_fp_has_work(fp))
		qed_sb_ack(fp->sb_info, IGU_INT_ENABLE, 1);
	else
		goto process_again;

	return IRQ_HANDLED;
}

/* simd handler for MSI/INTa */
static void qedi_simd_int_handler(void *cookie)
{
	/* Cookie is qedi_ctx struct */
	struct qedi_ctx *qedi = (struct qedi_ctx *)cookie;

	QEDI_WARN(&qedi->dbg_ctx, "qedi=%p.\n", qedi);
}

#define QEDI_SIMD_HANDLER_NUM		0
static void qedi_sync_free_irqs(struct qedi_ctx *qedi)
{
	int i;
	u16 idx;

	if (qedi->int_info.msix_cnt) {
		for (i = 0; i < qedi->int_info.used_cnt; i++) {
			idx = i * qedi->dev_info.common.num_hwfns +
			qedi_ops->common->get_affin_hwfn_idx(qedi->cdev);

			QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
				  "Freeing IRQ #%d vector_idx=%d.\n", i, idx);

			synchronize_irq(qedi->int_info.msix[idx].vector);
			irq_set_affinity_hint(qedi->int_info.msix[idx].vector,
					      NULL);
			free_irq(qedi->int_info.msix[idx].vector,
				 &qedi->fp_array[i]);
		}
	} else {
		qedi_ops->common->simd_handler_clean(qedi->cdev,
						     QEDI_SIMD_HANDLER_NUM);
	}

	qedi->int_info.used_cnt = 0;
	qedi_ops->common->set_fp_int(qedi->cdev, 0);
}

static int qedi_request_msix_irq(struct qedi_ctx *qedi)
{
	int i, rc, cpu;
	u16 idx;

	cpu = cpumask_first(cpu_online_mask);
	for (i = 0; i < qedi->msix_count; i++) {
		idx = i * qedi->dev_info.common.num_hwfns +
			  qedi_ops->common->get_affin_hwfn_idx(qedi->cdev);

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "dev_info: num_hwfns=%d affin_hwfn_idx=%d.\n",
			  qedi->dev_info.common.num_hwfns,
			  qedi_ops->common->get_affin_hwfn_idx(qedi->cdev));

		rc = request_irq(qedi->int_info.msix[idx].vector,
				 qedi_msix_handler, 0, "qedi",
				 &qedi->fp_array[i]);
		if (rc) {
			QEDI_WARN(&qedi->dbg_ctx, "request_irq failed.\n");
			qedi_sync_free_irqs(qedi);
			return rc;
		}
		qedi->int_info.used_cnt++;
		rc = irq_set_affinity_hint(qedi->int_info.msix[idx].vector,
					   get_cpu_mask(cpu));
		cpu = cpumask_next(cpu, cpu_online_mask);
	}

	return 0;
}

static int qedi_setup_int(struct qedi_ctx *qedi)
{
	int rc = 0;

	rc = qedi_ops->common->set_fp_int(qedi->cdev, qedi->num_queues);
	if (rc < 0)
		goto exit_setup_int;

	qedi->msix_count = rc;

	rc = qedi_ops->common->get_fp_int(qedi->cdev, &qedi->int_info);
	if (rc)
		goto exit_setup_int;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
		  "Number of msix_cnt = 0x%x num of cpus = 0x%x\n",
		   qedi->int_info.msix_cnt, num_online_cpus());

	if (qedi->int_info.msix_cnt) {
		rc = qedi_request_msix_irq(qedi);
		goto exit_setup_int;
	} else {
		qedi_ops->common->simd_handler_config(qedi->cdev, &qedi,
						      QEDI_SIMD_HANDLER_NUM,
						      qedi_simd_int_handler);
		qedi->int_info.used_cnt = 1;
	}

exit_setup_int:
	return rc;
}

static void qedi_free_nvm_iscsi_cfg(struct qedi_ctx *qedi)
{
	if (qedi->iscsi_image)
		dma_free_coherent(&qedi->pdev->dev,
				  sizeof(struct qedi_nvm_iscsi_image),
				  qedi->iscsi_image, qedi->nvm_buf_dma);
}

static int qedi_alloc_nvm_iscsi_cfg(struct qedi_ctx *qedi)
{
	qedi->iscsi_image = dma_alloc_coherent(&qedi->pdev->dev,
					       sizeof(struct qedi_nvm_iscsi_image),
					       &qedi->nvm_buf_dma, GFP_KERNEL);
	if (!qedi->iscsi_image) {
		QEDI_ERR(&qedi->dbg_ctx, "Could not allocate NVM BUF.\n");
		return -ENOMEM;
	}
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "NVM BUF addr=0x%p dma=0x%llx.\n", qedi->iscsi_image,
		  qedi->nvm_buf_dma);

	return 0;
}

static void qedi_free_bdq(struct qedi_ctx *qedi)
{
	int i;

	if (qedi->bdq_pbl_list)
		dma_free_coherent(&qedi->pdev->dev, QEDI_PAGE_SIZE,
				  qedi->bdq_pbl_list, qedi->bdq_pbl_list_dma);

	if (qedi->bdq_pbl)
		dma_free_coherent(&qedi->pdev->dev, qedi->bdq_pbl_mem_size,
				  qedi->bdq_pbl, qedi->bdq_pbl_dma);

	for (i = 0; i < QEDI_BDQ_NUM; i++) {
		if (qedi->bdq[i].buf_addr) {
			dma_free_coherent(&qedi->pdev->dev, QEDI_BDQ_BUF_SIZE,
					  qedi->bdq[i].buf_addr,
					  qedi->bdq[i].buf_dma);
		}
	}
}

static void qedi_free_global_queues(struct qedi_ctx *qedi)
{
	int i;
	struct global_queue **gl = qedi->global_queues;

	for (i = 0; i < qedi->num_queues; i++) {
		if (!gl[i])
			continue;

		if (gl[i]->cq)
			dma_free_coherent(&qedi->pdev->dev, gl[i]->cq_mem_size,
					  gl[i]->cq, gl[i]->cq_dma);
		if (gl[i]->cq_pbl)
			dma_free_coherent(&qedi->pdev->dev, gl[i]->cq_pbl_size,
					  gl[i]->cq_pbl, gl[i]->cq_pbl_dma);

		kfree(gl[i]);
	}
	qedi_free_bdq(qedi);
	qedi_free_nvm_iscsi_cfg(qedi);
}

static int qedi_alloc_bdq(struct qedi_ctx *qedi)
{
	int i;
	struct scsi_bd *pbl;
	u64 *list;

	/* Alloc dma memory for BDQ buffers */
	for (i = 0; i < QEDI_BDQ_NUM; i++) {
		qedi->bdq[i].buf_addr =
				dma_alloc_coherent(&qedi->pdev->dev,
						   QEDI_BDQ_BUF_SIZE,
						   &qedi->bdq[i].buf_dma,
						   GFP_KERNEL);
		if (!qedi->bdq[i].buf_addr) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not allocate BDQ buffer %d.\n", i);
			return -ENOMEM;
		}
	}

	/* Alloc dma memory for BDQ page buffer list */
	qedi->bdq_pbl_mem_size = QEDI_BDQ_NUM * sizeof(struct scsi_bd);
	qedi->bdq_pbl_mem_size = ALIGN(qedi->bdq_pbl_mem_size, QEDI_PAGE_SIZE);
	qedi->rq_num_entries = qedi->bdq_pbl_mem_size / sizeof(struct scsi_bd);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN, "rq_num_entries = %d.\n",
		  qedi->rq_num_entries);

	qedi->bdq_pbl = dma_alloc_coherent(&qedi->pdev->dev,
					   qedi->bdq_pbl_mem_size,
					   &qedi->bdq_pbl_dma, GFP_KERNEL);
	if (!qedi->bdq_pbl) {
		QEDI_ERR(&qedi->dbg_ctx, "Could not allocate BDQ PBL.\n");
		return -ENOMEM;
	}

	/*
	 * Populate BDQ PBL with physical and virtual address of individual
	 * BDQ buffers
	 */
	pbl = (struct scsi_bd  *)qedi->bdq_pbl;
	for (i = 0; i < QEDI_BDQ_NUM; i++) {
		pbl->address.hi =
				cpu_to_le32(QEDI_U64_HI(qedi->bdq[i].buf_dma));
		pbl->address.lo =
				cpu_to_le32(QEDI_U64_LO(qedi->bdq[i].buf_dma));
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
			  "pbl [0x%p] pbl->address hi [0x%llx] lo [0x%llx], idx [%d]\n",
			  pbl, pbl->address.hi, pbl->address.lo, i);
		pbl->opaque.iscsi_opaque.reserved_zero[0] = 0;
		pbl->opaque.iscsi_opaque.reserved_zero[1] = 0;
		pbl->opaque.iscsi_opaque.reserved_zero[2] = 0;
		pbl->opaque.iscsi_opaque.opaque = cpu_to_le16(i);
		pbl++;
	}

	/* Allocate list of PBL pages */
	qedi->bdq_pbl_list = dma_alloc_coherent(&qedi->pdev->dev,
						QEDI_PAGE_SIZE,
						&qedi->bdq_pbl_list_dma,
						GFP_KERNEL);
	if (!qedi->bdq_pbl_list) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Could not allocate list of PBL pages.\n");
		return -ENOMEM;
	}

	/*
	 * Now populate PBL list with pages that contain pointers to the
	 * individual buffers.
	 */
	qedi->bdq_pbl_list_num_entries = qedi->bdq_pbl_mem_size /
					 QEDI_PAGE_SIZE;
	list = (u64 *)qedi->bdq_pbl_list;
	for (i = 0; i < qedi->bdq_pbl_list_num_entries; i++) {
		*list = qedi->bdq_pbl_dma;
		list++;
	}

	return 0;
}

static int qedi_alloc_global_queues(struct qedi_ctx *qedi)
{
	u32 *list;
	int i;
	int status;
	u32 *pbl;
	dma_addr_t page;
	int num_pages;

	/*
	 * Number of global queues (CQ / RQ). This should
	 * be <= number of available MSIX vectors for the PF
	 */
	if (!qedi->num_queues) {
		QEDI_ERR(&qedi->dbg_ctx, "No MSI-X vectors available!\n");
		return -ENOMEM;
	}

	/* Make sure we allocated the PBL that will contain the physical
	 * addresses of our queues
	 */
	if (!qedi->p_cpuq) {
		status = -EINVAL;
		goto mem_alloc_failure;
	}

	qedi->global_queues = kzalloc((sizeof(struct global_queue *) *
				       qedi->num_queues), GFP_KERNEL);
	if (!qedi->global_queues) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Unable to allocate global queues array ptr memory\n");
		return -ENOMEM;
	}
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
		  "qedi->global_queues=%p.\n", qedi->global_queues);

	/* Allocate DMA coherent buffers for BDQ */
	status = qedi_alloc_bdq(qedi);
	if (status)
		goto mem_alloc_failure;

	/* Allocate DMA coherent buffers for NVM_ISCSI_CFG */
	status = qedi_alloc_nvm_iscsi_cfg(qedi);
	if (status)
		goto mem_alloc_failure;

	/* Allocate a CQ and an associated PBL for each MSI-X
	 * vector.
	 */
	for (i = 0; i < qedi->num_queues; i++) {
		qedi->global_queues[i] =
					kzalloc(sizeof(*qedi->global_queues[0]),
						GFP_KERNEL);
		if (!qedi->global_queues[i]) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Unable to allocation global queue %d.\n", i);
			status = -ENOMEM;
			goto mem_alloc_failure;
		}

		qedi->global_queues[i]->cq_mem_size =
		    (QEDI_CQ_SIZE + 8) * sizeof(union iscsi_cqe);
		qedi->global_queues[i]->cq_mem_size =
		    (qedi->global_queues[i]->cq_mem_size +
		    (QEDI_PAGE_SIZE - 1));

		qedi->global_queues[i]->cq_pbl_size =
		    (qedi->global_queues[i]->cq_mem_size /
		    QEDI_PAGE_SIZE) * sizeof(void *);
		qedi->global_queues[i]->cq_pbl_size =
		    (qedi->global_queues[i]->cq_pbl_size +
		    (QEDI_PAGE_SIZE - 1));

		qedi->global_queues[i]->cq = dma_alloc_coherent(&qedi->pdev->dev,
								qedi->global_queues[i]->cq_mem_size,
								&qedi->global_queues[i]->cq_dma,
								GFP_KERNEL);

		if (!qedi->global_queues[i]->cq) {
			QEDI_WARN(&qedi->dbg_ctx,
				  "Could not allocate cq.\n");
			status = -ENOMEM;
			goto mem_alloc_failure;
		}
		qedi->global_queues[i]->cq_pbl = dma_alloc_coherent(&qedi->pdev->dev,
								    qedi->global_queues[i]->cq_pbl_size,
								    &qedi->global_queues[i]->cq_pbl_dma,
								    GFP_KERNEL);

		if (!qedi->global_queues[i]->cq_pbl) {
			QEDI_WARN(&qedi->dbg_ctx,
				  "Could not allocate cq PBL.\n");
			status = -ENOMEM;
			goto mem_alloc_failure;
		}

		/* Create PBL */
		num_pages = qedi->global_queues[i]->cq_mem_size /
		    QEDI_PAGE_SIZE;
		page = qedi->global_queues[i]->cq_dma;
		pbl = (u32 *)qedi->global_queues[i]->cq_pbl;

		while (num_pages--) {
			*pbl = (u32)page;
			pbl++;
			*pbl = (u32)((u64)page >> 32);
			pbl++;
			page += QEDI_PAGE_SIZE;
		}
	}

	list = (u32 *)qedi->p_cpuq;

	/*
	 * The list is built as follows: CQ#0 PBL pointer, RQ#0 PBL pointer,
	 * CQ#1 PBL pointer, RQ#1 PBL pointer, etc.  Each PBL pointer points
	 * to the physical address which contains an array of pointers to the
	 * physical addresses of the specific queue pages.
	 */
	for (i = 0; i < qedi->num_queues; i++) {
		*list = (u32)qedi->global_queues[i]->cq_pbl_dma;
		list++;
		*list = (u32)((u64)qedi->global_queues[i]->cq_pbl_dma >> 32);
		list++;

		*list = (u32)0;
		list++;
		*list = (u32)((u64)0 >> 32);
		list++;
	}

	return 0;

mem_alloc_failure:
	qedi_free_global_queues(qedi);
	return status;
}

int qedi_alloc_sq(struct qedi_ctx *qedi, struct qedi_endpoint *ep)
{
	int rval = 0;
	u32 *pbl;
	dma_addr_t page;
	int num_pages;

	if (!ep)
		return -EIO;

	/* Calculate appropriate queue and PBL sizes */
	ep->sq_mem_size = QEDI_SQ_SIZE * sizeof(struct iscsi_wqe);
	ep->sq_mem_size += QEDI_PAGE_SIZE - 1;

	ep->sq_pbl_size = (ep->sq_mem_size / QEDI_PAGE_SIZE) * sizeof(void *);
	ep->sq_pbl_size = ep->sq_pbl_size + QEDI_PAGE_SIZE;

	ep->sq = dma_alloc_coherent(&qedi->pdev->dev, ep->sq_mem_size,
				    &ep->sq_dma, GFP_KERNEL);
	if (!ep->sq) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Could not allocate send queue.\n");
		rval = -ENOMEM;
		goto out;
	}
	ep->sq_pbl = dma_alloc_coherent(&qedi->pdev->dev, ep->sq_pbl_size,
					&ep->sq_pbl_dma, GFP_KERNEL);
	if (!ep->sq_pbl) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Could not allocate send queue PBL.\n");
		rval = -ENOMEM;
		goto out_free_sq;
	}

	/* Create PBL */
	num_pages = ep->sq_mem_size / QEDI_PAGE_SIZE;
	page = ep->sq_dma;
	pbl = (u32 *)ep->sq_pbl;

	while (num_pages--) {
		*pbl = (u32)page;
		pbl++;
		*pbl = (u32)((u64)page >> 32);
		pbl++;
		page += QEDI_PAGE_SIZE;
	}

	return rval;

out_free_sq:
	dma_free_coherent(&qedi->pdev->dev, ep->sq_mem_size, ep->sq,
			  ep->sq_dma);
out:
	return rval;
}

void qedi_free_sq(struct qedi_ctx *qedi, struct qedi_endpoint *ep)
{
	if (ep->sq_pbl)
		dma_free_coherent(&qedi->pdev->dev, ep->sq_pbl_size, ep->sq_pbl,
				  ep->sq_pbl_dma);
	if (ep->sq)
		dma_free_coherent(&qedi->pdev->dev, ep->sq_mem_size, ep->sq,
				  ep->sq_dma);
}

int qedi_get_task_idx(struct qedi_ctx *qedi)
{
	s16 tmp_idx;

again:
	tmp_idx = find_first_zero_bit(qedi->task_idx_map,
				      MAX_ISCSI_TASK_ENTRIES);

	if (tmp_idx >= MAX_ISCSI_TASK_ENTRIES) {
		QEDI_ERR(&qedi->dbg_ctx, "FW task context pool is full.\n");
		tmp_idx = -1;
		goto err_idx;
	}

	if (test_and_set_bit(tmp_idx, qedi->task_idx_map))
		goto again;

err_idx:
	return tmp_idx;
}

void qedi_clear_task_idx(struct qedi_ctx *qedi, int idx)
{
	if (!test_and_clear_bit(idx, qedi->task_idx_map))
		QEDI_ERR(&qedi->dbg_ctx,
			 "FW task context, already cleared, tid=0x%x\n", idx);
}

void qedi_update_itt_map(struct qedi_ctx *qedi, u32 tid, u32 proto_itt,
			 struct qedi_cmd *cmd)
{
	qedi->itt_map[tid].itt = proto_itt;
	qedi->itt_map[tid].p_cmd = cmd;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "update itt map tid=0x%x, with proto itt=0x%x\n", tid,
		  qedi->itt_map[tid].itt);
}

void qedi_get_task_tid(struct qedi_ctx *qedi, u32 itt, s16 *tid)
{
	u16 i;

	for (i = 0; i < MAX_ISCSI_TASK_ENTRIES; i++) {
		if (qedi->itt_map[i].itt == itt) {
			*tid = i;
			QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
				  "Ref itt=0x%x, found at tid=0x%x\n",
				  itt, *tid);
			return;
		}
	}

	WARN_ON(1);
}

void qedi_get_proto_itt(struct qedi_ctx *qedi, u32 tid, u32 *proto_itt)
{
	*proto_itt = qedi->itt_map[tid].itt;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "Get itt map tid [0x%x with proto itt[0x%x]",
		  tid, *proto_itt);
}

struct qedi_cmd *qedi_get_cmd_from_tid(struct qedi_ctx *qedi, u32 tid)
{
	struct qedi_cmd *cmd = NULL;

	if (tid >= MAX_ISCSI_TASK_ENTRIES)
		return NULL;

	cmd = qedi->itt_map[tid].p_cmd;
	if (cmd->task_id != tid)
		return NULL;

	qedi->itt_map[tid].p_cmd = NULL;

	return cmd;
}

static int qedi_alloc_itt(struct qedi_ctx *qedi)
{
	qedi->itt_map = kcalloc(MAX_ISCSI_TASK_ENTRIES,
				sizeof(struct qedi_itt_map), GFP_KERNEL);
	if (!qedi->itt_map) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Unable to allocate itt map array memory\n");
		return -ENOMEM;
	}
	return 0;
}

static void qedi_free_itt(struct qedi_ctx *qedi)
{
	kfree(qedi->itt_map);
}

static struct qed_ll2_cb_ops qedi_ll2_cb_ops = {
	.rx_cb = qedi_ll2_rx,
	.tx_cb = NULL,
};

static int qedi_percpu_io_thread(void *arg)
{
	struct qedi_percpu_s *p = arg;
	struct qedi_work *work, *tmp;
	unsigned long flags;
	LIST_HEAD(work_list);

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		spin_lock_irqsave(&p->p_work_lock, flags);
		while (!list_empty(&p->work_list)) {
			list_splice_init(&p->work_list, &work_list);
			spin_unlock_irqrestore(&p->p_work_lock, flags);

			list_for_each_entry_safe(work, tmp, &work_list, list) {
				list_del_init(&work->list);
				qedi_fp_process_cqes(work);
				if (!work->is_solicited)
					kfree(work);
			}
			cond_resched();
			spin_lock_irqsave(&p->p_work_lock, flags);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&p->p_work_lock, flags);
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

static int qedi_cpu_online(unsigned int cpu)
{
	struct qedi_percpu_s *p = this_cpu_ptr(&qedi_percpu);
	struct task_struct *thread;

	thread = kthread_create_on_node(qedi_percpu_io_thread, (void *)p,
					cpu_to_node(cpu),
					"qedi_thread/%d", cpu);
	if (IS_ERR(thread))
		return PTR_ERR(thread);

	kthread_bind(thread, cpu);
	p->iothread = thread;
	wake_up_process(thread);
	return 0;
}

static int qedi_cpu_offline(unsigned int cpu)
{
	struct qedi_percpu_s *p = this_cpu_ptr(&qedi_percpu);
	struct qedi_work *work, *tmp;
	struct task_struct *thread;
	unsigned long flags;

	spin_lock_irqsave(&p->p_work_lock, flags);
	thread = p->iothread;
	p->iothread = NULL;

	list_for_each_entry_safe(work, tmp, &p->work_list, list) {
		list_del_init(&work->list);
		qedi_fp_process_cqes(work);
		if (!work->is_solicited)
			kfree(work);
	}

	spin_unlock_irqrestore(&p->p_work_lock, flags);
	if (thread)
		kthread_stop(thread);
	return 0;
}

void qedi_reset_host_mtu(struct qedi_ctx *qedi, u16 mtu)
{
	struct qed_ll2_params params;

	qedi_recover_all_conns(qedi);

	qedi_ops->ll2->stop(qedi->cdev);
	qedi_ll2_free_skbs(qedi);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO, "old MTU %u, new MTU %u\n",
		  qedi->ll2_mtu, mtu);
	memset(&params, 0, sizeof(params));
	qedi->ll2_mtu = mtu;
	params.mtu = qedi->ll2_mtu + IPV6_HDR_LEN + TCP_HDR_LEN;
	params.drop_ttl0_packets = 0;
	params.rx_vlan_stripping = 1;
	ether_addr_copy(params.ll2_mac_address, qedi->dev_info.common.hw_mac);
	qedi_ops->ll2->start(qedi->cdev, &params);
}

/*
 * qedi_get_nvram_block: - Scan through the iSCSI NVRAM block (while accounting
 * for gaps) for the matching absolute-pf-id of the QEDI device.
 */
static struct nvm_iscsi_block *
qedi_get_nvram_block(struct qedi_ctx *qedi)
{
	int i;
	u8 pf;
	u32 flags;
	struct nvm_iscsi_block *block;

	pf = qedi->dev_info.common.abs_pf_id;
	block = &qedi->iscsi_image->iscsi_cfg.block[0];
	for (i = 0; i < NUM_OF_ISCSI_PF_SUPPORTED; i++, block++) {
		flags = ((block->id) & NVM_ISCSI_CFG_BLK_CTRL_FLAG_MASK) >>
			NVM_ISCSI_CFG_BLK_CTRL_FLAG_OFFSET;
		if (flags & (NVM_ISCSI_CFG_BLK_CTRL_FLAG_IS_NOT_EMPTY |
				NVM_ISCSI_CFG_BLK_CTRL_FLAG_PF_MAPPED) &&
			(pf == (block->id & NVM_ISCSI_CFG_BLK_MAPPED_PF_ID_MASK)
				>> NVM_ISCSI_CFG_BLK_MAPPED_PF_ID_OFFSET))
			return block;
	}
	return NULL;
}

static ssize_t qedi_show_boot_eth_info(void *data, int type, char *buf)
{
	struct qedi_ctx *qedi = data;
	struct nvm_iscsi_initiator *initiator;
	int rc = 1;
	u32 ipv6_en, dhcp_en, ip_len;
	struct nvm_iscsi_block *block;
	char *fmt, *ip, *sub, *gw;

	block = qedi_get_nvram_block(qedi);
	if (!block)
		return 0;

	initiator = &block->initiator;
	ipv6_en = block->generic.ctrl_flags &
		  NVM_ISCSI_CFG_GEN_IPV6_ENABLED;
	dhcp_en = block->generic.ctrl_flags &
		  NVM_ISCSI_CFG_GEN_DHCP_TCPIP_CONFIG_ENABLED;
	/* Static IP assignments. */
	fmt = ipv6_en ? "%pI6\n" : "%pI4\n";
	ip = ipv6_en ? initiator->ipv6.addr.byte : initiator->ipv4.addr.byte;
	ip_len = ipv6_en ? IPV6_LEN : IPV4_LEN;
	sub = ipv6_en ? initiator->ipv6.subnet_mask.byte :
	      initiator->ipv4.subnet_mask.byte;
	gw = ipv6_en ? initiator->ipv6.gateway.byte :
	     initiator->ipv4.gateway.byte;
	/* DHCP IP adjustments. */
	fmt = dhcp_en ? "%s\n" : fmt;
	if (dhcp_en) {
		ip = ipv6_en ? "0::0" : "0.0.0.0";
		sub = ip;
		gw = ip;
		ip_len = ipv6_en ? 5 : 8;
	}

	switch (type) {
	case ISCSI_BOOT_ETH_IP_ADDR:
		rc = snprintf(buf, ip_len, fmt, ip);
		break;
	case ISCSI_BOOT_ETH_SUBNET_MASK:
		rc = snprintf(buf, ip_len, fmt, sub);
		break;
	case ISCSI_BOOT_ETH_GATEWAY:
		rc = snprintf(buf, ip_len, fmt, gw);
		break;
	case ISCSI_BOOT_ETH_FLAGS:
		rc = snprintf(buf, 3, "%d\n", (char)SYSFS_FLAG_FW_SEL_BOOT);
		break;
	case ISCSI_BOOT_ETH_INDEX:
		rc = snprintf(buf, 3, "0\n");
		break;
	case ISCSI_BOOT_ETH_MAC:
		rc = sysfs_format_mac(buf, qedi->mac, ETH_ALEN);
		break;
	case ISCSI_BOOT_ETH_VLAN:
		rc = snprintf(buf, 12, "%d\n",
			      GET_FIELD2(initiator->generic_cont0,
					 NVM_ISCSI_CFG_INITIATOR_VLAN));
		break;
	case ISCSI_BOOT_ETH_ORIGIN:
		if (dhcp_en)
			rc = snprintf(buf, 3, "3\n");
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static umode_t qedi_eth_get_attr_visibility(void *data, int type)
{
	int rc = 1;

	switch (type) {
	case ISCSI_BOOT_ETH_FLAGS:
	case ISCSI_BOOT_ETH_MAC:
	case ISCSI_BOOT_ETH_INDEX:
	case ISCSI_BOOT_ETH_IP_ADDR:
	case ISCSI_BOOT_ETH_SUBNET_MASK:
	case ISCSI_BOOT_ETH_GATEWAY:
	case ISCSI_BOOT_ETH_ORIGIN:
	case ISCSI_BOOT_ETH_VLAN:
		rc = 0444;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static ssize_t qedi_show_boot_ini_info(void *data, int type, char *buf)
{
	struct qedi_ctx *qedi = data;
	struct nvm_iscsi_initiator *initiator;
	int rc;
	struct nvm_iscsi_block *block;

	block = qedi_get_nvram_block(qedi);
	if (!block)
		return 0;

	initiator = &block->initiator;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_ISCSI_NAME_MAX_LEN,
			     initiator->initiator_name.byte);
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static umode_t qedi_ini_get_attr_visibility(void *data, int type)
{
	int rc;

	switch (type) {
	case ISCSI_BOOT_INI_INITIATOR_NAME:
		rc = 0444;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static ssize_t
qedi_show_boot_tgt_info(struct qedi_ctx *qedi, int type,
			char *buf, enum qedi_nvm_tgts idx)
{
	int rc = 1;
	u32 ctrl_flags, ipv6_en, chap_en, mchap_en, ip_len;
	struct nvm_iscsi_block *block;
	char *chap_name, *chap_secret;
	char *mchap_name, *mchap_secret;

	block = qedi_get_nvram_block(qedi);
	if (!block)
		goto exit_show_tgt_info;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_EVT,
		  "Port:%d, tgt_idx:%d\n",
		  GET_FIELD2(block->id, NVM_ISCSI_CFG_BLK_MAPPED_PF_ID), idx);

	ctrl_flags = block->target[idx].ctrl_flags &
		     NVM_ISCSI_CFG_TARGET_ENABLED;

	if (!ctrl_flags) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_EVT,
			  "Target disabled\n");
		goto exit_show_tgt_info;
	}

	ipv6_en = block->generic.ctrl_flags &
		  NVM_ISCSI_CFG_GEN_IPV6_ENABLED;
	ip_len = ipv6_en ? IPV6_LEN : IPV4_LEN;
	chap_en = block->generic.ctrl_flags &
		  NVM_ISCSI_CFG_GEN_CHAP_ENABLED;
	chap_name = chap_en ? block->initiator.chap_name.byte : NULL;
	chap_secret = chap_en ? block->initiator.chap_password.byte : NULL;

	mchap_en = block->generic.ctrl_flags &
		  NVM_ISCSI_CFG_GEN_CHAP_MUTUAL_ENABLED;
	mchap_name = mchap_en ? block->target[idx].chap_name.byte : NULL;
	mchap_secret = mchap_en ? block->target[idx].chap_password.byte : NULL;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_ISCSI_NAME_MAX_LEN,
			     block->target[idx].target_name.byte);
		break;
	case ISCSI_BOOT_TGT_IP_ADDR:
		if (ipv6_en)
			rc = snprintf(buf, ip_len, "%pI6\n",
				      block->target[idx].ipv6_addr.byte);
		else
			rc = snprintf(buf, ip_len, "%pI4\n",
				      block->target[idx].ipv4_addr.byte);
		break;
	case ISCSI_BOOT_TGT_PORT:
		rc = snprintf(buf, 12, "%d\n",
			      GET_FIELD2(block->target[idx].generic_cont0,
					 NVM_ISCSI_CFG_TARGET_TCP_PORT));
		break;
	case ISCSI_BOOT_TGT_LUN:
		rc = snprintf(buf, 22, "%.*d\n",
			      block->target[idx].lun.value[1],
			      block->target[idx].lun.value[0]);
		break;
	case ISCSI_BOOT_TGT_CHAP_NAME:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_CHAP_NAME_MAX_LEN,
			     chap_name);
		break;
	case ISCSI_BOOT_TGT_CHAP_SECRET:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_CHAP_PWD_MAX_LEN,
			     chap_secret);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_CHAP_NAME_MAX_LEN,
			     mchap_name);
		break;
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
		rc = sprintf(buf, "%.*s\n", NVM_ISCSI_CFG_CHAP_PWD_MAX_LEN,
			     mchap_secret);
		break;
	case ISCSI_BOOT_TGT_FLAGS:
		rc = snprintf(buf, 3, "%d\n", (char)SYSFS_FLAG_FW_SEL_BOOT);
		break;
	case ISCSI_BOOT_TGT_NIC_ASSOC:
		rc = snprintf(buf, 3, "0\n");
		break;
	default:
		rc = 0;
		break;
	}

exit_show_tgt_info:
	return rc;
}

static ssize_t qedi_show_boot_tgt_pri_info(void *data, int type, char *buf)
{
	struct qedi_ctx *qedi = data;

	return qedi_show_boot_tgt_info(qedi, type, buf, QEDI_NVM_TGT_PRI);
}

static ssize_t qedi_show_boot_tgt_sec_info(void *data, int type, char *buf)
{
	struct qedi_ctx *qedi = data;

	return qedi_show_boot_tgt_info(qedi, type, buf, QEDI_NVM_TGT_SEC);
}

static umode_t qedi_tgt_get_attr_visibility(void *data, int type)
{
	int rc;

	switch (type) {
	case ISCSI_BOOT_TGT_NAME:
	case ISCSI_BOOT_TGT_IP_ADDR:
	case ISCSI_BOOT_TGT_PORT:
	case ISCSI_BOOT_TGT_LUN:
	case ISCSI_BOOT_TGT_CHAP_NAME:
	case ISCSI_BOOT_TGT_CHAP_SECRET:
	case ISCSI_BOOT_TGT_REV_CHAP_NAME:
	case ISCSI_BOOT_TGT_REV_CHAP_SECRET:
	case ISCSI_BOOT_TGT_NIC_ASSOC:
	case ISCSI_BOOT_TGT_FLAGS:
		rc = 0444;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static void qedi_boot_release(void *data)
{
	struct qedi_ctx *qedi = data;

	scsi_host_put(qedi->shost);
}

static int qedi_get_boot_info(struct qedi_ctx *qedi)
{
	int ret = 1;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Get NVM iSCSI CFG image\n");
	ret = qedi_ops->common->nvm_get_image(qedi->cdev,
					      QED_NVM_IMAGE_ISCSI_CFG,
					      (char *)qedi->iscsi_image,
					      sizeof(struct qedi_nvm_iscsi_image));
	if (ret)
		QEDI_ERR(&qedi->dbg_ctx,
			 "Could not get NVM image. ret = %d\n", ret);

	return ret;
}

static int qedi_setup_boot_info(struct qedi_ctx *qedi)
{
	struct iscsi_boot_kobj *boot_kobj;

	if (qedi_get_boot_info(qedi))
		return -EPERM;

	qedi->boot_kset = iscsi_boot_create_host_kset(qedi->shost->host_no);
	if (!qedi->boot_kset)
		goto kset_free;

	if (!scsi_host_get(qedi->shost))
		goto kset_free;

	boot_kobj = iscsi_boot_create_target(qedi->boot_kset, 0, qedi,
					     qedi_show_boot_tgt_pri_info,
					     qedi_tgt_get_attr_visibility,
					     qedi_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(qedi->shost))
		goto kset_free;

	boot_kobj = iscsi_boot_create_target(qedi->boot_kset, 1, qedi,
					     qedi_show_boot_tgt_sec_info,
					     qedi_tgt_get_attr_visibility,
					     qedi_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(qedi->shost))
		goto kset_free;

	boot_kobj = iscsi_boot_create_initiator(qedi->boot_kset, 0, qedi,
						qedi_show_boot_ini_info,
						qedi_ini_get_attr_visibility,
						qedi_boot_release);
	if (!boot_kobj)
		goto put_host;

	if (!scsi_host_get(qedi->shost))
		goto kset_free;

	boot_kobj = iscsi_boot_create_ethernet(qedi->boot_kset, 0, qedi,
					       qedi_show_boot_eth_info,
					       qedi_eth_get_attr_visibility,
					       qedi_boot_release);
	if (!boot_kobj)
		goto put_host;

	return 0;

put_host:
	scsi_host_put(qedi->shost);
kset_free:
	iscsi_boot_destroy_kset(qedi->boot_kset);
	return -ENOMEM;
}

static pci_ers_result_t qedi_io_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct qedi_ctx *qedi = pci_get_drvdata(pdev);

	QEDI_ERR(&qedi->dbg_ctx, "%s: PCI error detected [%d]\n",
		 __func__, state);

	if (test_and_set_bit(QEDI_IN_RECOVERY, &qedi->flags)) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "Recovery already in progress.\n");
		return PCI_ERS_RESULT_NONE;
	}

	qedi_ops->common->recovery_process(qedi->cdev);

	return PCI_ERS_RESULT_CAN_RECOVER;
}

static void __qedi_remove(struct pci_dev *pdev, int mode)
{
	struct qedi_ctx *qedi = pci_get_drvdata(pdev);
	int rval;
	u16 retry = 10;

	if (mode == QEDI_MODE_NORMAL)
		iscsi_host_remove(qedi->shost, false);
	else if (mode == QEDI_MODE_SHUTDOWN)
		iscsi_host_remove(qedi->shost, true);

	if (mode == QEDI_MODE_NORMAL || mode == QEDI_MODE_SHUTDOWN) {
		if (qedi->tmf_thread) {
			destroy_workqueue(qedi->tmf_thread);
			qedi->tmf_thread = NULL;
		}

		if (qedi->offload_thread) {
			destroy_workqueue(qedi->offload_thread);
			qedi->offload_thread = NULL;
		}
	}

#ifdef CONFIG_DEBUG_FS
	qedi_dbg_host_exit(&qedi->dbg_ctx);
#endif
	if (!test_bit(QEDI_IN_OFFLINE, &qedi->flags))
		qedi_ops->common->set_power_state(qedi->cdev, PCI_D0);

	qedi_sync_free_irqs(qedi);

	if (!test_bit(QEDI_IN_OFFLINE, &qedi->flags)) {
		while (retry--) {
			rval = qedi_ops->stop(qedi->cdev);
			if (rval < 0)
				msleep(1000);
			else
				break;
		}
		qedi_ops->ll2->stop(qedi->cdev);
	}

	cancel_delayed_work_sync(&qedi->recovery_work);
	cancel_delayed_work_sync(&qedi->board_disable_work);

	qedi_free_iscsi_pf_param(qedi);

	rval = qedi_ops->common->update_drv_state(qedi->cdev, false);
	if (rval)
		QEDI_ERR(&qedi->dbg_ctx, "Failed to send drv state to MFW\n");

	if (!test_bit(QEDI_IN_OFFLINE, &qedi->flags)) {
		qedi_ops->common->slowpath_stop(qedi->cdev);
		qedi_ops->common->remove(qedi->cdev);
	}

	qedi_destroy_fp(qedi);

	if (mode == QEDI_MODE_NORMAL || mode == QEDI_MODE_SHUTDOWN) {
		qedi_release_cid_que(qedi);
		qedi_cm_free_mem(qedi);
		qedi_free_uio(qedi->udev);
		qedi_free_itt(qedi);

		if (qedi->ll2_recv_thread) {
			kthread_stop(qedi->ll2_recv_thread);
			qedi->ll2_recv_thread = NULL;
		}
		qedi_ll2_free_skbs(qedi);

		if (qedi->boot_kset)
			iscsi_boot_destroy_kset(qedi->boot_kset);

		iscsi_host_free(qedi->shost);
	}
}

static void qedi_board_disable_work(struct work_struct *work)
{
	struct qedi_ctx *qedi =
			container_of(work, struct qedi_ctx,
				     board_disable_work.work);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Fan failure, Unloading firmware context.\n");

	if (test_and_set_bit(QEDI_IN_SHUTDOWN, &qedi->flags))
		return;

	__qedi_remove(qedi->pdev, QEDI_MODE_NORMAL);
}

static void qedi_shutdown(struct pci_dev *pdev)
{
	struct qedi_ctx *qedi = pci_get_drvdata(pdev);

	QEDI_ERR(&qedi->dbg_ctx, "%s: Shutdown qedi\n", __func__);
	if (test_and_set_bit(QEDI_IN_SHUTDOWN, &qedi->flags))
		return;
	__qedi_remove(pdev, QEDI_MODE_SHUTDOWN);
}

static int qedi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct qedi_ctx *qedi;

	if (!pdev) {
		QEDI_ERR(NULL, "pdev is NULL.\n");
		return -ENODEV;
	}

	qedi = pci_get_drvdata(pdev);

	QEDI_ERR(&qedi->dbg_ctx, "%s: Device does not support suspend operation\n", __func__);

	return -EPERM;
}

static int __qedi_probe(struct pci_dev *pdev, int mode)
{
	struct qedi_ctx *qedi;
	struct qed_ll2_params params;
	u8 dp_level = 0;
	bool is_vf = false;
	char host_buf[16];
	struct qed_link_params link_params;
	struct qed_slowpath_params sp_params;
	struct qed_probe_params qed_params;
	void *task_start, *task_end;
	int rc;
	u16 retry = 10;

	if (mode != QEDI_MODE_RECOVERY) {
		qedi = qedi_host_alloc(pdev);
		if (!qedi) {
			rc = -ENOMEM;
			goto exit_probe;
		}
	} else {
		qedi = pci_get_drvdata(pdev);
	}

retry_probe:
	if (mode == QEDI_MODE_RECOVERY)
		msleep(2000);

	memset(&qed_params, 0, sizeof(qed_params));
	qed_params.protocol = QED_PROTOCOL_ISCSI;
	qed_params.dp_module = qedi_qed_debug;
	qed_params.dp_level = dp_level;
	qed_params.is_vf = is_vf;
	qedi->cdev = qedi_ops->common->probe(pdev, &qed_params);
	if (!qedi->cdev) {
		if (mode == QEDI_MODE_RECOVERY && retry) {
			QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
				  "Retry %d initialize hardware\n", retry);
			retry--;
			goto retry_probe;
		}

		rc = -ENODEV;
		QEDI_ERR(&qedi->dbg_ctx, "Cannot initialize hardware\n");
		goto free_host;
	}

	set_bit(QEDI_ERR_ATTN_CLR_EN, &qedi->qedi_err_flags);
	set_bit(QEDI_ERR_IS_RECOVERABLE, &qedi->qedi_err_flags);
	atomic_set(&qedi->link_state, QEDI_LINK_DOWN);

	rc = qedi_ops->fill_dev_info(qedi->cdev, &qedi->dev_info);
	if (rc)
		goto free_host;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "dev_info: num_hwfns=%d affin_hwfn_idx=%d.\n",
		  qedi->dev_info.common.num_hwfns,
		  qedi_ops->common->get_affin_hwfn_idx(qedi->cdev));

	rc = qedi_set_iscsi_pf_param(qedi);
	if (rc) {
		rc = -ENOMEM;
		QEDI_ERR(&qedi->dbg_ctx,
			 "Set iSCSI pf param fail\n");
		goto free_host;
	}

	qedi_ops->common->update_pf_params(qedi->cdev, &qedi->pf_params);

	rc = qedi_prepare_fp(qedi);
	if (rc) {
		QEDI_ERR(&qedi->dbg_ctx, "Cannot start slowpath.\n");
		goto free_pf_params;
	}

	/* Start the Slowpath-process */
	memset(&sp_params, 0, sizeof(struct qed_slowpath_params));
	sp_params.int_mode = QED_INT_MODE_MSIX;
	sp_params.drv_major = QEDI_DRIVER_MAJOR_VER;
	sp_params.drv_minor = QEDI_DRIVER_MINOR_VER;
	sp_params.drv_rev = QEDI_DRIVER_REV_VER;
	sp_params.drv_eng = QEDI_DRIVER_ENG_VER;
	strscpy(sp_params.name, "qedi iSCSI", QED_DRV_VER_STR_SIZE);
	rc = qedi_ops->common->slowpath_start(qedi->cdev, &sp_params);
	if (rc) {
		QEDI_ERR(&qedi->dbg_ctx, "Cannot start slowpath\n");
		goto stop_hw;
	}

	/* update_pf_params needs to be called before and after slowpath
	 * start
	 */
	qedi_ops->common->update_pf_params(qedi->cdev, &qedi->pf_params);

	rc = qedi_setup_int(qedi);
	if (rc)
		goto stop_iscsi_func;

	qedi_ops->common->set_power_state(qedi->cdev, PCI_D0);

	/* Learn information crucial for qedi to progress */
	rc = qedi_ops->fill_dev_info(qedi->cdev, &qedi->dev_info);
	if (rc)
		goto stop_iscsi_func;

	/* Record BDQ producer doorbell addresses */
	qedi->bdq_primary_prod = qedi->dev_info.primary_dbq_rq_addr;
	qedi->bdq_secondary_prod = qedi->dev_info.secondary_bdq_rq_addr;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
		  "BDQ primary_prod=%p secondary_prod=%p.\n",
		  qedi->bdq_primary_prod,
		  qedi->bdq_secondary_prod);

	/*
	 * We need to write the number of BDs in the BDQ we've preallocated so
	 * the f/w will do a prefetch and we'll get an unsolicited CQE when a
	 * packet arrives.
	 */
	qedi->bdq_prod_idx = QEDI_BDQ_NUM;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
		  "Writing %d to primary and secondary BDQ doorbell registers.\n",
		  qedi->bdq_prod_idx);
	writew(qedi->bdq_prod_idx, qedi->bdq_primary_prod);
	readw(qedi->bdq_primary_prod);
	writew(qedi->bdq_prod_idx, qedi->bdq_secondary_prod);
	readw(qedi->bdq_secondary_prod);

	ether_addr_copy(qedi->mac, qedi->dev_info.common.hw_mac);
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC, "MAC address is %pM.\n",
		  qedi->mac);

	snprintf(host_buf, sizeof(host_buf), "host_%d", qedi->shost->host_no);
	qedi_ops->common->set_name(qedi->cdev, host_buf);

	qedi_ops->register_ops(qedi->cdev, &qedi_cb_ops, qedi);

	memset(&params, 0, sizeof(params));
	params.mtu = DEF_PATH_MTU + IPV6_HDR_LEN + TCP_HDR_LEN;
	qedi->ll2_mtu = DEF_PATH_MTU;
	params.drop_ttl0_packets = 0;
	params.rx_vlan_stripping = 1;
	ether_addr_copy(params.ll2_mac_address, qedi->dev_info.common.hw_mac);

	if (mode != QEDI_MODE_RECOVERY) {
		/* set up rx path */
		INIT_LIST_HEAD(&qedi->ll2_skb_list);
		spin_lock_init(&qedi->ll2_lock);
		/* start qedi context */
		spin_lock_init(&qedi->hba_lock);
		spin_lock_init(&qedi->task_idx_lock);
		mutex_init(&qedi->stats_lock);
	}
	qedi_ops->ll2->register_cb_ops(qedi->cdev, &qedi_ll2_cb_ops, qedi);
	qedi_ops->ll2->start(qedi->cdev, &params);

	if (mode != QEDI_MODE_RECOVERY) {
		qedi->ll2_recv_thread = kthread_run(qedi_ll2_recv_thread,
						    (void *)qedi,
						    "qedi_ll2_thread");
	}

	rc = qedi_ops->start(qedi->cdev, &qedi->tasks,
			     qedi, qedi_iscsi_event_cb);
	if (rc) {
		rc = -ENODEV;
		QEDI_ERR(&qedi->dbg_ctx, "Cannot start iSCSI function\n");
		goto stop_slowpath;
	}

	task_start = qedi_get_task_mem(&qedi->tasks, 0);
	task_end = qedi_get_task_mem(&qedi->tasks, MAX_TID_BLOCKS_ISCSI - 1);
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_DISC,
		  "Task context start=%p, end=%p block_size=%u.\n",
		   task_start, task_end, qedi->tasks.size);

	memset(&link_params, 0, sizeof(link_params));
	link_params.link_up = true;
	rc = qedi_ops->common->set_link(qedi->cdev, &link_params);
	if (rc) {
		QEDI_WARN(&qedi->dbg_ctx, "Link set up failed.\n");
		atomic_set(&qedi->link_state, QEDI_LINK_DOWN);
	}

#ifdef CONFIG_DEBUG_FS
	qedi_dbg_host_init(&qedi->dbg_ctx, qedi_debugfs_ops,
			   qedi_dbg_fops);
#endif
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "QLogic FastLinQ iSCSI Module qedi %s, FW %d.%d.%d.%d\n",
		  QEDI_MODULE_VERSION, FW_MAJOR_VERSION, FW_MINOR_VERSION,
		  FW_REVISION_VERSION, FW_ENGINEERING_VERSION);

	if (mode == QEDI_MODE_NORMAL) {
		if (iscsi_host_add(qedi->shost, &pdev->dev)) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not add iscsi host\n");
			rc = -ENOMEM;
			goto remove_host;
		}

		/* Allocate uio buffers */
		rc = qedi_alloc_uio_rings(qedi);
		if (rc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "UIO alloc ring failed err=%d\n", rc);
			goto remove_host;
		}

		rc = qedi_init_uio(qedi);
		if (rc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "UIO init failed, err=%d\n", rc);
			goto free_uio;
		}

		/* host the array on iscsi_conn */
		rc = qedi_setup_cid_que(qedi);
		if (rc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not setup cid que\n");
			goto free_uio;
		}

		rc = qedi_cm_alloc_mem(qedi);
		if (rc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not alloc cm memory\n");
			goto free_cid_que;
		}

		rc = qedi_alloc_itt(qedi);
		if (rc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not alloc itt memory\n");
			goto free_cid_que;
		}

		sprintf(host_buf, "host_%d", qedi->shost->host_no);
		qedi->tmf_thread =
			alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, host_buf);
		if (!qedi->tmf_thread) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Unable to start tmf thread!\n");
			rc = -ENODEV;
			goto free_cid_que;
		}

		qedi->offload_thread = alloc_workqueue("qedi_ofld%d",
						       WQ_MEM_RECLAIM,
						       1, qedi->shost->host_no);
		if (!qedi->offload_thread) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Unable to start offload thread!\n");
			rc = -ENODEV;
			goto free_tmf_thread;
		}

		INIT_DELAYED_WORK(&qedi->recovery_work, qedi_recovery_handler);
		INIT_DELAYED_WORK(&qedi->board_disable_work,
				  qedi_board_disable_work);

		/* F/w needs 1st task context memory entry for performance */
		set_bit(QEDI_RESERVE_TASK_ID, qedi->task_idx_map);
		atomic_set(&qedi->num_offloads, 0);

		if (qedi_setup_boot_info(qedi))
			QEDI_ERR(&qedi->dbg_ctx,
				 "No iSCSI boot target configured\n");

		rc = qedi_ops->common->update_drv_state(qedi->cdev, true);
		if (rc)
			QEDI_ERR(&qedi->dbg_ctx,
				 "Failed to send drv state to MFW\n");

	}

	return 0;

free_tmf_thread:
	destroy_workqueue(qedi->tmf_thread);
free_cid_que:
	qedi_release_cid_que(qedi);
free_uio:
	qedi_free_uio(qedi->udev);
remove_host:
#ifdef CONFIG_DEBUG_FS
	qedi_dbg_host_exit(&qedi->dbg_ctx);
#endif
	iscsi_host_remove(qedi->shost, false);
stop_iscsi_func:
	qedi_ops->stop(qedi->cdev);
stop_slowpath:
	qedi_ops->common->slowpath_stop(qedi->cdev);
stop_hw:
	qedi_ops->common->remove(qedi->cdev);
free_pf_params:
	qedi_free_iscsi_pf_param(qedi);
free_host:
	iscsi_host_free(qedi->shost);
exit_probe:
	return rc;
}

static void qedi_mark_conn_recovery(struct iscsi_cls_session *cls_session)
{
	struct iscsi_session *session = cls_session->dd_data;
	struct iscsi_conn *conn = session->leadconn;
	struct qedi_conn *qedi_conn = conn->dd_data;

	iscsi_conn_failure(qedi_conn->cls_conn->dd_data, ISCSI_ERR_CONN_FAILED);
}

static void qedi_recovery_handler(struct work_struct *work)
{
	struct qedi_ctx *qedi =
			container_of(work, struct qedi_ctx, recovery_work.work);

	iscsi_host_for_each_session(qedi->shost, qedi_mark_conn_recovery);

	/* Call common_ops->recovery_prolog to allow the MFW to quiesce
	 * any PCI transactions.
	 */
	qedi_ops->common->recovery_prolog(qedi->cdev);

	__qedi_remove(qedi->pdev, QEDI_MODE_RECOVERY);
	__qedi_probe(qedi->pdev, QEDI_MODE_RECOVERY);
	clear_bit(QEDI_IN_RECOVERY, &qedi->flags);
}

static int qedi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return __qedi_probe(pdev, QEDI_MODE_NORMAL);
}

static void qedi_remove(struct pci_dev *pdev)
{
	__qedi_remove(pdev, QEDI_MODE_NORMAL);
}

static struct pci_device_id qedi_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, 0x165E) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, 0x8084) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, qedi_pci_tbl);

static enum cpuhp_state qedi_cpuhp_state;

static struct pci_error_handlers qedi_err_handler = {
	.error_detected = qedi_io_error_detected,
};

static struct pci_driver qedi_pci_driver = {
	.name = QEDI_MODULE_NAME,
	.id_table = qedi_pci_tbl,
	.probe = qedi_probe,
	.remove = qedi_remove,
	.shutdown = qedi_shutdown,
	.err_handler = &qedi_err_handler,
	.suspend = qedi_suspend,
};

static int __init qedi_init(void)
{
	struct qedi_percpu_s *p;
	int cpu, rc = 0;

	qedi_ops = qed_get_iscsi_ops();
	if (!qedi_ops) {
		QEDI_ERR(NULL, "Failed to get qed iSCSI operations\n");
		return -EINVAL;
	}

#ifdef CONFIG_DEBUG_FS
	qedi_dbg_init("qedi");
#endif

	qedi_scsi_transport = iscsi_register_transport(&qedi_iscsi_transport);
	if (!qedi_scsi_transport) {
		QEDI_ERR(NULL, "Could not register qedi transport");
		rc = -ENOMEM;
		goto exit_qedi_init_1;
	}

	for_each_possible_cpu(cpu) {
		p = &per_cpu(qedi_percpu, cpu);
		INIT_LIST_HEAD(&p->work_list);
		spin_lock_init(&p->p_work_lock);
		p->iothread = NULL;
	}

	rc = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "scsi/qedi:online",
			       qedi_cpu_online, qedi_cpu_offline);
	if (rc < 0)
		goto exit_qedi_init_2;
	qedi_cpuhp_state = rc;

	rc = pci_register_driver(&qedi_pci_driver);
	if (rc) {
		QEDI_ERR(NULL, "Failed to register driver\n");
		goto exit_qedi_hp;
	}

	return 0;

exit_qedi_hp:
	cpuhp_remove_state(qedi_cpuhp_state);
exit_qedi_init_2:
	iscsi_unregister_transport(&qedi_iscsi_transport);
exit_qedi_init_1:
#ifdef CONFIG_DEBUG_FS
	qedi_dbg_exit();
#endif
	qed_put_iscsi_ops();
	return rc;
}

static void __exit qedi_cleanup(void)
{
	pci_unregister_driver(&qedi_pci_driver);
	cpuhp_remove_state(qedi_cpuhp_state);
	iscsi_unregister_transport(&qedi_iscsi_transport);

#ifdef CONFIG_DEBUG_FS
	qedi_dbg_exit();
#endif
	qed_put_iscsi_ops();
}

MODULE_DESCRIPTION("QLogic FastLinQ 4xxxx iSCSI Module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("QLogic Corporation");
MODULE_VERSION(QEDI_MODULE_VERSION);
module_init(qedi_init);
module_exit(qedi_cleanup);
