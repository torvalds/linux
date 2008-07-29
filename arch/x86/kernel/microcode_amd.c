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

/* no concurrent ->write()s are allowed on /dev/cpu/microcode */
extern struct mutex (microcode_mutex);

struct equiv_cpu_entry *equiv_cpu_table;

extern struct ucode_cpu_info ucode_cpu_info[NR_CPUS];

static void collect_cpu_info_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	/* We should bind the task to the CPU */
	BUG_ON(raw_smp_processor_id() != cpu);
	uci->rev = 0;
	uci->pf = 0;
	uci->mc.mc_amd = NULL;
	uci->valid = 1;

	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 0x10) {
		printk(KERN_ERR "microcode: CPU%d not a capable AMD processor\n",
		       cpu);
		uci->valid = 0;
		return;
	}

	asm volatile("movl %1, %%ecx; rdmsr"
		     : "=a" (uci->rev)
		     : "i" (0x0000008B) : "ecx");

	printk(KERN_INFO "microcode: collect_cpu_info_amd : patch_id=0x%x\n",
	       uci->rev);
}

static int get_matching_microcode_amd(void *mc, int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct microcode_header_amd *mc_header = mc;
	unsigned long total_size = get_totalsize(mc_header);
	void *new_mc;
	struct pci_dev *nb_pci_dev, *sb_pci_dev;
	unsigned int current_cpu_id;
	unsigned int equiv_cpu_id = 0x00;
	unsigned int i = 0;

	/* We should bind the task to the CPU */
	BUG_ON(cpu != raw_smp_processor_id());

	/* This is a tricky part. We might be called from a write operation */
	/* to the device file instead of the usual process of firmware */
	/* loading. This routine needs to be able to distinguish both */
/* cases. This is done by checking if there alread is a equivalent */
	/* CPU table installed. If not, we're written through */
	/* /dev/cpu/microcode. */
/* Since we ignore all checks. The error case in which going through */
/* firmware loading and that table is not loaded has already been */
	/* checked earlier. */
	if (equiv_cpu_table == NULL) {
		printk(KERN_INFO "microcode: CPU%d microcode update with "
		       "version 0x%x (current=0x%x)\n",
		       cpu, mc_header->patch_id, uci->rev);
		goto out;
	}

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

	if (mc_header->patch_id <= uci->rev)
		return 0;

	printk(KERN_INFO "microcode: CPU%d found a matching microcode "
	       "update with version 0x%x (current=0x%x)\n",
	       cpu, mc_header->patch_id, uci->rev);

out:
	new_mc = vmalloc(UCODE_MAX_SIZE);
	if (!new_mc) {
		printk(KERN_ERR "microcode: error, can't allocate memory\n");
		return -ENOMEM;
	}
	memset(new_mc, 0, UCODE_MAX_SIZE);

	/* free previous update file */
	vfree(uci->mc.mc_amd);

	memcpy(new_mc, mc, total_size);

	uci->mc.mc_amd = new_mc;
	return 1;
}

static void apply_microcode_amd(int cpu)
{
	unsigned long flags;
	unsigned int eax, edx;
	unsigned int rev;
	int cpu_num = raw_smp_processor_id();
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu_num;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (uci->mc.mc_amd == NULL)
		return;

	spin_lock_irqsave(&microcode_update_lock, flags);

	edx = (unsigned int)(((unsigned long)
			      &(uci->mc.mc_amd->hdr.data_code)) >> 32);
	eax = (unsigned int)(((unsigned long)
			      &(uci->mc.mc_amd->hdr.data_code)) & 0xffffffffL);

	asm volatile("movl %0, %%ecx; wrmsr" :
		     : "i" (0xc0010020), "a" (eax), "d" (edx) : "ecx");

	/* get patch id after patching */
	asm volatile("movl %1, %%ecx; rdmsr"
		     : "=a" (rev)
		     : "i" (0x0000008B) : "ecx");

	spin_unlock_irqrestore(&microcode_update_lock, flags);

	/* check current patch id and patch's id for match */
	if (rev != uci->mc.mc_amd->hdr.patch_id) {
		printk(KERN_ERR "microcode: CPU%d update from revision "
		       "0x%x to 0x%x failed\n", cpu_num,
		       uci->mc.mc_amd->hdr.patch_id, rev);
		return;
	}

	printk(KERN_INFO "microcode: CPU%d updated from revision "
	       "0x%x to 0x%x \n",
	       cpu_num, uci->rev, uci->mc.mc_amd->hdr.patch_id);

	uci->rev = rev;
}

#ifdef CONFIG_MICROCODE_OLD_INTERFACE
extern void __user *user_buffer;        /* user area microcode data buffer */
extern unsigned int user_buffer_size;   /* it's size */

static long get_next_ucode_amd(void **mc, long offset)
{
	struct microcode_header_amd mc_header;
	unsigned long total_size;

	/* No more data */
	if (offset >= user_buffer_size)
		return 0;
	if (copy_from_user(&mc_header, user_buffer + offset, MC_HEADER_SIZE)) {
		printk(KERN_ERR "microcode: error! Can not read user data\n");
		return -EFAULT;
	}
	total_size = get_totalsize(&mc_header);
	if (offset + total_size > user_buffer_size) {
		printk(KERN_ERR "microcode: error! Bad total size in microcode "
		       "data file\n");
		return -EINVAL;
	}
	*mc = vmalloc(UCODE_MAX_SIZE);
	if (!*mc)
		return -ENOMEM;
	memset(*mc, 0, UCODE_MAX_SIZE);

	if (copy_from_user(*mc, user_buffer + offset, total_size)) {
		printk(KERN_ERR "microcode: error! Can not read user data\n");
		vfree(*mc);
		return -EFAULT;
	}
	return offset + total_size;
}
#else
#define get_next_ucode_amd() NULL
#endif

static long get_next_ucode_from_buffer_amd(void **mc, void *buf,
				       unsigned long size, long offset)
{
	struct microcode_header_amd *mc_header;
	unsigned long total_size;
	unsigned char *buf_pos = buf;

	/* No more data */
	if (offset >= size)
		return 0;

	if (buf_pos[offset] != UCODE_UCODE_TYPE) {
		printk(KERN_ERR "microcode: error! "
		       "Wrong microcode payload type field\n");
		return -EINVAL;
	}

	mc_header = (struct microcode_header_amd *)(&buf_pos[offset+8]);

	total_size = (unsigned long) (buf_pos[offset+4] +
				      (buf_pos[offset+5] << 8));

	printk(KERN_INFO "microcode: size %lu, total_size %lu, offset %ld\n",
		size, total_size, offset);

	if (offset + total_size > size) {
		printk(KERN_ERR "microcode: error! Bad data in microcode data file\n");
		return -EINVAL;
	}

	*mc = vmalloc(UCODE_MAX_SIZE);
	if (!*mc) {
		printk(KERN_ERR "microcode: error! "
		       "Can not allocate memory for microcode patch\n");
		return -ENOMEM;
	}

	memset(*mc, 0, UCODE_MAX_SIZE);
	memcpy(*mc, buf + offset + 8, total_size);

	return offset + total_size + 8;
}

static long install_equiv_cpu_table(void *buf, unsigned long size, long offset)
{
	unsigned int *buf_pos = buf;

	/* No more data */
	if (offset >= size)
		return 0;

	if (buf_pos[1] != UCODE_EQUIV_CPU_TABLE_TYPE) {
		printk(KERN_ERR "microcode: error! "
		       "Wrong microcode equivalnet cpu table type field\n");
		return 0;
	}

	if (size == 0) {
		printk(KERN_ERR "microcode: error! "
		       "Wrong microcode equivalnet cpu table length\n");
		return 0;
	}

	equiv_cpu_table = (struct equiv_cpu_entry *) vmalloc(size);
	if (!equiv_cpu_table) {
		printk(KERN_ERR "microcode: error, can't allocate memory for equiv CPU table\n");
		return 0;
	}

	memset(equiv_cpu_table, 0, size);
	memcpy(equiv_cpu_table, &buf_pos[3], size);

	return size + 12; /* add header length */
}

/* fake device for request_firmware */
extern struct platform_device *microcode_pdev;

static int cpu_request_microcode_amd(int cpu)
{
	char name[30];
	const struct firmware *firmware;
	void *buf;
	unsigned int *buf_pos;
	unsigned long size;
	long offset = 0;
	int error;
	void *mc;

	/* We should bind the task to the CPU */
	BUG_ON(cpu != raw_smp_processor_id());

	sprintf(name, "amd-ucode/microcode_amd.bin");
	error = request_firmware(&firmware, "amd-ucode/microcode_amd.bin",
				 &microcode_pdev->dev);
	if (error) {
		printk(KERN_ERR "microcode: ucode data file %s load failed\n",
		       name);
		return error;
	}

	buf_pos = (unsigned int *)firmware->data;
	buf = (void *)firmware->data;
	size = firmware->size;

	if (buf_pos[0] != UCODE_MAGIC) {
		printk(KERN_ERR "microcode: error! Wrong microcode patch file magic\n");
		return -EINVAL;
	}

	offset = install_equiv_cpu_table(buf, buf_pos[2], offset);

	if (!offset) {
		printk(KERN_ERR "microcode: installing equivalent cpu table failed\n");
		return -EINVAL;
	}

	while ((offset =
		get_next_ucode_from_buffer_amd(&mc, buf, size, offset)) > 0) {
		error = get_matching_microcode_amd(mc, cpu);
		if (error < 0)
			break;
		/*
		 * It's possible the data file has multiple matching ucode,
		 * lets keep searching till the latest version
		 */
		if (error == 1) {
			apply_microcode_amd(cpu);
			error = 0;
		}
		vfree(mc);
	}
	if (offset > 0) {
		vfree(mc);
		vfree(equiv_cpu_table);
		equiv_cpu_table = NULL;
	}
	if (offset < 0)
		error = offset;
	release_firmware(firmware);

	return error;
}

static int apply_microcode_check_cpu_amd(int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	unsigned int rev;
	cpumask_t old;
	int err = 0;

	/* Check if the microcode is available */
	if (!uci->mc.mc_amd)
		return 0;

	old = current->cpus_allowed;
	set_cpus_allowed_ptr(current, &cpumask_of_cpu(cpu));

	/* Check if the microcode we have in memory matches the CPU */
	if (c->x86_vendor != X86_VENDOR_AMD || c->x86 < 16)
		err = -EINVAL;

	if (!err) {
		asm volatile("movl %1, %%ecx; rdmsr"
		     : "=a" (rev)
		     : "i" (0x0000008B) : "ecx");

		if (uci->rev != rev)
			err = -EINVAL;
	}

	if (!err)
		apply_microcode_amd(cpu);
	else
		printk(KERN_ERR "microcode: Could not apply microcode to CPU%d:"
		       " rev=0x%x\n",
		       cpu,  uci->rev);

	set_cpus_allowed(current, old);
	return err;
}

static void microcode_fini_cpu_amd(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	mutex_lock(&microcode_mutex);
	uci->valid = 0;
	vfree(uci->mc.mc_amd);
	uci->mc.mc_amd = NULL;
	mutex_unlock(&microcode_mutex);
}

static struct microcode_ops microcode_amd_ops = {
	.get_next_ucode                   = get_next_ucode_amd,
	.get_matching_microcode           = get_matching_microcode_amd,
	.microcode_sanity_check           = NULL,
	.apply_microcode_check_cpu        = apply_microcode_check_cpu_amd,
	.cpu_request_microcode            = cpu_request_microcode_amd,
	.collect_cpu_info                 = collect_cpu_info_amd,
	.apply_microcode                  = apply_microcode_amd,
	.microcode_fini_cpu               = microcode_fini_cpu_amd,
};

static int __init microcode_amd_module_init(void)
{
	struct cpuinfo_x86 *c = &cpu_data(get_cpu());

	equiv_cpu_table = NULL;
	if (c->x86_vendor == X86_VENDOR_AMD)
		return microcode_init(&microcode_amd_ops, THIS_MODULE);
	else
		return -ENODEV;
}

static void __exit microcode_amd_module_exit(void)
{
	microcode_exit();
}

module_init(microcode_amd_module_init)
module_exit(microcode_amd_module_exit)
