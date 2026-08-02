// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2025 MediaTek Inc. */

#include "mt7925.h"
#include "regd.h"
#include "mcu.h"

static bool mt7925_disable_clc;
module_param_named(disable_clc, mt7925_disable_clc, bool, 0644);
MODULE_PARM_DESC(disable_clc, "disable CLC support");

static struct ieee80211_regdomain mt7925_regd_ww = {
	.n_reg_rules = 1,
	.alpha2 =  "00",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412 - 10, 2462 + 10, 40, 6, 20, 0),
	}
};

bool mt7925_regd_clc_supported(struct mt792x_dev *dev)
{
	if (mt7925_disable_clc ||
	    mt76_is_usb(&dev->mt76))
		return false;

	return true;
}

void mt7925_regd_be_ctrl(struct mt792x_dev *dev, u8 *alpha2)
{
	struct mt792x_phy *phy = &dev->phy;
	struct mt7925_clc_rule_v2 *rule;
	struct mt7925_clc *clc;
	bool old = dev->has_eht, new = true;
	u32 mtcl_conf = mt792x_acpi_get_mtcl_conf(&dev->phy, alpha2);
	u8 *pos;

	if (mtcl_conf != MT792X_ACPI_MTCL_INVALID &&
	    (((mtcl_conf >> 4) & 0x3) == 0)) {
		new = false;
		goto out;
	}

	if (!phy->clc[MT792x_CLC_BE_CTRL])
		goto out;

	clc = (struct mt7925_clc *)phy->clc[MT792x_CLC_BE_CTRL];
	pos = clc->data;

	while (1) {
		rule = (struct mt7925_clc_rule_v2 *)pos;

		if (rule->alpha2[0] == alpha2[0] &&
		    rule->alpha2[1] == alpha2[1]) {
			new = false;
			break;
		}

		/* Check the last one */
		if (rule->flag & BIT(0))
			break;

		pos += sizeof(*rule);
	}

out:
	if (old == new)
		return;

	dev->has_eht = new;
	mt7925_set_stream_he_eht_caps(phy);
}

static void
mt7925_regd_channel_update(struct wiphy *wiphy, struct mt792x_dev *dev)
{
#define IS_UNII_INVALID(idx, sfreq, efreq, cfreq) \
	(!(dev->phy.clc_chan_conf & BIT(idx)) && (cfreq) >= (sfreq) && (cfreq) <= (efreq))
#define MT7925_UNII_59G_IS_VALID	0x1
#define MT7925_UNII_6G_IS_VALID	0x1e
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_channel *ch;
	u32 mtcl_conf = mt792x_acpi_get_mtcl_conf(&dev->phy, mdev->alpha2);
	int i;

	if (mtcl_conf != MT792X_ACPI_MTCL_INVALID) {
		if ((mtcl_conf & 0x3) == 0)
			dev->phy.clc_chan_conf &= ~MT7925_UNII_59G_IS_VALID;
		if (((mtcl_conf >> 2) & 0x3) == 0)
			dev->phy.clc_chan_conf &= ~MT7925_UNII_6G_IS_VALID;
	}

	sband = wiphy->bands[NL80211_BAND_2GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		if (!dev->has_eht)
			ch->flags |= IEEE80211_CHAN_NO_EHT;
	}

	sband = wiphy->bands[NL80211_BAND_5GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		/* UNII-4 */
		if (IS_UNII_INVALID(0, 5845, 5925, ch->center_freq))
			ch->flags |= IEEE80211_CHAN_DISABLED;

		if (!dev->has_eht)
			ch->flags |= IEEE80211_CHAN_NO_EHT;
	}

	sband = wiphy->bands[NL80211_BAND_6GHZ];
	if (!sband)
		return;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		/* UNII-5/6/7/8 */
		if (IS_UNII_INVALID(1, 5925, 6425, ch->center_freq) ||
		    IS_UNII_INVALID(2, 6425, 6525, ch->center_freq) ||
		    IS_UNII_INVALID(3, 6525, 6875, ch->center_freq) ||
		    IS_UNII_INVALID(4, 6875, 7125, ch->center_freq))
			ch->flags |= IEEE80211_CHAN_DISABLED;

		if (!dev->has_eht)
			ch->flags |= IEEE80211_CHAN_NO_EHT;
	}
}

static int mt7925_mcu_apply_regd(struct mt792x_dev *dev, u8 *alpha2,
				  enum environment_cap env)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	ret = mt7925_mcu_set_clc(dev, alpha2, env);
	if (ret < 0)
		return ret;

	mt7925_regd_be_ctrl(dev, alpha2);
	mt7925_regd_channel_update(wiphy, dev);

	ret = mt7925_mcu_set_channel_domain(hw->priv);
	if (ret < 0)
		return ret;

	return mt7925_set_tx_sar_pwr(hw, NULL);
}

int mt7925_mcu_regd_update(struct mt792x_dev *dev, u8 *alpha2,
			   enum environment_cap country_ie_env)
{
	int ret = 0;

	dev->regd_in_progress = true;

	mt792x_mutex_acquire(dev);
	if (dev->regd_change)
		ret = mt7925_mcu_apply_regd(dev, alpha2, country_ie_env);
	mt792x_mutex_release(dev);
	dev->regd_change = false;
	dev->regd_in_progress = false;
	wake_up(&dev->wait);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7925_mcu_regd_update);

void mt7925_regd_notifier(struct wiphy *wiphy, struct regulatory_request *req)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_dev *mdev = &dev->mt76;

	if (req->initiator == NL80211_REGDOM_SET_BY_USER &&
	    !dev->regd_user)
		dev->regd_user = true;

	/* allow world regdom at the first boot only */
	if (!memcmp(req->alpha2, "00", 2) &&
	    mdev->alpha2[0] && mdev->alpha2[1])
		return;

	/* do not need to update the same country twice */
	if (!memcmp(req->alpha2, mdev->alpha2, 2) &&
	    dev->country_ie_env == req->country_ie_env)
		return;

	memcpy(mdev->alpha2, req->alpha2, 2);
	mdev->region = req->dfs_region;
	dev->country_ie_env = req->country_ie_env;

	dev->regd_change = true;

	if (pm->suspended)
		/* postpone the mcu update to resume */
		return;

	if (MT7925_REGD_SUPPORTED(&dev->phy)) {
		mt7925_regd_update(&dev->phy, req->alpha2);

		return;
	}

	mt7925_mcu_regd_update(dev, req->alpha2,
			       req->country_ie_env);
	return;
}

static struct sk_buff *
mt7925_regd_query_regdb(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt7925_clc *clc = phy->clc[MT792x_CLC_REGD];
	struct mt7925_regd_query_req *req;
	struct mt7925_regd_cc *regd_cc;
	struct sk_buff *ret_skb = NULL;
	u8 *pos, *last_pos;
	int ret = 0;

	if (!clc)
		return NULL;

	pos = clc->data;
	last_pos = pos + le32_to_cpu(clc->len) - sizeof(struct mt7925_clc);
	while (pos < last_pos) {
		u32 req_len = 0;
		u32 rules_len = 0;
		u32 sign_len = 4;
		u32 n_reg_rules;

		if (pos + sizeof(*regd_cc) > last_pos)
			break;

		regd_cc = (struct mt7925_regd_cc *)pos;
		n_reg_rules = le32_to_cpu(regd_cc->n_reg_rules);
		if (n_reg_rules > NL80211_MAX_SUPP_REG_RULES)
			break;

		rules_len = sizeof(struct mt7925_regd_rule_header) +
			    sizeof(struct mt7925_regd_rule) * n_reg_rules;

		if (pos + sizeof(*regd_cc) + rules_len + sign_len > last_pos)
			break;

		pos += sizeof(*regd_cc) + rules_len + sign_len;
		if (memcmp(regd_cc->alpha2, alpha2, 2))
			continue;

		req_len = sizeof(*req) + rules_len + sign_len;
		req = kzalloc(req_len, GFP_KERNEL);

		if (!req)
			return NULL;

		req->tag = cpu_to_le16(0x6);
		req->len = cpu_to_le16(req_len - 4);
		req->ver = regd_cc->ver;
		req->sign_type = regd_cc->sign_type;
		req->size = cpu_to_le32(rules_len + sign_len);
		req->n_reg_rules = regd_cc->n_reg_rules;

		memcpy(req->alpha2, regd_cc->alpha2, 2);
		memcpy(req->data, regd_cc->data, rules_len + sign_len);

		ret = mt76_mcu_send_and_get_msg(&dev->mt76,
						MCU_UNI_CMD(SET_POWER_LIMIT),
						req, req_len, true, &ret_skb);
		kfree(req);

		return ret < 0 ? NULL : ret_skb;
	}

	return NULL;
}

int mt7925_regd_update(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt7925_regd_rule *mt7925_rule;
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_regdomain *regd;
	struct ieee80211_reg_rule *rule;
	struct mt7925_regd_rule_ev *ev;
	int i, num_of_rules = 0;
	struct sk_buff *skb;
	int ret = 0;

	if (dev->hw_full_reset)
		return 0;

	if (!MT7925_REGD_SUPPORTED(phy))
		return -EOPNOTSUPP;

	mt792x_mutex_acquire(dev);
	skb = mt7925_regd_query_regdb(phy, alpha2);
	mt792x_mutex_release(dev);

	if (!skb) {
		ret = -EINVAL;
		goto err;
	}

	if (skb->len < sizeof(*ev) + 4) {
		ret = -EINVAL;
		goto err;
	}

	ev = (struct mt7925_regd_rule_ev *)(skb->data + 4);
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
		mt7925_rule = &ev->reg_rule[i];
		rule = &regd->reg_rules[i];

		rule->freq_range.start_freq_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7925_rule->start_freq));
		rule->freq_range.end_freq_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7925_rule->end_freq));
		rule->freq_range.max_bandwidth_khz =
			MHZ_TO_KHZ(le32_to_cpu(mt7925_rule->max_bw));
		/* not used by fw */
		rule->power_rule.max_antenna_gain = DBI_TO_MBI(6);
		rule->power_rule.max_eirp = DBM_TO_MBM(22);
		rule->flags = le32_to_cpu(mt7925_rule->flags);
	}

	regd->n_reg_rules = num_of_rules;
	regd->dfs_region = ev->dfs_region;

	memcpy(regd->alpha2, alpha2, 2);
	memcpy(mdev->alpha2, alpha2, 2);

	dev->regd_change = true;
	mt7925_mcu_regd_update(dev, alpha2, ENVIRON_ANY);

	ret = regulatory_set_wiphy_regd(wiphy, regd);

	kfree(regd);
err:
	dev_kfree_skb(skb);

	if (ret < 0)
		return regulatory_set_wiphy_regd(wiphy, &mt7925_regd_ww);

	return ret;
}
EXPORT_SYMBOL_GPL(mt7925_regd_update);

static bool
mt7925_regd_is_valid_alpha2(const char *alpha2)
{
	if (!alpha2)
		return false;

	if (alpha2[0] == '0' && alpha2[1] == '0')
		return true;

	if (isalpha(alpha2[0]) && isalpha(alpha2[1]))
		return true;

	return false;
}

bool
mt7925_regd_is_valid_channel(struct mt792x_dev *dev,
			     enum nl80211_band band,
			     struct ieee80211_channel *chan)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int i;

	if (!chan)
		return false;

	sband = wiphy->bands[band];
	if (!sband)
		return false;

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		if (ch->hw_value == chan->hw_value &&
		    ((ch->flags & IEEE80211_CHAN_DISABLED) == 0))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(mt7925_regd_is_valid_channel);

int mt7925_regd_change(struct mt792x_phy *phy, char *alpha2)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	if (dev->hw_full_reset)
		return 0;

	if (!mt7925_regd_is_valid_alpha2(alpha2) ||
	    !mt7925_regd_clc_supported(dev) ||
	    dev->regd_user)
		return -EINVAL;

	if ((mdev->alpha2[0] && mdev->alpha2[0] != '0') &&
	    (mdev->alpha2[1] && mdev->alpha2[1] != '0'))
		return 0;

	/* do not need to update the same country twice */
	if (!memcmp(alpha2, mdev->alpha2, 2))
		return 0;

	if (MT7925_REGD_SUPPORTED(phy)) {
		return mt7925_regd_update(phy, alpha2);
	} else if (phy->chip_cap & MT792x_CHIP_CAP_11D_EN) {
		return regulatory_hint(wiphy, alpha2);
	} else {
		return mt7925_mcu_set_clc(dev, alpha2, ENVIRON_INDOOR);
	}
}
EXPORT_SYMBOL_GPL(mt7925_regd_change);

int mt7925_regd_init(struct mt792x_phy *phy)
{
	struct wiphy *wiphy = phy->mt76->hw->wiphy;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	if (MT7925_REGD_SUPPORTED(phy)) {
		wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED |
					   REGULATORY_DISABLE_BEACON_HINTS;
		return mt7925_regd_update(phy, "00");
	} else if (phy->chip_cap & MT792x_CHIP_CAP_11D_EN) {
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE |
					   REGULATORY_DISABLE_BEACON_HINTS;
	} else {
		memzero_explicit(&mdev->alpha2, sizeof(mdev->alpha2));
	}

	return 0;
}
