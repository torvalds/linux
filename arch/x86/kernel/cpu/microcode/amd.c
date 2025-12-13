// SPDX-License-Identifier: GPL-2.0-only
/*
 *  AMD CPU Microcode Update Driver for Linux
 *
 *  This driver allows to upgrade microcode on F10h AMD
 *  CPUs and later.
 *
 *  Copyright (C) 2008-2011 Advanced Micro Devices Inc.
 *	          2013-2018 Borislav Petkov <bp@alien8.de>
 *
 *  Author: Peter Oruba <peter.oruba@amd.com>
 *
 *  Based on work by:
 *  Tigran Aivazian <aivazian.tigran@gmail.com>
 *
 *  early loader:
 *  Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 *  Author: Jacob Shin <jacob.shin@amd.com>
 *  Fixes: Borislav Petkov <bp@suse.de>
 */
#define pr_fmt(fmt) "microcode: " fmt

#include <linux/earlycpio.h>
#include <linux/firmware.h>
#include <linux/bsearch.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <crypto/sha2.h>

#include <asm/microcode.h>
#include <asm/processor.h>
#include <asm/cmdline.h>
#include <asm/setup.h>
#include <asm/cpu.h>
#include <asm/msr.h>
#include <asm/tlb.h>

#include "internal.h"

struct ucode_patch {
	struct list_head plist;
	void *data;
	unsigned int size;
	u32 patch_id;
	u16 equiv_cpu;
};

static LIST_HEAD(microcode_cache);

#define UCODE_MAGIC			0x00414d44
#define UCODE_EQUIV_CPU_TABLE_TYPE	0x00000000
#define UCODE_UCODE_TYPE		0x00000001

#define SECTION_HDR_SIZE		8
#define CONTAINER_HDR_SZ		12

struct equiv_cpu_entry {
	u32	installed_cpu;
	u32	fixed_errata_mask;
	u32	fixed_errata_compare;
	u16	equiv_cpu;
	u16	res;
} __packed;

struct microcode_header_amd {
	u32	data_code;
	u32	patch_id;
	u16	mc_patch_data_id;
	u8	mc_patch_data_len;
	u8	init_flag;
	u32	mc_patch_data_checksum;
	u32	nb_dev_id;
	u32	sb_dev_id;
	u16	processor_rev_id;
	u8	nb_rev_id;
	u8	sb_rev_id;
	u8	bios_api_rev;
	u8	reserved1[3];
	u32	match_reg[8];
} __packed;

struct microcode_amd {
	struct microcode_header_amd	hdr;
	unsigned int			mpb[];
};

static struct equiv_cpu_table {
	unsigned int num_entries;
	struct equiv_cpu_entry *entry;
} equiv_table;

union zen_patch_rev {
	struct {
		__u32 rev	 : 8,
		      stepping	 : 4,
		      model	 : 4,
		      __reserved : 4,
		      ext_model	 : 4,
		      ext_fam	 : 8;
	};
	__u32 ucode_rev;
};

union cpuid_1_eax {
	struct {
		__u32 stepping    : 4,
		      model	  : 4,
		      family	  : 4,
		      __reserved0 : 4,
		      ext_model   : 4,
		      ext_fam     : 8,
		      __reserved1 : 4;
	};
	__u32 full;
};

/*
 * This points to the current valid container of microcode patches which we will
 * save from the initrd/builtin before jettisoning its contents. @mc is the
 * microcode patch we found to match.
 */
struct cont_desc {
	struct microcode_amd *mc;
	u32		     psize;
	u8		     *data;
	size_t		     size;
};

/*
 * Microcode patch container file is prepended to the initrd in cpio
 * format. See Documentation/arch/x86/microcode.rst
 */
static const char
ucode_path[] __maybe_unused = "kernel/x86/microcode/AuthenticAMD.bin";

/*
 * This is CPUID(1).EAX on the BSP. It is used in two ways:
 *
 * 1. To ignore the equivalence table on Zen1 and newer.
 *
 * 2. To match which patches to load because the patch revision ID
 *    already contains the f/m/s for which the microcode is destined
 *    for.
 */
static u32 bsp_cpuid_1_eax __ro_after_init;

static bool sha_check = true;

struct patch_digest {
	u32 patch_id;
	u8 sha256[SHA256_DIGEST_SIZE];
};

#include "amd_shas.c"

static int cmp_id(const void *key, const void *elem)
{
	struct patch_digest *pd = (struct patch_digest *)elem;
	u32 patch_id = *(u32 *)key;

	if (patch_id == pd->patch_id)
		return 0;
	else if (patch_id < pd->patch_id)
		return -1;
	else
		return 1;
}

static u32 cpuid_to_ucode_rev(unsigned int val)
{
	union zen_patch_rev p = {};
	union cpuid_1_eax c;

	c.full = val;

	p.stepping  = c.stepping;
	p.model     = c.model;
	p.ext_model = c.ext_model;
	p.ext_fam   = c.ext_fam;

	return p.ucode_rev;
}

static bool need_sha_check(u32 cur_rev)
{
	if (!cur_rev) {
		cur_rev = cpuid_to_ucode_rev(bsp_cpuid_1_eax);
		pr_info_once("No current revision, generating the lowest one: 0x%x\n", cur_rev);
	}

	switch (cur_rev >> 8) {
	case 0x80012: return cur_rev <= 0x8001277; break;
	case 0x80082: return cur_rev <= 0x800820f; break;
	case 0x83010: return cur_rev <= 0x830107c; break;
	case 0x86001: return cur_rev <= 0x860010e; break;
	case 0x86081: return cur_rev <= 0x8608108; break;
	case 0x87010: return cur_rev <= 0x8701034; break;
	case 0x8a000: return cur_rev <= 0x8a0000a; break;
	case 0xa0010: return cur_rev <= 0xa00107a; break;
	case 0xa0011: return cur_rev <= 0xa0011da; break;
	case 0xa0012: return cur_rev <= 0xa001243; break;
	case 0xa0082: return cur_rev <= 0xa00820e; break;
	case 0xa1011: return cur_rev <= 0xa101153; break;
	case 0xa1012: return cur_rev <= 0xa10124e; break;
	case 0xa1081: return cur_rev <= 0xa108109; break;
	case 0xa2010: return cur_rev <= 0xa20102f; break;
	case 0xa2012: return cur_rev <= 0xa201212; break;
	case 0xa4041: return cur_rev <= 0xa404109; break;
	case 0xa5000: return cur_rev <= 0xa500013; break;
	case 0xa6012: return cur_rev <= 0xa60120a; break;
	case 0xa7041: return cur_rev <= 0xa704109; break;
	case 0xa7052: return cur_rev <= 0xa705208; break;
	case 0xa7080: return cur_rev <= 0xa708009; break;
	case 0xa70c0: return cur_rev <= 0xa70C009; break;
	case 0xaa001: return cur_rev <= 0xaa00116; break;
	case 0xaa002: return cur_rev <= 0xaa00218; break;
	case 0xb0021: return cur_rev <= 0xb002146; break;
	case 0xb0081: return cur_rev <= 0xb008111; break;
	case 0xb1010: return cur_rev <= 0xb101046; break;
	case 0xb2040: return cur_rev <= 0xb204031; break;
	case 0xb4040: return cur_rev <= 0xb404031; break;
	case 0xb4041: return cur_rev <= 0xb404101; break;
	case 0xb6000: return cur_rev <= 0xb600031; break;
	case 0xb6080: return cur_rev <= 0xb608031; break;
	case 0xb7000: return cur_rev <= 0xb700031; break;
	default: break;
	}

	pr_info("You should not be seeing this. Please send the following couple of lines to x86-<at>-kernel.org\n");
	pr_info("CPUID(1).EAX: 0x%x, current revision: 0x%x\n", bsp_cpuid_1_eax, cur_rev);
	return true;
}

static bool cpu_has_entrysign(void)
{
	unsigned int fam   = x86_family(bsp_cpuid_1_eax);
	unsigned int model = x86_model(bsp_cpuid_1_eax);

	if (fam == 0x17 || fam == 0x19)
		return true;

	if (fam == 0x1a) {
		if (model <= 0x2f ||
		    (0x40 <= model && model <= 0x4f) ||
		    (0x60 <= model && model <= 0x6f))
			return true;
	}

	return false;
}

static bool verify_sha256_digest(u32 patch_id, u32 cur_rev, const u8 *data, unsigned int len)
{
	struct patch_digest *pd = NULL;
	u8 digest[SHA256_DIGEST_SIZE];
	int i;

	if (!cpu_has_entrysign())
		return true;

	if (!need_sha_check(cur_rev))
		return true;

	if (!sha_check)
		return true;

	pd = bsearch(&patch_id, phashes, ARRAY_SIZE(phashes), sizeof(struct patch_digest), cmp_id);
	if (!pd) {
		pr_err("No sha256 digest for patch ID: 0x%x found\n", patch_id);
		return false;
	}

	sha256(data, len, digest);

	if (memcmp(digest, pd->sha256, sizeof(digest))) {
		pr_err("Patch 0x%x SHA256 digest mismatch!\n", patch_id);

		for (i = 0; i < SHA256_DIGEST_SIZE; i++)
			pr_cont("0x%x ", digest[i]);
		pr_info("\n");

		return false;
	}

	return true;
}

static union cpuid_1_eax ucode_rev_to_cpuid(unsigned int val)
{
	union zen_patch_rev p;
	union cpuid_1_eax c;

	p.ucode_rev = val;
	c.full = 0;

	c.stepping  = p.stepping;
	c.model     = p.model;
	c.ext_model = p.ext_model;
	c.family    = 0xf;
	c.ext_fam   = p.ext_fam;

	return c;
}

static u32 get_patch_level(void)
{
	u32 rev, dummy __always_unused;

	if (IS_ENABLED(CONFIG_MICROCODE_DBG)) {
		int cpu = smp_processor_id();

		if (!microcode_rev[cpu]) {
			if (!base_rev)
				base_rev = cpuid_to_ucode_rev(bsp_cpuid_1_eax);

			microcode_rev[cpu] = base_rev;

			ucode_dbg("CPU%d, base_rev: 0x%x\n", cpu, base_rev);
		}

		return microcode_rev[cpu];
	}

	native_rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	return rev;
}

static u16 find_equiv_id(struct equiv_cpu_table *et, u32 sig)
{
	unsigned int i;

	/* Zen and newer do not need an equivalence table. */
	if (x86_family(bsp_cpuid_1_eax) >= 0x17)
		return 0;

	if (!et || !et->num_entries)
		return 0;

	for (i = 0; i < et->num_entries; i++) {
		struct equiv_cpu_entry *e = &et->entry[i];

		if (sig == e->installed_cpu)
			return e->equiv_cpu;
	}
	return 0;
}

/*
 * Check whether there is a valid microcode container file at the beginning
 * of @buf of size @buf_size.
 */
static bool verify_container(const u8 *buf, size_t buf_size)
{
	u32 cont_magic;

	if (buf_size <= CONTAINER_HDR_SZ) {
		ucode_dbg("Truncated microcode container header.\n");
		return false;
	}

	cont_magic = *(const u32 *)buf;
	if (cont_magic != UCODE_MAGIC) {
		ucode_dbg("Invalid magic value (0x%08x).\n", cont_magic);
		return false;
	}

	return true;
}

/*
 * Check whether there is a valid, non-truncated CPU equivalence table at the
 * beginning of @buf of size @buf_size.
 */
static bool verify_equivalence_table(const u8 *buf, size_t buf_size)
{
	const u32 *hdr = (const u32 *)buf;
	u32 cont_type, equiv_tbl_len;

	if (!verify_container(buf, buf_size))
		return false;

	/* Zen and newer do not need an equivalence table. */
	if (x86_family(bsp_cpuid_1_eax) >= 0x17)
		return true;

	cont_type = hdr[1];
	if (cont_type != UCODE_EQUIV_CPU_TABLE_TYPE) {
		ucode_dbg("Wrong microcode container equivalence table type: %u.\n",
			  cont_type);
		return false;
	}

	buf_size -= CONTAINER_HDR_SZ;

	equiv_tbl_len = hdr[2];
	if (equiv_tbl_len < sizeof(struct equiv_cpu_entry) ||
	    buf_size < equiv_tbl_len) {
		ucode_dbg("Truncated equivalence table.\n");
		return false;
	}

	return true;
}

/*
 * Check whether there is a valid, non-truncated microcode patch section at the
 * beginning of @buf of size @buf_size.
 *
 * On success, @sh_psize returns the patch size according to the section header,
 * to the caller.
 */
static bool __verify_patch_section(const u8 *buf, size_t buf_size, u32 *sh_psize)
{
	u32 p_type, p_size;
	const u32 *hdr;

	if (buf_size < SECTION_HDR_SIZE) {
		ucode_dbg("Truncated patch section.\n");
		return false;
	}

	hdr = (const u32 *)buf;
	p_type = hdr[0];
	p_size = hdr[1];

	if (p_type != UCODE_UCODE_TYPE) {
		ucode_dbg("Invalid type field (0x%x) in container file section header.\n",
			  p_type);
		return false;
	}

	if (p_size < sizeof(struct microcode_header_amd)) {
		ucode_dbg("Patch of size %u too short.\n", p_size);
		return false;
	}

	*sh_psize = p_size;

	return true;
}

/*
 * Check whether the passed remaining file @buf_size is large enough to contain
 * a patch of the indicated @sh_psize (and also whether this size does not
 * exceed the per-family maximum). @sh_psize is the size read from the section
 * header.
 */
static bool __verify_patch_size(u32 sh_psize, size_t buf_size)
{
	u8 family = x86_family(bsp_cpuid_1_eax);
	u32 max_size;

	if (family >= 0x15)
		goto ret;

#define F1XH_MPB_MAX_SIZE 2048
#define F14H_MPB_MAX_SIZE 1824

	switch (family) {
	case 0x10 ... 0x12:
		max_size = F1XH_MPB_MAX_SIZE;
		break;
	case 0x14:
		max_size = F14H_MPB_MAX_SIZE;
		break;
	default:
		WARN(1, "%s: WTF family: 0x%x\n", __func__, family);
		return false;
	}

	if (sh_psize > max_size)
		return false;

ret:
	/* Working with the whole buffer so < is ok. */
	return sh_psize <= buf_size;
}

/*
 * Verify the patch in @buf.
 *
 * Returns:
 * negative: on error
 * positive: patch is not for this family, skip it
 * 0: success
 */
static int verify_patch(const u8 *buf, size_t buf_size, u32 *patch_size)
{
	u8 family = x86_family(bsp_cpuid_1_eax);
	struct microcode_header_amd *mc_hdr;
	u32 sh_psize;
	u16 proc_id;
	u8 patch_fam;

	if (!__verify_patch_section(buf, buf_size, &sh_psize))
		return -1;

	/*
	 * The section header length is not included in this indicated size
	 * but is present in the leftover file length so we need to subtract
	 * it before passing this value to the function below.
	 */
	buf_size -= SECTION_HDR_SIZE;

	/*
	 * Check if the remaining buffer is big enough to contain a patch of
	 * size sh_psize, as the section claims.
	 */
	if (buf_size < sh_psize) {
		ucode_dbg("Patch of size %u truncated.\n", sh_psize);
		return -1;
	}

	if (!__verify_patch_size(sh_psize, buf_size)) {
		ucode_dbg("Per-family patch size mismatch.\n");
		return -1;
	}

	*patch_size = sh_psize;

	mc_hdr	= (struct microcode_header_amd *)(buf + SECTION_HDR_SIZE);
	if (mc_hdr->nb_dev_id || mc_hdr->sb_dev_id) {
		pr_err("Patch-ID 0x%08x: chipset-specific code unsupported.\n", mc_hdr->patch_id);
		return -1;
	}

	proc_id	= mc_hdr->processor_rev_id;
	patch_fam = 0xf + (proc_id >> 12);

	ucode_dbg("Patch-ID 0x%08x: family: 0x%x\n", mc_hdr->patch_id, patch_fam);

	if (patch_fam != family)
		return 1;

	return 0;
}

static bool mc_patch_matches(struct microcode_amd *mc, u16 eq_id)
{
	/* Zen and newer do not need an equivalence table. */
	if (x86_family(bsp_cpuid_1_eax) >= 0x17)
		return ucode_rev_to_cpuid(mc->hdr.patch_id).full == bsp_cpuid_1_eax;
	else
		return eq_id == mc->hdr.processor_rev_id;
}

/*
 * This scans the ucode blob for the proper container as we can have multiple
 * containers glued together.
 *
 * Returns the amount of bytes consumed while scanning. @desc contains all the
 * data we're going to use in later stages of the application.
 */
static size_t parse_container(u8 *ucode, size_t size, struct cont_desc *desc)
{
	struct equiv_cpu_table table;
	size_t orig_size = size;
	u32 *hdr = (u32 *)ucode;
	u16 eq_id;
	u8 *buf;

	if (!verify_equivalence_table(ucode, size))
		return 0;

	buf = ucode;

	table.entry = (struct equiv_cpu_entry *)(buf + CONTAINER_HDR_SZ);
	table.num_entries = hdr[2] / sizeof(struct equiv_cpu_entry);

	/*
	 * Find the equivalence ID of our CPU in this table. Even if this table
	 * doesn't contain a patch for the CPU, scan through the whole container
	 * so that it can be skipped in case there are other containers appended.
	 */
	eq_id = find_equiv_id(&table, bsp_cpuid_1_eax);

	buf  += hdr[2] + CONTAINER_HDR_SZ;
	size -= hdr[2] + CONTAINER_HDR_SZ;

	/*
	 * Scan through the rest of the container to find where it ends. We do
	 * some basic sanity-checking too.
	 */
	while (size > 0) {
		struct microcode_amd *mc;
		u32 patch_size;
		int ret;

		ret = verify_patch(buf, size, &patch_size);
		if (ret < 0) {
			/*
			 * Patch verification failed, skip to the next container, if
			 * there is one. Before exit, check whether that container has
			 * found a patch already. If so, use it.
			 */
			goto out;
		} else if (ret > 0) {
			goto skip;
		}

		mc = (struct microcode_amd *)(buf + SECTION_HDR_SIZE);

		ucode_dbg("patch_id: 0x%x\n", mc->hdr.patch_id);

		if (mc_patch_matches(mc, eq_id)) {
			desc->psize = patch_size;
			desc->mc = mc;

			ucode_dbg(" match: size: %d\n", patch_size);
		}

skip:
		/* Skip patch section header too: */
		buf  += patch_size + SECTION_HDR_SIZE;
		size -= patch_size + SECTION_HDR_SIZE;
	}

out:
	/*
	 * If we have found a patch (desc->mc), it means we're looking at the
	 * container which has a patch for this CPU so return 0 to mean, @ucode
	 * already points to the proper container. Otherwise, we return the size
	 * we scanned so that we can advance to the next container in the
	 * buffer.
	 */
	if (desc->mc) {
		desc->data = ucode;
		desc->size = orig_size - size;

		return 0;
	}

	return orig_size - size;
}

/*
 * Scan the ucode blob for the proper container as we can have multiple
 * containers glued together.
 */
static void scan_containers(u8 *ucode, size_t size, struct cont_desc *desc)
{
	while (size) {
		size_t s = parse_container(ucode, size, desc);
		if (!s)
			return;

		/* catch wraparound */
		if (size >= s) {
			ucode += s;
			size  -= s;
		} else {
			return;
		}
	}
}

static bool __apply_microcode_amd(struct microcode_amd *mc, u32 *cur_rev,
				  unsigned int psize)
{
	unsigned long p_addr = (unsigned long)&mc->hdr.data_code;

	if (!verify_sha256_digest(mc->hdr.patch_id, *cur_rev, (const u8 *)p_addr, psize))
		return false;

	native_wrmsrq(MSR_AMD64_PATCH_LOADER, p_addr);

	if (x86_family(bsp_cpuid_1_eax) == 0x17) {
		unsigned long p_addr_end = p_addr + psize - 1;

		invlpg(p_addr);

		/*
		 * Flush next page too if patch image is crossing a page
		 * boundary.
		 */
		if (p_addr >> PAGE_SHIFT != p_addr_end >> PAGE_SHIFT)
			invlpg(p_addr_end);
	}

	if (IS_ENABLED(CONFIG_MICROCODE_DBG))
		microcode_rev[smp_processor_id()] = mc->hdr.patch_id;

	/* verify patch application was successful */
	*cur_rev = get_patch_level();

	ucode_dbg("updated rev: 0x%x\n", *cur_rev);

	if (*cur_rev != mc->hdr.patch_id)
		return false;

	return true;
}

static bool get_builtin_microcode(struct cpio_data *cp)
{
	char fw_name[36] = "amd-ucode/microcode_amd.bin";
	u8 family = x86_family(bsp_cpuid_1_eax);
	struct firmware fw;

	if (IS_ENABLED(CONFIG_X86_32))
		return false;

	if (family >= 0x15)
		snprintf(fw_name, sizeof(fw_name),
			 "amd-ucode/microcode_amd_fam%02hhxh.bin", family);

	if (firmware_request_builtin(&fw, fw_name)) {
		cp->size = fw.size;
		cp->data = (void *)fw.data;
		return true;
	}

	return false;
}

static bool __init find_blobs_in_containers(struct cpio_data *ret)
{
	struct cpio_data cp;
	bool found;

	if (!get_builtin_microcode(&cp))
		cp = find_microcode_in_initrd(ucode_path);

	found = cp.data && cp.size;
	if (found)
		*ret = cp;

	return found;
}

/*
 * Early load occurs before we can vmalloc(). So we look for the microcode
 * patch container file in initrd, traverse equivalent cpu table, look for a
 * matching microcode patch, and update, all in initrd memory in place.
 * When vmalloc() is available for use later -- on 64-bit during first AP load,
 * and on 32-bit during save_microcode_in_initrd() -- we can call
 * load_microcode_amd() to save equivalent cpu table and microcode patches in
 * kernel heap memory.
 */
void __init load_ucode_amd_bsp(struct early_load_data *ed, unsigned int cpuid_1_eax)
{
	struct cont_desc desc = { };
	struct microcode_amd *mc;
	struct cpio_data cp = { };
	char buf[4];
	u32 rev;

	if (cmdline_find_option(boot_command_line, "microcode.amd_sha_check", buf, 4)) {
		if (!strncmp(buf, "off", 3)) {
			sha_check = false;
			pr_warn_once("It is a very very bad idea to disable the blobs SHA check!\n");
			add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
		}
	}

	bsp_cpuid_1_eax = cpuid_1_eax;

	rev = get_patch_level();
	ed->old_rev = rev;

	/* Needed in load_microcode_amd() */
	ucode_cpu_info[0].cpu_sig.sig = cpuid_1_eax;

	if (!find_blobs_in_containers(&cp))
		return;

	scan_containers(cp.data, cp.size, &desc);

	mc = desc.mc;
	if (!mc)
		return;

	/*
	 * Allow application of the same revision to pick up SMT-specific
	 * changes even if the revision of the other SMT thread is already
	 * up-to-date.
	 */
	if (ed->old_rev > mc->hdr.patch_id)
		return;

	if (__apply_microcode_amd(mc, &rev, desc.psize))
		ed->new_rev = rev;
}

static inline bool patch_cpus_equivalent(struct ucode_patch *p,
					 struct ucode_patch *n,
					 bool ignore_stepping)
{
	/* Zen and newer hardcode the f/m/s in the patch ID */
        if (x86_family(bsp_cpuid_1_eax) >= 0x17) {
		union cpuid_1_eax p_cid = ucode_rev_to_cpuid(p->patch_id);
		union cpuid_1_eax n_cid = ucode_rev_to_cpuid(n->patch_id);

		if (ignore_stepping) {
			p_cid.stepping = 0;
			n_cid.stepping = 0;
		}

		return p_cid.full == n_cid.full;
	} else {
		return p->equiv_cpu == n->equiv_cpu;
	}
}

/*
 * a small, trivial cache of per-family ucode patches
 */
static struct ucode_patch *cache_find_patch(struct ucode_cpu_info *uci, u16 equiv_cpu)
{
	struct ucode_patch *p;
	struct ucode_patch n;

	n.equiv_cpu = equiv_cpu;
	n.patch_id  = uci->cpu_sig.rev;

	list_for_each_entry(p, &microcode_cache, plist)
		if (patch_cpus_equivalent(p, &n, false))
			return p;

	return NULL;
}

static inline int patch_newer(struct ucode_patch *p, struct ucode_patch *n)
{
	/* Zen and newer hardcode the f/m/s in the patch ID */
        if (x86_family(bsp_cpuid_1_eax) >= 0x17) {
		union zen_patch_rev zp, zn;

		zp.ucode_rev = p->patch_id;
		zn.ucode_rev = n->patch_id;

		if (zn.stepping != zp.stepping)
			return -1;

		return zn.rev > zp.rev;
	} else {
		return n->patch_id > p->patch_id;
	}
}

static void update_cache(struct ucode_patch *new_patch)
{
	struct ucode_patch *p;
	int ret;

	list_for_each_entry(p, &microcode_cache, plist) {
		if (patch_cpus_equivalent(p, new_patch, true)) {
			ret = patch_newer(p, new_patch);
			if (ret < 0)
				continue;
			else if (!ret) {
				/* we already have the latest patch */
				kfree(new_patch->data);
				kfree(new_patch);
				return;
			}

			list_replace(&p->plist, &new_patch->plist);
			kfree(p->data);
			kfree(p);
			return;
		}
	}
	/* no patch found, add it */
	list_add_tail(&new_patch->plist, &microcode_cache);
}

static void free_cache(void)
{
	struct ucode_patch *p, *tmp;

	list_for_each_entry_safe(p, tmp, &microcode_cache, plist) {
		__list_del(p->plist.prev, p->plist.next);
		kfree(p->data);
		kfree(p);
	}
}

static struct ucode_patch *find_patch(unsigned int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	u16 equiv_id = 0;

	uci->cpu_sig.rev = get_patch_level();

	if (x86_family(bsp_cpuid_1_eax) < 0x17) {
		equiv_id = find_equiv_id(&equiv_table, uci->cpu_sig.sig);
		if (!equiv_id)
			return NULL;
	}

	return cache_find_patch(uci, equiv_id);
}

void reload_ucode_amd(unsigned int cpu)
{
	u32 rev, dummy __always_unused;
	struct microcode_amd *mc;
	struct ucode_patch *p;

	p = find_patch(cpu);
	if (!p)
		return;

	mc = p->data;

	rev = get_patch_level();
	if (rev < mc->hdr.patch_id) {
		if (__apply_microcode_amd(mc, &rev, p->size))
			pr_info_once("reload revision: 0x%08x\n", rev);
	}
}

static int collect_cpu_info_amd(int cpu, struct cpu_signature *csig)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct ucode_patch *p;

	csig->sig = cpuid_eax(0x00000001);
	csig->rev = get_patch_level();

	/*
	 * a patch could have been loaded early, set uci->mc so that
	 * mc_bp_resume() can call apply_microcode()
	 */
	p = find_patch(cpu);
	if (p && (p->patch_id == csig->rev))
		uci->mc = p->data;

	return 0;
}

static enum ucode_state apply_microcode_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct microcode_amd *mc_amd;
	struct ucode_cpu_info *uci;
	struct ucode_patch *p;
	enum ucode_state ret;
	u32 rev;

	BUG_ON(raw_smp_processor_id() != cpu);

	uci = ucode_cpu_info + cpu;

	p = find_patch(cpu);
	if (!p)
		return UCODE_NFOUND;

	rev = uci->cpu_sig.rev;

	mc_amd  = p->data;
	uci->mc = p->data;

	/* need to apply patch? */
	if (rev > mc_amd->hdr.patch_id) {
		ret = UCODE_OK;
		goto out;
	}

	if (!__apply_microcode_amd(mc_amd, &rev, p->size)) {
		pr_err("CPU%d: update failed for patch_level=0x%08x\n",
			cpu, mc_amd->hdr.patch_id);
		return UCODE_ERROR;
	}

	rev = mc_amd->hdr.patch_id;
	ret = UCODE_UPDATED;

out:
	uci->cpu_sig.rev = rev;
	c->microcode	 = rev;

	/* Update boot_cpu_data's revision too, if we're on the BSP: */
	if (c->cpu_index == boot_cpu_data.cpu_index)
		boot_cpu_data.microcode = rev;

	return ret;
}

void load_ucode_amd_ap(unsigned int cpuid_1_eax)
{
	unsigned int cpu = smp_processor_id();

	ucode_cpu_info[cpu].cpu_sig.sig = cpuid_1_eax;
	apply_microcode_amd(cpu);
}

static size_t install_equiv_cpu_table(const u8 *buf, size_t buf_size)
{
	u32 equiv_tbl_len;
	const u32 *hdr;

	if (!verify_equivalence_table(buf, buf_size))
		return 0;

	hdr = (const u32 *)buf;
	equiv_tbl_len = hdr[2];

	/* Zen and newer do not need an equivalence table. */
	if (x86_family(bsp_cpuid_1_eax) >= 0x17)
		goto out;

	equiv_table.entry = vmalloc(equiv_tbl_len);
	if (!equiv_table.entry) {
		pr_err("failed to allocate equivalent CPU table\n");
		return 0;
	}

	memcpy(equiv_table.entry, buf + CONTAINER_HDR_SZ, equiv_tbl_len);
	equiv_table.num_entries = equiv_tbl_len / sizeof(struct equiv_cpu_entry);

out:
	/* add header length */
	return equiv_tbl_len + CONTAINER_HDR_SZ;
}

static void free_equiv_cpu_table(void)
{
	if (x86_family(bsp_cpuid_1_eax) >= 0x17)
		return;

	vfree(equiv_table.entry);
	memset(&equiv_table, 0, sizeof(equiv_table));
}

static void cleanup(void)
{
	free_equiv_cpu_table();
	free_cache();
}

/*
 * Return a non-negative value even if some of the checks failed so that
 * we can skip over the next patch. If we return a negative value, we
 * signal a grave error like a memory allocation has failed and the
 * driver cannot continue functioning normally. In such cases, we tear
 * down everything we've used up so far and exit.
 */
static int verify_and_add_patch(u8 family, u8 *fw, unsigned int leftover,
				unsigned int *patch_size)
{
	struct microcode_header_amd *mc_hdr;
	struct ucode_patch *patch;
	u16 proc_id;
	int ret;

	ret = verify_patch(fw, leftover, patch_size);
	if (ret)
		return ret;

	patch = kzalloc(sizeof(*patch), GFP_KERNEL);
	if (!patch) {
		pr_err("Patch allocation failure.\n");
		return -EINVAL;
	}

	patch->data = kmemdup(fw + SECTION_HDR_SIZE, *patch_size, GFP_KERNEL);
	if (!patch->data) {
		pr_err("Patch data allocation failure.\n");
		kfree(patch);
		return -EINVAL;
	}
	patch->size = *patch_size;

	mc_hdr      = (struct microcode_header_amd *)(fw + SECTION_HDR_SIZE);
	proc_id     = mc_hdr->processor_rev_id;

	INIT_LIST_HEAD(&patch->plist);
	patch->patch_id  = mc_hdr->patch_id;
	patch->equiv_cpu = proc_id;

	ucode_dbg("%s: Adding patch_id: 0x%08x, proc_id: 0x%04x\n",
		 __func__, patch->patch_id, proc_id);

	/* ... and add to cache. */
	update_cache(patch);

	return 0;
}

/* Scan the blob in @data and add microcode patches to the cache. */
static enum ucode_state __load_microcode_amd(u8 family, const u8 *data, size_t size)
{
	u8 *fw = (u8 *)data;
	size_t offset;

	offset = install_equiv_cpu_table(data, size);
	if (!offset)
		return UCODE_ERROR;

	fw   += offset;
	size -= offset;

	if (*(u32 *)fw != UCODE_UCODE_TYPE) {
		pr_err("invalid type field in container file section header\n");
		free_equiv_cpu_table();
		return UCODE_ERROR;
	}

	while (size > 0) {
		unsigned int crnt_size = 0;
		int ret;

		ret = verify_and_add_patch(family, fw, size, &crnt_size);
		if (ret < 0)
			return UCODE_ERROR;

		fw   +=  crnt_size + SECTION_HDR_SIZE;
		size -= (crnt_size + SECTION_HDR_SIZE);
	}

	return UCODE_OK;
}

static enum ucode_state _load_microcode_amd(u8 family, const u8 *data, size_t size)
{
	enum ucode_state ret;

	/* free old equiv table */
	free_equiv_cpu_table();

	ret = __load_microcode_amd(family, data, size);
	if (ret != UCODE_OK)
		cleanup();

	return ret;
}

static enum ucode_state load_microcode_amd(u8 family, const u8 *data, size_t size)
{
	struct cpuinfo_x86 *c;
	unsigned int nid, cpu;
	struct ucode_patch *p;
	enum ucode_state ret;

	ret = _load_microcode_amd(family, data, size);
	if (ret != UCODE_OK)
		return ret;

	for_each_node_with_cpus(nid) {
		cpu = cpumask_first(cpumask_of_node(nid));
		c = &cpu_data(cpu);

		p = find_patch(cpu);
		if (!p)
			continue;

		if (c->microcode >= p->patch_id)
			continue;

		ret = UCODE_NEW;
	}

	return ret;
}

static int __init save_microcode_in_initrd(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;
	struct cont_desc desc = { 0 };
	unsigned int cpuid_1_eax;
	enum ucode_state ret;
	struct cpio_data cp;

	if (microcode_loader_disabled() || c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10)
		return 0;

	cpuid_1_eax = native_cpuid_eax(1);

	if (!find_blobs_in_containers(&cp))
		return -EINVAL;

	scan_containers(cp.data, cp.size, &desc);
	if (!desc.mc)
		return -EINVAL;

	ret = _load_microcode_amd(x86_family(cpuid_1_eax), desc.data, desc.size);
	if (ret > UCODE_UPDATED)
		return -EINVAL;

	return 0;
}
early_initcall(save_microcode_in_initrd);

/*
 * AMD microcode firmware naming convention, up to family 15h they are in
 * the legacy file:
 *
 *    amd-ucode/microcode_amd.bin
 *
 * This legacy file is always smaller than 2K in size.
 *
 * Beginning with family 15h, they are in family-specific firmware files:
 *
 *    amd-ucode/microcode_amd_fam15h.bin
 *    amd-ucode/microcode_amd_fam16h.bin
 *    ...
 *
 * These might be larger than 2K.
 */
static enum ucode_state request_microcode_amd(int cpu, struct device *device)
{
	char fw_name[36] = "amd-ucode/microcode_amd.bin";
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	enum ucode_state ret = UCODE_NFOUND;
	const struct firmware *fw;

	if (force_minrev)
		return UCODE_NFOUND;

	if (c->x86 >= 0x15)
		snprintf(fw_name, sizeof(fw_name), "amd-ucode/microcode_amd_fam%.2xh.bin", c->x86);

	if (request_firmware_direct(&fw, (const char *)fw_name, device)) {
		ucode_dbg("failed to load file %s\n", fw_name);
		goto out;
	}

	ret = UCODE_ERROR;
	if (!verify_container(fw->data, fw->size))
		goto fw_release;

	ret = load_microcode_amd(c->x86, fw->data, fw->size);

 fw_release:
	release_firmware(fw);

 out:
	return ret;
}

static void microcode_fini_cpu_amd(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	uci->mc = NULL;
}

static void finalize_late_load_amd(int result)
{
	if (result)
		cleanup();
}

static struct microcode_ops microcode_amd_ops = {
	.request_microcode_fw	= request_microcode_amd,
	.collect_cpu_info	= collect_cpu_info_amd,
	.apply_microcode	= apply_microcode_amd,
	.microcode_fini_cpu	= microcode_fini_cpu_amd,
	.finalize_late_load	= finalize_late_load_amd,
	.nmi_safe		= true,
};

struct microcode_ops * __init init_amd_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		pr_warn("AMD CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}
	return &microcode_amd_ops;
}

void __exit exit_amd_microcode(void)
{
	cleanup();
}
