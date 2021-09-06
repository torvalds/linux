// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2020, 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (C) 2015-2018 Etnaviv Project
//

#include <linux/devcoredump.h>

#include "mcp251xfd.h"
#include "mcp251xfd-dump.h"

struct mcp251xfd_dump_iter {
	void *start;
	struct mcp251xfd_dump_object_header *hdr;
	void *data;
};

struct mcp251xfd_dump_reg_space {
	u16 base;
	u16 size;
};

struct mcp251xfd_dump_ring {
	enum mcp251xfd_dump_object_ring_key key;
	u32 val;
};

static const struct mcp251xfd_dump_reg_space mcp251xfd_dump_reg_space[] = {
	{
		.base = MCP251XFD_REG_CON,
		.size = MCP251XFD_REG_FLTOBJ(32) - MCP251XFD_REG_CON,
	}, {
		.base = MCP251XFD_RAM_START,
		.size = MCP251XFD_RAM_SIZE,
	}, {
		.base = MCP251XFD_REG_OSC,
		.size = MCP251XFD_REG_DEVID - MCP251XFD_REG_OSC,
	},
};

static void mcp251xfd_dump_header(struct mcp251xfd_dump_iter *iter,
				  enum mcp251xfd_dump_object_type object_type,
				  const void *data_end)
{
	struct mcp251xfd_dump_object_header *hdr = iter->hdr;
	unsigned int len;

	len = data_end - iter->data;
	if (!len)
		return;

	hdr->magic = cpu_to_le32(MCP251XFD_DUMP_MAGIC);
	hdr->type = cpu_to_le32(object_type);
	hdr->offset = cpu_to_le32(iter->data - iter->start);
	hdr->len = cpu_to_le32(len);

	iter->hdr++;
	iter->data += len;
}

static void mcp251xfd_dump_registers(const struct mcp251xfd_priv *priv,
				     struct mcp251xfd_dump_iter *iter)
{
	const int val_bytes = regmap_get_val_bytes(priv->map_rx);
	struct mcp251xfd_dump_object_reg *reg = iter->data;
	unsigned int i, j;
	int err;

	for (i = 0; i < ARRAY_SIZE(mcp251xfd_dump_reg_space); i++) {
		const struct mcp251xfd_dump_reg_space *reg_space;
		void *buf;

		reg_space = &mcp251xfd_dump_reg_space[i];

		buf = kmalloc(reg_space->size, GFP_KERNEL);
		if (!buf)
			goto out;

		err = regmap_bulk_read(priv->map_reg, reg_space->base,
				       buf, reg_space->size / val_bytes);
		if (err) {
			kfree(buf);
			continue;
		}

		for (j = 0; j < reg_space->size; j += sizeof(u32), reg++) {
			reg->reg = cpu_to_le32(reg_space->base + j);
			reg->val = cpu_to_le32p(buf + j);
		}

		kfree(buf);
	}

 out:
	mcp251xfd_dump_header(iter, MCP251XFD_DUMP_OBJECT_TYPE_REG, reg);
}

static void mcp251xfd_dump_ring(struct mcp251xfd_dump_iter *iter,
				enum mcp251xfd_dump_object_type object_type,
				const struct mcp251xfd_dump_ring *dump_ring,
				unsigned int len)
{
	struct mcp251xfd_dump_object_reg *reg = iter->data;
	unsigned int i;

	for (i = 0; i < len; i++, reg++) {
		reg->reg = cpu_to_le32(dump_ring[i].key);
		reg->val = cpu_to_le32(dump_ring[i].val);
	}

	mcp251xfd_dump_header(iter, object_type, reg);
}

static void mcp251xfd_dump_tef_ring(const struct mcp251xfd_priv *priv,
				    struct mcp251xfd_dump_iter *iter)
{
	const struct mcp251xfd_tef_ring *tef = priv->tef;
	const struct mcp251xfd_tx_ring *tx = priv->tx;
	const struct mcp251xfd_dump_ring dump_ring[] = {
		{
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_HEAD,
			.val = tef->head,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_TAIL,
			.val = tef->tail,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_BASE,
			.val = 0,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_NR,
			.val = 0,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_FIFO_NR,
			.val = 0,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_NUM,
			.val = tx->obj_num,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_SIZE,
			.val = sizeof(struct mcp251xfd_hw_tef_obj),
		},
	};

	mcp251xfd_dump_ring(iter, MCP251XFD_DUMP_OBJECT_TYPE_TEF,
			    dump_ring, ARRAY_SIZE(dump_ring));
}

static void mcp251xfd_dump_rx_ring_one(const struct mcp251xfd_priv *priv,
				       struct mcp251xfd_dump_iter *iter,
				       const struct mcp251xfd_rx_ring *rx)
{
	const struct mcp251xfd_dump_ring dump_ring[] = {
		{
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_HEAD,
			.val = rx->head,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_TAIL,
			.val = rx->tail,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_BASE,
			.val = rx->base,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_NR,
			.val = rx->nr,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_FIFO_NR,
			.val = rx->fifo_nr,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_NUM,
			.val = rx->obj_num,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_SIZE,
			.val = rx->obj_size,
		},
	};

	mcp251xfd_dump_ring(iter, MCP251XFD_DUMP_OBJECT_TYPE_RX,
			    dump_ring, ARRAY_SIZE(dump_ring));
}

static void mcp251xfd_dump_rx_ring(const struct mcp251xfd_priv *priv,
				   struct mcp251xfd_dump_iter *iter)
{
	struct mcp251xfd_rx_ring *rx_ring;
	unsigned int i;

	mcp251xfd_for_each_rx_ring(priv, rx_ring, i)
		mcp251xfd_dump_rx_ring_one(priv, iter, rx_ring);
}

static void mcp251xfd_dump_tx_ring(const struct mcp251xfd_priv *priv,
				   struct mcp251xfd_dump_iter *iter)
{
	const struct mcp251xfd_tx_ring *tx = priv->tx;
	const struct mcp251xfd_dump_ring dump_ring[] = {
		{
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_HEAD,
			.val = tx->head,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_TAIL,
			.val = tx->tail,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_BASE,
			.val = tx->base,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_NR,
			.val = 0,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_FIFO_NR,
			.val = MCP251XFD_TX_FIFO,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_NUM,
			.val = tx->obj_num,
		}, {
			.key = MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_SIZE,
			.val = tx->obj_size,
		},
	};

	mcp251xfd_dump_ring(iter, MCP251XFD_DUMP_OBJECT_TYPE_TX,
			    dump_ring, ARRAY_SIZE(dump_ring));
}

static void mcp251xfd_dump_end(const struct mcp251xfd_priv *priv,
			       struct mcp251xfd_dump_iter *iter)
{
	struct mcp251xfd_dump_object_header *hdr = iter->hdr;

	hdr->magic = cpu_to_le32(MCP251XFD_DUMP_MAGIC);
	hdr->type = cpu_to_le32(MCP251XFD_DUMP_OBJECT_TYPE_END);
	hdr->offset = cpu_to_le32(0);
	hdr->len = cpu_to_le32(0);

	/* provoke NULL pointer access, if used after END object */
	iter->hdr = NULL;
}

void mcp251xfd_dump(const struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_dump_iter iter;
	unsigned int rings_num, obj_num;
	unsigned int file_size = 0;
	unsigned int i;

	/* register space + end marker */
	obj_num = 2;

	/* register space */
	for (i = 0; i < ARRAY_SIZE(mcp251xfd_dump_reg_space); i++)
		file_size += mcp251xfd_dump_reg_space[i].size / sizeof(u32) *
			sizeof(struct mcp251xfd_dump_object_reg);

	/* TEF ring, RX ring, TX rings */
	rings_num = 1 + priv->rx_ring_num + 1;
	obj_num += rings_num;
	file_size += rings_num * __MCP251XFD_DUMP_OBJECT_RING_KEY_MAX  *
		sizeof(struct mcp251xfd_dump_object_reg);

	/* size of the headers */
	file_size += sizeof(*iter.hdr) * obj_num;

	/* allocate the file in vmalloc memory, it's likely to be big */
	iter.start = __vmalloc(file_size, GFP_KERNEL | __GFP_NOWARN |
			       __GFP_ZERO | __GFP_NORETRY);
	if (!iter.start) {
		netdev_warn(priv->ndev, "Failed to allocate devcoredump file.\n");
		return;
	}

	/* point the data member after the headers */
	iter.hdr = iter.start;
	iter.data = &iter.hdr[obj_num];

	mcp251xfd_dump_registers(priv, &iter);
	mcp251xfd_dump_tef_ring(priv, &iter);
	mcp251xfd_dump_rx_ring(priv, &iter);
	mcp251xfd_dump_tx_ring(priv, &iter);
	mcp251xfd_dump_end(priv, &iter);

	dev_coredumpv(&priv->spi->dev, iter.start,
		      iter.data - iter.start, GFP_KERNEL);
}
