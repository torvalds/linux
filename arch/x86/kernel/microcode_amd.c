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
 *  Andreas Herrmann <andreas.herrmann3@amd.com>
 *  Borislav Petkov <borislav.petkov@amd.com>
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

MODULE_DESCRIPTION("AMD Microcode Update Driver");
MODULE_AUTHOR("Peter Oruba");
MODULE_LICENSE("GPL v2");

#define UCODE_MAGIC                0x00414d44
#define UCODE_EQUIV_CPU_TABLE_TYPE 0x00000000
#define UCODE_UCODE_TYPE           0x00000001

struct equiv_cpu_entry {
	u32	installed_cpu;
	u32	fixed_errata_mask;
	u32	fixed_errata_compare;
	u16	equiv_cpu;
	u16	res;
} __attribute__((packed));

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
} __attribute__((packed));

struct microcode_amd {
	struct microcode_header_amd	hdr;
	unsigned int			mpb[0];
};

#define SECTION_HDR_SIZE	8
#define CONTAINER_HDR_SZ	12

static struct equiv_cpu_entry *equiv_cpu_table;

/* page-sized ucode patch buffer */
void *patch;

static int collect_cpu_info_amd(int cpu, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	csig->rev = c->microcode;
	pr_info("CPU%d: patch_level=0x%08x\n", cpu, csig->rev);

	return 0;
}

static unsigned int verify_ucode_size(int cpu, u32 patch_size,
				      unsigned int size)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	u32 max_size;

#define F1XH_MPB_MAX_SIZE 2048
#define F14H_MPB_MAX_SIZE 1824
#define F15H_MPB_MAX_SIZE 4096

	switch (c->x86) {
	case 0x14:
		max_size = F14H_MPB_MAX_SIZE;
		break;
	case 0x15:
		max_size = F15H_MPB_MAX_SIZE;
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

static u16 find_equiv_id(void)
{
	unsigned int current_cpu_id, i = 0;

	BUG_ON(equiv_cpu_table == NULL);

	current_cpu_id = cpuid_eax(0x00000001);

	while (equiv_cpu_table[i].installed_cpu != 0) {
		if (current_cpu_id == equiv_cpu_table[i].installed_cpu)
			return equiv_cpu_table[i].equiv_cpu;

		i++;
	}
	return 0;
}

/*
 * we signal a good patch is found by returning its size > 0
 */
static int get_matching_microcode(int cpu, const u8 *ucode_ptr,
				  unsigned int leftover_size, int rev,
				  unsigned int *current_size)
{
	struct microcode_header_amd *mc_hdr;
	unsigned int actual_size, patch_size;
	u16 equiv_cpu_id;

	/* size of the current patch we're staring at */
	patch_size = *(u32 *)(ucode_ptr + 4);
	*current_size = patch_size + SECTION_HDR_SIZE;

	equiv_cpu_id = find_equiv_id();
	if (!equiv_cpu_id)
		return 0;

	/*
	 * let's look at the patch header itself now
	 */
	mc_hdr = (struct microcode_header_amd *)(ucode_ptr + SECTION_HDR_SIZE);

	if (mc_hdr->processor_rev_id != equiv_cpu_id)
		return 0;

	/* ucode might be chipset specific -- currently we don't support this */
	if (mc_hdr->nb_dev_id || mc_hdr->sb_dev_id) {
		pr_err("CPU%d: chipset specific code not yet supported\n",
		       cpu);
		return 0;
	}

	if (mc_hdr->patch_id <= rev)
		return 0;

	/*
	 * now that the header looks sane, verify its size
	 */
	actual_size = verify_ucode_size(cpu, patch_size, leftover_size);
	if (!actual_size)
		return 0;

	/* clear the patch buffer */
	memset(patch, 0, PAGE_SIZE);

	/* all looks ok, get the binary patch */
	get_ucode_data(patch, ucode_ptr + SECTION_HDR_SIZE, actual_size);

	return actual_size;
}

static int apply_microcode_amd(int cpu)
{
	u32 rev, dummy;
	int cpu_num = raw_smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;
	struct microcode_amd *mc_amd = uci->mc;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (mc_amd == NULL)
		return 0;

	wrmsrl(MSR_AMD64_PATCH_LOADER, (u64)(long)&mc_amd->hdr.data_code);
	/* get patch id after patching */
	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	/* check current patch id and patch's id for match */
	if (rev != mc_amd->hdr.patch_id) {
		pr_err("CPU%d: update failed for patch_level=0x%08x\n",
		       cpu, mc_amd->hdr.patch_id);
		return -1;
	}

	pr_info("CPU%d: new patch_level=0x%08x\n", cpu, rev);
	uci->cpu_sig.rev = rev;
	c->microcode = rev;

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

	get_ucode_data(equiv_cpu_table, buf + CONTAINER_HDR_SZ, size);

	/* add header length */
	return size + CONTAINER_HDR_SZ;
}

static void free_equiv_cpu_table(void)
{
	vfree(equiv_cpu_table);
	equiv_cpu_table = NULL;
}

static enum ucode_state
generic_load_microcode(int cpu, const u8 *data, size_t size)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct microcode_header_amd *mc_hdr = NULL;
	unsigned int mc_size, leftover, current_size = 0;
	int offset;
	const u8 *ucode_ptr = data;
	void *new_mc = NULL;
	unsigned int new_rev = uci->cpu_sig.rev;
	enum ucode_state state = UCODE_ERROR;

	offset = install_equiv_cpu_table(ucode_ptr);
	if (offset < 0) {
		pr_err("failed to create equivalent cpu table\n");
		goto out;
	}
	ucode_ptr += offset;
	leftover = size - offset;

	if (*(u32 *)ucode_ptr != UCODE_UCODE_TYPE) {
		pr_err("invalid type field in container file section header\n");
		goto free_table;
	}

	while (leftover) {
		mc_size = get_matching_microcode(cpu, ucode_ptr, leftover,
						 new_rev, &current_size);
		if (mc_size) {
			mc_hdr  = patch;
			new_mc  = patch;
			new_rev = mc_hdr->patch_id;
			goto out_ok;
		}

		ucode_ptr += current_size;
		leftover  -= current_size;
	}

	if (!new_mc) {
		state = UCODE_NFOUND;
		goto free_table;
	}

out_ok:
	uci->mc = new_mc;
	state = UCODE_OK;
	pr_debug("CPU%d update ucode (0x%08x -> 0x%08x)\n",
		 cpu, uci->cpu_sig.rev, new_rev);

free_table:
	free_equiv_cpu_table();

out:
	return state;
}

/*
 * AMD microcode firmware naming convention, up to family 15h they are in
 * the legacy file:
 *
 *    amd-ucode/microcode_amd.bin
 *
 * This legacy file is always smaller than 2K in size.
 *
 * Starting at family 15h they are in family specific firmware files:
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
	const struct firmware *fw;
	enum ucode_state ret = UCODE_NFOUND;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	if (c->x86 >= 0x15)
		snprintf(fw_name, sizeof(fw_name), "amd-ucode/microcode_amd_fam%.2xh.bin", c->x86);

	if (request_firmware(&fw, (const char *)fw_name, device)) {
		pr_err("failed to load file %s\n", fw_name);
		goto out;
	}

	ret = UCODE_ERROR;
	if (*(u32 *)fw->data != UCODE_MAGIC) {
		pr_err("invalid magic value (0x%08x)\n", *(u32 *)fw->data);
		goto fw_release;
	}

	ret = generic_load_microcode(cpu, fw->data, fw->size);

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

	patch = (void *)get_zeroed_page(GFP_KERNEL);
	if (!patch)
		return NULL;

	return &microcode_amd_ops;
}

void __exit exit_amd_microcode(void)
{
	free_page((unsigned long)patch);
}
