#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

/**
 * struct x86_init_resources - platform specific resource related ops
 * @probe_roms:			probe BIOS roms
 *
 */
struct x86_init_resources {
	void (*probe_roms)(void);
};

/**
 * struct x86_init_ops - functions for platform specific setup
 *
 */
struct x86_init_ops {
	struct x86_init_resources resources;
};

extern struct x86_init_ops x86_init;

extern void x86_init_noop(void);

#endif
