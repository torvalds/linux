// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Chelsio Communications, Inc. All rights reserved. */

#include "cxgb4.h"

static int cxgb4_mps_ref_dec(struct adapter *adap, u16 idx)
{
	struct mps_entries_ref *mps_entry, *tmp;
	int ret = -EINVAL;

	spin_lock(&adap->mps_ref_lock);
	list_for_each_entry_safe(mps_entry, tmp, &adap->mps_ref, list) {
		if (mps_entry->idx == idx) {
			if (!refcount_dec_and_test(&mps_entry->refcnt)) {
				spin_unlock(&adap->mps_ref_lock);
				return -EBUSY;
			}
			list_del(&mps_entry->list);
			kfree(mps_entry);
			ret = 0;
			break;
		}
	}
	spin_unlock(&adap->mps_ref_lock);
	return ret;
}

static int cxgb4_mps_ref_inc(struct adapter *adap, const u8 *mac_addr,
			     u16 idx, const u8 *mask)
{
	u8 bitmask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct mps_entries_ref *mps_entry;
	int ret = 0;

	spin_lock_bh(&adap->mps_ref_lock);
	list_for_each_entry(mps_entry, &adap->mps_ref, list) {
		if (mps_entry->idx == idx) {
			refcount_inc(&mps_entry->refcnt);
			goto unlock;
		}
	}
	mps_entry = kzalloc(sizeof(*mps_entry), GFP_ATOMIC);
	if (!mps_entry) {
		ret = -ENOMEM;
		goto unlock;
	}
	ether_addr_copy(mps_entry->mask, mask ? mask : bitmask);
	ether_addr_copy(mps_entry->addr, mac_addr);
	mps_entry->idx = idx;
	refcount_set(&mps_entry->refcnt, 1);
	list_add_tail(&mps_entry->list, &adap->mps_ref);
unlock:
	spin_unlock_bh(&adap->mps_ref_lock);
	return ret;
}

int cxgb4_update_mac_filt(struct port_info *pi, unsigned int viid,
			  int *tcam_idx, const u8 *addr,
			  bool persistent, u8 *smt_idx)
{
	int ret;

	ret = cxgb4_change_mac(pi, viid, tcam_idx,
			       addr, persistent, smt_idx);
	if (ret < 0)
		return ret;

	cxgb4_mps_ref_inc(pi->adapter, addr, *tcam_idx, NULL);
	return ret;
}

int cxgb4_free_raw_mac_filt(struct adapter *adap,
			    unsigned int viid,
			    const u8 *addr,
			    const u8 *mask,
			    unsigned int idx,
			    u8 lookup_type,
			    u8 port_id,
			    bool sleep_ok)
{
	int ret = 0;

	if (!cxgb4_mps_ref_dec(adap, idx))
		ret = t4_free_raw_mac_filt(adap, viid, addr,
					   mask, idx, lookup_type,
					   port_id, sleep_ok);

	return ret;
}

int cxgb4_alloc_raw_mac_filt(struct adapter *adap,
			     unsigned int viid,
			     const u8 *addr,
			     const u8 *mask,
			     unsigned int idx,
			     u8 lookup_type,
			     u8 port_id,
			     bool sleep_ok)
{
	int ret;

	ret = t4_alloc_raw_mac_filt(adap, viid, addr,
				    mask, idx, lookup_type,
				    port_id, sleep_ok);
	if (ret < 0)
		return ret;

	if (cxgb4_mps_ref_inc(adap, addr, ret, mask)) {
		ret = -ENOMEM;
		t4_free_raw_mac_filt(adap, viid, addr,
				     mask, idx, lookup_type,
				     port_id, sleep_ok);
	}

	return ret;
}

int cxgb4_free_encap_mac_filt(struct adapter *adap, unsigned int viid,
			      int idx, bool sleep_ok)
{
	int ret = 0;

	if (!cxgb4_mps_ref_dec(adap, idx))
		ret = t4_free_encap_mac_filt(adap, viid, idx, sleep_ok);

	return ret;
}

int cxgb4_alloc_encap_mac_filt(struct adapter *adap, unsigned int viid,
			       const u8 *addr, const u8 *mask,
			       unsigned int vni, unsigned int vni_mask,
			       u8 dip_hit, u8 lookup_type, bool sleep_ok)
{
	int ret;

	ret = t4_alloc_encap_mac_filt(adap, viid, addr, mask, vni, vni_mask,
				      dip_hit, lookup_type, sleep_ok);
	if (ret < 0)
		return ret;

	if (cxgb4_mps_ref_inc(adap, addr, ret, mask)) {
		ret = -ENOMEM;
		t4_free_encap_mac_filt(adap, viid, ret, sleep_ok);
	}
	return ret;
}

int cxgb4_init_mps_ref_entries(struct adapter *adap)
{
	spin_lock_init(&adap->mps_ref_lock);
	INIT_LIST_HEAD(&adap->mps_ref);

	return 0;
}

void cxgb4_free_mps_ref_entries(struct adapter *adap)
{
	struct mps_entries_ref *mps_entry, *tmp;

	if (!list_empty(&adap->mps_ref))
		return;

	spin_lock(&adap->mps_ref_lock);
	list_for_each_entry_safe(mps_entry, tmp, &adap->mps_ref, list) {
		list_del(&mps_entry->list);
		kfree(mps_entry);
	}
	spin_unlock(&adap->mps_ref_lock);
}
