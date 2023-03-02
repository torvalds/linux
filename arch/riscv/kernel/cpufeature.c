// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/bitmap.h>
#include <linux/ctype.h>
#include <linux/libfdt.h>
#include <linux/log2.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/of.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/errata_list.h>
#include <asm/hwcap.h>
#include <asm/patch.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

#define NUM_ALPHA_EXTS ('z' - 'a' + 1)

unsigned long elf_hwcap __read_mostly;

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

/**
 * riscv_isa_extension_base() - Get base extension word
 *
 * @isa_bitmap: ISA bitmap to use
 * Return: base extension word as unsigned long value
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
unsigned long riscv_isa_extension_base(const unsigned long *isa_bitmap)
{
	if (!isa_bitmap)
		return riscv_isa[0];
	return isa_bitmap[0];
}
EXPORT_SYMBOL_GPL(riscv_isa_extension_base);

/**
 * __riscv_isa_extension_available() - Check whether given extension
 * is available or not
 *
 * @isa_bitmap: ISA bitmap to use
 * @bit: bit position of the desired extension
 * Return: true or false
 *
 * NOTE: If isa_bitmap is NULL then Host ISA bitmap will be used.
 */
bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, int bit)
{
	const unsigned long *bmap = (isa_bitmap) ? isa_bitmap : riscv_isa;

	if (bit >= RISCV_ISA_EXT_MAX)
		return false;

	return test_bit(bit, bmap) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_extension_available);

static bool riscv_isa_extension_check(int id)
{
	switch (id) {
	case RISCV_ISA_EXT_ZICBOM:
		if (!riscv_cbom_block_size) {
			pr_err("Zicbom detected in ISA string, but no cbom-block-size found\n");
			return false;
		} else if (!is_power_of_2(riscv_cbom_block_size)) {
			pr_err("cbom-block-size present, but is not a power-of-2\n");
			return false;
		}
		return true;
	}

	return true;
}

void __init riscv_fill_hwcap(void)
{
	struct device_node *node;
	const char *isa;
	char print_str[NUM_ALPHA_EXTS + 1];
	int i, j, rc;
	unsigned long isa2hwcap[26] = {0};
	unsigned long hartid;

	isa2hwcap['i' - 'a'] = COMPAT_HWCAP_ISA_I;
	isa2hwcap['m' - 'a'] = COMPAT_HWCAP_ISA_M;
	isa2hwcap['a' - 'a'] = COMPAT_HWCAP_ISA_A;
	isa2hwcap['f' - 'a'] = COMPAT_HWCAP_ISA_F;
	isa2hwcap['d' - 'a'] = COMPAT_HWCAP_ISA_D;
	isa2hwcap['c' - 'a'] = COMPAT_HWCAP_ISA_C;

	elf_hwcap = 0;

	bitmap_zero(riscv_isa, RISCV_ISA_EXT_MAX);

	for_each_of_cpu_node(node) {
		unsigned long this_hwcap = 0;
		DECLARE_BITMAP(this_isa, RISCV_ISA_EXT_MAX);
		const char *temp;

		rc = riscv_of_processor_hartid(node, &hartid);
		if (rc < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &isa)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}

		temp = isa;
#if IS_ENABLED(CONFIG_32BIT)
		if (!strncmp(isa, "rv32", 4))
			isa += 4;
#elif IS_ENABLED(CONFIG_64BIT)
		if (!strncmp(isa, "rv64", 4))
			isa += 4;
#endif
		/* The riscv,isa DT property must start with rv64 or rv32 */
		if (temp == isa)
			continue;
		bitmap_zero(this_isa, RISCV_ISA_EXT_MAX);
		for (; *isa; ++isa) {
			const char *ext = isa++;
			const char *ext_end = isa;
			bool ext_long = false, ext_err = false;

			switch (*ext) {
			case 's':
				/**
				 * Workaround for invalid single-letter 's' & 'u'(QEMU).
				 * No need to set the bit in riscv_isa as 's' & 'u' are
				 * not valid ISA extensions. It works until multi-letter
				 * extension starting with "Su" appears.
				 */
				if (ext[-1] != '_' && ext[1] == 'u') {
					++isa;
					ext_err = true;
					break;
				}
				fallthrough;
			case 'x':
			case 'z':
				ext_long = true;
				/* Multi-letter extension must be delimited */
				for (; *isa && *isa != '_'; ++isa)
					if (unlikely(!islower(*isa)
						     && !isdigit(*isa)))
						ext_err = true;
				/* Parse backwards */
				ext_end = isa;
				if (unlikely(ext_err))
					break;
				if (!isdigit(ext_end[-1]))
					break;
				/* Skip the minor version */
				while (isdigit(*--ext_end))
					;
				if (ext_end[0] != 'p'
				    || !isdigit(ext_end[-1])) {
					/* Advance it to offset the pre-decrement */
					++ext_end;
					break;
				}
				/* Skip the major version */
				while (isdigit(*--ext_end))
					;
				++ext_end;
				break;
			default:
				if (unlikely(!islower(*ext))) {
					ext_err = true;
					break;
				}
				/* Find next extension */
				if (!isdigit(*isa))
					break;
				/* Skip the minor version */
				while (isdigit(*++isa))
					;
				if (*isa != 'p')
					break;
				if (!isdigit(*++isa)) {
					--isa;
					break;
				}
				/* Skip the major version */
				while (isdigit(*++isa))
					;
				break;
			}
			if (*isa != '_')
				--isa;

#define SET_ISA_EXT_MAP(name, bit)						\
			do {							\
				if ((ext_end - ext == sizeof(name) - 1) &&	\
				     !memcmp(ext, name, sizeof(name) - 1) &&	\
				     riscv_isa_extension_check(bit))		\
					set_bit(bit, this_isa);			\
			} while (false)						\

			if (unlikely(ext_err))
				continue;
			if (!ext_long) {
				int nr = *ext - 'a';

				if (riscv_isa_extension_check(nr)) {
					this_hwcap |= isa2hwcap[nr];
					set_bit(nr, this_isa);
				}
			} else {
				/* sorted alphabetically */
				SET_ISA_EXT_MAP("sscofpmf", RISCV_ISA_EXT_SSCOFPMF);
				SET_ISA_EXT_MAP("sstc", RISCV_ISA_EXT_SSTC);
				SET_ISA_EXT_MAP("svinval", RISCV_ISA_EXT_SVINVAL);
				SET_ISA_EXT_MAP("svpbmt", RISCV_ISA_EXT_SVPBMT);
				SET_ISA_EXT_MAP("zbb", RISCV_ISA_EXT_ZBB);
				SET_ISA_EXT_MAP("zicbom", RISCV_ISA_EXT_ZICBOM);
				SET_ISA_EXT_MAP("zihintpause", RISCV_ISA_EXT_ZIHINTPAUSE);
			}
#undef SET_ISA_EXT_MAP
		}

		/*
		 * All "okay" hart should have same isa. Set HWCAP based on
		 * common capabilities of every "okay" hart, in case they don't
		 * have.
		 */
		if (elf_hwcap)
			elf_hwcap &= this_hwcap;
		else
			elf_hwcap = this_hwcap;

		if (bitmap_empty(riscv_isa, RISCV_ISA_EXT_MAX))
			bitmap_copy(riscv_isa, this_isa, RISCV_ISA_EXT_MAX);
		else
			bitmap_and(riscv_isa, riscv_isa, this_isa, RISCV_ISA_EXT_MAX);
	}

	/* We don't support systems with F but without D, so mask those out
	 * here. */
	if ((elf_hwcap & COMPAT_HWCAP_ISA_F) && !(elf_hwcap & COMPAT_HWCAP_ISA_D)) {
		pr_info("This kernel does not support systems with F but not D\n");
		elf_hwcap &= ~COMPAT_HWCAP_ISA_F;
	}

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < NUM_ALPHA_EXTS; i++)
		if (riscv_isa[0] & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: base ISA extensions %s\n", print_str);

	memset(print_str, 0, sizeof(print_str));
	for (i = 0, j = 0; i < NUM_ALPHA_EXTS; i++)
		if (elf_hwcap & BIT_MASK(i))
			print_str[j++] = (char)('a' + i);
	pr_info("riscv: ELF capabilities %s\n", print_str);
}

#ifdef CONFIG_RISCV_ALTERNATIVE
void __init_or_module riscv_cpufeature_patch_func(struct alt_entry *begin,
						  struct alt_entry *end,
						  unsigned int stage)
{
	struct alt_entry *alt;
	void *oldptr, *altptr;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != 0)
			continue;
		if (alt->errata_id >= RISCV_ISA_EXT_MAX) {
			WARN(1, "This extension id:%d is not in ISA extension list",
				alt->errata_id);
			continue;
		}

		if (!__riscv_isa_extension_available(NULL, alt->errata_id))
			continue;

		oldptr = ALT_OLD_PTR(alt);
		altptr = ALT_ALT_PTR(alt);

		mutex_lock(&text_mutex);
		patch_text_nosync(oldptr, altptr, alt->alt_len);
		riscv_alternative_fix_offsets(oldptr, alt->alt_len, oldptr - altptr);
		mutex_unlock(&text_mutex);
	}
}
#endif
