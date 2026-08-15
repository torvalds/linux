// SPDX-License-Identifier: GPL-2.0
/* Forwarding and multicast database interface for the rtl8365mb switch family
 *
 * Copyright (C) 2022 Alvin Šipraga <alsi@bang-olufsen.dk>
 */

#include <linux/etherdevice.h>

#include "rtl8365mb_l2.h"
#include "rtl8365mb_table.h"
#include <linux/regmap.h>

#define RTL8365MB_L2_ENTRY_SIZE			6

#define RTL8365MB_L2_UC_D0_MAC5_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_UC_D0_MAC4_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_UC_D1_MAC3_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_UC_D1_MAC2_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_UC_D2_MAC1_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_UC_D2_MAC0_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_UC_D3_VID_MSK		GENMASK(11, 0)
#define RTL8365MB_L2_UC_D3_IVL_MSK		GENMASK(13, 13)
#define RTL8365MB_L2_UC_D3_PORT_EXT_MSK	GENMASK(15, 15)
#define   RTL8365MB_L2_UC_PORT_HI_MSK		GENMASK(3, 3)
#define RTL8365MB_L2_UC_D4_EFID_MSK		GENMASK(2, 0)
#define RTL8365MB_L2_UC_D4_FID_MSK		GENMASK(6, 3)
#define RTL8365MB_L2_UC_D4_SA_PRI_MSK		GENMASK(7, 7)
#define RTL8365MB_L2_UC_D4_PORT_MSK		GENMASK(10, 8)
#define   RTL8365MB_L2_UC_PORT_LO_MSK		GENMASK(2, 0)
#define RTL8365MB_L2_UC_D4_AGE_MSK		GENMASK(13, 11)
#define RTL8365MB_L2_UC_D4_AUTH_MSK		GENMASK(14, 14)
#define RTL8365MB_L2_UC_D4_SA_BLOCK_MSK	GENMASK(15, 15)

#define RTL8365MB_L2_UC_D5_DA_BLOCK_MSK	GENMASK(0, 0)
#define RTL8365MB_L2_UC_D5_PRIORITY_MSK	GENMASK(3, 1)
#define RTL8365MB_L2_UC_D5_FWD_PRI_MSK		GENMASK(4, 4)
#define RTL8365MB_L2_UC_D5_STATIC_MSK		GENMASK(5, 5)

#define RTL8365MB_L2_MC_D0_MAC5_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_MC_D0_MAC4_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_MC_D1_MAC3_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_MC_D1_MAC2_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_MC_D2_MAC1_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_MC_D2_MAC0_MSK		GENMASK(15, 8)
#define RTL8365MB_L2_MC_D3_VID_MSK		GENMASK(11, 0)
#define RTL8365MB_L2_MC_D3_IVL_MSK		GENMASK(13, 13)
#define RTL8365MB_L2_MC_D3_MBR_HI1_MSK		GENMASK(15, 14)
#define   RTL8365MB_L2_MC_MBR_HI1_MSK		GENMASK(9, 8)

#define RTL8365MB_L2_MC_D4_MBR_MSK		GENMASK(7, 0)
#define   RTL8365MB_L2_MC_MBR_LO_MSK		GENMASK(7, 0)
#define RTL8365MB_L2_MC_D4_IGMPIDX_MSK		GENMASK(15, 8)

#define RTL8365MB_L2_MC_D5_IGMP_ASIC_MSK	GENMASK(0, 0)
#define RTL8365MB_L2_MC_D5_PRIORITY_MSK	GENMASK(3, 1)
#define RTL8365MB_L2_MC_D5_FWD_PRI_MSK		GENMASK(4, 4)
#define RTL8365MB_L2_MC_D5_STATIC_MSK		GENMASK(5, 5)
#define RTL8365MB_L2_MC_D5_MBR_HI2_MSK		GENMASK(7, 7)
#define   RTL8365MB_L2_MC_MBR_HI2_MSK		GENMASK(10, 10)

/* Port flush command registers - writing a 1 to the port's MASK bit will
 * initiate the flush procedure. Completion is signalled when the corresponding
 * BUSY bit is 0.
 */
#define RTL8365MB_L2_FLUSH_PORT_REG		0x0A36
#define   RTL8365MB_L2_FLUSH_PORT_MSK_MSK	GENMASK(7, 0)
#define   RTL8365MB_L2_FLUSH_PORT_BUSY_MSK	GENMASK(15, 8)

#define RTL8365MB_L2_FLUSH_PORT_EXT_REG		0x0A35
#define   RTL8365MB_L2_FLUSH_PORT_EXT_MSK_MSK	GENMASK(2, 0)
#define   RTL8365MB_L2_FLUSH_PORT_EXT_BUSY_MSK	GENMASK(5, 3)

#define RTL8365MB_L2_FLUSH_CTRL1_REG		0x0A37
#define   RTL8365MB_L2_FLUSH_CTRL1_VID_MSK	GENMASK(11, 0)
#define   RTL8365MB_L2_FLUSH_CTRL1_FID_MSK	GENMASK(15, 12)

#define RTL8365MB_L2_FLUSH_CTRL2_REG		0x0A38
#define   RTL8365MB_L2_FLUSH_CTRL2_MODE_MSK	GENMASK(1, 0)
#define   RTL8365MB_L2_FLUSH_CTRL2_MODE_PORT	0
#define   RTL8365MB_L2_FLUSH_CTRL2_MODE_PORT_VID 1
#define   RTL8365MB_L2_FLUSH_CTRL2_MODE_PORT_FID 2
#define   RTL8365MB_L2_FLUSH_CTRL2_TYPE_MSK	GENMASK(2, 2)
#define   RTL8365MB_L2_FLUSH_CTRL2_TYPE_DYNAMIC	0
#define   RTL8365MB_L2_FLUSH_CTRL2_TYPE_BOTH	1

/* This flushes the entire LUT, reading it back it will turn 0 when the
 * operation is complete
 */
#define RTL8365MB_L2_FLUSH_CTRL3_REG		0x0A39
#define   RTL8365MB_L2_FLUSH_CTRL3_MSK		GENMASK(0, 0)

struct rtl8365mb_l2_uc_key {
	u8 mac_addr[ETH_ALEN];
	u16 vid;
	u16 fid;
	bool ivl;
	u16 efid;
};

struct rtl8365mb_l2_uc {
	struct rtl8365mb_l2_uc_key key;
	u8 port;
	u8 age;
	u8 priority;

	bool sa_block;
	bool da_block;
	bool auth;
	bool is_static;
	bool sa_pri;
	bool fwd_pri;
};

struct rtl8365mb_l2_mc_key {
	u8 mac_addr[ETH_ALEN];
	union {
		u16 vid; /* IVL */
		u16 fid; /* SVL */
	};
	bool ivl;
};

struct rtl8365mb_l2_mc {
	struct rtl8365mb_l2_mc_key key;
	u16 member;
	u8 priority;
	u8 igmpidx;

	bool is_static;
	bool fwd_pri;
	bool igmp_asic;
};

static void rtl8365mb_l2_data_to_uc(const u16 *data, struct rtl8365mb_l2_uc *uc)
{
	u32 val;

	uc->key.mac_addr[5] = FIELD_GET(RTL8365MB_L2_UC_D0_MAC5_MSK, data[0]);
	uc->key.mac_addr[4] = FIELD_GET(RTL8365MB_L2_UC_D0_MAC4_MSK, data[0]);
	uc->key.mac_addr[3] = FIELD_GET(RTL8365MB_L2_UC_D1_MAC3_MSK, data[1]);
	uc->key.mac_addr[2] = FIELD_GET(RTL8365MB_L2_UC_D1_MAC2_MSK, data[1]);
	uc->key.mac_addr[1] = FIELD_GET(RTL8365MB_L2_UC_D2_MAC1_MSK, data[2]);
	uc->key.mac_addr[0] = FIELD_GET(RTL8365MB_L2_UC_D2_MAC0_MSK, data[2]);
	uc->key.efid = FIELD_GET(RTL8365MB_L2_UC_D4_EFID_MSK, data[4]);
	uc->key.vid = FIELD_GET(RTL8365MB_L2_UC_D3_VID_MSK, data[3]);
	uc->key.ivl = FIELD_GET(RTL8365MB_L2_UC_D3_IVL_MSK, data[3]);
	uc->key.fid = FIELD_GET(RTL8365MB_L2_UC_D4_FID_MSK, data[4]);
	uc->age = FIELD_GET(RTL8365MB_L2_UC_D4_AGE_MSK, data[4]);
	uc->auth = FIELD_GET(RTL8365MB_L2_UC_D4_AUTH_MSK, data[4]);

	val = FIELD_GET(RTL8365MB_L2_UC_D4_PORT_MSK, data[4]);
	uc->port = FIELD_PREP(RTL8365MB_L2_UC_PORT_LO_MSK, val);
	val = FIELD_GET(RTL8365MB_L2_UC_D3_PORT_EXT_MSK, data[3]);
	uc->port |= FIELD_PREP(RTL8365MB_L2_UC_PORT_HI_MSK, val);

	uc->sa_pri = FIELD_GET(RTL8365MB_L2_UC_D4_SA_PRI_MSK, data[4]);
	uc->fwd_pri = FIELD_GET(RTL8365MB_L2_UC_D5_FWD_PRI_MSK, data[5]);
	uc->sa_block = FIELD_GET(RTL8365MB_L2_UC_D4_SA_BLOCK_MSK, data[4]);
	uc->da_block = FIELD_GET(RTL8365MB_L2_UC_D5_DA_BLOCK_MSK, data[5]);
	uc->priority = FIELD_GET(RTL8365MB_L2_UC_D5_PRIORITY_MSK, data[5]);
	uc->is_static = FIELD_GET(RTL8365MB_L2_UC_D5_STATIC_MSK, data[5]);
}

static void rtl8365mb_l2_uc_to_data(const struct rtl8365mb_l2_uc *uc, u16 *data)
{
	u32 val;

	memset(data, 0, RTL8365MB_L2_ENTRY_SIZE * 2);
	data[0] |=
		FIELD_PREP(RTL8365MB_L2_UC_D0_MAC5_MSK, uc->key.mac_addr[5]);
	data[0] |=
		FIELD_PREP(RTL8365MB_L2_UC_D0_MAC4_MSK, uc->key.mac_addr[4]);
	data[1] |=
		FIELD_PREP(RTL8365MB_L2_UC_D1_MAC3_MSK, uc->key.mac_addr[3]);
	data[1] |=
		FIELD_PREP(RTL8365MB_L2_UC_D1_MAC2_MSK, uc->key.mac_addr[2]);
	data[2] |=
		FIELD_PREP(RTL8365MB_L2_UC_D2_MAC1_MSK, uc->key.mac_addr[1]);
	data[2] |=
		FIELD_PREP(RTL8365MB_L2_UC_D2_MAC0_MSK, uc->key.mac_addr[0]);
	data[3] |= FIELD_PREP(RTL8365MB_L2_UC_D3_VID_MSK, uc->key.vid);
	data[3] |= FIELD_PREP(RTL8365MB_L2_UC_D3_IVL_MSK, uc->key.ivl);

	val = FIELD_GET(RTL8365MB_L2_UC_PORT_HI_MSK, uc->port);
	data[3] |= FIELD_PREP(RTL8365MB_L2_UC_D3_PORT_EXT_MSK, val);

	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_FID_MSK, uc->key.fid);
	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_EFID_MSK, uc->key.efid);
	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_AGE_MSK, uc->age);
	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_AUTH_MSK, uc->auth);

	val = FIELD_GET(RTL8365MB_L2_UC_PORT_LO_MSK, uc->port);
	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_PORT_MSK, val);

	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_SA_PRI_MSK, uc->sa_pri);
	data[4] |= FIELD_PREP(RTL8365MB_L2_UC_D4_SA_BLOCK_MSK, uc->sa_block);
	data[5] |= FIELD_PREP(RTL8365MB_L2_UC_D5_FWD_PRI_MSK, uc->fwd_pri);
	data[5] |= FIELD_PREP(RTL8365MB_L2_UC_D5_DA_BLOCK_MSK, uc->da_block);
	data[5] |= FIELD_PREP(RTL8365MB_L2_UC_D5_PRIORITY_MSK, uc->priority);
	data[5] |= FIELD_PREP(RTL8365MB_L2_UC_D5_STATIC_MSK, uc->is_static);
}

static void rtl8365mb_l2_data_to_mc(const u16 *data, struct rtl8365mb_l2_mc *mc)
{
	u32 val;

	mc->key.mac_addr[5] = FIELD_GET(RTL8365MB_L2_MC_D0_MAC5_MSK, data[0]);
	mc->key.mac_addr[4] = FIELD_GET(RTL8365MB_L2_MC_D0_MAC4_MSK, data[0]);
	mc->key.mac_addr[3] = FIELD_GET(RTL8365MB_L2_MC_D1_MAC3_MSK, data[1]);
	mc->key.mac_addr[2] = FIELD_GET(RTL8365MB_L2_MC_D1_MAC2_MSK, data[1]);
	mc->key.mac_addr[1] = FIELD_GET(RTL8365MB_L2_MC_D2_MAC1_MSK, data[2]);
	mc->key.mac_addr[0] = FIELD_GET(RTL8365MB_L2_MC_D2_MAC0_MSK, data[2]);
	/* key.vid,key.fid shares the same memory space */
	mc->key.vid = FIELD_GET(RTL8365MB_L2_MC_D3_VID_MSK, data[3]);
	mc->key.ivl = FIELD_GET(RTL8365MB_L2_MC_D3_IVL_MSK, data[3]);
	mc->priority = FIELD_GET(RTL8365MB_L2_MC_D5_PRIORITY_MSK, data[5]);
	mc->fwd_pri = FIELD_GET(RTL8365MB_L2_MC_D5_FWD_PRI_MSK, data[5]);
	mc->is_static = FIELD_GET(RTL8365MB_L2_MC_D5_STATIC_MSK, data[5]);

	val = FIELD_GET(RTL8365MB_L2_MC_D4_MBR_MSK, data[4]);
	mc->member = FIELD_PREP(RTL8365MB_L2_MC_MBR_LO_MSK, val);
	val = FIELD_GET(RTL8365MB_L2_MC_D3_MBR_HI1_MSK, data[3]);
	mc->member |= FIELD_PREP(RTL8365MB_L2_MC_MBR_HI1_MSK, val);
	val = FIELD_GET(RTL8365MB_L2_MC_D5_MBR_HI2_MSK, data[5]);
	mc->member |= FIELD_PREP(RTL8365MB_L2_MC_MBR_HI2_MSK, val);

	mc->igmpidx = FIELD_GET(RTL8365MB_L2_MC_D4_IGMPIDX_MSK, data[4]);
	mc->igmp_asic = FIELD_GET(RTL8365MB_L2_MC_D5_IGMP_ASIC_MSK, data[5]);
}

static void rtl8365mb_l2_mc_to_data(const struct rtl8365mb_l2_mc *mc, u16 *data)
{
	u32 val;

	memset(data, 0, RTL8365MB_L2_ENTRY_SIZE * 2);
	data[0] |= FIELD_PREP(RTL8365MB_L2_MC_D0_MAC5_MSK, mc->key.mac_addr[5]);
	data[0] |= FIELD_PREP(RTL8365MB_L2_MC_D0_MAC4_MSK, mc->key.mac_addr[4]);
	data[1] |= FIELD_PREP(RTL8365MB_L2_MC_D1_MAC3_MSK, mc->key.mac_addr[3]);
	data[1] |= FIELD_PREP(RTL8365MB_L2_MC_D1_MAC2_MSK, mc->key.mac_addr[2]);
	data[2] |= FIELD_PREP(RTL8365MB_L2_MC_D2_MAC1_MSK, mc->key.mac_addr[1]);
	data[2] |= FIELD_PREP(RTL8365MB_L2_MC_D2_MAC0_MSK, mc->key.mac_addr[0]);
	data[3] |= FIELD_PREP(RTL8365MB_L2_MC_D3_VID_MSK, mc->key.vid);
	data[3] |= FIELD_PREP(RTL8365MB_L2_MC_D3_IVL_MSK, mc->key.ivl);

	val = FIELD_GET(RTL8365MB_L2_MC_MBR_HI1_MSK, mc->member);
	data[3] |= FIELD_PREP(RTL8365MB_L2_MC_D3_MBR_HI1_MSK, val);

	val = FIELD_GET(RTL8365MB_L2_MC_MBR_LO_MSK, mc->member);
	data[4] |= FIELD_PREP(RTL8365MB_L2_MC_D4_MBR_MSK, val);

	data[4] |= FIELD_PREP(RTL8365MB_L2_MC_D4_IGMPIDX_MSK, mc->igmpidx);
	data[5] |= FIELD_PREP(RTL8365MB_L2_MC_D5_IGMP_ASIC_MSK, mc->igmp_asic);
	data[5] |= FIELD_PREP(RTL8365MB_L2_MC_D5_PRIORITY_MSK, mc->priority);
	data[5] |= FIELD_PREP(RTL8365MB_L2_MC_D5_FWD_PRI_MSK, mc->fwd_pri);
	data[5] |= FIELD_PREP(RTL8365MB_L2_MC_D5_STATIC_MSK, mc->is_static);

	val = FIELD_GET(RTL8365MB_L2_MC_MBR_HI2_MSK, mc->member);
	data[5] |= FIELD_PREP(RTL8365MB_L2_MC_D5_MBR_HI2_MSK, val);
}

/*
 * rtl8365mb_l2_get_next_uc() - get the next Unicast L2 entry
 * @priv: realtek_priv pointer
 * @addr: as input, the table index to start the walk
 *        as output, the found table index
 * @port: restrict the walk on entries related to port
 * @entry: returned L2 Unicast table entry
 *
 * This function gets the next unicast L2 table entry starting from @addr
 * and checking exclusively entries related to @port.
 *
 * On success, it returns 0, updates @addr to the index of the found entry,
 * and populates @entry. If the search reaches the end of the table and
 * wraps around and @addr will be strictly lower than the input @addr.
 * Callers must detect this wrap-around condition to prevent infinite loops.
 *
 * If the table contains no matching entries at all, it returns -ENOENT
 * and leaves @addr and @entry unmodified.
 *
 * Return: Returns 0 on success, a negative error on failure.
 **/
int rtl8365mb_l2_get_next_uc(struct realtek_priv *priv, u16 *addr, int port,
			     struct realtek_fdb_entry *entry)
{
	u16 data[RTL8365MB_L2_ENTRY_SIZE] = { 0 };
	struct rtl8365mb_l2_uc uc;
	int ret;

	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_READ, addr,
				    RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT,
				    port, data, RTL8365MB_L2_ENTRY_SIZE);
	if (ret)
		return ret;

	rtl8365mb_l2_data_to_uc(data, &uc);

	ether_addr_copy(entry->mac_addr, uc.key.mac_addr);
	entry->vid = uc.key.vid;
	entry->is_static = uc.is_static;

	return 0;
}

int rtl8365mb_l2_add_uc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 efid, u16 vid)
{
	u16 data[RTL8365MB_L2_ENTRY_SIZE] = { 0 };
	struct rtl8365mb_l2_uc uc = { 0 };
	u16 addr;
	int ret;

	memcpy(uc.key.mac_addr, mac_addr, ETH_ALEN);
	uc.key.efid = efid;
	uc.key.fid = 0;
	uc.key.ivl = true;
	uc.key.vid = vid;

	uc.port = port;
	/* Entries programmed by DSA (including those dynamically learned by
	 * the software bridge and injected into the CPU port via assisted
	 * learning) must be static. We do not let HW decrease age behind the
	 * OS's back. As a trade-off, these will show up as permanent to users.
	 */
	uc.is_static = true;
	/* age greater than 0 adds/updates entries */
	uc.age = 1;
	rtl8365mb_l2_uc_to_data(&uc, data);

	/* add the new entry or update an existing one */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_WRITE, &addr,
				    0, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);

	/* Assume the missing new entry as the table is full */
	if (ret == -ENOENT)
		return -ENOSPC;

	/* addr will hold the table index, but it is not used here */
	return ret;
}

int rtl8365mb_l2_del_uc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 efid, u16 vid)
{
	u16 data[RTL8365MB_L2_ENTRY_SIZE] = { 0 };
	struct rtl8365mb_l2_uc uc = { 0 };
	u16 addr;
	int ret;

	memcpy(uc.key.mac_addr, mac_addr, ETH_ALEN);
	uc.key.efid = efid;
	uc.key.fid = 0;
	uc.key.ivl = true;
	uc.key.vid = vid;
	/* age 0 deletes the entry */
	uc.age = 0;
	rtl8365mb_l2_uc_to_data(&uc, data);

	/* it looks like the switch will always add/update the entry,
	 * even when age is 0 or uc.key did not match an existing entry,
	 * just to immediately drop it because age is zero. You can still
	 * get the added/updated address from @addr
	 */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_WRITE, &addr,
				    0, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);

	if (ret == -ENOENT) {
		dev_dbg(priv->dev, "%s: %pM vid=%d efid=%d missing\n",
			__func__, mac_addr, vid, efid);
		/* Silently return success */
		return 0;
	}

	/* addr will hold the table index, but it is not used here */
	return ret;
}

int rtl8365mb_l2_flush(struct realtek_priv *priv, int port, u16 vid)
{
	int mode = vid ? RTL8365MB_L2_FLUSH_CTRL2_MODE_PORT_VID :
			 RTL8365MB_L2_FLUSH_CTRL2_MODE_PORT;
	u32 val, mask;
	int ret;

	mutex_lock(&priv->map_lock);

	/* Configure flushing mode; only flush dynamic entries */
	ret = regmap_write(priv->map_nolock, RTL8365MB_L2_FLUSH_CTRL2_REG,
			   FIELD_PREP(RTL8365MB_L2_FLUSH_CTRL2_MODE_MSK,
				      mode) |
			   FIELD_PREP(RTL8365MB_L2_FLUSH_CTRL2_TYPE_MSK,
				      RTL8365MB_L2_FLUSH_CTRL2_TYPE_DYNAMIC));
	if (ret)
		goto out;

	ret = regmap_write(priv->map_nolock, RTL8365MB_L2_FLUSH_CTRL1_REG,
			   FIELD_PREP(RTL8365MB_L2_FLUSH_CTRL1_VID_MSK, vid));

	if (ret)
		goto out;
	/* Now issue the flush command and wait for its completion. There are
	 * two registers for this purpose, and which one to use depends on the
	 * port number. The _EXT register is for ports 8 or higher.
	 */
	if (port < 8) {
		val = FIELD_PREP(RTL8365MB_L2_FLUSH_PORT_MSK_MSK,
				 BIT(port) & 0xFF);
		ret = regmap_write(priv->map_nolock,
				   RTL8365MB_L2_FLUSH_PORT_REG, val);
		if (ret)
			goto out;

		mask = FIELD_PREP(RTL8365MB_L2_FLUSH_PORT_BUSY_MSK,
				  BIT(port) & 0xFF);
		ret = regmap_read_poll_timeout(priv->map_nolock,
					       RTL8365MB_L2_FLUSH_PORT_REG,
					       val, !(val & mask), 10, 10000);
		if (ret)
			goto out;
	} else {
		val = FIELD_PREP(RTL8365MB_L2_FLUSH_PORT_EXT_MSK_MSK,
				 BIT(port) >> 8);
		ret = regmap_write(priv->map_nolock,
				   RTL8365MB_L2_FLUSH_PORT_EXT_REG, val);
		if (ret)
			goto out;

		mask = FIELD_PREP(RTL8365MB_L2_FLUSH_PORT_EXT_BUSY_MSK,
				  BIT(port) >> 8);
		ret = regmap_read_poll_timeout(priv->map_nolock,
					       RTL8365MB_L2_FLUSH_PORT_EXT_REG,
					       val, !(val & mask), 10, 10000);
		if (ret)
			goto out;
	}

out:
	mutex_unlock(&priv->map_lock);

	return ret;
}

int rtl8365mb_l2_add_mc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 vid)
{
	u16 data[RTL8365MB_L2_ENTRY_SIZE] = { 0 };
	struct rtl8365mb_l2_mc mc = { 0 };
	u16 addr;
	int ret;

	memcpy(mc.key.mac_addr, mac_addr, ETH_ALEN);
	mc.key.vid = vid;
	mc.key.ivl = true;
	/* Already set the port and is_static, although not used in OP_READ,
	 * data will be ready for OP_WRITE if it is a new entry.
	 */
	mc.member |= BIT(port);
	mc.is_static = 1;
	rtl8365mb_l2_mc_to_data(&mc, data);

	/* First look for an existing entry (to get existing port members) */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_READ, &addr,
				    RTL8365MB_TABLE_L2_METHOD_MAC, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);
	if (!ret) {
		/* There is already an entry... */
		rtl8365mb_l2_data_to_mc(data, &mc);
		dev_dbg(priv->dev,
			"%s: found %pM addr=%d member=0x%x igmpidx=0x%x %s\n",
			__func__, mac_addr, addr, mc.member, mc.igmpidx,
			mc.is_static ? "static" : "dynamic");
		/* the port must be added as a member */
		mc.member |= BIT(port);

		if (!mc.is_static) {
			dev_dbg(priv->dev,
				"%s: promoting addr=%d group to static\n",
				__func__, addr);
			mc.is_static = 1;
		}

		rtl8365mb_l2_mc_to_data(&mc, data);
	} else if (ret == -ENOENT) {
		/* New entry, no need to update data again as it already
		 * includes the member.
		 *
		 * Multicast hardware entries do not support EFID (bridge
		 * isolation). However, traffic isolation is still maintained
		 * because the hardware applies the port isolation masks
		 * (pmasks) configured in bridge_join after the L2 lookup.
		 * Entries from different bridges will collide on the same
		 * MAC+VID slot with an OR'ed member mask, but packets will
		 * only exit through ports allowed by the source port's pmask.
		 */
	} else {
		return ret;
	}

	/* add the new entry or update an existing one */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_WRITE, &addr,
				    0, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);

	/* Assume the missing new entry as the table is full */
	if (ret == -ENOENT)
		return -ENOSPC;

	return ret;
}

int rtl8365mb_l2_del_mc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 vid)
{
	u16 data[RTL8365MB_L2_ENTRY_SIZE] = { 0 };
	struct rtl8365mb_l2_mc mc = { 0 };
	u16 addr;
	int ret;

	memcpy(mc.key.mac_addr, mac_addr, ETH_ALEN);
	mc.key.vid = vid;
	mc.key.ivl = true;
	rtl8365mb_l2_mc_to_data(&mc, data);

	/* First look for an existing entry (to get existing port members) */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_READ, &addr,
				    RTL8365MB_TABLE_L2_METHOD_MAC, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);
	if (ret == -ENOENT) {
		dev_dbg(priv->dev, "%s: %pM vid=%d missing\n",
			__func__, mac_addr, vid);
		/* Silently return success */
		return 0;
	}

	if (ret)
		/* Return on any other error */
		return ret;

	rtl8365mb_l2_data_to_mc(data, &mc);
	dev_dbg(priv->dev,
		"%s: found %pM addr=%d member=0x%x igmpidx=0x%x %s\n",
		__func__, mac_addr, addr, mc.member, mc.igmpidx,
		mc.is_static ? "static" : "dynamic");
	/* the port must be removed as a member */
	mc.member &= ~BIT(port);
	if (!mc.member) {
		/* Multicast entries do not have an age field. Clearing both
		 * the member portmask and is_static flags is the hardware
		 * signal to invalidate and reclaim the L2 table slot.
		 */
		mc.is_static = 0;
		mc.igmpidx = 0;
		mc.priority = 0;
		mc.fwd_pri = 0;
		mc.igmp_asic = 0;
	}
	rtl8365mb_l2_mc_to_data(&mc, data);

	/* update the existing entry. */
	ret = rtl8365mb_table_query(priv, RTL8365MB_TABLE_L2,
				    RTL8365MB_TABLE_OP_WRITE, &addr,
				    0, 0,
				    data, RTL8365MB_L2_ENTRY_SIZE);
	return ret;
}
