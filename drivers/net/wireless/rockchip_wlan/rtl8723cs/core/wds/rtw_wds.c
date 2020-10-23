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
#define _RTW_WDS_C_

#include <drv_types.h>

#if defined(CONFIG_RTW_WDS)
#include <linux/jhash.h>

#if defined(CONFIG_AP_MODE)

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
static void rtw_wpath_free_rcu(struct rtw_wds_path *wpath)
{
	kfree_rcu(wpath, rcu);
	rtw_mstat_update(MSTAT_TYPE_PHY, MSTAT_FREE, sizeof(struct rtw_wds_path));
}
#else
static void rtw_wpath_free_rcu_callback(rtw_rcu_head *head)
{
	struct rtw_wds_path *wpath;

	wpath = container_of(head, struct rtw_wds_path, rcu);
	rtw_mfree(wpath, sizeof(struct rtw_wds_path));
}

static void rtw_wpath_free_rcu(struct rtw_wds_path *wpath)
{
	call_rcu(&wpath->rcu, rtw_wpath_free_rcu_callback);
}
#endif
#endif /* PLATFORM_LINUX */

static void rtw_wds_path_free_rcu(struct rtw_wds_table *tbl, struct rtw_wds_path *wpath);

static u32 rtw_wds_table_hash(const void *addr, u32 len, u32 seed)
{
	/* Use last four bytes of hw addr as hash index */
	return jhash_1word(*(u32 *)(addr+2), seed);
}

static const rtw_rhashtable_params rtw_wds_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct rtw_wds_path, dst),
	.head_offset = offsetof(struct rtw_wds_path, rhash),
	.hashfn = rtw_wds_table_hash,
};

static void rtw_wds_path_rht_free(void *ptr, void *tblptr)
{
	struct rtw_wds_path *wpath = ptr;
	struct rtw_wds_table *tbl = tblptr;

	rtw_wds_path_free_rcu(tbl, wpath);
}

static struct rtw_wds_table *rtw_wds_table_alloc(void)
{
	struct rtw_wds_table *newtbl;

	newtbl = rtw_malloc(sizeof(struct rtw_wds_table));
	if (!newtbl)
		return NULL;

	return newtbl;
}

static void rtw_wds_table_free(struct rtw_wds_table *tbl)
{
	rtw_rhashtable_free_and_destroy(&tbl->rhead,
				    rtw_wds_path_rht_free, tbl);
	rtw_mfree(tbl, sizeof(struct rtw_wds_table));
}

void rtw_wds_path_assign_nexthop(struct rtw_wds_path *wpath, struct sta_info *sta)
{
	rtw_rcu_assign_pointer(wpath->next_hop, sta);
}

static struct rtw_wds_path *rtw_wpath_lookup(struct rtw_wds_table *tbl, const u8 *dst)
{
	struct rtw_wds_path *wpath;

	if (!tbl)
		return NULL;

	wpath = rtw_rhashtable_lookup_fast(&tbl->rhead, dst, rtw_wds_rht_params);

	return wpath;
}

struct rtw_wds_path *rtw_wds_path_lookup(_adapter *adapter, const u8 *dst)
{
	return rtw_wpath_lookup(adapter->wds_paths, dst);
}

static struct rtw_wds_path *
__rtw_wds_path_lookup_by_idx(struct rtw_wds_table *tbl, int idx)
{
	int i = 0, ret;
	struct rtw_wds_path *wpath = NULL;
	rtw_rhashtable_iter iter;

	if (!tbl)
		return NULL;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return NULL;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto err;

	while ((wpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wpath) && PTR_ERR(wpath) == -EAGAIN)
			continue;
		if (IS_ERR(wpath))
			break;
		if (i++ == idx)
			break;
	}
err:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);

	if (IS_ERR(wpath) || !wpath)
		return NULL;

	return wpath;
}

/**
 * Locking: must be called within a read rcu section.
 */
struct rtw_wds_path *
rtw_wds_path_lookup_by_idx(_adapter *adapter, int idx)
{
	return __rtw_wds_path_lookup_by_idx(adapter->wds_paths, idx);
}

void dump_wpath(void *sel, _adapter *adapter)
{
	struct rtw_wds_path *wpath;
	int idx = 0;
	char dst[ETH_ALEN];
	char next_hop[ETH_ALEN];
	u32 age_ms;

	RTW_PRINT_SEL(sel, "num:%d\n", ATOMIC_READ(&adapter->wds_path_num));
	RTW_PRINT_SEL(sel, "%-17s %-17s %-6s\n"
		, "dst", "next_hop", "age"
	);

	do {
		rtw_rcu_read_lock();

		wpath = rtw_wds_path_lookup_by_idx(adapter, idx);
		if (wpath) {
			_rtw_memcpy(dst, wpath->dst, ETH_ALEN);
			_rtw_memcpy(next_hop, wpath->next_hop->cmn.mac_addr, ETH_ALEN);
			age_ms = rtw_get_passing_time_ms(wpath->last_update);
		}

		rtw_rcu_read_unlock();

		if (wpath) {
			RTW_PRINT_SEL(sel, MAC_FMT" "MAC_FMT" %6u\n"
				, MAC_ARG(dst), MAC_ARG(next_hop)
				, age_ms < 999999 ? age_ms : 999999
			);
		}

		idx++;
	} while (wpath);
}

static
struct rtw_wds_path *rtw_wds_path_new(_adapter *adapter,
				const u8 *dst)
{
	struct rtw_wds_path *new_wpath;

	new_wpath = rtw_zmalloc(sizeof(struct rtw_wds_path));
	if (!new_wpath)
		return NULL;

	new_wpath->adapter = adapter;
	_rtw_memcpy(new_wpath->dst, dst, ETH_ALEN);
	new_wpath->last_update = rtw_get_current_time();

	return new_wpath;
}

/**
 * Returns: 0 on success
 *
 * State: the initial state of the new path is set to 0
 */
struct rtw_wds_path *rtw_wds_path_add(_adapter *adapter,
	const u8 *dst, struct sta_info *next_hop)
{
	struct rtw_wds_table *tbl = adapter->wds_paths;
	struct rtw_wds_path *wpath, *new_wpath;
	int ret;

	if (!tbl)
		return ERR_PTR(-ENOTSUPP);

	if (_rtw_memcmp(dst, adapter_mac_addr(adapter), ETH_ALEN) == _TRUE)
		/* never add ourselves as neighbours */
		return ERR_PTR(-ENOTSUPP);

	if (IS_MCAST(dst))
		return ERR_PTR(-ENOTSUPP);

	if (ATOMIC_INC_UNLESS(&adapter->wds_path_num, RTW_WDS_MAX_PATHS) == 0)
		return ERR_PTR(-ENOSPC);

	new_wpath = rtw_wds_path_new(adapter, dst);
	if (!new_wpath)
		return ERR_PTR(-ENOMEM);

	do {
		ret = rtw_rhashtable_lookup_insert_fast(&tbl->rhead,
						    &new_wpath->rhash,
						    rtw_wds_rht_params);

		if (ret == -EEXIST)
			wpath = rtw_rhashtable_lookup_fast(&tbl->rhead,
						       dst,
						       rtw_wds_rht_params);

	} while (unlikely(ret == -EEXIST && !wpath));

	if (ret && ret != -EEXIST)
		return ERR_PTR(ret);

	/* At this point either new_wpath was added, or we found a
	 * matching entry already in the table; in the latter case
	 * free the unnecessary new entry.
	 */
	if (ret == -EEXIST) {
		rtw_mfree(new_wpath, sizeof(struct rtw_wds_path));
		new_wpath = wpath;
	}
	rtw_wds_path_assign_nexthop(new_wpath, next_hop);

	return new_wpath;
}

static void rtw_wds_path_free_rcu(struct rtw_wds_table *tbl,
			       struct rtw_wds_path *wpath)
{
	_adapter *adapter = wpath->adapter;

	ATOMIC_DEC(&adapter->wds_path_num);

	rtw_wpath_free_rcu(wpath);
}

static void __rtw_wds_path_del(struct rtw_wds_table *tbl, struct rtw_wds_path *wpath)
{
	rtw_rhashtable_remove_fast(&tbl->rhead, &wpath->rhash, rtw_wds_rht_params);
	rtw_wds_path_free_rcu(tbl, wpath);
}

void rtw_wds_path_flush_by_nexthop(struct sta_info *sta)
{
	_adapter *adapter = sta->padapter;
	struct rtw_wds_table *tbl = adapter->wds_paths;
	struct rtw_wds_path *wpath;
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

	while ((wpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wpath) && PTR_ERR(wpath) == -EAGAIN)
			continue;
		if (IS_ERR(wpath))
			break;

		if (rtw_rcu_access_pointer(wpath->next_hop) == sta)
			__rtw_wds_path_del(tbl, wpath);
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

static void rtw_wds_table_flush_by_iface(struct rtw_wds_table *tbl)
{
	struct rtw_wds_path *wpath;
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

	while ((wpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wpath) && PTR_ERR(wpath) == -EAGAIN)
			continue;
		if (IS_ERR(wpath))
			break;
		__rtw_wds_path_del(tbl, wpath);
	}
out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

void rtw_wds_path_flush_by_iface(_adapter *adapter)
{
	rtw_wds_table_flush_by_iface(adapter->wds_paths);
}

static int rtw_wds_table_path_del(struct rtw_wds_table *tbl,
			  const u8 *addr)
{
	struct rtw_wds_path *wpath;

	if (!tbl)
		return -ENXIO;

	rtw_rcu_read_lock();
	wpath = rtw_rhashtable_lookup_fast(&tbl->rhead, addr, rtw_wds_rht_params);
	if (!wpath) {
		rtw_rcu_read_unlock();
		return -ENXIO;
	}

	__rtw_wds_path_del(tbl, wpath);
	rtw_rcu_read_unlock();
	return 0;
}

int rtw_wds_path_del(_adapter *adapter, const u8 *addr)
{
	int err;

	err = rtw_wds_table_path_del(adapter->wds_paths, addr);
	return err;
}

int rtw_wds_pathtbl_init(_adapter *adapter)
{
	struct rtw_wds_table *tbl_path;
	int ret;

	tbl_path = rtw_wds_table_alloc();
	if (!tbl_path)
		return -ENOMEM;

	rtw_rhashtable_init(&tbl_path->rhead, &rtw_wds_rht_params);

	ATOMIC_SET(&adapter->wds_path_num, 0);
	adapter->wds_paths = tbl_path;

	return 0;
}

static
void rtw_wds_path_tbl_expire(_adapter *adapter,
			  struct rtw_wds_table *tbl)
{
	struct rtw_wds_path *wpath;
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

	while ((wpath = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wpath) && PTR_ERR(wpath) == -EAGAIN)
			continue;
		if (IS_ERR(wpath))
			break;
		if (rtw_time_after(rtw_get_current_time(), wpath->last_update + RTW_WDS_PATH_EXPIRE))
			__rtw_wds_path_del(tbl, wpath);
	}

out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

void rtw_wds_path_expire(_adapter *adapter)
{
	rtw_wds_path_tbl_expire(adapter, adapter->wds_paths);
}

void rtw_wds_pathtbl_unregister(_adapter *adapter)
{
	if (adapter->wds_paths) {
		rtw_wds_table_free(adapter->wds_paths);
		adapter->wds_paths = NULL;
	}
}

int rtw_wds_nexthop_lookup(_adapter *adapter, const u8 *da, u8 *ra)
{
	struct rtw_wds_path *wpath;
	struct sta_info *next_hop;
	int err = -ENOENT;

	rtw_rcu_read_lock();
	wpath = rtw_wds_path_lookup(adapter, da);

	if (!wpath)
		goto endlookup;

	next_hop = rtw_rcu_dereference(wpath->next_hop);
	if (next_hop) {
		_rtw_memcpy(ra, next_hop->cmn.mac_addr, ETH_ALEN);
		err = 0;
	}

endlookup:
	rtw_rcu_read_unlock();
	return err;
}

#endif /* defined(CONFIG_AP_MODE) */

/* WDS group adddressed proxy TX record */
struct rtw_wds_gptr {
	u8 src[ETH_ALEN];
	systime last_update;
	rtw_rhash_head rhash;
	_adapter *adapter;
	rtw_rcu_head rcu;
};

#define RTW_WDS_GPTR_EXPIRE (2 * HZ)

/* Maximum number of gptrs per interface */
#define RTW_WDS_MAX_GPTRS		1024

#ifdef PLATFORM_LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
static void rtw_wgptr_free_rcu(struct rtw_wds_gptr *wgptr)
{
	kfree_rcu(wgptr, rcu);
	rtw_mstat_update(MSTAT_TYPE_PHY, MSTAT_FREE, sizeof(struct rtw_wds_gptr));
}
#else
static void rtw_wgptr_free_rcu_callback(rtw_rcu_head *head)
{
	struct rtw_wds_gptr *wgptr;

	wgptr = container_of(head, struct rtw_wds_gptr, rcu);
	rtw_mfree(wgptr, sizeof(struct rtw_wds_gptr));
}

static void rtw_wgptr_free_rcu(struct rtw_wds_gptr *wgptr)
{
	call_rcu(&wgptr->rcu, rtw_wgptr_free_rcu_callback);
}
#endif
#endif /* PLATFORM_LINUX */

static void rtw_wds_gptr_free_rcu(struct rtw_wds_gptr_table *tbl, struct rtw_wds_gptr *wgptr)
{
	_adapter *adapter = wgptr->adapter;

	ATOMIC_DEC(&adapter->wds_gpt_record_num);

	rtw_wgptr_free_rcu(wgptr);
}

static u32 rtw_wds_gptr_table_hash(const void *addr, u32 len, u32 seed)
{
	/* Use last four bytes of hw addr as hash index */
	return jhash_1word(*(u32 *)(addr+2), seed);
}

static const rtw_rhashtable_params rtw_wds_gptr_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct rtw_wds_gptr, src),
	.head_offset = offsetof(struct rtw_wds_gptr, rhash),
	.hashfn = rtw_wds_gptr_table_hash,
};

static void rtw_wds_gptr_rht_free(void *ptr, void *tblptr)
{
	struct rtw_wds_gptr *wgptr = ptr;
	struct rtw_wds_gptr_table *tbl = tblptr;

	rtw_wds_gptr_free_rcu(tbl, wgptr);
}

static struct rtw_wds_gptr_table *rtw_wds_gptr_table_alloc(void)
{
	struct rtw_wds_gptr_table *newtbl;

	newtbl = rtw_malloc(sizeof(struct rtw_wds_gptr_table));
	if (!newtbl)
		return NULL;

	return newtbl;
}

static void rtw_wds_gptr_table_free(struct rtw_wds_gptr_table *tbl)
{
	rtw_rhashtable_free_and_destroy(&tbl->rhead,
				    rtw_wds_gptr_rht_free, tbl);
	rtw_mfree(tbl, sizeof(struct rtw_wds_gptr_table));
}

static struct rtw_wds_gptr *rtw_wds_gptr_lookup(_adapter *adapter, const u8 *src)
{
	struct rtw_wds_gptr_table *tbl = adapter->wds_gpt_records;

	if (!tbl)
		return NULL;

	return rtw_rhashtable_lookup_fast(&tbl->rhead, src, rtw_wds_gptr_rht_params);
}

/**
 * Locking: must be called within a read rcu section.
 */
static struct rtw_wds_gptr *rtw_wds_gptr_lookup_by_idx(_adapter *adapter, int idx)
{
	int i = 0, ret;
	struct rtw_wds_gptr_table *tbl = adapter->wds_gpt_records;
	struct rtw_wds_gptr *wgptr = NULL;
	rtw_rhashtable_iter iter;

	if (!tbl)
		return NULL;

	ret = rtw_rhashtable_walk_enter(&tbl->rhead, &iter);
	if (ret)
		return NULL;

	ret = rtw_rhashtable_walk_start(&iter);
	if (ret && ret != -EAGAIN)
		goto err;

	while ((wgptr = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wgptr) && PTR_ERR(wgptr) == -EAGAIN)
			continue;
		if (IS_ERR(wgptr))
			break;
		if (i++ == idx)
			break;
	}
err:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);

	if (IS_ERR(wgptr) || !wgptr)
		return NULL;

	return wgptr;
}

void dump_wgptr(void *sel, _adapter *adapter)
{
	struct rtw_wds_gptr *wgptr;
	int idx = 0;
	char src[ETH_ALEN];
	u32 age_ms;

	RTW_PRINT_SEL(sel, "num:%d\n", ATOMIC_READ(&adapter->wds_gpt_record_num));
	RTW_PRINT_SEL(sel, "%-17s %-6s\n"
		, "src", "age"
	);

	do {
		rtw_rcu_read_lock();

		wgptr = rtw_wds_gptr_lookup_by_idx(adapter, idx);
		if (wgptr) {
			_rtw_memcpy(src, wgptr->src, ETH_ALEN);
			age_ms = rtw_get_passing_time_ms(wgptr->last_update);
		}

		rtw_rcu_read_unlock();

		if (wgptr) {
			RTW_PRINT_SEL(sel, MAC_FMT" %6u\n"
				, MAC_ARG(src)
				, age_ms < 999999 ? age_ms : 999999
			);
		}

		idx++;
	} while (wgptr);
}

static struct rtw_wds_gptr *rtw_wds_gptr_new(_adapter *adapter, const u8 *src)
{
	struct rtw_wds_gptr *new_wgptr;

	new_wgptr = rtw_zmalloc(sizeof(struct rtw_wds_gptr));
	if (!new_wgptr)
		return NULL;

	new_wgptr->adapter = adapter;
	_rtw_memcpy(new_wgptr->src, src, ETH_ALEN);
	new_wgptr->last_update = rtw_get_current_time();

	return new_wgptr;
}

static struct rtw_wds_gptr *rtw_wds_gptr_add(_adapter *adapter, const u8 *src)
{
	struct rtw_wds_gptr_table *tbl = adapter->wds_gpt_records;
	struct rtw_wds_gptr *wgptr, *new_wgptr;
	int ret;

	if (!tbl)
		return ERR_PTR(-ENOTSUPP);

	if (ATOMIC_INC_UNLESS(&adapter->wds_gpt_record_num, RTW_WDS_MAX_PATHS) == 0)
		return ERR_PTR(-ENOSPC);

	new_wgptr = rtw_wds_gptr_new(adapter, src);
	if (!new_wgptr)
		return ERR_PTR(-ENOMEM);

	do {
		ret = rtw_rhashtable_lookup_insert_fast(&tbl->rhead,
						    &new_wgptr->rhash,
						    rtw_wds_gptr_rht_params);

		if (ret == -EEXIST)
			wgptr = rtw_rhashtable_lookup_fast(&tbl->rhead,
						       src,
						       rtw_wds_gptr_rht_params);

	} while (unlikely(ret == -EEXIST && !wgptr));

	if (ret && ret != -EEXIST)
		return ERR_PTR(ret);

	/* At this point either new_wgptr was added, or we found a
	 * matching entry already in the table; in the latter case
	 * free the unnecessary new entry.
	 */
	if (ret == -EEXIST) {
		rtw_mfree(new_wgptr, sizeof(struct rtw_wds_gptr));
		new_wgptr = wgptr;
	}

	return new_wgptr;
}

bool rtw_rx_wds_gptr_check(_adapter *adapter, const u8 *src)
{
	struct rtw_wds_gptr *wgptr;
	bool ret = 0;

	rtw_rcu_read_lock();

	wgptr = rtw_wds_gptr_lookup(adapter, src);
	if (wgptr)
		ret = rtw_time_after(wgptr->last_update + RTW_WDS_GPTR_EXPIRE, rtw_get_current_time());

	rtw_rcu_read_unlock();

	return ret;
}

void rtw_tx_wds_gptr_update(_adapter *adapter, const u8 *src)
{
	struct rtw_wds_gptr *wgptr;

	rtw_rcu_read_lock();
	wgptr = rtw_wds_gptr_lookup(adapter, src);
	if (!wgptr)
		rtw_wds_gptr_add(adapter, src);
	else
		wgptr->last_update = rtw_get_current_time();
	rtw_rcu_read_unlock();
}

static void __rtw_wds_gptr_del(struct rtw_wds_gptr_table *tbl, struct rtw_wds_gptr *wgptr)
{
	rtw_rhashtable_remove_fast(&tbl->rhead, &wgptr->rhash, rtw_wds_gptr_rht_params);
	rtw_wds_gptr_free_rcu(tbl, wgptr);
}

void rtw_wds_gptr_expire(_adapter *adapter)
{
	struct rtw_wds_gptr_table *tbl = adapter->wds_gpt_records;
	struct rtw_wds_gptr *wgptr;
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

	while ((wgptr = rtw_rhashtable_walk_next(&iter))) {
		if (IS_ERR(wgptr) && PTR_ERR(wgptr) == -EAGAIN)
			continue;
		if (IS_ERR(wgptr))
			break;
		if (rtw_time_after(rtw_get_current_time(), wgptr->last_update + RTW_WDS_GPTR_EXPIRE))
			__rtw_wds_gptr_del(tbl, wgptr);
	}

out:
	rtw_rhashtable_walk_stop(&iter);
	rtw_rhashtable_walk_exit(&iter);
}

int rtw_wds_gptr_tbl_init(_adapter *adapter)
{
	struct rtw_wds_gptr_table *tbl;
	int ret;

	tbl = rtw_wds_gptr_table_alloc();
	if (!tbl)
		return -ENOMEM;

	rtw_rhashtable_init(&tbl->rhead, &rtw_wds_gptr_rht_params);

	ATOMIC_SET(&adapter->wds_gpt_record_num, 0);
	adapter->wds_gpt_records = tbl;

	return 0;
}

void rtw_wds_gptr_tbl_unregister(_adapter *adapter)
{
	if (adapter->wds_gpt_records) {
		rtw_wds_gptr_table_free(adapter->wds_gpt_records);
		adapter->wds_gpt_records = NULL;
	}
}
#endif /* defined(CONFIG_RTW_WDS) */

