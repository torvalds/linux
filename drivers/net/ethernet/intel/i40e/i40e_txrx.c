// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/prefetch.h>
#include <linux/bpf_trace.h>
#include <net/xdp.h>
#include "i40e.h"
#include "i40e_trace.h"
#include "i40e_prototype.h"
#include "i40e_txrx_common.h"
#include "i40e_xsk.h"

#define I40E_TXD_CMD (I40E_TX_DESC_CMD_EOP | I40E_TX_DESC_CMD_RS)
/**
 * i40e_fdir - Generate a Flow Director descriptor based on fdata
 * @tx_ring: Tx ring to send buffer on
 * @fdata: Flow director filter data
 * @add: Indicate if we are adding a rule or deleting one
 *
 **/
static void i40e_fdir(struct i40e_ring *tx_ring,
		      struct i40e_fdir_filter *fdata, bool add)
{
	struct i40e_filter_program_desc *fdir_desc;
	struct i40e_pf *pf = tx_ring->vsi->back;
	u32 flex_ptype, dtype_cmd;
	u16 i;

	/* grab the next descriptor */
	i = tx_ring->next_to_use;
	fdir_desc = I40E_TX_FDIRDESC(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	flex_ptype = I40E_TXD_FLTR_QW0_QINDEX_MASK &
		     (fdata->q_index << I40E_TXD_FLTR_QW0_QINDEX_SHIFT);

	flex_ptype |= I40E_TXD_FLTR_QW0_FLEXOFF_MASK &
		      (fdata->flex_off << I40E_TXD_FLTR_QW0_FLEXOFF_SHIFT);

	flex_ptype |= I40E_TXD_FLTR_QW0_PCTYPE_MASK &
		      (fdata->pctype << I40E_TXD_FLTR_QW0_PCTYPE_SHIFT);

	flex_ptype |= I40E_TXD_FLTR_QW0_PCTYPE_MASK &
		      (fdata->flex_offset << I40E_TXD_FLTR_QW0_FLEXOFF_SHIFT);

	/* Use LAN VSI Id if not programmed by user */
	flex_ptype |= I40E_TXD_FLTR_QW0_DEST_VSI_MASK &
		      ((u32)(fdata->dest_vsi ? : pf->vsi[pf->lan_vsi]->id) <<
		       I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT);

	dtype_cmd = I40E_TX_DESC_DTYPE_FILTER_PROG;

	dtype_cmd |= add ?
		     I40E_FILTER_PROGRAM_DESC_PCMD_ADD_UPDATE <<
		     I40E_TXD_FLTR_QW1_PCMD_SHIFT :
		     I40E_FILTER_PROGRAM_DESC_PCMD_REMOVE <<
		     I40E_TXD_FLTR_QW1_PCMD_SHIFT;

	dtype_cmd |= I40E_TXD_FLTR_QW1_DEST_MASK &
		     (fdata->dest_ctl << I40E_TXD_FLTR_QW1_DEST_SHIFT);

	dtype_cmd |= I40E_TXD_FLTR_QW1_FD_STATUS_MASK &
		     (fdata->fd_status << I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT);

	if (fdata->cnt_index) {
		dtype_cmd |= I40E_TXD_FLTR_QW1_CNT_ENA_MASK;
		dtype_cmd |= I40E_TXD_FLTR_QW1_CNTINDEX_MASK &
			     ((u32)fdata->cnt_index <<
			      I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT);
	}

	fdir_desc->qindex_flex_ptype_vsi = cpu_to_le32(flex_ptype);
	fdir_desc->rsvd = cpu_to_le32(0);
	fdir_desc->dtype_cmd_cntindex = cpu_to_le32(dtype_cmd);
	fdir_desc->fd_id = cpu_to_le32(fdata->fd_id);
}

#define I40E_FD_CLEAN_DELAY 10
/**
 * i40e_program_fdir_filter - Program a Flow Director filter
 * @fdir_data: Packet data that will be filter parameters
 * @raw_packet: the pre-allocated packet buffer for FDir
 * @pf: The PF pointer
 * @add: True for add/update, False for remove
 **/
static int i40e_program_fdir_filter(struct i40e_fdir_filter *fdir_data,
				    u8 *raw_packet, struct i40e_pf *pf,
				    bool add)
{
	struct i40e_tx_buffer *tx_buf, *first;
	struct i40e_tx_desc *tx_desc;
	struct i40e_ring *tx_ring;
	struct i40e_vsi *vsi;
	struct device *dev;
	dma_addr_t dma;
	u32 td_cmd = 0;
	u16 i;

	/* find existing FDIR VSI */
	vsi = i40e_find_vsi_by_type(pf, I40E_VSI_FDIR);
	if (!vsi)
		return -ENOENT;

	tx_ring = vsi->tx_rings[0];
	dev = tx_ring->dev;

	/* we need two descriptors to add/del a filter and we can wait */
	for (i = I40E_FD_CLEAN_DELAY; I40E_DESC_UNUSED(tx_ring) < 2; i--) {
		if (!i)
			return -EAGAIN;
		msleep_interruptible(1);
	}

	dma = dma_map_single(dev, raw_packet,
			     I40E_FDIR_MAX_RAW_PACKET_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma))
		goto dma_fail;

	/* grab the next descriptor */
	i = tx_ring->next_to_use;
	first = &tx_ring->tx_bi[i];
	i40e_fdir(tx_ring, fdir_data, add);

	/* Now program a dummy descriptor */
	i = tx_ring->next_to_use;
	tx_desc = I40E_TX_DESC(tx_ring, i);
	tx_buf = &tx_ring->tx_bi[i];

	tx_ring->next_to_use = ((i + 1) < tx_ring->count) ? i + 1 : 0;

	memset(tx_buf, 0, sizeof(struct i40e_tx_buffer));

	/* record length, and DMA address */
	dma_unmap_len_set(tx_buf, len, I40E_FDIR_MAX_RAW_PACKET_SIZE);
	dma_unmap_addr_set(tx_buf, dma, dma);

	tx_desc->buffer_addr = cpu_to_le64(dma);
	td_cmd = I40E_TXD_CMD | I40E_TX_DESC_CMD_DUMMY;

	tx_buf->tx_flags = I40E_TX_FLAGS_FD_SB;
	tx_buf->raw_buf = (void *)raw_packet;

	tx_desc->cmd_type_offset_bsz =
		build_ctob(td_cmd, 0, I40E_FDIR_MAX_RAW_PACKET_SIZE, 0);

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.
	 */
	wmb();

	/* Mark the data descriptor to be watched */
	first->next_to_watch = tx_desc;

	writel(tx_ring->next_to_use, tx_ring->tail);
	return 0;

dma_fail:
	return -1;
}

#define IP_HEADER_OFFSET 14
#define I40E_UDPIP_DUMMY_PACKET_LEN 42
/**
 * i40e_add_del_fdir_udpv4 - Add/Remove UDPv4 filters
 * @vsi: pointer to the targeted VSI
 * @fd_data: the flow director data required for the FDir descriptor
 * @add: true adds a filter, false removes it
 *
 * Returns 0 if the filters were successfully added or removed
 **/
static int i40e_add_del_fdir_udpv4(struct i40e_vsi *vsi,
				   struct i40e_fdir_filter *fd_data,
				   bool add)
{
	struct i40e_pf *pf = vsi->back;
	struct udphdr *udp;
	struct iphdr *ip;
	u8 *raw_packet;
	int ret;
	static char packet[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0,
		0x45, 0, 0, 0x1c, 0, 0, 0x40, 0, 0x40, 0x11, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	raw_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE, GFP_KERNEL);
	if (!raw_packet)
		return -ENOMEM;
	memcpy(raw_packet, packet, I40E_UDPIP_DUMMY_PACKET_LEN);

	ip = (struct iphdr *)(raw_packet + IP_HEADER_OFFSET);
	udp = (struct udphdr *)(raw_packet + IP_HEADER_OFFSET
	      + sizeof(struct iphdr));

	ip->daddr = fd_data->dst_ip;
	udp->dest = fd_data->dst_port;
	ip->saddr = fd_data->src_ip;
	udp->source = fd_data->src_port;

	if (fd_data->flex_filter) {
		u8 *payload = raw_packet + I40E_UDPIP_DUMMY_PACKET_LEN;
		__be16 pattern = fd_data->flex_word;
		u16 off = fd_data->flex_offset;

		*((__force __be16 *)(payload + off)) = pattern;
	}

	fd_data->pctype = I40E_FILTER_PCTYPE_NONF_IPV4_UDP;
	ret = i40e_program_fdir_filter(fd_data, raw_packet, pf, add);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "PCTYPE:%d, Filter command send failed for fd_id:%d (ret = %d)\n",
			 fd_data->pctype, fd_data->fd_id, ret);
		/* Free the packet buffer since it wasn't added to the ring */
		kfree(raw_packet);
		return -EOPNOTSUPP;
	} else if (I40E_DEBUG_FD & pf->hw.debug_mask) {
		if (add)
			dev_info(&pf->pdev->dev,
				 "Filter OK for PCTYPE %d loc = %d\n",
				 fd_data->pctype, fd_data->fd_id);
		else
			dev_info(&pf->pdev->dev,
				 "Filter deleted for PCTYPE %d loc = %d\n",
				 fd_data->pctype, fd_data->fd_id);
	}

	if (add)
		pf->fd_udp4_filter_cnt++;
	else
		pf->fd_udp4_filter_cnt--;

	return 0;
}

#define I40E_TCPIP_DUMMY_PACKET_LEN 54
/**
 * i40e_add_del_fdir_tcpv4 - Add/Remove TCPv4 filters
 * @vsi: pointer to the targeted VSI
 * @fd_data: the flow director data required for the FDir descriptor
 * @add: true adds a filter, false removes it
 *
 * Returns 0 if the filters were successfully added or removed
 **/
static int i40e_add_del_fdir_tcpv4(struct i40e_vsi *vsi,
				   struct i40e_fdir_filter *fd_data,
				   bool add)
{
	struct i40e_pf *pf = vsi->back;
	struct tcphdr *tcp;
	struct iphdr *ip;
	u8 *raw_packet;
	int ret;
	/* Dummy packet */
	static char packet[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0,
		0x45, 0, 0, 0x28, 0, 0, 0x40, 0, 0x40, 0x6, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x11,
		0x0, 0x72, 0, 0, 0, 0};

	raw_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE, GFP_KERNEL);
	if (!raw_packet)
		return -ENOMEM;
	memcpy(raw_packet, packet, I40E_TCPIP_DUMMY_PACKET_LEN);

	ip = (struct iphdr *)(raw_packet + IP_HEADER_OFFSET);
	tcp = (struct tcphdr *)(raw_packet + IP_HEADER_OFFSET
	      + sizeof(struct iphdr));

	ip->daddr = fd_data->dst_ip;
	tcp->dest = fd_data->dst_port;
	ip->saddr = fd_data->src_ip;
	tcp->source = fd_data->src_port;

	if (fd_data->flex_filter) {
		u8 *payload = raw_packet + I40E_TCPIP_DUMMY_PACKET_LEN;
		__be16 pattern = fd_data->flex_word;
		u16 off = fd_data->flex_offset;

		*((__force __be16 *)(payload + off)) = pattern;
	}

	fd_data->pctype = I40E_FILTER_PCTYPE_NONF_IPV4_TCP;
	ret = i40e_program_fdir_filter(fd_data, raw_packet, pf, add);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "PCTYPE:%d, Filter command send failed for fd_id:%d (ret = %d)\n",
			 fd_data->pctype, fd_data->fd_id, ret);
		/* Free the packet buffer since it wasn't added to the ring */
		kfree(raw_packet);
		return -EOPNOTSUPP;
	} else if (I40E_DEBUG_FD & pf->hw.debug_mask) {
		if (add)
			dev_info(&pf->pdev->dev, "Filter OK for PCTYPE %d loc = %d)\n",
				 fd_data->pctype, fd_data->fd_id);
		else
			dev_info(&pf->pdev->dev,
				 "Filter deleted for PCTYPE %d loc = %d\n",
				 fd_data->pctype, fd_data->fd_id);
	}

	if (add) {
		pf->fd_tcp4_filter_cnt++;
		if ((pf->flags & I40E_FLAG_FD_ATR_ENABLED) &&
		    I40E_DEBUG_FD & pf->hw.debug_mask)
			dev_info(&pf->pdev->dev, "Forcing ATR off, sideband rules for TCP/IPv4 flow being applied\n");
		set_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state);
	} else {
		pf->fd_tcp4_filter_cnt--;
	}

	return 0;
}

#define I40E_SCTPIP_DUMMY_PACKET_LEN 46
/**
 * i40e_add_del_fdir_sctpv4 - Add/Remove SCTPv4 Flow Director filters for
 * a specific flow spec
 * @vsi: pointer to the targeted VSI
 * @fd_data: the flow director data required for the FDir descriptor
 * @add: true adds a filter, false removes it
 *
 * Returns 0 if the filters were successfully added or removed
 **/
static int i40e_add_del_fdir_sctpv4(struct i40e_vsi *vsi,
				    struct i40e_fdir_filter *fd_data,
				    bool add)
{
	struct i40e_pf *pf = vsi->back;
	struct sctphdr *sctp;
	struct iphdr *ip;
	u8 *raw_packet;
	int ret;
	/* Dummy packet */
	static char packet[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0,
		0x45, 0, 0, 0x20, 0, 0, 0x40, 0, 0x40, 0x84, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	raw_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE, GFP_KERNEL);
	if (!raw_packet)
		return -ENOMEM;
	memcpy(raw_packet, packet, I40E_SCTPIP_DUMMY_PACKET_LEN);

	ip = (struct iphdr *)(raw_packet + IP_HEADER_OFFSET);
	sctp = (struct sctphdr *)(raw_packet + IP_HEADER_OFFSET
	      + sizeof(struct iphdr));

	ip->daddr = fd_data->dst_ip;
	sctp->dest = fd_data->dst_port;
	ip->saddr = fd_data->src_ip;
	sctp->source = fd_data->src_port;

	if (fd_data->flex_filter) {
		u8 *payload = raw_packet + I40E_SCTPIP_DUMMY_PACKET_LEN;
		__be16 pattern = fd_data->flex_word;
		u16 off = fd_data->flex_offset;

		*((__force __be16 *)(payload + off)) = pattern;
	}

	fd_data->pctype = I40E_FILTER_PCTYPE_NONF_IPV4_SCTP;
	ret = i40e_program_fdir_filter(fd_data, raw_packet, pf, add);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "PCTYPE:%d, Filter command send failed for fd_id:%d (ret = %d)\n",
			 fd_data->pctype, fd_data->fd_id, ret);
		/* Free the packet buffer since it wasn't added to the ring */
		kfree(raw_packet);
		return -EOPNOTSUPP;
	} else if (I40E_DEBUG_FD & pf->hw.debug_mask) {
		if (add)
			dev_info(&pf->pdev->dev,
				 "Filter OK for PCTYPE %d loc = %d\n",
				 fd_data->pctype, fd_data->fd_id);
		else
			dev_info(&pf->pdev->dev,
				 "Filter deleted for PCTYPE %d loc = %d\n",
				 fd_data->pctype, fd_data->fd_id);
	}

	if (add)
		pf->fd_sctp4_filter_cnt++;
	else
		pf->fd_sctp4_filter_cnt--;

	return 0;
}

#define I40E_IP_DUMMY_PACKET_LEN 34
/**
 * i40e_add_del_fdir_ipv4 - Add/Remove IPv4 Flow Director filters for
 * a specific flow spec
 * @vsi: pointer to the targeted VSI
 * @fd_data: the flow director data required for the FDir descriptor
 * @add: true adds a filter, false removes it
 *
 * Returns 0 if the filters were successfully added or removed
 **/
static int i40e_add_del_fdir_ipv4(struct i40e_vsi *vsi,
				  struct i40e_fdir_filter *fd_data,
				  bool add)
{
	struct i40e_pf *pf = vsi->back;
	struct iphdr *ip;
	u8 *raw_packet;
	int ret;
	int i;
	static char packet[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0,
		0x45, 0, 0, 0x14, 0, 0, 0x40, 0, 0x40, 0x10, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0};

	for (i = I40E_FILTER_PCTYPE_NONF_IPV4_OTHER;
	     i <= I40E_FILTER_PCTYPE_FRAG_IPV4;	i++) {
		raw_packet = kzalloc(I40E_FDIR_MAX_RAW_PACKET_SIZE, GFP_KERNEL);
		if (!raw_packet)
			return -ENOMEM;
		memcpy(raw_packet, packet, I40E_IP_DUMMY_PACKET_LEN);
		ip = (struct iphdr *)(raw_packet + IP_HEADER_OFFSET);

		ip->saddr = fd_data->src_ip;
		ip->daddr = fd_data->dst_ip;
		ip->protocol = 0;

		if (fd_data->flex_filter) {
			u8 *payload = raw_packet + I40E_IP_DUMMY_PACKET_LEN;
			__be16 pattern = fd_data->flex_word;
			u16 off = fd_data->flex_offset;

			*((__force __be16 *)(payload + off)) = pattern;
		}

		fd_data->pctype = i;
		ret = i40e_program_fdir_filter(fd_data, raw_packet, pf, add);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "PCTYPE:%d, Filter command send failed for fd_id:%d (ret = %d)\n",
				 fd_data->pctype, fd_data->fd_id, ret);
			/* The packet buffer wasn't added to the ring so we
			 * need to free it now.
			 */
			kfree(raw_packet);
			return -EOPNOTSUPP;
		} else if (I40E_DEBUG_FD & pf->hw.debug_mask) {
			if (add)
				dev_info(&pf->pdev->dev,
					 "Filter OK for PCTYPE %d loc = %d\n",
					 fd_data->pctype, fd_data->fd_id);
			else
				dev_info(&pf->pdev->dev,
					 "Filter deleted for PCTYPE %d loc = %d\n",
					 fd_data->pctype, fd_data->fd_id);
		}
	}

	if (add)
		pf->fd_ip4_filter_cnt++;
	else
		pf->fd_ip4_filter_cnt--;

	return 0;
}

/**
 * i40e_add_del_fdir - Build raw packets to add/del fdir filter
 * @vsi: pointer to the targeted VSI
 * @input: filter to add or delete
 * @add: true adds a filter, false removes it
 *
 **/
int i40e_add_del_fdir(struct i40e_vsi *vsi,
		      struct i40e_fdir_filter *input, bool add)
{
	struct i40e_pf *pf = vsi->back;
	int ret;

	switch (input->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		ret = i40e_add_del_fdir_tcpv4(vsi, input, add);
		break;
	case UDP_V4_FLOW:
		ret = i40e_add_del_fdir_udpv4(vsi, input, add);
		break;
	case SCTP_V4_FLOW:
		ret = i40e_add_del_fdir_sctpv4(vsi, input, add);
		break;
	case IP_USER_FLOW:
		switch (input->ip4_proto) {
		case IPPROTO_TCP:
			ret = i40e_add_del_fdir_tcpv4(vsi, input, add);
			break;
		case IPPROTO_UDP:
			ret = i40e_add_del_fdir_udpv4(vsi, input, add);
			break;
		case IPPROTO_SCTP:
			ret = i40e_add_del_fdir_sctpv4(vsi, input, add);
			break;
		case IPPROTO_IP:
			ret = i40e_add_del_fdir_ipv4(vsi, input, add);
			break;
		default:
			/* We cannot support masking based on protocol */
			dev_info(&pf->pdev->dev, "Unsupported IPv4 protocol 0x%02x\n",
				 input->ip4_proto);
			return -EINVAL;
		}
		break;
	default:
		dev_info(&pf->pdev->dev, "Unsupported flow type 0x%02x\n",
			 input->flow_type);
		return -EINVAL;
	}

	/* The buffer allocated here will be normally be freed by
	 * i40e_clean_fdir_tx_irq() as it reclaims resources after transmit
	 * completion. In the event of an error adding the buffer to the FDIR
	 * ring, it will immediately be freed. It may also be freed by
	 * i40e_clean_tx_ring() when closing the VSI.
	 */
	return ret;
}

/**
 * i40e_fd_handle_status - check the Programming Status for FD
 * @rx_ring: the Rx ring for this descriptor
 * @qword0_raw: qword0
 * @qword1: qword1 after le_to_cpu
 * @prog_id: the id originally used for programming
 *
 * This is used to verify if the FD programming or invalidation
 * requested by SW to the HW is successful or not and take actions accordingly.
 **/
static void i40e_fd_handle_status(struct i40e_ring *rx_ring, u64 qword0_raw,
				  u64 qword1, u8 prog_id)
{
	struct i40e_pf *pf = rx_ring->vsi->back;
	struct pci_dev *pdev = pf->pdev;
	struct i40e_16b_rx_wb_qw0 *qw0;
	u32 fcnt_prog, fcnt_avail;
	u32 error;

	qw0 = (struct i40e_16b_rx_wb_qw0 *)&qword0_raw;
	error = (qword1 & I40E_RX_PROG_STATUS_DESC_QW1_ERROR_MASK) >>
		I40E_RX_PROG_STATUS_DESC_QW1_ERROR_SHIFT;

	if (error == BIT(I40E_RX_PROG_STATUS_DESC_FD_TBL_FULL_SHIFT)) {
		pf->fd_inv = le32_to_cpu(qw0->hi_dword.fd_id);
		if (qw0->hi_dword.fd_id != 0 ||
		    (I40E_DEBUG_FD & pf->hw.debug_mask))
			dev_warn(&pdev->dev, "ntuple filter loc = %d, could not be added\n",
				 pf->fd_inv);

		/* Check if the programming error is for ATR.
		 * If so, auto disable ATR and set a state for
		 * flush in progress. Next time we come here if flush is in
		 * progress do nothing, once flush is complete the state will
		 * be cleared.
		 */
		if (test_bit(__I40E_FD_FLUSH_REQUESTED, pf->state))
			return;

		pf->fd_add_err++;
		/* store the current atr filter count */
		pf->fd_atr_cnt = i40e_get_current_atr_cnt(pf);

		if (qw0->hi_dword.fd_id == 0 &&
		    test_bit(__I40E_FD_SB_AUTO_DISABLED, pf->state)) {
			/* These set_bit() calls aren't atomic with the
			 * test_bit() here, but worse case we potentially
			 * disable ATR and queue a flush right after SB
			 * support is re-enabled. That shouldn't cause an
			 * issue in practice
			 */
			set_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state);
			set_bit(__I40E_FD_FLUSH_REQUESTED, pf->state);
		}

		/* filter programming failed most likely due to table full */
		fcnt_prog = i40e_get_global_fd_count(pf);
		fcnt_avail = pf->fdir_pf_filter_count;
		/* If ATR is running fcnt_prog can quickly change,
		 * if we are very close to full, it makes sense to disable
		 * FD ATR/SB and then re-enable it when there is room.
		 */
		if (fcnt_prog >= (fcnt_avail - I40E_FDIR_BUFFER_FULL_MARGIN)) {
			if ((pf->flags & I40E_FLAG_FD_SB_ENABLED) &&
			    !test_and_set_bit(__I40E_FD_SB_AUTO_DISABLED,
					      pf->state))
				if (I40E_DEBUG_FD & pf->hw.debug_mask)
					dev_warn(&pdev->dev, "FD filter space full, new ntuple rules will not be added\n");
		}
	} else if (error == BIT(I40E_RX_PROG_STATUS_DESC_NO_FD_ENTRY_SHIFT)) {
		if (I40E_DEBUG_FD & pf->hw.debug_mask)
			dev_info(&pdev->dev, "ntuple filter fd_id = %d, could not be removed\n",
				 qw0->hi_dword.fd_id);
	}
}

/**
 * i40e_unmap_and_free_tx_resource - Release a Tx buffer
 * @ring:      the ring that owns the buffer
 * @tx_buffer: the buffer to free
 **/
static void i40e_unmap_and_free_tx_resource(struct i40e_ring *ring,
					    struct i40e_tx_buffer *tx_buffer)
{
	if (tx_buffer->skb) {
		if (tx_buffer->tx_flags & I40E_TX_FLAGS_FD_SB)
			kfree(tx_buffer->raw_buf);
		else if (ring_is_xdp(ring))
			xdp_return_frame(tx_buffer->xdpf);
		else
			dev_kfree_skb_any(tx_buffer->skb);
		if (dma_unmap_len(tx_buffer, len))
			dma_unmap_single(ring->dev,
					 dma_unmap_addr(tx_buffer, dma),
					 dma_unmap_len(tx_buffer, len),
					 DMA_TO_DEVICE);
	} else if (dma_unmap_len(tx_buffer, len)) {
		dma_unmap_page(ring->dev,
			       dma_unmap_addr(tx_buffer, dma),
			       dma_unmap_len(tx_buffer, len),
			       DMA_TO_DEVICE);
	}

	tx_buffer->next_to_watch = NULL;
	tx_buffer->skb = NULL;
	dma_unmap_len_set(tx_buffer, len, 0);
	/* tx_buffer must be completely set up in the transmit path */
}

/**
 * i40e_clean_tx_ring - Free any empty Tx buffers
 * @tx_ring: ring to be cleaned
 **/
void i40e_clean_tx_ring(struct i40e_ring *tx_ring)
{
	unsigned long bi_size;
	u16 i;

	if (ring_is_xdp(tx_ring) && tx_ring->xsk_pool) {
		i40e_xsk_clean_tx_ring(tx_ring);
	} else {
		/* ring already cleared, nothing to do */
		if (!tx_ring->tx_bi)
			return;

		/* Free all the Tx ring sk_buffs */
		for (i = 0; i < tx_ring->count; i++)
			i40e_unmap_and_free_tx_resource(tx_ring,
							&tx_ring->tx_bi[i]);
	}

	bi_size = sizeof(struct i40e_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_bi, 0, bi_size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	if (!tx_ring->netdev)
		return;

	/* cleanup Tx queue statistics */
	netdev_tx_reset_queue(txring_txq(tx_ring));
}

/**
 * i40e_free_tx_resources - Free Tx resources per queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/
void i40e_free_tx_resources(struct i40e_ring *tx_ring)
{
	i40e_clean_tx_ring(tx_ring);
	kfree(tx_ring->tx_bi);
	tx_ring->tx_bi = NULL;

	if (tx_ring->desc) {
		dma_free_coherent(tx_ring->dev, tx_ring->size,
				  tx_ring->desc, tx_ring->dma);
		tx_ring->desc = NULL;
	}
}

/**
 * i40e_get_tx_pending - how many tx descriptors not processed
 * @ring: the ring of descriptors
 * @in_sw: use SW variables
 *
 * Since there is no access to the ring head register
 * in XL710, we need to use our local copies
 **/
u32 i40e_get_tx_pending(struct i40e_ring *ring, bool in_sw)
{
	u32 head, tail;

	if (!in_sw) {
		head = i40e_get_head(ring);
		tail = readl(ring->tail);
	} else {
		head = ring->next_to_clean;
		tail = ring->next_to_use;
	}

	if (head != tail)
		return (head < tail) ?
			tail - head : (tail + ring->count - head);

	return 0;
}

/**
 * i40e_detect_recover_hung - Function to detect and recover hung_queues
 * @vsi:  pointer to vsi struct with tx queues
 *
 * VSI has netdev and netdev has TX queues. This function is to check each of
 * those TX queues if they are hung, trigger recovery by issuing SW interrupt.
 **/
void i40e_detect_recover_hung(struct i40e_vsi *vsi)
{
	struct i40e_ring *tx_ring = NULL;
	struct net_device *netdev;
	unsigned int i;
	int packets;

	if (!vsi)
		return;

	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return;

	netdev = vsi->netdev;
	if (!netdev)
		return;

	if (!netif_carrier_ok(netdev))
		return;

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		tx_ring = vsi->tx_rings[i];
		if (tx_ring && tx_ring->desc) {
			/* If packet counter has not changed the queue is
			 * likely stalled, so force an interrupt for this
			 * queue.
			 *
			 * prev_pkt_ctr would be negative if there was no
			 * pending work.
			 */
			packets = tx_ring->stats.packets & INT_MAX;
			if (tx_ring->tx_stats.prev_pkt_ctr == packets) {
				i40e_force_wb(vsi, tx_ring->q_vector);
				continue;
			}

			/* Memory barrier between read of packet count and call
			 * to i40e_get_tx_pending()
			 */
			smp_rmb();
			tx_ring->tx_stats.prev_pkt_ctr =
			    i40e_get_tx_pending(tx_ring, true) ? packets : -1;
		}
	}
}

/**
 * i40e_clean_tx_irq - Reclaim resources after transmit completes
 * @vsi: the VSI we care about
 * @tx_ring: Tx ring to clean
 * @napi_budget: Used to determine if we are in netpoll
 *
 * Returns true if there's any budget left (e.g. the clean is finished)
 **/
static bool i40e_clean_tx_irq(struct i40e_vsi *vsi,
			      struct i40e_ring *tx_ring, int napi_budget)
{
	int i = tx_ring->next_to_clean;
	struct i40e_tx_buffer *tx_buf;
	struct i40e_tx_desc *tx_head;
	struct i40e_tx_desc *tx_desc;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int budget = vsi->work_limit;

	tx_buf = &tx_ring->tx_bi[i];
	tx_desc = I40E_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	tx_head = I40E_TX_DESC(tx_ring, i40e_get_head(tx_ring));

	do {
		struct i40e_tx_desc *eop_desc = tx_buf->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		i40e_trace(clean_tx_irq, tx_ring, tx_desc, tx_buf);
		/* we have caught up to head, no work left to do */
		if (tx_head == tx_desc)
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buf->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buf->bytecount;
		total_packets += tx_buf->gso_segs;

		/* free the skb/XDP data */
		if (ring_is_xdp(tx_ring))
			xdp_return_frame(tx_buf->xdpf);
		else
			napi_consume_skb(tx_buf->skb, napi_budget);

		/* unmap skb header data */
		dma_unmap_single(tx_ring->dev,
				 dma_unmap_addr(tx_buf, dma),
				 dma_unmap_len(tx_buf, len),
				 DMA_TO_DEVICE);

		/* clear tx_buffer data */
		tx_buf->skb = NULL;
		dma_unmap_len_set(tx_buf, len, 0);

		/* unmap remaining buffers */
		while (tx_desc != eop_desc) {
			i40e_trace(clean_tx_irq_unmap,
				   tx_ring, tx_desc, tx_buf);

			tx_buf++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buf = tx_ring->tx_bi;
				tx_desc = I40E_TX_DESC(tx_ring, 0);
			}

			/* unmap any remaining paged data */
			if (dma_unmap_len(tx_buf, len)) {
				dma_unmap_page(tx_ring->dev,
					       dma_unmap_addr(tx_buf, dma),
					       dma_unmap_len(tx_buf, len),
					       DMA_TO_DEVICE);
				dma_unmap_len_set(tx_buf, len, 0);
			}
		}

		/* move us one more past the eop_desc for start of next pkt */
		tx_buf++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buf = tx_ring->tx_bi;
			tx_desc = I40E_TX_DESC(tx_ring, 0);
		}

		prefetch(tx_desc);

		/* update budget accounting */
		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;
	i40e_update_tx_stats(tx_ring, total_packets, total_bytes);
	i40e_arm_wb(tx_ring, vsi, budget);

	if (ring_is_xdp(tx_ring))
		return !!budget;

	/* notify netdev of completed buffers */
	netdev_tx_completed_queue(txring_txq(tx_ring),
				  total_packets, total_bytes);

#define TX_WAKE_THRESHOLD ((s16)(DESC_NEEDED * 2))
	if (unlikely(total_packets && netif_carrier_ok(tx_ring->netdev) &&
		     (I40E_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD))) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (__netif_subqueue_stopped(tx_ring->netdev,
					     tx_ring->queue_index) &&
		   !test_bit(__I40E_VSI_DOWN, vsi->state)) {
			netif_wake_subqueue(tx_ring->netdev,
					    tx_ring->queue_index);
			++tx_ring->tx_stats.restart_queue;
		}
	}

	return !!budget;
}

/**
 * i40e_enable_wb_on_itr - Arm hardware to do a wb, interrupts are not enabled
 * @vsi: the VSI we care about
 * @q_vector: the vector on which to enable writeback
 *
 **/
static void i40e_enable_wb_on_itr(struct i40e_vsi *vsi,
				  struct i40e_q_vector *q_vector)
{
	u16 flags = q_vector->tx.ring[0].flags;
	u32 val;

	if (!(flags & I40E_TXR_FLAGS_WB_ON_ITR))
		return;

	if (q_vector->arm_wb_state)
		return;

	if (vsi->back->flags & I40E_FLAG_MSIX_ENABLED) {
		val = I40E_PFINT_DYN_CTLN_WB_ON_ITR_MASK |
		      I40E_PFINT_DYN_CTLN_ITR_INDX_MASK; /* set noitr */

		wr32(&vsi->back->hw,
		     I40E_PFINT_DYN_CTLN(q_vector->reg_idx),
		     val);
	} else {
		val = I40E_PFINT_DYN_CTL0_WB_ON_ITR_MASK |
		      I40E_PFINT_DYN_CTL0_ITR_INDX_MASK; /* set noitr */

		wr32(&vsi->back->hw, I40E_PFINT_DYN_CTL0, val);
	}
	q_vector->arm_wb_state = true;
}

/**
 * i40e_force_wb - Issue SW Interrupt so HW does a wb
 * @vsi: the VSI we care about
 * @q_vector: the vector  on which to force writeback
 *
 **/
void i40e_force_wb(struct i40e_vsi *vsi, struct i40e_q_vector *q_vector)
{
	if (vsi->back->flags & I40E_FLAG_MSIX_ENABLED) {
		u32 val = I40E_PFINT_DYN_CTLN_INTENA_MASK |
			  I40E_PFINT_DYN_CTLN_ITR_INDX_MASK | /* set noitr */
			  I40E_PFINT_DYN_CTLN_SWINT_TRIG_MASK |
			  I40E_PFINT_DYN_CTLN_SW_ITR_INDX_ENA_MASK;
			  /* allow 00 to be written to the index */

		wr32(&vsi->back->hw,
		     I40E_PFINT_DYN_CTLN(q_vector->reg_idx), val);
	} else {
		u32 val = I40E_PFINT_DYN_CTL0_INTENA_MASK |
			  I40E_PFINT_DYN_CTL0_ITR_INDX_MASK | /* set noitr */
			  I40E_PFINT_DYN_CTL0_SWINT_TRIG_MASK |
			  I40E_PFINT_DYN_CTL0_SW_ITR_INDX_ENA_MASK;
			/* allow 00 to be written to the index */

		wr32(&vsi->back->hw, I40E_PFINT_DYN_CTL0, val);
	}
}

static inline bool i40e_container_is_rx(struct i40e_q_vector *q_vector,
					struct i40e_ring_container *rc)
{
	return &q_vector->rx == rc;
}

static inline unsigned int i40e_itr_divisor(struct i40e_q_vector *q_vector)
{
	unsigned int divisor;

	switch (q_vector->vsi->back->hw.phy.link_info.link_speed) {
	case I40E_LINK_SPEED_40GB:
		divisor = I40E_ITR_ADAPTIVE_MIN_INC * 1024;
		break;
	case I40E_LINK_SPEED_25GB:
	case I40E_LINK_SPEED_20GB:
		divisor = I40E_ITR_ADAPTIVE_MIN_INC * 512;
		break;
	default:
	case I40E_LINK_SPEED_10GB:
		divisor = I40E_ITR_ADAPTIVE_MIN_INC * 256;
		break;
	case I40E_LINK_SPEED_1GB:
	case I40E_LINK_SPEED_100MB:
		divisor = I40E_ITR_ADAPTIVE_MIN_INC * 32;
		break;
	}

	return divisor;
}

/**
 * i40e_update_itr - update the dynamic ITR value based on statistics
 * @q_vector: structure containing interrupt and ring information
 * @rc: structure containing ring performance data
 *
 * Stores a new ITR value based on packets and byte
 * counts during the last interrupt.  The advantage of per interrupt
 * computation is faster updates and more accurate ITR for the current
 * traffic pattern.  Constants in this function were computed
 * based on theoretical maximum wire speed and thresholds were set based
 * on testing data as well as attempting to minimize response time
 * while increasing bulk throughput.
 **/
static void i40e_update_itr(struct i40e_q_vector *q_vector,
			    struct i40e_ring_container *rc)
{
	unsigned int avg_wire_size, packets, bytes, itr;
	unsigned long next_update = jiffies;

	/* If we don't have any rings just leave ourselves set for maximum
	 * possible latency so we take ourselves out of the equation.
	 */
	if (!rc->ring || !ITR_IS_DYNAMIC(rc->ring->itr_setting))
		return;

	/* For Rx we want to push the delay up and default to low latency.
	 * for Tx we want to pull the delay down and default to high latency.
	 */
	itr = i40e_container_is_rx(q_vector, rc) ?
	      I40E_ITR_ADAPTIVE_MIN_USECS | I40E_ITR_ADAPTIVE_LATENCY :
	      I40E_ITR_ADAPTIVE_MAX_USECS | I40E_ITR_ADAPTIVE_LATENCY;

	/* If we didn't update within up to 1 - 2 jiffies we can assume
	 * that either packets are coming in so slow there hasn't been
	 * any work, or that there is so much work that NAPI is dealing
	 * with interrupt moderation and we don't need to do anything.
	 */
	if (time_after(next_update, rc->next_update))
		goto clear_counts;

	/* If itr_countdown is set it means we programmed an ITR within
	 * the last 4 interrupt cycles. This has a side effect of us
	 * potentially firing an early interrupt. In order to work around
	 * this we need to throw out any data received for a few
	 * interrupts following the update.
	 */
	if (q_vector->itr_countdown) {
		itr = rc->target_itr;
		goto clear_counts;
	}

	packets = rc->total_packets;
	bytes = rc->total_bytes;

	if (i40e_container_is_rx(q_vector, rc)) {
		/* If Rx there are 1 to 4 packets and bytes are less than
		 * 9000 assume insufficient data to use bulk rate limiting
		 * approach unless Tx is already in bulk rate limiting. We
		 * are likely latency driven.
		 */
		if (packets && packets < 4 && bytes < 9000 &&
		    (q_vector->tx.target_itr & I40E_ITR_ADAPTIVE_LATENCY)) {
			itr = I40E_ITR_ADAPTIVE_LATENCY;
			goto adjust_by_size;
		}
	} else if (packets < 4) {
		/* If we have Tx and Rx ITR maxed and Tx ITR is running in
		 * bulk mode and we are receiving 4 or fewer packets just
		 * reset the ITR_ADAPTIVE_LATENCY bit for latency mode so
		 * that the Rx can relax.
		 */
		if (rc->target_itr == I40E_ITR_ADAPTIVE_MAX_USECS &&
		    (q_vector->rx.target_itr & I40E_ITR_MASK) ==
		     I40E_ITR_ADAPTIVE_MAX_USECS)
			goto clear_counts;
	} else if (packets > 32) {
		/* If we have processed over 32 packets in a single interrupt
		 * for Tx assume we need to switch over to "bulk" mode.
		 */
		rc->target_itr &= ~I40E_ITR_ADAPTIVE_LATENCY;
	}

	/* We have no packets to actually measure against. This means
	 * either one of the other queues on this vector is active or
	 * we are a Tx queue doing TSO with too high of an interrupt rate.
	 *
	 * Between 4 and 56 we can assume that our current interrupt delay
	 * is only slightly too low. As such we should increase it by a small
	 * fixed amount.
	 */
	if (packets < 56) {
		itr = rc->target_itr + I40E_ITR_ADAPTIVE_MIN_INC;
		if ((itr & I40E_ITR_MASK) > I40E_ITR_ADAPTIVE_MAX_USECS) {
			itr &= I40E_ITR_ADAPTIVE_LATENCY;
			itr += I40E_ITR_ADAPTIVE_MAX_USECS;
		}
		goto clear_counts;
	}

	if (packets <= 256) {
		itr = min(q_vector->tx.current_itr, q_vector->rx.current_itr);
		itr &= I40E_ITR_MASK;

		/* Between 56 and 112 is our "goldilocks" zone where we are
		 * working out "just right". Just report that our current
		 * ITR is good for us.
		 */
		if (packets <= 112)
			goto clear_counts;

		/* If packet count is 128 or greater we are likely looking
		 * at a slight overrun of the delay we want. Try halving
		 * our delay to see if that will cut the number of packets
		 * in half per interrupt.
		 */
		itr /= 2;
		itr &= I40E_ITR_MASK;
		if (itr < I40E_ITR_ADAPTIVE_MIN_USECS)
			itr = I40E_ITR_ADAPTIVE_MIN_USECS;

		goto clear_counts;
	}

	/* The paths below assume we are dealing with a bulk ITR since
	 * number of packets is greater than 256. We are just going to have
	 * to compute a value and try to bring the count under control,
	 * though for smaller packet sizes there isn't much we can do as
	 * NAPI polling will likely be kicking in sooner rather than later.
	 */
	itr = I40E_ITR_ADAPTIVE_BULK;

adjust_by_size:
	/* If packet counts are 256 or greater we can assume we have a gross
	 * overestimation of what the rate should be. Instead of trying to fine
	 * tune it just use the formula below to try and dial in an exact value
	 * give the current packet size of the frame.
	 */
	avg_wire_size = bytes / packets;

	/* The following is a crude approximation of:
	 *  wmem_default / (size + overhead) = desired_pkts_per_int
	 *  rate / bits_per_byte / (size + ethernet overhead) = pkt_rate
	 *  (desired_pkt_rate / pkt_rate) * usecs_per_sec = ITR value
	 *
	 * Assuming wmem_default is 212992 and overhead is 640 bytes per
	 * packet, (256 skb, 64 headroom, 320 shared info), we can reduce the
	 * formula down to
	 *
	 *  (170 * (size + 24)) / (size + 640) = ITR
	 *
	 * We first do some math on the packet size and then finally bitshift
	 * by 8 after rounding up. We also have to account for PCIe link speed
	 * difference as ITR scales based on this.
	 */
	if (avg_wire_size <= 60) {
		/* Start at 250k ints/sec */
		avg_wire_size = 4096;
	} else if (avg_wire_size <= 380) {
		/* 250K ints/sec to 60K ints/sec */
		avg_wire_size *= 40;
		avg_wire_size += 1696;
	} else if (avg_wire_size <= 1084) {
		/* 60K ints/sec to 36K ints/sec */
		avg_wire_size *= 15;
		avg_wire_size += 11452;
	} else if (avg_wire_size <= 1980) {
		/* 36K ints/sec to 30K ints/sec */
		avg_wire_size *= 5;
		avg_wire_size += 22420;
	} else {
		/* plateau at a limit of 30K ints/sec */
		avg_wire_size = 32256;
	}

	/* If we are in low latency mode halve our delay which doubles the
	 * rate to somewhere between 100K to 16K ints/sec
	 */
	if (itr & I40E_ITR_ADAPTIVE_LATENCY)
		avg_wire_size /= 2;

	/* Resultant value is 256 times larger than it needs to be. This
	 * gives us room to adjust the value as needed to either increase
	 * or decrease the value based on link speeds of 10G, 2.5G, 1G, etc.
	 *
	 * Use addition as we have already recorded the new latency flag
	 * for the ITR value.
	 */
	itr += DIV_ROUND_UP(avg_wire_size, i40e_itr_divisor(q_vector)) *
	       I40E_ITR_ADAPTIVE_MIN_INC;

	if ((itr & I40E_ITR_MASK) > I40E_ITR_ADAPTIVE_MAX_USECS) {
		itr &= I40E_ITR_ADAPTIVE_LATENCY;
		itr += I40E_ITR_ADAPTIVE_MAX_USECS;
	}

clear_counts:
	/* write back value */
	rc->target_itr = itr;

	/* next update should occur within next jiffy */
	rc->next_update = next_update + 1;

	rc->total_bytes = 0;
	rc->total_packets = 0;
}

static struct i40e_rx_buffer *i40e_rx_bi(struct i40e_ring *rx_ring, u32 idx)
{
	return &rx_ring->rx_bi[idx];
}

/**
 * i40e_reuse_rx_page - page flip buffer and store it back on the ring
 * @rx_ring: rx descriptor ring to store buffers on
 * @old_buff: donor buffer to have page reused
 *
 * Synchronizes page for reuse by the adapter
 **/
static void i40e_reuse_rx_page(struct i40e_ring *rx_ring,
			       struct i40e_rx_buffer *old_buff)
{
	struct i40e_rx_buffer *new_buff;
	u16 nta = rx_ring->next_to_alloc;

	new_buff = i40e_rx_bi(rx_ring, nta);

	/* update, and store next to alloc */
	nta++;
	rx_ring->next_to_alloc = (nta < rx_ring->count) ? nta : 0;

	/* transfer page from old buffer to new buffer */
	new_buff->dma		= old_buff->dma;
	new_buff->page		= old_buff->page;
	new_buff->page_offset	= old_buff->page_offset;
	new_buff->pagecnt_bias	= old_buff->pagecnt_bias;

	rx_ring->rx_stats.page_reuse_count++;

	/* clear contents of buffer_info */
	old_buff->page = NULL;
}

/**
 * i40e_clean_programming_status - clean the programming status descriptor
 * @rx_ring: the rx ring that has this descriptor
 * @qword0_raw: qword0
 * @qword1: qword1 representing status_error_len in CPU ordering
 *
 * Flow director should handle FD_FILTER_STATUS to check its filter programming
 * status being successful or not and take actions accordingly. FCoE should
 * handle its context/filter programming/invalidation status and take actions.
 *
 * Returns an i40e_rx_buffer to reuse if the cleanup occurred, otherwise NULL.
 **/
void i40e_clean_programming_status(struct i40e_ring *rx_ring, u64 qword0_raw,
				   u64 qword1)
{
	u8 id;

	id = (qword1 & I40E_RX_PROG_STATUS_DESC_QW1_PROGID_MASK) >>
		  I40E_RX_PROG_STATUS_DESC_QW1_PROGID_SHIFT;

	if (id == I40E_RX_PROG_STATUS_DESC_FD_FILTER_STATUS)
		i40e_fd_handle_status(rx_ring, qword0_raw, qword1, id);
}

/**
 * i40e_setup_tx_descriptors - Allocate the Tx descriptors
 * @tx_ring: the tx ring to set up
 *
 * Return 0 on success, negative on error
 **/
int i40e_setup_tx_descriptors(struct i40e_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int bi_size;

	if (!dev)
		return -ENOMEM;

	/* warn if we are about to overwrite the pointer */
	WARN_ON(tx_ring->tx_bi);
	bi_size = sizeof(struct i40e_tx_buffer) * tx_ring->count;
	tx_ring->tx_bi = kzalloc(bi_size, GFP_KERNEL);
	if (!tx_ring->tx_bi)
		goto err;

	u64_stats_init(&tx_ring->syncp);

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(struct i40e_tx_desc);
	/* add u32 for head writeback, align after this takes care of
	 * guaranteeing this is at least one cache line in size
	 */
	tx_ring->size += sizeof(u32);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		dev_info(dev, "Unable to allocate memory for the Tx descriptor ring, size=%d\n",
			 tx_ring->size);
		goto err;
	}

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	tx_ring->tx_stats.prev_pkt_ctr = -1;
	return 0;

err:
	kfree(tx_ring->tx_bi);
	tx_ring->tx_bi = NULL;
	return -ENOMEM;
}

int i40e_alloc_rx_bi(struct i40e_ring *rx_ring)
{
	unsigned long sz = sizeof(*rx_ring->rx_bi) * rx_ring->count;

	rx_ring->rx_bi = kzalloc(sz, GFP_KERNEL);
	return rx_ring->rx_bi ? 0 : -ENOMEM;
}

static void i40e_clear_rx_bi(struct i40e_ring *rx_ring)
{
	memset(rx_ring->rx_bi, 0, sizeof(*rx_ring->rx_bi) * rx_ring->count);
}

/**
 * i40e_clean_rx_ring - Free Rx buffers
 * @rx_ring: ring to be cleaned
 **/
void i40e_clean_rx_ring(struct i40e_ring *rx_ring)
{
	u16 i;

	/* ring already cleared, nothing to do */
	if (!rx_ring->rx_bi)
		return;

	if (rx_ring->skb) {
		dev_kfree_skb(rx_ring->skb);
		rx_ring->skb = NULL;
	}

	if (rx_ring->xsk_pool) {
		i40e_xsk_clean_rx_ring(rx_ring);
		goto skip_free;
	}

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct i40e_rx_buffer *rx_bi = i40e_rx_bi(rx_ring, i);

		if (!rx_bi->page)
			continue;

		/* Invalidate cache lines that may have been written to by
		 * device so that we avoid corrupting memory.
		 */
		dma_sync_single_range_for_cpu(rx_ring->dev,
					      rx_bi->dma,
					      rx_bi->page_offset,
					      rx_ring->rx_buf_len,
					      DMA_FROM_DEVICE);

		/* free resources associated with mapping */
		dma_unmap_page_attrs(rx_ring->dev, rx_bi->dma,
				     i40e_rx_pg_size(rx_ring),
				     DMA_FROM_DEVICE,
				     I40E_RX_DMA_ATTR);

		__page_frag_cache_drain(rx_bi->page, rx_bi->pagecnt_bias);

		rx_bi->page = NULL;
		rx_bi->page_offset = 0;
	}

skip_free:
	if (rx_ring->xsk_pool)
		i40e_clear_rx_bi_zc(rx_ring);
	else
		i40e_clear_rx_bi(rx_ring);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;
}

/**
 * i40e_free_rx_resources - Free Rx resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/
void i40e_free_rx_resources(struct i40e_ring *rx_ring)
{
	i40e_clean_rx_ring(rx_ring);
	if (rx_ring->vsi->type == I40E_VSI_MAIN)
		xdp_rxq_info_unreg(&rx_ring->xdp_rxq);
	rx_ring->xdp_prog = NULL;
	kfree(rx_ring->rx_bi);
	rx_ring->rx_bi = NULL;

	if (rx_ring->desc) {
		dma_free_coherent(rx_ring->dev, rx_ring->size,
				  rx_ring->desc, rx_ring->dma);
		rx_ring->desc = NULL;
	}
}

/**
 * i40e_setup_rx_descriptors - Allocate Rx descriptors
 * @rx_ring: Rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/
int i40e_setup_rx_descriptors(struct i40e_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int err;

	u64_stats_init(&rx_ring->syncp);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * sizeof(union i40e_rx_desc);
	rx_ring->size = ALIGN(rx_ring->size, 4096);
	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);

	if (!rx_ring->desc) {
		dev_info(dev, "Unable to allocate memory for the Rx descriptor ring, size=%d\n",
			 rx_ring->size);
		return -ENOMEM;
	}

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	/* XDP RX-queue info only needed for RX rings exposed to XDP */
	if (rx_ring->vsi->type == I40E_VSI_MAIN) {
		err = xdp_rxq_info_reg(&rx_ring->xdp_rxq, rx_ring->netdev,
				       rx_ring->queue_index);
		if (err < 0)
			return err;
	}

	rx_ring->xdp_prog = rx_ring->vsi->xdp_prog;

	return 0;
}

/**
 * i40e_release_rx_desc - Store the new tail and head values
 * @rx_ring: ring to bump
 * @val: new head index
 **/
void i40e_release_rx_desc(struct i40e_ring *rx_ring, u32 val)
{
	rx_ring->next_to_use = val;

	/* update next to alloc since we have filled the ring */
	rx_ring->next_to_alloc = val;

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}

/**
 * i40e_rx_offset - Return expected offset into page to access data
 * @rx_ring: Ring we are requesting offset of
 *
 * Returns the offset value for ring into the data buffer.
 */
static inline unsigned int i40e_rx_offset(struct i40e_ring *rx_ring)
{
	return ring_uses_build_skb(rx_ring) ? I40E_SKB_PAD : 0;
}

static unsigned int i40e_rx_frame_truesize(struct i40e_ring *rx_ring,
					   unsigned int size)
{
	unsigned int truesize;

#if (PAGE_SIZE < 8192)
	truesize = i40e_rx_pg_size(rx_ring) / 2; /* Must be power-of-2 */
#else
	truesize = i40e_rx_offset(rx_ring) ?
		SKB_DATA_ALIGN(size + i40e_rx_offset(rx_ring)) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) :
		SKB_DATA_ALIGN(size);
#endif
	return truesize;
}

/**
 * i40e_alloc_mapped_page - recycle or make a new page
 * @rx_ring: ring to use
 * @bi: rx_buffer struct to modify
 *
 * Returns true if the page was successfully allocated or
 * reused.
 **/
static bool i40e_alloc_mapped_page(struct i40e_ring *rx_ring,
				   struct i40e_rx_buffer *bi)
{
	struct page *page = bi->page;
	dma_addr_t dma;

	/* since we are recycling buffers we should seldom need to alloc */
	if (likely(page)) {
		rx_ring->rx_stats.page_reuse_count++;
		return true;
	}

	/* alloc new page for storage */
	page = dev_alloc_pages(i40e_rx_pg_order(rx_ring));
	if (unlikely(!page)) {
		rx_ring->rx_stats.alloc_page_failed++;
		return false;
	}

	/* map page for use */
	dma = dma_map_page_attrs(rx_ring->dev, page, 0,
				 i40e_rx_pg_size(rx_ring),
				 DMA_FROM_DEVICE,
				 I40E_RX_DMA_ATTR);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (dma_mapping_error(rx_ring->dev, dma)) {
		__free_pages(page, i40e_rx_pg_order(rx_ring));
		rx_ring->rx_stats.alloc_page_failed++;
		return false;
	}

	bi->dma = dma;
	bi->page = page;
	bi->page_offset = i40e_rx_offset(rx_ring);
	page_ref_add(page, USHRT_MAX - 1);
	bi->pagecnt_bias = USHRT_MAX;

	return true;
}

/**
 * i40e_alloc_rx_buffers - Replace used receive buffers
 * @rx_ring: ring to place buffers on
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 **/
bool i40e_alloc_rx_buffers(struct i40e_ring *rx_ring, u16 cleaned_count)
{
	u16 ntu = rx_ring->next_to_use;
	union i40e_rx_desc *rx_desc;
	struct i40e_rx_buffer *bi;

	/* do nothing if no valid netdev defined */
	if (!rx_ring->netdev || !cleaned_count)
		return false;

	rx_desc = I40E_RX_DESC(rx_ring, ntu);
	bi = i40e_rx_bi(rx_ring, ntu);

	do {
		if (!i40e_alloc_mapped_page(rx_ring, bi))
			goto no_buffers;

		/* sync the buffer for use by the device */
		dma_sync_single_range_for_device(rx_ring->dev, bi->dma,
						 bi->page_offset,
						 rx_ring->rx_buf_len,
						 DMA_FROM_DEVICE);

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		rx_desc->read.pkt_addr = cpu_to_le64(bi->dma + bi->page_offset);

		rx_desc++;
		bi++;
		ntu++;
		if (unlikely(ntu == rx_ring->count)) {
			rx_desc = I40E_RX_DESC(rx_ring, 0);
			bi = i40e_rx_bi(rx_ring, 0);
			ntu = 0;
		}

		/* clear the status bits for the next_to_use descriptor */
		rx_desc->wb.qword1.status_error_len = 0;

		cleaned_count--;
	} while (cleaned_count);

	if (rx_ring->next_to_use != ntu)
		i40e_release_rx_desc(rx_ring, ntu);

	return false;

no_buffers:
	if (rx_ring->next_to_use != ntu)
		i40e_release_rx_desc(rx_ring, ntu);

	/* make sure to come back via polling to try again after
	 * allocation failure
	 */
	return true;
}

/**
 * i40e_rx_checksum - Indicate in skb if hw indicated a good cksum
 * @vsi: the VSI we care about
 * @skb: skb currently being received and modified
 * @rx_desc: the receive descriptor
 **/
static inline void i40e_rx_checksum(struct i40e_vsi *vsi,
				    struct sk_buff *skb,
				    union i40e_rx_desc *rx_desc)
{
	struct i40e_rx_ptype_decoded decoded;
	u32 rx_error, rx_status;
	bool ipv4, ipv6;
	u8 ptype;
	u64 qword;

	qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	ptype = (qword & I40E_RXD_QW1_PTYPE_MASK) >> I40E_RXD_QW1_PTYPE_SHIFT;
	rx_error = (qword & I40E_RXD_QW1_ERROR_MASK) >>
		   I40E_RXD_QW1_ERROR_SHIFT;
	rx_status = (qword & I40E_RXD_QW1_STATUS_MASK) >>
		    I40E_RXD_QW1_STATUS_SHIFT;
	decoded = decode_rx_desc_ptype(ptype);

	skb->ip_summed = CHECKSUM_NONE;

	skb_checksum_none_assert(skb);

	/* Rx csum enabled and ip headers found? */
	if (!(vsi->netdev->features & NETIF_F_RXCSUM))
		return;

	/* did the hardware decode the packet and checksum? */
	if (!(rx_status & BIT(I40E_RX_DESC_STATUS_L3L4P_SHIFT)))
		return;

	/* both known and outer_ip must be set for the below code to work */
	if (!(decoded.known && decoded.outer_ip))
		return;

	ipv4 = (decoded.outer_ip == I40E_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV4);
	ipv6 = (decoded.outer_ip == I40E_RX_PTYPE_OUTER_IP) &&
	       (decoded.outer_ip_ver == I40E_RX_PTYPE_OUTER_IPV6);

	if (ipv4 &&
	    (rx_error & (BIT(I40E_RX_DESC_ERROR_IPE_SHIFT) |
			 BIT(I40E_RX_DESC_ERROR_EIPE_SHIFT))))
		goto checksum_fail;

	/* likely incorrect csum if alternate IP extension headers found */
	if (ipv6 &&
	    rx_status & BIT(I40E_RX_DESC_STATUS_IPV6EXADD_SHIFT))
		/* don't increment checksum err here, non-fatal err */
		return;

	/* there was some L4 error, count error and punt packet to the stack */
	if (rx_error & BIT(I40E_RX_DESC_ERROR_L4E_SHIFT))
		goto checksum_fail;

	/* handle packets that were not able to be checksummed due
	 * to arrival speed, in this case the stack can compute
	 * the csum.
	 */
	if (rx_error & BIT(I40E_RX_DESC_ERROR_PPRS_SHIFT))
		return;

	/* If there is an outer header present that might contain a checksum
	 * we need to bump the checksum level by 1 to reflect the fact that
	 * we are indicating we validated the inner checksum.
	 */
	if (decoded.tunnel_type >= I40E_RX_PTYPE_TUNNEL_IP_GRENAT)
		skb->csum_level = 1;

	/* Only report checksum unnecessary for TCP, UDP, or SCTP */
	switch (decoded.inner_prot) {
	case I40E_RX_PTYPE_INNER_PROT_TCP:
	case I40E_RX_PTYPE_INNER_PROT_UDP:
	case I40E_RX_PTYPE_INNER_PROT_SCTP:
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		fallthrough;
	default:
		break;
	}

	return;

checksum_fail:
	vsi->back->hw_csum_rx_error++;
}

/**
 * i40e_ptype_to_htype - get a hash type
 * @ptype: the ptype value from the descriptor
 *
 * Returns a hash type to be used by skb_set_hash
 **/
static inline int i40e_ptype_to_htype(u8 ptype)
{
	struct i40e_rx_ptype_decoded decoded = decode_rx_desc_ptype(ptype);

	if (!decoded.known)
		return PKT_HASH_TYPE_NONE;

	if (decoded.outer_ip == I40E_RX_PTYPE_OUTER_IP &&
	    decoded.payload_layer == I40E_RX_PTYPE_PAYLOAD_LAYER_PAY4)
		return PKT_HASH_TYPE_L4;
	else if (decoded.outer_ip == I40E_RX_PTYPE_OUTER_IP &&
		 decoded.payload_layer == I40E_RX_PTYPE_PAYLOAD_LAYER_PAY3)
		return PKT_HASH_TYPE_L3;
	else
		return PKT_HASH_TYPE_L2;
}

/**
 * i40e_rx_hash - set the hash value in the skb
 * @ring: descriptor ring
 * @rx_desc: specific descriptor
 * @skb: skb currently being received and modified
 * @rx_ptype: Rx packet type
 **/
static inline void i40e_rx_hash(struct i40e_ring *ring,
				union i40e_rx_desc *rx_desc,
				struct sk_buff *skb,
				u8 rx_ptype)
{
	u32 hash;
	const __le64 rss_mask =
		cpu_to_le64((u64)I40E_RX_DESC_FLTSTAT_RSS_HASH <<
			    I40E_RX_DESC_STATUS_FLTSTAT_SHIFT);

	if (!(ring->netdev->features & NETIF_F_RXHASH))
		return;

	if ((rx_desc->wb.qword1.status_error_len & rss_mask) == rss_mask) {
		hash = le32_to_cpu(rx_desc->wb.qword0.hi_dword.rss);
		skb_set_hash(skb, hash, i40e_ptype_to_htype(rx_ptype));
	}
}

/**
 * i40e_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being populated
 *
 * This function checks the ring, descriptor, and packet information in
 * order to populate the hash, checksum, VLAN, protocol, and
 * other fields within the skb.
 **/
void i40e_process_skb_fields(struct i40e_ring *rx_ring,
			     union i40e_rx_desc *rx_desc, struct sk_buff *skb)
{
	u64 qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);
	u32 rx_status = (qword & I40E_RXD_QW1_STATUS_MASK) >>
			I40E_RXD_QW1_STATUS_SHIFT;
	u32 tsynvalid = rx_status & I40E_RXD_QW1_STATUS_TSYNVALID_MASK;
	u32 tsyn = (rx_status & I40E_RXD_QW1_STATUS_TSYNINDX_MASK) >>
		   I40E_RXD_QW1_STATUS_TSYNINDX_SHIFT;
	u8 rx_ptype = (qword & I40E_RXD_QW1_PTYPE_MASK) >>
		      I40E_RXD_QW1_PTYPE_SHIFT;

	if (unlikely(tsynvalid))
		i40e_ptp_rx_hwtstamp(rx_ring->vsi->back, skb, tsyn);

	i40e_rx_hash(rx_ring, rx_desc, skb, rx_ptype);

	i40e_rx_checksum(rx_ring->vsi, skb, rx_desc);

	skb_record_rx_queue(skb, rx_ring->queue_index);

	if (qword & BIT(I40E_RX_DESC_STATUS_L2TAG1P_SHIFT)) {
		__le16 vlan_tag = rx_desc->wb.qword0.lo_dword.l2tag1;

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
				       le16_to_cpu(vlan_tag));
	}

	/* modifies the skb - consumes the enet header */
	skb->protocol = eth_type_trans(skb, rx_ring->netdev);
}

/**
 * i40e_cleanup_headers - Correct empty headers
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @skb: pointer to current skb being fixed
 * @rx_desc: pointer to the EOP Rx descriptor
 *
 * Also address the case where we are pulling data in on pages only
 * and as such no data is present in the skb header.
 *
 * In addition if skb is not at least 60 bytes we need to pad it so that
 * it is large enough to qualify as a valid Ethernet frame.
 *
 * Returns true if an error was encountered and skb was freed.
 **/
static bool i40e_cleanup_headers(struct i40e_ring *rx_ring, struct sk_buff *skb,
				 union i40e_rx_desc *rx_desc)

{
	/* ERR_MASK will only have valid bits if EOP set, and
	 * what we are doing here is actually checking
	 * I40E_RX_DESC_ERROR_RXE_SHIFT, since it is the zeroth bit in
	 * the error field
	 */
	if (unlikely(i40e_test_staterr(rx_desc,
				       BIT(I40E_RXD_QW1_ERROR_SHIFT)))) {
		dev_kfree_skb_any(skb);
		return true;
	}

	/* if eth_skb_pad returns an error the skb was freed */
	if (eth_skb_pad(skb))
		return true;

	return false;
}

/**
 * i40e_page_is_reusable - check if any reuse is possible
 * @page: page struct to check
 *
 * A page is not reusable if it was allocated under low memory
 * conditions, or it's not in the same NUMA node as this CPU.
 */
static inline bool i40e_page_is_reusable(struct page *page)
{
	return (page_to_nid(page) == numa_mem_id()) &&
		!page_is_pfmemalloc(page);
}

/**
 * i40e_can_reuse_rx_page - Determine if this page can be reused by
 * the adapter for another receive
 *
 * @rx_buffer: buffer containing the page
 * @rx_buffer_pgcnt: buffer page refcount pre xdp_do_redirect() call
 *
 * If page is reusable, rx_buffer->page_offset is adjusted to point to
 * an unused region in the page.
 *
 * For small pages, @truesize will be a constant value, half the size
 * of the memory at page.  We'll attempt to alternate between high and
 * low halves of the page, with one half ready for use by the hardware
 * and the other half being consumed by the stack.  We use the page
 * ref count to determine whether the stack has finished consuming the
 * portion of this page that was passed up with a previous packet.  If
 * the page ref count is >1, we'll assume the "other" half page is
 * still busy, and this page cannot be reused.
 *
 * For larger pages, @truesize will be the actual space used by the
 * received packet (adjusted upward to an even multiple of the cache
 * line size).  This will advance through the page by the amount
 * actually consumed by the received packets while there is still
 * space for a buffer.  Each region of larger pages will be used at
 * most once, after which the page will not be reused.
 *
 * In either case, if the page is reusable its refcount is increased.
 **/
static bool i40e_can_reuse_rx_page(struct i40e_rx_buffer *rx_buffer,
				   int rx_buffer_pgcnt)
{
	unsigned int pagecnt_bias = rx_buffer->pagecnt_bias;
	struct page *page = rx_buffer->page;

	/* Is any reuse possible? */
	if (unlikely(!i40e_page_is_reusable(page)))
		return false;

#if (PAGE_SIZE < 8192)
	/* if we are only owner of page we can reuse it */
	if (unlikely((rx_buffer_pgcnt - pagecnt_bias) > 1))
		return false;
#else
#define I40E_LAST_OFFSET \
	(SKB_WITH_OVERHEAD(PAGE_SIZE) - I40E_RXBUFFER_2048)
	if (rx_buffer->page_offset > I40E_LAST_OFFSET)
		return false;
#endif

	/* If we have drained the page fragment pool we need to update
	 * the pagecnt_bias and page count so that we fully restock the
	 * number of references the driver holds.
	 */
	if (unlikely(pagecnt_bias == 1)) {
		page_ref_add(page, USHRT_MAX - 1);
		rx_buffer->pagecnt_bias = USHRT_MAX;
	}

	return true;
}

/**
 * i40e_add_rx_frag - Add contents of Rx buffer to sk_buff
 * @rx_ring: rx descriptor ring to transact packets on
 * @rx_buffer: buffer containing page to add
 * @skb: sk_buff to place the data into
 * @size: packet length from rx_desc
 *
 * This function will add the data contained in rx_buffer->page to the skb.
 * It will just attach the page as a frag to the skb.
 *
 * The function will then update the page offset.
 **/
static void i40e_add_rx_frag(struct i40e_ring *rx_ring,
			     struct i40e_rx_buffer *rx_buffer,
			     struct sk_buff *skb,
			     unsigned int size)
{
#if (PAGE_SIZE < 8192)
	unsigned int truesize = i40e_rx_pg_size(rx_ring) / 2;
#else
	unsigned int truesize = SKB_DATA_ALIGN(size + i40e_rx_offset(rx_ring));
#endif

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, rx_buffer->page,
			rx_buffer->page_offset, size, truesize);

	/* page is being used so we must update the page offset */
#if (PAGE_SIZE < 8192)
	rx_buffer->page_offset ^= truesize;
#else
	rx_buffer->page_offset += truesize;
#endif
}

/**
 * i40e_get_rx_buffer - Fetch Rx buffer and synchronize data for use
 * @rx_ring: rx descriptor ring to transact packets on
 * @size: size of buffer to add to skb
 * @rx_buffer_pgcnt: buffer page refcount
 *
 * This function will pull an Rx buffer from the ring and synchronize it
 * for use by the CPU.
 */
static struct i40e_rx_buffer *i40e_get_rx_buffer(struct i40e_ring *rx_ring,
						 const unsigned int size,
						 int *rx_buffer_pgcnt)
{
	struct i40e_rx_buffer *rx_buffer;

	rx_buffer = i40e_rx_bi(rx_ring, rx_ring->next_to_clean);
	*rx_buffer_pgcnt =
#if (PAGE_SIZE < 8192)
		page_count(rx_buffer->page);
#else
		0;
#endif
	prefetch_page_address(rx_buffer->page);

	/* we are reusing so sync this buffer for CPU use */
	dma_sync_single_range_for_cpu(rx_ring->dev,
				      rx_buffer->dma,
				      rx_buffer->page_offset,
				      size,
				      DMA_FROM_DEVICE);

	/* We have pulled a buffer for use, so decrement pagecnt_bias */
	rx_buffer->pagecnt_bias--;

	return rx_buffer;
}

/**
 * i40e_construct_skb - Allocate skb and populate it
 * @rx_ring: rx descriptor ring to transact packets on
 * @rx_buffer: rx buffer to pull data from
 * @xdp: xdp_buff pointing to the data
 *
 * This function allocates an skb.  It then populates it with the page
 * data from the current receive descriptor, taking care to set up the
 * skb correctly.
 */
static struct sk_buff *i40e_construct_skb(struct i40e_ring *rx_ring,
					  struct i40e_rx_buffer *rx_buffer,
					  struct xdp_buff *xdp)
{
	unsigned int size = xdp->data_end - xdp->data;
#if (PAGE_SIZE < 8192)
	unsigned int truesize = i40e_rx_pg_size(rx_ring) / 2;
#else
	unsigned int truesize = SKB_DATA_ALIGN(size);
#endif
	unsigned int headlen;
	struct sk_buff *skb;

	/* prefetch first cache line of first page */
	net_prefetch(xdp->data);

	/* Note, we get here by enabling legacy-rx via:
	 *
	 *    ethtool --set-priv-flags <dev> legacy-rx on
	 *
	 * In this mode, we currently get 0 extra XDP headroom as
	 * opposed to having legacy-rx off, where we process XDP
	 * packets going to stack via i40e_build_skb(). The latter
	 * provides us currently with 192 bytes of headroom.
	 *
	 * For i40e_construct_skb() mode it means that the
	 * xdp->data_meta will always point to xdp->data, since
	 * the helper cannot expand the head. Should this ever
	 * change in future for legacy-rx mode on, then lets also
	 * add xdp->data_meta handling here.
	 */

	/* allocate a skb to store the frags */
	skb = __napi_alloc_skb(&rx_ring->q_vector->napi,
			       I40E_RX_HDR_SIZE,
			       GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!skb))
		return NULL;

	/* Determine available headroom for copy */
	headlen = size;
	if (headlen > I40E_RX_HDR_SIZE)
		headlen = eth_get_headlen(skb->dev, xdp->data,
					  I40E_RX_HDR_SIZE);

	/* align pull length to size of long to optimize memcpy performance */
	memcpy(__skb_put(skb, headlen), xdp->data,
	       ALIGN(headlen, sizeof(long)));

	/* update all of the pointers */
	size -= headlen;
	if (size) {
		skb_add_rx_frag(skb, 0, rx_buffer->page,
				rx_buffer->page_offset + headlen,
				size, truesize);

		/* buffer is used by skb, update page_offset */
#if (PAGE_SIZE < 8192)
		rx_buffer->page_offset ^= truesize;
#else
		rx_buffer->page_offset += truesize;
#endif
	} else {
		/* buffer is unused, reset bias back to rx_buffer */
		rx_buffer->pagecnt_bias++;
	}

	return skb;
}

/**
 * i40e_build_skb - Build skb around an existing buffer
 * @rx_ring: Rx descriptor ring to transact packets on
 * @rx_buffer: Rx buffer to pull data from
 * @xdp: xdp_buff pointing to the data
 *
 * This function builds an skb around an existing Rx buffer, taking care
 * to set up the skb correctly and avoid any memcpy overhead.
 */
static struct sk_buff *i40e_build_skb(struct i40e_ring *rx_ring,
				      struct i40e_rx_buffer *rx_buffer,
				      struct xdp_buff *xdp)
{
	unsigned int metasize = xdp->data - xdp->data_meta;
#if (PAGE_SIZE < 8192)
	unsigned int truesize = i40e_rx_pg_size(rx_ring) / 2;
#else
	unsigned int truesize = SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) +
				SKB_DATA_ALIGN(xdp->data_end -
					       xdp->data_hard_start);
#endif
	struct sk_buff *skb;

	/* Prefetch first cache line of first page. If xdp->data_meta
	 * is unused, this points exactly as xdp->data, otherwise we
	 * likely have a consumer accessing first few bytes of meta
	 * data, and then actual data.
	 */
	net_prefetch(xdp->data_meta);

	/* build an skb around the page buffer */
	skb = build_skb(xdp->data_hard_start, truesize);
	if (unlikely(!skb))
		return NULL;

	/* update pointers within the skb to store the data */
	skb_reserve(skb, xdp->data - xdp->data_hard_start);
	__skb_put(skb, xdp->data_end - xdp->data);
	if (metasize)
		skb_metadata_set(skb, metasize);

	/* buffer is used by skb, update page_offset */
#if (PAGE_SIZE < 8192)
	rx_buffer->page_offset ^= truesize;
#else
	rx_buffer->page_offset += truesize;
#endif

	return skb;
}

/**
 * i40e_put_rx_buffer - Clean up used buffer and either recycle or free
 * @rx_ring: rx descriptor ring to transact packets on
 * @rx_buffer: rx buffer to pull data from
 * @rx_buffer_pgcnt: rx buffer page refcount pre xdp_do_redirect() call
 *
 * This function will clean up the contents of the rx_buffer.  It will
 * either recycle the buffer or unmap it and free the associated resources.
 */
static void i40e_put_rx_buffer(struct i40e_ring *rx_ring,
			       struct i40e_rx_buffer *rx_buffer,
			       int rx_buffer_pgcnt)
{
	if (i40e_can_reuse_rx_page(rx_buffer, rx_buffer_pgcnt)) {
		/* hand second half of page back to the ring */
		i40e_reuse_rx_page(rx_ring, rx_buffer);
	} else {
		/* we are not reusing the buffer so unmap it */
		dma_unmap_page_attrs(rx_ring->dev, rx_buffer->dma,
				     i40e_rx_pg_size(rx_ring),
				     DMA_FROM_DEVICE, I40E_RX_DMA_ATTR);
		__page_frag_cache_drain(rx_buffer->page,
					rx_buffer->pagecnt_bias);
		/* clear contents of buffer_info */
		rx_buffer->page = NULL;
	}
}

/**
 * i40e_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 * @skb: Current socket buffer containing buffer in progress
 *
 * This function updates next to clean.  If the buffer is an EOP buffer
 * this function exits returning false, otherwise it will place the
 * sk_buff in the next buffer to be chained and return true indicating
 * that this is in fact a non-EOP buffer.
 **/
static bool i40e_is_non_eop(struct i40e_ring *rx_ring,
			    union i40e_rx_desc *rx_desc,
			    struct sk_buff *skb)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	/* fetch, update, and store next to clean */
	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;

	prefetch(I40E_RX_DESC(rx_ring, ntc));

	/* if we are the last buffer then there is nothing else to do */
#define I40E_RXD_EOF BIT(I40E_RX_DESC_STATUS_EOF_SHIFT)
	if (likely(i40e_test_staterr(rx_desc, I40E_RXD_EOF)))
		return false;

	rx_ring->rx_stats.non_eop_descs++;

	return true;
}

static int i40e_xmit_xdp_ring(struct xdp_frame *xdpf,
			      struct i40e_ring *xdp_ring);

int i40e_xmit_xdp_tx_ring(struct xdp_buff *xdp, struct i40e_ring *xdp_ring)
{
	struct xdp_frame *xdpf = xdp_convert_buff_to_frame(xdp);

	if (unlikely(!xdpf))
		return I40E_XDP_CONSUMED;

	return i40e_xmit_xdp_ring(xdpf, xdp_ring);
}

/**
 * i40e_run_xdp - run an XDP program
 * @rx_ring: Rx ring being processed
 * @xdp: XDP buffer containing the frame
 **/
static int i40e_run_xdp(struct i40e_ring *rx_ring, struct xdp_buff *xdp)
{
	int err, result = I40E_XDP_PASS;
	struct i40e_ring *xdp_ring;
	struct bpf_prog *xdp_prog;
	u32 act;

	rcu_read_lock();
	xdp_prog = READ_ONCE(rx_ring->xdp_prog);

	if (!xdp_prog)
		goto xdp_out;

	prefetchw(xdp->data_hard_start); /* xdp_frame write */

	act = bpf_prog_run_xdp(xdp_prog, xdp);
	switch (act) {
	case XDP_PASS:
		break;
	case XDP_TX:
		xdp_ring = rx_ring->vsi->xdp_rings[rx_ring->queue_index];
		result = i40e_xmit_xdp_tx_ring(xdp, xdp_ring);
		if (result == I40E_XDP_CONSUMED)
			goto out_failure;
		break;
	case XDP_REDIRECT:
		err = xdp_do_redirect(rx_ring->netdev, xdp, xdp_prog);
		if (err)
			goto out_failure;
		result = I40E_XDP_REDIR;
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		fallthrough;
	case XDP_ABORTED:
out_failure:
		trace_xdp_exception(rx_ring->netdev, xdp_prog, act);
		fallthrough; /* handle aborts by dropping packet */
	case XDP_DROP:
		result = I40E_XDP_CONSUMED;
		break;
	}
xdp_out:
	rcu_read_unlock();
	return result;
}

/**
 * i40e_rx_buffer_flip - adjusted rx_buffer to point to an unused region
 * @rx_ring: Rx ring
 * @rx_buffer: Rx buffer to adjust
 * @size: Size of adjustment
 **/
static void i40e_rx_buffer_flip(struct i40e_ring *rx_ring,
				struct i40e_rx_buffer *rx_buffer,
				unsigned int size)
{
	unsigned int truesize = i40e_rx_frame_truesize(rx_ring, size);

#if (PAGE_SIZE < 8192)
	rx_buffer->page_offset ^= truesize;
#else
	rx_buffer->page_offset += truesize;
#endif
}

/**
 * i40e_xdp_ring_update_tail - Updates the XDP Tx ring tail register
 * @xdp_ring: XDP Tx ring
 *
 * This function updates the XDP Tx ring tail register.
 **/
void i40e_xdp_ring_update_tail(struct i40e_ring *xdp_ring)
{
	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.
	 */
	wmb();
	writel_relaxed(xdp_ring->next_to_use, xdp_ring->tail);
}

/**
 * i40e_update_rx_stats - Update Rx ring statistics
 * @rx_ring: rx descriptor ring
 * @total_rx_bytes: number of bytes received
 * @total_rx_packets: number of packets received
 *
 * This function updates the Rx ring statistics.
 **/
void i40e_update_rx_stats(struct i40e_ring *rx_ring,
			  unsigned int total_rx_bytes,
			  unsigned int total_rx_packets)
{
	u64_stats_update_begin(&rx_ring->syncp);
	rx_ring->stats.packets += total_rx_packets;
	rx_ring->stats.bytes += total_rx_bytes;
	u64_stats_update_end(&rx_ring->syncp);
	rx_ring->q_vector->rx.total_packets += total_rx_packets;
	rx_ring->q_vector->rx.total_bytes += total_rx_bytes;
}

/**
 * i40e_finalize_xdp_rx - Bump XDP Tx tail and/or flush redirect map
 * @rx_ring: Rx ring
 * @xdp_res: Result of the receive batch
 *
 * This function bumps XDP Tx tail and/or flush redirect map, and
 * should be called when a batch of packets has been processed in the
 * napi loop.
 **/
void i40e_finalize_xdp_rx(struct i40e_ring *rx_ring, unsigned int xdp_res)
{
	if (xdp_res & I40E_XDP_REDIR)
		xdp_do_flush_map();

	if (xdp_res & I40E_XDP_TX) {
		struct i40e_ring *xdp_ring =
			rx_ring->vsi->xdp_rings[rx_ring->queue_index];

		i40e_xdp_ring_update_tail(xdp_ring);
	}
}

/**
 * i40e_inc_ntc: Advance the next_to_clean index
 * @rx_ring: Rx ring
 **/
static void i40e_inc_ntc(struct i40e_ring *rx_ring)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;
	prefetch(I40E_RX_DESC(rx_ring, ntc));
}

/**
 * i40e_clean_rx_irq - Clean completed descriptors from Rx ring - bounce buf
 * @rx_ring: rx descriptor ring to transact packets on
 * @budget: Total limit on number of packets to process
 *
 * This function provides a "bounce buffer" approach to Rx interrupt
 * processing.  The advantage to this is that on systems that have
 * expensive overhead for IOMMU access this provides a means of avoiding
 * it by maintaining the mapping of the page to the system.
 *
 * Returns amount of work completed
 **/
static int i40e_clean_rx_irq(struct i40e_ring *rx_ring, int budget)
{
	unsigned int total_rx_bytes = 0, total_rx_packets = 0;
	struct sk_buff *skb = rx_ring->skb;
	u16 cleaned_count = I40E_DESC_UNUSED(rx_ring);
	unsigned int xdp_xmit = 0;
	bool failure = false;
	struct xdp_buff xdp;
	int xdp_res = 0;

#if (PAGE_SIZE < 8192)
	xdp.frame_sz = i40e_rx_frame_truesize(rx_ring, 0);
#endif
	xdp.rxq = &rx_ring->xdp_rxq;

	while (likely(total_rx_packets < (unsigned int)budget)) {
		struct i40e_rx_buffer *rx_buffer;
		union i40e_rx_desc *rx_desc;
		int rx_buffer_pgcnt;
		unsigned int size;
		u64 qword;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= I40E_RX_BUFFER_WRITE) {
			failure = failure ||
				  i40e_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = I40E_RX_DESC(rx_ring, rx_ring->next_to_clean);

		/* status_error_len will always be zero for unused descriptors
		 * because it's cleared in cleanup, and overlaps with hdr_addr
		 * which is always zero because packet split isn't used, if the
		 * hardware wrote DD then the length will be non-zero
		 */
		qword = le64_to_cpu(rx_desc->wb.qword1.status_error_len);

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we have
		 * verified the descriptor has been written back.
		 */
		dma_rmb();

		if (i40e_rx_is_programming_status(qword)) {
			i40e_clean_programming_status(rx_ring,
						      rx_desc->raw.qword[0],
						      qword);
			rx_buffer = i40e_rx_bi(rx_ring, rx_ring->next_to_clean);
			i40e_inc_ntc(rx_ring);
			i40e_reuse_rx_page(rx_ring, rx_buffer);
			cleaned_count++;
			continue;
		}

		size = (qword & I40E_RXD_QW1_LENGTH_PBUF_MASK) >>
		       I40E_RXD_QW1_LENGTH_PBUF_SHIFT;
		if (!size)
			break;

		i40e_trace(clean_rx_irq, rx_ring, rx_desc, skb);
		rx_buffer = i40e_get_rx_buffer(rx_ring, size, &rx_buffer_pgcnt);

		/* retrieve a buffer from the ring */
		if (!skb) {
			xdp.data = page_address(rx_buffer->page) +
				   rx_buffer->page_offset;
			xdp.data_meta = xdp.data;
			xdp.data_hard_start = xdp.data -
					      i40e_rx_offset(rx_ring);
			xdp.data_end = xdp.data + size;
#if (PAGE_SIZE > 4096)
			/* At larger PAGE_SIZE, frame_sz depend on len size */
			xdp.frame_sz = i40e_rx_frame_truesize(rx_ring, size);
#endif
			xdp_res = i40e_run_xdp(rx_ring, &xdp);
		}

		if (xdp_res) {
			if (xdp_res & (I40E_XDP_TX | I40E_XDP_REDIR)) {
				xdp_xmit |= xdp_res;
				i40e_rx_buffer_flip(rx_ring, rx_buffer, size);
			} else {
				rx_buffer->pagecnt_bias++;
			}
			total_rx_bytes += size;
			total_rx_packets++;
		} else if (skb) {
			i40e_add_rx_frag(rx_ring, rx_buffer, skb, size);
		} else if (ring_uses_build_skb(rx_ring)) {
			skb = i40e_build_skb(rx_ring, rx_buffer, &xdp);
		} else {
			skb = i40e_construct_skb(rx_ring, rx_buffer, &xdp);
		}

		/* exit if we failed to retrieve a buffer */
		if (!xdp_res && !skb) {
			rx_ring->rx_stats.alloc_buff_failed++;
			rx_buffer->pagecnt_bias++;
			break;
		}

		i40e_put_rx_buffer(rx_ring, rx_buffer, rx_buffer_pgcnt);
		cleaned_count++;

		if (i40e_is_non_eop(rx_ring, rx_desc, skb))
			continue;

		if (xdp_res || i40e_cleanup_headers(rx_ring, skb, rx_desc)) {
			skb = NULL;
			continue;
		}

		/* probably a little skewed due to removing CRC */
		total_rx_bytes += skb->len;

		/* populate checksum, VLAN, and protocol */
		i40e_process_skb_fields(rx_ring, rx_desc, skb);

		i40e_trace(clean_rx_irq_rx, rx_ring, rx_desc, skb);
		napi_gro_receive(&rx_ring->q_vector->napi, skb);
		skb = NULL;

		/* update budget accounting */
		total_rx_packets++;
	}

	i40e_finalize_xdp_rx(rx_ring, xdp_xmit);
	rx_ring->skb = skb;

	i40e_update_rx_stats(rx_ring, total_rx_bytes, total_rx_packets);

	/* guarantee a trip back through this routine if there was a failure */
	return failure ? budget : (int)total_rx_packets;
}

static inline u32 i40e_buildreg_itr(const int type, u16 itr)
{
	u32 val;

	/* We don't bother with setting the CLEARPBA bit as the data sheet
	 * points out doing so is "meaningless since it was already
	 * auto-cleared". The auto-clearing happens when the interrupt is
	 * asserted.
	 *
	 * Hardware errata 28 for also indicates that writing to a
	 * xxINT_DYN_CTLx CSR with INTENA_MSK (bit 31) set to 0 will clear
	 * an event in the PBA anyway so we need to rely on the automask
	 * to hold pending events for us until the interrupt is re-enabled
	 *
	 * The itr value is reported in microseconds, and the register
	 * value is recorded in 2 microsecond units. For this reason we
	 * only need to shift by the interval shift - 1 instead of the
	 * full value.
	 */
	itr &= I40E_ITR_MASK;

	val = I40E_PFINT_DYN_CTLN_INTENA_MASK |
	      (type << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT) |
	      (itr << (I40E_PFINT_DYN_CTLN_INTERVAL_SHIFT - 1));

	return val;
}

/* a small macro to shorten up some long lines */
#define INTREG I40E_PFINT_DYN_CTLN

/* The act of updating the ITR will cause it to immediately trigger. In order
 * to prevent this from throwing off adaptive update statistics we defer the
 * update so that it can only happen so often. So after either Tx or Rx are
 * updated we make the adaptive scheme wait until either the ITR completely
 * expires via the next_update expiration or we have been through at least
 * 3 interrupts.
 */
#define ITR_COUNTDOWN_START 3

/**
 * i40e_update_enable_itr - Update itr and re-enable MSIX interrupt
 * @vsi: the VSI we care about
 * @q_vector: q_vector for which itr is being updated and interrupt enabled
 *
 **/
static inline void i40e_update_enable_itr(struct i40e_vsi *vsi,
					  struct i40e_q_vector *q_vector)
{
	struct i40e_hw *hw = &vsi->back->hw;
	u32 intval;

	/* If we don't have MSIX, then we only need to re-enable icr0 */
	if (!(vsi->back->flags & I40E_FLAG_MSIX_ENABLED)) {
		i40e_irq_dynamic_enable_icr0(vsi->back);
		return;
	}

	/* These will do nothing if dynamic updates are not enabled */
	i40e_update_itr(q_vector, &q_vector->tx);
	i40e_update_itr(q_vector, &q_vector->rx);

	/* This block of logic allows us to get away with only updating
	 * one ITR value with each interrupt. The idea is to perform a
	 * pseudo-lazy update with the following criteria.
	 *
	 * 1. Rx is given higher priority than Tx if both are in same state
	 * 2. If we must reduce an ITR that is given highest priority.
	 * 3. We then give priority to increasing ITR based on amount.
	 */
	if (q_vector->rx.target_itr < q_vector->rx.current_itr) {
		/* Rx ITR needs to be reduced, this is highest priority */
		intval = i40e_buildreg_itr(I40E_RX_ITR,
					   q_vector->rx.target_itr);
		q_vector->rx.current_itr = q_vector->rx.target_itr;
		q_vector->itr_countdown = ITR_COUNTDOWN_START;
	} else if ((q_vector->tx.target_itr < q_vector->tx.current_itr) ||
		   ((q_vector->rx.target_itr - q_vector->rx.current_itr) <
		    (q_vector->tx.target_itr - q_vector->tx.current_itr))) {
		/* Tx ITR needs to be reduced, this is second priority
		 * Tx ITR needs to be increased more than Rx, fourth priority
		 */
		intval = i40e_buildreg_itr(I40E_TX_ITR,
					   q_vector->tx.target_itr);
		q_vector->tx.current_itr = q_vector->tx.target_itr;
		q_vector->itr_countdown = ITR_COUNTDOWN_START;
	} else if (q_vector->rx.current_itr != q_vector->rx.target_itr) {
		/* Rx ITR needs to be increased, third priority */
		intval = i40e_buildreg_itr(I40E_RX_ITR,
					   q_vector->rx.target_itr);
		q_vector->rx.current_itr = q_vector->rx.target_itr;
		q_vector->itr_countdown = ITR_COUNTDOWN_START;
	} else {
		/* No ITR update, lowest priority */
		intval = i40e_buildreg_itr(I40E_ITR_NONE, 0);
		if (q_vector->itr_countdown)
			q_vector->itr_countdown--;
	}

	if (!test_bit(__I40E_VSI_DOWN, vsi->state))
		wr32(hw, INTREG(q_vector->reg_idx), intval);
}

/**
 * i40e_napi_poll - NAPI polling Rx/Tx cleanup routine
 * @napi: napi struct with our devices info in it
 * @budget: amount of work driver is allowed to do this pass, in packets
 *
 * This function will clean all queues associated with a q_vector.
 *
 * Returns the amount of work done
 **/
int i40e_napi_poll(struct napi_struct *napi, int budget)
{
	struct i40e_q_vector *q_vector =
			       container_of(napi, struct i40e_q_vector, napi);
	struct i40e_vsi *vsi = q_vector->vsi;
	struct i40e_ring *ring;
	bool clean_complete = true;
	bool arm_wb = false;
	int budget_per_ring;
	int work_done = 0;

	if (test_bit(__I40E_VSI_DOWN, vsi->state)) {
		napi_complete(napi);
		return 0;
	}

	/* Since the actual Tx work is minimal, we can give the Tx a larger
	 * budget and be more aggressive about cleaning up the Tx descriptors.
	 */
	i40e_for_each_ring(ring, q_vector->tx) {
		bool wd = ring->xsk_pool ?
			  i40e_clean_xdp_tx_irq(vsi, ring) :
			  i40e_clean_tx_irq(vsi, ring, budget);

		if (!wd) {
			clean_complete = false;
			continue;
		}
		arm_wb |= ring->arm_wb;
		ring->arm_wb = false;
	}

	/* Handle case where we are called by netpoll with a budget of 0 */
	if (budget <= 0)
		goto tx_only;

	/* normally we have 1 Rx ring per q_vector */
	if (unlikely(q_vector->num_ringpairs > 1))
		/* We attempt to distribute budget to each Rx queue fairly, but
		 * don't allow the budget to go below 1 because that would exit
		 * polling early.
		 */
		budget_per_ring = max_t(int, budget / q_vector->num_ringpairs, 1);
	else
		/* Max of 1 Rx ring in this q_vector so give it the budget */
		budget_per_ring = budget;

	i40e_for_each_ring(ring, q_vector->rx) {
		int cleaned = ring->xsk_pool ?
			      i40e_clean_rx_irq_zc(ring, budget_per_ring) :
			      i40e_clean_rx_irq(ring, budget_per_ring);

		work_done += cleaned;
		/* if we clean as many as budgeted, we must not be done */
		if (cleaned >= budget_per_ring)
			clean_complete = false;
	}

	/* If work not completed, return budget and polling will return */
	if (!clean_complete) {
		int cpu_id = smp_processor_id();

		/* It is possible that the interrupt affinity has changed but,
		 * if the cpu is pegged at 100%, polling will never exit while
		 * traffic continues and the interrupt will be stuck on this
		 * cpu.  We check to make sure affinity is correct before we
		 * continue to poll, otherwise we must stop polling so the
		 * interrupt can move to the correct cpu.
		 */
		if (!cpumask_test_cpu(cpu_id, &q_vector->affinity_mask)) {
			/* Tell napi that we are done polling */
			napi_complete_done(napi, work_done);

			/* Force an interrupt */
			i40e_force_wb(vsi, q_vector);

			/* Return budget-1 so that polling stops */
			return budget - 1;
		}
tx_only:
		if (arm_wb) {
			q_vector->tx.ring[0].tx_stats.tx_force_wb++;
			i40e_enable_wb_on_itr(vsi, q_vector);
		}
		return budget;
	}

	if (vsi->back->flags & I40E_TXR_FLAGS_WB_ON_ITR)
		q_vector->arm_wb_state = false;

	/* Exit the polling mode, but don't re-enable interrupts if stack might
	 * poll us due to busy-polling
	 */
	if (likely(napi_complete_done(napi, work_done)))
		i40e_update_enable_itr(vsi, q_vector);

	return min(work_done, budget - 1);
}

/**
 * i40e_atr - Add a Flow Director ATR filter
 * @tx_ring:  ring to add programming descriptor to
 * @skb:      send buffer
 * @tx_flags: send tx flags
 **/
static void i40e_atr(struct i40e_ring *tx_ring, struct sk_buff *skb,
		     u32 tx_flags)
{
	struct i40e_filter_program_desc *fdir_desc;
	struct i40e_pf *pf = tx_ring->vsi->back;
	union {
		unsigned char *network;
		struct iphdr *ipv4;
		struct ipv6hdr *ipv6;
	} hdr;
	struct tcphdr *th;
	unsigned int hlen;
	u32 flex_ptype, dtype_cmd;
	int l4_proto;
	u16 i;

	/* make sure ATR is enabled */
	if (!(pf->flags & I40E_FLAG_FD_ATR_ENABLED))
		return;

	if (test_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state))
		return;

	/* if sampling is disabled do nothing */
	if (!tx_ring->atr_sample_rate)
		return;

	/* Currently only IPv4/IPv6 with TCP is supported */
	if (!(tx_flags & (I40E_TX_FLAGS_IPV4 | I40E_TX_FLAGS_IPV6)))
		return;

	/* snag network header to get L4 type and address */
	hdr.network = (tx_flags & I40E_TX_FLAGS_UDP_TUNNEL) ?
		      skb_inner_network_header(skb) : skb_network_header(skb);

	/* Note: tx_flags gets modified to reflect inner protocols in
	 * tx_enable_csum function if encap is enabled.
	 */
	if (tx_flags & I40E_TX_FLAGS_IPV4) {
		/* access ihl as u8 to avoid unaligned access on ia64 */
		hlen = (hdr.network[0] & 0x0F) << 2;
		l4_proto = hdr.ipv4->protocol;
	} else {
		/* find the start of the innermost ipv6 header */
		unsigned int inner_hlen = hdr.network - skb->data;
		unsigned int h_offset = inner_hlen;

		/* this function updates h_offset to the end of the header */
		l4_proto =
		  ipv6_find_hdr(skb, &h_offset, IPPROTO_TCP, NULL, NULL);
		/* hlen will contain our best estimate of the tcp header */
		hlen = h_offset - inner_hlen;
	}

	if (l4_proto != IPPROTO_TCP)
		return;

	th = (struct tcphdr *)(hdr.network + hlen);

	/* Due to lack of space, no more new filters can be programmed */
	if (th->syn && test_bit(__I40E_FD_ATR_AUTO_DISABLED, pf->state))
		return;
	if (pf->flags & I40E_FLAG_HW_ATR_EVICT_ENABLED) {
		/* HW ATR eviction will take care of removing filters on FIN
		 * and RST packets.
		 */
		if (th->fin || th->rst)
			return;
	}

	tx_ring->atr_count++;

	/* sample on all syn/fin/rst packets or once every atr sample rate */
	if (!th->fin &&
	    !th->syn &&
	    !th->rst &&
	    (tx_ring->atr_count < tx_ring->atr_sample_rate))
		return;

	tx_ring->atr_count = 0;

	/* grab the next descriptor */
	i = tx_ring->next_to_use;
	fdir_desc = I40E_TX_FDIRDESC(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	flex_ptype = (tx_ring->queue_index << I40E_TXD_FLTR_QW0_QINDEX_SHIFT) &
		      I40E_TXD_FLTR_QW0_QINDEX_MASK;
	flex_ptype |= (tx_flags & I40E_TX_FLAGS_IPV4) ?
		      (I40E_FILTER_PCTYPE_NONF_IPV4_TCP <<
		       I40E_TXD_FLTR_QW0_PCTYPE_SHIFT) :
		      (I40E_FILTER_PCTYPE_NONF_IPV6_TCP <<
		       I40E_TXD_FLTR_QW0_PCTYPE_SHIFT);

	flex_ptype |= tx_ring->vsi->id << I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT;

	dtype_cmd = I40E_TX_DESC_DTYPE_FILTER_PROG;

	dtype_cmd |= (th->fin || th->rst) ?
		     (I40E_FILTER_PROGRAM_DESC_PCMD_REMOVE <<
		      I40E_TXD_FLTR_QW1_PCMD_SHIFT) :
		     (I40E_FILTER_PROGRAM_DESC_PCMD_ADD_UPDATE <<
		      I40E_TXD_FLTR_QW1_PCMD_SHIFT);

	dtype_cmd |= I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_QINDEX <<
		     I40E_TXD_FLTR_QW1_DEST_SHIFT;

	dtype_cmd |= I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID <<
		     I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT;

	dtype_cmd |= I40E_TXD_FLTR_QW1_CNT_ENA_MASK;
	if (!(tx_flags & I40E_TX_FLAGS_UDP_TUNNEL))
		dtype_cmd |=
			((u32)I40E_FD_ATR_STAT_IDX(pf->hw.pf_id) <<
			I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT) &
			I40E_TXD_FLTR_QW1_CNTINDEX_MASK;
	else
		dtype_cmd |=
			((u32)I40E_FD_ATR_TUNNEL_STAT_IDX(pf->hw.pf_id) <<
			I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT) &
			I40E_TXD_FLTR_QW1_CNTINDEX_MASK;

	if (pf->flags & I40E_FLAG_HW_ATR_EVICT_ENABLED)
		dtype_cmd |= I40E_TXD_FLTR_QW1_ATR_MASK;

	fdir_desc->qindex_flex_ptype_vsi = cpu_to_le32(flex_ptype);
	fdir_desc->rsvd = cpu_to_le32(0);
	fdir_desc->dtype_cmd_cntindex = cpu_to_le32(dtype_cmd);
	fdir_desc->fd_id = cpu_to_le32(0);
}

/**
 * i40e_tx_prepare_vlan_flags - prepare generic TX VLAN tagging flags for HW
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 * @flags:   the tx flags to be set
 *
 * Checks the skb and set up correspondingly several generic transmit flags
 * related to VLAN tagging for the HW, such as VLAN, DCB, etc.
 *
 * Returns error code indicate the frame should be dropped upon error and the
 * otherwise  returns 0 to indicate the flags has been set properly.
 **/
static inline int i40e_tx_prepare_vlan_flags(struct sk_buff *skb,
					     struct i40e_ring *tx_ring,
					     u32 *flags)
{
	__be16 protocol = skb->protocol;
	u32  tx_flags = 0;

	if (protocol == htons(ETH_P_8021Q) &&
	    !(tx_ring->netdev->features & NETIF_F_HW_VLAN_CTAG_TX)) {
		/* When HW VLAN acceleration is turned off by the user the
		 * stack sets the protocol to 8021q so that the driver
		 * can take any steps required to support the SW only
		 * VLAN handling.  In our case the driver doesn't need
		 * to take any further steps so just set the protocol
		 * to the encapsulated ethertype.
		 */
		skb->protocol = vlan_get_protocol(skb);
		goto out;
	}

	/* if we have a HW VLAN tag being added, default to the HW one */
	if (skb_vlan_tag_present(skb)) {
		tx_flags |= skb_vlan_tag_get(skb) << I40E_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= I40E_TX_FLAGS_HW_VLAN;
	/* else if it is a SW VLAN, check the next protocol and store the tag */
	} else if (protocol == htons(ETH_P_8021Q)) {
		struct vlan_hdr *vhdr, _vhdr;

		vhdr = skb_header_pointer(skb, ETH_HLEN, sizeof(_vhdr), &_vhdr);
		if (!vhdr)
			return -EINVAL;

		protocol = vhdr->h_vlan_encapsulated_proto;
		tx_flags |= ntohs(vhdr->h_vlan_TCI) << I40E_TX_FLAGS_VLAN_SHIFT;
		tx_flags |= I40E_TX_FLAGS_SW_VLAN;
	}

	if (!(tx_ring->vsi->back->flags & I40E_FLAG_DCB_ENABLED))
		goto out;

	/* Insert 802.1p priority into VLAN header */
	if ((tx_flags & (I40E_TX_FLAGS_HW_VLAN | I40E_TX_FLAGS_SW_VLAN)) ||
	    (skb->priority != TC_PRIO_CONTROL)) {
		tx_flags &= ~I40E_TX_FLAGS_VLAN_PRIO_MASK;
		tx_flags |= (skb->priority & 0x7) <<
				I40E_TX_FLAGS_VLAN_PRIO_SHIFT;
		if (tx_flags & I40E_TX_FLAGS_SW_VLAN) {
			struct vlan_ethhdr *vhdr;
			int rc;

			rc = skb_cow_head(skb, 0);
			if (rc < 0)
				return rc;
			vhdr = (struct vlan_ethhdr *)skb->data;
			vhdr->h_vlan_TCI = htons(tx_flags >>
						 I40E_TX_FLAGS_VLAN_SHIFT);
		} else {
			tx_flags |= I40E_TX_FLAGS_HW_VLAN;
		}
	}

out:
	*flags = tx_flags;
	return 0;
}

/**
 * i40e_tso - set up the tso context descriptor
 * @first:    pointer to first Tx buffer for xmit
 * @hdr_len:  ptr to the size of the packet header
 * @cd_type_cmd_tso_mss: Quad Word 1
 *
 * Returns 0 if no TSO can happen, 1 if tso is going, or error
 **/
static int i40e_tso(struct i40e_tx_buffer *first, u8 *hdr_len,
		    u64 *cd_type_cmd_tso_mss)
{
	struct sk_buff *skb = first->skb;
	u64 cd_cmd, cd_tso_len, cd_mss;
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		struct udphdr *udp;
		unsigned char *hdr;
	} l4;
	u32 paylen, l4_offset;
	u16 gso_segs, gso_size;
	int err;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* initialize outer IP header fields */
	if (ip.v4->version == 4) {
		ip.v4->tot_len = 0;
		ip.v4->check = 0;
	} else {
		ip.v6->payload_len = 0;
	}

	if (skb_shinfo(skb)->gso_type & (SKB_GSO_GRE |
					 SKB_GSO_GRE_CSUM |
					 SKB_GSO_IPXIP4 |
					 SKB_GSO_IPXIP6 |
					 SKB_GSO_UDP_TUNNEL |
					 SKB_GSO_UDP_TUNNEL_CSUM)) {
		if (!(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL) &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM)) {
			l4.udp->len = 0;

			/* determine offset of outer transport header */
			l4_offset = l4.hdr - skb->data;

			/* remove payload length from outer checksum */
			paylen = skb->len - l4_offset;
			csum_replace_by_diff(&l4.udp->check,
					     (__force __wsum)htonl(paylen));
		}

		/* reset pointers to inner headers */
		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);

		/* initialize inner IP header fields */
		if (ip.v4->version == 4) {
			ip.v4->tot_len = 0;
			ip.v4->check = 0;
		} else {
			ip.v6->payload_len = 0;
		}
	}

	/* determine offset of inner transport header */
	l4_offset = l4.hdr - skb->data;

	/* remove payload length from inner checksum */
	paylen = skb->len - l4_offset;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		csum_replace_by_diff(&l4.udp->check, (__force __wsum)htonl(paylen));
		/* compute length of segmentation header */
		*hdr_len = sizeof(*l4.udp) + l4_offset;
	} else {
		csum_replace_by_diff(&l4.tcp->check, (__force __wsum)htonl(paylen));
		/* compute length of segmentation header */
		*hdr_len = (l4.tcp->doff * 4) + l4_offset;
	}

	/* pull values out of skb_shinfo */
	gso_size = skb_shinfo(skb)->gso_size;
	gso_segs = skb_shinfo(skb)->gso_segs;

	/* update GSO size and bytecount with header size */
	first->gso_segs = gso_segs;
	first->bytecount += (first->gso_segs - 1) * *hdr_len;

	/* find the field values */
	cd_cmd = I40E_TX_CTX_DESC_TSO;
	cd_tso_len = skb->len - *hdr_len;
	cd_mss = gso_size;
	*cd_type_cmd_tso_mss |= (cd_cmd << I40E_TXD_CTX_QW1_CMD_SHIFT) |
				(cd_tso_len << I40E_TXD_CTX_QW1_TSO_LEN_SHIFT) |
				(cd_mss << I40E_TXD_CTX_QW1_MSS_SHIFT);
	return 1;
}

/**
 * i40e_tsyn - set up the tsyn context descriptor
 * @tx_ring:  ptr to the ring to send
 * @skb:      ptr to the skb we're sending
 * @tx_flags: the collected send information
 * @cd_type_cmd_tso_mss: Quad Word 1
 *
 * Returns 0 if no Tx timestamp can happen and 1 if the timestamp will happen
 **/
static int i40e_tsyn(struct i40e_ring *tx_ring, struct sk_buff *skb,
		     u32 tx_flags, u64 *cd_type_cmd_tso_mss)
{
	struct i40e_pf *pf;

	if (likely(!(skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP)))
		return 0;

	/* Tx timestamps cannot be sampled when doing TSO */
	if (tx_flags & I40E_TX_FLAGS_TSO)
		return 0;

	/* only timestamp the outbound packet if the user has requested it and
	 * we are not already transmitting a packet to be timestamped
	 */
	pf = i40e_netdev_to_pf(tx_ring->netdev);
	if (!(pf->flags & I40E_FLAG_PTP))
		return 0;

	if (pf->ptp_tx &&
	    !test_and_set_bit_lock(__I40E_PTP_TX_IN_PROGRESS, pf->state)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		pf->ptp_tx_start = jiffies;
		pf->ptp_tx_skb = skb_get(skb);
	} else {
		pf->tx_hwtstamp_skipped++;
		return 0;
	}

	*cd_type_cmd_tso_mss |= (u64)I40E_TX_CTX_DESC_TSYN <<
				I40E_TXD_CTX_QW1_CMD_SHIFT;

	return 1;
}

/**
 * i40e_tx_enable_csum - Enable Tx checksum offloads
 * @skb: send buffer
 * @tx_flags: pointer to Tx flags currently set
 * @td_cmd: Tx descriptor command bits to set
 * @td_offset: Tx descriptor header offsets to set
 * @tx_ring: Tx descriptor ring
 * @cd_tunneling: ptr to context desc bits
 **/
static int i40e_tx_enable_csum(struct sk_buff *skb, u32 *tx_flags,
			       u32 *td_cmd, u32 *td_offset,
			       struct i40e_ring *tx_ring,
			       u32 *cd_tunneling)
{
	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;
	union {
		struct tcphdr *tcp;
		struct udphdr *udp;
		unsigned char *hdr;
	} l4;
	unsigned char *exthdr;
	u32 offset, cmd = 0;
	__be16 frag_off;
	u8 l4_proto = 0;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);

	/* compute outer L2 header size */
	offset = ((ip.hdr - skb->data) / 2) << I40E_TX_DESC_LENGTH_MACLEN_SHIFT;

	if (skb->encapsulation) {
		u32 tunnel = 0;
		/* define outer network header type */
		if (*tx_flags & I40E_TX_FLAGS_IPV4) {
			tunnel |= (*tx_flags & I40E_TX_FLAGS_TSO) ?
				  I40E_TX_CTX_EXT_IP_IPV4 :
				  I40E_TX_CTX_EXT_IP_IPV4_NO_CSUM;

			l4_proto = ip.v4->protocol;
		} else if (*tx_flags & I40E_TX_FLAGS_IPV6) {
			int ret;

			tunnel |= I40E_TX_CTX_EXT_IP_IPV6;

			exthdr = ip.hdr + sizeof(*ip.v6);
			l4_proto = ip.v6->nexthdr;
			ret = ipv6_skip_exthdr(skb, exthdr - skb->data,
					       &l4_proto, &frag_off);
			if (ret < 0)
				return -1;
		}

		/* define outer transport */
		switch (l4_proto) {
		case IPPROTO_UDP:
			tunnel |= I40E_TXD_CTX_UDP_TUNNELING;
			*tx_flags |= I40E_TX_FLAGS_UDP_TUNNEL;
			break;
		case IPPROTO_GRE:
			tunnel |= I40E_TXD_CTX_GRE_TUNNELING;
			*tx_flags |= I40E_TX_FLAGS_UDP_TUNNEL;
			break;
		case IPPROTO_IPIP:
		case IPPROTO_IPV6:
			*tx_flags |= I40E_TX_FLAGS_UDP_TUNNEL;
			l4.hdr = skb_inner_network_header(skb);
			break;
		default:
			if (*tx_flags & I40E_TX_FLAGS_TSO)
				return -1;

			skb_checksum_help(skb);
			return 0;
		}

		/* compute outer L3 header size */
		tunnel |= ((l4.hdr - ip.hdr) / 4) <<
			  I40E_TXD_CTX_QW0_EXT_IPLEN_SHIFT;

		/* switch IP header pointer from outer to inner header */
		ip.hdr = skb_inner_network_header(skb);

		/* compute tunnel header size */
		tunnel |= ((ip.hdr - l4.hdr) / 2) <<
			  I40E_TXD_CTX_QW0_NATLEN_SHIFT;

		/* indicate if we need to offload outer UDP header */
		if ((*tx_flags & I40E_TX_FLAGS_TSO) &&
		    !(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL) &&
		    (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_TUNNEL_CSUM))
			tunnel |= I40E_TXD_CTX_QW0_L4T_CS_MASK;

		/* record tunnel offload values */
		*cd_tunneling |= tunnel;

		/* switch L4 header pointer from outer to inner */
		l4.hdr = skb_inner_transport_header(skb);
		l4_proto = 0;

		/* reset type as we transition from outer to inner headers */
		*tx_flags &= ~(I40E_TX_FLAGS_IPV4 | I40E_TX_FLAGS_IPV6);
		if (ip.v4->version == 4)
			*tx_flags |= I40E_TX_FLAGS_IPV4;
		if (ip.v6->version == 6)
			*tx_flags |= I40E_TX_FLAGS_IPV6;
	}

	/* Enable IP checksum offloads */
	if (*tx_flags & I40E_TX_FLAGS_IPV4) {
		l4_proto = ip.v4->protocol;
		/* the stack computes the IP header already, the only time we
		 * need the hardware to recompute it is in the case of TSO.
		 */
		cmd |= (*tx_flags & I40E_TX_FLAGS_TSO) ?
		       I40E_TX_DESC_CMD_IIPT_IPV4_CSUM :
		       I40E_TX_DESC_CMD_IIPT_IPV4;
	} else if (*tx_flags & I40E_TX_FLAGS_IPV6) {
		cmd |= I40E_TX_DESC_CMD_IIPT_IPV6;

		exthdr = ip.hdr + sizeof(*ip.v6);
		l4_proto = ip.v6->nexthdr;
		if (l4.hdr != exthdr)
			ipv6_skip_exthdr(skb, exthdr - skb->data,
					 &l4_proto, &frag_off);
	}

	/* compute inner L3 header size */
	offset |= ((l4.hdr - ip.hdr) / 4) << I40E_TX_DESC_LENGTH_IPLEN_SHIFT;

	/* Enable L4 checksum offloads */
	switch (l4_proto) {
	case IPPROTO_TCP:
		/* enable checksum offloads */
		cmd |= I40E_TX_DESC_CMD_L4T_EOFT_TCP;
		offset |= l4.tcp->doff << I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	case IPPROTO_SCTP:
		/* enable SCTP checksum offload */
		cmd |= I40E_TX_DESC_CMD_L4T_EOFT_SCTP;
		offset |= (sizeof(struct sctphdr) >> 2) <<
			  I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	case IPPROTO_UDP:
		/* enable UDP checksum offload */
		cmd |= I40E_TX_DESC_CMD_L4T_EOFT_UDP;
		offset |= (sizeof(struct udphdr) >> 2) <<
			  I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT;
		break;
	default:
		if (*tx_flags & I40E_TX_FLAGS_TSO)
			return -1;
		skb_checksum_help(skb);
		return 0;
	}

	*td_cmd |= cmd;
	*td_offset |= offset;

	return 1;
}

/**
 * i40e_create_tx_ctx Build the Tx context descriptor
 * @tx_ring:  ring to create the descriptor on
 * @cd_type_cmd_tso_mss: Quad Word 1
 * @cd_tunneling: Quad Word 0 - bits 0-31
 * @cd_l2tag2: Quad Word 0 - bits 32-63
 **/
static void i40e_create_tx_ctx(struct i40e_ring *tx_ring,
			       const u64 cd_type_cmd_tso_mss,
			       const u32 cd_tunneling, const u32 cd_l2tag2)
{
	struct i40e_tx_context_desc *context_desc;
	int i = tx_ring->next_to_use;

	if ((cd_type_cmd_tso_mss == I40E_TX_DESC_DTYPE_CONTEXT) &&
	    !cd_tunneling && !cd_l2tag2)
		return;

	/* grab the next descriptor */
	context_desc = I40E_TX_CTXTDESC(tx_ring, i);

	i++;
	tx_ring->next_to_use = (i < tx_ring->count) ? i : 0;

	/* cpu_to_le32 and assign to struct fields */
	context_desc->tunneling_params = cpu_to_le32(cd_tunneling);
	context_desc->l2tag2 = cpu_to_le16(cd_l2tag2);
	context_desc->rsvd = cpu_to_le16(0);
	context_desc->type_cmd_tso_mss = cpu_to_le64(cd_type_cmd_tso_mss);
}

/**
 * __i40e_maybe_stop_tx - 2nd level check for tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns -EBUSY if a stop is needed, else 0
 **/
int __i40e_maybe_stop_tx(struct i40e_ring *tx_ring, int size)
{
	netif_stop_subqueue(tx_ring->netdev, tx_ring->queue_index);
	/* Memory barrier before checking head and tail */
	smp_mb();

	/* Check again in a case another CPU has just made room available. */
	if (likely(I40E_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! - use start_queue because it doesn't call schedule */
	netif_start_subqueue(tx_ring->netdev, tx_ring->queue_index);
	++tx_ring->tx_stats.restart_queue;
	return 0;
}

/**
 * __i40e_chk_linearize - Check if there are more than 8 buffers per packet
 * @skb:      send buffer
 *
 * Note: Our HW can't DMA more than 8 buffers to build a packet on the wire
 * and so we need to figure out the cases where we need to linearize the skb.
 *
 * For TSO we need to count the TSO header and segment payload separately.
 * As such we need to check cases where we have 7 fragments or more as we
 * can potentially require 9 DMA transactions, 1 for the TSO header, 1 for
 * the segment payload in the first descriptor, and another 7 for the
 * fragments.
 **/
bool __i40e_chk_linearize(struct sk_buff *skb)
{
	const skb_frag_t *frag, *stale;
	int nr_frags, sum;

	/* no need to check if number of frags is less than 7 */
	nr_frags = skb_shinfo(skb)->nr_frags;
	if (nr_frags < (I40E_MAX_BUFFER_TXD - 1))
		return false;

	/* We need to walk through the list and validate that each group
	 * of 6 fragments totals at least gso_size.
	 */
	nr_frags -= I40E_MAX_BUFFER_TXD - 2;
	frag = &skb_shinfo(skb)->frags[0];

	/* Initialize size to the negative value of gso_size minus 1.  We
	 * use this as the worst case scenerio in which the frag ahead
	 * of us only provides one byte which is why we are limited to 6
	 * descriptors for a single transmit as the header and previous
	 * fragment are already consuming 2 descriptors.
	 */
	sum = 1 - skb_shinfo(skb)->gso_size;

	/* Add size of frags 0 through 4 to create our initial sum */
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);
	sum += skb_frag_size(frag++);

	/* Walk through fragments adding latest fragment, testing it, and
	 * then removing stale fragments from the sum.
	 */
	for (stale = &skb_shinfo(skb)->frags[0];; stale++) {
		int stale_size = skb_frag_size(stale);

		sum += skb_frag_size(frag++);

		/* The stale fragment may present us with a smaller
		 * descriptor than the actual fragment size. To account
		 * for that we need to remove all the data on the front and
		 * figure out what the remainder would be in the last
		 * descriptor associated with the fragment.
		 */
		if (stale_size > I40E_MAX_DATA_PER_TXD) {
			int align_pad = -(skb_frag_off(stale)) &
					(I40E_MAX_READ_REQ_SIZE - 1);

			sum -= align_pad;
			stale_size -= align_pad;

			do {
				sum -= I40E_MAX_DATA_PER_TXD_ALIGNED;
				stale_size -= I40E_MAX_DATA_PER_TXD_ALIGNED;
			} while (stale_size > I40E_MAX_DATA_PER_TXD);
		}

		/* if sum is negative we failed to make sufficient progress */
		if (sum < 0)
			return true;

		if (!nr_frags--)
			break;

		sum -= stale_size;
	}

	return false;
}

/**
 * i40e_tx_map - Build the Tx descriptor
 * @tx_ring:  ring to send buffer on
 * @skb:      send buffer
 * @first:    first buffer info buffer to use
 * @tx_flags: collected send information
 * @hdr_len:  size of the packet header
 * @td_cmd:   the command field in the descriptor
 * @td_offset: offset for checksum or crc
 *
 * Returns 0 on success, -1 on failure to DMA
 **/
static inline int i40e_tx_map(struct i40e_ring *tx_ring, struct sk_buff *skb,
			      struct i40e_tx_buffer *first, u32 tx_flags,
			      const u8 hdr_len, u32 td_cmd, u32 td_offset)
{
	unsigned int data_len = skb->data_len;
	unsigned int size = skb_headlen(skb);
	skb_frag_t *frag;
	struct i40e_tx_buffer *tx_bi;
	struct i40e_tx_desc *tx_desc;
	u16 i = tx_ring->next_to_use;
	u32 td_tag = 0;
	dma_addr_t dma;
	u16 desc_count = 1;

	if (tx_flags & I40E_TX_FLAGS_HW_VLAN) {
		td_cmd |= I40E_TX_DESC_CMD_IL2TAG1;
		td_tag = (tx_flags & I40E_TX_FLAGS_VLAN_MASK) >>
			 I40E_TX_FLAGS_VLAN_SHIFT;
	}

	first->tx_flags = tx_flags;

	dma = dma_map_single(tx_ring->dev, skb->data, size, DMA_TO_DEVICE);

	tx_desc = I40E_TX_DESC(tx_ring, i);
	tx_bi = first;

	for (frag = &skb_shinfo(skb)->frags[0];; frag++) {
		unsigned int max_data = I40E_MAX_DATA_PER_TXD_ALIGNED;

		if (dma_mapping_error(tx_ring->dev, dma))
			goto dma_error;

		/* record length, and DMA address */
		dma_unmap_len_set(tx_bi, len, size);
		dma_unmap_addr_set(tx_bi, dma, dma);

		/* align size to end of page */
		max_data += -dma & (I40E_MAX_READ_REQ_SIZE - 1);
		tx_desc->buffer_addr = cpu_to_le64(dma);

		while (unlikely(size > I40E_MAX_DATA_PER_TXD)) {
			tx_desc->cmd_type_offset_bsz =
				build_ctob(td_cmd, td_offset,
					   max_data, td_tag);

			tx_desc++;
			i++;
			desc_count++;

			if (i == tx_ring->count) {
				tx_desc = I40E_TX_DESC(tx_ring, 0);
				i = 0;
			}

			dma += max_data;
			size -= max_data;

			max_data = I40E_MAX_DATA_PER_TXD_ALIGNED;
			tx_desc->buffer_addr = cpu_to_le64(dma);
		}

		if (likely(!data_len))
			break;

		tx_desc->cmd_type_offset_bsz = build_ctob(td_cmd, td_offset,
							  size, td_tag);

		tx_desc++;
		i++;
		desc_count++;

		if (i == tx_ring->count) {
			tx_desc = I40E_TX_DESC(tx_ring, 0);
			i = 0;
		}

		size = skb_frag_size(frag);
		data_len -= size;

		dma = skb_frag_dma_map(tx_ring->dev, frag, 0, size,
				       DMA_TO_DEVICE);

		tx_bi = &tx_ring->tx_bi[i];
	}

	netdev_tx_sent_queue(txring_txq(tx_ring), first->bytecount);

	i++;
	if (i == tx_ring->count)
		i = 0;

	tx_ring->next_to_use = i;

	i40e_maybe_stop_tx(tx_ring, DESC_NEEDED);

	/* write last descriptor with EOP bit */
	td_cmd |= I40E_TX_DESC_CMD_EOP;

	/* We OR these values together to check both against 4 (WB_STRIDE)
	 * below. This is safe since we don't re-use desc_count afterwards.
	 */
	desc_count |= ++tx_ring->packet_stride;

	if (desc_count >= WB_STRIDE) {
		/* write last descriptor with RS bit set */
		td_cmd |= I40E_TX_DESC_CMD_RS;
		tx_ring->packet_stride = 0;
	}

	tx_desc->cmd_type_offset_bsz =
			build_ctob(td_cmd, td_offset, size, td_tag);

	skb_tx_timestamp(skb);

	/* Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.
	 *
	 * We also use this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	wmb();

	/* set next_to_watch value indicating a packet is present */
	first->next_to_watch = tx_desc;

	/* notify HW of packet */
	if (netif_xmit_stopped(txring_txq(tx_ring)) || !netdev_xmit_more()) {
		writel(i, tx_ring->tail);
	}

	return 0;

dma_error:
	dev_info(tx_ring->dev, "TX DMA map failed\n");

	/* clear dma mappings for failed tx_bi map */
	for (;;) {
		tx_bi = &tx_ring->tx_bi[i];
		i40e_unmap_and_free_tx_resource(tx_ring, tx_bi);
		if (tx_bi == first)
			break;
		if (i == 0)
			i = tx_ring->count;
		i--;
	}

	tx_ring->next_to_use = i;

	return -1;
}

/**
 * i40e_xmit_xdp_ring - transmits an XDP buffer to an XDP Tx ring
 * @xdpf: data to transmit
 * @xdp_ring: XDP Tx ring
 **/
static int i40e_xmit_xdp_ring(struct xdp_frame *xdpf,
			      struct i40e_ring *xdp_ring)
{
	u16 i = xdp_ring->next_to_use;
	struct i40e_tx_buffer *tx_bi;
	struct i40e_tx_desc *tx_desc;
	void *data = xdpf->data;
	u32 size = xdpf->len;
	dma_addr_t dma;

	if (!unlikely(I40E_DESC_UNUSED(xdp_ring))) {
		xdp_ring->tx_stats.tx_busy++;
		return I40E_XDP_CONSUMED;
	}
	dma = dma_map_single(xdp_ring->dev, data, size, DMA_TO_DEVICE);
	if (dma_mapping_error(xdp_ring->dev, dma))
		return I40E_XDP_CONSUMED;

	tx_bi = &xdp_ring->tx_bi[i];
	tx_bi->bytecount = size;
	tx_bi->gso_segs = 1;
	tx_bi->xdpf = xdpf;

	/* record length, and DMA address */
	dma_unmap_len_set(tx_bi, len, size);
	dma_unmap_addr_set(tx_bi, dma, dma);

	tx_desc = I40E_TX_DESC(xdp_ring, i);
	tx_desc->buffer_addr = cpu_to_le64(dma);
	tx_desc->cmd_type_offset_bsz = build_ctob(I40E_TX_DESC_CMD_ICRC
						  | I40E_TXD_CMD,
						  0, size, 0);

	/* Make certain all of the status bits have been updated
	 * before next_to_watch is written.
	 */
	smp_wmb();

	xdp_ring->xdp_tx_active++;
	i++;
	if (i == xdp_ring->count)
		i = 0;

	tx_bi->next_to_watch = tx_desc;
	xdp_ring->next_to_use = i;

	return I40E_XDP_TX;
}

/**
 * i40e_xmit_frame_ring - Sends buffer on Tx ring
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 **/
static netdev_tx_t i40e_xmit_frame_ring(struct sk_buff *skb,
					struct i40e_ring *tx_ring)
{
	u64 cd_type_cmd_tso_mss = I40E_TX_DESC_DTYPE_CONTEXT;
	u32 cd_tunneling = 0, cd_l2tag2 = 0;
	struct i40e_tx_buffer *first;
	u32 td_offset = 0;
	u32 tx_flags = 0;
	__be16 protocol;
	u32 td_cmd = 0;
	u8 hdr_len = 0;
	int tso, count;
	int tsyn;

	/* prefetch the data, we'll need it later */
	prefetch(skb->data);

	i40e_trace(xmit_frame_ring, skb, tx_ring);

	count = i40e_xmit_descriptor_count(skb);
	if (i40e_chk_linearize(skb, count)) {
		if (__skb_linearize(skb)) {
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
		count = i40e_txd_use_count(skb->len);
		tx_ring->tx_stats.tx_linearize++;
	}

	/* need: 1 descriptor per page * PAGE_SIZE/I40E_MAX_DATA_PER_TXD,
	 *       + 1 desc for skb_head_len/I40E_MAX_DATA_PER_TXD,
	 *       + 4 desc gap to avoid the cache line where head is,
	 *       + 1 desc for context descriptor,
	 * otherwise try next time
	 */
	if (i40e_maybe_stop_tx(tx_ring, count + 4 + 1)) {
		tx_ring->tx_stats.tx_busy++;
		return NETDEV_TX_BUSY;
	}

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_bi[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = skb->len;
	first->gso_segs = 1;

	/* prepare the xmit flags */
	if (i40e_tx_prepare_vlan_flags(skb, tx_ring, &tx_flags))
		goto out_drop;

	/* obtain protocol of skb */
	protocol = vlan_get_protocol(skb);

	/* setup IPv4/IPv6 offloads */
	if (protocol == htons(ETH_P_IP))
		tx_flags |= I40E_TX_FLAGS_IPV4;
	else if (protocol == htons(ETH_P_IPV6))
		tx_flags |= I40E_TX_FLAGS_IPV6;

	tso = i40e_tso(first, &hdr_len, &cd_type_cmd_tso_mss);

	if (tso < 0)
		goto out_drop;
	else if (tso)
		tx_flags |= I40E_TX_FLAGS_TSO;

	/* Always offload the checksum, since it's in the data descriptor */
	tso = i40e_tx_enable_csum(skb, &tx_flags, &td_cmd, &td_offset,
				  tx_ring, &cd_tunneling);
	if (tso < 0)
		goto out_drop;

	tsyn = i40e_tsyn(tx_ring, skb, tx_flags, &cd_type_cmd_tso_mss);

	if (tsyn)
		tx_flags |= I40E_TX_FLAGS_TSYN;

	/* always enable CRC insertion offload */
	td_cmd |= I40E_TX_DESC_CMD_ICRC;

	i40e_create_tx_ctx(tx_ring, cd_type_cmd_tso_mss,
			   cd_tunneling, cd_l2tag2);

	/* Add Flow Director ATR if it's enabled.
	 *
	 * NOTE: this must always be directly before the data descriptor.
	 */
	i40e_atr(tx_ring, skb, tx_flags);

	if (i40e_tx_map(tx_ring, skb, first, tx_flags, hdr_len,
			td_cmd, td_offset))
		goto cleanup_tx_tstamp;

	return NETDEV_TX_OK;

out_drop:
	i40e_trace(xmit_frame_ring_drop, first->skb, tx_ring);
	dev_kfree_skb_any(first->skb);
	first->skb = NULL;
cleanup_tx_tstamp:
	if (unlikely(tx_flags & I40E_TX_FLAGS_TSYN)) {
		struct i40e_pf *pf = i40e_netdev_to_pf(tx_ring->netdev);

		dev_kfree_skb_any(pf->ptp_tx_skb);
		pf->ptp_tx_skb = NULL;
		clear_bit_unlock(__I40E_PTP_TX_IN_PROGRESS, pf->state);
	}

	return NETDEV_TX_OK;
}

/**
 * i40e_lan_xmit_frame - Selects the correct VSI and Tx queue to send buffer
 * @skb:    send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 **/
netdev_tx_t i40e_lan_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_ring *tx_ring = vsi->tx_rings[skb->queue_mapping];

	/* hardware can't handle really short frames, hardware padding works
	 * beyond this point
	 */
	if (skb_put_padto(skb, I40E_MIN_TX_LEN))
		return NETDEV_TX_OK;

	return i40e_xmit_frame_ring(skb, tx_ring);
}

/**
 * i40e_xdp_xmit - Implements ndo_xdp_xmit
 * @dev: netdev
 * @n: number of frames
 * @frames: array of XDP buffer pointers
 * @flags: XDP extra info
 *
 * Returns number of frames successfully sent. Frames that fail are
 * free'ed via XDP return API.
 *
 * For error cases, a negative errno code is returned and no-frames
 * are transmitted (caller must handle freeing frames).
 **/
int i40e_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		  u32 flags)
{
	struct i40e_netdev_priv *np = netdev_priv(dev);
	unsigned int queue_index = smp_processor_id();
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	struct i40e_ring *xdp_ring;
	int drops = 0;
	int i;

	if (test_bit(__I40E_VSI_DOWN, vsi->state))
		return -ENETDOWN;

	if (!i40e_enabled_xdp_vsi(vsi) || queue_index >= vsi->num_queue_pairs ||
	    test_bit(__I40E_CONFIG_BUSY, pf->state))
		return -ENXIO;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	xdp_ring = vsi->xdp_rings[queue_index];

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];
		int err;

		err = i40e_xmit_xdp_ring(xdpf, xdp_ring);
		if (err != I40E_XDP_TX) {
			xdp_return_frame_rx_napi(xdpf);
			drops++;
		}
	}

	if (unlikely(flags & XDP_XMIT_FLUSH))
		i40e_xdp_ring_update_tail(xdp_ring);

	return n - drops;
}
