// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

#include <linux/can/dev.h>

void can_sjw_set_default(struct can_bittiming *bt)
{
	if (bt->sjw)
		return;

	/* If user space provides no sjw, use 1 as default */
	bt->sjw = 1;
}

int can_sjw_check(const struct net_device *dev, const struct can_bittiming *bt,
		  const struct can_bittiming_const *btc, struct netlink_ext_ack *extack)
{
	if (bt->sjw > btc->sjw_max)
		return -ERANGE;

	return 0;
}

/* Checks the validity of the specified bit-timing parameters prop_seg,
 * phase_seg1, phase_seg2 and sjw and tries to determine the bitrate
 * prescaler value brp. You can find more information in the header
 * file linux/can/netlink.h.
 */
static int can_fixup_bittiming(const struct net_device *dev, struct can_bittiming *bt,
			       const struct can_bittiming_const *btc,
			       struct netlink_ext_ack *extack)
{
	const struct can_priv *priv = netdev_priv(dev);
	unsigned int tseg1;
	u64 brp64;
	int err;

	can_sjw_set_default(bt);

	err = can_sjw_check(dev, bt, btc, extack);
	if (err)
		return err;

	tseg1 = bt->prop_seg + bt->phase_seg1;
	if (tseg1 < btc->tseg1_min || tseg1 > btc->tseg1_max ||
	    bt->phase_seg2 < btc->tseg2_min || bt->phase_seg2 > btc->tseg2_max)
		return -ERANGE;

	brp64 = (u64)priv->clock.freq * (u64)bt->tq;
	if (btc->brp_inc > 1)
		do_div(brp64, btc->brp_inc);
	brp64 += 500000000UL - 1;
	do_div(brp64, 1000000000UL); /* the practicable BRP */
	if (btc->brp_inc > 1)
		brp64 *= btc->brp_inc;
	bt->brp = (u32)brp64;

	if (bt->brp < btc->brp_min || bt->brp > btc->brp_max)
		return -EINVAL;

	bt->bitrate = priv->clock.freq / (bt->brp * can_bit_time(bt));
	bt->sample_point = ((CAN_SYNC_SEG + tseg1) * 1000) / can_bit_time(bt);
	bt->tq = DIV_U64_ROUND_CLOSEST(mul_u32_u32(bt->brp, NSEC_PER_SEC),
				       priv->clock.freq);

	return 0;
}

/* Checks the validity of predefined bitrate settings */
static int
can_validate_bitrate(const struct net_device *dev, const struct can_bittiming *bt,
		     const u32 *bitrate_const,
		     const unsigned int bitrate_const_cnt,
		     struct netlink_ext_ack *extack)
{
	unsigned int i;

	for (i = 0; i < bitrate_const_cnt; i++) {
		if (bt->bitrate == bitrate_const[i])
			return 0;
	}

	return -EINVAL;
}

int can_get_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		      const struct can_bittiming_const *btc,
		      const u32 *bitrate_const,
		      const unsigned int bitrate_const_cnt,
		      struct netlink_ext_ack *extack)
{
	/* Depending on the given can_bittiming parameter structure the CAN
	 * timing parameters are calculated based on the provided bitrate OR
	 * alternatively the CAN timing parameters (tq, prop_seg, etc.) are
	 * provided directly which are then checked and fixed up.
	 */
	if (!bt->tq && bt->bitrate && btc)
		return can_calc_bittiming(dev, bt, btc, extack);
	if (bt->tq && !bt->bitrate && btc)
		return can_fixup_bittiming(dev, bt, btc, extack);
	if (!bt->tq && bt->bitrate && bitrate_const)
		return can_validate_bitrate(dev, bt, bitrate_const,
					    bitrate_const_cnt, extack);

	return -EINVAL;
}
