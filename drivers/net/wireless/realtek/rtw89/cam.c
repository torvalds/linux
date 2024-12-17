// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"

static struct sk_buff *
rtw89_cam_get_sec_key_cmd(struct rtw89_dev *rtwdev,
			  struct rtw89_sec_cam_entry *sec_cam,
			  bool ext_key)
{
	struct sk_buff *skb;
	u32 cmd_len = H2C_SEC_CAM_LEN;
	u32 key32[4];
	u8 *cmd;
	int i, j;

	skb = rtw89_fw_h2c_alloc_skb_with_hdr(rtwdev, cmd_len);
	if (!skb)
		return NULL;

	skb_put_zero(skb, cmd_len);

	for (i = 0; i < 4; i++) {
		j = i * 4;
		j += ext_key ? 16 : 0;
		key32[i] = FIELD_PREP(GENMASK(7, 0), sec_cam->key[j + 0]) |
			   FIELD_PREP(GENMASK(15, 8), sec_cam->key[j + 1]) |
			   FIELD_PREP(GENMASK(23, 16), sec_cam->key[j + 2]) |
			   FIELD_PREP(GENMASK(31, 24), sec_cam->key[j + 3]);
	}

	cmd = skb->data;
	RTW89_SET_FWCMD_SEC_IDX(cmd, sec_cam->sec_cam_idx + (ext_key ? 1 : 0));
	RTW89_SET_FWCMD_SEC_OFFSET(cmd, sec_cam->offset);
	RTW89_SET_FWCMD_SEC_LEN(cmd, sec_cam->len);
	RTW89_SET_FWCMD_SEC_TYPE(cmd, sec_cam->type);
	RTW89_SET_FWCMD_SEC_EXT_KEY(cmd, ext_key);
	RTW89_SET_FWCMD_SEC_SPP_MODE(cmd, sec_cam->spp_mode);
	RTW89_SET_FWCMD_SEC_KEY0(cmd, key32[0]);
	RTW89_SET_FWCMD_SEC_KEY1(cmd, key32[1]);
	RTW89_SET_FWCMD_SEC_KEY2(cmd, key32[2]);
	RTW89_SET_FWCMD_SEC_KEY3(cmd, key32[3]);

	return skb;
}

static int rtw89_cam_send_sec_key_cmd(struct rtw89_dev *rtwdev,
				      struct rtw89_sec_cam_entry *sec_cam)
{
	struct sk_buff *skb, *ext_skb;
	int ret;

	skb = rtw89_cam_get_sec_key_cmd(rtwdev, sec_cam, false);
	if (!skb) {
		rtw89_err(rtwdev, "failed to get sec key command\n");
		return -ENOMEM;
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, skb,
			      FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_SEC_CAM,
			      H2C_FUNC_MAC_SEC_UPD, 1, 0,
			      H2C_SEC_CAM_LEN);
	ret = rtw89_h2c_tx(rtwdev, skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send sec key h2c: %d\n", ret);
		dev_kfree_skb(skb);
		return ret;
	}

	if (!sec_cam->ext_key)
		return 0;

	ext_skb = rtw89_cam_get_sec_key_cmd(rtwdev, sec_cam, true);
	if (!ext_skb) {
		rtw89_err(rtwdev, "failed to get ext sec key command\n");
		return -ENOMEM;
	}

	rtw89_h2c_pkt_set_hdr(rtwdev, ext_skb,
			      FWCMD_TYPE_H2C,
			      H2C_CAT_MAC,
			      H2C_CL_MAC_SEC_CAM,
			      H2C_FUNC_MAC_SEC_UPD,
			      1, 0, H2C_SEC_CAM_LEN);
	ret = rtw89_h2c_tx(rtwdev, ext_skb, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to send ext sec key h2c: %d\n", ret);
		dev_kfree_skb(ext_skb);
		return ret;
	}

	return 0;
}

static int rtw89_cam_get_avail_sec_cam(struct rtw89_dev *rtwdev,
				       u8 *sec_cam_idx, bool ext_key)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	u8 sec_cam_num = chip->scam_num;
	u8 idx = 0;

	if (!ext_key) {
		idx = find_first_zero_bit(cam_info->sec_cam_map, sec_cam_num);
		if (idx >= sec_cam_num)
			return -EBUSY;

		set_bit(idx, cam_info->sec_cam_map);
		*sec_cam_idx = idx;

		return 0;
	}

again:
	idx = find_next_zero_bit(cam_info->sec_cam_map, sec_cam_num, idx);
	if (idx >= sec_cam_num - 1)
		return -EBUSY;
	/* ext keys need two cam entries for 256-bit key */
	if (test_bit(idx + 1, cam_info->sec_cam_map)) {
		idx++;
		goto again;
	}

	set_bit(idx, cam_info->sec_cam_map);
	set_bit(idx + 1, cam_info->sec_cam_map);
	*sec_cam_idx = idx;

	return 0;
}

static int rtw89_cam_get_addr_cam_key_idx(struct rtw89_addr_cam_entry *addr_cam,
					  struct rtw89_sec_cam_entry *sec_cam,
					  struct ieee80211_key_conf *key,
					  u8 *key_idx)
{
	u8 idx;

	/* RTW89_ADDR_CAM_SEC_NONE	: not enabled
	 * RTW89_ADDR_CAM_SEC_ALL_UNI	: 0 - 6 unicast
	 * RTW89_ADDR_CAM_SEC_NORMAL	: 0 - 1 unicast, 2 - 4 group, 5 - 6 BIP
	 * RTW89_ADDR_CAM_SEC_4GROUP	: 0 - 1 unicast, 2 - 5 group, 6 BIP
	 */
	switch (addr_cam->sec_ent_mode) {
	case RTW89_ADDR_CAM_SEC_NONE:
		return -EINVAL;
	case RTW89_ADDR_CAM_SEC_ALL_UNI:
		idx = find_first_zero_bit(addr_cam->sec_cam_map,
					  RTW89_SEC_CAM_IN_ADDR_CAM);
		if (idx >= RTW89_SEC_CAM_IN_ADDR_CAM)
			return -EBUSY;
		*key_idx = idx;
		break;
	case RTW89_ADDR_CAM_SEC_NORMAL:
		if (sec_cam->type == RTW89_SEC_KEY_TYPE_BIP_CCMP128) {
			idx = find_next_zero_bit(addr_cam->sec_cam_map,
						 RTW89_SEC_CAM_IN_ADDR_CAM, 5);
			if (idx > 6)
				return -EBUSY;
			*key_idx = idx;
			break;
		}

		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			idx = find_next_zero_bit(addr_cam->sec_cam_map,
						 RTW89_SEC_CAM_IN_ADDR_CAM, 0);
			if (idx > 1)
				return -EBUSY;
			*key_idx = idx;
			break;
		}

		/* Group keys */
		idx = find_next_zero_bit(addr_cam->sec_cam_map,
					 RTW89_SEC_CAM_IN_ADDR_CAM, 2);
		if (idx > 4)
			return -EBUSY;
		*key_idx = idx;
		break;
	case RTW89_ADDR_CAM_SEC_4GROUP:
		if (sec_cam->type == RTW89_SEC_KEY_TYPE_BIP_CCMP128) {
			if (test_bit(6, addr_cam->sec_cam_map))
				return -EINVAL;
			*key_idx = 6;
			break;
		}

		if (key->flags & IEEE80211_KEY_FLAG_PAIRWISE) {
			idx = find_next_zero_bit(addr_cam->sec_cam_map,
						 RTW89_SEC_CAM_IN_ADDR_CAM, 0);
			if (idx > 1)
				return -EBUSY;
			*key_idx = idx;
			break;
		}

		/* Group keys */
		idx = find_next_zero_bit(addr_cam->sec_cam_map,
					 RTW89_SEC_CAM_IN_ADDR_CAM, 2);
		if (idx > 5)
			return -EBUSY;
		*key_idx = idx;
		break;
	}

	return 0;
}

static int __rtw89_cam_detach_sec_cam(struct rtw89_dev *rtwdev,
				      struct rtw89_vif_link *rtwvif_link,
				      struct rtw89_sta_link *rtwsta_link,
				      const struct rtw89_sec_cam_entry *sec_cam,
				      bool inform_fw)
{
	struct rtw89_addr_cam_entry *addr_cam;
	unsigned int i;
	int ret = 0;

	addr_cam = rtw89_get_addr_cam_of(rtwvif_link, rtwsta_link);

	for_each_set_bit(i, addr_cam->sec_cam_map, RTW89_SEC_CAM_IN_ADDR_CAM) {
		if (addr_cam->sec_ent[i] != sec_cam->sec_cam_idx)
			continue;

		clear_bit(i, addr_cam->sec_cam_map);
	}

	if (inform_fw) {
		ret = rtw89_chip_h2c_dctl_sec_cam(rtwdev, rtwvif_link, rtwsta_link);
		if (ret)
			rtw89_err(rtwdev,
				  "failed to update dctl cam del key: %d\n", ret);
		ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
		if (ret)
			rtw89_err(rtwdev, "failed to update cam del key: %d\n", ret);
	}

	return ret;
}

static int __rtw89_cam_attach_sec_cam(struct rtw89_dev *rtwdev,
				      struct rtw89_vif_link *rtwvif_link,
				      struct rtw89_sta_link *rtwsta_link,
				      struct ieee80211_key_conf *key,
				      struct rtw89_sec_cam_entry *sec_cam)
{
	struct rtw89_addr_cam_entry *addr_cam;
	u8 key_idx = 0;
	int ret;

	addr_cam = rtw89_get_addr_cam_of(rtwvif_link, rtwsta_link);

	if (key->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	    key->cipher == WLAN_CIPHER_SUITE_WEP104)
		addr_cam->sec_ent_mode = RTW89_ADDR_CAM_SEC_ALL_UNI;

	ret = rtw89_cam_get_addr_cam_key_idx(addr_cam, sec_cam, key, &key_idx);
	if (ret) {
		rtw89_err(rtwdev, "failed to get addr cam key idx %d, %d\n",
			  addr_cam->sec_ent_mode, sec_cam->type);
		return ret;
	}

	addr_cam->sec_ent_keyid[key_idx] = key->keyidx;
	addr_cam->sec_ent[key_idx] = sec_cam->sec_cam_idx;
	set_bit(key_idx, addr_cam->sec_cam_map);
	ret = rtw89_chip_h2c_dctl_sec_cam(rtwdev, rtwvif_link, rtwsta_link);
	if (ret) {
		rtw89_err(rtwdev, "failed to update dctl cam sec entry: %d\n",
			  ret);
		return ret;
	}
	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
	if (ret) {
		rtw89_err(rtwdev, "failed to update addr cam sec entry: %d\n",
			  ret);
		clear_bit(key_idx, addr_cam->sec_cam_map);
		return ret;
	}

	return 0;
}

static int rtw89_cam_detach_sec_cam(struct rtw89_dev *rtwdev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    const struct rtw89_sec_cam_entry *sec_cam,
				    bool inform_fw)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);
	struct rtw89_sta_link *rtwsta_link;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	int ret;

	if (!vif) {
		rtw89_err(rtwdev, "No iface for deleting sec cam\n");
		return -EINVAL;
	}

	rtwvif = vif_to_rtwvif(vif);

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		rtwsta_link = rtwsta ? rtwsta->links[link_id] : NULL;
		if (rtwsta && !rtwsta_link)
			continue;

		ret = __rtw89_cam_detach_sec_cam(rtwdev, rtwvif_link, rtwsta_link,
						 sec_cam, inform_fw);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtw89_cam_attach_sec_cam(struct rtw89_dev *rtwdev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_key_conf *key,
				    struct rtw89_sec_cam_entry *sec_cam)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);
	struct rtw89_sta_link *rtwsta_link;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	int key_link_id;
	int ret;

	if (!vif) {
		rtw89_err(rtwdev, "No iface for adding sec cam\n");
		return -EINVAL;
	}

	rtwvif = vif_to_rtwvif(vif);

	key_link_id = ieee80211_vif_is_mld(vif) ? key->link_id : 0;
	if (key_link_id >= 0) {
		rtwvif_link = rtwvif->links[key_link_id];
		rtwsta_link = rtwsta ? rtwsta->links[key_link_id] : NULL;

		if (!rtwvif_link || (rtwsta && !rtwsta_link)) {
			rtw89_err(rtwdev, "No drv link for adding sec cam\n");
			return -ENOLINK;
		}

		return __rtw89_cam_attach_sec_cam(rtwdev, rtwvif_link,
						  rtwsta_link, key, sec_cam);
	}

	/* key_link_id < 0: MLD pairwise key */
	if (!rtwsta) {
		rtw89_err(rtwdev, "No sta for adding MLD pairwise sec cam\n");
		return -EINVAL;
	}

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id) {
		rtwvif_link = rtwsta_link->rtwvif_link;
		ret = __rtw89_cam_attach_sec_cam(rtwdev, rtwvif_link,
						 rtwsta_link, key, sec_cam);
		if (ret)
			return ret;
	}

	return 0;
}

static int rtw89_cam_sec_key_install(struct rtw89_dev *rtwdev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct ieee80211_key_conf *key,
				     u8 hw_key_type, bool ext_key)
{
	struct rtw89_sec_cam_entry *sec_cam = NULL;
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	u8 sec_cam_idx;
	int ret;

	/* maximum key length 256-bit */
	if (key->keylen > 32) {
		rtw89_err(rtwdev, "invalid sec key length %d\n", key->keylen);
		return -EINVAL;
	}

	ret = rtw89_cam_get_avail_sec_cam(rtwdev, &sec_cam_idx, ext_key);
	if (ret) {
		rtw89_warn(rtwdev, "no available sec cam: %d ext: %d\n",
			   ret, ext_key);
		return ret;
	}

	sec_cam = kzalloc(sizeof(*sec_cam), GFP_KERNEL);
	if (!sec_cam) {
		ret = -ENOMEM;
		goto err_release_cam;
	}

	key->hw_key_idx = sec_cam_idx;
	cam_info->sec_entries[sec_cam_idx] = sec_cam;

	sec_cam->sec_cam_idx = sec_cam_idx;
	sec_cam->type = hw_key_type;
	sec_cam->len = RTW89_SEC_CAM_LEN;
	sec_cam->ext_key = ext_key;
	memcpy(sec_cam->key, key->key, key->keylen);
	ret = rtw89_cam_send_sec_key_cmd(rtwdev, sec_cam);
	if (ret) {
		rtw89_err(rtwdev, "failed to send sec key cmd: %d\n", ret);
		goto err_release_cam;
	}

	/* associate with addr cam */
	ret = rtw89_cam_attach_sec_cam(rtwdev, vif, sta, key, sec_cam);
	if (ret) {
		rtw89_err(rtwdev, "failed to attach sec cam: %d\n", ret);
		goto err_release_cam;
	}

	return 0;

err_release_cam:
	cam_info->sec_entries[sec_cam_idx] = NULL;
	kfree(sec_cam);
	clear_bit(sec_cam_idx, cam_info->sec_cam_map);
	if (ext_key)
		clear_bit(sec_cam_idx + 1, cam_info->sec_cam_map);

	return ret;
}

int rtw89_cam_sec_key_add(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 hw_key_type;
	bool ext_key = false;
	int ret;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		hw_key_type = RTW89_SEC_KEY_TYPE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		hw_key_type = RTW89_SEC_KEY_TYPE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		hw_key_type = RTW89_SEC_KEY_TYPE_CCMP128;
		if (!chip->hw_mgmt_tx_encrypt)
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		hw_key_type = RTW89_SEC_KEY_TYPE_CCMP256;
		if (!chip->hw_mgmt_tx_encrypt)
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		ext_key = true;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		hw_key_type = RTW89_SEC_KEY_TYPE_GCMP128;
		if (!chip->hw_mgmt_tx_encrypt)
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		hw_key_type = RTW89_SEC_KEY_TYPE_GCMP256;
		if (!chip->hw_mgmt_tx_encrypt)
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		ext_key = true;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		hw_key_type = RTW89_SEC_KEY_TYPE_BIP_CCMP128;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!chip->hw_sec_hdr)
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;

	ret = rtw89_cam_sec_key_install(rtwdev, vif, sta, key, hw_key_type,
					ext_key);
	if (ret) {
		rtw89_err(rtwdev, "failed to install key type %d ext %d: %d\n",
			  hw_key_type, ext_key, ret);
		return ret;
	}

	return 0;
}

int rtw89_cam_sec_key_del(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key,
			  bool inform_fw)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	const struct rtw89_sec_cam_entry *sec_cam;
	u8 sec_cam_idx;
	int ret;

	sec_cam_idx = key->hw_key_idx;
	sec_cam = cam_info->sec_entries[sec_cam_idx];
	if (!sec_cam)
		return -EINVAL;

	ret = rtw89_cam_detach_sec_cam(rtwdev, vif, sta, sec_cam, inform_fw);

	/* clear valid bit in addr cam will disable sec cam,
	 * so we don't need to send H2C command again
	 */
	cam_info->sec_entries[sec_cam_idx] = NULL;
	clear_bit(sec_cam_idx, cam_info->sec_cam_map);
	if (sec_cam->ext_key)
		clear_bit(sec_cam_idx + 1, cam_info->sec_cam_map);

	kfree(sec_cam);

	return ret;
}

static void rtw89_cam_reset_key_iter(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     struct ieee80211_key_conf *key,
				     void *data)
{
	struct rtw89_dev *rtwdev = (struct rtw89_dev *)data;

	rtw89_cam_sec_key_del(rtwdev, vif, sta, key, false);
}

void rtw89_cam_deinit_addr_cam(struct rtw89_dev *rtwdev,
			       struct rtw89_addr_cam_entry *addr_cam)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;

	addr_cam->valid = false;
	clear_bit(addr_cam->addr_cam_idx, cam_info->addr_cam_map);
}

void rtw89_cam_deinit_bssid_cam(struct rtw89_dev *rtwdev,
				struct rtw89_bssid_cam_entry *bssid_cam)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;

	bssid_cam->valid = false;
	clear_bit(bssid_cam->bssid_cam_idx, cam_info->bssid_cam_map);
}

void rtw89_cam_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_addr_cam_entry *addr_cam = &rtwvif_link->addr_cam;
	struct rtw89_bssid_cam_entry *bssid_cam = &rtwvif_link->bssid_cam;

	rtw89_cam_deinit_addr_cam(rtwdev, addr_cam);
	rtw89_cam_deinit_bssid_cam(rtwdev, bssid_cam);
}

void rtw89_cam_reset_keys(struct rtw89_dev *rtwdev)
{
	rcu_read_lock();
	ieee80211_iter_keys_rcu(rtwdev->hw, NULL, rtw89_cam_reset_key_iter, rtwdev);
	rcu_read_unlock();
}

static int rtw89_cam_get_avail_addr_cam(struct rtw89_dev *rtwdev,
					u8 *addr_cam_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	u8 addr_cam_num = chip->acam_num;
	u8 idx;

	idx = find_first_zero_bit(cam_info->addr_cam_map, addr_cam_num);
	if (idx >= addr_cam_num)
		return -EBUSY;

	set_bit(idx, cam_info->addr_cam_map);
	*addr_cam_idx = idx;

	return 0;
}

static u8 rtw89_get_addr_cam_entry_size(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	switch (chip->chip_id) {
	case RTL8852A:
	case RTL8852B:
	case RTL8851B:
	case RTL8852BT:
		return ADDR_CAM_ENT_SIZE;
	default:
		return ADDR_CAM_ENT_SHORT_SIZE;
	}
}

int rtw89_cam_init_addr_cam(struct rtw89_dev *rtwdev,
			    struct rtw89_addr_cam_entry *addr_cam,
			    const struct rtw89_bssid_cam_entry *bssid_cam)
{
	u8 addr_cam_idx;
	int i;
	int ret;

	if (unlikely(addr_cam->valid)) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "addr cam is already valid; skip init\n");
		return 0;
	}

	ret = rtw89_cam_get_avail_addr_cam(rtwdev, &addr_cam_idx);
	if (ret) {
		rtw89_err(rtwdev, "failed to get available addr cam\n");
		return ret;
	}

	addr_cam->addr_cam_idx = addr_cam_idx;
	addr_cam->len = rtw89_get_addr_cam_entry_size(rtwdev);
	addr_cam->offset = 0;
	addr_cam->valid = true;
	addr_cam->addr_mask = 0;
	addr_cam->mask_sel = RTW89_NO_MSK;
	addr_cam->sec_ent_mode = RTW89_ADDR_CAM_SEC_NORMAL;
	bitmap_zero(addr_cam->sec_cam_map, RTW89_SEC_CAM_IN_ADDR_CAM);

	for (i = 0; i < RTW89_SEC_CAM_IN_ADDR_CAM; i++) {
		addr_cam->sec_ent_keyid[i] = 0;
		addr_cam->sec_ent[i] = 0;
	}

	/* associate addr cam with bssid cam */
	addr_cam->bssid_cam_idx = bssid_cam->bssid_cam_idx;

	return 0;
}

static int rtw89_cam_get_avail_bssid_cam(struct rtw89_dev *rtwdev,
					 u8 *bssid_cam_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	u8 bssid_cam_num = chip->bcam_num;
	u8 idx;

	idx = find_first_zero_bit(cam_info->bssid_cam_map, bssid_cam_num);
	if (idx >= bssid_cam_num)
		return -EBUSY;

	set_bit(idx, cam_info->bssid_cam_map);
	*bssid_cam_idx = idx;

	return 0;
}

int rtw89_cam_init_bssid_cam(struct rtw89_dev *rtwdev,
			     struct rtw89_vif_link *rtwvif_link,
			     struct rtw89_bssid_cam_entry *bssid_cam,
			     const u8 *bssid)
{
	u8 bssid_cam_idx;
	int ret;

	if (unlikely(bssid_cam->valid)) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "bssid cam is already valid; skip init\n");
		return 0;
	}

	ret = rtw89_cam_get_avail_bssid_cam(rtwdev, &bssid_cam_idx);
	if (ret) {
		rtw89_err(rtwdev, "failed to get available bssid cam\n");
		return ret;
	}

	bssid_cam->bssid_cam_idx = bssid_cam_idx;
	bssid_cam->phy_idx = rtwvif_link->phy_idx;
	bssid_cam->len = BSSID_CAM_ENT_SIZE;
	bssid_cam->offset = 0;
	bssid_cam->valid = true;
	ether_addr_copy(bssid_cam->bssid, bssid);

	return 0;
}

void rtw89_cam_bssid_changed(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_bssid_cam_entry *bssid_cam = &rtwvif_link->bssid_cam;

	ether_addr_copy(bssid_cam->bssid, rtwvif_link->bssid);
}

int rtw89_cam_init(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_addr_cam_entry *addr_cam = &rtwvif_link->addr_cam;
	struct rtw89_bssid_cam_entry *bssid_cam = &rtwvif_link->bssid_cam;
	int ret;

	ret = rtw89_cam_init_bssid_cam(rtwdev, rtwvif_link, bssid_cam,
				       rtwvif_link->bssid);
	if (ret) {
		rtw89_err(rtwdev, "failed to init bssid cam\n");
		return ret;
	}

	ret = rtw89_cam_init_addr_cam(rtwdev, addr_cam, bssid_cam);
	if (ret) {
		rtw89_err(rtwdev, "failed to init addr cam\n");
		return ret;
	}

	return 0;
}

int rtw89_cam_fill_bssid_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link, u8 *cmd)
{
	struct rtw89_bssid_cam_entry *bssid_cam = rtw89_get_bssid_cam_of(rtwvif_link,
									 rtwsta_link);
	struct ieee80211_bss_conf *bss_conf;
	u8 bss_color;
	u8 bss_mask;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);
	bss_color = bss_conf->he_bss_color.color;

	if (bss_conf->nontransmitted)
		bss_mask = RTW89_BSSID_MATCH_5_BYTES;
	else
		bss_mask = RTW89_BSSID_MATCH_ALL;

	rcu_read_unlock();

	FWCMD_SET_ADDR_BSSID_IDX(cmd, bssid_cam->bssid_cam_idx);
	FWCMD_SET_ADDR_BSSID_OFFSET(cmd, bssid_cam->offset);
	FWCMD_SET_ADDR_BSSID_LEN(cmd, bssid_cam->len);
	FWCMD_SET_ADDR_BSSID_VALID(cmd, bssid_cam->valid);
	FWCMD_SET_ADDR_BSSID_MASK(cmd, bss_mask);
	FWCMD_SET_ADDR_BSSID_BB_SEL(cmd, bssid_cam->phy_idx);
	FWCMD_SET_ADDR_BSSID_BSS_COLOR(cmd, bss_color);

	FWCMD_SET_ADDR_BSSID_BSSID0(cmd, bssid_cam->bssid[0]);
	FWCMD_SET_ADDR_BSSID_BSSID1(cmd, bssid_cam->bssid[1]);
	FWCMD_SET_ADDR_BSSID_BSSID2(cmd, bssid_cam->bssid[2]);
	FWCMD_SET_ADDR_BSSID_BSSID3(cmd, bssid_cam->bssid[3]);
	FWCMD_SET_ADDR_BSSID_BSSID4(cmd, bssid_cam->bssid[4]);
	FWCMD_SET_ADDR_BSSID_BSSID5(cmd, bssid_cam->bssid[5]);

	return 0;
}

static u8 rtw89_cam_addr_hash(u8 start, const u8 *addr)
{
	u8 hash = 0;
	u8 i;

	for (i = start; i < ETH_ALEN; i++)
		hash ^= addr[i];

	return hash;
}

void rtw89_cam_fill_addr_cam_info(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link,
				  const u8 *scan_mac_addr,
				  u8 *cmd)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_addr_cam_entry *addr_cam =
		rtw89_get_addr_cam_of(rtwvif_link, rtwsta_link);
	struct ieee80211_sta *sta = rtwsta_link_to_sta_safe(rtwsta_link);
	struct ieee80211_link_sta *link_sta;
	const u8 *sma = scan_mac_addr ? scan_mac_addr : rtwvif_link->mac_addr;
	u8 sma_hash, tma_hash, addr_msk_start;
	u8 sma_start = 0;
	u8 tma_start = 0;
	const u8 *tma;

	rcu_read_lock();

	if (sta) {
		link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
		tma = link_sta->addr;
	} else {
		tma = rtwvif_link->bssid;
	}

	if (addr_cam->addr_mask != 0) {
		addr_msk_start = __ffs(addr_cam->addr_mask);
		if (addr_cam->mask_sel == RTW89_SMA)
			sma_start = addr_msk_start;
		else if (addr_cam->mask_sel == RTW89_TMA)
			tma_start = addr_msk_start;
	}
	sma_hash = rtw89_cam_addr_hash(sma_start, sma);
	tma_hash = rtw89_cam_addr_hash(tma_start, tma);

	FWCMD_SET_ADDR_IDX(cmd, addr_cam->addr_cam_idx);
	FWCMD_SET_ADDR_OFFSET(cmd, addr_cam->offset);
	FWCMD_SET_ADDR_LEN(cmd, addr_cam->len);

	FWCMD_SET_ADDR_VALID(cmd, addr_cam->valid);
	FWCMD_SET_ADDR_NET_TYPE(cmd, rtwvif_link->net_type);
	FWCMD_SET_ADDR_BCN_HIT_COND(cmd, rtwvif_link->bcn_hit_cond);
	FWCMD_SET_ADDR_HIT_RULE(cmd, rtwvif_link->hit_rule);
	FWCMD_SET_ADDR_BB_SEL(cmd, rtwvif_link->phy_idx);
	FWCMD_SET_ADDR_ADDR_MASK(cmd, addr_cam->addr_mask);
	FWCMD_SET_ADDR_MASK_SEL(cmd, addr_cam->mask_sel);
	FWCMD_SET_ADDR_SMA_HASH(cmd, sma_hash);
	FWCMD_SET_ADDR_TMA_HASH(cmd, tma_hash);

	FWCMD_SET_ADDR_BSSID_CAM_IDX(cmd, addr_cam->bssid_cam_idx);

	FWCMD_SET_ADDR_SMA0(cmd, sma[0]);
	FWCMD_SET_ADDR_SMA1(cmd, sma[1]);
	FWCMD_SET_ADDR_SMA2(cmd, sma[2]);
	FWCMD_SET_ADDR_SMA3(cmd, sma[3]);
	FWCMD_SET_ADDR_SMA4(cmd, sma[4]);
	FWCMD_SET_ADDR_SMA5(cmd, sma[5]);

	FWCMD_SET_ADDR_TMA0(cmd, tma[0]);
	FWCMD_SET_ADDR_TMA1(cmd, tma[1]);
	FWCMD_SET_ADDR_TMA2(cmd, tma[2]);
	FWCMD_SET_ADDR_TMA3(cmd, tma[3]);
	FWCMD_SET_ADDR_TMA4(cmd, tma[4]);
	FWCMD_SET_ADDR_TMA5(cmd, tma[5]);

	FWCMD_SET_ADDR_PORT_INT(cmd, rtwvif_link->port);
	FWCMD_SET_ADDR_TSF_SYNC(cmd, rtwvif_link->port);
	FWCMD_SET_ADDR_TF_TRS(cmd, rtwvif_link->trigger);
	FWCMD_SET_ADDR_LSIG_TXOP(cmd, rtwvif_link->lsig_txop);
	FWCMD_SET_ADDR_TGT_IND(cmd, rtwvif_link->tgt_ind);
	FWCMD_SET_ADDR_FRM_TGT_IND(cmd, rtwvif_link->frm_tgt_ind);
	FWCMD_SET_ADDR_MACID(cmd, rtwsta_link ? rtwsta_link->mac_id :
						rtwvif_link->mac_id);
	if (rtwvif_link->net_type == RTW89_NET_TYPE_INFRA)
		FWCMD_SET_ADDR_AID12(cmd, vif->cfg.aid & 0xfff);
	else if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE)
		FWCMD_SET_ADDR_AID12(cmd, sta ? sta->aid & 0xfff : 0);
	FWCMD_SET_ADDR_WOL_PATTERN(cmd, rtwvif_link->wowlan_pattern);
	FWCMD_SET_ADDR_WOL_UC(cmd, rtwvif_link->wowlan_uc);
	FWCMD_SET_ADDR_WOL_MAGIC(cmd, rtwvif_link->wowlan_magic);
	FWCMD_SET_ADDR_WAPI(cmd, addr_cam->wapi);
	FWCMD_SET_ADDR_SEC_ENT_MODE(cmd, addr_cam->sec_ent_mode);
	FWCMD_SET_ADDR_SEC_ENT0_KEYID(cmd, addr_cam->sec_ent_keyid[0]);
	FWCMD_SET_ADDR_SEC_ENT1_KEYID(cmd, addr_cam->sec_ent_keyid[1]);
	FWCMD_SET_ADDR_SEC_ENT2_KEYID(cmd, addr_cam->sec_ent_keyid[2]);
	FWCMD_SET_ADDR_SEC_ENT3_KEYID(cmd, addr_cam->sec_ent_keyid[3]);
	FWCMD_SET_ADDR_SEC_ENT4_KEYID(cmd, addr_cam->sec_ent_keyid[4]);
	FWCMD_SET_ADDR_SEC_ENT5_KEYID(cmd, addr_cam->sec_ent_keyid[5]);
	FWCMD_SET_ADDR_SEC_ENT6_KEYID(cmd, addr_cam->sec_ent_keyid[6]);

	FWCMD_SET_ADDR_SEC_ENT_VALID(cmd, addr_cam->sec_cam_map[0] & 0xff);
	FWCMD_SET_ADDR_SEC_ENT0(cmd, addr_cam->sec_ent[0]);
	FWCMD_SET_ADDR_SEC_ENT1(cmd, addr_cam->sec_ent[1]);
	FWCMD_SET_ADDR_SEC_ENT2(cmd, addr_cam->sec_ent[2]);
	FWCMD_SET_ADDR_SEC_ENT3(cmd, addr_cam->sec_ent[3]);
	FWCMD_SET_ADDR_SEC_ENT4(cmd, addr_cam->sec_ent[4]);
	FWCMD_SET_ADDR_SEC_ENT5(cmd, addr_cam->sec_ent[5]);
	FWCMD_SET_ADDR_SEC_ENT6(cmd, addr_cam->sec_ent[6]);

	rcu_read_unlock();
}

void rtw89_cam_fill_dctl_sec_cam_info_v1(struct rtw89_dev *rtwdev,
					 struct rtw89_vif_link *rtwvif_link,
					 struct rtw89_sta_link *rtwsta_link,
					 struct rtw89_h2c_dctlinfo_ud_v1 *h2c)
{
	struct rtw89_addr_cam_entry *addr_cam =
		rtw89_get_addr_cam_of(rtwvif_link, rtwsta_link);
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	u8 *ptk_tx_iv = rtw_wow->key_info.ptk_tx_iv;

	h2c->c0 = le32_encode_bits(rtwsta_link ? rtwsta_link->mac_id :
						 rtwvif_link->mac_id,
				   DCTLINFO_V1_C0_MACID) |
		  le32_encode_bits(1, DCTLINFO_V1_C0_OP);

	h2c->w4 = le32_encode_bits(addr_cam->sec_ent_keyid[0],
				   DCTLINFO_V1_W4_SEC_ENT0_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[1],
				   DCTLINFO_V1_W4_SEC_ENT1_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[2],
				   DCTLINFO_V1_W4_SEC_ENT2_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[3],
				   DCTLINFO_V1_W4_SEC_ENT3_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[4],
				   DCTLINFO_V1_W4_SEC_ENT4_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[5],
				   DCTLINFO_V1_W4_SEC_ENT5_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[6],
				   DCTLINFO_V1_W4_SEC_ENT6_KEYID);
	h2c->m4 = cpu_to_le32(DCTLINFO_V1_W4_SEC_ENT0_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT1_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT2_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT3_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT4_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT5_KEYID |
			      DCTLINFO_V1_W4_SEC_ENT6_KEYID);

	h2c->w5 = le32_encode_bits(addr_cam->sec_cam_map[0] & 0xff,
				   DCTLINFO_V1_W5_SEC_ENT_VALID) |
		  le32_encode_bits(addr_cam->sec_ent[0],
				   DCTLINFO_V1_W5_SEC_ENT0) |
		  le32_encode_bits(addr_cam->sec_ent[1],
				   DCTLINFO_V1_W5_SEC_ENT1) |
		  le32_encode_bits(addr_cam->sec_ent[2],
				   DCTLINFO_V1_W5_SEC_ENT2);
	h2c->m5 = cpu_to_le32(DCTLINFO_V1_W5_SEC_ENT_VALID |
			      DCTLINFO_V1_W5_SEC_ENT0      |
			      DCTLINFO_V1_W5_SEC_ENT1      |
			      DCTLINFO_V1_W5_SEC_ENT2);

	h2c->w6 = le32_encode_bits(addr_cam->sec_ent[3],
				   DCTLINFO_V1_W6_SEC_ENT3) |
		  le32_encode_bits(addr_cam->sec_ent[4],
				   DCTLINFO_V1_W6_SEC_ENT4) |
		  le32_encode_bits(addr_cam->sec_ent[5],
				   DCTLINFO_V1_W6_SEC_ENT5) |
		  le32_encode_bits(addr_cam->sec_ent[6],
				   DCTLINFO_V1_W6_SEC_ENT6);
	h2c->m6 = cpu_to_le32(DCTLINFO_V1_W6_SEC_ENT3 |
			      DCTLINFO_V1_W6_SEC_ENT4 |
			      DCTLINFO_V1_W6_SEC_ENT5 |
			      DCTLINFO_V1_W6_SEC_ENT6);

	if (rtw_wow->ptk_alg) {
		h2c->w0 = le32_encode_bits(ptk_tx_iv[0] | ptk_tx_iv[1] << 8,
					   DCTLINFO_V1_W0_AES_IV_L);
		h2c->m0 = cpu_to_le32(DCTLINFO_V1_W0_AES_IV_L);

		h2c->w1 = le32_encode_bits(ptk_tx_iv[4]       |
					   ptk_tx_iv[5] << 8  |
					   ptk_tx_iv[6] << 16 |
					   ptk_tx_iv[7] << 24,
					   DCTLINFO_V1_W1_AES_IV_H);
		h2c->m1 = cpu_to_le32(DCTLINFO_V1_W1_AES_IV_H);

		h2c->w4 |= le32_encode_bits(rtw_wow->ptk_keyidx,
					    DCTLINFO_V1_W4_SEC_KEY_ID);
		h2c->m4 |= cpu_to_le32(DCTLINFO_V1_W4_SEC_KEY_ID);
	}
}

void rtw89_cam_fill_dctl_sec_cam_info_v2(struct rtw89_dev *rtwdev,
					 struct rtw89_vif_link *rtwvif_link,
					 struct rtw89_sta_link *rtwsta_link,
					 struct rtw89_h2c_dctlinfo_ud_v2 *h2c)
{
	struct ieee80211_sta *sta = rtwsta_link_to_sta_safe(rtwsta_link);
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif_link->rtwvif);
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_addr_cam_entry *addr_cam =
		rtw89_get_addr_cam_of(rtwvif_link, rtwsta_link);
	bool is_mld = sta ? sta->mlo : ieee80211_vif_is_mld(vif);
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	u8 *ptk_tx_iv = rtw_wow->key_info.ptk_tx_iv;
	u8 *mld_sma, *mld_tma, *mld_bssid;

	h2c->c0 = le32_encode_bits(rtwsta_link ? rtwsta_link->mac_id :
						 rtwvif_link->mac_id,
				   DCTLINFO_V2_C0_MACID) |
		  le32_encode_bits(1, DCTLINFO_V2_C0_OP);

	h2c->w2 = le32_encode_bits(is_mld, DCTLINFO_V2_W2_IS_MLD);
	h2c->m2 = cpu_to_le32(DCTLINFO_V2_W2_IS_MLD);

	h2c->w4 = le32_encode_bits(addr_cam->sec_ent_keyid[0],
				   DCTLINFO_V2_W4_SEC_ENT0_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[1],
				   DCTLINFO_V2_W4_SEC_ENT1_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[2],
				   DCTLINFO_V2_W4_SEC_ENT2_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[3],
				   DCTLINFO_V2_W4_SEC_ENT3_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[4],
				   DCTLINFO_V2_W4_SEC_ENT4_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[5],
				   DCTLINFO_V2_W4_SEC_ENT5_KEYID) |
		  le32_encode_bits(addr_cam->sec_ent_keyid[6],
				   DCTLINFO_V2_W4_SEC_ENT6_KEYID);
	h2c->m4 = cpu_to_le32(DCTLINFO_V2_W4_SEC_ENT0_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT1_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT2_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT3_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT4_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT5_KEYID |
			      DCTLINFO_V2_W4_SEC_ENT6_KEYID);

	h2c->w5 = le32_encode_bits(addr_cam->sec_cam_map[0],
				   DCTLINFO_V2_W5_SEC_ENT_VALID_V1) |
		  le32_encode_bits(addr_cam->sec_ent[0],
				   DCTLINFO_V2_W5_SEC_ENT0_V1);
	h2c->m5 = cpu_to_le32(DCTLINFO_V2_W5_SEC_ENT_VALID_V1 |
			      DCTLINFO_V2_W5_SEC_ENT0_V1);

	h2c->w6 = le32_encode_bits(addr_cam->sec_ent[1],
				   DCTLINFO_V2_W6_SEC_ENT1_V1) |
		  le32_encode_bits(addr_cam->sec_ent[2],
				   DCTLINFO_V2_W6_SEC_ENT2_V1) |
		  le32_encode_bits(addr_cam->sec_ent[3],
				   DCTLINFO_V2_W6_SEC_ENT3_V1) |
		  le32_encode_bits(addr_cam->sec_ent[4],
				   DCTLINFO_V2_W6_SEC_ENT4_V1);
	h2c->m6 = cpu_to_le32(DCTLINFO_V2_W6_SEC_ENT1_V1 |
			      DCTLINFO_V2_W6_SEC_ENT2_V1 |
			      DCTLINFO_V2_W6_SEC_ENT3_V1 |
			      DCTLINFO_V2_W6_SEC_ENT4_V1);

	h2c->w7 = le32_encode_bits(addr_cam->sec_ent[5],
				   DCTLINFO_V2_W7_SEC_ENT5_V1) |
		  le32_encode_bits(addr_cam->sec_ent[6],
				   DCTLINFO_V2_W7_SEC_ENT6_V1);
	h2c->m7 = cpu_to_le32(DCTLINFO_V2_W7_SEC_ENT5_V1 |
			      DCTLINFO_V2_W7_SEC_ENT6_V1);

	if (rtw_wow->ptk_alg) {
		h2c->w0 = le32_encode_bits(ptk_tx_iv[0] | ptk_tx_iv[1] << 8,
					   DCTLINFO_V2_W0_AES_IV_L);
		h2c->m0 = cpu_to_le32(DCTLINFO_V2_W0_AES_IV_L);

		h2c->w1 = le32_encode_bits(ptk_tx_iv[4] |
					   ptk_tx_iv[5] << 8 |
					   ptk_tx_iv[6] << 16 |
					   ptk_tx_iv[7] << 24,
					   DCTLINFO_V2_W1_AES_IV_H);
		h2c->m1 = cpu_to_le32(DCTLINFO_V2_W1_AES_IV_H);

		h2c->w4 |= le32_encode_bits(rtw_wow->ptk_keyidx,
					    DCTLINFO_V2_W4_SEC_KEY_ID);
		h2c->m4 |= cpu_to_le32(DCTLINFO_V2_W4_SEC_KEY_ID);
	}

	if (!is_mld)
		return;

	if (rtwvif_link->net_type == RTW89_NET_TYPE_INFRA) {
		mld_sma = rtwvif->mac_addr;
		mld_tma = vif->cfg.ap_addr;
		mld_bssid = vif->cfg.ap_addr;
	} else if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE && sta) {
		mld_sma = rtwvif->mac_addr;
		mld_tma = sta->addr;
		mld_bssid = rtwvif->mac_addr;
	} else {
		return;
	}

	h2c->w8 = le32_encode_bits(mld_sma[0], DCTLINFO_V2_W8_MLD_SMA_0) |
		  le32_encode_bits(mld_sma[1], DCTLINFO_V2_W8_MLD_SMA_1) |
		  le32_encode_bits(mld_sma[2], DCTLINFO_V2_W8_MLD_SMA_2) |
		  le32_encode_bits(mld_sma[3], DCTLINFO_V2_W8_MLD_SMA_3);
	h2c->m8 = cpu_to_le32(DCTLINFO_V2_W8_ALL);

	h2c->w9 = le32_encode_bits(mld_sma[4], DCTLINFO_V2_W9_MLD_SMA_4) |
		  le32_encode_bits(mld_sma[5], DCTLINFO_V2_W9_MLD_SMA_5) |
		  le32_encode_bits(mld_tma[0], DCTLINFO_V2_W9_MLD_TMA_0) |
		  le32_encode_bits(mld_tma[1], DCTLINFO_V2_W9_MLD_TMA_1);
	h2c->m9 = cpu_to_le32(DCTLINFO_V2_W9_ALL);

	h2c->w10 = le32_encode_bits(mld_tma[2], DCTLINFO_V2_W10_MLD_TMA_2) |
		   le32_encode_bits(mld_tma[3], DCTLINFO_V2_W10_MLD_TMA_3) |
		   le32_encode_bits(mld_tma[4], DCTLINFO_V2_W10_MLD_TMA_4) |
		   le32_encode_bits(mld_tma[5], DCTLINFO_V2_W10_MLD_TMA_5);
	h2c->m10 = cpu_to_le32(DCTLINFO_V2_W10_ALL);

	h2c->w11 = le32_encode_bits(mld_bssid[0], DCTLINFO_V2_W11_MLD_BSSID_0) |
		   le32_encode_bits(mld_bssid[1], DCTLINFO_V2_W11_MLD_BSSID_1) |
		   le32_encode_bits(mld_bssid[2], DCTLINFO_V2_W11_MLD_BSSID_2) |
		   le32_encode_bits(mld_bssid[3], DCTLINFO_V2_W11_MLD_BSSID_3);
	h2c->m11 = cpu_to_le32(DCTLINFO_V2_W11_ALL);

	h2c->w12 = le32_encode_bits(mld_bssid[4], DCTLINFO_V2_W12_MLD_BSSID_4) |
		   le32_encode_bits(mld_bssid[5], DCTLINFO_V2_W12_MLD_BSSID_5);
	h2c->m12 = cpu_to_le32(DCTLINFO_V2_W12_ALL);
}
