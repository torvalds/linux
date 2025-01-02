// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 Felix Fietkau <nbd@nbd.name>
 */
#include "mt76.h"

static int
mt76_phy_update_channel(struct mt76_phy *phy,
			struct ieee80211_chanctx_conf *conf)
{
	phy->radar_enabled = conf->radar_enabled;
	phy->main_chandef = conf->def;
	phy->chanctx = (struct mt76_chanctx *)conf->drv_priv;

	return __mt76_set_channel(phy, &phy->main_chandef, false);
}

int mt76_add_chanctx(struct ieee80211_hw *hw,
		     struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	int ret = -EINVAL;

	phy = ctx->phy = dev->band_phys[conf->def.chan->band];
	if (WARN_ON_ONCE(!phy))
		return ret;

	if (dev->scan.phy == phy)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);
	if (!phy->chanctx)
		ret = mt76_phy_update_channel(phy, conf);
	else
		ret = 0;
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_add_chanctx);

void mt76_remove_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;

	phy = ctx->phy;
	if (WARN_ON_ONCE(!phy))
		return;

	if (dev->scan.phy == phy)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);
	if (phy->chanctx == ctx)
		phy->chanctx = NULL;
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_remove_chanctx);

void mt76_change_chanctx(struct ieee80211_hw *hw,
			 struct ieee80211_chanctx_conf *conf,
			 u32 changed)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;

	if (!(changed & (IEEE80211_CHANCTX_CHANGE_WIDTH |
			 IEEE80211_CHANCTX_CHANGE_RADAR)))
		return;

	cancel_delayed_work_sync(&phy->mac_work);

	mutex_lock(&dev->mutex);
	mt76_phy_update_channel(phy, conf);
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_change_chanctx);


int mt76_assign_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	int link_id = link_conf->link_id;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;
	bool mlink_alloc = false;
	int ret = 0;

	if (dev->scan.vif == vif)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = mt76_vif_conf_link(dev, vif, link_conf);
	if (!mlink) {
		mlink = kzalloc(dev->drv->link_data_size, GFP_KERNEL);
		if (!mlink) {
			ret = -ENOMEM;
			goto out;
		}
		mlink_alloc = true;
	}

	mlink->ctx = conf;
	ret = dev->drv->vif_link_add(phy, vif, link_conf, mlink);
	if (ret) {
		if (mlink_alloc)
			kfree(mlink);
		goto out;
	}

	if (link_conf != &vif->bss_conf)
		rcu_assign_pointer(mvif->link[link_id], mlink);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_assign_vif_chanctx);

void mt76_unassign_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf,
			       struct ieee80211_chanctx_conf *conf)
{
	struct mt76_chanctx *ctx = (struct mt76_chanctx *)conf->drv_priv;
	struct mt76_vif_link *mlink = (struct mt76_vif_link *)vif->drv_priv;
	struct mt76_vif_data *mvif = mlink->mvif;
	int link_id = link_conf->link_id;
	struct mt76_phy *phy = ctx->phy;
	struct mt76_dev *dev = phy->dev;

	if (dev->scan.vif == vif)
		mt76_abort_scan(dev);

	mutex_lock(&dev->mutex);

	if (vif->type == NL80211_IFTYPE_MONITOR &&
	    is_zero_ether_addr(vif->addr))
		goto out;

	mlink = mt76_vif_conf_link(dev, vif, link_conf);
	if (!mlink)
		goto out;

	if (link_conf != &vif->bss_conf)
		rcu_assign_pointer(mvif->link[link_id], NULL);

	dev->drv->vif_link_remove(phy, vif, link_conf, mlink);
	mlink->ctx = NULL;

	if (link_conf != &vif->bss_conf)
		kfree_rcu(mlink, rcu_head);

out:
	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76_unassign_vif_chanctx);

int mt76_switch_vif_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_vif_chanctx_switch *vifs,
			    int n_vifs,
			    enum ieee80211_chanctx_switch_mode mode)
{
	struct mt76_chanctx *old_ctx = (struct mt76_chanctx *)vifs->old_ctx->drv_priv;
	struct mt76_chanctx *new_ctx = (struct mt76_chanctx *)vifs->new_ctx->drv_priv;
	struct ieee80211_chanctx_conf *conf = vifs->new_ctx;
	struct mt76_phy *old_phy = old_ctx->phy;
	struct mt76_phy *phy = hw->priv;
	struct mt76_dev *dev = phy->dev;
	struct mt76_vif_link *mlink;
	bool update_chan;
	int i, ret = 0;

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS)
		phy = new_ctx->phy = dev->band_phys[conf->def.chan->band];
	else
		phy = new_ctx->phy;
	if (!phy)
		return -EINVAL;

	update_chan = phy->chanctx != new_ctx;
	if (update_chan) {
		if (dev->scan.phy == phy)
			mt76_abort_scan(dev);

		cancel_delayed_work_sync(&phy->mac_work);
	}

	mutex_lock(&dev->mutex);

	if (mode == CHANCTX_SWMODE_SWAP_CONTEXTS &&
	    phy != old_phy && old_phy->chanctx == old_ctx)
		old_phy->chanctx = NULL;

	if (update_chan)
		ret = mt76_phy_update_channel(phy, vifs->new_ctx);

	if (ret)
		goto out;

	if (old_phy == phy)
		goto skip_link_replace;

	for (i = 0; i < n_vifs; i++) {
		mlink = mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;

		dev->drv->vif_link_remove(old_phy, vifs[i].vif,
					  vifs[i].link_conf, mlink);

		ret = dev->drv->vif_link_add(phy, vifs[i].vif,
					     vifs[i].link_conf, mlink);
		if (ret)
			goto out;

	}

skip_link_replace:
	for (i = 0; i < n_vifs; i++) {
		mlink = mt76_vif_conf_link(dev, vifs[i].vif, vifs[i].link_conf);
		if (!mlink)
			continue;

		mlink->ctx = vifs->new_ctx;
	}

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76_switch_vif_chanctx);
