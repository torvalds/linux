// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2025 MediaTek Inc. */

#include <linux/of.h>
#include "mt7921.h"
#include "regd.h"
#include "mcu.h"

static bool mt7921_disable_clc;
module_param_named(disable_clc, mt7921_disable_clc, bool, 0644);
MODULE_PARM_DESC(disable_clc, "disable CLC support");

static struct ieee80211_regdomain mt7921_regd_ww = {
	.n_reg_rules = 1,
	.alpha2 =  "00",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412 - 10, 2462 + 10, 40, 6, 20, 0),
	}
};

bool mt7921_regd_clc_supported(struct mt792x_dev *dev)
{
	if (mt7921_disable_clc ||
	    mt76_is_usb(&dev->mt76))
		return false;

	return true;
}

static void
mt7921_regd_channel_update(struct wiphy *wiphy, struct mt792x_dev *dev)
{
#define IS_UNII_INVALID(idx, sfreq, efreq) \
	(!(dev->phy.clc_chan_conf & BIT(idx)) && (cfreq) >= (sfreq) && (cfreq) <= (efreq))
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct device_node *np, *band_np;
	struct ieee80211_channel *ch;
	int i, cfreq;

	np = mt76_find_power_limits_node(mdev);

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	band_np = np ? of_get_child_by_name(np, "txpower-5g") : NULL;
	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		cfreq = ch->center_freq;

		if (np && (!band_np || !mt76_find_channel_node(band_np, ch))) {
			ch->flags |= IEEE80211_CHAN_DISABLED;
			continue;
		}

		/* UNII-4 */
		if (IS_UNII_INVALID(0, 5845, 5925))
			ch->flags |= IEEE80211_CHAN_DISABLED;
	}

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	band_np = np ? of_get_child_by_name(np, "txpower-6g") : NULL;
	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];
		cfreq = ch->center_freq;

		if (np && (!band_np || !mt76_find_channel_node(band_np, ch))) {
			ch->flags |= IEEE80211_CHAN_DISABLED;
			continue;
		}

		/* UNII-5/6/7/8 */
		if (IS_UNII_INVALID(1, 5925, 6425) ||
		    IS_UNII_INVALID(2, 6425, 6525) ||
		    IS_UNII_INVALID(3, 6525, 6875) ||
		    IS_UNII_INVALID(4, 6875, 7125))
			ch->flags |= IEEE80211_CHAN_DISABLED;
	}
}

int __mt7921_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			     enum environment_cap country_ie_env)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_hw *hw = mdev->hw;
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	lockdep_assert_held(&dev->mt76.mutex);

	if (!dev->regd_change)
		return 0;

	ret = mt7921_mcu_set_clc(dev, alpha2, country_ie_env);
	if (ret < 0)
		return ret;

	mt7921_regd_channel_update(wiphy, dev);

	ret = mt76_connac_mcu_set_channel_domain(hw->priv);
	if (ret < 0)
		return ret;

	ret = mt7921_set_tx_sar_pwr(hw, NULL);

	return ret;
}

int mt7921_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env)
{
	int ret;

	dev->regd_in_progress = true;

	mt792x_mutex_acquire(dev);
	ret = __mt7921_mcu_regd_update(dev, alpha2, country_ie_env);
	mt792x_mutex_release(dev);

	dev->regd_change = false;
	dev->regd_in_progress = false;
	wake_up(&dev->wait);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7921_mcu_regd_update);

void mt7921_regd_notifier(struct wiphy *wiphy,
			  struct regulatory_request *req)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_dev *mdev = &dev->mt76;

	if (req->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !dev->regd_user)
		dev->regd_user = true;

	/* do not need to update the same country twice */
	if (!memcmp(req->alpha2, mdev->alpha2, 2) &&
	    dev->country_ie_env == req->country_ie_env)
		return;

	memcpy(mdev->alpha2, req->alpha2, 2);
	mdev->region = req->dfs_region;
	dev->country_ie_env = req->country_ie_env;

	if (req->initiator == NL80211_REGDOM_SET_BY_USER) {
		if (dev->mt76.alpha2[0] == '0' && dev->mt76.alpha2[1] == '0')
			wiphy->regulatory_flags &= ~REGULATORY_COUNTRY_IE_IGNORE;
		else
			wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	}

	dev->regd_change = true;

	if (pm->suspended)
		return;

	if (MT7921_REGD_SUPPORTED(&dev->phy)) {
		mt7921_regd_update(&dev->phy, req->alpha2);

		return;
	}

	mt7921_mcu_regd_update(dev, req->alpha2,
			       req->country_ie_env);
}

static struct sk_buff *
mt7921_regd_query_regdb(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt7921_clc *clc = phy->clc[MT792x_CLC_REGD];
	struct mt7921_regd_query_req *req;
	struct mt7921_regd_cc *regd_cc;
	struct sk_buff *ret_skb = NULL;
	u8 *pos, *last_pos;
	int ret = 0;

	if (!clc)
		return NULL;

	pos = clc->data;
	last_pos = pos + le32_to_cpu(clc->len) - sizeof(struct mt7921_clc);
	while (pos < last_pos) {
		u32 req_len = 0;
		u32 rules_len = 0;
		u32 sign_len = 4;
		u32 n_reg_rules;

		if (pos + sizeof(*regd_cc) > last_pos)
			break;

		regd_cc = (struct mt7921_regd_cc *)pos;
		n_reg_rules = le32_to_cpu(regd_cc->n_reg_rules);
		if (n_reg_rules > NL80211_MAX_SUPP_REG_RULES)
			break;

		rules_len = sizeof(struct mt7921_regd_rule_header) +
			    sizeof(struct mt7921_regd_rule) * n_reg_rules;

		if (pos + sizeof(*regd_cc) + rules_len + sign_len > last_pos)
			break;

		pos += sizeof(*regd_cc) + rules_len + sign_len;
		if (memcmp(regd_cc->alpha2, alpha2, 2))
			continue;

		req_len = sizeof(*req) + rules_len + sign_len;
		req = kzalloc(req_len, GFP_KERNEL);

		if (!req)
			return NULL;

		req->ver = regd_cc->ver;
		req->sign_type = regd_cc->sign_type;
		req->size = cpu_to_le32(rules_len + sign_len);
		req->n_reg_rules = regd_cc->n_reg_rules;

		memcpy(req->alpha2, regd_cc->alpha2, 2);
		memcpy(req->data, regd_cc->data, rules_len + sign_len);

		ret = mt76_mcu_send_and_get_msg(&dev->mt76,
						MCU_CE_CMD(SET_REGD_CH),
						req, req_len, true, &ret_skb);

		kfree(req);

		return ret < 0 ? NULL : ret_skb;
	}

	return NULL;
}

int mt7921_regd_update(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt7921_regd_rule *mt7921_rule;
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_regdomain *regd;
	struct ieee80211_reg_rule *rule;
	struct mt7921_regd_rule_ev *ev;
	int i, num_of_rules = 0;
	struct sk_buff *skb;
	int ret = 0;

	if (dev->hw_full_reset)
		return 0;

	if (!MT7921_REGD_SUPPORTED(phy))
		return -EOPNOTSUPP;

	mt792x_mutex_acquire(dev);
	skb = mt7921_regd_query_regdb(phy, alpha2);
	mt792x_mutex_release(dev);

	if (!skb) {
		ret = -EINVAL;
		goto err;
	}

	if (skb->len < sizeof(*ev) + 4) {
		ret = -EINVAL;
		goto err;
	}

	ev = (struct mt7921_regd_rule_ev *)(skb->data + 4);
	num_of_rules = le32_to_cpu(ev->n_reg_rules);

	if (!num_of_rules ||
	    WARN_ON_ONCE(num_of_rules > NL80211_MAX_SUPP_REG_RULES)) {
		ret = -EINVAL;
		goto err;
	}

	if (skb->len < struct_size(ev, reg_rule, num_of_rules) + 4) {
		ret = -EINVAL;
		goto err;
	}

	regd = kzalloc(struct_size(regd, reg_rules, num_of_rules), GFP_KERNEL);
	if (!regd) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < num_of_rules; i++) {
		mt7921_rule = &ev->reg_rule[i];
		rule = &regd->reg_rules[i];

		rule->freq_range.start_freq_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7921_rule->start_freq));
		rule->freq_range.end_freq_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7921_rule->end_freq));
		rule->freq_range.max_bandwidth_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7921_rule->max_bw));
		/* not used by fw */
		rule->power_rule.max_antenna_gain = DBI_TO_MBI(6);
		rule->power_rule.max_eirp = DBM_TO_MBM(22);
		rule->flags = le32_to_cpu(mt7921_rule->flags);
	}

	regd->n_reg_rules = num_of_rules;
	regd->dfs_region = ev->dfs_region;

	memcpy(regd->alpha2, alpha2, 2);
	memcpy(mdev->alpha2, alpha2, 2);

	dev->regd_change = true;
	mt7921_mcu_regd_update(dev, alpha2, ENVIRON_ANY);

	ret = regulatory_set_wiphy_regd(wiphy, regd);

	kfree(regd);
err:
	dev_kfree_skb(skb);

	if (ret < 0)
		return regulatory_set_wiphy_regd(wiphy, &mt7921_regd_ww);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7921_regd_update);

static bool
mt7921_regd_is_valid_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;

	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;

	if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
		return true;

	return false;
}

int mt7921_regd_change(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	if (dev->hw_full_reset)
		return 0;

	if (!mt7921_regd_is_valid_alpha2(alpha2) ||
	    !mt7921_regd_clc_supported(dev) ||
	    dev->regd_user)
		return -EINVAL;

	if (mdev->alpha2[0] != '0' && mdev->alpha2[1] != '0')
		return 0;

	/* do not need to update the same country twice */
	if (!memcmp(alpha2, mdev->alpha2, 2))
		return 0;

	if (MT7921_REGD_SUPPORTED(phy))
		return mt7921_regd_update(phy, alpha2);
	else if (phy->chip_cap & MT792x_CHIP_CAP_11D_EN)
		return regulatory_hint(wiphy, alpha2);
	else
		return mt7921_mcu_set_clc(dev, alpha2, ENVIRON_INDOOR);
}

int mt7921_regd_init(struct mt792x_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	if (MT7921_REGD_SUPPORTED(phy)) {
		wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED |
					   REGULATORY_DISABLE_BEACON_HINTS;
		return mt7921_regd_update(phy, "00");
	} else if (phy->chip_cap & MT792x_CHIP_CAP_11D_EN)
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE |
					   REGULATORY_DISABLE_BEACON_HINTS;
	else
		memzero_explicit(&mdev->alpha2, sizeof(mdev->alpha2));

	return 0;
}
