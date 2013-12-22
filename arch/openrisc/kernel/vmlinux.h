#ifndef __OPENRISC_VMLINUX_H_
#define __OPENRISC_VMLINUX_H_

#ifdef CONFIG_BLK_DEV_INITRD
extern char __initrd_start, __initrd_end;
#endif

extern u32 __dtb_start[];

#endif
