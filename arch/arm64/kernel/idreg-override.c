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
#define FTR_ALIAS_OPTION_LEN	116

static u64 __boot_status __initdata;

struct ftr_set_desc {
	char 				name[FTR_DESC_NAME_LEN];
	struct arm64_ftr_override	*override;
	struct {
		char			name[FTR_DESC_FIELD_LEN];
		u8			shift;
		u8			width;
		bool			(*filter)(u64 val);
	} 				fields[];
};

#define FIELD(n, s, f)	{ .name = n, .shift = s, .width = 4, .filter = f }

static bool __init mmfr1_vh_filter(u64 val)
{
	/*
	 * If we ever reach this point while running VHE, we're
	 * guaranteed to be on one of these funky, VHE-stuck CPUs. If
	 * the user was trying to force nVHE on us, proceed with
	 * attitude adjustment.
	 */
	return !(__boot_status == (BOOT_CPU_FLAG_E2H | BOOT_CPU_MODE_EL2) &&
		 val == 0);
}

static const struct ftr_set_desc mmfr1 __initconst = {
	.name		= "id_aa64mmfr1",
	.override	= &id_aa64mmfr1_override,
	.fields		= {
		FIELD("vh", ID_AA64MMFR1_EL1_VH_SHIFT, mmfr1_vh_filter),
		{}
	},
};

static bool __init pfr0_sve_filter(u64 val)
{
	/*
	 * Disabling SVE also means disabling all the features that
	 * are associated with it. The easiest way to do it is just to
	 * override id_aa64zfr0_el1 to be 0.
	 */
	if (!val) {
		id_aa64zfr0_override.val = 0;
		id_aa64zfr0_override.mask = GENMASK(63, 0);
	}

	return true;
}

static const struct ftr_set_desc pfr0 __initconst = {
	.name		= "id_aa64pfr0",
	.override	= &id_aa64pfr0_override,
	.fields		= {
	        FIELD("sve", ID_AA64PFR0_EL1_SVE_SHIFT, pfr0_sve_filter),
		{}
	},
};

static bool __init pfr1_sme_filter(u64 val)
{
	/*
	 * Similarly to SVE, disabling SME also means disabling all
	 * the features that are associated with it. Just set
	 * id_aa64smfr0_el1 to 0 and don't look back.
	 */
	if (!val) {
		id_aa64smfr0_override.val = 0;
		id_aa64smfr0_override.mask = GENMASK(63, 0);
	}

	return true;
}

static const struct ftr_set_desc pfr1 __initconst = {
	.name		= "id_aa64pfr1",
	.override	= &id_aa64pfr1_override,
	.fields		= {
		FIELD("bt", ID_AA64PFR1_EL1_BT_SHIFT, NULL ),
		FIELD("mte", ID_AA64PFR1_EL1_MTE_SHIFT, NULL),
		FIELD("sme", ID_AA64PFR1_EL1_SME_SHIFT, pfr1_sme_filter),
		{}
	},
};

static const struct ftr_set_desc isar1 __initconst = {
	.name		= "id_aa64isar1",
	.override	= &id_aa64isar1_override,
	.fields		= {
		FIELD("gpi", ID_AA64ISAR1_EL1_GPI_SHIFT, NULL),
		FIELD("gpa", ID_AA64ISAR1_EL1_GPA_SHIFT, NULL),
		FIELD("api", ID_AA64ISAR1_EL1_API_SHIFT, NULL),
		FIELD("apa", ID_AA64ISAR1_EL1_APA_SHIFT, NULL),
		{}
	},
};

static const struct ftr_set_desc isar2 __initconst = {
	.name		= "id_aa64isar2",
	.override	= &id_aa64isar2_override,
	.fields		= {
		FIELD("gpa3", ID_AA64ISAR2_EL1_GPA3_SHIFT, NULL),
		FIELD("apa3", ID_AA64ISAR2_EL1_APA3_SHIFT, NULL),
		FIELD("mops", ID_AA64ISAR2_EL1_MOPS_SHIFT, NULL),
		{}
	},
};

static const struct ftr_set_desc smfr0 __initconst = {
	.name		= "id_aa64smfr0",
	.override	= &id_aa64smfr0_override,
	.fields		= {
		FIELD("smever", ID_AA64SMFR0_EL1_SMEver_SHIFT, NULL),
		/* FA64 is a one bit field... :-/ */
		{ "fa64", ID_AA64SMFR0_EL1_FA64_SHIFT, 1, },
		{}
	},
};

static bool __init hvhe_filter(u64 val)
{
	u64 mmfr1 = read_sysreg(id_aa64mmfr1_el1);

	return (val == 1 &&
		lower_32_bits(__boot_status) == BOOT_CPU_MODE_EL2 &&
		cpuid_feature_extract_unsigned_field(mmfr1,
						     ID_AA64MMFR1_EL1_VH_SHIFT));
}

static const struct ftr_set_desc sw_features __initconst = {
	.name		= "arm64_sw",
	.override	= &arm64_sw_feature_override,
	.fields		= {
		FIELD("nokaslr", ARM64_SW_FEATURE_OVERRIDE_NOKASLR, NULL),
		FIELD("hvhe", ARM64_SW_FEATURE_OVERRIDE_HVHE, hvhe_filter),
		{}
	},
};

static const struct ftr_set_desc * const regs[] __initconst = {
	&mmfr1,
	&pfr0,
	&pfr1,
	&isar1,
	&isar2,
	&smfr0,
	&sw_features,
};

static const struct {
	char	alias[FTR_ALIAS_NAME_LEN];
	char	feature[FTR_ALIAS_OPTION_LEN];
} aliases[] __initconst = {
	{ "kvm-arm.mode=nvhe",		"id_aa64mmfr1.vh=0" },
	{ "kvm-arm.mode=protected",	"id_aa64mmfr1.vh=0" },
	{ "arm64.nosve",		"id_aa64pfr0.sve=0" },
	{ "arm64.nosme",		"id_aa64pfr1.sme=0" },
	{ "arm64.nobti",		"id_aa64pfr1.bt=0" },
	{ "arm64.nopauth",
	  "id_aa64isar1.gpi=0 id_aa64isar1.gpa=0 "
	  "id_aa64isar1.api=0 id_aa64isar1.apa=0 "
	  "id_aa64isar2.gpa3=0 id_aa64isar2.apa3=0"	   },
	{ "arm64.nomops",		"id_aa64isar2.mops=0" },
	{ "arm64.nomte",		"id_aa64pfr1.mte=0" },
	{ "nokaslr",			"arm64_sw.nokaslr=1" },
};

static int __init parse_nokaslr(char *unused)
{
	/* nokaslr param handling is done by early cpufeature code */
	return 0;
}
early_param("nokaslr", parse_nokaslr);

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
			u64 width = regs[i]->fields[f].width ?: 4;
			u64 mask = GENMASK_ULL(shift + width - 1, shift);
			u64 v;

			if (find_field(cmdline, regs[i], f, &v))
				continue;

			/*
			 * If an override gets filtered out, advertise
			 * it by setting the value to the all-ones while
			 * clearing the mask... Yes, this is fragile.
			 */
			if (regs[i]->fields[f].filter &&
			    !regs[i]->fields[f].filter(v)) {
				regs[i]->override->val  |= mask;
				regs[i]->override->mask &= ~mask;
				continue;
			}

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

		len = strscpy(buf, cmdline, ARRAY_SIZE(buf));
		if (len == -E2BIG)
			len = ARRAY_SIZE(buf) - 1;

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

	if (IS_ENABLED(CONFIG_CMDLINE_FORCE) || !prop)
		__parse_cmdline(CONFIG_CMDLINE, true);

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE) && prop)
		__parse_cmdline(prop, true);
}

/* Keep checkers quiet */
void init_feature_override(u64 boot_status);

asmlinkage void __init init_feature_override(u64 boot_status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (regs[i]->override) {
			regs[i]->override->val  = 0;
			regs[i]->override->mask = 0;
		}
	}

	__boot_status = boot_status;

	parse_cmdline();

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (regs[i]->override)
			dcache_clean_inval_poc((unsigned long)regs[i]->override,
					    (unsigned long)regs[i]->override +
					    sizeof(*regs[i]->override));
	}
}
