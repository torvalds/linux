// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/types.h>

#include "gsi.h"
#include "gsi_trans.h"
#include "ipa.h"
#include "ipa_cmd.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"
#include "ipa_reg.h"
#include "ipa_table.h"
#include "ipa_version.h"

/**
 * DOC: IPA Filter and Route Tables
 *
 * The IPA has tables defined in its local (IPA-resident) memory that define
 * filter and routing rules.  An entry in either of these tables is a little
 * endian 64-bit "slot" that holds the address of a rule definition.  (The
 * size of these slots is 64 bits regardless of the host DMA address size.)
 *
 * Separate tables (both filter and route) are used for IPv4 and IPv6.  There
 * is normally another set of "hashed" filter and route tables, which are
 * used with a hash of message metadata.  Hashed operation is not supported
 * by all IPA hardware (IPA v4.2 doesn't support hashed tables).
 *
 * Rules can be in local memory or in DRAM (system memory).  The offset of
 * an object (such as a route or filter table) in IPA-resident memory must
 * 128-byte aligned.  An object in system memory (such as a route or filter
 * rule) must be at an 8-byte aligned address.  We currently only place
 * route or filter rules in system memory.
 *
 * A rule consists of a contiguous block of 32-bit values terminated with
 * 32 zero bits.  A special "zero entry" rule consisting of 64 zero bits
 * represents "no filtering" or "no routing," and is the reset value for
 * filter or route table rules.
 *
 * Each filter rule is associated with an AP or modem TX endpoint, though
 * not all TX endpoints support filtering.  The first 64-bit slot in a
 * filter table is a bitmap indicating which endpoints have entries in
 * the table.  Each set bit in this bitmap indicates the presence of the
 * address of a filter rule in the memory following the bitmap.  Until IPA
 * v5.0,  the low-order bit (bit 0) in this bitmap represents a special
 * global filter, which applies to all traffic.  Otherwise the position of
 * each set bit represents an endpoint for which a filter rule is defined.
 *
 * The global rule is not used in current code, and support for it is
 * removed starting at IPA v5.0.  For IPA v5.0+, the endpoint bitmap
 * position defines the endpoint ID--i.e. if bit 1 is set in the endpoint
 * bitmap, endpoint 1 has a filter rule.  Older versions of IPA represent
 * the presence of a filter rule for endpoint X by bit (X + 1) being set.
 * I.e., bit 1 set indicates the presence of a filter rule for endpoint 0,
 * and bit 3 set means there is a filter rule present for endpoint 2.
 *
 * Each filter table entry has the address of a set of equations that
 * implement a filter rule.  So following the endpoint bitmap there
 * will be such an address/entry for each endpoint with a set bit in
 * the bitmap.
 *
 * The AP initializes all entries in a filter table to refer to a "zero"
 * rule.  Once initialized, the modem and AP update the entries for
 * endpoints they "own" directly.  Currently the AP does not use the IPA
 * filtering functionality.
 *
 * This diagram shows an example of a filter table with an endpoint
 * bitmap as defined prior to IPA v5.0.
 *
 *                    IPA Filter Table
 *                 ----------------------
 * endpoint bitmap | 0x0000000000000048 | Bits 3 and 6 set (endpoints 2 and 5)
 *                 |--------------------|
 * 1st endpoint    | 0x000123456789abc0 | DMA address for modem endpoint 2 rule
 *                 |--------------------|
 * 2nd endpoint    | 0x000123456789abf0 | DMA address for AP endpoint 5 rule
 *                 |--------------------|
 * (unused)        |                    | (Unused space in filter table)
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * (unused)        |                    | (Unused space in filter table)
 *                 ----------------------
 *
 * The set of available route rules is divided about equally between the AP
 * and modem.  The AP initializes all entries in a route table to refer to
 * a "zero entry".  Once initialized, the modem and AP are responsible for
 * updating their own entries.  All entries in a route table are usable,
 * though the AP currently does not use the IPA routing functionality.
 *
 *                    IPA Route Table
 *                 ----------------------
 * 1st modem route | 0x0001234500001100 | DMA address for first route rule
 *                 |--------------------|
 * 2nd modem route | 0x0001234500001140 | DMA address for second route rule
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * Last modem route| 0x0001234500002280 | DMA address for Nth route rule
 *                 |--------------------|
 * 1st AP route    | 0x0001234500001100 | DMA address for route rule (N+1)
 *                 |--------------------|
 * 2nd AP route    | 0x0001234500001140 | DMA address for next route rule
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * Last AP route   | 0x0001234500002280 | DMA address for last route rule
 *                 ----------------------
 */

/* Filter or route rules consist of a set of 32-bit values followed by a
 * 32-bit all-zero rule list terminator.  The "zero rule" is simply an
 * all-zero rule followed by the list terminator.
 */
#define IPA_ZERO_RULE_SIZE		(2 * sizeof(__le32))

/* Check things that can be validated at build time. */
static void ipa_table_validate_build(void)
{
	/* Filter and route tables contain DMA addresses that refer
	 * to filter or route rules.  But the size of a table entry
	 * is 64 bits regardless of what the size of an AP DMA address
	 * is.  A fixed constant defines the size of an entry, and
	 * code in ipa_table_init() uses a pointer to __le64 to
	 * initialize tables.
	 */
	BUILD_BUG_ON(sizeof(dma_addr_t) > sizeof(__le64));

	/* A "zero rule" is used to represent no filtering or no routing.
	 * It is a 64-bit block of zeroed memory.  Code in ipa_table_init()
	 * assumes that it can be written using a pointer to __le64.
	 */
	BUILD_BUG_ON(IPA_ZERO_RULE_SIZE != sizeof(__le64));
}

static const struct ipa_mem *
ipa_table_mem(struct ipa *ipa, bool filter, bool hashed, bool ipv6)
{
	enum ipa_mem_id mem_id;

	mem_id = filter ? hashed ? ipv6 ? IPA_MEM_V6_FILTER_HASHED
					: IPA_MEM_V4_FILTER_HASHED
				 : ipv6 ? IPA_MEM_V6_FILTER
					: IPA_MEM_V4_FILTER
			: hashed ? ipv6 ? IPA_MEM_V6_ROUTE_HASHED
					: IPA_MEM_V4_ROUTE_HASHED
				 : ipv6 ? IPA_MEM_V6_ROUTE
					: IPA_MEM_V4_ROUTE;

	return ipa_mem_find(ipa, mem_id);
}

/* Return true if hashed tables are supported */
bool ipa_table_hash_support(struct ipa *ipa)
{
	return ipa->version != IPA_VERSION_4_2;
}

bool ipa_filtered_valid(struct ipa *ipa, u64 filtered)
{
	struct device *dev = ipa->dev;
	u32 count;

	if (!filtered) {
		dev_err(dev, "at least one filtering endpoint is required\n");

		return false;
	}

	count = hweight64(filtered);
	if (count > ipa->filter_count) {
		dev_err(dev, "too many filtering endpoints (%u > %u)\n",
			count, ipa->filter_count);

		return false;
	}

	return true;
}

/* Zero entry count means no table, so just return a 0 address */
static dma_addr_t ipa_table_addr(struct ipa *ipa, bool filter_mask, u16 count)
{
	u32 skip;

	if (!count)
		return 0;

	WARN_ON(count > max_t(u32, ipa->filter_count, ipa->route_count));

	/* Skip over the zero rule and possibly the filter mask */
	skip = filter_mask ? 1 : 2;

	return ipa->table_addr + skip * sizeof(*ipa->table_virt);
}

static void ipa_table_reset_add(struct gsi_trans *trans, bool filter,
				bool hashed, bool ipv6, u16 first, u16 count)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	const struct ipa_mem *mem;
	dma_addr_t addr;
	u32 offset;
	u16 size;

	/* Nothing to do if the memory region is doesn't exist or is empty */
	mem = ipa_table_mem(ipa, filter, hashed, ipv6);
	if (!mem || !mem->size)
		return;

	if (filter)
		first++;	/* skip over bitmap */

	offset = mem->offset + first * sizeof(__le64);
	size = count * sizeof(__le64);
	addr = ipa_table_addr(ipa, false, count);

	ipa_cmd_dma_shared_mem_add(trans, offset, size, addr, true);
}

/* Reset entries in a single filter table belonging to either the AP or
 * modem to refer to the zero entry.  The memory region supplied will be
 * for the IPv4 and IPv6 non-hashed and hashed filter tables.
 */
static int
ipa_filter_reset_table(struct ipa *ipa, bool hashed, bool ipv6, bool modem)
{
	u64 ep_mask = ipa->filtered;
	struct gsi_trans *trans;
	enum gsi_ee_id ee_id;

	trans = ipa_cmd_trans_alloc(ipa, hweight64(ep_mask));
	if (!trans) {
		dev_err(ipa->dev, "no transaction for %s filter reset\n",
			modem ? "modem" : "AP");
		return -EBUSY;
	}

	ee_id = modem ? GSI_EE_MODEM : GSI_EE_AP;
	while (ep_mask) {
		u32 endpoint_id = __ffs(ep_mask);
		struct ipa_endpoint *endpoint;

		ep_mask ^= BIT(endpoint_id);

		endpoint = &ipa->endpoint[endpoint_id];
		if (endpoint->ee_id != ee_id)
			continue;

		ipa_table_reset_add(trans, true, hashed, ipv6, endpoint_id, 1);
	}

	gsi_trans_commit_wait(trans);

	return 0;
}

/* Theoretically, each filter table could have more filter slots to
 * update than the maximum number of commands in a transaction.  So
 * we do each table separately.
 */
static int ipa_filter_reset(struct ipa *ipa, bool modem)
{
	int ret;

	ret = ipa_filter_reset_table(ipa, false, false, modem);
	if (ret)
		return ret;

	ret = ipa_filter_reset_table(ipa, false, true, modem);
	if (ret || !ipa_table_hash_support(ipa))
		return ret;

	ret = ipa_filter_reset_table(ipa, true, false, modem);
	if (ret)
		return ret;

	return ipa_filter_reset_table(ipa, true, true, modem);
}

/* The AP routes and modem routes are each contiguous within the
 * table.  We can update each table with a single command, and we
 * won't exceed the per-transaction command limit.
 * */
static int ipa_route_reset(struct ipa *ipa, bool modem)
{
	bool hash_support = ipa_table_hash_support(ipa);
	u32 modem_route_count = ipa->modem_route_count;
	struct gsi_trans *trans;
	u16 first;
	u16 count;

	trans = ipa_cmd_trans_alloc(ipa, hash_support ? 4 : 2);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for %s route reset\n",
			modem ? "modem" : "AP");
		return -EBUSY;
	}

	if (modem) {
		first = 0;
		count = modem_route_count;
	} else {
		first = modem_route_count;
		count = ipa->route_count - modem_route_count;
	}

	ipa_table_reset_add(trans, false, false, false, first, count);
	ipa_table_reset_add(trans, false, false, true, first, count);

	if (hash_support) {
		ipa_table_reset_add(trans, false, true, false, first, count);
		ipa_table_reset_add(trans, false, true, true, first, count);
	}

	gsi_trans_commit_wait(trans);

	return 0;
}

void ipa_table_reset(struct ipa *ipa, bool modem)
{
	struct device *dev = ipa->dev;
	const char *ee_name;
	int ret;

	ee_name = modem ? "modem" : "AP";

	/* Report errors, but reset filter and route tables */
	ret = ipa_filter_reset(ipa, modem);
	if (ret)
		dev_err(dev, "error %d resetting filter table for %s\n",
				ret, ee_name);

	ret = ipa_route_reset(ipa, modem);
	if (ret)
		dev_err(dev, "error %d resetting route table for %s\n",
				ret, ee_name);
}

int ipa_table_hash_flush(struct ipa *ipa)
{
	struct gsi_trans *trans;
	const struct reg *reg;
	u32 val;

	if (!ipa_table_hash_support(ipa))
		return 0;

	trans = ipa_cmd_trans_alloc(ipa, 1);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for hash flush\n");
		return -EBUSY;
	}

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, FILT_ROUT_HASH_FLUSH);

		val = reg_bit(reg, IPV6_ROUTER_HASH);
		val |= reg_bit(reg, IPV6_FILTER_HASH);
		val |= reg_bit(reg, IPV4_ROUTER_HASH);
		val |= reg_bit(reg, IPV4_FILTER_HASH);
	} else {
		reg = ipa_reg(ipa, FILT_ROUT_CACHE_FLUSH);

		/* IPA v5.0+ uses a unified cache (both IPv4 and IPv6) */
		val = reg_bit(reg, ROUTER_CACHE);
		val |= reg_bit(reg, FILTER_CACHE);
	}

	ipa_cmd_register_write_add(trans, reg_offset(reg), val, val, false);

	gsi_trans_commit_wait(trans);

	return 0;
}

static void ipa_table_init_add(struct gsi_trans *trans, bool filter, bool ipv6)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	const struct ipa_mem *hash_mem;
	enum ipa_cmd_opcode opcode;
	const struct ipa_mem *mem;
	dma_addr_t hash_addr;
	dma_addr_t addr;
	u32 hash_offset;
	u32 zero_offset;
	u16 hash_count;
	u32 zero_size;
	u16 hash_size;
	u16 count;
	u16 size;

	opcode = filter ? ipv6 ? IPA_CMD_IP_V6_FILTER_INIT
			       : IPA_CMD_IP_V4_FILTER_INIT
			: ipv6 ? IPA_CMD_IP_V6_ROUTING_INIT
			       : IPA_CMD_IP_V4_ROUTING_INIT;

	/* The non-hashed region will exist (see ipa_table_mem_valid()) */
	mem = ipa_table_mem(ipa, filter, false, ipv6);
	hash_mem = ipa_table_mem(ipa, filter, true, ipv6);
	hash_offset = hash_mem ? hash_mem->offset : 0;

	/* Compute the number of table entries to initialize */
	if (filter) {
		/* The number of filtering endpoints determines number of
		 * entries in the filter table; we also add one more "slot"
		 * to hold the bitmap itself.  The size of the hashed filter
		 * table is either the same as the non-hashed one, or zero.
		 */
		count = 1 + hweight64(ipa->filtered);
		hash_count = hash_mem && hash_mem->size ? count : 0;
	} else {
		/* The size of a route table region determines the number
		 * of entries it has.
		 */
		count = mem->size / sizeof(__le64);
		hash_count = hash_mem ? hash_mem->size / sizeof(__le64) : 0;
	}
	size = count * sizeof(__le64);
	hash_size = hash_count * sizeof(__le64);

	addr = ipa_table_addr(ipa, filter, count);
	hash_addr = ipa_table_addr(ipa, filter, hash_count);

	ipa_cmd_table_init_add(trans, opcode, size, mem->offset, addr,
			       hash_size, hash_offset, hash_addr);
	if (!filter)
		return;

	/* Zero the unused space in the filter table */
	zero_offset = mem->offset + size;
	zero_size = mem->size - size;
	ipa_cmd_dma_shared_mem_add(trans, zero_offset, zero_size,
				   ipa->zero_addr, true);
	if (!hash_size)
		return;

	/* Zero the unused space in the hashed filter table */
	zero_offset = hash_offset + hash_size;
	zero_size = hash_mem->size - hash_size;
	ipa_cmd_dma_shared_mem_add(trans, zero_offset, zero_size,
				   ipa->zero_addr, true);
}

int ipa_table_setup(struct ipa *ipa)
{
	struct gsi_trans *trans;

	/* We will need at most 8 TREs:
	 * - IPv4:
	 *     - One for route table initialization (non-hashed and hashed)
	 *     - One for filter table initialization (non-hashed and hashed)
	 *     - One to zero unused entries in the non-hashed filter table
	 *     - One to zero unused entries in the hashed filter table
	 * - IPv6:
	 *     - One for route table initialization (non-hashed and hashed)
	 *     - One for filter table initialization (non-hashed and hashed)
	 *     - One to zero unused entries in the non-hashed filter table
	 *     - One to zero unused entries in the hashed filter table
	 * All platforms support at least 8 TREs in a transaction.
	 */
	trans = ipa_cmd_trans_alloc(ipa, 8);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for table setup\n");
		return -EBUSY;
	}

	ipa_table_init_add(trans, false, false);
	ipa_table_init_add(trans, false, true);
	ipa_table_init_add(trans, true, false);
	ipa_table_init_add(trans, true, true);

	gsi_trans_commit_wait(trans);

	return 0;
}

/**
 * ipa_filter_tuple_zero() - Zero an endpoint's hashed filter tuple
 * @endpoint:	Endpoint whose filter hash tuple should be zeroed
 *
 * Endpoint must be for the AP (not modem) and support filtering. Updates
 * the filter hash values without changing route ones.
 */
static void ipa_filter_tuple_zero(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct reg *reg;
	u32 offset;
	u32 val;

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, ENDP_FILTER_ROUTER_HSH_CFG);

		offset = reg_n_offset(reg, endpoint_id);
		val = ioread32(endpoint->ipa->reg_virt + offset);

		/* Zero all filter-related fields, preserving the rest */
		val &= ~reg_fmask(reg, FILTER_HASH_MSK_ALL);
	} else {
		/* IPA v5.0 separates filter and router cache configuration */
		reg = ipa_reg(ipa, ENDP_FILTER_CACHE_CFG);
		offset = reg_n_offset(reg, endpoint_id);

		/* Zero all filter-related fields */
		val = 0;
	}

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/* Configure a hashed filter table; there is no ipa_filter_deconfig() */
static void ipa_filter_config(struct ipa *ipa, bool modem)
{
	enum gsi_ee_id ee_id = modem ? GSI_EE_MODEM : GSI_EE_AP;
	u64 ep_mask = ipa->filtered;

	if (!ipa_table_hash_support(ipa))
		return;

	while (ep_mask) {
		u32 endpoint_id = __ffs(ep_mask);
		struct ipa_endpoint *endpoint;

		ep_mask ^= BIT(endpoint_id);

		endpoint = &ipa->endpoint[endpoint_id];
		if (endpoint->ee_id == ee_id)
			ipa_filter_tuple_zero(endpoint);
	}
}

static bool ipa_route_id_modem(struct ipa *ipa, u32 route_id)
{
	return route_id < ipa->modem_route_count;
}

/**
 * ipa_route_tuple_zero() - Zero a hashed route table entry tuple
 * @ipa:	IPA pointer
 * @route_id:	Route table entry whose hash tuple should be zeroed
 *
 * Updates the route hash values without changing filter ones.
 */
static void ipa_route_tuple_zero(struct ipa *ipa, u32 route_id)
{
	const struct reg *reg;
	u32 offset;
	u32 val;

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, ENDP_FILTER_ROUTER_HSH_CFG);
		offset = reg_n_offset(reg, route_id);

		val = ioread32(ipa->reg_virt + offset);

		/* Zero all route-related fields, preserving the rest */
		val &= ~reg_fmask(reg, ROUTER_HASH_MSK_ALL);
	} else {
		/* IPA v5.0 separates filter and router cache configuration */
		reg = ipa_reg(ipa, ENDP_ROUTER_CACHE_CFG);
		offset = reg_n_offset(reg, route_id);

		/* Zero all route-related fields */
		val = 0;
	}

	iowrite32(val, ipa->reg_virt + offset);
}

/* Configure a hashed route table; there is no ipa_route_deconfig() */
static void ipa_route_config(struct ipa *ipa, bool modem)
{
	u32 route_id;

	if (!ipa_table_hash_support(ipa))
		return;

	for (route_id = 0; route_id < ipa->route_count; route_id++)
		if (ipa_route_id_modem(ipa, route_id) == modem)
			ipa_route_tuple_zero(ipa, route_id);
}

/* Configure a filter and route tables; there is no ipa_table_deconfig() */
void ipa_table_config(struct ipa *ipa)
{
	ipa_filter_config(ipa, false);
	ipa_filter_config(ipa, true);
	ipa_route_config(ipa, false);
	ipa_route_config(ipa, true);
}

/* Verify the sizes of all IPA table filter or routing table memory regions
 * are valid.  If valid, this records the size of the routing table.
 */
bool ipa_table_mem_valid(struct ipa *ipa, bool filter)
{
	bool hash_support = ipa_table_hash_support(ipa);
	const struct ipa_mem *mem_hashed;
	const struct ipa_mem *mem_ipv4;
	const struct ipa_mem *mem_ipv6;
	u32 count;

	/* IPv4 and IPv6 non-hashed tables are expected to be defined and
	 * have the same size.  Both must have at least two entries (and
	 * would normally have more than that).
	 */
	mem_ipv4 = ipa_table_mem(ipa, filter, false, false);
	if (!mem_ipv4)
		return false;

	mem_ipv6 = ipa_table_mem(ipa, filter, false, true);
	if (!mem_ipv6)
		return false;

	if (mem_ipv4->size != mem_ipv6->size)
		return false;

	/* Compute and record the number of entries for each table type */
	count = mem_ipv4->size / sizeof(__le64);
	if (count < 2)
		return false;
	if (filter)
		ipa->filter_count = count - 1;	/* Filter map in first entry */
	else
		ipa->route_count = count;

	/* Table offset and size must fit in TABLE_INIT command fields */
	if (!ipa_cmd_table_init_valid(ipa, mem_ipv4, !filter))
		return false;

	/* Make sure the regions are big enough */
	if (filter) {
		/* Filter tables must able to hold the endpoint bitmap plus
		 * an entry for each endpoint that supports filtering
		 */
		if (count < 1 + hweight64(ipa->filtered))
			return false;
	} else {
		/* Routing tables must be able to hold all modem entries,
		 * plus at least one entry for the AP.
		 */
		if (count < ipa->modem_route_count + 1)
			return false;
	}

	/* If hashing is supported, hashed tables are expected to be defined,
	 * and have the same size as non-hashed tables.  If hashing is not
	 * supported, hashed tables are expected to have zero size (or not
	 * be defined).
	 */
	mem_hashed = ipa_table_mem(ipa, filter, true, false);
	if (hash_support) {
		if (!mem_hashed || mem_hashed->size != mem_ipv4->size)
			return false;
	} else {
		if (mem_hashed && mem_hashed->size)
			return false;
	}

	/* Same check for IPv6 tables */
	mem_hashed = ipa_table_mem(ipa, filter, true, true);
	if (hash_support) {
		if (!mem_hashed || mem_hashed->size != mem_ipv6->size)
			return false;
	} else {
		if (mem_hashed && mem_hashed->size)
			return false;
	}

	return true;
}

/* Initialize a coherent DMA allocation containing initialized filter and
 * route table data.  This is used when initializing or resetting the IPA
 * filter or route table.
 *
 * The first entry in a filter table contains a bitmap indicating which
 * endpoints contain entries in the table.  In addition to that first entry,
 * there is a fixed maximum number of entries that follow.  Filter table
 * entries are 64 bits wide, and (other than the bitmap) contain the DMA
 * address of a filter rule.  A "zero rule" indicates no filtering, and
 * consists of 64 bits of zeroes.  When a filter table is initialized (or
 * reset) its entries are made to refer to the zero rule.
 *
 * Each entry in a route table is the DMA address of a routing rule.  For
 * routing there is also a 64-bit "zero rule" that means no routing, and
 * when a route table is initialized or reset, its entries are made to refer
 * to the zero rule.  The zero rule is shared for route and filter tables.
 *
 *	     +-------------------+
 *	 --> |     zero rule     |
 *	/    |-------------------|
 *	|    |     filter mask   |
 *	|\   |-------------------|
 *	| ---- zero rule address | \
 *	|\   |-------------------|  |
 *	| ---- zero rule address |  |	Max IPA filter count
 *	|    |-------------------|   >	or IPA route count,
 *	|	      ...	    |	whichever is greater
 *	 \   |-------------------|  |
 *	  ---- zero rule address | /
 *	     +-------------------+
 */
int ipa_table_init(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	dma_addr_t addr;
	__le64 le_addr;
	__le64 *virt;
	size_t size;
	u32 count;

	ipa_table_validate_build();

	count = max_t(u32, ipa->filter_count, ipa->route_count);

	/* The IPA hardware requires route and filter table rules to be
	 * aligned on a 128-byte boundary.  We put the "zero rule" at the
	 * base of the table area allocated here.  The DMA address returned
	 * by dma_alloc_coherent() is guaranteed to be a power-of-2 number
	 * of pages, which satisfies the rule alignment requirement.
	 */
	size = IPA_ZERO_RULE_SIZE + (1 + count) * sizeof(__le64);
	virt = dma_alloc_coherent(dev, size, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	ipa->table_virt = virt;
	ipa->table_addr = addr;

	/* First slot is the zero rule */
	*virt++ = 0;

	/* Next is the filter table bitmap.  The "soft" bitmap value might
	 * need to be converted to the hardware representation by shifting
	 * it left one position.  Prior to IPA v5.0, bit 0 repesents global
	 * filtering, which is possible but not used.  IPA v5.0+ eliminated
	 * that option, so there's no shifting required.
	 */
	if (ipa->version < IPA_VERSION_5_0)
		*virt++ = cpu_to_le64(ipa->filtered << 1);
	else
		*virt++ = cpu_to_le64(ipa->filtered);

	/* All the rest contain the DMA address of the zero rule */
	le_addr = cpu_to_le64(addr);
	while (count--)
		*virt++ = le_addr;

	return 0;
}

void ipa_table_exit(struct ipa *ipa)
{
	u32 count = max_t(u32, 1 + ipa->filter_count, ipa->route_count);
	struct device *dev = ipa->dev;
	size_t size;

	size = IPA_ZERO_RULE_SIZE + (1 + count) * sizeof(__le64);

	dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
	ipa->table_addr = 0;
	ipa->table_virt = NULL;
}
