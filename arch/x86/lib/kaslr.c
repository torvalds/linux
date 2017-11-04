// SPDX-License-Identifier: GPL-2.0
/*
 * Entropy functions used on early boot for KASLR base and memory
 * randomization. The base randomization is done in the compressed
 * kernel and memory randomization is done early when the regular
 * kernel starts. This file is included in the compressed kernel and
 * normally linked in the regular.
 */
#include <asm/asm.h>
#include <asm/kaslr.h>
#include <asm/msr.h>
#include <asm/archrandom.h>
#include <asm/e820/api.h>
#include <asm/io.h>

/*
 * When built for the regular kernel, several functions need to be stubbed out
 * or changed to their regular kernel equivalent.
 */
#ifndef KASLR_COMPRESSED_BOOT
#include <asm/cpufeature.h>
#include <asm/setup.h>

#define debug_putstr(v) early_printk("%s", v)
#define has_cpuflag(f) boot_cpu_has(f)
#define get_boot_seed() kaslr_offset()
#endif

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

unsigned long kaslr_get_random_long(const char *purpose)
{
#ifdef CONFIG_X86_64
	const unsigned long mix_const = 0x5d6008cbf3848dd3UL;
#else
	const unsigned long mix_const = 0x3f39e593UL;
#endif
	unsigned long raw, random = get_boot_seed();
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
	asm(_ASM_MUL "%3"
	    : "=a" (random), "=d" (raw)
	    : "a" (random), "rm" (mix_const));
	random += raw;

	debug_putstr("...\n");

	return random;
}
