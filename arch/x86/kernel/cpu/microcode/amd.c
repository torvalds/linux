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
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <asm/microcode.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/cpu.h>
#include <asm/msr.h>

#include "internal.h"

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

#define PATCH_MAX_SIZE (3 * PAGE_SIZE)

static struct equiv_cpu_table {
	unsigned int num_entries;
	struct equiv_cpu_entry *entry;
} equiv_table;

/*
 * This points to the current valid container of microcode patches which we will
 * save from the initrd/builtin before jettisoning its contents. @mc is the
 * microcode patch we found to match.
 */
struct cont_desc {
	struct microcode_amd *mc;
	u32		     cpuid_1_eax;
	u32		     psize;
	u8		     *data;
	size_t		     size;
};

static u32 ucode_new_rev;

/*
 * Microcode patch container file is prepended to the initrd in cpio
 * format. See Documentation/arch/x86/microcode.rst
 */
static const char
ucode_path[] __maybe_unused = "kernel/x86/microcode/AuthenticAMD.bin";

static u16 find_equiv_id(struct equiv_cpu_table *et, u32 sig)
{
	unsigned int i;

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
 * of @buf of size @buf_size. Set @early to use this function in the early path.
 */
static bool verify_container(const u8 *buf, size_t buf_size, bool early)
{
	u32 cont_magic;

	if (buf_size <= CONTAINER_HDR_SZ) {
		if (!early)
			pr_debug("Truncated microcode container header.\n");

		return false;
	}

	cont_magic = *(const u32 *)buf;
	if (cont_magic != UCODE_MAGIC) {
		if (!early)
			pr_debug("Invalid magic value (0x%08x).\n", cont_magic);

		return false;
	}

	return true;
}

/*
 * Check whether there is a valid, non-truncated CPU equivalence table at the
 * beginning of @buf of size @buf_size. Set @early to use this function in the
 * early path.
 */
static bool verify_equivalence_table(const u8 *buf, size_t buf_size, bool early)
{
	const u32 *hdr = (const u32 *)buf;
	u32 cont_type, equiv_tbl_len;

	if (!verify_container(buf, buf_size, early))
		return false;

	cont_type = hdr[1];
	if (cont_type != UCODE_EQUIV_CPU_TABLE_TYPE) {
		if (!early)
			pr_debug("Wrong microcode container equivalence table type: %u.\n",
			       cont_type);

		return false;
	}

	buf_size -= CONTAINER_HDR_SZ;

	equiv_tbl_len = hdr[2];
	if (equiv_tbl_len < sizeof(struct equiv_cpu_entry) ||
	    buf_size < equiv_tbl_len) {
		if (!early)
			pr_debug("Truncated equivalence table.\n");

		return false;
	}

	return true;
}

/*
 * Check whether there is a valid, non-truncated microcode patch section at the
 * beginning of @buf of size @buf_size. Set @early to use this function in the
 * early path.
 *
 * On success, @sh_psize returns the patch size according to the section header,
 * to the caller.
 */
static bool
__verify_patch_section(const u8 *buf, size_t buf_size, u32 *sh_psize, bool early)
{
	u32 p_type, p_size;
	const u32 *hdr;

	if (buf_size < SECTION_HDR_SIZE) {
		if (!early)
			pr_debug("Truncated patch section.\n");

		return false;
	}

	hdr = (const u32 *)buf;
	p_type = hdr[0];
	p_size = hdr[1];

	if (p_type != UCODE_UCODE_TYPE) {
		if (!early)
			pr_debug("Invalid type field (0x%x) in container file section header.\n",
				p_type);

		return false;
	}

	if (p_size < sizeof(struct microcode_header_amd)) {
		if (!early)
			pr_debug("Patch of size %u too short.\n", p_size);

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
static unsigned int __verify_patch_size(u8 family, u32 sh_psize, size_t buf_size)
{
	u32 max_size;

	if (family >= 0x15)
		return min_t(u32, sh_psize, buf_size);

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
		return 0;
	}

	if (sh_psize > min_t(u32, buf_size, max_size))
		return 0;

	return sh_psize;
}

/*
 * Verify the patch in @buf.
 *
 * Returns:
 * negative: on error
 * positive: patch is not for this family, skip it
 * 0: success
 */
static int
verify_patch(u8 family, const u8 *buf, size_t buf_size, u32 *patch_size, bool early)
{
	struct microcode_header_amd *mc_hdr;
	unsigned int ret;
	u32 sh_psize;
	u16 proc_id;
	u8 patch_fam;

	if (!__verify_patch_section(buf, buf_size, &sh_psize, early))
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
		if (!early)
			pr_debug("Patch of size %u truncated.\n", sh_psize);

		return -1;
	}

	ret = __verify_patch_size(family, sh_psize, buf_size);
	if (!ret) {
		if (!early)
			pr_debug("Per-family patch size mismatch.\n");
		return -1;
	}

	*patch_size = sh_psize;

	mc_hdr	= (struct microcode_header_amd *)(buf + SECTION_HDR_SIZE);
	if (mc_hdr->nb_dev_id || mc_hdr->sb_dev_id) {
		if (!early)
			pr_err("Patch-ID 0x%08x: chipset-specific code unsupported.\n", mc_hdr->patch_id);
		return -1;
	}

	proc_id	= mc_hdr->processor_rev_id;
	patch_fam = 0xf + (proc_id >> 12);
	if (patch_fam != family)
		return 1;

	return 0;
}

/*
 * This scans the ucode blob for the proper container as we can have multiple
 * containers glued together. Returns the equivalence ID from the equivalence
 * table or 0 if none found.
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

	if (!verify_equivalence_table(ucode, size, true))
		return 0;

	buf = ucode;

	table.entry = (struct equiv_cpu_entry *)(buf + CONTAINER_HDR_SZ);
	table.num_entries = hdr[2] / sizeof(struct equiv_cpu_entry);

	/*
	 * Find the equivalence ID of our CPU in this table. Even if this table
	 * doesn't contain a patch for the CPU, scan through the whole container
	 * so that it can be skipped in case there are other containers appended.
	 */
	eq_id = find_equiv_id(&table, desc->cpuid_1_eax);

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

		ret = verify_patch(x86_family(desc->cpuid_1_eax), buf, size, &patch_size, true);
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
		if (eq_id == mc->hdr.processor_rev_id) {
			desc->psize = patch_size;
			desc->mc = mc;
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

static int __apply_microcode_amd(struct microcode_amd *mc)
{
	u32 rev, dummy;

	native_wrmsrl(MSR_AMD64_PATCH_LOADER, (u64)(long)&mc->hdr.data_code);

	/* verify patch application was successful */
	native_rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);
	if (rev != mc->hdr.patch_id)
		return -1;

	return 0;
}

/*
 * Early load occurs before we can vmalloc(). So we look for the microcode
 * patch container file in initrd, traverse equivalent cpu table, look for a
 * matching microcode patch, and update, all in initrd memory in place.
 * When vmalloc() is available for use later -- on 64-bit during first AP load,
 * and on 32-bit during save_microcode_in_initrd_amd() -- we can call
 * load_microcode_amd() to save equivalent cpu table and microcode patches in
 * kernel heap memory.
 *
 * Returns true if container found (sets @desc), false otherwise.
 */
static bool early_apply_microcode(u32 cpuid_1_eax, void *ucode, size_t size)
{
	struct cont_desc desc = { 0 };
	struct microcode_amd *mc;
	u32 rev, dummy, *new_rev;
	bool ret = false;

#ifdef CONFIG_X86_32
	new_rev = (u32 *)__pa_nodebug(&ucode_new_rev);
#else
	new_rev = &ucode_new_rev;
#endif

	desc.cpuid_1_eax = cpuid_1_eax;

	scan_containers(ucode, size, &desc);

	mc = desc.mc;
	if (!mc)
		return ret;

	native_rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	/*
	 * Allow application of the same revision to pick up SMT-specific
	 * changes even if the revision of the other SMT thread is already
	 * up-to-date.
	 */
	if (rev > mc->hdr.patch_id)
		return ret;

	if (!__apply_microcode_amd(mc)) {
		*new_rev = mc->hdr.patch_id;
		ret      = true;
	}

	return ret;
}

static bool get_builtin_microcode(struct cpio_data *cp, unsigned int family)
{
	char fw_name[36] = "amd-ucode/microcode_amd.bin";
	struct firmware fw;

	if (IS_ENABLED(CONFIG_X86_32))
		return false;

	if (family >= 0x15)
		snprintf(fw_name, sizeof(fw_name),
			 "amd-ucode/microcode_amd_fam%.2xh.bin", family);

	if (firmware_request_builtin(&fw, fw_name)) {
		cp->size = fw.size;
		cp->data = (void *)fw.data;
		return true;
	}

	return false;
}

static void find_blobs_in_containers(unsigned int cpuid_1_eax, struct cpio_data *ret)
{
	struct ucode_cpu_info *uci;
	struct cpio_data cp;
	const char *path;
	bool use_pa;

	if (IS_ENABLED(CONFIG_X86_32)) {
		uci	= (struct ucode_cpu_info *)__pa_nodebug(ucode_cpu_info);
		path	= (const char *)__pa_nodebug(ucode_path);
		use_pa	= true;
	} else {
		uci     = ucode_cpu_info;
		path	= ucode_path;
		use_pa	= false;
	}

	if (!get_builtin_microcode(&cp, x86_family(cpuid_1_eax)))
		cp = find_microcode_in_initrd(path, use_pa);

	/* Needed in load_microcode_amd() */
	uci->cpu_sig.sig = cpuid_1_eax;

	*ret = cp;
}

static void apply_ucode_from_containers(unsigned int cpuid_1_eax)
{
	struct cpio_data cp = { };

	find_blobs_in_containers(cpuid_1_eax, &cp);
	if (!(cp.data && cp.size))
		return;

	early_apply_microcode(cpuid_1_eax, cp.data, cp.size);
}

void load_ucode_amd_early(unsigned int cpuid_1_eax)
{
	return apply_ucode_from_containers(cpuid_1_eax);
}

static enum ucode_state load_microcode_amd(u8 family, const u8 *data, size_t size);

int __init save_microcode_in_initrd_amd(unsigned int cpuid_1_eax)
{
	struct cont_desc desc = { 0 };
	enum ucode_state ret;
	struct cpio_data cp;

	cp = find_microcode_in_initrd(ucode_path, false);
	if (!(cp.data && cp.size))
		return -EINVAL;

	desc.cpuid_1_eax = cpuid_1_eax;

	scan_containers(cp.data, cp.size, &desc);
	if (!desc.mc)
		return -EINVAL;

	ret = load_microcode_amd(x86_family(cpuid_1_eax), desc.data, desc.size);
	if (ret > UCODE_UPDATED)
		return -EINVAL;

	return 0;
}

/*
 * a small, trivial cache of per-family ucode patches
 */
static struct ucode_patch *cache_find_patch(u16 equiv_cpu)
{
	struct ucode_patch *p;

	list_for_each_entry(p, &microcode_cache, plist)
		if (p->equiv_cpu == equiv_cpu)
			return p;
	return NULL;
}

static void update_cache(struct ucode_patch *new_patch)
{
	struct ucode_patch *p;

	list_for_each_entry(p, &microcode_cache, plist) {
		if (p->equiv_cpu == new_patch->equiv_cpu) {
			if (p->patch_id >= new_patch->patch_id) {
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
	u16 equiv_id;


	equiv_id = find_equiv_id(&equiv_table, uci->cpu_sig.sig);
	if (!equiv_id)
		return NULL;

	return cache_find_patch(equiv_id);
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

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	if (rev < mc->hdr.patch_id) {
		if (!__apply_microcode_amd(mc)) {
			ucode_new_rev = mc->hdr.patch_id;
			pr_info("reload patch_level=0x%08x\n", ucode_new_rev);
		}
	}
}

static int collect_cpu_info_amd(int cpu, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct ucode_patch *p;

	csig->sig = cpuid_eax(0x00000001);
	csig->rev = c->microcode;

	/*
	 * a patch could have been loaded early, set uci->mc so that
	 * mc_bp_resume() can call apply_microcode()
	 */
	p = find_patch(cpu);
	if (p && (p->patch_id == csig->rev))
		uci->mc = p->data;

	pr_info("CPU%d: patch_level=0x%08x\n", cpu, csig->rev);

	return 0;
}

static enum ucode_state apply_microcode_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct microcode_amd *mc_amd;
	struct ucode_cpu_info *uci;
	struct ucode_patch *p;
	enum ucode_state ret;
	u32 rev, dummy __always_unused;

	BUG_ON(raw_smp_processor_id() != cpu);

	uci = ucode_cpu_info + cpu;

	p = find_patch(cpu);
	if (!p)
		return UCODE_NFOUND;

	mc_amd  = p->data;
	uci->mc = p->data;

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	/* need to apply patch? */
	if (rev > mc_amd->hdr.patch_id) {
		ret = UCODE_OK;
		goto out;
	}

	if (__apply_microcode_amd(mc_amd)) {
		pr_err("CPU%d: update failed for patch_level=0x%08x\n",
			cpu, mc_amd->hdr.patch_id);
		return UCODE_ERROR;
	}

	rev = mc_amd->hdr.patch_id;
	ret = UCODE_UPDATED;

	pr_info("CPU%d: new patch_level=0x%08x\n", cpu, rev);

out:
	uci->cpu_sig.rev = rev;
	c->microcode	 = rev;

	/* Update boot_cpu_data's revision too, if we're on the BSP: */
	if (c->cpu_index == boot_cpu_data.cpu_index)
		boot_cpu_data.microcode = rev;

	return ret;
}

static size_t install_equiv_cpu_table(const u8 *buf, size_t buf_size)
{
	u32 equiv_tbl_len;
	const u32 *hdr;

	if (!verify_equivalence_table(buf, buf_size, false))
		return 0;

	hdr = (const u32 *)buf;
	equiv_tbl_len = hdr[2];

	equiv_table.entry = vmalloc(equiv_tbl_len);
	if (!equiv_table.entry) {
		pr_err("failed to allocate equivalent CPU table\n");
		return 0;
	}

	memcpy(equiv_table.entry, buf + CONTAINER_HDR_SZ, equiv_tbl_len);
	equiv_table.num_entries = equiv_tbl_len / sizeof(struct equiv_cpu_entry);

	/* add header length */
	return equiv_tbl_len + CONTAINER_HDR_SZ;
}

static void free_equiv_cpu_table(void)
{
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

	ret = verify_patch(family, fw, leftover, patch_size, false);
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

	pr_debug("%s: Added patch_id: 0x%08x, proc_id: 0x%04x\n",
		 __func__, patch->patch_id, proc_id);

	/* ... and add to cache. */
	update_cache(patch);

	return 0;
}

/* Scan the blob in @data and add microcode patches to the cache. */
static enum ucode_state __load_microcode_amd(u8 family, const u8 *data,
					     size_t size)
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

static enum ucode_state load_microcode_amd(u8 family, const u8 *data, size_t size)
{
	struct cpuinfo_x86 *c;
	unsigned int nid, cpu;
	struct ucode_patch *p;
	enum ucode_state ret;

	/* free old equiv table */
	free_equiv_cpu_table();

	ret = __load_microcode_amd(family, data, size);
	if (ret != UCODE_OK) {
		cleanup();
		return ret;
	}

	for_each_node(nid) {
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

	if (c->x86 >= 0x15)
		snprintf(fw_name, sizeof(fw_name), "amd-ucode/microcode_amd_fam%.2xh.bin", c->x86);

	if (request_firmware_direct(&fw, (const char *)fw_name, device)) {
		pr_debug("failed to load file %s\n", fw_name);
		goto out;
	}

	ret = UCODE_ERROR;
	if (!verify_container(fw->data, fw->size, false))
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

static struct microcode_ops microcode_amd_ops = {
	.request_microcode_fw             = request_microcode_amd,
	.collect_cpu_info                 = collect_cpu_info_amd,
	.apply_microcode                  = apply_microcode_amd,
	.microcode_fini_cpu               = microcode_fini_cpu_amd,
};

struct microcode_ops * __init init_amd_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		pr_warn("AMD CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	if (ucode_new_rev)
		pr_info_once("microcode updated early to new patch_level=0x%08x\n",
			     ucode_new_rev);

	return &microcode_amd_ops;
}

void __exit exit_amd_microcode(void)
{
	cleanup();
}
