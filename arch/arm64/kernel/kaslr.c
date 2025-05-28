// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/cache.h>
#include <linux/init.h>
#include <linux/printk.h>

#include <asm/cpufeature.h>
#include <asm/memory.h>

bool __ro_after_init __kaslr_is_enabled = false;

void __init kaslr_init(void)
{
	if (kaslr_disabled_cmdline()) {
		pr_info("KASLR disabled on command line\n");
		return;
	}

	/*
	 * The KASLR offset modulo MIN_KIMG_ALIGN is taken from the physical
	 * placement of the image rather than from the seed, so a displacement
	 * of less than MIN_KIMG_ALIGN means that no seed was provided.
	 */
	if (kaslr_offset() < MIN_KIMG_ALIGN) {
		pr_warn("KASLR disabled due to lack of seed\n");
		return;
	}

	pr_info("KASLR enabled\n");
	__kaslr_is_enabled = true;
}

static int __init parse_nokaslr(char *unused)
{
	/* nokaslr param handling is done by early cpufeature code */
	return 0;
}
early_param("nokaslr", parse_nokaslr);
