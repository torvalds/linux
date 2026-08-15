/* SPDX-License-Identifier: GPL-2.0-or-later */
/**
 * Adopted for the Linux kernel from IPXE project, see
 * https://github.com/ipxe/ipxe/blob/master/src/arch/riscv32/libgcc/llshift.S
 */

#include <linux/linkage.h>
#include <asm/asm.h>

/**
 * Logical shift right
 *
 * @v a1:a0             Value to shift
 * @v a2                Shift amount
 * @ret a1:a0           Shifted value
 */

SYM_FUNC_START(__lshrdi3)

        /* Perform shift by 32 bits, if applicable */
        li      t0, 32
        sub     t1, t0, a2
        bgtz    t1, 1f
        mv      a0, a1
        mv      a1, zero
1:      /* Perform shift by modulo-32 bits, if applicable */
        andi    a2, a2, 0x1f
        beqz    a2, 2f
        sll     t2, a1, t1
        srl     a1, a1, a2
        srl     a0, a0, a2
        or      a0, a0, t2
2:      ret

SYM_FUNC_END(__lshrdi3)
EXPORT_SYMBOL(__lshrdi3)
