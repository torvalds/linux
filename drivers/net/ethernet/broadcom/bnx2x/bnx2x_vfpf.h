/* bnx2x_vfpf.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2011-2012 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Ariel Elior <ariele@broadcom.com>
 */
#ifndef VF_PF_IF_H
#define VF_PF_IF_H

/* Common definitions for all HVs */
struct vf_pf_resc_request {
	u8  num_rxqs;
	u8  num_txqs;
	u8  num_sbs;
	u8  num_mac_filters;
	u8  num_vlan_filters;
	u8  num_mc_filters; /* No limit  so superfluous */
};

struct hw_sb_info {
	u8 hw_sb_id;	/* aka absolute igu id, used to ack the sb */
	u8 sb_qid;	/* used to update DHC for sb */
};

/* HW VF-PF channel definitions
 * A.K.A VF-PF mailbox
 */
#define TLV_BUFFER_SIZE			1024

enum {
	PFVF_STATUS_WAITING = 0,
	PFVF_STATUS_SUCCESS,
	PFVF_STATUS_FAILURE,
	PFVF_STATUS_NOT_SUPPORTED,
	PFVF_STATUS_NO_RESOURCE
};

/* vf pf channel tlvs */
/* general tlv header (used for both vf->pf request and pf->vf response) */
struct channel_tlv {
	u16 type;
	u16 length;
};

/* header of first vf->pf tlv carries the offset used to calculate response
 * buffer address
 */
struct vfpf_first_tlv {
	struct channel_tlv tl;
	u32 resp_msg_offset;
};

/* header of pf->vf tlvs, carries the status of handling the request */
struct pfvf_tlv {
	struct channel_tlv tl;
	u8 status;
	u8 padding[3];
};

/* response tlv used for most tlvs */
struct pfvf_general_resp_tlv {
	struct pfvf_tlv hdr;
};

/* used to terminate and pad a tlv list */
struct channel_list_end_tlv {
	struct channel_tlv tl;
	u8 padding[4];
};

/* Acquire */
struct vfpf_acquire_tlv {
	struct vfpf_first_tlv first_tlv;

	struct vf_pf_vfdev_info {
		/* the following fields are for debug purposes */
		u8  vf_id;		/* ME register value */
		u8  vf_os;		/* e.g. Linux, W2K8 */
		u8 padding[2];
	} vfdev_info;

	struct vf_pf_resc_request resc_request;

	aligned_u64 bulletin_addr;
};

/* acquire response tlv - carries the allocated resources */
struct pfvf_acquire_resp_tlv {
	struct pfvf_tlv hdr;
	struct pf_vf_pfdev_info {
		u32 chip_num;
		u32 pf_cap;
#define PFVF_CAP_RSS		0x00000001
#define PFVF_CAP_DHC		0x00000002
#define PFVF_CAP_TPA		0x00000004
		char fw_ver[32];
		u16 db_size;
		u8  indices_per_sb;
		u8  padding;
	} pfdev_info;
	struct pf_vf_resc {
		/* in case of status NO_RESOURCE in message hdr, pf will fill
		 * this struct with suggested amount of resources for next
		 * acquire request
		 */
#define PFVF_MAX_QUEUES_PER_VF         16
#define PFVF_MAX_SBS_PER_VF            16
		struct hw_sb_info hw_sbs[PFVF_MAX_SBS_PER_VF];
		u8	hw_qid[PFVF_MAX_QUEUES_PER_VF];
		u8	num_rxqs;
		u8	num_txqs;
		u8	num_sbs;
		u8	num_mac_filters;
		u8	num_vlan_filters;
		u8	num_mc_filters;
		u8	permanent_mac_addr[ETH_ALEN];
		u8	current_mac_addr[ETH_ALEN];
		u8	padding[2];
	} resc;
};

/* release the VF's acquired resources */
struct vfpf_release_tlv {
	struct vfpf_first_tlv	first_tlv;
	u16			vf_id;
	u8 padding[2];
};

struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

union vfpf_tlvs {
	struct vfpf_first_tlv		first_tlv;
	struct vfpf_acquire_tlv		acquire;
	struct vfpf_release_tlv         release;
	struct channel_list_end_tlv     list_end;
	struct tlv_buffer_size		tlv_buf_size;
};

union pfvf_tlvs {
	struct pfvf_general_resp_tlv    general_resp;
	struct pfvf_acquire_resp_tlv	acquire_resp;
	struct channel_list_end_tlv	list_end;
	struct tlv_buffer_size		tlv_buf_size;
};

#define MAX_TLVS_IN_LIST 50

enum channel_tlvs {
	CHANNEL_TLV_NONE,
	CHANNEL_TLV_ACQUIRE,
	CHANNEL_TLV_RELEASE,
	CHANNEL_TLV_LIST_END,
	CHANNEL_TLV_MAX
};

#endif /* VF_PF_IF_H */
