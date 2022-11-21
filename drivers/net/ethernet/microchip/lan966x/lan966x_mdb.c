// SPDX-License-Identifier: GPL-2.0+

#include <net/switchdev.h>

#include "lan966x_main.h"

struct lan966x_pgid_entry {
	struct list_head list;
	int index;
	refcount_t refcount;
	u16 ports;
};

struct lan966x_mdb_entry {
	struct list_head list;
	unsigned char mac[ETH_ALEN];
	u16 vid;
	u16 ports;
	struct lan966x_pgid_entry *pgid;
	u8 cpu_copy;
};

void lan966x_mdb_init(struct lan966x *lan966x)
{
	INIT_LIST_HEAD(&lan966x->mdb_entries);
	INIT_LIST_HEAD(&lan966x->pgid_entries);
}

static void lan966x_mdb_purge_mdb_entries(struct lan966x *lan966x)
{
	struct lan966x_mdb_entry *mdb_entry, *tmp;

	list_for_each_entry_safe(mdb_entry, tmp, &lan966x->mdb_entries, list) {
		list_del(&mdb_entry->list);
		kfree(mdb_entry);
	}
}

static void lan966x_mdb_purge_pgid_entries(struct lan966x *lan966x)
{
	struct lan966x_pgid_entry *pgid_entry, *tmp;

	list_for_each_entry_safe(pgid_entry, tmp, &lan966x->pgid_entries, list) {
		list_del(&pgid_entry->list);
		kfree(pgid_entry);
	}
}

void lan966x_mdb_deinit(struct lan966x *lan966x)
{
	lan966x_mdb_purge_mdb_entries(lan966x);
	lan966x_mdb_purge_pgid_entries(lan966x);
}

static struct lan966x_mdb_entry *
lan966x_mdb_entry_get(struct lan966x *lan966x,
		      const unsigned char *mac,
		      u16 vid)
{
	struct lan966x_mdb_entry *mdb_entry;

	list_for_each_entry(mdb_entry, &lan966x->mdb_entries, list) {
		if (ether_addr_equal(mdb_entry->mac, mac) &&
		    mdb_entry->vid == vid)
			return mdb_entry;
	}

	return NULL;
}

static struct lan966x_mdb_entry *
lan966x_mdb_entry_add(struct lan966x *lan966x,
		      const struct switchdev_obj_port_mdb *mdb)
{
	struct lan966x_mdb_entry *mdb_entry;

	mdb_entry = kzalloc(sizeof(*mdb_entry), GFP_KERNEL);
	if (!mdb_entry)
		return ERR_PTR(-ENOMEM);

	ether_addr_copy(mdb_entry->mac, mdb->addr);
	mdb_entry->vid = mdb->vid;

	list_add_tail(&mdb_entry->list, &lan966x->mdb_entries);

	return mdb_entry;
}

static void lan966x_mdb_encode_mac(unsigned char *mac,
				   struct lan966x_mdb_entry *mdb_entry,
				   enum macaccess_entry_type type)
{
	ether_addr_copy(mac, mdb_entry->mac);

	if (type == ENTRYTYPE_MACV4) {
		mac[0] = 0;
		mac[1] = mdb_entry->ports >> 8;
		mac[2] = mdb_entry->ports & 0xff;
	} else if (type == ENTRYTYPE_MACV6) {
		mac[0] = mdb_entry->ports >> 8;
		mac[1] = mdb_entry->ports & 0xff;
	}
}

static int lan966x_mdb_ip_add(struct lan966x_port *port,
			      const struct switchdev_obj_port_mdb *mdb,
			      enum macaccess_entry_type type)
{
	bool cpu_port = netif_is_bridge_master(mdb->obj.orig_dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_mdb_entry *mdb_entry;
	unsigned char mac[ETH_ALEN];
	bool cpu_copy = false;

	mdb_entry = lan966x_mdb_entry_get(lan966x, mdb->addr, mdb->vid);
	if (!mdb_entry) {
		mdb_entry = lan966x_mdb_entry_add(lan966x, mdb);
		if (IS_ERR(mdb_entry))
			return PTR_ERR(mdb_entry);
	} else {
		lan966x_mdb_encode_mac(mac, mdb_entry, type);
		lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	}

	if (cpu_port)
		mdb_entry->cpu_copy++;
	else
		mdb_entry->ports |= BIT(port->chip_port);

	/* Copy the frame to CPU only if the CPU is in the VLAN */
	if (lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x, mdb_entry->vid) &&
	    mdb_entry->cpu_copy)
		cpu_copy = true;

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	return lan966x_mac_ip_learn(lan966x, cpu_copy,
				    mac, mdb_entry->vid, type);
}

static int lan966x_mdb_ip_del(struct lan966x_port *port,
			      const struct switchdev_obj_port_mdb *mdb,
			      enum macaccess_entry_type type)
{
	bool cpu_port = netif_is_bridge_master(mdb->obj.orig_dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_mdb_entry *mdb_entry;
	unsigned char mac[ETH_ALEN];
	u16 ports;

	mdb_entry = lan966x_mdb_entry_get(lan966x, mdb->addr, mdb->vid);
	if (!mdb_entry)
		return -ENOENT;

	ports = mdb_entry->ports;
	if (cpu_port) {
		/* If there are still other references to the CPU port then
		 * there is no point to delete and add again the same entry
		 */
		mdb_entry->cpu_copy--;
		if (mdb_entry->cpu_copy)
			return 0;
	} else {
		ports &= ~BIT(port->chip_port);
	}

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);

	mdb_entry->ports = ports;

	if (!mdb_entry->ports && !mdb_entry->cpu_copy) {
		list_del(&mdb_entry->list);
		kfree(mdb_entry);
		return 0;
	}

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	return lan966x_mac_ip_learn(lan966x, mdb_entry->cpu_copy,
				    mac, mdb_entry->vid, type);
}

static struct lan966x_pgid_entry *
lan966x_pgid_entry_add(struct lan966x *lan966x, int index, u16 ports)
{
	struct lan966x_pgid_entry *pgid_entry;

	pgid_entry = kzalloc(sizeof(*pgid_entry), GFP_KERNEL);
	if (!pgid_entry)
		return ERR_PTR(-ENOMEM);

	pgid_entry->ports = ports;
	pgid_entry->index = index;
	refcount_set(&pgid_entry->refcount, 1);

	list_add_tail(&pgid_entry->list, &lan966x->pgid_entries);

	return pgid_entry;
}

static struct lan966x_pgid_entry *
lan966x_pgid_entry_get(struct lan966x *lan966x,
		       struct lan966x_mdb_entry *mdb_entry)
{
	struct lan966x_pgid_entry *pgid_entry;
	int index;

	/* Try to find an existing pgid that uses the same ports as the
	 * mdb_entry
	 */
	list_for_each_entry(pgid_entry, &lan966x->pgid_entries, list) {
		if (pgid_entry->ports == mdb_entry->ports) {
			refcount_inc(&pgid_entry->refcount);
			return pgid_entry;
		}
	}

	/* Try to find an empty pgid entry and allocate one in case it finds it,
	 * otherwise it means that there are no more resources
	 */
	for (index = PGID_GP_START; index < PGID_GP_END; index++) {
		bool used = false;

		list_for_each_entry(pgid_entry, &lan966x->pgid_entries, list) {
			if (pgid_entry->index == index) {
				used = true;
				break;
			}
		}

		if (!used)
			return lan966x_pgid_entry_add(lan966x, index,
						      mdb_entry->ports);
	}

	return ERR_PTR(-ENOSPC);
}

static void lan966x_pgid_entry_del(struct lan966x *lan966x,
				   struct lan966x_pgid_entry *pgid_entry)
{
	if (!refcount_dec_and_test(&pgid_entry->refcount))
		return;

	list_del(&pgid_entry->list);
	kfree(pgid_entry);
}

static int lan966x_mdb_l2_add(struct lan966x_port *port,
			      const struct switchdev_obj_port_mdb *mdb,
			      enum macaccess_entry_type type)
{
	bool cpu_port = netif_is_bridge_master(mdb->obj.orig_dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_pgid_entry *pgid_entry;
	struct lan966x_mdb_entry *mdb_entry;
	unsigned char mac[ETH_ALEN];

	mdb_entry = lan966x_mdb_entry_get(lan966x, mdb->addr, mdb->vid);
	if (!mdb_entry) {
		mdb_entry = lan966x_mdb_entry_add(lan966x, mdb);
		if (IS_ERR(mdb_entry))
			return PTR_ERR(mdb_entry);
	} else {
		lan966x_pgid_entry_del(lan966x, mdb_entry->pgid);
		lan966x_mdb_encode_mac(mac, mdb_entry, type);
		lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	}

	if (cpu_port) {
		mdb_entry->ports |= BIT(CPU_PORT);
		mdb_entry->cpu_copy++;
	} else {
		mdb_entry->ports |= BIT(port->chip_port);
	}

	pgid_entry = lan966x_pgid_entry_get(lan966x, mdb_entry);
	if (IS_ERR(pgid_entry)) {
		list_del(&mdb_entry->list);
		kfree(mdb_entry);
		return PTR_ERR(pgid_entry);
	}
	mdb_entry->pgid = pgid_entry;

	/* Copy the frame to CPU only if the CPU is in the VLAN */
	if (!lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x, mdb_entry->vid) &&
	    mdb_entry->cpu_copy)
		mdb_entry->ports &= BIT(CPU_PORT);

	lan_rmw(ANA_PGID_PGID_SET(mdb_entry->ports),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(pgid_entry->index));

	return lan966x_mac_learn(lan966x, pgid_entry->index, mdb_entry->mac,
				 mdb_entry->vid, type);
}

static int lan966x_mdb_l2_del(struct lan966x_port *port,
			      const struct switchdev_obj_port_mdb *mdb,
			      enum macaccess_entry_type type)
{
	bool cpu_port = netif_is_bridge_master(mdb->obj.orig_dev);
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_pgid_entry *pgid_entry;
	struct lan966x_mdb_entry *mdb_entry;
	unsigned char mac[ETH_ALEN];
	u16 ports;

	mdb_entry = lan966x_mdb_entry_get(lan966x, mdb->addr, mdb->vid);
	if (!mdb_entry)
		return -ENOENT;

	ports = mdb_entry->ports;
	if (cpu_port) {
		/* If there are still other references to the CPU port then
		 * there is no point to delete and add again the same entry
		 */
		mdb_entry->cpu_copy--;
		if (mdb_entry->cpu_copy)
			return 0;

		ports &= ~BIT(CPU_PORT);
	} else {
		ports &= ~BIT(port->chip_port);
	}

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	lan966x_pgid_entry_del(lan966x, mdb_entry->pgid);

	mdb_entry->ports = ports;

	if (!mdb_entry->ports) {
		list_del(&mdb_entry->list);
		kfree(mdb_entry);
		return 0;
	}

	pgid_entry = lan966x_pgid_entry_get(lan966x, mdb_entry);
	if (IS_ERR(pgid_entry)) {
		list_del(&mdb_entry->list);
		kfree(mdb_entry);
		return PTR_ERR(pgid_entry);
	}
	mdb_entry->pgid = pgid_entry;

	lan_rmw(ANA_PGID_PGID_SET(mdb_entry->ports),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(pgid_entry->index));

	return lan966x_mac_learn(lan966x, pgid_entry->index, mdb_entry->mac,
				 mdb_entry->vid, type);
}

static enum macaccess_entry_type
lan966x_mdb_classify(const unsigned char *mac)
{
	if (mac[0] == 0x01 && mac[1] == 0x00 && mac[2] == 0x5e)
		return ENTRYTYPE_MACV4;
	if (mac[0] == 0x33 && mac[1] == 0x33)
		return ENTRYTYPE_MACV6;
	return ENTRYTYPE_LOCKED;
}

int lan966x_handle_port_mdb_add(struct lan966x_port *port,
				const struct switchdev_obj *obj)
{
	const struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	enum macaccess_entry_type type;

	/* Split the way the entries are added for ipv4/ipv6 and for l2. The
	 * reason is that for ipv4/ipv6 it doesn't require to use any pgid
	 * entry, while for l2 is required to use pgid entries
	 */
	type = lan966x_mdb_classify(mdb->addr);
	if (type == ENTRYTYPE_MACV4 || type == ENTRYTYPE_MACV6)
		return lan966x_mdb_ip_add(port, mdb, type);

	return lan966x_mdb_l2_add(port, mdb, type);
}

int lan966x_handle_port_mdb_del(struct lan966x_port *port,
				const struct switchdev_obj *obj)
{
	const struct switchdev_obj_port_mdb *mdb = SWITCHDEV_OBJ_PORT_MDB(obj);
	enum macaccess_entry_type type;

	/* Split the way the entries are removed for ipv4/ipv6 and for l2. The
	 * reason is that for ipv4/ipv6 it doesn't require to use any pgid
	 * entry, while for l2 is required to use pgid entries
	 */
	type = lan966x_mdb_classify(mdb->addr);
	if (type == ENTRYTYPE_MACV4 || type == ENTRYTYPE_MACV6)
		return lan966x_mdb_ip_del(port, mdb, type);

	return lan966x_mdb_l2_del(port, mdb, type);
}

static void lan966x_mdb_ip_cpu_copy(struct lan966x *lan966x,
				    struct lan966x_mdb_entry *mdb_entry,
				    enum macaccess_entry_type type)
{
	unsigned char mac[ETH_ALEN];

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	lan966x_mac_ip_learn(lan966x, true, mac, mdb_entry->vid, type);
}

static void lan966x_mdb_l2_cpu_copy(struct lan966x *lan966x,
				    struct lan966x_mdb_entry *mdb_entry,
				    enum macaccess_entry_type type)
{
	struct lan966x_pgid_entry *pgid_entry;
	unsigned char mac[ETH_ALEN];

	lan966x_pgid_entry_del(lan966x, mdb_entry->pgid);
	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);

	mdb_entry->ports |= BIT(CPU_PORT);

	pgid_entry = lan966x_pgid_entry_get(lan966x, mdb_entry);
	if (IS_ERR(pgid_entry))
		return;

	mdb_entry->pgid = pgid_entry;

	lan_rmw(ANA_PGID_PGID_SET(mdb_entry->ports),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(pgid_entry->index));

	lan966x_mac_learn(lan966x, pgid_entry->index, mdb_entry->mac,
			  mdb_entry->vid, type);
}

void lan966x_mdb_write_entries(struct lan966x *lan966x, u16 vid)
{
	struct lan966x_mdb_entry *mdb_entry;
	enum macaccess_entry_type type;

	list_for_each_entry(mdb_entry, &lan966x->mdb_entries, list) {
		if (mdb_entry->vid != vid || !mdb_entry->cpu_copy)
			continue;

		type = lan966x_mdb_classify(mdb_entry->mac);
		if (type == ENTRYTYPE_MACV4 || type == ENTRYTYPE_MACV6)
			lan966x_mdb_ip_cpu_copy(lan966x, mdb_entry, type);
		else
			lan966x_mdb_l2_cpu_copy(lan966x, mdb_entry, type);
	}
}

static void lan966x_mdb_ip_cpu_remove(struct lan966x *lan966x,
				      struct lan966x_mdb_entry *mdb_entry,
				      enum macaccess_entry_type type)
{
	unsigned char mac[ETH_ALEN];

	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	lan966x_mac_ip_learn(lan966x, false, mac, mdb_entry->vid, type);
}

static void lan966x_mdb_l2_cpu_remove(struct lan966x *lan966x,
				      struct lan966x_mdb_entry *mdb_entry,
				      enum macaccess_entry_type type)
{
	struct lan966x_pgid_entry *pgid_entry;
	unsigned char mac[ETH_ALEN];

	lan966x_pgid_entry_del(lan966x, mdb_entry->pgid);
	lan966x_mdb_encode_mac(mac, mdb_entry, type);
	lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);

	mdb_entry->ports &= ~BIT(CPU_PORT);

	pgid_entry = lan966x_pgid_entry_get(lan966x, mdb_entry);
	if (IS_ERR(pgid_entry))
		return;

	mdb_entry->pgid = pgid_entry;

	lan_rmw(ANA_PGID_PGID_SET(mdb_entry->ports),
		ANA_PGID_PGID,
		lan966x, ANA_PGID(pgid_entry->index));

	lan966x_mac_learn(lan966x, pgid_entry->index, mdb_entry->mac,
			  mdb_entry->vid, type);
}

void lan966x_mdb_erase_entries(struct lan966x *lan966x, u16 vid)
{
	struct lan966x_mdb_entry *mdb_entry;
	enum macaccess_entry_type type;

	list_for_each_entry(mdb_entry, &lan966x->mdb_entries, list) {
		if (mdb_entry->vid != vid || !mdb_entry->cpu_copy)
			continue;

		type = lan966x_mdb_classify(mdb_entry->mac);
		if (type == ENTRYTYPE_MACV4 || type == ENTRYTYPE_MACV6)
			lan966x_mdb_ip_cpu_remove(lan966x, mdb_entry, type);
		else
			lan966x_mdb_l2_cpu_remove(lan966x, mdb_entry, type);
	}
}

void lan966x_mdb_clear_entries(struct lan966x *lan966x)
{
	struct lan966x_mdb_entry *mdb_entry;
	enum macaccess_entry_type type;
	unsigned char mac[ETH_ALEN];

	list_for_each_entry(mdb_entry, &lan966x->mdb_entries, list) {
		type = lan966x_mdb_classify(mdb_entry->mac);

		lan966x_mdb_encode_mac(mac, mdb_entry, type);
		/* Remove just the MAC entry, still keep the PGID in case of L2
		 * entries because this can be restored at later point
		 */
		lan966x_mac_forget(lan966x, mac, mdb_entry->vid, type);
	}
}

void lan966x_mdb_restore_entries(struct lan966x *lan966x)
{
	struct lan966x_mdb_entry *mdb_entry;
	enum macaccess_entry_type type;
	unsigned char mac[ETH_ALEN];
	bool cpu_copy = false;

	list_for_each_entry(mdb_entry, &lan966x->mdb_entries, list) {
		type = lan966x_mdb_classify(mdb_entry->mac);

		lan966x_mdb_encode_mac(mac, mdb_entry, type);
		if (type == ENTRYTYPE_MACV4 || type == ENTRYTYPE_MACV6) {
			/* Copy the frame to CPU only if the CPU is in the VLAN */
			if (lan966x_vlan_cpu_member_cpu_vlan_mask(lan966x,
								  mdb_entry->vid) &&
			    mdb_entry->cpu_copy)
				cpu_copy = true;

			lan966x_mac_ip_learn(lan966x, cpu_copy, mac,
					     mdb_entry->vid, type);
		} else {
			lan966x_mac_learn(lan966x, mdb_entry->pgid->index,
					  mdb_entry->mac,
					  mdb_entry->vid, type);
		}
	}
}
