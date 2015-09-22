/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include <linux/if_ether.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fip.h>
#include <scsi/fc/fc_fcoe.h>
#include <scsi/libfc.h>
#include <scsi/libfcoe.h>
#include <uapi/linux/dcbnl.h>

#include "i40e.h"
#include "i40e_fcoe.h"

/**
 * i40e_rx_is_fcoe - returns true if the rx packet type is FCoE
 * @ptype: the packet type field from rx descriptor write-back
 **/
static inline bool i40e_rx_is_fcoe(u16 ptype)
{
	return (ptype >= I40E_RX_PTYPE_L2_FCOE_PAY3) &&
	       (ptype <= I40E_RX_PTYPE_L2_FCOE_VFT_FCOTHER);
}

/**
 * i40e_fcoe_sof_is_class2 - returns true if this is a FC Class 2 SOF
 * @sof: the FCoE start of frame delimiter
 **/
static inline bool i40e_fcoe_sof_is_class2(u8 sof)
{
	return (sof == FC_SOF_I2) || (sof == FC_SOF_N2);
}

/**
 * i40e_fcoe_sof_is_class3 - returns true if this is a FC Class 3 SOF
 * @sof: the FCoE start of frame delimiter
 **/
static inline bool i40e_fcoe_sof_is_class3(u8 sof)
{
	return (sof == FC_SOF_I3) || (sof == FC_SOF_N3);
}

/**
 * i40e_fcoe_sof_is_supported - returns true if the FC SOF is supported by HW
 * @sof: the input SOF value from the frame
 **/
static inline bool i40e_fcoe_sof_is_supported(u8 sof)
{
	return i40e_fcoe_sof_is_class2(sof) ||
	       i40e_fcoe_sof_is_class3(sof);
}

/**
 * i40e_fcoe_fc_sof - pull the SOF from FCoE header in the frame
 * @skb: the frame whose EOF is to be pulled from
 **/
static inline int i40e_fcoe_fc_sof(struct sk_buff *skb, u8 *sof)
{
	*sof = ((struct fcoe_hdr *)skb_network_header(skb))->fcoe_sof;

	if (!i40e_fcoe_sof_is_supported(*sof))
		return -EINVAL;
	return 0;
}

/**
 * i40e_fcoe_eof_is_supported - returns true if the EOF is supported by HW
 * @eof:     the input EOF value from the frame
 **/
static inline bool i40e_fcoe_eof_is_supported(u8 eof)
{
	return (eof == FC_EOF_N) || (eof == FC_EOF_T) ||
	       (eof == FC_EOF_NI) || (eof == FC_EOF_A);
}

/**
 * i40e_fcoe_fc_eof - pull EOF from FCoE trailer in the frame
 * @skb: the frame whose EOF is to be pulled from
 **/
static inline int i40e_fcoe_fc_eof(struct sk_buff *skb, u8 *eof)
{
	/* the first byte of the last dword is EOF */
	skb_copy_bits(skb, skb->len - 4, eof, 1);

	if (!i40e_fcoe_eof_is_supported(*eof))
		return -EINVAL;
	return 0;
}

/**
 * i40e_fcoe_ctxt_eof - convert input FC EOF for descriptor programming
 * @eof: the input eof value from the frame
 *
 * The FC EOF is converted to the value understood by HW for descriptor
 * programming. Never call this w/o calling i40e_fcoe_eof_is_supported()
 * first and that already checks for all supported valid eof values.
 **/
static inline u32 i40e_fcoe_ctxt_eof(u8 eof)
{
	switch (eof) {
	case FC_EOF_N:
		return I40E_TX_DESC_CMD_L4T_EOFT_EOF_N;
	case FC_EOF_T:
		return I40E_TX_DESC_CMD_L4T_EOFT_EOF_T;
	case FC_EOF_NI:
		return I40E_TX_DESC_CMD_L4T_EOFT_EOF_NI;
	case FC_EOF_A:
		return I40E_TX_DESC_CMD_L4T_EOFT_EOF_A;
	default:
		/* Supported valid eof shall be already checked by
		 * calling i40e_fcoe_eof_is_supported() first,
		 * therefore this default case shall never hit.
		 */
		WARN_ON(1);
		return -EINVAL;
	}
}

/**
 * i40e_fcoe_xid_is_valid - returns true if the exchange id is valid
 * @xid: the exchange id
 **/
static inline bool i40e_fcoe_xid_is_valid(u16 xid)
{
	return (xid != FC_XID_UNKNOWN) && (xid < I40E_FCOE_DDP_MAX);
}

/**
 * i40e_fcoe_ddp_unmap - unmap the mapped sglist associated
 * @pf: pointer to PF
 * @ddp: sw DDP context
 *
 * Unmap the scatter-gather list associated with the given SW DDP context
 *
 * Returns: data length already ddp-ed in bytes
 *
 **/
static inline void i40e_fcoe_ddp_unmap(struct i40e_pf *pf,
				       struct i40e_fcoe_ddp *ddp)
{
	if (test_and_set_bit(__I40E_FCOE_DDP_UNMAPPED, &ddp->flags))
		return;

	if (ddp->sgl) {
		dma_unmap_sg(&pf->pdev->dev, ddp->sgl, ddp->sgc,
			     DMA_FROM_DEVICE);
		ddp->sgl = NULL;
		ddp->sgc = 0;
	}

	if (ddp->pool) {
		dma_pool_free(ddp->pool, ddp->udl, ddp->udp);
		ddp->pool = NULL;
	}
}

/**
 * i40e_fcoe_ddp_clear - clear the given SW DDP context
 * @ddp - SW DDP context
 **/
static inline void i40e_fcoe_ddp_clear(struct i40e_fcoe_ddp *ddp)
{
	memset(ddp, 0, sizeof(struct i40e_fcoe_ddp));
	ddp->xid = FC_XID_UNKNOWN;
	ddp->flags = __I40E_FCOE_DDP_NONE;
}

/**
 * i40e_fcoe_progid_is_fcoe - check if the prog_id is for FCoE
 * @id: the prog id for the programming status Rx descriptor write-back
 **/
static inline bool i40e_fcoe_progid_is_fcoe(u8 id)
{
	return (id == I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_PROG_STATUS) ||
	       (id == I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_INVL_STATUS);
}

/**
 * i40e_fcoe_fc_get_xid - get xid from the frame header
 * @fh: the fc frame header
 *
 * In case the incoming frame's exchange is originated from
 * the initiator, then received frame's exchange id is ANDed
 * with fc_cpu_mask bits to get the same cpu on which exchange
 * was originated, otherwise just use the current cpu.
 *
 * Returns ox_id if exchange originator, rx_id if responder
 **/
static inline u16 i40e_fcoe_fc_get_xid(struct fc_frame_header *fh)
{
	u32 f_ctl = ntoh24(fh->fh_f_ctl);

	return (f_ctl & FC_FC_EX_CTX) ?
		be16_to_cpu(fh->fh_ox_id) :
		be16_to_cpu(fh->fh_rx_id);
}

/**
 * i40e_fcoe_fc_frame_header - get fc frame header from skb
 * @skb: packet
 *
 * This checks if there is a VLAN header and returns the data
 * pointer to the start of the fc_frame_header.
 *
 * Returns pointer to the fc_frame_header
 **/
static inline struct fc_frame_header *i40e_fcoe_fc_frame_header(
	struct sk_buff *skb)
{
	void *fh = skb->data + sizeof(struct fcoe_hdr);

	if (eth_hdr(skb)->h_proto == htons(ETH_P_8021Q))
		fh += sizeof(struct vlan_hdr);

	return (struct fc_frame_header *)fh;
}

/**
 * i40e_fcoe_ddp_put - release the DDP context for a given exchange id
 * @netdev: the corresponding net_device
 * @xid: the exchange id that corresponding DDP context will be released
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_done
 * and it is expected to be called by ULD, i.e., FCP layer of libfc
 * to release the corresponding ddp context when the I/O is done.
 *
 * Returns : data length already ddp-ed in bytes
 **/
static int i40e_fcoe_ddp_put(struct net_device *netdev, u16 xid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	int len = 0;
	struct i40e_fcoe_ddp *ddp = &fcoe->ddp[xid];

	if (!fcoe || !ddp)
		goto out;

	if (test_bit(__I40E_FCOE_DDP_DONE, &ddp->flags))
		len = ddp->len;
	i40e_fcoe_ddp_unmap(pf, ddp);
out:
	return len;
}

/**
 * i40e_fcoe_sw_init - sets up the HW for FCoE
 * @pf: pointer to PF
 *
 * Returns 0 if FCoE is supported otherwise the error code
 **/
int i40e_init_pf_fcoe(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	pf->flags &= ~I40E_FLAG_FCOE_ENABLED;
	pf->num_fcoe_qps = 0;
	pf->fcoe_hmc_cntx_num = 0;
	pf->fcoe_hmc_filt_num = 0;

	if (!pf->hw.func_caps.fcoe) {
		dev_info(&pf->pdev->dev, "FCoE capability is disabled\n");
		return 0;
	}

	if (!pf->hw.func_caps.dcb) {
		dev_warn(&pf->pdev->dev,
			 "Hardware is not DCB capable not enabling FCoE.\n");
		return 0;
	}

	/* enable FCoE hash filter */
	val = rd32(hw, I40E_PFQF_HENA(1));
	val |= BIT(I40E_FILTER_PCTYPE_FCOE_OX - 32);
	val |= BIT(I40E_FILTER_PCTYPE_FCOE_RX - 32);
	val &= I40E_PFQF_HENA_PTYPE_ENA_MASK;
	wr32(hw, I40E_PFQF_HENA(1), val);

	/* enable flag */
	pf->flags |= I40E_FLAG_FCOE_ENABLED;
	pf->num_fcoe_qps = I40E_DEFAULT_FCOE;

	/* Reserve 4K DDP contexts and 20K filter size for FCoE */
	pf->fcoe_hmc_cntx_num = BIT(I40E_DMA_CNTX_SIZE_4K) *
				I40E_DMA_CNTX_BASE_SIZE;
	pf->fcoe_hmc_filt_num = pf->fcoe_hmc_cntx_num +
				BIT(I40E_HASH_FILTER_SIZE_16K) *
				I40E_HASH_FILTER_BASE_SIZE;

	/* FCoE object: max 16K filter buckets and 4K DMA contexts */
	pf->filter_settings.fcoe_filt_num = I40E_HASH_FILTER_SIZE_16K;
	pf->filter_settings.fcoe_cntx_num = I40E_DMA_CNTX_SIZE_4K;

	/* Setup max frame with FCoE_MTU plus L2 overheads */
	val = rd32(hw, I40E_GLFCOE_RCTL);
	val &= ~I40E_GLFCOE_RCTL_MAX_SIZE_MASK;
	val |= ((FCOE_MTU + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)
		 << I40E_GLFCOE_RCTL_MAX_SIZE_SHIFT);
	wr32(hw, I40E_GLFCOE_RCTL, val);

	dev_info(&pf->pdev->dev, "FCoE is supported.\n");
	return 0;
}

/**
 * i40e_get_fcoe_tc_map - Return TC map for FCoE APP
 * @pf: pointer to PF
 *
 **/
u8 i40e_get_fcoe_tc_map(struct i40e_pf *pf)
{
	struct i40e_dcb_app_priority_table app;
	struct i40e_hw *hw = &pf->hw;
	u8 enabled_tc = 0;
	u8 tc, i;
	/* Get the FCoE APP TLV */
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	for (i = 0; i < dcbcfg->numapps; i++) {
		app = dcbcfg->app[i];
		if (app.selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
		    app.protocolid == ETH_P_FCOE) {
			tc = dcbcfg->etscfg.prioritytable[app.priority];
			enabled_tc |= BIT(tc);
			break;
		}
	}

	/* TC0 if there is no TC defined for FCoE APP TLV */
	enabled_tc = enabled_tc ? enabled_tc : 0x1;

	return enabled_tc;
}

/**
 * i40e_fcoe_vsi_init - prepares the VSI context for creating a FCoE VSI
 * @vsi: pointer to the associated VSI struct
 * @ctxt: pointer to the associated VSI context to be passed to HW
 *
 * Returns 0 on success or < 0 on error
 **/
int i40e_fcoe_vsi_init(struct i40e_vsi *vsi, struct i40e_vsi_context *ctxt)
{
	struct i40e_aqc_vsi_properties_data *info = &ctxt->info;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u8 enabled_tc = 0;

	if (!(pf->flags & I40E_FLAG_FCOE_ENABLED)) {
		dev_err(&pf->pdev->dev,
			"FCoE is not enabled for this device\n");
		return -EPERM;
	}

	/* initialize the hardware for FCoE */
	ctxt->pf_num = hw->pf_id;
	ctxt->vf_num = 0;
	ctxt->uplink_seid = vsi->uplink_seid;
	ctxt->connection_type = I40E_AQ_VSI_CONN_TYPE_NORMAL;
	ctxt->flags = I40E_AQ_VSI_TYPE_PF;

	/* FCoE VSI would need the following sections */
	info->valid_sections |= cpu_to_le16(I40E_AQ_VSI_PROP_QUEUE_OPT_VALID);

	/* FCoE VSI does not need these sections */
	info->valid_sections &= cpu_to_le16(~(I40E_AQ_VSI_PROP_SECURITY_VALID |
					    I40E_AQ_VSI_PROP_VLAN_VALID |
					    I40E_AQ_VSI_PROP_CAS_PV_VALID |
					    I40E_AQ_VSI_PROP_INGRESS_UP_VALID |
					    I40E_AQ_VSI_PROP_EGRESS_UP_VALID));

	if (i40e_is_vsi_uplink_mode_veb(vsi)) {
		info->valid_sections |=
				cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);
		info->switch_id =
				cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);
	}
	enabled_tc = i40e_get_fcoe_tc_map(pf);
	i40e_vsi_setup_queue_map(vsi, ctxt, enabled_tc, true);

	/* set up queue option section: only enable FCoE */
	info->queueing_opt_flags = I40E_AQ_VSI_QUE_OPT_FCOE_ENA;

	return 0;
}

/**
 * i40e_fcoe_enable - this is the implementation of ndo_fcoe_enable,
 * indicating the upper FCoE protocol stack is ready to use FCoE
 * offload features.
 *
 * @netdev: pointer to the netdev that FCoE is created on
 *
 * Returns 0 on success
 *
 * in RTNL
 *
 **/
int i40e_fcoe_enable(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;

	if (!(pf->flags & I40E_FLAG_FCOE_ENABLED)) {
		netdev_err(netdev, "HW does not support FCoE.\n");
		return -ENODEV;
	}

	if (vsi->type != I40E_VSI_FCOE) {
		netdev_err(netdev, "interface does not support FCoE.\n");
		return -EBUSY;
	}

	atomic_inc(&fcoe->refcnt);

	return 0;
}

/**
 * i40e_fcoe_disable- disables FCoE for upper FCoE protocol stack.
 * @dev: pointer to the netdev that FCoE is created on
 *
 * Returns 0 on success
 *
 **/
int i40e_fcoe_disable(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;

	if (!(pf->flags & I40E_FLAG_FCOE_ENABLED)) {
		netdev_err(netdev, "device does not support FCoE\n");
		return -ENODEV;
	}
	if (vsi->type != I40E_VSI_FCOE)
		return -EBUSY;

	if (!atomic_dec_and_test(&fcoe->refcnt))
		return -EINVAL;

	netdev_info(netdev, "FCoE disabled\n");

	return 0;
}

/**
 * i40e_fcoe_dma_pool_free - free the per cpu pool for FCoE DDP
 * @fcoe: the FCoE sw object
 * @dev: the device that the pool is associated with
 * @cpu: the cpu for this pool
 *
 **/
static void i40e_fcoe_dma_pool_free(struct i40e_fcoe *fcoe,
				    struct device *dev,
				    unsigned int cpu)
{
	struct i40e_fcoe_ddp_pool *ddp_pool;

	ddp_pool = per_cpu_ptr(fcoe->ddp_pool, cpu);
	if (!ddp_pool->pool) {
		dev_warn(dev, "DDP pool already freed for cpu %d\n", cpu);
		return;
	}
	dma_pool_destroy(ddp_pool->pool);
	ddp_pool->pool = NULL;
}

/**
 * i40e_fcoe_dma_pool_create - per cpu pool for FCoE DDP
 * @fcoe: the FCoE sw object
 * @dev: the device that the pool is associated with
 * @cpu: the cpu for this pool
 *
 * Returns 0 on successful or non zero on failure
 *
 **/
static int i40e_fcoe_dma_pool_create(struct i40e_fcoe *fcoe,
				     struct device *dev,
				     unsigned int cpu)
{
	struct i40e_fcoe_ddp_pool *ddp_pool;
	struct dma_pool *pool;
	char pool_name[32];

	ddp_pool = per_cpu_ptr(fcoe->ddp_pool, cpu);
	if (ddp_pool && ddp_pool->pool) {
		dev_warn(dev, "DDP pool already allocated for cpu %d\n", cpu);
		return 0;
	}
	snprintf(pool_name, sizeof(pool_name), "i40e_fcoe_ddp_%d", cpu);
	pool = dma_pool_create(pool_name, dev, I40E_FCOE_DDP_PTR_MAX,
			       I40E_FCOE_DDP_PTR_ALIGN, PAGE_SIZE);
	if (!pool) {
		dev_err(dev, "dma_pool_create %s failed\n", pool_name);
		return -ENOMEM;
	}
	ddp_pool->pool = pool;
	return 0;
}

/**
 * i40e_fcoe_free_ddp_resources - release FCoE DDP resources
 * @vsi: the vsi FCoE is associated with
 *
 **/
void i40e_fcoe_free_ddp_resources(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	int cpu, i;

	/* do nothing if not FCoE VSI */
	if (vsi->type != I40E_VSI_FCOE)
		return;

	/* do nothing if no DDP pools were allocated */
	if (!fcoe->ddp_pool)
		return;

	for (i = 0; i < I40E_FCOE_DDP_MAX; i++)
		i40e_fcoe_ddp_put(vsi->netdev, i);

	for_each_possible_cpu(cpu)
		i40e_fcoe_dma_pool_free(fcoe, &pf->pdev->dev, cpu);

	free_percpu(fcoe->ddp_pool);
	fcoe->ddp_pool = NULL;

	netdev_info(vsi->netdev, "VSI %d,%d FCoE DDP resources released\n",
		    vsi->id, vsi->seid);
}

/**
 * i40e_fcoe_setup_ddp_resources - allocate per cpu DDP resources
 * @vsi: the VSI FCoE is associated with
 *
 * Returns 0 on successful or non zero on failure
 *
 **/
int i40e_fcoe_setup_ddp_resources(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct device *dev = &pf->pdev->dev;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	unsigned int cpu;
	int i;

	if (vsi->type != I40E_VSI_FCOE)
		return -ENODEV;

	/* do nothing if no DDP pools were allocated */
	if (fcoe->ddp_pool)
		return -EEXIST;

	/* allocate per CPU memory to track DDP pools */
	fcoe->ddp_pool = alloc_percpu(struct i40e_fcoe_ddp_pool);
	if (!fcoe->ddp_pool) {
		dev_err(&pf->pdev->dev, "failed to allocate percpu DDP\n");
		return -ENOMEM;
	}

	/* allocate pci pool for each cpu */
	for_each_possible_cpu(cpu) {
		if (!i40e_fcoe_dma_pool_create(fcoe, dev, cpu))
			continue;

		dev_err(dev, "failed to alloc DDP pool on cpu:%d\n", cpu);
		i40e_fcoe_free_ddp_resources(vsi);
		return -ENOMEM;
	}

	/* initialize the sw context */
	for (i = 0; i < I40E_FCOE_DDP_MAX; i++)
		i40e_fcoe_ddp_clear(&fcoe->ddp[i]);

	netdev_info(vsi->netdev, "VSI %d,%d FCoE DDP resources allocated\n",
		    vsi->id, vsi->seid);

	return 0;
}

/**
 * i40e_fcoe_handle_status - check the Programming Status for FCoE
 * @rx_ring: the Rx ring for this descriptor
 * @rx_desc: the Rx descriptor for Programming Status, not a packet descriptor.
 *
 * Check if this is the Rx Programming Status descriptor write-back for FCoE.
 * This is used to verify if the context/filter programming or invalidation
 * requested by SW to the HW is successful or not and take actions accordingly.
 **/
void i40e_fcoe_handle_status(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc, u8 prog_id)
{
	struct i40e_pf *pf = rx_ring->vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	struct i40e_fcoe_ddp *ddp;
	u32 error;
	u16 xid;
	u64 qw;

	/* we only care for FCoE here */
	if (!i40e_fcoe_progid_is_fcoe(prog_id))
		return;

	xid = le32_to_cpu(rx_desc->wb.qword0.hi_dword.fcoe_param) &
	      (I40E_FCOE_DDP_MAX - 1);

	if (!i40e_fcoe_xid_is_valid(xid))
		return;

	ddp = &fcoe->ddp[xid];
	WARN_ON(xid != ddp->xid);

	qw = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	error = (qw & I40E_RX_PROG_STATUS_DESC_QW1_ERROR_MASK) >>
		I40E_RX_PROG_STATUS_DESC_QW1_ERROR_SHIFT;

	/* DDP context programming status: failure or success */
	if (prog_id == I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_PROG_STATUS) {
		if (I40E_RX_PROG_FCOE_ERROR_TBL_FULL(error)) {
			dev_err(&pf->pdev->dev, "xid %x ddp->xid %x TABLE FULL\n",
				xid, ddp->xid);
			ddp->prerr |= I40E_RX_PROG_FCOE_ERROR_TBL_FULL_BIT;
		}
		if (I40E_RX_PROG_FCOE_ERROR_CONFLICT(error)) {
			dev_err(&pf->pdev->dev, "xid %x ddp->xid %x CONFLICT\n",
				xid, ddp->xid);
			ddp->prerr |= I40E_RX_PROG_FCOE_ERROR_CONFLICT_BIT;
		}
	}

	/* DDP context invalidation status: failure or success */
	if (prog_id == I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_INVL_STATUS) {
		if (I40E_RX_PROG_FCOE_ERROR_INVLFAIL(error)) {
			dev_err(&pf->pdev->dev, "xid %x ddp->xid %x INVALIDATION FAILURE\n",
				xid, ddp->xid);
			ddp->prerr |= I40E_RX_PROG_FCOE_ERROR_INVLFAIL_BIT;
		}
		/* clear the flag so we can retry invalidation */
		clear_bit(__I40E_FCOE_DDP_ABORTED, &ddp->flags);
	}

	/* unmap DMA */
	i40e_fcoe_ddp_unmap(pf, ddp);
	i40e_fcoe_ddp_clear(ddp);
}

/**
 * i40e_fcoe_handle_offload - check ddp status and mark it done
 * @adapter: i40e adapter
 * @rx_desc: advanced rx descriptor
 * @skb: the skb holding the received data
 *
 * This checks ddp status.
 *
 * Returns : < 0 indicates an error or not a FCOE ddp, 0 indicates
 * not passing the skb to ULD, > 0 indicates is the length of data
 * being ddped.
 *
 **/
int i40e_fcoe_handle_offload(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc,
			     struct sk_buff *skb)
{
	struct i40e_pf *pf = rx_ring->vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	struct fc_frame_header *fh = NULL;
	struct i40e_fcoe_ddp *ddp = NULL;
	u32 status, fltstat;
	u32 error, fcerr;
	int rc = -EINVAL;
	u16 ptype;
	u16 xid;
	u64 qw;

	/* check this rxd is for programming status */
	qw = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	/* packet descriptor, check packet type */
	ptype = (qw & I40E_RXD_QW1_PTYPE_MASK) >> I40E_RXD_QW1_PTYPE_SHIFT;
	if (!i40e_rx_is_fcoe(ptype))
		goto out_no_ddp;

	error = (qw & I40E_RXD_QW1_ERROR_MASK) >> I40E_RXD_QW1_ERROR_SHIFT;
	fcerr = (error >> I40E_RX_DESC_ERROR_L3L4E_SHIFT) &
		 I40E_RX_DESC_FCOE_ERROR_MASK;

	/* check stateless offload error */
	if (unlikely(fcerr == I40E_RX_DESC_ERROR_L3L4E_PROT)) {
		dev_err(&pf->pdev->dev, "Protocol Error\n");
		skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	/* check hw status on ddp */
	status = (qw & I40E_RXD_QW1_STATUS_MASK) >> I40E_RXD_QW1_STATUS_SHIFT;
	fltstat = (status >> I40E_RX_DESC_STATUS_FLTSTAT_SHIFT) &
		   I40E_RX_DESC_FLTSTAT_FCMASK;

	/* now we are ready to check DDP */
	fh = i40e_fcoe_fc_frame_header(skb);
	xid = i40e_fcoe_fc_get_xid(fh);
	if (!i40e_fcoe_xid_is_valid(xid))
		goto out_no_ddp;

	/* non DDP normal receive, return to the protocol stack */
	if (fltstat == I40E_RX_DESC_FLTSTAT_NOMTCH)
		goto out_no_ddp;

	/* do we have a sw ddp context setup ? */
	ddp = &fcoe->ddp[xid];
	if (!ddp->sgl)
		goto out_no_ddp;

	/* fetch xid from hw rxd wb, which should match up the sw ctxt */
	xid = le16_to_cpu(rx_desc->wb.qword0.lo_dword.mirr_fcoe.fcoe_ctx_id);
	if (ddp->xid != xid) {
		dev_err(&pf->pdev->dev, "xid 0x%x does not match ctx_xid 0x%x\n",
			ddp->xid, xid);
		goto out_put_ddp;
	}

	/* the same exchange has already errored out */
	if (ddp->fcerr) {
		dev_err(&pf->pdev->dev, "xid 0x%x fcerr 0x%x reported fcer 0x%x\n",
			xid, ddp->fcerr, fcerr);
		goto out_put_ddp;
	}

	/* fcoe param is valid by now with correct DDPed length */
	ddp->len = le32_to_cpu(rx_desc->wb.qword0.hi_dword.fcoe_param);
	ddp->fcerr = fcerr;
	/* header posting only, useful only for target mode and debugging */
	if (fltstat == I40E_RX_DESC_FLTSTAT_DDP) {
		/* For target mode, we get header of the last packet but it
		 * does not have the FCoE trailer field, i.e., CRC and EOF
		 * Ordered Set since they are offloaded by the HW, so fill
		 * it up correspondingly to allow the packet to pass through
		 * to the upper protocol stack.
		 */
		u32 f_ctl = ntoh24(fh->fh_f_ctl);

		if ((f_ctl & FC_FC_END_SEQ) &&
		    (fh->fh_r_ctl == FC_RCTL_DD_SOL_DATA)) {
			struct fcoe_crc_eof *crc = NULL;

			crc = (struct fcoe_crc_eof *)skb_put(skb, sizeof(*crc));
			crc->fcoe_eof = FC_EOF_T;
		} else {
			/* otherwise, drop the header only frame */
			rc = 0;
			goto out_no_ddp;
		}
	}

out_put_ddp:
	/* either we got RSP or we have an error, unmap DMA in both cases */
	i40e_fcoe_ddp_unmap(pf, ddp);
	if (ddp->len && !ddp->fcerr) {
		int pkts;

		rc = ddp->len;
		i40e_fcoe_ddp_clear(ddp);
		ddp->len = rc;
		pkts = DIV_ROUND_UP(rc, 2048);
		rx_ring->stats.bytes += rc;
		rx_ring->stats.packets += pkts;
		rx_ring->q_vector->rx.total_bytes += rc;
		rx_ring->q_vector->rx.total_packets += pkts;
		set_bit(__I40E_FCOE_DDP_DONE, &ddp->flags);
	}

out_no_ddp:
	return rc;
}

/**
 * i40e_fcoe_ddp_setup - called to set up ddp context
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 * @target_mode: indicates this is a DDP request for target
 *
 * Returns : 1 for success and 0 for no DDP on this I/O
 **/
static int i40e_fcoe_ddp_setup(struct net_device *netdev, u16 xid,
			       struct scatterlist *sgl, unsigned int sgc,
			       int target_mode)
{
	static const unsigned int bufflen = I40E_FCOE_DDP_BUF_MIN;
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_fcoe_ddp_pool *ddp_pool;
	struct i40e_pf *pf = np->vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	unsigned int i, j, dmacount;
	struct i40e_fcoe_ddp *ddp;
	unsigned int firstoff = 0;
	unsigned int thisoff = 0;
	unsigned int thislen = 0;
	struct scatterlist *sg;
	dma_addr_t addr = 0;
	unsigned int len;

	if (xid >= I40E_FCOE_DDP_MAX) {
		dev_warn(&pf->pdev->dev, "xid=0x%x out-of-range\n", xid);
		return 0;
	}

	/* no DDP if we are already down or resetting */
	if (test_bit(__I40E_DOWN, &pf->state) ||
	    test_bit(__I40E_NEEDS_RESTART, &pf->state)) {
		dev_info(&pf->pdev->dev, "xid=0x%x device in reset/down\n",
			 xid);
		return 0;
	}

	ddp = &fcoe->ddp[xid];
	if (ddp->sgl) {
		dev_info(&pf->pdev->dev, "xid 0x%x w/ non-null sgl=%p nents=%d\n",
			 xid, ddp->sgl, ddp->sgc);
		return 0;
	}
	i40e_fcoe_ddp_clear(ddp);

	if (!fcoe->ddp_pool) {
		dev_info(&pf->pdev->dev, "No DDP pool, xid 0x%x\n", xid);
		return 0;
	}

	ddp_pool = per_cpu_ptr(fcoe->ddp_pool, get_cpu());
	if (!ddp_pool->pool) {
		dev_info(&pf->pdev->dev, "No percpu ddp pool, xid 0x%x\n", xid);
		goto out_noddp;
	}

	/* setup dma from scsi command sgl */
	dmacount = dma_map_sg(&pf->pdev->dev, sgl, sgc, DMA_FROM_DEVICE);
	if (dmacount == 0) {
		dev_info(&pf->pdev->dev, "dma_map_sg for sgl %p, sgc %d failed\n",
			 sgl, sgc);
		goto out_noddp_unmap;
	}

	/* alloc the udl from our ddp pool */
	ddp->udl = dma_pool_alloc(ddp_pool->pool, GFP_ATOMIC, &ddp->udp);
	if (!ddp->udl) {
		dev_info(&pf->pdev->dev,
			 "Failed allocated ddp context, xid 0x%x\n", xid);
		goto out_noddp_unmap;
	}

	j = 0;
	ddp->len = 0;
	for_each_sg(sgl, sg, dmacount, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
		ddp->len += len;
		while (len) {
			/* max number of buffers allowed in one DDP context */
			if (j >= I40E_FCOE_DDP_BUFFCNT_MAX) {
				dev_info(&pf->pdev->dev,
					 "xid=%x:%d,%d,%d:addr=%llx not enough descriptors\n",
					 xid, i, j, dmacount, (u64)addr);
				goto out_noddp_free;
			}

			/* get the offset of length of current buffer */
			thisoff = addr & ((dma_addr_t)bufflen - 1);
			thislen = min_t(unsigned int, (bufflen - thisoff), len);
			/* all but the 1st buffer (j == 0)
			 * must be aligned on bufflen
			 */
			if ((j != 0) && (thisoff))
				goto out_noddp_free;

			/* all but the last buffer
			 * ((i == (dmacount - 1)) && (thislen == len))
			 * must end at bufflen
			 */
			if (((i != (dmacount - 1)) || (thislen != len)) &&
			    ((thislen + thisoff) != bufflen))
				goto out_noddp_free;

			ddp->udl[j] = (u64)(addr - thisoff);
			/* only the first buffer may have none-zero offset */
			if (j == 0)
				firstoff = thisoff;
			len -= thislen;
			addr += thislen;
			j++;
		}
	}
	/* only the last buffer may have non-full bufflen */
	ddp->lastsize = thisoff + thislen;
	ddp->firstoff = firstoff;
	ddp->list_len = j;
	ddp->pool = ddp_pool->pool;
	ddp->sgl = sgl;
	ddp->sgc = sgc;
	ddp->xid = xid;
	if (target_mode)
		set_bit(__I40E_FCOE_DDP_TARGET, &ddp->flags);
	set_bit(__I40E_FCOE_DDP_INITALIZED, &ddp->flags);

	put_cpu();
	return 1; /* Success */

out_noddp_free:
	dma_pool_free(ddp->pool, ddp->udl, ddp->udp);
	i40e_fcoe_ddp_clear(ddp);

out_noddp_unmap:
	dma_unmap_sg(&pf->pdev->dev, sgl, sgc, DMA_FROM_DEVICE);
out_noddp:
	put_cpu();
	return 0;
}

/**
 * i40e_fcoe_ddp_get - called to set up ddp context in initiator mode
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_setup
 * and is expected to be called from ULD, e.g., FCP layer of libfc
 * to set up ddp for the corresponding xid of the given sglist for
 * the corresponding I/O.
 *
 * Returns : 1 for success and 0 for no ddp
 **/
static int i40e_fcoe_ddp_get(struct net_device *netdev, u16 xid,
			     struct scatterlist *sgl, unsigned int sgc)
{
	return i40e_fcoe_ddp_setup(netdev, xid, sgl, sgc, 0);
}

/**
 * i40e_fcoe_ddp_target - called to set up ddp context in target mode
 * @netdev: the corresponding net_device
 * @xid: the exchange id requesting ddp
 * @sgl: the scatter-gather list for this request
 * @sgc: the number of scatter-gather items
 *
 * This is the implementation of net_device_ops.ndo_fcoe_ddp_target
 * and is expected to be called from ULD, e.g., FCP layer of libfc
 * to set up ddp for the corresponding xid of the given sglist for
 * the corresponding I/O. The DDP in target mode is a write I/O request
 * from the initiator.
 *
 * Returns : 1 for success and 0 for no ddp
 **/
static int i40e_fcoe_ddp_target(struct net_device *netdev, u16 xid,
				struct scatterlist *sgl, unsigned int sgc)
{
	return i40e_fcoe_ddp_setup(netdev, xid, sgl, sgc, 1);
}

/**
 * i40e_fcoe_program_ddp - programs the HW DDP related descriptors
 * @tx_ring: transmit ring for this packet
 * @skb:     the packet to be sent out
 * @sof: the SOF to indicate class of service
 *
 * Determine if it is READ/WRITE command, and finds out if there is
 * a matching SW DDP context for this command. DDP is applicable
 * only in case of READ if initiator or WRITE in case of
 * responder (via checking XFER_RDY).
 *
 * Note: caller checks sof and ddp sw context
 *
 * Returns : none
 *
 **/
static void i40e_fcoe_program_ddp(struct i40e_ring *tx_ring,
				  struct sk_buff *skb,
				  struct i40e_fcoe_ddp *ddp, u8 sof)
{
	struct i40e_fcoe_filter_context_desc *filter_desc = NULL;
	struct i40e_fcoe_queue_context_desc *queue_desc = NULL;
	struct i40e_fcoe_ddp_context_desc *ddp_desc = NULL;
	struct i40e_pf *pf = tx_ring->vsi->back;
	u16 i = tx_ring->next_to_use;
	struct fc_frame_header *fh;
	u64 flags_rsvd_lanq = 0;
	bool target_mode;

	/* check if abort is still pending */
	if (test_bit(__I40E_FCOE_DDP_ABORTED, &ddp->flags)) {
		dev_warn(&pf->pdev->dev,
			 "DDP abort is still pending xid:%hx and ddp->flags:%lx:\n",
			 ddp->xid, ddp->flags);
		return;
	}

	/* set the flag to indicate this is programmed */
	if (test_and_set_bit(__I40E_FCOE_DDP_PROGRAMMED, &ddp->flags)) {
		dev_warn(&pf->pdev->dev,
			 "DDP is already programmed for xid:%hx and ddp->flags:%lx:\n",
			 ddp->xid, ddp->flags);
		return;
	}

	/* Prepare the DDP context descriptor */
	ddp_desc = I40E_DDP_CONTEXT_DESC(tx_ring, i);
	i++;
	if (i == tx_ring->count)
		i = 0;

	ddp_desc->type_cmd_foff_lsize =
				cpu_to_le64(I40E_TX_DESC_DTYPE_DDP_CTX	|
				((u64)I40E_FCOE_DDP_CTX_DESC_BSIZE_4K  <<
				I40E_FCOE_DDP_CTX_QW1_CMD_SHIFT)	|
				((u64)ddp->firstoff		       <<
				I40E_FCOE_DDP_CTX_QW1_FOFF_SHIFT)	|
				((u64)ddp->lastsize		       <<
				I40E_FCOE_DDP_CTX_QW1_LSIZE_SHIFT));
	ddp_desc->rsvd = cpu_to_le64(0);

	/* target mode needs last packet in the sequence  */
	target_mode = test_bit(__I40E_FCOE_DDP_TARGET, &ddp->flags);
	if (target_mode)
		ddp_desc->type_cmd_foff_lsize |=
			cpu_to_le64(I40E_FCOE_DDP_CTX_DESC_LASTSEQH);

	/* Prepare queue_context descriptor */
	queue_desc = I40E_QUEUE_CONTEXT_DESC(tx_ring, i++);
	if (i == tx_ring->count)
		i = 0;
	queue_desc->dmaindx_fbase = cpu_to_le64(ddp->xid | ((u64)ddp->udp));
	queue_desc->flen_tph = cpu_to_le64(ddp->list_len |
				((u64)(I40E_FCOE_QUEUE_CTX_DESC_TPHRDESC |
				I40E_FCOE_QUEUE_CTX_DESC_TPHDATA) <<
				I40E_FCOE_QUEUE_CTX_QW1_TPH_SHIFT));

	/* Prepare filter_context_desc */
	filter_desc = I40E_FILTER_CONTEXT_DESC(tx_ring, i);
	i++;
	if (i == tx_ring->count)
		i = 0;

	fh = (struct fc_frame_header *)skb_transport_header(skb);
	filter_desc->param = cpu_to_le32(ntohl(fh->fh_parm_offset));
	filter_desc->seqn = cpu_to_le16(ntohs(fh->fh_seq_cnt));
	filter_desc->rsvd_dmaindx = cpu_to_le16(ddp->xid <<
				I40E_FCOE_FILTER_CTX_QW0_DMAINDX_SHIFT);

	flags_rsvd_lanq = I40E_FCOE_FILTER_CTX_DESC_CTYP_DDP;
	flags_rsvd_lanq |= (u64)(target_mode ?
			I40E_FCOE_FILTER_CTX_DESC_ENODE_RSP :
			I40E_FCOE_FILTER_CTX_DESC_ENODE_INIT);

	flags_rsvd_lanq |= (u64)((sof == FC_SOF_I2 || sof == FC_SOF_N2) ?
			I40E_FCOE_FILTER_CTX_DESC_FC_CLASS2 :
			I40E_FCOE_FILTER_CTX_DESC_FC_CLASS3);

	flags_rsvd_lanq |= ((u64)skb->queue_mapping <<
				I40E_FCOE_FILTER_CTX_QW1_LANQINDX_SHIFT);
	filter_desc->flags_rsvd_lanq = cpu_to_le64(flags_rsvd_lanq);

	/* By this time, all offload related descriptors has been programmed */
	tx_ring->next_to_use = i;
}

/**
 * i40e_fcoe_invalidate_ddp - invalidates DDP in case of abort
 * @tx_ring: transmit ring for this packet
 * @skb: the packet associated w/ this DDP invalidation, i.e., ABTS
 * @ddp: the SW DDP context for this DDP
 *
 * Programs the Tx context descriptor to do DDP invalidation.
 **/
static void i40e_fcoe_invalidate_ddp(struct i40e_ring *tx_ring,
				     struct sk_buff *skb,
				     struct i40e_fcoe_ddp *ddp)
{
	struct i40e_tx_context_desc *context_desc;
	int i;

	if (test_and_set_bit(__I40E_FCOE_DDP_ABORTED, &ddp->flags))
		return;

	i = tx_ring->next_to_use;
	context_desc = I40E_TX_CTXTDESC(tx_ring, i);
	i++;
	if (i == tx_ring->count)
		i = 0;

	context_desc->tunneling_params = cpu_to_le32(0);
	context_desc->l2tag2 = cpu_to_le16(0);
	context_desc->rsvd = cpu_to_le16(0);
	context_desc->type_cmd_tso_mss = cpu_to_le64(
		I40E_TX_DESC_DTYPE_FCOE_CTX |
		(I40E_FCOE_TX_CTX_DESC_OPCODE_DDP_CTX_INVL <<
		I40E_TXD_CTX_QW1_CMD_SHIFT) |
		(I40E_FCOE_TX_CTX_DESC_OPCODE_SINGLE_SEND <<
		I40E_TXD_CTX_QW1_CMD_SHIFT));
	tx_ring->next_to_use = i;
}

/**
 * i40e_fcoe_handle_ddp - check we should setup or invalidate DDP
 * @tx_ring: transmit ring for this packet
 * @skb: the packet to be sent out
 * @sof: the SOF to indicate class of service
 *
 * Determine if it is ABTS/READ/XFER_RDY, and finds out if there is
 * a matching SW DDP context for this command. DDP is applicable
 * only in case of READ if initiator or WRITE in case of
 * responder (via checking XFER_RDY). In case this is an ABTS, send
 * just invalidate the context.
 **/
static void i40e_fcoe_handle_ddp(struct i40e_ring *tx_ring,
				 struct sk_buff *skb, u8 sof)
{
	struct i40e_pf *pf = tx_ring->vsi->back;
	struct i40e_fcoe *fcoe = &pf->fcoe;
	struct fc_frame_header *fh;
	struct i40e_fcoe_ddp *ddp;
	u32 f_ctl;
	u8 r_ctl;
	u16 xid;

	fh = (struct fc_frame_header *)skb_transport_header(skb);
	f_ctl = ntoh24(fh->fh_f_ctl);
	r_ctl = fh->fh_r_ctl;
	ddp = NULL;

	if ((r_ctl == FC_RCTL_DD_DATA_DESC) && (f_ctl & FC_FC_EX_CTX)) {
		/* exchange responder? if so, XFER_RDY for write */
		xid = ntohs(fh->fh_rx_id);
		if (i40e_fcoe_xid_is_valid(xid)) {
			ddp = &fcoe->ddp[xid];
			if ((ddp->xid == xid) &&
			    (test_bit(__I40E_FCOE_DDP_TARGET, &ddp->flags)))
				i40e_fcoe_program_ddp(tx_ring, skb, ddp, sof);
		}
	} else if (r_ctl == FC_RCTL_DD_UNSOL_CMD) {
		/* exchange originator, check READ cmd */
		xid = ntohs(fh->fh_ox_id);
		if (i40e_fcoe_xid_is_valid(xid)) {
			ddp = &fcoe->ddp[xid];
			if ((ddp->xid == xid) &&
			    (!test_bit(__I40E_FCOE_DDP_TARGET, &ddp->flags)))
				i40e_fcoe_program_ddp(tx_ring, skb, ddp, sof);
		}
	} else if (r_ctl == FC_RCTL_BA_ABTS) {
		/* exchange originator, check ABTS */
		xid = ntohs(fh->fh_ox_id);
		if (i40e_fcoe_xid_is_valid(xid)) {
			ddp = &fcoe->ddp[xid];
			if ((ddp->xid == xid) &&
			    (!test_bit(__I40E_FCOE_DDP_TARGET, &ddp->flags)))
				i40e_fcoe_invalidate_ddp(tx_ring, skb, ddp);
		}
	}
}

/**
 * i40e_fcoe_tso - set up FCoE TSO
 * @tx_ring:  ring to send buffer on
 * @skb:      send buffer
 * @tx_flags: collected send information
 * @hdr_len:  the tso header length
 * @sof: the SOF to indicate class of service
 *
 * Note must already have sof checked to be either class 2 or class 3 before
 * calling this function.
 *
 * Returns 1 to indicate sequence segmentation offload is properly setup
 * or returns 0 to indicate no tso is needed, otherwise returns error
 * code to drop the frame.
 **/
static int i40e_fcoe_tso(struct i40e_ring *tx_ring,
			 struct sk_buff *skb,
			 u32 tx_flags, u8 *hdr_len, u8 sof)
{
	struct i40e_tx_context_desc *context_desc;
	u32 cd_type, cd_cmd, cd_tso_len, cd_mss;
	struct fc_frame_header *fh;
	u64 cd_type_cmd_tso_mss;

	/* must match gso type as FCoE */
	if (!skb_is_gso(skb))
		return 0;

	/* is it the expected gso type for FCoE ?*/
	if (skb_shinfo(skb)->gso_type != SKB_GSO_FCOE) {
		netdev_err(skb->dev,
			   "wrong gso type %d:expecting SKB_GSO_FCOE\n",
			   skb_shinfo(skb)->gso_type);
		return -EINVAL;
	}

	/* header and trailer are inserted by hw */
	*hdr_len = skb_transport_offset(skb) + sizeof(struct fc_frame_header) +
		   sizeof(struct fcoe_crc_eof);

	/* check sof to decide a class 2 or 3 TSO */
	if (likely(i40e_fcoe_sof_is_class3(sof)))
		cd_cmd = I40E_FCOE_TX_CTX_DESC_OPCODE_TSO_FC_CLASS3;
	else
		cd_cmd = I40E_FCOE_TX_CTX_DESC_OPCODE_TSO_FC_CLASS2;

	/* param field valid? */
	fh = (struct fc_frame_header *)skb_transport_header(skb);
	if (fh->fh_f_ctl[2] & FC_FC_REL_OFF)
		cd_cmd |= I40E_FCOE_TX_CTX_DESC_RELOFF;

	/* fill the field values */
	cd_type = I40E_TX_DESC_DTYPE_FCOE_CTX;
	cd_tso_len = skb->len - *hdr_len;
	cd_mss = skb_shinfo(skb)->gso_size;
	cd_type_cmd_tso_mss =
		((u64)cd_type  << I40E_TXD_CTX_QW1_DTYPE_SHIFT)     |
		((u64)cd_cmd     << I40E_TXD_CTX_QW1_CMD_SHIFT)	    |
		((u64)cd_tso_len << I40E_TXD_CTX_QW1_TSO_LEN_SHIFT) |
		((u64)cd_mss     << I40E_TXD_CTX_QW1_MSS_SHIFT);

	/* grab the next descriptor */
	context_desc = I40E_TX_CTXTDESC(tx_ring, tx_ring->next_to_use);
	tx_ring->next_to_use++;
	if (tx_ring->next_to_use == tx_ring->count)
		tx_ring->next_to_use = 0;

	context_desc->tunneling_params = 0;
	context_desc->l2tag2 = cpu_to_le16((tx_flags & I40E_TX_FLAGS_VLAN_MASK)
					    >> I40E_TX_FLAGS_VLAN_SHIFT);
	context_desc->type_cmd_tso_mss = cpu_to_le64(cd_type_cmd_tso_mss);

	return 1;
}

/**
 * i40e_fcoe_tx_map - build the tx descriptor
 * @tx_ring:  ring to send buffer on
 * @skb:      send buffer
 * @first:    first buffer info buffer to use
 * @tx_flags: collected send information
 * @hdr_len:  ptr to the size of the packet header
 * @eof:      the frame eof value
 *
 * Note, for FCoE, sof and eof are already checked
 **/
static void i40e_fcoe_tx_map(struct i40e_ring *tx_ring,
			     struct sk_buff *skb,
			     struct i40e_tx_buffer *first,
			     u32 tx_flags, u8 hdr_len, u8 eof)
{
	u32 td_offset = 0;
	u32 td_cmd = 0;
	u32 maclen;

	/* insert CRC */
	td_cmd = I40E_TX_DESC_CMD_ICRC;

	/* setup MACLEN */
	maclen = skb_network_offset(skb);
	if (tx_flags & I40E_TX_FLAGS_SW_VLAN)
		maclen += sizeof(struct vlan_hdr);

	if (skb->protocol == htons(ETH_P_FCOE)) {
		/* for FCoE, maclen should exclude ether type */
		maclen -= 2;
		/* setup type as FCoE and EOF insertion */
		td_cmd |= (I40E_TX_DESC_CMD_FCOET | i40e_fcoe_ctxt_eof(eof));
		/* setup FCoELEN and FCLEN */
		td_offset |= ((((sizeof(struct fcoe_hdr) + 2) >> 2) <<
				I40E_TX_DESC_LENGTH_IPLEN_SHIFT) |
			      ((sizeof(struct fc_frame_header) >> 2) <<
				I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT));
		/* trim to exclude trailer */
		pskb_trim(skb, skb->len - sizeof(struct fcoe_crc_eof));
	}

	/* MACLEN is ether header length in words not bytes */
	td_offset |= (maclen >> 1) << I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

	i40e_tx_map(tx_ring, skb, first, tx_flags, hdr_len, td_cmd, td_offset);
}

/**
 * i40e_fcoe_set_skb_header - adjust skb header point for FIP/FCoE/FC
 * @skb: the skb to be adjusted
 *
 * Returns true if this skb is a FCoE/FIP or VLAN carried FCoE/FIP and then
 * adjusts the skb header pointers correspondingly. Otherwise, returns false.
 **/
static inline int i40e_fcoe_set_skb_header(struct sk_buff *skb)
{
	__be16 protocol = skb->protocol;

	skb_reset_mac_header(skb);
	skb->mac_len = sizeof(struct ethhdr);
	if (protocol == htons(ETH_P_8021Q)) {
		struct vlan_ethhdr *veth = (struct vlan_ethhdr *)eth_hdr(skb);

		protocol = veth->h_vlan_encapsulated_proto;
		skb->mac_len += sizeof(struct vlan_hdr);
	}

	/* FCoE or FIP only */
	if ((protocol != htons(ETH_P_FIP)) &&
	    (protocol != htons(ETH_P_FCOE)))
		return -EINVAL;

	/* set header to L2 of FCoE/FIP */
	skb_set_network_header(skb, skb->mac_len);
	if (protocol == htons(ETH_P_FIP))
		return 0;

	/* set header to L3 of FC */
	skb_set_transport_header(skb, skb->mac_len + sizeof(struct fcoe_hdr));
	return 0;
}

/**
 * i40e_fcoe_xmit_frame - transmit buffer
 * @skb:     send buffer
 * @netdev:  the fcoe netdev
 *
 * Returns 0 if sent, else an error code
 **/
static netdev_tx_t i40e_fcoe_xmit_frame(struct sk_buff *skb,
					struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(skb->dev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_ring *tx_ring = vsi->tx_rings[skb->queue_mapping];
	struct i40e_tx_buffer *first;
	u32 tx_flags = 0;
	u8 hdr_len = 0;
	u8 sof = 0;
	u8 eof = 0;
	int fso;

	if (i40e_fcoe_set_skb_header(skb))
		goto out_drop;

	if (!i40e_xmit_descriptor_count(skb, tx_ring))
		return NETDEV_TX_BUSY;

	/* prepare the xmit flags */
	if (i40e_tx_prepare_vlan_flags(skb, tx_ring, &tx_flags))
		goto out_drop;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_bi[tx_ring->next_to_use];

	/* FIP is a regular L2 traffic w/o offload */
	if (skb->protocol == htons(ETH_P_FIP))
		goto out_send;

	/* check sof and eof, only supports FC Class 2 or 3 */
	if (i40e_fcoe_fc_sof(skb, &sof) || i40e_fcoe_fc_eof(skb, &eof)) {
		netdev_err(netdev, "SOF/EOF error:%02x - %02x\n", sof, eof);
		goto out_drop;
	}

	/* always do FCCRC for FCoE */
	tx_flags |= I40E_TX_FLAGS_FCCRC;

	/* check we should do sequence offload */
	fso = i40e_fcoe_tso(tx_ring, skb, tx_flags, &hdr_len, sof);
	if (fso < 0)
		goto out_drop;
	else if (fso)
		tx_flags |= I40E_TX_FLAGS_FSO;
	else
		i40e_fcoe_handle_ddp(tx_ring, skb, sof);

out_send:
	/* send out the packet */
	i40e_fcoe_tx_map(tx_ring, skb, first, tx_flags, hdr_len, eof);

	i40e_maybe_stop_tx(tx_ring, DESC_NEEDED);
	return NETDEV_TX_OK;

out_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/**
 * i40e_fcoe_change_mtu - NDO callback to change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns error as operation not permitted
 *
 **/
static int i40e_fcoe_change_mtu(struct net_device *netdev, int new_mtu)
{
	netdev_warn(netdev, "MTU change is not supported on FCoE interfaces\n");
	return -EPERM;
}

/**
 * i40e_fcoe_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 *
 **/
static int i40e_fcoe_set_features(struct net_device *netdev,
				  netdev_features_t features)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);

	return 0;
}

static const struct net_device_ops i40e_fcoe_netdev_ops = {
	.ndo_open		= i40e_open,
	.ndo_stop		= i40e_close,
	.ndo_get_stats64	= i40e_get_netdev_stats_struct,
	.ndo_set_rx_mode	= i40e_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= i40e_set_mac,
	.ndo_change_mtu		= i40e_fcoe_change_mtu,
	.ndo_do_ioctl		= i40e_ioctl,
	.ndo_tx_timeout		= i40e_tx_timeout,
	.ndo_vlan_rx_add_vid	= i40e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= i40e_vlan_rx_kill_vid,
	.ndo_setup_tc		= i40e_setup_tc,

#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= i40e_netpoll,
#endif
	.ndo_start_xmit		= i40e_fcoe_xmit_frame,
	.ndo_fcoe_enable	= i40e_fcoe_enable,
	.ndo_fcoe_disable	= i40e_fcoe_disable,
	.ndo_fcoe_ddp_setup	= i40e_fcoe_ddp_get,
	.ndo_fcoe_ddp_done	= i40e_fcoe_ddp_put,
	.ndo_fcoe_ddp_target	= i40e_fcoe_ddp_target,
	.ndo_set_features	= i40e_fcoe_set_features,
};

/* fcoe network device type */
static struct device_type fcoe_netdev_type = {
	.name = "fcoe",
};

/**
 * i40e_fcoe_config_netdev - prepares the VSI context for creating a FCoE VSI
 * @vsi: pointer to the associated VSI struct
 * @ctxt: pointer to the associated VSI context to be passed to HW
 *
 * Returns 0 on success or < 0 on error
 **/
void i40e_fcoe_config_netdev(struct net_device *netdev, struct i40e_vsi *vsi)
{
	struct i40e_hw *hw = &vsi->back->hw;
	struct i40e_pf *pf = vsi->back;

	if (vsi->type != I40E_VSI_FCOE)
		return;

	netdev->features = (NETIF_F_HW_VLAN_CTAG_TX |
			    NETIF_F_HW_VLAN_CTAG_RX |
			    NETIF_F_HW_VLAN_CTAG_FILTER);

	netdev->vlan_features = netdev->features;
	netdev->vlan_features &= ~(NETIF_F_HW_VLAN_CTAG_TX |
				   NETIF_F_HW_VLAN_CTAG_RX |
				   NETIF_F_HW_VLAN_CTAG_FILTER);
	netdev->fcoe_ddp_xid = I40E_FCOE_DDP_MAX - 1;
	netdev->features |= NETIF_F_ALL_FCOE;
	netdev->vlan_features |= NETIF_F_ALL_FCOE;
	netdev->hw_features |= netdev->features;
	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;

	strlcpy(netdev->name, "fcoe%d", IFNAMSIZ-1);
	netdev->mtu = FCOE_MTU;
	SET_NETDEV_DEV(netdev, &pf->pdev->dev);
	SET_NETDEV_DEVTYPE(netdev, &fcoe_netdev_type);
	/* set different dev_port value 1 for FCoE netdev than the default
	 * zero dev_port value for PF netdev, this helps biosdevname user
	 * tool to differentiate them correctly while both attached to the
	 * same PCI function.
	 */
	netdev->dev_port = 1;
	i40e_add_filter(vsi, hw->mac.san_addr, 0, false, false);
	i40e_add_filter(vsi, (u8[6]) FC_FCOE_FLOGI_MAC, 0, false, false);
	i40e_add_filter(vsi, FIP_ALL_FCOE_MACS, 0, false, false);
	i40e_add_filter(vsi, FIP_ALL_ENODE_MACS, 0, false, false);

	/* use san mac */
	ether_addr_copy(netdev->dev_addr, hw->mac.san_addr);
	ether_addr_copy(netdev->perm_addr, hw->mac.san_addr);
	/* fcoe netdev ops */
	netdev->netdev_ops = &i40e_fcoe_netdev_ops;
}

/**
 * i40e_fcoe_vsi_setup - allocate and set up FCoE VSI
 * @pf: the PF that VSI is associated with
 *
 **/
void i40e_fcoe_vsi_setup(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi;
	u16 seid;
	int i;

	if (!(pf->flags & I40E_FLAG_FCOE_ENABLED))
		return;

	BUG_ON(!pf->vsi[pf->lan_vsi]);

	for (i = 0; i < pf->num_alloc_vsi; i++) {
		vsi = pf->vsi[i];
		if (vsi && vsi->type == I40E_VSI_FCOE) {
			dev_warn(&pf->pdev->dev,
				 "FCoE VSI already created\n");
			return;
		}
	}

	seid = pf->vsi[pf->lan_vsi]->seid;
	vsi = i40e_vsi_setup(pf, I40E_VSI_FCOE, seid, 0);
	if (vsi) {
		dev_dbg(&pf->pdev->dev,
			"Successfully created FCoE VSI seid %d id %d uplink_seid %d PF seid %d\n",
			vsi->seid, vsi->id, vsi->uplink_seid, seid);
	} else {
		dev_info(&pf->pdev->dev, "Failed to create FCoE VSI\n");
	}
}
