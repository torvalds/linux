/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013 Broadcom Corporation */

#ifndef BCM_KONA_SMC_H
#define BCM_KONA_SMC_H

#include <linux/types.h>

/* Broadcom Secure Service API service IDs, return codes, and exit codes */
#define SSAPI_ENABLE_L2_CACHE		0x01000002
#define SEC_ROM_RET_OK			0x00000001
#define SEC_EXIT_NORMAL			0x1

extern int __init bcm_kona_smc_init(void);

extern unsigned bcm_kona_smc(unsigned service_id,
			     unsigned arg0,
			     unsigned arg1,
			     unsigned arg2,
			     unsigned arg3);

#endif /* BCM_KONA_SMC_H */
