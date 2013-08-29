/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2012 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_SELFTEST_H
#define EFX_SELFTEST_H

#include "net_driver.h"

/*
 * Self tests
 */

struct efx_loopback_self_tests {
	int tx_sent[EFX_TXQ_TYPES];
	int tx_done[EFX_TXQ_TYPES];
	int rx_good;
	int rx_bad;
};

#define EFX_MAX_PHY_TESTS 20

/* Efx self test results
 * For fields which are not counters, 1 indicates success and -1
 * indicates failure.
 */
struct efx_self_tests {
	/* online tests */
	int phy_alive;
	int nvram;
	int interrupt;
	int eventq_dma[EFX_MAX_CHANNELS];
	int eventq_int[EFX_MAX_CHANNELS];
	/* offline tests */
	int registers;
	int phy_ext[EFX_MAX_PHY_TESTS];
	struct efx_loopback_self_tests loopback[LOOPBACK_TEST_MAX + 1];
};

extern void efx_loopback_rx_packet(struct efx_nic *efx,
				   const char *buf_ptr, int pkt_len);
extern int efx_selftest(struct efx_nic *efx,
			struct efx_self_tests *tests,
			unsigned flags);
extern void efx_selftest_async_start(struct efx_nic *efx);
extern void efx_selftest_async_cancel(struct efx_nic *efx);
extern void efx_selftest_async_work(struct work_struct *data);

#endif /* EFX_SELFTEST_H */
