/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007-2017 Cavium, Inc.
 */
#include <asm/asm.h>
#include <asm/regdef.h>

#define CVMSEG_BASE	-32768
#define CVMSEG_SIZE	6912
#define SAVE_REG(r)	sd $r, CVMSEG_BASE + CVMSEG_SIZE - ((32 - r) * 8)($0)

        NESTED(octeon_wdt_nmi_stage2, 0, sp)
	.set 	push
	.set 	noreorder
	.set 	noat
	/* Clear Dcache so cvmseg works right. */
	cache	1,0($0)
	/* Use K0 to do a read/modify/write of CVMMEMCTL */
	dmfc0	k0, $11, 7
	/* Clear out the size of CVMSEG	*/
	dins	k0, $0, 0, 6
	/* Set CVMSEG to its largest value */
	ori	k0, k0, 0x1c0 | 54
	/* Store the CVMMEMCTL value */
	dmtc0	k0, $11, 7
	/*
	 * Restore K0 from the debug scratch register, it was saved in
	 * the boot-vector code.
	 */
	dmfc0	k0, $31

	/*
	 * Save all registers to the top CVMSEG. This shouldn't
	 * corrupt any state used by the kernel. Also all registers
	 * should have the value right before the NMI.
	 */
	SAVE_REG(0)
	SAVE_REG(1)
	SAVE_REG(2)
	SAVE_REG(3)
	SAVE_REG(4)
	SAVE_REG(5)
	SAVE_REG(6)
	SAVE_REG(7)
	SAVE_REG(8)
	SAVE_REG(9)
	SAVE_REG(10)
	SAVE_REG(11)
	SAVE_REG(12)
	SAVE_REG(13)
	SAVE_REG(14)
	SAVE_REG(15)
	SAVE_REG(16)
	SAVE_REG(17)
	SAVE_REG(18)
	SAVE_REG(19)
	SAVE_REG(20)
	SAVE_REG(21)
	SAVE_REG(22)
	SAVE_REG(23)
	SAVE_REG(24)
	SAVE_REG(25)
	SAVE_REG(26)
	SAVE_REG(27)
	SAVE_REG(28)
	SAVE_REG(29)
	SAVE_REG(30)
	SAVE_REG(31)
	/* Write zero to all CVMSEG locations per Core-15169 */
	dli	a0, CVMSEG_SIZE - (33 * 8)
1:	sd	zero, CVMSEG_BASE(a0)
	daddiu	a0, a0, -8
	bgez	a0, 1b
	nop
	/* Set the stack to begin right below the registers */
	dli	sp, CVMSEG_BASE + CVMSEG_SIZE - (32 * 8)
	/* Load the address of the third stage handler */
	dla	$25, octeon_wdt_nmi_stage3
	/* Call the third stage handler */
	jal	$25
	/* a0 is the address of the saved registers */
	 move	a0, sp
	/* Loop forvever if we get here. */
2:	b	2b
	nop
	.set pop
	END(octeon_wdt_nmi_stage2)
