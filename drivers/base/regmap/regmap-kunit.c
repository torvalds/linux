// SPDX-License-Identifier: GPL-2.0
//
// regmap KUnit tests
//
// Copyright 2023 Arm Ltd

#include <kunit/device.h>
#include <kunit/resource.h>
#include <kunit/test.h>
#include "internal.h"

#define BLOCK_TEST_SIZE 12

KUNIT_DEFINE_ACTION_WRAPPER(regmap_exit_action, regmap_exit, struct regmap *);

struct regmap_test_priv {
	struct device *dev;
};

struct regmap_test_param {
	enum regcache_type cache;
	enum regmap_endian val_endian;

	unsigned int from_reg;
	bool fast_io;
};

static void get_changed_bytes(void *orig, void *new, size_t size)
{
	char *o = orig;
	char *n = new;
	int i;

	get_random_bytes(new, size);

	/*
	 * This could be nicer and more efficient but we shouldn't
	 * super care.
	 */
	for (i = 0; i < size; i++)
		while (n[i] == o[i])
			get_random_bytes(&n[i], 1);
}

static const struct regmap_config test_regmap_config = {
	.reg_stride = 1,
	.val_bits = sizeof(unsigned int) * 8,
};

static const char *regcache_type_name(enum regcache_type type)
{
	switch (type) {
	case REGCACHE_NONE:
		return "none";
	case REGCACHE_FLAT:
		return "flat";
	case REGCACHE_RBTREE:
		return "rbtree";
	case REGCACHE_MAPLE:
		return "maple";
	default:
		return NULL;
	}
}

static const char *regmap_endian_name(enum regmap_endian endian)
{
	switch (endian) {
	case REGMAP_ENDIAN_BIG:
		return "big";
	case REGMAP_ENDIAN_LITTLE:
		return "little";
	case REGMAP_ENDIAN_DEFAULT:
		return "default";
	case REGMAP_ENDIAN_NATIVE:
		return "native";
	default:
		return NULL;
	}
}

static void param_to_desc(const struct regmap_test_param *param, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "%s-%s%s @%#x",
		 regcache_type_name(param->cache),
		 regmap_endian_name(param->val_endian),
		 param->fast_io ? " fast I/O" : "",
		 param->from_reg);
}

static const struct regmap_test_param regcache_types_list[] = {
	{ .cache = REGCACHE_NONE },
	{ .cache = REGCACHE_NONE, .fast_io = true },
	{ .cache = REGCACHE_FLAT },
	{ .cache = REGCACHE_FLAT, .fast_io = true },
	{ .cache = REGCACHE_RBTREE },
	{ .cache = REGCACHE_RBTREE, .fast_io = true },
	{ .cache = REGCACHE_MAPLE },
	{ .cache = REGCACHE_MAPLE, .fast_io = true },
};

KUNIT_ARRAY_PARAM(regcache_types, regcache_types_list, param_to_desc);

static const struct regmap_test_param real_cache_types_only_list[] = {
	{ .cache = REGCACHE_FLAT },
	{ .cache = REGCACHE_FLAT, .fast_io = true },
	{ .cache = REGCACHE_RBTREE },
	{ .cache = REGCACHE_RBTREE, .fast_io = true },
	{ .cache = REGCACHE_MAPLE },
	{ .cache = REGCACHE_MAPLE, .fast_io = true },
};

KUNIT_ARRAY_PARAM(real_cache_types_only, real_cache_types_only_list, param_to_desc);

static const struct regmap_test_param real_cache_types_list[] = {
	{ .cache = REGCACHE_FLAT,   .from_reg = 0 },
	{ .cache = REGCACHE_FLAT,   .from_reg = 0, .fast_io = true },
	{ .cache = REGCACHE_FLAT,   .from_reg = 0x2001 },
	{ .cache = REGCACHE_FLAT,   .from_reg = 0x2002 },
	{ .cache = REGCACHE_FLAT,   .from_reg = 0x2003 },
	{ .cache = REGCACHE_FLAT,   .from_reg = 0x2004 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0, .fast_io = true },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2001 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2002 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2003 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2004 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0, .fast_io = true },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2001 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2002 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2003 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2004 },
};

KUNIT_ARRAY_PARAM(real_cache_types, real_cache_types_list, param_to_desc);

static const struct regmap_test_param sparse_cache_types_list[] = {
	{ .cache = REGCACHE_RBTREE, .from_reg = 0 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0, .fast_io = true },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2001 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2002 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2003 },
	{ .cache = REGCACHE_RBTREE, .from_reg = 0x2004 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0, .fast_io = true },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2001 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2002 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2003 },
	{ .cache = REGCACHE_MAPLE,  .from_reg = 0x2004 },
};

KUNIT_ARRAY_PARAM(sparse_cache_types, sparse_cache_types_list, param_to_desc);

static struct regmap *gen_regmap(struct kunit *test,
				 struct regmap_config *config,
				 struct regmap_ram_data **data)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap_test_priv *priv = test->priv;
	unsigned int *buf;
	struct regmap *ret = ERR_PTR(-ENOMEM);
	size_t size;
	int i, error;
	struct reg_default *defaults;

	config->cache_type = param->cache;
	config->fast_io = param->fast_io;

	if (config->max_register == 0) {
		config->max_register = param->from_reg;
		if (config->num_reg_defaults)
			config->max_register += (config->num_reg_defaults - 1) *
						config->reg_stride;
		else
			config->max_register += (BLOCK_TEST_SIZE * config->reg_stride);
	}

	size = array_size(config->max_register + 1, sizeof(*buf));
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	get_random_bytes(buf, size);

	*data = kzalloc(sizeof(**data), GFP_KERNEL);
	if (!(*data))
		goto out_free;
	(*data)->vals = buf;

	if (config->num_reg_defaults) {
		defaults = kunit_kcalloc(test,
					 config->num_reg_defaults,
					 sizeof(struct reg_default),
					 GFP_KERNEL);
		if (!defaults)
			goto out_free;

		config->reg_defaults = defaults;

		for (i = 0; i < config->num_reg_defaults; i++) {
			defaults[i].reg = param->from_reg + (i * config->reg_stride);
			defaults[i].def = buf[param->from_reg + (i * config->reg_stride)];
		}
	}

	ret = regmap_init_ram(priv->dev, config, *data);
	if (IS_ERR(ret))
		goto out_free;

	/* This calls regmap_exit() on failure, which frees buf and *data */
	error = kunit_add_action_or_reset(test, regmap_exit_action, ret);
	if (error)
		ret = ERR_PTR(error);

	return ret;

out_free:
	kfree(buf);
	kfree(*data);

	return ret;
}

static bool reg_5_false(struct device *dev, unsigned int reg)
{
	struct kunit *test = dev_get_drvdata(dev);
	const struct regmap_test_param *param = test->param_value;

	return reg != (param->from_reg + 5);
}

static void basic_read_write(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val, rval;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* If we write a value to a register we can read it back */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, 0, val));
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, 0, &rval));
	KUNIT_EXPECT_EQ(test, val, rval);

	/* If using a cache the cache satisfied the read */
	KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[0]);
}

static void bulk_write(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE], rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/*
	 * Data written via the bulk API can be read back with single
	 * reads.
	 */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, 0, val,
						   BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval[i]));

	KUNIT_EXPECT_MEMEQ(test, val, rval, sizeof(val));

	/* If using a cache the cache satisfied the read */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void bulk_read(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE], rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* Data written as single writes can be read via the bulk API */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, val[i]));
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, 0, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, val, rval, sizeof(val));

	/* If using a cache the cache satisfied the read */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void multi_write(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	struct reg_sequence sequence[BLOCK_TEST_SIZE];
	unsigned int val[BLOCK_TEST_SIZE], rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/*
	 * Data written via the multi API can be read back with single
	 * reads.
	 */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		sequence[i].reg = i;
		sequence[i].def = val[i];
		sequence[i].delay_us = 0;
	}
	KUNIT_EXPECT_EQ(test, 0,
			regmap_multi_reg_write(map, sequence, BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval[i]));

	KUNIT_EXPECT_MEMEQ(test, val, rval, sizeof(val));

	/* If using a cache the cache satisfied the read */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void multi_read(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int regs[BLOCK_TEST_SIZE];
	unsigned int val[BLOCK_TEST_SIZE], rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* Data written as single writes can be read via the multi API */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		regs[i] = i;
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, val[i]));
	}
	KUNIT_EXPECT_EQ(test, 0,
			regmap_multi_reg_read(map, regs, rval, BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, val, rval, sizeof(val));

	/* If using a cache the cache satisfied the read */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void read_bypassed(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE], rval;
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	KUNIT_EXPECT_FALSE(test, map->cache_bypass);

	get_random_bytes(&val, sizeof(val));

	/* Write some test values */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, param->from_reg, val, ARRAY_SIZE(val)));

	regcache_cache_only(map, true);

	/*
	 * While in cache-only regmap_read_bypassed() should return the register
	 * value and leave the map in cache-only.
	 */
	for (i = 0; i < ARRAY_SIZE(val); i++) {
		/* Put inverted bits in rval to prove we really read the value */
		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_EQ(test, val[i], rval);

		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read_bypassed(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_EQ(test, val[i], rval);
		KUNIT_EXPECT_TRUE(test, map->cache_only);
		KUNIT_EXPECT_FALSE(test, map->cache_bypass);
	}

	/*
	 * Change the underlying register values to prove it is returning
	 * real values not cached values.
	 */
	for (i = 0; i < ARRAY_SIZE(val); i++) {
		val[i] = ~val[i];
		data->vals[param->from_reg + i] = val[i];
	}

	for (i = 0; i < ARRAY_SIZE(val); i++) {
		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_NE(test, val[i], rval);

		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read_bypassed(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_EQ(test, val[i], rval);
		KUNIT_EXPECT_TRUE(test, map->cache_only);
		KUNIT_EXPECT_FALSE(test, map->cache_bypass);
	}
}

static void read_bypassed_volatile(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE], rval;
	int i;

	config = test_regmap_config;
	/* All registers except #5 volatile */
	config.volatile_reg = reg_5_false;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	KUNIT_EXPECT_FALSE(test, map->cache_bypass);

	get_random_bytes(&val, sizeof(val));

	/* Write some test values */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, param->from_reg, val, ARRAY_SIZE(val)));

	regcache_cache_only(map, true);

	/*
	 * While in cache-only regmap_read_bypassed() should return the register
	 * value and leave the map in cache-only.
	 */
	for (i = 0; i < ARRAY_SIZE(val); i++) {
		/* Register #5 is non-volatile so should read from cache */
		KUNIT_EXPECT_EQ(test, (i == 5) ? 0 : -EBUSY,
				regmap_read(map, param->from_reg + i, &rval));

		/* Put inverted bits in rval to prove we really read the value */
		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read_bypassed(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_EQ(test, val[i], rval);
		KUNIT_EXPECT_TRUE(test, map->cache_only);
		KUNIT_EXPECT_FALSE(test, map->cache_bypass);
	}

	/*
	 * Change the underlying register values to prove it is returning
	 * real values not cached values.
	 */
	for (i = 0; i < ARRAY_SIZE(val); i++) {
		val[i] = ~val[i];
		data->vals[param->from_reg + i] = val[i];
	}

	for (i = 0; i < ARRAY_SIZE(val); i++) {
		if (i == 5)
			continue;

		rval = ~val[i];
		KUNIT_EXPECT_EQ(test, 0, regmap_read_bypassed(map, param->from_reg + i, &rval));
		KUNIT_EXPECT_EQ(test, val[i], rval);
		KUNIT_EXPECT_TRUE(test, map->cache_only);
		KUNIT_EXPECT_FALSE(test, map->cache_bypass);
	}
}

static void write_readonly(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;
	config.writeable_reg = reg_5_false;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[i] = false;

	/* Change the value of all registers, readonly should fail */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, i != 5, regmap_write(map, i, val) == 0);

	/* Did that match what we see on the device? */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, i != 5, data->written[i]);
}

static void read_writeonly(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.readable_reg = reg_5_false;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[i] = false;

	/*
	 * Try to read all the registers, the writeonly one should
	 * fail if we aren't using the flat cache.
	 */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		if (config.cache_type != REGCACHE_FLAT) {
			KUNIT_EXPECT_EQ(test, i != 5,
					regmap_read(map, i, &val) == 0);
		} else {
			KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &val));
		}
	}

	/* Did we trigger a hardware access? */
	KUNIT_EXPECT_FALSE(test, data->read[5]);
}

static void reg_defaults(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Read back the expected default data */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, 0, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, data->vals, rval, sizeof(rval));

	/* The data should have been read from cache if there was one */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void reg_defaults_read_dev(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;
	config.num_reg_defaults_raw = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* We should have read the cache defaults back from the map */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		KUNIT_EXPECT_EQ(test, config.cache_type != REGCACHE_NONE, data->read[i]);
		data->read[i] = false;
	}

	/* Read back the expected default data */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, 0, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, data->vals, rval, sizeof(rval));

	/* The data should have been read from cache if there was one */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
}

static void register_patch(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	struct reg_sequence patch[2];
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	/* We need defaults so readback works */
	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Stash the original values */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, 0, rval,
						  BLOCK_TEST_SIZE));

	/* Patch a couple of values */
	patch[0].reg = 2;
	patch[0].def = rval[2] + 1;
	patch[0].delay_us = 0;
	patch[1].reg = 5;
	patch[1].def = rval[5] + 1;
	patch[1].delay_us = 0;
	KUNIT_EXPECT_EQ(test, 0, regmap_register_patch(map, patch,
						       ARRAY_SIZE(patch)));

	/* Only the patched registers are written */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		switch (i) {
		case 2:
		case 5:
			KUNIT_EXPECT_TRUE(test, data->written[i]);
			KUNIT_EXPECT_EQ(test, data->vals[i], rval[i] + 1);
			break;
		default:
			KUNIT_EXPECT_FALSE(test, data->written[i]);
			KUNIT_EXPECT_EQ(test, data->vals[i], rval[i]);
			break;
		}
	}
}

static void stride(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval;
	int i;

	config = test_regmap_config;
	config.reg_stride = 2;
	config.num_reg_defaults = BLOCK_TEST_SIZE / 2;

	/*
	 * Allow one extra register so that the read/written arrays
	 * are sized big enough to include an entry for the odd
	 * address past the final reg_default register.
	 */
	config.max_register = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Only even addresses can be accessed, try both read and write */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		data->read[i] = false;
		data->written[i] = false;

		if (i % 2) {
			KUNIT_EXPECT_NE(test, 0, regmap_read(map, i, &rval));
			KUNIT_EXPECT_NE(test, 0, regmap_write(map, i, rval));
			KUNIT_EXPECT_FALSE(test, data->read[i]);
			KUNIT_EXPECT_FALSE(test, data->written[i]);
		} else {
			KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval));
			KUNIT_EXPECT_EQ(test, data->vals[i], rval);
			KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE,
					data->read[i]);

			KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, rval));
			KUNIT_EXPECT_TRUE(test, data->written[i]);
		}
	}
}

static struct regmap_range_cfg test_range = {
	.selector_reg = 1,
	.selector_mask = 0xff,

	.window_start = 4,
	.window_len = 10,

	.range_min = 20,
	.range_max = 40,
};

static bool test_range_window_volatile(struct device *dev, unsigned int reg)
{
	if (reg >= test_range.window_start &&
	    reg <= test_range.window_start + test_range.window_len)
		return true;

	return false;
}

static bool test_range_all_volatile(struct device *dev, unsigned int reg)
{
	if (test_range_window_volatile(dev, reg))
		return true;

	if (reg >= test_range.range_min && reg <= test_range.range_max)
		return true;

	return false;
}

static void basic_ranges(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.volatile_reg = test_range_all_volatile;
	config.ranges = &test_range;
	config.num_ranges = 1;
	config.max_register = test_range.range_max;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	for (i = test_range.range_min; i < test_range.range_max; i++) {
		data->read[i] = false;
		data->written[i] = false;
	}

	/* Reset the page to a non-zero value to trigger a change */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, test_range.selector_reg,
					      test_range.range_max));

	/* Check we set the page and use the window for writes */
	data->written[test_range.selector_reg] = false;
	data->written[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, test_range.range_min, 0));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->written[test_range.window_start]);

	data->written[test_range.selector_reg] = false;
	data->written[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map,
					      test_range.range_min +
					      test_range.window_len,
					      0));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->written[test_range.window_start]);

	/* Same for reads */
	data->written[test_range.selector_reg] = false;
	data->read[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, test_range.range_min, &val));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->read[test_range.window_start]);

	data->written[test_range.selector_reg] = false;
	data->read[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map,
					     test_range.range_min +
					     test_range.window_len,
					     &val));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->read[test_range.window_start]);

	/* No physical access triggered in the virtual range */
	for (i = test_range.range_min; i < test_range.range_max; i++) {
		KUNIT_EXPECT_FALSE(test, data->read[i]);
		KUNIT_EXPECT_FALSE(test, data->written[i]);
	}
}

/* Try to stress dynamic creation of cache data structures */
static void stress_insert(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval, *vals;
	size_t buf_sz;
	int i;

	config = test_regmap_config;
	config.max_register = 300;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	buf_sz = array_size(sizeof(*vals), config.max_register);
	vals = kunit_kmalloc(test, buf_sz, GFP_KERNEL);
	KUNIT_ASSERT_FALSE(test, vals == NULL);

	get_random_bytes(vals, buf_sz);

	/* Write data into the map/cache in ever decreasing strides */
	for (i = 0; i < config.max_register; i += 100)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 50)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 25)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 10)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 5)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 3)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i += 2)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));
	for (i = 0; i < config.max_register; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, i, vals[i]));

	/* Do reads from the cache (if there is one) match? */
	for (i = 0; i < config.max_register; i ++) {
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval));
		KUNIT_EXPECT_EQ(test, rval, vals[i]);
		KUNIT_EXPECT_EQ(test, config.cache_type == REGCACHE_NONE, data->read[i]);
	}
}

static void cache_bypass(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val, rval;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* Ensure the cache has a value in it */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg, val));

	/* Bypass then write a different value */
	regcache_cache_bypass(map, true);
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg, val + 1));

	/* Read the bypassed value */
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg, &rval));
	KUNIT_EXPECT_EQ(test, val + 1, rval);
	KUNIT_EXPECT_EQ(test, data->vals[param->from_reg], rval);

	/* Disable bypass, the cache should still return the original value */
	regcache_cache_bypass(map, false);
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg, &rval));
	KUNIT_EXPECT_EQ(test, val, rval);
}

static void cache_sync_marked_dirty(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* Put some data into the cache */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, param->from_reg, val,
						   BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	/* Trash the data on the device itself then resync */
	regcache_mark_dirty(map);
	memset(data->vals, 0, sizeof(val));
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Did we just write the correct data out? */
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], val, sizeof(val));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, true, data->written[param->from_reg + i]);
}

static void cache_sync_after_cache_only(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[BLOCK_TEST_SIZE];
	unsigned int val_mask;
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	val_mask = GENMASK(config.val_bits - 1, 0);
	get_random_bytes(&val, sizeof(val));

	/* Put some data into the cache */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, param->from_reg, val,
						   BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	/* Set cache-only and change the values */
	regcache_cache_only(map, true);
	for (i = 0; i < ARRAY_SIZE(val); ++i)
		val[i] = ~val[i] & val_mask;

	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, param->from_reg, val,
						   BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_FALSE(test, data->written[param->from_reg + i]);

	KUNIT_EXPECT_MEMNEQ(test, &data->vals[param->from_reg], val, sizeof(val));

	/* Exit cache-only and sync the cache without marking hardware registers dirty */
	regcache_cache_only(map, false);

	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Did we just write the correct data out? */
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], val, sizeof(val));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_TRUE(test, data->written[param->from_reg + i]);
}

static void cache_sync_defaults_marked_dirty(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* Change the value of one register */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + 2, val));

	/* Resync */
	regcache_mark_dirty(map);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Did we just sync the one register we touched? */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, i == 2, data->written[param->from_reg + i]);

	/* Rewrite registers back to their defaults */
	for (i = 0; i < config.num_reg_defaults; ++i)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, config.reg_defaults[i].reg,
						      config.reg_defaults[i].def));

	/*
	 * Resync after regcache_mark_dirty() should not write out registers
	 * that are at default value
	 */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;
	regcache_mark_dirty(map);
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_FALSE(test, data->written[param->from_reg + i]);
}

static void cache_sync_default_after_cache_only(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int orig_val;
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + 2, &orig_val));

	/* Enter cache-only and change the value of one register */
	regcache_cache_only(map, true);
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + 2, orig_val + 1));

	/* Exit cache-only and resync, should write out the changed register */
	regcache_cache_only(map, false);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Was the register written out? */
	KUNIT_EXPECT_TRUE(test, data->written[param->from_reg + 2]);
	KUNIT_EXPECT_EQ(test, data->vals[param->from_reg + 2], orig_val + 1);

	/* Enter cache-only and write register back to its default value */
	regcache_cache_only(map, true);
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + 2, orig_val));

	/* Resync should write out the new value */
	regcache_cache_only(map, false);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));
	KUNIT_EXPECT_TRUE(test, data->written[param->from_reg + 2]);
	KUNIT_EXPECT_EQ(test, data->vals[param->from_reg + 2], orig_val);
}

static void cache_sync_readonly(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.writeable_reg = reg_5_false;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Read all registers to fill the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + i, &val));

	/* Change the value of all registers, readonly should fail */
	get_random_bytes(&val, sizeof(val));
	regcache_cache_only(map, true);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, i != 5, regmap_write(map, param->from_reg + i, val) == 0);
	regcache_cache_only(map, false);

	/* Resync */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Did that match what we see on the device? */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, i != 5, data->written[param->from_reg + i]);
}

static void cache_sync_patch(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	struct reg_sequence patch[2];
	unsigned int rval[BLOCK_TEST_SIZE], val;
	int i;

	/* We need defaults so readback works */
	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Stash the original values */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));

	/* Patch a couple of values */
	patch[0].reg = param->from_reg + 2;
	patch[0].def = rval[2] + 1;
	patch[0].delay_us = 0;
	patch[1].reg = param->from_reg + 5;
	patch[1].def = rval[5] + 1;
	patch[1].delay_us = 0;
	KUNIT_EXPECT_EQ(test, 0, regmap_register_patch(map, patch,
						       ARRAY_SIZE(patch)));

	/* Sync the cache */
	regcache_mark_dirty(map);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* The patch should be on the device but not in the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + i, &val));
		KUNIT_EXPECT_EQ(test, val, rval[i]);

		switch (i) {
		case 2:
		case 5:
			KUNIT_EXPECT_EQ(test, true, data->written[param->from_reg + i]);
			KUNIT_EXPECT_EQ(test, data->vals[param->from_reg + i], rval[i] + 1);
			break;
		default:
			KUNIT_EXPECT_EQ(test, false, data->written[param->from_reg + i]);
			KUNIT_EXPECT_EQ(test, data->vals[param->from_reg + i], rval[i]);
			break;
		}
	}
}

static void cache_drop(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Ensure the data is read from the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++) {
		KUNIT_EXPECT_FALSE(test, data->read[param->from_reg + i]);
		data->read[param->from_reg + i] = false;
	}
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], rval, sizeof(rval));

	/* Drop some registers */
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, param->from_reg + 3,
						      param->from_reg + 5));

	/* Reread and check only the dropped registers hit the device. */
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, data->read[param->from_reg + i], i >= 3 && i <= 5);
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], rval, sizeof(rval));
}

static void cache_drop_with_non_contiguous_ranges(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val[4][BLOCK_TEST_SIZE];
	unsigned int reg;
	const int num_ranges = ARRAY_SIZE(val) * 2;
	int rangeidx, i;

	static_assert(ARRAY_SIZE(val) == 4);

	config = test_regmap_config;
	config.max_register = param->from_reg + (num_ranges * BLOCK_TEST_SIZE);

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	for (i = 0; i < config.max_register + 1; i++)
		data->written[i] = false;

	/* Create non-contiguous cache blocks by writing every other range */
	get_random_bytes(&val, sizeof(val));
	for (rangeidx = 0; rangeidx < num_ranges; rangeidx += 2) {
		reg = param->from_reg + (rangeidx * BLOCK_TEST_SIZE);
		KUNIT_EXPECT_EQ(test, 0, regmap_bulk_write(map, reg,
							   &val[rangeidx / 2],
							   BLOCK_TEST_SIZE));
		KUNIT_EXPECT_MEMEQ(test, &data->vals[reg],
				   &val[rangeidx / 2], sizeof(val[rangeidx / 2]));
	}

	/* Check that odd ranges weren't written */
	for (rangeidx = 1; rangeidx < num_ranges; rangeidx += 2) {
		reg = param->from_reg + (rangeidx * BLOCK_TEST_SIZE);
		for (i = 0; i < BLOCK_TEST_SIZE; i++)
			KUNIT_EXPECT_FALSE(test, data->written[reg + i]);
	}

	/* Drop range 2 */
	reg = param->from_reg + (2 * BLOCK_TEST_SIZE);
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, reg, reg + BLOCK_TEST_SIZE - 1));

	/* Drop part of range 4 */
	reg = param->from_reg + (4 * BLOCK_TEST_SIZE);
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, reg + 3, reg + 5));

	/* Mark dirty and reset mock registers to 0 */
	regcache_mark_dirty(map);
	for (i = 0; i < config.max_register + 1; i++) {
		data->vals[i] = 0;
		data->written[i] = false;
	}

	/* The registers that were dropped from range 4 should now remain at 0 */
	val[4 / 2][3] = 0;
	val[4 / 2][4] = 0;
	val[4 / 2][5] = 0;

	/* Sync and check that the expected register ranges were written */
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* Check that odd ranges weren't written */
	for (rangeidx = 1; rangeidx < num_ranges; rangeidx += 2) {
		reg = param->from_reg + (rangeidx * BLOCK_TEST_SIZE);
		for (i = 0; i < BLOCK_TEST_SIZE; i++)
			KUNIT_EXPECT_FALSE(test, data->written[reg + i]);
	}

	/* Check that even ranges (except 2 and 4) were written */
	for (rangeidx = 0; rangeidx < num_ranges; rangeidx += 2) {
		if ((rangeidx == 2) || (rangeidx == 4))
			continue;

		reg = param->from_reg + (rangeidx * BLOCK_TEST_SIZE);
		for (i = 0; i < BLOCK_TEST_SIZE; i++)
			KUNIT_EXPECT_TRUE(test, data->written[reg + i]);

		KUNIT_EXPECT_MEMEQ(test, &data->vals[reg],
				   &val[rangeidx / 2], sizeof(val[rangeidx / 2]));
	}

	/* Check that range 2 wasn't written */
	reg = param->from_reg + (2 * BLOCK_TEST_SIZE);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_FALSE(test, data->written[reg + i]);

	/* Check that range 4 was partially written */
	reg = param->from_reg + (4 * BLOCK_TEST_SIZE);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, data->written[reg + i], i < 3 || i > 5);

	KUNIT_EXPECT_MEMEQ(test, &data->vals[reg], &val[4 / 2], sizeof(val[4 / 2]));

	/* Nothing before param->from_reg should have been written */
	for (i = 0; i < param->from_reg; i++)
		KUNIT_EXPECT_FALSE(test, data->written[i]);
}

static void cache_drop_all_and_sync_marked_dirty(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Ensure the data is read from the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], rval, sizeof(rval));

	/* Change all values in cache from defaults */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + i, rval[i] + 1));

	/* Drop all registers */
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, 0, config.max_register));

	/* Mark dirty and cache sync should not write anything. */
	regcache_mark_dirty(map);
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));
	for (i = 0; i <= config.max_register; i++)
		KUNIT_EXPECT_FALSE(test, data->written[i]);
}

static void cache_drop_all_and_sync_no_defaults(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Ensure the data is read from the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], rval, sizeof(rval));

	/* Change all values in cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + i, rval[i] + 1));

	/* Drop all registers */
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, 0, config.max_register));

	/*
	 * Sync cache without marking it dirty. All registers were dropped
	 * so the cache should not have any entries to write out.
	 */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));
	for (i = 0; i <= config.max_register; i++)
		KUNIT_EXPECT_FALSE(test, data->written[i]);
}

static void cache_drop_all_and_sync_has_defaults(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval[BLOCK_TEST_SIZE];
	int i;

	config = test_regmap_config;
	config.num_reg_defaults = BLOCK_TEST_SIZE;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Ensure the data is read from the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[param->from_reg + i] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_bulk_read(map, param->from_reg, rval,
						  BLOCK_TEST_SIZE));
	KUNIT_EXPECT_MEMEQ(test, &data->vals[param->from_reg], rval, sizeof(rval));

	/* Change all values in cache from defaults */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_write(map, param->from_reg + i, rval[i] + 1));

	/* Drop all registers */
	KUNIT_EXPECT_EQ(test, 0, regcache_drop_region(map, 0, config.max_register));

	/*
	 * Sync cache without marking it dirty. All registers were dropped
	 * so the cache should not have any entries to write out.
	 */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->written[param->from_reg + i] = false;

	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));
	for (i = 0; i <= config.max_register; i++)
		KUNIT_EXPECT_FALSE(test, data->written[i]);
}

static void cache_present(struct kunit *test)
{
	const struct regmap_test_param *param = test->param_value;
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		data->read[param->from_reg + i] = false;

	/* No defaults so no registers cached. */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_ASSERT_FALSE(test, regcache_reg_cached(map, param->from_reg + i));

	/* We didn't trigger any reads */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_ASSERT_FALSE(test, data->read[param->from_reg + i]);

	/* Fill the cache */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, param->from_reg + i, &val));

	/* Now everything should be cached */
	for (i = 0; i < BLOCK_TEST_SIZE; i++)
		KUNIT_ASSERT_TRUE(test, regcache_reg_cached(map, param->from_reg + i));
}

/* Check that caching the window register works with sync */
static void cache_range_window_reg(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = test_regmap_config;
	config.volatile_reg = test_range_window_volatile;
	config.ranges = &test_range;
	config.num_ranges = 1;
	config.max_register = test_range.range_max;

	map = gen_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Write new values to the entire range */
	for (i = test_range.range_min; i <= test_range.range_max; i++)
		KUNIT_ASSERT_EQ(test, 0, regmap_write(map, i, 0));

	val = data->vals[test_range.selector_reg] & test_range.selector_mask;
	KUNIT_ASSERT_EQ(test, val, 2);

	/* Write to the first register in the range to reset the page */
	KUNIT_ASSERT_EQ(test, 0, regmap_write(map, test_range.range_min, 0));
	val = data->vals[test_range.selector_reg] & test_range.selector_mask;
	KUNIT_ASSERT_EQ(test, val, 0);

	/* Trigger a cache sync */
	regcache_mark_dirty(map);
	KUNIT_ASSERT_EQ(test, 0, regcache_sync(map));

	/* Write to the first register again, the page should be reset */
	KUNIT_ASSERT_EQ(test, 0, regmap_write(map, test_range.range_min, 0));
	val = data->vals[test_range.selector_reg] & test_range.selector_mask;
	KUNIT_ASSERT_EQ(test, val, 0);

	/* Trigger another cache sync */
	regcache_mark_dirty(map);
	KUNIT_ASSERT_EQ(test, 0, regcache_sync(map));

	/* Write to the last register again, the page should be reset */
	KUNIT_ASSERT_EQ(test, 0, regmap_write(map, test_range.range_max, 0));
	val = data->vals[test_range.selector_reg] & test_range.selector_mask;
	KUNIT_ASSERT_EQ(test, val, 2);
}

static const struct regmap_test_param raw_types_list[] = {
	{ .cache = REGCACHE_NONE,   .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_NONE,   .val_endian = REGMAP_ENDIAN_BIG },
	{ .cache = REGCACHE_FLAT,   .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_FLAT,   .val_endian = REGMAP_ENDIAN_BIG },
	{ .cache = REGCACHE_RBTREE, .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_RBTREE, .val_endian = REGMAP_ENDIAN_BIG },
	{ .cache = REGCACHE_MAPLE,  .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_MAPLE,  .val_endian = REGMAP_ENDIAN_BIG },
};

KUNIT_ARRAY_PARAM(raw_test_types, raw_types_list, param_to_desc);

static const struct regmap_test_param raw_cache_types_list[] = {
	{ .cache = REGCACHE_FLAT,   .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_FLAT,   .val_endian = REGMAP_ENDIAN_BIG },
	{ .cache = REGCACHE_RBTREE, .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_RBTREE, .val_endian = REGMAP_ENDIAN_BIG },
	{ .cache = REGCACHE_MAPLE,  .val_endian = REGMAP_ENDIAN_LITTLE },
	{ .cache = REGCACHE_MAPLE,  .val_endian = REGMAP_ENDIAN_BIG },
};

KUNIT_ARRAY_PARAM(raw_test_cache_types, raw_cache_types_list, param_to_desc);

static const struct regmap_config raw_regmap_config = {
	.max_register = BLOCK_TEST_SIZE,

	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.reg_bits = 16,
	.val_bits = 16,
};

static struct regmap *gen_raw_regmap(struct kunit *test,
				     struct regmap_config *config,
				     struct regmap_ram_data **data)
{
	struct regmap_test_priv *priv = test->priv;
	const struct regmap_test_param *param = test->param_value;
	u16 *buf;
	struct regmap *ret = ERR_PTR(-ENOMEM);
	int i, error;
	struct reg_default *defaults;
	size_t size;

	config->cache_type = param->cache;
	config->val_format_endian = param->val_endian;
	config->disable_locking = config->cache_type == REGCACHE_RBTREE ||
					config->cache_type == REGCACHE_MAPLE;

	size = array_size(config->max_register + 1, BITS_TO_BYTES(config->reg_bits));
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	get_random_bytes(buf, size);

	*data = kzalloc(sizeof(**data), GFP_KERNEL);
	if (!(*data))
		goto out_free;
	(*data)->vals = (void *)buf;

	config->num_reg_defaults = config->max_register + 1;
	defaults = kunit_kcalloc(test,
				 config->num_reg_defaults,
				 sizeof(struct reg_default),
				 GFP_KERNEL);
	if (!defaults)
		goto out_free;
	config->reg_defaults = defaults;

	for (i = 0; i < config->num_reg_defaults; i++) {
		defaults[i].reg = i;
		switch (param->val_endian) {
		case REGMAP_ENDIAN_LITTLE:
			defaults[i].def = le16_to_cpu(buf[i]);
			break;
		case REGMAP_ENDIAN_BIG:
			defaults[i].def = be16_to_cpu(buf[i]);
			break;
		default:
			ret = ERR_PTR(-EINVAL);
			goto out_free;
		}
	}

	/*
	 * We use the defaults in the tests but they don't make sense
	 * to the core if there's no cache.
	 */
	if (config->cache_type == REGCACHE_NONE)
		config->num_reg_defaults = 0;

	ret = regmap_init_raw_ram(priv->dev, config, *data);
	if (IS_ERR(ret))
		goto out_free;

	/* This calls regmap_exit() on failure, which frees buf and *data */
	error = kunit_add_action_or_reset(test, regmap_exit_action, ret);
	if (error)
		ret = ERR_PTR(error);

	return ret;

out_free:
	kfree(buf);
	kfree(*data);

	return ret;
}

static void raw_read_defaults_single(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int rval;
	int i;

	config = raw_regmap_config;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Check that we can read the defaults via the API */
	for (i = 0; i < config.max_register + 1; i++) {
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval));
		KUNIT_EXPECT_EQ(test, config.reg_defaults[i].def, rval);
	}
}

static void raw_read_defaults(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	u16 *rval;
	u16 def;
	size_t val_len;
	int i;

	config = raw_regmap_config;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	val_len = array_size(sizeof(*rval), config.max_register + 1);
	rval = kunit_kmalloc(test, val_len, GFP_KERNEL);
	KUNIT_ASSERT_TRUE(test, rval != NULL);
	if (!rval)
		return;

	/* Check that we can read the defaults via the API */
	KUNIT_EXPECT_EQ(test, 0, regmap_raw_read(map, 0, rval, val_len));
	for (i = 0; i < config.max_register + 1; i++) {
		def = config.reg_defaults[i].def;
		if (config.val_format_endian == REGMAP_ENDIAN_BIG) {
			KUNIT_EXPECT_EQ(test, def, be16_to_cpu((__force __be16)rval[i]));
		} else {
			KUNIT_EXPECT_EQ(test, def, le16_to_cpu((__force __le16)rval[i]));
		}
	}
}

static void raw_write_read_single(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	u16 val;
	unsigned int rval;

	config = raw_regmap_config;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	get_random_bytes(&val, sizeof(val));

	/* If we write a value to a register we can read it back */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, 0, val));
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, 0, &rval));
	KUNIT_EXPECT_EQ(test, val, rval);
}

static void raw_write(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	u16 *hw_buf;
	u16 val[2];
	unsigned int rval;
	int i;

	config = raw_regmap_config;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	hw_buf = (u16 *)data->vals;

	get_random_bytes(&val, sizeof(val));

	/* Do a raw write */
	KUNIT_EXPECT_EQ(test, 0, regmap_raw_write(map, 2, val, sizeof(val)));

	/* We should read back the new values, and defaults for the rest */
	for (i = 0; i < config.max_register + 1; i++) {
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval));

		switch (i) {
		case 2:
		case 3:
			if (config.val_format_endian == REGMAP_ENDIAN_BIG) {
				KUNIT_EXPECT_EQ(test, rval,
						be16_to_cpu((__force __be16)val[i % 2]));
			} else {
				KUNIT_EXPECT_EQ(test, rval,
						le16_to_cpu((__force __le16)val[i % 2]));
			}
			break;
		default:
			KUNIT_EXPECT_EQ(test, config.reg_defaults[i].def, rval);
			break;
		}
	}

	/* The values should appear in the "hardware" */
	KUNIT_EXPECT_MEMEQ(test, &hw_buf[2], val, sizeof(val));
}

static bool reg_zero(struct device *dev, unsigned int reg)
{
	return reg == 0;
}

static bool ram_reg_zero(struct regmap_ram_data *data, unsigned int reg)
{
	return reg == 0;
}

static void raw_noinc_write(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	u16 val_test, val_last;
	u16 val_array[BLOCK_TEST_SIZE];

	config = raw_regmap_config;
	config.volatile_reg = reg_zero;
	config.writeable_noinc_reg = reg_zero;
	config.readable_noinc_reg = reg_zero;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	data->noinc_reg = ram_reg_zero;

	get_random_bytes(&val_array, sizeof(val_array));

	if (config.val_format_endian == REGMAP_ENDIAN_BIG) {
		val_test = be16_to_cpu(val_array[1]) + 100;
		val_last = be16_to_cpu(val_array[BLOCK_TEST_SIZE - 1]);
	} else {
		val_test = le16_to_cpu(val_array[1]) + 100;
		val_last = le16_to_cpu(val_array[BLOCK_TEST_SIZE - 1]);
	}

	/* Put some data into the register following the noinc register */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, 1, val_test));

	/* Write some data to the noinc register */
	KUNIT_EXPECT_EQ(test, 0, regmap_noinc_write(map, 0, val_array,
						    sizeof(val_array)));

	/* We should read back the last value written */
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, 0, &val));
	KUNIT_ASSERT_EQ(test, val_last, val);

	/* Make sure we didn't touch the register after the noinc register */
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, 1, &val));
	KUNIT_ASSERT_EQ(test, val_test, val);
}

static void raw_sync(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	u16 val[3];
	u16 *hw_buf;
	unsigned int rval;
	int i;

	config = raw_regmap_config;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	hw_buf = (u16 *)data->vals;

	get_changed_bytes(&hw_buf[2], &val[0], sizeof(val));

	/* Do a regular write and a raw write in cache only mode */
	regcache_cache_only(map, true);
	KUNIT_EXPECT_EQ(test, 0, regmap_raw_write(map, 2, val,
						  sizeof(u16) * 2));
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, 4, val[2]));

	/* We should read back the new values, and defaults for the rest */
	for (i = 0; i < config.max_register + 1; i++) {
		KUNIT_EXPECT_EQ(test, 0, regmap_read(map, i, &rval));

		switch (i) {
		case 2:
		case 3:
			if (config.val_format_endian == REGMAP_ENDIAN_BIG) {
				KUNIT_EXPECT_EQ(test, rval,
						be16_to_cpu((__force __be16)val[i - 2]));
			} else {
				KUNIT_EXPECT_EQ(test, rval,
						le16_to_cpu((__force __le16)val[i - 2]));
			}
			break;
		case 4:
			KUNIT_EXPECT_EQ(test, rval, val[i - 2]);
			break;
		default:
			KUNIT_EXPECT_EQ(test, config.reg_defaults[i].def, rval);
			break;
		}
	}

	/*
	 * The value written via _write() was translated by the core,
	 * translate the original copy for comparison purposes.
	 */
	if (config.val_format_endian == REGMAP_ENDIAN_BIG)
		val[2] = cpu_to_be16(val[2]);
	else
		val[2] = cpu_to_le16(val[2]);

	/* The values should not appear in the "hardware" */
	KUNIT_EXPECT_MEMNEQ(test, &hw_buf[2], &val[0], sizeof(val));

	for (i = 0; i < config.max_register + 1; i++)
		data->written[i] = false;

	/* Do the sync */
	regcache_cache_only(map, false);
	regcache_mark_dirty(map);
	KUNIT_EXPECT_EQ(test, 0, regcache_sync(map));

	/* The values should now appear in the "hardware" */
	KUNIT_EXPECT_MEMEQ(test, &hw_buf[2], &val[0], sizeof(val));
}

static void raw_ranges(struct kunit *test)
{
	struct regmap *map;
	struct regmap_config config;
	struct regmap_ram_data *data;
	unsigned int val;
	int i;

	config = raw_regmap_config;
	config.volatile_reg = test_range_all_volatile;
	config.ranges = &test_range;
	config.num_ranges = 1;
	config.max_register = test_range.range_max;

	map = gen_raw_regmap(test, &config, &data);
	KUNIT_ASSERT_FALSE(test, IS_ERR(map));
	if (IS_ERR(map))
		return;

	/* Reset the page to a non-zero value to trigger a change */
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, test_range.selector_reg,
					      test_range.range_max));

	/* Check we set the page and use the window for writes */
	data->written[test_range.selector_reg] = false;
	data->written[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map, test_range.range_min, 0));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->written[test_range.window_start]);

	data->written[test_range.selector_reg] = false;
	data->written[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_write(map,
					      test_range.range_min +
					      test_range.window_len,
					      0));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->written[test_range.window_start]);

	/* Same for reads */
	data->written[test_range.selector_reg] = false;
	data->read[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map, test_range.range_min, &val));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->read[test_range.window_start]);

	data->written[test_range.selector_reg] = false;
	data->read[test_range.window_start] = false;
	KUNIT_EXPECT_EQ(test, 0, regmap_read(map,
					     test_range.range_min +
					     test_range.window_len,
					     &val));
	KUNIT_EXPECT_TRUE(test, data->written[test_range.selector_reg]);
	KUNIT_EXPECT_TRUE(test, data->read[test_range.window_start]);

	/* No physical access triggered in the virtual range */
	for (i = test_range.range_min; i < test_range.range_max; i++) {
		KUNIT_EXPECT_FALSE(test, data->read[i]);
		KUNIT_EXPECT_FALSE(test, data->written[i]);
	}
}

static struct kunit_case regmap_test_cases[] = {
	KUNIT_CASE_PARAM(basic_read_write, regcache_types_gen_params),
	KUNIT_CASE_PARAM(read_bypassed, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(read_bypassed_volatile, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(bulk_write, regcache_types_gen_params),
	KUNIT_CASE_PARAM(bulk_read, regcache_types_gen_params),
	KUNIT_CASE_PARAM(multi_write, regcache_types_gen_params),
	KUNIT_CASE_PARAM(multi_read, regcache_types_gen_params),
	KUNIT_CASE_PARAM(write_readonly, regcache_types_gen_params),
	KUNIT_CASE_PARAM(read_writeonly, regcache_types_gen_params),
	KUNIT_CASE_PARAM(reg_defaults, regcache_types_gen_params),
	KUNIT_CASE_PARAM(reg_defaults_read_dev, regcache_types_gen_params),
	KUNIT_CASE_PARAM(register_patch, regcache_types_gen_params),
	KUNIT_CASE_PARAM(stride, regcache_types_gen_params),
	KUNIT_CASE_PARAM(basic_ranges, regcache_types_gen_params),
	KUNIT_CASE_PARAM(stress_insert, regcache_types_gen_params),
	KUNIT_CASE_PARAM(cache_bypass, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_marked_dirty, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_after_cache_only, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_defaults_marked_dirty, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_default_after_cache_only, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_readonly, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_sync_patch, real_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_drop, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_drop_with_non_contiguous_ranges, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_drop_all_and_sync_marked_dirty, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_drop_all_and_sync_no_defaults, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_drop_all_and_sync_has_defaults, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_present, sparse_cache_types_gen_params),
	KUNIT_CASE_PARAM(cache_range_window_reg, real_cache_types_only_gen_params),

	KUNIT_CASE_PARAM(raw_read_defaults_single, raw_test_types_gen_params),
	KUNIT_CASE_PARAM(raw_read_defaults, raw_test_types_gen_params),
	KUNIT_CASE_PARAM(raw_write_read_single, raw_test_types_gen_params),
	KUNIT_CASE_PARAM(raw_write, raw_test_types_gen_params),
	KUNIT_CASE_PARAM(raw_noinc_write, raw_test_types_gen_params),
	KUNIT_CASE_PARAM(raw_sync, raw_test_cache_types_gen_params),
	KUNIT_CASE_PARAM(raw_ranges, raw_test_cache_types_gen_params),
	{}
};

static int regmap_test_init(struct kunit *test)
{
	struct regmap_test_priv *priv;
	struct device *dev;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	test->priv = priv;

	dev = kunit_device_register(test, "regmap_test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	priv->dev = get_device(dev);
	dev_set_drvdata(dev, test);

	return 0;
}

static void regmap_test_exit(struct kunit *test)
{
	struct regmap_test_priv *priv = test->priv;

	/* Destroy the dummy struct device */
	if (priv && priv->dev)
		put_device(priv->dev);
}

static struct kunit_suite regmap_test_suite = {
	.name = "regmap",
	.init = regmap_test_init,
	.exit = regmap_test_exit,
	.test_cases = regmap_test_cases,
};
kunit_test_suite(regmap_test_suite);

MODULE_DESCRIPTION("Regmap KUnit tests");
MODULE_LICENSE("GPL v2");
