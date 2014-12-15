#include <linux/linkage.h>
/*
void swap_vdin_y(unsigned char* dst, unsigned char* src, unsigned int size)
*/
    .section .text
    ENTRY(swap_vdin_y)
    stmdb sp!, {lr}
    vpush      {q0,q1,q2}

    cmp        r2, #32
    blt        swap_end
swap_32:
    VLD4.8     {d0, d1, d2, d3}, [r1]!
    VREV16.8   d5, d0   
    VREV16.8   d4, d1   
    VSWP       d2, d3 
    VREV16.8   d3, d3 
    VREV16.8   d2, d2   
    VST4.8     {d2, d3, d4, d5}, [r0]!
    sub        r2, r2, #32
    cmp        r2, #32
    bge        swap_32
    
swap_end:
    vpop       {q0,q1,q2}
    ldmia      sp!, {pc}
    .end
