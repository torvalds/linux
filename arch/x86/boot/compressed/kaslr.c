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

static unsigned long get_random_long(void)
{
#ifdef CONFIG_X86_64
	const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
#else
	const unsigned long mix_const = 0x3f39e593UL;
#endif
	unsigned long raw, random = get_random_boot();
	bool use_i8254 = true;

	debug_putstr("KASLR using");

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

#define MEM_AVOID_MAX 4
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
 * In theroy, KASLR can put the kernel anywhere in area of [16M, 64T). The
 * mem_avoid array is used to store the ranges that need to be avoided when
 * KASLR searches for a an appropriate random address. We must avoid any
 * regions that are unsafe to overlap with during decompression, and other
 * things like the initrd, cmdline and boot_params.
 *
 * How to calculate the unsafe areas is detailed here, and is informed by
 * the decompression calculations in header.S, and the diagram in misc.c.
 *
 * The compressed vmlinux (ZO) plus relocs and the run space of ZO can't be
 * overwritten by decompression output.
 *
 * ZO sits against the end of the decompression buffer, so we can calculate
 * where text, data, bss, etc of ZO are positioned.
 *
 * The follow are already enforced by the code:
 *  - init_size >= kernel_total_size
 *  - input + input_len >= output + output_len
 *  - kernel_total_size could be >= or < output_len
 *
 * From this, we can make several observations, illustrated by a diagram:
 *  - init_size >= kernel_total_size
 *  - input + input_len > output + output_len
 *  - kernel_total_size >= output_len
 *
 * 0   output            input            input+input_len    output+init_size
 * |     |                 |                       |                       |
 * |     |                 |                       |                       |
 * |-----|--------|--------|------------------|----|------------|----------|
 *                |                           |                 |
 *                |                           |                 |
 * output+init_size-ZO_INIT_SIZE   output+output_len  output+kernel_total_size
 *
 * [output, output+init_size) is for the buffer for decompressing the
 * compressed kernel (ZO).
 *
 * [output, output+kernel_total_size) is for the uncompressed kernel (VO)
 * and its bss, brk, etc.
 * [output, output+output_len) is VO plus relocs
 *
 * [output+init_size-ZO_INIT_SIZE, output+init_size) is the copied ZO.
 * [input, input+input_len) is the copied compressed (VO (vmlinux after
 * objcopy) plus relocs), not the ZO.
 *
 * [input+input_len, output+init_size) is [_text, _end) for ZO. That was the
 * first range in mem_avoid, which included ZO's heap and stack. Also
 * [input, input+input_size) need be put in mem_avoid array, but since it
 * is adjacent to the first entry, they can be merged. This is how we get
 * the first entry in mem_avoid[].
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
	mem_avoid[0].start = input;
	mem_avoid[0].size = (output + init_size) - input;

	/* Avoid initrd. */
	initrd_start  = (u64)boot_params->ext_ramdisk_image << 32;
	initrd_start |= boot_params->hdr.ramdisk_image;
	initrd_size  = (u64)boot_params->ext_ramdisk_size << 32;
	initrd_size |= boot_params->hdr.ramdisk_size;
	mem_avoid[1].start = initrd_start;
	mem_avoid[1].size = initrd_size;

	/* Avoid kernel command line. */
	cmd_line  = (u64)boot_params->ext_cmd_line_ptr << 32;
	cmd_line |= boot_params->hdr.cmd_line_ptr;
	/* Calculate size of cmd_line. */
	ptr = (char *)(unsigned long)cmd_line;
	for (cmd_line_size = 0; ptr[cmd_line_size++]; )
		;
	mem_avoid[2].start = cmd_line;
	mem_avoid[2].size = cmd_line_size;

	/* Avoid params */
	mem_avoid[3].start = (unsigned long)boot_params;
	mem_avoid[3].size = sizeof(*boot_params);
}

/* Does this memory vector overlap a known avoided area? */
static bool mem_avoid_overlap(struct mem_vector *img)
{
	int i;
	struct setup_data *ptr;

	for (i = 0; i < MEM_AVOID_MAX; i++) {
		if (mem_overlaps(img, &mem_avoid[i]))
			return true;
	}

	/* Avoid all entries in the setup_data linked list. */
	ptr = (struct setup_data *)(unsigned long)boot_params->hdr.setup_data;
	while (ptr) {
		struct mem_vector avoid;

		avoid.start = (unsigned long)ptr;
		avoid.size = sizeof(*ptr) + ptr->len;

		if (mem_overlaps(img, &avoid))
			return true;

		ptr = (struct setup_data *)(unsigned long)ptr->next;
	}

	return false;
}

static unsigned long slots[KERNEL_IMAGE_SIZE / CONFIG_PHYSICAL_ALIGN];
static unsigned long slot_max;

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

	return slots[get_random_long() % slot_max];
}

static void process_e820_entry(struct e820entry *entry,
			       unsigned long minimum,
			       unsigned long image_size)
{
	struct mem_vector region, img;

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
		if (mem_avoid_overlap(&img))
			continue;
		slots_append(img.start);
	}
}

static unsigned long find_random_addr(unsigned long minimum,
				      unsigned long size)
{
	int i;
	unsigned long addr;

	/* Make sure minimum is aligned. */
	minimum = ALIGN(minimum, CONFIG_PHYSICAL_ALIGN);

	/* Verify potential e820 positions, appending to slots list. */
	for (i = 0; i < boot_params->e820_entries; i++) {
		process_e820_entry(&boot_params->e820_map[i], minimum, size);
	}

	return slots_fetch_random();
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
	random_addr = find_random_addr(output, output_size);
	if (!random_addr) {
		warn("KASLR disabled: could not find suitable E820 region!");
		goto out;
	}

	/* Always enforce the minimum. */
	if (random_addr < choice)
		goto out;

	choice = random_addr;
out:
	return (unsigned char *)choice;
}
