// SPDX-License-Identifier: GPL-2.0-or-later

struct scanu_result_wext{
	struct list_head scanu_re_list;
	struct cfg80211_bss *bss;
	struct scanu_result_ind *ind;
	u32_l *payload;
};

void aicwf_set_wireless_ext( struct net_device *ndev, struct rwnx_hw *rwnx_hw);
void aicwf_scan_complete_event(struct net_device *dev);

