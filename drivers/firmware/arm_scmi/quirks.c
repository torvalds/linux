// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Protocol Quirks
 *
 * Copyright (C) 2025 ARM Ltd.
 */

/**
 * DOC: Theory of operation
 *
 * A framework to define SCMI quirks and their activation conditions based on
 * existing static_keys kernel facilities.
 *
 * Quirks are named and their activation conditions defined using the macro
 * DEFINE_SCMI_QUIRK() in this file.
 *
 * After a quirk is defined, a corresponding entry must also be added to the
 * global @scmi_quirks_table in this file using __DECLARE_SCMI_QUIRK_ENTRY().
 *
 * Additionally a corresponding quirk declaration must be added also to the
 * quirk.h file using DECLARE_SCMI_QUIRK().
 *
 * The needed quirk code-snippet itself will be defined local to the SCMI code
 * that is meant to fix and will be associated to the previously defined quirk
 * and related activation conditions using the macro SCMI_QUIRK().
 *
 * At runtime, during the SCMI stack probe sequence, once the SCMI Server had
 * advertised the running platform Vendor, SubVendor and Implementation Version
 * data, all the defined quirks matching the activation conditions will be
 * enabled.
 *
 * Example
 *
 * quirk.c
 * -------
 *  DEFINE_SCMI_QUIRK(fix_me, "vendor", "subvend", "0x12000-0x30000",
 *		      "someone,plat_A", "another,plat_b", "vend,sku");
 *
 *  static struct scmi_quirk *scmi_quirks_table[] = {
 *	...
 *	__DECLARE_SCMI_QUIRK_ENTRY(fix_me),
 *	NULL
 *  };
 *
 * quirk.h
 * -------
 *  DECLARE_SCMI_QUIRK(fix_me);
 *
 * <somewhere_in_the_scmi_stack.c>
 * ------------------------------
 *
 *  #define QUIRK_CODE_SNIPPET_FIX_ME()		\
 *  ({						\
 *	if (p->condition)			\
 *		a_ptr->calculated_val = 123;	\
 *  })
 *
 *
 *  int some_function_to_fix(int param, struct something *p)
 *  {
 *	struct some_strut *a_ptr;
 *
 *	a_ptr = some_load_func(p);
 *	SCMI_QUIRK(fix_me, QUIRK_CODE_SNIPPET_FIX_ME);
 *	some_more_func(a_ptr);
 *	...
 *
 *	return 0;
 *  }
 *
 */

#include <linux/ctype.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/hashtable.h>
#include <linux/kstrtox.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/static_key.h>
#include <linux/string.h>
#include <linux/stringhash.h>
#include <linux/types.h>

#include "quirks.h"

#define SCMI_QUIRKS_HT_SZ	4

struct scmi_quirk {
	bool enabled;
	const char *name;
	const char *vendor;
	const char *sub_vendor_id;
	const char *impl_ver_range;
	u32 start_range;
	u32 end_range;
	struct static_key_false *key;
	struct hlist_node hash;
	unsigned int hkey;
	const char *const compats[];
};

#define __DEFINE_SCMI_QUIRK_ENTRY(_qn, _ven, _sub, _impl, ...)	\
	static struct scmi_quirk scmi_quirk_entry_ ## _qn = {		\
		.name = __stringify(quirk_ ## _qn),			\
		.vendor = _ven,						\
		.sub_vendor_id = _sub,					\
		.impl_ver_range = _impl,				\
		.key = &(scmi_quirk_ ## _qn),				\
		.compats = { __VA_ARGS__ __VA_OPT__(,) NULL },		\
	}

#define __DECLARE_SCMI_QUIRK_ENTRY(_qn)		(&(scmi_quirk_entry_ ## _qn))

/*
 * Define a quirk by name and provide the matching tokens where:
 *
 *  _qn: A string which will be used to build the quirk and the global
 *	 static_key names.
 *  _ven : SCMI Vendor ID string match, NULL means any.
 *  _sub : SCMI SubVendor ID string match, NULL means any.
 *  _impl : SCMI Implementation Version string match, NULL means any.
 *          This string can be used to express version ranges which will be
 *          interpreted as follows:
 *
 *			NULL		[0, 0xFFFFFFFF]
 *			"X"		[X, X]
 *			"X-"		[X, 0xFFFFFFFF]
 *			"-X"		[0, X]
 *			"X-Y"		[X, Y]
 *
 *          with X <= Y and <v> in [X, Y] meaning X <= <v> <= Y
 *
 *  ... : An optional variadic macros argument used to provide a comma-separated
 *	  list of compatible strings matches; when no variadic argument is
 *	  provided, ANY compatible will match this quirk.
 *
 *  This implicitly define also a properly named global static-key that
 *  will be used to dynamically enable the quirk at initialization time.
 *
 *  Note that it is possible to associate multiple quirks to the same
 *  matching pattern, if your firmware quality is really astounding :P
 *
 * Example:
 *
 * Compatibles list NOT provided, so ANY compatible will match:
 *
 *  DEFINE_SCMI_QUIRK(my_new_issue, "Vend", "SVend", "0x12000-0x30000");
 *
 *
 * A few compatibles provided to match against:
 *
 *  DEFINE_SCMI_QUIRK(my_new_issue, "Vend", "SVend", "0x12000-0x30000",
 *		      "xvend,plat_a", "xvend,plat_b", "xvend,sku_name");
 */
#define DEFINE_SCMI_QUIRK(_qn, _ven, _sub, _impl, ...)			\
	DEFINE_STATIC_KEY_FALSE(scmi_quirk_ ## _qn);			\
	__DEFINE_SCMI_QUIRK_ENTRY(_qn, _ven, _sub, _impl, ##__VA_ARGS__)

/*
 * Same as DEFINE_SCMI_QUIRK but EXPORTED: this is meant to address quirks
 * that possibly reside in code that is included in loadable kernel modules
 * that needs to be able to access the global static keys at runtime to
 * determine if enabled or not. (see SCMI_QUIRK to understand usage)
 */
#define DEFINE_SCMI_QUIRK_EXPORTED(_qn, _ven, _sub, _impl, ...)		\
	DEFINE_STATIC_KEY_FALSE(scmi_quirk_ ## _qn);			\
	EXPORT_SYMBOL_GPL(scmi_quirk_ ## _qn);				\
	__DEFINE_SCMI_QUIRK_ENTRY(_qn, _ven, _sub, _impl, ##__VA_ARGS__)

/* Global Quirks Definitions */
DEFINE_SCMI_QUIRK(clock_rates_triplet_out_of_spec, NULL, NULL, NULL);
DEFINE_SCMI_QUIRK(perf_level_get_fc_force, "Qualcomm", NULL, "0x20000-");

/*
 * Quirks Pointers Array
 *
 * This is filled at compile-time with the list of pointers to all the currently
 * defined quirks descriptors.
 */
static struct scmi_quirk *scmi_quirks_table[] = {
	__DECLARE_SCMI_QUIRK_ENTRY(clock_rates_triplet_out_of_spec),
	__DECLARE_SCMI_QUIRK_ENTRY(perf_level_get_fc_force),
	NULL
};

/*
 * Quirks HashTable
 *
 * A run-time populated hashtable containing all the defined quirks descriptors
 * hashed by matching pattern.
 */
static DEFINE_READ_MOSTLY_HASHTABLE(scmi_quirks_ht, SCMI_QUIRKS_HT_SZ);

static unsigned int scmi_quirk_signature(const char *vend, const char *sub_vend)
{
	char *signature, *p;
	unsigned int hash32;
	unsigned long hash = 0;

	/* vendor_id/sub_vendor_id guaranteed <= SCMI_SHORT_NAME_MAX_SIZE */
	signature = kasprintf(GFP_KERNEL, "|%s|%s|", vend ?: "", sub_vend ?: "");
	if (!signature)
		return 0;

	pr_debug("SCMI Quirk Signature >>>%s<<<\n", signature);

	p = signature;
	while (*p)
		hash = partial_name_hash(tolower(*p++), hash);
	hash32 = end_name_hash(hash);

	kfree(signature);

	return hash32;
}

static int scmi_quirk_range_parse(struct scmi_quirk *quirk)
{
	const char *last, *first __free(kfree) = NULL;
	size_t len;
	char *sep;
	int ret;

	quirk->start_range = 0;
	quirk->end_range = 0xFFFFFFFF;
	len = quirk->impl_ver_range ? strlen(quirk->impl_ver_range) : 0;
	if (!len)
		return 0;

	first = kmemdup(quirk->impl_ver_range, len + 1, GFP_KERNEL);
	if (!first)
		return -ENOMEM;

	last = first + len - 1;
	sep = strchr(first, '-');
	if (sep)
		*sep = '\0';

	if (sep == first) /* -X */
		ret = kstrtouint(first + 1, 0, &quirk->end_range);
	else /* X OR X- OR X-y */
		ret = kstrtouint(first, 0, &quirk->start_range);
	if (ret)
		return ret;

	if (!sep)
		quirk->end_range = quirk->start_range;
	else if (sep != last) /* x-Y */
		ret = kstrtouint(sep + 1, 0, &quirk->end_range);

	if (quirk->start_range > quirk->end_range)
		return -EINVAL;

	return ret;
}

void scmi_quirks_initialize(void)
{
	struct scmi_quirk *quirk;
	int i;

	for (i = 0, quirk = scmi_quirks_table[0]; quirk;
	     i++, quirk = scmi_quirks_table[i]) {
		int ret;

		ret = scmi_quirk_range_parse(quirk);
		if (ret) {
			pr_err("SCMI skip QUIRK [%s] - BAD RANGE - |%s|\n",
			       quirk->name, quirk->impl_ver_range);
			continue;
		}
		quirk->hkey = scmi_quirk_signature(quirk->vendor,
						   quirk->sub_vendor_id);

		hash_add(scmi_quirks_ht, &quirk->hash, quirk->hkey);

		pr_debug("Registered SCMI QUIRK [%s] -- %p - Key [0x%08X] - %s/%s/[0x%08X-0x%08X]\n",
			 quirk->name, quirk, quirk->hkey,
			 quirk->vendor, quirk->sub_vendor_id,
			 quirk->start_range, quirk->end_range);
	}

	pr_debug("SCMI Quirks initialized\n");
}

void scmi_quirks_enable(struct device *dev, const char *vend,
			const char *subv, const u32 impl)
{
	for (int i = 3; i >= 0; i--) {
		struct scmi_quirk *quirk;
		unsigned int hkey;

		hkey = scmi_quirk_signature(i > 1 ? vend : NULL,
					    i > 2 ? subv : NULL);

		/*
		 * Note that there could be multiple matches so we
		 * will enable multiple quirk part of a hash collision
		 * domain...BUT we cannot assume that ALL quirks on the
		 * same collision domain are a full match.
		 */
		hash_for_each_possible(scmi_quirks_ht, quirk, hash, hkey) {
			if (quirk->enabled || quirk->hkey != hkey ||
			    impl < quirk->start_range ||
			    impl > quirk->end_range)
				continue;

			if (quirk->compats[0] &&
			    !of_machine_compatible_match(quirk->compats))
				continue;

			dev_info(dev, "Enabling SCMI Quirk [%s]\n",
				 quirk->name);

			dev_dbg(dev,
				"Quirk matched on: %s/%s/%s/[0x%08X-0x%08X]\n",
				quirk->compats[0], quirk->vendor,
				quirk->sub_vendor_id,
				quirk->start_range, quirk->end_range);

			static_branch_enable(quirk->key);
			quirk->enabled = true;
		}
	}
}
