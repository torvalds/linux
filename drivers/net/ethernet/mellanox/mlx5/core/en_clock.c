/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/clocksource.h>
#include "en.h"

enum {
	MLX5E_CYCLES_SHIFT	= 23
};

void mlx5e_fill_hwstamp(struct mlx5e_tstamp *tstamp, u64 timestamp,
			struct skb_shared_hwtstamps *hwts)
{
	u64 nsec;

	read_lock(&tstamp->lock);
	nsec = timecounter_cyc2time(&tstamp->clock, timestamp);
	read_unlock(&tstamp->lock);

	hwts->hwtstamp = ns_to_ktime(nsec);
}

static cycle_t mlx5e_read_internal_timer(const struct cyclecounter *cc)
{
	struct mlx5e_tstamp *tstamp = container_of(cc, struct mlx5e_tstamp,
						   cycles);

	return mlx5_read_internal_timer(tstamp->mdev) & cc->mask;
}

static void mlx5e_timestamp_overflow(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mlx5e_tstamp *tstamp = container_of(dwork, struct mlx5e_tstamp,
						   overflow_work);
	unsigned long flags;

	write_lock_irqsave(&tstamp->lock, flags);
	timecounter_read(&tstamp->clock);
	write_unlock_irqrestore(&tstamp->lock, flags);
	schedule_delayed_work(&tstamp->overflow_work, tstamp->overflow_period);
}

int mlx5e_hwstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct hwtstamp_config config;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* TX HW timestamp */
	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	/* RX HW timestamp */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		/* Reset CQE compression to Admin default */
		mlx5e_modify_rx_cqe_compression(priv, priv->params.rx_cqe_compress_admin);
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		/* Disable CQE compression */
		mlx5e_modify_rx_cqe_compression(priv, false);
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	memcpy(&priv->tstamp.hwtstamp_config, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

int mlx5e_hwstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct hwtstamp_config *cfg = &priv->tstamp.hwtstamp_config;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, cfg, sizeof(*cfg)) ? -EFAULT : 0;
}

static int mlx5e_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct mlx5e_tstamp *tstamp = container_of(ptp, struct mlx5e_tstamp,
						   ptp_info);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	write_lock_irqsave(&tstamp->lock, flags);
	timecounter_init(&tstamp->clock, &tstamp->cycles, ns);
	write_unlock_irqrestore(&tstamp->lock, flags);

	return 0;
}

static int mlx5e_ptp_gettime(struct ptp_clock_info *ptp,
			     struct timespec64 *ts)
{
	struct mlx5e_tstamp *tstamp = container_of(ptp, struct mlx5e_tstamp,
						   ptp_info);
	u64 ns;
	unsigned long flags;

	write_lock_irqsave(&tstamp->lock, flags);
	ns = timecounter_read(&tstamp->clock);
	write_unlock_irqrestore(&tstamp->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int mlx5e_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct mlx5e_tstamp *tstamp = container_of(ptp, struct mlx5e_tstamp,
						   ptp_info);
	unsigned long flags;

	write_lock_irqsave(&tstamp->lock, flags);
	timecounter_adjtime(&tstamp->clock, delta);
	write_unlock_irqrestore(&tstamp->lock, flags);

	return 0;
}

static int mlx5e_ptp_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	u64 adj;
	u32 diff;
	unsigned long flags;
	int neg_adj = 0;
	struct mlx5e_tstamp *tstamp = container_of(ptp, struct mlx5e_tstamp,
						  ptp_info);

	if (delta < 0) {
		neg_adj = 1;
		delta = -delta;
	}

	adj = tstamp->nominal_c_mult;
	adj *= delta;
	diff = div_u64(adj, 1000000000ULL);

	write_lock_irqsave(&tstamp->lock, flags);
	timecounter_read(&tstamp->clock);
	tstamp->cycles.mult = neg_adj ? tstamp->nominal_c_mult - diff :
					tstamp->nominal_c_mult + diff;
	write_unlock_irqrestore(&tstamp->lock, flags);

	return 0;
}

static const struct ptp_clock_info mlx5e_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.max_adj	= 100000000,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= mlx5e_ptp_adjfreq,
	.adjtime	= mlx5e_ptp_adjtime,
	.gettime64	= mlx5e_ptp_gettime,
	.settime64	= mlx5e_ptp_settime,
	.enable		= NULL,
};

static void mlx5e_timestamp_init_config(struct mlx5e_tstamp *tstamp)
{
	tstamp->hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;
	tstamp->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
}

void mlx5e_timestamp_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tstamp *tstamp = &priv->tstamp;
	u64 ns;
	u64 frac = 0;
	u32 dev_freq;

	mlx5e_timestamp_init_config(tstamp);
	dev_freq = MLX5_CAP_GEN(priv->mdev, device_frequency_khz);
	if (!dev_freq) {
		mlx5_core_warn(priv->mdev, "invalid device_frequency_khz, aborting HW clock init\n");
		return;
	}
	rwlock_init(&tstamp->lock);
	tstamp->cycles.read = mlx5e_read_internal_timer;
	tstamp->cycles.shift = MLX5E_CYCLES_SHIFT;
	tstamp->cycles.mult = clocksource_khz2mult(dev_freq,
						   tstamp->cycles.shift);
	tstamp->nominal_c_mult = tstamp->cycles.mult;
	tstamp->cycles.mask = CLOCKSOURCE_MASK(41);
	tstamp->mdev = priv->mdev;

	timecounter_init(&tstamp->clock, &tstamp->cycles,
			 ktime_to_ns(ktime_get_real()));

	/* Calculate period in seconds to call the overflow watchdog - to make
	 * sure counter is checked at least once every wrap around.
	 */
	ns = cyclecounter_cyc2ns(&tstamp->cycles, tstamp->cycles.mask,
				 frac, &frac);
	do_div(ns, NSEC_PER_SEC / 2 / HZ);
	tstamp->overflow_period = ns;

	INIT_DELAYED_WORK(&tstamp->overflow_work, mlx5e_timestamp_overflow);
	if (tstamp->overflow_period)
		schedule_delayed_work(&tstamp->overflow_work, 0);
	else
		mlx5_core_warn(priv->mdev, "invalid overflow period, overflow_work is not scheduled\n");

	/* Configure the PHC */
	tstamp->ptp_info = mlx5e_ptp_clock_info;
	snprintf(tstamp->ptp_info.name, 16, "mlx5 ptp");

	tstamp->ptp = ptp_clock_register(&tstamp->ptp_info,
					 &priv->mdev->pdev->dev);
	if (IS_ERR_OR_NULL(tstamp->ptp)) {
		mlx5_core_warn(priv->mdev, "ptp_clock_register failed %ld\n",
			       PTR_ERR(tstamp->ptp));
		tstamp->ptp = NULL;
	}
}

void mlx5e_timestamp_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tstamp *tstamp = &priv->tstamp;

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz))
		return;

	if (priv->tstamp.ptp) {
		ptp_clock_unregister(priv->tstamp.ptp);
		priv->tstamp.ptp = NULL;
	}

	cancel_delayed_work_sync(&tstamp->overflow_work);
}
