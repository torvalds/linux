/*
 * linux/arch/mips/tx4927/common/tx4927_irq_handler.S
 *
 * Primary interrupt handler for tx4927 based systems
 *
 * Author: MontaVista Software, Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *         source@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
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
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/tx4927/tx4927.h>

		.align	5
		NESTED(tx4927_irq_handler, PT_SIZE, sp)
		SAVE_ALL
		CLI
		.set	at

		mfc0	t0, CP0_CAUSE  
		mfc0	t1, CP0_STATUS
		and	t0, t1
        
		andi	t1, t0, STATUSF_IP7	/* cpu timer */
		bnez	t1, ll_ip7
		
		/* IP6..IP3 multiplexed -- do not use */

		andi	t1, t0, STATUSF_IP2	/* tx4927 pic */
		bnez	t1, ll_ip2

		andi	t1, t0, STATUSF_IP0	/* user line 0 */
		bnez	t1, ll_ip0

		andi	t1, t0, STATUSF_IP1	/* user line 1 */
		bnez	t1, ll_ip1

		.set	reorder

		/* wrong alarm or masked ... */
		j	spurious_interrupt
		nop
		END(tx4927_irq_handler)

		.align	5


ll_ip7:
		li	a0, TX4927_IRQ_CPU_TIMER
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_ip2:
		jal	tx4927_irq_nested
		nop
		beqz 	v0, goto_spurious_interrupt
		nop
		move	a0, v0
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

goto_spurious_interrupt:
	j spurious_interrupt
	nop

ll_ip1:
		li	a0, TX4927_IRQ_USER1
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq

ll_ip0:
		li	a0, TX4927_IRQ_USER0
		move	a1, sp
		jal	do_IRQ
		j	ret_from_irq
