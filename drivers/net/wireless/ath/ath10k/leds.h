/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018 Sebastian Gottschall <s.gottschall@dd-wrt.com>
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#ifndef _LEDS_H_
#define _LEDS_H_

#include "core.h"

#ifdef CONFIG_ATH10K_LEDS
void ath10k_leds_unregister(struct ath10k *ar);
int ath10k_leds_start(struct ath10k *ar);
int ath10k_leds_register(struct ath10k *ar);
#else
static inline void ath10k_leds_unregister(struct ath10k *ar)
{
}

static inline int ath10k_leds_start(struct ath10k *ar)
{
	return 0;
}

static inline int ath10k_leds_register(struct ath10k *ar)
{
	return 0;
}

#endif
#endif /* _LEDS_H_ */
