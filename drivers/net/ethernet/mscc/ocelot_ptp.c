// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot PTP clock driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 * Copyright 2020 NXP
 */
#include <linux/time64.h>

#include <linux/dsa/ocelot.h>
#include <linux/ptp_classify.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot.h>
#include "ocelot.h"

#define OCELOT_PTP_TX_TSTAMP_TIMEOUT		(5 * HZ)

int ocelot_ptp_gettime64(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	time64_t s;
	u32 val;
	s64 ns;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_SAVE);
	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	s = ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN) & 0xffff;
	s <<= 32;
	s += ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
	ns = ocelot_read_rix(ocelot, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);

	/* Deal with negative values */
	if (ns >= 0x3ffffff0 && ns <= 0x3fffffff) {
		s--;
		ns &= 0xf;
		ns += 999999984;
	}

	set_normalized_timespec64(ts, s, ns);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_gettime64);

int ocelot_ptp_settime64(struct ptp_clock_info *ptp,
			 const struct timespec64 *ts)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	ocelot_write_rix(ocelot, lower_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_LSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, upper_32_bits(ts->tv_sec), PTP_PIN_TOD_SEC_MSB,
			 TOD_ACC_PIN);
	ocelot_write_rix(ocelot, ts->tv_nsec, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_LOAD);

	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);

	if (ocelot->ops->tas_clock_adjust)
		ocelot->ops->tas_clock_adjust(ocelot);

	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_settime64);

int ocelot_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	if (delta > -(NSEC_PER_SEC / 2) && delta < (NSEC_PER_SEC / 2)) {
		struct ocelot *ocelot = container_of(ptp, struct ocelot,
						     ptp_info);
		unsigned long flags;
		u32 val;

		spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK |
			 PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, 0, PTP_PIN_TOD_SEC_MSB, TOD_ACC_PIN);
		ocelot_write_rix(ocelot, delta, PTP_PIN_TOD_NSEC, TOD_ACC_PIN);

		val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);
		val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK |
			 PTP_PIN_CFG_DOM);
		val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_DELTA);

		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);

		spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);

		if (ocelot->ops->tas_clock_adjust)
			ocelot->ops->tas_clock_adjust(ocelot);
	} else {
		/* Fall back using ocelot_ptp_settime64 which is not exact. */
		struct timespec64 ts;
		u64 now;

		ocelot_ptp_gettime64(ptp, &ts);

		now = ktime_to_ns(timespec64_to_ktime(ts));
		ts = ns_to_timespec64(now + delta);

		ocelot_ptp_settime64(ptp, &ts);
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_adjtime);

int ocelot_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	u32 unit = 0, direction = 0;
	unsigned long flags;
	u64 adj = 0;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	if (!scaled_ppm)
		goto disable_adj;

	if (scaled_ppm < 0) {
		direction = PTP_CFG_CLK_ADJ_CFG_DIR;
		scaled_ppm = -scaled_ppm;
	}

	adj = PSEC_PER_SEC << 16;
	do_div(adj, scaled_ppm);
	do_div(adj, 1000);

	/* If the adjustment value is too large, use ns instead */
	if (adj >= (1L << 30)) {
		unit = PTP_CFG_CLK_ADJ_FREQ_NS;
		do_div(adj, 1000);
	}

	/* Still too big */
	if (adj >= (1L << 30))
		goto disable_adj;

	ocelot_write(ocelot, unit | adj, PTP_CLK_CFG_ADJ_FREQ);
	ocelot_write(ocelot, PTP_CFG_CLK_ADJ_CFG_ENA | direction,
		     PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;

disable_adj:
	ocelot_write(ocelot, 0, PTP_CLK_CFG_ADJ_CFG);

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_adjfine);

int ocelot_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
		      enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_PEROUT:
		break;
	case PTP_PF_EXTTS:
	case PTP_PF_PHYSYNC:
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_verify);

int ocelot_ptp_enable(struct ptp_clock_info *ptp,
		      struct ptp_clock_request *rq, int on)
{
	struct ocelot *ocelot = container_of(ptp, struct ocelot, ptp_info);
	struct timespec64 ts_phase, ts_period;
	enum ocelot_ptp_pins ptp_pin;
	unsigned long flags;
	bool pps = false;
	int pin = -1;
	s64 wf_high;
	s64 wf_low;
	u32 val;

	switch (rq->type) {
	case PTP_CLK_REQ_PEROUT:
		/* Reject requests with unsupported flags */
		if (rq->perout.flags & ~(PTP_PEROUT_DUTY_CYCLE |
					 PTP_PEROUT_PHASE))
			return -EOPNOTSUPP;

		pin = ptp_find_pin(ocelot->ptp_clock, PTP_PF_PEROUT,
				   rq->perout.index);
		if (pin == 0)
			ptp_pin = PTP_PIN_0;
		else if (pin == 1)
			ptp_pin = PTP_PIN_1;
		else if (pin == 2)
			ptp_pin = PTP_PIN_2;
		else if (pin == 3)
			ptp_pin = PTP_PIN_3;
		else
			return -EBUSY;

		ts_period.tv_sec = rq->perout.period.sec;
		ts_period.tv_nsec = rq->perout.period.nsec;

		if (ts_period.tv_sec == 1 && ts_period.tv_nsec == 0)
			pps = true;

		/* Handle turning off */
		if (!on) {
			spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);
			val = PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_IDLE);
			ocelot_write_rix(ocelot, val, PTP_PIN_CFG, ptp_pin);
			spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
			break;
		}

		if (rq->perout.flags & PTP_PEROUT_PHASE) {
			ts_phase.tv_sec = rq->perout.phase.sec;
			ts_phase.tv_nsec = rq->perout.phase.nsec;
		} else {
			/* Compatibility */
			ts_phase.tv_sec = rq->perout.start.sec;
			ts_phase.tv_nsec = rq->perout.start.nsec;
		}
		if (ts_phase.tv_sec || (ts_phase.tv_nsec && !pps)) {
			dev_warn(ocelot->dev,
				 "Absolute start time not supported!\n");
			dev_warn(ocelot->dev,
				 "Accept nsec for PPS phase adjustment, otherwise start time should be 0 0.\n");
			return -EINVAL;
		}

		/* Calculate waveform high and low times */
		if (rq->perout.flags & PTP_PEROUT_DUTY_CYCLE) {
			struct timespec64 ts_on;

			ts_on.tv_sec = rq->perout.on.sec;
			ts_on.tv_nsec = rq->perout.on.nsec;

			wf_high = timespec64_to_ns(&ts_on);
		} else {
			if (pps) {
				wf_high = 1000;
			} else {
				wf_high = timespec64_to_ns(&ts_period);
				wf_high = div_s64(wf_high, 2);
			}
		}

		wf_low = timespec64_to_ns(&ts_period);
		wf_low -= wf_high;

		/* Handle PPS request */
		if (pps) {
			spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);
			ocelot_write_rix(ocelot, ts_phase.tv_nsec,
					 PTP_PIN_WF_LOW_PERIOD, ptp_pin);
			ocelot_write_rix(ocelot, wf_high,
					 PTP_PIN_WF_HIGH_PERIOD, ptp_pin);
			val = PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_CLOCK);
			val |= PTP_PIN_CFG_SYNC;
			ocelot_write_rix(ocelot, val, PTP_PIN_CFG, ptp_pin);
			spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
			break;
		}

		/* Handle periodic clock */
		if (wf_high > 0x3fffffff || wf_high <= 0x6)
			return -EINVAL;
		if (wf_low > 0x3fffffff || wf_low <= 0x6)
			return -EINVAL;

		spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);
		ocelot_write_rix(ocelot, wf_low, PTP_PIN_WF_LOW_PERIOD,
				 ptp_pin);
		ocelot_write_rix(ocelot, wf_high, PTP_PIN_WF_HIGH_PERIOD,
				 ptp_pin);
		val = PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_CLOCK);
		ocelot_write_rix(ocelot, val, PTP_PIN_CFG, ptp_pin);
		spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
EXPORT_SYMBOL(ocelot_ptp_enable);

static void ocelot_populate_l2_ptp_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_ETYPE;
	*(__be16 *)trap->key.etype.etype.value = htons(ETH_P_1588);
	*(__be16 *)trap->key.etype.etype.mask = htons(0xffff);
}

static void
ocelot_populate_ipv4_ptp_event_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV4;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv4.dport.value = PTP_EV_PORT;
	trap->key.ipv4.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv6_ptp_event_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV6;
	trap->key.ipv6.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv6.proto.mask[0] = 0xff;
	trap->key.ipv6.dport.value = PTP_EV_PORT;
	trap->key.ipv6.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv4_ptp_general_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV4;
	trap->key.ipv4.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv4.proto.mask[0] = 0xff;
	trap->key.ipv4.dport.value = PTP_GEN_PORT;
	trap->key.ipv4.dport.mask = 0xffff;
}

static void
ocelot_populate_ipv6_ptp_general_trap_key(struct ocelot_vcap_filter *trap)
{
	trap->key_type = OCELOT_VCAP_KEY_IPV6;
	trap->key.ipv6.proto.value[0] = IPPROTO_UDP;
	trap->key.ipv6.proto.mask[0] = 0xff;
	trap->key.ipv6.dport.value = PTP_GEN_PORT;
	trap->key.ipv6.dport.mask = 0xffff;
}

static int ocelot_l2_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long l2_cookie = OCELOT_VCAP_IS2_L2_PTP_TRAP(ocelot);

	return ocelot_trap_add(ocelot, port, l2_cookie, true,
			       ocelot_populate_l2_ptp_trap_key);
}

static int ocelot_l2_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long l2_cookie = OCELOT_VCAP_IS2_L2_PTP_TRAP(ocelot);

	return ocelot_trap_del(ocelot, port, l2_cookie);
}

static int ocelot_ipv4_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long ipv4_gen_cookie = OCELOT_VCAP_IS2_IPV4_GEN_PTP_TRAP(ocelot);
	unsigned long ipv4_ev_cookie = OCELOT_VCAP_IS2_IPV4_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_add(ocelot, port, ipv4_ev_cookie, true,
			      ocelot_populate_ipv4_ptp_event_trap_key);
	if (err)
		return err;

	err = ocelot_trap_add(ocelot, port, ipv4_gen_cookie, false,
			      ocelot_populate_ipv4_ptp_general_trap_key);
	if (err)
		ocelot_trap_del(ocelot, port, ipv4_ev_cookie);

	return err;
}

static int ocelot_ipv4_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long ipv4_gen_cookie = OCELOT_VCAP_IS2_IPV4_GEN_PTP_TRAP(ocelot);
	unsigned long ipv4_ev_cookie = OCELOT_VCAP_IS2_IPV4_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_del(ocelot, port, ipv4_ev_cookie);
	err |= ocelot_trap_del(ocelot, port, ipv4_gen_cookie);
	return err;
}

static int ocelot_ipv6_ptp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long ipv6_gen_cookie = OCELOT_VCAP_IS2_IPV6_GEN_PTP_TRAP(ocelot);
	unsigned long ipv6_ev_cookie = OCELOT_VCAP_IS2_IPV6_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_add(ocelot, port, ipv6_ev_cookie, true,
			      ocelot_populate_ipv6_ptp_event_trap_key);
	if (err)
		return err;

	err = ocelot_trap_add(ocelot, port, ipv6_gen_cookie, false,
			      ocelot_populate_ipv6_ptp_general_trap_key);
	if (err)
		ocelot_trap_del(ocelot, port, ipv6_ev_cookie);

	return err;
}

static int ocelot_ipv6_ptp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long ipv6_gen_cookie = OCELOT_VCAP_IS2_IPV6_GEN_PTP_TRAP(ocelot);
	unsigned long ipv6_ev_cookie = OCELOT_VCAP_IS2_IPV6_EV_PTP_TRAP(ocelot);
	int err;

	err = ocelot_trap_del(ocelot, port, ipv6_ev_cookie);
	err |= ocelot_trap_del(ocelot, port, ipv6_gen_cookie);
	return err;
}

static int ocelot_setup_ptp_traps(struct ocelot *ocelot, int port,
				  bool l2, bool l4)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err;

	ocelot_port->trap_proto &= ~(OCELOT_PROTO_PTP_L2 |
				     OCELOT_PROTO_PTP_L4);

	if (l2)
		err = ocelot_l2_ptp_trap_add(ocelot, port);
	else
		err = ocelot_l2_ptp_trap_del(ocelot, port);
	if (err)
		return err;

	if (l4) {
		err = ocelot_ipv4_ptp_trap_add(ocelot, port);
		if (err)
			goto err_ipv4;

		err = ocelot_ipv6_ptp_trap_add(ocelot, port);
		if (err)
			goto err_ipv6;
	} else {
		err = ocelot_ipv4_ptp_trap_del(ocelot, port);

		err |= ocelot_ipv6_ptp_trap_del(ocelot, port);
	}
	if (err)
		return err;

	if (l2)
		ocelot_port->trap_proto |= OCELOT_PROTO_PTP_L2;
	if (l4)
		ocelot_port->trap_proto |= OCELOT_PROTO_PTP_L4;

	return 0;

err_ipv6:
	ocelot_ipv4_ptp_trap_del(ocelot, port);
err_ipv4:
	if (l2)
		ocelot_l2_ptp_trap_del(ocelot, port);
	return err;
}

static int ocelot_traps_to_ptp_rx_filter(unsigned int proto)
{
	if ((proto & OCELOT_PROTO_PTP_L2) && (proto & OCELOT_PROTO_PTP_L4))
		return HWTSTAMP_FILTER_PTP_V2_EVENT;
	else if (proto & OCELOT_PROTO_PTP_L2)
		return HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
	else if (proto & OCELOT_PROTO_PTP_L4)
		return HWTSTAMP_FILTER_PTP_V2_L4_EVENT;

	return HWTSTAMP_FILTER_NONE;
}

static int ocelot_ptp_tx_type_to_cmd(int tx_type, int *ptp_cmd)
{
	switch (tx_type) {
	case HWTSTAMP_TX_ON:
		*ptp_cmd = IFH_REW_OP_TWO_STEP_PTP;
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		/* IFH_REW_OP_ONE_STEP_PTP updates the correctionField,
		 * what we need to update is the originTimestamp.
		 */
		*ptp_cmd = IFH_REW_OP_ORIGIN_PTP;
		break;
	case HWTSTAMP_TX_OFF:
		*ptp_cmd = 0;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

int ocelot_hwstamp_get(struct ocelot *ocelot, int port, struct ifreq *ifr)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct hwtstamp_config cfg = {};

	switch (ocelot_port->ptp_cmd) {
	case IFH_REW_OP_TWO_STEP_PTP:
		cfg.tx_type = HWTSTAMP_TX_ON;
		break;
	case IFH_REW_OP_ORIGIN_PTP:
		cfg.tx_type = HWTSTAMP_TX_ONESTEP_SYNC;
		break;
	default:
		cfg.tx_type = HWTSTAMP_TX_OFF;
		break;
	}

	cfg.rx_filter = ocelot_traps_to_ptp_rx_filter(ocelot_port->trap_proto);

	return copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)) ? -EFAULT : 0;
}
EXPORT_SYMBOL(ocelot_hwstamp_get);

int ocelot_hwstamp_set(struct ocelot *ocelot, int port, struct ifreq *ifr)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int ptp_cmd, old_ptp_cmd = ocelot_port->ptp_cmd;
	bool l2 = false, l4 = false;
	struct hwtstamp_config cfg;
	bool old_l2, old_l4;
	int err;

	if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
		return -EFAULT;

	/* Tx type sanity check */
	err = ocelot_ptp_tx_type_to_cmd(cfg.tx_type, &ptp_cmd);
	if (err)
		return err;

	switch (cfg.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		l2 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		l2 = true;
		l4 = true;
		break;
	default:
		return -ERANGE;
	}

	old_l2 = ocelot_port->trap_proto & OCELOT_PROTO_PTP_L2;
	old_l4 = ocelot_port->trap_proto & OCELOT_PROTO_PTP_L4;

	err = ocelot_setup_ptp_traps(ocelot, port, l2, l4);
	if (err)
		return err;

	ocelot_port->ptp_cmd = ptp_cmd;

	cfg.rx_filter = ocelot_traps_to_ptp_rx_filter(ocelot_port->trap_proto);

	if (copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg))) {
		err = -EFAULT;
		goto out_restore_ptp_traps;
	}

	return 0;
out_restore_ptp_traps:
	ocelot_setup_ptp_traps(ocelot, port, old_l2, old_l4);
	ocelot_port->ptp_cmd = old_ptp_cmd;
	return err;
}
EXPORT_SYMBOL(ocelot_hwstamp_set);

int ocelot_get_ts_info(struct ocelot *ocelot, int port,
		       struct kernel_ethtool_ts_info *info)
{
	if (ocelot->ptp_clock) {
		info->phc_index = ptp_clock_index(ocelot->ptp_clock);
	} else {
		info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE;
		return 0;
	}
	info->so_timestamping |= SOF_TIMESTAMPING_TX_SOFTWARE |
				 SOF_TIMESTAMPING_TX_HARDWARE |
				 SOF_TIMESTAMPING_RX_HARDWARE |
				 SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON) |
			 BIT(HWTSTAMP_TX_ONESTEP_SYNC);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			   BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT);

	return 0;
}
EXPORT_SYMBOL(ocelot_get_ts_info);

static struct sk_buff *ocelot_port_dequeue_ptp_tx_skb(struct ocelot *ocelot,
						      int port, u8 ts_id,
						      u32 seqid)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct sk_buff *skb, *skb_tmp, *skb_match = NULL;
	struct ptp_header *hdr;

	spin_lock(&ocelot->ts_id_lock);

	skb_queue_walk_safe(&ocelot_port->tx_skbs, skb, skb_tmp) {
		if (OCELOT_SKB_CB(skb)->ts_id != ts_id)
			continue;

		/* Check that the timestamp ID is for the expected PTP
		 * sequenceId. We don't have to test ptp_parse_header() against
		 * NULL, because we've pre-validated the packet's ptp_class.
		 */
		hdr = ptp_parse_header(skb, OCELOT_SKB_CB(skb)->ptp_class);
		if (seqid != ntohs(hdr->sequence_id))
			continue;

		__skb_unlink(skb, &ocelot_port->tx_skbs);
		ocelot->ptp_skbs_in_flight--;
		skb_match = skb;
		break;
	}

	spin_unlock(&ocelot->ts_id_lock);

	return skb_match;
}

static int ocelot_port_queue_ptp_tx_skb(struct ocelot *ocelot, int port,
					struct sk_buff *clone)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	DECLARE_BITMAP(ts_id_in_flight, OCELOT_MAX_PTP_ID);
	struct sk_buff *skb, *skb_tmp;
	unsigned long n;

	spin_lock(&ocelot->ts_id_lock);

	/* To get a better chance of acquiring a timestamp ID, first flush the
	 * stale packets still waiting in the TX timestamping queue. They are
	 * probably lost.
	 */
	skb_queue_walk_safe(&ocelot_port->tx_skbs, skb, skb_tmp) {
		if (time_before(OCELOT_SKB_CB(skb)->ptp_tx_time +
				OCELOT_PTP_TX_TSTAMP_TIMEOUT, jiffies)) {
			u64_stats_update_begin(&ocelot_port->ts_stats->syncp);
			ocelot_port->ts_stats->lost++;
			u64_stats_update_end(&ocelot_port->ts_stats->syncp);

			dev_dbg_ratelimited(ocelot->dev,
					    "port %d invalidating stale timestamp ID %u which seems lost\n",
					    port, OCELOT_SKB_CB(skb)->ts_id);

			__skb_unlink(skb, &ocelot_port->tx_skbs);
			kfree_skb(skb);
			ocelot->ptp_skbs_in_flight--;
		} else {
			__set_bit(OCELOT_SKB_CB(skb)->ts_id, ts_id_in_flight);
		}
	}

	if (ocelot->ptp_skbs_in_flight == OCELOT_PTP_FIFO_SIZE) {
		spin_unlock(&ocelot->ts_id_lock);
		return -EBUSY;
	}

	n = find_first_zero_bit(ts_id_in_flight, OCELOT_MAX_PTP_ID);
	if (n == OCELOT_MAX_PTP_ID) {
		spin_unlock(&ocelot->ts_id_lock);
		return -EBUSY;
	}

	/* Found an available timestamp ID, use it */
	OCELOT_SKB_CB(clone)->ts_id = n;
	OCELOT_SKB_CB(clone)->ptp_tx_time = jiffies;
	ocelot->ptp_skbs_in_flight++;
	__skb_queue_tail(&ocelot_port->tx_skbs, clone);

	spin_unlock(&ocelot->ts_id_lock);

	dev_dbg_ratelimited(ocelot->dev, "port %d timestamp id %lu\n", port, n);

	return 0;
}

static bool ocelot_ptp_is_onestep_sync(struct sk_buff *skb,
				       unsigned int ptp_class)
{
	struct ptp_header *hdr;
	u8 msgtype, twostep;

	hdr = ptp_parse_header(skb, ptp_class);
	if (!hdr)
		return false;

	msgtype = ptp_get_msgtype(hdr, ptp_class);
	twostep = hdr->flag_field[0] & 0x2;

	if (msgtype == PTP_MSGTYPE_SYNC && twostep == 0)
		return true;

	return false;
}

int ocelot_port_txtstamp_request(struct ocelot *ocelot, int port,
				 struct sk_buff *skb,
				 struct sk_buff **clone)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u8 ptp_cmd = ocelot_port->ptp_cmd;
	unsigned int ptp_class;
	int err;

	/* Don't do anything if PTP timestamping not enabled */
	if (!ptp_cmd)
		return 0;

	ptp_class = ptp_classify_raw(skb);
	if (ptp_class == PTP_CLASS_NONE) {
		err = -EINVAL;
		goto error;
	}

	/* Store ptp_cmd in OCELOT_SKB_CB(skb)->ptp_cmd */
	if (ptp_cmd == IFH_REW_OP_ORIGIN_PTP) {
		if (ocelot_ptp_is_onestep_sync(skb, ptp_class)) {
			OCELOT_SKB_CB(skb)->ptp_cmd = ptp_cmd;

			u64_stats_update_begin(&ocelot_port->ts_stats->syncp);
			ocelot_port->ts_stats->onestep_pkts_unconfirmed++;
			u64_stats_update_end(&ocelot_port->ts_stats->syncp);

			return 0;
		}

		/* Fall back to two-step timestamping */
		ptp_cmd = IFH_REW_OP_TWO_STEP_PTP;
	}

	if (ptp_cmd == IFH_REW_OP_TWO_STEP_PTP) {
		*clone = skb_clone_sk(skb);
		if (!(*clone)) {
			err = -ENOMEM;
			goto error;
		}

		/* Store timestamp ID in OCELOT_SKB_CB(clone)->ts_id */
		err = ocelot_port_queue_ptp_tx_skb(ocelot, port, *clone);
		if (err) {
			kfree_skb(*clone);
			goto error;
		}

		skb_shinfo(*clone)->tx_flags |= SKBTX_IN_PROGRESS;
		OCELOT_SKB_CB(skb)->ptp_cmd = ptp_cmd;
		OCELOT_SKB_CB(*clone)->ptp_class = ptp_class;
	}

	return 0;

error:
	u64_stats_update_begin(&ocelot_port->ts_stats->syncp);
	ocelot_port->ts_stats->err++;
	u64_stats_update_end(&ocelot_port->ts_stats->syncp);
	return err;
}
EXPORT_SYMBOL(ocelot_port_txtstamp_request);

static void ocelot_get_hwtimestamp(struct ocelot *ocelot,
				   struct timespec64 *ts)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&ocelot->ptp_clock_lock, flags);

	/* Read current PTP time to get seconds */
	val = ocelot_read_rix(ocelot, PTP_PIN_CFG, TOD_ACC_PIN);

	val &= ~(PTP_PIN_CFG_SYNC | PTP_PIN_CFG_ACTION_MASK | PTP_PIN_CFG_DOM);
	val |= PTP_PIN_CFG_ACTION(PTP_PIN_ACTION_SAVE);
	ocelot_write_rix(ocelot, val, PTP_PIN_CFG, TOD_ACC_PIN);
	ts->tv_sec = ocelot_read_rix(ocelot, PTP_PIN_TOD_SEC_LSB, TOD_ACC_PIN);

	/* Read packet HW timestamp from FIFO */
	val = ocelot_read(ocelot, SYS_PTP_TXSTAMP);
	ts->tv_nsec = SYS_PTP_TXSTAMP_PTP_TXSTAMP(val);

	/* Sec has incremented since the ts was registered */
	if ((ts->tv_sec & 0x1) != !!(val & SYS_PTP_TXSTAMP_PTP_TXSTAMP_SEC))
		ts->tv_sec--;

	spin_unlock_irqrestore(&ocelot->ptp_clock_lock, flags);
}

void ocelot_get_txtstamp(struct ocelot *ocelot)
{
	int budget = OCELOT_PTP_QUEUE_SZ;

	while (budget--) {
		struct skb_shared_hwtstamps shhwtstamps;
		struct ocelot_port *ocelot_port;
		u32 val, id, seqid, txport;
		struct sk_buff *skb_match;
		struct timespec64 ts;

		val = ocelot_read(ocelot, SYS_PTP_STATUS);

		/* Check if a timestamp can be retrieved */
		if (!(val & SYS_PTP_STATUS_PTP_MESS_VLD))
			break;

		WARN_ON(val & SYS_PTP_STATUS_PTP_OVFL);

		/* Retrieve the ts ID and Tx port */
		id = SYS_PTP_STATUS_PTP_MESS_ID_X(val);
		txport = SYS_PTP_STATUS_PTP_MESS_TXPORT_X(val);
		seqid = SYS_PTP_STATUS_PTP_MESS_SEQ_ID(val);
		ocelot_port = ocelot->ports[txport];

		/* Retrieve its associated skb */
		skb_match = ocelot_port_dequeue_ptp_tx_skb(ocelot, txport, id,
							   seqid);
		if (!skb_match) {
			u64_stats_update_begin(&ocelot_port->ts_stats->syncp);
			ocelot_port->ts_stats->err++;
			u64_stats_update_end(&ocelot_port->ts_stats->syncp);

			dev_dbg_ratelimited(ocelot->dev,
					    "port %d received TX timestamp (seqid %d, ts id %u) for packet previously declared stale\n",
					    txport, seqid, id);

			goto next_ts;
		}

		u64_stats_update_begin(&ocelot_port->ts_stats->syncp);
		ocelot_port->ts_stats->pkts++;
		u64_stats_update_end(&ocelot_port->ts_stats->syncp);

		/* Get the h/w timestamp */
		ocelot_get_hwtimestamp(ocelot, &ts);

		/* Set the timestamp into the skb */
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_complete_tx_timestamp(skb_match, &shhwtstamps);

next_ts:
		ocelot_write(ocelot, SYS_PTP_NXT_PTP_NXT, SYS_PTP_NXT);
	}
}
EXPORT_SYMBOL(ocelot_get_txtstamp);

int ocelot_init_timestamp(struct ocelot *ocelot,
			  const struct ptp_clock_info *info)
{
	struct ptp_clock *ptp_clock;
	int i;

	ocelot->ptp_info = *info;

	for (i = 0; i < OCELOT_PTP_PINS_NUM; i++) {
		struct ptp_pin_desc *p = &ocelot->ptp_pins[i];

		snprintf(p->name, sizeof(p->name), "switch_1588_dat%d", i);
		p->index = i;
		p->func = PTP_PF_NONE;
	}

	ocelot->ptp_info.pin_config = &ocelot->ptp_pins[0];

	ptp_clock = ptp_clock_register(&ocelot->ptp_info, ocelot->dev);
	if (IS_ERR(ptp_clock))
		return PTR_ERR(ptp_clock);
	/* Check if PHC support is missing at the configuration level */
	if (!ptp_clock)
		return 0;

	ocelot->ptp_clock = ptp_clock;

	ocelot_write(ocelot, SYS_PTP_CFG_PTP_STAMP_WID(30), SYS_PTP_CFG);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_LOW);
	ocelot_write(ocelot, 0xffffffff, ANA_TABLES_PTP_ID_HIGH);

	ocelot_write(ocelot, PTP_CFG_MISC_PTP_EN, PTP_CFG_MISC);

	return 0;
}
EXPORT_SYMBOL(ocelot_init_timestamp);

int ocelot_deinit_timestamp(struct ocelot *ocelot)
{
	if (ocelot->ptp_clock)
		ptp_clock_unregister(ocelot->ptp_clock);
	return 0;
}
EXPORT_SYMBOL(ocelot_deinit_timestamp);
