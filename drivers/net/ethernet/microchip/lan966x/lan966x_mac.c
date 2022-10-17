// SPDX-License-Identifier: GPL-2.0+

#include <net/switchdev.h>
#include "lan966x_main.h"

#define LAN966X_MAC_COLUMNS		4
#define MACACCESS_CMD_IDLE		0
#define MACACCESS_CMD_LEARN		1
#define MACACCESS_CMD_FORGET		2
#define MACACCESS_CMD_AGE		3
#define MACACCESS_CMD_GET_NEXT		4
#define MACACCESS_CMD_INIT		5
#define MACACCESS_CMD_READ		6
#define MACACCESS_CMD_WRITE		7
#define MACACCESS_CMD_SYNC_GET_NEXT	8

#define LAN966X_MAC_INVALID_ROW		-1

struct lan966x_mac_entry {
	struct list_head list;
	unsigned char mac[ETH_ALEN] __aligned(2);
	u16 vid;
	u16 port_index;
	int row;
	bool lag;
};

struct lan966x_mac_raw_entry {
	u32 mach;
	u32 macl;
	u32 maca;
	bool processed;
};

static int lan966x_mac_get_status(struct lan966x *lan966x)
{
	return lan_rd(lan966x, ANA_MACACCESS);
}

static int lan966x_mac_wait_for_completion(struct lan966x *lan966x)
{
	u32 val;

	return readx_poll_timeout_atomic(lan966x_mac_get_status,
					 lan966x, val,
					 (ANA_MACACCESS_MAC_TABLE_CMD_GET(val)) ==
					 MACACCESS_CMD_IDLE,
					 TABLE_UPDATE_SLEEP_US,
					 TABLE_UPDATE_TIMEOUT_US);
}

static void lan966x_mac_select(struct lan966x *lan966x,
			       const unsigned char mac[ETH_ALEN],
			       unsigned int vid)
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

	lan_wr(macl, lan966x, ANA_MACLDATA);
	lan_wr(mach, lan966x, ANA_MACHDATA);
}

static int __lan966x_mac_learn_locked(struct lan966x *lan966x, int pgid,
				      bool cpu_copy,
				      const unsigned char mac[ETH_ALEN],
				      unsigned int vid,
				      enum macaccess_entry_type type)
{
	lockdep_assert_held(&lan966x->mac_lock);

	lan966x_mac_select(lan966x, mac, vid);

	/* Issue a write command */
	lan_wr(ANA_MACACCESS_VALID_SET(1) |
	       ANA_MACACCESS_CHANGE2SW_SET(0) |
	       ANA_MACACCESS_MAC_CPU_COPY_SET(cpu_copy) |
	       ANA_MACACCESS_DEST_IDX_SET(pgid) |
	       ANA_MACACCESS_ENTRYTYPE_SET(type) |
	       ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_LEARN),
	       lan966x, ANA_MACACCESS);

	return lan966x_mac_wait_for_completion(lan966x);
}

static int __lan966x_mac_learn(struct lan966x *lan966x, int pgid,
			       bool cpu_copy,
			       const unsigned char mac[ETH_ALEN],
			       unsigned int vid,
			       enum macaccess_entry_type type)
{
	int ret;

	spin_lock(&lan966x->mac_lock);
	ret = __lan966x_mac_learn_locked(lan966x, pgid, cpu_copy, mac, vid, type);
	spin_unlock(&lan966x->mac_lock);

	return ret;
}

/* The mask of the front ports is encoded inside the mac parameter via a call
 * to lan966x_mdb_encode_mac().
 */
int lan966x_mac_ip_learn(struct lan966x *lan966x,
			 bool cpu_copy,
			 const unsigned char mac[ETH_ALEN],
			 unsigned int vid,
			 enum macaccess_entry_type type)
{
	WARN_ON(type != ENTRYTYPE_MACV4 && type != ENTRYTYPE_MACV6);

	return __lan966x_mac_learn(lan966x, 0, cpu_copy, mac, vid, type);
}

int lan966x_mac_learn(struct lan966x *lan966x, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid,
		      enum macaccess_entry_type type)
{
	WARN_ON(type != ENTRYTYPE_NORMAL && type != ENTRYTYPE_LOCKED);

	return __lan966x_mac_learn(lan966x, port, false, mac, vid, type);
}

static int lan966x_mac_learn_locked(struct lan966x *lan966x, int port,
				    const unsigned char mac[ETH_ALEN],
				    unsigned int vid,
				    enum macaccess_entry_type type)
{
	WARN_ON(type != ENTRYTYPE_NORMAL && type != ENTRYTYPE_LOCKED);

	return __lan966x_mac_learn_locked(lan966x, port, false, mac, vid, type);
}

static int lan966x_mac_forget_locked(struct lan966x *lan966x,
				     const unsigned char mac[ETH_ALEN],
				     unsigned int vid,
				     enum macaccess_entry_type type)
{
	lockdep_assert_held(&lan966x->mac_lock);

	lan966x_mac_select(lan966x, mac, vid);

	/* Issue a forget command */
	lan_wr(ANA_MACACCESS_ENTRYTYPE_SET(type) |
	       ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_FORGET),
	       lan966x, ANA_MACACCESS);

	return lan966x_mac_wait_for_completion(lan966x);
}

int lan966x_mac_forget(struct lan966x *lan966x,
		       const unsigned char mac[ETH_ALEN],
		       unsigned int vid,
		       enum macaccess_entry_type type)
{
	int ret;

	spin_lock(&lan966x->mac_lock);
	ret = lan966x_mac_forget_locked(lan966x, mac, vid, type);
	spin_unlock(&lan966x->mac_lock);

	return ret;
}

int lan966x_mac_cpu_learn(struct lan966x *lan966x, const char *addr, u16 vid)
{
	return lan966x_mac_learn(lan966x, PGID_CPU, addr, vid, ENTRYTYPE_LOCKED);
}

int lan966x_mac_cpu_forget(struct lan966x *lan966x, const char *addr, u16 vid)
{
	return lan966x_mac_forget(lan966x, addr, vid, ENTRYTYPE_LOCKED);
}

void lan966x_mac_set_ageing(struct lan966x *lan966x,
			    u32 ageing)
{
	lan_rmw(ANA_AUTOAGE_AGE_PERIOD_SET(ageing / 2),
		ANA_AUTOAGE_AGE_PERIOD,
		lan966x, ANA_AUTOAGE);
}

void lan966x_mac_init(struct lan966x *lan966x)
{
	/* Clear the MAC table */
	lan_wr(MACACCESS_CMD_INIT, lan966x, ANA_MACACCESS);
	lan966x_mac_wait_for_completion(lan966x);

	spin_lock_init(&lan966x->mac_lock);
	INIT_LIST_HEAD(&lan966x->mac_entries);
}

static struct lan966x_mac_entry *lan966x_mac_alloc_entry(struct lan966x_port *port,
							 const unsigned char *mac,
							 u16 vid)
{
	struct lan966x_mac_entry *mac_entry;

	mac_entry = kzalloc(sizeof(*mac_entry), GFP_ATOMIC);
	if (!mac_entry)
		return NULL;

	memcpy(mac_entry->mac, mac, ETH_ALEN);
	mac_entry->vid = vid;
	mac_entry->port_index = port->chip_port;
	mac_entry->row = LAN966X_MAC_INVALID_ROW;
	mac_entry->lag = port->bond ? true : false;
	return mac_entry;
}

static struct lan966x_mac_entry *lan966x_mac_find_entry(struct lan966x *lan966x,
							const unsigned char *mac,
							u16 vid, u16 port_index)
{
	struct lan966x_mac_entry *res = NULL;
	struct lan966x_mac_entry *mac_entry;

	list_for_each_entry(mac_entry, &lan966x->mac_entries, list) {
		if (mac_entry->vid == vid &&
		    ether_addr_equal(mac, mac_entry->mac) &&
		    mac_entry->port_index == port_index) {
			res = mac_entry;
			break;
		}
	}

	return res;
}

static int lan966x_mac_lookup(struct lan966x *lan966x,
			      const unsigned char mac[ETH_ALEN],
			      unsigned int vid, enum macaccess_entry_type type)
{
	int ret;

	lan966x_mac_select(lan966x, mac, vid);

	/* Issue a read command */
	lan_wr(ANA_MACACCESS_ENTRYTYPE_SET(type) |
	       ANA_MACACCESS_VALID_SET(1) |
	       ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_READ),
	       lan966x, ANA_MACACCESS);

	ret = lan966x_mac_wait_for_completion(lan966x);
	if (ret)
		return ret;

	return ANA_MACACCESS_VALID_GET(lan_rd(lan966x, ANA_MACACCESS));
}

static void lan966x_fdb_call_notifiers(enum switchdev_notifier_type type,
				       const char *mac, u16 vid,
				       struct net_device *dev)
{
	struct switchdev_notifier_fdb_info info = { 0 };

	info.addr = mac;
	info.vid = vid;
	info.offloaded = true;
	call_switchdev_notifiers(type, dev, &info.info, NULL);
}

int lan966x_mac_add_entry(struct lan966x *lan966x, struct lan966x_port *port,
			  const unsigned char *addr, u16 vid)
{
	struct lan966x_mac_entry *mac_entry;

	spin_lock(&lan966x->mac_lock);
	if (lan966x_mac_lookup(lan966x, addr, vid, ENTRYTYPE_NORMAL)) {
		spin_unlock(&lan966x->mac_lock);
		return 0;
	}

	/* In case the entry already exists, don't add it again to SW,
	 * just update HW, but we need to look in the actual HW because
	 * it is possible for an entry to be learn by HW and before we
	 * get the interrupt the frame will reach CPU and the CPU will
	 * add the entry but without the extern_learn flag.
	 */
	mac_entry = lan966x_mac_find_entry(lan966x, addr, vid, port->chip_port);
	if (mac_entry) {
		spin_unlock(&lan966x->mac_lock);
		goto mac_learn;
	}

	mac_entry = lan966x_mac_alloc_entry(port, addr, vid);
	if (!mac_entry) {
		spin_unlock(&lan966x->mac_lock);
		return -ENOMEM;
	}

	list_add_tail(&mac_entry->list, &lan966x->mac_entries);
	spin_unlock(&lan966x->mac_lock);

	lan966x_fdb_call_notifiers(SWITCHDEV_FDB_OFFLOADED, addr, vid,
				   port->bond ?: port->dev);

mac_learn:
	lan966x_mac_learn(lan966x, port->chip_port, addr, vid, ENTRYTYPE_LOCKED);

	return 0;
}

int lan966x_mac_del_entry(struct lan966x *lan966x, const unsigned char *addr,
			  u16 vid)
{
	struct lan966x_mac_entry *mac_entry, *tmp;

	spin_lock(&lan966x->mac_lock);
	list_for_each_entry_safe(mac_entry, tmp, &lan966x->mac_entries,
				 list) {
		if (mac_entry->vid == vid &&
		    ether_addr_equal(addr, mac_entry->mac)) {
			lan966x_mac_forget_locked(lan966x, mac_entry->mac,
						  mac_entry->vid,
						  ENTRYTYPE_LOCKED);

			list_del(&mac_entry->list);
			kfree(mac_entry);
		}
	}
	spin_unlock(&lan966x->mac_lock);

	return 0;
}

void lan966x_mac_lag_replace_port_entry(struct lan966x *lan966x,
					struct lan966x_port *src,
					struct lan966x_port *dst)
{
	struct lan966x_mac_entry *mac_entry;

	spin_lock(&lan966x->mac_lock);
	list_for_each_entry(mac_entry, &lan966x->mac_entries, list) {
		if (mac_entry->port_index == src->chip_port &&
		    mac_entry->lag) {
			lan966x_mac_forget_locked(lan966x, mac_entry->mac,
						  mac_entry->vid,
						  ENTRYTYPE_LOCKED);

			lan966x_mac_learn_locked(lan966x, dst->chip_port,
						 mac_entry->mac, mac_entry->vid,
						 ENTRYTYPE_LOCKED);
			mac_entry->port_index = dst->chip_port;
		}
	}
	spin_unlock(&lan966x->mac_lock);
}

void lan966x_mac_lag_remove_port_entry(struct lan966x *lan966x,
				       struct lan966x_port *src)
{
	struct lan966x_mac_entry *mac_entry, *tmp;

	spin_lock(&lan966x->mac_lock);
	list_for_each_entry_safe(mac_entry, tmp, &lan966x->mac_entries,
				 list) {
		if (mac_entry->port_index == src->chip_port &&
		    mac_entry->lag) {
			lan966x_mac_forget_locked(lan966x, mac_entry->mac,
						  mac_entry->vid,
						  ENTRYTYPE_LOCKED);

			list_del(&mac_entry->list);
			kfree(mac_entry);
		}
	}
	spin_unlock(&lan966x->mac_lock);
}

void lan966x_mac_purge_entries(struct lan966x *lan966x)
{
	struct lan966x_mac_entry *mac_entry, *tmp;

	spin_lock(&lan966x->mac_lock);
	list_for_each_entry_safe(mac_entry, tmp, &lan966x->mac_entries,
				 list) {
		lan966x_mac_forget_locked(lan966x, mac_entry->mac,
					  mac_entry->vid, ENTRYTYPE_LOCKED);

		list_del(&mac_entry->list);
		kfree(mac_entry);
	}
	spin_unlock(&lan966x->mac_lock);
}

static void lan966x_mac_notifiers(enum switchdev_notifier_type type,
				  unsigned char *mac, u32 vid,
				  struct net_device *dev)
{
	rtnl_lock();
	lan966x_fdb_call_notifiers(type, mac, vid, dev);
	rtnl_unlock();
}

static void lan966x_mac_process_raw_entry(struct lan966x_mac_raw_entry *raw_entry,
					  u8 *mac, u16 *vid, u32 *dest_idx)
{
	mac[0] = (raw_entry->mach >> 8)  & 0xff;
	mac[1] = (raw_entry->mach >> 0)  & 0xff;
	mac[2] = (raw_entry->macl >> 24) & 0xff;
	mac[3] = (raw_entry->macl >> 16) & 0xff;
	mac[4] = (raw_entry->macl >> 8)  & 0xff;
	mac[5] = (raw_entry->macl >> 0)  & 0xff;

	*vid = (raw_entry->mach >> 16) & 0xfff;
	*dest_idx = ANA_MACACCESS_DEST_IDX_GET(raw_entry->maca);
}

static void lan966x_mac_irq_process(struct lan966x *lan966x, u32 row,
				    struct lan966x_mac_raw_entry *raw_entries)
{
	struct lan966x_mac_entry *mac_entry, *tmp;
	unsigned char mac[ETH_ALEN] __aligned(2);
	struct list_head mac_deleted_entries;
	struct lan966x_port *port;
	u32 dest_idx;
	u32 column;
	u16 vid;

	INIT_LIST_HEAD(&mac_deleted_entries);

	spin_lock(&lan966x->mac_lock);
	list_for_each_entry_safe(mac_entry, tmp, &lan966x->mac_entries, list) {
		bool found = false;

		if (mac_entry->row != row)
			continue;

		for (column = 0; column < LAN966X_MAC_COLUMNS; ++column) {
			/* All the valid entries are at the start of the row,
			 * so when get one invalid entry it can just skip the
			 * rest of the columns
			 */
			if (!ANA_MACACCESS_VALID_GET(raw_entries[column].maca))
				break;

			lan966x_mac_process_raw_entry(&raw_entries[column],
						      mac, &vid, &dest_idx);
			if (WARN_ON(dest_idx >= lan966x->num_phys_ports))
				continue;

			/* If the entry in SW is found, then there is nothing
			 * to do
			 */
			if (mac_entry->vid == vid &&
			    ether_addr_equal(mac_entry->mac, mac) &&
			    mac_entry->port_index == dest_idx) {
				raw_entries[column].processed = true;
				found = true;
				break;
			}
		}

		if (!found) {
			list_del(&mac_entry->list);
			/* Move the entry from SW list to a tmp list such that
			 * it would be deleted later
			 */
			list_add_tail(&mac_entry->list, &mac_deleted_entries);
		}
	}
	spin_unlock(&lan966x->mac_lock);

	list_for_each_entry_safe(mac_entry, tmp, &mac_deleted_entries, list) {
		/* Notify the bridge that the entry doesn't exist
		 * anymore in the HW
		 */
		port = lan966x->ports[mac_entry->port_index];
		lan966x_mac_notifiers(SWITCHDEV_FDB_DEL_TO_BRIDGE,
				      mac_entry->mac, mac_entry->vid,
				      port->bond ?: port->dev);
		list_del(&mac_entry->list);
		kfree(mac_entry);
	}

	/* Now go to the list of columns and see if any entry was not in the SW
	 * list, then that means that the entry is new so it needs to notify the
	 * bridge.
	 */
	for (column = 0; column < LAN966X_MAC_COLUMNS; ++column) {
		/* All the valid entries are at the start of the row, so when
		 * get one invalid entry it can just skip the rest of the columns
		 */
		if (!ANA_MACACCESS_VALID_GET(raw_entries[column].maca))
			break;

		/* If the entry already exists then don't do anything */
		if (raw_entries[column].processed)
			continue;

		lan966x_mac_process_raw_entry(&raw_entries[column],
					      mac, &vid, &dest_idx);
		if (WARN_ON(dest_idx >= lan966x->num_phys_ports))
			continue;

		spin_lock(&lan966x->mac_lock);
		mac_entry = lan966x_mac_find_entry(lan966x, mac, vid, dest_idx);
		if (mac_entry) {
			spin_unlock(&lan966x->mac_lock);
			continue;
		}

		port = lan966x->ports[dest_idx];
		mac_entry = lan966x_mac_alloc_entry(port, mac, vid);
		if (!mac_entry) {
			spin_unlock(&lan966x->mac_lock);
			return;
		}

		mac_entry->row = row;
		list_add_tail(&mac_entry->list, &lan966x->mac_entries);
		spin_unlock(&lan966x->mac_lock);

		lan966x_mac_notifiers(SWITCHDEV_FDB_ADD_TO_BRIDGE,
				      mac, vid, port->bond ?: port->dev);
	}
}

irqreturn_t lan966x_mac_irq_handler(struct lan966x *lan966x)
{
	struct lan966x_mac_raw_entry entry[LAN966X_MAC_COLUMNS] = { 0 };
	u32 index, column;
	bool stop = true;
	u32 val;

	/* Start the scan from 0, 0 */
	lan_wr(ANA_MACTINDX_M_INDEX_SET(0) |
	       ANA_MACTINDX_BUCKET_SET(0),
	       lan966x, ANA_MACTINDX);

	while (1) {
		spin_lock(&lan966x->mac_lock);
		lan_rmw(ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_SYNC_GET_NEXT),
			ANA_MACACCESS_MAC_TABLE_CMD,
			lan966x, ANA_MACACCESS);
		lan966x_mac_wait_for_completion(lan966x);

		val = lan_rd(lan966x, ANA_MACTINDX);
		index = ANA_MACTINDX_M_INDEX_GET(val);
		column = ANA_MACTINDX_BUCKET_GET(val);

		/* The SYNC-GET-NEXT returns all the entries(4) in a row in
		 * which is suffered a change. By change it means that new entry
		 * was added or an entry was removed because of ageing.
		 * It would return all the columns for that row. And after that
		 * it would return the next row The stop conditions of the
		 * SYNC-GET-NEXT is when it reaches 'directly' to row 0
		 * column 3. So if SYNC-GET-NEXT returns row 0 and column 0
		 * then it is required to continue to read more even if it
		 * reaches row 0 and column 3.
		 */
		if (index == 0 && column == 0)
			stop = false;

		if (column == LAN966X_MAC_COLUMNS - 1 &&
		    index == 0 && stop) {
			spin_unlock(&lan966x->mac_lock);
			break;
		}

		entry[column].mach = lan_rd(lan966x, ANA_MACHDATA);
		entry[column].macl = lan_rd(lan966x, ANA_MACLDATA);
		entry[column].maca = lan_rd(lan966x, ANA_MACACCESS);
		spin_unlock(&lan966x->mac_lock);

		/* Once all the columns are read process them */
		if (column == LAN966X_MAC_COLUMNS - 1) {
			lan966x_mac_irq_process(lan966x, index, entry);
			/* A row was processed so it is safe to assume that the
			 * next row/column can be the stop condition
			 */
			stop = true;
		}
	}

	lan_rmw(ANA_ANAINTR_INTR_SET(0),
		ANA_ANAINTR_INTR,
		lan966x, ANA_ANAINTR);

	return IRQ_HANDLED;
}
