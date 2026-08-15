// SPDX-License-Identifier: GPL-2.0
/* Look-up table query interface for the rtl8365mb switch family
 *
 * Copyright (C) 2022 Alvin Šipraga <alsi@bang-olufsen.dk>
 */

#include "rtl8365mb_table.h"
#include <linux/regmap.h>

/* Table access control register */
#define RTL8365MB_TABLE_CTRL_REG		0x0500
/* Should be one of rtl8365mb_table enum members */
#define   RTL8365MB_TABLE_CTRL_TABLE_MASK	GENMASK(2, 0)
/* Should be one of rtl8365mb_table_op enum members */
#define   RTL8365MB_TABLE_CTRL_OP_MASK		GENMASK(3, 3)
/* Should be one of rtl8365mb_table_l2_method enum members */
#define   RTL8365MB_TABLE_CTRL_METHOD_MASK	GENMASK(6, 4)
#define   RTL8365MB_TABLE_CTRL_PORT_MASK	GENMASK(11, 8)

/* Table access address register */
#define RTL8365MB_TABLE_ACCESS_ADDR_REG		0x0501
#define   RTL8365MB_TABLE_ADDR_MASK		GENMASK(12, 0)

/* Table status register */
#define RTL8365MB_TABLE_STATUS_REG			0x0502
#define   RTL8365MB_TABLE_STATUS_ADDRESS_MASK		GENMASK(10, 0)
/* set for L3, unset for L2  */
#define   RTL8365MB_TABLE_STATUS_ADDR_TYPE_MASK		GENMASK(11, 11)
#define   RTL8365MB_TABLE_STATUS_HIT_STATUS_MASK	GENMASK(12, 12)
#define   RTL8365MB_TABLE_STATUS_BUSY_FLAG_MASK		GENMASK(13, 13)
#define   RTL8365MB_TABLE_STATUS_ADDRESS_EXT_MASK	GENMASK(14, 14)

/* Table read/write registers */
#define RTL8365MB_TABLE_WRITE_BASE			0x0510
#define RTL8365MB_TABLE_WRITE_REG(_x) \
		(RTL8365MB_TABLE_WRITE_BASE + (_x))
#define RTL8365MB_TABLE_READ_BASE			0x0520
#define RTL8365MB_TABLE_READ_REG(_x) \
		(RTL8365MB_TABLE_READ_BASE + (_x))
#define RTL8365MB_TABLE_10TH_DATA_MASK			GENMASK(3, 0)
#define RTL8365MB_TABLE_WRITE_10TH_REG \
		RTL8365MB_TABLE_WRITE_REG(RTL8365MB_TABLE_ENTRY_MAX_SIZE - 1)

static int rtl8365mb_table_poll_busy(struct realtek_priv *priv)
{
	u32 val;

	return regmap_read_poll_timeout(priv->map_nolock,
			RTL8365MB_TABLE_STATUS_REG, val,
			!FIELD_GET(RTL8365MB_TABLE_STATUS_BUSY_FLAG_MASK, val),
			10, 10000);
}

int rtl8365mb_table_query(struct realtek_priv *priv,
			  enum rtl8365mb_table table,
			  enum rtl8365mb_table_op op, u16 *addr,
			  enum rtl8365mb_table_l2_method method,
			  u16 port, u16 *data, size_t size)
{
	bool addr_as_input = true;
	bool write_data = false;
	int ret = 0;
	u32 cmd;
	u32 val;
	u32 hit;

	/* Prepare target table and operation (read or write) */
	cmd = 0;
	cmd |= FIELD_PREP(RTL8365MB_TABLE_CTRL_TABLE_MASK, table);
	cmd |= FIELD_PREP(RTL8365MB_TABLE_CTRL_OP_MASK, op);
	if (op == RTL8365MB_TABLE_OP_READ && table == RTL8365MB_TABLE_L2) {
		cmd |= FIELD_PREP(RTL8365MB_TABLE_CTRL_METHOD_MASK, method);
		switch (method) {
		case RTL8365MB_TABLE_L2_METHOD_MAC:
			/*
			 * Method MAC requires as input the same L2 table format
			 * you'll get as result. However, it might only use mac
			 * address and FID/VID fields.
			 */
			write_data = true;

			/* METHOD_MAC does not use addr as input, but may return
			 * the matched index.
			 */
			addr_as_input = false;

			break;
		case RTL8365MB_TABLE_L2_METHOD_ADDR:
		case RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT:
		case RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC:
		case RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_MC:
			break;
		case RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT:
			cmd |= FIELD_PREP(RTL8365MB_TABLE_CTRL_PORT_MASK, port);
			break;
		default:
			return -EINVAL;
		}
	} else if (op == RTL8365MB_TABLE_OP_WRITE) {
		write_data = true;

		/* Writing to L2 does not use addr as input, as the table index
		 * is derived from key fields.
		 */
		if (table == RTL8365MB_TABLE_L2)
			addr_as_input = false;
	}

	/* To prevent concurrent access to the look-up tables, take the regmap
	 * lock manually and access via the map_nolock regmap.
	 */
	mutex_lock(&priv->map_lock);

	/* Protect from a busy table access (i.e. previous access timeouts) */
	ret = rtl8365mb_table_poll_busy(priv);
	if (ret)
		goto out;

	/* Write entry data if writing to the table (or L2_METHOD_MAC) */
	if (write_data) {
		/* bulk write data up to 9th word */
		ret = regmap_bulk_write(priv->map_nolock,
					RTL8365MB_TABLE_WRITE_BASE,
					data,
					min_t(size_t, size,
					      RTL8365MB_TABLE_ENTRY_MAX_SIZE -
						      1));
		if (ret)
			goto out;

		/* 10th register uses only 4 least significant bits */
		if (size == RTL8365MB_TABLE_ENTRY_MAX_SIZE) {
			val = FIELD_PREP(RTL8365MB_TABLE_10TH_DATA_MASK,
					 data[size - 1]);
			ret = regmap_update_bits(priv->map_nolock,
						 RTL8365MB_TABLE_WRITE_10TH_REG,
						 RTL8365MB_TABLE_10TH_DATA_MASK,
						 val);
		}

		if (ret)
			goto out;
	}

	/* Write address (if needed) */
	if (addr_as_input) {
		ret = regmap_write(priv->map_nolock,
				   RTL8365MB_TABLE_ACCESS_ADDR_REG,
				   FIELD_PREP(RTL8365MB_TABLE_ADDR_MASK,
					      *addr));
		if (ret)
			goto out;
	}

	/* Execute */
	ret = regmap_write(priv->map_nolock, RTL8365MB_TABLE_CTRL_REG, cmd);
	if (ret)
		goto out;

	/* Poll for completion */
	ret = rtl8365mb_table_poll_busy(priv);
	if (ret)
		goto out;

	/* For both reads and writes to the L2 table, check status */
	if (table == RTL8365MB_TABLE_L2) {
		ret = regmap_read(priv->map_nolock, RTL8365MB_TABLE_STATUS_REG,
				  &val);
		if (ret)
			goto out;

		/* Did the query find an entry? */
		hit = FIELD_GET(RTL8365MB_TABLE_STATUS_HIT_STATUS_MASK, val);
		if (!hit) {
			ret = -ENOENT;
			goto out;
		}

		/* If so, extract the address */
		*addr = 0;
		*addr |= FIELD_GET(RTL8365MB_TABLE_STATUS_ADDRESS_MASK, val);
		*addr |= FIELD_GET(RTL8365MB_TABLE_STATUS_ADDRESS_EXT_MASK, val)
			 << 11;
		/* only set if it is a L3 address */
		*addr |= FIELD_GET(RTL8365MB_TABLE_STATUS_ADDR_TYPE_MASK, val)
			 << 12;
	}

	/* Finally, get the table entry if we were reading */
	if (op == RTL8365MB_TABLE_OP_READ) {
		ret = regmap_bulk_read(priv->map_nolock,
				       RTL8365MB_TABLE_READ_BASE,
				       data, size);
		if (ret)
			goto out;

		/* For the biggest table entries, the uppermost table
		 * entry register has space for only one nibble. Mask
		 * out the remainder bits. Empirically I saw nothing
		 * wrong with omitting this mask, but it may prevent
		 * unwanted behaviour. FYI.
		 */
		if (size == RTL8365MB_TABLE_ENTRY_MAX_SIZE) {
			val = FIELD_GET(RTL8365MB_TABLE_10TH_DATA_MASK,
					data[size - 1]);
			data[size - 1] = val;
		}
	}

out:
	mutex_unlock(&priv->map_lock);

	return ret;
}
