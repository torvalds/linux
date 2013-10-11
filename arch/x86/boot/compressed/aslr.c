#include "misc.h"

#ifdef CONFIG_RANDOMIZE_BASE
#include <asm/msr.h>
#include <asm/archrandom.h>

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

static unsigned long get_random_long(void)
{
	unsigned long random;

	if (has_cpuflag(X86_FEATURE_RDRAND)) {
		debug_putstr("KASLR using RDRAND...\n");
		if (rdrand_long(&random))
			return random;
	}

	if (has_cpuflag(X86_FEATURE_TSC)) {
		uint32_t raw;

		debug_putstr("KASLR using RDTSC...\n");
		rdtscl(raw);

		/* Only use the low bits of rdtsc. */
		random = raw & 0xffff;
	} else {
		debug_putstr("KASLR using i8254...\n");
		random = i8254();
	}

	/* Extend timer bits poorly... */
	random |= (random << 16);
#ifdef CONFIG_X86_64
	random |= (random << 32);
#endif
	return random;
}

unsigned char *choose_kernel_location(unsigned char *input,
				      unsigned long input_size,
				      unsigned char *output,
				      unsigned long output_size)
{
	unsigned long choice = (unsigned long)output;

	if (cmdline_find_option_bool("nokaslr")) {
		debug_putstr("KASLR disabled...\n");
		goto out;
	}

	/* XXX: choose random location. */

out:
	return (unsigned char *)choice;
}

#endif /* CONFIG_RANDOMIZE_BASE */
