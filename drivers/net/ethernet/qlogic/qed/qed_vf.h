/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_VF_H
#define _QED_VF_H

struct vf_pf_resc_request {
	u8 num_rxqs;
	u8 num_txqs;
	u8 num_sbs;
	u8 num_mac_filters;
	u8 num_vlan_filters;
	u8 num_mc_filters;
	u16 padding;
};

struct hw_sb_info {
	u16 hw_sb_id;
	u8 sb_qid;
	u8 padding[5];
};

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

#define VFPF_ACQUIRE_OS_LINUX (0)
#define VFPF_ACQUIRE_OS_WINDOWS (1)
#define VFPF_ACQUIRE_OS_ESX (2)
#define VFPF_ACQUIRE_OS_SOLARIS (3)
#define VFPF_ACQUIRE_OS_LINUX_USERSPACE (4)

struct vfpf_acquire_tlv {
	struct vfpf_first_tlv first_tlv;

	struct vf_pf_vfdev_info {
#define VFPF_ACQUIRE_CAP_OBSOLETE	(1 << 0)
#define VFPF_ACQUIRE_CAP_100G		(1 << 1) /* VF can support 100g */
		u64 capabilities;
		u8 fw_major;
		u8 fw_minor;
		u8 fw_revision;
		u8 fw_engineering;
		u32 driver_version;
		u16 opaque_fid;	/* ME register value */
		u8 os_type;	/* VFPF_ACQUIRE_OS_* value */
		u8 padding[5];
	} vfdev_info;

	struct vf_pf_resc_request resc_request;

	u64 bulletin_addr;
	u32 bulletin_size;
	u32 padding;
};

struct pfvf_storm_stats {
	u32 address;
	u32 len;
};

struct pfvf_stats_info {
	struct pfvf_storm_stats mstats;
	struct pfvf_storm_stats pstats;
	struct pfvf_storm_stats tstats;
	struct pfvf_storm_stats ustats;
};

struct pfvf_acquire_resp_tlv {
	struct pfvf_tlv hdr;

	struct pf_vf_pfdev_info {
		u32 chip_num;
		u32 mfw_ver;

		u16 fw_major;
		u16 fw_minor;
		u16 fw_rev;
		u16 fw_eng;

		u64 capabilities;
#define PFVF_ACQUIRE_CAP_DEFAULT_UNTAGGED	BIT(0)
#define PFVF_ACQUIRE_CAP_100G			BIT(1)	/* If set, 100g PF */
/* There are old PF versions where the PF might mistakenly override the sanity
 * mechanism [version-based] and allow a VF that can't be supported to pass
 * the acquisition phase.
 * To overcome this, PFs now indicate that they're past that point and the new
 * VFs would fail probe on the older PFs that fail to do so.
 */
#define PFVF_ACQUIRE_CAP_POST_FW_OVERRIDE	BIT(2)

		u16 db_size;
		u8 indices_per_sb;
		u8 os_type;

		/* These should match the PF's qed_dev values */
		u16 chip_rev;
		u8 dev_type;

		u8 padding;

		struct pfvf_stats_info stats_info;

		u8 port_mac[ETH_ALEN];
		u8 padding2[2];
	} pfdev_info;

	struct pf_vf_resc {
#define PFVF_MAX_QUEUES_PER_VF		16
#define PFVF_MAX_SBS_PER_VF		16
		struct hw_sb_info hw_sbs[PFVF_MAX_SBS_PER_VF];
		u8 hw_qid[PFVF_MAX_QUEUES_PER_VF];
		u8 cid[PFVF_MAX_QUEUES_PER_VF];

		u8 num_rxqs;
		u8 num_txqs;
		u8 num_sbs;
		u8 num_mac_filters;
		u8 num_vlan_filters;
		u8 num_mc_filters;
		u8 padding[2];
	} resc;

	u32 bulletin_size;
	u32 padding;
};

#define TLV_BUFFER_SIZE                 1024
struct tlv_buffer_size {
	u8 tlv_buffer[TLV_BUFFER_SIZE];
};

union vfpf_tlvs {
	struct vfpf_first_tlv first_tlv;
	struct vfpf_acquire_tlv acquire;
	struct channel_list_end_tlv list_end;
	struct tlv_buffer_size tlv_buf_size;
};

union pfvf_tlvs {
	struct pfvf_def_resp_tlv default_resp;
	struct pfvf_acquire_resp_tlv acquire_resp;
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
	CHANNEL_TLV_ACQUIRE,
	CHANNEL_TLV_LIST_END,
	CHANNEL_TLV_MAX
};

/* This data is held in the qed_hwfn structure for VFs only. */
struct qed_vf_iov {
	union vfpf_tlvs *vf2pf_request;
	dma_addr_t vf2pf_request_phys;
	union pfvf_tlvs *pf2vf_reply;
	dma_addr_t pf2vf_reply_phys;

	/* Should be taken whenever the mailbox buffers are accessed */
	struct mutex mutex;
	u8 *offset;

	/* Bulletin Board */
	struct qed_bulletin bulletin;
	struct qed_bulletin_content bulletin_shadow;

	/* we set aside a copy of the acquire response */
	struct pfvf_acquire_resp_tlv acquire_resp;
};

#ifdef CONFIG_QED_SRIOV
/**
 * @brief Get number of Rx queues allocated for VF by qed
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated RX queues
 */
void qed_vf_get_num_rxqs(struct qed_hwfn *p_hwfn, u8 *num_rxqs);

/**
 * @brief Get port mac address for VF
 *
 * @param p_hwfn
 * @param port_mac - destination location for port mac
 */
void qed_vf_get_port_mac(struct qed_hwfn *p_hwfn, u8 *port_mac);

/**
 * @brief Get number of VLAN filters allocated for VF by qed
 *
 *  @param p_hwfn
 *  @param num_rxqs - allocated VLAN filters
 */
void qed_vf_get_num_vlan_filters(struct qed_hwfn *p_hwfn,
				 u8 *num_vlan_filters);

/**
 * @brief Set firmware version information in dev_info from VFs acquire response tlv
 *
 * @param p_hwfn
 * @param fw_major
 * @param fw_minor
 * @param fw_rev
 * @param fw_eng
 */
void qed_vf_get_fw_version(struct qed_hwfn *p_hwfn,
			   u16 *fw_major, u16 *fw_minor,
			   u16 *fw_rev, u16 *fw_eng);

/**
 * @brief hw preparation for VF
 *      sends ACQUIRE message
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_vf_hw_prepare(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_vf_get_igu_sb_id - Get the IGU SB ID for a given
 *        sb_id. For VFs igu sbs don't have to be contiguous
 *
 * @param p_hwfn
 * @param sb_id
 *
 * @return INLINE u16
 */
u16 qed_vf_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id);
#else
static inline void qed_vf_get_num_rxqs(struct qed_hwfn *p_hwfn, u8 *num_rxqs)
{
}

static inline void qed_vf_get_port_mac(struct qed_hwfn *p_hwfn, u8 *port_mac)
{
}

static inline void qed_vf_get_num_vlan_filters(struct qed_hwfn *p_hwfn,
					       u8 *num_vlan_filters)
{
}

static inline void qed_vf_get_fw_version(struct qed_hwfn *p_hwfn,
					 u16 *fw_major, u16 *fw_minor,
					 u16 *fw_rev, u16 *fw_eng)
{
}

static inline int qed_vf_hw_prepare(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline u16 qed_vf_get_igu_sb_id(struct qed_hwfn *p_hwfn, u16 sb_id)
{
	return 0;
}
#endif

#endif
