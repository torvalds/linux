/* ===----- trampoline_setup.c - Implement __trampoline_setup -------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

extern void __clear_cache(void* start, void* end);

/*
 * The ppc compiler generates calls to __trampoline_setup() when creating 
 * trampoline functions on the stack for use with nested functions.
 * This function creates a custom 40-byte trampoline function on the stack 
 * which loads r11 with a pointer to the outer function's locals
 * and then jumps to the target nested function.
 */

#if __ppc__ && !defined(__powerpc64__)
COMPILER_RT_ABI void
__trampoline_setup(uint32_t* trampOnStack, int trampSizeAllocated, 
                   const void* realFunc, void* localsPtr)
{
    /* should never happen, but if compiler did not allocate */
    /* enough space on stack for the trampoline, abort */
    if ( trampSizeAllocated < 40 )
        compilerrt_abort();
    
    /* create trampoline */
    trampOnStack[0] = 0x7c0802a6;    /* mflr r0 */
    trampOnStack[1] = 0x4800000d;    /* bl Lbase */
    trampOnStack[2] = (uint32_t)realFunc;
    trampOnStack[3] = (uint32_t)localsPtr;
    trampOnStack[4] = 0x7d6802a6;    /* Lbase: mflr r11 */
    trampOnStack[5] = 0x818b0000;    /* lwz    r12,0(r11) */
    trampOnStack[6] = 0x7c0803a6;    /* mtlr r0 */
    trampOnStack[7] = 0x7d8903a6;    /* mtctr r12 */
    trampOnStack[8] = 0x816b0004;    /* lwz    r11,4(r11) */
    trampOnStack[9] = 0x4e800420;    /* bctr */
    
    /* clear instruction cache */
    __clear_cache(trampOnStack, &trampOnStack[10]);
}
#endif /* __ppc__ && !defined(__powerpc64__) */
