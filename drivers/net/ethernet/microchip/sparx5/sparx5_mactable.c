// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/switchdev.h>
#include <linux/if_bridge.h>
#include <linux/iopoll.h>

#include "sparx5_main_regs.h"
#include "sparx5_main.h"

/* Commands for Mac Table Command register */
#define MAC_CMD_LEARN         0 /* Insert (Learn) 1 entry */
#define MAC_CMD_UNLEARN       1 /* Unlearn (Forget) 1 entry */
#define MAC_CMD_LOOKUP        2 /* Look up 1 entry */
#define MAC_CMD_READ          3 /* Read entry at Mac Table Index */
#define MAC_CMD_WRITE         4 /* Write entry at Mac Table Index */
#define MAC_CMD_SCAN          5 /* Scan (Age or find next) */
#define MAC_CMD_FIND_SMALLEST 6 /* Get next entry */
#define MAC_CMD_CLEAR_ALL     7 /* Delete all entries in table */

/* Commands for MAC_ENTRY_ADDR_TYPE */
#define  MAC_ENTRY_ADDR_TYPE_UPSID_PN         0
#define  MAC_ENTRY_ADDR_TYPE_UPSID_CPU_OR_INT 1
#define  MAC_ENTRY_ADDR_TYPE_GLAG             2
#define  MAC_ENTRY_ADDR_TYPE_MC_IDX           3

#define TABLE_UPDATE_SLEEP_US 10
#define TABLE_UPDATE_TIMEOUT_US 100000

struct sparx5_mact_entry {
	struct list_head list;
	unsigned char mac[ETH_ALEN];
	u32 flags;
#define MAC_ENT_ALIVE	BIT(0)
#define MAC_ENT_MOVED	BIT(1)
#define MAC_ENT_LOCK	BIT(2)
	u16 vid;
	u16 port;
};

static int sparx5_mact_get_status(struct sparx5 *sparx5)
{
	return spx5_rd(sparx5, LRN_COMMON_ACCESS_CTRL);
}

static int sparx5_mact_wait_for_completion(struct sparx5 *sparx5)
{
	u32 val;

	return readx_poll_timeout(sparx5_mact_get_status,
		sparx5, val,
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_GET(val) == 0,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

static void sparx5_mact_select(struct sparx5 *sparx5,
			       const unsigned char mac[ETH_ALEN],
			       u16 vid)
{
	u32 macl = 0, mach = 0;

	/* Set the MAC address to handle and the vlan associated in a format
	 * understood by the hardware.
	 */
	mach |= vid    << 16;
	mach |= mac[0] << 8;
	mach |= mac[1] << 0;
	macl |= mac[2] << 24;
	macl |= mac[3] << 16;
	macl |= mac[4] << 8;
	macl |= mac[5] << 0;

	spx5_wr(mach, sparx5, LRN_MAC_ACCESS_CFG_0);
	spx5_wr(macl, sparx5, LRN_MAC_ACCESS_CFG_1);
}

int sparx5_mact_learn(struct sparx5 *sparx5, int pgid,
		      const unsigned char mac[ETH_ALEN], u16 vid)
{
	int addr, type, ret;

	if (pgid < SPX5_PORTS) {
		type = MAC_ENTRY_ADDR_TYPE_UPSID_PN;
		addr = pgid % 32;
		addr += (pgid / 32) << 5; /* Add upsid */
	} else {
		type = MAC_ENTRY_ADDR_TYPE_MC_IDX;
		addr = pgid - SPX5_PORTS;
	}

	mutex_lock(&sparx5->lock);

	sparx5_mact_select(sparx5, mac, vid);

	/* MAC entry properties */
	spx5_wr(LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_ADDR_SET(addr) |
		LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_ADDR_TYPE_SET(type) |
		LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_VLD_SET(1) |
		LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_LOCKED_SET(1),
		sparx5, LRN_MAC_ACCESS_CFG_2);
	spx5_wr(0, sparx5, LRN_MAC_ACCESS_CFG_3);

	/*  Insert/learn new entry */
	spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET(MAC_CMD_LEARN) |
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
		sparx5, LRN_COMMON_ACCESS_CTRL);

	ret = sparx5_mact_wait_for_completion(sparx5);

	mutex_unlock(&sparx5->lock);

	return ret;
}

int sparx5_mc_unsync(struct net_device *dev, const unsigned char *addr)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5 *sparx5 = port->sparx5;

	return sparx5_mact_forget(sparx5, addr, port->pvid);
}

int sparx5_mc_sync(struct net_device *dev, const unsigned char *addr)
{
	struct sparx5_port *port = netdev_priv(dev);
	struct sparx5 *sparx5 = port->sparx5;

	return sparx5_mact_learn(sparx5, PGID_CPU, addr, port->pvid);
}

static int sparx5_mact_get(struct sparx5 *sparx5,
			   unsigned char mac[ETH_ALEN],
			   u16 *vid, u32 *pcfg2)
{
	u32 mach, macl, cfg2;
	int ret = -ENOENT;

	cfg2 = spx5_rd(sparx5, LRN_MAC_ACCESS_CFG_2);
	if (LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_VLD_GET(cfg2)) {
		mach = spx5_rd(sparx5, LRN_MAC_ACCESS_CFG_0);
		macl = spx5_rd(sparx5, LRN_MAC_ACCESS_CFG_1);
		mac[0] = ((mach >> 8)  & 0xff);
		mac[1] = ((mach >> 0)  & 0xff);
		mac[2] = ((macl >> 24) & 0xff);
		mac[3] = ((macl >> 16) & 0xff);
		mac[4] = ((macl >> 8)  & 0xff);
		mac[5] = ((macl >> 0)  & 0xff);
		*vid = mach >> 16;
		*pcfg2 = cfg2;
		ret = 0;
	}

	return ret;
}

bool sparx5_mact_getnext(struct sparx5 *sparx5,
			 unsigned char mac[ETH_ALEN], u16 *vid, u32 *pcfg2)
{
	u32 cfg2;
	int ret;

	mutex_lock(&sparx5->lock);

	sparx5_mact_select(sparx5, mac, *vid);

	spx5_wr(LRN_SCAN_NEXT_CFG_SCAN_NEXT_IGNORE_LOCKED_ENA_SET(1) |
		LRN_SCAN_NEXT_CFG_SCAN_NEXT_UNTIL_FOUND_ENA_SET(1),
		sparx5, LRN_SCAN_NEXT_CFG);
	spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET
		(MAC_CMD_FIND_SMALLEST) |
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
		sparx5, LRN_COMMON_ACCESS_CTRL);

	ret = sparx5_mact_wait_for_completion(sparx5);
	if (ret == 0) {
		ret = sparx5_mact_get(sparx5, mac, vid, &cfg2);
		if (ret == 0)
			*pcfg2 = cfg2;
	}

	mutex_unlock(&sparx5->lock);

	return ret == 0;
}

bool sparx5_mact_find(struct sparx5 *sparx5,
		      const unsigned char mac[ETH_ALEN], u16 vid, u32 *pcfg2)
{
	int ret;
	u32 cfg2;

	mutex_lock(&sparx5->lock);

	sparx5_mact_select(sparx5, mac, vid);

	/* Issue a lookup command */
	spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET(MAC_CMD_LOOKUP) |
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
		sparx5, LRN_COMMON_ACCESS_CTRL);

	ret = sparx5_mact_wait_for_completion(sparx5);
	if (ret == 0) {
		cfg2 = spx5_rd(sparx5, LRN_MAC_ACCESS_CFG_2);
		if (LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_VLD_GET(cfg2))
			*pcfg2 = cfg2;
		else
			ret = -ENOENT;
	}

	mutex_unlock(&sparx5->lock);

	return ret;
}

int sparx5_mact_forget(struct sparx5 *sparx5,
		       const unsigned char mac[ETH_ALEN], u16 vid)
{
	int ret;

	mutex_lock(&sparx5->lock);

	sparx5_mact_select(sparx5, mac, vid);

	/* Issue an unlearn command */
	spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET(MAC_CMD_UNLEARN) |
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
		sparx5, LRN_COMMON_ACCESS_CTRL);

	ret = sparx5_mact_wait_for_completion(sparx5);

	mutex_unlock(&sparx5->lock);

	return ret;
}

static struct sparx5_mact_entry *alloc_mact_entry(struct sparx5 *sparx5,
						  const unsigned char *mac,
						  u16 vid, u16 port_index)
{
	struct sparx5_mact_entry *mact_entry;

	mact_entry = devm_kzalloc(sparx5->dev,
				  sizeof(*mact_entry), GFP_ATOMIC);
	if (!mact_entry)
		return NULL;

	memcpy(mact_entry->mac, mac, ETH_ALEN);
	mact_entry->vid = vid;
	mact_entry->port = port_index;
	return mact_entry;
}

static struct sparx5_mact_entry *find_mact_entry(struct sparx5 *sparx5,
						 const unsigned char *mac,
						 u16 vid, u16 port_index)
{
	struct sparx5_mact_entry *mact_entry;
	struct sparx5_mact_entry *res = NULL;

	mutex_lock(&sparx5->mact_lock);
	list_for_each_entry(mact_entry, &sparx5->mact_entries, list) {
		if (mact_entry->vid == vid &&
		    ether_addr_equal(mac, mact_entry->mac) &&
		    mact_entry->port == port_index) {
			res = mact_entry;
			break;
		}
	}
	mutex_unlock(&sparx5->mact_lock);

	return res;
}

static void sparx5_fdb_call_notifiers(enum switchdev_notifier_type type,
				      const char *mac, u16 vid,
				      struct net_device *dev, bool offloaded)
{
	struct switchdev_notifier_fdb_info info = {};

	info.addr = mac;
	info.vid = vid;
	info.offloaded = offloaded;
	call_switchdev_notifiers(type, dev, &info.info, NULL);
}

int sparx5_add_mact_entry(struct sparx5 *sparx5,
			  struct net_device *dev,
			  u16 portno,
			  const unsigned char *addr, u16 vid)
{
	struct sparx5_mact_entry *mact_entry;
	int ret;
	u32 cfg2;

	ret = sparx5_mact_find(sparx5, addr, vid, &cfg2);
	if (!ret)
		return 0;

	/* In case the entry already exists, don't add it again to SW,
	 * just update HW, but we need to look in the actual HW because
	 * it is possible for an entry to be learn by HW and before the
	 * mact thread to start the frame will reach CPU and the CPU will
	 * add the entry but without the extern_learn flag.
	 */
	mact_entry = find_mact_entry(sparx5, addr, vid, portno);
	if (mact_entry)
		goto update_hw;

	/* Add the entry in SW MAC table not to get the notification when
	 * SW is pulling again
	 */
	mact_entry = alloc_mact_entry(sparx5, addr, vid, portno);
	if (!mact_entry)
		return -ENOMEM;

	mutex_lock(&sparx5->mact_lock);
	list_add_tail(&mact_entry->list, &sparx5->mact_entries);
	mutex_unlock(&sparx5->mact_lock);

update_hw:
	ret = sparx5_mact_learn(sparx5, portno, addr, vid);

	/* New entry? */
	if (mact_entry->flags == 0) {
		mact_entry->flags |= MAC_ENT_LOCK; /* Don't age this */
		sparx5_fdb_call_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE, addr, vid,
					  dev, true);
	}

	return ret;
}

int sparx5_del_mact_entry(struct sparx5 *sparx5,
			  const unsigned char *addr,
			  u16 vid)
{
	struct sparx5_mact_entry *mact_entry, *tmp;

	/* Delete the entry in SW MAC table not to get the notification when
	 * SW is pulling again
	 */
	mutex_lock(&sparx5->mact_lock);
	list_for_each_entry_safe(mact_entry, tmp, &sparx5->mact_entries,
				 list) {
		if ((vid == 0 || mact_entry->vid == vid) &&
		    ether_addr_equal(addr, mact_entry->mac)) {
			list_del(&mact_entry->list);
			devm_kfree(sparx5->dev, mact_entry);

			sparx5_mact_forget(sparx5, addr, mact_entry->vid);
		}
	}
	mutex_unlock(&sparx5->mact_lock);

	return 0;
}

static void sparx5_mact_handle_entry(struct sparx5 *sparx5,
				     unsigned char mac[ETH_ALEN],
				     u16 vid, u32 cfg2)
{
	struct sparx5_mact_entry *mact_entry;
	bool found = false;
	u16 port;

	if (LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_ADDR_TYPE_GET(cfg2) !=
	    MAC_ENTRY_ADDR_TYPE_UPSID_PN)
		return;

	port = LRN_MAC_ACCESS_CFG_2_MAC_ENTRY_ADDR_GET(cfg2);
	if (port >= SPX5_PORTS)
		return;

	if (!test_bit(port, sparx5->bridge_mask))
		return;

	mutex_lock(&sparx5->mact_lock);
	list_for_each_entry(mact_entry, &sparx5->mact_entries, list) {
		if (mact_entry->vid == vid &&
		    ether_addr_equal(mac, mact_entry->mac)) {
			found = true;
			mact_entry->flags |= MAC_ENT_ALIVE;
			if (mact_entry->port != port) {
				dev_warn(sparx5->dev, "Entry move: %d -> %d\n",
					 mact_entry->port, port);
				mact_entry->port = port;
				mact_entry->flags |= MAC_ENT_MOVED;
			}
			/* Entry handled */
			break;
		}
	}
	mutex_unlock(&sparx5->mact_lock);

	if (found && !(mact_entry->flags & MAC_ENT_MOVED))
		/* Present, not moved */
		return;

	if (!found) {
		/* Entry not found - now add */
		mact_entry = alloc_mact_entry(sparx5, mac, vid, port);
		if (!mact_entry)
			return;

		mact_entry->flags |= MAC_ENT_ALIVE;
		mutex_lock(&sparx5->mact_lock);
		list_add_tail(&mact_entry->list, &sparx5->mact_entries);
		mutex_unlock(&sparx5->mact_lock);
	}

	/* New or moved entry - notify bridge */
	sparx5_fdb_call_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE,
				  mac, vid, sparx5->ports[port]->ndev,
				  true);
}

void sparx5_mact_pull_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct sparx5 *sparx5 = container_of(del_work, struct sparx5,
					     mact_work);
	struct sparx5_mact_entry *mact_entry, *tmp;
	unsigned char mac[ETH_ALEN];
	u32 cfg2;
	u16 vid;
	int ret;

	/* Reset MAC entry flags */
	mutex_lock(&sparx5->mact_lock);
	list_for_each_entry(mact_entry, &sparx5->mact_entries, list)
		mact_entry->flags &= MAC_ENT_LOCK;
	mutex_unlock(&sparx5->mact_lock);

	/* MAIN mac address processing loop */
	vid = 0;
	memset(mac, 0, sizeof(mac));
	do {
		mutex_lock(&sparx5->lock);
		sparx5_mact_select(sparx5, mac, vid);
		spx5_wr(LRN_SCAN_NEXT_CFG_SCAN_NEXT_UNTIL_FOUND_ENA_SET(1),
			sparx5, LRN_SCAN_NEXT_CFG);
		spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET
			(MAC_CMD_FIND_SMALLEST) |
			LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
			sparx5, LRN_COMMON_ACCESS_CTRL);
		ret = sparx5_mact_wait_for_completion(sparx5);
		if (ret == 0)
			ret = sparx5_mact_get(sparx5, mac, &vid, &cfg2);
		mutex_unlock(&sparx5->lock);
		if (ret == 0)
			sparx5_mact_handle_entry(sparx5, mac, vid, cfg2);
	} while (ret == 0);

	mutex_lock(&sparx5->mact_lock);
	list_for_each_entry_safe(mact_entry, tmp, &sparx5->mact_entries,
				 list) {
		/* If the entry is in HW or permanent, then skip */
		if (mact_entry->flags & (MAC_ENT_ALIVE | MAC_ENT_LOCK))
			continue;

		sparx5_fdb_call_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE,
					  mact_entry->mac, mact_entry->vid,
					  sparx5->ports[mact_entry->port]->ndev,
					  true);

		list_del(&mact_entry->list);
		devm_kfree(sparx5->dev, mact_entry);
	}
	mutex_unlock(&sparx5->mact_lock);

	queue_delayed_work(sparx5->mact_queue, &sparx5->mact_work,
			   SPX5_MACT_PULL_DELAY);
}

void sparx5_set_ageing(struct sparx5 *sparx5, int msecs)
{
	int value = max(1, msecs / 10); /* unit 10 ms */

	spx5_rmw(LRN_AUTOAGE_CFG_UNIT_SIZE_SET(2) | /* 10 ms */
		 LRN_AUTOAGE_CFG_PERIOD_VAL_SET(value / 2), /* one bit ageing */
		 LRN_AUTOAGE_CFG_UNIT_SIZE |
		 LRN_AUTOAGE_CFG_PERIOD_VAL,
		 sparx5,
		 LRN_AUTOAGE_CFG(0));
}

void sparx5_mact_init(struct sparx5 *sparx5)
{
	mutex_init(&sparx5->lock);

	/*  Flush MAC table */
	spx5_wr(LRN_COMMON_ACCESS_CTRL_CPU_ACCESS_CMD_SET(MAC_CMD_CLEAR_ALL) |
		LRN_COMMON_ACCESS_CTRL_MAC_TABLE_ACCESS_SHOT_SET(1),
		sparx5, LRN_COMMON_ACCESS_CTRL);

	if (sparx5_mact_wait_for_completion(sparx5) != 0)
		dev_warn(sparx5->dev, "MAC flush error\n");

	sparx5_set_ageing(sparx5, BR_DEFAULT_AGEING_TIME / HZ * 1000);
}
