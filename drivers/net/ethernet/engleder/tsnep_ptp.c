// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

void tsnep_get_system_time(struct tsnep_adapter *adapter, u64 *time)
{
	u32 high_before;
	u32 low;
	u32 high;

	/* read high dword twice to detect overrun */
	high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	do {
		low = ioread32(adapter->addr + ECM_SYSTEM_TIME_LOW);
		high_before = high;
		high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	} while (high != high_before);
	*time = (((u64)high) << 32) | ((u64)low);
}

int tsnep_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config config;

	if (!ifr)
		return -EINVAL;

	if (cmd == SIOCSHWTSTAMP) {
		if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
			return -EFAULT;

		switch (config.tx_type) {
		case HWTSTAMP_TX_OFF:
		case HWTSTAMP_TX_ON:
			break;
		default:
			return -ERANGE;
		}

		switch (config.rx_filter) {
		case HWTSTAMP_FILTER_NONE:
			break;
		case HWTSTAMP_FILTER_ALL:
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
		case HWTSTAMP_FILTER_NTP_ALL:
			config.rx_filter = HWTSTAMP_FILTER_ALL;
			break;
		default:
			return -ERANGE;
		}

		memcpy(&adapter->hwtstamp_config, &config,
		       sizeof(adapter->hwtstamp_config));
	}

	if (copy_to_user(ifr->ifr_data, &adapter->hwtstamp_config,
			 sizeof(adapter->hwtstamp_config)))
		return -EFAULT;

	return 0;
}

static int tsnep_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct tsnep_adapter *adapter = container_of(ptp, struct tsnep_adapter,
						     ptp_clock_info);
	bool negative = false;
	u64 rate_offset;

	if (scaled_ppm < 0) {
		scaled_ppm = -scaled_ppm;
		negative = true;
	}

	/* convert from 16 bit to 32 bit binary fractional, divide by 1000000 to
	 * eliminate ppm, multiply with 8 to compensate 8ns clock cycle time,
	 * simplify calculation because 15625 * 8 = 1000000 / 8
	 */
	rate_offset = scaled_ppm;
	rate_offset <<= 16 - 3;
	rate_offset = div_u64(rate_offset, 15625);

	rate_offset &= ECM_CLOCK_RATE_OFFSET_MASK;
	if (negative)
		rate_offset |= ECM_CLOCK_RATE_OFFSET_SIGN;
	iowrite32(rate_offset & 0xFFFFFFFF, adapter->addr + ECM_CLOCK_RATE);

	return 0;
}

static int tsnep_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct tsnep_adapter *adapter = container_of(ptp, struct tsnep_adapter,
						     ptp_clock_info);
	u64 system_time;
	unsigned long flags;

	spin_lock_irqsave(&adapter->ptp_lock, flags);

	tsnep_get_system_time(adapter, &system_time);

	system_time += delta;

	/* high dword is buffered in hardware and synchronously written to
	 * system time when low dword is written
	 */
	iowrite32(system_time >> 32, adapter->addr + ECM_SYSTEM_TIME_HIGH);
	iowrite32(system_time & 0xFFFFFFFF,
		  adapter->addr + ECM_SYSTEM_TIME_LOW);

	spin_unlock_irqrestore(&adapter->ptp_lock, flags);

	return 0;
}

static int tsnep_ptp_gettimex64(struct ptp_clock_info *ptp,
				struct timespec64 *ts,
				struct ptp_system_timestamp *sts)
{
	struct tsnep_adapter *adapter = container_of(ptp, struct tsnep_adapter,
						     ptp_clock_info);
	u32 high_before;
	u32 low;
	u32 high;
	u64 system_time;

	/* read high dword twice to detect overrun */
	high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	do {
		ptp_read_system_prets(sts);
		low = ioread32(adapter->addr + ECM_SYSTEM_TIME_LOW);
		ptp_read_system_postts(sts);
		high_before = high;
		high = ioread32(adapter->addr + ECM_SYSTEM_TIME_HIGH);
	} while (high != high_before);
	system_time = (((u64)high) << 32) | ((u64)low);

	*ts = ns_to_timespec64(system_time);

	return 0;
}

static int tsnep_ptp_settime64(struct ptp_clock_info *ptp,
			       const struct timespec64 *ts)
{
	struct tsnep_adapter *adapter = container_of(ptp, struct tsnep_adapter,
						     ptp_clock_info);
	u64 system_time = timespec64_to_ns(ts);
	unsigned long flags;

	spin_lock_irqsave(&adapter->ptp_lock, flags);

	/* high dword is buffered in hardware and synchronously written to
	 * system time when low dword is written
	 */
	iowrite32(system_time >> 32, adapter->addr + ECM_SYSTEM_TIME_HIGH);
	iowrite32(system_time & 0xFFFFFFFF,
		  adapter->addr + ECM_SYSTEM_TIME_LOW);

	spin_unlock_irqrestore(&adapter->ptp_lock, flags);

	return 0;
}

static int tsnep_ptp_getcyclesx64(struct ptp_clock_info *ptp,
				  struct timespec64 *ts,
				  struct ptp_system_timestamp *sts)
{
	struct tsnep_adapter *adapter = container_of(ptp, struct tsnep_adapter,
						     ptp_clock_info);
	u32 high_before;
	u32 low;
	u32 high;
	u64 counter;

	/* read high dword twice to detect overrun */
	high = ioread32(adapter->addr + ECM_COUNTER_HIGH);
	do {
		ptp_read_system_prets(sts);
		low = ioread32(adapter->addr + ECM_COUNTER_LOW);
		ptp_read_system_postts(sts);
		high_before = high;
		high = ioread32(adapter->addr + ECM_COUNTER_HIGH);
	} while (high != high_before);
	counter = (((u64)high) << 32) | ((u64)low);

	*ts = ns_to_timespec64(counter);

	return 0;
}

int tsnep_ptp_init(struct tsnep_adapter *adapter)
{
	int retval = 0;

	adapter->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	adapter->hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;

	snprintf(adapter->ptp_clock_info.name, 16, "%s", TSNEP);
	adapter->ptp_clock_info.owner = THIS_MODULE;
	/* at most 2^-1ns adjustment every clock cycle for 8ns clock cycle time,
	 * stay slightly below because only bits below 2^-1ns are supported
	 */
	adapter->ptp_clock_info.max_adj = (500000000 / 8 - 1);
	adapter->ptp_clock_info.adjfine = tsnep_ptp_adjfine;
	adapter->ptp_clock_info.adjtime = tsnep_ptp_adjtime;
	adapter->ptp_clock_info.gettimex64 = tsnep_ptp_gettimex64;
	adapter->ptp_clock_info.settime64 = tsnep_ptp_settime64;
	adapter->ptp_clock_info.getcyclesx64 = tsnep_ptp_getcyclesx64;

	spin_lock_init(&adapter->ptp_lock);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_clock_info,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		netdev_err(adapter->netdev, "ptp_clock_register failed\n");

		retval = PTR_ERR(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
	} else if (adapter->ptp_clock) {
		netdev_info(adapter->netdev, "PHC added\n");
	}

	return retval;
}

void tsnep_ptp_cleanup(struct tsnep_adapter *adapter)
{
	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		netdev_info(adapter->netdev, "PHC removed\n");
	}
}
