/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_SRIOV_H
#define _QED_SRIOV_H
#include <linux/types.h>
#include "qed_vf.h"

#define QED_ETH_VF_NUM_MAC_FILTERS 1
#define QED_ETH_VF_NUM_VLAN_FILTERS 2
#define QED_VF_ARRAY_LENGTH (3)

#ifdef CONFIG_QED_SRIOV
#define IS_VF(cdev)             ((cdev)->b_is_vf)
#define IS_PF(cdev)             (!((cdev)->b_is_vf))
#define IS_PF_SRIOV(p_hwfn)     (!!((p_hwfn)->cdev->p_iov_info))
#else
#define IS_VF(cdev)             (0)
#define IS_PF(cdev)             (1)
#define IS_PF_SRIOV(p_hwfn)     (0)
#endif
#define IS_PF_SRIOV_ALLOC(p_hwfn)       (!!((p_hwfn)->pf_iov_info))

#define QED_MAX_VF_CHAINS_PER_PF 16

#define QED_ETH_MAX_VF_NUM_VLAN_FILTERS	\
	(MAX_NUM_VFS * QED_ETH_VF_NUM_VLAN_FILTERS)

enum qed_iov_vport_update_flag {
	QED_IOV_VP_UPDATE_ACTIVATE,
	QED_IOV_VP_UPDATE_VLAN_STRIP,
	QED_IOV_VP_UPDATE_TX_SWITCH,
	QED_IOV_VP_UPDATE_MCAST,
	QED_IOV_VP_UPDATE_ACCEPT_PARAM,
	QED_IOV_VP_UPDATE_RSS,
	QED_IOV_VP_UPDATE_ACCEPT_ANY_VLAN,
	QED_IOV_VP_UPDATE_SGE_TPA,
	QED_IOV_VP_UPDATE_MAX,
};

struct qed_public_vf_info {
	/* These copies will later be reflected in the bulletin board,
	 * but this copy should be newer.
	 */
	u8 forced_mac[ETH_ALEN];
	u16 forced_vlan;
	u8 mac[ETH_ALEN];

	/* IFLA_VF_LINK_STATE_<X> */
	int link_state;

	/* Currently configured Tx rate in MB/sec. 0 if unconfigured */
	int tx_rate;
};

/* This struct is part of qed_dev and contains data relevant to all hwfns;
 * Initialized only if SR-IOV cpabability is exposed in PCIe config space.
 */
struct qed_hw_sriov_info {
	int pos;		/* capability position */
	int nres;		/* number of resources */
	u32 cap;		/* SR-IOV Capabilities */
	u16 ctrl;		/* SR-IOV Control */
	u16 total_vfs;		/* total VFs associated with the PF */
	u16 num_vfs;		/* number of vfs that have been started */
	u16 initial_vfs;	/* initial VFs associated with the PF */
	u16 nr_virtfn;		/* number of VFs available */
	u16 offset;		/* first VF Routing ID offset */
	u16 stride;		/* following VF stride */
	u16 vf_device_id;	/* VF device id */
	u32 pgsz;		/* page size for BAR alignment */
	u8 link;		/* Function Dependency Link */

	u32 first_vf_in_pf;
};

/* This mailbox is maintained per VF in its PF contains all information
 * required for sending / receiving a message.
 */
struct qed_iov_vf_mbx {
	union vfpf_tlvs *req_virt;
	dma_addr_t req_phys;
	union pfvf_tlvs *reply_virt;
	dma_addr_t reply_phys;

	/* Address in VF where a pending message is located */
	dma_addr_t pending_req;

	u8 *offset;

	/* saved VF request header */
	struct vfpf_first_tlv first_tlv;
};

struct qed_vf_q_info {
	u16 fw_rx_qid;
	u16 fw_tx_qid;
	u8 fw_cid;
	u8 rxq_active;
	u8 txq_active;
};

enum vf_state {
	VF_FREE = 0,		/* VF ready to be acquired holds no resc */
	VF_ACQUIRED,		/* VF, acquired, but not initalized */
	VF_ENABLED,		/* VF, Enabled */
	VF_RESET,		/* VF, FLR'd, pending cleanup */
	VF_STOPPED		/* VF, Stopped */
};

struct qed_vf_vlan_shadow {
	bool used;
	u16 vid;
};

struct qed_vf_shadow_config {
	/* Shadow copy of all guest vlans */
	struct qed_vf_vlan_shadow vlans[QED_ETH_VF_NUM_VLAN_FILTERS + 1];

	/* Shadow copy of all configured MACs; Empty if forcing MACs */
	u8 macs[QED_ETH_VF_NUM_MAC_FILTERS][ETH_ALEN];
	u8 inner_vlan_removal;
};

/* PFs maintain an array of this structure, per VF */
struct qed_vf_info {
	struct qed_iov_vf_mbx vf_mbx;
	enum vf_state state;
	bool b_init;
	u8 to_disable;

	struct qed_bulletin bulletin;
	dma_addr_t vf_bulletin;

	/* PF saves a copy of the last VF acquire message */
	struct vfpf_acquire_tlv acquire;

	u32 concrete_fid;
	u16 opaque_fid;
	u16 mtu;

	u8 vport_id;
	u8 relative_vf_id;
	u8 abs_vf_id;
#define QED_VF_ABS_ID(p_hwfn, p_vf)	(QED_PATH_ID(p_hwfn) ?		      \
					 (p_vf)->abs_vf_id + MAX_NUM_VFS_BB : \
					 (p_vf)->abs_vf_id)

	u8 vport_instance;
	u8 num_rxqs;
	u8 num_txqs;

	u8 num_sbs;

	u8 num_mac_filters;
	u8 num_vlan_filters;
	struct qed_vf_q_info vf_queues[QED_MAX_VF_CHAINS_PER_PF];
	u16 igu_sbs[QED_MAX_VF_CHAINS_PER_PF];
	u8 num_active_rxqs;
	struct qed_public_vf_info p_vf_info;
	bool spoof_chk;
	bool req_spoofchk_val;

	/* Stores the configuration requested by VF */
	struct qed_vf_shadow_config shadow_config;

	/* A bitfield using bulletin's valid-map bits, used to indicate
	 * which of the bulletin board features have been configured.
	 */
	u64 configured_features;
#define QED_IOV_CONFIGURED_FEATURES_MASK        ((1 << MAC_ADDR_FORCED) | \
						 (1 << VLAN_ADDR_FORCED))
};

/* This structure is part of qed_hwfn and used only for PFs that have sriov
 * capability enabled.
 */
struct qed_pf_iov {
	struct qed_vf_info vfs_array[MAX_NUM_VFS];
	u64 pending_events[QED_VF_ARRAY_LENGTH];
	u64 pending_flr[QED_VF_ARRAY_LENGTH];

	/* Allocate message address continuosuly and split to each VF */
	void *mbx_msg_virt_addr;
	dma_addr_t mbx_msg_phys_addr;
	u32 mbx_msg_size;
	void *mbx_reply_virt_addr;
	dma_addr_t mbx_reply_phys_addr;
	u32 mbx_reply_size;
	void *p_bulletins;
	dma_addr_t bulletins_phys;
	u32 bulletins_size;
};

enum qed_iov_wq_flag {
	QED_IOV_WQ_MSG_FLAG,
	QED_IOV_WQ_SET_UNICAST_FILTER_FLAG,
	QED_IOV_WQ_BULLETIN_UPDATE_FLAG,
	QED_IOV_WQ_STOP_WQ_FLAG,
	QED_IOV_WQ_FLR_FLAG,
};

#ifdef CONFIG_QED_SRIOV
/**
 * @brief - Given a VF index, return index of next [including that] active VF.
 *
 * @param p_hwfn
 * @param rel_vf_id
 *
 * @return MAX_NUM_VFS in case no further active VFs, otherwise index.
 */
u16 qed_iov_get_next_active_vf(struct qed_hwfn *p_hwfn, u16 rel_vf_id);

/**
 * @brief Read sriov related information and allocated resources
 *  reads from configuraiton space, shmem, etc.
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_iov_hw_info(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_add_tlv - place a given tlv on the tlv buffer at next offset
 *
 * @param p_hwfn
 * @param p_iov
 * @param type
 * @param length
 *
 * @return pointer to the newly placed tlv
 */
void *qed_add_tlv(struct qed_hwfn *p_hwfn, u8 **offset, u16 type, u16 length);

/**
 * @brief list the types and lengths of the tlvs on the buffer
 *
 * @param p_hwfn
 * @param tlvs_list
 */
void qed_dp_tlv_list(struct qed_hwfn *p_hwfn, void *tlvs_list);

/**
 * @brief qed_iov_alloc - allocate sriov related resources
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_iov_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_iov_setup - setup sriov related resources
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_iov_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief qed_iov_free - free sriov related resources
 *
 * @param p_hwfn
 */
void qed_iov_free(struct qed_hwfn *p_hwfn);

/**
 * @brief free sriov related memory that was allocated during hw_prepare
 *
 * @param cdev
 */
void qed_iov_free_hw_info(struct qed_dev *cdev);

/**
 * @brief qed_sriov_eqe_event - handle async sriov event arrived on eqe.
 *
 * @param p_hwfn
 * @param opcode
 * @param echo
 * @param data
 */
int qed_sriov_eqe_event(struct qed_hwfn *p_hwfn,
			u8 opcode, __le16 echo, union event_ring_data *data);

/**
 * @brief Mark structs of vfs that have been FLR-ed.
 *
 * @param p_hwfn
 * @param disabled_vfs - bitmask of all VFs on path that were FLRed
 *
 * @return 1 iff one of the PF's vfs got FLRed. 0 otherwise.
 */
int qed_iov_mark_vf_flr(struct qed_hwfn *p_hwfn, u32 *disabled_vfs);

/**
 * @brief Search extended TLVs in request/reply buffer.
 *
 * @param p_hwfn
 * @param p_tlvs_list - Pointer to tlvs list
 * @param req_type - Type of TLV
 *
 * @return pointer to tlv type if found, otherwise returns NULL.
 */
void *qed_iov_search_list_tlvs(struct qed_hwfn *p_hwfn,
			       void *p_tlvs_list, u16 req_type);

void qed_iov_wq_stop(struct qed_dev *cdev, bool schedule_first);
int qed_iov_wq_start(struct qed_dev *cdev);

void qed_schedule_iov(struct qed_hwfn *hwfn, enum qed_iov_wq_flag flag);
void qed_vf_start_iov_wq(struct qed_dev *cdev);
int qed_sriov_disable(struct qed_dev *cdev, bool pci_enabled);
void qed_inform_vf_link_state(struct qed_hwfn *hwfn);
#else
static inline u16 qed_iov_get_next_active_vf(struct qed_hwfn *p_hwfn,
					     u16 rel_vf_id)
{
	return MAX_NUM_VFS;
}

static inline int qed_iov_hw_info(struct qed_hwfn *p_hwfn)
{
	return 0;
}

static inline int qed_iov_alloc(struct qed_hwfn *p_hwfn)
{
	return 0;
}

static inline void qed_iov_setup(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
}

static inline void qed_iov_free(struct qed_hwfn *p_hwfn)
{
}

static inline void qed_iov_free_hw_info(struct qed_dev *cdev)
{
}

static inline int qed_sriov_eqe_event(struct qed_hwfn *p_hwfn,
				      u8 opcode,
				      __le16 echo, union event_ring_data *data)
{
	return -EINVAL;
}

static inline int qed_iov_mark_vf_flr(struct qed_hwfn *p_hwfn,
				      u32 *disabled_vfs)
{
	return 0;
}

static inline void qed_iov_wq_stop(struct qed_dev *cdev, bool schedule_first)
{
}

static inline int qed_iov_wq_start(struct qed_dev *cdev)
{
	return 0;
}

static inline void qed_schedule_iov(struct qed_hwfn *hwfn,
				    enum qed_iov_wq_flag flag)
{
}

static inline void qed_vf_start_iov_wq(struct qed_dev *cdev)
{
}

static inline int qed_sriov_disable(struct qed_dev *cdev, bool pci_enabled)
{
	return 0;
}

static inline void qed_inform_vf_link_state(struct qed_hwfn *hwfn)
{
}
#endif

#define qed_for_each_vf(_p_hwfn, _i)			  \
	for (_i = qed_iov_get_next_active_vf(_p_hwfn, 0); \
	     _i < MAX_NUM_VFS;				  \
	     _i = qed_iov_get_next_active_vf(_p_hwfn, _i + 1))

#endif
