#ifndef _POWERPC_KERNEL_SETUP_H
#define _POWERPC_KERNEL_SETUP_H

void check_for_initrd(void);
void do_init_bootmem(void);
void setup_panic(void);
extern int do_early_xmon;

#endif /* _POWERPC_KERNEL_SETUP_H */
