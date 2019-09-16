// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch hardware timestamping support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 National Instruments
 *      Erik Hons <erik.hons@ni.com>
 *      Brandon Streiff <brandon.streiff@ni.com>
 *      Dane Wagner <dane.wagner@ni.com>
 */

#include "chip.h"
#include "global2.h"
#include "hwtstamp.h"
#include "ptp.h"
#include <linux/ptp_classify.h>

#define SKB_PTP_TYPE(__skb) (*(unsigned int *)((__skb)->cb))

static int mv88e6xxx_port_ptp_read(struct mv88e6xxx_chip *chip, int port,
				   int addr, u16 *data, int len)
{
	if (!chip->info->ops->avb_ops->port_ptp_read)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->port_ptp_read(chip, port, addr,
						       data, len);
}

static int mv88e6xxx_port_ptp_write(struct mv88e6xxx_chip *chip, int port,
				    int addr, u16 data)
{
	if (!chip->info->ops->avb_ops->port_ptp_write)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->port_ptp_write(chip, port, addr,
							data);
}

static int mv88e6xxx_ptp_write(struct mv88e6xxx_chip *chip, int addr,
			       u16 data)
{
	if (!chip->info->ops->avb_ops->ptp_write)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->ptp_write(chip, addr, data);
}

static int mv88e6xxx_ptp_read(struct mv88e6xxx_chip *chip, int addr,
			      u16 *data)
{
	if (!chip->info->ops->avb_ops->ptp_read)
		return -EOPNOTSUPP;

	return chip->info->ops->avb_ops->ptp_read(chip, addr, data, 1);
}

/* TX_TSTAMP_TIMEOUT: This limits the time spent polling for a TX
 * timestamp. When working properly, hardware will produce a timestamp
 * within 1ms. Software may enounter delays due to MDIO contention, so
 * the timeout is set accordingly.
 */
#define TX_TSTAMP_TIMEOUT	msecs_to_jiffies(40)

int mv88e6xxx_get_ts_info(struct dsa_switch *ds, int port,
			  struct ethtool_ts_info *info)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops;
	struct mv88e6xxx_chip *chip;

	chip = ds->priv;
	ptp_ops = chip->info->ops->ptp_ops;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->phc_index = ptp_clock_index(chip->ptp_clock);
	info->tx_types =
		(1 << HWTSTAMP_TX_OFF) |
		(1 << HWTSTAMP_TX_ON);
	info->rx_filters = ptp_ops->rx_filters;

	return 0;
}

static int mv88e6xxx_set_hwtstamp_config(struct mv88e6xxx_chip *chip, int port,
					 struct hwtstamp_config *config)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	bool tstamp_enable = false;

	/* Prevent the TX/RX paths from trying to interact with the
	 * timestamp hardware while we reconfigure it.
	 */
	clear_bit_unlock(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state);

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tstamp_enable = false;
		break;
	case HWTSTAMP_TX_ON:
		tstamp_enable = true;
		break;
	default:
		return -ERANGE;
	}

	/* The switch supports timestamping both L2 and L4; one cannot be
	 * disabled independently of the other.
	 */

	if (!(BIT(config->rx_filter) & ptp_ops->rx_filters)) {
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		dev_dbg(chip->dev, "Unsupported rx_filter %d\n",
			config->rx_filter);
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tstamp_enable = false;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_ALL:
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	mv88e6xxx_reg_lock(chip);
	if (tstamp_enable) {
		chip->enable_count += 1;
		if (chip->enable_count == 1 && ptp_ops->global_enable)
			ptp_ops->global_enable(chip);
		if (ptp_ops->port_enable)
			ptp_ops->port_enable(chip, port);
	} else {
		if (ptp_ops->port_disable)
			ptp_ops->port_disable(chip, port);
		chip->enable_count -= 1;
		if (chip->enable_count == 0 && ptp_ops->global_disable)
			ptp_ops->global_disable(chip);
	}
	mv88e6xxx_reg_unlock(chip);

	/* Once hardware has been configured, enable timestamp checks
	 * in the RX/TX paths.
	 */
	if (tstamp_enable)
		set_bit(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state);

	return 0;
}

int mv88e6xxx_port_hwtstamp_set(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	struct hwtstamp_config config;
	int err;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = mv88e6xxx_set_hwtstamp_config(chip, port, &config);
	if (err)
		return err;

	/* Save the chosen configuration to be returned later. */
	memcpy(&ps->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int mv88e6xxx_port_hwtstamp_get(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	struct hwtstamp_config *config = &ps->tstamp_config;

	if (!chip->info->ptp_support)
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/* Get the start of the PTP header in this skb */
static u8 *parse_ptp_header(struct sk_buff *skb, unsigned int type)
{
	u8 *data = skb_mac_header(skb);
	unsigned int offset = 0;

	if (type & PTP_CLASS_VLAN)
		offset += VLAN_HLEN;

	switch (type & PTP_CLASS_PMASK) {
	case PTP_CLASS_IPV4:
		offset += ETH_HLEN + IPV4_HLEN(data + offset) + UDP_HLEN;
		break;
	case PTP_CLASS_IPV6:
		offset += ETH_HLEN + IP6_HLEN + UDP_HLEN;
		break;
	case PTP_CLASS_L2:
		offset += ETH_HLEN;
		break;
	default:
		return NULL;
	}

	/* Ensure that the entire header is present in this packet. */
	if (skb->len + ETH_HLEN < offset + 34)
		return NULL;

	return data + offset;
}

/* Returns a pointer to the PTP header if the caller should time stamp,
 * or NULL if the caller should not.
 */
static u8 *mv88e6xxx_should_tstamp(struct mv88e6xxx_chip *chip, int port,
				   struct sk_buff *skb, unsigned int type)
{
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	u8 *hdr;

	if (!chip->info->ptp_support)
		return NULL;

	hdr = parse_ptp_header(skb, type);
	if (!hdr)
		return NULL;

	if (!test_bit(MV88E6XXX_HWTSTAMP_ENABLED, &ps->state))
		return NULL;

	return hdr;
}

static int mv88e6xxx_ts_valid(u16 status)
{
	if (!(status & MV88E6XXX_PTP_TS_VALID))
		return 0;
	if (status & MV88E6XXX_PTP_TS_STATUS_MASK)
		return 0;
	return 1;
}

static int seq_match(struct sk_buff *skb, u16 ts_seqid)
{
	unsigned int type = SKB_PTP_TYPE(skb);
	u8 *hdr = parse_ptp_header(skb, type);
	__be16 *seqid;

	seqid = (__be16 *)(hdr + OFF_PTP_SEQUENCE_ID);

	return ts_seqid == ntohs(*seqid);
}

static void mv88e6xxx_get_rxts(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_port_hwtstamp *ps,
			       struct sk_buff *skb, u16 reg,
			       struct sk_buff_head *rxq)
{
	u16 buf[4] = { 0 }, status, seq_id;
	struct skb_shared_hwtstamps *shwt;
	struct sk_buff_head received;
	u64 ns, timelo, timehi;
	unsigned long flags;
	int err;

	/* The latched timestamp belongs to one of the received frames. */
	__skb_queue_head_init(&received);
	spin_lock_irqsave(&rxq->lock, flags);
	skb_queue_splice_tail_init(rxq, &received);
	spin_unlock_irqrestore(&rxq->lock, flags);

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_ptp_read(chip, ps->port_id,
				      reg, buf, ARRAY_SIZE(buf));
	mv88e6xxx_reg_unlock(chip);
	if (err)
		pr_err("failed to get the receive time stamp\n");

	status = buf[0];
	timelo = buf[1];
	timehi = buf[2];
	seq_id = buf[3];

	if (status & MV88E6XXX_PTP_TS_VALID) {
		mv88e6xxx_reg_lock(chip);
		err = mv88e6xxx_port_ptp_write(chip, ps->port_id, reg, 0);
		mv88e6xxx_reg_unlock(chip);
		if (err)
			pr_err("failed to clear the receive status\n");
	}
	/* Since the device can only handle one time stamp at a time,
	 * we purge any extra frames from the queue.
	 */
	for ( ; skb; skb = __skb_dequeue(&received)) {
		if (mv88e6xxx_ts_valid(status) && seq_match(skb, seq_id)) {
			ns = timehi << 16 | timelo;

			mv88e6xxx_reg_lock(chip);
			ns = timecounter_cyc2time(&chip->tstamp_tc, ns);
			mv88e6xxx_reg_unlock(chip);
			shwt = skb_hwtstamps(skb);
			memset(shwt, 0, sizeof(*shwt));
			shwt->hwtstamp = ns_to_ktime(ns);
			status &= ~MV88E6XXX_PTP_TS_VALID;
		}
		netif_rx_ni(skb);
	}
}

static void mv88e6xxx_rxtstamp_work(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_port_hwtstamp *ps)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	struct sk_buff *skb;

	skb = skb_dequeue(&ps->rx_queue);

	if (skb)
		mv88e6xxx_get_rxts(chip, ps, skb, ptp_ops->arr0_sts_reg,
				   &ps->rx_queue);

	skb = skb_dequeue(&ps->rx_queue2);
	if (skb)
		mv88e6xxx_get_rxts(chip, ps, skb, ptp_ops->arr1_sts_reg,
				   &ps->rx_queue2);
}

static int is_pdelay_resp(u8 *msgtype)
{
	return (*msgtype & 0xf) == 3;
}

bool mv88e6xxx_port_rxtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *skb, unsigned int type)
{
	struct mv88e6xxx_port_hwtstamp *ps;
	struct mv88e6xxx_chip *chip;
	u8 *hdr;

	chip = ds->priv;
	ps = &chip->port_hwtstamp[port];

	if (ps->tstamp_config.rx_filter != HWTSTAMP_FILTER_PTP_V2_EVENT)
		return false;

	hdr = mv88e6xxx_should_tstamp(chip, port, skb, type);
	if (!hdr)
		return false;

	SKB_PTP_TYPE(skb) = type;

	if (is_pdelay_resp(hdr))
		skb_queue_tail(&ps->rx_queue2, skb);
	else
		skb_queue_tail(&ps->rx_queue, skb);

	ptp_schedule_worker(chip->ptp_clock, 0);

	return true;
}

static int mv88e6xxx_txtstamp_work(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_port_hwtstamp *ps)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	struct skb_shared_hwtstamps shhwtstamps;
	u16 departure_block[4], status;
	struct sk_buff *tmp_skb;
	u32 time_raw;
	int err;
	u64 ns;

	if (!ps->tx_skb)
		return 0;

	mv88e6xxx_reg_lock(chip);
	err = mv88e6xxx_port_ptp_read(chip, ps->port_id,
				      ptp_ops->dep_sts_reg,
				      departure_block,
				      ARRAY_SIZE(departure_block));
	mv88e6xxx_reg_unlock(chip);

	if (err)
		goto free_and_clear_skb;

	if (!(departure_block[0] & MV88E6XXX_PTP_TS_VALID)) {
		if (time_is_before_jiffies(ps->tx_tstamp_start +
					   TX_TSTAMP_TIMEOUT)) {
			dev_warn(chip->dev, "p%d: clearing tx timestamp hang\n",
				 ps->port_id);
			goto free_and_clear_skb;
		}
		/* The timestamp should be available quickly, while getting it
		 * is high priority and time bounded to only 10ms. A poll is
		 * warranted so restart the work.
		 */
		return 1;
	}

	/* We have the timestamp; go ahead and clear valid now */
	mv88e6xxx_reg_lock(chip);
	mv88e6xxx_port_ptp_write(chip, ps->port_id, ptp_ops->dep_sts_reg, 0);
	mv88e6xxx_reg_unlock(chip);

	status = departure_block[0] & MV88E6XXX_PTP_TS_STATUS_MASK;
	if (status != MV88E6XXX_PTP_TS_STATUS_NORMAL) {
		dev_warn(chip->dev, "p%d: tx timestamp overrun\n", ps->port_id);
		goto free_and_clear_skb;
	}

	if (departure_block[3] != ps->tx_seq_id) {
		dev_warn(chip->dev, "p%d: unexpected seq. id\n", ps->port_id);
		goto free_and_clear_skb;
	}

	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	time_raw = ((u32)departure_block[2] << 16) | departure_block[1];
	mv88e6xxx_reg_lock(chip);
	ns = timecounter_cyc2time(&chip->tstamp_tc, time_raw);
	mv88e6xxx_reg_unlock(chip);
	shhwtstamps.hwtstamp = ns_to_ktime(ns);

	dev_dbg(chip->dev,
		"p%d: txtstamp %llx status 0x%04x skb ID 0x%04x hw ID 0x%04x\n",
		ps->port_id, ktime_to_ns(shhwtstamps.hwtstamp),
		departure_block[0], ps->tx_seq_id, departure_block[3]);

	/* skb_complete_tx_timestamp() will free up the client to make
	 * another timestamp-able transmit. We have to be ready for it
	 * -- by clearing the ps->tx_skb "flag" -- beforehand.
	 */

	tmp_skb = ps->tx_skb;
	ps->tx_skb = NULL;
	clear_bit_unlock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state);
	skb_complete_tx_timestamp(tmp_skb, &shhwtstamps);

	return 0;

free_and_clear_skb:
	dev_kfree_skb_any(ps->tx_skb);
	ps->tx_skb = NULL;
	clear_bit_unlock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

	return 0;
}

long mv88e6xxx_hwtstamp_work(struct ptp_clock_info *ptp)
{
	struct mv88e6xxx_chip *chip = ptp_to_chip(ptp);
	struct dsa_switch *ds = chip->ds;
	struct mv88e6xxx_port_hwtstamp *ps;
	int i, restart = 0;

	for (i = 0; i < ds->num_ports; i++) {
		if (!dsa_is_user_port(ds, i))
			continue;

		ps = &chip->port_hwtstamp[i];
		if (test_bit(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS, &ps->state))
			restart |= mv88e6xxx_txtstamp_work(chip, ps);

		mv88e6xxx_rxtstamp_work(chip, ps);
	}

	return restart ? 1 : -1;
}

bool mv88e6xxx_port_txtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *clone, unsigned int type)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];
	__be16 *seq_ptr;
	u8 *hdr;

	if (!(skb_shinfo(clone)->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	hdr = mv88e6xxx_should_tstamp(chip, port, clone, type);
	if (!hdr)
		return false;

	seq_ptr = (__be16 *)(hdr + OFF_PTP_SEQUENCE_ID);

	if (test_and_set_bit_lock(MV88E6XXX_HWTSTAMP_TX_IN_PROGRESS,
				  &ps->state))
		return false;

	ps->tx_skb = clone;
	ps->tx_tstamp_start = jiffies;
	ps->tx_seq_id = be16_to_cpup(seq_ptr);

	ptp_schedule_worker(chip->ptp_clock, 0);
	return true;
}

int mv88e6165_global_disable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_ptp_read(chip, MV88E6165_PTP_CFG, &val);
	if (err)
		return err;
	val |= MV88E6165_PTP_CFG_DISABLE_PTP;

	return mv88e6xxx_ptp_write(chip, MV88E6165_PTP_CFG, val);
}

int mv88e6165_global_enable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_ptp_read(chip, MV88E6165_PTP_CFG, &val);
	if (err)
		return err;

	val &= ~(MV88E6165_PTP_CFG_DISABLE_PTP | MV88E6165_PTP_CFG_TSPEC_MASK);

	return mv88e6xxx_ptp_write(chip, MV88E6165_PTP_CFG, val);
}

int mv88e6352_hwtstamp_port_disable(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_ptp_write(chip, port, MV88E6XXX_PORT_PTP_CFG0,
					MV88E6XXX_PORT_PTP_CFG0_DISABLE_PTP);
}

int mv88e6352_hwtstamp_port_enable(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_port_ptp_write(chip, port, MV88E6XXX_PORT_PTP_CFG0,
					MV88E6XXX_PORT_PTP_CFG0_DISABLE_TSPEC_MATCH);
}

static int mv88e6xxx_hwtstamp_port_setup(struct mv88e6xxx_chip *chip, int port)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	struct mv88e6xxx_port_hwtstamp *ps = &chip->port_hwtstamp[port];

	ps->port_id = port;

	skb_queue_head_init(&ps->rx_queue);
	skb_queue_head_init(&ps->rx_queue2);

	if (ptp_ops->port_disable)
		return ptp_ops->port_disable(chip, port);

	return 0;
}

int mv88e6xxx_hwtstamp_setup(struct mv88e6xxx_chip *chip)
{
	const struct mv88e6xxx_ptp_ops *ptp_ops = chip->info->ops->ptp_ops;
	int err;
	int i;

	/* Disable timestamping on all ports. */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		err = mv88e6xxx_hwtstamp_port_setup(chip, i);
		if (err)
			return err;
	}

	/* Disable PTP globally */
	if (ptp_ops->global_disable) {
		err = ptp_ops->global_disable(chip);
		if (err)
			return err;
	}

	/* Set the ethertype of L2 PTP messages */
	err = mv88e6xxx_ptp_write(chip, MV88E6XXX_PTP_GC_ETYPE, ETH_P_1588);
	if (err)
		return err;

	/* MV88E6XXX_PTP_MSG_TYPE is a mask of PTP message types to
	 * timestamp. This affects all ports that have timestamping enabled,
	 * but the timestamp config is per-port; thus we configure all events
	 * here and only support the HWTSTAMP_FILTER_*_EVENT filter types.
	 */
	err = mv88e6xxx_ptp_write(chip, MV88E6XXX_PTP_MSGTYPE,
				  MV88E6XXX_PTP_MSGTYPE_ALL_EVENT);
	if (err)
		return err;

	/* Use ARRIVAL1 for peer delay response messages. */
	err = mv88e6xxx_ptp_write(chip, MV88E6XXX_PTP_TS_ARRIVAL_PTR,
				  MV88E6XXX_PTP_MSGTYPE_PDLAY_RES);
	if (err)
		return err;

	/* 88E6341 devices default to timestamping at the PHY, but this has
	 * a hardware issue that results in unreliable timestamps. Force
	 * these devices to timestamp at the MAC.
	 */
	if (chip->info->family == MV88E6XXX_FAMILY_6341) {
		u16 val = MV88E6341_PTP_CFG_UPDATE |
			  MV88E6341_PTP_CFG_MODE_IDX |
			  MV88E6341_PTP_CFG_MODE_TS_AT_MAC;
		err = mv88e6xxx_ptp_write(chip, MV88E6341_PTP_CFG, val);
		if (err)
			return err;
	}

	return 0;
}

void mv88e6xxx_hwtstamp_free(struct mv88e6xxx_chip *chip)
{
}
