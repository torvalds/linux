/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <asm/asm.h>

ENTRY(__memmove)
WEAK(memmove)
        move    t0, a0
        move    t1, a1

        beq     a0, a1, exit_memcpy
        beqz    a2, exit_memcpy
        srli    t2, a2, 0x2

        slt     t3, a0, a1
        beqz    t3, do_reverse

        andi    a2, a2, 0x3
        li      t4, 1
        beqz    t2, byte_copy

word_copy:
        lw      t3, 0(a1)
        addi    t2, t2, -1
        addi    a1, a1, 4
        sw      t3, 0(a0)
        addi    a0, a0, 4
        bnez    t2, word_copy
        beqz    a2, exit_memcpy
        j       byte_copy

do_reverse:
        add     a0, a0, a2
        add     a1, a1, a2
        andi    a2, a2, 0x3
        li      t4, -1
        beqz    t2, reverse_byte_copy

reverse_word_copy:
        addi    a1, a1, -4
        addi    t2, t2, -1
        lw      t3, 0(a1)
        addi    a0, a0, -4
        sw      t3, 0(a0)
        bnez    t2, reverse_word_copy
        beqz    a2, exit_memcpy

reverse_byte_copy:
        addi    a0, a0, -1
        addi    a1, a1, -1

byte_copy:
        lb      t3, 0(a1)
        addi    a2, a2, -1
        sb      t3, 0(a0)
        add     a1, a1, t4
        add     a0, a0, t4
        bnez    a2, byte_copy

exit_memcpy:
        move a0, t0
        move a1, t1
        ret
END(__memmove)
