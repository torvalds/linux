// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/llist.h>

#include "i915_drv.h"
#include "intel_engine.h"
#include "intel_engine_user.h"
#include "intel_gt.h"
#include "uc/intel_guc_submission.h"

struct intel_engine_cs *
intel_engine_lookup_user(struct drm_i915_private *i915, u8 class, u8 instance)
{
	struct rb_node *p = i915->uabi_engines.rb_node;

	while (p) {
		struct intel_engine_cs *it =
			rb_entry(p, typeof(*it), uabi_node);

		if (class < it->uabi_class)
			p = p->rb_left;
		else if (class > it->uabi_class ||
			 instance > it->uabi_instance)
			p = p->rb_right;
		else if (instance < it->uabi_instance)
			p = p->rb_left;
		else
			return it;
	}

	return NULL;
}

void intel_engine_add_user(struct intel_engine_cs *engine)
{
	llist_add((struct llist_node *)&engine->uabi_node,
		  (struct llist_head *)&engine->i915->uabi_engines);
}

static const u8 uabi_classes[] = {
	[RENDER_CLASS] = I915_ENGINE_CLASS_RENDER,
	[COPY_ENGINE_CLASS] = I915_ENGINE_CLASS_COPY,
	[VIDEO_DECODE_CLASS] = I915_ENGINE_CLASS_VIDEO,
	[VIDEO_ENHANCEMENT_CLASS] = I915_ENGINE_CLASS_VIDEO_ENHANCE,
	[COMPUTE_CLASS] = I915_ENGINE_CLASS_COMPUTE,
};

static int engine_cmp(void *priv, const struct list_head *A,
		      const struct list_head *B)
{
	const struct intel_engine_cs *a =
		container_of((struct rb_node *)A, typeof(*a), uabi_node);
	const struct intel_engine_cs *b =
		container_of((struct rb_node *)B, typeof(*b), uabi_node);

	if (uabi_classes[a->class] < uabi_classes[b->class])
		return -1;
	if (uabi_classes[a->class] > uabi_classes[b->class])
		return 1;

	if (a->instance < b->instance)
		return -1;
	if (a->instance > b->instance)
		return 1;

	return 0;
}

static struct llist_node *get_engines(struct drm_i915_private *i915)
{
	return llist_del_all((struct llist_head *)&i915->uabi_engines);
}

static void sort_engines(struct drm_i915_private *i915,
			 struct list_head *engines)
{
	struct llist_node *pos, *next;

	llist_for_each_safe(pos, next, get_engines(i915)) {
		struct intel_engine_cs *engine =
			container_of((struct rb_node *)pos, typeof(*engine),
				     uabi_node);
		list_add((struct list_head *)&engine->uabi_node, engines);
	}
	list_sort(NULL, engines, engine_cmp);
}

static void set_scheduler_caps(struct drm_i915_private *i915)
{
	static const struct {
		u8 engine;
		u8 sched;
	} map[] = {
#define MAP(x, y) { ilog2(I915_ENGINE_##x), ilog2(I915_SCHEDULER_CAP_##y) }
		MAP(HAS_PREEMPTION, PREEMPTION),
		MAP(HAS_SEMAPHORES, SEMAPHORES),
		MAP(SUPPORTS_STATS, ENGINE_BUSY_STATS),
#undef MAP
	};
	struct intel_engine_cs *engine;
	u32 enabled, disabled;

	enabled = 0;
	disabled = 0;
	for_each_uabi_engine(engine, i915) { /* all engines must agree! */
		int i;

		if (engine->sched_engine->schedule)
			enabled |= (I915_SCHEDULER_CAP_ENABLED |
				    I915_SCHEDULER_CAP_PRIORITY);
		else
			disabled |= (I915_SCHEDULER_CAP_ENABLED |
				     I915_SCHEDULER_CAP_PRIORITY);

		if (intel_uc_uses_guc_submission(&to_gt(i915)->uc))
			enabled |= I915_SCHEDULER_CAP_STATIC_PRIORITY_MAP;

		for (i = 0; i < ARRAY_SIZE(map); i++) {
			if (engine->flags & BIT(map[i].engine))
				enabled |= BIT(map[i].sched);
			else
				disabled |= BIT(map[i].sched);
		}
	}

	i915->caps.scheduler = enabled & ~disabled;
	if (!(i915->caps.scheduler & I915_SCHEDULER_CAP_ENABLED))
		i915->caps.scheduler = 0;
}

const char *intel_engine_class_repr(u8 class)
{
	static const char * const uabi_names[] = {
		[RENDER_CLASS] = "rcs",
		[COPY_ENGINE_CLASS] = "bcs",
		[VIDEO_DECODE_CLASS] = "vcs",
		[VIDEO_ENHANCEMENT_CLASS] = "vecs",
		[COMPUTE_CLASS] = "ccs",
	};

	if (class >= ARRAY_SIZE(uabi_names) || !uabi_names[class])
		return "xxx";

	return uabi_names[class];
}

struct legacy_ring {
	struct intel_gt *gt;
	u8 class;
	u8 instance;
};

static int legacy_ring_idx(const struct legacy_ring *ring)
{
	static const struct {
		u8 base, max;
	} map[] = {
		[RENDER_CLASS] = { RCS0, 1 },
		[COPY_ENGINE_CLASS] = { BCS0, 1 },
		[VIDEO_DECODE_CLASS] = { VCS0, I915_MAX_VCS },
		[VIDEO_ENHANCEMENT_CLASS] = { VECS0, I915_MAX_VECS },
		[COMPUTE_CLASS] = { CCS0, I915_MAX_CCS },
	};

	if (GEM_DEBUG_WARN_ON(ring->class >= ARRAY_SIZE(map)))
		return INVALID_ENGINE;

	if (GEM_DEBUG_WARN_ON(ring->instance >= map[ring->class].max))
		return INVALID_ENGINE;

	return map[ring->class].base + ring->instance;
}

static void add_legacy_ring(struct legacy_ring *ring,
			    struct intel_engine_cs *engine)
{
	if (engine->gt != ring->gt || engine->class != ring->class) {
		ring->gt = engine->gt;
		ring->class = engine->class;
		ring->instance = 0;
	}

	engine->legacy_idx = legacy_ring_idx(ring);
	if (engine->legacy_idx != INVALID_ENGINE)
		ring->instance++;
}

void intel_engines_driver_register(struct drm_i915_private *i915)
{
	struct legacy_ring ring = {};
	struct list_head *it, *next;
	struct rb_node **p, *prev;
	LIST_HEAD(engines);

	sort_engines(i915, &engines);

	prev = NULL;
	p = &i915->uabi_engines.rb_node;
	list_for_each_safe(it, next, &engines) {
		struct intel_engine_cs *engine =
			container_of((struct rb_node *)it, typeof(*engine),
				     uabi_node);
		char old[sizeof(engine->name)];

		if (intel_gt_has_unrecoverable_error(engine->gt))
			continue; /* ignore incomplete engines */

		GEM_BUG_ON(engine->class >= ARRAY_SIZE(uabi_classes));
		engine->uabi_class = uabi_classes[engine->class];

		GEM_BUG_ON(engine->uabi_class >=
			   ARRAY_SIZE(i915->engine_uabi_class_count));
		engine->uabi_instance =
			i915->engine_uabi_class_count[engine->uabi_class]++;

		/* Replace the internal name with the final user facing name */
		memcpy(old, engine->name, sizeof(engine->name));
		scnprintf(engine->name, sizeof(engine->name), "%s%u",
			  intel_engine_class_repr(engine->class),
			  engine->uabi_instance);
		DRM_DEBUG_DRIVER("renamed %s to %s\n", old, engine->name);

		rb_link_node(&engine->uabi_node, prev, p);
		rb_insert_color(&engine->uabi_node, &i915->uabi_engines);

		GEM_BUG_ON(intel_engine_lookup_user(i915,
						    engine->uabi_class,
						    engine->uabi_instance) != engine);

		/* Fix up the mapping to match default execbuf::user_map[] */
		add_legacy_ring(&ring, engine);

		prev = &engine->uabi_node;
		p = &prev->rb_right;
	}

	if (IS_ENABLED(CONFIG_DRM_I915_SELFTESTS) &&
	    IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)) {
		struct intel_engine_cs *engine;
		unsigned int isolation;
		int class, inst;
		int errors = 0;

		for (class = 0; class < ARRAY_SIZE(i915->engine_uabi_class_count); class++) {
			for (inst = 0; inst < i915->engine_uabi_class_count[class]; inst++) {
				engine = intel_engine_lookup_user(i915,
								  class, inst);
				if (!engine) {
					pr_err("UABI engine not found for { class:%d, instance:%d }\n",
					       class, inst);
					errors++;
					continue;
				}

				if (engine->uabi_class != class ||
				    engine->uabi_instance != inst) {
					pr_err("Wrong UABI engine:%s { class:%d, instance:%d } found for { class:%d, instance:%d }\n",
					       engine->name,
					       engine->uabi_class,
					       engine->uabi_instance,
					       class, inst);
					errors++;
					continue;
				}
			}
		}

		/*
		 * Make sure that classes with multiple engine instances all
		 * share the same basic configuration.
		 */
		isolation = intel_engines_has_context_isolation(i915);
		for_each_uabi_engine(engine, i915) {
			unsigned int bit = BIT(engine->uabi_class);
			unsigned int expected = engine->default_state ? bit : 0;

			if ((isolation & bit) != expected) {
				pr_err("mismatching default context state for class %d on engine %s\n",
				       engine->uabi_class, engine->name);
				errors++;
			}
		}

		if (drm_WARN(&i915->drm, errors,
			     "Invalid UABI engine mapping found"))
			i915->uabi_engines = RB_ROOT;
	}

	set_scheduler_caps(i915);
}

unsigned int intel_engines_has_context_isolation(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	unsigned int which;

	which = 0;
	for_each_uabi_engine(engine, i915)
		if (engine->default_state)
			which |= BIT(engine->uabi_class);

	return which;
}
