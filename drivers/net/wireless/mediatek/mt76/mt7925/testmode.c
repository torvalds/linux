// SPDX-License-Identifier: ISC

#include "mt7925.h"
#include "mcu.h"

#define MT7925_EVT_RSP_LEN 512

enum mt7925_testmode_attr {
	MT7925_TM_ATTR_UNSPEC,
	MT7925_TM_ATTR_SET,
	MT7925_TM_ATTR_QUERY,
	MT7925_TM_ATTR_RSP,

	/* keep last */
	NUM_MT7925_TM_ATTRS,
	MT7925_TM_ATTR_MAX = NUM_MT7925_TM_ATTRS - 1,
};

struct mt7925_tm_cmd {
	u8 padding[4];
	struct uni_cmd_testmode_ctrl c;
} __packed;

struct mt7925_tm_evt {
	u32 param0;
	u32 param1;
} __packed;

static const struct nla_policy mt7925_tm_policy[NUM_MT7925_TM_ATTRS] = {
	[MT7925_TM_ATTR_SET] = NLA_POLICY_EXACT_LEN(sizeof(struct mt7925_tm_cmd)),
	[MT7925_TM_ATTR_QUERY] = NLA_POLICY_EXACT_LEN(sizeof(struct mt7925_tm_cmd)),
};

static int
mt7925_tm_set(struct mt792x_dev *dev, struct mt7925_tm_cmd *req)
{
	struct mt7925_rftest_cmd cmd;
	struct mt7925_rftest_cmd *pcmd = &cmd;
	bool testmode = false, normal = false;
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_phy *phy = &dev->mphy;
	int ret = -ENOTCONN;

	memset(pcmd, 0, sizeof(*pcmd));
	memcpy(pcmd, req, sizeof(struct mt7925_tm_cmd));

	mutex_lock(&dev->mt76.mutex);

	if (pcmd->ctrl.action == CMD_TEST_CTRL_ACT_SWITCH_MODE) {
		if (pcmd->ctrl.data.op_mode == CMD_TEST_CTRL_ACT_SWITCH_MODE_NORMAL)
			normal = true;
		else
			testmode = true;
	}

	if (testmode) {
		/* Make sure testmode running on full power mode */
		pm->enable = false;
		cancel_delayed_work_sync(&pm->ps_work);
		cancel_work_sync(&pm->wake_work);
		__mt792x_mcu_drv_pmctrl(dev);

		phy->test.state = MT76_TM_STATE_ON;
	}

	if (!mt76_testmode_enabled(phy))
		goto out;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(TESTMODE_CTRL), &cmd,
				sizeof(cmd), false);

	if (ret)
		goto out;

	if (normal) {
		/* Switch back to the normal world */
		phy->test.state = MT76_TM_STATE_OFF;
		pm->enable = true;
	}
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7925_tm_query(struct mt792x_dev *dev, struct mt7925_tm_cmd *req,
		char *evt_resp)
{
	struct mt7925_rftest_cmd cmd;
	char *pcmd = (char *)&cmd;
	struct sk_buff *skb = NULL;
	int ret = 1;

	memset(pcmd, 0, sizeof(*pcmd));
	memcpy(pcmd + 4, (char *)&req->c, sizeof(struct uni_cmd_testmode_ctrl));

	if (*((uint16_t *)req->padding) == MCU_UNI_CMD_TESTMODE_CTRL)
		ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_QUERY(TESTMODE_CTRL),
						&cmd, sizeof(cmd), true, &skb);
	else if (*((uint16_t *)req->padding) == MCU_UNI_CMD_TESTMODE_RX_STAT)
		ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_UNI_QUERY(TESTMODE_RX_STAT),
						&cmd, sizeof(cmd), true, &skb);

	if (ret)
		goto out;

	memcpy((char *)evt_resp, (char *)skb->data + 8, MT7925_EVT_RSP_LEN);

out:
	dev_kfree_skb(skb);

	return ret;
}

int mt7925_testmode_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			void *data, int len)
{
	struct nlattr *tb[NUM_MT76_TM_ATTRS];
	struct mt76_phy *mphy = hw->priv;
	struct mt792x_phy *phy = mphy->priv;
	int err;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state) ||
	    !(hw->conf.flags & IEEE80211_CONF_MONITOR))
		return -ENOTCONN;

	err = nla_parse_deprecated(tb, MT76_TM_ATTR_MAX, data, len,
				   mt76_tm_policy, NULL);
	if (err)
		return err;

	if (tb[MT76_TM_ATTR_DRV_DATA]) {
		struct nlattr *drv_tb[NUM_MT7925_TM_ATTRS], *data;
		int ret;

		data = tb[MT76_TM_ATTR_DRV_DATA];
		ret = nla_parse_nested_deprecated(drv_tb,
						  MT7925_TM_ATTR_MAX,
						  data, mt7925_tm_policy,
						  NULL);
		if (ret)
			return ret;

		data = drv_tb[MT7925_TM_ATTR_SET];
		if (data)
			return mt7925_tm_set(phy->dev, nla_data(data));
	}

	return -EINVAL;
}

int mt7925_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *msg,
			 struct netlink_callback *cb, void *data, int len)
{
	struct nlattr *tb[NUM_MT76_TM_ATTRS];
	struct mt76_phy *mphy = hw->priv;
	struct mt792x_phy *phy = mphy->priv;
	int err;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state) ||
	    !(hw->conf.flags & IEEE80211_CONF_MONITOR) ||
	    !mt76_testmode_enabled(mphy))
		return -ENOTCONN;

	if (cb->args[2]++ > 0)
		return -ENOENT;

	err = nla_parse_deprecated(tb, MT76_TM_ATTR_MAX, data, len,
				   mt76_tm_policy, NULL);
	if (err)
		return err;

	if (tb[MT76_TM_ATTR_DRV_DATA]) {
		struct nlattr *drv_tb[NUM_MT7925_TM_ATTRS], *data;
		int ret;

		data = tb[MT76_TM_ATTR_DRV_DATA];
		ret = nla_parse_nested_deprecated(drv_tb,
						  MT7925_TM_ATTR_MAX,
						  data, mt7925_tm_policy,
						  NULL);
		if (ret)
			return ret;

		data = drv_tb[MT7925_TM_ATTR_QUERY];
		if (data) {
			char evt_resp[MT7925_EVT_RSP_LEN];

			err = mt7925_tm_query(phy->dev, nla_data(data),
					      evt_resp);
			if (err)
				return err;

			return nla_put(msg, MT7925_TM_ATTR_RSP,
				       sizeof(evt_resp), evt_resp);
		}
	}

	return -EINVAL;
}
