/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_VF_H
#define _QED_VF_H

enum {
	PFVF_STATUS_WAITING,
	PFVF_STATUS_SUCCESS,
	PFVF_STATUS_FAILURE,
	PFVF_STATUS_NOT_SUPPORTED,
	PFVF_STATUS_NO_RESOURCE,
	PFVF_STATUS_FORCED,
};

/* vf pf channel tlvs */
/* general tlv header (used for both vf->pf request and pf->vf response) */
struct channel_tlv {
	u16 type;
	u16 length;
};

/* header of first vf->pf tlv carries the offset used to calculate reponse
 * buffer address
 */
struct vfpf_first_tlv {
	struct channel_tlv tl;
	u32 padding;
	u64 reply_address;
};

/* header of pf->vf tlvs, carries the status of handling the request */
struct pfvf_tlv {
	struct channel_tlv tl;
	u8 status;
	u8 padding[3];
};

/* response tlv used for most tlvs */
struct pfvf_def_resp_tlv {
	struct pfvf_tlv hdr;
};

/* used to terminate and pad a tlv list */
struct channel_list_end_tlv {
	struct channel_tlv tl;
	u8 padding[4];
};

#define TLV_BUFFER_SIZE                 1024
struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

union vfpf_tlvs {
	struct vfpf_first_tlv first_tlv;
	struct channel_list_end_tlv list_end;
	struct tlv_buffer_size tlv_buf_size;
};

union pfvf_tlvs {
	struct pfvf_def_resp_tlv default_resp;
	struct tlv_buffer_size tlv_buf_size;
};

struct qed_bulletin_content {
	/* crc of structure to ensure is not in mid-update */
	u32 crc;

	u32 version;

	/* bitmap indicating which fields hold valid values */
	u64 valid_bitmap;
};

struct qed_bulletin {
	dma_addr_t phys;
	struct qed_bulletin_content *p_virt;
	u32 size;
};

enum {
	CHANNEL_TLV_NONE,	/* ends tlv sequence */
	CHANNEL_TLV_LIST_END,
	CHANNEL_TLV_MAX
};

#endif
