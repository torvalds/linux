// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/export.h>

#include <asm/cpu.h>
#include <asm/cpuid/leaf_types.h>

static unsigned int __x86_family(unsigned int base_fam, unsigned int ext_fam)
{
	if (base_fam == 0xf)
		base_fam += ext_fam;

	return base_fam;
}

static unsigned int __x86_model(unsigned int family, unsigned int base_model,
				unsigned int ext_model)
{
	if (family >= 0x6)
		base_model |= ext_model << 4;

	return base_model;
}

unsigned int x86_family(unsigned int sig)
{
	return __x86_family((sig >> 8) & 0xf, (sig >> 20) & 0xff);
}
EXPORT_SYMBOL_GPL(x86_family);

unsigned int x86_model(unsigned int sig)
{
	return __x86_model(x86_family(sig), (sig >> 4) & 0xf, (sig >> 16) & 0xf);
}
EXPORT_SYMBOL_GPL(x86_model);

unsigned int x86_stepping(unsigned int sig)
{
	return sig & 0xf;
}
EXPORT_SYMBOL_GPL(x86_stepping);

unsigned int cpuid_family(const struct leaf_0x1_0 *l)
{
	return __x86_family(l->base_family_id, l->ext_family);
}

unsigned int cpuid_model(const struct leaf_0x1_0 *l)
{
	return __x86_model(cpuid_family(l), l->base_model, l->ext_model);
}
