/*
 * arch/alpha/boot/main.c
 *
 * Copyright (C) 1994, 1995 Linus Torvalds
 *
 * This file is the bootloader for the Linux/AXP kernel
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <generated/utsrelease.h>
#include <linux/mm.h>

#include <asm/console.h>
#include <asm/hwrpb.h>
#include <asm/pgtable.h>

#include <stdarg.h>

#include "ksize.h"

extern int vsprintf(char *, const char *, va_list);
extern unsigned long switch_to_osf_pal(unsigned long nr,
	struct pcb_struct * pcb_va, struct pcb_struct * pcb_pa,
	unsigned long *vptb);
struct hwrpb_struct *hwrpb = INIT_HWRPB;
static struct pcb_struct pcb_va[1];

/*
 * Find a physical address of a virtual object..
 *
 * This is easy using the virtual page table address.
 */

static inline void *
find_pa(unsigned long *vptb, void *ptr)
{
	unsigned long address = (unsigned long) ptr;
	unsigned long result;

	result = vptb[address >> 13];
	result >>= 32;
	result <<= 13;
	result |= address & 0x1fff;
	return (void *) result;
}	

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

#define VPTB	((unsigned long *) 0x200000000)
#define L1	((unsigned long *) 0x200802000)

void
pal_init(void)
{
	unsigned long i, rev;
	struct percpu_struct * percpu;
	struct pcb_struct * pcb_pa;

	/* Create the dummy PCB.  */
	pcb_va->ksp = 0;
	pcb_va->usp = 0;
	pcb_va->ptbr = L1[1] >> 32;
	pcb_va->asn = 0;
	pcb_va->pcc = 0;
	pcb_va->unique = 0;
	pcb_va->flags = 1;
	pcb_va->res1 = 0;
	pcb_va->res2 = 0;
	pcb_pa = find_pa(VPTB, pcb_va);

	/*
	 * a0 = 2 (OSF)
	 * a1 = return address, but we give the asm the vaddr of the PCB
	 * a2 = physical addr of PCB
	 * a3 = new virtual page table pointer
	 * a4 = KSP (but the asm sets it)
	 */
	srm_printk("Switching to OSF PAL-code .. ");

	i = switch_to_osf_pal(2, pcb_va, pcb_pa, VPTB);
	if (i) {
		srm_printk("failed, code %ld\n", i);
		__halt();
	}

	percpu = (struct percpu_struct *)
		(INIT_HWRPB->processor_offset + (unsigned long) INIT_HWRPB);
	rev = percpu->pal_revision = percpu->palcode_avail[2];

	srm_printk("Ok (rev %lx)\n", rev);

	tbia(); /* do it directly in case we are SMP */
}

static inline long openboot(void)
{
	char bootdev[256];
	long result;

	result = callback_getenv(ENV_BOOTED_DEV, bootdev, 255);
	if (result < 0)
		return result;
	return callback_open(bootdev, result & 255);
}

static inline long close(long dev)
{
	return callback_close(dev);
}

static inline long load(long dev, unsigned long addr, unsigned long count)
{
	char bootfile[256];
	extern char _end;
	long result, boot_size = &_end - (char *) BOOT_ADDR;

	result = callback_getenv(ENV_BOOTED_FILE, bootfile, 255);
	if (result < 0)
		return result;
	result &= 255;
	bootfile[result] = '\0';
	if (result)
		srm_printk("Boot file specification (%s) not implemented\n",
		       bootfile);
	return callback_read(dev, count, (void *)addr, boot_size/512 + 1);
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
		: /* no outputs: it doesn't even return */
		: "r" (START_ADDR),
		  "r" (PAGE_SIZE + INIT_STACK));
}

void start_kernel(void)
{
	long i;
	long dev;
	int nbytes;
	char envval[256];

	srm_printk("Linux/AXP bootloader for Linux " UTS_RELEASE "\n");
	if (INIT_HWRPB->pagesize != 8192) {
		srm_printk("Expected 8kB pages, got %ldkB\n", INIT_HWRPB->pagesize >> 10);
		return;
	}
	pal_init();
	dev = openboot();
	if (dev < 0) {
		srm_printk("Unable to open boot device: %016lx\n", dev);
		return;
	}
	dev &= 0xffffffff;
	srm_printk("Loading vmlinux ...");
	i = load(dev, START_ADDR, KERNEL_SIZE);
	close(dev);
	if (i != KERNEL_SIZE) {
		srm_printk("Failed (%lx)\n", i);
		return;
	}

	nbytes = callback_getenv(ENV_BOOTED_OSFLAGS, envval, sizeof(envval));
	if (nbytes < 0) {
		nbytes = 0;
	}
	envval[nbytes] = '\0';
	strcpy((char*)ZERO_PGE, envval);

	srm_printk(" Ok\nNow booting the kernel\n");
	runkernel();
	for (i = 0 ; i < 0x100000000 ; i++)
		/* nothing */;
	__halt();
}
