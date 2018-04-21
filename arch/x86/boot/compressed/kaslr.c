// SPDX-License-Identifier: GPL-2.0
/*
 * kaslr.c
 *
 * This contains the routines needed to generate a reasonable level of
 * entropy to choose a randomized kernel base address offset in support
 * of Kernel Address Space Layout Randomization (KASLR). Additionally
 * handles walking the physical memory maps (and tracking memory regions
 * to avoid) in order to select a physical memory location that can
 * contain the entire properly aligned running kernel image.
 *
 */

/*
 * isspace() in linux/ctype.h is expected by next_args() to filter
 * out "space/lf/tab". While boot/ctype.h conflicts with linux/ctype.h,
 * since isdigit() is implemented in both of them. Hence disable it
 * here.
 */
#define BOOT_CTYPE_H

/*
 * _ctype[] in lib/ctype.c is needed by isspace() of linux/ctype.h.
 * While both lib/ctype.c and lib/cmdline.c will bring EXPORT_SYMBOL
 * which is meaningless and will cause compiling error in some cases.
 * So do not include linux/export.h and define EXPORT_SYMBOL(sym)
 * as empty.
 */
#define _LINUX_EXPORT_H
#define EXPORT_SYMBOL(sym)

#include "misc.h"
#include "error.h"
#include "../string.h"

#include <generated/compile.h>
#include <linux/module.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/ctype.h>
#include <linux/efi.h>
#include <generated/utsrelease.h>
#include <asm/efi.h>

/* Macros used by the included decompressor code below. */
#define STATIC
#include <linux/decompress/mm.h>

#ifdef CONFIG_X86_5LEVEL
unsigned int pgtable_l5_enabled __ro_after_init;
unsigned int pgdir_shift __ro_after_init = 39;
unsigned int ptrs_per_p4d __ro_after_init = 1;
#endif

extern unsigned long get_cmd_line_ptr(void);

/* Used by PAGE_KERN* macros: */
pteval_t __default_kernel_pte_mask __read_mostly = ~0;

/* Simplified build-specific string for starting entropy. */
static const char build_str[] = UTS_RELEASE " (" LINUX_COMPILE_BY "@"
		LINUX_COMPILE_HOST ") (" LINUX_COMPILER ") " UTS_VERSION;

static unsigned long rotate_xor(unsigned long hash, const void *area,
				size_t size)
{
	size_t i;
	unsigned long *ptr = (unsigned long *)area;

	for (i = 0; i < size / sizeof(hash); i++) {
		/* Rotate by odd number of bits and XOR. */
		hash = (hash << ((sizeof(hash) * 8) - 7)) | (hash >> 7);
		hash ^= ptr[i];
	}

	return hash;
}

/* Attempt to create a simple but unpredictable starting entropy. */
static unsigned long get_boot_seed(void)
{
	unsigned long hash = 0;

	hash = rotate_xor(hash, build_str, sizeof(build_str));
	hash = rotate_xor(hash, boot_params, sizeof(*boot_params));

	return hash;
}

#define KASLR_COMPRESSED_BOOT
#include "../../lib/kaslr.c"

struct mem_vector {
	unsigned long long start;
	unsigned long long size;
};

/* Only supporting at most 4 unusable memmap regions with kaslr */
#define MAX_MEMMAP_REGIONS	4

static bool memmap_too_large;


/* Store memory limit specified by "mem=nn[KMG]" or "memmap=nn[KMG]" */
unsigned long long mem_limit = ULLONG_MAX;


enum mem_avoid_index {
	MEM_AVOID_ZO_RANGE = 0,
	MEM_AVOID_INITRD,
	MEM_AVOID_CMDLINE,
	MEM_AVOID_BOOTPARAMS,
	MEM_AVOID_MEMMAP_BEGIN,
	MEM_AVOID_MEMMAP_END = MEM_AVOID_MEMMAP_BEGIN + MAX_MEMMAP_REGIONS - 1,
	MEM_AVOID_MAX,
};

static struct mem_vector mem_avoid[MEM_AVOID_MAX];

static bool mem_overlaps(struct mem_vector *one, struct mem_vector *two)
{
	/* Item one is entirely before item two. */
	if (one->start + one->size <= two->start)
		return false;
	/* Item one is entirely after item two. */
	if (one->start >= two->start + two->size)
		return false;
	return true;
}

char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
#include "../../../../lib/ctype.c"
#include "../../../../lib/cmdline.c"

static int
parse_memmap(char *p, unsigned long long *start, unsigned long long *size)
{
	char *oldp;

	if (!p)
		return -EINVAL;

	/* We don't care about this option here */
	if (!strncmp(p, "exactmap", 8))
		return -EINVAL;

	oldp = p;
	*size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	switch (*p) {
	case '#':
	case '$':
	case '!':
		*start = memparse(p + 1, &p);
		return 0;
	case '@':
		/* memmap=nn@ss specifies usable region, should be skipped */
		*size = 0;
		/* Fall through */
	default:
		/*
		 * If w/o offset, only size specified, memmap=nn[KMG] has the
		 * same behaviour as mem=nn[KMG]. It limits the max address
		 * system can use. Region above the limit should be avoided.
		 */
		*start = 0;
		return 0;
	}

	return -EINVAL;
}

static void mem_avoid_memmap(char *str)
{
	static int i;

	if (i >= MAX_MEMMAP_REGIONS)
		return;

	while (str && (i < MAX_MEMMAP_REGIONS)) {
		int rc;
		unsigned long long start, size;
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		rc = parse_memmap(str, &start, &size);
		if (rc < 0)
			break;
		str = k;

		if (start == 0) {
			/* Store the specified memory limit if size > 0 */
			if (size > 0)
				mem_limit = size;

			continue;
		}

		mem_avoid[MEM_AVOID_MEMMAP_BEGIN + i].start = start;
		mem_avoid[MEM_AVOID_MEMMAP_BEGIN + i].size = size;
		i++;
	}

	/* More than 4 memmaps, fail kaslr */
	if ((i >= MAX_MEMMAP_REGIONS) && str)
		memmap_too_large = true;
}

static int handle_mem_memmap(void)
{
	char *args = (char *)get_cmd_line_ptr();
	size_t len = strlen((char *)args);
	char *tmp_cmdline;
	char *param, *val;
	u64 mem_size;

	if (!strstr(args, "memmap=") && !strstr(args, "mem="))
		return 0;

	tmp_cmdline = malloc(len + 1);
	if (!tmp_cmdline)
		error("Failed to allocate space for tmp_cmdline");

	memcpy(tmp_cmdline, args, len);
	tmp_cmdline[len] = 0;
	args = tmp_cmdline;

	/* Chew leading spaces */
	args = skip_spaces(args);

	while (*args) {
		args = next_arg(args, &param, &val);
		/* Stop at -- */
		if (!val && strcmp(param, "--") == 0) {
			warn("Only '--' specified in cmdline");
			free(tmp_cmdline);
			return -1;
		}

		if (!strcmp(param, "memmap")) {
			mem_avoid_memmap(val);
		} else if (!strcmp(param, "mem")) {
			char *p = val;

			if (!strcmp(p, "nopentium"))
				continue;
			mem_size = memparse(p, &p);
			if (mem_size == 0) {
				free(tmp_cmdline);
				return -EINVAL;
			}
			mem_limit = mem_size;
		}
	}

	free(tmp_cmdline);
	return 0;
}

/*
 * In theory, KASLR can put the kernel anywhere in the range of [16M, 64T).
 * The mem_avoid array is used to store the ranges that need to be avoided
 * when KASLR searches for an appropriate random address. We must avoid any
 * regions that are unsafe to overlap with during decompression, and other
 * things like the initrd, cmdline and boot_params. This comment seeks to
 * explain mem_avoid as clearly as possible since incorrect mem_avoid
 * memory ranges lead to really hard to debug boot failures.
 *
 * The initrd, cmdline, and boot_params are trivial to identify for
 * avoiding. They are MEM_AVOID_INITRD, MEM_AVOID_CMDLINE, and
 * MEM_AVOID_BOOTPARAMS respectively below.
 *
 * What is not obvious how to avoid is the range of memory that is used
 * during decompression (MEM_AVOID_ZO_RANGE below). This range must cover
 * the compressed kernel (ZO) and its run space, which is used to extract
 * the uncompressed kernel (VO) and relocs.
 *
 * ZO's full run size sits against the end of the decompression buffer, so
 * we can calculate where text, data, bss, etc of ZO are positioned more
 * easily.
 *
 * For additional background, the decompression calculations can be found
 * in header.S, and the memory diagram is based on the one found in misc.c.
 *
 * The following conditions are already enforced by the image layouts and
 * associated code:
 *  - input + input_size >= output + output_size
 *  - kernel_total_size <= init_size
 *  - kernel_total_size <= output_size (see Note below)
 *  - output + init_size >= output + output_size
 *
 * (Note that kernel_total_size and output_size have no fundamental
 * relationship, but output_size is passed to choose_random_location
 * as a maximum of the two. The diagram is showing a case where
 * kernel_total_size is larger than output_size, but this case is
 * handled by bumping output_size.)
 *
 * The above conditions can be illustrated by a diagram:
 *
 * 0   output            input            input+input_size    output+init_size
 * |     |                 |                             |             |
 * |     |                 |                             |             |
 * |-----|--------|--------|--------------|-----------|--|-------------|
 *                |                       |           |
 *                |                       |           |
 * output+init_size-ZO_INIT_SIZE  output+output_size  output+kernel_total_size
 *
 * [output, output+init_size) is the entire memory range used for
 * extracting the compressed image.
 *
 * [output, output+kernel_total_size) is the range needed for the
 * uncompressed kernel (VO) and its run size (bss, brk, etc).
 *
 * [output, output+output_size) is VO plus relocs (i.e. the entire
 * uncompressed payload contained by ZO). This is the area of the buffer
 * written to during decompression.
 *
 * [output+init_size-ZO_INIT_SIZE, output+init_size) is the worst-case
 * range of the copied ZO and decompression code. (i.e. the range
 * covered backwards of size ZO_INIT_SIZE, starting from output+init_size.)
 *
 * [input, input+input_size) is the original copied compressed image (ZO)
 * (i.e. it does not include its run size). This range must be avoided
 * because it contains the data used for decompression.
 *
 * [input+input_size, output+init_size) is [_text, _end) for ZO. This
 * range includes ZO's heap and stack, and must be avoided since it
 * performs the decompression.
 *
 * Since the above two ranges need to be avoided and they are adjacent,
 * they can be merged, resulting in: [input, output+init_size) which
 * becomes the MEM_AVOID_ZO_RANGE below.
 */
static void mem_avoid_init(unsigned long input, unsigned long input_size,
			   unsigned long output)
{
	unsigned long init_size = boot_params->hdr.init_size;
	u64 initrd_start, initrd_size;
	u64 cmd_line, cmd_line_size;
	char *ptr;

	/*
	 * Avoid the region that is unsafe to overlap during
	 * decompression.
	 */
	mem_avoid[MEM_AVOID_ZO_RANGE].start = input;
	mem_avoid[MEM_AVOID_ZO_RANGE].size = (output + init_size) - input;
	add_identity_map(mem_avoid[MEM_AVOID_ZO_RANGE].start,
			 mem_avoid[MEM_AVOID_ZO_RANGE].size);

	/* Avoid initrd. */
	initrd_start  = (u64)boot_params->ext_ramdisk_image << 32;
	initrd_start |= boot_params->hdr.ramdisk_image;
	initrd_size  = (u64)boot_params->ext_ramdisk_size << 32;
	initrd_size |= boot_params->hdr.ramdisk_size;
	mem_avoid[MEM_AVOID_INITRD].start = initrd_start;
	mem_avoid[MEM_AVOID_INITRD].size = initrd_size;
	/* No need to set mapping for initrd, it will be handled in VO. */

	/* Avoid kernel command line. */
	cmd_line  = (u64)boot_params->ext_cmd_line_ptr << 32;
	cmd_line |= boot_params->hdr.cmd_line_ptr;
	/* Calculate size of cmd_line. */
	ptr = (char *)(unsigned long)cmd_line;
	for (cmd_line_size = 0; ptr[cmd_line_size++];)
		;
	mem_avoid[MEM_AVOID_CMDLINE].start = cmd_line;
	mem_avoid[MEM_AVOID_CMDLINE].size = cmd_line_size;
	add_identity_map(mem_avoid[MEM_AVOID_CMDLINE].start,
			 mem_avoid[MEM_AVOID_CMDLINE].size);

	/* Avoid boot parameters. */
	mem_avoid[MEM_AVOID_BOOTPARAMS].start = (unsigned long)boot_params;
	mem_avoid[MEM_AVOID_BOOTPARAMS].size = sizeof(*boot_params);
	add_identity_map(mem_avoid[MEM_AVOID_BOOTPARAMS].start,
			 mem_avoid[MEM_AVOID_BOOTPARAMS].size);

	/* We don't need to set a mapping for setup_data. */

	/* Mark the memmap regions we need to avoid */
	handle_mem_memmap();

#ifdef CONFIG_X86_VERBOSE_BOOTUP
	/* Make sure video RAM can be used. */
	add_identity_map(0, PMD_SIZE);
#endif
}

/*
 * Does this memory vector overlap a known avoided area? If so, record the
 * overlap region with the lowest address.
 */
static bool mem_avoid_overlap(struct mem_vector *img,
			      struct mem_vector *overlap)
{
	int i;
	struct setup_data *ptr;
	unsigned long earliest = img->start + img->size;
	bool is_overlapping = false;

	for (i = 0; i < MEM_AVOID_MAX; i++) {
		if (mem_overlaps(img, &mem_avoid[i]) &&
		    mem_avoid[i].start < earliest) {
			*overlap = mem_avoid[i];
			earliest = overlap->start;
			is_overlapping = true;
		}
	}

	/* Avoid all entries in the setup_data linked list. */
	ptr = (struct setup_data *)(unsigned long)boot_params->hdr.setup_data;
	while (ptr) {
		struct mem_vector avoid;

		avoid.start = (unsigned long)ptr;
		avoid.size = sizeof(*ptr) + ptr->len;

		if (mem_overlaps(img, &avoid) && (avoid.start < earliest)) {
			*overlap = avoid;
			earliest = overlap->start;
			is_overlapping = true;
		}

		ptr = (struct setup_data *)(unsigned long)ptr->next;
	}

	return is_overlapping;
}

struct slot_area {
	unsigned long addr;
	int num;
};

#define MAX_SLOT_AREA 100

static struct slot_area slot_areas[MAX_SLOT_AREA];

static unsigned long slot_max;

static unsigned long slot_area_index;

static void store_slot_info(struct mem_vector *region, unsigned long image_size)
{
	struct slot_area slot_area;

	if (slot_area_index == MAX_SLOT_AREA)
		return;

	slot_area.addr = region->start;
	slot_area.num = (region->size - image_size) /
			CONFIG_PHYSICAL_ALIGN + 1;

	if (slot_area.num > 0) {
		slot_areas[slot_area_index++] = slot_area;
		slot_max += slot_area.num;
	}
}

static unsigned long slots_fetch_random(void)
{
	unsigned long slot;
	int i;

	/* Handle case of no slots stored. */
	if (slot_max == 0)
		return 0;

	slot = kaslr_get_random_long("Physical") % slot_max;

	for (i = 0; i < slot_area_index; i++) {
		if (slot >= slot_areas[i].num) {
			slot -= slot_areas[i].num;
			continue;
		}
		return slot_areas[i].addr + slot * CONFIG_PHYSICAL_ALIGN;
	}

	if (i == slot_area_index)
		debug_putstr("slots_fetch_random() failed!?\n");
	return 0;
}

static void process_mem_region(struct mem_vector *entry,
			       unsigned long minimum,
			       unsigned long image_size)
{
	struct mem_vector region, overlap;
	struct slot_area slot_area;
	unsigned long start_orig, end;
	struct mem_vector cur_entry;

	/* On 32-bit, ignore entries entirely above our maximum. */
	if (IS_ENABLED(CONFIG_X86_32) && entry->start >= KERNEL_IMAGE_SIZE)
		return;

	/* Ignore entries entirely below our minimum. */
	if (entry->start + entry->size < minimum)
		return;

	/* Ignore entries above memory limit */
	end = min(entry->size + entry->start, mem_limit);
	if (entry->start >= end)
		return;
	cur_entry.start = entry->start;
	cur_entry.size = end - entry->start;

	region.start = cur_entry.start;
	region.size = cur_entry.size;

	/* Give up if slot area array is full. */
	while (slot_area_index < MAX_SLOT_AREA) {
		start_orig = region.start;

		/* Potentially raise address to minimum location. */
		if (region.start < minimum)
			region.start = minimum;

		/* Potentially raise address to meet alignment needs. */
		region.start = ALIGN(region.start, CONFIG_PHYSICAL_ALIGN);

		/* Did we raise the address above the passed in memory entry? */
		if (region.start > cur_entry.start + cur_entry.size)
			return;

		/* Reduce size by any delta from the original address. */
		region.size -= region.start - start_orig;

		/* On 32-bit, reduce region size to fit within max size. */
		if (IS_ENABLED(CONFIG_X86_32) &&
		    region.start + region.size > KERNEL_IMAGE_SIZE)
			region.size = KERNEL_IMAGE_SIZE - region.start;

		/* Return if region can't contain decompressed kernel */
		if (region.size < image_size)
			return;

		/* If nothing overlaps, store the region and return. */
		if (!mem_avoid_overlap(&region, &overlap)) {
			store_slot_info(&region, image_size);
			return;
		}

		/* Store beginning of region if holds at least image_size. */
		if (overlap.start > region.start + image_size) {
			struct mem_vector beginning;

			beginning.start = region.start;
			beginning.size = overlap.start - region.start;
			store_slot_info(&beginning, image_size);
		}

		/* Return if overlap extends to or past end of region. */
		if (overlap.start + overlap.size >= region.start + region.size)
			return;

		/* Clip off the overlapping region and start over. */
		region.size -= overlap.start - region.start + overlap.size;
		region.start = overlap.start + overlap.size;
	}
}

#ifdef CONFIG_EFI
/*
 * Returns true if mirror region found (and must have been processed
 * for slots adding)
 */
static bool
process_efi_entries(unsigned long minimum, unsigned long image_size)
{
	struct efi_info *e = &boot_params->efi_info;
	bool efi_mirror_found = false;
	struct mem_vector region;
	efi_memory_desc_t *md;
	unsigned long pmap;
	char *signature;
	u32 nr_desc;
	int i;

	signature = (char *)&e->efi_loader_signature;
	if (strncmp(signature, EFI32_LOADER_SIGNATURE, 4) &&
	    strncmp(signature, EFI64_LOADER_SIGNATURE, 4))
		return false;

#ifdef CONFIG_X86_32
	/* Can't handle data above 4GB at this time */
	if (e->efi_memmap_hi) {
		warn("EFI memmap is above 4GB, can't be handled now on x86_32. EFI should be disabled.\n");
		return false;
	}
	pmap =  e->efi_memmap;
#else
	pmap = (e->efi_memmap | ((__u64)e->efi_memmap_hi << 32));
#endif

	nr_desc = e->efi_memmap_size / e->efi_memdesc_size;
	for (i = 0; i < nr_desc; i++) {
		md = efi_early_memdesc_ptr(pmap, e->efi_memdesc_size, i);
		if (md->attribute & EFI_MEMORY_MORE_RELIABLE) {
			efi_mirror_found = true;
			break;
		}
	}

	for (i = 0; i < nr_desc; i++) {
		md = efi_early_memdesc_ptr(pmap, e->efi_memdesc_size, i);

		/*
		 * Here we are more conservative in picking free memory than
		 * the EFI spec allows:
		 *
		 * According to the spec, EFI_BOOT_SERVICES_{CODE|DATA} are also
		 * free memory and thus available to place the kernel image into,
		 * but in practice there's firmware where using that memory leads
		 * to crashes.
		 *
		 * Only EFI_CONVENTIONAL_MEMORY is guaranteed to be free.
		 */
		if (md->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (efi_mirror_found &&
		    !(md->attribute & EFI_MEMORY_MORE_RELIABLE))
			continue;

		region.start = md->phys_addr;
		region.size = md->num_pages << EFI_PAGE_SHIFT;
		process_mem_region(&region, minimum, image_size);
		if (slot_area_index == MAX_SLOT_AREA) {
			debug_putstr("Aborted EFI scan (slot_areas full)!\n");
			break;
		}
	}
	return true;
}
#else
static inline bool
process_efi_entries(unsigned long minimum, unsigned long image_size)
{
	return false;
}
#endif

static void process_e820_entries(unsigned long minimum,
				 unsigned long image_size)
{
	int i;
	struct mem_vector region;
	struct boot_e820_entry *entry;

	/* Verify potential e820 positions, appending to slots list. */
	for (i = 0; i < boot_params->e820_entries; i++) {
		entry = &boot_params->e820_table[i];
		/* Skip non-RAM entries. */
		if (entry->type != E820_TYPE_RAM)
			continue;
		region.start = entry->addr;
		region.size = entry->size;
		process_mem_region(&region, minimum, image_size);
		if (slot_area_index == MAX_SLOT_AREA) {
			debug_putstr("Aborted e820 scan (slot_areas full)!\n");
			break;
		}
	}
}

static unsigned long find_random_phys_addr(unsigned long minimum,
					   unsigned long image_size)
{
	/* Check if we had too many memmaps. */
	if (memmap_too_large) {
		debug_putstr("Aborted memory entries scan (more than 4 memmap= args)!\n");
		return 0;
	}

	/* Make sure minimum is aligned. */
	minimum = ALIGN(minimum, CONFIG_PHYSICAL_ALIGN);

	if (process_efi_entries(minimum, image_size))
		return slots_fetch_random();

	process_e820_entries(minimum, image_size);
	return slots_fetch_random();
}

static unsigned long find_random_virt_addr(unsigned long minimum,
					   unsigned long image_size)
{
	unsigned long slots, random_addr;

	/* Make sure minimum is aligned. */
	minimum = ALIGN(minimum, CONFIG_PHYSICAL_ALIGN);
	/* Align image_size for easy slot calculations. */
	image_size = ALIGN(image_size, CONFIG_PHYSICAL_ALIGN);

	/*
	 * There are how many CONFIG_PHYSICAL_ALIGN-sized slots
	 * that can hold image_size within the range of minimum to
	 * KERNEL_IMAGE_SIZE?
	 */
	slots = (KERNEL_IMAGE_SIZE - minimum - image_size) /
		 CONFIG_PHYSICAL_ALIGN + 1;

	random_addr = kaslr_get_random_long("Virtual") % slots;

	return random_addr * CONFIG_PHYSICAL_ALIGN + minimum;
}

/*
 * Since this function examines addresses much more numerically,
 * it takes the input and output pointers as 'unsigned long'.
 */
void choose_random_location(unsigned long input,
			    unsigned long input_size,
			    unsigned long *output,
			    unsigned long output_size,
			    unsigned long *virt_addr)
{
	unsigned long random_addr, min_addr;

	if (cmdline_find_option_bool("nokaslr")) {
		warn("KASLR disabled: 'nokaslr' on cmdline.");
		return;
	}

#ifdef CONFIG_X86_5LEVEL
	if (__read_cr4() & X86_CR4_LA57) {
		pgtable_l5_enabled = 1;
		pgdir_shift = 48;
		ptrs_per_p4d = 512;
	}
#endif

	boot_params->hdr.loadflags |= KASLR_FLAG;

	/* Prepare to add new identity pagetables on demand. */
	initialize_identity_maps();

	/* Record the various known unsafe memory ranges. */
	mem_avoid_init(input, input_size, *output);

	/*
	 * Low end of the randomization range should be the
	 * smaller of 512M or the initial kernel image
	 * location:
	 */
	min_addr = min(*output, 512UL << 20);

	/* Walk available memory entries to find a random address. */
	random_addr = find_random_phys_addr(min_addr, output_size);
	if (!random_addr) {
		warn("Physical KASLR disabled: no suitable memory region!");
	} else {
		/* Update the new physical address location. */
		if (*output != random_addr) {
			add_identity_map(random_addr, output_size);
			*output = random_addr;
		}

		/*
		 * This loads the identity mapping page table.
		 * This should only be done if a new physical address
		 * is found for the kernel, otherwise we should keep
		 * the old page table to make it be like the "nokaslr"
		 * case.
		 */
		finalize_identity_maps();
	}


	/* Pick random virtual address starting from LOAD_PHYSICAL_ADDR. */
	if (IS_ENABLED(CONFIG_X86_64))
		random_addr = find_random_virt_addr(LOAD_PHYSICAL_ADDR, output_size);
	*virt_addr = random_addr;
}
