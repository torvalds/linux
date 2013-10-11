#include "misc.h"

#ifdef CONFIG_RANDOMIZE_BASE

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
