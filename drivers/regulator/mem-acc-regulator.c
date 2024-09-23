// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2014-2018, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"ACC: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/debug-regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/string.h>
#include <linux/qcom_scm.h>

#define MEM_ACC_DEFAULT_SEL_SIZE	2

#define BYTES_PER_FUSE_ROW		8

/* mem-acc config flags */

enum {
	MEM_ACC_USE_CORNER_ACC_MAP	= BIT(0),
	MEM_ACC_USE_ADDR_VAL_MAP	= BIT(1),
};

#define FUSE_MAP_NO_MATCH		(-1)
#define FUSE_PARAM_MATCH_ANY		(-1)
#define PARAM_MATCH_ANY			(-1)

enum {
	MEMORY_L1,
	MEMORY_L2,
	MEMORY_MAX,
};

#define MEM_ACC_TYPE_MAX		6

/**
 * struct acc_reg_value - Acc register configuration structure
 * @addr_index:	An index in to phys_reg_addr_list and remap_reg_addr_list
 *		to get the ACC register physical address and remapped address.
 * @reg_val:	Value to program in to the register mapped by addr_index.
 */
struct acc_reg_value {
	u32		addr_index;
	u32		reg_val;
};

struct corner_acc_reg_config {
	struct acc_reg_value	*reg_config_list;
	int			max_reg_config_len;
};

struct mem_acc_regulator {
	struct device		*dev;
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;

	int			corner;
	bool			mem_acc_supported[MEMORY_MAX];
	bool			mem_acc_custom_supported[MEMORY_MAX];

	u32			*acc_sel_mask[MEMORY_MAX];
	u32			*acc_sel_bit_pos[MEMORY_MAX];
	u32			acc_sel_bit_size[MEMORY_MAX];
	u32			num_acc_sel[MEMORY_MAX];
	u32			*acc_en_bit_pos;
	u32			num_acc_en;
	u32			*corner_acc_map;
	u32			num_corners;
	u32			override_fuse_value;
	int			override_map_match;
	int			override_map_count;


	void __iomem		*acc_sel_base[MEMORY_MAX];
	void __iomem		*acc_en_base;
	phys_addr_t		acc_sel_addr[MEMORY_MAX];
	phys_addr_t		acc_en_addr;
	u32			flags;

	void __iomem		*acc_custom_addr[MEMORY_MAX];
	u32			*acc_custom_data[MEMORY_MAX];

	phys_addr_t		mem_acc_type_addr[MEM_ACC_TYPE_MAX];
	u32			*mem_acc_type_data;

	/* eFuse parameters */
	phys_addr_t		efuse_addr;
	void __iomem		*efuse_base;

	u32			num_acc_reg;
	u32			*phys_reg_addr_list;
	void __iomem		**remap_reg_addr_list;
	struct corner_acc_reg_config	*corner_acc_reg_config;
	u32			*override_acc_range_fuse_list;
	int			override_acc_range_fuse_num;
};

static DEFINE_MUTEX(mem_acc_memory_mutex);

static u64 mem_acc_read_efuse_row(struct mem_acc_regulator *mem_acc_vreg,
					u32 row_num, bool use_tz_api)
{
	u64 efuse_bits;

	if (!use_tz_api) {
		efuse_bits = readl_relaxed(mem_acc_vreg->efuse_base
			+ row_num * BYTES_PER_FUSE_ROW);
		return efuse_bits;
	}

	pr_err("read row %d unsuccessful, no support for tz_api\n", row_num);

	return 0;
}

static inline u32 apc_to_acc_corner(struct mem_acc_regulator *mem_acc_vreg,
								int corner)
{
	/*
	 * corner_acc_map maps the corner from index 0 and  APC corner value
	 * starts from the value 1
	 */
	return mem_acc_vreg->corner_acc_map[corner - 1];
}

static void __update_acc_sel(struct mem_acc_regulator *mem_acc_vreg,
						int corner, int mem_type)
{
	u32 acc_data, acc_data_old, i, bit, acc_corner;

	acc_data = readl_relaxed(mem_acc_vreg->acc_sel_base[mem_type]);
	acc_data_old = acc_data;
	for (i = 0; i < mem_acc_vreg->num_acc_sel[mem_type]; i++) {
		bit = mem_acc_vreg->acc_sel_bit_pos[mem_type][i];
		acc_data &= ~mem_acc_vreg->acc_sel_mask[mem_type][i];
		acc_corner = apc_to_acc_corner(mem_acc_vreg, corner);
		acc_data |= (acc_corner << bit) &
			mem_acc_vreg->acc_sel_mask[mem_type][i];
	}
	pr_debug("corner=%d old_acc_sel=0x%02x new_acc_sel=0x%02x mem_type=%d\n",
			corner, acc_data_old, acc_data, mem_type);
	writel_relaxed(acc_data, mem_acc_vreg->acc_sel_base[mem_type]);
}

static void __update_acc_type(struct mem_acc_regulator *mem_acc_vreg,
				int corner)
{
	int i, rc;

	for (i = 0; i < MEM_ACC_TYPE_MAX; i++) {
		if (mem_acc_vreg->mem_acc_type_addr[i]) {
			rc = qcom_scm_io_writel(mem_acc_vreg->mem_acc_type_addr[i],
				mem_acc_vreg->mem_acc_type_data[corner - 1 + i *
				mem_acc_vreg->num_corners]);
			if (rc)
				pr_err("qcom_scm_io_writel: %pa failure rc:%d\n",
					&(mem_acc_vreg->mem_acc_type_addr[i]),
					rc);
		}
	}
}

static void __update_acc_custom(struct mem_acc_regulator *mem_acc_vreg,
						int corner, int mem_type)
{
	writel_relaxed(
		mem_acc_vreg->acc_custom_data[mem_type][corner-1],
		mem_acc_vreg->acc_custom_addr[mem_type]);
	pr_debug("corner=%d mem_type=%d custom_data=0x%2x\n", corner,
		mem_type, mem_acc_vreg->acc_custom_data[mem_type][corner-1]);
}

static void update_acc_sel(struct mem_acc_regulator *mem_acc_vreg, int corner)
{
	int i;

	for (i = 0; i < MEMORY_MAX; i++) {
		if (mem_acc_vreg->mem_acc_supported[i])
			__update_acc_sel(mem_acc_vreg, corner, i);
		if (mem_acc_vreg->mem_acc_custom_supported[i])
			__update_acc_custom(mem_acc_vreg, corner, i);
	}

	if (mem_acc_vreg->mem_acc_type_data)
		__update_acc_type(mem_acc_vreg, corner);
}

static void update_acc_reg(struct mem_acc_regulator *mem_acc_vreg, int corner)
{
	struct corner_acc_reg_config *corner_acc_reg_config;
	struct acc_reg_value *reg_config_list;
	int i, index;
	u32 addr_index, reg_val;

	corner_acc_reg_config =
		&mem_acc_vreg->corner_acc_reg_config[mem_acc_vreg->corner];
	reg_config_list = corner_acc_reg_config->reg_config_list;
	for (i = 0; i < corner_acc_reg_config->max_reg_config_len; i++) {
		/*
		 * Use (corner - 1) in the below equation as
		 * the reg_config_list[] stores the values starting from
		 * index '0' where as the minimum corner value allowed
		 * in regulator framework is '1'.
		 */
		index = (corner - 1) * corner_acc_reg_config->max_reg_config_len
			+ i;
		addr_index = reg_config_list[index].addr_index;
		reg_val = reg_config_list[index].reg_val;

		if (addr_index == PARAM_MATCH_ANY)
			break;

		writel_relaxed(reg_val,
				mem_acc_vreg->remap_reg_addr_list[addr_index]);
		/* make sure write complete */
		mb();

		pr_debug("corner=%d register:0x%x value:0x%x\n", corner,
			mem_acc_vreg->phys_reg_addr_list[addr_index], reg_val);
	}
}

static int mem_acc_regulator_set_voltage(struct regulator_dev *rdev,
		int corner, int corner_max, unsigned int *selector)
{
	struct mem_acc_regulator *mem_acc_vreg = rdev_get_drvdata(rdev);
	int i;

	if (corner > mem_acc_vreg->num_corners) {
		pr_err("Invalid corner=%d requested\n", corner);
		return -EINVAL;
	}

	pr_debug("old corner=%d, new corner=%d\n",
			mem_acc_vreg->corner, corner);

	if (corner == mem_acc_vreg->corner)
		return 0;

	/* go up or down one level at a time */
	mutex_lock(&mem_acc_memory_mutex);

	if (mem_acc_vreg->flags & MEM_ACC_USE_ADDR_VAL_MAP) {
		update_acc_reg(mem_acc_vreg, corner);
	} else if (mem_acc_vreg->flags & MEM_ACC_USE_CORNER_ACC_MAP) {
		if (corner > mem_acc_vreg->corner) {
			for (i = mem_acc_vreg->corner + 1; i <= corner; i++) {
				pr_debug("UP: to corner %d\n", i);
				update_acc_sel(mem_acc_vreg, i);
			}
		} else {
			for (i = mem_acc_vreg->corner - 1; i >= corner; i--) {
				pr_debug("DOWN: to corner %d\n", i);
				update_acc_sel(mem_acc_vreg, i);
			}
		}
	}

	mutex_unlock(&mem_acc_memory_mutex);

	pr_debug("new voltage corner set %d\n", corner);

	mem_acc_vreg->corner = corner;

	return 0;
}

static int mem_acc_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct mem_acc_regulator *mem_acc_vreg = rdev_get_drvdata(rdev);

	return mem_acc_vreg->corner;
}

static const struct regulator_ops mem_acc_corner_ops = {
	.set_voltage		= mem_acc_regulator_set_voltage,
	.get_voltage		= mem_acc_regulator_get_voltage,
};

static int __mem_acc_sel_init(struct mem_acc_regulator *mem_acc_vreg,
							int mem_type)
{
	int i;
	u32 bit, mask;

	mem_acc_vreg->acc_sel_mask[mem_type] = devm_kzalloc(mem_acc_vreg->dev,
		mem_acc_vreg->num_acc_sel[mem_type] * sizeof(u32), GFP_KERNEL);
	if (!mem_acc_vreg->acc_sel_mask[mem_type])
		return -ENOMEM;

	for (i = 0; i < mem_acc_vreg->num_acc_sel[mem_type]; i++) {
		bit = mem_acc_vreg->acc_sel_bit_pos[mem_type][i];
		mask = BIT(mem_acc_vreg->acc_sel_bit_size[mem_type]) - 1;
		mem_acc_vreg->acc_sel_mask[mem_type][i] = mask << bit;
	}

	return 0;
}

static int mem_acc_sel_init(struct mem_acc_regulator *mem_acc_vreg)
{
	int i, rc;

	for (i = 0; i < MEMORY_MAX; i++) {
		if (mem_acc_vreg->mem_acc_supported[i]) {
			rc = __mem_acc_sel_init(mem_acc_vreg, i);
			if (rc) {
				pr_err("Unable to initialize mem_type=%d rc=%d\n",
					i, rc);
				return rc;
			}
		}
	}

	return 0;
}

static void mem_acc_en_init(struct mem_acc_regulator *mem_acc_vreg)
{
	int i, bit;
	u32 acc_data;

	acc_data = readl_relaxed(mem_acc_vreg->acc_en_base);
	pr_debug("init: acc_en_register=%x\n", acc_data);
	for (i = 0; i < mem_acc_vreg->num_acc_en; i++) {
		bit = mem_acc_vreg->acc_en_bit_pos[i];
		acc_data |= BIT(bit);
	}
	pr_debug("final: acc_en_register=%x\n", acc_data);
	writel_relaxed(acc_data, mem_acc_vreg->acc_en_base);
}

static int populate_acc_data(struct mem_acc_regulator *mem_acc_vreg,
			const char *prop_name, u32 **value, u32 *len)
{
	int rc;

	if (!of_get_property(mem_acc_vreg->dev->of_node, prop_name, len)) {
		pr_err("Unable to find %s property\n", prop_name);
		return -EINVAL;
	}
	*len /= sizeof(u32);
	if (!(*len)) {
		pr_err("Incorrect entries in %s\n", prop_name);
		return -EINVAL;
	}

	*value = devm_kzalloc(mem_acc_vreg->dev, (*len) * sizeof(u32),
							GFP_KERNEL);
	if (!(*value)) {
		pr_err("Unable to allocate memory for %s\n", prop_name);
		return -ENOMEM;
	}

	pr_debug("Found %s, data-length = %d\n", prop_name, *len);

	rc = of_property_read_u32_array(mem_acc_vreg->dev->of_node,
					prop_name, *value, *len);
	if (rc) {
		pr_err("Unable to populate %s rc=%d\n", prop_name, rc);
		return rc;
	}

	return 0;
}

static int mem_acc_sel_setup(struct mem_acc_regulator *mem_acc_vreg,
			struct resource *res, int mem_type)
{
	int len, rc;
	char *mem_select_str;
	char *mem_select_size_str;

	mem_acc_vreg->acc_sel_addr[mem_type] = res->start;
	len = resource_size(res);
	pr_debug("'acc_sel_addr' = %pa mem_type=%d (len=%d)\n",
					&res->start, mem_type, len);

	mem_acc_vreg->acc_sel_base[mem_type] = devm_ioremap(mem_acc_vreg->dev,
			mem_acc_vreg->acc_sel_addr[mem_type], len);
	if (!mem_acc_vreg->acc_sel_base[mem_type]) {
		pr_err("Unable to map 'acc_sel_addr' %pa for mem_type=%d\n",
			&mem_acc_vreg->acc_sel_addr[mem_type], mem_type);
		return -EINVAL;
	}

	switch (mem_type) {
	case MEMORY_L1:
		mem_select_str = "qcom,acc-sel-l1-bit-pos";
		mem_select_size_str = "qcom,acc-sel-l1-bit-size";
		break;
	case MEMORY_L2:
		mem_select_str = "qcom,acc-sel-l2-bit-pos";
		mem_select_size_str = "qcom,acc-sel-l2-bit-size";
		break;
	default:
		pr_err("Invalid memory type: %d\n", mem_type);
		return -EINVAL;
	}

	mem_acc_vreg->acc_sel_bit_size[mem_type] = MEM_ACC_DEFAULT_SEL_SIZE;
	of_property_read_u32(mem_acc_vreg->dev->of_node, mem_select_size_str,
			&mem_acc_vreg->acc_sel_bit_size[mem_type]);

	rc = populate_acc_data(mem_acc_vreg, mem_select_str,
			&mem_acc_vreg->acc_sel_bit_pos[mem_type],
			&mem_acc_vreg->num_acc_sel[mem_type]);
	if (rc)
		pr_err("Unable to populate '%s' rc=%d\n", mem_select_str, rc);

	return rc;
}

static int mem_acc_efuse_init(struct platform_device *pdev,
				 struct mem_acc_regulator *mem_acc_vreg)
{
	struct resource *res;
	int len;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "efuse_addr");
	if (!res || !res->start) {
		mem_acc_vreg->efuse_base = NULL;
		pr_debug("'efuse_addr' resource missing or not used.\n");
		return 0;
	}

	mem_acc_vreg->efuse_addr = res->start;
	len = resource_size(res);

	pr_info("efuse_addr = %pa (len=0x%x)\n", &res->start, len);

	mem_acc_vreg->efuse_base = devm_ioremap(&pdev->dev,
						mem_acc_vreg->efuse_addr, len);
	if (!mem_acc_vreg->efuse_base) {
		pr_err("Unable to map efuse_addr %pa\n",
				&mem_acc_vreg->efuse_addr);
		return -EINVAL;
	}

	return 0;
}

static int mem_acc_custom_data_init(struct platform_device *pdev,
				 struct mem_acc_regulator *mem_acc_vreg,
				 int mem_type)
{
	struct resource *res;
	char *custom_apc_addr_str, *custom_apc_data_str;
	int len, rc = 0;

	switch (mem_type) {
	case MEMORY_L1:
		custom_apc_addr_str = "acc-l1-custom";
		custom_apc_data_str = "qcom,l1-acc-custom-data";
		break;
	case MEMORY_L2:
		custom_apc_addr_str = "acc-l2-custom";
		custom_apc_data_str = "qcom,l2-acc-custom-data";
		break;
	default:
		pr_err("Invalid memory type: %d\n", mem_type);
		return -EINVAL;
	}

	if (!of_find_property(mem_acc_vreg->dev->of_node,
				custom_apc_data_str, NULL)) {
		pr_debug("%s custom_data not specified\n", custom_apc_data_str);
		return 0;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						custom_apc_addr_str);
	if (!res || !res->start) {
		pr_debug("%s resource missing\n", custom_apc_addr_str);
		return -EINVAL;
	}

	len = resource_size(res);
	mem_acc_vreg->acc_custom_addr[mem_type] =
		devm_ioremap(mem_acc_vreg->dev, res->start, len);
	if (!mem_acc_vreg->acc_custom_addr[mem_type]) {
		pr_err("Unable to map %s %pa\n",
			custom_apc_addr_str, &res->start);
		return -EINVAL;
	}

	rc = populate_acc_data(mem_acc_vreg, custom_apc_data_str,
				&mem_acc_vreg->acc_custom_data[mem_type], &len);
	if (rc) {
		pr_err("Unable to find %s rc=%d\n", custom_apc_data_str, rc);
		return rc;
	}

	if (mem_acc_vreg->num_corners != len) {
		pr_err("Custom data is not present for all the corners\n");
		return -EINVAL;
	}

	mem_acc_vreg->mem_acc_custom_supported[mem_type] = true;

	return 0;
}

static int override_mem_acc_custom_data(struct mem_acc_regulator *mem_acc_vreg,
		 int mem_type)
{
	char *custom_apc_data_str;
	int len, rc = 0, i;
	int tuple_count, tuple_match;
	u32 index = 0, value = 0;

	switch (mem_type) {
	case MEMORY_L1:
		custom_apc_data_str = "qcom,override-l1-acc-custom-data";
		break;
	case MEMORY_L2:
		custom_apc_data_str = "qcom,override-l2-acc-custom-data";
		break;
	default:
		pr_err("Invalid memory type: %d\n", mem_type);
		return -EINVAL;
	}

	if (!of_find_property(mem_acc_vreg->dev->of_node,
				custom_apc_data_str, &len)) {
		pr_debug("%s not specified\n", custom_apc_data_str);
		return 0;
	}

	if (mem_acc_vreg->override_map_count) {
		if (mem_acc_vreg->override_map_match == FUSE_MAP_NO_MATCH)
			return 0;
		tuple_count = mem_acc_vreg->override_map_count;
		tuple_match = mem_acc_vreg->override_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != mem_acc_vreg->num_corners * tuple_count * sizeof(u32)) {
		pr_err("%s length=%d is invalid\n", custom_apc_data_str, len);
		return -EINVAL;
	}

	for (i = 0; i < mem_acc_vreg->num_corners; i++) {
		index = (tuple_match * mem_acc_vreg->num_corners) + i;
		rc = of_property_read_u32_index(mem_acc_vreg->dev->of_node,
					custom_apc_data_str, index, &value);
		if (rc) {
			pr_err("Unable read %s index %u, rc=%d\n",
					custom_apc_data_str, index, rc);
			return rc;
		}
		mem_acc_vreg->acc_custom_data[mem_type][i] = value;
	}

	return 0;
}

static int mem_acc_override_corner_map(struct mem_acc_regulator *mem_acc_vreg)
{
	int len = 0, i, rc;
	int tuple_count, tuple_match;
	u32 index = 0, value = 0;
	char *prop_str = "qcom,override-corner-acc-map";

	if (!of_find_property(mem_acc_vreg->dev->of_node, prop_str, &len))
		return 0;

	if (mem_acc_vreg->override_map_count) {
		if (mem_acc_vreg->override_map_match ==	FUSE_MAP_NO_MATCH)
			return 0;
		tuple_count = mem_acc_vreg->override_map_count;
		tuple_match = mem_acc_vreg->override_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	if (len != mem_acc_vreg->num_corners * tuple_count * sizeof(u32)) {
		pr_err("%s length=%d is invalid\n", prop_str, len);
		return -EINVAL;
	}

	for (i = 0; i < mem_acc_vreg->num_corners; i++) {
		index = (tuple_match * mem_acc_vreg->num_corners) + i;
		rc = of_property_read_u32_index(mem_acc_vreg->dev->of_node,
						prop_str, index, &value);
		if (rc) {
			pr_err("Unable read %s index %u, rc=%d\n",
						prop_str, index, rc);
			return rc;
		}
		mem_acc_vreg->corner_acc_map[i] = value;
	}

	return 0;

}

static void mem_acc_read_efuse_param(struct mem_acc_regulator *mem_acc_vreg,
		u32 *fuse_sel, int *val)
{
	u64 fuse_bits;

	fuse_bits = mem_acc_read_efuse_row(mem_acc_vreg, fuse_sel[0],
					   fuse_sel[3]);
	/*
	 * fuse_sel[1] = LSB position in row (shift)
	 * fuse_sel[2] = num of bits (mask)
	 */
	*val = (fuse_bits >> fuse_sel[1]) & ((1 << fuse_sel[2]) - 1);
}

#define FUSE_TUPLE_SIZE 4
static int mem_acc_parse_override_fuse_version_map(
			 struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	int i, rc, tuple_size;
	int len = 0;
	u32 *tmp;
	u32 fuse_sel[4];
	char *prop_str;

	prop_str = "qcom,override-acc-fuse-sel";
	rc = of_property_read_u32_array(of_node, prop_str, fuse_sel,
					FUSE_TUPLE_SIZE);
	if (rc < 0) {
		pr_err("Read failed - %s rc=%d\n", prop_str, rc);
		return rc;
	}

	mem_acc_read_efuse_param(mem_acc_vreg, fuse_sel,
				 &mem_acc_vreg->override_fuse_value);

	prop_str = "qcom,override-fuse-version-map";
	if (!of_find_property(of_node, prop_str, &len))
		return -EINVAL;

	tuple_size = 1;
	mem_acc_vreg->override_map_count = len / (sizeof(u32) * tuple_size);
	if (len == 0 || len % (sizeof(u32) * tuple_size)) {
		pr_err("%s length=%d is invalid\n", prop_str, len);
		return -EINVAL;
	}

	tmp = kzalloc(len, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, prop_str, tmp,
			mem_acc_vreg->override_map_count * tuple_size);
	if (rc) {
		pr_err("could not read %s rc=%d\n", prop_str, rc);
		goto done;
	}

	for (i = 0; i < mem_acc_vreg->override_map_count; i++) {
		if (tmp[i * tuple_size] != mem_acc_vreg->override_fuse_value
		    && tmp[i * tuple_size] != FUSE_PARAM_MATCH_ANY) {
			continue;
		} else {
			mem_acc_vreg->override_map_match = i;
			break;
		}
	}

	if (mem_acc_vreg->override_map_match != FUSE_MAP_NO_MATCH)
		pr_info("override_fuse_val=%d, %s tuple match found: %d\n",
			mem_acc_vreg->override_fuse_value, prop_str,
			mem_acc_vreg->override_map_match);
	else
		pr_err("%s tuple match not found\n", prop_str);

done:
	kfree(tmp);
	return rc;
}

static int mem_acc_parse_override_fuse_version_range(
			 struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	int i, j, rc, size, row_size;
	int num_fuse_sel, len = 0;
	u32 *tmp = NULL;
	char *prop_str;
	u32 *fuse_val, *fuse_sel;
	char *buf = NULL;
	int pos = 0, buflen;

	prop_str = "qcom,override-acc-range-fuse-list";
	if (!of_find_property(of_node, prop_str, &len)) {
		pr_err("%s property is missing\n", prop_str);
		return -EINVAL;
	}

	size = len / sizeof(u32);
	if (len == 0 || (size % FUSE_TUPLE_SIZE)) {
		pr_err("%s property length (%d) is invalid\n", prop_str, len);
		return -EINVAL;
	}

	num_fuse_sel = size / FUSE_TUPLE_SIZE;
	fuse_val = devm_kcalloc(mem_acc_vreg->dev, num_fuse_sel,
				sizeof(*fuse_val), GFP_KERNEL);
	if (!fuse_val)
		return -ENOMEM;
	mem_acc_vreg->override_acc_range_fuse_list = fuse_val;
	mem_acc_vreg->override_acc_range_fuse_num = num_fuse_sel;

	fuse_sel = kzalloc(len, GFP_KERNEL);
	if (!fuse_sel) {
		rc = -ENOMEM;
		goto done;
	}

	rc = of_property_read_u32_array(of_node, prop_str, fuse_sel,
					size);
	if (rc) {
		pr_err("%s read failed, rc=%d\n", prop_str, rc);
		goto done;
	}

	for (i = 0; i < num_fuse_sel; i++) {
		mem_acc_read_efuse_param(mem_acc_vreg, &fuse_sel[i * 4],
					 &fuse_val[i]);
	}

	prop_str = "qcom,override-fuse-range-map";
	if (!of_find_property(of_node, prop_str, &len))
		goto done;

	row_size = num_fuse_sel * 2;
	mem_acc_vreg->override_map_count = len / (sizeof(u32) * row_size);

	if (len == 0 || len % (sizeof(u32) * row_size)) {
		pr_err("%s length=%d is invalid\n", prop_str, len);
		rc = -EINVAL;
		goto done;
	}

	tmp = kzalloc(len, GFP_KERNEL);
	if (!tmp) {
		rc = -ENOMEM;
		goto done;
	}

	rc = of_property_read_u32_array(of_node, prop_str, tmp,
				mem_acc_vreg->override_map_count * row_size);
	if (rc) {
		pr_err("could not read %s rc=%d\n", prop_str, rc);
		goto done;
	}

	for (i = 0; i < mem_acc_vreg->override_map_count; i++) {
		for (j = 0; j < num_fuse_sel; j++) {
			if (tmp[i * row_size + j * 2] > fuse_val[j]
				|| tmp[i * row_size + j * 2 + 1] < fuse_val[j])
				break;
		}

		if (j == num_fuse_sel) {
			mem_acc_vreg->override_map_match = i;
			break;
		}
	}

	/*
	 * Log register and value mapping since they are useful for
	 * baseline MEM ACC logging.
	 */
	buflen = num_fuse_sel * sizeof("fuse_selxxxx = XXXX ");
	buf = kzalloc(buflen, GFP_KERNEL);
	if (!buf)
		goto done;

	for (j = 0; j < num_fuse_sel; j++)
		pos += scnprintf(buf + pos, buflen - pos, "fuse_sel%d = %d ",
				 j, fuse_val[j]);
	buf[pos] = '\0';
	if (mem_acc_vreg->override_map_match != FUSE_MAP_NO_MATCH)
		pr_info("%s %s tuple match found: %d\n", buf, prop_str,
			mem_acc_vreg->override_map_match);
	else
		pr_err("%s %s tuple match not found\n", buf, prop_str);

done:
	kfree(fuse_sel);
	kfree(tmp);
	kfree(buf);
	return rc;
}

#define MAX_CHARS_PER_INT	20

static int mem_acc_reg_addr_val_dump(struct mem_acc_regulator *mem_acc_vreg,
			struct corner_acc_reg_config *corner_acc_reg_config,
			u32 corner)
{
	int i, k, index, pos = 0;
	u32 addr_index;
	size_t buflen;
	char *buf;
	struct acc_reg_value *reg_config_list =
					corner_acc_reg_config->reg_config_list;
	int max_reg_config_len = corner_acc_reg_config->max_reg_config_len;
	int num_corners = mem_acc_vreg->num_corners;

	/*
	 * Log register and value mapping since they are useful for
	 * baseline MEM ACC logging.
	 */
	buflen = max_reg_config_len * (MAX_CHARS_PER_INT + 6) * sizeof(*buf);
	buf = kzalloc(buflen, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("Could not allocate memory for acc register and value logging\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_corners; i++) {
		if (corner == i + 1)
			continue;

		pr_debug("Corner: %d --> %d:\n", corner, i + 1);
		pos = 0;
		for (k = 0; k < max_reg_config_len; k++) {
			index = i * max_reg_config_len + k;
			addr_index = reg_config_list[index].addr_index;
			if (addr_index == PARAM_MATCH_ANY)
				break;

			pos += scnprintf(buf + pos, buflen - pos,
				"<0x%x 0x%x> ",
				mem_acc_vreg->phys_reg_addr_list[addr_index],
				reg_config_list[index].reg_val);
		}
		buf[pos] = '\0';
		pr_debug("%s\n", buf);
	}

	kfree(buf);
	return 0;
}

static int mem_acc_get_reg_addr_val(struct device_node *of_node,
		const char *prop_str, struct acc_reg_value *reg_config_list,
		int list_offset, int list_size, u32 max_reg_index)
{

	int i, index, rc  = 0;

	for (i = 0; i < list_size / 2; i++) {
		index = (list_offset * list_size) + i * 2;
		rc = of_property_read_u32_index(of_node, prop_str, index,
					&reg_config_list[i].addr_index);
		rc |= of_property_read_u32_index(of_node, prop_str, index + 1,
					&reg_config_list[i].reg_val);
		if (rc) {
			pr_err("could not read %s at tuple %u: rc=%d\n",
				prop_str, index, rc);
			return rc;
		}

		if (reg_config_list[i].addr_index == PARAM_MATCH_ANY)
			continue;

		if ((!reg_config_list[i].addr_index) ||
			reg_config_list[i].addr_index > max_reg_index) {
			pr_err("Invalid register index %u in %s at tuple %u\n",
				reg_config_list[i].addr_index, prop_str, index);
			return -EINVAL;
		}
	}

	return rc;
}

static int mem_acc_override_reg_addr_val_init(
			struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	struct corner_acc_reg_config *corner_acc_reg_config;
	struct acc_reg_value *override_reg_config_list;
	int i, tuple_count, tuple_match, len = 0, rc = 0;
	u32 list_size, override_max_reg_config_len;
	char prop_str[40];
	struct property *prop;
	int num_corners = mem_acc_vreg->num_corners;

	if (!mem_acc_vreg->corner_acc_reg_config)
		return 0;

	if (mem_acc_vreg->override_map_count) {
		if (mem_acc_vreg->override_map_match == FUSE_MAP_NO_MATCH)
			return 0;
		tuple_count = mem_acc_vreg->override_map_count;
		tuple_match = mem_acc_vreg->override_map_match;
	} else {
		tuple_count = 1;
		tuple_match = 0;
	}

	corner_acc_reg_config = mem_acc_vreg->corner_acc_reg_config;
	for (i = 1; i <= num_corners; i++) {
		snprintf(prop_str, sizeof(prop_str),
			 "qcom,override-corner%d-addr-val-map", i);
		prop = of_find_property(of_node, prop_str, &len);
		list_size = len / (tuple_count * sizeof(u32));
		if (!prop) {
			pr_debug("%s property not specified\n", prop_str);
			continue;
		}

		if ((!list_size) || list_size < (num_corners * 2)) {
			pr_err("qcom,override-corner%d-addr-val-map property is missed or invalid length: len=%d\n",
			i, len);
			return -EINVAL;
		}

		override_max_reg_config_len = list_size / (num_corners * 2);
		override_reg_config_list =
				corner_acc_reg_config[i].reg_config_list;

		if (corner_acc_reg_config[i].max_reg_config_len
					!= override_max_reg_config_len) {
			override_reg_config_list =
				devm_kcalloc(mem_acc_vreg->dev,
				override_max_reg_config_len * num_corners,
				sizeof(*override_reg_config_list), GFP_KERNEL);
			if (!override_reg_config_list)
				return -ENOMEM;

			corner_acc_reg_config[i].max_reg_config_len =
						override_max_reg_config_len;
			corner_acc_reg_config[i].reg_config_list =
						override_reg_config_list;
		}

		rc = mem_acc_get_reg_addr_val(of_node, prop_str,
					override_reg_config_list, tuple_match,
					list_size, mem_acc_vreg->num_acc_reg);
		if (rc) {
			pr_err("Failed to read %s property: rc=%d\n",
				prop_str, rc);
			return rc;
		}

		rc = mem_acc_reg_addr_val_dump(mem_acc_vreg,
						&corner_acc_reg_config[i], i);
		if (rc) {
			pr_err("could not dump acc address-value dump for corner=%d: rc=%d\n",
				i, rc);
			return rc;
		}
	}

	return rc;
}

static int mem_acc_parse_override_config(struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	int i, rc = 0;

	/* Specify default no match case. */
	mem_acc_vreg->override_map_match = FUSE_MAP_NO_MATCH;
	mem_acc_vreg->override_map_count = 0;

	if (of_find_property(of_node, "qcom,override-fuse-range-map",
			     NULL)) {
		rc = mem_acc_parse_override_fuse_version_range(mem_acc_vreg);
		if (rc) {
			pr_err("parsing qcom,override-fuse-range-map property failed, rc=%d\n",
				rc);
			return rc;
		}
	} else if (of_find_property(of_node, "qcom,override-fuse-version-map",
				    NULL)) {
		rc = mem_acc_parse_override_fuse_version_map(mem_acc_vreg);
		if (rc) {
			pr_err("parsing qcom,override-fuse-version-map property failed, rc=%d\n",
				rc);
			return rc;
		}
	} else {
		/* No override fuse configuration defined in device node */
		return 0;
	}

	if (mem_acc_vreg->override_map_match == FUSE_MAP_NO_MATCH)
		return 0;

	rc = mem_acc_override_corner_map(mem_acc_vreg);
	if (rc) {
		pr_err("Unable to override corner map rc=%d\n", rc);
		return rc;
	}

	rc = mem_acc_override_reg_addr_val_init(mem_acc_vreg);
	if (rc) {
		pr_err("Unable to override reg_config_list init rc=%d\n",
			rc);
		return rc;
	}

	for (i = 0; i < MEMORY_MAX; i++) {
		rc = override_mem_acc_custom_data(mem_acc_vreg, i);
		if (rc) {
			pr_err("Unable to override custom data for mem_type=%d rc=%d\n",
				i, rc);
			return rc;
		}
	}

	return rc;
}

static int mem_acc_init_reg_config(struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	int i, size, len = 0, rc = 0;
	u32 addr_index, reg_val, index;
	char *prop_str = "qcom,acc-init-reg-config";

	if (!of_find_property(of_node, prop_str, &len)) {
		/* Initial acc register configuration not specified */
		return rc;
	}

	size = len / sizeof(u32);
	if ((!size) || (size % 2)) {
		pr_err("%s specified with invalid length: %d\n",
			prop_str, size);
		return -EINVAL;
	}

	for (i = 0; i < size / 2; i++) {
		index = i * 2;
		rc = of_property_read_u32_index(of_node, prop_str, index,
						&addr_index);
		rc |= of_property_read_u32_index(of_node, prop_str, index + 1,
						&reg_val);
		if (rc) {
			pr_err("could not read %s at tuple %u: rc=%d\n",
				prop_str, index, rc);
			return rc;
		}

		if ((!addr_index) || addr_index > mem_acc_vreg->num_acc_reg) {
			pr_err("Invalid register index %u in %s at tuple %u\n",
				addr_index, prop_str, index);
			return -EINVAL;
		}

		writel_relaxed(reg_val,
				mem_acc_vreg->remap_reg_addr_list[addr_index]);
		/* make sure write complete */
		mb();

		pr_debug("acc initial config: register:0x%x value:0x%x\n",
			mem_acc_vreg->phys_reg_addr_list[addr_index], reg_val);
	}

	return rc;
}

static int mem_acc_get_reg_addr(struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	void __iomem **remap_reg_addr_list;
	u32 *phys_reg_addr_list;
	int i, num_acc_reg, len = 0, rc = 0;

	if (!of_find_property(of_node, "qcom,acc-reg-addr-list", &len)) {
		/* acc register address list not specified */
		return rc;
	}

	num_acc_reg = len / sizeof(u32);
	if (!num_acc_reg) {
		pr_err("qcom,acc-reg-addr-list has invalid len = %d\n", len);
		return -EINVAL;
	}

	phys_reg_addr_list = devm_kcalloc(mem_acc_vreg->dev, num_acc_reg + 1,
				sizeof(*phys_reg_addr_list), GFP_KERNEL);
	if (!phys_reg_addr_list)
		return -ENOMEM;

	remap_reg_addr_list = devm_kcalloc(mem_acc_vreg->dev, num_acc_reg + 1,
				sizeof(*remap_reg_addr_list), GFP_KERNEL);
	if (!remap_reg_addr_list)
		return -ENOMEM;

	rc = of_property_read_u32_array(of_node, "qcom,acc-reg-addr-list",
					&phys_reg_addr_list[1], num_acc_reg);
	if (rc) {
		pr_err("Read- qcom,acc-reg-addr-list failed: rc=%d\n", rc);
		return rc;
	}

	for (i = 1; i <= num_acc_reg; i++) {
		remap_reg_addr_list[i] = devm_ioremap(mem_acc_vreg->dev,
						phys_reg_addr_list[i], 0x4);
		if (!remap_reg_addr_list[i]) {
			pr_err("Unable to map register address 0x%x\n",
					phys_reg_addr_list[i]);
			return -EINVAL;
		}
	}

	mem_acc_vreg->num_acc_reg = num_acc_reg;
	mem_acc_vreg->phys_reg_addr_list = phys_reg_addr_list;
	mem_acc_vreg->remap_reg_addr_list = remap_reg_addr_list;

	return rc;
}

static int mem_acc_reg_config_init(struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = mem_acc_vreg->dev->of_node;
	struct acc_reg_value *reg_config_list;
	int len, size, rc, i, num_corners;
	struct property *prop;
	char prop_str[30];
	struct corner_acc_reg_config *corner_acc_reg_config;

	rc = of_property_read_u32(of_node, "qcom,num-acc-corners",
				&num_corners);
	if (rc) {
		pr_err("could not read qcom,num-acc-corners: rc=%d\n", rc);
		return rc;
	}

	mem_acc_vreg->num_corners = num_corners;

	rc = of_property_read_u32(of_node, "qcom,boot-acc-corner",
				&mem_acc_vreg->corner);
	if (rc) {
		pr_err("could not read qcom,boot-acc-corner: rc=%d\n", rc);
		return rc;
	}
	pr_debug("boot acc corner = %d\n", mem_acc_vreg->corner);

	corner_acc_reg_config = devm_kcalloc(mem_acc_vreg->dev, num_corners + 1,
						sizeof(*corner_acc_reg_config),
						GFP_KERNEL);
	if (!corner_acc_reg_config)
		return -ENOMEM;

	for (i = 1; i <= num_corners; i++) {
		snprintf(prop_str, sizeof(prop_str),
				"qcom,corner%d-reg-config", i);
		prop = of_find_property(of_node, prop_str, &len);
		size = len / sizeof(u32);
		if ((!prop) || (!size) || size < (num_corners * 2)) {
			pr_err("%s property is missed or invalid length: len=%d\n",
				prop_str, len);
			return -EINVAL;
		}

		reg_config_list = devm_kcalloc(mem_acc_vreg->dev, size / 2,
					sizeof(*reg_config_list), GFP_KERNEL);
		if (!reg_config_list)
			return -ENOMEM;

		rc = mem_acc_get_reg_addr_val(of_node, prop_str,
						reg_config_list, 0, size,
						mem_acc_vreg->num_acc_reg);
		if (rc) {
			pr_err("Failed to read %s property: rc=%d\n",
				prop_str, rc);
			return rc;
		}

		corner_acc_reg_config[i].max_reg_config_len =
						size / (num_corners * 2);
		corner_acc_reg_config[i].reg_config_list = reg_config_list;

		rc = mem_acc_reg_addr_val_dump(mem_acc_vreg,
						&corner_acc_reg_config[i], i);
		if (rc) {
			pr_err("could not dump acc address-value dump for corner=%d: rc=%d\n",
				i, rc);
			return rc;
		}
	}

	mem_acc_vreg->corner_acc_reg_config = corner_acc_reg_config;
	mem_acc_vreg->flags |= MEM_ACC_USE_ADDR_VAL_MAP;
	return rc;
}

#define MEM_TYPE_STRING_LEN	20
static int mem_acc_init(struct platform_device *pdev,
		struct mem_acc_regulator *mem_acc_vreg)
{
	struct device_node *of_node = pdev->dev.of_node;
	struct resource *res;
	int len, rc, i, j;
	bool acc_type_present = false;
	char tmps[MEM_TYPE_STRING_LEN];

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "acc-en");
	if (!res || !res->start) {
		pr_debug("'acc-en' resource missing or not used.\n");
	} else {
		mem_acc_vreg->acc_en_addr = res->start;
		len = resource_size(res);
		pr_debug("'acc_en_addr' = %pa (len=0x%x)\n", &res->start, len);

		mem_acc_vreg->acc_en_base = devm_ioremap(mem_acc_vreg->dev,
				mem_acc_vreg->acc_en_addr, len);
		if (!mem_acc_vreg->acc_en_base) {
			pr_err("Unable to map 'acc_en_addr' %pa\n",
					&mem_acc_vreg->acc_en_addr);
			return -EINVAL;
		}

		rc = populate_acc_data(mem_acc_vreg, "qcom,acc-en-bit-pos",
				&mem_acc_vreg->acc_en_bit_pos,
				&mem_acc_vreg->num_acc_en);
		if (rc) {
			pr_err("Unable to populate 'qcom,acc-en-bit-pos' rc=%d\n",
					rc);
			return rc;
		}
	}

	rc = mem_acc_efuse_init(pdev, mem_acc_vreg);
	if (rc) {
		pr_err("Wrong eFuse address specified: rc=%d\n", rc);
		return rc;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "acc-sel-l1");
	if (!res || !res->start) {
		pr_debug("'acc-sel-l1' resource missing or not used.\n");
	} else {
		rc = mem_acc_sel_setup(mem_acc_vreg, res, MEMORY_L1);
		if (rc) {
			pr_err("Unable to setup mem-acc for mem_type=%d rc=%d\n",
					MEMORY_L1, rc);
			return rc;
		}
		mem_acc_vreg->mem_acc_supported[MEMORY_L1] = true;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "acc-sel-l2");
	if (!res || !res->start) {
		pr_debug("'acc-sel-l2' resource missing or not used.\n");
	} else {
		rc = mem_acc_sel_setup(mem_acc_vreg, res, MEMORY_L2);
		if (rc) {
			pr_err("Unable to setup mem-acc for mem_type=%d rc=%d\n",
					MEMORY_L2, rc);
			return rc;
		}
		mem_acc_vreg->mem_acc_supported[MEMORY_L2] = true;
	}

	for (i = 0; i < MEM_ACC_TYPE_MAX; i++) {
		snprintf(tmps, MEM_TYPE_STRING_LEN, "mem-acc-type%d", i + 1);
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, tmps);

		if (!res || !res->start) {
			pr_debug("'%s' resource missing or not used.\n", tmps);
		} else {
			mem_acc_vreg->mem_acc_type_addr[i] = res->start;
			acc_type_present = true;
		}
	}

	rc = mem_acc_get_reg_addr(mem_acc_vreg);
	if (rc) {
		pr_err("Unable to get acc register addresses: rc=%d\n", rc);
		return rc;
	}

	if (mem_acc_vreg->phys_reg_addr_list) {
		rc = mem_acc_reg_config_init(mem_acc_vreg);
		if (rc) {
			pr_err("acc register address-value map failed: rc=%d\n",
				rc);
			return rc;
		}
	}

	if (of_find_property(of_node, "qcom,corner-acc-map", NULL)) {
		rc = populate_acc_data(mem_acc_vreg, "qcom,corner-acc-map",
			&mem_acc_vreg->corner_acc_map,
			&mem_acc_vreg->num_corners);

		/* Check if at least one valid mem-acc config. is specified */
		for (i = 0; i < MEMORY_MAX; i++) {
			if (mem_acc_vreg->mem_acc_supported[i])
				break;
		}
		if (i == MEMORY_MAX && !acc_type_present) {
			pr_err("No mem-acc configuration specified\n");
			return -EINVAL;
		}

		mem_acc_vreg->flags |= MEM_ACC_USE_CORNER_ACC_MAP;
	}

	if ((mem_acc_vreg->flags & MEM_ACC_USE_CORNER_ACC_MAP) &&
		(mem_acc_vreg->flags & MEM_ACC_USE_ADDR_VAL_MAP)) {
		pr_err("Invalid configuration, both qcom,corner-acc-map and qcom,cornerX-addr-val-map specified\n");
		return -EINVAL;
	}

	pr_debug("num_corners = %d\n", mem_acc_vreg->num_corners);

	if (mem_acc_vreg->num_acc_en)
		mem_acc_en_init(mem_acc_vreg);

	if (mem_acc_vreg->phys_reg_addr_list) {
		rc = mem_acc_init_reg_config(mem_acc_vreg);
		if (rc) {
			pr_err("acc initial register configuration failed: rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = mem_acc_sel_init(mem_acc_vreg);
	if (rc) {
		pr_err("Unable to initialize mem_acc_sel reg rc=%d\n", rc);
		return rc;
	}

	for (i = 0; i < MEMORY_MAX; i++) {
		rc = mem_acc_custom_data_init(pdev, mem_acc_vreg, i);
		if (rc) {
			pr_err("Unable to initialize custom data for mem_type=%d rc=%d\n",
					i, rc);
			return rc;
		}
	}

	rc = mem_acc_parse_override_config(mem_acc_vreg);
	if (rc) {
		pr_err("Unable to parse mem acc override configuration, rc=%d\n",
			rc);
		return rc;
	}
	if (acc_type_present) {
		mem_acc_vreg->mem_acc_type_data = devm_kzalloc(
			mem_acc_vreg->dev, mem_acc_vreg->num_corners *
			MEM_ACC_TYPE_MAX * sizeof(u32), GFP_KERNEL);

		if (!mem_acc_vreg->mem_acc_type_data) {
			pr_err("Unable to allocate memory for mem_acc_type\n");
			return -ENOMEM;
		}

		for (i = 0; i < MEM_ACC_TYPE_MAX; i++) {
			if (mem_acc_vreg->mem_acc_type_addr[i]) {
				snprintf(tmps, MEM_TYPE_STRING_LEN,
					"qcom,mem-acc-type%d", i + 1);

				j = i * mem_acc_vreg->num_corners;
				rc = of_property_read_u32_array(
					mem_acc_vreg->dev->of_node,
					tmps,
					&mem_acc_vreg->mem_acc_type_data[j],
					mem_acc_vreg->num_corners);
				if (rc) {
					pr_err("Unable to get property %s rc=%d\n",
						tmps, rc);
					return rc;
				}
			}
		}
	}

	return 0;
}

static int mem_acc_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config reg_config = {};
	struct mem_acc_regulator *mem_acc_vreg;
	struct regulator_desc *rdesc;
	struct regulator_init_data *init_data;
	int rc;

	if (!pdev->dev.of_node) {
		pr_err("Device tree node is missing\n");
		return -EINVAL;
	}

	init_data = of_get_regulator_init_data(&pdev->dev, pdev->dev.of_node,
					NULL);
	if (!init_data) {
		pr_err("regulator init data is missing\n");
		return -EINVAL;
	}

	init_data->constraints.input_uV = init_data->constraints.max_uV;
	init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE;

	mem_acc_vreg = devm_kzalloc(&pdev->dev, sizeof(*mem_acc_vreg),
			GFP_KERNEL);
	if (!mem_acc_vreg)
		return -ENOMEM;

	mem_acc_vreg->dev = &pdev->dev;

	rc = mem_acc_init(pdev, mem_acc_vreg);
	if (rc) {
		pr_err("Unable to initialize mem_acc configuration rc=%d\n",
				rc);
		return rc;
	}

	rdesc			= &mem_acc_vreg->rdesc;
	rdesc->owner		= THIS_MODULE;
	rdesc->type		= REGULATOR_VOLTAGE;
	rdesc->ops		= &mem_acc_corner_ops;
	rdesc->name		= init_data->constraints.name;

	reg_config.dev = &pdev->dev;
	reg_config.init_data = init_data;
	reg_config.driver_data = mem_acc_vreg;
	reg_config.of_node = pdev->dev.of_node;
	mem_acc_vreg->rdev = regulator_register(&pdev->dev, rdesc, &reg_config);
	if (IS_ERR(mem_acc_vreg->rdev)) {
		rc = PTR_ERR(mem_acc_vreg->rdev);
		if (rc != -EPROBE_DEFER)
			pr_err("regulator_register failed: rc=%d\n", rc);
		return rc;
	}

	platform_set_drvdata(pdev, mem_acc_vreg);

	rc = devm_regulator_debug_register(&pdev->dev, mem_acc_vreg->rdev);
	if (rc)
		pr_err("Failed to register debug regulator, rc=%d\n", rc);

	return 0;
}

static int mem_acc_regulator_remove(struct platform_device *pdev)
{
	struct mem_acc_regulator *mem_acc_vreg = platform_get_drvdata(pdev);

	regulator_unregister(mem_acc_vreg->rdev);

	return 0;
}

static const struct of_device_id mem_acc_regulator_match_table[] = {
	{ .compatible = "qcom,mem-acc-regulator", },
	{}
};

static struct platform_driver mem_acc_regulator_driver = {
	.probe		= mem_acc_regulator_probe,
	.remove		= mem_acc_regulator_remove,
	.driver		= {
		.name		= "qcom,mem-acc-regulator",
		.of_match_table = mem_acc_regulator_match_table,

	},
};

int __init mem_acc_regulator_init(void)
{
	return platform_driver_register(&mem_acc_regulator_driver);
}
postcore_initcall(mem_acc_regulator_init);

static void __exit mem_acc_regulator_exit(void)
{
	platform_driver_unregister(&mem_acc_regulator_driver);
}
module_exit(mem_acc_regulator_exit);

MODULE_DESCRIPTION("MEM-ACC-SEL regulator driver");
MODULE_LICENSE("GPL");
