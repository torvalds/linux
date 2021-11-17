/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2012 Solarflare Communications Inc.
 */

#ifndef EF4_SELFTEST_H
#define EF4_SELFTEST_H

#include "net_driver.h"

/*
 * Self tests
 */

struct ef4_loopback_self_tests {
	int tx_sent[EF4_TXQ_TYPES];
	int tx_done[EF4_TXQ_TYPES];
	int rx_good;
	int rx_bad;
};

#define EF4_MAX_PHY_TESTS 20

/* Efx self test results
 * For fields which are not counters, 1 indicates success and -1
 * indicates failure; 0 indicates test could not be run.
 */
struct ef4_self_tests {
	/* online tests */
	int phy_alive;
	int nvram;
	int interrupt;
	int eventq_dma[EF4_MAX_CHANNELS];
	int eventq_int[EF4_MAX_CHANNELS];
	/* offline tests */
	int memory;
	int registers;
	int phy_ext[EF4_MAX_PHY_TESTS];
	struct ef4_loopback_self_tests loopback[LOOPBACK_TEST_MAX + 1];
};

void ef4_loopback_rx_packet(struct ef4_nic *efx, const char *buf_ptr,
			    int pkt_len);
int ef4_selftest(struct ef4_nic *efx, struct ef4_self_tests *tests,
		 unsigned flags);
void ef4_selftest_async_start(struct ef4_nic *efx);
void ef4_selftest_async_cancel(struct ef4_nic *efx);
void ef4_selftest_async_work(struct work_struct *data);

#endif /* EF4_SELFTEST_H */
