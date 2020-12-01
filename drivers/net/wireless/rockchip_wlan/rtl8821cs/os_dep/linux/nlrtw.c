/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2020 Realtek Corporation.
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
#define _RTW_NLRTW_C_

#include <drv_types.h>
#include "nlrtw.h"

#ifdef CONFIG_RTW_NLRTW

#include <net/netlink.h>
#include <net/genetlink.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
#include <uapi/linux/netlink.h>
#endif


enum nlrtw_cmds {
	NLRTW_CMD_UNSPEC,

	NLRTW_CMD_CHANNEL_UTILIZATION,
	NLRTW_CMD_REG_CHANGE,
	NLRTW_CMD_REG_BEACON_HINT,
	NLRTW_CMD_RADAR_EVENT,
	NLRTW_CMD_RADIO_OPMODE,

	__NLRTW_CMD_AFTER_LAST,
	NLRTW_CMD_MAX = __NLRTW_CMD_AFTER_LAST - 1
};

enum nlrtw_attrs {
	NLRTW_ATTR_UNSPEC,

	NLRTW_ATTR_WIPHY_NAME,
	NLRTW_ATTR_CHANNEL_UTILIZATIONS,
	NLRTW_ATTR_CHANNEL_UTILIZATION_THRESHOLD,
	NLRTW_ATTR_CHANNEL_CENTER,
	NLRTW_ATTR_CHANNEL_WIDTH,
	NLRTW_ATTR_RADAR_EVENT,
	NLRTW_ATTR_OP_CLASS,
	NLRTW_ATTR_OP_CHANNEL,
	NLRTW_ATTR_OP_TXPWR_MAX,
	NLRTW_ATTR_IF_OPMODES,

	__NLRTW_ATTR_AFTER_LAST,
	NUM_NLRTW_ATTR = __NLRTW_ATTR_AFTER_LAST,
	NLRTW_ATTR_MAX = __NLRTW_ATTR_AFTER_LAST - 1
};

enum nlrtw_ch_util_attrs {
	__NLRTW_ATTR_CHANNEL_UTILIZATION_INVALID,

	NLRTW_ATTR_CHANNEL_UTILIZATION_VALUE,
	NLRTW_ATTR_CHANNEL_UTILIZATION_BSSID,

	__NLRTW_ATTR_CHANNEL_UTILIZATION_AFTER_LAST,
	NUM_NLRTW_ATTR_CHANNEL_UTILIZATION = __NLRTW_ATTR_CHANNEL_UTILIZATION_AFTER_LAST,
	NLRTW_ATTR_CHANNEL_UTILIZATION_MAX = __NLRTW_ATTR_CHANNEL_UTILIZATION_AFTER_LAST - 1
};

enum nlrtw_radar_event {
	NLRTW_RADAR_DETECTED,
	NLRTW_RADAR_CAC_FINISHED,
	NLRTW_RADAR_CAC_ABORTED,
	NLRTW_RADAR_NOP_FINISHED,
	NLRTW_RADAR_NOP_STARTED, /* NON_OCP started not by local radar detection */
};

enum nlrtw_if_opmode_attrs {
	NLRTW_IF_OPMODE_UNSPEC,

	NLRTW_IF_OPMODE_MACADDR,
	NLRTW_IF_OPMODE_OP_CLASS,
	NLRTW_IF_OPMODE_OP_CHANNEL,

	__NLRTW_IF_OPMODE_ATTR_AFTER_LAST,
	NUM_NLRTW_IF_OPMODE_ATTR = __NLRTW_IF_OPMODE_ATTR_AFTER_LAST,
	NLRTW_IF_OPMODE_ATTR_MAX = __NLRTW_IF_OPMODE_ATTR_AFTER_LAST - 1
};

static int nlrtw_ch_util_set(struct sk_buff *skb, struct genl_info *info)
{
	unsigned int msg;

	if (!info->attrs[NLRTW_ATTR_CHANNEL_UTILIZATION_THRESHOLD])
		return -EINVAL;
	msg = nla_get_u8(info->attrs[NLRTW_ATTR_CHANNEL_UTILIZATION_THRESHOLD]);

	return 0;
}

static struct nla_policy nlrtw_genl_policy[NUM_NLRTW_ATTR] = {
	[NLRTW_ATTR_CHANNEL_UTILIZATION_THRESHOLD] = { .type = NLA_U8 },
};

static struct genl_ops nlrtw_genl_ops[] = {
	{
		.cmd = NLRTW_CMD_CHANNEL_UTILIZATION,
		.flags = 0,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
		.policy = nlrtw_genl_policy,
#endif
		.doit = nlrtw_ch_util_set,
		.dumpit = NULL,
	},
};

enum nlrtw_multicast_groups {
	NLRTW_MCGRP_DEFAULT,
};
static struct genl_multicast_group nlrtw_genl_mcgrp[] = {
	[NLRTW_MCGRP_DEFAULT] = { .name = "nlrtw_default" },
};

/* family definition */
static struct genl_family nlrtw_genl_family = {
	.hdrsize = 0,
	.name = "nlrtw_"DRV_NAME,
	.version = 1,
	.maxattr = NLRTW_ATTR_MAX,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 2, 0)
	.policy = nlrtw_genl_policy,
#endif
	.netnsok = true,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 12)
	.module = THIS_MODULE,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	.ops = nlrtw_genl_ops,
	.n_ops = ARRAY_SIZE(nlrtw_genl_ops),
	.mcgrps = nlrtw_genl_mcgrp,
	.n_mcgrps = ARRAY_SIZE(nlrtw_genl_mcgrp),
#endif
};

static inline int nlrtw_multicast(const struct genl_family *family,
				  struct sk_buff *skb, u32 portid,
				  unsigned int group, gfp_t flags)
{
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	ret = genlmsg_multicast(&nlrtw_genl_family, skb, portid, group, flags);
#else
	ret = genlmsg_multicast(skb, portid, nlrtw_genl_mcgrp[group].id, flags);
#endif
	return ret;
}

int rtw_nlrtw_ch_util_rpt(_adapter *adapter, u8 n_rpts, u8 *val, u8 **mac_addr)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	struct nlattr *nl_ch_util, *nl_ch_utils;
	struct wiphy *wiphy;
	u8 i;
	int ret;

	wiphy = adapter_to_wiphy(adapter);
	if (!wiphy)
		return -EINVAL;

	/* allocate memory */
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		nlmsg_free(skb);
		return -ENOMEM;
	}

	/* create the message headers */
	msg_header = genlmsg_put(skb, 0, 0, &nlrtw_genl_family, 0,
				 NLRTW_CMD_CHANNEL_UTILIZATION);
	if (!msg_header) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* add attributes */
	ret = nla_put_string(skb, NLRTW_ATTR_WIPHY_NAME, wiphy_name(wiphy));

	nl_ch_utils = nla_nest_start(skb, NLRTW_ATTR_CHANNEL_UTILIZATIONS);
	if (!nl_ch_utils) {
		ret = -EMSGSIZE;
		goto err_out;
	}

	for (i = 0; i < n_rpts; i++) {
		nl_ch_util = nla_nest_start(skb, i);
		if (!nl_ch_util) {
			ret = -EMSGSIZE;
			goto err_out;
		}

		ret = nla_put(skb, NLRTW_ATTR_CHANNEL_UTILIZATION_BSSID, ETH_ALEN, *(mac_addr + i));
		if (ret != 0)
			goto err_out;

		ret = nla_put_u8(skb, NLRTW_ATTR_CHANNEL_UTILIZATION_VALUE, *(val + i));
		if (ret != 0)
			goto err_out;

		nla_nest_end(skb, nl_ch_util);
	}

	nla_nest_end(skb, nl_ch_utils);

	/* finalize the message */
	genlmsg_end(skb, msg_header);

	ret = nlrtw_multicast(&nlrtw_genl_family, skb, 0, NLRTW_MCGRP_DEFAULT, GFP_KERNEL);
	if (ret == -ESRCH) {
		RTW_INFO("[%s] return ESRCH(No such process)."
			 " Maybe no process waits for this msg\n", __func__);
		return ret;
	} else if (ret != 0) {
		RTW_INFO("[%s] ret = %d\n", __func__, ret);
		return ret;
	}

	return 0;
err_out:
	nlmsg_free(skb);
	return ret;
}

int rtw_nlrtw_reg_change_event(_adapter *adapter)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	struct wiphy *wiphy;
	u8 i;
	int ret;

	wiphy = adapter_to_wiphy(adapter);
	if (!wiphy) {
		ret = -EINVAL;
		goto err_out;
	}

	/* allocate memory */
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* create the message headers */
	msg_header = genlmsg_put(skb, 0, 0, &nlrtw_genl_family, 0, NLRTW_CMD_REG_CHANGE);
	if (!msg_header) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* add attributes */
	ret = nla_put_string(skb, NLRTW_ATTR_WIPHY_NAME, wiphy_name(wiphy));
	if (ret)
		goto err_out;

	/* finalize the message */
	genlmsg_end(skb, msg_header);

	ret = nlrtw_multicast(&nlrtw_genl_family, skb, 0, NLRTW_MCGRP_DEFAULT, GFP_KERNEL);
	if (ret == -ESRCH) {
		RTW_DBG(FUNC_WIPHY_FMT" return -ESRCH(No such process)."
			 " Maybe no process waits for this msg\n", FUNC_WIPHY_ARG(wiphy));
		return ret;
	} else if (ret != 0) {
		RTW_WARN(FUNC_WIPHY_FMT" return %d\n", FUNC_WIPHY_ARG(wiphy), ret);
		return ret;
	}

	return 0;

err_out:
	if (skb)
		nlmsg_free(skb);
	return ret;
}

int rtw_nlrtw_reg_beacon_hint_event(_adapter *adapter)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	struct wiphy *wiphy;
	u8 i;
	int ret;

	wiphy = adapter_to_wiphy(adapter);
	if (!wiphy) {
		ret = -EINVAL;
		goto err_out;
	}

	/* allocate memory */
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* create the message headers */
	msg_header = genlmsg_put(skb, 0, 0, &nlrtw_genl_family, 0, NLRTW_CMD_REG_BEACON_HINT);
	if (!msg_header) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* add attributes */
	ret = nla_put_string(skb, NLRTW_ATTR_WIPHY_NAME, wiphy_name(wiphy));
	if (ret)
		goto err_out;

	/* finalize the message */
	genlmsg_end(skb, msg_header);

	ret = nlrtw_multicast(&nlrtw_genl_family, skb, 0, NLRTW_MCGRP_DEFAULT, GFP_KERNEL);
	if (ret == -ESRCH) {
		RTW_DBG(FUNC_WIPHY_FMT" return -ESRCH(No such process)."
			 " Maybe no process waits for this msg\n", FUNC_WIPHY_ARG(wiphy));
		return ret;
	} else if (ret != 0) {
		RTW_WARN(FUNC_WIPHY_FMT" return %d\n", FUNC_WIPHY_ARG(wiphy), ret);
		return ret;
	}

	return 0;

err_out:
	if (skb)
		nlmsg_free(skb);
	return ret;
}

#ifdef CONFIG_DFS_MASTER
static int _rtw_nlrtw_radar_event(_adapter *adapter, enum nlrtw_radar_event evt_type, u8 cch, u8 bw)
{
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	struct wiphy *wiphy;
	u8 i;
	int ret;

	wiphy = adapter_to_wiphy(adapter);
	if (!wiphy) {
		ret = -EINVAL;
		goto err_out;
	}

	/* allocate memory */
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* create the message headers */
	msg_header = genlmsg_put(skb, 0, 0, &nlrtw_genl_family, 0, NLRTW_CMD_RADAR_EVENT);
	if (!msg_header) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* add attributes */
	ret = nla_put_string(skb, NLRTW_ATTR_WIPHY_NAME, wiphy_name(wiphy));
	if (ret)
		goto err_out;

	ret = nla_put_u8(skb, NLRTW_ATTR_RADAR_EVENT, (uint8_t)evt_type);
	if (ret != 0)
		goto err_out;

	ret = nla_put_u8(skb, NLRTW_ATTR_CHANNEL_CENTER, cch);
	if (ret != 0)
		goto err_out;

	ret = nla_put_u8(skb, NLRTW_ATTR_CHANNEL_WIDTH, bw);
	if (ret != 0)
		goto err_out;

	/* finalize the message */
	genlmsg_end(skb, msg_header);

	ret = nlrtw_multicast(&nlrtw_genl_family, skb, 0, NLRTW_MCGRP_DEFAULT, GFP_KERNEL);
	if (ret == -ESRCH) {
		RTW_DBG(FUNC_WIPHY_FMT" return -ESRCH(No such process)."
			 " Maybe no process waits for this msg\n", FUNC_WIPHY_ARG(wiphy));
		return ret;
	} else if (ret != 0) {
		RTW_WARN(FUNC_WIPHY_FMT" return %d\n", FUNC_WIPHY_ARG(wiphy), ret);
		return ret;
	}

	return 0;

err_out:
	if (skb)
		nlmsg_free(skb);
	return ret;
}

int rtw_nlrtw_radar_detect_event(_adapter *adapter, u8 cch, u8 bw)
{
	return _rtw_nlrtw_radar_event(adapter, NLRTW_RADAR_DETECTED, cch, bw);
}

int rtw_nlrtw_cac_finish_event(_adapter *adapter, u8 cch, u8 bw)
{
	return _rtw_nlrtw_radar_event(adapter, NLRTW_RADAR_CAC_FINISHED, cch, bw);
}

int rtw_nlrtw_cac_abort_event(_adapter *adapter, u8 cch, u8 bw)
{
	return _rtw_nlrtw_radar_event(adapter, NLRTW_RADAR_CAC_ABORTED, cch, bw);
}

int rtw_nlrtw_nop_finish_event(_adapter *adapter, u8 cch, u8 bw)
{
	return _rtw_nlrtw_radar_event(adapter, NLRTW_RADAR_NOP_FINISHED, cch, bw);
}

int rtw_nlrtw_nop_start_event(_adapter *adapter, u8 cch, u8 bw)
{
	return _rtw_nlrtw_radar_event(adapter, NLRTW_RADAR_NOP_STARTED, cch, bw);
}
#endif /* CONFIG_DFS_MASTER */

int rtw_nlrtw_radio_opmode_notify(struct rf_ctl_t *rfctl)
{
	struct dvobj_priv *dvobj = rfctl_to_dvobj(rfctl);
	_adapter *iface;
	struct sk_buff *skb = NULL;
	void *msg_header = NULL;
	struct nlattr *nl_if_opmodes, *nl_if_opmode;
	struct wiphy *wiphy;
	u16 op_txpwr_max_u16;
	u8 i;
	int ret;

	wiphy = dvobj_to_wiphy(dvobj);
	if (!wiphy) {
		ret = -EINVAL;
		goto err_out;
	}

	/* allocate memory */
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto err_out;
	}

	/* create the message headers */
	msg_header = genlmsg_put(skb, 0, 0, &nlrtw_genl_family, 0, NLRTW_CMD_RADIO_OPMODE);
	if (!msg_header) {
		ret = -ENOBUFS;
		goto err_out;
	}

	/* add attributes */
	ret = nla_put_string(skb, NLRTW_ATTR_WIPHY_NAME, wiphy_name(wiphy));
	if (ret)
		goto err_out;

	ret = nla_put_u8(skb, NLRTW_ATTR_OP_CLASS, rfctl->op_class);
	if (ret != 0)
		goto err_out;

	ret = nla_put_u8(skb, NLRTW_ATTR_OP_CHANNEL, rfctl->op_ch);
	if (ret != 0)
		goto err_out;

	*((s16 *)&op_txpwr_max_u16) = rfctl->op_txpwr_max;
	ret = nla_put_u16(skb, NLRTW_ATTR_OP_TXPWR_MAX, op_txpwr_max_u16);
	if (ret != 0)
		goto err_out;

	if (0)
		RTW_INFO("radio: %u,%u %d\n", rfctl->op_class, rfctl->op_ch, rfctl->op_txpwr_max);

	nl_if_opmodes = nla_nest_start(skb, NLRTW_ATTR_IF_OPMODES);
	if (!nl_if_opmodes) {
		ret = -ENOBUFS;
		goto err_out;
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (!dvobj->padapters[i])
			continue;
		iface = dvobj->padapters[i];

		if (!rfctl->if_op_class[i] || !rfctl->if_op_ch[i])
			continue;

		if (0)
			RTW_INFO(ADPT_FMT": %u,%u\n", ADPT_ARG(iface), rfctl->if_op_class[i], rfctl->if_op_ch[i]);

		nl_if_opmode = nla_nest_start(skb, i + 1);
		if (!nl_if_opmode) {
			ret = -ENOBUFS;
			goto err_out;
		}

		ret = nla_put(skb, NLRTW_IF_OPMODE_MACADDR, ETH_ALEN, adapter_mac_addr(iface));
		if (ret != 0)
			goto err_out;

		ret = nla_put_u8(skb, NLRTW_IF_OPMODE_OP_CLASS, rfctl->if_op_class[i]);
		if (ret != 0)
			goto err_out;

		ret = nla_put_u8(skb, NLRTW_IF_OPMODE_OP_CHANNEL, rfctl->if_op_ch[i]);
		if (ret != 0)
			goto err_out;

		nla_nest_end(skb, nl_if_opmode);
	}

	nla_nest_end(skb, nl_if_opmodes);

	/* finalize the message */
	genlmsg_end(skb, msg_header);

	ret = nlrtw_multicast(&nlrtw_genl_family, skb, 0, NLRTW_MCGRP_DEFAULT, GFP_KERNEL);
	if (ret == -ESRCH) {
		RTW_DBG(FUNC_WIPHY_FMT" return -ESRCH(No such process)."
			 " Maybe no process waits for this msg\n", FUNC_WIPHY_ARG(wiphy));
		return ret;
	} else if (ret != 0) {
		RTW_WARN(FUNC_WIPHY_FMT" return %d\n", FUNC_WIPHY_ARG(wiphy), ret);
		return ret;
	}

	return 0;

err_out:
	if (skb)
		nlmsg_free(skb);
	return ret;
}

int rtw_nlrtw_init(void)
{
	int err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	err = genl_register_family(&nlrtw_genl_family);
	if (err)
		return err;
#else
	err = genl_register_family_with_ops(&nlrtw_genl_family, nlrtw_genl_ops, ARRAY_SIZE(nlrtw_genl_ops));
	if (err)
		return err;

	err = genl_register_mc_group(&nlrtw_genl_family, &nlrtw_genl_mcgrp[0]);
	if (err) {
		genl_unregister_family(&nlrtw_genl_family);
		return err;
	}
#endif
	RTW_INFO("[%s] %s\n", __func__, nlrtw_genl_family.name);
	return 0;
}

int rtw_nlrtw_deinit(void)
{
	int err;

	err = genl_unregister_family(&nlrtw_genl_family);
	RTW_INFO("[%s] err = %d\n", __func__, err);

	return err;
}
#endif /* CONFIG_RTW_NLRTW */
