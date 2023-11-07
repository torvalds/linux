// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* Copyright (c) 2019-2020 Marvell International Ltd. */

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
	case DRV_TLV_SCSI_TO:
	case DRV_TLV_R_T_TOV:
	case DRV_TLV_R_A_TOV:
	case DRV_TLV_E_D_TOV:
	case DRV_TLV_CR_TOV:
	case DRV_TLV_BOOT_TYPE:
	case DRV_TLV_NPIV_STATE:
	case DRV_TLV_NUM_OF_NPIV_IDS:
	case DRV_TLV_SWITCH_NAME:
	case DRV_TLV_SWITCH_PORT_NUM:
	case DRV_TLV_SWITCH_PORT_ID:
	case DRV_TLV_VENDOR_NAME:
	case DRV_TLV_SWITCH_MODEL:
	case DRV_TLV_SWITCH_FW_VER:
	case DRV_TLV_QOS_PRIORITY_PER_802_1P:
	case DRV_TLV_PORT_ALIAS:
	case DRV_TLV_PORT_STATE:
	case DRV_TLV_FIP_TX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_LINK_FAILURE_COUNT:
	case DRV_TLV_FCOE_BOOT_PROGRESS:
	case DRV_TLV_RX_BROADCAST_PACKETS:
	case DRV_TLV_TX_BROADCAST_PACKETS:
	case DRV_TLV_FCOE_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
	case DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
	case DRV_TLV_FCOE_RX_FRAMES_RECEIVED:
	case DRV_TLV_FCOE_RX_BYTES_RECEIVED:
	case DRV_TLV_FCOE_TX_FRAMES_SENT:
	case DRV_TLV_FCOE_TX_BYTES_SENT:
	case DRV_TLV_CRC_ERROR_COUNT:
	case DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_1_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_2_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_2_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_3_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_3_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_4_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_4_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_5_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_5_TIMESTAMP:
	case DRV_TLV_LOSS_OF_SYNC_ERROR_COUNT:
	case DRV_TLV_LOSS_OF_SIGNAL_ERRORS:
	case DRV_TLV_PRIMITIVE_SEQUENCE_PROTOCOL_ERROR_COUNT:
	case DRV_TLV_DISPARITY_ERROR_COUNT:
	case DRV_TLV_CODE_VIOLATION_ERROR_COUNT:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_2:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_3:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_4:
	case DRV_TLV_LAST_FLOGI_TIMESTAMP:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_2:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_3:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_4:
	case DRV_TLV_LAST_FLOGI_ACC_TIMESTAMP:
	case DRV_TLV_LAST_FLOGI_RJT:
	case DRV_TLV_LAST_FLOGI_RJT_TIMESTAMP:
	case DRV_TLV_FDISCS_SENT_COUNT:
	case DRV_TLV_FDISC_ACCS_RECEIVED:
	case DRV_TLV_FDISC_RJTS_RECEIVED:
	case DRV_TLV_PLOGI_SENT_COUNT:
	case DRV_TLV_PLOGI_ACCS_RECEIVED:
	case DRV_TLV_PLOGI_RJTS_RECEIVED:
	case DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_1_TIMESTAMP:
	case DRV_TLV_PLOGI_2_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_2_TIMESTAMP:
	case DRV_TLV_PLOGI_3_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_3_TIMESTAMP:
	case DRV_TLV_PLOGI_4_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_4_TIMESTAMP:
	case DRV_TLV_PLOGI_5_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_5_TIMESTAMP:
	case DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_1_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_2_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_2_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_3_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_3_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_4_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_4_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_5_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_5_ACC_TIMESTAMP:
	case DRV_TLV_LOGOS_ISSUED:
	case DRV_TLV_LOGO_ACCS_RECEIVED:
	case DRV_TLV_LOGO_RJTS_RECEIVED:
	case DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_1_TIMESTAMP:
	case DRV_TLV_LOGO_2_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_2_TIMESTAMP:
	case DRV_TLV_LOGO_3_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_3_TIMESTAMP:
	case DRV_TLV_LOGO_4_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_4_TIMESTAMP:
	case DRV_TLV_LOGO_5_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_5_TIMESTAMP:
	case DRV_TLV_LOGOS_RECEIVED:
	case DRV_TLV_ACCS_ISSUED:
	case DRV_TLV_PRLIS_ISSUED:
	case DRV_TLV_ACCS_RECEIVED:
	case DRV_TLV_ABTS_SENT_COUNT:
	case DRV_TLV_ABTS_ACCS_RECEIVED:
	case DRV_TLV_ABTS_RJTS_RECEIVED:
	case DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_1_TIMESTAMP:
	case DRV_TLV_ABTS_2_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_2_TIMESTAMP:
	case DRV_TLV_ABTS_3_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_3_TIMESTAMP:
	case DRV_TLV_ABTS_4_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_4_TIMESTAMP:
	case DRV_TLV_ABTS_5_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_5_TIMESTAMP:
	case DRV_TLV_RSCNS_RECEIVED:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_2:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_3:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_4:
	case DRV_TLV_LUN_RESETS_ISSUED:
	case DRV_TLV_ABORT_TASK_SETS_ISSUED:
	case DRV_TLV_TPRLOS_SENT:
	case DRV_TLV_NOS_SENT_COUNT:
	case DRV_TLV_NOS_RECEIVED_COUNT:
	case DRV_TLV_OLS_COUNT:
	case DRV_TLV_LR_COUNT:
	case DRV_TLV_LRR_COUNT:
	case DRV_TLV_LIP_SENT_COUNT:
	case DRV_TLV_LIP_RECEIVED_COUNT:
	case DRV_TLV_EOFA_COUNT:
	case DRV_TLV_EOFNI_COUNT:
	case DRV_TLV_SCSI_STATUS_CHECK_CONDITION_COUNT:
	case DRV_TLV_SCSI_STATUS_CONDITION_MET_COUNT:
	case DRV_TLV_SCSI_STATUS_BUSY_COUNT:
	case DRV_TLV_SCSI_STATUS_INTERMEDIATE_COUNT:
	case DRV_TLV_SCSI_STATUS_INTERMEDIATE_CONDITION_MET_COUNT:
	case DRV_TLV_SCSI_STATUS_RESERVATION_CONFLICT_COUNT:
	case DRV_TLV_SCSI_STATUS_TASK_SET_FULL_COUNT:
	case DRV_TLV_SCSI_STATUS_ACA_ACTIVE_COUNT:
	case DRV_TLV_SCSI_STATUS_TASK_ABORTED_COUNT:
	case DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_1_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_CONDITION_2_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_2_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_CONDITION_3_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_3_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_CONDITION_4_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_4_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_CONDITION_5_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_5_TIMESTAMP:
		*tlv_group = QED_MFW_TLV_FCOE;
		break;
	case DRV_TLV_TARGET_LLMNR_ENABLED:
	case DRV_TLV_HEADER_DIGEST_FLAG_ENABLED:
	case DRV_TLV_DATA_DIGEST_FLAG_ENABLED:
	case DRV_TLV_AUTHENTICATION_METHOD:
	case DRV_TLV_ISCSI_BOOT_TARGET_PORTAL:
	case DRV_TLV_MAX_FRAME_SIZE:
	case DRV_TLV_PDU_TX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_SIZE:
	case DRV_TLV_ISCSI_BOOT_PROGRESS:
	case DRV_TLV_PDU_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
	case DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
	case DRV_TLV_ISCSI_PDU_RX_FRAMES_RECEIVED:
	case DRV_TLV_ISCSI_PDU_RX_BYTES_RECEIVED:
	case DRV_TLV_ISCSI_PDU_TX_FRAMES_SENT:
	case DRV_TLV_ISCSI_PDU_TX_BYTES_SENT:
		*tlv_group |= QED_MFW_TLV_ISCSI;
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

static int
qed_mfw_get_tlv_time_value(struct qed_mfw_tlv_time *p_time,
			   struct qed_tlv_parsed_buf *p_buf)
{
	if (!p_time->b_set)
		return -1;

	/* Validate numbers */
	if (p_time->month > 12)
		p_time->month = 0;
	if (p_time->day > 31)
		p_time->day = 0;
	if (p_time->hour > 23)
		p_time->hour = 0;
	if (p_time->min > 59)
		p_time->min = 0;
	if (p_time->msec > 999)
		p_time->msec = 0;
	if (p_time->usec > 999)
		p_time->usec = 0;

	memset(p_buf->data, 0, sizeof(u8) * QED_TLV_DATA_MAX);
	snprintf(p_buf->data, 14, "%d%d%d%d%d%d",
		 p_time->month, p_time->day,
		 p_time->hour, p_time->min, p_time->msec, p_time->usec);

	p_buf->p_val = p_buf->data;

	return QED_MFW_TLV_TIME_SIZE;
}

static int
qed_mfw_get_fcoe_tlv_value(struct qed_drv_tlv_hdr *p_tlv,
			   struct qed_mfw_tlv_fcoe *p_drv_buf,
			   struct qed_tlv_parsed_buf *p_buf)
{
	struct qed_mfw_tlv_time *p_time;
	u8 idx;

	switch (p_tlv->tlv_type) {
	case DRV_TLV_SCSI_TO:
		if (p_drv_buf->scsi_timeout_set) {
			p_buf->p_val = &p_drv_buf->scsi_timeout;
			return sizeof(p_drv_buf->scsi_timeout);
		}
		break;
	case DRV_TLV_R_T_TOV:
		if (p_drv_buf->rt_tov_set) {
			p_buf->p_val = &p_drv_buf->rt_tov;
			return sizeof(p_drv_buf->rt_tov);
		}
		break;
	case DRV_TLV_R_A_TOV:
		if (p_drv_buf->ra_tov_set) {
			p_buf->p_val = &p_drv_buf->ra_tov;
			return sizeof(p_drv_buf->ra_tov);
		}
		break;
	case DRV_TLV_E_D_TOV:
		if (p_drv_buf->ed_tov_set) {
			p_buf->p_val = &p_drv_buf->ed_tov;
			return sizeof(p_drv_buf->ed_tov);
		}
		break;
	case DRV_TLV_CR_TOV:
		if (p_drv_buf->cr_tov_set) {
			p_buf->p_val = &p_drv_buf->cr_tov;
			return sizeof(p_drv_buf->cr_tov);
		}
		break;
	case DRV_TLV_BOOT_TYPE:
		if (p_drv_buf->boot_type_set) {
			p_buf->p_val = &p_drv_buf->boot_type;
			return sizeof(p_drv_buf->boot_type);
		}
		break;
	case DRV_TLV_NPIV_STATE:
		if (p_drv_buf->npiv_state_set) {
			p_buf->p_val = &p_drv_buf->npiv_state;
			return sizeof(p_drv_buf->npiv_state);
		}
		break;
	case DRV_TLV_NUM_OF_NPIV_IDS:
		if (p_drv_buf->num_npiv_ids_set) {
			p_buf->p_val = &p_drv_buf->num_npiv_ids;
			return sizeof(p_drv_buf->num_npiv_ids);
		}
		break;
	case DRV_TLV_SWITCH_NAME:
		if (p_drv_buf->switch_name_set) {
			p_buf->p_val = &p_drv_buf->switch_name;
			return sizeof(p_drv_buf->switch_name);
		}
		break;
	case DRV_TLV_SWITCH_PORT_NUM:
		if (p_drv_buf->switch_portnum_set) {
			p_buf->p_val = &p_drv_buf->switch_portnum;
			return sizeof(p_drv_buf->switch_portnum);
		}
		break;
	case DRV_TLV_SWITCH_PORT_ID:
		if (p_drv_buf->switch_portid_set) {
			p_buf->p_val = &p_drv_buf->switch_portid;
			return sizeof(p_drv_buf->switch_portid);
		}
		break;
	case DRV_TLV_VENDOR_NAME:
		if (p_drv_buf->vendor_name_set) {
			p_buf->p_val = &p_drv_buf->vendor_name;
			return sizeof(p_drv_buf->vendor_name);
		}
		break;
	case DRV_TLV_SWITCH_MODEL:
		if (p_drv_buf->switch_model_set) {
			p_buf->p_val = &p_drv_buf->switch_model;
			return sizeof(p_drv_buf->switch_model);
		}
		break;
	case DRV_TLV_SWITCH_FW_VER:
		if (p_drv_buf->switch_fw_version_set) {
			p_buf->p_val = &p_drv_buf->switch_fw_version;
			return sizeof(p_drv_buf->switch_fw_version);
		}
		break;
	case DRV_TLV_QOS_PRIORITY_PER_802_1P:
		if (p_drv_buf->qos_pri_set) {
			p_buf->p_val = &p_drv_buf->qos_pri;
			return sizeof(p_drv_buf->qos_pri);
		}
		break;
	case DRV_TLV_PORT_ALIAS:
		if (p_drv_buf->port_alias_set) {
			p_buf->p_val = &p_drv_buf->port_alias;
			return sizeof(p_drv_buf->port_alias);
		}
		break;
	case DRV_TLV_PORT_STATE:
		if (p_drv_buf->port_state_set) {
			p_buf->p_val = &p_drv_buf->port_state;
			return sizeof(p_drv_buf->port_state);
		}
		break;
	case DRV_TLV_FIP_TX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->fip_tx_descr_size_set) {
			p_buf->p_val = &p_drv_buf->fip_tx_descr_size;
			return sizeof(p_drv_buf->fip_tx_descr_size);
		}
		break;
	case DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->fip_rx_descr_size_set) {
			p_buf->p_val = &p_drv_buf->fip_rx_descr_size;
			return sizeof(p_drv_buf->fip_rx_descr_size);
		}
		break;
	case DRV_TLV_LINK_FAILURE_COUNT:
		if (p_drv_buf->link_failures_set) {
			p_buf->p_val = &p_drv_buf->link_failures;
			return sizeof(p_drv_buf->link_failures);
		}
		break;
	case DRV_TLV_FCOE_BOOT_PROGRESS:
		if (p_drv_buf->fcoe_boot_progress_set) {
			p_buf->p_val = &p_drv_buf->fcoe_boot_progress;
			return sizeof(p_drv_buf->fcoe_boot_progress);
		}
		break;
	case DRV_TLV_RX_BROADCAST_PACKETS:
		if (p_drv_buf->rx_bcast_set) {
			p_buf->p_val = &p_drv_buf->rx_bcast;
			return sizeof(p_drv_buf->rx_bcast);
		}
		break;
	case DRV_TLV_TX_BROADCAST_PACKETS:
		if (p_drv_buf->tx_bcast_set) {
			p_buf->p_val = &p_drv_buf->tx_bcast;
			return sizeof(p_drv_buf->tx_bcast);
		}
		break;
	case DRV_TLV_FCOE_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
		if (p_drv_buf->fcoe_txq_depth_set) {
			p_buf->p_val = &p_drv_buf->fcoe_txq_depth;
			return sizeof(p_drv_buf->fcoe_txq_depth);
		}
		break;
	case DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
		if (p_drv_buf->fcoe_rxq_depth_set) {
			p_buf->p_val = &p_drv_buf->fcoe_rxq_depth;
			return sizeof(p_drv_buf->fcoe_rxq_depth);
		}
		break;
	case DRV_TLV_FCOE_RX_FRAMES_RECEIVED:
		if (p_drv_buf->fcoe_rx_frames_set) {
			p_buf->p_val = &p_drv_buf->fcoe_rx_frames;
			return sizeof(p_drv_buf->fcoe_rx_frames);
		}
		break;
	case DRV_TLV_FCOE_RX_BYTES_RECEIVED:
		if (p_drv_buf->fcoe_rx_bytes_set) {
			p_buf->p_val = &p_drv_buf->fcoe_rx_bytes;
			return sizeof(p_drv_buf->fcoe_rx_bytes);
		}
		break;
	case DRV_TLV_FCOE_TX_FRAMES_SENT:
		if (p_drv_buf->fcoe_tx_frames_set) {
			p_buf->p_val = &p_drv_buf->fcoe_tx_frames;
			return sizeof(p_drv_buf->fcoe_tx_frames);
		}
		break;
	case DRV_TLV_FCOE_TX_BYTES_SENT:
		if (p_drv_buf->fcoe_tx_bytes_set) {
			p_buf->p_val = &p_drv_buf->fcoe_tx_bytes;
			return sizeof(p_drv_buf->fcoe_tx_bytes);
		}
		break;
	case DRV_TLV_CRC_ERROR_COUNT:
		if (p_drv_buf->crc_count_set) {
			p_buf->p_val = &p_drv_buf->crc_count;
			return sizeof(p_drv_buf->crc_count);
		}
		break;
	case DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_2_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_3_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_4_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_CRC_ERROR_5_RECEIVED_SOURCE_FC_ID:
		idx = (p_tlv->tlv_type -
		       DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID) / 2;

		if (p_drv_buf->crc_err_src_fcid_set[idx]) {
			p_buf->p_val = &p_drv_buf->crc_err_src_fcid[idx];
			return sizeof(p_drv_buf->crc_err_src_fcid[idx]);
		}
		break;
	case DRV_TLV_CRC_ERROR_1_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_2_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_3_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_4_TIMESTAMP:
	case DRV_TLV_CRC_ERROR_5_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_CRC_ERROR_1_TIMESTAMP) / 2;

		return qed_mfw_get_tlv_time_value(&p_drv_buf->crc_err[idx],
						  p_buf);
	case DRV_TLV_LOSS_OF_SYNC_ERROR_COUNT:
		if (p_drv_buf->losync_err_set) {
			p_buf->p_val = &p_drv_buf->losync_err;
			return sizeof(p_drv_buf->losync_err);
		}
		break;
	case DRV_TLV_LOSS_OF_SIGNAL_ERRORS:
		if (p_drv_buf->losig_err_set) {
			p_buf->p_val = &p_drv_buf->losig_err;
			return sizeof(p_drv_buf->losig_err);
		}
		break;
	case DRV_TLV_PRIMITIVE_SEQUENCE_PROTOCOL_ERROR_COUNT:
		if (p_drv_buf->primtive_err_set) {
			p_buf->p_val = &p_drv_buf->primtive_err;
			return sizeof(p_drv_buf->primtive_err);
		}
		break;
	case DRV_TLV_DISPARITY_ERROR_COUNT:
		if (p_drv_buf->disparity_err_set) {
			p_buf->p_val = &p_drv_buf->disparity_err;
			return sizeof(p_drv_buf->disparity_err);
		}
		break;
	case DRV_TLV_CODE_VIOLATION_ERROR_COUNT:
		if (p_drv_buf->code_violation_err_set) {
			p_buf->p_val = &p_drv_buf->code_violation_err;
			return sizeof(p_drv_buf->code_violation_err);
		}
		break;
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_2:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_3:
	case DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_4:
		idx = p_tlv->tlv_type -
			DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1;
		if (p_drv_buf->flogi_param_set[idx]) {
			p_buf->p_val = &p_drv_buf->flogi_param[idx];
			return sizeof(p_drv_buf->flogi_param[idx]);
		}
		break;
	case DRV_TLV_LAST_FLOGI_TIMESTAMP:
		return qed_mfw_get_tlv_time_value(&p_drv_buf->flogi_tstamp,
						  p_buf);
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_2:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_3:
	case DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_4:
		idx = p_tlv->tlv_type -
			DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1;

		if (p_drv_buf->flogi_acc_param_set[idx]) {
			p_buf->p_val = &p_drv_buf->flogi_acc_param[idx];
			return sizeof(p_drv_buf->flogi_acc_param[idx]);
		}
		break;
	case DRV_TLV_LAST_FLOGI_ACC_TIMESTAMP:
		return qed_mfw_get_tlv_time_value(&p_drv_buf->flogi_acc_tstamp,
						  p_buf);
	case DRV_TLV_LAST_FLOGI_RJT:
		if (p_drv_buf->flogi_rjt_set) {
			p_buf->p_val = &p_drv_buf->flogi_rjt;
			return sizeof(p_drv_buf->flogi_rjt);
		}
		break;
	case DRV_TLV_LAST_FLOGI_RJT_TIMESTAMP:
		return qed_mfw_get_tlv_time_value(&p_drv_buf->flogi_rjt_tstamp,
						  p_buf);
	case DRV_TLV_FDISCS_SENT_COUNT:
		if (p_drv_buf->fdiscs_set) {
			p_buf->p_val = &p_drv_buf->fdiscs;
			return sizeof(p_drv_buf->fdiscs);
		}
		break;
	case DRV_TLV_FDISC_ACCS_RECEIVED:
		if (p_drv_buf->fdisc_acc_set) {
			p_buf->p_val = &p_drv_buf->fdisc_acc;
			return sizeof(p_drv_buf->fdisc_acc);
		}
		break;
	case DRV_TLV_FDISC_RJTS_RECEIVED:
		if (p_drv_buf->fdisc_rjt_set) {
			p_buf->p_val = &p_drv_buf->fdisc_rjt;
			return sizeof(p_drv_buf->fdisc_rjt);
		}
		break;
	case DRV_TLV_PLOGI_SENT_COUNT:
		if (p_drv_buf->plogi_set) {
			p_buf->p_val = &p_drv_buf->plogi;
			return sizeof(p_drv_buf->plogi);
		}
		break;
	case DRV_TLV_PLOGI_ACCS_RECEIVED:
		if (p_drv_buf->plogi_acc_set) {
			p_buf->p_val = &p_drv_buf->plogi_acc;
			return sizeof(p_drv_buf->plogi_acc);
		}
		break;
	case DRV_TLV_PLOGI_RJTS_RECEIVED:
		if (p_drv_buf->plogi_rjt_set) {
			p_buf->p_val = &p_drv_buf->plogi_rjt;
			return sizeof(p_drv_buf->plogi_rjt);
		}
		break;
	case DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_2_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_3_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_4_SENT_DESTINATION_FC_ID:
	case DRV_TLV_PLOGI_5_SENT_DESTINATION_FC_ID:
		idx = (p_tlv->tlv_type -
		       DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID) / 2;

		if (p_drv_buf->plogi_dst_fcid_set[idx]) {
			p_buf->p_val = &p_drv_buf->plogi_dst_fcid[idx];
			return sizeof(p_drv_buf->plogi_dst_fcid[idx]);
		}
		break;
	case DRV_TLV_PLOGI_1_TIMESTAMP:
	case DRV_TLV_PLOGI_2_TIMESTAMP:
	case DRV_TLV_PLOGI_3_TIMESTAMP:
	case DRV_TLV_PLOGI_4_TIMESTAMP:
	case DRV_TLV_PLOGI_5_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_PLOGI_1_TIMESTAMP) / 2;

		return qed_mfw_get_tlv_time_value(&p_drv_buf->plogi_tstamp[idx],
						  p_buf);
	case DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_2_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_3_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_4_ACC_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_PLOGI_5_ACC_RECEIVED_SOURCE_FC_ID:
		idx = (p_tlv->tlv_type -
		       DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID) / 2;

		if (p_drv_buf->plogi_acc_src_fcid_set[idx]) {
			p_buf->p_val = &p_drv_buf->plogi_acc_src_fcid[idx];
			return sizeof(p_drv_buf->plogi_acc_src_fcid[idx]);
		}
		break;
	case DRV_TLV_PLOGI_1_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_2_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_3_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_4_ACC_TIMESTAMP:
	case DRV_TLV_PLOGI_5_ACC_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_PLOGI_1_ACC_TIMESTAMP) / 2;
		p_time = &p_drv_buf->plogi_acc_tstamp[idx];

		return qed_mfw_get_tlv_time_value(p_time, p_buf);
	case DRV_TLV_LOGOS_ISSUED:
		if (p_drv_buf->tx_plogos_set) {
			p_buf->p_val = &p_drv_buf->tx_plogos;
			return sizeof(p_drv_buf->tx_plogos);
		}
		break;
	case DRV_TLV_LOGO_ACCS_RECEIVED:
		if (p_drv_buf->plogo_acc_set) {
			p_buf->p_val = &p_drv_buf->plogo_acc;
			return sizeof(p_drv_buf->plogo_acc);
		}
		break;
	case DRV_TLV_LOGO_RJTS_RECEIVED:
		if (p_drv_buf->plogo_rjt_set) {
			p_buf->p_val = &p_drv_buf->plogo_rjt;
			return sizeof(p_drv_buf->plogo_rjt);
		}
		break;
	case DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_2_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_3_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_4_RECEIVED_SOURCE_FC_ID:
	case DRV_TLV_LOGO_5_RECEIVED_SOURCE_FC_ID:
		idx = (p_tlv->tlv_type - DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID) /
			2;

		if (p_drv_buf->plogo_src_fcid_set[idx]) {
			p_buf->p_val = &p_drv_buf->plogo_src_fcid[idx];
			return sizeof(p_drv_buf->plogo_src_fcid[idx]);
		}
		break;
	case DRV_TLV_LOGO_1_TIMESTAMP:
	case DRV_TLV_LOGO_2_TIMESTAMP:
	case DRV_TLV_LOGO_3_TIMESTAMP:
	case DRV_TLV_LOGO_4_TIMESTAMP:
	case DRV_TLV_LOGO_5_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_LOGO_1_TIMESTAMP) / 2;

		return qed_mfw_get_tlv_time_value(&p_drv_buf->plogo_tstamp[idx],
						  p_buf);
	case DRV_TLV_LOGOS_RECEIVED:
		if (p_drv_buf->rx_logos_set) {
			p_buf->p_val = &p_drv_buf->rx_logos;
			return sizeof(p_drv_buf->rx_logos);
		}
		break;
	case DRV_TLV_ACCS_ISSUED:
		if (p_drv_buf->tx_accs_set) {
			p_buf->p_val = &p_drv_buf->tx_accs;
			return sizeof(p_drv_buf->tx_accs);
		}
		break;
	case DRV_TLV_PRLIS_ISSUED:
		if (p_drv_buf->tx_prlis_set) {
			p_buf->p_val = &p_drv_buf->tx_prlis;
			return sizeof(p_drv_buf->tx_prlis);
		}
		break;
	case DRV_TLV_ACCS_RECEIVED:
		if (p_drv_buf->rx_accs_set) {
			p_buf->p_val = &p_drv_buf->rx_accs;
			return sizeof(p_drv_buf->rx_accs);
		}
		break;
	case DRV_TLV_ABTS_SENT_COUNT:
		if (p_drv_buf->tx_abts_set) {
			p_buf->p_val = &p_drv_buf->tx_abts;
			return sizeof(p_drv_buf->tx_abts);
		}
		break;
	case DRV_TLV_ABTS_ACCS_RECEIVED:
		if (p_drv_buf->rx_abts_acc_set) {
			p_buf->p_val = &p_drv_buf->rx_abts_acc;
			return sizeof(p_drv_buf->rx_abts_acc);
		}
		break;
	case DRV_TLV_ABTS_RJTS_RECEIVED:
		if (p_drv_buf->rx_abts_rjt_set) {
			p_buf->p_val = &p_drv_buf->rx_abts_rjt;
			return sizeof(p_drv_buf->rx_abts_rjt);
		}
		break;
	case DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_2_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_3_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_4_SENT_DESTINATION_FC_ID:
	case DRV_TLV_ABTS_5_SENT_DESTINATION_FC_ID:
		idx = (p_tlv->tlv_type -
		       DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID) / 2;

		if (p_drv_buf->abts_dst_fcid_set[idx]) {
			p_buf->p_val = &p_drv_buf->abts_dst_fcid[idx];
			return sizeof(p_drv_buf->abts_dst_fcid[idx]);
		}
		break;
	case DRV_TLV_ABTS_1_TIMESTAMP:
	case DRV_TLV_ABTS_2_TIMESTAMP:
	case DRV_TLV_ABTS_3_TIMESTAMP:
	case DRV_TLV_ABTS_4_TIMESTAMP:
	case DRV_TLV_ABTS_5_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_ABTS_1_TIMESTAMP) / 2;

		return qed_mfw_get_tlv_time_value(&p_drv_buf->abts_tstamp[idx],
						  p_buf);
	case DRV_TLV_RSCNS_RECEIVED:
		if (p_drv_buf->rx_rscn_set) {
			p_buf->p_val = &p_drv_buf->rx_rscn;
			return sizeof(p_drv_buf->rx_rscn);
		}
		break;
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_2:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_3:
	case DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_4:
		idx = p_tlv->tlv_type - DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1;

		if (p_drv_buf->rx_rscn_nport_set[idx]) {
			p_buf->p_val = &p_drv_buf->rx_rscn_nport[idx];
			return sizeof(p_drv_buf->rx_rscn_nport[idx]);
		}
		break;
	case DRV_TLV_LUN_RESETS_ISSUED:
		if (p_drv_buf->tx_lun_rst_set) {
			p_buf->p_val = &p_drv_buf->tx_lun_rst;
			return sizeof(p_drv_buf->tx_lun_rst);
		}
		break;
	case DRV_TLV_ABORT_TASK_SETS_ISSUED:
		if (p_drv_buf->abort_task_sets_set) {
			p_buf->p_val = &p_drv_buf->abort_task_sets;
			return sizeof(p_drv_buf->abort_task_sets);
		}
		break;
	case DRV_TLV_TPRLOS_SENT:
		if (p_drv_buf->tx_tprlos_set) {
			p_buf->p_val = &p_drv_buf->tx_tprlos;
			return sizeof(p_drv_buf->tx_tprlos);
		}
		break;
	case DRV_TLV_NOS_SENT_COUNT:
		if (p_drv_buf->tx_nos_set) {
			p_buf->p_val = &p_drv_buf->tx_nos;
			return sizeof(p_drv_buf->tx_nos);
		}
		break;
	case DRV_TLV_NOS_RECEIVED_COUNT:
		if (p_drv_buf->rx_nos_set) {
			p_buf->p_val = &p_drv_buf->rx_nos;
			return sizeof(p_drv_buf->rx_nos);
		}
		break;
	case DRV_TLV_OLS_COUNT:
		if (p_drv_buf->ols_set) {
			p_buf->p_val = &p_drv_buf->ols;
			return sizeof(p_drv_buf->ols);
		}
		break;
	case DRV_TLV_LR_COUNT:
		if (p_drv_buf->lr_set) {
			p_buf->p_val = &p_drv_buf->lr;
			return sizeof(p_drv_buf->lr);
		}
		break;
	case DRV_TLV_LRR_COUNT:
		if (p_drv_buf->lrr_set) {
			p_buf->p_val = &p_drv_buf->lrr;
			return sizeof(p_drv_buf->lrr);
		}
		break;
	case DRV_TLV_LIP_SENT_COUNT:
		if (p_drv_buf->tx_lip_set) {
			p_buf->p_val = &p_drv_buf->tx_lip;
			return sizeof(p_drv_buf->tx_lip);
		}
		break;
	case DRV_TLV_LIP_RECEIVED_COUNT:
		if (p_drv_buf->rx_lip_set) {
			p_buf->p_val = &p_drv_buf->rx_lip;
			return sizeof(p_drv_buf->rx_lip);
		}
		break;
	case DRV_TLV_EOFA_COUNT:
		if (p_drv_buf->eofa_set) {
			p_buf->p_val = &p_drv_buf->eofa;
			return sizeof(p_drv_buf->eofa);
		}
		break;
	case DRV_TLV_EOFNI_COUNT:
		if (p_drv_buf->eofni_set) {
			p_buf->p_val = &p_drv_buf->eofni;
			return sizeof(p_drv_buf->eofni);
		}
		break;
	case DRV_TLV_SCSI_STATUS_CHECK_CONDITION_COUNT:
		if (p_drv_buf->scsi_chks_set) {
			p_buf->p_val = &p_drv_buf->scsi_chks;
			return sizeof(p_drv_buf->scsi_chks);
		}
		break;
	case DRV_TLV_SCSI_STATUS_CONDITION_MET_COUNT:
		if (p_drv_buf->scsi_cond_met_set) {
			p_buf->p_val = &p_drv_buf->scsi_cond_met;
			return sizeof(p_drv_buf->scsi_cond_met);
		}
		break;
	case DRV_TLV_SCSI_STATUS_BUSY_COUNT:
		if (p_drv_buf->scsi_busy_set) {
			p_buf->p_val = &p_drv_buf->scsi_busy;
			return sizeof(p_drv_buf->scsi_busy);
		}
		break;
	case DRV_TLV_SCSI_STATUS_INTERMEDIATE_COUNT:
		if (p_drv_buf->scsi_inter_set) {
			p_buf->p_val = &p_drv_buf->scsi_inter;
			return sizeof(p_drv_buf->scsi_inter);
		}
		break;
	case DRV_TLV_SCSI_STATUS_INTERMEDIATE_CONDITION_MET_COUNT:
		if (p_drv_buf->scsi_inter_cond_met_set) {
			p_buf->p_val = &p_drv_buf->scsi_inter_cond_met;
			return sizeof(p_drv_buf->scsi_inter_cond_met);
		}
		break;
	case DRV_TLV_SCSI_STATUS_RESERVATION_CONFLICT_COUNT:
		if (p_drv_buf->scsi_rsv_conflicts_set) {
			p_buf->p_val = &p_drv_buf->scsi_rsv_conflicts;
			return sizeof(p_drv_buf->scsi_rsv_conflicts);
		}
		break;
	case DRV_TLV_SCSI_STATUS_TASK_SET_FULL_COUNT:
		if (p_drv_buf->scsi_tsk_full_set) {
			p_buf->p_val = &p_drv_buf->scsi_tsk_full;
			return sizeof(p_drv_buf->scsi_tsk_full);
		}
		break;
	case DRV_TLV_SCSI_STATUS_ACA_ACTIVE_COUNT:
		if (p_drv_buf->scsi_aca_active_set) {
			p_buf->p_val = &p_drv_buf->scsi_aca_active;
			return sizeof(p_drv_buf->scsi_aca_active);
		}
		break;
	case DRV_TLV_SCSI_STATUS_TASK_ABORTED_COUNT:
		if (p_drv_buf->scsi_tsk_abort_set) {
			p_buf->p_val = &p_drv_buf->scsi_tsk_abort;
			return sizeof(p_drv_buf->scsi_tsk_abort);
		}
		break;
	case DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_CONDITION_2_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_CONDITION_3_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_CONDITION_4_RECEIVED_SK_ASC_ASCQ:
	case DRV_TLV_SCSI_CHECK_CONDITION_5_RECEIVED_SK_ASC_ASCQ:
		idx = (p_tlv->tlv_type -
		       DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ) / 2;

		if (p_drv_buf->scsi_rx_chk_set[idx]) {
			p_buf->p_val = &p_drv_buf->scsi_rx_chk[idx];
			return sizeof(p_drv_buf->scsi_rx_chk[idx]);
		}
		break;
	case DRV_TLV_SCSI_CHECK_1_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_2_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_3_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_4_TIMESTAMP:
	case DRV_TLV_SCSI_CHECK_5_TIMESTAMP:
		idx = (p_tlv->tlv_type - DRV_TLV_SCSI_CHECK_1_TIMESTAMP) / 2;
		p_time = &p_drv_buf->scsi_chk_tstamp[idx];

		return qed_mfw_get_tlv_time_value(p_time, p_buf);
	default:
		break;
	}

	return -1;
}

static int
qed_mfw_get_iscsi_tlv_value(struct qed_drv_tlv_hdr *p_tlv,
			    struct qed_mfw_tlv_iscsi *p_drv_buf,
			    struct qed_tlv_parsed_buf *p_buf)
{
	switch (p_tlv->tlv_type) {
	case DRV_TLV_TARGET_LLMNR_ENABLED:
		if (p_drv_buf->target_llmnr_set) {
			p_buf->p_val = &p_drv_buf->target_llmnr;
			return sizeof(p_drv_buf->target_llmnr);
		}
		break;
	case DRV_TLV_HEADER_DIGEST_FLAG_ENABLED:
		if (p_drv_buf->header_digest_set) {
			p_buf->p_val = &p_drv_buf->header_digest;
			return sizeof(p_drv_buf->header_digest);
		}
		break;
	case DRV_TLV_DATA_DIGEST_FLAG_ENABLED:
		if (p_drv_buf->data_digest_set) {
			p_buf->p_val = &p_drv_buf->data_digest;
			return sizeof(p_drv_buf->data_digest);
		}
		break;
	case DRV_TLV_AUTHENTICATION_METHOD:
		if (p_drv_buf->auth_method_set) {
			p_buf->p_val = &p_drv_buf->auth_method;
			return sizeof(p_drv_buf->auth_method);
		}
		break;
	case DRV_TLV_ISCSI_BOOT_TARGET_PORTAL:
		if (p_drv_buf->boot_taget_portal_set) {
			p_buf->p_val = &p_drv_buf->boot_taget_portal;
			return sizeof(p_drv_buf->boot_taget_portal);
		}
		break;
	case DRV_TLV_MAX_FRAME_SIZE:
		if (p_drv_buf->frame_size_set) {
			p_buf->p_val = &p_drv_buf->frame_size;
			return sizeof(p_drv_buf->frame_size);
		}
		break;
	case DRV_TLV_PDU_TX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->tx_desc_size_set) {
			p_buf->p_val = &p_drv_buf->tx_desc_size;
			return sizeof(p_drv_buf->tx_desc_size);
		}
		break;
	case DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_SIZE:
		if (p_drv_buf->rx_desc_size_set) {
			p_buf->p_val = &p_drv_buf->rx_desc_size;
			return sizeof(p_drv_buf->rx_desc_size);
		}
		break;
	case DRV_TLV_ISCSI_BOOT_PROGRESS:
		if (p_drv_buf->boot_progress_set) {
			p_buf->p_val = &p_drv_buf->boot_progress;
			return sizeof(p_drv_buf->boot_progress);
		}
		break;
	case DRV_TLV_PDU_TX_DESCRIPTOR_QUEUE_AVG_DEPTH:
		if (p_drv_buf->tx_desc_qdepth_set) {
			p_buf->p_val = &p_drv_buf->tx_desc_qdepth;
			return sizeof(p_drv_buf->tx_desc_qdepth);
		}
		break;
	case DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_AVG_DEPTH:
		if (p_drv_buf->rx_desc_qdepth_set) {
			p_buf->p_val = &p_drv_buf->rx_desc_qdepth;
			return sizeof(p_drv_buf->rx_desc_qdepth);
		}
		break;
	case DRV_TLV_ISCSI_PDU_RX_FRAMES_RECEIVED:
		if (p_drv_buf->rx_frames_set) {
			p_buf->p_val = &p_drv_buf->rx_frames;
			return sizeof(p_drv_buf->rx_frames);
		}
		break;
	case DRV_TLV_ISCSI_PDU_RX_BYTES_RECEIVED:
		if (p_drv_buf->rx_bytes_set) {
			p_buf->p_val = &p_drv_buf->rx_bytes;
			return sizeof(p_drv_buf->rx_bytes);
		}
		break;
	case DRV_TLV_ISCSI_PDU_TX_FRAMES_SENT:
		if (p_drv_buf->tx_frames_set) {
			p_buf->p_val = &p_drv_buf->tx_frames;
			return sizeof(p_drv_buf->tx_frames);
		}
		break;
	case DRV_TLV_ISCSI_PDU_TX_BYTES_SENT:
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
		else if (tlv_group == QED_MFW_TLV_FCOE)
			len = qed_mfw_get_fcoe_tlv_value(&tlv,
							 &p_tlv_data->fcoe,
							 &buffer);
		else
			len = qed_mfw_get_iscsi_tlv_value(&tlv,
							  &p_tlv_data->iscsi,
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
		val = be32_to_cpu((__force __be32)val);
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

	if ((tlv_group & QED_MFW_TLV_FCOE) &&
	    p_hwfn->hw_info.personality != QED_PCI_FCOE) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Skipping FCoE TLVs for non-FCoE function\n");
		tlv_group &= ~QED_MFW_TLV_FCOE;
	}

	if ((tlv_group & QED_MFW_TLV_ISCSI) &&
	    p_hwfn->hw_info.personality != QED_PCI_ISCSI) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Skipping iSCSI TLVs for non-iSCSI function\n");
		tlv_group &= ~QED_MFW_TLV_ISCSI;
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
		val = (__force u32)cpu_to_be32(val);
		qed_wr(p_hwfn, p_ptt, addr + offset, val);
	}

drv_done:
	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_GET_TLV_DONE, 0, &resp,
			 &param);

	vfree(p_mfw_buf);

	return rc;
}
