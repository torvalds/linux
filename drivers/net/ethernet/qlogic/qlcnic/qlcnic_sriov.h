/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 */

#ifndef _QLCNIC_83XX_SRIOV_H_
#define _QLCNIC_83XX_SRIOV_H_

#include <linux/types.h>
#include <linux/pci.h>

#include "qlcnic.h"

extern const u32 qlcnic_83xx_reg_tbl[];
extern const u32 qlcnic_83xx_ext_reg_tbl[];

struct qlcnic_bc_payload {
	u64 payload[126];
};

struct qlcnic_bc_hdr {
#if defined(__LITTLE_ENDIAN)
	u8	version;
	u8	msg_type:4;
	u8	rsvd1:3;
	u8	op_type:1;
	u8	num_cmds;
	u8	num_frags;
	u8	frag_num;
	u8	cmd_op;
	u16	seq_id;
	u64	rsvd3;
#elif defined(__BIG_ENDIAN)
	u8	num_frags;
	u8	num_cmds;
	u8	op_type:1;
	u8	rsvd1:3;
	u8	msg_type:4;
	u8	version;
	u16	seq_id;
	u8	cmd_op;
	u8	frag_num;
	u64	rsvd3;
#endif
};

enum qlcnic_bc_commands {
	QLCNIC_BC_CMD_CHANNEL_INIT = 0x0,
	QLCNIC_BC_CMD_CHANNEL_TERM = 0x1,
	QLCNIC_BC_CMD_GET_ACL = 0x2,
	QLCNIC_BC_CMD_CFG_GUEST_VLAN = 0x3,
};

#define QLCNIC_83XX_SRIOV_VF_MAX_MAC 2
#define QLC_BC_CMD 1

struct qlcnic_trans_list {
	/* Lock for manipulating list */
	spinlock_t		lock;
	struct list_head	wait_list;
	int			count;
};

enum qlcnic_trans_state {
	QLC_INIT = 0,
	QLC_WAIT_FOR_CHANNEL_FREE,
	QLC_WAIT_FOR_RESP,
	QLC_ABORT,
	QLC_END,
};

struct qlcnic_bc_trans {
	u8				func_id;
	u8				active;
	u8				curr_rsp_frag;
	u8				curr_req_frag;
	u16				cmd_id;
	u16				req_pay_size;
	u16				rsp_pay_size;
	u32				trans_id;
	enum qlcnic_trans_state		trans_state;
	struct list_head		list;
	struct qlcnic_bc_hdr		*req_hdr;
	struct qlcnic_bc_hdr		*rsp_hdr;
	struct qlcnic_bc_payload	*req_pay;
	struct qlcnic_bc_payload	*rsp_pay;
	struct completion		resp_cmpl;
	struct qlcnic_vf_info		*vf;
};

enum qlcnic_vf_state {
	QLC_BC_VF_SEND = 0,
	QLC_BC_VF_RECV,
	QLC_BC_VF_CHANNEL,
	QLC_BC_VF_STATE,
	QLC_BC_VF_FLR,
	QLC_BC_VF_SOFT_FLR,
};

enum qlcnic_vlan_mode {
	QLC_NO_VLAN_MODE = 0,
	QLC_PVID_MODE,
	QLC_GUEST_VLAN_MODE,
};

struct qlcnic_resources {
	u16 num_tx_mac_filters;
	u16 num_rx_ucast_mac_filters;
	u16 num_rx_mcast_mac_filters;

	u16 num_txvlan_keys;

	u16 num_rx_queues;
	u16 num_tx_queues;

	u16 num_rx_buf_rings;
	u16 num_rx_status_rings;

	u16 num_destip;
	u32 num_lro_flows_supported;
	u16 max_local_ipv6_addrs;
	u16 max_remote_ipv6_addrs;
};

struct qlcnic_vport {
	u16			handle;
	u16			max_tx_bw;
	u16			min_tx_bw;
	u16			pvid;
	u8			vlan_mode;
	u8			qos;
	bool			spoofchk;
	u8			mac[6];
};

struct qlcnic_vf_info {
	u8				pci_func;
	u16				rx_ctx_id;
	u16				tx_ctx_id;
	u16				*sriov_vlans;
	int				num_vlan;
	unsigned long			state;
	struct completion		ch_free_cmpl;
	struct work_struct		trans_work;
	struct work_struct		flr_work;
	/* It synchronizes commands sent from VF */
	struct mutex			send_cmd_lock;
	struct qlcnic_bc_trans		*send_cmd;
	struct qlcnic_bc_trans		*flr_trans;
	struct qlcnic_trans_list	rcv_act;
	struct qlcnic_trans_list	rcv_pend;
	struct qlcnic_adapter		*adapter;
	struct qlcnic_vport		*vp;
	spinlock_t			vlan_list_lock;	/* Lock for VLAN list */
};

struct qlcnic_async_cmd {
	struct list_head	list;
	struct qlcnic_cmd_args	*cmd;
};

struct qlcnic_back_channel {
	u16			trans_counter;
	struct workqueue_struct *bc_trans_wq;
	struct workqueue_struct *bc_async_wq;
	struct workqueue_struct *bc_flr_wq;
	struct qlcnic_adapter	*adapter;
	struct list_head	async_cmd_list;
	struct work_struct	vf_async_work;
	spinlock_t		queue_lock; /* async_cmd_list queue lock */
};

struct qlcnic_sriov {
	u16				vp_handle;
	u8				num_vfs;
	u8				any_vlan;
	u8				vlan_mode;
	u16				num_allowed_vlans;
	u16				*allowed_vlans;
	u16				vlan;
	struct qlcnic_resources		ff_max;
	struct qlcnic_back_channel	bc;
	struct qlcnic_vf_info		*vf_info;
};

int qlcnic_sriov_init(struct qlcnic_adapter *, int);
void qlcnic_sriov_cleanup(struct qlcnic_adapter *);
void __qlcnic_sriov_cleanup(struct qlcnic_adapter *);
void qlcnic_sriov_vf_register_map(struct qlcnic_hardware_context *);
int qlcnic_sriov_vf_init(struct qlcnic_adapter *, int);
void qlcnic_sriov_vf_set_ops(struct qlcnic_adapter *);
int qlcnic_sriov_func_to_index(struct qlcnic_adapter *, u8);
void qlcnic_sriov_handle_bc_event(struct qlcnic_adapter *, u32);
int qlcnic_sriov_cfg_bc_intr(struct qlcnic_adapter *, u8);
void qlcnic_sriov_cleanup_async_list(struct qlcnic_back_channel *);
void qlcnic_sriov_cleanup_list(struct qlcnic_trans_list *);
int __qlcnic_sriov_add_act_list(struct qlcnic_sriov *, struct qlcnic_vf_info *,
				struct qlcnic_bc_trans *);
int qlcnic_sriov_get_vf_vport_info(struct qlcnic_adapter *,
				   struct qlcnic_info *, u16);
int qlcnic_sriov_cfg_vf_guest_vlan(struct qlcnic_adapter *, u16, u8);
void qlcnic_sriov_free_vlans(struct qlcnic_adapter *);
int qlcnic_sriov_alloc_vlans(struct qlcnic_adapter *);
bool qlcnic_sriov_check_any_vlan(struct qlcnic_vf_info *);
void qlcnic_sriov_del_vlan_id(struct qlcnic_sriov *,
			      struct qlcnic_vf_info *, u16);
void qlcnic_sriov_add_vlan_id(struct qlcnic_sriov *,
			      struct qlcnic_vf_info *, u16);

static inline bool qlcnic_sriov_enable_check(struct qlcnic_adapter *adapter)
{
	return test_bit(__QLCNIC_SRIOV_ENABLE, &adapter->state) ? true : false;
}

#ifdef CONFIG_QLCNIC_SRIOV
void qlcnic_sriov_pf_process_bc_cmd(struct qlcnic_adapter *,
				    struct qlcnic_bc_trans *,
				    struct qlcnic_cmd_args *);
void qlcnic_sriov_pf_disable(struct qlcnic_adapter *);
void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *);
int qlcnic_pci_sriov_configure(struct pci_dev *, int);
void qlcnic_pf_set_interface_id_create_rx_ctx(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_create_tx_ctx(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_del_rx_ctx(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_del_tx_ctx(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_promisc(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_ipaddr(struct qlcnic_adapter *, u32 *);
void qlcnic_pf_set_interface_id_macaddr(struct qlcnic_adapter *, u32 *);
void qlcnic_sriov_pf_handle_flr(struct qlcnic_sriov *, struct qlcnic_vf_info *);
bool qlcnic_sriov_soft_flr_check(struct qlcnic_adapter *,
				 struct qlcnic_bc_trans *,
				 struct qlcnic_vf_info *);
void qlcnic_sriov_pf_reset(struct qlcnic_adapter *);
int qlcnic_sriov_pf_reinit(struct qlcnic_adapter *);
int qlcnic_sriov_set_vf_mac(struct net_device *, int, u8 *);
int qlcnic_sriov_set_vf_tx_rate(struct net_device *, int, int, int);
int qlcnic_sriov_get_vf_config(struct net_device *, int ,
			       struct ifla_vf_info *);
int qlcnic_sriov_set_vf_vlan(struct net_device *, int, u16, u8, __be16);
int qlcnic_sriov_set_vf_spoofchk(struct net_device *, int, bool);
#else
static inline void qlcnic_sriov_pf_disable(struct qlcnic_adapter *adapter) {}
static inline void qlcnic_sriov_pf_cleanup(struct qlcnic_adapter *adapter) {}
static inline void
qlcnic_pf_set_interface_id_create_rx_ctx(struct qlcnic_adapter *adapter,
					 u32 *int_id) {}
static inline void
qlcnic_pf_set_interface_id_create_tx_ctx(struct qlcnic_adapter *adapter,
					 u32 *int_id) {}
static inline void
qlcnic_pf_set_interface_id_del_rx_ctx(struct qlcnic_adapter *adapter,
				      u32 *int_id) {}
static inline void
qlcnic_pf_set_interface_id_del_tx_ctx(struct qlcnic_adapter *adapter,
				      u32 *int_id) {}
static inline void
qlcnic_pf_set_interface_id_ipaddr(struct qlcnic_adapter *adapter, u32 *int_id)
{}
static inline void
qlcnic_pf_set_interface_id_macaddr(struct qlcnic_adapter *adapter, u32 *int_id)
{}
static inline void
qlcnic_pf_set_interface_id_promisc(struct qlcnic_adapter *adapter, u32 *int_id)
{}
static inline void qlcnic_sriov_pf_handle_flr(struct qlcnic_sriov *sriov,
					      struct qlcnic_vf_info *vf) {}
static inline bool qlcnic_sriov_soft_flr_check(struct qlcnic_adapter *adapter,
					       struct qlcnic_bc_trans *trans,
					       struct qlcnic_vf_info *vf)
{ return false; }
static inline void qlcnic_sriov_pf_reset(struct qlcnic_adapter *adapter) {}
static inline int qlcnic_sriov_pf_reinit(struct qlcnic_adapter *adapter)
{ return 0; }
#endif

#endif
