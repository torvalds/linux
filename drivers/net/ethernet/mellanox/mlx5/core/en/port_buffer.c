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
#include "port_buffer.h"

int mlx5e_port_query_buffer(struct mlx5e_priv *priv,
			    struct mlx5e_port_buffer *port_buffer)
{
	u16 port_buff_cell_sz = priv->dcbx.port_buff_cell_sz;
	struct mlx5_core_dev *mdev = priv->mdev;
	int sz = MLX5_ST_SZ_BYTES(pbmc_reg);
	u32 total_used = 0;
	void *buffer;
	void *out;
	int err;
	int i;

	out = kzalloc(sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5e_port_query_pbmc(mdev, out);
	if (err)
		goto out;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
		buffer = MLX5_ADDR_OF(pbmc_reg, out, buffer[i]);
		port_buffer->buffer[i].lossy =
			MLX5_GET(bufferx_reg, buffer, lossy);
		port_buffer->buffer[i].epsb =
			MLX5_GET(bufferx_reg, buffer, epsb);
		port_buffer->buffer[i].size =
			MLX5_GET(bufferx_reg, buffer, size) * port_buff_cell_sz;
		port_buffer->buffer[i].xon =
			MLX5_GET(bufferx_reg, buffer, xon_threshold) * port_buff_cell_sz;
		port_buffer->buffer[i].xoff =
			MLX5_GET(bufferx_reg, buffer, xoff_threshold) * port_buff_cell_sz;
		total_used += port_buffer->buffer[i].size;

		mlx5e_dbg(HW, priv, "buffer %d: size=%d, xon=%d, xoff=%d, epsb=%d, lossy=%d\n", i,
			  port_buffer->buffer[i].size,
			  port_buffer->buffer[i].xon,
			  port_buffer->buffer[i].xoff,
			  port_buffer->buffer[i].epsb,
			  port_buffer->buffer[i].lossy);
	}

	port_buffer->port_buffer_size =
		MLX5_GET(pbmc_reg, out, port_buffer_size) * port_buff_cell_sz;
	port_buffer->spare_buffer_size =
		port_buffer->port_buffer_size - total_used;

	mlx5e_dbg(HW, priv, "total buffer size=%d, spare buffer size=%d\n",
		  port_buffer->port_buffer_size,
		  port_buffer->spare_buffer_size);
out:
	kfree(out);
	return err;
}

static int port_set_buffer(struct mlx5e_priv *priv,
			   struct mlx5e_port_buffer *port_buffer)
{
	u16 port_buff_cell_sz = priv->dcbx.port_buff_cell_sz;
	struct mlx5_core_dev *mdev = priv->mdev;
	int sz = MLX5_ST_SZ_BYTES(pbmc_reg);
	void *in;
	int err;
	int i;

	in = kzalloc(sz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	err = mlx5e_port_query_pbmc(mdev, in);
	if (err)
		goto out;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
		void *buffer = MLX5_ADDR_OF(pbmc_reg, in, buffer[i]);
		u64 size = port_buffer->buffer[i].size;
		u64 xoff = port_buffer->buffer[i].xoff;
		u64 xon = port_buffer->buffer[i].xon;

		do_div(size, port_buff_cell_sz);
		do_div(xoff, port_buff_cell_sz);
		do_div(xon, port_buff_cell_sz);
		MLX5_SET(bufferx_reg, buffer, size, size);
		MLX5_SET(bufferx_reg, buffer, lossy, port_buffer->buffer[i].lossy);
		MLX5_SET(bufferx_reg, buffer, xoff_threshold, xoff);
		MLX5_SET(bufferx_reg, buffer, xon_threshold, xon);
	}

	err = mlx5e_port_set_pbmc(mdev, in);
out:
	kfree(in);
	return err;
}

/* xoff = ((301+2.16 * len [m]) * speed [Gbps] + 2.72 MTU [B])
 * minimum speed value is 40Gbps
 */
static u32 calculate_xoff(struct mlx5e_priv *priv, unsigned int mtu)
{
	u32 speed;
	u32 xoff;
	int err;

	err = mlx5e_port_linkspeed(priv->mdev, &speed);
	if (err)
		speed = SPEED_40000;
	speed = max_t(u32, speed, SPEED_40000);

	xoff = (301 + 216 * priv->dcbx.cable_len / 100) * speed / 1000 + 272 * mtu / 100;

	mlx5e_dbg(HW, priv, "%s: xoff=%d\n", __func__, xoff);
	return xoff;
}

static int update_xoff_threshold(struct mlx5e_port_buffer *port_buffer,
				 u32 xoff, unsigned int max_mtu, u16 port_buff_cell_sz)
{
	int i;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
		if (port_buffer->buffer[i].lossy) {
			port_buffer->buffer[i].xoff = 0;
			port_buffer->buffer[i].xon  = 0;
			continue;
		}

		if (port_buffer->buffer[i].size <
		    (xoff + max_mtu + port_buff_cell_sz)) {
			pr_err("buffer_size[%d]=%d is not enough for lossless buffer\n",
			       i, port_buffer->buffer[i].size);
			return -ENOMEM;
		}

		port_buffer->buffer[i].xoff = port_buffer->buffer[i].size - xoff;
		port_buffer->buffer[i].xon  =
			port_buffer->buffer[i].xoff - max_mtu;
	}

	return 0;
}

/**
 *	update_buffer_lossy	- Update buffer configuration based on pfc
 *	@max_mtu: netdev's max_mtu
 *	@pfc_en: <input> current pfc configuration
 *	@buffer: <input> current prio to buffer mapping
 *	@xoff:   <input> xoff value
 *	@port_buff_cell_sz: <input> port buffer cell_size
 *	@port_buffer: <output> port receive buffer configuration
 *	@change: <output>
 *
 *	Update buffer configuration based on pfc configuration and
 *	priority to buffer mapping.
 *	Buffer's lossy bit is changed to:
 *		lossless if there is at least one PFC enabled priority
 *		mapped to this buffer lossy if all priorities mapped to
 *		this buffer are PFC disabled
 *
 *	@return: 0 if no error,
 *	sets change to true if buffer configuration was modified.
 */
static int update_buffer_lossy(unsigned int max_mtu,
			       u8 pfc_en, u8 *buffer, u32 xoff, u16 port_buff_cell_sz,
			       struct mlx5e_port_buffer *port_buffer,
			       bool *change)
{
	bool changed = false;
	u8 lossy_count;
	u8 prio_count;
	u8 lossy;
	int prio;
	int err;
	int i;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
		prio_count = 0;
		lossy_count = 0;

		for (prio = 0; prio < MLX5E_MAX_PRIORITY; prio++) {
			if (buffer[prio] != i)
				continue;

			prio_count++;
			lossy_count += !(pfc_en & (1 << prio));
		}

		if (lossy_count == prio_count)
			lossy = 1;
		else /* lossy_count < prio_count */
			lossy = 0;

		if (lossy != port_buffer->buffer[i].lossy) {
			port_buffer->buffer[i].lossy = lossy;
			changed = true;
		}
	}

	if (changed) {
		err = update_xoff_threshold(port_buffer, xoff, max_mtu, port_buff_cell_sz);
		if (err)
			return err;

		*change = true;
	}

	return 0;
}

static int fill_pfc_en(struct mlx5_core_dev *mdev, u8 *pfc_en)
{
	u32 g_rx_pause, g_tx_pause;
	int err;

	err = mlx5_query_port_pause(mdev, &g_rx_pause, &g_tx_pause);
	if (err)
		return err;

	/* If global pause enabled, set all active buffers to lossless.
	 * Otherwise, check PFC setting.
	 */
	if (g_rx_pause || g_tx_pause)
		*pfc_en = 0xff;
	else
		err = mlx5_query_port_pfc(mdev, pfc_en, NULL);

	return err;
}

#define MINIMUM_MAX_MTU 9216
int mlx5e_port_manual_buffer_config(struct mlx5e_priv *priv,
				    u32 change, unsigned int mtu,
				    struct ieee_pfc *pfc,
				    u32 *buffer_size,
				    u8 *prio2buffer)
{
	u16 port_buff_cell_sz = priv->dcbx.port_buff_cell_sz;
	struct mlx5e_port_buffer port_buffer;
	u32 xoff = calculate_xoff(priv, mtu);
	bool update_prio2buffer = false;
	u8 buffer[MLX5E_MAX_PRIORITY];
	bool update_buffer = false;
	unsigned int max_mtu;
	u32 total_used = 0;
	u8 curr_pfc_en;
	int err;
	int i;

	mlx5e_dbg(HW, priv, "%s: change=%x\n", __func__, change);
	max_mtu = max_t(unsigned int, priv->netdev->max_mtu, MINIMUM_MAX_MTU);

	err = mlx5e_port_query_buffer(priv, &port_buffer);
	if (err)
		return err;

	if (change & MLX5E_PORT_BUFFER_CABLE_LEN) {
		update_buffer = true;
		err = update_xoff_threshold(&port_buffer, xoff, max_mtu, port_buff_cell_sz);
		if (err)
			return err;
	}

	if (change & MLX5E_PORT_BUFFER_PFC) {
		err = mlx5e_port_query_priority2buffer(priv->mdev, buffer);
		if (err)
			return err;

		err = update_buffer_lossy(max_mtu, pfc->pfc_en, buffer, xoff, port_buff_cell_sz,
					  &port_buffer, &update_buffer);
		if (err)
			return err;
	}

	if (change & MLX5E_PORT_BUFFER_PRIO2BUFFER) {
		update_prio2buffer = true;
		err = fill_pfc_en(priv->mdev, &curr_pfc_en);
		if (err)
			return err;

		err = update_buffer_lossy(max_mtu, curr_pfc_en, prio2buffer, port_buff_cell_sz,
					  xoff, &port_buffer, &update_buffer);
		if (err)
			return err;
	}

	if (change & MLX5E_PORT_BUFFER_SIZE) {
		for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
			mlx5e_dbg(HW, priv, "%s: buffer[%d]=%d\n", __func__, i, buffer_size[i]);
			if (!port_buffer.buffer[i].lossy && !buffer_size[i]) {
				mlx5e_dbg(HW, priv, "%s: lossless buffer[%d] size cannot be zero\n",
					  __func__, i);
				return -EINVAL;
			}

			port_buffer.buffer[i].size = buffer_size[i];
			total_used += buffer_size[i];
		}

		mlx5e_dbg(HW, priv, "%s: total buffer requested=%d\n", __func__, total_used);

		if (total_used > port_buffer.port_buffer_size)
			return -EINVAL;

		update_buffer = true;
		err = update_xoff_threshold(&port_buffer, xoff, max_mtu, port_buff_cell_sz);
		if (err)
			return err;
	}

	/* Need to update buffer configuration if xoff value is changed */
	if (!update_buffer && xoff != priv->dcbx.xoff) {
		update_buffer = true;
		err = update_xoff_threshold(&port_buffer, xoff, max_mtu, port_buff_cell_sz);
		if (err)
			return err;
	}
	priv->dcbx.xoff = xoff;

	/* Apply the settings */
	if (update_buffer) {
		err = port_set_buffer(priv, &port_buffer);
		if (err)
			return err;
	}

	if (update_prio2buffer)
		err = mlx5e_port_set_priority2buffer(priv->mdev, prio2buffer);

	return err;
}
