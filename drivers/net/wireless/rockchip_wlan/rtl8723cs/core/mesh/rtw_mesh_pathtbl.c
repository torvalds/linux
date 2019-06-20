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
#define _RTW_MESH_PATHTBL_C_

#ifdef CONFIG_RTW_MESH
#include <drv_types.h>
#include <linux/jhash.h>

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
static void rtw_mpath_free_rcu(struct rtw_mesh_path *mpath)
{
	kfree_rcu(mpath, rcu);
	rtw_mstat_update(MSTAT_TYPE_PHY, MSTAT_FREE, sizeof(struct rtw_mesh_path));
}
#else
static void rtw_mpath_free_rcu_callback(rtw_rcu_head *head)
{
	struct rtw_mesh_path *mpath;

	mpath = container_of(head, struct rtw_mesh_path, rcu);
	rtw_mfree(mpath, sizeof(struct rtw_mesh_path));
}

static void rtw_mpath_free_rcu(struct rtw_mesh_path *mpath)
{
	call_rcu(&mpath->rcu, rtw_mpath_free_rcu_callback);
}
#endif
#endif /* PLATFORM_LINUX */

static void rtw_mesh_path_free_rcu(struct rtw_mesh_table *tbl, struct rtw_mesh_path *mpath);

static u32 rtw_mesh_table_hash(const void *addr, u32 len, u32 seed)
{
	/* Use last four bytes of hw addr as hash index */
	return jhash_1word(*(u32 *)(addr+2), seed);
}

static const rtw_rhashtable_params rtw_mesh_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct rtw_mesh_path, dst),
	.head_offset = offsetof(struct rtw_mesh_path, rhash),
	.hashfn = rtw_mesh_table_hash,
};

static inline bool rtw_mpath_expired(struct rtw_mesh_path *mpath)
{
	return (mpath->flags & RTW_MESH_PATH_ACTIVE) &&
	       rtw_time_after(rtw_get_current_time(), mpath->exp_time) &&
	       !(mpath->flags & RTW_MESH_PATH_FIXED);
}

static void rtw_mesh_path_rht_free(void *ptr, void *tblptr)
{
	struct rtw_mesh_path *mpath = ptr;
	struct rtw_mesh_table *tbl = tblptr;

	rtw_mesh_path_free_rcu(tbl, mpath);
}

static struct rtw_mesh_table *rtw_mesh_table_alloc(void)
{
	struct rtw_mesh_table *newtbl;

	newtbl = rtw_malloc(sizeof(struct rtw_mesh_table));
	if (!newtbl)
		return NULL;

	rtw_hlist_head_init(&newtbl->known_gates);
	ATOMIC_SET(&newtbl->entries,  0);
	_rtw_spinlock_init(&newtbl->gates_lock);

	return newtbl;
}

static void rtw_mesh_table_free(struct rtw_mesh_table *tbl)
{
	rtw_rhashtable_free_and_destroy(&tbl->rhead,
				    rtw_mesh_path_rht_free, tbl);
	rtw_mfree(tbl, sizeof(struct rtw_mesh_table));
}

/**
 *
 * rtw_mesh_path_assign_nexthop - update mesh path next hop
 *
 * @mpath: mesh path to update
 * @sta: next hop to assign
 *
 * Locking: mpath->state_lock must be held when calling this function
 */
void rtw_mesh_path_assign_nexthop(struct rtw_mesh_path *mpath, struct sta_info *sta)
{
	struct xmit_frame *xframe;
	_list *list, *head;

	rtw_rcu_assign_pointer(mpath->next_hop, sta);

	enter_critical_bh(&mpath->frame_queue.lock);
	head = &mpath->frame_queue.queue;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		xframe = LIST_CONTAINOR(list, struct xmit_frame, list);
		list = get_next(list);
		_rtw_memcpy(xframe->attrib.ra, sta->cmn.mac_addr, ETH_ALEN);
	}

	exit_critical_bh(&mpath->frame_queue.lock);
}

static void rtw_prepare_for_gate(struct xmit_frame *xframe, char *dst_addr,
			     struct rtw_mesh_path *gate_mpath)
{
	struct pkt_attrib *attrib = &xframe->attrib;
	char *next_hop;

	if (attrib->mesh_frame_mode == MESH_UCAST_DATA)
		attrib->mesh_frame_mode = MESH_UCAST_PX_DATA;

	/* update next hop */
	rtw_rcu_read_lock();
	next_hop = rtw_rcu_dereference(gate_mpath->next_hop)->cmn.mac_addr;
	_rtw_memcpy(attrib->ra, next_hop, ETH_ALEN);
	rtw_rcu_read_unlock();
	_rtw_memcpy(attrib->mda, dst_addr, ETH_ALEN);
}

/**
 *
 * rtw_mesh_path_move_to_queue - Move or copy frames from one mpath queue to another
 *
 * This function is used to transfer or copy frames from an unresolved mpath to
 * a gate mpath.  The function also adds the Address Extension field and
 * updates the next hop.
 *
 * If a frame already has an Address Extension field, only the next hop and
 * destination addresses are updated.
 *
 * The gate mpath must be an active mpath with a valid mpath->next_hop.
 *
 * @mpath: An active mpath the frames will be sent to (i.e. the gate)
 * @from_mpath: The failed mpath
 * @copy: When true, copy all the frames to the new mpath queue.  When false,
 * move them.
 */
static void rtw_mesh_path_move_to_queue(struct rtw_mesh_path *gate_mpath,
				    struct rtw_mesh_path *from_mpath,
				    bool copy)
{
	struct xmit_frame *fskb;
	_list *list, *head;
	_list failq;
	u32 failq_len;
	_irqL flags;

	if (rtw_warn_on(gate_mpath == from_mpath))
		return;
	if (rtw_warn_on(!gate_mpath->next_hop))
		return;

	_rtw_init_listhead(&failq);

	_enter_critical_bh(&from_mpath->frame_queue.lock, &flags);
	rtw_list_splice_init(&from_mpath->frame_queue.queue, &failq);
	failq_len = from_mpath->frame_queue_len;
	from_mpath->frame_queue_len = 0;
	_exit_critical_bh(&from_mpath->frame_queue.lock, &flags);

	head = &failq;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		if (gate_mpath->frame_queue_len >= RTW_MESH_FRAME_QUEUE_LEN) {
			RTW_MPATH_DBG(FUNC_ADPT_FMT" mpath queue for gate %pM is full!\n"
				, FUNC_ADPT_ARG(gate_mpath->adapter), gate_mpath->dst);
			break;
		}

		fskb = LIST_CONTAINOR(list, struct xmit_frame, list);
		list = get_next(list);

		rtw_list_delete(&fskb->list);
		failq_len--;
		rtw_prepare_for_gate(fskb, gate_mpath->dst, gate_mpath);
		_enter_critical_bh(&gate_mpath->frame_queue.lock, &flags);
		rtw_list_insert_tail(&fskb->list, get_list_head(&gate_mpath->frame_queue));
		gate_mpath->frame_queue_len++;
		_exit_critical_bh(&gate_mpath->frame_queue.lock, &flags);

		#if 0 /* TODO: copy */
		skb = rtw_skb_copy(fskb);
		if (rtw_warn_on(!skb))
			break;

		rtw_prepare_for_gate(skb, gate_mpath->dst, gate_mpath);
		skb_queue_tail(&gate_mpath->frame_queue, skb);

		if (copy)
			continue;

		__skb_unlink(fskb, &failq);
		rtw_skb_free(fskb);
		#endif
	}

	RTW_MPATH_DBG(FUNC_ADPT_FMT" mpath queue for gate %pM has %d frames\n"
		, FUNC_ADPT_ARG(gate_mpath->adapter), gate_mpath->dst, gate_mpath->frame_queue_len);

	if (!copy)
		return;

	_enter_critical_bh(&from_mpath->frame_queue.lock, &flags);
	rtw_list_splice(&failq, &from_mpath->frame_queue.queue);
	from_mpath->frame_queue_len += failq_len;
	_exit_critical_bh(&from_mpath->frame_queue.lock, &flags);
}


static struct rtw_mesh_path *rtw_mpath_lookup(struct rtw_mesh_table *tbl, const u8 *dst)
{
	struct rtw_mesh_path *mpath;

	if (!tbl)
		return NULL;

	mpath = rtw_rhashtable_lookup_fast(&tbl->rhead, dst, rtw_mesh_rht_params);

	if (mpath && rtw_mpath_expired(mpath)) {
		enter_critical_bh(&mpath->state_lock);
		mpath->flags &= ~RTW_MESH_PATH_ACTIVE;
		exit_critical_bh(&mpath->state_lock);
	}
	return mpath;
}

/**
 * rtw_mesh_path_lookup - look up a path in the mesh path table
 * @sdata: local subif
 * @dst: hardware address (ETH_ALEN length) of destination
 *
 * Returns: pointer to the mesh path structure, or NULL if not found
 *
 * Locking: must be called within a read rcu section.
 */
struct rtw_mesh_path *
rtw_mesh_path_lookup(_adapter *adapter, const u8 *dst)
{
	return rtw_mpath_lookup(adapter->mesh_info.mesh_paths, dst);
}

struct rtw_mesh_path *
rtw_mpp_path_lookup(_adapter *adapter, const u8 *dst)
{
	return rtw_mpath_lookup(adapter->mesh_info.mpp_paths, dst);
}

static struct rtw_mesh_path *
__rtw_mesh_path_lookup_by_idx(struct rtw_mesh_table *tbl, int idx)
{
	int i = 0, ret;
	struct rtw_mesh_path *mpath = NULL;
	rtw_rhashtable_iter iter;

	if (!tbl)
		return NULL;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return NULL;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto err;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if (i++ == idx)
			break;
	}
err:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);

	if (IS_ERR(mpath) || !mpath)
		return NULL;

	if (rtw_mpath_expired(mpath)) {
		enter_critical_bh(&mpath->state_lock);
		mpath->flags &= ~RTW_MESH_PATH_ACTIVE;
		exit_critical_bh(&mpath->state_lock);
	}
	return mpath;
}

/**
 * rtw_mesh_path_lookup_by_idx - look up a path in the mesh path table by its index
 * @idx: index
 * @sdata: local subif, or NULL for all entries
 *
 * Returns: pointer to the mesh path structure, or NULL if not found.
 *
 * Locking: must be called within a read rcu section.
 */
struct rtw_mesh_path *
rtw_mesh_path_lookup_by_idx(_adapter *adapter, int idx)
{
	return __rtw_mesh_path_lookup_by_idx(adapter->mesh_info.mesh_paths, idx);
}

void dump_mpath(void *sel, _adapter *adapter)
{
	struct rtw_mesh_path *mpath;
	int idx = 0;
	char dst[ETH_ALEN];
	char next_hop[ETH_ALEN];
	u32 sn, metric, qlen;
	u32 exp_ms = 0, dto_ms;
	u8 drty;
	enum rtw_mesh_path_flags flags;

	RTW_PRINT_SEL(sel, "%-17s %-17s %-10s %-10s %-4s %-6s %-6s %-4s flags\n"
		, "dst", "next_hop", "sn", "metric", "qlen", "exp_ms", "dto_ms", "drty"
	);

	do {
		rtw_rcu_read_lock();

		mpath = rtw_mesh_path_lookup_by_idx(adapter, idx);
		if (mpath) {
			_rtw_memcpy(dst, mpath->dst, ETH_ALEN);
			_rtw_memcpy(next_hop, mpath->next_hop->cmn.mac_addr, ETH_ALEN);
			sn = mpath->sn;
			metric = mpath->metric;
			qlen = mpath->frame_queue_len;
			if (rtw_time_after(mpath->exp_time, rtw_get_current_time()))
				exp_ms = rtw_get_remaining_time_ms(mpath->exp_time);
			dto_ms = rtw_systime_to_ms(mpath->discovery_timeout);
			drty = mpath->discovery_retries;
			flags = mpath->flags;
		}

		rtw_rcu_read_unlock();

		if (mpath) {
			RTW_PRINT_SEL(sel, MAC_FMT" "MAC_FMT" %10u %10u %4u %6u %6u %4u%s%s%s%s%s\n"
				, MAC_ARG(dst), MAC_ARG(next_hop), sn, metric, qlen
				, exp_ms < 999999 ? exp_ms : 999999
				, dto_ms < 999999 ? dto_ms : 999999
				, drty
				, (flags & RTW_MESH_PATH_ACTIVE) ? " ACT" : ""
				, (flags & RTW_MESH_PATH_RESOLVING) ? " RSVING" : ""
				, (flags & RTW_MESH_PATH_SN_VALID) ? " SN_VALID" : ""
				, (flags & RTW_MESH_PATH_FIXED) ?  " FIXED" : ""
				, (flags & RTW_MESH_PATH_RESOLVED) ? " RSVED" : ""
			);
		}

		idx++;
	} while (mpath);
}

/**
 * rtw_mpp_path_lookup_by_idx - look up a path in the proxy path table by its index
 * @idx: index
 * @sdata: local subif, or NULL for all entries
 *
 * Returns: pointer to the proxy path structure, or NULL if not found.
 *
 * Locking: must be called within a read rcu section.
 */
struct rtw_mesh_path *
rtw_mpp_path_lookup_by_idx(_adapter *adapter, int idx)
{
	return __rtw_mesh_path_lookup_by_idx(adapter->mesh_info.mpp_paths, idx);
}

/**
 * rtw_mesh_path_add_gate - add the given mpath to a mesh gate to our path table
 * @mpath: gate path to add to table
 */
int rtw_mesh_path_add_gate(struct rtw_mesh_path *mpath)
{
	struct rtw_mesh_cfg *mcfg;
	struct rtw_mesh_info *minfo;
	struct rtw_mesh_table *tbl;
	int err, ori_num_gates;

	rtw_rcu_read_lock();
	tbl = mpath->adapter->mesh_info.mesh_paths;
	if (!tbl) {
		err = -ENOENT;
		goto err_rcu;
	}

	enter_critical_bh(&mpath->state_lock);
	mcfg = &mpath->adapter->mesh_cfg;
	mpath->gate_timeout = rtw_get_current_time() +
			      rtw_ms_to_systime(mcfg->path_gate_timeout_factor *
					        mpath->gate_ann_int);
	if (mpath->is_gate) {
		err = -EEXIST;
		exit_critical_bh(&mpath->state_lock);
		goto err_rcu;
	}

	minfo = &mpath->adapter->mesh_info;
	mpath->is_gate = true;
	_rtw_spinlock(&tbl->gates_lock);
	ori_num_gates = minfo->num_gates;
	minfo->num_gates++;
	rtw_hlist_add_head_rcu(&mpath->gate_list, &tbl->known_gates);

	if (ori_num_gates == 0
		|| rtw_macaddr_is_larger(mpath->dst, minfo->max_addr_gate->dst)
	) {
		minfo->max_addr_gate = mpath;
		minfo->max_addr_gate_is_larger_than_self =
			rtw_macaddr_is_larger(mpath->dst, adapter_mac_addr(mpath->adapter));
	}

	_rtw_spinunlock(&tbl->gates_lock);

	exit_critical_bh(&mpath->state_lock);

	if (ori_num_gates == 0) {
		update_beacon(mpath->adapter, WLAN_EID_MESH_CONFIG, NULL, _TRUE);
		#if CONFIG_RTW_MESH_CTO_MGATE_CARRIER
		if (!rtw_mesh_cto_mgate_required(mpath->adapter))
			rtw_netif_carrier_on(mpath->adapter->pnetdev);
		#endif
	}

	RTW_MPATH_DBG(
		  FUNC_ADPT_FMT" Mesh path: Recorded new gate: %pM. %d known gates\n",
		  FUNC_ADPT_ARG(mpath->adapter),
		  mpath->dst, mpath->adapter->mesh_info.num_gates);
	err = 0;
err_rcu:
	rtw_rcu_read_unlock();
	return err;
}

/**
 * rtw_mesh_gate_del - remove a mesh gate from the list of known gates
 * @tbl: table which holds our list of known gates
 * @mpath: gate mpath
 */
void rtw_mesh_gate_del(struct rtw_mesh_table *tbl, struct rtw_mesh_path *mpath)
{
	struct rtw_mesh_cfg *mcfg;
	struct rtw_mesh_info *minfo;
	int ori_num_gates;

	rtw_lockdep_assert_held(&mpath->state_lock);
	if (!mpath->is_gate)
		return;

	mcfg = &mpath->adapter->mesh_cfg;
	minfo = &mpath->adapter->mesh_info;

	mpath->is_gate = false;
	enter_critical_bh(&tbl->gates_lock);
	rtw_hlist_del_rcu(&mpath->gate_list);
	ori_num_gates = minfo->num_gates;
	minfo->num_gates--;

	if (ori_num_gates == 1) {
		minfo->max_addr_gate = NULL;
		minfo->max_addr_gate_is_larger_than_self = 0;
	} else if (minfo->max_addr_gate == mpath) {
		struct rtw_mesh_path *gate, *max_addr_gate = NULL;
		rtw_hlist_node *node;

		rtw_hlist_for_each_entry_rcu(gate, node, &tbl->known_gates, gate_list) {
			if (!max_addr_gate || rtw_macaddr_is_larger(gate->dst, max_addr_gate->dst))
				max_addr_gate = gate;
		}
		minfo->max_addr_gate = max_addr_gate;
		minfo->max_addr_gate_is_larger_than_self =
			rtw_macaddr_is_larger(max_addr_gate->dst, adapter_mac_addr(mpath->adapter));
	}

	exit_critical_bh(&tbl->gates_lock);

	if (ori_num_gates == 1) {
		update_beacon(mpath->adapter, WLAN_EID_MESH_CONFIG, NULL, _TRUE);
		#if CONFIG_RTW_MESH_CTO_MGATE_CARRIER
		if (rtw_mesh_cto_mgate_required(mpath->adapter))
			rtw_netif_carrier_off(mpath->adapter->pnetdev);
		#endif
	}

	RTW_MPATH_DBG(
		  FUNC_ADPT_FMT" Mesh path: Deleted gate: %pM. %d known gates\n",
		  FUNC_ADPT_ARG(mpath->adapter),
		  mpath->dst, mpath->adapter->mesh_info.num_gates);
}

/**
 * rtw_mesh_gate_search - search a mesh gate from the list of known gates
 * @tbl: table which holds our list of known gates
 * @addr: address of gate
 */
bool rtw_mesh_gate_search(struct rtw_mesh_table *tbl, const u8 *addr)
{
	struct rtw_mesh_path *gate;
	rtw_hlist_node *node;
	bool exist = 0;

	rtw_rcu_read_lock();
	rtw_hlist_for_each_entry_rcu(gate, node, &tbl->known_gates, gate_list) {
		if (_rtw_memcmp(gate->dst, addr, ETH_ALEN) == _TRUE) {
			exist = 1;
			break;
		}
	}

	rtw_rcu_read_unlock();

	return exist;
}

/**
 * rtw_mesh_gate_num - number of gates known to this interface
 * @sdata: subif data
 */
int rtw_mesh_gate_num(_adapter *adapter)
{
	return adapter->mesh_info.num_gates;
}

bool rtw_mesh_is_primary_gate(_adapter *adapter)
{
	struct rtw_mesh_cfg *mcfg = &adapter->mesh_cfg;
	struct rtw_mesh_info *minfo = &adapter->mesh_info;

	return mcfg->dot11MeshGateAnnouncementProtocol
		&& !minfo->max_addr_gate_is_larger_than_self;
}

void dump_known_gates(void *sel, _adapter *adapter)
{
	struct rtw_mesh_info *minfo = &adapter->mesh_info;
	struct rtw_mesh_table *tbl;
	struct rtw_mesh_path *gate;
	rtw_hlist_node *node;

	if (!rtw_mesh_gate_num(adapter))
		goto exit;

	rtw_rcu_read_lock();

	tbl = minfo->mesh_paths;
	if (!tbl)
		goto unlock;

	RTW_PRINT_SEL(sel, "num:%d\n", rtw_mesh_gate_num(adapter));

	rtw_hlist_for_each_entry_rcu(gate, node, &tbl->known_gates, gate_list) {
		RTW_PRINT_SEL(sel, "%c"MAC_FMT"\n"
			, gate == minfo->max_addr_gate ? '*' : ' '
			, MAC_ARG(gate->dst));
	}

unlock:
	rtw_rcu_read_unlock();
exit:
	return;
}

static
struct rtw_mesh_path *rtw_mesh_path_new(_adapter *adapter,
				const u8 *dst)
{
	struct rtw_mesh_path *new_mpath;

	new_mpath = rtw_zmalloc(sizeof(struct rtw_mesh_path));
	if (!new_mpath)
		return NULL;

	_rtw_memcpy(new_mpath->dst, dst, ETH_ALEN);
	_rtw_memset(new_mpath->rann_snd_addr, 0xFF, ETH_ALEN);
	new_mpath->is_root = false;
	new_mpath->adapter = adapter;
	new_mpath->flags = 0;
	new_mpath->gate_asked = false;
	_rtw_init_queue(&new_mpath->frame_queue);
	new_mpath->frame_queue_len = 0;
	new_mpath->exp_time = rtw_get_current_time();
	_rtw_spinlock_init(&new_mpath->state_lock);
	rtw_init_timer(&new_mpath->timer, adapter, rtw_mesh_path_timer, new_mpath);

	return new_mpath;
}

/**
 * rtw_mesh_path_add - allocate and add a new path to the mesh path table
 * @dst: destination address of the path (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 on success
 *
 * State: the initial state of the new path is set to 0
 */
struct rtw_mesh_path *rtw_mesh_path_add(_adapter *adapter,
				const u8 *dst)
{
	struct rtw_mesh_table *tbl = adapter->mesh_info.mesh_paths;
	struct rtw_mesh_path *mpath, *new_mpath;
	int ret;

	if (!tbl)
		return ERR_PTR(-ENOTSUPP);

	if (_rtw_memcmp(dst, adapter_mac_addr(adapter), ETH_ALEN) == _TRUE)
		/* never add ourselves as neighbours */
		return ERR_PTR(-ENOTSUPP);

	if (is_multicast_mac_addr(dst))
		return ERR_PTR(-ENOTSUPP);

	if (ATOMIC_INC_UNLESS(&adapter->mesh_info.mpaths, RTW_MESH_MAX_MPATHS) == 0)
		return ERR_PTR(-ENOSPC);

	new_mpath = rtw_mesh_path_new(adapter, dst);
	if (!new_mpath)
		return ERR_PTR(-ENOMEM);

	do {
		ret = rtw_rhashtable_lookup_insert_fast(&tbl->rhead,
						    &new_mpath->rhash,
						    rtw_mesh_rht_params);

		if (ret == -EEXIST)
			mpath = rtw_rhashtable_lookup_fast(&tbl->rhead,
						       dst,
						       rtw_mesh_rht_params);

	} while (unlikely(ret == -EEXIST && !mpath));

	if (ret && ret != -EEXIST)
		return ERR_PTR(ret);

	/* At this point either new_mpath was added, or we found a
	 * matching entry already in the table; in the latter case
	 * free the unnecessary new entry.
	 */
	if (ret == -EEXIST) {
		rtw_mfree(new_mpath, sizeof(struct rtw_mesh_path));
		new_mpath = mpath;
	}
	adapter->mesh_info.mesh_paths_generation++;
	return new_mpath;
}

int rtw_mpp_path_add(_adapter *adapter,
		 const u8 *dst, const u8 *mpp)
{
	struct rtw_mesh_table *tbl = adapter->mesh_info.mpp_paths;
	struct rtw_mesh_path *new_mpath;
	int ret;

	if (!tbl)
		return -ENOTSUPP;

	if (_rtw_memcmp(dst, adapter_mac_addr(adapter), ETH_ALEN) == _TRUE)
		/* never add ourselves as neighbours */
		return -ENOTSUPP;

	if (is_multicast_mac_addr(dst))
		return -ENOTSUPP;

	new_mpath = rtw_mesh_path_new(adapter, dst);

	if (!new_mpath)
		return -ENOMEM;

	_rtw_memcpy(new_mpath->mpp, mpp, ETH_ALEN);
	ret = rtw_rhashtable_lookup_insert_fast(&tbl->rhead,
					    &new_mpath->rhash,
					    rtw_mesh_rht_params);

	adapter->mesh_info.mpp_paths_generation++;
	return ret;
}

void dump_mpp(void *sel, _adapter *adapter)
{
	struct rtw_mesh_path *mpath;
	int idx = 0;
	char dst[ETH_ALEN];
	char mpp[ETH_ALEN];

	RTW_PRINT_SEL(sel, "%-17s %-17s\n", "dst", "mpp");

	do {
		rtw_rcu_read_lock();

		mpath = rtw_mpp_path_lookup_by_idx(adapter, idx);
		if (mpath) {
			_rtw_memcpy(dst, mpath->dst, ETH_ALEN);
			_rtw_memcpy(mpp, mpath->mpp, ETH_ALEN);
		}

		rtw_rcu_read_unlock();

		if (mpath) {
			RTW_PRINT_SEL(sel, MAC_FMT" "MAC_FMT"\n"
				, MAC_ARG(dst), MAC_ARG(mpp));
		}

		idx++;
	} while (mpath);
}

/**
 * rtw_mesh_plink_broken - deactivates paths and sends perr when a link breaks
 *
 * @sta: broken peer link
 *
 * This function must be called from the rate control algorithm if enough
 * delivery errors suggest that a peer link is no longer usable.
 */
void rtw_mesh_plink_broken(struct sta_info *sta)
{
	_adapter *adapter = sta->padapter;
	struct rtw_mesh_table *tbl = adapter->mesh_info.mesh_paths;
	static const u8 bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct rtw_mesh_path *mpath;
	rtw_rhashtable_iter iter;
	int ret;

	if (!tbl)
		return;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if (rtw_rcu_access_pointer(mpath->next_hop) == sta &&
		    mpath->flags & RTW_MESH_PATH_ACTIVE &&
		    !(mpath->flags & RTW_MESH_PATH_FIXED)) {
			enter_critical_bh(&mpath->state_lock);
			mpath->flags &= ~RTW_MESH_PATH_ACTIVE;
			++mpath->sn;
			exit_critical_bh(&mpath->state_lock);
			rtw_mesh_path_error_tx(adapter,
				adapter->mesh_cfg.element_ttl,
				mpath->dst, mpath->sn,
				WLAN_REASON_MESH_PATH_DEST_UNREACHABLE, bcast);
		}
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

static void rtw_mesh_path_free_rcu(struct rtw_mesh_table *tbl,
			       struct rtw_mesh_path *mpath)
{
	_adapter *adapter = mpath->adapter;

	enter_critical_bh(&mpath->state_lock);
	mpath->flags |= RTW_MESH_PATH_RESOLVING | RTW_MESH_PATH_DELETED;
	rtw_mesh_gate_del(tbl, mpath);
	exit_critical_bh(&mpath->state_lock);
	_cancel_timer_ex(&mpath->timer);
	ATOMIC_DEC(&adapter->mesh_info.mpaths);
	ATOMIC_DEC(&tbl->entries);
	_rtw_spinlock_free(&mpath->state_lock);

	rtw_mesh_path_flush_pending(mpath);

	rtw_mpath_free_rcu(mpath);
}

static void __rtw_mesh_path_del(struct rtw_mesh_table *tbl, struct rtw_mesh_path *mpath)
{
	rtw_rhashtable_remove_fast(&tbl->rhead, &mpath->rhash, rtw_mesh_rht_params);
	rtw_mesh_path_free_rcu(tbl, mpath);
}

/**
 * rtw_mesh_path_flush_by_nexthop - Deletes mesh paths if their next hop matches
 *
 * @sta: mesh peer to match
 *
 * RCU notes: this function is called when a mesh plink transitions from
 * PLINK_ESTAB to any other state, since PLINK_ESTAB state is the only one that
 * allows path creation. This will happen before the sta can be freed (because
 * sta_info_destroy() calls this) so any reader in a rcu read block will be
 * protected against the plink disappearing.
 */
void rtw_mesh_path_flush_by_nexthop(struct sta_info *sta)
{
	_adapter *adapter = sta->padapter;
	struct rtw_mesh_table *tbl = adapter->mesh_info.mesh_paths;
	struct rtw_mesh_path *mpath;
	rtw_rhashtable_iter iter;
	int ret;

	if (!tbl)
		return;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;

		if (rtw_rcu_access_pointer(mpath->next_hop) == sta)
			__rtw_mesh_path_del(tbl, mpath);
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

static void rtw_mpp_flush_by_proxy(_adapter *adapter,
			       const u8 *proxy)
{
	struct rtw_mesh_table *tbl = adapter->mesh_info.mpp_paths;
	struct rtw_mesh_path *mpath;
	rtw_rhashtable_iter iter;
	int ret;

	if (!tbl)
		return;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;

		if (_rtw_memcmp(mpath->mpp, proxy, ETH_ALEN) == _TRUE)
			__rtw_mesh_path_del(tbl, mpath);
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

static void rtw_table_flush_by_iface(struct rtw_mesh_table *tbl)
{
	struct rtw_mesh_path *mpath;
	rtw_rhashtable_iter iter;
	int ret;

	if (!tbl)
		return;
	
	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		__rtw_mesh_path_del(tbl, mpath);
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

/**
 * rtw_mesh_path_flush_by_iface - Deletes all mesh paths associated with a given iface
 *
 * This function deletes both mesh paths as well as mesh portal paths.
 *
 * @sdata: interface data to match
 *
 */
void rtw_mesh_path_flush_by_iface(_adapter *adapter)
{
	rtw_table_flush_by_iface(adapter->mesh_info.mesh_paths);
	rtw_table_flush_by_iface(adapter->mesh_info.mpp_paths);
}

/**
 * rtw_table_path_del - delete a path from the mesh or mpp table
 *
 * @tbl: mesh or mpp path table
 * @sdata: local subif
 * @addr: dst address (ETH_ALEN length)
 *
 * Returns: 0 if successful
 */
static int rtw_table_path_del(struct rtw_mesh_table *tbl,
			  const u8 *addr)
{
	struct rtw_mesh_path *mpath;

	if (!tbl)
		return -ENXIO;

	rtw_rcu_read_lock();
	mpath = rtw_rhashtable_lookup_fast(&tbl->rhead, addr, rtw_mesh_rht_params);
	if (!mpath) {
		rtw_rcu_read_unlock();
		return -ENXIO;
	}

	__rtw_mesh_path_del(tbl, mpath);
	rtw_rcu_read_unlock();
	return 0;
}


/**
 * rtw_mesh_path_del - delete a mesh path from the table
 *
 * @addr: dst address (ETH_ALEN length)
 * @sdata: local subif
 *
 * Returns: 0 if successful
 */
int rtw_mesh_path_del(_adapter *adapter, const u8 *addr)
{
	int err;

	/* flush relevant mpp entries first */
	rtw_mpp_flush_by_proxy(adapter, addr);

	err = rtw_table_path_del(adapter->mesh_info.mesh_paths, addr);
	adapter->mesh_info.mesh_paths_generation++;
	return err;
}

/**
 * rtw_mesh_path_tx_pending - sends pending frames in a mesh path queue
 *
 * @mpath: mesh path to activate
 *
 * Locking: the state_lock of the mpath structure must NOT be held when calling
 * this function.
 */
void rtw_mesh_path_tx_pending(struct rtw_mesh_path *mpath)
{
	if (mpath->flags & RTW_MESH_PATH_ACTIVE) {
		struct rtw_mesh_info *minfo = &mpath->adapter->mesh_info;
		_list q;
		u32 q_len = 0;

		_rtw_init_listhead(&q);

		/* move to local queue */
		enter_critical_bh(&mpath->frame_queue.lock);
		if (mpath->frame_queue_len) {
			rtw_list_splice_init(&mpath->frame_queue.queue, &q);
			q_len = mpath->frame_queue_len;
			mpath->frame_queue_len = 0;
		}
		exit_critical_bh(&mpath->frame_queue.lock);

		if (q_len) {
			/* move to mpath_tx_queue */
			enter_critical_bh(&minfo->mpath_tx_queue.lock);
			rtw_list_splice_tail(&q, &minfo->mpath_tx_queue.queue);
			minfo->mpath_tx_queue_len += q_len;
			exit_critical_bh(&minfo->mpath_tx_queue.lock);

			/* schedule mpath_tx_tasklet */
			tasklet_hi_schedule(&minfo->mpath_tx_tasklet);
		}
	}
}

/**
 * rtw_mesh_path_send_to_gates - sends pending frames to all known mesh gates
 *
 * @mpath: mesh path whose queue will be emptied
 *
 * If there is only one gate, the frames are transferred from the failed mpath
 * queue to that gate's queue.  If there are more than one gates, the frames
 * are copied from each gate to the next.  After frames are copied, the
 * mpath queues are emptied onto the transmission queue.
 */
int rtw_mesh_path_send_to_gates(struct rtw_mesh_path *mpath)
{
	_adapter *adapter = mpath->adapter;
	struct rtw_mesh_table *tbl;
	struct rtw_mesh_path *from_mpath = mpath;
	struct rtw_mesh_path *gate;
	bool copy = false;
	rtw_hlist_node *node;

	tbl = adapter->mesh_info.mesh_paths;
	if (!tbl)
		return 0;

	rtw_rcu_read_lock();
	rtw_hlist_for_each_entry_rcu(gate, node, &tbl->known_gates, gate_list) {
		if (gate->flags & RTW_MESH_PATH_ACTIVE) {
			RTW_MPATH_DBG(FUNC_ADPT_FMT" Forwarding to %pM\n",
				FUNC_ADPT_ARG(adapter), gate->dst);
			rtw_mesh_path_move_to_queue(gate, from_mpath, copy);
			from_mpath = gate;
			copy = true;
		} else {
			RTW_MPATH_DBG(
				  FUNC_ADPT_FMT" Not forwarding to %pM (flags %#x)\n",
				  FUNC_ADPT_ARG(adapter), gate->dst, gate->flags);
		}
	}

	rtw_hlist_for_each_entry_rcu(gate, node, &tbl->known_gates, gate_list) {
		RTW_MPATH_DBG(FUNC_ADPT_FMT" Sending to %pM\n",
			FUNC_ADPT_ARG(adapter), gate->dst);
		rtw_mesh_path_tx_pending(gate);
	}
	rtw_rcu_read_unlock();

	return (from_mpath == mpath) ? -EHOSTUNREACH : 0;
}

/**
 * rtw_mesh_path_discard_frame - discard a frame whose path could not be resolved
 *
 * @skb: frame to discard
 * @sdata: network subif the frame was to be sent through
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void rtw_mesh_path_discard_frame(_adapter *adapter,
			     struct xmit_frame *xframe)
{
	rtw_free_xmitframe(&adapter->xmitpriv, xframe);
	adapter->mesh_info.mshstats.dropped_frames_no_route++;
}

/**
 * rtw_mesh_path_flush_pending - free the pending queue of a mesh path
 *
 * @mpath: mesh path whose queue has to be freed
 *
 * Locking: the function must me called within a rcu_read_lock region
 */
void rtw_mesh_path_flush_pending(struct rtw_mesh_path *mpath)
{
	struct xmit_frame *xframe;
	_list *list, *head;
	_list tmp;

	_rtw_init_listhead(&tmp);

	enter_critical_bh(&mpath->frame_queue.lock);
	rtw_list_splice_init(&mpath->frame_queue.queue, &tmp);
	mpath->frame_queue_len = 0;
	exit_critical_bh(&mpath->frame_queue.lock);

	head = &tmp;
	list = get_next(head);
	while (rtw_end_of_queue_search(head, list) == _FALSE) {
		xframe = LIST_CONTAINOR(list, struct xmit_frame, list);
		list = get_next(list);
		rtw_list_delete(&xframe->list);
		rtw_mesh_path_discard_frame(mpath->adapter, xframe);
	}
}

/**
 * rtw_mesh_path_fix_nexthop - force a specific next hop for a mesh path
 *
 * @mpath: the mesh path to modify
 * @next_hop: the next hop to force
 *
 * Locking: this function must be called holding mpath->state_lock
 */
void rtw_mesh_path_fix_nexthop(struct rtw_mesh_path *mpath, struct sta_info *next_hop)
{
	enter_critical_bh(&mpath->state_lock);
	rtw_mesh_path_assign_nexthop(mpath, next_hop);
	mpath->sn = 0xffff;
	mpath->metric = 0;
	mpath->hop_count = 0;
	mpath->exp_time = 0;
	mpath->flags = RTW_MESH_PATH_FIXED | RTW_MESH_PATH_SN_VALID;
	rtw_mesh_path_activate(mpath);
	exit_critical_bh(&mpath->state_lock);
	rtw_ewma_err_rate_init(&next_hop->metrics.err_rate);
	/* init it at a low value - 0 start is tricky */
	rtw_ewma_err_rate_add(&next_hop->metrics.err_rate, 1);
	rtw_mesh_path_tx_pending(mpath);
}

int rtw_mesh_pathtbl_init(_adapter *adapter)
{
	struct rtw_mesh_table *tbl_path, *tbl_mpp;
	int ret;

	tbl_path = rtw_mesh_table_alloc();
	if (!tbl_path)
		return -ENOMEM;

	tbl_mpp = rtw_mesh_table_alloc();
	if (!tbl_mpp) {
		ret = -ENOMEM;
		goto free_path;
	}

	rtw_rhashtable_init(&tbl_path->rhead, &rtw_mesh_rht_params);
	rtw_rhashtable_init(&tbl_mpp->rhead, &rtw_mesh_rht_params);

	adapter->mesh_info.mesh_paths = tbl_path;
	adapter->mesh_info.mpp_paths = tbl_mpp;

	return 0;

free_path:
	rtw_mesh_table_free(tbl_path);
	return ret;
}

static
void rtw_mesh_path_tbl_expire(_adapter *adapter,
			  struct rtw_mesh_table *tbl)
{
	struct rtw_mesh_path *mpath;
	rtw_rhashtable_iter iter;
	int ret;

	if (!tbl)
		return;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto out;

	while ((mpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(mpath) && PTR_ERR(mpath) == -EAGAIN)
			continue;
		if (IS_ERR(mpath))
			break;
		if ((!(mpath->flags & RTW_MESH_PATH_RESOLVING)) &&
		    (!(mpath->flags & RTW_MESH_PATH_FIXED)) &&
		     rtw_time_after(rtw_get_current_time(), mpath->exp_time + RTW_MESH_PATH_EXPIRE))
			__rtw_mesh_path_del(tbl, mpath);

		if (mpath->is_gate &&  /* need not to deal with non-gate case */
		    rtw_time_after(rtw_get_current_time(), mpath->gate_timeout)) {
			RTW_MPATH_DBG(FUNC_ADPT_FMT"mpath [%pM] expired systime is %lu systime is %lu\n",
				      FUNC_ADPT_ARG(adapter), mpath->dst,
				      mpath->gate_timeout, rtw_get_current_time());
			enter_critical_bh(&mpath->state_lock);
			if (mpath->gate_asked) { /* asked gate before */
				rtw_mesh_gate_del(tbl, mpath);
				exit_critical_bh(&mpath->state_lock);
			} else {
				mpath->gate_asked = true;
				mpath->gate_timeout = rtw_get_current_time() + rtw_ms_to_systime(mpath->gate_ann_int);
				exit_critical_bh(&mpath->state_lock);
				rtw_mesh_queue_preq(mpath, RTW_PREQ_Q_F_START | RTW_PREQ_Q_F_REFRESH);
				RTW_MPATH_DBG(FUNC_ADPT_FMT"mpath [%pM] ask mesh gate existence (is_root=%d)\n",
				      FUNC_ADPT_ARG(adapter), mpath->dst, mpath->is_root);
			}
		}
	}

out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

void rtw_mesh_path_expire(_adapter *adapter)
{
	rtw_mesh_path_tbl_expire(adapter, adapter->mesh_info.mesh_paths);
	rtw_mesh_path_tbl_expire(adapter, adapter->mesh_info.mpp_paths);
}

void rtw_mesh_pathtbl_unregister(_adapter *adapter)
{
	if (adapter->mesh_info.mesh_paths) {
		rtw_mesh_table_free(adapter->mesh_info.mesh_paths);
		adapter->mesh_info.mesh_paths = NULL;
	}

	if (adapter->mesh_info.mpp_paths) {
		rtw_mesh_table_free(adapter->mesh_info.mpp_paths);
		adapter->mesh_info.mpp_paths = NULL;
	}
}
#endif /* CONFIG_RTW_MESH */

