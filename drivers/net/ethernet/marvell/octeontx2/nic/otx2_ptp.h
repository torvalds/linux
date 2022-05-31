/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef OTX2_PTP_H
#define OTX2_PTP_H

static inline u64 otx2_ptp_convert_rx_timestamp(u64 timestamp)
{
	return be64_to_cpu(*(__be64 *)&timestamp);
}

static inline u64 otx2_ptp_convert_tx_timestamp(u64 timestamp)
{
	return timestamp;
}

static inline u64 cn10k_ptp_convert_timestamp(u64 timestamp)
{
	return ((timestamp >> 32) * NSEC_PER_SEC) + (timestamp & 0xFFFFFFFFUL);
}

int otx2_ptp_init(struct otx2_nic *pfvf);
void otx2_ptp_destroy(struct otx2_nic *pfvf);

int otx2_ptp_clock_index(struct otx2_nic *pfvf);
int otx2_ptp_tstamp2time(struct otx2_nic *pfvf, u64 tstamp, u64 *tsns);

#endif
