// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intel CPU Microcode Update Driver for Linux
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <aivazian.tigran@gmail.com>
 *		 2006 Shaohua Li <shaohua.li@intel.com>
 *
 * Intel CPU microcode early update for Linux
 *
 * Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *		      H Peter Anvin" <hpa@zytor.com>
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
#include <linux/uio.h>
#include <linux/mm.h>

#include <asm/microcode_intel.h>
#include <asm/intel-family.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
#include <asm/msr.h>

static const char ucode_path[] = "kernel/x86/microcode/GenuineIntel.bin";

/* Current microcode patch used in early patching on the APs. */
static struct microcode_intel *intel_ucode_patch;

/* last level cache size per core */
static int llc_size_per_core;

static inline bool cpu_signatures_match(unsigned int s1, unsigned int p1,
					unsigned int s2, unsigned int p2)
{
	if (s1 != s2)
		return false;

	/* Processor flags are either both 0 ... */
	if (!p1 && !p2)
		return true;

	/* ... or they intersect. */
	return p1 & p2;
}

/*
 * Returns 1 if update has been found, 0 otherwise.
 */
static int find_matching_signature(void *mc, unsigned int csig, int cpf)
{
	struct microcode_header_intel *mc_hdr = mc;
	struct extended_sigtable *ext_hdr;
	struct extended_signature *ext_sig;
	int i;

	if (cpu_signatures_match(csig, cpf, mc_hdr->sig, mc_hdr->pf))
		return 1;

	/* Look for ext. headers: */
	if (get_totalsize(mc_hdr) <= get_datasize(mc_hdr) + MC_HEADER_SIZE)
		return 0;

	ext_hdr = mc + get_datasize(mc_hdr) + MC_HEADER_SIZE;
	ext_sig = (void *)ext_hdr + EXT_HEADER_SIZE;

	for (i = 0; i < ext_hdr->count; i++) {
		if (cpu_signatures_match(csig, cpf, ext_sig->sig, ext_sig->pf))
			return 1;
		ext_sig++;
	}
	return 0;
}

/*
 * Returns 1 if update has been found, 0 otherwise.
 */
static int has_newer_microcode(void *mc, unsigned int csig, int cpf, int new_rev)
{
	struct microcode_header_intel *mc_hdr = mc;

	if (mc_hdr->rev <= new_rev)
		return 0;

	return find_matching_signature(mc, csig, cpf);
}

/*
 * Given CPU signature and a microcode patch, this function finds if the
 * microcode patch has matching family and model with the CPU.
 *
 * %true - if there's a match
 * %false - otherwise
 */
static bool microcode_matches(struct microcode_header_intel *mc_header,
			      unsigned long sig)
{
	unsigned long total_size = get_totalsize(mc_header);
	unsigned long data_size = get_datasize(mc_header);
	struct extended_sigtable *ext_header;
	unsigned int fam_ucode, model_ucode;
	struct extended_signature *ext_sig;
	unsigned int fam, model;
	int ext_sigcount, i;

	fam   = x86_family(sig);
	model = x86_model(sig);

	fam_ucode   = x86_family(mc_header->sig);
	model_ucode = x86_model(mc_header->sig);

	if (fam == fam_ucode && model == model_ucode)
		return true;

	/* Look for ext. headers: */
	if (total_size <= data_size + MC_HEADER_SIZE)
		return false;

	ext_header   = (void *) mc_header + data_size + MC_HEADER_SIZE;
	ext_sig      = (void *)ext_header + EXT_HEADER_SIZE;
	ext_sigcount = ext_header->count;

	for (i = 0; i < ext_sigcount; i++) {
		fam_ucode   = x86_family(ext_sig->sig);
		model_ucode = x86_model(ext_sig->sig);

		if (fam == fam_ucode && model == model_ucode)
			return true;

		ext_sig++;
	}
	return false;
}

static struct ucode_patch *memdup_patch(void *data, unsigned int size)
{
	struct ucode_patch *p;

	p = kzalloc(sizeof(struct ucode_patch), GFP_KERNEL);
	if (!p)
		return NULL;

	p->data = kmemdup(data, size, GFP_KERNEL);
	if (!p->data) {
		kfree(p);
		return NULL;
	}

	return p;
}

static void save_microcode_patch(void *data, unsigned int size)
{
	struct microcode_header_intel *mc_hdr, *mc_saved_hdr;
	struct ucode_patch *iter, *tmp, *p = NULL;
	bool prev_found = false;
	unsigned int sig, pf;

	mc_hdr = (struct microcode_header_intel *)data;

	list_for_each_entry_safe(iter, tmp, &microcode_cache, plist) {
		mc_saved_hdr = (struct microcode_header_intel *)iter->data;
		sig	     = mc_saved_hdr->sig;
		pf	     = mc_saved_hdr->pf;

		if (find_matching_signature(data, sig, pf)) {
			prev_found = true;

			if (mc_hdr->rev <= mc_saved_hdr->rev)
				continue;

			p = memdup_patch(data, size);
			if (!p)
				pr_err("Error allocating buffer %p\n", data);
			else {
				list_replace(&iter->plist, &p->plist);
				kfree(iter->data);
				kfree(iter);
			}
		}
	}

	/*
	 * There weren't any previous patches found in the list cache; save the
	 * newly found.
	 */
	if (!prev_found) {
		p = memdup_patch(data, size);
		if (!p)
			pr_err("Error allocating buffer for %p\n", data);
		else
			list_add_tail(&p->plist, &microcode_cache);
	}

	if (!p)
		return;

	/*
	 * Save for early loading. On 32-bit, that needs to be a physical
	 * address as the APs are running from physical addresses, before
	 * paging has been enabled.
	 */
	if (IS_ENABLED(CONFIG_X86_32))
		intel_ucode_patch = (struct microcode_intel *)__pa_nodebug(p->data);
	else
		intel_ucode_patch = p->data;
}

static int microcode_sanity_check(void *mc, int print_err)
{
	unsigned long total_size, data_size, ext_table_size;
	struct microcode_header_intel *mc_header = mc;
	struct extended_sigtable *ext_header = NULL;
	u32 sum, orig_sum, ext_sigcount = 0, i;
	struct extended_signature *ext_sig;

	total_size = get_totalsize(mc_header);
	data_size = get_datasize(mc_header);

	if (data_size + MC_HEADER_SIZE > total_size) {
		if (print_err)
			pr_err("Error: bad microcode data file size.\n");
		return -EINVAL;
	}

	if (mc_header->ldrver != 1 || mc_header->hdrver != 1) {
		if (print_err)
			pr_err("Error: invalid/unknown microcode update format.\n");
		return -EINVAL;
	}

	ext_table_size = total_size - (MC_HEADER_SIZE + data_size);
	if (ext_table_size) {
		u32 ext_table_sum = 0;
		u32 *ext_tablep;

		if ((ext_table_size < EXT_HEADER_SIZE)
		 || ((ext_table_size - EXT_HEADER_SIZE) % EXT_SIGNATURE_SIZE)) {
			if (print_err)
				pr_err("Error: truncated extended signature table.\n");
			return -EINVAL;
		}

		ext_header = mc + MC_HEADER_SIZE + data_size;
		if (ext_table_size != exttable_size(ext_header)) {
			if (print_err)
				pr_err("Error: extended signature table size mismatch.\n");
			return -EFAULT;
		}

		ext_sigcount = ext_header->count;

		/*
		 * Check extended table checksum: the sum of all dwords that
		 * comprise a valid table must be 0.
		 */
		ext_tablep = (u32 *)ext_header;

		i = ext_table_size / sizeof(u32);
		while (i--)
			ext_table_sum += ext_tablep[i];

		if (ext_table_sum) {
			if (print_err)
				pr_warn("Bad extended signature table checksum, aborting.\n");
			return -EINVAL;
		}
	}

	/*
	 * Calculate the checksum of update data and header. The checksum of
	 * valid update data and header including the extended signature table
	 * must be 0.
	 */
	orig_sum = 0;
	i = (MC_HEADER_SIZE + data_size) / sizeof(u32);
	while (i--)
		orig_sum += ((u32 *)mc)[i];

	if (orig_sum) {
		if (print_err)
			pr_err("Bad microcode data checksum, aborting.\n");
		return -EINVAL;
	}

	if (!ext_table_size)
		return 0;

	/*
	 * Check extended signature checksum: 0 => valid.
	 */
	for (i = 0; i < ext_sigcount; i++) {
		ext_sig = (void *)ext_header + EXT_HEADER_SIZE +
			  EXT_SIGNATURE_SIZE * i;

		sum = (mc_header->sig + mc_header->pf + mc_header->cksum) -
		      (ext_sig->sig + ext_sig->pf + ext_sig->cksum);
		if (sum) {
			if (print_err)
				pr_err("Bad extended signature checksum, aborting.\n");
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * Get microcode matching with BSP's model. Only CPUs with the same model as
 * BSP can stay in the platform.
 */
static struct microcode_intel *
scan_microcode(void *data, size_t size, struct ucode_cpu_info *uci, bool save)
{
	struct microcode_header_intel *mc_header;
	struct microcode_intel *patch = NULL;
	unsigned int mc_size;

	while (size) {
		if (size < sizeof(struct microcode_header_intel))
			break;

		mc_header = (struct microcode_header_intel *)data;

		mc_size = get_totalsize(mc_header);
		if (!mc_size ||
		    mc_size > size ||
		    microcode_sanity_check(data, 0) < 0)
			break;

		size -= mc_size;

		if (!microcode_matches(mc_header, uci->cpu_sig.sig)) {
			data += mc_size;
			continue;
		}

		if (save) {
			save_microcode_patch(data, mc_size);
			goto next;
		}


		if (!patch) {
			if (!has_newer_microcode(data,
						 uci->cpu_sig.sig,
						 uci->cpu_sig.pf,
						 uci->cpu_sig.rev))
				goto next;

		} else {
			struct microcode_header_intel *phdr = &patch->hdr;

			if (!has_newer_microcode(data,
						 phdr->sig,
						 phdr->pf,
						 phdr->rev))
				goto next;
		}

		/* We have a newer patch, save it. */
		patch = data;

next:
		data += mc_size;
	}

	if (size)
		return NULL;

	return patch;
}

static int collect_cpu_info_early(struct ucode_cpu_info *uci)
{
	unsigned int val[2];
	unsigned int family, model;
	struct cpu_signature csig = { 0 };
	unsigned int eax, ebx, ecx, edx;

	memset(uci, 0, sizeof(*uci));

	eax = 0x00000001;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	csig.sig = eax;

	family = x86_family(eax);
	model  = x86_model(eax);

	if ((model >= 5) || (family > 6)) {
		/* get processor flags from MSR 0x17 */
		native_rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig.pf = 1 << ((val[1] >> 18) & 7);
	}

	csig.rev = intel_get_microcode_revision();

	uci->cpu_sig = csig;
	uci->valid = 1;

	return 0;
}

static void show_saved_mc(void)
{
#ifdef DEBUG
	int i = 0, j;
	unsigned int sig, pf, rev, total_size, data_size, date;
	struct ucode_cpu_info uci;
	struct ucode_patch *p;

	if (list_empty(&microcode_cache)) {
		pr_debug("no microcode data saved.\n");
		return;
	}

	collect_cpu_info_early(&uci);

	sig	= uci.cpu_sig.sig;
	pf	= uci.cpu_sig.pf;
	rev	= uci.cpu_sig.rev;
	pr_debug("CPU: sig=0x%x, pf=0x%x, rev=0x%x\n", sig, pf, rev);

	list_for_each_entry(p, &microcode_cache, plist) {
		struct microcode_header_intel *mc_saved_header;
		struct extended_sigtable *ext_header;
		struct extended_signature *ext_sig;
		int ext_sigcount;

		mc_saved_header = (struct microcode_header_intel *)p->data;

		sig	= mc_saved_header->sig;
		pf	= mc_saved_header->pf;
		rev	= mc_saved_header->rev;
		date	= mc_saved_header->date;

		total_size	= get_totalsize(mc_saved_header);
		data_size	= get_datasize(mc_saved_header);

		pr_debug("mc_saved[%d]: sig=0x%x, pf=0x%x, rev=0x%x, total size=0x%x, date = %04x-%02x-%02x\n",
			 i++, sig, pf, rev, total_size,
			 date & 0xffff,
			 date >> 24,
			 (date >> 16) & 0xff);

		/* Look for ext. headers: */
		if (total_size <= data_size + MC_HEADER_SIZE)
			continue;

		ext_header = (void *)mc_saved_header + data_size + MC_HEADER_SIZE;
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

/*
 * Save this microcode patch. It will be loaded early when a CPU is
 * hot-added or resumes.
 */
static void save_mc_for_early(u8 *mc, unsigned int size)
{
	/* Synchronization during CPU hotplug. */
	static DEFINE_MUTEX(x86_cpu_microcode_mutex);

	mutex_lock(&x86_cpu_microcode_mutex);

	save_microcode_patch(mc, size);
	show_saved_mc();

	mutex_unlock(&x86_cpu_microcode_mutex);
}

static bool load_builtin_intel_microcode(struct cpio_data *cp)
{
	unsigned int eax = 1, ebx, ecx = 0, edx;
	char name[30];

	if (IS_ENABLED(CONFIG_X86_32))
		return false;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		      x86_family(eax), x86_model(eax), x86_stepping(eax));

	return get_builtin_firmware(cp, name);
}

/*
 * Print ucode update info.
 */
static void
print_ucode_info(struct ucode_cpu_info *uci, unsigned int date)
{
	pr_info_once("microcode updated early to revision 0x%x, date = %04x-%02x-%02x\n",
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
 * At this point, we can not call printk() yet. Delay printing microcode info in
 * show_ucode_info_early() until printk() works.
 */
static void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc;
	int *delay_ucode_info_p;
	int *current_mc_date_p;

	mc = uci->mc;
	if (!mc)
		return;

	delay_ucode_info_p = (int *)__pa_nodebug(&delay_ucode_info);
	current_mc_date_p = (int *)__pa_nodebug(&current_mc_date);

	*delay_ucode_info_p = 1;
	*current_mc_date_p = mc->hdr.date;
}
#else

static inline void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc;

	mc = uci->mc;
	if (!mc)
		return;

	print_ucode_info(uci, mc->hdr.date);
}
#endif

static int apply_microcode_early(struct ucode_cpu_info *uci, bool early)
{
	struct microcode_intel *mc;
	u32 rev;

	mc = uci->mc;
	if (!mc)
		return 0;

	/*
	 * Save us the MSR write below - which is a particular expensive
	 * operation - when the other hyperthread has updated the microcode
	 * already.
	 */
	rev = intel_get_microcode_revision();
	if (rev >= mc->hdr.rev) {
		uci->cpu_sig.rev = rev;
		return UCODE_OK;
	}

	/*
	 * Writeback and invalidate caches before updating microcode to avoid
	 * internal issues depending on what the microcode is updating.
	 */
	native_wbinvd();

	/* write microcode via MSR 0x79 */
	native_wrmsrl(MSR_IA32_UCODE_WRITE, (unsigned long)mc->bits);

	rev = intel_get_microcode_revision();
	if (rev != mc->hdr.rev)
		return -1;

	uci->cpu_sig.rev = rev;

	if (early)
		print_ucode(uci);
	else
		print_ucode_info(uci, mc->hdr.date);

	return 0;
}

int __init save_microcode_in_initrd_intel(void)
{
	struct ucode_cpu_info uci;
	struct cpio_data cp;

	/*
	 * initrd is going away, clear patch ptr. We will scan the microcode one
	 * last time before jettisoning and save a patch, if found. Then we will
	 * update that pointer too, with a stable patch address to use when
	 * resuming the cores.
	 */
	intel_ucode_patch = NULL;

	if (!load_builtin_intel_microcode(&cp))
		cp = find_microcode_in_initrd(ucode_path, false);

	if (!(cp.data && cp.size))
		return 0;

	collect_cpu_info_early(&uci);

	scan_microcode(cp.data, cp.size, &uci, true);

	show_saved_mc();

	return 0;
}

/*
 * @res_patch, output: a pointer to the patch we found.
 */
static struct microcode_intel *__load_ucode_intel(struct ucode_cpu_info *uci)
{
	static const char *path;
	struct cpio_data cp;
	bool use_pa;

	if (IS_ENABLED(CONFIG_X86_32)) {
		path	  = (const char *)__pa_nodebug(ucode_path);
		use_pa	  = true;
	} else {
		path	  = ucode_path;
		use_pa	  = false;
	}

	/* try built-in microcode first */
	if (!load_builtin_intel_microcode(&cp))
		cp = find_microcode_in_initrd(path, use_pa);

	if (!(cp.data && cp.size))
		return NULL;

	collect_cpu_info_early(uci);

	return scan_microcode(cp.data, cp.size, uci, false);
}

void __init load_ucode_intel_bsp(void)
{
	struct microcode_intel *patch;
	struct ucode_cpu_info uci;

	patch = __load_ucode_intel(&uci);
	if (!patch)
		return;

	uci.mc = patch;

	apply_microcode_early(&uci, true);
}

void load_ucode_intel_ap(void)
{
	struct microcode_intel *patch, **iup;
	struct ucode_cpu_info uci;

	if (IS_ENABLED(CONFIG_X86_32))
		iup = (struct microcode_intel **) __pa_nodebug(&intel_ucode_patch);
	else
		iup = &intel_ucode_patch;

reget:
	if (!*iup) {
		patch = __load_ucode_intel(&uci);
		if (!patch)
			return;

		*iup = patch;
	}

	uci.mc = *iup;

	if (apply_microcode_early(&uci, true)) {
		/* Mixed-silicon system? Try to refetch the proper patch: */
		*iup = NULL;

		goto reget;
	}
}

static struct microcode_intel *find_patch(struct ucode_cpu_info *uci)
{
	struct microcode_header_intel *phdr;
	struct ucode_patch *iter, *tmp;

	list_for_each_entry_safe(iter, tmp, &microcode_cache, plist) {

		phdr = (struct microcode_header_intel *)iter->data;

		if (phdr->rev <= uci->cpu_sig.rev)
			continue;

		if (!find_matching_signature(phdr,
					     uci->cpu_sig.sig,
					     uci->cpu_sig.pf))
			continue;

		return iter->data;
	}
	return NULL;
}

void reload_ucode_intel(void)
{
	struct microcode_intel *p;
	struct ucode_cpu_info uci;

	collect_cpu_info_early(&uci);

	p = find_patch(&uci);
	if (!p)
		return;

	uci.mc = p;

	apply_microcode_early(&uci, false);
}

static int collect_cpu_info(int cpu_num, struct cpu_signature *csig)
{
	static struct cpu_signature prev;
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

	/* No extra locking on prev, races are harmless. */
	if (csig->sig != prev.sig || csig->pf != prev.pf || csig->rev != prev.rev) {
		pr_info("sig=0x%x, pf=0x%x, revision=0x%x\n",
			csig->sig, csig->pf, csig->rev);
		prev = *csig;
	}

	return 0;
}

static enum ucode_state apply_microcode_intel(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct microcode_intel *mc;
	enum ucode_state ret;
	static int prev_rev;
	u32 rev;

	/* We should bind the task to the CPU */
	if (WARN_ON(raw_smp_processor_id() != cpu))
		return UCODE_ERROR;

	/* Look for a newer patch in our cache: */
	mc = find_patch(uci);
	if (!mc) {
		mc = uci->mc;
		if (!mc)
			return UCODE_NFOUND;
	}

	/*
	 * Save us the MSR write below - which is a particular expensive
	 * operation - when the other hyperthread has updated the microcode
	 * already.
	 */
	rev = intel_get_microcode_revision();
	if (rev >= mc->hdr.rev) {
		ret = UCODE_OK;
		goto out;
	}

	/*
	 * Writeback and invalidate caches before updating microcode to avoid
	 * internal issues depending on what the microcode is updating.
	 */
	native_wbinvd();

	/* write microcode via MSR 0x79 */
	wrmsrl(MSR_IA32_UCODE_WRITE, (unsigned long)mc->bits);

	rev = intel_get_microcode_revision();

	if (rev != mc->hdr.rev) {
		pr_err("CPU%d update to revision 0x%x failed\n",
		       cpu, mc->hdr.rev);
		return UCODE_ERROR;
	}

	if (rev != prev_rev) {
		pr_info("updated to revision 0x%x, date = %04x-%02x-%02x\n",
			rev,
			mc->hdr.date & 0xffff,
			mc->hdr.date >> 24,
			(mc->hdr.date >> 16) & 0xff);
		prev_rev = rev;
	}

	ret = UCODE_UPDATED;

out:
	uci->cpu_sig.rev = rev;
	c->microcode	 = rev;

	/* Update boot_cpu_data's revision too, if we're on the BSP: */
	if (c->cpu_index == boot_cpu_data.cpu_index)
		boot_cpu_data.microcode = rev;

	return ret;
}

static enum ucode_state generic_load_microcode(int cpu, struct iov_iter *iter)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	unsigned int curr_mc_size = 0, new_mc_size = 0;
	enum ucode_state ret = UCODE_OK;
	int new_rev = uci->cpu_sig.rev;
	u8 *new_mc = NULL, *mc = NULL;
	unsigned int csig, cpf;

	while (iov_iter_count(iter)) {
		struct microcode_header_intel mc_header;
		unsigned int mc_size, data_size;
		u8 *data;

		if (!copy_from_iter_full(&mc_header, sizeof(mc_header), iter)) {
			pr_err("error! Truncated or inaccessible header in microcode data file\n");
			break;
		}

		mc_size = get_totalsize(&mc_header);
		if (mc_size < sizeof(mc_header)) {
			pr_err("error! Bad data in microcode data file (totalsize too small)\n");
			break;
		}
		data_size = mc_size - sizeof(mc_header);
		if (data_size > iov_iter_count(iter)) {
			pr_err("error! Bad data in microcode data file (truncated file?)\n");
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

		memcpy(mc, &mc_header, sizeof(mc_header));
		data = mc + sizeof(mc_header);
		if (!copy_from_iter_full(data, data_size, iter) ||
		    microcode_sanity_check(mc, 1) < 0) {
			break;
		}

		csig = uci->cpu_sig.sig;
		cpf = uci->cpu_sig.pf;
		if (has_newer_microcode(mc, csig, cpf, new_rev)) {
			vfree(new_mc);
			new_rev = mc_header.rev;
			new_mc  = mc;
			new_mc_size = mc_size;
			mc = NULL;	/* trigger new vmalloc */
			ret = UCODE_NEW;
		}
	}

	vfree(mc);

	if (iov_iter_count(iter)) {
		vfree(new_mc);
		return UCODE_ERROR;
	}

	if (!new_mc)
		return UCODE_NFOUND;

	vfree(uci->mc);
	uci->mc = (struct microcode_intel *)new_mc;

	/*
	 * If early loading microcode is supported, save this mc into
	 * permanent memory. So it will be loaded early when a CPU is hot added
	 * or resumes.
	 */
	save_mc_for_early(new_mc, new_mc_size);

	pr_debug("CPU%d found a matching microcode update with version 0x%x (current=0x%x)\n",
		 cpu, new_rev, uci->cpu_sig.rev);

	return ret;
}

static bool is_blacklisted(unsigned int cpu)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	/*
	 * Late loading on model 79 with microcode revision less than 0x0b000021
	 * and LLC size per core bigger than 2.5MB may result in a system hang.
	 * This behavior is documented in item BDF90, #334165 (Intel Xeon
	 * Processor E7-8800/4800 v4 Product Family).
	 */
	if (c->x86 == 6 &&
	    c->x86_model == INTEL_FAM6_BROADWELL_X &&
	    c->x86_stepping == 0x01 &&
	    llc_size_per_core > 2621440 &&
	    c->microcode < 0x0b000021) {
		pr_err_once("Erratum BDF90: late loading with revision < 0x0b000021 (0x%x) disabled.\n", c->microcode);
		pr_err_once("Please consider either early loading through initrd/built-in or a potential BIOS update.\n");
		return true;
	}

	return false;
}

static enum ucode_state request_microcode_fw(int cpu, struct device *device,
					     bool refresh_fw)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	const struct firmware *firmware;
	struct iov_iter iter;
	enum ucode_state ret;
	struct kvec kvec;
	char name[30];

	if (is_blacklisted(cpu))
		return UCODE_NFOUND;

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_stepping);

	if (request_firmware_direct(&firmware, name, device)) {
		pr_debug("data file %s load failed\n", name);
		return UCODE_NFOUND;
	}

	kvec.iov_base = (void *)firmware->data;
	kvec.iov_len = firmware->size;
	iov_iter_kvec(&iter, WRITE, &kvec, 1, firmware->size);
	ret = generic_load_microcode(cpu, &iter);

	release_firmware(firmware);

	return ret;
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	struct iov_iter iter;
	struct iovec iov;

	if (is_blacklisted(cpu))
		return UCODE_NFOUND;

	iov.iov_base = (void __user *)buf;
	iov.iov_len = size;
	iov_iter_init(&iter, WRITE, &iov, 1, size);

	return generic_load_microcode(cpu, &iter);
}

static struct microcode_ops microcode_intel_ops = {
	.request_microcode_user		  = request_microcode_user,
	.request_microcode_fw             = request_microcode_fw,
	.collect_cpu_info                 = collect_cpu_info,
	.apply_microcode                  = apply_microcode_intel,
};

static int __init calc_llc_size_per_core(struct cpuinfo_x86 *c)
{
	u64 llc_size = c->x86_cache_size * 1024ULL;

	do_div(llc_size, c->x86_max_cores);

	return (int)llc_size;
}

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	llc_size_per_core = calc_llc_size_per_core(c);

	return &microcode_intel_ops;
}
