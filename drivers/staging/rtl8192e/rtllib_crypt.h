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

/*
 * This file defines the interface to the rtllib crypto module.
 */
#ifndef RTLLIB_CRYPT_H
#define RTLLIB_CRYPT_H

#include <linux/skbuff.h>

struct rtllib_crypto_ops {
	const char *name;

	/* init new crypto context (e.g., allocate private data space,
	 * select IV, etc.); returns NULL on failure or pointer to allocated
	 * private data on success */
	void * (*init)(int keyidx);

	/* deinitialize crypto context and free allocated private data */
	void (*deinit)(void *priv);

	/* encrypt/decrypt return < 0 on error or >= 0 on success. The return
	 * value from decrypt_mpdu is passed as the keyidx value for
	 * decrypt_msdu. skb must have enough head and tail room for the
	 * encryption; if not, error will be returned; these functions are
	 * called for all MPDUs (i.e., fragments).
	 */
	int (*encrypt_mpdu)(struct sk_buff *skb, int hdr_len, void *priv);
	int (*decrypt_mpdu)(struct sk_buff *skb, int hdr_len, void *priv);

	/* These functions are called for full MSDUs, i.e. full frames.
	 * These can be NULL if full MSDU operations are not needed. */
	int (*encrypt_msdu)(struct sk_buff *skb, int hdr_len, void *priv);
	int (*decrypt_msdu)(struct sk_buff *skb, int keyidx, int hdr_len,
			    void *priv, struct rtllib_device* ieee);

	int (*set_key)(void *key, int len, u8 *seq, void *priv);
	int (*get_key)(void *key, int len, u8 *seq, void *priv);

	/* procfs handler for printing out key information and possible
	 * statistics */
	char * (*print_stats)(char *p, void *priv);

	/* maximum number of bytes added by encryption; encrypt buf is
	 * allocated with extra_prefix_len bytes, copy of in_buf, and
	 * extra_postfix_len; encrypt need not use all this space, but
	 * the result must start at the beginning of the struct buffer and
	 * correct length must be returned */
	int extra_prefix_len, extra_postfix_len;

	struct module *owner;
};

struct rtllib_crypt_data {
	struct list_head list; /* delayed deletion list */
	struct rtllib_crypto_ops *ops;
	void *priv;
	atomic_t refcnt;
};

int rtllib_register_crypto_ops(struct rtllib_crypto_ops *ops);
int rtllib_unregister_crypto_ops(struct rtllib_crypto_ops *ops);
struct rtllib_crypto_ops *rtllib_get_crypto_ops(const char *name);
void rtllib_crypt_deinit_entries(struct rtllib_device *, int);
void rtllib_crypt_deinit_handler(unsigned long);
void rtllib_crypt_delayed_deinit(struct rtllib_device *ieee,
				 struct rtllib_crypt_data **crypt);
#endif
