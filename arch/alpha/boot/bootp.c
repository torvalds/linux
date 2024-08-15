// SPDX-License-Identifier: GPL-2.0
/*
 * arch/alpha/boot/bootp.c
 *
 * Copyright (C) 1997 Jay Estabrook
 *
 * This file is used for creating a bootp file for the Linux/AXP kernel
 *
 * based significantly on the arch/alpha/boot/main.c of Linus Torvalds
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <generated/utsrelease.h>
#include <linux/mm.h>

#include <asm/console.h>
#include <asm/hwrpb.h>
#include <asm/io.h>

#include <stdarg.h>

#include "ksize.h"

extern unsigned long switch_to_osf_pal(unsigned long nr,
	struct pcb_struct *pcb_va, struct pcb_struct *pcb_pa,
	unsigned long *vptb);

extern void move_stack(unsigned long new_stack);

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

static inline void
load(unsigned long dst, unsigned long src, unsigned long count)
{
	memcpy((void *)dst, (void *)src, count);
}

/*
 * Start the kernel.
 */
static inline void
runkernel(void)
{
	__asm__ __volatile__(
		"bis %0,%0,$27\n\t"
		"jmp ($27)"
		: /* no outputs: it doesn't even return */
		: "r" (START_ADDR));
}

extern char _end;
#define KERNEL_ORIGIN \
	((((unsigned long)&_end) + 511) & ~511)

void
start_kernel(void)
{
	/*
	 * Note that this crufty stuff with static and envval
	 * and envbuf is because:
	 *
	 * 1. Frequently, the stack is short, and we don't want to overrun;
	 * 2. Frequently the stack is where we are going to copy the kernel to;
	 * 3. A certain SRM console required the GET_ENV output to stack.
	 *    ??? A comment in the aboot sources indicates that the GET_ENV
	 *    destination must be quadword aligned.  Might this explain the
	 *    behaviour, rather than requiring output to the stack, which
	 *    seems rather far-fetched.
	 */
	static long nbytes;
	static char envval[256] __attribute__((aligned(8)));
	static unsigned long initrd_start;

	srm_printk("Linux/AXP bootp loader for Linux " UTS_RELEASE "\n");
	if (INIT_HWRPB->pagesize != 8192) {
		srm_printk("Expected 8kB pages, got %ldkB\n",
		           INIT_HWRPB->pagesize >> 10);
		return;
	}
	if (INIT_HWRPB->vptb != (unsigned long) VPTB) {
		srm_printk("Expected vptb at %p, got %p\n",
			   VPTB, (void *)INIT_HWRPB->vptb);
		return;
	}
	pal_init();

	/* The initrd must be page-aligned.  See below for the 
	   cause of the magic number 5.  */
	initrd_start = ((START_ADDR + 5*KERNEL_SIZE + PAGE_SIZE) |
			(PAGE_SIZE-1)) + 1;
#ifdef INITRD_IMAGE_SIZE
	srm_printk("Initrd positioned at %#lx\n", initrd_start);
#endif

	/*
	 * Move the stack to a safe place to ensure it won't be
	 * overwritten by kernel image.
	 */
	move_stack(initrd_start - PAGE_SIZE);

	nbytes = callback_getenv(ENV_BOOTED_OSFLAGS, envval, sizeof(envval));
	if (nbytes < 0 || nbytes >= sizeof(envval)) {
		nbytes = 0;
	}
	envval[nbytes] = '\0';
	srm_printk("Loading the kernel...'%s'\n", envval);

	/* NOTE: *no* callbacks or printouts from here on out!!! */

	/* This is a hack, as some consoles seem to get virtual 20000000 (ie
	 * where the SRM console puts the kernel bootp image) memory
	 * overlapping physical memory where the kernel wants to be put,
	 * which causes real problems when attempting to copy the former to
	 * the latter... :-(
	 *
	 * So, we first move the kernel virtual-to-physical way above where
	 * we physically want the kernel to end up, then copy it from there
	 * to its final resting place... ;-}
	 *
	 * Sigh...  */

#ifdef INITRD_IMAGE_SIZE
	load(initrd_start, KERNEL_ORIGIN+KERNEL_SIZE, INITRD_IMAGE_SIZE);
#endif
        load(START_ADDR+(4*KERNEL_SIZE), KERNEL_ORIGIN, KERNEL_SIZE);
        load(START_ADDR, START_ADDR+(4*KERNEL_SIZE), KERNEL_SIZE);

	memset((char*)ZERO_PGE, 0, PAGE_SIZE);
	strcpy((char*)ZERO_PGE, envval);
#ifdef INITRD_IMAGE_SIZE
	((long *)(ZERO_PGE+256))[0] = initrd_start;
	((long *)(ZERO_PGE+256))[1] = INITRD_IMAGE_SIZE;
#endif

	runkernel();
}
