#include <linux/linkage.h>
/*
void convert422_to_nv21_vdin(unsigned char* src, unsigned char* dst_y, unsigned char *dst_uv, unsigned int size)
*/
    .section .text
    ENTRY(convert422_to_nv21_vdin)
    stmdb sp!, {r4-r7, lr}
    vpush      {q0,q1,q2,q3,q4}

    mov        r7, r3
    add        r4, r7, r7
    cmp        r3, #16
    blt        end
loop_16:
    add        r5, r0, r4
    add        r6, r1, r7
    VLD4.8     {d0, d1, d2, d3}, [r0]!
    VLD4.8     {d4, d5, d6, d7}, [r5]!
    VREV16.8   d9, d1    
    VREV16.8   d8, d3  
    VST2.8     {d8, d9}, [r1]!
    VREV16.8   d9, d5    
    VREV16.8   d8, d7  
    VST2.8     {d8, d9}, [r6]!
    VREV16.8   d9, d0    
    VREV16.8   d8, d2  
    VST2.8     {d8, d9}, [r2]!
    sub        r3, r3, #16
    cmp        r3, #16
    bge        loop_16
    
end:
    vpop       {q0,q1,q2,q3,q4}
    ldmia      sp!, {r4-r7, pc}
    .end
