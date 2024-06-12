/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SH_SETUP_H
#define _SH_SETUP_H

#include <uapi/asm/setup.h>

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	((unsigned char *)empty_zero_page)

#define MOUNT_ROOT_RDONLY (*(unsigned long *) (PARAM+0x000))
#define RAMDISK_FLAGS (*(unsigned long *) (PARAM+0x004))
#define ORIG_ROOT_DEV (*(unsigned long *) (PARAM+0x008))
#define LOADER_TYPE (*(unsigned long *) (PARAM+0x00c))
#define INITRD_START (*(unsigned long *) (PARAM+0x010))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x014))
/* ... */
#define COMMAND_LINE ((char *) (PARAM+0x100))

void sh_mv_setup(void);
void check_for_initrd(void);
void per_cpu_trap_init(void);
void sh_fdt_init(phys_addr_t dt_phys);

#endif /* _SH_SETUP_H */
