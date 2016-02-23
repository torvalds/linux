/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2015, Applied Micro Circuits Corporation
 * Author: Iyappan Subramanian <isubramanian@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xgene_enet_main.h"
#include "xgene_enet_hw.h"
#include "xgene_enet_ring2.h"

static void xgene_enet_ring_init(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;
	u64 addr = ring->dma;

	if (xgene_enet_ring_owner(ring->id) == RING_OWNER_CPU) {
		ring_cfg[0] |= SET_VAL(X2_INTLINE, ring->id & RING_BUFNUM_MASK);
		ring_cfg[3] |= SET_BIT(X2_DEQINTEN);
	}
	ring_cfg[0] |= SET_VAL(X2_CFGCRID, 1);

	addr >>= 8;
	ring_cfg[2] |= QCOHERENT | SET_VAL(RINGADDRL, addr);

	addr >>= 27;
	ring_cfg[3] |= SET_VAL(RINGSIZE, ring->cfgsize)
		    | ACCEPTLERR
		    | SET_VAL(RINGADDRH, addr);
	ring_cfg[4] |= SET_VAL(X2_SELTHRSH, 1);
	ring_cfg[5] |= SET_BIT(X2_QBASE_AM) | SET_BIT(X2_MSG_AM);
}

static void xgene_enet_ring_set_type(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;
	bool is_bufpool;
	u32 val;

	is_bufpool = xgene_enet_is_bufpool(ring->id);
	val = (is_bufpool) ? RING_BUFPOOL : RING_REGULAR;
	ring_cfg[4] |= SET_VAL(X2_RINGTYPE, val);
	if (is_bufpool)
		ring_cfg[3] |= SET_VAL(RINGMODE, BUFPOOL_MODE);
}

static void xgene_enet_ring_set_recombbuf(struct xgene_enet_desc_ring *ring)
{
	u32 *ring_cfg = ring->state;

	ring_cfg[3] |= RECOMBBUF;
	ring_cfg[4] |= SET_VAL(X2_RECOMTIMEOUT, 0x7);
}

static void xgene_enet_ring_wr32(struct xgene_enet_desc_ring *ring,
				 u32 offset, u32 data)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);

	iowrite32(data, pdata->ring_csr_addr + offset);
}

static void xgene_enet_write_ring_state(struct xgene_enet_desc_ring *ring)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);
	int i;

	xgene_enet_ring_wr32(ring, CSR_RING_CONFIG, ring->num);
	for (i = 0; i < pdata->ring_ops->num_ring_config; i++) {
		xgene_enet_ring_wr32(ring, CSR_RING_WR_BASE + (i * 4),
				     ring->state[i]);
	}
}

static void xgene_enet_clr_ring_state(struct xgene_enet_desc_ring *ring)
{
	memset(ring->state, 0, sizeof(ring->state));
	xgene_enet_write_ring_state(ring);
}

static void xgene_enet_set_ring_state(struct xgene_enet_desc_ring *ring)
{
	enum xgene_ring_owner owner;

	xgene_enet_ring_set_type(ring);

	owner = xgene_enet_ring_owner(ring->id);
	if (owner == RING_OWNER_ETH0 || owner == RING_OWNER_ETH1)
		xgene_enet_ring_set_recombbuf(ring);

	xgene_enet_ring_init(ring);
	xgene_enet_write_ring_state(ring);
}

static void xgene_enet_set_ring_id(struct xgene_enet_desc_ring *ring)
{
	u32 ring_id_val, ring_id_buf;
	bool is_bufpool;

	if (xgene_enet_ring_owner(ring->id) == RING_OWNER_CPU)
		return;

	is_bufpool = xgene_enet_is_bufpool(ring->id);

	ring_id_val = ring->id & GENMASK(9, 0);
	ring_id_val |= OVERWRITE;

	ring_id_buf = (ring->num << 9) & GENMASK(18, 9);
	ring_id_buf |= PREFETCH_BUF_EN;
	if (is_bufpool)
		ring_id_buf |= IS_BUFFER_POOL;

	xgene_enet_ring_wr32(ring, CSR_RING_ID, ring_id_val);
	xgene_enet_ring_wr32(ring, CSR_RING_ID_BUF, ring_id_buf);
}

static void xgene_enet_clr_desc_ring_id(struct xgene_enet_desc_ring *ring)
{
	u32 ring_id;

	ring_id = ring->id | OVERWRITE;
	xgene_enet_ring_wr32(ring, CSR_RING_ID, ring_id);
	xgene_enet_ring_wr32(ring, CSR_RING_ID_BUF, 0);
}

static struct xgene_enet_desc_ring *xgene_enet_setup_ring(
				    struct xgene_enet_desc_ring *ring)
{
	bool is_bufpool;
	u32 addr, i;

	xgene_enet_clr_ring_state(ring);
	xgene_enet_set_ring_state(ring);
	xgene_enet_set_ring_id(ring);

	ring->slots = xgene_enet_get_numslots(ring->id, ring->size);

	is_bufpool = xgene_enet_is_bufpool(ring->id);
	if (is_bufpool || xgene_enet_ring_owner(ring->id) != RING_OWNER_CPU)
		return ring;

	addr = CSR_VMID0_INTR_MBOX + (4 * (ring->id & RING_BUFNUM_MASK));
	xgene_enet_ring_wr32(ring, addr, ring->irq_mbox_dma >> 10);

	for (i = 0; i < ring->slots; i++)
		xgene_enet_mark_desc_slot_empty(&ring->raw_desc[i]);

	return ring;
}

static void xgene_enet_clear_ring(struct xgene_enet_desc_ring *ring)
{
	xgene_enet_clr_desc_ring_id(ring);
	xgene_enet_clr_ring_state(ring);
}

static void xgene_enet_wr_cmd(struct xgene_enet_desc_ring *ring, int count)
{
	u32 data = 0;

	if (xgene_enet_ring_owner(ring->id) == RING_OWNER_CPU) {
		data = SET_VAL(X2_INTLINE, ring->id & RING_BUFNUM_MASK) |
		       INTR_CLEAR;
	}
	data |= (count & GENMASK(16, 0));

	iowrite32(data, ring->cmd);
}

static u32 xgene_enet_ring_len(struct xgene_enet_desc_ring *ring)
{
	u32 __iomem *cmd_base = ring->cmd_base;
	u32 ring_state, num_msgs;

	ring_state = ioread32(&cmd_base[1]);
	num_msgs = GET_VAL(X2_NUMMSGSINQ, ring_state);

	return num_msgs;
}

struct xgene_ring_ops xgene_ring2_ops = {
	.num_ring_config = X2_NUM_RING_CONFIG,
	.num_ring_id_shift = 13,
	.setup = xgene_enet_setup_ring,
	.clear = xgene_enet_clear_ring,
	.wr_cmd = xgene_enet_wr_cmd,
	.len = xgene_enet_ring_len,
};
