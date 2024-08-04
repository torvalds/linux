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

#include "misc.h"
#include "error.h"
#include "../string.h"
#include "efi.h"

#include <generated/compile.h>
#include <linux/module.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/ctype.h>
#include <generated/utsversion.h>
#include <generated/utsrelease.h>

#define _SETUP
#include <asm/setup.h>	/* For COMMAND_LINE_SIZE */
#undef _SETUP

extern unsigned long get_cmd_line_ptr(void);

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
	hash = rotate_xor(hash, boot_params_ptr, sizeof(*boot_params_ptr));

	return hash;
}

#define KASLR_COMPRESSED_BOOT
#include "../../lib/kaslr.c"


/* Only supporting at most 4 unusable memmap regions with kaslr */
#define MAX_MEMMAP_REGIONS	4

static bool memmap_too_large;


/*
 * Store memory limit: MAXMEM on 64-bit and KERNEL_IMAGE_SIZE on 32-bit.
 * It may be reduced by "mem=nn[KMG]" or "memmap=nn[KMG]" command line options.
 */
static u64 mem_limit;

/* Number of immovable memory regions */
static int num_immovable_mem;

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
parse_memmap(char *p, u64 *start, u64 *size)
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
		/*
		 * memmap=nn@ss specifies usable region, should
		 * be skipped
		 */
		*size = 0;
		fallthrough;
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
		u64 start, size;
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		rc = parse_memmap(str, &start, &size);
		if (rc < 0)
			break;
		str = k;

		if (start == 0) {
			/* Store the specified memory limit if size > 0 */
			if (size > 0 && size < mem_limit)
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

/* Store the number of 1GB huge pages which users specified: */
static unsigned long max_gb_huge_pages;

static void parse_gb_huge_pages(char *param, char *val)
{
	static bool gbpage_sz;
	char *p;

	if (!strcmp(param, "hugepagesz")) {
		p = val;
		if (memparse(p, &p) != PUD_SIZE) {
			gbpage_sz = false;
			return;
		}

		if (gbpage_sz)
			warn("Repeatedly set hugeTLB page size of 1G!\n");
		gbpage_sz = true;
		return;
	}

	if (!strcmp(param, "hugepages") && gbpage_sz) {
		p = val;
		max_gb_huge_pages = simple_strtoull(p, &p, 0);
		return;
	}
}

static void handle_mem_options(void)
{
	char *args = (char *)get_cmd_line_ptr();
	size_t len;
	char *tmp_cmdline;
	char *param, *val;
	u64 mem_size;

	if (!args)
		return;

	len = strnlen(args, COMMAND_LINE_SIZE-1);
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
		if (!val && strcmp(param, "--") == 0)
			break;

		if (!strcmp(param, "memmap")) {
			mem_avoid_memmap(val);
		} else if (IS_ENABLED(CONFIG_X86_64) && strstr(param, "hugepages")) {
			parse_gb_huge_pages(param, val);
		} else if (!strcmp(param, "mem")) {
			char *p = val;

			if (!strcmp(p, "nopentium"))
				continue;
			mem_size = memparse(p, &p);
			if (mem_size == 0)
				break;

			if (mem_size < mem_limit)
				mem_limit = mem_size;
		}
	}

	free(tmp_cmdline);
	return;
}

/*
 * In theory, KASLR can put the kernel anywhere in the range of [16M, MAXMEM)
 * on 64-bit, and [16M, KERNEL_IMAGE_SIZE) on 32-bit.
 *
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
	unsigned long init_size = boot_params_ptr->hdr.init_size;
	u64 initrd_start, initrd_size;
	unsigned long cmd_line, cmd_line_size;

	/*
	 * Avoid the region that is unsafe to overlap during
	 * decompression.
	 */
	mem_avoid[MEM_AVOID_ZO_RANGE].start = input;
	mem_avoid[MEM_AVOID_ZO_RANGE].size = (output + init_size) - input;

	/* Avoid initrd. */
	initrd_start  = (u64)boot_params_ptr->ext_ramdisk_image << 32;
	initrd_start |= boot_params_ptr->hdr.ramdisk_image;
	initrd_size  = (u64)boot_params_ptr->ext_ramdisk_size << 32;
	initrd_size |= boot_params_ptr->hdr.ramdisk_size;
	mem_avoid[MEM_AVOID_INITRD].start = initrd_start;
	mem_avoid[MEM_AVOID_INITRD].size = initrd_size;
	/* No need to set mapping for initrd, it will be handled in VO. */

	/* Avoid kernel command line. */
	cmd_line = get_cmd_line_ptr();
	/* Calculate size of cmd_line. */
	if (cmd_line) {
		cmd_line_size = strnlen((char *)cmd_line, COMMAND_LINE_SIZE-1) + 1;
		mem_avoid[MEM_AVOID_CMDLINE].start = cmd_line;
		mem_avoid[MEM_AVOID_CMDLINE].size = cmd_line_size;
	}

	/* Avoid boot parameters. */
	mem_avoid[MEM_AVOID_BOOTPARAMS].start = (unsigned long)boot_params_ptr;
	mem_avoid[MEM_AVOID_BOOTPARAMS].size = sizeof(*boot_params_ptr);

	/* We don't need to set a mapping for setup_data. */

	/* Mark the memmap regions we need to avoid */
	handle_mem_options();

	/* Enumerate the immovable memory regions */
	num_immovable_mem = count_immovable_mem_regions();
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
	u64 earliest = img->start + img->size;
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
	ptr = (struct setup_data *)(unsigned long)boot_params_ptr->hdr.setup_data;
	while (ptr) {
		struct mem_vector avoid;

		avoid.start = (unsigned long)ptr;
		avoid.size = sizeof(*ptr) + ptr->len;

		if (mem_overlaps(img, &avoid) && (avoid.start < earliest)) {
			*overlap = avoid;
			earliest = overlap->start;
			is_overlapping = true;
		}

		if (ptr->type == SETUP_INDIRECT &&
		    ((struct setup_indirect *)ptr->data)->type != SETUP_INDIRECT) {
			avoid.start = ((struct setup_indirect *)ptr->data)->addr;
			avoid.size = ((struct setup_indirect *)ptr->data)->len;

			if (mem_overlaps(img, &avoid) && (avoid.start < earliest)) {
				*overlap = avoid;
				earliest = overlap->start;
				is_overlapping = true;
			}
		}

		ptr = (struct setup_data *)(unsigned long)ptr->next;
	}

	return is_overlapping;
}

struct slot_area {
	u64 addr;
	unsigned long num;
};

#define MAX_SLOT_AREA 100

static struct slot_area slot_areas[MAX_SLOT_AREA];
static unsigned int slot_area_index;
static unsigned long slot_max;

static void store_slot_info(struct mem_vector *region, unsigned long image_size)
{
	struct slot_area slot_area;

	if (slot_area_index == MAX_SLOT_AREA)
		return;

	slot_area.addr = region->start;
	slot_area.num = 1 + (region->size - image_size) / CONFIG_PHYSICAL_ALIGN;

	slot_areas[slot_area_index++] = slot_area;
	slot_max += slot_area.num;
}

/*
 * Skip as many 1GB huge pages as possible in the passed region
 * according to the number which users specified:
 */
static void
process_gb_huge_pages(struct mem_vector *region, unsigned long image_size)
{
	u64 pud_start, pud_end;
	unsigned long gb_huge_pages;
	struct mem_vector tmp;

	if (!IS_ENABLED(CONFIG_X86_64) || !max_gb_huge_pages) {
		store_slot_info(region, image_size);
		return;
	}

	/* Are there any 1GB pages in the region? */
	pud_start = ALIGN(region->start, PUD_SIZE);
	pud_end = ALIGN_DOWN(region->start + region->size, PUD_SIZE);

	/* No good 1GB huge pages found: */
	if (pud_start >= pud_end) {
		store_slot_info(region, image_size);
		return;
	}

	/* Check if the head part of the region is usable. */
	if (pud_start >= region->start + image_size) {
		tmp.start = region->start;
		tmp.size = pud_start - region->start;
		store_slot_info(&tmp, image_size);
	}

	/* Skip the good 1GB pages. */
	gb_huge_pages = (pud_end - pud_start) >> PUD_SHIFT;
	if (gb_huge_pages > max_gb_huge_pages) {
		pud_end = pud_start + (max_gb_huge_pages << PUD_SHIFT);
		max_gb_huge_pages = 0;
	} else {
		max_gb_huge_pages -= gb_huge_pages;
	}

	/* Check if the tail part of the region is usable. */
	if (region->start + region->size >= pud_end + image_size) {
		tmp.start = pud_end;
		tmp.size = region->start + region->size - pud_end;
		store_slot_info(&tmp, image_size);
	}
}

static u64 slots_fetch_random(void)
{
	unsigned long slot;
	unsigned int i;

	/* Handle case of no slots stored. */
	if (slot_max == 0)
		return 0;

	slot = kaslr_get_random_long("Physical") % slot_max;

	for (i = 0; i < slot_area_index; i++) {
		if (slot >= slot_areas[i].num) {
			slot -= slot_areas[i].num;
			continue;
		}
		return slot_areas[i].addr + ((u64)slot * CONFIG_PHYSICAL_ALIGN);
	}

	if (i == slot_area_index)
		debug_putstr("slots_fetch_random() failed!?\n");
	return 0;
}

static void __process_mem_region(struct mem_vector *entry,
				 unsigned long minimum,
				 unsigned long image_size)
{
	struct mem_vector region, overlap;
	u64 region_end;

	/* Enforce minimum and memory limit. */
	region.start = max_t(u64, entry->start, minimum);
	region_end = min(entry->start + entry->size, mem_limit);

	/* Give up if slot area array is full. */
	while (slot_area_index < MAX_SLOT_AREA) {
		/* Potentially raise address to meet alignment needs. */
		region.start = ALIGN(region.start, CONFIG_PHYSICAL_ALIGN);

		/* Did we raise the address above the passed in memory entry? */
		if (region.start > region_end)
			return;

		/* Reduce size by any delta from the original address. */
		region.size = region_end - region.start;

		/* Return if region can't contain decompressed kernel */
		if (region.size < image_size)
			return;

		/* If nothing overlaps, store the region and return. */
		if (!mem_avoid_overlap(&region, &overlap)) {
			process_gb_huge_pages(&region, image_size);
			return;
		}

		/* Store beginning of region if holds at least image_size. */
		if (overlap.start >= region.start + image_size) {
			region.size = overlap.start - region.start;
			process_gb_huge_pages(&region, image_size);
		}

		/* Clip off the overlapping region and start over. */
		region.start = overlap.start + overlap.size;
	}
}

static bool process_mem_region(struct mem_vector *region,
			       unsigned long minimum,
			       unsigned long image_size)
{
	int i;
	/*
	 * If no immovable memory found, or MEMORY_HOTREMOVE disabled,
	 * use @region directly.
	 */
	if (!num_immovable_mem) {
		__process_mem_region(region, minimum, image_size);

		if (slot_area_index == MAX_SLOT_AREA) {
			debug_putstr("Aborted e820/efi memmap scan (slot_areas full)!\n");
			return true;
		}
		return false;
	}

#if defined(CONFIG_MEMORY_HOTREMOVE) && defined(CONFIG_ACPI)
	/*
	 * If immovable memory found, filter the intersection between
	 * immovable memory and @region.
	 */
	for (i = 0; i < num_immovable_mem; i++) {
		u64 start, end, entry_end, region_end;
		struct mem_vector entry;

		if (!mem_overlaps(region, &immovable_mem[i]))
			continue;

		start = immovable_mem[i].start;
		end = start + immovable_mem[i].size;
		region_end = region->start + region->size;

		entry.start = clamp(region->start, start, end);
		entry_end = clamp(region_end, start, end);
		entry.size = entry_end - entry.start;

		__process_mem_region(&entry, minimum, image_size);

		if (slot_area_index == MAX_SLOT_AREA) {
			debug_putstr("Aborted e820/efi memmap scan when walking immovable regions(slot_areas full)!\n");
			return true;
		}
	}
#endif
	return false;
}

#ifdef CONFIG_EFI

/*
 * Only EFI_CONVENTIONAL_MEMORY and EFI_UNACCEPTED_MEMORY (if supported) are
 * guaranteed to be free.
 *
 * Pick free memory more conservatively than the EFI spec allows: according to
 * the spec, EFI_BOOT_SERVICES_{CODE|DATA} are also free memory and thus
 * available to place the kernel image into, but in practice there's firmware
 * where using that memory leads to crashes. Buggy vendor EFI code registers
 * for an event that triggers on SetVirtualAddressMap(). The handler assumes
 * that EFI_BOOT_SERVICES_DATA memory has not been touched by loader yet, which
 * is probably true for Windows.
 *
 * Preserve EFI_BOOT_SERVICES_* regions until after SetVirtualAddressMap().
 */
static inline bool memory_type_is_free(efi_memory_desc_t *md)
{
	if (md->type == EFI_CONVENTIONAL_MEMORY)
		return true;

	if (IS_ENABLED(CONFIG_UNACCEPTED_MEMORY) &&
	    md->type == EFI_UNACCEPTED_MEMORY)
		    return true;

	return false;
}

/*
 * Returns true if we processed the EFI memmap, which we prefer over the E820
 * table if it is available.
 */
static bool
process_efi_entries(unsigned long minimum, unsigned long image_size)
{
	struct efi_info *e = &boot_params_ptr->efi_info;
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

		if (!memory_type_is_free(md))
			continue;

		if (efi_soft_reserve_enabled() &&
		    (md->attribute & EFI_MEMORY_SP))
			continue;

		if (efi_mirror_found &&
		    !(md->attribute & EFI_MEMORY_MORE_RELIABLE))
			continue;

		region.start = md->phys_addr;
		region.size = md->num_pages << EFI_PAGE_SHIFT;
		if (process_mem_region(&region, minimum, image_size))
			break;
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
	for (i = 0; i < boot_params_ptr->e820_entries; i++) {
		entry = &boot_params_ptr->e820_table[i];
		/* Skip non-RAM entries. */
		if (entry->type != E820_TYPE_RAM)
			continue;
		region.start = entry->addr;
		region.size = entry->size;
		if (process_mem_region(&region, minimum, image_size))
			break;
	}
}

static unsigned long find_random_phys_addr(unsigned long minimum,
					   unsigned long image_size)
{
	u64 phys_addr;

	/* Bail out early if it's impossible to succeed. */
	if (minimum + image_size > mem_limit)
		return 0;

	/* Check if we had too many memmaps. */
	if (memmap_too_large) {
		debug_putstr("Aborted memory entries scan (more than 4 memmap= args)!\n");
		return 0;
	}

	if (!process_efi_entries(minimum, image_size))
		process_e820_entries(minimum, image_size);

	phys_addr = slots_fetch_random();

	/* Perform a final check to make sure the address is in range. */
	if (phys_addr < minimum || phys_addr + image_size > mem_limit) {
		warn("Invalid physical address chosen!\n");
		return 0;
	}

	return (unsigned long)phys_addr;
}

static unsigned long find_random_virt_addr(unsigned long minimum,
					   unsigned long image_size)
{
	unsigned long slots, random_addr;

	/*
	 * There are how many CONFIG_PHYSICAL_ALIGN-sized slots
	 * that can hold image_size within the range of minimum to
	 * KERNEL_IMAGE_SIZE?
	 */
	slots = 1 + (KERNEL_IMAGE_SIZE - minimum - image_size) / CONFIG_PHYSICAL_ALIGN;

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

	boot_params_ptr->hdr.loadflags |= KASLR_FLAG;

	if (IS_ENABLED(CONFIG_X86_32))
		mem_limit = KERNEL_IMAGE_SIZE;
	else
		mem_limit = MAXMEM;

	/* Record the various known unsafe memory ranges. */
	mem_avoid_init(input, input_size, *output);

	/*
	 * Low end of the randomization range should be the
	 * smaller of 512M or the initial kernel image
	 * location:
	 */
	min_addr = min(*output, 512UL << 20);
	/* Make sure minimum is aligned. */
	min_addr = ALIGN(min_addr, CONFIG_PHYSICAL_ALIGN);

	/* Walk available memory entries to find a random address. */
	random_addr = find_random_phys_addr(min_addr, output_size);
	if (!random_addr) {
		warn("Physical KASLR disabled: no suitable memory region!");
	} else {
		/* Update the new physical address location. */
		if (*output != random_addr)
			*output = random_addr;
	}


	/* Pick random virtual address starting from LOAD_PHYSICAL_ADDR. */
	if (IS_ENABLED(CONFIG_X86_64))
		random_addr = find_random_virt_addr(LOAD_PHYSICAL_ADDR, output_size);
	*virt_addr = random_addr;
}
