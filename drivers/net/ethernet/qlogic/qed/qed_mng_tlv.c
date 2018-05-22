// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include "qed.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"

#define TLV_TYPE(p)     (p[0])
#define TLV_LENGTH(p)   (p[1])
#define TLV_FLAGS(p)    (p[3])

#define QED_TLV_DATA_MAX (14)
struct qed_tlv_parsed_buf {
	/* To be filled with the address to set in Value field */
	void *p_val;

	/* To be used internally in case the value has to be modified */
	u8 data[QED_TLV_DATA_MAX];
};

static int qed_mfw_get_tlv_group(u8 tlv_type, u8 *tlv_group)
{
	switch (tlv_type) {
	case DRV_TLV_FEATURE_FLAGS:
	case DRV_TLV_LOCAL_ADMIN_ADDR:
	case DRV_TLV_ADDITIONAL_MAC_ADDR_1:
	case DRV_TLV_ADDITIONAL_MAC_ADDR_2:
	case DRV_TLV_OS_DRIVER_STATES:
	case DRV_TLV_PXE_BOOT_PROGRESS:
	case DRV_TLV_RX_FRAMES_RECEIVED:
	case DRV_TLV_RX_BYTES_RECEIVED:
	case DRV_TLV_TX_FRAMES_SENT:
	case DRV_TLV_TX_BYTES_SENT:
	case DRV_TLV_NPIV_ENABLED:
	case DRV_TLV_PCIE_BUS_RX_UTILIZATION:
	case DRV_TLV_PCIE_BUS_TX_UTILIZATION:
	case DRV_TLV_DEVICE_CPU_CORES_UTILIZATION:
	case DRV_TLV_LAST_VALID_DCC_TLV_RECEIVED:
	case DRV_TLV_NCSI_RX_BYTES_RECEIVED:
	case DRV_TLV_NCSI_TX_BYTES_SENT:
		*tlv_group |= QED_MFW_TLV_GENERIC;
		break;
	case DRV_TLV_LSO_MAX_OFFLOAD_SIZE:
	case DRV_TLV_LSO_MIN_SEGMENT_COUNT:
	case DRV_TLV_PROMISCUOUS_MODE:
	case DRV_TLV_TX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_RX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_NUM_OF_NET_QUEUE_VMQ_CFG:
	case DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV4:
	case DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV6:
	case DRV_TLV_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
	case DRV_TLV_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
	case DRV_TLV_IOV_OFFLOAD:
	case DRV_TLV_TX_QUEUES_EMPTY:
	case DRV_TLV_RX_QUEUES_EMPTY:
	case DRV_TLV_TX_QUEUES_FULL:
	case DRV_TLV_RX_QUEUES_FULL:
		*tlv_group |= QED_MFW_TLV_ETH;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Returns size of the data buffer or, -1 in case TLV data is not available. */
static int
qed_mfw_get_gen_tlv_value(struct qed_drv_tlv_hdr *p_tlv,
			  struct qed_mfw_tlv_generic *p_drv_buf,
			  struct qed_tlv_parsed_buf *p_buf)
{
	switch (p_tlv->tlv_type) {
	case DRV_TLV_FEATURE_FLAGS:
		if (p_drv_buf->flags.b_set) {
			memset(p_buf->data, 0, sizeof(u8) * QED_TLV_DATA_MAX);
			p_buf->data[0] = p_drv_buf->flags.ipv4_csum_offload ?
			    1 : 0;
			p_buf->data[0] |= (p_drv_buf->flags.lso_supported ?
					   1 : 0) << 1;
			p_buf->p_val = p_buf->data;
			return QED_MFW_TLV_FLAGS_SIZE;
		}
		break;

	case DRV_TLV_LOCAL_ADMIN_ADDR:
	case DRV_TLV_ADDITIONAL_MAC_ADDR_1:
	case DRV_TLV_ADDITIONAL_MAC_ADDR_2:
		{
			int idx = p_tlv->tlv_type - DRV_TLV_LOCAL_ADMIN_ADDR;

			if (p_drv_buf->mac_set[idx]) {
				p_buf->p_val = p_drv_buf->mac[idx];
				return ETH_ALEN;
			}
			break;
		}

	case DRV_TLV_RX_FRAMES_RECEIVED:
		if (p_drv_buf->rx_frames_set) {
			p_buf->p_val = &p_drv_buf->rx_frames;
			return sizeof(p_drv_buf->rx_frames);
		}
		break;
	case DRV_TLV_RX_BYTES_RECEIVED:
		if (p_drv_buf->rx_bytes_set) {
			p_buf->p_val = &p_drv_buf->rx_bytes;
			return sizeof(p_drv_buf->rx_bytes);
		}
		break;
	case DRV_TLV_TX_FRAMES_SENT:
		if (p_drv_buf->tx_frames_set) {
			p_buf->p_val = &p_drv_buf->tx_frames;
			return sizeof(p_drv_buf->tx_frames);
		}
		break;
	case DRV_TLV_TX_BYTES_SENT:
		if (p_drv_buf->tx_bytes_set) {
			p_buf->p_val = &p_drv_buf->tx_bytes;
			return sizeof(p_drv_buf->tx_bytes);
		}
		break;
	default:
		break;
	}

	return -1;
}

static int
qed_mfw_get_eth_tlv_value(struct qed_drv_tlv_hdr *p_tlv,
			  struct qed_mfw_tlv_eth *p_drv_buf,
			  struct qed_tlv_parsed_buf *p_buf)
{
	switch (p_tlv->tlv_type) {
	case DRV_TLV_LSO_MAX_OFFLOAD_SIZE:
		if (p_drv_buf->lso_maxoff_size_set) {
			p_buf->p_val = &p_drv_buf->lso_maxoff_size;
			return sizeof(p_drv_buf->lso_maxoff_size);
		}
		break;
	case DRV_TLV_LSO_MIN_SEGMENT_COUNT:
		if (p_drv_buf->lso_minseg_size_set) {
			p_buf->p_val = &p_drv_buf->lso_minseg_size;
			return sizeof(p_drv_buf->lso_minseg_size);
		}
		break;
	case DRV_TLV_PROMISCUOUS_MODE:
		if (p_drv_buf->prom_mode_set) {
			p_buf->p_val = &p_drv_buf->prom_mode;
			return sizeof(p_drv_buf->prom_mode);
		}
		break;
	case DRV_TLV_TX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->tx_descr_size_set) {
			p_buf->p_val = &p_drv_buf->tx_descr_size;
			return sizeof(p_drv_buf->tx_descr_size);
		}
		break;
	case DRV_TLV_RX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->rx_descr_size_set) {
			p_buf->p_val = &p_drv_buf->rx_descr_size;
			return sizeof(p_drv_buf->rx_descr_size);
		}
		break;
	case DRV_TLV_NUM_OF_NET_QUEUE_VMQ_CFG:
		if (p_drv_buf->netq_count_set) {
			p_buf->p_val = &p_drv_buf->netq_count;
			return sizeof(p_drv_buf->netq_count);
		}
		break;
	case DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV4:
		if (p_drv_buf->tcp4_offloads_set) {
			p_buf->p_val = &p_drv_buf->tcp4_offloads;
			return sizeof(p_drv_buf->tcp4_offloads);
		}
		break;
	case DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV6:
		if (p_drv_buf->tcp6_offloads_set) {
			p_buf->p_val = &p_drv_buf->tcp6_offloads;
			return sizeof(p_drv_buf->tcp6_offloads);
		}
		break;
	case DRV_TLV_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
		if (p_drv_buf->tx_descr_qdepth_set) {
			p_buf->p_val = &p_drv_buf->tx_descr_qdepth;
			return sizeof(p_drv_buf->tx_descr_qdepth);
		}
		break;
	case DRV_TLV_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
		if (p_drv_buf->rx_descr_qdepth_set) {
			p_buf->p_val = &p_drv_buf->rx_descr_qdepth;
			return sizeof(p_drv_buf->rx_descr_qdepth);
		}
		break;
	case DRV_TLV_IOV_OFFLOAD:
		if (p_drv_buf->iov_offload_set) {
			p_buf->p_val = &p_drv_buf->iov_offload;
			return sizeof(p_drv_buf->iov_offload);
		}
		break;
	case DRV_TLV_TX_QUEUES_EMPTY:
		if (p_drv_buf->txqs_empty_set) {
			p_buf->p_val = &p_drv_buf->txqs_empty;
			return sizeof(p_drv_buf->txqs_empty);
		}
		break;
	case DRV_TLV_RX_QUEUES_EMPTY:
		if (p_drv_buf->rxqs_empty_set) {
			p_buf->p_val = &p_drv_buf->rxqs_empty;
			return sizeof(p_drv_buf->rxqs_empty);
		}
		break;
	case DRV_TLV_TX_QUEUES_FULL:
		if (p_drv_buf->num_txqs_full_set) {
			p_buf->p_val = &p_drv_buf->num_txqs_full;
			return sizeof(p_drv_buf->num_txqs_full);
		}
		break;
	case DRV_TLV_RX_QUEUES_FULL:
		if (p_drv_buf->num_rxqs_full_set) {
			p_buf->p_val = &p_drv_buf->num_rxqs_full;
			return sizeof(p_drv_buf->num_rxqs_full);
		}
		break;
	default:
		break;
	}

	return -1;
}

static int qed_mfw_update_tlvs(struct qed_hwfn *p_hwfn,
			       u8 tlv_group, u8 *p_mfw_buf, u32 size)
{
	union qed_mfw_tlv_data *p_tlv_data;
	struct qed_tlv_parsed_buf buffer;
	struct qed_drv_tlv_hdr tlv;
	int len = 0;
	u32 offset;
	u8 *p_tlv;

	p_tlv_data = vzalloc(sizeof(*p_tlv_data));
	if (!p_tlv_data)
		return -ENOMEM;

	if (qed_mfw_fill_tlv_data(p_hwfn, tlv_group, p_tlv_data)) {
		vfree(p_tlv_data);
		return -EINVAL;
	}

	memset(&tlv, 0, sizeof(tlv));
	for (offset = 0; offset < size;
	     offset += sizeof(tlv) + sizeof(u32) * tlv.tlv_length) {
		p_tlv = &p_mfw_buf[offset];
		tlv.tlv_type = TLV_TYPE(p_tlv);
		tlv.tlv_length = TLV_LENGTH(p_tlv);
		tlv.tlv_flags = TLV_FLAGS(p_tlv);

		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Type %d length = %d flags = 0x%x\n", tlv.tlv_type,
			   tlv.tlv_length, tlv.tlv_flags);

		if (tlv_group == QED_MFW_TLV_GENERIC)
			len = qed_mfw_get_gen_tlv_value(&tlv,
							&p_tlv_data->generic,
							&buffer);
		else if (tlv_group == QED_MFW_TLV_ETH)
			len = qed_mfw_get_eth_tlv_value(&tlv,
							&p_tlv_data->eth,
							&buffer);

		if (len > 0) {
			WARN(len > 4 * tlv.tlv_length,
			     "Incorrect MFW TLV length %d, it shouldn't be greater than %d\n",
			     len, 4 * tlv.tlv_length);
			len = min_t(int, len, 4 * tlv.tlv_length);
			tlv.tlv_flags |= QED_DRV_TLV_FLAGS_CHANGED;
			TLV_FLAGS(p_tlv) = tlv.tlv_flags;
			memcpy(p_mfw_buf + offset + sizeof(tlv),
			       buffer.p_val, len);
		}
	}

	vfree(p_tlv_data);

	return 0;
}

int qed_mfw_process_tlv_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 addr, size, offset, resp, param, val, global_offsize, global_addr;
	u8 tlv_group = 0, id, *p_mfw_buf = NULL, *p_temp;
	struct qed_drv_tlv_hdr tlv;
	int rc;

	addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
				    PUBLIC_GLOBAL);
	global_offsize = qed_rd(p_hwfn, p_ptt, addr);
	global_addr = SECTION_ADDR(global_offsize, 0);
	addr = global_addr + offsetof(struct public_global, data_ptr);
	addr = qed_rd(p_hwfn, p_ptt, addr);
	size = qed_rd(p_hwfn, p_ptt, global_addr +
		      offsetof(struct public_global, data_size));

	if (!size) {
		DP_NOTICE(p_hwfn, "Invalid TLV req size = %d\n", size);
		goto drv_done;
	}

	p_mfw_buf = vzalloc(size);
	if (!p_mfw_buf) {
		DP_NOTICE(p_hwfn, "Failed allocate memory for p_mfw_buf\n");
		goto drv_done;
	}

	/* Read the TLV request to local buffer. MFW represents the TLV in
	 * little endian format and mcp returns it bigendian format. Hence
	 * driver need to convert data to little endian first and then do the
	 * memcpy (casting) to preserve the MFW TLV format in the driver buffer.
	 *
	 */
	for (offset = 0; offset < size; offset += sizeof(u32)) {
		val = qed_rd(p_hwfn, p_ptt, addr + offset);
		val = be32_to_cpu(val);
		memcpy(&p_mfw_buf[offset], &val, sizeof(u32));
	}

	/* Parse the headers to enumerate the requested TLV groups */
	for (offset = 0; offset < size;
	     offset += sizeof(tlv) + sizeof(u32) * tlv.tlv_length) {
		p_temp = &p_mfw_buf[offset];
		tlv.tlv_type = TLV_TYPE(p_temp);
		tlv.tlv_length = TLV_LENGTH(p_temp);
		if (qed_mfw_get_tlv_group(tlv.tlv_type, &tlv_group))
			DP_VERBOSE(p_hwfn, NETIF_MSG_DRV,
				   "Un recognized TLV %d\n", tlv.tlv_type);
	}

	/* Sanitize the TLV groups according to personality */
	if ((tlv_group & QED_MFW_TLV_ETH) && !QED_IS_L2_PERSONALITY(p_hwfn)) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Skipping L2 TLVs for non-L2 function\n");
		tlv_group &= ~QED_MFW_TLV_ETH;
	}

	/* Update the TLV values in the local buffer */
	for (id = QED_MFW_TLV_GENERIC; id < QED_MFW_TLV_MAX; id <<= 1) {
		if (tlv_group & id)
			if (qed_mfw_update_tlvs(p_hwfn, id, p_mfw_buf, size))
				goto drv_done;
	}

	/* Write the TLV data to shared memory. The stream of 4 bytes first need
	 * to be mem-copied to u32 element to make it as LSB format. And then
	 * converted to big endian as required by mcp-write.
	 */
	for (offset = 0; offset < size; offset += sizeof(u32)) {
		memcpy(&val, &p_mfw_buf[offset], sizeof(u32));
		val = cpu_to_be32(val);
		qed_wr(p_hwfn, p_ptt, addr + offset, val);
	}

drv_done:
	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_GET_TLV_DONE, 0, &resp,
			 &param);

	vfree(p_mfw_buf);

	return rc;
}
