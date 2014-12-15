#include <linux/linkage.h>
/*
void swap_mem(unsigned char* src, unsigned char* dst, unsigned int size)
*/
    .section .text
    ENTRY(swap_mem)
    stmdb sp!, {lr}
    vpush      {q0,q1}

    cmp        r2, #16
    blt        swap_end
swap_16:
    VLD4.8     {d0,d1,d2,d3}, [r0]!
    VSWP       d1, d3
    VST4.8     {d0,d1,d2,d3}, [r1]!
    sub        r2, r2, #16
    cmp        r2, #16
    bge        swap_16
    
swap_end:
    vpop       {q0,q1}
    ldmia      sp!, {pc}
    .end
