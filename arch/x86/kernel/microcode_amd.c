/*
 *  AMD CPU Microcode Update Driver for Linux
 *  Copyright (C) 2008 Advanced Micro Devices Inc.
 *
 *  Author: Peter Oruba <peter.oruba@amd.com>
 *
 *  Based on work by:
 *  Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *
 *  This driver allows to upgrade microcode on AMD
 *  family 0x10 and 0x11 processors.
 *
 *  Licensed under the terms of the GNU General Public
 *  License version 2. See file COPYING for details.
 */
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

#define UCODE_MAX_SIZE			2048
#define UCODE_CONTAINER_SECTION_HDR	8
#define UCODE_CONTAINER_HEADER_SIZE	12

static struct equiv_cpu_entry *equiv_cpu_table;

static int collect_cpu_info_amd(int cpu, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	u32 dummy;

	memset(csig, 0, sizeof(*csig));
	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		printk(KERN_WARNING "microcode: CPU%d: AMD CPU family 0x%x not "
		       "supported\n", cpu, c->x86);
		return -1;
	}
	rdmsr(MSR_AMD64_PATCH_LEVEL, csig->rev, dummy);
	printk(KERN_INFO "microcode: CPU%d: patch_level=0x%x\n", cpu, csig->rev);
	return 0;
}

static int get_matching_microcode(int cpu, void *mc, int rev)
{
	struct microcode_header_amd *mc_header = mc;
	unsigned int current_cpu_id;
	u16 equiv_cpu_id = 0;
	unsigned int i = 0;

	BUG_ON(equiv_cpu_table == NULL);
	current_cpu_id = cpuid_eax(0x00000001);

	while (equiv_cpu_table[i].installed_cpu != 0) {
		if (current_cpu_id == equiv_cpu_table[i].installed_cpu) {
			equiv_cpu_id = equiv_cpu_table[i].equiv_cpu;
			break;
		}
		i++;
	}

	if (!equiv_cpu_id) {
		printk(KERN_WARNING "microcode: CPU%d: cpu revision "
		       "not listed in equivalent cpu table\n", cpu);
		return 0;
	}

	if (mc_header->processor_rev_id != equiv_cpu_id) {
		printk(KERN_ERR	"microcode: CPU%d: patch mismatch "
		       "(processor_rev_id: %x, equiv_cpu_id: %x)\n",
		       cpu, mc_header->processor_rev_id, equiv_cpu_id);
		return 0;
	}

	/* ucode might be chipset specific -- currently we don't support this */
	if (mc_header->nb_dev_id || mc_header->sb_dev_id) {
		printk(KERN_ERR "microcode: CPU%d: loading of chipset "
		       "specific code not yet supported\n", cpu);
		return 0;
	}

	if (mc_header->patch_id <= rev)
		return 0;

	return 1;
}

static int apply_microcode_amd(int cpu)
{
	u32 rev, dummy;
	int cpu_num = raw_smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;
	struct microcode_amd *mc_amd = uci->mc;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (mc_amd == NULL)
		return 0;

	wrmsrl(MSR_AMD64_PATCH_LOADER, (u64)(long)&mc_amd->hdr.data_code);
	/* get patch id after patching */
	rdmsr(MSR_AMD64_PATCH_LEVEL, rev, dummy);

	/* check current patch id and patch's id for match */
	if (rev != mc_amd->hdr.patch_id) {
		printk(KERN_ERR "microcode: CPU%d: update failed "
		       "(for patch_level=0x%x)\n", cpu, mc_amd->hdr.patch_id);
		return -1;
	}

	printk(KERN_INFO "microcode: CPU%d: updated (new patch_level=0x%x)\n",
	       cpu, rev);

	uci->cpu_sig.rev = rev;

	return 0;
}

static int get_ucode_data(void *to, const u8 *from, size_t n)
{
	memcpy(to, from, n);
	return 0;
}

static void *
get_next_ucode(const u8 *buf, unsigned int size, unsigned int *mc_size)
{
	unsigned int total_size;
	u8 section_hdr[UCODE_CONTAINER_SECTION_HDR];
	void *mc;

	if (get_ucode_data(section_hdr, buf, UCODE_CONTAINER_SECTION_HDR))
		return NULL;

	if (section_hdr[0] != UCODE_UCODE_TYPE) {
		printk(KERN_ERR "microcode: error: invalid type field in "
		       "container file section header\n");
		return NULL;
	}

	total_size = (unsigned long) (section_hdr[4] + (section_hdr[5] << 8));

	printk(KERN_DEBUG "microcode: size %u, total_size %u\n",
	       size, total_size);

	if (total_size > size || total_size > UCODE_MAX_SIZE) {
		printk(KERN_ERR "microcode: error: size mismatch\n");
		return NULL;
	}

	mc = vmalloc(UCODE_MAX_SIZE);
	if (mc) {
		memset(mc, 0, UCODE_MAX_SIZE);
		if (get_ucode_data(mc, buf + UCODE_CONTAINER_SECTION_HDR,
				   total_size)) {
			vfree(mc);
			mc = NULL;
		} else
			*mc_size = total_size + UCODE_CONTAINER_SECTION_HDR;
	}
	return mc;
}

static int install_equiv_cpu_table(const u8 *buf)
{
	u8 *container_hdr[UCODE_CONTAINER_HEADER_SIZE];
	unsigned int *buf_pos = (unsigned int *)container_hdr;
	unsigned long size;

	if (get_ucode_data(&container_hdr, buf, UCODE_CONTAINER_HEADER_SIZE))
		return 0;

	size = buf_pos[2];

	if (buf_pos[1] != UCODE_EQUIV_CPU_TABLE_TYPE || !size) {
		printk(KERN_ERR "microcode: error: invalid type field in "
		       "container file section header\n");
		return 0;
	}

	equiv_cpu_table = (struct equiv_cpu_entry *) vmalloc(size);
	if (!equiv_cpu_table) {
		printk(KERN_ERR "microcode: failed to allocate "
		       "equivalent CPU table\n");
		return 0;
	}

	buf += UCODE_CONTAINER_HEADER_SIZE;
	if (get_ucode_data(equiv_cpu_table, buf, size)) {
		vfree(equiv_cpu_table);
		return 0;
	}

	return size + UCODE_CONTAINER_HEADER_SIZE; /* add header length */
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
	const u8 *ucode_ptr = data;
	void *new_mc = NULL;
	void *mc;
	int new_rev = uci->cpu_sig.rev;
	unsigned int leftover;
	unsigned long offset;
	enum ucode_state state = UCODE_OK;

	offset = install_equiv_cpu_table(ucode_ptr);
	if (!offset) {
		printk(KERN_ERR "microcode: failed to create "
		       "equivalent cpu table\n");
		return UCODE_ERROR;
	}

	ucode_ptr += offset;
	leftover = size - offset;

	while (leftover) {
		unsigned int uninitialized_var(mc_size);
		struct microcode_header_amd *mc_header;

		mc = get_next_ucode(ucode_ptr, leftover, &mc_size);
		if (!mc)
			break;

		mc_header = (struct microcode_header_amd *)mc;
		if (get_matching_microcode(cpu, mc, new_rev)) {
			vfree(new_mc);
			new_rev = mc_header->patch_id;
			new_mc  = mc;
		} else
			vfree(mc);

		ucode_ptr += mc_size;
		leftover  -= mc_size;
	}

	if (new_mc) {
		if (!leftover) {
			vfree(uci->mc);
			uci->mc = new_mc;
			pr_debug("microcode: CPU%d found a matching microcode "
				 "update with version 0x%x (current=0x%x)\n",
				 cpu, new_rev, uci->cpu_sig.rev);
		} else {
			vfree(new_mc);
			state = UCODE_ERROR;
		}
	} else
		state = UCODE_NFOUND;

	free_equiv_cpu_table();

	return state;
}

static enum ucode_state request_microcode_fw(int cpu, struct device *device)
{
	const char *fw_name = "amd-ucode/microcode_amd.bin";
	const struct firmware *firmware;
	enum ucode_state ret;

	if (request_firmware(&firmware, fw_name, device)) {
		printk(KERN_ERR "microcode: failed to load file %s\n", fw_name);
		return UCODE_NFOUND;
	}

	if (*(u32 *)firmware->data != UCODE_MAGIC) {
		printk(KERN_ERR "microcode: invalid UCODE_MAGIC (0x%08x)\n",
		       *(u32 *)firmware->data);
		return UCODE_ERROR;
	}

	ret = generic_load_microcode(cpu, firmware->data, firmware->size);

	release_firmware(firmware);

	return ret;
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	printk(KERN_INFO "microcode: AMD microcode update via "
	       "/dev/cpu/microcode not supported\n");
	return UCODE_ERROR;
}

static void microcode_fini_cpu_amd(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	vfree(uci->mc);
	uci->mc = NULL;
}

static struct microcode_ops microcode_amd_ops = {
	.request_microcode_user           = request_microcode_user,
	.request_microcode_fw             = request_microcode_fw,
	.collect_cpu_info                 = collect_cpu_info_amd,
	.apply_microcode                  = apply_microcode_amd,
	.microcode_fini_cpu               = microcode_fini_cpu_amd,
};

struct microcode_ops * __init init_amd_microcode(void)
{
	return &microcode_amd_ops;
}
