/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef NFP_CRYPTO_H
#define NFP_CRYPTO_H 1

#ifdef CONFIG_TLS_DEVICE
int nfp_net_tls_init(struct nfp_net *nn);
#else
static inline int nfp_net_tls_init(struct nfp_net *nn)
{
	return 0;
}
#endif

#endif
