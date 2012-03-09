/*
 *  PS3 repository routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <asm/lv1call.h>

#include "platform.h"

enum ps3_vendor_id {
	PS3_VENDOR_ID_NONE = 0,
	PS3_VENDOR_ID_SONY = 0x8000000000000000UL,
};

enum ps3_lpar_id {
	PS3_LPAR_ID_CURRENT = 0,
	PS3_LPAR_ID_PME = 1,
};

#define dump_field(_a, _b) _dump_field(_a, _b, __func__, __LINE__)
static void _dump_field(const char *hdr, u64 n, const char *func, int line)
{
#if defined(DEBUG)
	char s[16];
	const char *const in = (const char *)&n;
	unsigned int i;

	for (i = 0; i < 8; i++)
		s[i] = (in[i] <= 126 && in[i] >= 32) ? in[i] : '.';
	s[i] = 0;

	pr_devel("%s:%d: %s%016llx : %s\n", func, line, hdr, n, s);
#endif
}

#define dump_node_name(_a, _b, _c, _d, _e) \
	_dump_node_name(_a, _b, _c, _d, _e, __func__, __LINE__)
static void _dump_node_name(unsigned int lpar_id, u64 n1, u64 n2, u64 n3,
	u64 n4, const char *func, int line)
{
	pr_devel("%s:%d: lpar: %u\n", func, line, lpar_id);
	_dump_field("n1: ", n1, func, line);
	_dump_field("n2: ", n2, func, line);
	_dump_field("n3: ", n3, func, line);
	_dump_field("n4: ", n4, func, line);
}

#define dump_node(_a, _b, _c, _d, _e, _f, _g) \
	_dump_node(_a, _b, _c, _d, _e, _f, _g, __func__, __LINE__)
static void _dump_node(unsigned int lpar_id, u64 n1, u64 n2, u64 n3, u64 n4,
	u64 v1, u64 v2, const char *func, int line)
{
	pr_devel("%s:%d: lpar: %u\n", func, line, lpar_id);
	_dump_field("n1: ", n1, func, line);
	_dump_field("n2: ", n2, func, line);
	_dump_field("n3: ", n3, func, line);
	_dump_field("n4: ", n4, func, line);
	pr_devel("%s:%d: v1: %016llx\n", func, line, v1);
	pr_devel("%s:%d: v2: %016llx\n", func, line, v2);
}

/**
 * make_first_field - Make the first field of a repository node name.
 * @text: Text portion of the field.
 * @index: Numeric index portion of the field.  Use zero for 'don't care'.
 *
 * This routine sets the vendor id to zero (non-vendor specific).
 * Returns field value.
 */

static u64 make_first_field(const char *text, u64 index)
{
	u64 n;

	strncpy((char *)&n, text, 8);
	return PS3_VENDOR_ID_NONE + (n >> 32) + index;
}

/**
 * make_field - Make subsequent fields of a repository node name.
 * @text: Text portion of the field.  Use "" for 'don't care'.
 * @index: Numeric index portion of the field.  Use zero for 'don't care'.
 *
 * Returns field value.
 */

static u64 make_field(const char *text, u64 index)
{
	u64 n;

	strncpy((char *)&n, text, 8);
	return n + index;
}

/**
 * read_node - Read a repository node from raw fields.
 * @n1: First field of node name.
 * @n2: Second field of node name.  Use zero for 'don't care'.
 * @n3: Third field of node name.  Use zero for 'don't care'.
 * @n4: Fourth field of node name.  Use zero for 'don't care'.
 * @v1: First repository value (high word).
 * @v2: Second repository value (low word).  Optional parameter, use zero
 *      for 'don't care'.
 */

static int read_node(unsigned int lpar_id, u64 n1, u64 n2, u64 n3, u64 n4,
	u64 *_v1, u64 *_v2)
{
	int result;
	u64 v1;
	u64 v2;

	if (lpar_id == PS3_LPAR_ID_CURRENT) {
		u64 id;
		lv1_get_logical_partition_id(&id);
		lpar_id = id;
	}

	result = lv1_read_repository_node(lpar_id, n1, n2, n3, n4, &v1,
		&v2);

	if (result) {
		pr_warn("%s:%d: lv1_read_repository_node failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		dump_node_name(lpar_id, n1, n2, n3, n4);
		return -ENOENT;
	}

	dump_node(lpar_id, n1, n2, n3, n4, v1, v2);

	if (_v1)
		*_v1 = v1;
	if (_v2)
		*_v2 = v2;

	if (v1 && !_v1)
		pr_devel("%s:%d: warning: discarding non-zero v1: %016llx\n",
			__func__, __LINE__, v1);
	if (v2 && !_v2)
		pr_devel("%s:%d: warning: discarding non-zero v2: %016llx\n",
			__func__, __LINE__, v2);

	return 0;
}

int ps3_repository_read_bus_str(unsigned int bus_index, const char *bus_str,
	u64 *value)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field(bus_str, 0),
		0, 0,
		value, NULL);
}

int ps3_repository_read_bus_id(unsigned int bus_index, u64 *bus_id)
{
	int result;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("id", 0),
		0, 0,
		bus_id, NULL);
	return result;
}

int ps3_repository_read_bus_type(unsigned int bus_index,
	enum ps3_bus_type *bus_type)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("type", 0),
		0, 0,
		&v1, NULL);
	*bus_type = v1;
	return result;
}

int ps3_repository_read_bus_num_dev(unsigned int bus_index,
	unsigned int *num_dev)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("num_dev", 0),
		0, 0,
		&v1, NULL);
	*num_dev = v1;
	return result;
}

int ps3_repository_read_dev_str(unsigned int bus_index,
	unsigned int dev_index, const char *dev_str, u64 *value)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field(dev_str, 0),
		0,
		value, NULL);
}

int ps3_repository_read_dev_id(unsigned int bus_index, unsigned int dev_index,
	u64 *dev_id)
{
	int result;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("id", 0),
		0,
		dev_id, NULL);
	return result;
}

int ps3_repository_read_dev_type(unsigned int bus_index,
	unsigned int dev_index, enum ps3_dev_type *dev_type)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("type", 0),
		0,
		&v1, NULL);
	*dev_type = v1;
	return result;
}

int ps3_repository_read_dev_intr(unsigned int bus_index,
	unsigned int dev_index, unsigned int intr_index,
	enum ps3_interrupt_type *intr_type, unsigned int *interrupt_id)
{
	int result;
	u64 v1 = 0;
	u64 v2 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("intr", intr_index),
		0,
		&v1, &v2);
	*intr_type = v1;
	*interrupt_id = v2;
	return result;
}

int ps3_repository_read_dev_reg_type(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index,
	enum ps3_reg_type *reg_type)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("reg", reg_index),
		make_field("type", 0),
		&v1, NULL);
	*reg_type = v1;
	return result;
}

int ps3_repository_read_dev_reg_addr(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index, u64 *bus_addr, u64 *len)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("reg", reg_index),
		make_field("data", 0),
		bus_addr, len);
}

int ps3_repository_read_dev_reg(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index,
	enum ps3_reg_type *reg_type, u64 *bus_addr, u64 *len)
{
	int result = ps3_repository_read_dev_reg_type(bus_index, dev_index,
		reg_index, reg_type);
	return result ? result
		: ps3_repository_read_dev_reg_addr(bus_index, dev_index,
		reg_index, bus_addr, len);
}



int ps3_repository_find_device(struct ps3_repository_device *repo)
{
	int result;
	struct ps3_repository_device tmp = *repo;
	unsigned int num_dev;

	BUG_ON(repo->bus_index > 10);
	BUG_ON(repo->dev_index > 10);

	result = ps3_repository_read_bus_num_dev(tmp.bus_index, &num_dev);

	if (result) {
		pr_devel("%s:%d read_bus_num_dev failed\n", __func__, __LINE__);
		return result;
	}

	pr_devel("%s:%d: bus_type %u, bus_index %u, bus_id %llu, num_dev %u\n",
		__func__, __LINE__, tmp.bus_type, tmp.bus_index, tmp.bus_id,
		num_dev);

	if (tmp.dev_index >= num_dev) {
		pr_devel("%s:%d: no device found\n", __func__, __LINE__);
		return -ENODEV;
	}

	result = ps3_repository_read_dev_type(tmp.bus_index, tmp.dev_index,
		&tmp.dev_type);

	if (result) {
		pr_devel("%s:%d read_dev_type failed\n", __func__, __LINE__);
		return result;
	}

	result = ps3_repository_read_dev_id(tmp.bus_index, tmp.dev_index,
		&tmp.dev_id);

	if (result) {
		pr_devel("%s:%d ps3_repository_read_dev_id failed\n", __func__,
		__LINE__);
		return result;
	}

	pr_devel("%s:%d: found: dev_type %u, dev_index %u, dev_id %llu\n",
		__func__, __LINE__, tmp.dev_type, tmp.dev_index, tmp.dev_id);

	*repo = tmp;
	return 0;
}

int ps3_repository_find_device_by_id(struct ps3_repository_device *repo,
				     u64 bus_id, u64 dev_id)
{
	int result = -ENODEV;
	struct ps3_repository_device tmp;
	unsigned int num_dev;

	pr_devel(" -> %s:%u: find device by id %llu:%llu\n", __func__, __LINE__,
		 bus_id, dev_id);

	for (tmp.bus_index = 0; tmp.bus_index < 10; tmp.bus_index++) {
		result = ps3_repository_read_bus_id(tmp.bus_index,
						    &tmp.bus_id);
		if (result) {
			pr_devel("%s:%u read_bus_id(%u) failed\n", __func__,
				 __LINE__, tmp.bus_index);
			return result;
		}

		if (tmp.bus_id == bus_id)
			goto found_bus;

		pr_devel("%s:%u: skip, bus_id %llu\n", __func__, __LINE__,
			 tmp.bus_id);
	}
	pr_devel(" <- %s:%u: bus not found\n", __func__, __LINE__);
	return result;

found_bus:
	result = ps3_repository_read_bus_type(tmp.bus_index, &tmp.bus_type);
	if (result) {
		pr_devel("%s:%u read_bus_type(%u) failed\n", __func__,
			 __LINE__, tmp.bus_index);
		return result;
	}

	result = ps3_repository_read_bus_num_dev(tmp.bus_index, &num_dev);
	if (result) {
		pr_devel("%s:%u read_bus_num_dev failed\n", __func__,
			 __LINE__);
		return result;
	}

	for (tmp.dev_index = 0; tmp.dev_index < num_dev; tmp.dev_index++) {
		result = ps3_repository_read_dev_id(tmp.bus_index,
						    tmp.dev_index,
						    &tmp.dev_id);
		if (result) {
			pr_devel("%s:%u read_dev_id(%u:%u) failed\n", __func__,
				 __LINE__, tmp.bus_index, tmp.dev_index);
			return result;
		}

		if (tmp.dev_id == dev_id)
			goto found_dev;

		pr_devel("%s:%u: skip, dev_id %llu\n", __func__, __LINE__,
			 tmp.dev_id);
	}
	pr_devel(" <- %s:%u: dev not found\n", __func__, __LINE__);
	return result;

found_dev:
	result = ps3_repository_read_dev_type(tmp.bus_index, tmp.dev_index,
					      &tmp.dev_type);
	if (result) {
		pr_devel("%s:%u read_dev_type failed\n", __func__, __LINE__);
		return result;
	}

	pr_devel(" <- %s:%u: found: type (%u:%u) index (%u:%u) id (%llu:%llu)\n",
		 __func__, __LINE__, tmp.bus_type, tmp.dev_type, tmp.bus_index,
		 tmp.dev_index, tmp.bus_id, tmp.dev_id);
	*repo = tmp;
	return 0;
}

int __devinit ps3_repository_find_devices(enum ps3_bus_type bus_type,
	int (*callback)(const struct ps3_repository_device *repo))
{
	int result = 0;
	struct ps3_repository_device repo;

	pr_devel(" -> %s:%d: find bus_type %u\n", __func__, __LINE__, bus_type);

	repo.bus_type = bus_type;
	result = ps3_repository_find_bus(repo.bus_type, 0, &repo.bus_index);
	if (result) {
		pr_devel(" <- %s:%u: bus not found\n", __func__, __LINE__);
		return result;
	}

	result = ps3_repository_read_bus_id(repo.bus_index, &repo.bus_id);
	if (result) {
		pr_devel("%s:%d read_bus_id(%u) failed\n", __func__, __LINE__,
			 repo.bus_index);
		return result;
	}

	for (repo.dev_index = 0; ; repo.dev_index++) {
		result = ps3_repository_find_device(&repo);
		if (result == -ENODEV) {
			result = 0;
			break;
		} else if (result)
			break;

		result = callback(&repo);
		if (result) {
			pr_devel("%s:%d: abort at callback\n", __func__,
				__LINE__);
			break;
		}
	}

	pr_devel(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int ps3_repository_find_bus(enum ps3_bus_type bus_type, unsigned int from,
	unsigned int *bus_index)
{
	unsigned int i;
	enum ps3_bus_type type;
	int error;

	for (i = from; i < 10; i++) {
		error = ps3_repository_read_bus_type(i, &type);
		if (error) {
			pr_devel("%s:%d read_bus_type failed\n",
				__func__, __LINE__);
			*bus_index = UINT_MAX;
			return error;
		}
		if (type == bus_type) {
			*bus_index = i;
			return 0;
		}
	}
	*bus_index = UINT_MAX;
	return -ENODEV;
}

int ps3_repository_find_interrupt(const struct ps3_repository_device *repo,
	enum ps3_interrupt_type intr_type, unsigned int *interrupt_id)
{
	int result = 0;
	unsigned int res_index;

	pr_devel("%s:%d: find intr_type %u\n", __func__, __LINE__, intr_type);

	*interrupt_id = UINT_MAX;

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_interrupt_type t;
		unsigned int id;

		result = ps3_repository_read_dev_intr(repo->bus_index,
			repo->dev_index, res_index, &t, &id);

		if (result) {
			pr_devel("%s:%d read_dev_intr failed\n",
				__func__, __LINE__);
			return result;
		}

		if (t == intr_type) {
			*interrupt_id = id;
			break;
		}
	}

	if (res_index == 10)
		return -ENODEV;

	pr_devel("%s:%d: found intr_type %u at res_index %u\n",
		__func__, __LINE__, intr_type, res_index);

	return result;
}

int ps3_repository_find_reg(const struct ps3_repository_device *repo,
	enum ps3_reg_type reg_type, u64 *bus_addr, u64 *len)
{
	int result = 0;
	unsigned int res_index;

	pr_devel("%s:%d: find reg_type %u\n", __func__, __LINE__, reg_type);

	*bus_addr = *len = 0;

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_reg_type t;
		u64 a;
		u64 l;

		result = ps3_repository_read_dev_reg(repo->bus_index,
			repo->dev_index, res_index, &t, &a, &l);

		if (result) {
			pr_devel("%s:%d read_dev_reg failed\n",
				__func__, __LINE__);
			return result;
		}

		if (t == reg_type) {
			*bus_addr = a;
			*len = l;
			break;
		}
	}

	if (res_index == 10)
		return -ENODEV;

	pr_devel("%s:%d: found reg_type %u at res_index %u\n",
		__func__, __LINE__, reg_type, res_index);

	return result;
}

int ps3_repository_read_stor_dev_port(unsigned int bus_index,
	unsigned int dev_index, u64 *port)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("port", 0),
		0, port, NULL);
}

int ps3_repository_read_stor_dev_blk_size(unsigned int bus_index,
	unsigned int dev_index, u64 *blk_size)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("blk_size", 0),
		0, blk_size, NULL);
}

int ps3_repository_read_stor_dev_num_blocks(unsigned int bus_index,
	unsigned int dev_index, u64 *num_blocks)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("n_blocks", 0),
		0, num_blocks, NULL);
}

int ps3_repository_read_stor_dev_num_regions(unsigned int bus_index,
	unsigned int dev_index, unsigned int *num_regions)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("n_regs", 0),
		0, &v1, NULL);
	*num_regions = v1;
	return result;
}

int ps3_repository_read_stor_dev_region_id(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index,
	unsigned int *region_id)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
	    make_first_field("bus", bus_index),
	    make_field("dev", dev_index),
	    make_field("region", region_index),
	    make_field("id", 0),
	    &v1, NULL);
	*region_id = v1;
	return result;
}

int ps3_repository_read_stor_dev_region_size(unsigned int bus_index,
	unsigned int dev_index,	unsigned int region_index, u64 *region_size)
{
	return read_node(PS3_LPAR_ID_PME,
	    make_first_field("bus", bus_index),
	    make_field("dev", dev_index),
	    make_field("region", region_index),
	    make_field("size", 0),
	    region_size, NULL);
}

int ps3_repository_read_stor_dev_region_start(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index, u64 *region_start)
{
	return read_node(PS3_LPAR_ID_PME,
	    make_first_field("bus", bus_index),
	    make_field("dev", dev_index),
	    make_field("region", region_index),
	    make_field("start", 0),
	    region_start, NULL);
}

int ps3_repository_read_stor_dev_info(unsigned int bus_index,
	unsigned int dev_index, u64 *port, u64 *blk_size,
	u64 *num_blocks, unsigned int *num_regions)
{
	int result;

	result = ps3_repository_read_stor_dev_port(bus_index, dev_index, port);
	if (result)
	    return result;

	result = ps3_repository_read_stor_dev_blk_size(bus_index, dev_index,
		blk_size);
	if (result)
	    return result;

	result = ps3_repository_read_stor_dev_num_blocks(bus_index, dev_index,
		num_blocks);
	if (result)
	    return result;

	result = ps3_repository_read_stor_dev_num_regions(bus_index, dev_index,
		num_regions);
	return result;
}

int ps3_repository_read_stor_dev_region(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index,
	unsigned int *region_id, u64 *region_start, u64 *region_size)
{
	int result;

	result = ps3_repository_read_stor_dev_region_id(bus_index, dev_index,
		region_index, region_id);
	if (result)
	    return result;

	result = ps3_repository_read_stor_dev_region_start(bus_index, dev_index,
		region_index, region_start);
	if (result)
	    return result;

	result = ps3_repository_read_stor_dev_region_size(bus_index, dev_index,
		region_index, region_size);
	return result;
}

/**
 * ps3_repository_read_num_pu - Number of logical PU processors for this lpar.
 */

int ps3_repository_read_num_pu(u64 *num_pu)
{
	*num_pu = 0;
	return read_node(PS3_LPAR_ID_CURRENT,
			   make_first_field("bi", 0),
			   make_field("pun", 0),
			   0, 0,
			   num_pu, NULL);
}

/**
 * ps3_repository_read_pu_id - Read the logical PU id.
 * @pu_index: Zero based index.
 * @pu_id: The logical PU id.
 */

int ps3_repository_read_pu_id(unsigned int pu_index, u64 *pu_id)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("pu", pu_index),
		0, 0,
		pu_id, NULL);
}

int ps3_repository_read_rm_size(unsigned int ppe_id, u64 *rm_size)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("pu", 0),
		ppe_id,
		make_field("rm_size", 0),
		rm_size, NULL);
}

int ps3_repository_read_region_total(u64 *region_total)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("rgntotal", 0),
		0, 0,
		region_total, NULL);
}

/**
 * ps3_repository_read_mm_info - Read mm info for single pu system.
 * @rm_base: Real mode memory base address.
 * @rm_size: Real mode memory size.
 * @region_total: Maximum memory region size.
 */

int ps3_repository_read_mm_info(u64 *rm_base, u64 *rm_size, u64 *region_total)
{
	int result;
	u64 ppe_id;

	lv1_get_logical_ppe_id(&ppe_id);
	*rm_base = 0;
	result = ps3_repository_read_rm_size(ppe_id, rm_size);
	return result ? result
		: ps3_repository_read_region_total(region_total);
}

/**
 * ps3_repository_read_num_spu_reserved - Number of physical spus reserved.
 * @num_spu: Number of physical spus.
 */

int ps3_repository_read_num_spu_reserved(unsigned int *num_spu_reserved)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("spun", 0),
		0, 0,
		&v1, NULL);
	*num_spu_reserved = v1;
	return result;
}

/**
 * ps3_repository_read_num_spu_resource_id - Number of spu resource reservations.
 * @num_resource_id: Number of spu resource ids.
 */

int ps3_repository_read_num_spu_resource_id(unsigned int *num_resource_id)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("spursvn", 0),
		0, 0,
		&v1, NULL);
	*num_resource_id = v1;
	return result;
}

/**
 * ps3_repository_read_spu_resource_id - spu resource reservation id value.
 * @res_index: Resource reservation index.
 * @resource_type: Resource reservation type.
 * @resource_id: Resource reservation id.
 */

int ps3_repository_read_spu_resource_id(unsigned int res_index,
	enum ps3_spu_resource_type *resource_type, unsigned int *resource_id)
{
	int result;
	u64 v1 = 0;
	u64 v2 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("spursv", 0),
		res_index,
		0,
		&v1, &v2);
	*resource_type = v1;
	*resource_id = v2;
	return result;
}

static int ps3_repository_read_boot_dat_address(u64 *address)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("boot_dat", 0),
		make_field("address", 0),
		0,
		address, NULL);
}

int ps3_repository_read_boot_dat_size(unsigned int *size)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("boot_dat", 0),
		make_field("size", 0),
		0,
		&v1, NULL);
	*size = v1;
	return result;
}

int ps3_repository_read_vuart_av_port(unsigned int *port)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("vir_uart", 0),
		make_field("port", 0),
		make_field("avset", 0),
		&v1, NULL);
	*port = v1;
	return result;
}

int ps3_repository_read_vuart_sysmgr_port(unsigned int *port)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("vir_uart", 0),
		make_field("port", 0),
		make_field("sysmgr", 0),
		&v1, NULL);
	*port = v1;
	return result;
}

/**
  * ps3_repository_read_boot_dat_info - Get address and size of cell_ext_os_area.
  * address: lpar address of cell_ext_os_area
  * @size: size of cell_ext_os_area
  */

int ps3_repository_read_boot_dat_info(u64 *lpar_addr, unsigned int *size)
{
	int result;

	*size = 0;
	result = ps3_repository_read_boot_dat_address(lpar_addr);
	return result ? result
		: ps3_repository_read_boot_dat_size(size);
}

/**
 * ps3_repository_read_num_be - Number of physical BE processors in the system.
 */

int ps3_repository_read_num_be(unsigned int *num_be)
{
	int result;
	u64 v1 = 0;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("ben", 0),
		0,
		0,
		0,
		&v1, NULL);
	*num_be = v1;
	return result;
}

/**
 * ps3_repository_read_be_node_id - Read the physical BE processor node id.
 * @be_index: Zero based index.
 * @node_id: The BE processor node id.
 */

int ps3_repository_read_be_node_id(unsigned int be_index, u64 *node_id)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("be", be_index),
		0,
		0,
		0,
		node_id, NULL);
}

/**
 * ps3_repository_read_be_id - Read the physical BE processor id.
 * @node_id: The BE processor node id.
 * @be_id: The BE processor id.
 */

int ps3_repository_read_be_id(u64 node_id, u64 *be_id)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("be", 0),
		node_id,
		0,
		0,
		be_id, NULL);
}

int ps3_repository_read_tb_freq(u64 node_id, u64 *tb_freq)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("be", 0),
		node_id,
		make_field("clock", 0),
		0,
		tb_freq, NULL);
}

int ps3_repository_read_be_tb_freq(unsigned int be_index, u64 *tb_freq)
{
	int result;
	u64 node_id;

	*tb_freq = 0;
	result = ps3_repository_read_be_node_id(be_index, &node_id);
	return result ? result
		: ps3_repository_read_tb_freq(node_id, tb_freq);
}

int ps3_repository_read_lpm_privileges(unsigned int be_index, u64 *lpar,
	u64 *rights)
{
	int result;
	u64 node_id;

	*lpar = 0;
	*rights = 0;
	result = ps3_repository_read_be_node_id(be_index, &node_id);
	return result ? result
		: read_node(PS3_LPAR_ID_PME,
			    make_first_field("be", 0),
			    node_id,
			    make_field("lpm", 0),
			    make_field("priv", 0),
			    lpar, rights);
}

#if defined(DEBUG)

int ps3_repository_dump_resource_info(const struct ps3_repository_device *repo)
{
	int result = 0;
	unsigned int res_index;

	pr_devel(" -> %s:%d: (%u:%u)\n", __func__, __LINE__,
		repo->bus_index, repo->dev_index);

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_interrupt_type intr_type;
		unsigned int interrupt_id;

		result = ps3_repository_read_dev_intr(repo->bus_index,
			repo->dev_index, res_index, &intr_type, &interrupt_id);

		if (result) {
			if (result !=  LV1_NO_ENTRY)
				pr_devel("%s:%d ps3_repository_read_dev_intr"
					" (%u:%u) failed\n", __func__, __LINE__,
					repo->bus_index, repo->dev_index);
			break;
		}

		pr_devel("%s:%d (%u:%u) intr_type %u, interrupt_id %u\n",
			__func__, __LINE__, repo->bus_index, repo->dev_index,
			intr_type, interrupt_id);
	}

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_reg_type reg_type;
		u64 bus_addr;
		u64 len;

		result = ps3_repository_read_dev_reg(repo->bus_index,
			repo->dev_index, res_index, &reg_type, &bus_addr, &len);

		if (result) {
			if (result !=  LV1_NO_ENTRY)
				pr_devel("%s:%d ps3_repository_read_dev_reg"
					" (%u:%u) failed\n", __func__, __LINE__,
					repo->bus_index, repo->dev_index);
			break;
		}

		pr_devel("%s:%d (%u:%u) reg_type %u, bus_addr %llxh, len %llxh\n",
			__func__, __LINE__, repo->bus_index, repo->dev_index,
			reg_type, bus_addr, len);
	}

	pr_devel(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int dump_stor_dev_info(struct ps3_repository_device *repo)
{
	int result = 0;
	unsigned int num_regions, region_index;
	u64 port, blk_size, num_blocks;

	pr_devel(" -> %s:%d: (%u:%u)\n", __func__, __LINE__,
		repo->bus_index, repo->dev_index);

	result = ps3_repository_read_stor_dev_info(repo->bus_index,
		repo->dev_index, &port, &blk_size, &num_blocks, &num_regions);
	if (result) {
		pr_devel("%s:%d ps3_repository_read_stor_dev_info"
			" (%u:%u) failed\n", __func__, __LINE__,
			repo->bus_index, repo->dev_index);
		goto out;
	}

	pr_devel("%s:%d  (%u:%u): port %llu, blk_size %llu, num_blocks "
		 "%llu, num_regions %u\n",
		 __func__, __LINE__, repo->bus_index, repo->dev_index,
		port, blk_size, num_blocks, num_regions);

	for (region_index = 0; region_index < num_regions; region_index++) {
		unsigned int region_id;
		u64 region_start, region_size;

		result = ps3_repository_read_stor_dev_region(repo->bus_index,
			repo->dev_index, region_index, &region_id,
			&region_start, &region_size);
		if (result) {
			 pr_devel("%s:%d ps3_repository_read_stor_dev_region"
				  " (%u:%u) failed\n", __func__, __LINE__,
				  repo->bus_index, repo->dev_index);
			break;
		}

		pr_devel("%s:%d (%u:%u) region_id %u, start %lxh, size %lxh\n",
			__func__, __LINE__, repo->bus_index, repo->dev_index,
			region_id, (unsigned long)region_start,
			(unsigned long)region_size);
	}

out:
	pr_devel(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int dump_device_info(struct ps3_repository_device *repo,
	unsigned int num_dev)
{
	int result = 0;

	pr_devel(" -> %s:%d: bus_%u\n", __func__, __LINE__, repo->bus_index);

	for (repo->dev_index = 0; repo->dev_index < num_dev;
		repo->dev_index++) {

		result = ps3_repository_read_dev_type(repo->bus_index,
			repo->dev_index, &repo->dev_type);

		if (result) {
			pr_devel("%s:%d ps3_repository_read_dev_type"
				" (%u:%u) failed\n", __func__, __LINE__,
				repo->bus_index, repo->dev_index);
			break;
		}

		result = ps3_repository_read_dev_id(repo->bus_index,
			repo->dev_index, &repo->dev_id);

		if (result) {
			pr_devel("%s:%d ps3_repository_read_dev_id"
				" (%u:%u) failed\n", __func__, __LINE__,
				repo->bus_index, repo->dev_index);
			continue;
		}

		pr_devel("%s:%d  (%u:%u): dev_type %u, dev_id %lu\n", __func__,
			__LINE__, repo->bus_index, repo->dev_index,
			repo->dev_type, (unsigned long)repo->dev_id);

		ps3_repository_dump_resource_info(repo);

		if (repo->bus_type == PS3_BUS_TYPE_STORAGE)
			dump_stor_dev_info(repo);
	}

	pr_devel(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int ps3_repository_dump_bus_info(void)
{
	int result = 0;
	struct ps3_repository_device repo;

	pr_devel(" -> %s:%d\n", __func__, __LINE__);

	memset(&repo, 0, sizeof(repo));

	for (repo.bus_index = 0; repo.bus_index < 10; repo.bus_index++) {
		unsigned int num_dev;

		result = ps3_repository_read_bus_type(repo.bus_index,
			&repo.bus_type);

		if (result) {
			pr_devel("%s:%d read_bus_type(%u) failed\n",
				__func__, __LINE__, repo.bus_index);
			break;
		}

		result = ps3_repository_read_bus_id(repo.bus_index,
			&repo.bus_id);

		if (result) {
			pr_devel("%s:%d read_bus_id(%u) failed\n",
				__func__, __LINE__, repo.bus_index);
			continue;
		}

		if (repo.bus_index != repo.bus_id)
			pr_devel("%s:%d bus_index != bus_id\n",
				__func__, __LINE__);

		result = ps3_repository_read_bus_num_dev(repo.bus_index,
			&num_dev);

		if (result) {
			pr_devel("%s:%d read_bus_num_dev(%u) failed\n",
				__func__, __LINE__, repo.bus_index);
			continue;
		}

		pr_devel("%s:%d bus_%u: bus_type %u, bus_id %lu, num_dev %u\n",
			__func__, __LINE__, repo.bus_index, repo.bus_type,
			(unsigned long)repo.bus_id, num_dev);

		dump_device_info(&repo, num_dev);
	}

	pr_devel(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

#endif /* defined(DEBUG) */
