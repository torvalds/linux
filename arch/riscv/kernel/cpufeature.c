// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/cpu.h>
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
#include <asm/text-patching.h>
#include <asm/hwprobe.h>
#include <asm/processor.h>
#include <asm/sbi.h>
#include <asm/vector.h>
#include <asm/vendor_extensions.h>

#define NUM_ALPHA_EXTS ('z' - 'a' + 1)

unsigned long elf_hwcap __read_mostly;

/* Host ISA bitmap */
static DECLARE_BITMAP(riscv_isa, RISCV_ISA_EXT_MAX) __read_mostly;

/* Per-cpu ISA extensions. */
struct riscv_isainfo hart_isa[NR_CPUS];

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
bool __riscv_isa_extension_available(const unsigned long *isa_bitmap, unsigned int bit)
{
	const unsigned long *bmap = (isa_bitmap) ? isa_bitmap : riscv_isa;

	if (bit >= RISCV_ISA_EXT_MAX)
		return false;

	return test_bit(bit, bmap) ? true : false;
}
EXPORT_SYMBOL_GPL(__riscv_isa_extension_available);

static int riscv_ext_zicbom_validate(const struct riscv_isa_ext_data *data,
				     const unsigned long *isa_bitmap)
{
	if (!riscv_cbom_block_size) {
		pr_err("Zicbom detected in ISA string, disabling as no cbom-block-size found\n");
		return -EINVAL;
	}
	if (!is_power_of_2(riscv_cbom_block_size)) {
		pr_err("Zicbom disabled as cbom-block-size present, but is not a power-of-2\n");
		return -EINVAL;
	}
	return 0;
}

static int riscv_ext_zicboz_validate(const struct riscv_isa_ext_data *data,
				     const unsigned long *isa_bitmap)
{
	if (!riscv_cboz_block_size) {
		pr_err("Zicboz detected in ISA string, disabling as no cboz-block-size found\n");
		return -EINVAL;
	}
	if (!is_power_of_2(riscv_cboz_block_size)) {
		pr_err("Zicboz disabled as cboz-block-size present, but is not a power-of-2\n");
		return -EINVAL;
	}
	return 0;
}

static int riscv_ext_zca_depends(const struct riscv_isa_ext_data *data,
				 const unsigned long *isa_bitmap)
{
	if (__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_ZCA))
		return 0;

	return -EPROBE_DEFER;
}
static int riscv_ext_zcd_validate(const struct riscv_isa_ext_data *data,
				  const unsigned long *isa_bitmap)
{
	if (__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_ZCA) &&
	    __riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_d))
		return 0;

	return -EPROBE_DEFER;
}

static int riscv_ext_zcf_validate(const struct riscv_isa_ext_data *data,
				  const unsigned long *isa_bitmap)
{
	if (IS_ENABLED(CONFIG_64BIT))
		return -EINVAL;

	if (__riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_ZCA) &&
	    __riscv_isa_extension_available(isa_bitmap, RISCV_ISA_EXT_f))
		return 0;

	return -EPROBE_DEFER;
}

static const unsigned int riscv_zk_bundled_exts[] = {
	RISCV_ISA_EXT_ZBKB,
	RISCV_ISA_EXT_ZBKC,
	RISCV_ISA_EXT_ZBKX,
	RISCV_ISA_EXT_ZKND,
	RISCV_ISA_EXT_ZKNE,
	RISCV_ISA_EXT_ZKR,
	RISCV_ISA_EXT_ZKT,
};

static const unsigned int riscv_zkn_bundled_exts[] = {
	RISCV_ISA_EXT_ZBKB,
	RISCV_ISA_EXT_ZBKC,
	RISCV_ISA_EXT_ZBKX,
	RISCV_ISA_EXT_ZKND,
	RISCV_ISA_EXT_ZKNE,
	RISCV_ISA_EXT_ZKNH,
};

static const unsigned int riscv_zks_bundled_exts[] = {
	RISCV_ISA_EXT_ZBKB,
	RISCV_ISA_EXT_ZBKC,
	RISCV_ISA_EXT_ZKSED,
	RISCV_ISA_EXT_ZKSH
};

#define RISCV_ISA_EXT_ZVKN	\
	RISCV_ISA_EXT_ZVKNED,	\
	RISCV_ISA_EXT_ZVKNHB,	\
	RISCV_ISA_EXT_ZVKB,	\
	RISCV_ISA_EXT_ZVKT

static const unsigned int riscv_zvkn_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKN
};

static const unsigned int riscv_zvknc_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKN,
	RISCV_ISA_EXT_ZVBC
};

static const unsigned int riscv_zvkng_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKN,
	RISCV_ISA_EXT_ZVKG
};

#define RISCV_ISA_EXT_ZVKS	\
	RISCV_ISA_EXT_ZVKSED,	\
	RISCV_ISA_EXT_ZVKSH,	\
	RISCV_ISA_EXT_ZVKB,	\
	RISCV_ISA_EXT_ZVKT

static const unsigned int riscv_zvks_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKS
};

static const unsigned int riscv_zvksc_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKS,
	RISCV_ISA_EXT_ZVBC
};

static const unsigned int riscv_zvksg_bundled_exts[] = {
	RISCV_ISA_EXT_ZVKS,
	RISCV_ISA_EXT_ZVKG
};

static const unsigned int riscv_zvbb_exts[] = {
	RISCV_ISA_EXT_ZVKB
};

#define RISCV_ISA_EXT_ZVE64F_IMPLY_LIST	\
	RISCV_ISA_EXT_ZVE64X,		\
	RISCV_ISA_EXT_ZVE32F,		\
	RISCV_ISA_EXT_ZVE32X

#define RISCV_ISA_EXT_ZVE64D_IMPLY_LIST	\
	RISCV_ISA_EXT_ZVE64F,		\
	RISCV_ISA_EXT_ZVE64F_IMPLY_LIST

#define RISCV_ISA_EXT_V_IMPLY_LIST	\
	RISCV_ISA_EXT_ZVE64D,		\
	RISCV_ISA_EXT_ZVE64D_IMPLY_LIST

static const unsigned int riscv_zve32f_exts[] = {
	RISCV_ISA_EXT_ZVE32X
};

static const unsigned int riscv_zve64f_exts[] = {
	RISCV_ISA_EXT_ZVE64F_IMPLY_LIST
};

static const unsigned int riscv_zve64d_exts[] = {
	RISCV_ISA_EXT_ZVE64D_IMPLY_LIST
};

static const unsigned int riscv_v_exts[] = {
	RISCV_ISA_EXT_V_IMPLY_LIST
};

static const unsigned int riscv_zve64x_exts[] = {
	RISCV_ISA_EXT_ZVE32X,
	RISCV_ISA_EXT_ZVE64X
};

/*
 * While the [ms]envcfg CSRs were not defined until version 1.12 of the RISC-V
 * privileged ISA, the existence of the CSRs is implied by any extension which
 * specifies [ms]envcfg bit(s). Hence, we define a custom ISA extension for the
 * existence of the CSR, and treat it as a subset of those other extensions.
 */
static const unsigned int riscv_xlinuxenvcfg_exts[] = {
	RISCV_ISA_EXT_XLINUXENVCFG
};

/*
 * Zc* spec states that:
 * - C always implies Zca
 * - C+F implies Zcf (RV32 only)
 * - C+D implies Zcd
 *
 * These extensions will be enabled and then validated depending on the
 * availability of F/D RV32.
 */
static const unsigned int riscv_c_exts[] = {
	RISCV_ISA_EXT_ZCA,
	RISCV_ISA_EXT_ZCF,
	RISCV_ISA_EXT_ZCD,
};

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
	__RISCV_ISA_EXT_SUPERSET(c, RISCV_ISA_EXT_c, riscv_c_exts),
	__RISCV_ISA_EXT_SUPERSET(v, RISCV_ISA_EXT_v, riscv_v_exts),
	__RISCV_ISA_EXT_DATA(h, RISCV_ISA_EXT_h),
	__RISCV_ISA_EXT_SUPERSET_VALIDATE(zicbom, RISCV_ISA_EXT_ZICBOM, riscv_xlinuxenvcfg_exts,
					  riscv_ext_zicbom_validate),
	__RISCV_ISA_EXT_SUPERSET_VALIDATE(zicboz, RISCV_ISA_EXT_ZICBOZ, riscv_xlinuxenvcfg_exts,
					  riscv_ext_zicboz_validate),
	__RISCV_ISA_EXT_DATA(zicntr, RISCV_ISA_EXT_ZICNTR),
	__RISCV_ISA_EXT_DATA(zicond, RISCV_ISA_EXT_ZICOND),
	__RISCV_ISA_EXT_DATA(zicsr, RISCV_ISA_EXT_ZICSR),
	__RISCV_ISA_EXT_DATA(zifencei, RISCV_ISA_EXT_ZIFENCEI),
	__RISCV_ISA_EXT_DATA(zihintntl, RISCV_ISA_EXT_ZIHINTNTL),
	__RISCV_ISA_EXT_DATA(zihintpause, RISCV_ISA_EXT_ZIHINTPAUSE),
	__RISCV_ISA_EXT_DATA(zihpm, RISCV_ISA_EXT_ZIHPM),
	__RISCV_ISA_EXT_DATA(zimop, RISCV_ISA_EXT_ZIMOP),
	__RISCV_ISA_EXT_DATA(zacas, RISCV_ISA_EXT_ZACAS),
	__RISCV_ISA_EXT_DATA(zawrs, RISCV_ISA_EXT_ZAWRS),
	__RISCV_ISA_EXT_DATA(zfa, RISCV_ISA_EXT_ZFA),
	__RISCV_ISA_EXT_DATA(zfh, RISCV_ISA_EXT_ZFH),
	__RISCV_ISA_EXT_DATA(zfhmin, RISCV_ISA_EXT_ZFHMIN),
	__RISCV_ISA_EXT_DATA(zca, RISCV_ISA_EXT_ZCA),
	__RISCV_ISA_EXT_DATA_VALIDATE(zcb, RISCV_ISA_EXT_ZCB, riscv_ext_zca_depends),
	__RISCV_ISA_EXT_DATA_VALIDATE(zcd, RISCV_ISA_EXT_ZCD, riscv_ext_zcd_validate),
	__RISCV_ISA_EXT_DATA_VALIDATE(zcf, RISCV_ISA_EXT_ZCF, riscv_ext_zcf_validate),
	__RISCV_ISA_EXT_DATA_VALIDATE(zcmop, RISCV_ISA_EXT_ZCMOP, riscv_ext_zca_depends),
	__RISCV_ISA_EXT_DATA(zba, RISCV_ISA_EXT_ZBA),
	__RISCV_ISA_EXT_DATA(zbb, RISCV_ISA_EXT_ZBB),
	__RISCV_ISA_EXT_DATA(zbc, RISCV_ISA_EXT_ZBC),
	__RISCV_ISA_EXT_DATA(zbkb, RISCV_ISA_EXT_ZBKB),
	__RISCV_ISA_EXT_DATA(zbkc, RISCV_ISA_EXT_ZBKC),
	__RISCV_ISA_EXT_DATA(zbkx, RISCV_ISA_EXT_ZBKX),
	__RISCV_ISA_EXT_DATA(zbs, RISCV_ISA_EXT_ZBS),
	__RISCV_ISA_EXT_BUNDLE(zk, riscv_zk_bundled_exts),
	__RISCV_ISA_EXT_BUNDLE(zkn, riscv_zkn_bundled_exts),
	__RISCV_ISA_EXT_DATA(zknd, RISCV_ISA_EXT_ZKND),
	__RISCV_ISA_EXT_DATA(zkne, RISCV_ISA_EXT_ZKNE),
	__RISCV_ISA_EXT_DATA(zknh, RISCV_ISA_EXT_ZKNH),
	__RISCV_ISA_EXT_DATA(zkr, RISCV_ISA_EXT_ZKR),
	__RISCV_ISA_EXT_BUNDLE(zks, riscv_zks_bundled_exts),
	__RISCV_ISA_EXT_DATA(zkt, RISCV_ISA_EXT_ZKT),
	__RISCV_ISA_EXT_DATA(zksed, RISCV_ISA_EXT_ZKSED),
	__RISCV_ISA_EXT_DATA(zksh, RISCV_ISA_EXT_ZKSH),
	__RISCV_ISA_EXT_DATA(ztso, RISCV_ISA_EXT_ZTSO),
	__RISCV_ISA_EXT_SUPERSET(zvbb, RISCV_ISA_EXT_ZVBB, riscv_zvbb_exts),
	__RISCV_ISA_EXT_DATA(zvbc, RISCV_ISA_EXT_ZVBC),
	__RISCV_ISA_EXT_SUPERSET(zve32f, RISCV_ISA_EXT_ZVE32F, riscv_zve32f_exts),
	__RISCV_ISA_EXT_DATA(zve32x, RISCV_ISA_EXT_ZVE32X),
	__RISCV_ISA_EXT_SUPERSET(zve64d, RISCV_ISA_EXT_ZVE64D, riscv_zve64d_exts),
	__RISCV_ISA_EXT_SUPERSET(zve64f, RISCV_ISA_EXT_ZVE64F, riscv_zve64f_exts),
	__RISCV_ISA_EXT_SUPERSET(zve64x, RISCV_ISA_EXT_ZVE64X, riscv_zve64x_exts),
	__RISCV_ISA_EXT_DATA(zvfh, RISCV_ISA_EXT_ZVFH),
	__RISCV_ISA_EXT_DATA(zvfhmin, RISCV_ISA_EXT_ZVFHMIN),
	__RISCV_ISA_EXT_DATA(zvkb, RISCV_ISA_EXT_ZVKB),
	__RISCV_ISA_EXT_DATA(zvkg, RISCV_ISA_EXT_ZVKG),
	__RISCV_ISA_EXT_BUNDLE(zvkn, riscv_zvkn_bundled_exts),
	__RISCV_ISA_EXT_BUNDLE(zvknc, riscv_zvknc_bundled_exts),
	__RISCV_ISA_EXT_DATA(zvkned, RISCV_ISA_EXT_ZVKNED),
	__RISCV_ISA_EXT_BUNDLE(zvkng, riscv_zvkng_bundled_exts),
	__RISCV_ISA_EXT_DATA(zvknha, RISCV_ISA_EXT_ZVKNHA),
	__RISCV_ISA_EXT_DATA(zvknhb, RISCV_ISA_EXT_ZVKNHB),
	__RISCV_ISA_EXT_BUNDLE(zvks, riscv_zvks_bundled_exts),
	__RISCV_ISA_EXT_BUNDLE(zvksc, riscv_zvksc_bundled_exts),
	__RISCV_ISA_EXT_DATA(zvksed, RISCV_ISA_EXT_ZVKSED),
	__RISCV_ISA_EXT_DATA(zvksh, RISCV_ISA_EXT_ZVKSH),
	__RISCV_ISA_EXT_BUNDLE(zvksg, riscv_zvksg_bundled_exts),
	__RISCV_ISA_EXT_DATA(zvkt, RISCV_ISA_EXT_ZVKT),
	__RISCV_ISA_EXT_DATA(smaia, RISCV_ISA_EXT_SMAIA),
	__RISCV_ISA_EXT_DATA(smstateen, RISCV_ISA_EXT_SMSTATEEN),
	__RISCV_ISA_EXT_DATA(ssaia, RISCV_ISA_EXT_SSAIA),
	__RISCV_ISA_EXT_DATA(sscofpmf, RISCV_ISA_EXT_SSCOFPMF),
	__RISCV_ISA_EXT_DATA(sstc, RISCV_ISA_EXT_SSTC),
	__RISCV_ISA_EXT_DATA(svinval, RISCV_ISA_EXT_SVINVAL),
	__RISCV_ISA_EXT_DATA(svnapot, RISCV_ISA_EXT_SVNAPOT),
	__RISCV_ISA_EXT_DATA(svpbmt, RISCV_ISA_EXT_SVPBMT),
	__RISCV_ISA_EXT_DATA(svvptc, RISCV_ISA_EXT_SVVPTC),
};

const size_t riscv_isa_ext_count = ARRAY_SIZE(riscv_isa_ext);

static void riscv_isa_set_ext(const struct riscv_isa_ext_data *ext, unsigned long *bitmap)
{
	if (ext->id != RISCV_ISA_EXT_INVALID)
		set_bit(ext->id, bitmap);

	for (int i = 0; i < ext->subset_ext_size; i++) {
		if (ext->subset_ext_ids[i] != RISCV_ISA_EXT_INVALID)
			set_bit(ext->subset_ext_ids[i], bitmap);
	}
}

static const struct riscv_isa_ext_data *riscv_get_isa_ext_data(unsigned int ext_id)
{
	for (int i = 0; i < riscv_isa_ext_count; i++) {
		if (riscv_isa_ext[i].id == ext_id)
			return &riscv_isa_ext[i];
	}

	return NULL;
}

/*
 * "Resolve" a source ISA bitmap into one that matches kernel configuration as
 * well as correct extension dependencies. Some extensions depends on specific
 * kernel configuration to be usable (V needs CONFIG_RISCV_ISA_V for instance)
 * and this function will actually validate all the extensions provided in
 * source_isa into the resolved_isa based on extensions validate() callbacks.
 */
static void __init riscv_resolve_isa(unsigned long *source_isa,
				     unsigned long *resolved_isa, unsigned long *this_hwcap,
				     unsigned long *isa2hwcap)
{
	bool loop;
	const struct riscv_isa_ext_data *ext;
	DECLARE_BITMAP(prev_resolved_isa, RISCV_ISA_EXT_MAX);
	int max_loop_count = riscv_isa_ext_count, ret;
	unsigned int bit;

	do {
		loop = false;
		if (max_loop_count-- < 0) {
			pr_err("Failed to reach a stable ISA state\n");
			return;
		}
		bitmap_copy(prev_resolved_isa, resolved_isa, RISCV_ISA_EXT_MAX);
		for_each_set_bit(bit, source_isa, RISCV_ISA_EXT_MAX) {
			ext = riscv_get_isa_ext_data(bit);

			if (ext && ext->validate) {
				ret = ext->validate(ext, resolved_isa);
				if (ret == -EPROBE_DEFER) {
					loop = true;
					continue;
				} else if (ret) {
					/* Disable the extension entirely */
					clear_bit(bit, source_isa);
					continue;
				}
			}

			set_bit(bit, resolved_isa);
			/* No need to keep it in source isa now that it is enabled */
			clear_bit(bit, source_isa);

			/* Single letter extensions get set in hwcap */
			if (bit < RISCV_ISA_EXT_BASE)
				*this_hwcap |= isa2hwcap[bit];
		}
	} while (loop && memcmp(prev_resolved_isa, resolved_isa, sizeof(prev_resolved_isa)));
}

static void __init match_isa_ext(const char *name, const char *name_end, unsigned long *bitmap)
{
	for (int i = 0; i < riscv_isa_ext_count; i++) {
		const struct riscv_isa_ext_data *ext = &riscv_isa_ext[i];

		if ((name_end - name == strlen(ext->name)) &&
		    !strncasecmp(name, ext->name, name_end - name)) {
			riscv_isa_set_ext(ext, bitmap);
			break;
		}
	}
}

static void __init riscv_parse_isa_string(const char *isa, unsigned long *bitmap)
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
		bool ext_err = false;

		switch (*ext) {
		case 'x':
		case 'X':
			if (acpi_disabled)
				pr_warn_once("Vendor extensions are ignored in riscv,isa. Use riscv,isa-extensions instead.");
			/*
			 * To skip an extension, we find its end.
			 * As multi-letter extensions must be split from other multi-letter
			 * extensions with an "_", the end of a multi-letter extension will
			 * either be the null character or the "_" at the start of the next
			 * multi-letter extension.
			 */
			for (; *isa && *isa != '_'; ++isa)
				;
			ext_err = true;
			break;
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

		if (unlikely(ext_err))
			continue;

		match_isa_ext(ext, ext_end, bitmap);
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
	u64 boot_vendorid;
	u64 boot_archid;

	if (!acpi_disabled) {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, &rhct);
		if (ACPI_FAILURE(status))
			return;
	}

	boot_vendorid = riscv_get_mvendorid();
	boot_archid = riscv_get_marchid();

	for_each_possible_cpu(cpu) {
		struct riscv_isainfo *isainfo = &hart_isa[cpu];
		unsigned long this_hwcap = 0;
		DECLARE_BITMAP(source_isa, RISCV_ISA_EXT_MAX) = { 0 };

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

		riscv_parse_isa_string(isa, source_isa);

		/*
		 * These ones were as they were part of the base ISA when the
		 * port & dt-bindings were upstreamed, and so can be set
		 * unconditionally where `i` is in riscv,isa on DT systems.
		 */
		if (acpi_disabled) {
			set_bit(RISCV_ISA_EXT_ZICSR, source_isa);
			set_bit(RISCV_ISA_EXT_ZIFENCEI, source_isa);
			set_bit(RISCV_ISA_EXT_ZICNTR, source_isa);
			set_bit(RISCV_ISA_EXT_ZIHPM, source_isa);
		}

		/*
		 * "V" in ISA strings is ambiguous in practice: it should mean
		 * just the standard V-1.0 but vendors aren't well behaved.
		 * Many vendors with T-Head CPU cores which implement the 0.7.1
		 * version of the vector specification put "v" into their DTs.
		 * CPU cores with the ratified spec will contain non-zero
		 * marchid.
		 */
		if (acpi_disabled && boot_vendorid == THEAD_VENDOR_ID && boot_archid == 0x0) {
			this_hwcap &= ~isa2hwcap[RISCV_ISA_EXT_v];
			clear_bit(RISCV_ISA_EXT_v, source_isa);
		}

		riscv_resolve_isa(source_isa, isainfo->isa, &this_hwcap, isa2hwcap);

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

static void __init riscv_fill_cpu_vendor_ext(struct device_node *cpu_node, int cpu)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return;

	for (int i = 0; i < riscv_isa_vendor_ext_list_size; i++) {
		struct riscv_isa_vendor_ext_data_list *ext_list = riscv_isa_vendor_ext_list[i];

		for (int j = 0; j < ext_list->ext_data_count; j++) {
			const struct riscv_isa_ext_data ext = ext_list->ext_data[j];
			struct riscv_isavendorinfo *isavendorinfo = &ext_list->per_hart_isa_bitmap[cpu];

			if (of_property_match_string(cpu_node, "riscv,isa-extensions",
						     ext.property) < 0)
				continue;

			/*
			 * Assume that subset extensions are all members of the
			 * same vendor.
			 */
			if (ext.subset_ext_size)
				for (int k = 0; k < ext.subset_ext_size; k++)
					set_bit(ext.subset_ext_ids[k], isavendorinfo->isa);

			set_bit(ext.id, isavendorinfo->isa);
		}
	}
}

/*
 * Populate all_harts_isa_bitmap for each vendor with all of the extensions that
 * are shared across CPUs for that vendor.
 */
static void __init riscv_fill_vendor_ext_list(int cpu)
{
	if (!IS_ENABLED(CONFIG_RISCV_ISA_VENDOR_EXT))
		return;

	for (int i = 0; i < riscv_isa_vendor_ext_list_size; i++) {
		struct riscv_isa_vendor_ext_data_list *ext_list = riscv_isa_vendor_ext_list[i];

		if (!ext_list->is_initialized) {
			bitmap_copy(ext_list->all_harts_isa_bitmap.isa,
				    ext_list->per_hart_isa_bitmap[cpu].isa,
				    RISCV_ISA_VENDOR_EXT_MAX);
			ext_list->is_initialized = true;
		} else {
			bitmap_and(ext_list->all_harts_isa_bitmap.isa,
				   ext_list->all_harts_isa_bitmap.isa,
				   ext_list->per_hart_isa_bitmap[cpu].isa,
				   RISCV_ISA_VENDOR_EXT_MAX);
		}
	}
}

static int __init riscv_fill_hwcap_from_ext_list(unsigned long *isa2hwcap)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		unsigned long this_hwcap = 0;
		struct device_node *cpu_node;
		struct riscv_isainfo *isainfo = &hart_isa[cpu];
		DECLARE_BITMAP(source_isa, RISCV_ISA_EXT_MAX) = { 0 };

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
			const struct riscv_isa_ext_data *ext = &riscv_isa_ext[i];

			if (of_property_match_string(cpu_node, "riscv,isa-extensions",
						     ext->property) < 0)
				continue;

			riscv_isa_set_ext(ext, source_isa);
		}

		riscv_resolve_isa(source_isa, isainfo->isa, &this_hwcap, isa2hwcap);
		riscv_fill_cpu_vendor_ext(cpu_node, cpu);

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

		riscv_fill_vendor_ext_list(cpu);
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

	if (__riscv_isa_extension_available(NULL, RISCV_ISA_EXT_ZVE32X)) {
		/*
		 * This cannot fail when called on the boot hart
		 */
		riscv_v_setup_vsize();
	}

	if (elf_hwcap & COMPAT_HWCAP_ISA_V) {
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

void riscv_user_isa_enable(void)
{
	if (riscv_cpu_has_extension_unlikely(smp_processor_id(), RISCV_ISA_EXT_ZICBOZ))
		csr_set(CSR_ENVCFG, ENVCFG_CBZE);
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
	u16 id, value, vendor;

	if (stage == RISCV_ALTERNATIVES_EARLY_BOOT)
		return;

	for (alt = begin; alt < end; alt++) {
		id = PATCH_ID_CPUFEATURE_ID(alt->patch_id);
		vendor = PATCH_ID_CPUFEATURE_ID(alt->vendor_id);

		/*
		 * Any alternative with a patch_id that is less than
		 * RISCV_ISA_EXT_MAX is interpreted as a standard extension.
		 *
		 * Any alternative with patch_id that is greater than or equal
		 * to RISCV_VENDOR_EXT_ALTERNATIVES_BASE is interpreted as a
		 * vendor extension.
		 */
		if (id < RISCV_ISA_EXT_MAX) {
			/*
			 * This patch should be treated as errata so skip
			 * processing here.
			 */
			if (alt->vendor_id != 0)
				continue;

			if (!__riscv_isa_extension_available(NULL, id))
				continue;

			value = PATCH_ID_CPUFEATURE_VALUE(alt->patch_id);
			if (!riscv_cpufeature_patch_check(id, value))
				continue;
		} else if (id >= RISCV_VENDOR_EXT_ALTERNATIVES_BASE) {
			if (!__riscv_isa_vendor_extension_available(VENDOR_EXT_ALL_CPUS, vendor,
								    id - RISCV_VENDOR_EXT_ALTERNATIVES_BASE))
				continue;
		} else {
			WARN(1, "This extension id:%d is not in ISA extension list", id);
			continue;
		}

		oldptr = ALT_OLD_PTR(alt);
		altptr = ALT_ALT_PTR(alt);

		mutex_lock(&text_mutex);
		patch_text_nosync(oldptr, altptr, alt->alt_len);
		riscv_alternative_fix_offsets(oldptr, alt->alt_len, oldptr - altptr);
		mutex_unlock(&text_mutex);
	}
}
#endif
