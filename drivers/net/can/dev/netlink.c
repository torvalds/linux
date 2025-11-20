// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <linux/can/dev.h>
#include <net/rtnetlink.h>

static const struct nla_policy can_policy[IFLA_CAN_MAX + 1] = {
	[IFLA_CAN_STATE] = { .type = NLA_U32 },
	[IFLA_CAN_CTRLMODE] = { .len = sizeof(struct can_ctrlmode) },
	[IFLA_CAN_RESTART_MS] = { .type = NLA_U32 },
	[IFLA_CAN_RESTART] = { .type = NLA_U32 },
	[IFLA_CAN_BITTIMING] = { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_BITTIMING_CONST] = { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_CLOCK] = { .len = sizeof(struct can_clock) },
	[IFLA_CAN_BERR_COUNTER] = { .len = sizeof(struct can_berr_counter) },
	[IFLA_CAN_DATA_BITTIMING] = { .len = sizeof(struct can_bittiming) },
	[IFLA_CAN_DATA_BITTIMING_CONST] = { .len = sizeof(struct can_bittiming_const) },
	[IFLA_CAN_TERMINATION] = { .type = NLA_U16 },
	[IFLA_CAN_TDC] = { .type = NLA_NESTED },
	[IFLA_CAN_CTRLMODE_EXT] = { .type = NLA_NESTED },
};

static const struct nla_policy can_tdc_policy[IFLA_CAN_TDC_MAX + 1] = {
	[IFLA_CAN_TDC_TDCV_MIN] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCV_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO_MIN] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF_MIN] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF_MAX] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCV] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCO] = { .type = NLA_U32 },
	[IFLA_CAN_TDC_TDCF] = { .type = NLA_U32 },
};

static int can_validate_bittiming(struct nlattr *data[],
				  struct netlink_ext_ack *extack,
				  int ifla_can_bittiming)
{
	struct can_bittiming *bt;

	if (!data[ifla_can_bittiming])
		return 0;

	static_assert(__alignof__(*bt) <= NLA_ALIGNTO);
	bt = nla_data(data[ifla_can_bittiming]);

	/* sample point is in one-tenth of a percent */
	if (bt->sample_point >= 1000) {
		NL_SET_ERR_MSG(extack, "sample point must be between 0 and 100%");
		return -EINVAL;
	}

	return 0;
}

static int can_validate_tdc(struct nlattr *data_tdc,
			    struct netlink_ext_ack *extack, u32 tdc_flags)
{
	bool tdc_manual = tdc_flags & CAN_CTRLMODE_TDC_MANUAL_MASK;
	bool tdc_auto = tdc_flags & CAN_CTRLMODE_TDC_AUTO_MASK;
	int err;

	if (tdc_auto && tdc_manual) {
		NL_SET_ERR_MSG(extack,
			       "TDC manual and auto modes are mutually exclusive");
		return -EOPNOTSUPP;
	}

	/* If one of the CAN_CTRLMODE_TDC_* flag is set then TDC
	 * must be set and vice-versa
	 */
	if ((tdc_auto || tdc_manual) && !data_tdc) {
		NL_SET_ERR_MSG(extack, "TDC parameters are missing");
		return -EOPNOTSUPP;
	}
	if (!(tdc_auto || tdc_manual) && data_tdc) {
		NL_SET_ERR_MSG(extack, "TDC mode (auto or manual) is missing");
		return -EOPNOTSUPP;
	}

	/* If providing TDC parameters, at least TDCO is needed. TDCV
	 * is needed if and only if CAN_CTRLMODE_TDC_MANUAL is set
	 */
	if (data_tdc) {
		struct nlattr *tb_tdc[IFLA_CAN_TDC_MAX + 1];

		err = nla_parse_nested(tb_tdc, IFLA_CAN_TDC_MAX,
				       data_tdc, can_tdc_policy, extack);
		if (err)
			return err;

		if (tb_tdc[IFLA_CAN_TDC_TDCV]) {
			if (tdc_auto) {
				NL_SET_ERR_MSG(extack,
					       "TDCV is incompatible with TDC auto mode");
				return -EOPNOTSUPP;
			}
		} else {
			if (tdc_manual) {
				NL_SET_ERR_MSG(extack,
					       "TDC manual mode requires TDCV");
				return -EOPNOTSUPP;
			}
		}

		if (!tb_tdc[IFLA_CAN_TDC_TDCO]) {
			NL_SET_ERR_MSG(extack, "TDCO is missing");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int can_validate_databittiming(struct nlattr *data[],
				      struct netlink_ext_ack *extack,
				      int ifla_can_data_bittiming, u32 flags)
{
	struct nlattr *data_tdc;
	const char *type;
	u32 tdc_flags;
	bool is_on;
	int err;

	/* Make sure that valid CAN FD configurations always consist of
	 * - nominal/arbitration bittiming
	 * - data bittiming
	 * - control mode with CAN_CTRLMODE_FD set
	 * - TDC parameters are coherent (details in can_validate_tdc())
	 */

	if (ifla_can_data_bittiming == IFLA_CAN_DATA_BITTIMING) {
		data_tdc = data[IFLA_CAN_TDC];
		tdc_flags = flags & CAN_CTRLMODE_FD_TDC_MASK;
		is_on = flags & CAN_CTRLMODE_FD;
		type = "FD";
	} else {
		return -EOPNOTSUPP; /* Place holder for CAN XL */
	}

	if (is_on) {
		if (!data[IFLA_CAN_BITTIMING] || !data[ifla_can_data_bittiming]) {
			NL_SET_ERR_MSG_FMT(extack,
					   "Provide both nominal and %s data bittiming",
					   type);
			return -EOPNOTSUPP;
		}
	} else {
		if (data[ifla_can_data_bittiming]) {
			NL_SET_ERR_MSG_FMT(extack,
					   "%s data bittiming requires CAN %s",
					   type, type);
			return -EOPNOTSUPP;
		}
		if (data_tdc) {
			NL_SET_ERR_MSG_FMT(extack,
					   "%s TDC requires CAN %s",
					   type, type);
			return -EOPNOTSUPP;
		}
	}

	err = can_validate_bittiming(data, extack, ifla_can_data_bittiming);
	if (err)
		return err;

	err = can_validate_tdc(data_tdc, extack, tdc_flags);
	if (err)
		return err;

	return 0;
}

static int can_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	u32 flags = 0;
	int err;

	if (!data)
		return 0;

	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm = nla_data(data[IFLA_CAN_CTRLMODE]);

		flags = cm->flags & cm->mask;
	}

	err = can_validate_bittiming(data, extack, IFLA_CAN_BITTIMING);
	if (err)
		return err;

	err = can_validate_databittiming(data, extack,
					 IFLA_CAN_DATA_BITTIMING, flags);
	if (err)
		return err;

	return 0;
}

static int can_ctrlmode_changelink(struct net_device *dev,
				   struct nlattr *data[],
				   struct netlink_ext_ack *extack)
{
	struct can_priv *priv = netdev_priv(dev);
	struct can_ctrlmode *cm;
	u32 ctrlstatic, maskedflags, notsupp, ctrlstatic_missing;

	if (!data[IFLA_CAN_CTRLMODE])
		return 0;

	/* Do not allow changing controller mode while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	cm = nla_data(data[IFLA_CAN_CTRLMODE]);
	ctrlstatic = can_get_static_ctrlmode(priv);
	maskedflags = cm->flags & cm->mask;
	notsupp = maskedflags & ~(priv->ctrlmode_supported | ctrlstatic);
	ctrlstatic_missing = (maskedflags & ctrlstatic) ^ ctrlstatic;

	if (notsupp) {
		NL_SET_ERR_MSG_FMT(extack,
				   "requested control mode %s not supported",
				   can_get_ctrlmode_str(notsupp));
		return -EOPNOTSUPP;
	}

	/* do not check for static fd-non-iso if 'fd' is disabled */
	if (!(maskedflags & CAN_CTRLMODE_FD))
		ctrlstatic &= ~CAN_CTRLMODE_FD_NON_ISO;

	if (ctrlstatic_missing) {
		NL_SET_ERR_MSG_FMT(extack,
				   "missing required %s static control mode",
				   can_get_ctrlmode_str(ctrlstatic_missing));
		return -EOPNOTSUPP;
	}

	/* If a top dependency flag is provided, reset all its dependencies */
	if (cm->mask & CAN_CTRLMODE_FD)
		priv->ctrlmode &= ~CAN_CTRLMODE_FD_TDC_MASK;

	/* clear bits to be modified and copy the flag values */
	priv->ctrlmode &= ~cm->mask;
	priv->ctrlmode |= maskedflags;

	/* Wipe potential leftovers from previous CAN FD config */
	if (!(priv->ctrlmode & CAN_CTRLMODE_FD)) {
		memset(&priv->fd.data_bittiming, 0,
		       sizeof(priv->fd.data_bittiming));
		priv->ctrlmode &= ~CAN_CTRLMODE_FD_TDC_MASK;
		memset(&priv->fd.tdc, 0, sizeof(priv->fd.tdc));
	}

	can_set_default_mtu(dev);

	return 0;
}

static int can_tdc_changelink(struct data_bittiming_params *dbt_params,
			      const struct nlattr *nla,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb_tdc[IFLA_CAN_TDC_MAX + 1];
	struct can_tdc tdc = { 0 };
	const struct can_tdc_const *tdc_const = dbt_params->tdc_const;
	int err;

	if (!tdc_const) {
		NL_SET_ERR_MSG(extack, "The device does not support TDC");
		return -EOPNOTSUPP;
	}

	err = nla_parse_nested(tb_tdc, IFLA_CAN_TDC_MAX, nla,
			       can_tdc_policy, extack);
	if (err)
		return err;

	if (tb_tdc[IFLA_CAN_TDC_TDCV]) {
		u32 tdcv = nla_get_u32(tb_tdc[IFLA_CAN_TDC_TDCV]);

		if (tdcv < tdc_const->tdcv_min || tdcv > tdc_const->tdcv_max)
			return -EINVAL;

		tdc.tdcv = tdcv;
	}

	if (tb_tdc[IFLA_CAN_TDC_TDCO]) {
		u32 tdco = nla_get_u32(tb_tdc[IFLA_CAN_TDC_TDCO]);

		if (tdco < tdc_const->tdco_min || tdco > tdc_const->tdco_max)
			return -EINVAL;

		tdc.tdco = tdco;
	}

	if (tb_tdc[IFLA_CAN_TDC_TDCF]) {
		u32 tdcf = nla_get_u32(tb_tdc[IFLA_CAN_TDC_TDCF]);

		if (tdcf < tdc_const->tdcf_min || tdcf > tdc_const->tdcf_max)
			return -EINVAL;

		tdc.tdcf = tdcf;
	}

	dbt_params->tdc = tdc;

	return 0;
}

static int can_dbt_changelink(struct net_device *dev, struct nlattr *data[],
			      bool fd, struct netlink_ext_ack *extack)
{
	struct nlattr *data_bittiming, *data_tdc;
	struct can_priv *priv = netdev_priv(dev);
	struct data_bittiming_params *dbt_params;
	struct can_bittiming dbt;
	bool need_tdc_calc = false;
	u32 tdc_mask;
	int err;

	if (fd) {
		data_bittiming = data[IFLA_CAN_DATA_BITTIMING];
		data_tdc = data[IFLA_CAN_TDC];
		dbt_params = &priv->fd;
		tdc_mask = CAN_CTRLMODE_FD_TDC_MASK;
	} else {
		return -EOPNOTSUPP; /* Place holder for CAN XL */
	}

	if (!data_bittiming)
		return 0;

	/* Do not allow changing bittiming while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	/* Calculate bittiming parameters based on data_bittiming_const
	 * if set, otherwise pass bitrate directly via do_set_bitrate().
	 * Bail out if neither is given.
	 */
	if (!dbt_params->data_bittiming_const && !dbt_params->do_set_data_bittiming &&
	    !dbt_params->data_bitrate_const)
		return -EOPNOTSUPP;

	memcpy(&dbt, nla_data(data_bittiming), sizeof(dbt));
	err = can_get_bittiming(dev, &dbt, dbt_params->data_bittiming_const,
				dbt_params->data_bitrate_const,
				dbt_params->data_bitrate_const_cnt, extack);
	if (err)
		return err;

	if (priv->bitrate_max && dbt.bitrate > priv->bitrate_max) {
		NL_SET_ERR_MSG_FMT(extack,
				   "CAN data bitrate %u bps surpasses transceiver capabilities of %u bps",
				   dbt.bitrate, priv->bitrate_max);
		return -EINVAL;
	}

	memset(&dbt_params->tdc, 0, sizeof(dbt_params->tdc));
	if (data[IFLA_CAN_CTRLMODE]) {
		struct can_ctrlmode *cm = nla_data(data[IFLA_CAN_CTRLMODE]);

		need_tdc_calc = !(cm->mask & tdc_mask);
	}
	if (data_tdc) {
		/* TDC parameters are provided: use them */
		err = can_tdc_changelink(dbt_params, data_tdc, extack);
		if (err) {
			priv->ctrlmode &= ~tdc_mask;
			return err;
		}
	} else if (need_tdc_calc) {
		/* Neither of TDC parameters nor TDC flags are provided:
		 * do calculation
		 */
		can_calc_tdco(&dbt_params->tdc, dbt_params->tdc_const, &dbt,
			      tdc_mask, &priv->ctrlmode, priv->ctrlmode_supported);
	} /* else: both CAN_CTRLMODE_TDC_{AUTO,MANUAL} are explicitly
	   * turned off. TDC is disabled: do nothing
	   */

	memcpy(&dbt_params->data_bittiming, &dbt, sizeof(dbt));

	if (dbt_params->do_set_data_bittiming) {
		/* Finally, set the bit-timing registers */
		err = dbt_params->do_set_data_bittiming(dev);
		if (err)
			return err;
	}

	return 0;
}

static int can_changelink(struct net_device *dev, struct nlattr *tb[],
			  struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	struct can_priv *priv = netdev_priv(dev);
	int err;

	/* We need synchronization with dev->stop() */
	ASSERT_RTNL();

	can_ctrlmode_changelink(dev, data, extack);

	if (data[IFLA_CAN_BITTIMING]) {
		struct can_bittiming bt;

		/* Do not allow changing bittiming while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;

		/* Calculate bittiming parameters based on
		 * bittiming_const if set, otherwise pass bitrate
		 * directly via do_set_bitrate(). Bail out if neither
		 * is given.
		 */
		if (!priv->bittiming_const && !priv->do_set_bittiming &&
		    !priv->bitrate_const)
			return -EOPNOTSUPP;

		memcpy(&bt, nla_data(data[IFLA_CAN_BITTIMING]), sizeof(bt));
		err = can_get_bittiming(dev, &bt,
					priv->bittiming_const,
					priv->bitrate_const,
					priv->bitrate_const_cnt,
					extack);
		if (err)
			return err;

		if (priv->bitrate_max && bt.bitrate > priv->bitrate_max) {
			NL_SET_ERR_MSG_FMT(extack,
					   "arbitration bitrate %u bps surpasses transceiver capabilities of %u bps",
					   bt.bitrate, priv->bitrate_max);
			return -EINVAL;
		}

		memcpy(&priv->bittiming, &bt, sizeof(bt));

		if (priv->do_set_bittiming) {
			/* Finally, set the bit-timing registers */
			err = priv->do_set_bittiming(dev);
			if (err)
				return err;
		}
	}

	if (data[IFLA_CAN_RESTART_MS]) {
		unsigned int restart_ms = nla_get_u32(data[IFLA_CAN_RESTART_MS]);

		if (restart_ms != 0 && !priv->do_set_mode) {
			NL_SET_ERR_MSG(extack,
				       "Device doesn't support restart from Bus Off");
			return -EOPNOTSUPP;
		}

		/* Do not allow changing restart delay while running */
		if (dev->flags & IFF_UP)
			return -EBUSY;
		priv->restart_ms = restart_ms;
	}

	if (data[IFLA_CAN_RESTART]) {
		if (!priv->do_set_mode) {
			NL_SET_ERR_MSG(extack,
				       "Device doesn't support restart from Bus Off");
			return -EOPNOTSUPP;
		}

		/* Do not allow a restart while not running */
		if (!(dev->flags & IFF_UP))
			return -EINVAL;
		err = can_restart_now(dev);
		if (err)
			return err;
	}

	/* CAN FD */
	err = can_dbt_changelink(dev, data, true, extack);
	if (err)
		return err;

	if (data[IFLA_CAN_TERMINATION]) {
		const u16 termval = nla_get_u16(data[IFLA_CAN_TERMINATION]);
		const unsigned int num_term = priv->termination_const_cnt;
		unsigned int i;

		if (!priv->do_set_termination) {
			NL_SET_ERR_MSG(extack,
				       "Termination is not configurable on this device");
			return -EOPNOTSUPP;
		}

		/* check whether given value is supported by the interface */
		for (i = 0; i < num_term; i++) {
			if (termval == priv->termination_const[i])
				break;
		}
		if (i >= num_term)
			return -EINVAL;

		/* Finally, set the termination value */
		err = priv->do_set_termination(dev, termval);
		if (err)
			return err;

		priv->termination = termval;
	}

	return 0;
}

static size_t can_tdc_get_size(struct data_bittiming_params *dbt_params,
			       u32 tdc_flags)
{
	bool tdc_manual = tdc_flags & CAN_CTRLMODE_TDC_MANUAL_MASK;
	size_t size;

	if (!dbt_params->tdc_const)
		return 0;

	size = nla_total_size(0);			/* nest IFLA_CAN_TDC */
	if (tdc_manual) {
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV_MIN */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV_MAX */
	}
	size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCO_MIN */
	size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCO_MAX */
	if (dbt_params->tdc_const->tdcf_max) {
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF_MIN */
		size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF_MAX */
	}

	if (tdc_flags) {
		if (tdc_manual || dbt_params->do_get_auto_tdcv)
			size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCV */
		size += nla_total_size(sizeof(u32));		/* IFLA_CAN_TDCO */
		if (dbt_params->tdc_const->tdcf_max)
			size += nla_total_size(sizeof(u32));	/* IFLA_CAN_TDCF */
	}

	return size;
}

static size_t can_data_bittiming_get_size(struct data_bittiming_params *dbt_params,
					  u32 tdc_flags)
{
	size_t size = 0;

	if (dbt_params->data_bittiming.bitrate)		/* IFLA_CAN_DATA_BITTIMING */
		size += nla_total_size(sizeof(dbt_params->data_bittiming));
	if (dbt_params->data_bittiming_const)		/* IFLA_CAN_DATA_BITTIMING_CONST */
		size += nla_total_size(sizeof(*dbt_params->data_bittiming_const));
	if (dbt_params->data_bitrate_const)		/* IFLA_CAN_DATA_BITRATE_CONST */
		size += nla_total_size(sizeof(*dbt_params->data_bitrate_const) *
				       dbt_params->data_bitrate_const_cnt);
	size += can_tdc_get_size(dbt_params, tdc_flags);/* IFLA_CAN_TDC */

	return size;
}

static size_t can_ctrlmode_ext_get_size(void)
{
	return nla_total_size(0) +		/* nest IFLA_CAN_CTRLMODE_EXT */
		nla_total_size(sizeof(u32));	/* IFLA_CAN_CTRLMODE_SUPPORTED */
}

static size_t can_get_size(const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	size_t size = 0;

	if (priv->bittiming.bitrate)				/* IFLA_CAN_BITTIMING */
		size += nla_total_size(sizeof(struct can_bittiming));
	if (priv->bittiming_const)				/* IFLA_CAN_BITTIMING_CONST */
		size += nla_total_size(sizeof(struct can_bittiming_const));
	size += nla_total_size(sizeof(struct can_clock));	/* IFLA_CAN_CLOCK */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_STATE */
	size += nla_total_size(sizeof(struct can_ctrlmode));	/* IFLA_CAN_CTRLMODE */
	size += nla_total_size(sizeof(u32));			/* IFLA_CAN_RESTART_MS */
	if (priv->do_get_berr_counter)				/* IFLA_CAN_BERR_COUNTER */
		size += nla_total_size(sizeof(struct can_berr_counter));
	if (priv->termination_const) {
		size += nla_total_size(sizeof(priv->termination));		/* IFLA_CAN_TERMINATION */
		size += nla_total_size(sizeof(*priv->termination_const) *	/* IFLA_CAN_TERMINATION_CONST */
				       priv->termination_const_cnt);
	}
	if (priv->bitrate_const)				/* IFLA_CAN_BITRATE_CONST */
		size += nla_total_size(sizeof(*priv->bitrate_const) *
				       priv->bitrate_const_cnt);
	size += sizeof(priv->bitrate_max);			/* IFLA_CAN_BITRATE_MAX */
	size += can_ctrlmode_ext_get_size();			/* IFLA_CAN_CTRLMODE_EXT */

	size += can_data_bittiming_get_size(&priv->fd,
					    priv->ctrlmode & CAN_CTRLMODE_FD_TDC_MASK);

	return size;
}

static int can_bittiming_fill_info(struct sk_buff *skb, int ifla_can_bittiming,
				   struct can_bittiming *bittiming)
{
	return bittiming->bitrate != CAN_BITRATE_UNSET &&
		bittiming->bitrate != CAN_BITRATE_UNKNOWN &&
		nla_put(skb, ifla_can_bittiming, sizeof(*bittiming), bittiming);
}

static int can_bittiming_const_fill_info(struct sk_buff *skb,
					 int ifla_can_bittiming_const,
					 const struct can_bittiming_const *bittiming_const)
{
	return bittiming_const &&
		nla_put(skb, ifla_can_bittiming_const,
			sizeof(*bittiming_const), bittiming_const);
}

static int can_bitrate_const_fill_info(struct sk_buff *skb,
				       int ifla_can_bitrate_const,
				       const u32 *bitrate_const, unsigned int cnt)
{
	return bitrate_const &&
		nla_put(skb, ifla_can_bitrate_const,
			sizeof(*bitrate_const) * cnt, bitrate_const);
}

static int can_tdc_fill_info(struct sk_buff *skb, const struct net_device *dev,
			     int ifla_can_tdc)
{
	struct can_priv *priv = netdev_priv(dev);
	struct data_bittiming_params *dbt_params;
	const struct can_tdc_const *tdc_const;
	struct can_tdc *tdc;
	struct nlattr *nest;
	bool tdc_is_enabled, tdc_manual;

	if (ifla_can_tdc == IFLA_CAN_TDC) {
		dbt_params = &priv->fd;
		tdc_is_enabled = can_fd_tdc_is_enabled(priv);
		tdc_manual = priv->ctrlmode & CAN_CTRLMODE_TDC_MANUAL;
	} else {
		return -EOPNOTSUPP; /* Place holder for CAN XL */
	}
	tdc_const = dbt_params->tdc_const;
	tdc = &dbt_params->tdc;

	if (!tdc_const)
		return 0;

	nest = nla_nest_start(skb, ifla_can_tdc);
	if (!nest)
		return -EMSGSIZE;

	if (tdc_manual &&
	    (nla_put_u32(skb, IFLA_CAN_TDC_TDCV_MIN, tdc_const->tdcv_min) ||
	     nla_put_u32(skb, IFLA_CAN_TDC_TDCV_MAX, tdc_const->tdcv_max)))
		goto err_cancel;
	if (nla_put_u32(skb, IFLA_CAN_TDC_TDCO_MIN, tdc_const->tdco_min) ||
	    nla_put_u32(skb, IFLA_CAN_TDC_TDCO_MAX, tdc_const->tdco_max))
		goto err_cancel;
	if (tdc_const->tdcf_max &&
	    (nla_put_u32(skb, IFLA_CAN_TDC_TDCF_MIN, tdc_const->tdcf_min) ||
	     nla_put_u32(skb, IFLA_CAN_TDC_TDCF_MAX, tdc_const->tdcf_max)))
		goto err_cancel;

	if (tdc_is_enabled) {
		u32 tdcv;
		int err = -EINVAL;

		if (tdc_manual) {
			tdcv = tdc->tdcv;
			err = 0;
		} else if (dbt_params->do_get_auto_tdcv) {
			err = dbt_params->do_get_auto_tdcv(dev, &tdcv);
		}
		if (!err && nla_put_u32(skb, IFLA_CAN_TDC_TDCV, tdcv))
			goto err_cancel;
		if (nla_put_u32(skb, IFLA_CAN_TDC_TDCO, tdc->tdco))
			goto err_cancel;
		if (tdc_const->tdcf_max &&
		    nla_put_u32(skb, IFLA_CAN_TDC_TDCF, tdc->tdcf))
			goto err_cancel;
	}

	nla_nest_end(skb, nest);
	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int can_ctrlmode_ext_fill_info(struct sk_buff *skb,
				      const struct can_priv *priv)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, IFLA_CAN_CTRLMODE_EXT);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, IFLA_CAN_CTRLMODE_SUPPORTED,
			priv->ctrlmode_supported)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);
	return 0;
}

static int can_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);
	struct can_ctrlmode cm = {.flags = priv->ctrlmode};
	struct can_berr_counter bec = { };
	enum can_state state = priv->state;

	if (priv->do_get_state)
		priv->do_get_state(dev, &state);

	if (can_bittiming_fill_info(skb, IFLA_CAN_BITTIMING,
				    &priv->bittiming) ||

	    can_bittiming_const_fill_info(skb, IFLA_CAN_BITTIMING_CONST,
					  priv->bittiming_const) ||

	    nla_put(skb, IFLA_CAN_CLOCK, sizeof(priv->clock), &priv->clock) ||
	    nla_put_u32(skb, IFLA_CAN_STATE, state) ||
	    nla_put(skb, IFLA_CAN_CTRLMODE, sizeof(cm), &cm) ||
	    nla_put_u32(skb, IFLA_CAN_RESTART_MS, priv->restart_ms) ||

	    (priv->do_get_berr_counter &&
	     !priv->do_get_berr_counter(dev, &bec) &&
	     nla_put(skb, IFLA_CAN_BERR_COUNTER, sizeof(bec), &bec)) ||

	    can_bittiming_fill_info(skb, IFLA_CAN_DATA_BITTIMING,
				    &priv->fd.data_bittiming) ||

	    can_bittiming_const_fill_info(skb, IFLA_CAN_DATA_BITTIMING_CONST,
					  priv->fd.data_bittiming_const) ||

	    (priv->termination_const &&
	     (nla_put_u16(skb, IFLA_CAN_TERMINATION, priv->termination) ||
	      nla_put(skb, IFLA_CAN_TERMINATION_CONST,
		      sizeof(*priv->termination_const) *
		      priv->termination_const_cnt,
		      priv->termination_const))) ||

	    can_bitrate_const_fill_info(skb, IFLA_CAN_BITRATE_CONST,
					priv->bitrate_const,
					priv->bitrate_const_cnt) ||

	    can_bitrate_const_fill_info(skb, IFLA_CAN_DATA_BITRATE_CONST,
					priv->fd.data_bitrate_const,
					priv->fd.data_bitrate_const_cnt) ||

	    (nla_put(skb, IFLA_CAN_BITRATE_MAX,
		     sizeof(priv->bitrate_max),
		     &priv->bitrate_max)) ||

	    can_tdc_fill_info(skb, dev, IFLA_CAN_TDC) ||

	    can_ctrlmode_ext_fill_info(skb, priv)
	    )

		return -EMSGSIZE;

	return 0;
}

static size_t can_get_xstats_size(const struct net_device *dev)
{
	return sizeof(struct can_device_stats);
}

static int can_fill_xstats(struct sk_buff *skb, const struct net_device *dev)
{
	struct can_priv *priv = netdev_priv(dev);

	if (nla_put(skb, IFLA_INFO_XSTATS,
		    sizeof(priv->can_stats), &priv->can_stats))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int can_newlink(struct net_device *dev,
		       struct rtnl_newlink_params *params,
		       struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static void can_dellink(struct net_device *dev, struct list_head *head)
{
}

struct rtnl_link_ops can_link_ops __read_mostly = {
	.kind		= "can",
	.netns_refund	= true,
	.maxtype	= IFLA_CAN_MAX,
	.policy		= can_policy,
	.setup		= can_setup,
	.validate	= can_validate,
	.newlink	= can_newlink,
	.changelink	= can_changelink,
	.dellink	= can_dellink,
	.get_size	= can_get_size,
	.fill_info	= can_fill_info,
	.get_xstats_size = can_get_xstats_size,
	.fill_xstats	= can_fill_xstats,
};

int can_netlink_register(void)
{
	return rtnl_link_register(&can_link_ops);
}

void can_netlink_unregister(void)
{
	rtnl_link_unregister(&can_link_ops);
}
