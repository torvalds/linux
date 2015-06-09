/*
 * Original code based on Host AP (software wireless LAN access point) driver
 * for Intersil Prism2/2.5/3.
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Adaption to a generic IEEE 802.11 stack by James Ketrenos
 * <jketreno@linux.intel.com>
 *
 * Copyright (c) 2004, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

/* This file defines the interface to the rtllib crypto module.
 */
#ifndef RTLLIB_CRYPT_H
#define RTLLIB_CRYPT_H

#include <linux/skbuff.h>

int rtllib_register_crypto_ops(struct lib80211_crypto_ops *ops);
int rtllib_unregister_crypto_ops(struct lib80211_crypto_ops *ops);
struct lib80211_crypto_ops *rtllib_get_crypto_ops(const char *name);
void rtllib_crypt_deinit_entries(struct lib80211_crypt_info *info, int force);
void rtllib_crypt_deinit_handler(unsigned long data);
void rtllib_crypt_delayed_deinit(struct lib80211_crypt_info *info,
				 struct lib80211_crypt_data **crypt);
#endif
