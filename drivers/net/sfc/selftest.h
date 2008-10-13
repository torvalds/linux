/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
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
	int tx_sent[EFX_TX_QUEUE_COUNT];
	int tx_done[EFX_TX_QUEUE_COUNT];
	int rx_good;
	int rx_bad;
};

/* Efx self test results
 * For fields which are not counters, 1 indicates success and -1
 * indicates failure.
 */
struct efx_self_tests {
	/* online tests */
	int mii;
	int nvram;
	int interrupt;
	int eventq_dma[EFX_MAX_CHANNELS];
	int eventq_int[EFX_MAX_CHANNELS];
	int eventq_poll[EFX_MAX_CHANNELS];
	/* offline tests */
	int registers;
	int phy;
	int loopback_speed;
	int loopback_full_duplex;
	struct efx_loopback_self_tests loopback[LOOPBACK_TEST_MAX + 1];
};

extern void efx_loopback_rx_packet(struct efx_nic *efx,
				   const char *buf_ptr, int pkt_len);
extern int efx_online_test(struct efx_nic *efx,
			   struct efx_self_tests *tests);
extern int efx_offline_test(struct efx_nic *efx,
			    struct efx_self_tests *tests,
			    unsigned int loopback_modes);

#endif /* EFX_SELFTEST_H */
