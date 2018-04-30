/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ifndef __MLX5E_TLS_H__
#define __MLX5E_TLS_H__

#ifdef CONFIG_MLX5_EN_TLS

#include <net/tls.h>
#include "en.h"

struct mlx5e_tls_offload_context {
	struct tls_offload_context base;
	u32 expected_seq;
	__be32 swid;
};

static inline struct mlx5e_tls_offload_context *
mlx5e_get_tls_tx_context(struct tls_context *tls_ctx)
{
	BUILD_BUG_ON(sizeof(struct mlx5e_tls_offload_context) >
		     TLS_OFFLOAD_CONTEXT_SIZE);
	return container_of(tls_offload_ctx(tls_ctx),
			    struct mlx5e_tls_offload_context,
			    base);
}

void mlx5e_tls_build_netdev(struct mlx5e_priv *priv);

#else

static inline void mlx5e_tls_build_netdev(struct mlx5e_priv *priv) { }

#endif

#endif /* __MLX5E_TLS_H__ */
