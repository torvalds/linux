#include <linux/linkage.h>
/*
void interleave_uv(unsigned char* pU, unsigned char* pV, unsigned char *pUV, unsigned int size_u_or_v)
*/
    .section .text
    ENTRY(interleave_uv)
    stmdb      sp!, {r4, lr}
    
    mov        r4, #0                                        
loop:
    VLD1.8     {d0, d1}, [r0]                            
    VLD1.8     {d2, d3}, [r1]                            
    VZIP.8    Q0, Q1                                       
    VST1.8     {d0, d1, d2, d3}, [r2]
    add        r0, r0, #16
    add        r1, r1, #16  
    add        r2, r2, #32    
    add        r4, r4, #16 
    cmp        r4, r3
    blt        loop
    
    ldmia      sp!, {r4, pc}
    .end

