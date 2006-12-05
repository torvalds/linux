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

#include <asm/ps3.h>
#include <asm/lv1call.h>

enum ps3_vendor_id {
	PS3_VENDOR_ID_NONE = 0,
	PS3_VENDOR_ID_SONY = 0x8000000000000000UL,
};

enum ps3_lpar_id {
	PS3_LPAR_ID_CURRENT = 0,
	PS3_LPAR_ID_PME = 1,
};

#define dump_field(_a, _b) _dump_field(_a, _b, __func__, __LINE__)
static void _dump_field(const char *hdr, u64 n, const char* func, int line)
{
#if defined(DEBUG)
	char s[16];
	const char *const in = (const char *)&n;
	unsigned int i;

	for (i = 0; i < 8; i++)
		s[i] = (in[i] <= 126 && in[i] >= 32) ? in[i] : '.';
	s[i] = 0;

	pr_debug("%s:%d: %s%016lx : %s\n", func, line, hdr, n, s);
#endif
}

#define dump_node_name(_a, _b, _c, _d, _e) \
	_dump_node_name(_a, _b, _c, _d, _e, __func__, __LINE__)
static void _dump_node_name (unsigned int lpar_id, u64 n1, u64 n2, u64 n3,
	u64 n4, const char* func, int line)
{
	pr_debug("%s:%d: lpar: %u\n", func, line, lpar_id);
	_dump_field("n1: ", n1, func, line);
	_dump_field("n2: ", n2, func, line);
	_dump_field("n3: ", n3, func, line);
	_dump_field("n4: ", n4, func, line);
}

#define dump_node(_a, _b, _c, _d, _e, _f, _g) \
	_dump_node(_a, _b, _c, _d, _e, _f, _g, __func__, __LINE__)
static void _dump_node(unsigned int lpar_id, u64 n1, u64 n2, u64 n3, u64 n4,
	u64 v1, u64 v2, const char* func, int line)
{
	pr_debug("%s:%d: lpar: %u\n", func, line, lpar_id);
	_dump_field("n1: ", n1, func, line);
	_dump_field("n2: ", n2, func, line);
	_dump_field("n3: ", n3, func, line);
	_dump_field("n4: ", n4, func, line);
	pr_debug("%s:%d: v1: %016lx\n", func, line, v1);
	pr_debug("%s:%d: v2: %016lx\n", func, line, v2);
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

	result = lv1_get_repository_node_value(lpar_id, n1, n2, n3, n4, &v1,
		&v2);

	if (result) {
		pr_debug("%s:%d: lv1_get_repository_node_value failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		dump_node_name(lpar_id, n1, n2, n3, n4);
		return result;
	}

	dump_node(lpar_id, n1, n2, n3, n4, v1, v2);

	if (_v1)
		*_v1 = v1;
	if (_v2)
		*_v2 = v2;

	if (v1 && !_v1)
		pr_debug("%s:%d: warning: discarding non-zero v1: %016lx\n",
			__func__, __LINE__, v1);
	if (v2 && !_v2)
		pr_debug("%s:%d: warning: discarding non-zero v2: %016lx\n",
			__func__, __LINE__, v2);

	return result;
}

int ps3_repository_read_bus_str(unsigned int bus_index, const char *bus_str,
	u64 *value)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field(bus_str, 0),
		0, 0,
		value, 0);
}

int ps3_repository_read_bus_id(unsigned int bus_index, unsigned int *bus_id)
{
	int result;
	u64 v1;
	u64 v2; /* unused */

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("id", 0),
		0, 0,
		&v1, &v2);
	*bus_id = v1;
	return result;
}

int ps3_repository_read_bus_type(unsigned int bus_index,
	enum ps3_bus_type *bus_type)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("type", 0),
		0, 0,
		&v1, 0);
	*bus_type = v1;
	return result;
}

int ps3_repository_read_bus_num_dev(unsigned int bus_index,
	unsigned int *num_dev)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("num_dev", 0),
		0, 0,
		&v1, 0);
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
		value, 0);
}

int ps3_repository_read_dev_id(unsigned int bus_index, unsigned int dev_index,
	unsigned int *dev_id)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("id", 0),
		0,
		&v1, 0);
	*dev_id = v1;
	return result;
}

int ps3_repository_read_dev_type(unsigned int bus_index,
	unsigned int dev_index, enum ps3_dev_type *dev_type)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("type", 0),
		0,
		&v1, 0);
	*dev_type = v1;
	return result;
}

int ps3_repository_read_dev_intr(unsigned int bus_index,
	unsigned int dev_index, unsigned int intr_index,
	unsigned int *intr_type, unsigned int* interrupt_id)
{
	int result;
	u64 v1;
	u64 v2;

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
	unsigned int dev_index, unsigned int reg_index, unsigned int *reg_type)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("bus", bus_index),
		make_field("dev", dev_index),
		make_field("reg", reg_index),
		make_field("type", 0),
		&v1, 0);
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
	unsigned int dev_index, unsigned int reg_index, unsigned int *reg_type,
	u64 *bus_addr, u64 *len)
{
	int result = ps3_repository_read_dev_reg_type(bus_index, dev_index,
		reg_index, reg_type);
	return result ? result
		: ps3_repository_read_dev_reg_addr(bus_index, dev_index,
		reg_index, bus_addr, len);
}

#if defined(DEBUG)
int ps3_repository_dump_resource_info(unsigned int bus_index,
	unsigned int dev_index)
{
	int result = 0;
	unsigned int res_index;

	pr_debug(" -> %s:%d: (%u:%u)\n", __func__, __LINE__,
		bus_index, dev_index);

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_interrupt_type intr_type;
		unsigned int interrupt_id;

		result = ps3_repository_read_dev_intr(bus_index, dev_index,
			res_index, &intr_type, &interrupt_id);

		if (result) {
			if (result !=  LV1_NO_ENTRY)
				pr_debug("%s:%d ps3_repository_read_dev_intr"
					" (%u:%u) failed\n", __func__, __LINE__,
					bus_index, dev_index);
			break;
		}

		pr_debug("%s:%d (%u:%u) intr_type %u, interrupt_id %u\n",
			__func__, __LINE__, bus_index, dev_index, intr_type,
			interrupt_id);
	}

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_region_type reg_type;
		u64 bus_addr;
		u64 len;

		result = ps3_repository_read_dev_reg(bus_index, dev_index,
			res_index, &reg_type, &bus_addr, &len);

		if (result) {
			if (result !=  LV1_NO_ENTRY)
				pr_debug("%s:%d ps3_repository_read_dev_reg"
					" (%u:%u) failed\n", __func__, __LINE__,
					bus_index, dev_index);
			break;
		}

		pr_debug("%s:%d (%u:%u) reg_type %u, bus_addr %lxh, len %lxh\n",
			__func__, __LINE__, bus_index, dev_index, reg_type,
			bus_addr, len);
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static int dump_device_info(unsigned int bus_index, unsigned int num_dev)
{
	int result = 0;
	unsigned int dev_index;

	pr_debug(" -> %s:%d: bus_%u\n", __func__, __LINE__, bus_index);

	for (dev_index = 0; dev_index < num_dev; dev_index++) {
		enum ps3_dev_type dev_type;
		unsigned int dev_id;

		result = ps3_repository_read_dev_type(bus_index, dev_index,
			&dev_type);

		if (result) {
			pr_debug("%s:%d ps3_repository_read_dev_type"
				" (%u:%u) failed\n", __func__, __LINE__,
				bus_index, dev_index);
			break;
		}

		result = ps3_repository_read_dev_id(bus_index, dev_index,
			&dev_id);

		if (result) {
			pr_debug("%s:%d ps3_repository_read_dev_id"
				" (%u:%u) failed\n", __func__, __LINE__,
				bus_index, dev_index);
			continue;
		}

		pr_debug("%s:%d  (%u:%u): dev_type %u, dev_id %u\n", __func__,
			__LINE__, bus_index, dev_index, dev_type, dev_id);

		ps3_repository_dump_resource_info(bus_index, dev_index);
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int ps3_repository_dump_bus_info(void)
{
	int result = 0;
	unsigned int bus_index;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	for (bus_index = 0; bus_index < 10; bus_index++) {
		enum ps3_bus_type bus_type;
		unsigned int bus_id;
		unsigned int num_dev;

		result = ps3_repository_read_bus_type(bus_index, &bus_type);

		if (result) {
			pr_debug("%s:%d read_bus_type(%u) failed\n",
				__func__, __LINE__, bus_index);
			break;
		}

		result = ps3_repository_read_bus_id(bus_index, &bus_id);

		if (result) {
			pr_debug("%s:%d read_bus_id(%u) failed\n",
				__func__, __LINE__, bus_index);
			continue;
		}

		if (bus_index != bus_id)
			pr_debug("%s:%d bus_index != bus_id\n",
				__func__, __LINE__);

		result = ps3_repository_read_bus_num_dev(bus_index, &num_dev);

		if (result) {
			pr_debug("%s:%d read_bus_num_dev(%u) failed\n",
				__func__, __LINE__, bus_index);
			continue;
		}

		pr_debug("%s:%d bus_%u: bus_type %u, bus_id %u, num_dev %u\n",
			__func__, __LINE__, bus_index, bus_type, bus_id,
			num_dev);

		dump_device_info(bus_index, num_dev);
	}

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}
#endif /* defined(DEBUG) */

static int find_device(unsigned int bus_index, unsigned int num_dev,
	unsigned int start_dev_index, enum ps3_dev_type dev_type,
	struct ps3_repository_device *dev)
{
	int result = 0;
	unsigned int dev_index;

	pr_debug("%s:%d: find dev_type %u\n", __func__, __LINE__, dev_type);

	dev->dev_index = UINT_MAX;

	for (dev_index = start_dev_index; dev_index < num_dev; dev_index++) {
		enum ps3_dev_type x;

		result = ps3_repository_read_dev_type(bus_index, dev_index,
			&x);

		if (result) {
			pr_debug("%s:%d read_dev_type failed\n",
				__func__, __LINE__);
			return result;
		}

		if (x == dev_type)
			break;
	}

	BUG_ON(dev_index == num_dev);

	pr_debug("%s:%d: found dev_type %u at dev_index %u\n",
		__func__, __LINE__, dev_type, dev_index);

	result = ps3_repository_read_dev_id(bus_index, dev_index,
		&dev->did.dev_id);

	if (result) {
		pr_debug("%s:%d read_dev_id failed\n",
			__func__, __LINE__);
		return result;
	}

	dev->dev_index = dev_index;

	pr_debug("%s:%d found: dev_id %u\n", __func__, __LINE__,
		dev->did.dev_id);

	return result;
}

int ps3_repository_find_device (enum ps3_bus_type bus_type,
	enum ps3_dev_type dev_type,
	const struct ps3_repository_device *start_dev,
	struct ps3_repository_device *dev)
{
	int result = 0;
	unsigned int bus_index;
	unsigned int num_dev;

	pr_debug("%s:%d: find bus_type %u, dev_type %u\n", __func__, __LINE__,
		bus_type, dev_type);

	dev->bus_index = UINT_MAX;

	for (bus_index = start_dev ? start_dev->bus_index : 0; bus_index < 10;
		bus_index++) {
		enum ps3_bus_type x;

		result = ps3_repository_read_bus_type(bus_index, &x);

		if (result) {
			pr_debug("%s:%d read_bus_type failed\n",
				__func__, __LINE__);
			return result;
		}
		if (x == bus_type)
			break;
	}

	BUG_ON(bus_index == 10);

	pr_debug("%s:%d: found bus_type %u at bus_index %u\n",
		__func__, __LINE__, bus_type, bus_index);

	result = ps3_repository_read_bus_num_dev(bus_index, &num_dev);

	if (result) {
		pr_debug("%s:%d read_bus_num_dev failed\n",
			__func__, __LINE__);
		return result;
	}

	result = find_device(bus_index, num_dev, start_dev
		? start_dev->dev_index + 1 : 0, dev_type, dev);

	if (result) {
		pr_debug("%s:%d get_did failed\n", __func__, __LINE__);
		return result;
	}

	result = ps3_repository_read_bus_id(bus_index, &dev->did.bus_id);

	if (result) {
		pr_debug("%s:%d read_bus_id failed\n",
			__func__, __LINE__);
		return result;
	}

	dev->bus_index = bus_index;

	pr_debug("%s:%d found: bus_id %u, dev_id %u\n",
		__func__, __LINE__, dev->did.bus_id, dev->did.dev_id);

	return result;
}

int ps3_repository_find_interrupt(const struct ps3_repository_device *dev,
	enum ps3_interrupt_type intr_type, unsigned int *interrupt_id)
{
	int result = 0;
	unsigned int res_index;

	pr_debug("%s:%d: find intr_type %u\n", __func__, __LINE__, intr_type);

	*interrupt_id = UINT_MAX;

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_interrupt_type t;
		unsigned int id;

		result = ps3_repository_read_dev_intr(dev->bus_index,
			dev->dev_index, res_index, &t, &id);

		if (result) {
			pr_debug("%s:%d read_dev_intr failed\n",
				__func__, __LINE__);
			return result;
		}

		if (t == intr_type) {
			*interrupt_id = id;
			break;
		}
	}

	BUG_ON(res_index == 10);

	pr_debug("%s:%d: found intr_type %u at res_index %u\n",
		__func__, __LINE__, intr_type, res_index);

	return result;
}

int ps3_repository_find_region(const struct ps3_repository_device *dev,
	enum ps3_region_type reg_type, u64 *bus_addr, u64 *len)
{
	int result = 0;
	unsigned int res_index;

	pr_debug("%s:%d: find reg_type %u\n", __func__, __LINE__, reg_type);

	*bus_addr = *len = 0;

	for (res_index = 0; res_index < 10; res_index++) {
		enum ps3_region_type t;
		u64 a;
		u64 l;

		result = ps3_repository_read_dev_reg(dev->bus_index,
			dev->dev_index, res_index, &t, &a, &l);

		if (result) {
			pr_debug("%s:%d read_dev_reg failed\n",
				__func__, __LINE__);
			return result;
		}

		if (t == reg_type) {
			*bus_addr = a;
			*len = l;
			break;
		}
	}

	BUG_ON(res_index == 10);

	pr_debug("%s:%d: found reg_type %u at res_index %u\n",
		__func__, __LINE__, reg_type, res_index);

	return result;
}

int ps3_repository_read_rm_size(unsigned int ppe_id, u64 *rm_size)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("pu", 0),
		ppe_id,
		make_field("rm_size", 0),
		rm_size, 0);
}

int ps3_repository_read_region_total(u64 *region_total)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("rgntotal", 0),
		0, 0,
		region_total, 0);
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
	u64 v1;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("spun", 0),
		0, 0,
		&v1, 0);
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
	u64 v1;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("spursvn", 0),
		0, 0,
		&v1, 0);
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
	enum ps3_spu_resource_type* resource_type, unsigned int *resource_id)
{
	int result;
	u64 v1;
	u64 v2;

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

int ps3_repository_read_boot_dat_address(u64 *address)
{
	return read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("boot_dat", 0),
		make_field("address", 0),
		0,
		address, 0);
}

int ps3_repository_read_boot_dat_size(unsigned int *size)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_CURRENT,
		make_first_field("bi", 0),
		make_field("boot_dat", 0),
		make_field("size", 0),
		0,
		&v1, 0);
	*size = v1;
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

int ps3_repository_read_num_be(unsigned int *num_be)
{
	int result;
	u64 v1;

	result = read_node(PS3_LPAR_ID_PME,
		make_first_field("ben", 0),
		0,
		0,
		0,
		&v1, 0);
	*num_be = v1;
	return result;
}

int ps3_repository_read_be_node_id(unsigned int be_index, u64 *node_id)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("be", be_index),
		0,
		0,
		0,
		node_id, 0);
}

int ps3_repository_read_tb_freq(u64 node_id, u64 *tb_freq)
{
	return read_node(PS3_LPAR_ID_PME,
		make_first_field("be", 0),
		node_id,
		make_field("clock", 0),
		0,
		tb_freq, 0);
}

int ps3_repository_read_be_tb_freq(unsigned int be_index, u64 *tb_freq)
{
	int result;
	u64 node_id;

	*tb_freq = 0;
	result = ps3_repository_read_be_node_id(0, &node_id);
	return result ? result
		: ps3_repository_read_tb_freq(node_id, tb_freq);
}
