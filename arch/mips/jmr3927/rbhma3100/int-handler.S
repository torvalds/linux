/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *              ahennessy@mvista.com
 *
 * Based on arch/mips/tsdb/kernel/int-handler.S
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/jmr3927/jmr3927.h>

	/* A lot of complication here is taken away because:
	 *
	 * 1) We handle one interrupt and return, sitting in a loop
	 *    and moving across all the pending IRQ bits in the cause
	 *    register is _NOT_ the answer, the common case is one
	 *    pending IRQ so optimize in that direction.
	 *
	 * 2) We need not check against bits in the status register
	 *    IRQ mask, that would make this routine slow as hell.
	 *
	 * 3) Linux only thinks in terms of all IRQs on or all IRQs
	 *    off, nothing in between like BSD spl() brain-damage.
	 *
	 */

/* Flush write buffer (needed?)
 * NOTE: TX39xx performs "non-blocking load", so explicitly use the target
 * register of LBU to flush immediately.
 */
#define FLUSH_WB(tmp)	\
	la	tmp, JMR3927_IOC_REV_ADDR; \
	lbu	tmp, (tmp); \
	move	tmp, zero;

	.text
	.set	noreorder
	.set	noat
	.align	5
	NESTED(jmr3927_IRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at
	jal	jmr3927_irc_irqdispatch
	 move	a0, sp
	FLUSH_WB(t0)
	j	ret_from_irq
	 nop
	END(jmr3927_IRQ)
