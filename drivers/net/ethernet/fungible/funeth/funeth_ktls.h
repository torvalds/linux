/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef _FUN_KTLS_H
#define _FUN_KTLS_H

struct net_device;
struct funeth_priv;

#ifdef CONFIG_TLS_DEVICE
#include <net/tls.h>

struct fun_ktls_tx_ctx {
	__be64 tlsid;
	u32 next_seq;
};

int fun_ktls_init(struct net_device *netdev);
void fun_ktls_cleanup(struct funeth_priv *fp);

#else

static inline void fun_ktls_init(struct net_device *netdev)
{
}

static inline void fun_ktls_cleanup(struct funeth_priv *fp)
{
}
#endif

#endif /* _FUN_KTLS_H */
