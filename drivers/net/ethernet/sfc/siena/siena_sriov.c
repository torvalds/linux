// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2010-2012 Solarflare Communications Inc.
 */
#include <linux/pci.h>
#include <linux/module.h>
#include "net_driver.h"
#include "efx.h"
#include "efx_channels.h"
#include "nic.h"
#include "io.h"
#include "mcdi.h"
#include "filter.h"
#include "mcdi_pcol.h"
#include "farch_regs.h"
#include "siena_sriov.h"
#include "vfdi.h"

/* Number of longs required to track all the VIs in a VF */
#define VI_MASK_LENGTH BITS_TO_LONGS(1 << EFX_VI_SCALE_MAX)

/* Maximum number of RX queues supported */
#define VF_MAX_RX_QUEUES 63

/**
 * enum efx_vf_tx_filter_mode - TX MAC filtering behaviour
 * @VF_TX_FILTER_OFF: Disabled
 * @VF_TX_FILTER_AUTO: Enabled if MAC address assigned to VF and only
 *	2 TX queues allowed per VF.
 * @VF_TX_FILTER_ON: Enabled
 */
enum efx_vf_tx_filter_mode {
	VF_TX_FILTER_OFF,
	VF_TX_FILTER_AUTO,
	VF_TX_FILTER_ON,
};

/**
 * struct siena_vf - Back-end resource and protocol state for a PCI VF
 * @efx: The Efx NIC owning this VF
 * @pci_rid: The PCI requester ID for this VF
 * @pci_name: The PCI name (formatted address) of this VF
 * @index: Index of VF within its port and PF.
 * @req: VFDI incoming request work item. Incoming USR_EV events are received
 *	by the NAPI handler, but must be handled by executing MCDI requests
 *	inside a work item.
 * @req_addr: VFDI incoming request DMA address (in VF's PCI address space).
 * @req_type: Expected next incoming (from VF) %VFDI_EV_TYPE member.
 * @req_seqno: Expected next incoming (from VF) %VFDI_EV_SEQ member.
 * @msg_seqno: Next %VFDI_EV_SEQ member to reply to VF. Protected by
 *	@status_lock
 * @busy: VFDI request queued to be processed or being processed. Receiving
 *	a VFDI request when @busy is set is an error condition.
 * @buf: Incoming VFDI requests are DMA from the VF into this buffer.
 * @buftbl_base: Buffer table entries for this VF start at this index.
 * @rx_filtering: Receive filtering has been requested by the VF driver.
 * @rx_filter_flags: The flags sent in the %VFDI_OP_INSERT_FILTER request.
 * @rx_filter_qid: VF relative qid for RX filter requested by VF.
 * @rx_filter_id: Receive MAC filter ID. Only one filter per VF is supported.
 * @tx_filter_mode: Transmit MAC filtering mode.
 * @tx_filter_id: Transmit MAC filter ID.
 * @addr: The MAC address and outer vlan tag of the VF.
 * @status_addr: VF DMA address of page for &struct vfdi_status updates.
 * @status_lock: Mutex protecting @msg_seqno, @status_addr, @addr,
 *	@peer_page_addrs and @peer_page_count from simultaneous
 *	updates by the VM and consumption by
 *	efx_siena_sriov_update_vf_addr()
 * @peer_page_addrs: Pointer to an array of guest pages for local addresses.
 * @peer_page_count: Number of entries in @peer_page_count.
 * @evq0_addrs: Array of guest pages backing evq0.
 * @evq0_count: Number of entries in @evq0_addrs.
 * @flush_waitq: wait queue used by %VFDI_OP_FINI_ALL_QUEUES handler
 *	to wait for flush completions.
 * @txq_lock: Mutex for TX queue allocation.
 * @txq_mask: Mask of initialized transmit queues.
 * @txq_count: Number of initialized transmit queues.
 * @rxq_mask: Mask of initialized receive queues.
 * @rxq_count: Number of initialized receive queues.
 * @rxq_retry_mask: Mask or receive queues that need to be flushed again
 *	due to flush failure.
 * @rxq_retry_count: Number of receive queues in @rxq_retry_mask.
 * @reset_work: Work item to schedule a VF reset.
 */
struct siena_vf {
	struct efx_nic *efx;
	unsigned int pci_rid;
	char pci_name[13]; /* dddd:bb:dd.f */
	unsigned int index;
	struct work_struct req;
	u64 req_addr;
	int req_type;
	unsigned req_seqno;
	unsigned msg_seqno;
	bool busy;
	struct efx_buffer buf;
	unsigned buftbl_base;
	bool rx_filtering;
	enum efx_filter_flags rx_filter_flags;
	unsigned rx_filter_qid;
	int rx_filter_id;
	enum efx_vf_tx_filter_mode tx_filter_mode;
	int tx_filter_id;
	struct vfdi_endpoint addr;
	u64 status_addr;
	struct mutex status_lock;
	u64 *peer_page_addrs;
	unsigned peer_page_count;
	u64 evq0_addrs[EFX_MAX_VF_EVQ_SIZE * sizeof(efx_qword_t) /
		       EFX_BUF_SIZE];
	unsigned evq0_count;
	wait_queue_head_t flush_waitq;
	struct mutex txq_lock;
	unsigned long txq_mask[VI_MASK_LENGTH];
	unsigned txq_count;
	unsigned long rxq_mask[VI_MASK_LENGTH];
	unsigned rxq_count;
	unsigned long rxq_retry_mask[VI_MASK_LENGTH];
	atomic_t rxq_retry_count;
	struct work_struct reset_work;
};

struct efx_memcpy_req {
	unsigned int from_rid;
	void *from_buf;
	u64 from_addr;
	unsigned int to_rid;
	u64 to_addr;
	unsigned length;
};

/**
 * struct efx_local_addr - A MAC address on the vswitch without a VF.
 *
 * Siena does not have a switch, so VFs can't transmit data to each
 * other. Instead the VFs must be made aware of the local addresses
 * on the vswitch, so that they can arrange for an alternative
 * software datapath to be used.
 *
 * @link: List head for insertion into efx->local_addr_list.
 * @addr: Ethernet address
 */
struct efx_local_addr {
	struct list_head link;
	u8 addr[ETH_ALEN];
};

/**
 * struct efx_endpoint_page - Page of vfdi_endpoint structures
 *
 * @link: List head for insertion into efx->local_page_list.
 * @ptr: Pointer to page.
 * @addr: DMA address of page.
 */
struct efx_endpoint_page {
	struct list_head link;
	void *ptr;
	dma_addr_t addr;
};

/* Buffer table entries are reserved txq0,rxq0,evq0,txq1,rxq1,evq1 */
#define EFX_BUFTBL_TXQ_BASE(_vf, _qid)					\
	((_vf)->buftbl_base + EFX_VF_BUFTBL_PER_VI * (_qid))
#define EFX_BUFTBL_RXQ_BASE(_vf, _qid)					\
	(EFX_BUFTBL_TXQ_BASE(_vf, _qid) +				\
	 (EFX_MAX_DMAQ_SIZE * sizeof(efx_qword_t) / EFX_BUF_SIZE))
#define EFX_BUFTBL_EVQ_BASE(_vf, _qid)					\
	(EFX_BUFTBL_TXQ_BASE(_vf, _qid) +				\
	 (2 * EFX_MAX_DMAQ_SIZE * sizeof(efx_qword_t) / EFX_BUF_SIZE))

#define EFX_FIELD_MASK(_field)			\
	((1 << _field ## _WIDTH) - 1)

/* VFs can only use this many transmit channels */
static unsigned int vf_max_tx_channels = 2;
module_param(vf_max_tx_channels, uint, 0444);
MODULE_PARM_DESC(vf_max_tx_channels,
		 "Limit the number of TX channels VFs can use");

static int max_vfs = -1;
module_param(max_vfs, int, 0444);
MODULE_PARM_DESC(max_vfs,
		 "Reduce the number of VFs initialized by the driver");

/* Workqueue used by VFDI communication.  We can't use the global
 * workqueue because it may be running the VF driver's probe()
 * routine, which will be blocked there waiting for a VFDI response.
 */
static struct workqueue_struct *vfdi_workqueue;

static unsigned abs_index(struct siena_vf *vf, unsigned index)
{
	return EFX_VI_BASE + vf->index * efx_vf_size(vf->efx) + index;
}

static int efx_siena_sriov_cmd(struct efx_nic *efx, bool enable,
			       unsigned *vi_scale_out, unsigned *vf_total_out)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SRIOV_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_SRIOV_OUT_LEN);
	unsigned vi_scale, vf_total;
	size_t outlen;
	int rc;

	MCDI_SET_DWORD(inbuf, SRIOV_IN_ENABLE, enable ? 1 : 0);
	MCDI_SET_DWORD(inbuf, SRIOV_IN_VI_BASE, EFX_VI_BASE);
	MCDI_SET_DWORD(inbuf, SRIOV_IN_VF_COUNT, efx->vf_count);

	rc = efx_mcdi_rpc_quiet(efx, MC_CMD_SRIOV, inbuf, MC_CMD_SRIOV_IN_LEN,
				outbuf, MC_CMD_SRIOV_OUT_LEN, &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_SRIOV_OUT_LEN)
		return -EIO;

	vf_total = MCDI_DWORD(outbuf, SRIOV_OUT_VF_TOTAL);
	vi_scale = MCDI_DWORD(outbuf, SRIOV_OUT_VI_SCALE);
	if (vi_scale > EFX_VI_SCALE_MAX)
		return -EOPNOTSUPP;

	if (vi_scale_out)
		*vi_scale_out = vi_scale;
	if (vf_total_out)
		*vf_total_out = vf_total;

	return 0;
}

static void efx_siena_sriov_usrev(struct efx_nic *efx, bool enabled)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;

	EFX_POPULATE_OWORD_2(reg,
			     FRF_CZ_USREV_DIS, enabled ? 0 : 1,
			     FRF_CZ_DFLT_EVQ, nic_data->vfdi_channel->channel);
	efx_writeo(efx, &reg, FR_CZ_USR_EV_CFG);
}

static int efx_siena_sriov_memcpy(struct efx_nic *efx,
				  struct efx_memcpy_req *req,
				  unsigned int count)
{
	MCDI_DECLARE_BUF(inbuf, MCDI_CTL_SDU_LEN_MAX_V1);
	MCDI_DECLARE_STRUCT_PTR(record);
	unsigned int index, used;
	u64 from_addr;
	u32 from_rid;
	int rc;

	mb();	/* Finish writing source/reading dest before DMA starts */

	if (WARN_ON(count > MC_CMD_MEMCPY_IN_RECORD_MAXNUM))
		return -ENOBUFS;
	used = MC_CMD_MEMCPY_IN_LEN(count);

	for (index = 0; index < count; index++) {
		record = MCDI_ARRAY_STRUCT_PTR(inbuf, MEMCPY_IN_RECORD, index);
		MCDI_SET_DWORD(record, MEMCPY_RECORD_TYPEDEF_NUM_RECORDS,
			       count);
		MCDI_SET_DWORD(record, MEMCPY_RECORD_TYPEDEF_TO_RID,
			       req->to_rid);
		MCDI_SET_QWORD(record, MEMCPY_RECORD_TYPEDEF_TO_ADDR,
			       req->to_addr);
		if (req->from_buf == NULL) {
			from_rid = req->from_rid;
			from_addr = req->from_addr;
		} else {
			if (WARN_ON(used + req->length >
				    MCDI_CTL_SDU_LEN_MAX_V1)) {
				rc = -ENOBUFS;
				goto out;
			}

			from_rid = MC_CMD_MEMCPY_RECORD_TYPEDEF_RID_INLINE;
			from_addr = used;
			memcpy(_MCDI_PTR(inbuf, used), req->from_buf,
			       req->length);
			used += req->length;
		}

		MCDI_SET_DWORD(record, MEMCPY_RECORD_TYPEDEF_FROM_RID, from_rid);
		MCDI_SET_QWORD(record, MEMCPY_RECORD_TYPEDEF_FROM_ADDR,
			       from_addr);
		MCDI_SET_DWORD(record, MEMCPY_RECORD_TYPEDEF_LENGTH,
			       req->length);

		++req;
	}

	rc = efx_mcdi_rpc(efx, MC_CMD_MEMCPY, inbuf, used, NULL, 0, NULL);
out:
	mb();	/* Don't write source/read dest before DMA is complete */

	return rc;
}

/* The TX filter is entirely controlled by this driver, and is modified
 * underneath the feet of the VF
 */
static void efx_siena_sriov_reset_tx_filter(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct efx_filter_spec filter;
	u16 vlan;
	int rc;

	if (vf->tx_filter_id != -1) {
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					  vf->tx_filter_id);
		netif_dbg(efx, hw, efx->net_dev, "Removed vf %s tx filter %d\n",
			  vf->pci_name, vf->tx_filter_id);
		vf->tx_filter_id = -1;
	}

	if (is_zero_ether_addr(vf->addr.mac_addr))
		return;

	/* Turn on TX filtering automatically if not explicitly
	 * enabled or disabled.
	 */
	if (vf->tx_filter_mode == VF_TX_FILTER_AUTO && vf_max_tx_channels <= 2)
		vf->tx_filter_mode = VF_TX_FILTER_ON;

	vlan = ntohs(vf->addr.tci) & VLAN_VID_MASK;
	efx_filter_init_tx(&filter, abs_index(vf, 0));
	rc = efx_filter_set_eth_local(&filter,
				      vlan ? vlan : EFX_FILTER_VID_UNSPEC,
				      vf->addr.mac_addr);
	BUG_ON(rc);

	rc = efx_filter_insert_filter(efx, &filter, true);
	if (rc < 0) {
		netif_warn(efx, hw, efx->net_dev,
			   "Unable to migrate tx filter for vf %s\n",
			   vf->pci_name);
	} else {
		netif_dbg(efx, hw, efx->net_dev, "Inserted vf %s tx filter %d\n",
			  vf->pci_name, rc);
		vf->tx_filter_id = rc;
	}
}

/* The RX filter is managed here on behalf of the VF driver */
static void efx_siena_sriov_reset_rx_filter(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct efx_filter_spec filter;
	u16 vlan;
	int rc;

	if (vf->rx_filter_id != -1) {
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					  vf->rx_filter_id);
		netif_dbg(efx, hw, efx->net_dev, "Removed vf %s rx filter %d\n",
			  vf->pci_name, vf->rx_filter_id);
		vf->rx_filter_id = -1;
	}

	if (!vf->rx_filtering || is_zero_ether_addr(vf->addr.mac_addr))
		return;

	vlan = ntohs(vf->addr.tci) & VLAN_VID_MASK;
	efx_filter_init_rx(&filter, EFX_FILTER_PRI_REQUIRED,
			   vf->rx_filter_flags,
			   abs_index(vf, vf->rx_filter_qid));
	rc = efx_filter_set_eth_local(&filter,
				      vlan ? vlan : EFX_FILTER_VID_UNSPEC,
				      vf->addr.mac_addr);
	BUG_ON(rc);

	rc = efx_filter_insert_filter(efx, &filter, true);
	if (rc < 0) {
		netif_warn(efx, hw, efx->net_dev,
			   "Unable to insert rx filter for vf %s\n",
			   vf->pci_name);
	} else {
		netif_dbg(efx, hw, efx->net_dev, "Inserted vf %s rx filter %d\n",
			  vf->pci_name, rc);
		vf->rx_filter_id = rc;
	}
}

static void __efx_siena_sriov_update_vf_addr(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct siena_nic_data *nic_data = efx->nic_data;

	efx_siena_sriov_reset_tx_filter(vf);
	efx_siena_sriov_reset_rx_filter(vf);
	queue_work(vfdi_workqueue, &nic_data->peer_work);
}

/* Push the peer list to this VF. The caller must hold status_lock to interlock
 * with VFDI requests, and they must be serialised against manipulation of
 * local_page_list, either by acquiring local_lock or by running from
 * efx_siena_sriov_peer_work()
 */
static void __efx_siena_sriov_push_vf_status(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct siena_nic_data *nic_data = efx->nic_data;
	struct vfdi_status *status = nic_data->vfdi_status.addr;
	struct efx_memcpy_req copy[4];
	struct efx_endpoint_page *epp;
	unsigned int pos, count;
	unsigned data_offset;
	efx_qword_t event;

	WARN_ON(!mutex_is_locked(&vf->status_lock));
	WARN_ON(!vf->status_addr);

	status->local = vf->addr;
	status->generation_end = ++status->generation_start;

	memset(copy, '\0', sizeof(copy));
	/* Write generation_start */
	copy[0].from_buf = &status->generation_start;
	copy[0].to_rid = vf->pci_rid;
	copy[0].to_addr = vf->status_addr + offsetof(struct vfdi_status,
						     generation_start);
	copy[0].length = sizeof(status->generation_start);
	/* DMA the rest of the structure (excluding the generations). This
	 * assumes that the non-generation portion of vfdi_status is in
	 * one chunk starting at the version member.
	 */
	data_offset = offsetof(struct vfdi_status, version);
	copy[1].from_rid = efx->pci_dev->devfn;
	copy[1].from_addr = nic_data->vfdi_status.dma_addr + data_offset;
	copy[1].to_rid = vf->pci_rid;
	copy[1].to_addr = vf->status_addr + data_offset;
	copy[1].length =  status->length - data_offset;

	/* Copy the peer pages */
	pos = 2;
	count = 0;
	list_for_each_entry(epp, &nic_data->local_page_list, link) {
		if (count == vf->peer_page_count) {
			/* The VF driver will know they need to provide more
			 * pages because peer_addr_count is too large.
			 */
			break;
		}
		copy[pos].from_buf = NULL;
		copy[pos].from_rid = efx->pci_dev->devfn;
		copy[pos].from_addr = epp->addr;
		copy[pos].to_rid = vf->pci_rid;
		copy[pos].to_addr = vf->peer_page_addrs[count];
		copy[pos].length = EFX_PAGE_SIZE;

		if (++pos == ARRAY_SIZE(copy)) {
			efx_siena_sriov_memcpy(efx, copy, ARRAY_SIZE(copy));
			pos = 0;
		}
		++count;
	}

	/* Write generation_end */
	copy[pos].from_buf = &status->generation_end;
	copy[pos].to_rid = vf->pci_rid;
	copy[pos].to_addr = vf->status_addr + offsetof(struct vfdi_status,
						       generation_end);
	copy[pos].length = sizeof(status->generation_end);
	efx_siena_sriov_memcpy(efx, copy, pos + 1);

	/* Notify the guest */
	EFX_POPULATE_QWORD_3(event,
			     FSF_AZ_EV_CODE, FSE_CZ_EV_CODE_USER_EV,
			     VFDI_EV_SEQ, (vf->msg_seqno & 0xff),
			     VFDI_EV_TYPE, VFDI_EV_TYPE_STATUS);
	++vf->msg_seqno;
	efx_farch_generate_event(efx,
				 EFX_VI_BASE + vf->index * efx_vf_size(efx),
				 &event);
}

static void efx_siena_sriov_bufs(struct efx_nic *efx, unsigned offset,
				 u64 *addr, unsigned count)
{
	efx_qword_t buf;
	unsigned pos;

	for (pos = 0; pos < count; ++pos) {
		EFX_POPULATE_QWORD_3(buf,
				     FRF_AZ_BUF_ADR_REGION, 0,
				     FRF_AZ_BUF_ADR_FBUF,
				     addr ? addr[pos] >> 12 : 0,
				     FRF_AZ_BUF_OWNER_ID_FBUF, 0);
		efx_sram_writeq(efx, efx->membase + FR_BZ_BUF_FULL_TBL,
				&buf, offset + pos);
	}
}

static bool bad_vf_index(struct efx_nic *efx, unsigned index)
{
	return index >= efx_vf_size(efx);
}

static bool bad_buf_count(unsigned buf_count, unsigned max_entry_count)
{
	unsigned max_buf_count = max_entry_count *
		sizeof(efx_qword_t) / EFX_BUF_SIZE;

	return ((buf_count & (buf_count - 1)) || buf_count > max_buf_count);
}

/* Check that VI specified by per-port index belongs to a VF.
 * Optionally set VF index and VI index within the VF.
 */
static bool map_vi_index(struct efx_nic *efx, unsigned abs_index,
			 struct siena_vf **vf_out, unsigned *rel_index_out)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	unsigned vf_i;

	if (abs_index < EFX_VI_BASE)
		return true;
	vf_i = (abs_index - EFX_VI_BASE) / efx_vf_size(efx);
	if (vf_i >= efx->vf_init_count)
		return true;

	if (vf_out)
		*vf_out = nic_data->vf + vf_i;
	if (rel_index_out)
		*rel_index_out = abs_index % efx_vf_size(efx);
	return false;
}

static int efx_vfdi_init_evq(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct vfdi_req *req = vf->buf.addr;
	unsigned vf_evq = req->u.init_evq.index;
	unsigned buf_count = req->u.init_evq.buf_count;
	unsigned abs_evq = abs_index(vf, vf_evq);
	unsigned buftbl = EFX_BUFTBL_EVQ_BASE(vf, vf_evq);
	efx_oword_t reg;

	if (bad_vf_index(efx, vf_evq) ||
	    bad_buf_count(buf_count, EFX_MAX_VF_EVQ_SIZE)) {
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Invalid INIT_EVQ from %s: evq %d bufs %d\n",
				  vf->pci_name, vf_evq, buf_count);
		return VFDI_RC_EINVAL;
	}

	efx_siena_sriov_bufs(efx, buftbl, req->u.init_evq.addr, buf_count);

	EFX_POPULATE_OWORD_3(reg,
			     FRF_CZ_TIMER_Q_EN, 1,
			     FRF_CZ_HOST_NOTIFY_MODE, 0,
			     FRF_CZ_TIMER_MODE, FFE_CZ_TIMER_MODE_DIS);
	efx_writeo_table(efx, &reg, FR_BZ_TIMER_TBL, abs_evq);
	EFX_POPULATE_OWORD_3(reg,
			     FRF_AZ_EVQ_EN, 1,
			     FRF_AZ_EVQ_SIZE, __ffs(buf_count),
			     FRF_AZ_EVQ_BUF_BASE_ID, buftbl);
	efx_writeo_table(efx, &reg, FR_BZ_EVQ_PTR_TBL, abs_evq);

	if (vf_evq == 0) {
		memcpy(vf->evq0_addrs, req->u.init_evq.addr,
		       buf_count * sizeof(u64));
		vf->evq0_count = buf_count;
	}

	return VFDI_RC_SUCCESS;
}

static int efx_vfdi_init_rxq(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct vfdi_req *req = vf->buf.addr;
	unsigned vf_rxq = req->u.init_rxq.index;
	unsigned vf_evq = req->u.init_rxq.evq;
	unsigned buf_count = req->u.init_rxq.buf_count;
	unsigned buftbl = EFX_BUFTBL_RXQ_BASE(vf, vf_rxq);
	unsigned label;
	efx_oword_t reg;

	if (bad_vf_index(efx, vf_evq) || bad_vf_index(efx, vf_rxq) ||
	    vf_rxq >= VF_MAX_RX_QUEUES ||
	    bad_buf_count(buf_count, EFX_MAX_DMAQ_SIZE)) {
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Invalid INIT_RXQ from %s: rxq %d evq %d "
				  "buf_count %d\n", vf->pci_name, vf_rxq,
				  vf_evq, buf_count);
		return VFDI_RC_EINVAL;
	}
	if (__test_and_set_bit(req->u.init_rxq.index, vf->rxq_mask))
		++vf->rxq_count;
	efx_siena_sriov_bufs(efx, buftbl, req->u.init_rxq.addr, buf_count);

	label = req->u.init_rxq.label & EFX_FIELD_MASK(FRF_AZ_RX_DESCQ_LABEL);
	EFX_POPULATE_OWORD_6(reg,
			     FRF_AZ_RX_DESCQ_BUF_BASE_ID, buftbl,
			     FRF_AZ_RX_DESCQ_EVQ_ID, abs_index(vf, vf_evq),
			     FRF_AZ_RX_DESCQ_LABEL, label,
			     FRF_AZ_RX_DESCQ_SIZE, __ffs(buf_count),
			     FRF_AZ_RX_DESCQ_JUMBO,
			     !!(req->u.init_rxq.flags &
				VFDI_RXQ_FLAG_SCATTER_EN),
			     FRF_AZ_RX_DESCQ_EN, 1);
	efx_writeo_table(efx, &reg, FR_BZ_RX_DESC_PTR_TBL,
			 abs_index(vf, vf_rxq));

	return VFDI_RC_SUCCESS;
}

static int efx_vfdi_init_txq(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct vfdi_req *req = vf->buf.addr;
	unsigned vf_txq = req->u.init_txq.index;
	unsigned vf_evq = req->u.init_txq.evq;
	unsigned buf_count = req->u.init_txq.buf_count;
	unsigned buftbl = EFX_BUFTBL_TXQ_BASE(vf, vf_txq);
	unsigned label, eth_filt_en;
	efx_oword_t reg;

	if (bad_vf_index(efx, vf_evq) || bad_vf_index(efx, vf_txq) ||
	    vf_txq >= vf_max_tx_channels ||
	    bad_buf_count(buf_count, EFX_MAX_DMAQ_SIZE)) {
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Invalid INIT_TXQ from %s: txq %d evq %d "
				  "buf_count %d\n", vf->pci_name, vf_txq,
				  vf_evq, buf_count);
		return VFDI_RC_EINVAL;
	}

	mutex_lock(&vf->txq_lock);
	if (__test_and_set_bit(req->u.init_txq.index, vf->txq_mask))
		++vf->txq_count;
	mutex_unlock(&vf->txq_lock);
	efx_siena_sriov_bufs(efx, buftbl, req->u.init_txq.addr, buf_count);

	eth_filt_en = vf->tx_filter_mode == VF_TX_FILTER_ON;

	label = req->u.init_txq.label & EFX_FIELD_MASK(FRF_AZ_TX_DESCQ_LABEL);
	EFX_POPULATE_OWORD_8(reg,
			     FRF_CZ_TX_DPT_Q_MASK_WIDTH, min(efx->vi_scale, 1U),
			     FRF_CZ_TX_DPT_ETH_FILT_EN, eth_filt_en,
			     FRF_AZ_TX_DESCQ_EN, 1,
			     FRF_AZ_TX_DESCQ_BUF_BASE_ID, buftbl,
			     FRF_AZ_TX_DESCQ_EVQ_ID, abs_index(vf, vf_evq),
			     FRF_AZ_TX_DESCQ_LABEL, label,
			     FRF_AZ_TX_DESCQ_SIZE, __ffs(buf_count),
			     FRF_BZ_TX_NON_IP_DROP_DIS, 1);
	efx_writeo_table(efx, &reg, FR_BZ_TX_DESC_PTR_TBL,
			 abs_index(vf, vf_txq));

	return VFDI_RC_SUCCESS;
}

/* Returns true when efx_vfdi_fini_all_queues should wake */
static bool efx_vfdi_flush_wake(struct siena_vf *vf)
{
	/* Ensure that all updates are visible to efx_vfdi_fini_all_queues() */
	smp_mb();

	return (!vf->txq_count && !vf->rxq_count) ||
		atomic_read(&vf->rxq_retry_count);
}

static void efx_vfdi_flush_clear(struct siena_vf *vf)
{
	memset(vf->txq_mask, 0, sizeof(vf->txq_mask));
	vf->txq_count = 0;
	memset(vf->rxq_mask, 0, sizeof(vf->rxq_mask));
	vf->rxq_count = 0;
	memset(vf->rxq_retry_mask, 0, sizeof(vf->rxq_retry_mask));
	atomic_set(&vf->rxq_retry_count, 0);
}

static int efx_vfdi_fini_all_queues(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	efx_oword_t reg;
	unsigned count = efx_vf_size(efx);
	unsigned vf_offset = EFX_VI_BASE + vf->index * efx_vf_size(efx);
	unsigned timeout = HZ;
	unsigned index, rxqs_count;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_FLUSH_RX_QUEUES_IN_LENMAX);
	int rc;

	BUILD_BUG_ON(VF_MAX_RX_QUEUES >
		     MC_CMD_FLUSH_RX_QUEUES_IN_QID_OFST_MAXNUM);

	rtnl_lock();
	efx_siena_prepare_flush(efx);
	rtnl_unlock();

	/* Flush all the initialized queues */
	rxqs_count = 0;
	for (index = 0; index < count; ++index) {
		if (test_bit(index, vf->txq_mask)) {
			EFX_POPULATE_OWORD_2(reg,
					     FRF_AZ_TX_FLUSH_DESCQ_CMD, 1,
					     FRF_AZ_TX_FLUSH_DESCQ,
					     vf_offset + index);
			efx_writeo(efx, &reg, FR_AZ_TX_FLUSH_DESCQ);
		}
		if (test_bit(index, vf->rxq_mask)) {
			MCDI_SET_ARRAY_DWORD(
				inbuf, FLUSH_RX_QUEUES_IN_QID_OFST,
				rxqs_count, vf_offset + index);
			rxqs_count++;
		}
	}

	atomic_set(&vf->rxq_retry_count, 0);
	while (timeout && (vf->rxq_count || vf->txq_count)) {
		rc = efx_mcdi_rpc(efx, MC_CMD_FLUSH_RX_QUEUES, inbuf,
				  MC_CMD_FLUSH_RX_QUEUES_IN_LEN(rxqs_count),
				  NULL, 0, NULL);
		WARN_ON(rc < 0);

		timeout = wait_event_timeout(vf->flush_waitq,
					     efx_vfdi_flush_wake(vf),
					     timeout);
		rxqs_count = 0;
		for (index = 0; index < count; ++index) {
			if (test_and_clear_bit(index, vf->rxq_retry_mask)) {
				atomic_dec(&vf->rxq_retry_count);
				MCDI_SET_ARRAY_DWORD(
					inbuf, FLUSH_RX_QUEUES_IN_QID_OFST,
					rxqs_count, vf_offset + index);
				rxqs_count++;
			}
		}
	}

	rtnl_lock();
	siena_finish_flush(efx);
	rtnl_unlock();

	/* Irrespective of success/failure, fini the queues */
	EFX_ZERO_OWORD(reg);
	for (index = 0; index < count; ++index) {
		efx_writeo_table(efx, &reg, FR_BZ_RX_DESC_PTR_TBL,
				 vf_offset + index);
		efx_writeo_table(efx, &reg, FR_BZ_TX_DESC_PTR_TBL,
				 vf_offset + index);
		efx_writeo_table(efx, &reg, FR_BZ_EVQ_PTR_TBL,
				 vf_offset + index);
		efx_writeo_table(efx, &reg, FR_BZ_TIMER_TBL,
				 vf_offset + index);
	}
	efx_siena_sriov_bufs(efx, vf->buftbl_base, NULL,
			     EFX_VF_BUFTBL_PER_VI * efx_vf_size(efx));
	efx_vfdi_flush_clear(vf);

	vf->evq0_count = 0;

	return timeout ? 0 : VFDI_RC_ETIMEDOUT;
}

static int efx_vfdi_insert_filter(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct siena_nic_data *nic_data = efx->nic_data;
	struct vfdi_req *req = vf->buf.addr;
	unsigned vf_rxq = req->u.mac_filter.rxq;
	unsigned flags;

	if (bad_vf_index(efx, vf_rxq) || vf->rx_filtering) {
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Invalid INSERT_FILTER from %s: rxq %d "
				  "flags 0x%x\n", vf->pci_name, vf_rxq,
				  req->u.mac_filter.flags);
		return VFDI_RC_EINVAL;
	}

	flags = 0;
	if (req->u.mac_filter.flags & VFDI_MAC_FILTER_FLAG_RSS)
		flags |= EFX_FILTER_FLAG_RX_RSS;
	if (req->u.mac_filter.flags & VFDI_MAC_FILTER_FLAG_SCATTER)
		flags |= EFX_FILTER_FLAG_RX_SCATTER;
	vf->rx_filter_flags = flags;
	vf->rx_filter_qid = vf_rxq;
	vf->rx_filtering = true;

	efx_siena_sriov_reset_rx_filter(vf);
	queue_work(vfdi_workqueue, &nic_data->peer_work);

	return VFDI_RC_SUCCESS;
}

static int efx_vfdi_remove_all_filters(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct siena_nic_data *nic_data = efx->nic_data;

	vf->rx_filtering = false;
	efx_siena_sriov_reset_rx_filter(vf);
	queue_work(vfdi_workqueue, &nic_data->peer_work);

	return VFDI_RC_SUCCESS;
}

static int efx_vfdi_set_status_page(struct siena_vf *vf)
{
	struct efx_nic *efx = vf->efx;
	struct siena_nic_data *nic_data = efx->nic_data;
	struct vfdi_req *req = vf->buf.addr;
	u64 page_count = req->u.set_status_page.peer_page_count;
	u64 max_page_count =
		(EFX_PAGE_SIZE -
		 offsetof(struct vfdi_req, u.set_status_page.peer_page_addr[0]))
		/ sizeof(req->u.set_status_page.peer_page_addr[0]);

	if (!req->u.set_status_page.dma_addr || page_count > max_page_count) {
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Invalid SET_STATUS_PAGE from %s\n",
				  vf->pci_name);
		return VFDI_RC_EINVAL;
	}

	mutex_lock(&nic_data->local_lock);
	mutex_lock(&vf->status_lock);
	vf->status_addr = req->u.set_status_page.dma_addr;

	kfree(vf->peer_page_addrs);
	vf->peer_page_addrs = NULL;
	vf->peer_page_count = 0;

	if (page_count) {
		vf->peer_page_addrs = kcalloc(page_count, sizeof(u64),
					      GFP_KERNEL);
		if (vf->peer_page_addrs) {
			memcpy(vf->peer_page_addrs,
			       req->u.set_status_page.peer_page_addr,
			       page_count * sizeof(u64));
			vf->peer_page_count = page_count;
		}
	}

	__efx_siena_sriov_push_vf_status(vf);
	mutex_unlock(&vf->status_lock);
	mutex_unlock(&nic_data->local_lock);

	return VFDI_RC_SUCCESS;
}

static int efx_vfdi_clear_status_page(struct siena_vf *vf)
{
	mutex_lock(&vf->status_lock);
	vf->status_addr = 0;
	mutex_unlock(&vf->status_lock);

	return VFDI_RC_SUCCESS;
}

typedef int (*efx_vfdi_op_t)(struct siena_vf *vf);

static const efx_vfdi_op_t vfdi_ops[VFDI_OP_LIMIT] = {
	[VFDI_OP_INIT_EVQ] = efx_vfdi_init_evq,
	[VFDI_OP_INIT_TXQ] = efx_vfdi_init_txq,
	[VFDI_OP_INIT_RXQ] = efx_vfdi_init_rxq,
	[VFDI_OP_FINI_ALL_QUEUES] = efx_vfdi_fini_all_queues,
	[VFDI_OP_INSERT_FILTER] = efx_vfdi_insert_filter,
	[VFDI_OP_REMOVE_ALL_FILTERS] = efx_vfdi_remove_all_filters,
	[VFDI_OP_SET_STATUS_PAGE] = efx_vfdi_set_status_page,
	[VFDI_OP_CLEAR_STATUS_PAGE] = efx_vfdi_clear_status_page,
};

static void efx_siena_sriov_vfdi(struct work_struct *work)
{
	struct siena_vf *vf = container_of(work, struct siena_vf, req);
	struct efx_nic *efx = vf->efx;
	struct vfdi_req *req = vf->buf.addr;
	struct efx_memcpy_req copy[2];
	int rc;

	/* Copy this page into the local address space */
	memset(copy, '\0', sizeof(copy));
	copy[0].from_rid = vf->pci_rid;
	copy[0].from_addr = vf->req_addr;
	copy[0].to_rid = efx->pci_dev->devfn;
	copy[0].to_addr = vf->buf.dma_addr;
	copy[0].length = EFX_PAGE_SIZE;
	rc = efx_siena_sriov_memcpy(efx, copy, 1);
	if (rc) {
		/* If we can't get the request, we can't reply to the caller */
		if (net_ratelimit())
			netif_err(efx, hw, efx->net_dev,
				  "ERROR: Unable to fetch VFDI request from %s rc %d\n",
				  vf->pci_name, -rc);
		vf->busy = false;
		return;
	}

	if (req->op < VFDI_OP_LIMIT && vfdi_ops[req->op] != NULL) {
		rc = vfdi_ops[req->op](vf);
		if (rc == 0) {
			netif_dbg(efx, hw, efx->net_dev,
				  "vfdi request %d from %s ok\n",
				  req->op, vf->pci_name);
		}
	} else {
		netif_dbg(efx, hw, efx->net_dev,
			  "ERROR: Unrecognised request %d from VF %s addr "
			  "%llx\n", req->op, vf->pci_name,
			  (unsigned long long)vf->req_addr);
		rc = VFDI_RC_EOPNOTSUPP;
	}

	/* Allow subsequent VF requests */
	vf->busy = false;
	smp_wmb();

	/* Respond to the request */
	req->rc = rc;
	req->op = VFDI_OP_RESPONSE;

	memset(copy, '\0', sizeof(copy));
	copy[0].from_buf = &req->rc;
	copy[0].to_rid = vf->pci_rid;
	copy[0].to_addr = vf->req_addr + offsetof(struct vfdi_req, rc);
	copy[0].length = sizeof(req->rc);
	copy[1].from_buf = &req->op;
	copy[1].to_rid = vf->pci_rid;
	copy[1].to_addr = vf->req_addr + offsetof(struct vfdi_req, op);
	copy[1].length = sizeof(req->op);

	(void)efx_siena_sriov_memcpy(efx, copy, ARRAY_SIZE(copy));
}



/* After a reset the event queues inside the guests no longer exist. Fill the
 * event ring in guest memory with VFDI reset events, then (re-initialise) the
 * event queue to raise an interrupt. The guest driver will then recover.
 */

static void efx_siena_sriov_reset_vf(struct siena_vf *vf,
				     struct efx_buffer *buffer)
{
	struct efx_nic *efx = vf->efx;
	struct efx_memcpy_req copy_req[4];
	efx_qword_t event;
	unsigned int pos, count, k, buftbl, abs_evq;
	efx_oword_t reg;
	efx_dword_t ptr;
	int rc;

	BUG_ON(buffer->len != EFX_PAGE_SIZE);

	if (!vf->evq0_count)
		return;
	BUG_ON(vf->evq0_count & (vf->evq0_count - 1));

	mutex_lock(&vf->status_lock);
	EFX_POPULATE_QWORD_3(event,
			     FSF_AZ_EV_CODE, FSE_CZ_EV_CODE_USER_EV,
			     VFDI_EV_SEQ, vf->msg_seqno,
			     VFDI_EV_TYPE, VFDI_EV_TYPE_RESET);
	vf->msg_seqno++;
	for (pos = 0; pos < EFX_PAGE_SIZE; pos += sizeof(event))
		memcpy(buffer->addr + pos, &event, sizeof(event));

	for (pos = 0; pos < vf->evq0_count; pos += count) {
		count = min_t(unsigned, vf->evq0_count - pos,
			      ARRAY_SIZE(copy_req));
		for (k = 0; k < count; k++) {
			copy_req[k].from_buf = NULL;
			copy_req[k].from_rid = efx->pci_dev->devfn;
			copy_req[k].from_addr = buffer->dma_addr;
			copy_req[k].to_rid = vf->pci_rid;
			copy_req[k].to_addr = vf->evq0_addrs[pos + k];
			copy_req[k].length = EFX_PAGE_SIZE;
		}
		rc = efx_siena_sriov_memcpy(efx, copy_req, count);
		if (rc) {
			if (net_ratelimit())
				netif_err(efx, hw, efx->net_dev,
					  "ERROR: Unable to notify %s of reset"
					  ": %d\n", vf->pci_name, -rc);
			break;
		}
	}

	/* Reinitialise, arm and trigger evq0 */
	abs_evq = abs_index(vf, 0);
	buftbl = EFX_BUFTBL_EVQ_BASE(vf, 0);
	efx_siena_sriov_bufs(efx, buftbl, vf->evq0_addrs, vf->evq0_count);

	EFX_POPULATE_OWORD_3(reg,
			     FRF_CZ_TIMER_Q_EN, 1,
			     FRF_CZ_HOST_NOTIFY_MODE, 0,
			     FRF_CZ_TIMER_MODE, FFE_CZ_TIMER_MODE_DIS);
	efx_writeo_table(efx, &reg, FR_BZ_TIMER_TBL, abs_evq);
	EFX_POPULATE_OWORD_3(reg,
			     FRF_AZ_EVQ_EN, 1,
			     FRF_AZ_EVQ_SIZE, __ffs(vf->evq0_count),
			     FRF_AZ_EVQ_BUF_BASE_ID, buftbl);
	efx_writeo_table(efx, &reg, FR_BZ_EVQ_PTR_TBL, abs_evq);
	EFX_POPULATE_DWORD_1(ptr, FRF_AZ_EVQ_RPTR, 0);
	efx_writed(efx, &ptr, FR_BZ_EVQ_RPTR + FR_BZ_EVQ_RPTR_STEP * abs_evq);

	mutex_unlock(&vf->status_lock);
}

static void efx_siena_sriov_reset_vf_work(struct work_struct *work)
{
	struct siena_vf *vf = container_of(work, struct siena_vf, req);
	struct efx_nic *efx = vf->efx;
	struct efx_buffer buf;

	if (!efx_nic_alloc_buffer(efx, &buf, EFX_PAGE_SIZE, GFP_NOIO)) {
		efx_siena_sriov_reset_vf(vf, &buf);
		efx_nic_free_buffer(efx, &buf);
	}
}

static void efx_siena_sriov_handle_no_channel(struct efx_nic *efx)
{
	netif_err(efx, drv, efx->net_dev,
		  "ERROR: IOV requires MSI-X and 1 additional interrupt"
		  "vector. IOV disabled\n");
	efx->vf_count = 0;
}

static int efx_siena_sriov_probe_channel(struct efx_channel *channel)
{
	struct siena_nic_data *nic_data = channel->efx->nic_data;
	nic_data->vfdi_channel = channel;

	return 0;
}

static void
efx_siena_sriov_get_channel_name(struct efx_channel *channel,
				 char *buf, size_t len)
{
	snprintf(buf, len, "%s-iov", channel->efx->name);
}

static const struct efx_channel_type efx_siena_sriov_channel_type = {
	.handle_no_channel	= efx_siena_sriov_handle_no_channel,
	.pre_probe		= efx_siena_sriov_probe_channel,
	.post_remove		= efx_siena_channel_dummy_op_void,
	.get_name		= efx_siena_sriov_get_channel_name,
	/* no copy operation; channel must not be reallocated */
	.keep_eventq		= true,
};

void efx_siena_sriov_probe(struct efx_nic *efx)
{
	unsigned count;

	if (!max_vfs)
		return;

	if (efx_siena_sriov_cmd(efx, false, &efx->vi_scale, &count)) {
		pci_info(efx->pci_dev, "no SR-IOV VFs probed\n");
		return;
	}
	if (count > 0 && count > max_vfs)
		count = max_vfs;

	/* efx_nic_dimension_resources() will reduce vf_count as appopriate */
	efx->vf_count = count;

	efx->extra_channel_type[EFX_EXTRA_CHANNEL_IOV] = &efx_siena_sriov_channel_type;
}

/* Copy the list of individual addresses into the vfdi_status.peers
 * array and auxiliary pages, protected by %local_lock. Drop that lock
 * and then broadcast the address list to every VF.
 */
static void efx_siena_sriov_peer_work(struct work_struct *data)
{
	struct siena_nic_data *nic_data = container_of(data,
						       struct siena_nic_data,
						       peer_work);
	struct efx_nic *efx = nic_data->efx;
	struct vfdi_status *vfdi_status = nic_data->vfdi_status.addr;
	struct siena_vf *vf;
	struct efx_local_addr *local_addr;
	struct vfdi_endpoint *peer;
	struct efx_endpoint_page *epp;
	struct list_head pages;
	unsigned int peer_space;
	unsigned int peer_count;
	unsigned int pos;

	mutex_lock(&nic_data->local_lock);

	/* Move the existing peer pages off %local_page_list */
	INIT_LIST_HEAD(&pages);
	list_splice_tail_init(&nic_data->local_page_list, &pages);

	/* Populate the VF addresses starting from entry 1 (entry 0 is
	 * the PF address)
	 */
	peer = vfdi_status->peers + 1;
	peer_space = ARRAY_SIZE(vfdi_status->peers) - 1;
	peer_count = 1;
	for (pos = 0; pos < efx->vf_count; ++pos) {
		vf = nic_data->vf + pos;

		mutex_lock(&vf->status_lock);
		if (vf->rx_filtering && !is_zero_ether_addr(vf->addr.mac_addr)) {
			*peer++ = vf->addr;
			++peer_count;
			--peer_space;
			BUG_ON(peer_space == 0);
		}
		mutex_unlock(&vf->status_lock);
	}

	/* Fill the remaining addresses */
	list_for_each_entry(local_addr, &nic_data->local_addr_list, link) {
		ether_addr_copy(peer->mac_addr, local_addr->addr);
		peer->tci = 0;
		++peer;
		++peer_count;
		if (--peer_space == 0) {
			if (list_empty(&pages)) {
				epp = kmalloc(sizeof(*epp), GFP_KERNEL);
				if (!epp)
					break;
				epp->ptr = dma_alloc_coherent(
					&efx->pci_dev->dev, EFX_PAGE_SIZE,
					&epp->addr, GFP_KERNEL);
				if (!epp->ptr) {
					kfree(epp);
					break;
				}
			} else {
				epp = list_first_entry(
					&pages, struct efx_endpoint_page, link);
				list_del(&epp->link);
			}

			list_add_tail(&epp->link, &nic_data->local_page_list);
			peer = (struct vfdi_endpoint *)epp->ptr;
			peer_space = EFX_PAGE_SIZE / sizeof(struct vfdi_endpoint);
		}
	}
	vfdi_status->peer_count = peer_count;
	mutex_unlock(&nic_data->local_lock);

	/* Free any now unused endpoint pages */
	while (!list_empty(&pages)) {
		epp = list_first_entry(
			&pages, struct efx_endpoint_page, link);
		list_del(&epp->link);
		dma_free_coherent(&efx->pci_dev->dev, EFX_PAGE_SIZE,
				  epp->ptr, epp->addr);
		kfree(epp);
	}

	/* Finally, push the pages */
	for (pos = 0; pos < efx->vf_count; ++pos) {
		vf = nic_data->vf + pos;

		mutex_lock(&vf->status_lock);
		if (vf->status_addr)
			__efx_siena_sriov_push_vf_status(vf);
		mutex_unlock(&vf->status_lock);
	}
}

static void efx_siena_sriov_free_local(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct efx_local_addr *local_addr;
	struct efx_endpoint_page *epp;

	while (!list_empty(&nic_data->local_addr_list)) {
		local_addr = list_first_entry(&nic_data->local_addr_list,
					      struct efx_local_addr, link);
		list_del(&local_addr->link);
		kfree(local_addr);
	}

	while (!list_empty(&nic_data->local_page_list)) {
		epp = list_first_entry(&nic_data->local_page_list,
				       struct efx_endpoint_page, link);
		list_del(&epp->link);
		dma_free_coherent(&efx->pci_dev->dev, EFX_PAGE_SIZE,
				  epp->ptr, epp->addr);
		kfree(epp);
	}
}

static int efx_siena_sriov_vf_alloc(struct efx_nic *efx)
{
	unsigned index;
	struct siena_vf *vf;
	struct siena_nic_data *nic_data = efx->nic_data;

	nic_data->vf = kcalloc(efx->vf_count, sizeof(*nic_data->vf),
			       GFP_KERNEL);
	if (!nic_data->vf)
		return -ENOMEM;

	for (index = 0; index < efx->vf_count; ++index) {
		vf = nic_data->vf + index;

		vf->efx = efx;
		vf->index = index;
		vf->rx_filter_id = -1;
		vf->tx_filter_mode = VF_TX_FILTER_AUTO;
		vf->tx_filter_id = -1;
		INIT_WORK(&vf->req, efx_siena_sriov_vfdi);
		INIT_WORK(&vf->reset_work, efx_siena_sriov_reset_vf_work);
		init_waitqueue_head(&vf->flush_waitq);
		mutex_init(&vf->status_lock);
		mutex_init(&vf->txq_lock);
	}

	return 0;
}

static void efx_siena_sriov_vfs_fini(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;
	unsigned int pos;

	for (pos = 0; pos < efx->vf_count; ++pos) {
		vf = nic_data->vf + pos;

		efx_nic_free_buffer(efx, &vf->buf);
		kfree(vf->peer_page_addrs);
		vf->peer_page_addrs = NULL;
		vf->peer_page_count = 0;

		vf->evq0_count = 0;
	}
}

static int efx_siena_sriov_vfs_init(struct efx_nic *efx)
{
	struct pci_dev *pci_dev = efx->pci_dev;
	struct siena_nic_data *nic_data = efx->nic_data;
	unsigned index, devfn, sriov, buftbl_base;
	u16 offset, stride;
	struct siena_vf *vf;
	int rc;

	sriov = pci_find_ext_capability(pci_dev, PCI_EXT_CAP_ID_SRIOV);
	if (!sriov)
		return -ENOENT;

	pci_read_config_word(pci_dev, sriov + PCI_SRIOV_VF_OFFSET, &offset);
	pci_read_config_word(pci_dev, sriov + PCI_SRIOV_VF_STRIDE, &stride);

	buftbl_base = nic_data->vf_buftbl_base;
	devfn = pci_dev->devfn + offset;
	for (index = 0; index < efx->vf_count; ++index) {
		vf = nic_data->vf + index;

		/* Reserve buffer entries */
		vf->buftbl_base = buftbl_base;
		buftbl_base += EFX_VF_BUFTBL_PER_VI * efx_vf_size(efx);

		vf->pci_rid = devfn;
		snprintf(vf->pci_name, sizeof(vf->pci_name),
			 "%04x:%02x:%02x.%d",
			 pci_domain_nr(pci_dev->bus), pci_dev->bus->number,
			 PCI_SLOT(devfn), PCI_FUNC(devfn));

		rc = efx_nic_alloc_buffer(efx, &vf->buf, EFX_PAGE_SIZE,
					  GFP_KERNEL);
		if (rc)
			goto fail;

		devfn += stride;
	}

	return 0;

fail:
	efx_siena_sriov_vfs_fini(efx);
	return rc;
}

int efx_siena_sriov_init(struct efx_nic *efx)
{
	struct net_device *net_dev = efx->net_dev;
	struct siena_nic_data *nic_data = efx->nic_data;
	struct vfdi_status *vfdi_status;
	int rc;

	/* Ensure there's room for vf_channel */
	BUILD_BUG_ON(EFX_MAX_CHANNELS + 1 >= EFX_VI_BASE);
	/* Ensure that VI_BASE is aligned on VI_SCALE */
	BUILD_BUG_ON(EFX_VI_BASE & ((1 << EFX_VI_SCALE_MAX) - 1));

	if (efx->vf_count == 0)
		return 0;

	rc = efx_siena_sriov_cmd(efx, true, NULL, NULL);
	if (rc)
		goto fail_cmd;

	rc = efx_nic_alloc_buffer(efx, &nic_data->vfdi_status,
				  sizeof(*vfdi_status), GFP_KERNEL);
	if (rc)
		goto fail_status;
	vfdi_status = nic_data->vfdi_status.addr;
	memset(vfdi_status, 0, sizeof(*vfdi_status));
	vfdi_status->version = 1;
	vfdi_status->length = sizeof(*vfdi_status);
	vfdi_status->max_tx_channels = vf_max_tx_channels;
	vfdi_status->vi_scale = efx->vi_scale;
	vfdi_status->rss_rxq_count = efx->rss_spread;
	vfdi_status->peer_count = 1 + efx->vf_count;
	vfdi_status->timer_quantum_ns = efx->timer_quantum_ns;

	rc = efx_siena_sriov_vf_alloc(efx);
	if (rc)
		goto fail_alloc;

	mutex_init(&nic_data->local_lock);
	INIT_WORK(&nic_data->peer_work, efx_siena_sriov_peer_work);
	INIT_LIST_HEAD(&nic_data->local_addr_list);
	INIT_LIST_HEAD(&nic_data->local_page_list);

	rc = efx_siena_sriov_vfs_init(efx);
	if (rc)
		goto fail_vfs;

	rtnl_lock();
	ether_addr_copy(vfdi_status->peers[0].mac_addr, net_dev->dev_addr);
	efx->vf_init_count = efx->vf_count;
	rtnl_unlock();

	efx_siena_sriov_usrev(efx, true);

	/* At this point we must be ready to accept VFDI requests */

	rc = pci_enable_sriov(efx->pci_dev, efx->vf_count);
	if (rc)
		goto fail_pci;

	netif_info(efx, probe, net_dev,
		   "enabled SR-IOV for %d VFs, %d VI per VF\n",
		   efx->vf_count, efx_vf_size(efx));
	return 0;

fail_pci:
	efx_siena_sriov_usrev(efx, false);
	rtnl_lock();
	efx->vf_init_count = 0;
	rtnl_unlock();
	efx_siena_sriov_vfs_fini(efx);
fail_vfs:
	cancel_work_sync(&nic_data->peer_work);
	efx_siena_sriov_free_local(efx);
	kfree(nic_data->vf);
fail_alloc:
	efx_nic_free_buffer(efx, &nic_data->vfdi_status);
fail_status:
	efx_siena_sriov_cmd(efx, false, NULL, NULL);
fail_cmd:
	return rc;
}

void efx_siena_sriov_fini(struct efx_nic *efx)
{
	struct siena_vf *vf;
	unsigned int pos;
	struct siena_nic_data *nic_data = efx->nic_data;

	if (efx->vf_init_count == 0)
		return;

	/* Disable all interfaces to reconfiguration */
	BUG_ON(nic_data->vfdi_channel->enabled);
	efx_siena_sriov_usrev(efx, false);
	rtnl_lock();
	efx->vf_init_count = 0;
	rtnl_unlock();

	/* Flush all reconfiguration work */
	for (pos = 0; pos < efx->vf_count; ++pos) {
		vf = nic_data->vf + pos;
		cancel_work_sync(&vf->req);
		cancel_work_sync(&vf->reset_work);
	}
	cancel_work_sync(&nic_data->peer_work);

	pci_disable_sriov(efx->pci_dev);

	/* Tear down back-end state */
	efx_siena_sriov_vfs_fini(efx);
	efx_siena_sriov_free_local(efx);
	kfree(nic_data->vf);
	efx_nic_free_buffer(efx, &nic_data->vfdi_status);
	efx_siena_sriov_cmd(efx, false, NULL, NULL);
}

void efx_siena_sriov_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	struct siena_vf *vf;
	unsigned qid, seq, type, data;

	qid = EFX_QWORD_FIELD(*event, FSF_CZ_USER_QID);

	/* USR_EV_REG_VALUE is dword0, so access the VFDI_EV fields directly */
	BUILD_BUG_ON(FSF_CZ_USER_EV_REG_VALUE_LBN != 0);
	seq = EFX_QWORD_FIELD(*event, VFDI_EV_SEQ);
	type = EFX_QWORD_FIELD(*event, VFDI_EV_TYPE);
	data = EFX_QWORD_FIELD(*event, VFDI_EV_DATA);

	netif_vdbg(efx, hw, efx->net_dev,
		   "USR_EV event from qid %d seq 0x%x type %d data 0x%x\n",
		   qid, seq, type, data);

	if (map_vi_index(efx, qid, &vf, NULL))
		return;
	if (vf->busy)
		goto error;

	if (type == VFDI_EV_TYPE_REQ_WORD0) {
		/* Resynchronise */
		vf->req_type = VFDI_EV_TYPE_REQ_WORD0;
		vf->req_seqno = seq + 1;
		vf->req_addr = 0;
	} else if (seq != (vf->req_seqno++ & 0xff) || type != vf->req_type)
		goto error;

	switch (vf->req_type) {
	case VFDI_EV_TYPE_REQ_WORD0:
	case VFDI_EV_TYPE_REQ_WORD1:
	case VFDI_EV_TYPE_REQ_WORD2:
		vf->req_addr |= (u64)data << (vf->req_type << 4);
		++vf->req_type;
		return;

	case VFDI_EV_TYPE_REQ_WORD3:
		vf->req_addr |= (u64)data << 48;
		vf->req_type = VFDI_EV_TYPE_REQ_WORD0;
		vf->busy = true;
		queue_work(vfdi_workqueue, &vf->req);
		return;
	}

error:
	if (net_ratelimit())
		netif_err(efx, hw, efx->net_dev,
			  "ERROR: Screaming VFDI request from %s\n",
			  vf->pci_name);
	/* Reset the request and sequence number */
	vf->req_type = VFDI_EV_TYPE_REQ_WORD0;
	vf->req_seqno = seq + 1;
}

void efx_siena_sriov_flr(struct efx_nic *efx, unsigned vf_i)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;

	if (vf_i > efx->vf_init_count)
		return;
	vf = nic_data->vf + vf_i;
	netif_info(efx, hw, efx->net_dev,
		   "FLR on VF %s\n", vf->pci_name);

	vf->status_addr = 0;
	efx_vfdi_remove_all_filters(vf);
	efx_vfdi_flush_clear(vf);

	vf->evq0_count = 0;
}

int efx_siena_sriov_mac_address_changed(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct vfdi_status *vfdi_status = nic_data->vfdi_status.addr;

	if (!efx->vf_init_count)
		return 0;
	ether_addr_copy(vfdi_status->peers[0].mac_addr,
			efx->net_dev->dev_addr);
	queue_work(vfdi_workqueue, &nic_data->peer_work);

	return 0;
}

void efx_siena_sriov_tx_flush_done(struct efx_nic *efx, efx_qword_t *event)
{
	struct siena_vf *vf;
	unsigned queue, qid;

	queue = EFX_QWORD_FIELD(*event,  FSF_AZ_DRIVER_EV_SUBDATA);
	if (map_vi_index(efx, queue, &vf, &qid))
		return;
	/* Ignore flush completions triggered by an FLR */
	if (!test_bit(qid, vf->txq_mask))
		return;

	__clear_bit(qid, vf->txq_mask);
	--vf->txq_count;

	if (efx_vfdi_flush_wake(vf))
		wake_up(&vf->flush_waitq);
}

void efx_siena_sriov_rx_flush_done(struct efx_nic *efx, efx_qword_t *event)
{
	struct siena_vf *vf;
	unsigned ev_failed, queue, qid;

	queue = EFX_QWORD_FIELD(*event, FSF_AZ_DRIVER_EV_RX_DESCQ_ID);
	ev_failed = EFX_QWORD_FIELD(*event,
				    FSF_AZ_DRIVER_EV_RX_FLUSH_FAIL);
	if (map_vi_index(efx, queue, &vf, &qid))
		return;
	if (!test_bit(qid, vf->rxq_mask))
		return;

	if (ev_failed) {
		set_bit(qid, vf->rxq_retry_mask);
		atomic_inc(&vf->rxq_retry_count);
	} else {
		__clear_bit(qid, vf->rxq_mask);
		--vf->rxq_count;
	}
	if (efx_vfdi_flush_wake(vf))
		wake_up(&vf->flush_waitq);
}

/* Called from napi. Schedule the reset work item */
void efx_siena_sriov_desc_fetch_err(struct efx_nic *efx, unsigned dmaq)
{
	struct siena_vf *vf;
	unsigned int rel;

	if (map_vi_index(efx, dmaq, &vf, &rel))
		return;

	if (net_ratelimit())
		netif_err(efx, hw, efx->net_dev,
			  "VF %d DMA Q %d reports descriptor fetch error.\n",
			  vf->index, rel);
	queue_work(vfdi_workqueue, &vf->reset_work);
}

/* Reset all VFs */
void efx_siena_sriov_reset(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	unsigned int vf_i;
	struct efx_buffer buf;
	struct siena_vf *vf;

	ASSERT_RTNL();

	if (efx->vf_init_count == 0)
		return;

	efx_siena_sriov_usrev(efx, true);
	(void)efx_siena_sriov_cmd(efx, true, NULL, NULL);

	if (efx_nic_alloc_buffer(efx, &buf, EFX_PAGE_SIZE, GFP_NOIO))
		return;

	for (vf_i = 0; vf_i < efx->vf_init_count; ++vf_i) {
		vf = nic_data->vf + vf_i;
		efx_siena_sriov_reset_vf(vf, &buf);
	}

	efx_nic_free_buffer(efx, &buf);
}

int efx_init_sriov(void)
{
	/* A single threaded workqueue is sufficient. efx_siena_sriov_vfdi() and
	 * efx_siena_sriov_peer_work() spend almost all their time sleeping for
	 * MCDI to complete anyway
	 */
	vfdi_workqueue = create_singlethread_workqueue("sfc_vfdi");
	if (!vfdi_workqueue)
		return -ENOMEM;
	return 0;
}

void efx_fini_sriov(void)
{
	destroy_workqueue(vfdi_workqueue);
}

int efx_siena_sriov_set_vf_mac(struct efx_nic *efx, int vf_i, const u8 *mac)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;

	if (vf_i >= efx->vf_init_count)
		return -EINVAL;
	vf = nic_data->vf + vf_i;

	mutex_lock(&vf->status_lock);
	ether_addr_copy(vf->addr.mac_addr, mac);
	__efx_siena_sriov_update_vf_addr(vf);
	mutex_unlock(&vf->status_lock);

	return 0;
}

int efx_siena_sriov_set_vf_vlan(struct efx_nic *efx, int vf_i,
				u16 vlan, u8 qos)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;
	u16 tci;

	if (vf_i >= efx->vf_init_count)
		return -EINVAL;
	vf = nic_data->vf + vf_i;

	mutex_lock(&vf->status_lock);
	tci = (vlan & VLAN_VID_MASK) | ((qos & 0x7) << VLAN_PRIO_SHIFT);
	vf->addr.tci = htons(tci);
	__efx_siena_sriov_update_vf_addr(vf);
	mutex_unlock(&vf->status_lock);

	return 0;
}

int efx_siena_sriov_set_vf_spoofchk(struct efx_nic *efx, int vf_i,
				    bool spoofchk)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;
	int rc;

	if (vf_i >= efx->vf_init_count)
		return -EINVAL;
	vf = nic_data->vf + vf_i;

	mutex_lock(&vf->txq_lock);
	if (vf->txq_count == 0) {
		vf->tx_filter_mode =
			spoofchk ? VF_TX_FILTER_ON : VF_TX_FILTER_OFF;
		rc = 0;
	} else {
		/* This cannot be changed while TX queues are running */
		rc = -EBUSY;
	}
	mutex_unlock(&vf->txq_lock);
	return rc;
}

int efx_siena_sriov_get_vf_config(struct efx_nic *efx, int vf_i,
				  struct ifla_vf_info *ivi)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct siena_vf *vf;
	u16 tci;

	if (vf_i >= efx->vf_init_count)
		return -EINVAL;
	vf = nic_data->vf + vf_i;

	ivi->vf = vf_i;
	ether_addr_copy(ivi->mac, vf->addr.mac_addr);
	ivi->max_tx_rate = 0;
	ivi->min_tx_rate = 0;
	tci = ntohs(vf->addr.tci);
	ivi->vlan = tci & VLAN_VID_MASK;
	ivi->qos = (tci >> VLAN_PRIO_SHIFT) & 0x7;
	ivi->spoofchk = vf->tx_filter_mode == VF_TX_FILTER_ON;

	return 0;
}

bool efx_siena_sriov_wanted(struct efx_nic *efx)
{
	return efx->vf_count != 0;
}

int efx_siena_sriov_configure(struct efx_nic *efx, int num_vfs)
{
	return 0;
}
