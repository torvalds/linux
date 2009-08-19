#ifndef _ASM_X86_PLATFORM_H
#define _ASM_X86_PLATFORM_H

/**
 * struct x86_init_resources - platform specific resource related ops
 * @probe_roms:			probe BIOS roms
 * @reserve_resources:		reserve the standard resources for the
 *				platform
 * @reserve_ebda_region:	reserve the extended bios data area
 *
 */
struct x86_init_resources {
	void (*probe_roms)(void);
	void (*reserve_resources)(void);
	void (*reserve_ebda_region)(void);
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
