/*
 * dwc_otg_fiq_fsm.S - assembly stub for the FSM FIQ
 *
 * Copyright (c) 2013 Raspberry Pi Foundation
 *
 * Author: Jonathan Bell <jonathan@raspberrypi.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *	* Neither the name of Raspberry Pi nor the
 *	  names of its contributors may be used to endorse or promote products
 *	  derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <asm/assembler.h>
#include <linux/linkage.h>


.text

.global _dwc_otg_fiq_stub_end;

/**
  * _dwc_otg_fiq_stub() - entry copied to the FIQ vector page to allow
  * a C-style function call with arguments from the FIQ banked registers.
  * r0 = &hcd->fiq_state
  * r1 = &hcd->num_channels
  * r2 = &hcd->dma_buffers
  * Tramples: r0, r1, r2, r4, fp, ip
  */

ENTRY(_dwc_otg_fiq_stub)
	/* Stash unbanked regs - SP will have been set up for us */
	mov ip, sp;
	stmdb sp!, {r0-r12, lr};
#ifdef FIQ_DEBUG
	// Cycle profiling - read cycle counter at start
	mrc p15, 0, r5, c15, c12, 1;
#endif
	/* r11 = fp, don't trample it */
	mov r4, fp;
	/* set EABI frame size */
	sub fp, ip, #512;

	/* for fiq NOP mode - just need state */
	mov r0, r8;
	/* r9 = num_channels */
	mov r1, r9;
	/* r10 = struct *dma_bufs */
//	mov r2, r10;

	/* r4 = &fiq_c_function */
	blx r4;
#ifdef FIQ_DEBUG
	mrc p15, 0, r4, c15, c12, 1;
	subs r5, r5, r4;
	// r5 is now the cycle count time for executing the FIQ. Store it somewhere?
#endif
	ldmia sp!, {r0-r12, lr};
	subs pc, lr, #4;
_dwc_otg_fiq_stub_end:
END(_dwc_otg_fiq_stub)

