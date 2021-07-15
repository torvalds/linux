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

#include <linux/ctype.h>
#include <linux/module.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/skcipher.h>
#include <crypto/rng.h>
#include <trace/hooks/fips140.h>

#include "fips140-module.h"
#include "internal.h"

/*
 * This option allows deliberately failing the self-tests for a particular
 * algorithm.  This is for FIPS lab testing only.
 */
#ifdef CONFIG_CRYPTO_FIPS140_MOD_ERROR_INJECTION
char *fips140_broken_alg;
module_param_named(broken_alg, fips140_broken_alg, charp, 0);
#endif

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
 * implementation(s) of them.  When adding a new algorithm here, make sure to
 * consider whether it needs a self-test added to fips140_selftests[] as well.
 */
static const char * const fips140_algorithms[] __initconst = {
	"aes",

	"gcm(aes)",

	"ecb(aes)",
	"cbc(aes)",
	"ctr(aes)",
	"xts(aes)",

	"hmac(sha1)",
	"hmac(sha224)",
	"hmac(sha256)",
	"hmac(sha384)",
	"hmac(sha512)",
	"sha1",
	"sha224",
	"sha256",
	"sha384",
	"sha512",

	"stdrng",
};

static bool __init is_fips140_algo(struct crypto_alg *alg)
{
	int i;

	/*
	 * All software algorithms are synchronous, hardware algorithms must
	 * be covered by their own FIPS 140 certification.
	 */
	if (alg->cra_flags & CRYPTO_ALG_ASYNC)
		return false;

	for (i = 0; i < ARRAY_SIZE(fips140_algorithms); i++)
		if (!strcmp(alg->cra_name, fips140_algorithms[i]))
			return true;
	return false;
}

static LIST_HEAD(unchecked_fips140_algos);

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
	 * Find all registered algorithms that we care about, and move them to
	 * a private list so that they are no longer exposed via the algo
	 * lookup API. Subsequently, we will unregister them if they are not in
	 * active use. If they are, we cannot simply remove them but we can
	 * adapt them later to use our integrity checked backing code.
	 */
	list_for_each_entry_safe(alg, tmp, &crypto_alg_list, cra_list) {
		if (is_fips140_algo(alg)) {
			if (refcount_read(&alg->cra_refcnt) == 1) {
				/*
				 * This algorithm is not currently in use, but
				 * there may be template instances holding
				 * references to it via spawns. So let's tear
				 * it down like crypto_unregister_alg() would,
				 * but without releasing the lock, to prevent
				 * races with concurrent TFM allocations.
				 */
				alg->cra_flags |= CRYPTO_ALG_DEAD;
				list_move(&alg->cra_list, &remove_list);
				crypto_remove_spawns(alg, &spawns, NULL);
			} else {
				/*
				 * This algorithm is live, i.e., there are TFMs
				 * allocated that rely on it for its crypto
				 * transformations. We will swap these out
				 * later with integrity checked versions.
				 */
				pr_info("found already-live algorithm '%s' ('%s')\n",
					alg->cra_name, alg->cra_driver_name);
				list_move(&alg->cra_list,
					  &unchecked_fips140_algos);
			}
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

static bool __init check_fips140_module_hmac(void)
{
	SHASH_DESC_ON_STACK(desc, dontcare);
	u8 digest[SHA256_DIGEST_SIZE];
	void *textcopy, *rodatacopy;
	int textsize, rodatasize;
	int err;

	textsize	= &__fips140_text_end - &__fips140_text_start;
	rodatasize	= &__fips140_rodata_end - &__fips140_rodata_start;

	pr_info("text size  : 0x%x\n", textsize);
	pr_info("rodata size: 0x%x\n", rodatasize);

	textcopy = kmalloc(textsize + rodatasize, GFP_KERNEL);
	if (!textcopy) {
		pr_err("Failed to allocate memory for copy of .text\n");
		return false;
	}

	rodatacopy = textcopy + textsize;

	memcpy(textcopy, __text_start, textsize);
	memcpy(rodatacopy, __rodata_start, rodatasize);

	// apply the relocations in reverse on the copies of .text  and .rodata
	unapply_text_relocations(textcopy, textsize,
				 __this_module.arch.text_relocations,
				 __this_module.arch.num_text_relocations);

	unapply_rodata_relocations(rodatacopy, rodatasize,
				   __this_module.arch.rodata_relocations,
				   __this_module.arch.num_rodata_relocations);

	kfree(__this_module.arch.text_relocations);
	kfree(__this_module.arch.rodata_relocations);

	desc->tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(desc->tfm)) {
		pr_err("failed to allocate hmac tfm (%ld)\n", PTR_ERR(desc->tfm));
		kfree(textcopy);
		return false;
	}

	pr_info("using '%s' for integrity check\n",
		crypto_shash_driver_name(desc->tfm));

	err = crypto_shash_setkey(desc->tfm, fips140_integ_hmac_key,
				  strlen(fips140_integ_hmac_key)) ?:
	      crypto_shash_init(desc) ?:
	      crypto_shash_update(desc, textcopy, textsize) ?:
	      crypto_shash_finup(desc, rodatacopy, rodatasize, digest);

	crypto_free_shash(desc->tfm);
	kfree(textcopy);

	if (err) {
		pr_err("failed to calculate hmac shash (%d)\n", err);
		return false;
	}

	if (memcmp(digest, fips140_integ_hmac_digest, sizeof(digest))) {
		pr_err("provided_digest  : %*phN\n", (int)sizeof(digest),
		       fips140_integ_hmac_digest);

		pr_err("calculated digest: %*phN\n", (int)sizeof(digest),
		       digest);

		return false;
	}

	return true;
}

static bool __init update_live_fips140_algos(void)
{
	struct crypto_alg *alg, *new_alg, *tmp;

	/*
	 * Find all algorithms that we could not unregister the last time
	 * around, due to the fact that they were already in use.
	 */
	down_write(&crypto_alg_sem);
	list_for_each_entry_safe(alg, tmp, &unchecked_fips140_algos, cra_list) {

		/*
		 * Take this algo off the list before releasing the lock. This
		 * ensures that a concurrent invocation of
		 * crypto_unregister_alg() observes a consistent state, i.e.,
		 * the algo is still on the list, and crypto_unregister_alg()
		 * will release it, or it is not, and crypto_unregister_alg()
		 * will issue a warning but ignore this condition otherwise.
		 */
		list_del_init(&alg->cra_list);
		up_write(&crypto_alg_sem);

		/*
		 * Grab the algo that will replace the live one.
		 * Note that this will instantiate template based instances as
		 * well, as long as their driver name uses the conventional
		 * pattern of "template(algo)". In this case, we are relying on
		 * the fact that the templates carried by this module will
		 * supersede the builtin ones, due to the fact that they were
		 * registered later, and therefore appear first in the linked
		 * list. For example, "hmac(sha1-ce)" constructed using the
		 * builtin hmac template and the builtin SHA1 driver will be
		 * superseded by the integrity checked versions of HMAC and
		 * SHA1-ce carried in this module.
		 *
		 * Note that this takes a reference to the new algorithm which
		 * will never get released. This is intentional: once we copy
		 * the function pointers from the new algo into the old one, we
		 * cannot drop the new algo unless we are sure that the old one
		 * has been released, and this is someting we don't keep track
		 * of at the moment.
		 */
		new_alg = crypto_alg_mod_lookup(alg->cra_driver_name,
						alg->cra_flags & CRYPTO_ALG_TYPE_MASK,
						CRYPTO_ALG_TYPE_MASK | CRYPTO_NOLOAD);

		if (IS_ERR(new_alg)) {
			pr_crit("Failed to allocate '%s' for updating live algo (%ld)\n",
				alg->cra_driver_name, PTR_ERR(new_alg));
			return false;
		}

		/*
		 * The FIPS module's algorithms are expected to be built from
		 * the same source code as the in-kernel ones so that they are
		 * fully compatible. In general, there's no way to verify full
		 * compatibility at runtime, but we can at least verify that
		 * the algorithm properties match.
		 */
		if (alg->cra_ctxsize != new_alg->cra_ctxsize ||
		    alg->cra_alignmask != new_alg->cra_alignmask) {
			pr_crit("Failed to update live algo '%s' due to mismatch:\n"
				"cra_ctxsize   : %u vs %u\n"
				"cra_alignmask : 0x%x vs 0x%x\n",
				alg->cra_driver_name,
				alg->cra_ctxsize, new_alg->cra_ctxsize,
				alg->cra_alignmask, new_alg->cra_alignmask);
			return false;
		}

		/*
		 * Update the name and priority so the algorithm stands out as
		 * one that was updated in order to comply with FIPS140, and
		 * that it is not the preferred version for further use.
		 */
		strlcat(alg->cra_name, "+orig", CRYPTO_MAX_ALG_NAME);
		alg->cra_priority = 0;

		switch (alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
			struct aead_alg *old_aead, *new_aead;
			struct skcipher_alg *old_skcipher, *new_skcipher;
			struct shash_alg *old_shash, *new_shash;
			struct rng_alg *old_rng, *new_rng;

		case CRYPTO_ALG_TYPE_CIPHER:
			alg->cra_u.cipher = new_alg->cra_u.cipher;
			break;

		case CRYPTO_ALG_TYPE_AEAD:
			old_aead = container_of(alg, struct aead_alg, base);
			new_aead = container_of(new_alg, struct aead_alg, base);

			old_aead->setkey	= new_aead->setkey;
			old_aead->setauthsize	= new_aead->setauthsize;
			old_aead->encrypt	= new_aead->encrypt;
			old_aead->decrypt	= new_aead->decrypt;
			old_aead->init		= new_aead->init;
			old_aead->exit		= new_aead->exit;
			break;

		case CRYPTO_ALG_TYPE_SKCIPHER:
			old_skcipher = container_of(alg, struct skcipher_alg, base);
			new_skcipher = container_of(new_alg, struct skcipher_alg, base);

			old_skcipher->setkey	= new_skcipher->setkey;
			old_skcipher->encrypt	= new_skcipher->encrypt;
			old_skcipher->decrypt	= new_skcipher->decrypt;
			old_skcipher->init	= new_skcipher->init;
			old_skcipher->exit	= new_skcipher->exit;
			break;

		case CRYPTO_ALG_TYPE_SHASH:
			old_shash = container_of(alg, struct shash_alg, base);
			new_shash = container_of(new_alg, struct shash_alg, base);

			old_shash->init		= new_shash->init;
			old_shash->update	= new_shash->update;
			old_shash->final	= new_shash->final;
			old_shash->finup	= new_shash->finup;
			old_shash->digest	= new_shash->digest;
			old_shash->export	= new_shash->export;
			old_shash->import	= new_shash->import;
			old_shash->setkey	= new_shash->setkey;
			old_shash->init_tfm	= new_shash->init_tfm;
			old_shash->exit_tfm	= new_shash->exit_tfm;
			break;

		case CRYPTO_ALG_TYPE_RNG:
			old_rng = container_of(alg, struct rng_alg, base);
			new_rng = container_of(new_alg, struct rng_alg, base);

			old_rng->generate	= new_rng->generate;
			old_rng->seed		= new_rng->seed;
			old_rng->set_ent	= new_rng->set_ent;
			break;
		default:
			/*
			 * This should never happen: every item on the
			 * fips140_algorithms list should match one of the
			 * cases above, so if we end up here, something is
			 * definitely wrong.
			 */
			pr_crit("Unexpected type %u for algo %s, giving up ...\n",
				alg->cra_flags & CRYPTO_ALG_TYPE_MASK,
				alg->cra_driver_name);
			return false;
		}

		/*
		 * Move the algorithm back to the algorithm list, so it is
		 * visible in /proc/crypto et al.
		 */
		down_write(&crypto_alg_sem);
		list_add_tail(&alg->cra_list, &crypto_alg_list);
	}
	up_write(&crypto_alg_sem);

	return true;
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

/*
 * Initialize the FIPS 140 module.
 *
 * Note: this routine iterates over the contents of the initcall section, which
 * consists of an array of function pointers that was emitted by the linker
 * rather than the compiler. This means that these function pointers lack the
 * usual CFI stubs that the compiler emits when CFI codegen is enabled. So
 * let's disable CFI locally when handling the initcall array, to avoid
 * surpises.
 */
static int __init __attribute__((__no_sanitize__("cfi")))
fips140_init(void)
{
	const u32 *initcall;

	pr_info("loading module\n");

	unregister_existing_fips140_algos();

	/* iterate over all init routines present in this module and call them */
	for (initcall = __initcall_start + 1;
	     initcall < &__initcall_end_marker;
	     initcall++) {
		int (*init)(void) = offset_to_ptr(initcall);
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

	if (!update_live_fips140_algos())
		goto panic;

	if (!update_fips140_library_routines())
		goto panic;

	/*
	 * Wait until all tasks have at least been scheduled once and preempted
	 * voluntarily. This ensures that none of the superseded algorithms that
	 * were already in use will still be live.
	 */
	synchronize_rcu_tasks();

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
		pr_crit("integrity check failed -- giving up!\n");
		goto panic;
	}
	pr_info("integrity check passed\n");

	pr_info("module successfully loaded\n");
	return 0;

panic:
	panic("FIPS 140 module load failure");
}

module_init(fips140_init);

MODULE_IMPORT_NS(CRYPTO_INTERNAL);
MODULE_LICENSE("GPL v2");

/*
 * Crypto-related helper functions, reproduced here so that they will be
 * covered by the FIPS 140 integrity check.
 *
 * Non-cryptographic helper functions such as memcpy() can be excluded from the
 * FIPS module, but there is ambiguity about other helper functions like
 * __crypto_xor() and crypto_inc() which aren't cryptographic by themselves,
 * but are more closely associated with cryptography than e.g. memcpy(). To
 * err on the side of caution, we include copies of these in the FIPS module.
 */
void __crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int len)
{
	while (len >= 8) {
		*(u64 *)dst = *(u64 *)src1 ^  *(u64 *)src2;
		dst += 8;
		src1 += 8;
		src2 += 8;
		len -= 8;
	}

	while (len >= 4) {
		*(u32 *)dst = *(u32 *)src1 ^ *(u32 *)src2;
		dst += 4;
		src1 += 4;
		src2 += 4;
		len -= 4;
	}

	while (len >= 2) {
		*(u16 *)dst = *(u16 *)src1 ^ *(u16 *)src2;
		dst += 2;
		src1 += 2;
		src2 += 2;
		len -= 2;
	}

	while (len--)
		*dst++ = *src1++ ^ *src2++;
}

void crypto_inc(u8 *a, unsigned int size)
{
	a += size;

	while (size--)
		if (++*--a)
			break;
}
