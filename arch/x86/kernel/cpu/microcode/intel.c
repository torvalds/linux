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
#include <linux/pci_ids.h>
#include <linux/uaccess.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/uio.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/cpu_device_id.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
#include <asm/msr.h>

#include "internal.h"

static const char ucode_path[] = "kernel/x86/microcode/GenuineIntel.bin";

#define UCODE_BSP_LOADED	((struct microcode_intel *)0x1UL)

/* Defines for the microcode staging mailbox interface */
#define MBOX_REG_NUM		4
#define MBOX_REG_SIZE		sizeof(u32)

#define MBOX_CONTROL_OFFSET	0x0
#define MBOX_STATUS_OFFSET	0x4
#define MBOX_WRDATA_OFFSET	0x8
#define MBOX_RDDATA_OFFSET	0xc

#define MASK_MBOX_CTRL_ABORT	BIT(0)
#define MASK_MBOX_CTRL_GO	BIT(31)

#define MASK_MBOX_STATUS_ERROR	BIT(2)
#define MASK_MBOX_STATUS_READY	BIT(31)

#define MASK_MBOX_RESP_SUCCESS	BIT(0)
#define MASK_MBOX_RESP_PROGRESS	BIT(1)
#define MASK_MBOX_RESP_ERROR	BIT(2)

#define MBOX_CMD_LOAD		0x3
#define MBOX_OBJ_STAGING	0xb
#define MBOX_HEADER(size)	((PCI_VENDOR_ID_INTEL)    | \
				 (MBOX_OBJ_STAGING << 16) | \
				 ((u64)((size) / sizeof(u32)) << 32))

/* The size of each mailbox header */
#define MBOX_HEADER_SIZE	sizeof(u64)
/* The size of staging hardware response */
#define MBOX_RESPONSE_SIZE	sizeof(u64)

#define MBOX_XACTION_TIMEOUT_MS	(10 * MSEC_PER_SEC)

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

/**
 * struct staging_state - Track the current staging process state
 *
 * @mmio_base:		MMIO base address for staging
 * @ucode_len:		Total size of the microcode image
 * @chunk_size:		Size of each data piece
 * @bytes_sent:		Total bytes transmitted so far
 * @offset:		Current offset in the microcode image
 */
struct staging_state {
	void __iomem		*mmio_base;
	unsigned int		ucode_len;
	unsigned int		chunk_size;
	unsigned int		bytes_sent;
	unsigned int		offset;
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

	if (IFM(x86_family(sig->sig), x86_model(sig->sig)) >= INTEL_PENTIUM_III_DESCHUTES) {
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

static inline u32 read_mbox_dword(void __iomem *mmio_base)
{
	u32 dword = readl(mmio_base + MBOX_RDDATA_OFFSET);

	/* Acknowledge read completion to the staging hardware */
	writel(0, mmio_base + MBOX_RDDATA_OFFSET);
	return dword;
}

static inline void write_mbox_dword(void __iomem *mmio_base, u32 dword)
{
	writel(dword, mmio_base + MBOX_WRDATA_OFFSET);
}

static inline u64 read_mbox_header(void __iomem *mmio_base)
{
	u32 high, low;

	low  = read_mbox_dword(mmio_base);
	high = read_mbox_dword(mmio_base);

	return ((u64)high << 32) | low;
}

static inline void write_mbox_header(void __iomem *mmio_base, u64 value)
{
	write_mbox_dword(mmio_base, value);
	write_mbox_dword(mmio_base, value >> 32);
}

static void write_mbox_data(void __iomem *mmio_base, u32 *chunk, unsigned int chunk_bytes)
{
	int i;

	/*
	 * The MMIO space is mapped as Uncached (UC). Each write arrives
	 * at the device as an individual transaction in program order.
	 * The device can then reassemble the sequence accordingly.
	 */
	for (i = 0; i < chunk_bytes / sizeof(u32); i++)
		write_mbox_dword(mmio_base, chunk[i]);
}

/*
 * Prepare for a new microcode transfer: reset hardware and record the
 * image size.
 */
static void init_stage(struct staging_state *ss)
{
	ss->ucode_len = get_totalsize(&ucode_patch_late->hdr);

	/*
	 * Abort any ongoing process, effectively resetting the device.
	 * Unlike regular mailbox data processing requests, this
	 * operation does not require a status check.
	 */
	writel(MASK_MBOX_CTRL_ABORT, ss->mmio_base + MBOX_CONTROL_OFFSET);
}

/*
 * Update the chunk size and decide whether another chunk can be sent.
 * This accounts for remaining data and retry limits.
 */
static bool can_send_next_chunk(struct staging_state *ss, int *err)
{
	/* A page size or remaining bytes if this is the final chunk */
	ss->chunk_size = min(PAGE_SIZE, ss->ucode_len - ss->offset);

	/*
	 * Each microcode image is divided into chunks, each at most
	 * one page size. A 10-chunk image would typically require 10
	 * transactions.
	 *
	 * However, the hardware managing the mailbox has limited
	 * resources and may not cache the entire image, potentially
	 * requesting the same chunk multiple times.
	 *
	 * To tolerate this behavior, allow up to twice the expected
	 * number of transactions (i.e., a 10-chunk image can take up to
	 * 20 attempts).
	 *
	 * If the number of attempts exceeds this limit, treat it as
	 * exceeding the maximum allowed transfer size.
	 */
	if (ss->bytes_sent + ss->chunk_size > ss->ucode_len * 2) {
		*err = -EMSGSIZE;
		return false;
	}

	*err = 0;
	return true;
}

/*
 * The hardware indicates completion by returning a sentinel end offset.
 */
static inline bool is_end_offset(u32 offset)
{
	return offset == UINT_MAX;
}

/*
 * Determine whether staging is complete: either the hardware signaled
 * the end offset, or no more transactions are permitted (retry limit
 * reached).
 */
static inline bool staging_is_complete(struct staging_state *ss, int *err)
{
	return is_end_offset(ss->offset) || !can_send_next_chunk(ss, err);
}

/*
 * Wait for the hardware to complete a transaction.
 * Return 0 on success, or an error code on failure.
 */
static int wait_for_transaction(struct staging_state *ss)
{
	u32 timeout, status;

	/* Allow time for hardware to complete the operation: */
	for (timeout = 0; timeout < MBOX_XACTION_TIMEOUT_MS; timeout++) {
		msleep(1);

		status = readl(ss->mmio_base + MBOX_STATUS_OFFSET);
		/* Break out early if the hardware is ready: */
		if (status & MASK_MBOX_STATUS_READY)
			break;
	}

	/* Check for explicit error response */
	if (status & MASK_MBOX_STATUS_ERROR)
		return -EIO;

	/*
	 * Hardware has neither responded to the action nor signaled any
	 * error. Treat this as a timeout.
	 */
	if (!(status & MASK_MBOX_STATUS_READY))
		return -ETIMEDOUT;

	return 0;
}

/*
 * Transmit a chunk of the microcode image to the hardware.
 * Return 0 on success, or an error code on failure.
 */
static int send_data_chunk(struct staging_state *ss, void *ucode_ptr)
{
	u32 *src_chunk = ucode_ptr + ss->offset;
	u16 mbox_size;

	/*
	 * Write a 'request' mailbox object in this order:
	 *  1. Mailbox header includes total size
	 *  2. Command header specifies the load operation
	 *  3. Data section contains a microcode chunk
	 *
	 * Thus, the mailbox size is two headers plus the chunk size.
	 */
	mbox_size = MBOX_HEADER_SIZE * 2 + ss->chunk_size;
	write_mbox_header(ss->mmio_base, MBOX_HEADER(mbox_size));
	write_mbox_header(ss->mmio_base, MBOX_CMD_LOAD);
	write_mbox_data(ss->mmio_base, src_chunk, ss->chunk_size);
	ss->bytes_sent += ss->chunk_size;

	/* Notify the hardware that the mailbox is ready for processing. */
	writel(MASK_MBOX_CTRL_GO, ss->mmio_base + MBOX_CONTROL_OFFSET);

	return wait_for_transaction(ss);
}

/*
 * Retrieve the next offset from the hardware response.
 * Return 0 on success, or an error code on failure.
 */
static int fetch_next_offset(struct staging_state *ss)
{
	const u64 expected_header = MBOX_HEADER(MBOX_HEADER_SIZE + MBOX_RESPONSE_SIZE);
	u32 offset, status;
	u64 header;

	/*
	 * The 'response' mailbox returns three fields, in order:
	 *  1. Header
	 *  2. Next offset in the microcode image
	 *  3. Status flags
	 */
	header = read_mbox_header(ss->mmio_base);
	offset = read_mbox_dword(ss->mmio_base);
	status = read_mbox_dword(ss->mmio_base);

	/* All valid responses must start with the expected header. */
	if (header != expected_header) {
		pr_err_once("staging: invalid response header (0x%llx)\n", header);
		return -EBADR;
	}

	/*
	 * Verify the offset: If not at the end marker, it must not
	 * exceed the microcode image length.
	 */
	if (!is_end_offset(offset) && offset > ss->ucode_len) {
		pr_err_once("staging: invalid offset (%u) past the image end (%u)\n",
			    offset, ss->ucode_len);
		return -EINVAL;
	}

	/* Hardware may report errors explicitly in the status field */
	if (status & MASK_MBOX_RESP_ERROR)
		return -EPROTO;

	ss->offset = offset;
	return 0;
}

/*
 * Handle the staging process using the mailbox MMIO interface. The
 * microcode image is transferred in chunks until completion.
 * Return 0 on success or an error code on failure.
 */
static int do_stage(u64 mmio_pa)
{
	struct staging_state ss = {};
	int err;

	ss.mmio_base = ioremap(mmio_pa, MBOX_REG_NUM * MBOX_REG_SIZE);
	if (WARN_ON_ONCE(!ss.mmio_base))
		return -EADDRNOTAVAIL;

	init_stage(&ss);

	/* Perform the staging process while within the retry limit */
	while (!staging_is_complete(&ss, &err)) {
		/* Send a chunk of microcode each time: */
		err = send_data_chunk(&ss, ucode_patch_late);
		if (err)
			break;
		/*
		 * Then, ask the hardware which piece of the image it
		 * needs next. The same piece may be sent more than once.
		 */
		err = fetch_next_offset(&ss);
		if (err)
			break;
	}

	iounmap(ss.mmio_base);

	return err;
}

static void stage_microcode(void)
{
	unsigned int pkg_id = UINT_MAX;
	int cpu, err;
	u64 mmio_pa;

	if (!IS_ALIGNED(get_totalsize(&ucode_patch_late->hdr), sizeof(u32))) {
		pr_err("Microcode image 32-bit misaligned (0x%x), staging failed.\n",
			get_totalsize(&ucode_patch_late->hdr));
		return;
	}

	lockdep_assert_cpus_held();

	/*
	 * The MMIO address is unique per package, and all the SMT
	 * primary threads are online here. Find each MMIO space by
	 * their package IDs to avoid duplicate staging.
	 */
	for_each_cpu(cpu, cpu_primary_thread_mask) {
		if (topology_logical_package_id(cpu) == pkg_id)
			continue;

		pkg_id = topology_logical_package_id(cpu);

		err = rdmsrq_on_cpu(cpu, MSR_IA32_MCU_STAGING_MBOX_ADDR, &mmio_pa);
		if (WARN_ON_ONCE(err))
			return;

		err = do_stage(mmio_pa);
		if (err) {
			pr_err("Error: staging failed (%d) for CPU%d at package %u.\n",
			       err, cpu, pkg_id);
			return;
		}
	}

	pr_info("Staging of patch revision 0x%x succeeded.\n", ucode_patch_late->hdr.rev);
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

	/* write microcode via MSR 0x79 */
	native_wrmsrq(MSR_IA32_UCODE_WRITE, (unsigned long)mc->bits);

	rev = intel_get_microcode_revision();
	if (rev != mc->hdr.rev)
		return UCODE_ERROR;

	uci->cpu_sig.rev = rev;
	return UCODE_UPDATED;
}

static enum ucode_state apply_microcode_early(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc = uci->mc;
	u32 cur_rev;

	return __apply_microcode(uci, mc, &cur_rev);
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

	intel_collect_cpu_info(&uci->cpu_sig);

	if (!load_builtin_intel_microcode(&cp))
		cp = find_microcode_in_initrd(ucode_path);

	if (!(cp.data && cp.size))
		return NULL;

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

	if (microcode_loader_disabled() || boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	uci.mc = get_microcode_blob(&uci, true);
	if (uci.mc)
		save_microcode_patch(uci.mc);
	return 0;
}
early_initcall(save_builtin_microcode);

/* Load microcode on BSP from initrd or builtin blobs */
void __init load_ucode_intel_bsp(struct early_load_data *ed)
{
	struct ucode_cpu_info uci;

	uci.mc = get_microcode_blob(&uci, false);
	ed->old_rev = uci.cpu_sig.rev;

	if (uci.mc && apply_microcode_early(&uci) == UCODE_UPDATED) {
		ucode_patch_va = UCODE_BSP_LOADED;
		ed->new_rev = uci.cpu_sig.rev;
	}
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

	cpu_data(cpu).microcode	 = uci->cpu_sig.rev;
	if (!cpu)
		boot_cpu_data.microcode = uci->cpu_sig.rev;

	return ret;
}

static bool ucode_validate_minrev(struct microcode_header_intel *mc_header)
{
	int cur_rev = boot_cpu_data.microcode;

	/*
	 * When late-loading, ensure the header declares a minimum revision
	 * required to perform a late-load. The previously reserved field
	 * is 0 in older microcode blobs.
	 */
	if (!mc_header->min_req_ver) {
		pr_info("Unsafe microcode update: Microcode header does not specify a required min version\n");
		return false;
	}

	/*
	 * Check whether the current revision is either greater or equal to
	 * to the minimum revision specified in the header.
	 */
	if (cur_rev < mc_header->min_req_ver) {
		pr_info("Unsafe microcode update: Current revision 0x%x too old\n", cur_rev);
		pr_info("Current should be at 0x%x or higher. Use early loading instead\n", mc_header->min_req_ver);
		return false;
	}
	return true;
}

static enum ucode_state parse_microcode_blobs(int cpu, struct iov_iter *iter)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	bool is_safe, new_is_safe = false;
	int cur_rev = uci->cpu_sig.rev;
	unsigned int curr_mc_size = 0;
	u8 *new_mc = NULL, *mc = NULL;

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

		is_safe = ucode_validate_minrev(&mc_header);
		if (force_minrev && !is_safe)
			continue;

		kvfree(new_mc);
		cur_rev = mc_header.rev;
		new_mc  = mc;
		new_is_safe = is_safe;
		mc = NULL;
	}

	if (iov_iter_count(iter))
		goto fail;

	kvfree(mc);
	if (!new_mc)
		return UCODE_NFOUND;

	ucode_patch_late = (struct microcode_intel *)new_mc;
	return new_is_safe ? UCODE_NEW_SAFE : UCODE_NEW;

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
	 * This behavior is documented in item BDX90, #334165 (Intel Xeon
	 * Processor E7-8800/4800 v4 Product Family).
	 */
	if (c->x86_vfm == INTEL_BROADWELL_X &&
	    c->x86_stepping == 0x01 &&
	    llc_size_per_core > 2621440 &&
	    c->microcode < 0x0b000021) {
		pr_err_once("Erratum BDX90: late loading with revision < 0x0b000021 (0x%x) disabled.\n", c->microcode);
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
	.stage_microcode	= stage_microcode,
	.use_nmi		= IS_ENABLED(CONFIG_X86_64),
};

static __init void calc_llc_size_per_core(struct cpuinfo_x86 *c)
{
	u64 llc_size = c->x86_cache_size * 1024ULL;

	do_div(llc_size, topology_num_cores_per_package());
	llc_size_per_core = (unsigned int)llc_size;
}

static __init bool staging_available(void)
{
	u64 val;

	val = x86_read_arch_cap_msr();
	if (!(val & ARCH_CAP_MCU_ENUM))
		return false;

	rdmsrq(MSR_IA32_MCU_ENUMERATION, val);
	return !!(val & MCU_STAGING);
}

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	if (staging_available()) {
		microcode_intel_ops.use_staging = true;
		pr_info("Enabled staging feature.\n");
	}

	calc_llc_size_per_core(c);

	return &microcode_intel_ops;
}
