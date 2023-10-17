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
#define pr_fmt(fmt) "microcode: " fmt
#include <linux/earlycpio.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/uio.h>
#include <linux/mm.h>

#include <asm/intel-family.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
#include <asm/msr.h>

#include "internal.h"

static const char ucode_path[] = "kernel/x86/microcode/GenuineIntel.bin";

#define UCODE_BSP_LOADED	((struct microcode_intel *)0x1UL)

/* Current microcode patch used in early patching on the APs. */
static struct microcode_intel *ucode_patch_va __read_mostly;
static struct microcode_intel *ucode_patch_late __read_mostly;

/* last level cache size per core */
static unsigned int llc_size_per_core __ro_after_init;

/* microcode format is extended from prescott processors */
struct extended_signature {
	unsigned int	sig;
	unsigned int	pf;
	unsigned int	cksum;
};

struct extended_sigtable {
	unsigned int			count;
	unsigned int			cksum;
	unsigned int			reserved[3];
	struct extended_signature	sigs[];
};

#define DEFAULT_UCODE_TOTALSIZE (DEFAULT_UCODE_DATASIZE + MC_HEADER_SIZE)
#define EXT_HEADER_SIZE		(sizeof(struct extended_sigtable))
#define EXT_SIGNATURE_SIZE	(sizeof(struct extended_signature))

static inline unsigned int get_totalsize(struct microcode_header_intel *hdr)
{
	return hdr->datasize ? hdr->totalsize : DEFAULT_UCODE_TOTALSIZE;
}

static inline unsigned int exttable_size(struct extended_sigtable *et)
{
	return et->count * EXT_SIGNATURE_SIZE + EXT_HEADER_SIZE;
}

void intel_collect_cpu_info(struct cpu_signature *sig)
{
	sig->sig = cpuid_eax(1);
	sig->pf = 0;
	sig->rev = intel_get_microcode_revision();

	if (x86_model(sig->sig) >= 5 || x86_family(sig->sig) > 6) {
		unsigned int val[2];

		/* get processor flags from MSR 0x17 */
		native_rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		sig->pf = 1 << ((val[1] >> 18) & 7);
	}
}
EXPORT_SYMBOL_GPL(intel_collect_cpu_info);

static inline bool cpu_signatures_match(struct cpu_signature *s1, unsigned int sig2,
					unsigned int pf2)
{
	if (s1->sig != sig2)
		return false;

	/* Processor flags are either both 0 or they intersect. */
	return ((!s1->pf && !pf2) || (s1->pf & pf2));
}

bool intel_find_matching_signature(void *mc, struct cpu_signature *sig)
{
	struct microcode_header_intel *mc_hdr = mc;
	struct extended_signature *ext_sig;
	struct extended_sigtable *ext_hdr;
	int i;

	if (cpu_signatures_match(sig, mc_hdr->sig, mc_hdr->pf))
		return true;

	/* Look for ext. headers: */
	if (get_totalsize(mc_hdr) <= intel_microcode_get_datasize(mc_hdr) + MC_HEADER_SIZE)
		return false;

	ext_hdr = mc + intel_microcode_get_datasize(mc_hdr) + MC_HEADER_SIZE;
	ext_sig = (void *)ext_hdr + EXT_HEADER_SIZE;

	for (i = 0; i < ext_hdr->count; i++) {
		if (cpu_signatures_match(sig, ext_sig->sig, ext_sig->pf))
			return true;
		ext_sig++;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(intel_find_matching_signature);

/**
 * intel_microcode_sanity_check() - Sanity check microcode file.
 * @mc: Pointer to the microcode file contents.
 * @print_err: Display failure reason if true, silent if false.
 * @hdr_type: Type of file, i.e. normal microcode file or In Field Scan file.
 *            Validate if the microcode header type matches with the type
 *            specified here.
 *
 * Validate certain header fields and verify if computed checksum matches
 * with the one specified in the header.
 *
 * Return: 0 if the file passes all the checks, -EINVAL if any of the checks
 * fail.
 */
int intel_microcode_sanity_check(void *mc, bool print_err, int hdr_type)
{
	unsigned long total_size, data_size, ext_table_size;
	struct microcode_header_intel *mc_header = mc;
	struct extended_sigtable *ext_header = NULL;
	u32 sum, orig_sum, ext_sigcount = 0, i;
	struct extended_signature *ext_sig;

	total_size = get_totalsize(mc_header);
	data_size = intel_microcode_get_datasize(mc_header);

	if (data_size + MC_HEADER_SIZE > total_size) {
		if (print_err)
			pr_err("Error: bad microcode data file size.\n");
		return -EINVAL;
	}

	if (mc_header->ldrver != 1 || mc_header->hdrver != hdr_type) {
		if (print_err)
			pr_err("Error: invalid/unknown microcode update format. Header type %d\n",
			       mc_header->hdrver);
		return -EINVAL;
	}

	ext_table_size = total_size - (MC_HEADER_SIZE + data_size);
	if (ext_table_size) {
		u32 ext_table_sum = 0;
		u32 *ext_tablep;

		if (ext_table_size < EXT_HEADER_SIZE ||
		    ((ext_table_size - EXT_HEADER_SIZE) % EXT_SIGNATURE_SIZE)) {
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
EXPORT_SYMBOL_GPL(intel_microcode_sanity_check);

static void update_ucode_pointer(struct microcode_intel *mc)
{
	kvfree(ucode_patch_va);

	/*
	 * Save the virtual address for early loading and for eventual free
	 * on late loading.
	 */
	ucode_patch_va = mc;
}

static void save_microcode_patch(struct microcode_intel *patch)
{
	unsigned int size = get_totalsize(&patch->hdr);
	struct microcode_intel *mc;

	mc = kvmemdup(patch, size, GFP_KERNEL);
	if (mc)
		update_ucode_pointer(mc);
	else
		pr_err("Unable to allocate microcode memory size: %u\n", size);
}

/* Scan blob for microcode matching the boot CPUs family, model, stepping */
static __init struct microcode_intel *scan_microcode(void *data, size_t size,
						     struct ucode_cpu_info *uci,
						     bool save)
{
	struct microcode_header_intel *mc_header;
	struct microcode_intel *patch = NULL;
	u32 cur_rev = uci->cpu_sig.rev;
	unsigned int mc_size;

	for (; size >= sizeof(struct microcode_header_intel); size -= mc_size, data += mc_size) {
		mc_header = (struct microcode_header_intel *)data;

		mc_size = get_totalsize(mc_header);
		if (!mc_size || mc_size > size ||
		    intel_microcode_sanity_check(data, false, MC_HEADER_TYPE_MICROCODE) < 0)
			break;

		if (!intel_find_matching_signature(data, &uci->cpu_sig))
			continue;

		/*
		 * For saving the early microcode, find the matching revision which
		 * was loaded on the BSP.
		 *
		 * On the BSP during early boot, find a newer revision than
		 * actually loaded in the CPU.
		 */
		if (save) {
			if (cur_rev != mc_header->rev)
				continue;
		} else if (cur_rev >= mc_header->rev) {
			continue;
		}

		patch = data;
		cur_rev = mc_header->rev;
	}

	return size ? NULL : patch;
}

static enum ucode_state __apply_microcode(struct ucode_cpu_info *uci,
					  struct microcode_intel *mc,
					  u32 *cur_rev)
{
	u32 rev;

	if (!mc)
		return UCODE_NFOUND;

	/*
	 * Save us the MSR write below - which is a particular expensive
	 * operation - when the other hyperthread has updated the microcode
	 * already.
	 */
	*cur_rev = intel_get_microcode_revision();
	if (*cur_rev >= mc->hdr.rev) {
		uci->cpu_sig.rev = *cur_rev;
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
		return UCODE_ERROR;

	uci->cpu_sig.rev = rev;
	return UCODE_UPDATED;
}

static enum ucode_state apply_microcode_early(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc = uci->mc;
	enum ucode_state ret;
	u32 cur_rev, date;

	ret = __apply_microcode(uci, mc, &cur_rev);
	if (ret == UCODE_UPDATED) {
		date = mc->hdr.date;
		pr_info_once("updated early: 0x%x -> 0x%x, date = %04x-%02x-%02x\n",
			     cur_rev, mc->hdr.rev, date & 0xffff, date >> 24, (date >> 16) & 0xff);
	}
	return ret;
}

static __init bool load_builtin_intel_microcode(struct cpio_data *cp)
{
	unsigned int eax = 1, ebx, ecx = 0, edx;
	struct firmware fw;
	char name[30];

	if (IS_ENABLED(CONFIG_X86_32))
		return false;

	native_cpuid(&eax, &ebx, &ecx, &edx);

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		x86_family(eax), x86_model(eax), x86_stepping(eax));

	if (firmware_request_builtin(&fw, name)) {
		cp->size = fw.size;
		cp->data = (void *)fw.data;
		return true;
	}
	return false;
}

static __init struct microcode_intel *get_microcode_blob(struct ucode_cpu_info *uci, bool save)
{
	struct cpio_data cp;

	if (!load_builtin_intel_microcode(&cp))
		cp = find_microcode_in_initrd(ucode_path);

	if (!(cp.data && cp.size))
		return NULL;

	intel_collect_cpu_info(&uci->cpu_sig);

	return scan_microcode(cp.data, cp.size, uci, save);
}

/*
 * Invoked from an early init call to save the microcode blob which was
 * selected during early boot when mm was not usable. The microcode must be
 * saved because initrd is going away. It's an early init call so the APs
 * just can use the pointer and do not have to scan initrd/builtin firmware
 * again.
 */
static int __init save_builtin_microcode(void)
{
	struct ucode_cpu_info uci;

	if (xchg(&ucode_patch_va, NULL) != UCODE_BSP_LOADED)
		return 0;

	if (dis_ucode_ldr || boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	uci.mc = get_microcode_blob(&uci, true);
	if (uci.mc)
		save_microcode_patch(uci.mc);
	return 0;
}
early_initcall(save_builtin_microcode);

/* Load microcode on BSP from initrd or builtin blobs */
void __init load_ucode_intel_bsp(void)
{
	struct ucode_cpu_info uci;

	uci.mc = get_microcode_blob(&uci, false);
	if (uci.mc && apply_microcode_early(&uci) == UCODE_UPDATED)
		ucode_patch_va = UCODE_BSP_LOADED;
}

void load_ucode_intel_ap(void)
{
	struct ucode_cpu_info uci;

	uci.mc = ucode_patch_va;
	if (uci.mc)
		apply_microcode_early(&uci);
}

/* Reload microcode on resume */
void reload_ucode_intel(void)
{
	struct ucode_cpu_info uci = { .mc = ucode_patch_va, };

	if (uci.mc)
		apply_microcode_early(&uci);
}

static int collect_cpu_info(int cpu_num, struct cpu_signature *csig)
{
	intel_collect_cpu_info(csig);
	return 0;
}

static enum ucode_state apply_microcode_late(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	struct microcode_intel *mc = ucode_patch_late;
	enum ucode_state ret;
	u32 cur_rev;

	if (WARN_ON_ONCE(smp_processor_id() != cpu))
		return UCODE_ERROR;

	ret = __apply_microcode(uci, mc, &cur_rev);
	if (ret != UCODE_UPDATED && ret != UCODE_OK)
		return ret;

	if (!cpu && uci->cpu_sig.rev != cur_rev) {
		pr_info("Updated to revision 0x%x, date = %04x-%02x-%02x\n",
			uci->cpu_sig.rev, mc->hdr.date & 0xffff, mc->hdr.date >> 24,
			(mc->hdr.date >> 16) & 0xff);
	}

	cpu_data(cpu).microcode	 = uci->cpu_sig.rev;
	if (!cpu)
		boot_cpu_data.microcode = uci->cpu_sig.rev;

	return ret;
}

static enum ucode_state parse_microcode_blobs(int cpu, struct iov_iter *iter)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	int cur_rev = uci->cpu_sig.rev;
	unsigned int curr_mc_size = 0;
	u8 *new_mc = NULL, *mc = NULL;

	if (force_minrev)
		return UCODE_NFOUND;

	while (iov_iter_count(iter)) {
		struct microcode_header_intel mc_header;
		unsigned int mc_size, data_size;
		u8 *data;

		if (!copy_from_iter_full(&mc_header, sizeof(mc_header), iter)) {
			pr_err("error! Truncated or inaccessible header in microcode data file\n");
			goto fail;
		}

		mc_size = get_totalsize(&mc_header);
		if (mc_size < sizeof(mc_header)) {
			pr_err("error! Bad data in microcode data file (totalsize too small)\n");
			goto fail;
		}
		data_size = mc_size - sizeof(mc_header);
		if (data_size > iov_iter_count(iter)) {
			pr_err("error! Bad data in microcode data file (truncated file?)\n");
			goto fail;
		}

		/* For performance reasons, reuse mc area when possible */
		if (!mc || mc_size > curr_mc_size) {
			kvfree(mc);
			mc = kvmalloc(mc_size, GFP_KERNEL);
			if (!mc)
				goto fail;
			curr_mc_size = mc_size;
		}

		memcpy(mc, &mc_header, sizeof(mc_header));
		data = mc + sizeof(mc_header);
		if (!copy_from_iter_full(data, data_size, iter) ||
		    intel_microcode_sanity_check(mc, true, MC_HEADER_TYPE_MICROCODE) < 0)
			goto fail;

		if (cur_rev >= mc_header.rev)
			continue;

		if (!intel_find_matching_signature(mc, &uci->cpu_sig))
			continue;

		kvfree(new_mc);
		cur_rev = mc_header.rev;
		new_mc  = mc;
		mc = NULL;
	}

	if (iov_iter_count(iter))
		goto fail;

	kvfree(mc);
	if (!new_mc)
		return UCODE_NFOUND;

	ucode_patch_late = (struct microcode_intel *)new_mc;
	return UCODE_NEW;

fail:
	kvfree(mc);
	kvfree(new_mc);
	return UCODE_ERROR;
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

static enum ucode_state request_microcode_fw(int cpu, struct device *device)
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
	iov_iter_kvec(&iter, ITER_SOURCE, &kvec, 1, firmware->size);
	ret = parse_microcode_blobs(cpu, &iter);

	release_firmware(firmware);

	return ret;
}

static void finalize_late_load(int result)
{
	if (!result)
		update_ucode_pointer(ucode_patch_late);
	else
		kvfree(ucode_patch_late);
	ucode_patch_late = NULL;
}

static struct microcode_ops microcode_intel_ops = {
	.request_microcode_fw	= request_microcode_fw,
	.collect_cpu_info	= collect_cpu_info,
	.apply_microcode	= apply_microcode_late,
	.finalize_late_load	= finalize_late_load,
	.use_nmi		= IS_ENABLED(CONFIG_X86_64),
};

static __init void calc_llc_size_per_core(struct cpuinfo_x86 *c)
{
	u64 llc_size = c->x86_cache_size * 1024ULL;

	do_div(llc_size, c->x86_max_cores);
	llc_size_per_core = (unsigned int)llc_size;
}

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	calc_llc_size_per_core(c);

	return &microcode_intel_ops;
}
