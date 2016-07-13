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
#include "misc.h"
#include "error.h"

#include <asm/msr.h>
#include <asm/archrandom.h>
#include <asm/e820.h>

#include <generated/compile.h>
#include <linux/module.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <generated/utsrelease.h>

/* Simplified build-specific string for starting entropy. */
static const char build_str[] = UTS_RELEASE " (" LINUX_COMPILE_BY "@"
		LINUX_COMPILE_HOST ") (" LINUX_COMPILER ") " UTS_VERSION;

#define I8254_PORT_CONTROL	0x43
#define I8254_PORT_COUNTER0	0x40
#define I8254_CMD_READBACK	0xC0
#define I8254_SELECT_COUNTER0	0x02
#define I8254_STATUS_NOTREADY	0x40
static inline u16 i8254(void)
{
	u16 status, timer;

	do {
		outb(I8254_PORT_CONTROL,
		     I8254_CMD_READBACK | I8254_SELECT_COUNTER0);
		status = inb(I8254_PORT_COUNTER0);
		timer  = inb(I8254_PORT_COUNTER0);
		timer |= inb(I8254_PORT_COUNTER0) << 8;
	} while (status & I8254_STATUS_NOTREADY);

	return timer;
}

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
static unsigned long get_random_boot(void)
{
	unsigned long hash = 0;

	hash = rotate_xor(hash, build_str, sizeof(build_str));
	hash = rotate_xor(hash, boot_params, sizeof(*boot_params));

	return hash;
}

static unsigned long get_random_long(const char *purpose)
{
#ifdef CONFIG_X86_64
	const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
#else
	const unsigned long mix_const = 0x3f39e593UL;
#endif
	unsigned long raw, random = get_random_boot();
	bool use_i8254 = true;

	debug_putstr(purpose);
	debug_putstr(" KASLR using");

	if (has_cpuflag(X86_FEATURE_RDRAND)) {
		debug_putstr(" RDRAND");
		if (rdrand_long(&raw)) {
			random ^= raw;
			use_i8254 = false;
		}
	}

	if (has_cpuflag(X86_FEATURE_TSC)) {
		debug_putstr(" RDTSC");
		raw = rdtsc();

		random ^= raw;
		use_i8254 = false;
	}

	if (use_i8254) {
		debug_putstr(" i8254");
		random ^= i8254();
	}

	/* Circular multiply for better bit diffusion */
	asm("mul %3"
	    : "=a" (random), "=d" (raw)
	    : "a" (random), "rm" (mix_const));
	random += raw;

	debug_putstr("...\n");

	return random;
}

struct mem_vector {
	unsigned long start;
	unsigned long size;
};

enum mem_avoid_index {
	MEM_AVOID_ZO_RANGE = 0,
	MEM_AVOID_INITRD,
	MEM_AVOID_CMDLINE,
	MEM_AVOID_BOOTPARAMS,
	MEM_AVOID_MAX,
};

static struct mem_vector mem_avoid[MEM_AVOID_MAX];

static bool mem_contains(struct mem_vector *region, struct mem_vector *item)
{
	/* Item at least partially before region. */
	if (item->start < region->start)
		return false;
	/* Item at least partially after region. */
	if (item->start + item->size > region->start + region->size)
		return false;
	return true;
}

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
	for (cmd_line_size = 0; ptr[cmd_line_size++]; )
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
			is_overlapping = true;
		}

		ptr = (struct setup_data *)(unsigned long)ptr->next;
	}

	return is_overlapping;
}

static unsigned long slots[KERNEL_IMAGE_SIZE / CONFIG_PHYSICAL_ALIGN];

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

static void slots_append(unsigned long addr)
{
	/* Overflowing the slots list should be impossible. */
	if (slot_max >= KERNEL_IMAGE_SIZE / CONFIG_PHYSICAL_ALIGN)
		return;

	slots[slot_max++] = addr;
}

static unsigned long slots_fetch_random(void)
{
	/* Handle case of no slots stored. */
	if (slot_max == 0)
		return 0;

	return slots[get_random_long("Physical") % slot_max];
}

static void process_e820_entry(struct e820entry *entry,
			       unsigned long minimum,
			       unsigned long image_size)
{
	struct mem_vector region, img, overlap;

	/* Skip non-RAM entries. */
	if (entry->type != E820_RAM)
		return;

	/* Ignore entries entirely above our maximum. */
	if (entry->addr >= KERNEL_IMAGE_SIZE)
		return;

	/* Ignore entries entirely below our minimum. */
	if (entry->addr + entry->size < minimum)
		return;

	region.start = entry->addr;
	region.size = entry->size;

	/* Potentially raise address to minimum location. */
	if (region.start < minimum)
		region.start = minimum;

	/* Potentially raise address to meet alignment requirements. */
	region.start = ALIGN(region.start, CONFIG_PHYSICAL_ALIGN);

	/* Did we raise the address above the bounds of this e820 region? */
	if (region.start > entry->addr + entry->size)
		return;

	/* Reduce size by any delta from the original address. */
	region.size -= region.start - entry->addr;

	/* Reduce maximum size to fit end of image within maximum limit. */
	if (region.start + region.size > KERNEL_IMAGE_SIZE)
		region.size = KERNEL_IMAGE_SIZE - region.start;

	/* Walk each aligned slot and check for avoided areas. */
	for (img.start = region.start, img.size = image_size ;
	     mem_contains(&region, &img) ;
	     img.start += CONFIG_PHYSICAL_ALIGN) {
		if (mem_avoid_overlap(&img, &overlap))
			continue;
		slots_append(img.start);
	}
}

static unsigned long find_random_phys_addr(unsigned long minimum,
					   unsigned long image_size)
{
	int i;
	unsigned long addr;

	/* Make sure minimum is aligned. */
	minimum = ALIGN(minimum, CONFIG_PHYSICAL_ALIGN);

	/* Verify potential e820 positions, appending to slots list. */
	for (i = 0; i < boot_params->e820_entries; i++) {
		process_e820_entry(&boot_params->e820_map[i], minimum,
				   image_size);
	}

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

	random_addr = get_random_long("Virtual") % slots;

	return random_addr * CONFIG_PHYSICAL_ALIGN + minimum;
}

/*
 * Since this function examines addresses much more numerically,
 * it takes the input and output pointers as 'unsigned long'.
 */
unsigned char *choose_random_location(unsigned long input,
				      unsigned long input_size,
				      unsigned long output,
				      unsigned long output_size)
{
	unsigned long choice = output;
	unsigned long random_addr;

#ifdef CONFIG_HIBERNATION
	if (!cmdline_find_option_bool("kaslr")) {
		warn("KASLR disabled: 'kaslr' not on cmdline (hibernation selected).");
		goto out;
	}
#else
	if (cmdline_find_option_bool("nokaslr")) {
		warn("KASLR disabled: 'nokaslr' on cmdline.");
		goto out;
	}
#endif

	boot_params->hdr.loadflags |= KASLR_FLAG;

	/* Record the various known unsafe memory ranges. */
	mem_avoid_init(input, input_size, output);

	/* Walk e820 and find a random address. */
	random_addr = find_random_phys_addr(output, output_size);
	if (!random_addr) {
		warn("KASLR disabled: could not find suitable E820 region!");
		goto out;
	}

	/* Always enforce the minimum. */
	if (random_addr < choice)
		goto out;

	choice = random_addr;

	add_identity_map(choice, output_size);

	/* This actually loads the identity pagetable on x86_64. */
	finalize_identity_maps();
out:
	return (unsigned char *)choice;
}
