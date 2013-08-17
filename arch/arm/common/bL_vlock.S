/*
 * vlock.S - simple voting lock implementation for ARM
 *
 * Created by:  Dave Martin, 2012-08-16
 * Copyright:   (C) 2012  Linaro Limited
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <linux/linkage.h>
#include <asm/bL_vlock.h>

#if VLOCK_VOTING_SIZE > 4
#define FEW(x...)
#define MANY(x...) x
#else
#define FEW(x...) x
#define MANY(x...)
#endif

@ voting lock for first-man coordination

.macro voting_begin rcpu:req, rbase:req, rscratch:req
        mov     \rscratch, #1
        strb    \rscratch, [\rbase, \rcpu]
.endm

.macro voting_end rcpu:req, rbase:req, rscratch:req
        mov     \rscratch, #0
        strb    \rscratch, [\rbase, \rcpu]
        dsb
        sev
.endm

@ r0: lock structure base
@ r1: CPU ID (0-based index within cluster)
ENTRY(vlock_trylock)
        add     r1, r1, #VLOCK_VOTING_OFFSET

        voting_begin    r0, r1, r2

        ldrb    r2, [r0, #VLOCK_OWNER_OFFSET]   @ check whether lock is held
        cmp     r2, #VLOCK_OWNER_NONE
        bne     trylock_fail                    @ fail if so

        strb    r1, [r0, #VLOCK_OWNER_OFFSET]   @ submit my vote

        voting_end      r0, r1, r2

        @ Wait for the current round of voting to finish:

 MANY(  mov     r3, #VLOCK_VOTING_OFFSET                        )
0:
 MANY(  ldr     r2, [r0, r3]                                    )
 FEW(   ldr     r2, [r0, #VLOCK_VOTING_OFFSET]                  )
        cmp     r2, #0
        wfene
        bne     0b
 MANY(  add     r3, r3, #4                                      )
 MANY(  cmp     r3, #VLOCK_VOTING_OFFSET + VLOCK_VOTING_SIZE    )
 MANY(  bne     0b                                              )

        @ Check who won:

        ldrb    r2, [r0, #VLOCK_OWNER_OFFSET]
        eor     r0, r1, r2                      @ zero if I won, else nonzero
        bx      lr

trylock_fail:
        voting_end      r0, r1, r2
        mov     r0, #1                          @ nonzero indicates that I lost
        bx      lr
ENDPROC(vlock_trylock)

@ r0: lock structure base
ENTRY(vlock_unlock)
        mov     r1, #VLOCK_OWNER_NONE
        dsb
        strb    r1, [r0, #VLOCK_OWNER_OFFSET]
        dsb
        sev
        mov     pc, lr
ENDPROC(vlock_unlock)
