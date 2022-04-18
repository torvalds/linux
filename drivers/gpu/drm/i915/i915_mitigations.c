// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_mitigations.h"

static unsigned long mitigations __read_mostly = ~0UL;

enum {
	CLEAR_RESIDUALS = 0,
};

static const char * const names[] = {
	[CLEAR_RESIDUALS] = "residuals",
};

bool i915_mitigate_clear_residuals(void)
{
	return READ_ONCE(mitigations) & BIT(CLEAR_RESIDUALS);
}

static int mitigations_set(const char *val, const struct kernel_param *kp)
{
	unsigned long new = ~0UL;
	char *str, *sep, *tok;
	bool first = true;
	int err = 0;

	BUILD_BUG_ON(ARRAY_SIZE(names) >= BITS_PER_TYPE(mitigations));

	str = kstrdup(val, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	for (sep = str; (tok = strsep(&sep, ","));) {
		bool enable = true;
		int i;

		/* Be tolerant of leading/trailing whitespace */
		tok = strim(tok);

		if (first) {
			first = false;

			if (!strcmp(tok, "auto"))
				continue;

			new = 0;
			if (!strcmp(tok, "off"))
				continue;
		}

		if (*tok == '!') {
			enable = !enable;
			tok++;
		}

		if (!strncmp(tok, "no", 2)) {
			enable = !enable;
			tok += 2;
		}

		if (*tok == '\0')
			continue;

		for (i = 0; i < ARRAY_SIZE(names); i++) {
			if (!strcmp(tok, names[i])) {
				if (enable)
					new |= BIT(i);
				else
					new &= ~BIT(i);
				break;
			}
		}
		if (i == ARRAY_SIZE(names)) {
			pr_err("Bad \"%s.mitigations=%s\", '%s' is unknown\n",
			       DRIVER_NAME, val, tok);
			err = -EINVAL;
			break;
		}
	}
	kfree(str);
	if (err)
		return err;

	WRITE_ONCE(mitigations, new);
	return 0;
}

static int mitigations_get(char *buffer, const struct kernel_param *kp)
{
	unsigned long local = READ_ONCE(mitigations);
	int count, i;
	bool enable;

	if (!local)
		return scnprintf(buffer, PAGE_SIZE, "%s\n", "off");

	if (local & BIT(BITS_PER_LONG - 1)) {
		count = scnprintf(buffer, PAGE_SIZE, "%s,", "auto");
		enable = false;
	} else {
		enable = true;
		count = 0;
	}

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if ((local & BIT(i)) != enable)
			continue;

		count += scnprintf(buffer + count, PAGE_SIZE - count,
				   "%s%s,", enable ? "" : "!", names[i]);
	}

	buffer[count - 1] = '\n';
	return count;
}

static const struct kernel_param_ops ops = {
	.set = mitigations_set,
	.get = mitigations_get,
};

module_param_cb_unsafe(mitigations, &ops, NULL, 0600);
MODULE_PARM_DESC(mitigations,
"Selectively enable security mitigations for all Intel® GPUs in the system.\n"
"\n"
"  auto -- enables all mitigations required for the platform [default]\n"
"  off  -- disables all mitigations\n"
"\n"
"Individual mitigations can be enabled by passing a comma-separated string,\n"
"e.g. mitigations=residuals to enable only clearing residuals or\n"
"mitigations=auto,noresiduals to disable only the clear residual mitigation.\n"
"Either '!' or 'no' may be used to switch from enabling the mitigation to\n"
"disabling it.\n"
"\n"
"Active mitigations for Ivybridge, Baytrail, Haswell:\n"
"  residuals -- clear all thread-local registers between contexts"
);
