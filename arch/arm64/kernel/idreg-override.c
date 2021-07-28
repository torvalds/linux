// SPDX-License-Identifier: GPL-2.0
/*
 * Early cpufeature override framework
 *
 * Copyright (C) 2020 Google LLC
 * Author: Marc Zyngier <maz@kernel.org>
 */

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>

#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/setup.h>

#define FTR_DESC_NAME_LEN	20
#define FTR_DESC_FIELD_LEN	10
#define FTR_ALIAS_NAME_LEN	30
#define FTR_ALIAS_OPTION_LEN	80

struct ftr_set_desc {
	char 				name[FTR_DESC_NAME_LEN];
	struct arm64_ftr_override	*override;
	struct {
		char			name[FTR_DESC_FIELD_LEN];
		u8			shift;
	} 				fields[];
};

static const struct ftr_set_desc mmfr1 __initconst = {
	.name		= "id_aa64mmfr1",
	.override	= &id_aa64mmfr1_override,
	.fields		= {
	        { "vh", ID_AA64MMFR1_VHE_SHIFT },
		{}
	},
};

static const struct ftr_set_desc pfr1 __initconst = {
	.name		= "id_aa64pfr1",
	.override	= &id_aa64pfr1_override,
	.fields		= {
	        { "bt", ID_AA64PFR1_BT_SHIFT },
		{ "mte", ID_AA64PFR1_MTE_SHIFT},
		{}
	},
};

static const struct ftr_set_desc isar1 __initconst = {
	.name		= "id_aa64isar1",
	.override	= &id_aa64isar1_override,
	.fields		= {
	        { "gpi", ID_AA64ISAR1_GPI_SHIFT },
	        { "gpa", ID_AA64ISAR1_GPA_SHIFT },
	        { "api", ID_AA64ISAR1_API_SHIFT },
	        { "apa", ID_AA64ISAR1_APA_SHIFT },
		{}
	},
};

extern struct arm64_ftr_override kaslr_feature_override;

static const struct ftr_set_desc kaslr __initconst = {
	.name		= "kaslr",
#ifdef CONFIG_RANDOMIZE_BASE
	.override	= &kaslr_feature_override,
#endif
	.fields		= {
		{ "disabled", 0 },
		{}
	},
};

static const struct ftr_set_desc * const regs[] __initconst = {
	&mmfr1,
	&pfr1,
	&isar1,
	&kaslr,
};

static const struct {
	char	alias[FTR_ALIAS_NAME_LEN];
	char	feature[FTR_ALIAS_OPTION_LEN];
} aliases[] __initconst = {
	{ "kvm-arm.mode=nvhe",		"id_aa64mmfr1.vh=0" },
	{ "kvm-arm.mode=protected",	"id_aa64mmfr1.vh=0" },
	{ "arm64.nobti",		"id_aa64pfr1.bt=0" },
	{ "arm64.nopauth",
	  "id_aa64isar1.gpi=0 id_aa64isar1.gpa=0 "
	  "id_aa64isar1.api=0 id_aa64isar1.apa=0"	   },
	{ "arm64.nomte",		"id_aa64pfr1.mte=0" },
	{ "nokaslr",			"kaslr.disabled=1" },
};

static int __init find_field(const char *cmdline,
			     const struct ftr_set_desc *reg, int f, u64 *v)
{
	char opt[FTR_DESC_NAME_LEN + FTR_DESC_FIELD_LEN + 2];
	int len;

	len = snprintf(opt, ARRAY_SIZE(opt), "%s.%s=",
		       reg->name, reg->fields[f].name);

	if (!parameqn(cmdline, opt, len))
		return -1;

	return kstrtou64(cmdline + len, 0, v);
}

static void __init match_options(const char *cmdline)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		int f;

		if (!regs[i]->override)
			continue;

		for (f = 0; strlen(regs[i]->fields[f].name); f++) {
			u64 shift = regs[i]->fields[f].shift;
			u64 mask = 0xfUL << shift;
			u64 v;

			if (find_field(cmdline, regs[i], f, &v))
				continue;

			regs[i]->override->val  &= ~mask;
			regs[i]->override->val  |= (v << shift) & mask;
			regs[i]->override->mask |= mask;

			return;
		}
	}
}

static __init void __parse_cmdline(const char *cmdline, bool parse_aliases)
{
	do {
		char buf[256];
		size_t len;
		int i;

		cmdline = skip_spaces(cmdline);

		for (len = 0; cmdline[len] && !isspace(cmdline[len]); len++);
		if (!len)
			return;

		len = min(len, ARRAY_SIZE(buf) - 1);
		strncpy(buf, cmdline, len);
		buf[len] = 0;

		if (strcmp(buf, "--") == 0)
			return;

		cmdline += len;

		match_options(buf);

		for (i = 0; parse_aliases && i < ARRAY_SIZE(aliases); i++)
			if (parameq(buf, aliases[i].alias))
				__parse_cmdline(aliases[i].feature, false);
	} while (1);
}

static __init const u8 *get_bootargs_cmdline(void)
{
	const u8 *prop;
	void *fdt;
	int node;

	fdt = get_early_fdt_ptr();
	if (!fdt)
		return NULL;

	node = fdt_path_offset(fdt, "/chosen");
	if (node < 0)
		return NULL;

	prop = fdt_getprop(fdt, node, "bootargs", NULL);
	if (!prop)
		return NULL;

	return strlen(prop) ? prop : NULL;
}

static __init void parse_cmdline(void)
{
	const u8 *prop = get_bootargs_cmdline();

	if (IS_ENABLED(CONFIG_CMDLINE_EXTEND) ||
	    IS_ENABLED(CONFIG_CMDLINE_FORCE) ||
	    !prop) {
		__parse_cmdline(CONFIG_CMDLINE, true);
	}

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE) && prop)
		__parse_cmdline(prop, true);
}

/* Keep checkers quiet */
void init_feature_override(void);

asmlinkage void __init init_feature_override(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (regs[i]->override) {
			regs[i]->override->val  = 0;
			regs[i]->override->mask = 0;
		}
	}

	parse_cmdline();

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (regs[i]->override)
			__flush_dcache_area(regs[i]->override,
					    sizeof(*regs[i]->override));
	}
}
