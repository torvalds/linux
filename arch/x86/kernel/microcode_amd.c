/*
 *  AMD CPU Microcode Update Driver for Linux
 *  Copyright (C) 2008-2011 Advanced Micro Devices Inc.
 *
 *  Author: Peter Oruba <peter.oruba@amd.com>
 *
 *  Based on work by:
 *  Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *
 *  Maintainers:
 *  Andreas Herrmann <herrmann.der.user@googlemail.com>
 *  Borislav Petkov <bp@alien8.de>
 *
 *  This driver allows to upgrade microcode on F10h AMD
 *  CPUs and later.
 *
 *  Licensed under the terms of the GNU General Public
 *  License version 2. See file COPYING for details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/firmware.h>
#include <linux/pci_ids.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/microcode.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/microcode_amd.h>

MODULE_DESCRIPTION("AMD Microcode Update Driver");
MODULE_AUTHOR("Peter Oruba");
MODULE_LICENSE("GPL v2");

static struct equiv_cpu_entry *equiv_cpu_table;

struct ucode_patch {
	struct list_head plist;
	void *data;
	u32 patch_id;
	u16 equiv_cpu;
};

static LIST_HEAD(pcache);

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

	list_for_each_entry(p, &pcache, plist)
		if (p->equiv_cpu == equiv_cpu)
			return p;
	return NULL;
}

static void update_cache(struct ucode_patch *new_patch)
{
	struct ucode_patch *p;

	list_for_each_entry(p, &pcache, plist) {
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
	list_add_tail(&new_patch->plist, &pcache);
}

static void free_cache(void)
{
	struct ucode_patch *p, *tmp;

	list_for_each_entry_safe(p, tmp, &pcache, plist) {
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

int __apply_microcode_amd(struct microcode_amd *mc_amd)
{
	u32 rev, dummy;

	wrmsrl(MSR_AMD64_PATCH_LOADER, (u64)(long)&mc_amd->hdr.data_code);

	/* verify patch application was successful */
	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);
	if (rev != mc_amd->hdr.patch_id)
		return -1;

	return 0;
}

int apply_microcode_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct microcode_amd *mc_amd;
	struct ucode_cpu_info *uci;
	struct ucode_patch *p;
	u32 rev, dummy;

	BUG_ON(raw_smp_processor_id() != cpu);

	uci = ucode_cpu_info + cpu;

	p = find_patch(cpu);
	if (!p)
		return 0;

	mc_amd  = p->data;
	uci->mc = p->data;

	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

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

	patch->data = kzalloc(patch_size, GFP_KERNEL);
	if (!patch->data) {
		pr_err("Patch data allocation failure.\n");
		kfree(patch);
		return -EINVAL;
	}

	/* All looks ok, copy patch... */
	memcpy(patch->data, fw + SECTION_HDR_SIZE, patch_size);
	INIT_LIST_HEAD(&patch->plist);
	patch->patch_id  = mc_hdr->patch_id;
	patch->equiv_cpu = proc_id;

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

enum ucode_state load_microcode_amd(u8 family, const u8 *data, size_t size)
{
	enum ucode_state ret;

	/* free old equiv table */
	free_equiv_cpu_table();

	ret = __load_microcode_amd(family, data, size);

	if (ret != UCODE_OK)
		cleanup();

#if defined(CONFIG_MICROCODE_AMD_EARLY) && defined(CONFIG_X86_32)
	/* save BSP's matching patch for early load */
	if (cpu_data(smp_processor_id()).cpu_index == boot_cpu_data.cpu_index) {
		struct ucode_patch *p = find_patch(smp_processor_id());
		if (p) {
			memset(amd_bsp_mpb, 0, MPB_MAX_SIZE);
			memcpy(amd_bsp_mpb, p->data, min_t(u32, ksize(p->data),
							   MPB_MAX_SIZE));
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

	if (request_firmware(&fw, (const char *)fw_name, device)) {
		pr_debug("failed to load file %s\n", fw_name);
		goto out;
	}

	ret = UCODE_ERROR;
	if (*(u32 *)fw->data != UCODE_MAGIC) {
		pr_err("invalid magic value (0x%08x)\n", *(u32 *)fw->data);
		goto fw_release;
	}

	ret = load_microcode_amd(c->x86, fw->data, fw->size);

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
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		pr_warning("AMD CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	return &microcode_amd_ops;
}

void __exit exit_amd_microcode(void)
{
	cleanup();
}
