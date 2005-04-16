/* The head-file for the Shark
 * by Alexander Schulz
 *
 * Does the following:
 * - get the memory layout from firmware. This can only be done as long as the mmu
 *   is still on.
 * - switch the mmu off, so we have physical addresses
 * - copy the kernel to 0x08508000. This is done to have a fixed address where the
 *   C-parts (misc.c) are executed. This address must be known at compile-time,
 *   but the load-address of the kernel depends on how much memory is installed.
 * - Jump to this location.
 * - Set r8 with 0, r7 with the architecture ID for head.S
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
	
		.section	".start", "ax"

		b	__beginning
	
__ofw_data:	.long	0				@ the number of memory blocks
		.space  128				@ (startaddr,size) ...
		.space  128				@ bootargs
		.align

__beginning:	mov	r4, r0				@ save the entry to the firmware

		mov	r0, #0xC0			@ disable irq and fiq
		mov	r1, r0
		mrs	r3, cpsr
		bic	r2, r3, r0
		eor	r2, r2, r1
		msr	cpsr_c, r2

		mov	r0, r4				@ get the Memory layout from firmware
		adr	r1, __ofw_data
		add	r2, r1, #4
		mov	lr, pc
		b	ofw_init
		mov	r1, #0

		adr	r2, __mmu_off			@ calculate physical address
		sub	r2, r2, #0xf0000000		@ openprom maps us at f000 virt, 0e50 phys
		adr	r0, __ofw_data
		ldr	r0, [r0, #4]
		add	r2, r2, r0
		add	r2, r2, #0x00500000

		mrc	p15, 0, r3, c1, c0
		bic	r3, r3, #0xC			@ Write Buffer and DCache
		bic	r3, r3, #0x1000			@ ICache
		mcr	p15, 0, r3, c1, c0		@ disabled

		mov	r0, #0
		mcr	p15, 0, r0, c7, c7		@ flush I,D caches on v4
		mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
		mcr	p15, 0, r0, c8, c7		@ flush I,D TLBs on v4

		bic	r3, r3, #0x1			@ MMU
		mcr	p15, 0, r3, c1, c0		@ disabled

		mov	pc, r2

__copy_target:	.long	0x08508000
__copy_end:	.long	0x08608000
		
		.word	_start
		.word	__bss_start

		.align
__temp_stack:	.space 128

__mmu_off:
		adr	r0, __ofw_data
		ldr	r0, [r0, #4]
		orr	r0, r0, #0x00600000
	
		ldr	r1, __copy_end
		ldr	r3, __copy_target

/* r0 = 0x0e600000 (current end of kernelcode)
 * r3 = 0x08508000 (where it should begin)
 * r1 = 0x08608000 (end of copying area, 1MB)
 * The kernel is compressed, so 1 MB should be enough.
 * copy the kernel to the beginning of physical memory
 * We start from the highest address, so we can copy
 * from 0x08500000 to 0x08508000 if we have only 8MB
 */

	
__Copy:		ldr	r2, [r0], #-4
		str	r2, [r1], #-4
		teq	r1, r3
		bne	__Copy
		/* and jump to it */
		adr	r2, __go_on
		adr	r0, __ofw_data
		ldr	r0, [r0, #4]
		sub	r2, r2, r0
		sub	r2, r2, #0x00500000
		ldr	r0, __copy_target
		add	r2, r2, r0
		mov	pc, r2

__go_on:
		adr	sp, __temp_stack
		add	sp, sp, #128
		adr	r0, __ofw_data
		mov	lr, pc
		b	create_params
	
		mov	r8, #0
		mov	r7, #15
