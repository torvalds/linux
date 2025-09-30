/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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
 */
#ifndef __MLX5_EN_PORT_BUFFER_H__
#define __MLX5_EN_PORT_BUFFER_H__

#include "en.h"
#include "port.h"

#define MLX5E_MAX_NETWORK_BUFFER 8
#define MLX5E_TOTAL_BUFFERS 10
#define MLX5E_DEFAULT_CABLE_LEN 7 /* 7 meters */

#define MLX5_BUFFER_SUPPORTED(mdev) (MLX5_CAP_GEN(mdev, pcam_reg) && \
				     MLX5_CAP_PCAM_REG(mdev, pbmc) && \
				     MLX5_CAP_PCAM_REG(mdev, pptb))

enum {
	MLX5E_PORT_BUFFER_CABLE_LEN   = BIT(0),
	MLX5E_PORT_BUFFER_PFC         = BIT(1),
	MLX5E_PORT_BUFFER_PRIO2BUFFER = BIT(2),
	MLX5E_PORT_BUFFER_SIZE        = BIT(3),
};

struct mlx5e_bufferx_reg {
	u8   lossy;
	u8   epsb;
	u32  size;
	u32  xoff;
	u32  xon;
};

struct mlx5e_port_buffer {
	u32                       port_buffer_size;
	u32                       spare_buffer_size;
	u32                       headroom_size;	  /* Buffers 0-7 */
	u32                       internal_buffers_size;  /* Buffers 8-9 */
	struct mlx5e_bufferx_reg  buffer[MLX5E_MAX_NETWORK_BUFFER];
};

#ifdef CONFIG_MLX5_CORE_EN_DCB
int mlx5e_port_manual_buffer_config(struct mlx5e_priv *priv,
				    u32 change, unsigned int mtu,
				    struct ieee_pfc *pfc,
				    u32 *buffer_size,
				    u8 *prio2buffer);
#else
static inline int
mlx5e_port_manual_buffer_config(struct mlx5e_priv *priv,
				u32 change, unsigned int mtu,
				void *pfc,
				u32 *buffer_size,
				u8 *prio2buffer)
{
	return 0;
}
#endif

int mlx5e_port_query_buffer(struct mlx5e_priv *priv,
			    struct mlx5e_port_buffer *port_buffer);
#endif
