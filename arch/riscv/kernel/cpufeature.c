// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/cpuhotplug.h>
#include <linux/ctype.h>
#include <linux/log2.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/of.h>
#include <asm/acpi.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/hwcap.h>
#include <asm/hwprobe.h>
#include <asm/patch.h>
#include <asm/processor.h>
#include <asm/vector.h>

#include "copy-unaligned.h"

#define NUM_ALPHA_EXTS ('z' - 'a' + 1)

#define MISALIGNED_ACCESS_JIFFIES_LG2 1
#define MISALIGNED_BUFFER_SIZE 0x4000
#define MISALIGNED_BUFFER_ORDER get_order(MISALIGNED_BUFFER_SIZE)
#define MISALIGNED_COPY_SIZE ((MISALIGNED_BUFFER_SIZE / 2) - 0x80)

unsigned long elf_hwcap __read_mostly;

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

/* Per-cpu ISA extensions. */
struct riscv_isainfo hart_isa[NR_CPUS];

/* Performance information */
DEFINE_PER_CPU(long, misaligned_access_speed);

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
			pr_err("Zicbom detected in ISA string, disabling as no cbom-block-size found\n");
			return false;
		} else if (!is_power_of_2(riscv_cbom_block_size)) {
			pr_err("Zicbom disabled as cbom-block-size present, but is not a power-of-2\n");
			return false;
		}
		return true;
	case RISCV_ISA_EXT_ZICBOZ:
		if (!riscv_cboz_block_size) {
			pr_err("Zicboz detected in ISA string, disabling as no cboz-block-size found\n");
			return false;
		} else if (!is_power_of_2(riscv_cboz_block_size)) {
			pr_err("Zicboz disabled as cboz-block-size present, but is not a power-of-2\n");
			return false;
		}
		return true;
	}

	return true;
}

#define __RISCV_ISA_EXT_DATA(_name, _id) {	\
	.name = #_name,				\
	.property = #_name,			\
	.id = _id,				\
}

/*
 * The canonical order of ISA extension names in the ISA string is defined in
 * chapter 27 of the unprivileged specification.
 *
 * Ordinarily, for in-kernel data structures, this order is unimportant but
 * isa_ext_arr defines the order of the ISA string in /proc/cpuinfo.
 *
 * The specification uses vague wording, such as should, when it comes to
 * ordering, so for our purposes the following rules apply:
 *
 * 1. All multi-letter extensions must be separated from other extensions by an
 *    underscore.
 *
 * 2. Additional standard extensions (starting with 'Z') must be sorted after
 *    single-letter extensions and before any higher-privileged extensions.
 *
 * 3. The first letter following the 'Z' conventionally indicates the most
 *    closely related alphabetical extension category, IMAFDQLCBKJTPVH.
 *    If multiple 'Z' extensions are named, they must be ordered first by
 *    category, then alphabetically within a category.
 *
 * 3. Standard supervisor-level extensions (starting with 'S') must be listed
 *    after standard unprivileged extensions.  If multiple supervisor-level
 *    extensions are listed, they must be ordered alphabetically.
 *
 * 4. Standard machine-level extensions (starting with 'Zxm') must be listed
 *    after any lower-privileged, standard extensions.  If multiple
 *    machine-level extensions are listed, they must be ordered
 *    alphabetically.
 *
 * 5. Non-standard extensions (starting with 'X') must be listed after all
 *    standard extensions. If multiple non-standard extensions are listed, they
 *    must be ordered alphabetically.
 *
 * An example string following the order is:
 *    rv64imadc_zifoo_zigoo_zafoo_sbar_scar_zxmbaz_xqux_xrux
 *
 * New entries to this struct should follow the ordering rules described above.
 */
const struct riscv_isa_ext_data riscv_isa_ext[] = {
	__RISCV_ISA_EXT_DATA(i, RISCV_ISA_EXT_i),
	__RISCV_ISA_EXT_DATA(m, RISCV_ISA_EXT_m),
	__RISCV_ISA_EXT_DATA(a, RISCV_ISA_EXT_a),
	__RISCV_ISA_EXT_DATA(f, RISCV_ISA_EXT_f),
	__RISCV_ISA_EXT_DATA(d, RISCV_ISA_EXT_d),
	__RISCV_ISA_EXT_DATA(q, RISCV_ISA_EXT_q),
	__RISCV_ISA_EXT_DATA(c, RISCV_ISA_EXT_c),
	__RISCV_ISA_EXT_DATA(b, RISCV_ISA_EXT_b),
	__RISCV_ISA_EXT_DATA(k, RISCV_ISA_EXT_k),
	__RISCV_ISA_EXT_DATA(j, RISCV_ISA_EXT_j),
	__RISCV_ISA_EXT_DATA(p, RISCV_ISA_EXT_p),
	__RISCV_ISA_EXT_DATA(v, RISCV_ISA_EXT_v),
	__RISCV_ISA_EXT_DATA(h, RISCV_ISA_EXT_h),
	__RISCV_ISA_EXT_DATA(zicbom, RISCV_ISA_EXT_ZICBOM),
	__RISCV_ISA_EXT_DATA(zicboz, RISCV_ISA_EXT_ZICBOZ),
	__RISCV_ISA_EXT_DATA(zicntr, RISCV_ISA_EXT_ZICNTR),
	__RISCV_ISA_EXT_DATA(zicond, RISCV_ISA_EXT_ZICOND),
	__RISCV_ISA_EXT_DATA(zicsr, RISCV_ISA_EXT_ZICSR),
	__RISCV_ISA_EXT_DATA(zifencei, RISCV_ISA_EXT_ZIFENCEI),
	__RISCV_ISA_EXT_DATA(zihintpause, RISCV_ISA_EXT_ZIHINTPAUSE),
	__RISCV_ISA_EXT_DATA(zihpm, RISCV_ISA_EXT_ZIHPM),
	__RISCV_ISA_EXT_DATA(zba, RISCV_ISA_EXT_ZBA),
	__RISCV_ISA_EXT_DATA(zbb, RISCV_ISA_EXT_ZBB),
	__RISCV_ISA_EXT_DATA(zbs, RISCV_ISA_EXT_ZBS),
	__RISCV_ISA_EXT_DATA(smaia, RISCV_ISA_EXT_SMAIA),
	__RISCV_ISA_EXT_DATA(smstateen, RISCV_ISA_EXT_SMSTATEEN),
	__RISCV_ISA_EXT_DATA(ssaia, RISCV_ISA_EXT_SSAIA),
	__RISCV_ISA_EXT_DATA(sscofpmf, RISCV_ISA_EXT_SSCOFPMF),
	__RISCV_ISA_EXT_DATA(sstc, RISCV_ISA_EXT_SSTC),
	__RISCV_ISA_EXT_DATA(svinval, RISCV_ISA_EXT_SVINVAL),
	__RISCV_ISA_EXT_DATA(svnapot, RISCV_ISA_EXT_SVNAPOT),
	__RISCV_ISA_EXT_DATA(svpbmt, RISCV_ISA_EXT_SVPBMT),
};

const size_t riscv_isa_ext_count = ARRAY_SIZE(riscv_isa_ext);

static void __init riscv_parse_isa_string(unsigned long *this_hwcap, struct riscv_isainfo *isainfo,
					  unsigned long *isa2hwcap, const char *isa)
{
	/*
	 * For all possible cpus, we have already validated in
	 * the boot process that they at least contain "rv" and
	 * whichever of "32"/"64" this kernel supports, and so this
	 * section can be skipped.
	 */
	isa += 4;

	while (*isa) {
		const char *ext = isa++;
		const char *ext_end = isa;
		bool ext_long = false, ext_err = false;

		switch (*ext) {
		case 's':
			/*
			 * Workaround for invalid single-letter 's' & 'u' (QEMU).
			 * No need to set the bit in riscv_isa as 's' & 'u' are
			 * not valid ISA extensions. It works unless the first
			 * multi-letter extension in the ISA string begins with
			 * "Su" and is not prefixed with an underscore.
			 */
			if (ext[-1] != '_' && ext[1] == 'u') {
				++isa;
				ext_err = true;
				break;
			}
			fallthrough;
		case 'S':
		case 'x':
		case 'X':
		case 'z':
		case 'Z':
			/*
			 * Before attempting to parse the extension itself, we find its end.
			 * As multi-letter extensions must be split from other multi-letter
			 * extensions with an "_", the end of a multi-letter extension will
			 * either be the null character or the "_" at the start of the next
			 * multi-letter extension.
			 *
			 * Next, as the extensions version is currently ignored, we
			 * eliminate that portion. This is done by parsing backwards from
			 * the end of the extension, removing any numbers. This may be a
			 * major or minor number however, so the process is repeated if a
			 * minor number was found.
			 *
			 * ext_end is intended to represent the first character *after* the
			 * name portion of an extension, but will be decremented to the last
			 * character itself while eliminating the extensions version number.
			 * A simple re-increment solves this problem.
			 */
			ext_long = true;
			for (; *isa && *isa != '_'; ++isa)
				if (unlikely(!isalnum(*isa)))
					ext_err = true;

			ext_end = isa;
			if (unlikely(ext_err))
				break;

			if (!isdigit(ext_end[-1]))
				break;

			while (isdigit(*--ext_end))
				;

			if (tolower(ext_end[0]) != 'p' || !isdigit(ext_end[-1])) {
				++ext_end;
				break;
			}

			while (isdigit(*--ext_end))
				;

			++ext_end;
			break;
		default:
			/*
			 * Things are a little easier for single-letter extensions, as they
			 * are parsed forwards.
			 *
			 * After checking that our starting position is valid, we need to
			 * ensure that, when isa was incremented at the start of the loop,
			 * that it arrived at the start of the next extension.
			 *
			 * If we are already on a non-digit, there is nothing to do. Either
			 * we have a multi-letter extension's _, or the start of an
			 * extension.
			 *
			 * Otherwise we have found the current extension's major version
			 * number. Parse past it, and a subsequent p/minor version number
			 * if present. The `p` extension must not appear immediately after
			 * a number, so there is no fear of missing it.
			 *
			 */
			if (unlikely(!isalpha(*ext))) {
				ext_err = true;
				break;
			}

			if (!isdigit(*isa))
				break;

			while (isdigit(*++isa))
				;

			if (tolower(*isa) != 'p')
				break;

			if (!isdigit(*++isa)) {
				--isa;
				break;
			}

			while (isdigit(*++isa))
				;

			break;
		}

		/*
		 * The parser expects that at the start of an iteration isa points to the
		 * first character of the next extension. As we stop parsing an extension
		 * on meeting a non-alphanumeric character, an extra increment is needed
		 * where the succeeding extension is a multi-letter prefixed with an "_".
		 */
		if (*isa == '_')
			++isa;

#define SET_ISA_EXT_MAP(name, bit)						\
		do {								\
			if ((ext_end - ext == strlen(name)) &&			\
			     !strncasecmp(ext, name, strlen(name)) &&		\
			     riscv_isa_extension_check(bit))			\
				set_bit(bit, isainfo->isa);			\
		} while (false)							\

		if (unlikely(ext_err))
			continue;
		if (!ext_long) {
			int nr = tolower(*ext) - 'a';

			if (riscv_isa_extension_check(nr)) {
				*this_hwcap |= isa2hwcap[nr];
				set_bit(nr, isainfo->isa);
			}
		} else {
			for (int i = 0; i < riscv_isa_ext_count; i++)
				SET_ISA_EXT_MAP(riscv_isa_ext[i].name,
						riscv_isa_ext[i].id);
		}
#undef SET_ISA_EXT_MAP
	}
}

static void __init riscv_fill_hwcap_from_isa_string(unsigned long *isa2hwcap)
{
	struct device_node *node;
	const char *isa;
	int rc;
	struct acpi_table_header *rhct;
	acpi_status status;
	unsigned int cpu;

	if (!acpi_disabled) {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, &rhct);
		if (ACPI_FAILURE(status))
			return;
	}

	for_each_possible_cpu(cpu) {
		struct riscv_isainfo *isainfo = &hart_isa[cpu];
		unsigned long this_hwcap = 0;

		if (acpi_disabled) {
			node = of_cpu_device_node_get(cpu);
			if (!node) {
				pr_warn("Unable to find cpu node\n");
				continue;
			}

			rc = of_property_read_string(node, "riscv,isa", &isa);
			of_node_put(node);
			if (rc) {
				pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
				continue;
			}
		} else {
			rc = acpi_get_riscv_isa(rhct, cpu, &isa);
			if (rc < 0) {
				pr_warn("Unable to get ISA for the hart - %d\n", cpu);
				continue;
			}
		}

		riscv_parse_isa_string(&this_hwcap, isainfo, isa2hwcap, isa);

		/*
		 * These ones were as they were part of the base ISA when the
		 * port & dt-bindings were upstreamed, and so can be set
		 * unconditionally where `i` is in riscv,isa on DT systems.
		 */
		if (acpi_disabled) {
			set_bit(RISCV_ISA_EXT_ZICSR, isainfo->isa);
			set_bit(RISCV_ISA_EXT_ZIFENCEI, isainfo->isa);
			set_bit(RISCV_ISA_EXT_ZICNTR, isainfo->isa);
			set_bit(RISCV_ISA_EXT_ZIHPM, isainfo->isa);
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
			bitmap_copy(riscv_isa, isainfo->isa, RISCV_ISA_EXT_MAX);
		else
			bitmap_and(riscv_isa, riscv_isa, isainfo->isa, RISCV_ISA_EXT_MAX);
	}

	if (!acpi_disabled && rhct)
		acpi_put_table((struct acpi_table_header *)rhct);
}

static int __init riscv_fill_hwcap_from_ext_list(unsigned long *isa2hwcap)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		unsigned long this_hwcap = 0;
		struct device_node *cpu_node;
		struct riscv_isainfo *isainfo = &hart_isa[cpu];

		cpu_node = of_cpu_device_node_get(cpu);
		if (!cpu_node) {
			pr_warn("Unable to find cpu node\n");
			continue;
		}

		if (!of_property_present(cpu_node, "riscv,isa-extensions")) {
			of_node_put(cpu_node);
			continue;
		}

		for (int i = 0; i < riscv_isa_ext_count; i++) {
			if (of_property_match_string(cpu_node, "riscv,isa-extensions",
						     riscv_isa_ext[i].property) < 0)
				continue;

			if (!riscv_isa_extension_check(riscv_isa_ext[i].id))
				continue;

			/* Only single letter extensions get set in hwcap */
			if (strnlen(riscv_isa_ext[i].name, 2) == 1)
				this_hwcap |= isa2hwcap[riscv_isa_ext[i].id];

			set_bit(riscv_isa_ext[i].id, isainfo->isa);
		}

		of_node_put(cpu_node);

		/*
		 * All "okay" harts should have same isa. Set HWCAP based on
		 * common capabilities of every "okay" hart, in case they don't.
		 */
		if (elf_hwcap)
			elf_hwcap &= this_hwcap;
		else
			elf_hwcap = this_hwcap;

		if (bitmap_empty(riscv_isa, RISCV_ISA_EXT_MAX))
			bitmap_copy(riscv_isa, isainfo->isa, RISCV_ISA_EXT_MAX);
		else
			bitmap_and(riscv_isa, riscv_isa, isainfo->isa, RISCV_ISA_EXT_MAX);
	}

	if (bitmap_empty(riscv_isa, RISCV_ISA_EXT_MAX))
		return -ENOENT;

	return 0;
}

#ifdef CONFIG_RISCV_ISA_FALLBACK
bool __initdata riscv_isa_fallback = true;
#else
bool __initdata riscv_isa_fallback;
static int __init riscv_isa_fallback_setup(char *__unused)
{
	riscv_isa_fallback = true;
	return 1;
}
early_param("riscv_isa_fallback", riscv_isa_fallback_setup);
#endif

void __init riscv_fill_hwcap(void)
{
	char print_str[NUM_ALPHA_EXTS + 1];
	unsigned long isa2hwcap[26] = {0};
	int i, j;

	isa2hwcap['i' - 'a'] = COMPAT_HWCAP_ISA_I;
	isa2hwcap['m' - 'a'] = COMPAT_HWCAP_ISA_M;
	isa2hwcap['a' - 'a'] = COMPAT_HWCAP_ISA_A;
	isa2hwcap['f' - 'a'] = COMPAT_HWCAP_ISA_F;
	isa2hwcap['d' - 'a'] = COMPAT_HWCAP_ISA_D;
	isa2hwcap['c' - 'a'] = COMPAT_HWCAP_ISA_C;
	isa2hwcap['v' - 'a'] = COMPAT_HWCAP_ISA_V;

	if (!acpi_disabled) {
		riscv_fill_hwcap_from_isa_string(isa2hwcap);
	} else {
		int ret = riscv_fill_hwcap_from_ext_list(isa2hwcap);

		if (ret && riscv_isa_fallback) {
			pr_info("Falling back to deprecated \"riscv,isa\"\n");
			riscv_fill_hwcap_from_isa_string(isa2hwcap);
		}
	}

	/*
	 * We don't support systems with F but without D, so mask those out
	 * here.
	 */
	if ((elf_hwcap & COMPAT_HWCAP_ISA_F) && !(elf_hwcap & COMPAT_HWCAP_ISA_D)) {
		pr_info("This kernel does not support systems with F but not D\n");
		elf_hwcap &= ~COMPAT_HWCAP_ISA_F;
	}

	if (elf_hwcap & COMPAT_HWCAP_ISA_V) {
		riscv_v_setup_vsize();
		/*
		 * ISA string in device tree might have 'v' flag, but
		 * CONFIG_RISCV_ISA_V is disabled in kernel.
		 * Clear V flag in elf_hwcap if CONFIG_RISCV_ISA_V is disabled.
		 */
		if (!IS_ENABLED(CONFIG_RISCV_ISA_V))
			elf_hwcap &= ~COMPAT_HWCAP_ISA_V;
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

unsigned long riscv_get_elf_hwcap(void)
{
	unsigned long hwcap;

	hwcap = (elf_hwcap & ((1UL << RISCV_ISA_EXT_BASE) - 1));

	if (!riscv_v_vstate_ctrl_user_allowed())
		hwcap &= ~COMPAT_HWCAP_ISA_V;

	return hwcap;
}

static int check_unaligned_access(void *param)
{
	int cpu = smp_processor_id();
	u64 start_cycles, end_cycles;
	u64 word_cycles;
	u64 byte_cycles;
	int ratio;
	unsigned long start_jiffies, now;
	struct page *page = param;
	void *dst;
	void *src;
	long speed = RISCV_HWPROBE_MISALIGNED_SLOW;

	if (check_unaligned_access_emulated(cpu))
		return 0;

	/* Make an unaligned destination buffer. */
	dst = (void *)((unsigned long)page_address(page) | 0x1);
	/* Unalign src as well, but differently (off by 1 + 2 = 3). */
	src = dst + (MISALIGNED_BUFFER_SIZE / 2);
	src += 2;
	word_cycles = -1ULL;
	/* Do a warmup. */
	__riscv_copy_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);
	preempt_disable();
	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	/*
	 * For a fixed amount of time, repeatedly try the function, and take
	 * the best time in cycles as the measurement.
	 */
	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		/* Ensure the CSR read can't reorder WRT to the copy. */
		mb();
		__riscv_copy_words_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		/* Ensure the copy ends before the end time is snapped. */
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < word_cycles)
			word_cycles = end_cycles - start_cycles;
	}

	byte_cycles = -1ULL;
	__riscv_copy_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
	start_jiffies = jiffies;
	while ((now = jiffies) == start_jiffies)
		cpu_relax();

	while (time_before(jiffies, now + (1 << MISALIGNED_ACCESS_JIFFIES_LG2))) {
		start_cycles = get_cycles64();
		mb();
		__riscv_copy_bytes_unaligned(dst, src, MISALIGNED_COPY_SIZE);
		mb();
		end_cycles = get_cycles64();
		if ((end_cycles - start_cycles) < byte_cycles)
			byte_cycles = end_cycles - start_cycles;
	}

	preempt_enable();

	/* Don't divide by zero. */
	if (!word_cycles || !byte_cycles) {
		pr_warn("cpu%d: rdtime lacks granularity needed to measure unaligned access speed\n",
			cpu);

		return 0;
	}

	if (word_cycles < byte_cycles)
		speed = RISCV_HWPROBE_MISALIGNED_FAST;

	ratio = div_u64((byte_cycles * 100), word_cycles);
	pr_info("cpu%d: Ratio of byte access time to unaligned word access is %d.%02d, unaligned accesses are %s\n",
		cpu,
		ratio / 100,
		ratio % 100,
		(speed == RISCV_HWPROBE_MISALIGNED_FAST) ? "fast" : "slow");

	per_cpu(misaligned_access_speed, cpu) = speed;
	return 0;
}

static void check_unaligned_access_nonboot_cpu(void *param)
{
	unsigned int cpu = smp_processor_id();
	struct page **pages = param;

	if (smp_processor_id() != 0)
		check_unaligned_access(pages[cpu]);
}

static int riscv_online_cpu(unsigned int cpu)
{
	static struct page *buf;

	/* We are already set since the last check */
	if (per_cpu(misaligned_access_speed, cpu) != RISCV_HWPROBE_MISALIGNED_UNKNOWN)
		return 0;

	buf = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
	if (!buf) {
		pr_warn("Allocation failure, not measuring misaligned performance\n");
		return -ENOMEM;
	}

	check_unaligned_access(buf);
	__free_pages(buf, MISALIGNED_BUFFER_ORDER);
	return 0;
}

/* Measure unaligned access on all CPUs present at boot in parallel. */
static int check_unaligned_access_all_cpus(void)
{
	unsigned int cpu;
	unsigned int cpu_count = num_possible_cpus();
	struct page **bufs = kzalloc(cpu_count * sizeof(struct page *),
				     GFP_KERNEL);

	if (!bufs) {
		pr_warn("Allocation failure, not measuring misaligned performance\n");
		return 0;
	}

	/*
	 * Allocate separate buffers for each CPU so there's no fighting over
	 * cache lines.
	 */
	for_each_cpu(cpu, cpu_online_mask) {
		bufs[cpu] = alloc_pages(GFP_KERNEL, MISALIGNED_BUFFER_ORDER);
		if (!bufs[cpu]) {
			pr_warn("Allocation failure, not measuring misaligned performance\n");
			goto out;
		}
	}

	/* Check everybody except 0, who stays behind to tend jiffies. */
	on_each_cpu(check_unaligned_access_nonboot_cpu, bufs, 1);

	/* Check core 0. */
	smp_call_on_cpu(0, check_unaligned_access, bufs[0], true);

	/* Setup hotplug callback for any new CPUs that come online. */
	cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "riscv:online",
				  riscv_online_cpu, NULL);

out:
	unaligned_emulation_finish();
	for_each_cpu(cpu, cpu_online_mask) {
		if (bufs[cpu])
			__free_pages(bufs[cpu], MISALIGNED_BUFFER_ORDER);
	}

	kfree(bufs);
	return 0;
}

arch_initcall(check_unaligned_access_all_cpus);

void riscv_user_isa_enable(void)
{
	if (riscv_cpu_has_extension_unlikely(smp_processor_id(), RISCV_ISA_EXT_ZICBOZ))
		csr_set(CSR_SENVCFG, ENVCFG_CBZE);
}

#ifdef CONFIG_RISCV_ALTERNATIVE
/*
 * Alternative patch sites consider 48 bits when determining when to patch
 * the old instruction sequence with the new. These bits are broken into a
 * 16-bit vendor ID and a 32-bit patch ID. A non-zero vendor ID means the
 * patch site is for an erratum, identified by the 32-bit patch ID. When
 * the vendor ID is zero, the patch site is for a cpufeature. cpufeatures
 * further break down patch ID into two 16-bit numbers. The lower 16 bits
 * are the cpufeature ID and the upper 16 bits are used for a value specific
 * to the cpufeature and patch site. If the upper 16 bits are zero, then it
 * implies no specific value is specified. cpufeatures that want to control
 * patching on a per-site basis will provide non-zero values and implement
 * checks here. The checks return true when patching should be done, and
 * false otherwise.
 */
static bool riscv_cpufeature_patch_check(u16 id, u16 value)
{
	if (!value)
		return true;

	switch (id) {
	case RISCV_ISA_EXT_ZICBOZ:
		/*
		 * Zicboz alternative applications provide the maximum
		 * supported block size order, or zero when it doesn't
		 * matter. If the current block size exceeds the maximum,
		 * then the alternative cannot be applied.
		 */
		return riscv_cboz_block_size <= (1U << value);
	}

	return false;
}

void __init_or_module riscv_cpufeature_patch_func(struct alt_entry *begin,
						  struct alt_entry *end,
						  unsigned int stage)
{
	struct alt_entry *alt;
	void *oldptr, *altptr;
	u16 id, value;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

	for (alt = begin; alt < end; alt++) {
		if (alt->vendor_id != 0)
			continue;

		id = PATCH_ID_CPUFEATURE_ID(alt->patch_id);

		if (id >= RISCV_ISA_EXT_MAX) {
			WARN(1, "This extension id:%d is not in ISA extension list", id);
			continue;
		}

		if (!__riscv_isa_extension_available(NULL, id))
			continue;

		value = PATCH_ID_CPUFEATURE_VALUE(alt->patch_id);
		if (!riscv_cpufeature_patch_check(id, value))
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
