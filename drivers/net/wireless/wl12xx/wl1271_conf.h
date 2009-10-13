/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_CONF_H__
#define __WL1271_CONF_H__

enum {
	CONF_HW_BIT_RATE_1MBPS   = BIT(0),
	CONF_HW_BIT_RATE_2MBPS   = BIT(1),
	CONF_HW_BIT_RATE_5_5MBPS = BIT(2),
	CONF_HW_BIT_RATE_6MBPS   = BIT(3),
	CONF_HW_BIT_RATE_9MBPS   = BIT(4),
	CONF_HW_BIT_RATE_11MBPS  = BIT(5),
	CONF_HW_BIT_RATE_12MBPS  = BIT(6),
	CONF_HW_BIT_RATE_18MBPS  = BIT(7),
	CONF_HW_BIT_RATE_22MBPS  = BIT(8),
	CONF_HW_BIT_RATE_24MBPS  = BIT(9),
	CONF_HW_BIT_RATE_36MBPS  = BIT(10),
	CONF_HW_BIT_RATE_48MBPS  = BIT(11),
	CONF_HW_BIT_RATE_54MBPS  = BIT(12),
	CONF_HW_BIT_RATE_MCS_0   = BIT(13),
	CONF_HW_BIT_RATE_MCS_1   = BIT(14),
	CONF_HW_BIT_RATE_MCS_2   = BIT(15),
	CONF_HW_BIT_RATE_MCS_3   = BIT(16),
	CONF_HW_BIT_RATE_MCS_4   = BIT(17),
	CONF_HW_BIT_RATE_MCS_5   = BIT(18),
	CONF_HW_BIT_RATE_MCS_6   = BIT(19),
	CONF_HW_BIT_RATE_MCS_7   = BIT(20)
};

enum {
	CONF_HW_RATE_INDEX_1MBPS   = 0,
	CONF_HW_RATE_INDEX_2MBPS   = 1,
	CONF_HW_RATE_INDEX_5_5MBPS = 2,
	CONF_HW_RATE_INDEX_6MBPS   = 3,
	CONF_HW_RATE_INDEX_9MBPS   = 4,
	CONF_HW_RATE_INDEX_11MBPS  = 5,
	CONF_HW_RATE_INDEX_12MBPS  = 6,
	CONF_HW_RATE_INDEX_18MBPS  = 7,
	CONF_HW_RATE_INDEX_22MBPS  = 8,
	CONF_HW_RATE_INDEX_24MBPS  = 9,
	CONF_HW_RATE_INDEX_36MBPS  = 10,
	CONF_HW_RATE_INDEX_48MBPS  = 11,
	CONF_HW_RATE_INDEX_54MBPS  = 12,
	CONF_HW_RATE_INDEX_MAX     = CONF_HW_RATE_INDEX_54MBPS,
};

struct conf_sg_settings {
	/*
	 * Defines the PER threshold in PPM of the BT voice of which reaching
	 * this value will trigger raising the priority of the BT voice by
	 * the BT IP until next NFS sample interval time as defined in
	 * nfs_sample_interval.
	 *
	 * Unit: PER value in PPM (parts per million)
	 * #Error_packets / #Total_packets

	 * Range: u32
	 */
	u32 per_threshold;

	/*
	 * This value is an absolute time in micro-seconds to limit the
	 * maximum scan duration compensation while in SG
	 */
	u32 max_scan_compensation_time;

	/* Defines the PER threshold of the BT voice of which reaching this
	 * value will trigger raising the priority of the BT voice until next
	 * NFS sample interval time as defined in sample_interval.
	 *
	 * Unit: msec
	 * Range: 1-65000
	 */
	u16 nfs_sample_interval;

	/*
	 * Defines the load ratio for the BT.
	 * The WLAN ratio is: 100 - load_ratio
	 *
	 * Unit: Percent
	 * Range: 0-100
	 */
	u8 load_ratio;

	/*
	 * true - Co-ex is allowed to enter/exit P.S automatically and
	 *        transparently to the host
	 *
	 * false - Co-ex is disallowed to enter/exit P.S and will trigger an
	 *         event to the host to notify for the need to enter/exit P.S
	 *         due to BT change state
	 *
	 */
	u8 auto_ps_mode;

	/*
	 * This parameter defines the compensation percentage of num of probe
	 * requests in case scan is initiated during BT voice/BT ACL
	 * guaranteed link.
	 *
	 * Unit: Percent
	 * Range: 0-255 (0 - No compensation)
	 */
	u8 probe_req_compensation;

	/*
	 * This parameter defines the compensation percentage of scan window
	 * size in case scan is initiated during BT voice/BT ACL Guaranteed
	 * link.
	 *
	 * Unit: Percent
	 * Range: 0-255 (0 - No compensation)
	 */
	u8 scan_window_compensation;

	/*
	 * Defines the antenna configuration.
	 *
	 * Range: 0 - Single Antenna; 1 - Dual Antenna
	 */
	u8 antenna_config;

	/*
	 * The percent out of the Max consecutive beacon miss roaming trigger
	 * which is the threshold for raising the priority of beacon
	 * reception.
	 *
	 * Range: 1-100
	 * N = MaxConsecutiveBeaconMiss
	 * P = coexMaxConsecutiveBeaconMissPrecent
	 * Threshold = MIN( N-1, round(N * P / 100))
	 */
	u8 beacon_miss_threshold;

	/*
	 * The RX rate threshold below which rate adaptation is assumed to be
	 * occurring at the AP which will raise priority for ACTIVE_RX and RX
	 * SP.
	 *
	 * Range: HW_BIT_RATE_*
	 */
	u32 rate_adaptation_threshold;

	/*
	 * The SNR above which the RX rate threshold indicating AP rate
	 * adaptation is valid
	 *
	 * Range: -128 - 127
	 */
	s8 rate_adaptation_snr;
};

struct conf_drv_settings {
	struct conf_sg_settings sg;
};

#endif
