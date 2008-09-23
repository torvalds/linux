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
 *  Licensed unter the terms of the GNU General Public
 *  License version 2. See file COPYING for details.
*/

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cpu.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <asm/msr.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/microcode.h>

MODULE_DESCRIPTION("AMD Microcode Update Driver");
MODULE_AUTHOR("Peter Oruba <peter.oruba@amd.com>");
MODULE_LICENSE("GPL v2");

#define UCODE_MAGIC                0x00414d44
#define UCODE_EQUIV_CPU_TABLE_TYPE 0x00000000
#define UCODE_UCODE_TYPE           0x00000001

struct equiv_cpu_entry {
	unsigned int installed_cpu;
	unsigned int fixed_errata_mask;
	unsigned int fixed_errata_compare;
	unsigned int equiv_cpu;
};

struct microcode_header_amd {
	unsigned int  data_code;
	unsigned int  patch_id;
	unsigned char mc_patch_data_id[2];
	unsigned char mc_patch_data_len;
	unsigned char init_flag;
	unsigned int  mc_patch_data_checksum;
	unsigned int  nb_dev_id;
	unsigned int  sb_dev_id;
	unsigned char processor_rev_id[2];
	unsigned char nb_rev_id;
	unsigned char sb_rev_id;
	unsigned char bios_api_rev;
	unsigned char reserved1[3];
	unsigned int  match_reg[8];
};

struct microcode_amd {
	struct microcode_header_amd hdr;
	unsigned int mpb[0];
};

#define UCODE_MAX_SIZE          (2048)
#define DEFAULT_UCODE_DATASIZE	(896)
#define MC_HEADER_SIZE		(sizeof(struct microcode_header_amd))
#define DEFAULT_UCODE_TOTALSIZE (DEFAULT_UCODE_DATASIZE + MC_HEADER_SIZE)
#define DWSIZE			(sizeof(u32))
/* For now we support a fixed ucode total size only */
#define get_totalsize(mc) \
	((((struct microcode_amd *)mc)->hdr.mc_patch_data_len * 28) \
	 + MC_HEADER_SIZE)

/* serialize access to the physical write */
static DEFINE_SPINLOCK(microcode_update_lock);

static struct equiv_cpu_entry *equiv_cpu_table;

static int collect_cpu_info_amd(int cpu, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	memset(csig, 0, sizeof(*csig));

	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		printk(KERN_ERR "microcode: CPU%d not a capable AMD processor\n",
		       cpu);
		return -1;
	}

	asm volatile("movl %1, %%ecx; rdmsr"
		     : "=a" (csig->rev)
		     : "i" (0x0000008B) : "ecx");

	printk(KERN_INFO "microcode: collect_cpu_info_amd : patch_id=0x%x\n",
		csig->rev);

	return 0;
}

static int get_matching_microcode(int cpu, void *mc, int rev)
{
	struct microcode_header_amd *mc_header = mc;
	struct pci_dev *nb_pci_dev, *sb_pci_dev;
	unsigned int current_cpu_id;
	unsigned int equiv_cpu_id = 0x00;
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
		printk(KERN_ERR "microcode: CPU%d cpu_id "
		       "not found in equivalent cpu table \n", cpu);
		return 0;
	}

	if ((mc_header->processor_rev_id[0]) != (equiv_cpu_id & 0xff)) {
		printk(KERN_ERR
			"microcode: CPU%d patch does not match "
			"(patch is %x, cpu extended is %x) \n",
			cpu, mc_header->processor_rev_id[0],
			(equiv_cpu_id & 0xff));
		return 0;
	}

	if ((mc_header->processor_rev_id[1]) != ((equiv_cpu_id >> 16) & 0xff)) {
		printk(KERN_ERR "microcode: CPU%d patch does not match "
			"(patch is %x, cpu base id is %x) \n",
			cpu, mc_header->processor_rev_id[1],
			((equiv_cpu_id >> 16) & 0xff));

		return 0;
	}

	/* ucode may be northbridge specific */
	if (mc_header->nb_dev_id) {
		nb_pci_dev = pci_get_device(PCI_VENDOR_ID_AMD,
					    (mc_header->nb_dev_id & 0xff),
					    NULL);
		if ((!nb_pci_dev) ||
		    (mc_header->nb_rev_id != nb_pci_dev->revision)) {
			printk(KERN_ERR "microcode: CPU%d NB mismatch \n", cpu);
			pci_dev_put(nb_pci_dev);
			return 0;
		}
		pci_dev_put(nb_pci_dev);
	}

	/* ucode may be southbridge specific */
	if (mc_header->sb_dev_id) {
		sb_pci_dev = pci_get_device(PCI_VENDOR_ID_AMD,
					    (mc_header->sb_dev_id & 0xff),
					    NULL);
		if ((!sb_pci_dev) ||
		    (mc_header->sb_rev_id != sb_pci_dev->revision)) {
			printk(KERN_ERR "microcode: CPU%d SB mismatch \n", cpu);
			pci_dev_put(sb_pci_dev);
			return 0;
		}
		pci_dev_put(sb_pci_dev);
	}

	if (mc_header->patch_id <= rev)
		return 0;

	return 1;
}

static void apply_microcode_amd(int cpu)
{
	unsigned long flags;
	unsigned int eax, edx;
	unsigned int rev;
	int cpu_num = raw_smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;
	struct microcode_amd *mc_amd = uci->mc;
	unsigned long addr;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (mc_amd == NULL)
		return;

	spin_lock_irqsave(&microcode_update_lock, flags);

	addr = (unsigned long)&mc_amd->hdr.data_code;
	edx = (unsigned int)(((unsigned long)upper_32_bits(addr)));
	eax = (unsigned int)(((unsigned long)lower_32_bits(addr)));

	asm volatile("movl %0, %%ecx; wrmsr" :
		     : "i" (0xc0010020), "a" (eax), "d" (edx) : "ecx");

	/* get patch id after patching */
	asm volatile("movl %1, %%ecx; rdmsr"
		     : "=a" (rev)
		     : "i" (0x0000008B) : "ecx");

	spin_unlock_irqrestore(&microcode_update_lock, flags);

	/* check current patch id and patch's id for match */
	if (rev != mc_amd->hdr.patch_id) {
		printk(KERN_ERR "microcode: CPU%d update from revision "
		       "0x%x to 0x%x failed\n", cpu_num,
		       mc_amd->hdr.patch_id, rev);
		return;
	}

	printk(KERN_INFO "microcode: CPU%d updated from revision "
	       "0x%x to 0x%x \n",
	       cpu_num, uci->cpu_sig.rev, mc_amd->hdr.patch_id);

	uci->cpu_sig.rev = rev;
}

static void * get_next_ucode(u8 *buf, unsigned int size,
			int (*get_ucode_data)(void *, const void *, size_t),
			unsigned int *mc_size)
{
	unsigned int total_size;
#define UCODE_CONTAINER_SECTION_HDR	8
	u8 section_hdr[UCODE_CONTAINER_SECTION_HDR];
	void *mc;

	if (get_ucode_data(section_hdr, buf, UCODE_CONTAINER_SECTION_HDR))
		return NULL;

	if (section_hdr[0] != UCODE_UCODE_TYPE) {
		printk(KERN_ERR "microcode: error! "
		       "Wrong microcode payload type field\n");
		return NULL;
	}

	total_size = (unsigned long) (section_hdr[4] + (section_hdr[5] << 8));

	printk(KERN_INFO "microcode: size %u, total_size %u\n",
		size, total_size);

	if (total_size > size || total_size > UCODE_MAX_SIZE) {
		printk(KERN_ERR "microcode: error! Bad data in microcode data file\n");
		return NULL;
	}

	mc = vmalloc(UCODE_MAX_SIZE);
	if (mc) {
		memset(mc, 0, UCODE_MAX_SIZE);
		if (get_ucode_data(mc, buf + UCODE_CONTAINER_SECTION_HDR, total_size)) {
			vfree(mc);
			mc = NULL;
		} else
			*mc_size = total_size + UCODE_CONTAINER_SECTION_HDR;
	}
#undef UCODE_CONTAINER_SECTION_HDR
	return mc;
}


static int install_equiv_cpu_table(u8 *buf,
		int (*get_ucode_data)(void *, const void *, size_t))
{
#define UCODE_CONTAINER_HEADER_SIZE	12
	u8 *container_hdr[UCODE_CONTAINER_HEADER_SIZE];
	unsigned int *buf_pos = (unsigned int *)container_hdr;
	unsigned long size;

	if (get_ucode_data(&container_hdr, buf, UCODE_CONTAINER_HEADER_SIZE))
		return 0;

	size = buf_pos[2];

	if (buf_pos[1] != UCODE_EQUIV_CPU_TABLE_TYPE || !size) {
		printk(KERN_ERR "microcode: error! "
		       "Wrong microcode equivalnet cpu table\n");
		return 0;
	}

	equiv_cpu_table = (struct equiv_cpu_entry *) vmalloc(size);
	if (!equiv_cpu_table) {
		printk(KERN_ERR "microcode: error, can't allocate memory for equiv CPU table\n");
		return 0;
	}

	buf += UCODE_CONTAINER_HEADER_SIZE;
	if (get_ucode_data(equiv_cpu_table, buf, size)) {
		vfree(equiv_cpu_table);
		return 0;
	}

	return size + UCODE_CONTAINER_HEADER_SIZE; /* add header length */
#undef UCODE_CONTAINER_HEADER_SIZE
}

static void free_equiv_cpu_table(void)
{
	if (equiv_cpu_table) {
		vfree(equiv_cpu_table);
		equiv_cpu_table = NULL;
	}
}

static int generic_load_microcode(int cpu, void *data, size_t size,
		int (*get_ucode_data)(void *, const void *, size_t))
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	u8 *ucode_ptr = data, *new_mc = NULL, *mc;
	int new_rev = uci->cpu_sig.rev;
	unsigned int leftover;
	unsigned long offset;

	offset = install_equiv_cpu_table(ucode_ptr, get_ucode_data);
	if (!offset) {
		printk(KERN_ERR "microcode: installing equivalent cpu table failed\n");
		return -EINVAL;
	}

	ucode_ptr += offset;
	leftover = size - offset;

	while (leftover) {
		unsigned int uninitialized_var(mc_size);
		struct microcode_header_amd *mc_header;

		mc = get_next_ucode(ucode_ptr, leftover, get_ucode_data, &mc_size);
		if (!mc)
			break;

		mc_header = (struct microcode_header_amd *)mc;
		if (get_matching_microcode(cpu, mc, new_rev)) {
			if (new_mc)
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
			if (uci->mc)
				vfree(uci->mc);
			uci->mc = new_mc;
			pr_debug("microcode: CPU%d found a matching microcode update with"
				" version 0x%x (current=0x%x)\n",
				cpu, new_rev, uci->cpu_sig.rev);
		} else
			vfree(new_mc);
	}

	free_equiv_cpu_table();

	return (int)leftover;
}

static int get_ucode_fw(void *to, const void *from, size_t n)
{
	memcpy(to, from, n);
	return 0;
}

static int request_microcode_fw(int cpu, struct device *device)
{
	const char *fw_name = "amd-ucode/microcode_amd.bin";
	const struct firmware *firmware;
	int ret;

	/* We should bind the task to the CPU */
	BUG_ON(cpu != raw_smp_processor_id());

	ret = request_firmware(&firmware, fw_name, device);
	if (ret) {
		printk(KERN_ERR "microcode: ucode data file %s load failed\n", fw_name);
		return ret;
	}

	ret = generic_load_microcode(cpu, (void*)firmware->data, firmware->size,
			&get_ucode_fw);

	release_firmware(firmware);

	return ret;
}

static int request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	printk(KERN_WARNING "microcode: AMD microcode update via /dev/cpu/microcode"
			"is not supported\n");
	return -1;
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
