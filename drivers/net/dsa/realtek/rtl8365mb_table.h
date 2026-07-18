/* SPDX-License-Identifier: GPL-2.0 */
/* Look-up table query interface for the rtl8365mb switch family
 *
 * Copyright (C) 2022 Alvin Šipraga <alsi@bang-olufsen.dk>
 */

#ifndef _REALTEK_RTL8365MB_TABLE_H
#define _REALTEK_RTL8365MB_TABLE_H

#include <linux/if_ether.h>
#include <linux/types.h>

#include "realtek.h"

#define RTL8365MB_TABLE_ENTRY_MAX_SIZE			10

/*
 * enum rtl8365mb_table - available switch tables
 * @RTL8365MB_TABLE_ACL_RULE: ACL rules
 * @RTL8365MB_TABLE_ACL_ACTION: ACL actions
 * @RTL8365MB_TABLE_CVLAN: VLAN4k configurations
 * @RTL8365MB_TABLE_L2: filtering database (2K hash table)
 * @RTL8365MB_TABLE_IGMP_GROUP: IGMP group database (readonly)
 *
 * NOTE: Don't change the enum values. They must concur with the field
 * described by @RTL8365MB_TABLE_CTRL_TABLE_MASK.
 */
enum rtl8365mb_table {
	RTL8365MB_TABLE_ACL_RULE = 1,
	RTL8365MB_TABLE_ACL_ACTION = 2,
	RTL8365MB_TABLE_CVLAN = 3,
	RTL8365MB_TABLE_L2 = 4,
	RTL8365MB_TABLE_IGMP_GROUP = 5,
};

/*
 * enum rtl8365mb_table_op - table query operation
 * @RTL8365MB_TABLE_OP_READ: read an entry from the target table
 * @RTL8365MB_TABLE_OP_WRITE: write an entry to the target table
 *
 * NOTE: Don't change the enum values. They must concur with the field
 * described by @RTL8365MB_TABLE_CTRL_OP_MASK.
 */
enum rtl8365mb_table_op {
	RTL8365MB_TABLE_OP_READ = 0,
	RTL8365MB_TABLE_OP_WRITE = 1,
};

/*
 * enum rtl8365mb_table_l2_method - look-up method for read queries of L2 table
 * @RTL8365MB_TABLE_L2_METHOD_MAC: look-up by source MAC address and FID (or
 *   VID)
 * @RTL8365MB_TABLE_L2_METHOD_ADDR: look-up by entry address
 * @RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT: look-up next entry starting from the
 *   supplied address
 * @RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC: same as ADDR_NEXT but search only
 *   unicast addresses
 * @RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_MC: same as ADDR_NEXT but search only
 *   multicast addresses
 * @RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT: same as ADDR_NEXT_UC but
 *   search only entries with matching source port
 *
 * NOTE: Don't change the enum values. They must concur with the field
 * described by @RTL8365MB_TABLE_CTRL_METHOD_MASK
 */
enum rtl8365mb_table_l2_method {
	RTL8365MB_TABLE_L2_METHOD_MAC = 0,
	RTL8365MB_TABLE_L2_METHOD_ADDR = 1,
	RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT = 2,
	RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC = 3,
	RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_MC = 4,
	/*
	 * RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_MC_L3 = 5,
	 * RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_MC_L2L3 = 6,
	 */
	RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT = 7,
};

/*
 * rtl8365mb_table_query() - read from or write to a switch table
 * @priv: driver context
 * @table: target table, see &enum rtl8365mb_table
 * @op: read or write operation, see &enum rtl8365mb_table_op
 * @addr: table address. For indexed tables, this selects the entry to access.
 *        For L2 read queries, it is ignored as input for MAC-based lookup
 *        methods and used as input for address-based lookup methods. On
 *        successful L2 queries, it is updated with the matched entry address.
 * @method: L2 table lookup method, see &enum rtl8365mb_table_l2_method.
 *	    Ignored for non-L2 tables.
 * @port: for L2 read queries using method
 *        %RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT, restrict the search
 *        to entries associated with this source port. Ignored otherwise.
 * @data: data buffer used to read from or write to the table. For L2 MAC
 *        lookups, this buffer provides the lookup key and receives the
 *        matched entry contents on success.
 * @size: size of @data in 16-bit words. The caller must ensure that @size
 *        matches the target table's entry size and does not exceed
 *        RTL8365MB_TABLE_ENTRY_MAX_SIZE.
 *
 * This function provides unified access to the internal tables of the switch.
 * All tables except the L2 table are simple indexed tables, where @addr
 * selects the entry and @op determines whether the access is a read or a
 * write operation.
 *
 * The content of @data is used as input when writing to tables or when
 * specifying the lookup key for L2 MAC searches, and as output for all
 * successful read operations. It remains unchanged during write operations or
 * failed read operations that return %-ENOENT. For other errors during read
 * operations, it is undefined.
 *
 * The L2 table is a hash table and supports multiple lookup methods. For
 * %RTL8365MB_TABLE_L2_METHOD_MAC, an entry is searched based on the MAC
 * address and FID/VID fields provided in @data, using the same format as
 * an L2 table entry. Address-based methods either read a specific entry
 * (%RTL8365MB_TABLE_L2_METHOD_ADDR) or iterate over valid entries starting
 * from @addr (%RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT and variants). When using
 * %RTL8365MB_TABLE_L2_METHOD_ADDR_NEXT_UC_PORT, only entries associated with
 * the specified @port are considered.
 *
 * On successful L2 operations, @addr is updated with the matched table address
 * or allocated entry address. If no matching entry is found, or if an L2 write
 * operation fails (e.g., due to a full table during addition or a missing entry
 * during deletion), %-ENOENT is returned and @addr remains unchanged. It is the
 * caller's responsibility to map the returned error to the appropriate
 * semantic error.
 *
 * @size must match the size of the target table entry, expressed in 16-bit
 * words.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int rtl8365mb_table_query(struct realtek_priv *priv,
			  enum rtl8365mb_table table,
			  enum rtl8365mb_table_op op, u16 *addr,
			  enum rtl8365mb_table_l2_method method,
			  u16 port, u16 *data, size_t size);

#endif /* _REALTEK_RTL8365MB_TABLE_H */
