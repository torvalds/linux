/*
 * FILE NAME
 *	arch/mips/vr41xx/common/int-handler.S
 *
 * BRIEF MODULE DESCRIPTION
 *	Interrupt dispatcher for the NEC VR4100 series.
 *
 * Author: Yoichi Yuasa
 *         yyuasa@mvista.com or source@mvista.com
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4100 series are supported.
 *
 *  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *  - Coped with INTASSIGN of NEC VR4133.
 */
#include <asm/asm.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

		.text
		.set	noreorder

		.align	5
		NESTED(vr41xx_handle_interrupt, PT_SIZE, ra)
		.set	noat
		SAVE_ALL
		CLI
		.set	at
		.set	noreorder

		/*
		 * Get the pending interrupts
		 */
		mfc0	t0, CP0_CAUSE
		mfc0	t1, CP0_STATUS
		andi	t0, 0xff00
		and	t0, t0, t1

		andi	t1, t0, CAUSEF_IP7	# MIPS timer interrupt
		bnez	t1, handle_irq
		li	a0, 7

		andi	t1, t0, 0x7800		# check for Int1-4
		beqz	t1, 1f

		andi	t1, t0, CAUSEF_IP3	# check for Int1
		bnez	t1, handle_int
		li	a0, 3

		andi	t1, t0, CAUSEF_IP4	# check for Int2
		bnez	t1, handle_int
		li	a0, 4

		andi	t1, t0, CAUSEF_IP5	# check for Int3
		bnez	t1, handle_int
		li	a0, 5

		andi	t1, t0, CAUSEF_IP6	# check for Int4
		bnez	t1, handle_int
		li	a0, 6

1:
		andi	t1, t0, CAUSEF_IP2	# check for Int0
		bnez	t1, handle_int
		li	a0, 2

		andi	t1, t0, CAUSEF_IP0	# check for IP0
		bnez	t1, handle_irq
		li	a0, 0

		andi	t1, t0, CAUSEF_IP1	# check for IP1
		bnez	t1, handle_irq
		li	a0, 1

		jal	spurious_interrupt
		nop
		j	ret_from_irq
		 nop

handle_int:
		jal	irq_dispatch
		move	a1, sp
		j	ret_from_irq
		nop

handle_irq:
		jal	do_IRQ
		move	a1, sp
		j	ret_from_irq
		END(vr41xx_handle_interrupt)
