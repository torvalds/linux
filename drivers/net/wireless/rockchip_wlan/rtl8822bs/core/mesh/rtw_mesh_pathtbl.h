/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __RTW_MESH_PATHTBL_H_
#define __RTW_MESH_PATHTBL_H_

#ifndef DBG_RTW_MPATH
#define DBG_RTW_MPATH 1
#endif
#if DBG_RTW_MPATH
#define RTW_MPATH_DBG(fmt, arg...) RTW_PRINT(fmt, ##arg)
#else
#define RTW_MPATH_DBG(fmt, arg...) do {} while (0)
#endif

/**
 * enum rtw_mesh_path_flags - mesh path flags
 *
 * @RTW_MESH_PATH_ACTIVE: the mesh path can be used for forwarding
 * @RTW_MESH_PATH_RESOLVING: the discovery process is running for this mesh path
 * @RTW_MESH_PATH_SN_VALID: the mesh path contains a valid destination sequence
 *	number
 * @RTW_MESH_PATH_FIXED: the mesh path has been manually set and should not be
 *	modified
 * @RTW_MESH_PATH_RESOLVED: the mesh path can has been resolved
 * @RTW_MESH_PATH_REQ_QUEUED: there is an unsent path request for this destination
 *	already queued up, waiting for the discovery process to start.
 * @RTW_MESH_PATH_DELETED: the mesh path has been deleted and should no longer
 *	be used
 * @RTW_MESH_PATH_ROOT_ADD_CHK: root additional check in root mode.
 *	With this flag, It will try the last used rann_snd_addr
 * @RTW_MESH_PATH_PEER_AKA: only used toward a peer, only used in active keep
 *	alive mechanism. PREQ's da = path dst
 * @RTW_MESH_PATH_BCAST_PREQ: for re-checking next hop resolve toward root.
 *	Use it to force path_discover sending broadcast PREQ for root.
 * 
 * RTW_MESH_PATH_RESOLVED is used by the mesh path timer to
 * decide when to stop or cancel the mesh path discovery.
 */
enum rtw_mesh_path_flags {
	RTW_MESH_PATH_ACTIVE =		BIT(0),
	RTW_MESH_PATH_RESOLVING =	BIT(1),
	RTW_MESH_PATH_SN_VALID =	BIT(2),
	RTW_MESH_PATH_FIXED	=	BIT(3),
	RTW_MESH_PATH_RESOLVED =	BIT(4),
	RTW_MESH_PATH_REQ_QUEUED =	BIT(5),
	RTW_MESH_PATH_DELETED =		BIT(6),
	RTW_MESH_PATH_ROOT_ADD_CHK =	BIT(7),
	RTW_MESH_PATH_PEER_AKA =	BIT(8),
	RTW_MESH_PATH_BCAST_PREQ =	BIT(9),	
};

/**
 * struct rtw_mesh_path - mesh path structure
 *
 * @dst: mesh path destination mac address
 * @mpp: mesh proxy mac address
 * @rhash: rhashtable list pointer
 * @gate_list: list pointer for known gates list
 * @sdata: mesh subif
 * @next_hop: mesh neighbor to which frames for this destination will be
 *	forwarded
 * @timer: mesh path discovery timer
 * @frame_queue: pending queue for frames sent to this destination while the
 *	path is unresolved
 * @rcu: rcu head for freeing mesh path
 * @sn: target sequence number
 * @metric: current metric to this destination
 * @hop_count: hops to destination
 * @exp_time: in jiffies, when the path will expire or when it expired
 * @discovery_timeout: timeout (lapse in jiffies) used for the last discovery
 *	retry
 * @discovery_retries: number of discovery retries
 * @flags: mesh path flags, as specified on &enum rtw_mesh_path_flags
 * @state_lock: mesh path state lock used to protect changes to the
 * mpath itself.  No need to take this lock when adding or removing
 * an mpath to a hash bucket on a path table.
 * @rann_snd_addr: the RANN sender address
 * @rann_metric: the aggregated path metric towards the root node
 * @last_preq_to_root: Timestamp of last PREQ sent to root
 * @is_root: the destination station of this path is a root node
 * @is_gate: the destination station of this path is a mesh gate
 *
 *
 * The dst address is unique in the mesh path table. Since the mesh_path is
 * protected by RCU, deleting the next_hop STA must remove / substitute the
 * mesh_path structure and wait until that is no longer reachable before
 * destroying the STA completely.
 */
struct rtw_mesh_path {
	u8 dst[ETH_ALEN];
	u8 mpp[ETH_ALEN];	/* used for MPP or MAP */
	rtw_rhash_head rhash;
	rtw_hlist_node gate_list;
	_adapter *adapter;
	struct sta_info __rcu *next_hop;
	_timer timer;
	_queue frame_queue;
	u32 frame_queue_len;
	rtw_rcu_head rcu;
	u32 sn;
	u32 metric;
	u8 hop_count;
	systime exp_time;
	systime discovery_timeout;
	systime gate_timeout;
	u32 gate_ann_int;    /* gate announce interval */
	u8 discovery_retries;
	enum rtw_mesh_path_flags flags;
	_lock state_lock;
	u8 rann_snd_addr[ETH_ALEN];
#ifdef CONFIG_RTW_MESH_ADD_ROOT_CHK
	u8 add_chk_rann_snd_addr[ETH_ALEN];
#endif
	u32 rann_metric;
	unsigned long last_preq_to_root;
	bool is_root;
	bool is_gate;
	bool gate_asked;
};

/**
 * struct rtw_mesh_table
 *
 * @known_gates: list of known mesh gates and their mpaths by the station. The
 * gate's mpath may or may not be resolved and active.
 * @gates_lock: protects updates to known_gates
 * @rhead: the rhashtable containing struct mesh_paths, keyed by dest addr
 * @entries: number of entries in the table
 */
struct rtw_mesh_table {
	rtw_hlist_head known_gates;
	_lock gates_lock;
	rtw_rhashtable rhead;
	ATOMIC_T entries;
};

#define RTW_MESH_PATH_EXPIRE (600 * HZ)

/* Maximum number of paths per interface */
#define RTW_MESH_MAX_MPATHS		1024

/* Number of frames buffered per destination for unresolved destinations */
#define RTW_MESH_FRAME_QUEUE_LEN	10

int rtw_mesh_nexthop_lookup(_adapter *adapter,
	const u8 *mda, const u8 *msa, u8 *ra);
int rtw_mesh_nexthop_resolve(_adapter *adapter,
			 struct xmit_frame *xframe);

struct rtw_mesh_path *rtw_mesh_path_lookup(_adapter *adapter,
				   const u8 *dst);
struct rtw_mesh_path *rtw_mpp_path_lookup(_adapter *adapter,
				  const u8 *dst);
int rtw_mpp_path_add(_adapter *adapter,
		 const u8 *dst, const u8 *mpp);
void dump_mpp(void *sel, _adapter *adapter);

struct rtw_mesh_path *
rtw_mesh_path_lookup_by_idx(_adapter *adapter, int idx);
void dump_mpath(void *sel, _adapter *adapter);

struct rtw_mesh_path *
rtw_mpp_path_lookup_by_idx(_adapter *adapter, int idx);
void rtw_mesh_path_fix_nexthop(struct rtw_mesh_path *mpath, struct sta_info *next_hop);
void rtw_mesh_path_expire(_adapter *adapter);

struct rtw_mesh_path *
rtw_mesh_path_add(_adapter *adapter, const u8 *dst);

int rtw_mesh_path_add_gate(struct rtw_mesh_path *mpath);
void rtw_mesh_gate_del(struct rtw_mesh_table *tbl, struct rtw_mesh_path *mpath);
bool rtw_mesh_gate_search(struct rtw_mesh_table *tbl, const u8 *addr);
int rtw_mesh_path_send_to_gates(struct rtw_mesh_path *mpath);
int rtw_mesh_gate_num(_adapter *adapter);
bool rtw_mesh_is_primary_gate(_adapter *adapter);
void dump_known_gates(void *sel, _adapter *adapter);

void rtw_mesh_plink_broken(struct sta_info *sta);

void rtw_mesh_path_assign_nexthop(struct rtw_mesh_path *mpath, struct sta_info *sta);
void rtw_mesh_path_flush_pending(struct rtw_mesh_path *mpath);
void rtw_mesh_path_tx_pending(struct rtw_mesh_path *mpath);
int rtw_mesh_pathtbl_init(_adapter *adapter);
void rtw_mesh_pathtbl_unregister(_adapter *adapter);
int rtw_mesh_path_del(_adapter *adapter, const u8 *addr);

void rtw_mesh_path_flush_by_nexthop(struct sta_info *sta);
void rtw_mesh_path_discard_frame(_adapter *adapter,
			     struct xmit_frame *xframe);

static inline void rtw_mesh_path_activate(struct rtw_mesh_path *mpath)
{
	mpath->flags |= RTW_MESH_PATH_ACTIVE | RTW_MESH_PATH_RESOLVED;
}

void rtw_mesh_path_flush_by_iface(_adapter *adapter);

#endif /* __RTW_MESH_PATHTBL_H_ */

