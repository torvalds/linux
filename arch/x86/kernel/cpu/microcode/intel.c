/*
 * Intel CPU Microcode Update Driver for Linux
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		 2006 Shaohua Li <shaohua.li@intel.com>
 *
 * Intel CPU microcode early update for Linux
 *
 * Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *		      H Peter Anvin" <hpa@zytor.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * This needs to be before all headers so that pr_debug in printk.h doesn't turn
 * printk calls into no_printk().
 *
 *#define DEBUG
 */
#define pr_fmt(fmt) "microcode: " fmt

#include <linux/earlycpio.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/mm.h>

#include <asm/microcode_intel.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
#include <asm/msr.h>

static unsigned long mc_saved_in_initrd[MAX_UCODE_COUNT];
static struct mc_saved_data {
	unsigned int mc_saved_count;
	struct microcode_intel **mc_saved;
} mc_saved_data;

static enum ucode_state
load_microcode_early(struct microcode_intel **saved,
		     unsigned int num_saved, struct ucode_cpu_info *uci)
{
	struct microcode_intel *ucode_ptr, *new_mc = NULL;
	struct microcode_header_intel *mc_hdr;
	int new_rev, ret, i;

	new_rev = uci->cpu_sig.rev;

	for (i = 0; i < num_saved; i++) {
		ucode_ptr = saved[i];
		mc_hdr	  = (struct microcode_header_intel *)ucode_ptr;

		ret = has_newer_microcode(ucode_ptr,
					  uci->cpu_sig.sig,
					  uci->cpu_sig.pf,
					  new_rev);
		if (!ret)
			continue;

		new_rev = mc_hdr->rev;
		new_mc  = ucode_ptr;
	}

	if (!new_mc)
		return UCODE_NFOUND;

	uci->mc = (struct microcode_intel *)new_mc;
	return UCODE_OK;
}

static inline void
copy_initrd_ptrs(struct microcode_intel **mc_saved, unsigned long *initrd,
		  unsigned long off, int num_saved)
{
	int i;

	for (i = 0; i < num_saved; i++)
		mc_saved[i] = (struct microcode_intel *)(initrd[i] + off);
}

#ifdef CONFIG_X86_32
static void
microcode_phys(struct microcode_intel **mc_saved_tmp,
	       struct mc_saved_data *mc_saved_data)
{
	int i;
	struct microcode_intel ***mc_saved;

	mc_saved = (struct microcode_intel ***)
		   __pa_nodebug(&mc_saved_data->mc_saved);
	for (i = 0; i < mc_saved_data->mc_saved_count; i++) {
		struct microcode_intel *p;

		p = *(struct microcode_intel **)
			__pa_nodebug(mc_saved_data->mc_saved + i);
		mc_saved_tmp[i] = (struct microcode_intel *)__pa_nodebug(p);
	}
}
#endif

static enum ucode_state
load_microcode(struct mc_saved_data *mc_saved_data, unsigned long *initrd,
	       unsigned long initrd_start, struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	unsigned int count = mc_saved_data->mc_saved_count;

	if (!mc_saved_data->mc_saved) {
		copy_initrd_ptrs(mc_saved_tmp, initrd, initrd_start, count);

		return load_microcode_early(mc_saved_tmp, count, uci);
	} else {
#ifdef CONFIG_X86_32
		microcode_phys(mc_saved_tmp, mc_saved_data);
		return load_microcode_early(mc_saved_tmp, count, uci);
#else
		return load_microcode_early(mc_saved_data->mc_saved,
						    count, uci);
#endif
	}
}

/*
 * Given CPU signature and a microcode patch, this function finds if the
 * microcode patch has matching family and model with the CPU.
 */
static enum ucode_state
matching_model_microcode(struct microcode_header_intel *mc_header,
			unsigned long sig)
{
	unsigned int fam, model;
	unsigned int fam_ucode, model_ucode;
	struct extended_sigtable *ext_header;
	unsigned long total_size = get_totalsize(mc_header);
	unsigned long data_size = get_datasize(mc_header);
	int ext_sigcount, i;
	struct extended_signature *ext_sig;

	fam   = __x86_family(sig);
	model = x86_model(sig);

	fam_ucode   = __x86_family(mc_header->sig);
	model_ucode = x86_model(mc_header->sig);

	if (fam == fam_ucode && model == model_ucode)
		return UCODE_OK;

	/* Look for ext. headers: */
	if (total_size <= data_size + MC_HEADER_SIZE)
		return UCODE_NFOUND;

	ext_header   = (void *) mc_header + data_size + MC_HEADER_SIZE;
	ext_sig      = (void *)ext_header + EXT_HEADER_SIZE;
	ext_sigcount = ext_header->count;

	for (i = 0; i < ext_sigcount; i++) {
		fam_ucode   = __x86_family(ext_sig->sig);
		model_ucode = x86_model(ext_sig->sig);

		if (fam == fam_ucode && model == model_ucode)
			return UCODE_OK;

		ext_sig++;
	}
	return UCODE_NFOUND;
}

static int
save_microcode(struct mc_saved_data *mc_saved_data,
	       struct microcode_intel **mc_saved_src,
	       unsigned int mc_saved_count)
{
	int i, j;
	struct microcode_intel **saved_ptr;
	int ret;

	if (!mc_saved_count)
		return -EINVAL;

	/*
	 * Copy new microcode data.
	 */
	saved_ptr = kcalloc(mc_saved_count, sizeof(struct microcode_intel *), GFP_KERNEL);
	if (!saved_ptr)
		return -ENOMEM;

	for (i = 0; i < mc_saved_count; i++) {
		struct microcode_header_intel *mc_hdr;
		struct microcode_intel *mc;
		unsigned long size;

		if (!mc_saved_src[i]) {
			ret = -EINVAL;
			goto err;
		}

		mc     = mc_saved_src[i];
		mc_hdr = &mc->hdr;
		size   = get_totalsize(mc_hdr);

		saved_ptr[i] = kmalloc(size, GFP_KERNEL);
		if (!saved_ptr[i]) {
			ret = -ENOMEM;
			goto err;
		}

		memcpy(saved_ptr[i], mc, size);
	}

	/*
	 * Point to newly saved microcode.
	 */
	mc_saved_data->mc_saved = saved_ptr;
	mc_saved_data->mc_saved_count = mc_saved_count;

	return 0;

err:
	for (j = 0; j <= i; j++)
		kfree(saved_ptr[j]);
	kfree(saved_ptr);

	return ret;
}

/*
 * A microcode patch in ucode_ptr is saved into mc_saved
 * - if it has matching signature and newer revision compared to an existing
 *   patch mc_saved.
 * - or if it is a newly discovered microcode patch.
 *
 * The microcode patch should have matching model with CPU.
 *
 * Returns: The updated number @num_saved of saved microcode patches.
 */
static unsigned int _save_mc(struct microcode_intel **mc_saved,
			     u8 *ucode_ptr, unsigned int num_saved)
{
	struct microcode_header_intel *mc_hdr, *mc_saved_hdr;
	unsigned int sig, pf;
	int found = 0, i;

	mc_hdr = (struct microcode_header_intel *)ucode_ptr;

	for (i = 0; i < num_saved; i++) {
		mc_saved_hdr = (struct microcode_header_intel *)mc_saved[i];
		sig	     = mc_saved_hdr->sig;
		pf	     = mc_saved_hdr->pf;

		if (!find_matching_signature(ucode_ptr, sig, pf))
			continue;

		found = 1;

		if (mc_hdr->rev <= mc_saved_hdr->rev)
			continue;

		/*
		 * Found an older ucode saved earlier. Replace it with
		 * this newer one.
		 */
		mc_saved[i] = (struct microcode_intel *)ucode_ptr;
		break;
	}

	/* Newly detected microcode, save it to memory. */
	if (i >= num_saved && !found)
		mc_saved[num_saved++] = (struct microcode_intel *)ucode_ptr;

	return num_saved;
}

/*
 * Get microcode matching with BSP's model. Only CPUs with the same model as
 * BSP can stay in the platform.
 */
static enum ucode_state __init
get_matching_model_microcode(int cpu, unsigned long start,
			     void *data, size_t size,
			     struct mc_saved_data *mc_saved_data,
			     unsigned long *mc_saved_in_initrd,
			     struct ucode_cpu_info *uci)
{
	u8 *ucode_ptr = data;
	unsigned int leftover = size;
	enum ucode_state state = UCODE_OK;
	unsigned int mc_size;
	struct microcode_header_intel *mc_header;
	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	unsigned int mc_saved_count = mc_saved_data->mc_saved_count;
	int i;

	while (leftover && mc_saved_count < ARRAY_SIZE(mc_saved_tmp)) {

		if (leftover < sizeof(mc_header))
			break;

		mc_header = (struct microcode_header_intel *)ucode_ptr;

		mc_size = get_totalsize(mc_header);
		if (!mc_size || mc_size > leftover ||
			microcode_sanity_check(ucode_ptr, 0) < 0)
			break;

		leftover -= mc_size;

		/*
		 * Since APs with same family and model as the BSP may boot in
		 * the platform, we need to find and save microcode patches
		 * with the same family and model as the BSP.
		 */
		if (matching_model_microcode(mc_header, uci->cpu_sig.sig) !=
			 UCODE_OK) {
			ucode_ptr += mc_size;
			continue;
		}

		mc_saved_count = _save_mc(mc_saved_tmp, ucode_ptr, mc_saved_count);

		ucode_ptr += mc_size;
	}

	if (leftover) {
		state = UCODE_ERROR;
		goto out;
	}

	if (mc_saved_count == 0) {
		state = UCODE_NFOUND;
		goto out;
	}

	for (i = 0; i < mc_saved_count; i++)
		mc_saved_in_initrd[i] = (unsigned long)mc_saved_tmp[i] - start;

	mc_saved_data->mc_saved_count = mc_saved_count;
out:
	return state;
}

static int collect_cpu_info_early(struct ucode_cpu_info *uci)
{
	unsigned int val[2];
	unsigned int family, model;
	struct cpu_signature csig;
	unsigned int eax, ebx, ecx, edx;

	csig.sig = 0;
	csig.pf = 0;
	csig.rev = 0;

	memset(uci, 0, sizeof(*uci));

	eax = 0x00000001;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	csig.sig = eax;

	family = __x86_family(csig.sig);
	model  = x86_model(csig.sig);

	if ((model >= 5) || (family > 6)) {
		/* get processor flags from MSR 0x17 */
		native_rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig.pf = 1 << ((val[1] >> 18) & 7);
	}
	native_wrmsr(MSR_IA32_UCODE_REV, 0, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	native_rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	csig.rev = val[1];

	uci->cpu_sig = csig;
	uci->valid = 1;

	return 0;
}

static void show_saved_mc(void)
{
#ifdef DEBUG
	int i, j;
	unsigned int sig, pf, rev, total_size, data_size, date;
	struct ucode_cpu_info uci;

	if (mc_saved_data.mc_saved_count == 0) {
		pr_debug("no microcode data saved.\n");
		return;
	}
	pr_debug("Total microcode saved: %d\n", mc_saved_data.mc_saved_count);

	collect_cpu_info_early(&uci);

	sig = uci.cpu_sig.sig;
	pf = uci.cpu_sig.pf;
	rev = uci.cpu_sig.rev;
	pr_debug("CPU: sig=0x%x, pf=0x%x, rev=0x%x\n", sig, pf, rev);

	for (i = 0; i < mc_saved_data.mc_saved_count; i++) {
		struct microcode_header_intel *mc_saved_header;
		struct extended_sigtable *ext_header;
		int ext_sigcount;
		struct extended_signature *ext_sig;

		mc_saved_header = (struct microcode_header_intel *)
				  mc_saved_data.mc_saved[i];
		sig = mc_saved_header->sig;
		pf = mc_saved_header->pf;
		rev = mc_saved_header->rev;
		total_size = get_totalsize(mc_saved_header);
		data_size = get_datasize(mc_saved_header);
		date = mc_saved_header->date;

		pr_debug("mc_saved[%d]: sig=0x%x, pf=0x%x, rev=0x%x, toal size=0x%x, date = %04x-%02x-%02x\n",
			 i, sig, pf, rev, total_size,
			 date & 0xffff,
			 date >> 24,
			 (date >> 16) & 0xff);

		/* Look for ext. headers: */
		if (total_size <= data_size + MC_HEADER_SIZE)
			continue;

		ext_header = (void *) mc_saved_header + data_size + MC_HEADER_SIZE;
		ext_sigcount = ext_header->count;
		ext_sig = (void *)ext_header + EXT_HEADER_SIZE;

		for (j = 0; j < ext_sigcount; j++) {
			sig = ext_sig->sig;
			pf = ext_sig->pf;

			pr_debug("\tExtended[%d]: sig=0x%x, pf=0x%x\n",
				 j, sig, pf);

			ext_sig++;
		}

	}
#endif
}

#ifdef CONFIG_HOTPLUG_CPU
static DEFINE_MUTEX(x86_cpu_microcode_mutex);
/*
 * Save this mc into mc_saved_data. So it will be loaded early when a CPU is
 * hot added or resumes.
 *
 * Please make sure this mc should be a valid microcode patch before calling
 * this function.
 */
int save_mc_for_early(u8 *mc)
{
	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	unsigned int mc_saved_count_init;
	unsigned int mc_saved_count;
	struct microcode_intel **mc_saved;
	int ret = 0;
	int i;

	/*
	 * Hold hotplug lock so mc_saved_data is not accessed by a CPU in
	 * hotplug.
	 */
	mutex_lock(&x86_cpu_microcode_mutex);

	mc_saved_count_init = mc_saved_data.mc_saved_count;
	mc_saved_count = mc_saved_data.mc_saved_count;
	mc_saved = mc_saved_data.mc_saved;

	if (mc_saved && mc_saved_count)
		memcpy(mc_saved_tmp, mc_saved,
		       mc_saved_count * sizeof(struct microcode_intel *));
	/*
	 * Save the microcode patch mc in mc_save_tmp structure if it's a newer
	 * version.
	 */
	mc_saved_count = _save_mc(mc_saved_tmp, mc, mc_saved_count);

	/*
	 * Save the mc_save_tmp in global mc_saved_data.
	 */
	ret = save_microcode(&mc_saved_data, mc_saved_tmp, mc_saved_count);
	if (ret) {
		pr_err("Cannot save microcode patch.\n");
		goto out;
	}

	show_saved_mc();

	/*
	 * Free old saved microcode data.
	 */
	if (mc_saved) {
		for (i = 0; i < mc_saved_count_init; i++)
			kfree(mc_saved[i]);
		kfree(mc_saved);
	}

out:
	mutex_unlock(&x86_cpu_microcode_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(save_mc_for_early);
#endif

static bool __init load_builtin_intel_microcode(struct cpio_data *cp)
{
#ifdef CONFIG_X86_64
	unsigned int eax = 0x00000001, ebx, ecx = 0, edx;
	unsigned int family, model, stepping;
	char name[30];

	native_cpuid(&eax, &ebx, &ecx, &edx);

	family   = __x86_family(eax);
	model    = x86_model(eax);
	stepping = eax & 0xf;

	sprintf(name, "intel-ucode/%02x-%02x-%02x", family, model, stepping);

	return get_builtin_firmware(cp, name);
#else
	return false;
#endif
}

static __initdata char ucode_name[] = "kernel/x86/microcode/GenuineIntel.bin";
static __init enum ucode_state
scan_microcode(struct mc_saved_data *mc_saved_data, unsigned long *initrd,
	       unsigned long start, unsigned long size,
	       struct ucode_cpu_info *uci)
{
	struct cpio_data cd;
	long offset = 0;
#ifdef CONFIG_X86_32
	char *p = (char *)__pa_nodebug(ucode_name);
#else
	char *p = ucode_name;
#endif

	cd.data = NULL;
	cd.size = 0;

	/* try built-in microcode if no initrd */
	if (!size) {
		if (!load_builtin_intel_microcode(&cd))
			return UCODE_ERROR;
	} else {
		cd = find_cpio_data(p, (void *)start, size, &offset);
		if (!cd.data)
			return UCODE_ERROR;
	}

	return get_matching_model_microcode(0, start, cd.data, cd.size,
					    mc_saved_data, initrd, uci);
}

/*
 * Print ucode update info.
 */
static void
print_ucode_info(struct ucode_cpu_info *uci, unsigned int date)
{
	int cpu = smp_processor_id();

	pr_info("CPU%d microcode updated early to revision 0x%x, date = %04x-%02x-%02x\n",
		cpu,
		uci->cpu_sig.rev,
		date & 0xffff,
		date >> 24,
		(date >> 16) & 0xff);
}

#ifdef CONFIG_X86_32

static int delay_ucode_info;
static int current_mc_date;

/*
 * Print early updated ucode info after printk works. This is delayed info dump.
 */
void show_ucode_info_early(void)
{
	struct ucode_cpu_info uci;

	if (delay_ucode_info) {
		collect_cpu_info_early(&uci);
		print_ucode_info(&uci, current_mc_date);
		delay_ucode_info = 0;
	}
}

/*
 * At this point, we can not call printk() yet. Keep microcode patch number in
 * mc_saved_data.mc_saved and delay printing microcode info in
 * show_ucode_info_early() until printk() works.
 */
static void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc_intel;
	int *delay_ucode_info_p;
	int *current_mc_date_p;

	mc_intel = uci->mc;
	if (mc_intel == NULL)
		return;

	delay_ucode_info_p = (int *)__pa_nodebug(&delay_ucode_info);
	current_mc_date_p = (int *)__pa_nodebug(&current_mc_date);

	*delay_ucode_info_p = 1;
	*current_mc_date_p = mc_intel->hdr.date;
}
#else

/*
 * Flush global tlb. We only do this in x86_64 where paging has been enabled
 * already and PGE should be enabled as well.
 */
static inline void flush_tlb_early(void)
{
	__native_flush_tlb_global_irq_disabled();
}

static inline void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc_intel;

	mc_intel = uci->mc;
	if (mc_intel == NULL)
		return;

	print_ucode_info(uci, mc_intel->hdr.date);
}
#endif

static int apply_microcode_early(struct ucode_cpu_info *uci, bool early)
{
	struct microcode_intel *mc_intel;
	unsigned int val[2];

	mc_intel = uci->mc;
	if (mc_intel == NULL)
		return 0;

	/* write microcode via MSR 0x79 */
	native_wrmsr(MSR_IA32_UCODE_WRITE,
	      (unsigned long) mc_intel->bits,
	      (unsigned long) mc_intel->bits >> 16 >> 16);
	native_wrmsr(MSR_IA32_UCODE_REV, 0, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	native_rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);
	if (val[1] != mc_intel->hdr.rev)
		return -1;

#ifdef CONFIG_X86_64
	/* Flush global tlb. This is precaution. */
	flush_tlb_early();
#endif
	uci->cpu_sig.rev = val[1];

	if (early)
		print_ucode(uci);
	else
		print_ucode_info(uci, mc_intel->hdr.date);

	return 0;
}

/*
 * This function converts microcode patch offsets previously stored in
 * mc_saved_in_initrd to pointers and stores the pointers in mc_saved_data.
 */
int __init save_microcode_in_initrd_intel(void)
{
	unsigned int count = mc_saved_data.mc_saved_count;
	struct microcode_intel *mc_saved[MAX_UCODE_COUNT];
	int ret = 0;

	if (count == 0)
		return ret;

	copy_initrd_ptrs(mc_saved, mc_saved_in_initrd, get_initrd_start(), count);
	ret = save_microcode(&mc_saved_data, mc_saved, count);
	if (ret)
		pr_err("Cannot save microcode patches from initrd.\n");

	show_saved_mc();

	return ret;
}

static void __init
_load_ucode_intel_bsp(struct mc_saved_data *mc_saved_data,
		      unsigned long *initrd,
		      unsigned long start, unsigned long size)
{
	struct ucode_cpu_info uci;
	enum ucode_state ret;

	collect_cpu_info_early(&uci);

	ret = scan_microcode(mc_saved_data, initrd, start, size, &uci);
	if (ret != UCODE_OK)
		return;

	ret = load_microcode(mc_saved_data, initrd, start, &uci);
	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, true);
}

void __init load_ucode_intel_bsp(void)
{
	u64 start, size;
#ifdef CONFIG_X86_32
	struct boot_params *p;

	p	= (struct boot_params *)__pa_nodebug(&boot_params);
	size	= p->hdr.ramdisk_size;

	/*
	 * Set start only if we have an initrd image. We cannot use initrd_start
	 * because it is not set that early yet.
	 */
	start	= (size ? p->hdr.ramdisk_image : 0);

	_load_ucode_intel_bsp((struct mc_saved_data *)__pa_nodebug(&mc_saved_data),
			      (unsigned long *)__pa_nodebug(&mc_saved_in_initrd),
			      start, size);
#else
	size	= boot_params.hdr.ramdisk_size;
	start	= (size ? boot_params.hdr.ramdisk_image + PAGE_OFFSET : 0);

	_load_ucode_intel_bsp(&mc_saved_data, mc_saved_in_initrd, start, size);
#endif
}

void load_ucode_intel_ap(void)
{
	struct mc_saved_data *mc_saved_data_p;
	struct ucode_cpu_info uci;
	unsigned long *mc_saved_in_initrd_p;
	enum ucode_state ret;
#ifdef CONFIG_X86_32

	mc_saved_in_initrd_p = (unsigned long *)__pa_nodebug(mc_saved_in_initrd);
	mc_saved_data_p = (struct mc_saved_data *)__pa_nodebug(&mc_saved_data);
#else
	mc_saved_in_initrd_p = mc_saved_in_initrd;
	mc_saved_data_p = &mc_saved_data;
#endif

	/*
	 * If there is no valid ucode previously saved in memory, no need to
	 * update ucode on this AP.
	 */
	if (mc_saved_data_p->mc_saved_count == 0)
		return;

	collect_cpu_info_early(&uci);
	ret = load_microcode(mc_saved_data_p, mc_saved_in_initrd_p,
			     get_initrd_start_addr(), &uci);

	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, true);
}

void reload_ucode_intel(void)
{
	struct ucode_cpu_info uci;
	enum ucode_state ret;

	if (!mc_saved_data.mc_saved_count)
		return;

	collect_cpu_info_early(&uci);

	ret = load_microcode_early(mc_saved_data.mc_saved,
				   mc_saved_data.mc_saved_count, &uci);
	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, false);
}

static int collect_cpu_info(int cpu_num, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);
	unsigned int val[2];

	memset(csig, 0, sizeof(*csig));

	csig->sig = cpuid_eax(0x00000001);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig->pf = 1 << ((val[1] >> 18) & 7);
	}

	csig->rev = c->microcode;
	pr_info("CPU%d sig=0x%x, pf=0x%x, revision=0x%x\n",
		cpu_num, csig->sig, csig->pf, csig->rev);

	return 0;
}

/*
 * return 0 - no update found
 * return 1 - found update
 */
static int get_matching_mc(struct microcode_intel *mc_intel, int cpu)
{
	struct cpu_signature cpu_sig;
	unsigned int csig, cpf, crev;

	collect_cpu_info(cpu, &cpu_sig);

	csig = cpu_sig.sig;
	cpf = cpu_sig.pf;
	crev = cpu_sig.rev;

	return has_newer_microcode(mc_intel, csig, cpf, crev);
}

static int apply_microcode_intel(int cpu)
{
	struct microcode_intel *mc_intel;
	struct ucode_cpu_info *uci;
	unsigned int val[2];
	int cpu_num = raw_smp_processor_id();
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);

	uci = ucode_cpu_info + cpu;
	mc_intel = uci->mc;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (mc_intel == NULL)
		return 0;

	/*
	 * Microcode on this CPU could be updated earlier. Only apply the
	 * microcode patch in mc_intel when it is newer than the one on this
	 * CPU.
	 */
	if (get_matching_mc(mc_intel, cpu) == 0)
		return 0;

	/* write microcode via MSR 0x79 */
	wrmsr(MSR_IA32_UCODE_WRITE,
	      (unsigned long) mc_intel->bits,
	      (unsigned long) mc_intel->bits >> 16 >> 16);
	wrmsr(MSR_IA32_UCODE_REV, 0, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	if (val[1] != mc_intel->hdr.rev) {
		pr_err("CPU%d update to revision 0x%x failed\n",
		       cpu_num, mc_intel->hdr.rev);
		return -1;
	}
	pr_info("CPU%d updated to revision 0x%x, date = %04x-%02x-%02x\n",
		cpu_num, val[1],
		mc_intel->hdr.date & 0xffff,
		mc_intel->hdr.date >> 24,
		(mc_intel->hdr.date >> 16) & 0xff);

	uci->cpu_sig.rev = val[1];
	c->microcode = val[1];

	return 0;
}

static enum ucode_state generic_load_microcode(int cpu, void *data, size_t size,
				int (*get_ucode_data)(void *, const void *, size_t))
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	u8 *ucode_ptr = data, *new_mc = NULL, *mc = NULL;
	int new_rev = uci->cpu_sig.rev;
	unsigned int leftover = size;
	enum ucode_state state = UCODE_OK;
	unsigned int curr_mc_size = 0;
	unsigned int csig, cpf;

	while (leftover) {
		struct microcode_header_intel mc_header;
		unsigned int mc_size;

		if (leftover < sizeof(mc_header)) {
			pr_err("error! Truncated header in microcode data file\n");
			break;
		}

		if (get_ucode_data(&mc_header, ucode_ptr, sizeof(mc_header)))
			break;

		mc_size = get_totalsize(&mc_header);
		if (!mc_size || mc_size > leftover) {
			pr_err("error! Bad data in microcode data file\n");
			break;
		}

		/* For performance reasons, reuse mc area when possible */
		if (!mc || mc_size > curr_mc_size) {
			vfree(mc);
			mc = vmalloc(mc_size);
			if (!mc)
				break;
			curr_mc_size = mc_size;
		}

		if (get_ucode_data(mc, ucode_ptr, mc_size) ||
		    microcode_sanity_check(mc, 1) < 0) {
			break;
		}

		csig = uci->cpu_sig.sig;
		cpf = uci->cpu_sig.pf;
		if (has_newer_microcode(mc, csig, cpf, new_rev)) {
			vfree(new_mc);
			new_rev = mc_header.rev;
			new_mc  = mc;
			mc = NULL;	/* trigger new vmalloc */
		}

		ucode_ptr += mc_size;
		leftover  -= mc_size;
	}

	vfree(mc);

	if (leftover) {
		vfree(new_mc);
		state = UCODE_ERROR;
		goto out;
	}

	if (!new_mc) {
		state = UCODE_NFOUND;
		goto out;
	}

	vfree(uci->mc);
	uci->mc = (struct microcode_intel *)new_mc;

	/*
	 * If early loading microcode is supported, save this mc into
	 * permanent memory. So it will be loaded early when a CPU is hot added
	 * or resumes.
	 */
	save_mc_for_early(new_mc);

	pr_debug("CPU%d found a matching microcode update with version 0x%x (current=0x%x)\n",
		 cpu, new_rev, uci->cpu_sig.rev);
out:
	return state;
}

static int get_ucode_fw(void *to, const void *from, size_t n)
{
	memcpy(to, from, n);
	return 0;
}

static enum ucode_state request_microcode_fw(int cpu, struct device *device,
					     bool refresh_fw)
{
	char name[30];
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	const struct firmware *firmware;
	enum ucode_state ret;

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_mask);

	if (request_firmware_direct(&firmware, name, device)) {
		pr_debug("data file %s load failed\n", name);
		return UCODE_NFOUND;
	}

	ret = generic_load_microcode(cpu, (void *)firmware->data,
				     firmware->size, &get_ucode_fw);

	release_firmware(firmware);

	return ret;
}

static int get_ucode_user(void *to, const void *from, size_t n)
{
	return copy_from_user(to, from, n);
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	return generic_load_microcode(cpu, (void *)buf, size, &get_ucode_user);
}

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	vfree(uci->mc);
	uci->mc = NULL;
}

static struct microcode_ops microcode_intel_ops = {
	.request_microcode_user		  = request_microcode_user,
	.request_microcode_fw             = request_microcode_fw,
	.collect_cpu_info                 = collect_cpu_info,
	.apply_microcode                  = apply_microcode_intel,
	.microcode_fini_cpu               = microcode_fini_cpu,
};

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	return &microcode_intel_ops;
}

