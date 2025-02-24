// SPDX-License-Identifier: GPL-2.0
/*
 * arch/alpha/boot/main.c
 *
 * Copyright (C) 1994, 1995 Linus Torvalds
 *
 * This file is the bootloader for the Linux/AXP kernel
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <generated/utsrelease.h>
#include <asm/console.h>
#include <asm/hwrpb.h>
#include "ksize.h"

#define VPTB ((unsigned long *)0x200000000)
#define L1   ((unsigned long *)0x200802000)
#define EXPECTED_PAGE_SIZE 8192
#define ENV_BUF_SIZE 256

extern unsigned long switch_to_osf_pal(unsigned long nr,
                                      struct pcb_struct *pcb_va,
                                      struct pcb_struct *pcb_pa,
                                      unsigned long *vptb);

struct hwrpb_struct *hwrpb = INIT_HWRPB;
static struct pcb_struct pcb_va[1];

/*
 * Find a physical address of a virtual object..
 *
 * This is easy using the virtual page table address.
 */
#define find_pa(ptr) ((void *)(((VPTB[((unsigned long)(ptr)) >> 13] >> 32) << 13) | \
                              (((unsigned long)(ptr)) & 0x1fff)))

#define get_env(var, buf) ({ \
    long _r = callback_getenv(var, buf, ENV_BUF_SIZE - 1); \
    if (_r >= 0) buf[_r & 0xff] = '\0'; \
    _r; \
})

/*
 * This function moves into OSF/1 pal-code, and has a temporary
 * PCB for that. The kernel proper should replace this PCB with
 * the real one as soon as possible.
 *
 * The page table muckery in here depends on the fact that the boot
 * code has the L1 page table identity-map itself in the second PTE
 * in the L1 page table. Thus the L1-page is virtually addressable
 * itself (through three levels) at virtual address 0x200802000.
 */
static void pal_init(void)
{
    struct percpu_struct *percpu = (struct percpu_struct *)
        (INIT_HWRPB->processor_offset + (unsigned long)INIT_HWRPB);
    pcb_va->ptbr = L1[1] >> 32;
    pcb_va->flags = 1;
    srm_printk("Switching to OSF PAL-code .. ");
    long i = switch_to_osf_pal(2, pcb_va, find_pa(pcb_va), VPTB);
    if (i) {
        srm_printk("failed, code %ld\n", i);
        __halt();
    }
    percpu->pal_revision = percpu->palcode_avail[2];
    srm_printk("Ok (rev %lx)\n", percpu->pal_revision);
    tbia();
}

static long boot_device_open(char *buf)
{
    long len = get_env(ENV_BOOTED_DEV, buf);
    return len < 0 ? len : callback_open(buf, len & 0xff);
}

static long load_kernel(long dev, char *buf)
{
    extern char _end;
    long boot_size = &_end - (char *)BOOT_ADDR;
    long len = get_env(ENV_BOOTED_FILE, buf);
    if (len > 0)
        srm_printk("Boot file specification (%s) not implemented\n", buf);
    return len < 0 ? len :
           callback_read(dev, KERNEL_SIZE, (void *)START_ADDR,
                        boot_size/512 + 1);
}

/*
 * Start the kernel.
 */
static void runkernel(void)
{
    __asm__ __volatile__(
        "bis %1,%1,$30\n\t"
        "bis %0,%0,$26\n\t"
        "ret ($26)"
        : : "r" (START_ADDR),
            "r" (PAGE_SIZE + INIT_STACK)
        : "$30", "$26", "memory"
    );
}

void start_kernel(void)
{
    char buf[ENV_BUF_SIZE];
    long dev;
    srm_printk("Linux/AXP bootloader for Linux " UTS_RELEASE "\n");
    if (INIT_HWRPB->pagesize != EXPECTED_PAGE_SIZE) {
        srm_printk("Expected 8kB pages, got %ldkB\n",
                   INIT_HWRPB->pagesize >> 10);
        __halt();
    }
    pal_init();
    dev = boot_device_open(buf);
    if (dev < 0) {
        srm_printk("Unable to open boot device: %016lx\n", dev);
        __halt();
    }
    dev &= 0xffffffff;
    srm_printk("Loading vmlinux ...");
    long i = load_kernel(dev, buf);
    if (i != KERNEL_SIZE) {
        srm_printk("Failed (%lx)\n", i);
        goto cleanup;
    }
    if (get_env(ENV_BOOTED_OSFLAGS, buf) >= 0)
        strcpy((char *)ZERO_PGE, buf);
    srm_printk(" Ok\nNow booting the kernel\n");
    callback_close(dev);
    runkernel();
cleanup:
    callback_close(dev);
    __halt();
}
