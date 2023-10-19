// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * DSA driver for:
 * Hirschmann Hellcreek TSN switch.
 *
 * Copyright (C) 2019,2020 Hochschule Offenburg
 * Copyright (C) 2019,2020 Linutronix GmbH
 * Authors: Kamil Alkhouri <kamil.alkhouri@hs-offenburg.de>
 *	    Kurt Kanzenbach <kurt@linutronix.de>
 */

#include <linux/ptp_classify.h>

#include "hellcreek.h"
#include "hellcreek_hwtstamp.h"
#include "hellcreek_ptp.h"

int hellcreek_get_ts_info(struct dsa_switch *ds, int port,
			  struct ethtool_ts_info *info)
{
	struct hellcreek *hellcreek = ds->priv;

	info->phc_index = hellcreek->ptp_clock ?
		ptp_clock_index(hellcreek->ptp_clock) : -1;
	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	/* enabled tx timestamping */
	info->tx_types = BIT(HWTSTAMP_TX_ON);

	/* L2 & L4 PTPv2 event rx messages are timestamped */
	info->rx_filters = BIT(HWTSTAMP_FILTER_PTP_V2_EVENT);

	return 0;
}

/* Enabling/disabling TX and RX HW timestamping for different PTP messages is
 * not available in the switch. Thus, this function only serves as a check if
 * the user requested what is actually available or not
 */
static int hellcreek_set_hwtstamp_config(struct hellcreek *hellcreek, int port,
					 struct hwtstamp_config *config)
{
	struct hellcreek_port_hwtstamp *ps =
		&hellcreek->ports[port].port_hwtstamp;
	bool tx_tstamp_enable = false;
	bool rx_tstamp_enable = false;

	/* Interaction with the timestamp hardware is prevented here.  It is
	 * enabled when this config function ends successfully
	 */
	clear_bit_unlock(HELLCREEK_HWTSTAMP_ENABLED, &ps->state);

	switch (config->tx_type) {
	case HWTSTAMP_TX_ON:
		tx_tstamp_enable = true;
		break;

	/* TX HW timestamping can't be disabled on the switch */
	case HWTSTAMP_TX_OFF:
		config->tx_type = HWTSTAMP_TX_ON;
		break;

	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	/* RX HW timestamping can't be disabled on the switch */
	case HWTSTAMP_FILTER_NONE:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
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
		rx_tstamp_enable = true;
		break;

	/* RX HW timestamping can't be enabled for all messages on the switch */
	case HWTSTAMP_FILTER_ALL:
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;

	default:
		return -ERANGE;
	}

	if (!tx_tstamp_enable)
		return -ERANGE;

	if (!rx_tstamp_enable)
		return -ERANGE;

	/* If this point is reached, then the requested hwtstamp config is
	 * compatible with the hwtstamp offered by the switch.  Therefore,
	 * enable the interaction with the HW timestamping
	 */
	set_bit(HELLCREEK_HWTSTAMP_ENABLED, &ps->state);

	return 0;
}

int hellcreek_port_hwtstamp_set(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port_hwtstamp *ps;
	struct hwtstamp_config config;
	int err;

	ps = &hellcreek->ports[port].port_hwtstamp;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = hellcreek_set_hwtstamp_config(hellcreek, port, &config);
	if (err)
		return err;

	/* Save the chosen configuration to be returned later */
	memcpy(&ps->tstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

int hellcreek_port_hwtstamp_get(struct dsa_switch *ds, int port,
				struct ifreq *ifr)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port_hwtstamp *ps;
	struct hwtstamp_config *config;

	ps = &hellcreek->ports[port].port_hwtstamp;
	config = &ps->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/* Returns a pointer to the PTP header if the caller should time stamp, or NULL
 * if the caller should not.
 */
static struct ptp_header *hellcreek_should_tstamp(struct hellcreek *hellcreek,
						  int port, struct sk_buff *skb,
						  unsigned int type)
{
	struct hellcreek_port_hwtstamp *ps =
		&hellcreek->ports[port].port_hwtstamp;
	struct ptp_header *hdr;

	hdr = ptp_parse_header(skb, type);
	if (!hdr)
		return NULL;

	if (!test_bit(HELLCREEK_HWTSTAMP_ENABLED, &ps->state))
		return NULL;

	return hdr;
}

static u64 hellcreek_get_reserved_field(const struct ptp_header *hdr)
{
	return be32_to_cpu(hdr->reserved2);
}

static void hellcreek_clear_reserved_field(struct ptp_header *hdr)
{
	hdr->reserved2 = 0;
}

static int hellcreek_ptp_hwtstamp_available(struct hellcreek *hellcreek,
					    unsigned int ts_reg)
{
	u16 status;

	status = hellcreek_ptp_read(hellcreek, ts_reg);

	if (status & PR_TS_STATUS_TS_LOST)
		dev_err(hellcreek->dev,
			"Tx time stamp lost! This should never happen!\n");

	/* If hwtstamp is not available, this means the previous hwtstamp was
	 * successfully read, and the one we need is not yet available
	 */
	return (status & PR_TS_STATUS_TS_AVAIL) ? 1 : 0;
}

/* Get nanoseconds timestamp from timestamping unit */
static u64 hellcreek_ptp_hwtstamp_read(struct hellcreek *hellcreek,
				       unsigned int ts_reg)
{
	u16 nsl, nsh;

	nsh = hellcreek_ptp_read(hellcreek, ts_reg);
	nsh = hellcreek_ptp_read(hellcreek, ts_reg);
	nsh = hellcreek_ptp_read(hellcreek, ts_reg);
	nsh = hellcreek_ptp_read(hellcreek, ts_reg);
	nsl = hellcreek_ptp_read(hellcreek, ts_reg);

	return (u64)nsl | ((u64)nsh << 16);
}

static int hellcreek_txtstamp_work(struct hellcreek *hellcreek,
				   struct hellcreek_port_hwtstamp *ps, int port)
{
	struct skb_shared_hwtstamps shhwtstamps;
	unsigned int status_reg, data_reg;
	struct sk_buff *tmp_skb;
	int ts_status;
	u64 ns = 0;

	if (!ps->tx_skb)
		return 0;

	switch (port) {
	case 2:
		status_reg = PR_TS_TX_P1_STATUS_C;
		data_reg   = PR_TS_TX_P1_DATA_C;
		break;
	case 3:
		status_reg = PR_TS_TX_P2_STATUS_C;
		data_reg   = PR_TS_TX_P2_DATA_C;
		break;
	default:
		dev_err(hellcreek->dev, "Wrong port for timestamping!\n");
		return 0;
	}

	ts_status = hellcreek_ptp_hwtstamp_available(hellcreek, status_reg);

	/* Not available yet? */
	if (ts_status == 0) {
		/* Check whether the operation of reading the tx timestamp has
		 * exceeded its allowed period
		 */
		if (time_is_before_jiffies(ps->tx_tstamp_start +
					   TX_TSTAMP_TIMEOUT)) {
			dev_err(hellcreek->dev,
				"Timeout while waiting for Tx timestamp!\n");
			goto free_and_clear_skb;
		}

		/* The timestamp should be available quickly, while getting it
		 * in high priority. Restart the work
		 */
		return 1;
	}

	mutex_lock(&hellcreek->ptp_lock);
	ns  = hellcreek_ptp_hwtstamp_read(hellcreek, data_reg);
	ns += hellcreek_ptp_gettime_seconds(hellcreek, ns);
	mutex_unlock(&hellcreek->ptp_lock);

	/* Now we have the timestamp in nanoseconds, store it in the correct
	 * structure in order to send it to the user
	 */
	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);

	tmp_skb = ps->tx_skb;
	ps->tx_skb = NULL;

	/* skb_complete_tx_timestamp() frees up the client to make another
	 * timestampable transmit.  We have to be ready for it by clearing the
	 * ps->tx_skb "flag" beforehand
	 */
	clear_bit_unlock(HELLCREEK_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

	/* Deliver a clone of the original outgoing tx_skb with tx hwtstamp */
	skb_complete_tx_timestamp(tmp_skb, &shhwtstamps);

	return 0;

free_and_clear_skb:
	dev_kfree_skb_any(ps->tx_skb);
	ps->tx_skb = NULL;
	clear_bit_unlock(HELLCREEK_HWTSTAMP_TX_IN_PROGRESS, &ps->state);

	return 0;
}

static void hellcreek_get_rxts(struct hellcreek *hellcreek,
			       struct hellcreek_port_hwtstamp *ps,
			       struct sk_buff *skb, struct sk_buff_head *rxq,
			       int port)
{
	struct skb_shared_hwtstamps *shwt;
	struct sk_buff_head received;
	unsigned long flags;

	/* The latched timestamp belongs to one of the received frames. */
	__skb_queue_head_init(&received);

	/* Lock & disable interrupts */
	spin_lock_irqsave(&rxq->lock, flags);

	/* Add the reception queue "rxq" to the "received" queue an reintialize
	 * "rxq".  From now on, we deal with "received" not with "rxq"
	 */
	skb_queue_splice_tail_init(rxq, &received);

	spin_unlock_irqrestore(&rxq->lock, flags);

	for (; skb; skb = __skb_dequeue(&received)) {
		struct ptp_header *hdr;
		unsigned int type;
		u64 ns;

		/* Get nanoseconds from ptp packet */
		type = SKB_PTP_TYPE(skb);
		hdr  = ptp_parse_header(skb, type);
		ns   = hellcreek_get_reserved_field(hdr);
		hellcreek_clear_reserved_field(hdr);

		/* Add seconds part */
		mutex_lock(&hellcreek->ptp_lock);
		ns += hellcreek_ptp_gettime_seconds(hellcreek, ns);
		mutex_unlock(&hellcreek->ptp_lock);

		/* Save time stamp */
		shwt = skb_hwtstamps(skb);
		memset(shwt, 0, sizeof(*shwt));
		shwt->hwtstamp = ns_to_ktime(ns);
		netif_rx(skb);
	}
}

static void hellcreek_rxtstamp_work(struct hellcreek *hellcreek,
				    struct hellcreek_port_hwtstamp *ps,
				    int port)
{
	struct sk_buff *skb;

	skb = skb_dequeue(&ps->rx_queue);
	if (skb)
		hellcreek_get_rxts(hellcreek, ps, skb, &ps->rx_queue, port);
}

long hellcreek_hwtstamp_work(struct ptp_clock_info *ptp)
{
	struct hellcreek *hellcreek = ptp_to_hellcreek(ptp);
	struct dsa_switch *ds = hellcreek->ds;
	int i, restart = 0;

	for (i = 0; i < ds->num_ports; i++) {
		struct hellcreek_port_hwtstamp *ps;

		if (!dsa_is_user_port(ds, i))
			continue;

		ps = &hellcreek->ports[i].port_hwtstamp;

		if (test_bit(HELLCREEK_HWTSTAMP_TX_IN_PROGRESS, &ps->state))
			restart |= hellcreek_txtstamp_work(hellcreek, ps, i);

		hellcreek_rxtstamp_work(hellcreek, ps, i);
	}

	return restart ? 1 : -1;
}

void hellcreek_port_txtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *skb)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port_hwtstamp *ps;
	struct ptp_header *hdr;
	struct sk_buff *clone;
	unsigned int type;

	ps = &hellcreek->ports[port].port_hwtstamp;

	type = ptp_classify_raw(skb);
	if (type == PTP_CLASS_NONE)
		return;

	/* Make sure the message is a PTP message that needs to be timestamped
	 * and the interaction with the HW timestamping is enabled. If not, stop
	 * here
	 */
	hdr = hellcreek_should_tstamp(hellcreek, port, skb, type);
	if (!hdr)
		return;

	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	if (test_and_set_bit_lock(HELLCREEK_HWTSTAMP_TX_IN_PROGRESS,
				  &ps->state)) {
		kfree_skb(clone);
		return;
	}

	ps->tx_skb = clone;

	/* store the number of ticks occurred since system start-up till this
	 * moment
	 */
	ps->tx_tstamp_start = jiffies;

	ptp_schedule_worker(hellcreek->ptp_clock, 0);
}

bool hellcreek_port_rxtstamp(struct dsa_switch *ds, int port,
			     struct sk_buff *skb, unsigned int type)
{
	struct hellcreek *hellcreek = ds->priv;
	struct hellcreek_port_hwtstamp *ps;
	struct ptp_header *hdr;

	ps = &hellcreek->ports[port].port_hwtstamp;

	/* This check only fails if the user did not initialize hardware
	 * timestamping beforehand.
	 */
	if (ps->tstamp_config.rx_filter != HWTSTAMP_FILTER_PTP_V2_EVENT)
		return false;

	/* Make sure the message is a PTP message that needs to be timestamped
	 * and the interaction with the HW timestamping is enabled. If not, stop
	 * here
	 */
	hdr = hellcreek_should_tstamp(hellcreek, port, skb, type);
	if (!hdr)
		return false;

	SKB_PTP_TYPE(skb) = type;

	skb_queue_tail(&ps->rx_queue, skb);

	ptp_schedule_worker(hellcreek->ptp_clock, 0);

	return true;
}

static void hellcreek_hwtstamp_port_setup(struct hellcreek *hellcreek, int port)
{
	struct hellcreek_port_hwtstamp *ps =
		&hellcreek->ports[port].port_hwtstamp;

	skb_queue_head_init(&ps->rx_queue);
}

int hellcreek_hwtstamp_setup(struct hellcreek *hellcreek)
{
	struct dsa_switch *ds = hellcreek->ds;
	int i;

	/* Initialize timestamping ports. */
	for (i = 0; i < ds->num_ports; ++i) {
		if (!dsa_is_user_port(ds, i))
			continue;

		hellcreek_hwtstamp_port_setup(hellcreek, i);
	}

	/* Select the synchronized clock as the source timekeeper for the
	 * timestamps and enable inline timestamping.
	 */
	hellcreek_ptp_write(hellcreek, PR_SETTINGS_C_TS_SRC_TK_MASK |
			    PR_SETTINGS_C_RES3TS,
			    PR_SETTINGS_C);

	return 0;
}

void hellcreek_hwtstamp_free(struct hellcreek *hellcreek)
{
	/* Nothing todo */
}
