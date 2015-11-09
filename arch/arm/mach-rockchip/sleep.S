
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>
//#include <asm/suspend.h>

#define _RKPM_SEELP_S_INCLUDE_
#include "pm.h"

.text
ENTRY(rkpm_slp_cpu_while_tst)
stmfd	sp!, { r3 - r12, lr }

1: mov r3,r3
   b 1b

ldmfd	sp!, { r3 - r12, pc }

ENDPROC(rkpm_slp_cpu_while_tst)

.data
.align
//65 A   48 0  97 a
ENTRY(rkpm_slp_cpu_resume)
9:	mov r1,r1 
       // b 9b
        mov r1,r1 
    #if 0    
	ldr r4, = 0xFF690000
	mov r5, #65
	str r5,[r4]
    #endif
       	setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r1  @ set SVC, irqs off

	MRC p15,0,R1,c0,c0,5
	AND     R1,R1,#0xf
	CMP     R1,#0
	BEQ     cpu0Run

//cpu 1 stop here
cpu1loop:
	mov r3, #50
	//str r3,[r4]
	WFENE                   // ; wait if it.s locked
	B     cpu1loop           //  ; if any failure, loop
	
cpu0Run:
	//mov r3, #48
	//str r3,[r0]
#if 0	
//v7_invalidate_l1
v7_invalidate_l1:
        mov     r0, #0
        mcr     p15, 0, r0, c7, c5, 0   @ invalidate I cache
        mcr     p15, 2, r0, c0, c0, 0
        mrc     p15, 1, r0, c0, c0, 0

        ldr     r1, =0x7fff
        and     r2, r1, r0, lsr #13

        ldr     r1, =0x3ff

        and     r3, r1, r0, lsr #3  @ NumWays - 1
        add     r2, r2, #1          @ NumSets

        and     r0, r0, #0x7
        add     r0, r0, #4          @ SetShift

        clz     r1, r3              @ WayShift
        add     r4, r3, #1          @ NumWays
1:      sub     r2, r2, #1          @ NumSets--
        mov     r3, r4              @ Temp = NumWays
2:      subs    r3, r3, #1          @ Temp--
        mov     r5, r3, lsl r1
        mov     r6, r2, lsl r0
        orr     r5, r5, r6          @ Reg = (Temp<<WayShift)|(NumSets<<SetShift)
        mcr     p15, 0, r5, c7, c6, 2
        bgt     2b
        cmp     r2, #0
        bgt     1b
        dsb
        isb
#endif
      #if 0 
	ldr r1,=PM_BOOT_CODE_SP
	mov sp, r1	//setting sp
      #endif
      1:  mov r1,r1 
         //b 1b

      
	adr r1,9b     // boot ram base
        ldr r5,8f     // resume data  offset ,from ram base
        add r5,r5,r1 // resume data addr
        
        //l2 resume 
        ldr r3 ,[r5,#(RKPM_BOOTDATA_L2LTY_F*4)] // l2 resume flags
        cmp r3,#1
        bne arm_errata__818325
        ldr r3 ,[r5,#(RKPM_BOOTDATA_L2LTY*4)]
        mcr p15, 1, r3, c9, c0, 2   
        
arm_errata__818325:
        ldr r3 ,[r5,#(RKPM_BOOTDATA_ARM_ERRATA_818325_F*4)]
        cmp r3,#1
        bne sp_set
        ldr r3 ,[r5,#(RKPM_BOOTDATA_ARM_ERRATA_818325*4)]
        mcreq   p15, 0, r3, c15, c0, 1

sp_set:        //sp
        ldr sp,[r5,#(RKPM_BOOTDATA_CPUSP*4)]   //sp      
        
	ldr r3,[r5,#(RKPM_BOOTDATA_DDR_F*4)]   //get SLP_DDR_NEED_RES ,if it is 1 ,ddr need to reusme	
	cmp r3,#1
	bne res
	ldr r1,[r5,#(RKPM_BOOTDATA_DDRCODE*4)]  // ddr resume code
	ldr r0,[r5,#(RKPM_BOOTDATA_DDRDATA*4)]  //ddr resume data
	blx r1
res:	
        1:  mov r1,r1
        //  b 1b
       #if 0    
        ldr r4, = (0xFF730000+0x18)
        ldr r7,[r4]
        ldr r6,=(0x3<<21)
        orr r7, r7,r6
        str r7,[r4]
       #endif
       
	ldr     pc, [r5,#(RKPM_BOOTDATA_CPUCODE*4)]
8:      .long (RKPM_BOOT_CODE_OFFSET+RKPM_BOOT_CODE_SIZE)
ENDPROC(rkpm_slp_cpu_resume)
