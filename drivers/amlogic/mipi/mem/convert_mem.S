#include <linux/linkage.h>
/*
void convert422_to_nv21_mem(unsigned char* src, unsigned char* dst_y, unsigned char *dst_uv, unsigned int size)
*/
    .section .text
    ENTRY(convert422_to_nv21_mem)
    stmdb sp!, {r4-r7, lr}
    vpush      {q0,q1,q2,q3}

    mov        r7, r3
    add        r4, r7, r7
    cmp        r3, #16
    blt        end
loop_16:
    add        r5, r0, r4
    add        r6, r1, r7
    VLD4.8     {d0, d1, d2, d3}, [r0]!
    VLD4.8     {d4, d5, d6, d7}, [r5]!
    VSWP       d1, d3
    VSWP       d5, d7
    VST2.8     {d1, d3}, [r1]!
    VST2.8     {d5, d7}, [r6]!
    VST2.8     {d0, d2}, [r2]!
    sub        r3, r3, #16
    cmp        r3, #16
    bge        loop_16
    
end:
    vpop       {q0,q1,q2,q3}
    ldmia      sp!, {r4-r7, pc}
    .end
