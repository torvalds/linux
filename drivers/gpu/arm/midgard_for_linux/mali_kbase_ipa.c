/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */


#include <linux/of.h>
#include <linux/sysfs.h>

#include <mali_kbase.h>

#define NR_IPA_GROUPS 8

struct kbase_ipa_context;

/**
 * struct ipa_group - represents a single IPA group
 * @name:               name of the IPA group
 * @capacitance:        capacitance constant for IPA group
 * @calc_power:         function to calculate power for IPA group
 */
struct ipa_group {
	const char *name;
	u32 capacitance;
	u32 (*calc_power)(struct kbase_ipa_context *,
			struct ipa_group *);
};

#include <mali_kbase_ipa_tables.h>

/**
 * struct kbase_ipa_context - IPA context per device
 * @kbdev:              pointer to kbase device
 * @groups:             array of IPA groups for this context
 * @vinstr_cli:         vinstr client handle
 * @vinstr_buffer:      buffer to dump hardware counters onto
 * @ipa_lock:           protects the entire IPA context
 */
struct kbase_ipa_context {
	struct kbase_device *kbdev;
	struct ipa_group groups[NR_IPA_GROUPS];
	struct kbase_vinstr_client *vinstr_cli;
	void *vinstr_buffer;
	struct mutex ipa_lock;
};

static ssize_t show_ipa_group(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct kbase_ipa_context *ctx = kbdev->ipa_ctx;
	ssize_t count = -EINVAL;
	size_t i;

	mutex_lock(&ctx->ipa_lock);
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		if (!strcmp(ctx->groups[i].name, attr->attr.name)) {
			count = snprintf(buf, PAGE_SIZE, "%lu\n",
				(unsigned long)ctx->groups[i].capacitance);
			break;
		}
	}
	mutex_unlock(&ctx->ipa_lock);
	return count;
}

static ssize_t set_ipa_group(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct kbase_ipa_context *ctx = kbdev->ipa_ctx;
	unsigned long capacitance;
	size_t i;
	int err;

	err = kstrtoul(buf, 0, &capacitance);
	if (err < 0)
		return err;
	if (capacitance > U32_MAX)
		return -ERANGE;

	mutex_lock(&ctx->ipa_lock);
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		if (!strcmp(ctx->groups[i].name, attr->attr.name)) {
			ctx->groups[i].capacitance = capacitance;
			mutex_unlock(&ctx->ipa_lock);
			return count;
		}
	}
	mutex_unlock(&ctx->ipa_lock);
	return -EINVAL;
}

static DEVICE_ATTR(group0, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group1, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group2, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group3, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group4, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group5, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group6, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);
static DEVICE_ATTR(group7, S_IRUGO | S_IWUSR, show_ipa_group, set_ipa_group);

static struct attribute *kbase_ipa_attrs[] = {
	&dev_attr_group0.attr,
	&dev_attr_group1.attr,
	&dev_attr_group2.attr,
	&dev_attr_group3.attr,
	&dev_attr_group4.attr,
	&dev_attr_group5.attr,
	&dev_attr_group6.attr,
	&dev_attr_group7.attr,
	NULL,
};

static struct attribute_group kbase_ipa_attr_group = {
	.name = "ipa",
	.attrs = kbase_ipa_attrs,
};

static void init_ipa_groups(struct kbase_ipa_context *ctx)
{
	memcpy(ctx->groups, ipa_groups_def, sizeof(ctx->groups));
}

#if defined(CONFIG_OF) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static int update_ipa_groups_from_dt(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;
	struct device_node *np, *child;
	struct ipa_group *group;
	size_t nr_groups;
	size_t i;
	int err;

	np = of_find_node_by_name(kbdev->dev->of_node, "ipa-groups");
	if (!np)
		return 0;

	nr_groups = 0;
	for_each_available_child_of_node(np, child)
		nr_groups++;
	if (!nr_groups || nr_groups > ARRAY_SIZE(ctx->groups)) {
		dev_err(kbdev->dev, "invalid number of IPA groups: %zu", nr_groups);
		err = -EINVAL;
		goto err0;
	}

	for_each_available_child_of_node(np, child) {
		const char *name;
		u32 capacitance;

		name = of_get_property(child, "label", NULL);
		if (!name) {
			dev_err(kbdev->dev, "label missing for IPA group");
			err = -EINVAL;
			goto err0;
		}
		err = of_property_read_u32(child, "capacitance",
				&capacitance);
		if (err < 0) {
			dev_err(kbdev->dev, "capacitance missing for IPA group");
			goto err0;
		}

		for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
			group = &ctx->groups[i];
			if (!strcmp(group->name, name)) {
				group->capacitance = capacitance;
				break;
			}
		}
	}

	of_node_put(np);
	return 0;
err0:
	of_node_put(np);
	return err;
}
#else
static int update_ipa_groups_from_dt(struct kbase_ipa_context *ctx)
{
	return 0;
}
#endif

static int reset_ipa_groups(struct kbase_ipa_context *ctx)
{
	init_ipa_groups(ctx);
	return update_ipa_groups_from_dt(ctx);
}

static inline u32 read_hwcnt(struct kbase_ipa_context *ctx,
	u32 offset)
{
	u8 *p = ctx->vinstr_buffer;

	return *(u32 *)&p[offset];
}

static inline u32 add_saturate(u32 a, u32 b)
{
	if (U32_MAX - a < b)
		return U32_MAX;
	return a + b;
}

/*
 * Calculate power estimation based on hardware counter `c'
 * across all shader cores.
 */
static u32 calc_power_sc_single(struct kbase_ipa_context *ctx,
	struct ipa_group *group, u32 c)
{
	struct kbase_device *kbdev = ctx->kbdev;
	u64 core_mask;
	u32 base = 0, r = 0;

	core_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
	while (core_mask != 0ull) {
		if ((core_mask & 1ull) != 0ull) {
			u64 n = read_hwcnt(ctx, base + c);
			u32 d = read_hwcnt(ctx, GPU_ACTIVE);
			u32 s = group->capacitance;

			r = add_saturate(r, div_u64(n * s, d));
		}
		base += NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT;
		core_mask >>= 1;
	}
	return r;
}

/*
 * Calculate power estimation based on hardware counter `c1'
 * and `c2' across all shader cores.
 */
static u32 calc_power_sc_double(struct kbase_ipa_context *ctx,
	struct ipa_group *group, u32 c1, u32 c2)
{
	struct kbase_device *kbdev = ctx->kbdev;
	u64 core_mask;
	u32 base = 0, r = 0;

	core_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;
	while (core_mask != 0ull) {
		if ((core_mask & 1ull) != 0ull) {
			u64 n = read_hwcnt(ctx, base + c1);
			u32 d = read_hwcnt(ctx, GPU_ACTIVE);
			u32 s = group->capacitance;

			r = add_saturate(r, div_u64(n * s, d));
			n = read_hwcnt(ctx, base + c2);
			r = add_saturate(r, div_u64(n * s, d));
		}
		base += NR_CNT_PER_BLOCK * NR_BYTES_PER_CNT;
		core_mask >>= 1;
	}
	return r;
}

static u32 calc_power_single(struct kbase_ipa_context *ctx,
	struct ipa_group *group, u32 c)
{
	u64 n = read_hwcnt(ctx, c);
	u32 d = read_hwcnt(ctx, GPU_ACTIVE);
	u32 s = group->capacitance;

	return div_u64(n * s, d);
}

static u32 calc_power_group0(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_single(ctx, group, L2_ANY_LOOKUP);
}

static u32 calc_power_group1(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_single(ctx, group, TILER_ACTIVE);
}

static u32 calc_power_group2(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_single(ctx, group, FRAG_ACTIVE);
}

static u32 calc_power_group3(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_double(ctx, group, VARY_SLOT_32,
			VARY_SLOT_16);
}

static u32 calc_power_group4(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_single(ctx, group, TEX_COORD_ISSUE);
}

static u32 calc_power_group5(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_single(ctx, group, EXEC_INSTR_COUNT);
}

static u32 calc_power_group6(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_double(ctx, group, BEATS_RD_LSC,
			BEATS_WR_LSC);
}

static u32 calc_power_group7(struct kbase_ipa_context *ctx,
		struct ipa_group *group)
{
	return calc_power_sc_single(ctx, group, EXEC_CORE_ACTIVE);
}

static int attach_vinstr(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;
	struct kbase_uk_hwcnt_reader_setup setup;
	size_t dump_size;

	dump_size = kbase_vinstr_dump_size(kbdev);
	ctx->vinstr_buffer = kzalloc(dump_size, GFP_KERNEL);
	if (!ctx->vinstr_buffer) {
		dev_err(kbdev->dev, "Failed to allocate IPA dump buffer");
		return -1;
	}

	setup.jm_bm = ~0u;
	setup.shader_bm = ~0u;
	setup.tiler_bm = ~0u;
	setup.mmu_l2_bm = ~0u;
	ctx->vinstr_cli = kbase_vinstr_hwcnt_kernel_setup(kbdev->vinstr_ctx,
			&setup, ctx->vinstr_buffer);
	if (!ctx->vinstr_cli) {
		dev_err(kbdev->dev, "Failed to register IPA with vinstr core");
		kfree(ctx->vinstr_buffer);
		ctx->vinstr_buffer = NULL;
		return -1;
	}
	return 0;
}

static void detach_vinstr(struct kbase_ipa_context *ctx)
{
	if (ctx->vinstr_cli)
		kbase_vinstr_detach_client(ctx->vinstr_cli);
	ctx->vinstr_cli = NULL;
	kfree(ctx->vinstr_buffer);
	ctx->vinstr_buffer = NULL;
}

struct kbase_ipa_context *kbase_ipa_init(struct kbase_device *kbdev)
{
	struct kbase_ipa_context *ctx;
	int err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	mutex_init(&ctx->ipa_lock);
	ctx->kbdev = kbdev;

	err = reset_ipa_groups(ctx);
	if (err < 0)
		goto err0;

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_ipa_attr_group);
	if (err < 0)
		goto err0;

	return ctx;
err0:
	kfree(ctx);
	return NULL;
}

void kbase_ipa_term(struct kbase_ipa_context *ctx)
{
	struct kbase_device *kbdev = ctx->kbdev;

	detach_vinstr(ctx);
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_ipa_attr_group);
	kfree(ctx);
}

u32 kbase_ipa_dynamic_power(struct kbase_ipa_context *ctx, int *err)
{
	struct ipa_group *group;
	u32 power = 0;
	size_t i;

	mutex_lock(&ctx->ipa_lock);
	if (!ctx->vinstr_cli) {
		*err = attach_vinstr(ctx);
		if (*err < 0)
			goto err0;
	}
	*err = kbase_vinstr_hwc_dump(ctx->vinstr_cli,
			BASE_HWCNT_READER_EVENT_MANUAL);
	if (*err)
		goto err0;
	for (i = 0; i < ARRAY_SIZE(ctx->groups); i++) {
		group = &ctx->groups[i];
		power = add_saturate(power, group->calc_power(ctx, group));
	}
err0:
	mutex_unlock(&ctx->ipa_lock);
	return power;
}
KBASE_EXPORT_TEST_API(kbase_ipa_dynamic_power);
