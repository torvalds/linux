/*
 * File:         arch/blackfin/kernel/entry.S
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>

#include <asm/mach-common/context.S>

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
.section .l1.text
#else
.text
#endif

ENTRY(_ret_from_fork)
	SP += -12;
	call _schedule_tail;
	SP += 12;
	r0 = [sp + PT_IPEND];
	cc = bittst(r0,1);
	if cc jump .Lin_kernel;
	RESTORE_CONTEXT
	rti;
.Lin_kernel:
	bitclr(r0,1);
	[sp + PT_IPEND] = r0;
	/* do a 'fake' RTI by jumping to [RETI]
	 * to avoid clearing supervisor mode in child
	*/
	RESTORE_ALL_SYS
	p0 = reti;
	jump (p0);
ENDPROC(_ret_from_fork)

ENTRY(_sys_fork)
	r0 = -EINVAL;
	rts;
ENDPROC(_sys_fork)

ENTRY(_sys_vfork)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	call _bfin_vfork;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_vfork)

ENTRY(_sys_clone)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	call _bfin_clone;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_clone)

ENTRY(_sys_rt_sigreturn)
	r0 = sp;
	r0 += 24;
	[--sp] = rets;
	SP += -12;
	call _do_rt_sigreturn;
	SP += 12;
	rets = [sp++];
	rts;
ENDPROC(_sys_rt_sigreturn)
