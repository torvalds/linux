/*
 * linux/arch/arm/mach-omap1/sleep.S
 *
 * Low-level OMAP730/1510/1610 sleep/wakeUp support
 *
 * Initial SA1110 code:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Adapted for PXA by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * Support for OMAP1510/1610 by Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/arch/io.h>
#include <asm/arch/pm.h>

		.text


/*
 * Forces OMAP into deep sleep state
 *
 * omapXXXX_cpu_suspend()
 *
 * The values of the registers ARM_IDLECT1 and ARM_IDLECT2 are passed
 * as arg0 and arg1 from caller. arg0 is stored in register r0 and arg1
 * in register r1.
 *
 * Note: This code get's copied to internal SRAM at boot. When the OMAP
 *	 wakes up it continues execution at the point it went to sleep.
 *
 * Note: Because of errata work arounds we have processor specific functions
 *       here. They are mostly the same, but slightly different.
 *
 */

#if defined(CONFIG_ARCH_OMAP730)
ENTRY(omap730_cpu_suspend)

	@ save registers on stack
	stmfd	sp!, {r0 - r12, lr}

	@ Drain write cache
	mov	r4, #0
	mcr	p15, 0, r0, c7, c10, 4
	nop

	@ load base address of Traffic Controller
	mov	r6, #TCMIF_ASM_BASE & 0xff000000
	orr	r6, r6, #TCMIF_ASM_BASE & 0x00ff0000
	orr	r6, r6, #TCMIF_ASM_BASE & 0x0000ff00

	@ prepare to put SDRAM into self-refresh manually
	ldr	r7, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]
	orr	r9, r7, #SELF_REFRESH_MODE & 0xff000000
	orr	r9, r9, #SELF_REFRESH_MODE & 0x000000ff
	str	r9, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]

	@ prepare to put EMIFS to Sleep
	ldr	r8, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]
	orr	r9, r8, #IDLE_EMIFS_REQUEST & 0xff
	str	r9, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ load base address of ARM_IDLECT1 and ARM_IDLECT2
	mov	r4, #CLKGEN_REG_ASM_BASE & 0xff000000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x00ff0000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x0000ff00

	@ turn off clock domains
	@ do not disable PERCK (0x04)
	mov	r5, #OMAP730_IDLECT2_SLEEP_VAL & 0xff
	orr	r5, r5, #OMAP730_IDLECT2_SLEEP_VAL & 0xff00
	strh	r5, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]

	@ request ARM idle
	mov	r3, #OMAP730_IDLECT1_SLEEP_VAL & 0xff
	orr	r3, r3, #OMAP730_IDLECT1_SLEEP_VAL & 0xff00
	strh	r3, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

	@ disable instruction cache
	mrc	p15, 0, r9, c1, c0, 0
	bic	r2, r9, #0x1000
	mcr	p15, 0, r2, c1, c0, 0
	nop

/*
 * Let's wait for the next wake up event to wake us up. r0 can't be
 * used here because r0 holds ARM_IDLECT1
 */
	mov	r2, #0
	mcr	p15, 0, r2, c7, c0, 4		@ wait for interrupt
/*
 * omap730_cpu_suspend()'s resume point.
 *
 * It will just start executing here, so we'll restore stuff from the
 * stack.
 */
	@ re-enable Icache
	mcr	p15, 0, r9, c1, c0, 0

	@ reset the ARM_IDLECT1 and ARM_IDLECT2.
	strh	r1, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]
	strh	r0, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

	@ Restore EMIFF controls
	str	r7, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]
	str	r8, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ restore regs and return
	ldmfd	sp!, {r0 - r12, pc}

ENTRY(omap730_cpu_suspend_sz)
	.word	. - omap730_cpu_suspend
#endif /* CONFIG_ARCH_OMAP730 */

#ifdef CONFIG_ARCH_OMAP15XX
ENTRY(omap1510_cpu_suspend)

	@ save registers on stack
	stmfd	sp!, {r0 - r12, lr}

	@ load base address of Traffic Controller
	mov	r4, #TCMIF_ASM_BASE & 0xff000000
	orr	r4, r4, #TCMIF_ASM_BASE & 0x00ff0000
	orr	r4, r4, #TCMIF_ASM_BASE & 0x0000ff00

	@ work around errata of OMAP1510 PDE bit for TC shut down
	@ clear PDE bit
	ldr	r5, [r4, #EMIFS_CONFIG_ASM_OFFSET & 0xff]
	bic	r5, r5, #PDE_BIT & 0xff
	str	r5, [r4, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ set PWD_EN bit
	and	r5, r5, #PWD_EN_BIT & 0xff
	str	r5, [r4, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ prepare to put SDRAM into self-refresh manually
	ldr	r5, [r4, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]
	orr	r5, r5, #SELF_REFRESH_MODE & 0xff000000
	orr	r5, r5, #SELF_REFRESH_MODE & 0x000000ff
	str	r5, [r4, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]

	@ prepare to put EMIFS to Sleep
	ldr	r5, [r4, #EMIFS_CONFIG_ASM_OFFSET & 0xff]
	orr	r5, r5, #IDLE_EMIFS_REQUEST & 0xff
	str	r5, [r4, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ load base address of ARM_IDLECT1 and ARM_IDLECT2
	mov	r4, #CLKGEN_REG_ASM_BASE & 0xff000000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x00ff0000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x0000ff00

	@ turn off clock domains
	mov	r5, #OMAP1510_IDLE_CLOCK_DOMAINS & 0xff
	orr	r5, r5, #OMAP1510_IDLE_CLOCK_DOMAINS & 0xff00
	strh	r5, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]

	@ request ARM idle
	mov	r3, #OMAP1510_DEEP_SLEEP_REQUEST & 0xff
	orr	r3, r3, #OMAP1510_DEEP_SLEEP_REQUEST & 0xff00
	strh	r3, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

	mov	r5, #IDLE_WAIT_CYCLES & 0xff
	orr	r5, r5, #IDLE_WAIT_CYCLES & 0xff00
l_1510_2:
	subs	r5, r5, #1
	bne	l_1510_2
/*
 * Let's wait for the next wake up event to wake us up. r0 can't be
 * used here because r0 holds ARM_IDLECT1
 */
	mov	r2, #0
	mcr	p15, 0, r2, c7, c0, 4		@ wait for interrupt
/*
 * omap1510_cpu_suspend()'s resume point.
 *
 * It will just start executing here, so we'll restore stuff from the
 * stack, reset the ARM_IDLECT1 and ARM_IDLECT2.
 */
	strh	r1, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]
	strh	r0, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

	@ restore regs and return
	ldmfd	sp!, {r0 - r12, pc}

ENTRY(omap1510_cpu_suspend_sz)
	.word	. - omap1510_cpu_suspend
#endif /* CONFIG_ARCH_OMAP15XX */

#if defined(CONFIG_ARCH_OMAP16XX)
ENTRY(omap1610_cpu_suspend)

	@ save registers on stack
	stmfd	sp!, {r0 - r12, lr}

	@ Drain write cache
	mov	r4, #0
	mcr	p15, 0, r0, c7, c10, 4
	nop

	@ Load base address of Traffic Controller
	mov	r6, #TCMIF_ASM_BASE & 0xff000000
	orr	r6, r6, #TCMIF_ASM_BASE & 0x00ff0000
	orr	r6, r6, #TCMIF_ASM_BASE & 0x0000ff00

	@ Prepare to put SDRAM into self-refresh manually
	ldr	r7, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]
	orr	r9, r7, #SELF_REFRESH_MODE & 0xff000000
	orr	r9, r9, #SELF_REFRESH_MODE & 0x000000ff
	str	r9, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]

	@ Prepare to put EMIFS to Sleep
	ldr	r8, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]
	orr	r9, r8, #IDLE_EMIFS_REQUEST & 0xff
	str	r9, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ Load base address of ARM_IDLECT1 and ARM_IDLECT2
	mov	r4, #CLKGEN_REG_ASM_BASE & 0xff000000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x00ff0000
	orr	r4, r4, #CLKGEN_REG_ASM_BASE & 0x0000ff00

	@ Turn off clock domains
	@ Do not disable PERCK (0x04)
	mov	r5, #OMAP1610_IDLECT2_SLEEP_VAL & 0xff
	orr	r5, r5, #OMAP1610_IDLECT2_SLEEP_VAL & 0xff00
	strh	r5, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]

	@ Request ARM idle
	mov	r3, #OMAP1610_IDLECT1_SLEEP_VAL & 0xff
	orr	r3, r3, #OMAP1610_IDLECT1_SLEEP_VAL & 0xff00
	strh	r3, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

/*
 * Let's wait for the next wake up event to wake us up. r0 can't be
 * used here because r0 holds ARM_IDLECT1
 */
	mov	r2, #0
	mcr	p15, 0, r2, c7, c0, 4		@ wait for interrupt

	@ Errata (HEL3SU467, section 1.4.4) specifies nop-instructions
	@ according to this formula:
	@ 2 + (4*DPLL_MULT)/DPLL_DIV/ARMDIV
	@ Max DPLL_MULT = 18
	@ DPLL_DIV = 1
	@ ARMDIV = 1
	@ => 74 nop-instructions
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@10
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@20
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@30
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@40
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@50
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@60
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop	@70
	nop
	nop
	nop
	nop	@74
/*
 * omap1610_cpu_suspend()'s resume point.
 *
 * It will just start executing here, so we'll restore stuff from the
 * stack.
 */
	@ Restore the ARM_IDLECT1 and ARM_IDLECT2.
	strh	r1, [r4, #ARM_IDLECT2_ASM_OFFSET & 0xff]
	strh	r0, [r4, #ARM_IDLECT1_ASM_OFFSET & 0xff]

	@ Restore EMIFF controls
	str	r7, [r6, #EMIFF_SDRAM_CONFIG_ASM_OFFSET & 0xff]
	str	r8, [r6, #EMIFS_CONFIG_ASM_OFFSET & 0xff]

	@ Restore regs and return
	ldmfd	sp!, {r0 - r12, pc}

ENTRY(omap1610_cpu_suspend_sz)
	.word	. - omap1610_cpu_suspend
#endif /* CONFIG_ARCH_OMAP16XX */
