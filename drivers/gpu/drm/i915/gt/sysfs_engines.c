// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "i915_drv.h"
#include "intel_engine.h"
#include "sysfs_engines.h"

struct kobj_engine {
	struct kobject base;
	struct intel_engine_cs *engine;
};

static struct intel_engine_cs *kobj_to_engine(struct kobject *kobj)
{
	return container_of(kobj, struct kobj_engine, base)->engine;
}

static ssize_t
name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", kobj_to_engine(kobj)->name);
}

static struct kobj_attribute name_attr =
__ATTR(name, 0444, name_show, NULL);

static ssize_t
class_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", kobj_to_engine(kobj)->uabi_class);
}

static struct kobj_attribute class_attr =
__ATTR(class, 0444, class_show, NULL);

static ssize_t
inst_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", kobj_to_engine(kobj)->uabi_instance);
}

static struct kobj_attribute inst_attr =
__ATTR(instance, 0444, inst_show, NULL);

static ssize_t
mmio_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%x\n", kobj_to_engine(kobj)->mmio_base);
}

static struct kobj_attribute mmio_attr =
__ATTR(mmio_base, 0444, mmio_show, NULL);

static const char * const vcs_caps[] = {
	[ilog2(I915_VIDEO_CLASS_CAPABILITY_HEVC)] = "hevc",
	[ilog2(I915_VIDEO_AND_ENHANCE_CLASS_CAPABILITY_SFC)] = "sfc",
};

static const char * const vecs_caps[] = {
	[ilog2(I915_VIDEO_AND_ENHANCE_CLASS_CAPABILITY_SFC)] = "sfc",
};

static ssize_t repr_trim(char *buf, ssize_t len)
{
	/* Trim off the trailing space and replace with a newline */
	if (len > PAGE_SIZE)
		len = PAGE_SIZE;
	if (len > 0)
		buf[len - 1] = '\n';

	return len;
}

static ssize_t
__caps_show(struct intel_engine_cs *engine,
	    u32 caps, char *buf, bool show_unknown)
{
	const char * const *repr;
	int count, n;
	ssize_t len;

	BUILD_BUG_ON(!typecheck(typeof(caps), engine->uabi_capabilities));

	switch (engine->class) {
	case VIDEO_DECODE_CLASS:
		repr = vcs_caps;
		count = ARRAY_SIZE(vcs_caps);
		break;

	case VIDEO_ENHANCEMENT_CLASS:
		repr = vecs_caps;
		count = ARRAY_SIZE(vecs_caps);
		break;

	default:
		repr = NULL;
		count = 0;
		break;
	}
	GEM_BUG_ON(count > BITS_PER_TYPE(typeof(caps)));

	len = 0;
	for_each_set_bit(n,
			 (unsigned long *)&caps,
			 show_unknown ? BITS_PER_TYPE(typeof(caps)) : count) {
		if (n >= count || !repr[n]) {
			if (GEM_WARN_ON(show_unknown))
				len += snprintf(buf + len, PAGE_SIZE - len,
						"[%x] ", n);
		} else {
			len += snprintf(buf + len, PAGE_SIZE - len,
					"%s ", repr[n]);
		}
		if (GEM_WARN_ON(len >= PAGE_SIZE))
			break;
	}
	return repr_trim(buf, len);
}

static ssize_t
caps_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct intel_engine_cs *engine = kobj_to_engine(kobj);

	return __caps_show(engine, engine->uabi_capabilities, buf, true);
}

static struct kobj_attribute caps_attr =
__ATTR(capabilities, 0444, caps_show, NULL);

static ssize_t
all_caps_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return __caps_show(kobj_to_engine(kobj), -1, buf, false);
}

static struct kobj_attribute all_caps_attr =
__ATTR(known_capabilities, 0444, all_caps_show, NULL);

static void kobj_engine_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type kobj_engine_type = {
	.release = kobj_engine_release,
	.sysfs_ops = &kobj_sysfs_ops
};

static struct kobject *
kobj_engine(struct kobject *dir, struct intel_engine_cs *engine)
{
	struct kobj_engine *ke;

	ke = kzalloc(sizeof(*ke), GFP_KERNEL);
	if (!ke)
		return NULL;

	kobject_init(&ke->base, &kobj_engine_type);
	ke->engine = engine;

	if (kobject_add(&ke->base, dir, "%s", engine->name)) {
		kobject_put(&ke->base);
		return NULL;
	}

	/* xfer ownership to sysfs tree */
	return &ke->base;
}

void intel_engines_add_sysfs(struct drm_i915_private *i915)
{
	static const struct attribute *files[] = {
		&name_attr.attr,
		&class_attr.attr,
		&inst_attr.attr,
		&mmio_attr.attr,
		&caps_attr.attr,
		&all_caps_attr.attr,
		NULL
	};

	struct device *kdev = i915->drm.primary->kdev;
	struct intel_engine_cs *engine;
	struct kobject *dir;

	dir = kobject_create_and_add("engine", &kdev->kobj);
	if (!dir)
		return;

	for_each_uabi_engine(engine, i915) {
		struct kobject *kobj;

		kobj = kobj_engine(dir, engine);
		if (!kobj)
			goto err_engine;

		if (sysfs_create_files(kobj, files))
			goto err_object;

		if (0) {
err_object:
			kobject_put(kobj);
err_engine:
			dev_err(kdev, "Failed to add sysfs engine '%s'\n",
				engine->name);
			break;
		}
	}
}
