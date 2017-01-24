/*
 *  AMD CPU Microcode Update Driver for Linux
 *
 *  This driver allows to upgrade microcode on F10h AMD
 *  CPUs and later.
 *
 *  Copyright (C) 2008-2011 Advanced Micro Devices Inc.
 *	          2013-2016 Borislav Petkov <bp@alien8.de>
 *
 *  Author: Peter Oruba <peter.oruba@amd.com>
 *
 *  Based on work by:
 *  Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *
 *  early loader:
 *  Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 *  Author: Jacob Shin <jacob.shin@amd.com>
 *  Fixes: Borislav Petkov <bp@suse.de>
 *
 *  Licensed under the terms of the GNU General Public
 *  License version 2. See file COPYING for details.
 */
#define pr_fmt(fmt) "microcode: " fmt

#include <linux/earlycpio.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <asm/microcode_amd.h>
#include <asm/microcode.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/cpu.h>
#include <asm/msr.h>

static struct equiv_cpu_entry *equiv_cpu_table;

/*
 * This points to the current valid container of microcode patches which we will
 * save from the initrd/builtin before jettisoning its contents.
 */
struct container {
	u8 *data;
	size_t size;
} cont;

static u32 ucode_new_rev;
static u8 amd_ucode_patch[PATCH_MAX_SIZE];
static u16 this_equiv_id;

/*
 * Microcode patch container file is prepended to the initrd in cpio
 * format. See Documentation/x86/early-microcode.txt
 */
static const char
ucode_path[] __maybe_unused = "kernel/x86/microcode/AuthenticAMD.bin";

static size_t compute_container_size(u8 *data, u32 total_size)
{
	size_t size = 0;
	u32 *header = (u32 *)data;

	if (header[0] != UCODE_MAGIC ||
	    header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return size;

	size = header[2] + CONTAINER_HDR_SZ;
	total_size -= size;
	data += size;

	while (total_size) {
		u16 patch_size;

		header = (u32 *)data;

		if (header[0] != UCODE_UCODE_TYPE)
			break;

		/*
		 * Sanity-check patch size.
		 */
		patch_size = header[1];
		if (patch_size > PATCH_MAX_SIZE)
			break;

		size	   += patch_size + SECTION_HDR_SIZE;
		data	   += patch_size + SECTION_HDR_SIZE;
		total_size -= patch_size + SECTION_HDR_SIZE;
	}

	return size;
}

static inline u16 find_equiv_id(struct equiv_cpu_entry *equiv_cpu_table,
				unsigned int sig)
{
	int i = 0;

	if (!equiv_cpu_table)
		return 0;

	while (equiv_cpu_table[i].installed_cpu != 0) {
		if (sig == equiv_cpu_table[i].installed_cpu)
			return equiv_cpu_table[i].equiv_cpu;

		i++;
	}
	return 0;
}

/*
 * This scans the ucode blob for the proper container as we can have multiple
 * containers glued together. Returns the equivalence ID from the equivalence
 * table or 0 if none found.
 */
static u16
find_proper_container(u8 *ucode, size_t size, struct container *ret_cont)
{
	struct container ret = { NULL, 0 };
	u32 eax, ebx, ecx, edx;
	struct equiv_cpu_entry *eq;
	int offset, left;
	u16 eq_id = 0;
	u32 *header;
	u8 *data;

	data   = ucode;
	left   = size;
	header = (u32 *)data;


	/* find equiv cpu table */
	if (header[0] != UCODE_MAGIC ||
	    header[1] != UCODE_EQUIV_CPU_TABLE_TYPE || /* type */
	    header[2] == 0)                            /* size */
		return eq_id;

	eax = 0x00000001;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	while (left > 0) {
		eq = (struct equiv_cpu_entry *)(data + CONTAINER_HDR_SZ);

		ret.data = data;

		/* Advance past the container header */
		offset = header[2] + CONTAINER_HDR_SZ;
		data  += offset;
		left  -= offset;

		eq_id = find_equiv_id(eq, eax);
		if (eq_id) {
			ret.size = compute_container_size(ret.data, left + offset);

			/*
			 * truncate how much we need to iterate over in the
			 * ucode update loop below
			 */
			left = ret.size - offset;

			*ret_cont = ret;
			return eq_id;
		}

		/*
		 * support multiple container files appended together. if this
		 * one does not have a matching equivalent cpu entry, we fast
		 * forward to the next container file.
		 */
		while (left > 0) {
			header = (u32 *)data;

			if (header[0] == UCODE_MAGIC &&
			    header[1] == UCODE_EQUIV_CPU_TABLE_TYPE)
				break;

			offset = header[1] + SECTION_HDR_SIZE;
			data  += offset;
			left  -= offset;
		}

		/* mark where the next microcode container file starts */
		offset    = data - (u8 *)ucode;
		ucode     = data;
	}

	return eq_id;
}

static int __apply_microcode_amd(struct microcode_amd *mc_amd)
{
	u32 rev, dummy;

	native_wrmsrl(MSR_AMD64_PATCH_LOADER, (u64)(long)&mc_amd->hdr.data_code);

	/* verify patch application was successful */
	native_rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);
	if (rev != mc_amd->hdr.patch_id)
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
 * Returns true if container found (sets @ret_cont), false otherwise.
 */
static bool apply_microcode_early_amd(void *ucode, size_t size, bool save_patch,
				      struct container *ret_cont)
{
	u8 (*patch)[PATCH_MAX_SIZE];
	u32 rev, *header, *new_rev;
	struct container ret;
	int offset, left;
	u16 eq_id = 0;
	u8  *data;

#ifdef CONFIG_X86_32
	new_rev = (u32 *)__pa_nodebug(&ucode_new_rev);
	patch	= (u8 (*)[PATCH_MAX_SIZE])__pa_nodebug(&amd_ucode_patch);
#else
	new_rev = &ucode_new_rev;
	patch	= &amd_ucode_patch;
#endif

	if (check_current_patch_level(&rev, true))
		return false;

	eq_id = find_proper_container(ucode, size, &ret);
	if (!eq_id)
		return false;

	this_equiv_id = eq_id;
	header = (u32 *)ret.data;

	/* We're pointing to an equiv table, skip over it. */
	data = ret.data +  header[2] + CONTAINER_HDR_SZ;
	left = ret.size - (header[2] + CONTAINER_HDR_SZ);

	while (left > 0) {
		struct microcode_amd *mc;

		header = (u32 *)data;
		if (header[0] != UCODE_UCODE_TYPE || /* type */
		    header[1] == 0)                  /* size */
			break;

		mc = (struct microcode_amd *)(data + SECTION_HDR_SIZE);

		if (eq_id == mc->hdr.processor_rev_id && rev < mc->hdr.patch_id) {

			if (!__apply_microcode_amd(mc)) {
				rev = mc->hdr.patch_id;
				*new_rev = rev;

				if (save_patch)
					memcpy(patch, mc, min_t(u32, header[1], PATCH_MAX_SIZE));
			}
		}

		offset  = header[1] + SECTION_HDR_SIZE;
		data   += offset;
		left   -= offset;
	}

	if (ret_cont)
		*ret_cont = ret;

	return true;
}

static bool get_builtin_microcode(struct cpio_data *cp, unsigned int family)
{
#ifdef CONFIG_X86_64
	char fw_name[36] = "amd-ucode/microcode_amd.bin";

	if (family >= 0x15)
		snprintf(fw_name, sizeof(fw_name),
			 "amd-ucode/microcode_amd_fam%.2xh.bin", family);

	return get_builtin_firmware(cp, fw_name);
#else
	return false;
#endif
}

void __init load_ucode_amd_bsp(unsigned int family)
{
	struct ucode_cpu_info *uci;
	u32 eax, ebx, ecx, edx;
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

	if (!get_builtin_microcode(&cp, family))
		cp = find_microcode_in_initrd(path, use_pa);

	if (!(cp.data && cp.size))
		return;

	/* Get BSP's CPUID.EAX(1), needed in load_microcode_amd() */
	eax = 1;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	uci->cpu_sig.sig = eax;

	apply_microcode_early_amd(cp.data, cp.size, true, NULL);
}

#ifdef CONFIG_X86_32
/*
 * On 32-bit, since AP's early load occurs before paging is turned on, we
 * cannot traverse cpu_equiv_table and microcode_cache in kernel heap memory.
 * So during cold boot, AP will apply_ucode_in_initrd() just like the BSP.
 * In save_microcode_in_initrd_amd() BSP's patch is copied to amd_ucode_patch,
 * which is used upon resume from suspend.
 */
void load_ucode_amd_ap(unsigned int family)
{
	struct microcode_amd *mc;
	struct cpio_data cp;

	mc = (struct microcode_amd *)__pa_nodebug(amd_ucode_patch);
	if (mc->hdr.patch_id && mc->hdr.processor_rev_id) {
		__apply_microcode_amd(mc);
		return;
	}

	if (!get_builtin_microcode(&cp, family))
		cp = find_microcode_in_initrd((const char *)__pa_nodebug(ucode_path), true);

	if (!(cp.data && cp.size))
		return;

	/*
	 * This would set amd_ucode_patch above so that the following APs can
	 * use it directly instead of going down this path again.
	 */
	apply_microcode_early_amd(cp.data, cp.size, true, NULL);
}
#else
void load_ucode_amd_ap(unsigned int family)
{
	struct equiv_cpu_entry *eq;
	struct microcode_amd *mc;
	u32 rev, eax;
	u16 eq_id;

	/* 64-bit runs with paging enabled, thus early==false. */
	if (check_current_patch_level(&rev, false))
		return;

	/* First AP hasn't cached it yet, go through the blob. */
	if (!cont.data) {
		struct cpio_data cp = { NULL, 0, "" };

		if (cont.size == -1)
			return;

reget:
		if (!get_builtin_microcode(&cp, family)) {
#ifdef CONFIG_BLK_DEV_INITRD
			cp = find_cpio_data(ucode_path, (void *)initrd_start,
					    initrd_end - initrd_start, NULL);
#endif
			if (!(cp.data && cp.size)) {
				/*
				 * Mark it so that other APs do not scan again
				 * for no real reason and slow down boot
				 * needlessly.
				 */
				cont.size = -1;
				return;
			}
		}

		if (!apply_microcode_early_amd(cp.data, cp.size, false, &cont)) {
			cont.size = -1;
			return;
		}
	}

	eax = cpuid_eax(0x00000001);
	eq  = (struct equiv_cpu_entry *)(cont.data + CONTAINER_HDR_SZ);

	eq_id = find_equiv_id(eq, eax);
	if (!eq_id)
		return;

	if (eq_id == this_equiv_id) {
		mc = (struct microcode_amd *)amd_ucode_patch;

		if (mc && rev < mc->hdr.patch_id) {
			if (!__apply_microcode_amd(mc))
				ucode_new_rev = mc->hdr.patch_id;
		}

	} else {

		/*
		 * AP has a different equivalence ID than BSP, looks like
		 * mixed-steppings silicon so go through the ucode blob anew.
		 */
		goto reget;
	}
}
#endif /* CONFIG_X86_32 */

static enum ucode_state
load_microcode_amd(int cpu, u8 family, const u8 *data, size_t size);

int __init save_microcode_in_initrd_amd(unsigned int fam)
{
	enum ucode_state ret;
	int retval = 0;
	u16 eq_id;

	if (!cont.data) {
		if (IS_ENABLED(CONFIG_X86_32) && (cont.size != -1)) {
			struct cpio_data cp = { NULL, 0, "" };

#ifdef CONFIG_BLK_DEV_INITRD
			cp = find_cpio_data(ucode_path, (void *)initrd_start,
					    initrd_end - initrd_start, NULL);
#endif

			if (!(cp.data && cp.size)) {
				cont.size = -1;
				return -EINVAL;
			}

			eq_id = find_proper_container(cp.data, cp.size, &cont);
			if (!eq_id) {
				cont.size = -1;
				return -EINVAL;
			}

		} else
			return -EINVAL;
	}

	ret = load_microcode_amd(smp_processor_id(), fam, cont.data, cont.size);
	if (ret != UCODE_OK)
		retval = -EINVAL;

	/*
	 * This will be freed any msec now, stash patches for the current
	 * family and switch to patch cache for cpu hotplug, etc later.
	 */
	cont.data = NULL;
	cont.size = 0;

	return retval;
}

void reload_ucode_amd(void)
{
	struct microcode_amd *mc;
	u32 rev;

	/*
	 * early==false because this is a syscore ->resume path and by
	 * that time paging is long enabled.
	 */
	if (check_current_patch_level(&rev, false))
		return;

	mc = (struct microcode_amd *)amd_ucode_patch;
	if (!mc)
		return;

	if (rev < mc->hdr.patch_id) {
		if (!__apply_microcode_amd(mc)) {
			ucode_new_rev = mc->hdr.patch_id;
			pr_info("reload patch_level=0x%08x\n", ucode_new_rev);
		}
	}
}
static u16 __find_equiv_id(unsigned int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	return find_equiv_id(equiv_cpu_table, uci->cpu_sig.sig);
}

static u32 find_cpu_family_by_equiv_cpu(u16 equiv_cpu)
{
	int i = 0;

	BUG_ON(!equiv_cpu_table);

	while (equiv_cpu_table[i].equiv_cpu != 0) {
		if (equiv_cpu == equiv_cpu_table[i].equiv_cpu)
			return equiv_cpu_table[i].installed_cpu;
		i++;
	}
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
			if (p->patch_id >= new_patch->patch_id)
				/* we already have the latest patch */
				return;

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
	u16 equiv_id;

	equiv_id = __find_equiv_id(cpu);
	if (!equiv_id)
		return NULL;

	return cache_find_patch(equiv_id);
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

static unsigned int verify_patch_size(u8 family, u32 patch_size,
				      unsigned int size)
{
	u32 max_size;

#define F1XH_MPB_MAX_SIZE 2048
#define F14H_MPB_MAX_SIZE 1824
#define F15H_MPB_MAX_SIZE 4096
#define F16H_MPB_MAX_SIZE 3458

	switch (family) {
	case 0x14:
		max_size = F14H_MPB_MAX_SIZE;
		break;
	case 0x15:
		max_size = F15H_MPB_MAX_SIZE;
		break;
	case 0x16:
		max_size = F16H_MPB_MAX_SIZE;
		break;
	default:
		max_size = F1XH_MPB_MAX_SIZE;
		break;
	}

	if (patch_size > min_t(u32, size, max_size)) {
		pr_err("patch size mismatch\n");
		return 0;
	}

	return patch_size;
}

/*
 * Those patch levels cannot be updated to newer ones and thus should be final.
 */
static u32 final_levels[] = {
	0x01000098,
	0x0100009f,
	0x010000af,
	0, /* T-101 terminator */
};

/*
 * Check the current patch level on this CPU.
 *
 * @rev: Use it to return the patch level. It is set to 0 in the case of
 * error.
 *
 * Returns:
 *  - true: if update should stop
 *  - false: otherwise
 */
bool check_current_patch_level(u32 *rev, bool early)
{
	u32 lvl, dummy, i;
	bool ret = false;
	u32 *levels;

	native_rdmsr(MSR_AMD64_PATCH_LEVEL, lvl, dummy);

	if (IS_ENABLED(CONFIG_X86_32) && early)
		levels = (u32 *)__pa_nodebug(&final_levels);
	else
		levels = final_levels;

	for (i = 0; levels[i]; i++) {
		if (lvl == levels[i]) {
			lvl = 0;
			ret = true;
			break;
		}
	}

	if (rev)
		*rev = lvl;

	return ret;
}

static int apply_microcode_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct microcode_amd *mc_amd;
	struct ucode_cpu_info *uci;
	struct ucode_patch *p;
	u32 rev;

	BUG_ON(raw_smp_processor_id() != cpu);

	uci = ucode_cpu_info + cpu;

	p = find_patch(cpu);
	if (!p)
		return 0;

	mc_amd  = p->data;
	uci->mc = p->data;

	if (check_current_patch_level(&rev, false))
		return -1;

	/* need to apply patch? */
	if (rev >= mc_amd->hdr.patch_id) {
		c->microcode = rev;
		uci->cpu_sig.rev = rev;
		return 0;
	}

	if (__apply_microcode_amd(mc_amd)) {
		pr_err("CPU%d: update failed for patch_level=0x%08x\n",
			cpu, mc_amd->hdr.patch_id);
		return -1;
	}
	pr_info("CPU%d: new patch_level=0x%08x\n", cpu,
		mc_amd->hdr.patch_id);

	uci->cpu_sig.rev = mc_amd->hdr.patch_id;
	c->microcode = mc_amd->hdr.patch_id;

	return 0;
}

static int install_equiv_cpu_table(const u8 *buf)
{
	unsigned int *ibuf = (unsigned int *)buf;
	unsigned int type = ibuf[1];
	unsigned int size = ibuf[2];

	if (type != UCODE_EQUIV_CPU_TABLE_TYPE || !size) {
		pr_err("empty section/"
		       "invalid type field in container file section header\n");
		return -EINVAL;
	}

	equiv_cpu_table = vmalloc(size);
	if (!equiv_cpu_table) {
		pr_err("failed to allocate equivalent CPU table\n");
		return -ENOMEM;
	}

	memcpy(equiv_cpu_table, buf + CONTAINER_HDR_SZ, size);

	/* add header length */
	return size + CONTAINER_HDR_SZ;
}

static void free_equiv_cpu_table(void)
{
	vfree(equiv_cpu_table);
	equiv_cpu_table = NULL;
}

static void cleanup(void)
{
	free_equiv_cpu_table();
	free_cache();
}

/*
 * We return the current size even if some of the checks failed so that
 * we can skip over the next patch. If we return a negative value, we
 * signal a grave error like a memory allocation has failed and the
 * driver cannot continue functioning normally. In such cases, we tear
 * down everything we've used up so far and exit.
 */
static int verify_and_add_patch(u8 family, u8 *fw, unsigned int leftover)
{
	struct microcode_header_amd *mc_hdr;
	struct ucode_patch *patch;
	unsigned int patch_size, crnt_size, ret;
	u32 proc_fam;
	u16 proc_id;

	patch_size  = *(u32 *)(fw + 4);
	crnt_size   = patch_size + SECTION_HDR_SIZE;
	mc_hdr	    = (struct microcode_header_amd *)(fw + SECTION_HDR_SIZE);
	proc_id	    = mc_hdr->processor_rev_id;

	proc_fam = find_cpu_family_by_equiv_cpu(proc_id);
	if (!proc_fam) {
		pr_err("No patch family for equiv ID: 0x%04x\n", proc_id);
		return crnt_size;
	}

	/* check if patch is for the current family */
	proc_fam = ((proc_fam >> 8) & 0xf) + ((proc_fam >> 20) & 0xff);
	if (proc_fam != family)
		return crnt_size;

	if (mc_hdr->nb_dev_id || mc_hdr->sb_dev_id) {
		pr_err("Patch-ID 0x%08x: chipset-specific code unsupported.\n",
			mc_hdr->patch_id);
		return crnt_size;
	}

	ret = verify_patch_size(family, patch_size, leftover);
	if (!ret) {
		pr_err("Patch-ID 0x%08x: size mismatch.\n", mc_hdr->patch_id);
		return crnt_size;
	}

	patch = kzalloc(sizeof(*patch), GFP_KERNEL);
	if (!patch) {
		pr_err("Patch allocation failure.\n");
		return -EINVAL;
	}

	patch->data = kmemdup(fw + SECTION_HDR_SIZE, patch_size, GFP_KERNEL);
	if (!patch->data) {
		pr_err("Patch data allocation failure.\n");
		kfree(patch);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&patch->plist);
	patch->patch_id  = mc_hdr->patch_id;
	patch->equiv_cpu = proc_id;

	pr_debug("%s: Added patch_id: 0x%08x, proc_id: 0x%04x\n",
		 __func__, patch->patch_id, proc_id);

	/* ... and add to cache. */
	update_cache(patch);

	return crnt_size;
}

static enum ucode_state __load_microcode_amd(u8 family, const u8 *data,
					     size_t size)
{
	enum ucode_state ret = UCODE_ERROR;
	unsigned int leftover;
	u8 *fw = (u8 *)data;
	int crnt_size = 0;
	int offset;

	offset = install_equiv_cpu_table(data);
	if (offset < 0) {
		pr_err("failed to create equivalent cpu table\n");
		return ret;
	}
	fw += offset;
	leftover = size - offset;

	if (*(u32 *)fw != UCODE_UCODE_TYPE) {
		pr_err("invalid type field in container file section header\n");
		free_equiv_cpu_table();
		return ret;
	}

	while (leftover) {
		crnt_size = verify_and_add_patch(family, fw, leftover);
		if (crnt_size < 0)
			return ret;

		fw	 += crnt_size;
		leftover -= crnt_size;
	}

	return UCODE_OK;
}

static enum ucode_state
load_microcode_amd(int cpu, u8 family, const u8 *data, size_t size)
{
	enum ucode_state ret;

	/* free old equiv table */
	free_equiv_cpu_table();

	ret = __load_microcode_amd(family, data, size);

	if (ret != UCODE_OK)
		cleanup();

#ifdef CONFIG_X86_32
	/* save BSP's matching patch for early load */
	if (cpu_data(cpu).cpu_index == boot_cpu_data.cpu_index) {
		struct ucode_patch *p = find_patch(cpu);
		if (p) {
			memset(amd_ucode_patch, 0, PATCH_MAX_SIZE);
			memcpy(amd_ucode_patch, p->data, min_t(u32, ksize(p->data),
							       PATCH_MAX_SIZE));
		}
	}
#endif
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
static enum ucode_state request_microcode_amd(int cpu, struct device *device,
					      bool refresh_fw)
{
	char fw_name[36] = "amd-ucode/microcode_amd.bin";
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	enum ucode_state ret = UCODE_NFOUND;
	const struct firmware *fw;

	/* reload ucode container only on the boot cpu */
	if (!refresh_fw || c->cpu_index != boot_cpu_data.cpu_index)
		return UCODE_OK;

	if (c->x86 >= 0x15)
		snprintf(fw_name, sizeof(fw_name), "amd-ucode/microcode_amd_fam%.2xh.bin", c->x86);

	if (request_firmware_direct(&fw, (const char *)fw_name, device)) {
		pr_debug("failed to load file %s\n", fw_name);
		goto out;
	}

	ret = UCODE_ERROR;
	if (*(u32 *)fw->data != UCODE_MAGIC) {
		pr_err("invalid magic value (0x%08x)\n", *(u32 *)fw->data);
		goto fw_release;
	}

	ret = load_microcode_amd(cpu, c->x86, fw->data, fw->size);

 fw_release:
	release_firmware(fw);

 out:
	return ret;
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	return UCODE_ERROR;
}

static void microcode_fini_cpu_amd(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	uci->mc = NULL;
}

static struct microcode_ops microcode_amd_ops = {
	.request_microcode_user           = request_microcode_user,
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
