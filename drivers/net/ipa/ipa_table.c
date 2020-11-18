// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2020 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include "ipa.h"
#include "ipa_version.h"
#include "ipa_endpoint.h"
#include "ipa_table.h"
#include "ipa_reg.h"
#include "ipa_mem.h"
#include "ipa_cmd.h"
#include "gsi.h"
#include "gsi_trans.h"

/**
 * DOC: IPA Filter and Route Tables
 *
 * The IPA has tables defined in its local shared memory that define filter
 * and routing rules.  Each entry in these tables contains a 64-bit DMA
 * address that refers to DRAM (system memory) containing a rule definition.
 * A rule consists of a contiguous block of 32-bit values terminated with
 * 32 zero bits.  A special "zero entry" rule consisting of 64 zero bits
 * represents "no filtering" or "no routing," and is the reset value for
 * filter or route table rules.  Separate tables (both filter and route)
 * used for IPv4 and IPv6.  Additionally, there can be hashed filter or
 * route tables, which are used when a hash of message metadata matches.
 * Hashed operation is not supported by all IPA hardware.
 *
 * Each filter rule is associated with an AP or modem TX endpoint, though
 * not all TX endpoints support filtering.  The first 64-bit entry in a
 * filter table is a bitmap indicating which endpoints have entries in
 * the table.  The low-order bit (bit 0) in this bitmap represents a
 * special global filter, which applies to all traffic.  This is not
 * used in the current code.  Bit 1, if set, indicates that there is an
 * entry (i.e. a DMA address referring to a rule) for endpoint 0 in the
 * table.  Bit 2, if set, indicates there is an entry for endpoint 1,
 * and so on.  Space is set aside in IPA local memory to hold as many
 * filter table entries as might be required, but typically they are not
 * all used.
 *
 * The AP initializes all entries in a filter table to refer to a "zero"
 * entry.  Once initialized the modem and AP update the entries for
 * endpoints they "own" directly.  Currently the AP does not use the
 * IPA filtering functionality.
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

/* IPA hardware constrains filter and route tables alignment */
#define IPA_TABLE_ALIGN			128	/* Minimum table alignment */

/* Assignment of route table entries to the modem and AP */
#define IPA_ROUTE_MODEM_MIN		0
#define IPA_ROUTE_MODEM_COUNT		8

#define IPA_ROUTE_AP_MIN		IPA_ROUTE_MODEM_COUNT
#define IPA_ROUTE_AP_COUNT \
		(IPA_ROUTE_COUNT_MAX - IPA_ROUTE_MODEM_COUNT)

/* Filter or route rules consist of a set of 32-bit values followed by a
 * 32-bit all-zero rule list terminator.  The "zero rule" is simply an
 * all-zero rule followed by the list terminator.
 */
#define IPA_ZERO_RULE_SIZE		(2 * sizeof(__le32))

#ifdef IPA_VALIDATE

/* Check things that can be validated at build time. */
static void ipa_table_validate_build(void)
{
	/* IPA hardware accesses memory 128 bytes at a time.  Addresses
	 * referred to by entries in filter and route tables must be
	 * aligned on 128-byte byte boundaries.  The only rule address
	 * ever use is the "zero rule", and it's aligned at the base
	 * of a coherent DMA allocation.
	 */
	BUILD_BUG_ON(ARCH_DMA_MINALIGN % IPA_TABLE_ALIGN);

	/* Filter and route tables contain DMA addresses that refer to
	 * filter or route rules.  We use a fixed constant to represent
	 * the size of either type of table entry.  Code in ipa_table_init()
	 * uses a pointer to __le64 to initialize table entriews.
	 */
	BUILD_BUG_ON(IPA_TABLE_ENTRY_SIZE != sizeof(dma_addr_t));
	BUILD_BUG_ON(sizeof(dma_addr_t) != sizeof(__le64));

	/* A "zero rule" is used to represent no filtering or no routing.
	 * It is a 64-bit block of zeroed memory.  Code in ipa_table_init()
	 * assumes that it can be written using a pointer to __le64.
	 */
	BUILD_BUG_ON(IPA_ZERO_RULE_SIZE != sizeof(__le64));

	/* Impose a practical limit on the number of routes */
	BUILD_BUG_ON(IPA_ROUTE_COUNT_MAX > 32);
	/* The modem must be allotted at least one route table entry */
	BUILD_BUG_ON(!IPA_ROUTE_MODEM_COUNT);
	/* But it can't have more than what is available */
	BUILD_BUG_ON(IPA_ROUTE_MODEM_COUNT > IPA_ROUTE_COUNT_MAX);

}

static bool
ipa_table_valid_one(struct ipa *ipa, bool route, bool ipv6, bool hashed)
{
	struct device *dev = &ipa->pdev->dev;
	const struct ipa_mem *mem;
	u32 size;

	if (route) {
		if (ipv6)
			mem = hashed ? &ipa->mem[IPA_MEM_V6_ROUTE_HASHED]
				     : &ipa->mem[IPA_MEM_V6_ROUTE];
		else
			mem = hashed ? &ipa->mem[IPA_MEM_V4_ROUTE_HASHED]
				     : &ipa->mem[IPA_MEM_V4_ROUTE];
		size = IPA_ROUTE_COUNT_MAX * IPA_TABLE_ENTRY_SIZE;
	} else {
		if (ipv6)
			mem = hashed ? &ipa->mem[IPA_MEM_V6_FILTER_HASHED]
				     : &ipa->mem[IPA_MEM_V6_FILTER];
		else
			mem = hashed ? &ipa->mem[IPA_MEM_V4_FILTER_HASHED]
				     : &ipa->mem[IPA_MEM_V4_FILTER];
		size = (1 + IPA_FILTER_COUNT_MAX) * IPA_TABLE_ENTRY_SIZE;
	}

	if (!ipa_cmd_table_valid(ipa, mem, route, ipv6, hashed))
		return false;

	/* mem->size >= size is sufficient, but we'll demand more */
	if (mem->size == size)
		return true;

	/* Hashed table regions can be zero size if hashing is not supported */
	if (hashed && !mem->size)
		return true;

	dev_err(dev, "IPv%c %s%s table region size 0x%02x, expected 0x%02x\n",
		ipv6 ? '6' : '4', hashed ? "hashed " : "",
		route ? "route" : "filter", mem->size, size);

	return false;
}

/* Verify the filter and route table memory regions are the expected size */
bool ipa_table_valid(struct ipa *ipa)
{
	bool valid = true;

	valid = valid && ipa_table_valid_one(ipa, false, false, false);
	valid = valid && ipa_table_valid_one(ipa, false, false, true);
	valid = valid && ipa_table_valid_one(ipa, false, true, false);
	valid = valid && ipa_table_valid_one(ipa, false, true, true);
	valid = valid && ipa_table_valid_one(ipa, true, false, false);
	valid = valid && ipa_table_valid_one(ipa, true, false, true);
	valid = valid && ipa_table_valid_one(ipa, true, true, false);
	valid = valid && ipa_table_valid_one(ipa, true, true, true);

	return valid;
}

bool ipa_filter_map_valid(struct ipa *ipa, u32 filter_map)
{
	struct device *dev = &ipa->pdev->dev;
	u32 count;

	if (!filter_map) {
		dev_err(dev, "at least one filtering endpoint is required\n");

		return false;
	}

	count = hweight32(filter_map);
	if (count > IPA_FILTER_COUNT_MAX) {
		dev_err(dev, "too many filtering endpoints (%u, max %u)\n",
			count, IPA_FILTER_COUNT_MAX);

		return false;
	}

	return true;
}

#else /* !IPA_VALIDATE */
static void ipa_table_validate_build(void)

{
}

#endif /* !IPA_VALIDATE */

/* Zero entry count means no table, so just return a 0 address */
static dma_addr_t ipa_table_addr(struct ipa *ipa, bool filter_mask, u16 count)
{
	u32 skip;

	if (!count)
		return 0;

/* assert(count <= max_t(u32, IPA_FILTER_COUNT_MAX, IPA_ROUTE_COUNT_MAX)); */

	/* Skip over the zero rule and possibly the filter mask */
	skip = filter_mask ? 1 : 2;

	return ipa->table_addr + skip * sizeof(*ipa->table_virt);
}

static void ipa_table_reset_add(struct gsi_trans *trans, bool filter,
				u16 first, u16 count, const struct ipa_mem *mem)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	dma_addr_t addr;
	u32 offset;
	u16 size;

	/* Nothing to do if the table memory regions is empty */
	if (!mem->size)
		return;

	if (filter)
		first++;	/* skip over bitmap */

	offset = mem->offset + first * IPA_TABLE_ENTRY_SIZE;
	size = count * IPA_TABLE_ENTRY_SIZE;
	addr = ipa_table_addr(ipa, false, count);

	ipa_cmd_dma_shared_mem_add(trans, offset, size, addr, true);
}

/* Reset entries in a single filter table belonging to either the AP or
 * modem to refer to the zero entry.  The memory region supplied will be
 * for the IPv4 and IPv6 non-hashed and hashed filter tables.
 */
static int
ipa_filter_reset_table(struct ipa *ipa, const struct ipa_mem *mem, bool modem)
{
	u32 ep_mask = ipa->filter_map;
	u32 count = hweight32(ep_mask);
	struct gsi_trans *trans;
	enum gsi_ee_id ee_id;

	if (!mem->size)
		return 0;

	trans = ipa_cmd_trans_alloc(ipa, count);
	if (!trans) {
		dev_err(&ipa->pdev->dev,
			"no transaction for %s filter reset\n",
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

		ipa_table_reset_add(trans, true, endpoint_id, 1, mem);
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

	ret = ipa_filter_reset_table(ipa, &ipa->mem[IPA_MEM_V4_FILTER], modem);
	if (ret)
		return ret;

	ret = ipa_filter_reset_table(ipa, &ipa->mem[IPA_MEM_V4_FILTER_HASHED],
				     modem);
	if (ret)
		return ret;

	ret = ipa_filter_reset_table(ipa, &ipa->mem[IPA_MEM_V6_FILTER], modem);
	if (ret)
		return ret;
	ret = ipa_filter_reset_table(ipa, &ipa->mem[IPA_MEM_V6_FILTER_HASHED],
				     modem);

	return ret;
}

/* The AP routes and modem routes are each contiguous within the
 * table.  We can update each table with a single command, and we
 * won't exceed the per-transaction command limit.
 * */
static int ipa_route_reset(struct ipa *ipa, bool modem)
{
	struct gsi_trans *trans;
	u16 first;
	u16 count;

	trans = ipa_cmd_trans_alloc(ipa, 4);
	if (!trans) {
		dev_err(&ipa->pdev->dev,
			"no transaction for %s route reset\n",
			modem ? "modem" : "AP");
		return -EBUSY;
	}

	if (modem) {
		first = IPA_ROUTE_MODEM_MIN;
		count = IPA_ROUTE_MODEM_COUNT;
	} else {
		first = IPA_ROUTE_AP_MIN;
		count = IPA_ROUTE_AP_COUNT;
	}

	ipa_table_reset_add(trans, false, first, count,
			    &ipa->mem[IPA_MEM_V4_ROUTE]);
	ipa_table_reset_add(trans, false, first, count,
			    &ipa->mem[IPA_MEM_V4_ROUTE_HASHED]);

	ipa_table_reset_add(trans, false, first, count,
			    &ipa->mem[IPA_MEM_V6_ROUTE]);
	ipa_table_reset_add(trans, false, first, count,
			    &ipa->mem[IPA_MEM_V6_ROUTE_HASHED]);

	gsi_trans_commit_wait(trans);

	return 0;
}

void ipa_table_reset(struct ipa *ipa, bool modem)
{
	struct device *dev = &ipa->pdev->dev;
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
	u32 offset = ipa_reg_filt_rout_hash_flush_offset(ipa->version);
	struct gsi_trans *trans;
	u32 val;

	/* IPA version 4.2 does not support hashed tables */
	if (ipa->version == IPA_VERSION_4_2)
		return 0;

	trans = ipa_cmd_trans_alloc(ipa, 1);
	if (!trans) {
		dev_err(&ipa->pdev->dev, "no transaction for hash flush\n");
		return -EBUSY;
	}

	val = IPV4_FILTER_HASH_FLUSH | IPV6_FILTER_HASH_FLUSH;
	val |= IPV6_ROUTER_HASH_FLUSH | IPV4_ROUTER_HASH_FLUSH;

	ipa_cmd_register_write_add(trans, offset, val, val, false);

	gsi_trans_commit_wait(trans);

	return 0;
}

static void ipa_table_init_add(struct gsi_trans *trans, bool filter,
			       enum ipa_cmd_opcode opcode,
			       const struct ipa_mem *mem,
			       const struct ipa_mem *hash_mem)
{
	struct ipa *ipa = container_of(trans->gsi, struct ipa, gsi);
	dma_addr_t hash_addr;
	dma_addr_t addr;
	u16 hash_count;
	u16 hash_size;
	u16 count;
	u16 size;

	/* The number of filtering endpoints determines number of entries
	 * in the filter table.  The hashed and non-hashed filter table
	 * will have the same number of entries.  The size of the route
	 * table region determines the number of entries it has.
	 */
	if (filter) {
		count = hweight32(ipa->filter_map);
		hash_count = hash_mem->size ? count : 0;
	} else {
		count = mem->size / IPA_TABLE_ENTRY_SIZE;
		hash_count = hash_mem->size / IPA_TABLE_ENTRY_SIZE;
	}
	size = count * IPA_TABLE_ENTRY_SIZE;
	hash_size = hash_count * IPA_TABLE_ENTRY_SIZE;

	addr = ipa_table_addr(ipa, filter, count);
	hash_addr = ipa_table_addr(ipa, filter, hash_count);

	ipa_cmd_table_init_add(trans, opcode, size, mem->offset, addr,
			       hash_size, hash_mem->offset, hash_addr);
}

int ipa_table_setup(struct ipa *ipa)
{
	struct gsi_trans *trans;

	trans = ipa_cmd_trans_alloc(ipa, 4);
	if (!trans) {
		dev_err(&ipa->pdev->dev, "no transaction for table setup\n");
		return -EBUSY;
	}

	ipa_table_init_add(trans, false, IPA_CMD_IP_V4_ROUTING_INIT,
			   &ipa->mem[IPA_MEM_V4_ROUTE],
			   &ipa->mem[IPA_MEM_V4_ROUTE_HASHED]);

	ipa_table_init_add(trans, false, IPA_CMD_IP_V6_ROUTING_INIT,
			   &ipa->mem[IPA_MEM_V6_ROUTE],
			   &ipa->mem[IPA_MEM_V6_ROUTE_HASHED]);

	ipa_table_init_add(trans, true, IPA_CMD_IP_V4_FILTER_INIT,
			   &ipa->mem[IPA_MEM_V4_FILTER],
			   &ipa->mem[IPA_MEM_V4_FILTER_HASHED]);

	ipa_table_init_add(trans, true, IPA_CMD_IP_V6_FILTER_INIT,
			   &ipa->mem[IPA_MEM_V6_FILTER],
			   &ipa->mem[IPA_MEM_V6_FILTER_HASHED]);

	gsi_trans_commit_wait(trans);

	return 0;
}

void ipa_table_teardown(struct ipa *ipa)
{
	/* Nothing to do */	/* XXX Maybe reset the tables? */
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
	u32 offset;
	u32 val;

	offset = IPA_REG_ENDP_FILTER_ROUTER_HSH_CFG_N_OFFSET(endpoint_id);

	val = ioread32(endpoint->ipa->reg_virt + offset);

	/* Zero all filter-related fields, preserving the rest */
	u32p_replace_bits(&val, 0, IPA_REG_ENDP_FILTER_HASH_MSK_ALL);

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

static void ipa_filter_config(struct ipa *ipa, bool modem)
{
	enum gsi_ee_id ee_id = modem ? GSI_EE_MODEM : GSI_EE_AP;
	u32 ep_mask = ipa->filter_map;

	/* IPA version 4.2 has no hashed route tables */
	if (ipa->version == IPA_VERSION_4_2)
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

static void ipa_filter_deconfig(struct ipa *ipa, bool modem)
{
	/* Nothing to do */
}

static bool ipa_route_id_modem(u32 route_id)
{
	return route_id >= IPA_ROUTE_MODEM_MIN &&
		route_id <= IPA_ROUTE_MODEM_MIN + IPA_ROUTE_MODEM_COUNT - 1;
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
	u32 offset = IPA_REG_ENDP_FILTER_ROUTER_HSH_CFG_N_OFFSET(route_id);
	u32 val;

	val = ioread32(ipa->reg_virt + offset);

	/* Zero all route-related fields, preserving the rest */
	u32p_replace_bits(&val, 0, IPA_REG_ENDP_ROUTER_HASH_MSK_ALL);

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_route_config(struct ipa *ipa, bool modem)
{
	u32 route_id;

	/* IPA version 4.2 has no hashed route tables */
	if (ipa->version == IPA_VERSION_4_2)
		return;

	for (route_id = 0; route_id < IPA_ROUTE_COUNT_MAX; route_id++)
		if (ipa_route_id_modem(route_id) == modem)
			ipa_route_tuple_zero(ipa, route_id);
}

static void ipa_route_deconfig(struct ipa *ipa, bool modem)
{
	/* Nothing to do */
}

void ipa_table_config(struct ipa *ipa)
{
	ipa_filter_config(ipa, false);
	ipa_filter_config(ipa, true);
	ipa_route_config(ipa, false);
	ipa_route_config(ipa, true);
}

void ipa_table_deconfig(struct ipa *ipa)
{
	ipa_route_deconfig(ipa, true);
	ipa_route_deconfig(ipa, false);
	ipa_filter_deconfig(ipa, true);
	ipa_filter_deconfig(ipa, false);
}

/*
 * Initialize a coherent DMA allocation containing initialized filter and
 * route table data.  This is used when initializing or resetting the IPA
 * filter or route table.
 *
 * The first entry in a filter table contains a bitmap indicating which
 * endpoints contain entries in the table.  In addition to that first entry,
 * there are at most IPA_FILTER_COUNT_MAX entries that follow.  Filter table
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
 * Note that the IPA hardware requires a filter or route rule address to be
 * aligned on a 128 byte boundary.  The coherent DMA buffer we allocate here
 * has a minimum alignment, and we place the zero rule at the base of that
 * allocated space.  In ipa_table_init() we verify the minimum DMA allocation
 * meets our requirement.
 *
 *	     +-------------------+
 *	 --> |     zero rule     |
 *	/    |-------------------|
 *	|    |     filter mask   |
 *	|\   |-------------------|
 *	| ---- zero rule address | \
 *	|\   |-------------------|  |
 *	| ---- zero rule address |  |	IPA_FILTER_COUNT_MAX
 *	|    |-------------------|   >	or IPA_ROUTE_COUNT_MAX,
 *	|	      ...	    |	whichever is greater
 *	 \   |-------------------|  |
 *	  ---- zero rule address | /
 *	     +-------------------+
 */
int ipa_table_init(struct ipa *ipa)
{
	u32 count = max_t(u32, IPA_FILTER_COUNT_MAX, IPA_ROUTE_COUNT_MAX);
	struct device *dev = &ipa->pdev->dev;
	dma_addr_t addr;
	__le64 le_addr;
	__le64 *virt;
	size_t size;

	ipa_table_validate_build();

	size = IPA_ZERO_RULE_SIZE + (1 + count) * IPA_TABLE_ENTRY_SIZE;
	virt = dma_alloc_coherent(dev, size, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	ipa->table_virt = virt;
	ipa->table_addr = addr;

	/* First slot is the zero rule */
	*virt++ = 0;

	/* Next is the filter table bitmap.  The "soft" bitmap value
	 * must be converted to the hardware representation by shifting
	 * it left one position.  (Bit 0 repesents global filtering,
	 * which is possible but not used.)
	 */
	*virt++ = cpu_to_le64((u64)ipa->filter_map << 1);

	/* All the rest contain the DMA address of the zero rule */
	le_addr = cpu_to_le64(addr);
	while (count--)
		*virt++ = le_addr;

	return 0;
}

void ipa_table_exit(struct ipa *ipa)
{
	u32 count = max_t(u32, 1 + IPA_FILTER_COUNT_MAX, IPA_ROUTE_COUNT_MAX);
	struct device *dev = &ipa->pdev->dev;
	size_t size;

	size = IPA_ZERO_RULE_SIZE + (1 + count) * IPA_TABLE_ENTRY_SIZE;

	dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
	ipa->table_addr = 0;
	ipa->table_virt = NULL;
}
