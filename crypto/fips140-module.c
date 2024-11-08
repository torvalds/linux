// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 *
 * This file is the core of fips140.ko, which contains various crypto algorithms
 * that are also built into vmlinux.  At load time, this module overrides the
 * built-in implementations of these algorithms with its implementations.  It
 * also runs self-tests on these algorithms and verifies the integrity of its
 * code and data.  If either of these steps fails, the kernel will panic.
 *
 * This module is intended to be loaded at early boot time in order to meet
 * FIPS 140 and NIAP FPT_TST_EXT.1 requirements.  It shouldn't be used if you
 * don't need to meet these requirements.
 */

/*
 * Since this .c file is the real entry point of fips140.ko, it needs to be
 * compiled normally, so undo the hacks that were done in fips140-defs.h.
 */
#define MODULE
#undef KBUILD_MODFILE
#undef __DISABLE_EXPORTS

#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/skcipher.h>
#include <crypto/rng.h>
#include <trace/hooks/fips140.h>

#include "fips140-module.h"
#include "internal.h"

/*
 * FIPS 140-2 prefers the use of HMAC with a public key over a plain hash.
 */
u8 __initdata fips140_integ_hmac_key[] = "The quick brown fox jumps over the lazy dog";

/* this is populated by the build tool */
u8 __initdata fips140_integ_hmac_digest[SHA256_DIGEST_SIZE];

const u32 __initcall_start_marker __section(".initcalls._start");
const u32 __initcall_end_marker __section(".initcalls._end");

const u8 __fips140_text_start __section(".text.._start");
const u8 __fips140_text_end __section(".text.._end");

const u8 __fips140_rodata_start __section(".rodata.._start");
const u8 __fips140_rodata_end __section(".rodata.._end");

/*
 * We need this little detour to prevent Clang from detecting out of bounds
 * accesses to __fips140_text_start and __fips140_rodata_start, which only exist
 * to delineate the section, and so their sizes are not relevant to us.
 */
const u32 *__initcall_start = &__initcall_start_marker;

const u8 *__text_start = &__fips140_text_start;
const u8 *__rodata_start = &__fips140_rodata_start;

/*
 * The list of the crypto API algorithms (by cra_name) that will be unregistered
 * by this module, in preparation for the module registering its own
 * implementation(s) of them.
 *
 * All algorithms that will be declared as FIPS-approved in the module
 * certification must be listed here, to ensure that the non-FIPS-approved
 * implementations of these algorithms in the kernel image aren't used.
 *
 * For every algorithm in this list, the module should contain all the "same"
 * implementations that the kernel image does, including the C implementation as
 * well as any architecture-specific implementations.  This is needed to avoid
 * performance regressions as well as the possibility of an algorithm being
 * unavailable on some CPUs.  E.g., "xcbc(aes)" isn't in this list, as the
 * module doesn't have a C implementation of it (and it won't be FIPS-approved).
 *
 * Due to a quirk in the FIPS requirements, "gcm(aes)" isn't actually able to be
 * FIPS-approved.  However, we otherwise treat it the same as the algorithms
 * that will be FIPS-approved, and therefore it's included in this list.
 *
 * When adding a new algorithm here, make sure to consider whether it needs a
 * self-test added to fips140_selftests[] as well.
 */
static const struct {
	const char *name;
	bool approved;
} fips140_algs_to_replace[] = {
	{"aes", true},

	{"cmac(aes)", true},
	{"ecb(aes)", true},

	{"cbc(aes)", true},
	{"cts(cbc(aes))", true},
	{"ctr(aes)", true},
	{"xts(aes)", true},
	{"gcm(aes)", false},

	{"hmac(sha1)", true},
	{"hmac(sha224)", true},
	{"hmac(sha256)", true},
	{"hmac(sha384)", true},
	{"hmac(sha512)", true},
	{"sha1", true},
	{"sha224", true},
	{"sha256", true},
	{"sha384", true},
	{"sha512", true},

	{"stdrng", true},
	{"jitterentropy_rng", false},
};

static bool __init fips140_should_unregister_alg(struct crypto_alg *alg)
{
	int i;

	/*
	 * All software algorithms are synchronous, hardware algorithms must
	 * be covered by their own FIPS 140 certification.
	 */
	if (alg->cra_flags & CRYPTO_ALG_ASYNC)
		return false;

	for (i = 0; i < ARRAY_SIZE(fips140_algs_to_replace); i++) {
		if (!strcmp(alg->cra_name, fips140_algs_to_replace[i].name))
			return true;
	}
	return false;
}

/*
 * FIPS 140-3 service indicators.  FIPS 140-3 requires that all services
 * "provide an indicator when the service utilises an approved cryptographic
 * algorithm, security function or process in an approved manner".  What this
 * means is very debatable, even with the help of the FIPS 140-3 Implementation
 * Guidance document.  However, it was decided that a function that takes in an
 * algorithm name and returns whether that algorithm is approved or not will
 * meet this requirement.  Note, this relies on some properties of the module:
 *
 *   - The module doesn't distinguish between "services" and "algorithms"; its
 *     services are simply its algorithms.
 *
 *   - The status of an approved algorithm is never non-approved, since (a) the
 *     module doesn't support operating in a non-approved mode, such as a mode
 *     where the self-tests are skipped; (b) there are no cases where the module
 *     supports non-approved settings for approved algorithms, e.g.
 *     non-approved key sizes; and (c) this function isn't available to be
 *     called until the module_init function has completed, so it's guaranteed
 *     that the self-tests and integrity check have already passed.
 *
 *   - The module does support some non-approved algorithms, so a single static
 *     indicator ("return true;") would not be acceptable.
 */
bool fips140_is_approved_service(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(fips140_algs_to_replace); i++) {
		if (!strcmp(name, fips140_algs_to_replace[i].name))
			return fips140_algs_to_replace[i].approved;
	}
	return false;
}
EXPORT_SYMBOL_GPL(fips140_is_approved_service);

/*
 * FIPS 140-3 requires that modules provide a "service" that outputs "the name
 * or module identifier and the versioning information that can be correlated
 * with a validation record".  This function meets that requirement.
 *
 * Note: the module also prints this same information to the kernel log when it
 * is loaded.  That might meet the requirement by itself.  However, given the
 * vagueness of what counts as a "service", we provide this function too, just
 * in case the certification lab or CMVP is happier with an explicit function.
 *
 * Note: /sys/modules/fips140/scmversion also provides versioning information
 * about the module.  However that file just shows the bare git commit ID, so it
 * probably isn't sufficient to meet the FIPS requirement, which seems to want
 * the "official" module name and version number used in the FIPS certificate.
 */
const char *fips140_module_version(void)
{
	return FIPS140_MODULE_NAME " " FIPS140_MODULE_VERSION;
}
EXPORT_SYMBOL_GPL(fips140_module_version);

static LIST_HEAD(existing_live_algos);

/*
 * Release a list of algorithms which have been removed from crypto_alg_list.
 *
 * Note that even though the list is a private list, we have to hold
 * crypto_alg_sem while iterating through it because crypto_unregister_alg() may
 * run concurrently (as we haven't taken a reference to the algorithms on the
 * list), and crypto_unregister_alg() will remove the algorithm from whichever
 * list it happens to be on, while holding crypto_alg_sem.  That's okay, since
 * in that case crypto_unregister_alg() will handle the crypto_alg_put().
 */
static void fips140_remove_final(struct list_head *list)
{
	struct crypto_alg *alg;
	struct crypto_alg *n;

	/*
	 * We need to take crypto_alg_sem to safely traverse the list (see
	 * comment above), but we have to drop it when doing each
	 * crypto_alg_put() as that may take crypto_alg_sem again.
	 */
	down_write(&crypto_alg_sem);
	list_for_each_entry_safe(alg, n, list, cra_list) {
		list_del_init(&alg->cra_list);
		up_write(&crypto_alg_sem);

		crypto_alg_put(alg);

		down_write(&crypto_alg_sem);
	}
	up_write(&crypto_alg_sem);
}

static void __init unregister_existing_fips140_algos(void)
{
	struct crypto_alg *alg, *tmp;
	LIST_HEAD(remove_list);
	LIST_HEAD(spawns);

	down_write(&crypto_alg_sem);

	/*
	 * Find all registered algorithms that we care about, and move them to a
	 * private list so that they are no longer exposed via the algo lookup
	 * API. Subsequently, we will unregister them if they are not in active
	 * use. If they are, we can't fully unregister them but we can ensure
	 * that new users won't use them.
	 */
	list_for_each_entry_safe(alg, tmp, &crypto_alg_list, cra_list) {
		if (!fips140_should_unregister_alg(alg))
			continue;
		if (refcount_read(&alg->cra_refcnt) == 1) {
			/*
			 * This algorithm is not currently in use, but there may
			 * be template instances holding references to it via
			 * spawns. So let's tear it down like
			 * crypto_unregister_alg() would, but without releasing
			 * the lock, to prevent races with concurrent TFM
			 * allocations.
			 */
			alg->cra_flags |= CRYPTO_ALG_DEAD;
			list_move(&alg->cra_list, &remove_list);
			crypto_remove_spawns(alg, &spawns, NULL);
		} else {
			/*
			 * This algorithm is live, i.e. it has TFMs allocated,
			 * so we can't fully unregister it.  It's not necessary
			 * to dynamically redirect existing users to the FIPS
			 * code, given that they can't be relying on FIPS
			 * certified crypto in the first place.  However, we do
			 * need to ensure that new users will get the FIPS code.
			 *
			 * In most cases, setting alg->cra_priority to 0
			 * achieves this.  However, that isn't enough for
			 * algorithms like "hmac(sha256)" that need to be
			 * instantiated from a template, since existing
			 * algorithms always take priority over a template being
			 * instantiated.  Therefore, we move the algorithm to
			 * a private list so that algorithm lookups won't find
			 * it anymore.  To further distinguish it from the FIPS
			 * algorithms, we also append "+orig" to its name.
			 */
			pr_info("found already-live algorithm '%s' ('%s')\n",
				alg->cra_name, alg->cra_driver_name);
			alg->cra_priority = 0;
			strlcat(alg->cra_name, "+orig", CRYPTO_MAX_ALG_NAME);
			strlcat(alg->cra_driver_name, "+orig",
				CRYPTO_MAX_ALG_NAME);
			list_move(&alg->cra_list, &existing_live_algos);
		}
	}
	up_write(&crypto_alg_sem);

	fips140_remove_final(&remove_list);
	fips140_remove_final(&spawns);
}

static void __init unapply_text_relocations(void *section, int section_size,
					    const Elf64_Rela *rela, int numrels)
{
	while (numrels--) {
		u32 *place = (u32 *)(section + rela->r_offset);

		BUG_ON(rela->r_offset >= section_size);

		switch (ELF64_R_TYPE(rela->r_info)) {
#ifdef CONFIG_ARM64
		case R_AARCH64_ABS32: /* for KCFI */
			*place = 0;
			break;

		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			*place &= ~GENMASK(25, 0);
			break;

		case R_AARCH64_ADR_PREL_LO21:
		case R_AARCH64_ADR_PREL_PG_HI21:
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
			*place &= ~(GENMASK(30, 29) | GENMASK(23, 5));
			break;

		case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC:
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC:
			*place &= ~GENMASK(21, 10);
			break;
		default:
			pr_err("unhandled relocation type %llu\n",
			       ELF64_R_TYPE(rela->r_info));
			BUG();
#else
#error
#endif
		}
		rela++;
	}
}

static void __init unapply_rodata_relocations(void *section, int section_size,
					      const Elf64_Rela *rela, int numrels)
{
	while (numrels--) {
		void *place = section + rela->r_offset;

		BUG_ON(rela->r_offset >= section_size);

		switch (ELF64_R_TYPE(rela->r_info)) {
#ifdef CONFIG_ARM64
		case R_AARCH64_ABS64:
			*(u64 *)place = 0;
			break;
		default:
			pr_err("unhandled relocation type %llu\n",
			       ELF64_R_TYPE(rela->r_info));
			BUG();
#else
#error
#endif
		}
		rela++;
	}
}

enum {
	PACIASP		= 0xd503233f,
	AUTIASP		= 0xd50323bf,
	SCS_PUSH	= 0xf800865e,
	SCS_POP		= 0xf85f8e5e,
};

/*
 * To make the integrity check work with dynamic Shadow Call Stack (SCS),
 * replace all instructions that push or pop from the SCS with the Pointer
 * Authentication Code (PAC) instructions that were present originally.
 */
static void __init unapply_scs_patch(void *section, int section_size)
{
#if defined(CONFIG_ARM64) && defined(CONFIG_UNWIND_PATCH_PAC_INTO_SCS)
	u32 *insns = section;
	int i;

	for (i = 0; i < section_size / sizeof(insns[0]); i++) {
		if (insns[i] == SCS_PUSH)
			insns[i] = PACIASP;
		else if (insns[i] == SCS_POP)
			insns[i] = AUTIASP;
	}
#endif
}

#ifdef CONFIG_CRYPTO_FIPS140_MOD_DEBUG_INTEGRITY_CHECK
static struct {
	const void *text;
	int textsize;
	const void *rodata;
	int rodatasize;
} saved_integrity_check_info;

static ssize_t fips140_text_read(struct file *file, char __user *to,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(to, count, ppos,
				       saved_integrity_check_info.text,
				       saved_integrity_check_info.textsize);
}

static ssize_t fips140_rodata_read(struct file *file, char __user *to,
				   size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(to, count, ppos,
				       saved_integrity_check_info.rodata,
				       saved_integrity_check_info.rodatasize);
}

static const struct file_operations fips140_text_fops = {
	.read = fips140_text_read,
};

static const struct file_operations fips140_rodata_fops = {
	.read = fips140_rodata_read,
};

static void fips140_init_integrity_debug_files(const void *text, int textsize,
					       const void *rodata,
					       int rodatasize)
{
	struct dentry *dir;

	dir = debugfs_create_dir("fips140", NULL);

	saved_integrity_check_info.text = kmemdup(text, textsize, GFP_KERNEL);
	saved_integrity_check_info.textsize = textsize;
	if (saved_integrity_check_info.text)
		debugfs_create_file("text", 0400, dir, NULL,
				    &fips140_text_fops);

	saved_integrity_check_info.rodata = kmemdup(rodata, rodatasize,
						    GFP_KERNEL);
	saved_integrity_check_info.rodatasize = rodatasize;
	if (saved_integrity_check_info.rodata)
		debugfs_create_file("rodata", 0400, dir, NULL,
				    &fips140_rodata_fops);
}
#else /* CONFIG_CRYPTO_FIPS140_MOD_DEBUG_INTEGRITY_CHECK */
static void fips140_init_integrity_debug_files(const void *text, int textsize,
					       const void *rodata,
					       int rodatasize)
{
}
#endif /* !CONFIG_CRYPTO_FIPS140_MOD_DEBUG_INTEGRITY_CHECK */

extern struct {
	u32	offset;
	u32	count;
} fips140_rela_text, fips140_rela_rodata;

static bool __init check_fips140_module_hmac(void)
{
	struct crypto_shash *tfm = NULL;
	SHASH_DESC_ON_STACK(desc, dontcare);
	u8 digest[SHA256_DIGEST_SIZE];
	void *textcopy, *rodatacopy;
	int textsize, rodatasize;
	bool ok = false;
	int err;

	textsize	= &__fips140_text_end - &__fips140_text_start;
	rodatasize	= &__fips140_rodata_end - &__fips140_rodata_start;

	pr_info("text size  : 0x%x\n", textsize);
	pr_info("rodata size: 0x%x\n", rodatasize);

	textcopy = kmalloc(textsize + rodatasize, GFP_KERNEL);
	if (!textcopy) {
		pr_err("Failed to allocate memory for copy of .text\n");
		goto out;
	}

	rodatacopy = textcopy + textsize;

	memcpy(textcopy, __text_start, textsize);
	memcpy(rodatacopy, __rodata_start, rodatasize);

	// apply the relocations in reverse on the copies of .text  and .rodata
	unapply_text_relocations(textcopy, textsize,
				 offset_to_ptr(&fips140_rela_text.offset),
				 fips140_rela_text.count);

	unapply_rodata_relocations(rodatacopy, rodatasize,
				  offset_to_ptr(&fips140_rela_rodata.offset),
				  fips140_rela_rodata.count);

	unapply_scs_patch(textcopy, textsize);

	fips140_init_integrity_debug_files(textcopy, textsize,
					   rodatacopy, rodatasize);

	fips140_inject_integrity_failure(textcopy);

	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("failed to allocate hmac tfm (%ld)\n", PTR_ERR(tfm));
		tfm = NULL;
		goto out;
	}
	desc->tfm = tfm;

	pr_info("using '%s' for integrity check\n",
		crypto_shash_driver_name(tfm));

	err = crypto_shash_setkey(tfm, fips140_integ_hmac_key,
				  strlen(fips140_integ_hmac_key)) ?:
	      crypto_shash_init(desc) ?:
	      crypto_shash_update(desc, textcopy, textsize) ?:
	      crypto_shash_finup(desc, rodatacopy, rodatasize, digest);

	/* Zeroizing this is important; see the comment below. */
	shash_desc_zero(desc);

	if (err) {
		pr_err("failed to calculate hmac shash (%d)\n", err);
		goto out;
	}

	if (memcmp(digest, fips140_integ_hmac_digest, sizeof(digest))) {
		pr_err("provided_digest  : %*phN\n", (int)sizeof(digest),
		       fips140_integ_hmac_digest);

		pr_err("calculated digest: %*phN\n", (int)sizeof(digest),
		       digest);
		goto out;
	}
	ok = true;
out:
	/*
	 * FIPS 140-3 requires that all "temporary value(s) generated during the
	 * integrity test" be zeroized (ref: FIPS 140-3 IG 9.7.B).  There is no
	 * technical reason to do this given that these values are public
	 * information, but this is the requirement so we follow it.
	 */
	crypto_free_shash(tfm);
	memzero_explicit(digest, sizeof(digest));
	kfree_sensitive(textcopy);
	return ok;
}

static void fips140_sha256(void *p, const u8 *data, unsigned int len, u8 *out,
			   int *hook_inuse)
{
	sha256(data, len, out);
	*hook_inuse = 1;
}

static void fips140_aes_expandkey(void *p, struct crypto_aes_ctx *ctx,
				  const u8 *in_key, unsigned int key_len,
				  int *err)
{
	*err = aes_expandkey(ctx, in_key, key_len);
}

static void fips140_aes_encrypt(void *priv, const struct crypto_aes_ctx *ctx,
				u8 *out, const u8 *in, int *hook_inuse)
{
	aes_encrypt(ctx, out, in);
	*hook_inuse = 1;
}

static void fips140_aes_decrypt(void *priv, const struct crypto_aes_ctx *ctx,
				u8 *out, const u8 *in, int *hook_inuse)
{
	aes_decrypt(ctx, out, in);
	*hook_inuse = 1;
}

static bool update_fips140_library_routines(void)
{
	int ret;

	ret = register_trace_android_vh_sha256(fips140_sha256, NULL) ?:
	      register_trace_android_vh_aes_expandkey(fips140_aes_expandkey, NULL) ?:
	      register_trace_android_vh_aes_encrypt(fips140_aes_encrypt, NULL) ?:
	      register_trace_android_vh_aes_decrypt(fips140_aes_decrypt, NULL);

	return ret == 0;
}

/* Initialize the FIPS 140 module */
static int __init fips140_init(void)
{
	const u32 *initcall;

	pr_info("loading " FIPS140_MODULE_NAME " " FIPS140_MODULE_VERSION "\n");
	fips140_init_thread = current;

	unregister_existing_fips140_algos();

	/* iterate over all init routines present in this module and call them */
	for (initcall = __initcall_start + 1;
	     initcall < &__initcall_end_marker;
	     initcall++) {
		initcall_t init = offset_to_ptr(initcall);
		int err = init();

		/*
		 * ENODEV is expected from initcalls that only register
		 * algorithms that depend on non-present CPU features.  Besides
		 * that, errors aren't expected here.
		 */
		if (err && err != -ENODEV) {
			pr_err("initcall %ps() failed: %d\n", init, err);
			goto panic;
		}
	}

	if (!fips140_run_selftests())
		goto panic;

	/*
	 * It may seem backward to perform the integrity check last, but this
	 * is intentional: the check itself uses hmac(sha256) which is one of
	 * the algorithms that are replaced with versions from this module, and
	 * the integrity check must use the replacement version.  Also, to be
	 * ready for FIPS 140-3, the integrity check algorithm must have already
	 * been self-tested.
	 */

	if (!check_fips140_module_hmac()) {
		if (!IS_ENABLED(CONFIG_CRYPTO_FIPS140_MOD_DEBUG_INTEGRITY_CHECK)) {
			pr_crit("integrity check failed -- giving up!\n");
			goto panic;
		}
		pr_crit("ignoring integrity check failure due to debug mode\n");
	} else {
		pr_info("integrity check passed\n");
	}

	complete_all(&fips140_tests_done);

	if (!update_fips140_library_routines())
		goto panic;

	if (!fips140_eval_testing_init())
		goto panic;

	pr_info("module successfully loaded\n");
	return 0;

panic:
	panic("FIPS 140 module load failure");
}

module_init(fips140_init);

MODULE_IMPORT_NS(CRYPTO_INTERNAL);
MODULE_LICENSE("GPL v2");

/*
 * Below are copies of some selected "crypto-related" helper functions that are
 * used by fips140.ko but are not already built into it, due to them being
 * defined in a file that cannot easily be built into fips140.ko (e.g.,
 * crypto/algapi.c) instead of one that can (e.g., most files in lib/).
 *
 * There is no hard rule about what needs to be included here, as this is for
 * FIPS certifiability, not any technical reason.  FIPS modules are supposed to
 * implement the "crypto" themselves, but to do so they are allowed to call
 * non-cryptographic helper functions from outside the module.  Something like
 * memcpy() is "clearly" non-cryptographic.  However, there is is ambiguity
 * about functions like crypto_inc() which aren't cryptographic by themselves,
 * but are more closely associated with cryptography than e.g. memcpy().  To err
 * on the side of caution, we define copies of some selected functions below so
 * that calls to them from within fips140.ko will remain in fips140.ko.
 */

static inline void crypto_inc_byte(u8 *a, unsigned int size)
{
	u8 *b = (a + size);
	u8 c;

	for (; size; size--) {
		c = *--b + 1;
		*b = c;
		if (c)
			break;
	}
}

void crypto_inc(u8 *a, unsigned int size)
{
	__be32 *b = (__be32 *)(a + size);
	u32 c;

	if (IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) ||
	    IS_ALIGNED((unsigned long)b, __alignof__(*b)))
		for (; size >= 4; size -= 4) {
			c = be32_to_cpu(*--b) + 1;
			*b = cpu_to_be32(c);
			if (likely(c))
				return;
		}

	crypto_inc_byte(a, size);
}
