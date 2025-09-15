// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 *
 * Author: Raju Rangoju <Raju.Rangoju@amd.com>
 */

#include "xgbe.h"
#include "xgbe-common.h"

void xgbe_update_tstamp_time(struct xgbe_prv_data *pdata,
			     unsigned int sec, unsigned int nsec)
{
	int count;

	/* Set the time values and tell the device */
	XGMAC_IOWRITE(pdata, MAC_STSUR, sec);
	XGMAC_IOWRITE(pdata, MAC_STNUR, nsec);

	/* issue command to update the system time value */
	XGMAC_IOWRITE(pdata, MAC_TSCR,
		      XGMAC_IOREAD(pdata, MAC_TSCR) |
		      (1 << MAC_TSCR_TSUPDT_INDEX));

	/* Wait for the time adjust/update to complete */
	count = 10000;
	while (--count && XGMAC_IOREAD_BITS(pdata, MAC_TSCR, TSUPDT))
		udelay(5);

	if (count < 0)
		netdev_err(pdata->netdev,
			   "timed out updating system timestamp\n");
}

void xgbe_update_tstamp_addend(struct xgbe_prv_data *pdata,
			       unsigned int addend)
{
	unsigned int count = 10000;

	/* Set the addend register value and tell the device */
	XGMAC_IOWRITE(pdata, MAC_TSAR, addend);
	XGMAC_IOWRITE_BITS(pdata, MAC_TSCR, TSADDREG, 1);

	/* Wait for addend update to complete */
	while (--count && XGMAC_IOREAD_BITS(pdata, MAC_TSCR, TSADDREG))
		udelay(5);

	if (!count)
		netdev_err(pdata->netdev,
			   "timed out updating timestamp addend register\n");
}

void xgbe_set_tstamp_time(struct xgbe_prv_data *pdata, unsigned int sec,
			  unsigned int nsec)
{
	unsigned int count = 10000;

	/* Set the time values and tell the device */
	XGMAC_IOWRITE(pdata, MAC_STSUR, sec);
	XGMAC_IOWRITE(pdata, MAC_STNUR, nsec);
	XGMAC_IOWRITE_BITS(pdata, MAC_TSCR, TSINIT, 1);

	/* Wait for time update to complete */
	while (--count && XGMAC_IOREAD_BITS(pdata, MAC_TSCR, TSINIT))
		udelay(5);

	if (!count)
		netdev_err(pdata->netdev, "timed out initializing timestamp\n");
}

u64 xgbe_get_tstamp_time(struct xgbe_prv_data *pdata)
{
	u64 nsec;

	nsec = XGMAC_IOREAD(pdata, MAC_STSR);
	nsec *= NSEC_PER_SEC;
	nsec += XGMAC_IOREAD(pdata, MAC_STNR);

	return nsec;
}

u64 xgbe_get_tx_tstamp(struct xgbe_prv_data *pdata)
{
	unsigned int tx_snr, tx_ssr;
	u64 nsec;

	if (pdata->vdata->tx_tstamp_workaround) {
		tx_snr = XGMAC_IOREAD(pdata, MAC_TXSNR);
		tx_ssr = XGMAC_IOREAD(pdata, MAC_TXSSR);
	} else {
		tx_ssr = XGMAC_IOREAD(pdata, MAC_TXSSR);
		tx_snr = XGMAC_IOREAD(pdata, MAC_TXSNR);
	}

	if (XGMAC_GET_BITS(tx_snr, MAC_TXSNR, TXTSSTSMIS))
		return 0;

	nsec = tx_ssr;
	nsec *= NSEC_PER_SEC;
	nsec += tx_snr;

	return nsec;
}

void xgbe_get_rx_tstamp(struct xgbe_packet_data *packet,
			struct xgbe_ring_desc *rdesc)
{
	u64 nsec;

	if (XGMAC_GET_BITS_LE(rdesc->desc3, RX_CONTEXT_DESC3, TSA) &&
	    !XGMAC_GET_BITS_LE(rdesc->desc3, RX_CONTEXT_DESC3, TSD)) {
		nsec = le32_to_cpu(rdesc->desc1);
		nsec *= NSEC_PER_SEC;
		nsec += le32_to_cpu(rdesc->desc0);
		if (nsec != 0xffffffffffffffffULL) {
			packet->rx_tstamp = nsec;
			XGMAC_SET_BITS(packet->attributes, RX_PACKET_ATTRIBUTES,
				       RX_TSTAMP, 1);
		}
	}
}

void xgbe_config_tstamp(struct xgbe_prv_data *pdata, unsigned int mac_tscr)
{
	unsigned int value = 0;

	value = XGMAC_IOREAD(pdata, MAC_TSCR);
	value |= mac_tscr;
	XGMAC_IOWRITE(pdata, MAC_TSCR, value);
}

void xgbe_tx_tstamp(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   tx_tstamp_work);
	struct skb_shared_hwtstamps hwtstamps;
	unsigned long flags;

	spin_lock_irqsave(&pdata->tstamp_lock, flags);
	if (!pdata->tx_tstamp_skb)
		goto unlock;

	if (pdata->tx_tstamp) {
		memset(&hwtstamps, 0, sizeof(hwtstamps));
		hwtstamps.hwtstamp = ns_to_ktime(pdata->tx_tstamp);
		skb_tstamp_tx(pdata->tx_tstamp_skb, &hwtstamps);
	}

	dev_kfree_skb_any(pdata->tx_tstamp_skb);

	pdata->tx_tstamp_skb = NULL;

unlock:
	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);
}

int xgbe_get_hwtstamp_settings(struct xgbe_prv_data *pdata, struct ifreq *ifreq)
{
	if (copy_to_user(ifreq->ifr_data, &pdata->tstamp_config,
			 sizeof(pdata->tstamp_config)))
		return -EFAULT;

	return 0;
}

int xgbe_set_hwtstamp_settings(struct xgbe_prv_data *pdata, struct ifreq *ifreq)
{
	struct hwtstamp_config config;
	unsigned int mac_tscr;

	if (copy_from_user(&config, ifreq->ifr_data, sizeof(config)))
		return -EFAULT;

	mac_tscr = 0;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		break;

	case HWTSTAMP_TX_ON:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;

	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENALL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* PTP v2, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;    /* to PTP v1, UDP, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;
		/* PTP v2, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;    /* to PTP v1, UDP, Sync packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* PTP v2, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		fallthrough;    /* to PTP v1, UDP, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* 802.AS1, Ethernet, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* 802.AS1, Ethernet, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* 802.AS1, Ethernet, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, AV8021ASMEN, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* PTP v2/802.AS1, any layer, any kind of event packet */
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, SNAPTYPSEL, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* PTP v2/802.AS1, any layer, Sync packet */
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

		/* PTP v2/802.AS1, any layer, Delay_req packet */
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSVER2ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV4ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSIPV6ENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSMSTRENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSEVNTENA, 1);
		XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);
		break;

	default:
		return -ERANGE;
	}

	xgbe_config_tstamp(pdata, mac_tscr);

	memcpy(&pdata->tstamp_config, &config, sizeof(config));

	return 0;
}

void xgbe_prep_tx_tstamp(struct xgbe_prv_data *pdata,
			 struct sk_buff *skb,
			 struct xgbe_packet_data *packet)
{
	unsigned long flags;

	if (XGMAC_GET_BITS(packet->attributes, TX_PACKET_ATTRIBUTES, PTP)) {
		spin_lock_irqsave(&pdata->tstamp_lock, flags);
		if (pdata->tx_tstamp_skb) {
			/* Another timestamp in progress, ignore this one */
			XGMAC_SET_BITS(packet->attributes,
				       TX_PACKET_ATTRIBUTES, PTP, 0);
		} else {
			pdata->tx_tstamp_skb = skb_get(skb);
			skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		}
		spin_unlock_irqrestore(&pdata->tstamp_lock, flags);
	}

	skb_tx_timestamp(skb);
}

int xgbe_init_ptp(struct xgbe_prv_data *pdata)
{
	unsigned int mac_tscr = 0;
	struct timespec64 now;
	u64 dividend;

	/* Register Settings to be done based on the link speed. */
	switch (pdata->phy.speed) {
	case SPEED_1000:
		XGMAC_IOWRITE(pdata, MAC_TICNR, MAC_TICNR_1G_INITVAL);
		XGMAC_IOWRITE(pdata, MAC_TECNR, MAC_TECNR_1G_INITVAL);
		break;
	case SPEED_2500:
	case SPEED_10000:
		XGMAC_IOWRITE_BITS(pdata, MAC_TICSNR, TSICSNS,
				   MAC_TICSNR_10G_INITVAL);
		XGMAC_IOWRITE(pdata, MAC_TECNR, MAC_TECNR_10G_INITVAL);
		XGMAC_IOWRITE_BITS(pdata, MAC_TECSNR, TSECSNS,
				   MAC_TECSNR_10G_INITVAL);
		break;
	case SPEED_UNKNOWN:
	default:
		break;
	}

	/* Enable IEEE1588 PTP clock. */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSENA, 1);

	/* Overwrite earlier timestamps */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TXTSSTSM, 1);

	/* Set one nano-second accuracy */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSCTRLSSR, 1);

	/* Set fine timestamp update */
	XGMAC_SET_BITS(mac_tscr, MAC_TSCR, TSCFUPDT, 1);

	xgbe_config_tstamp(pdata, mac_tscr);

	/* Exit if timestamping is not enabled */
	if (!XGMAC_GET_BITS(mac_tscr, MAC_TSCR, TSENA))
		return -EOPNOTSUPP;

	if (pdata->vdata->tstamp_ptp_clock_freq) {
		/* Initialize time registers based on
		 * 125MHz PTP Clock Frequency
		 */
		XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SSINC,
				   XGBE_V2_TSTAMP_SSINC);
		XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SNSINC,
				   XGBE_V2_TSTAMP_SNSINC);
	} else {
		/* Initialize time registers based on
		 * 50MHz PTP Clock Frequency
		 */
		XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SSINC, XGBE_TSTAMP_SSINC);
		XGMAC_IOWRITE_BITS(pdata, MAC_SSIR, SNSINC, XGBE_TSTAMP_SNSINC);
	}

	/* Calculate the addend:
	 *   addend = 2^32 / (PTP ref clock / (PTP clock based on SSINC))
	 *          = (2^32 * (PTP clock based on SSINC)) / PTP ref clock
	 */
	if (pdata->vdata->tstamp_ptp_clock_freq)
		dividend = XGBE_V2_PTP_ACT_CLK_FREQ;
	else
		dividend = XGBE_PTP_ACT_CLK_FREQ;

	dividend = (u64)(dividend << 32);
	pdata->tstamp_addend = div_u64(dividend, pdata->ptpclk_rate);

	xgbe_update_tstamp_addend(pdata, pdata->tstamp_addend);

	dma_wmb();
	/* initialize system time */
	ktime_get_real_ts64(&now);

	/* lower 32 bits of tv_sec are safe until y2106 */
	xgbe_set_tstamp_time(pdata, (u32)now.tv_sec, now.tv_nsec);

	return 0;
}
