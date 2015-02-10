#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>

#define _RKPM_SEELP_S_INCLUDE_
#include "pm.h"

.text
ENTRY(rk312x_pm_slp_cpu_while_tst)
stmfd  sp!, { r3 - r12, lr }

1: mov r3,r3
	b 1b

ldmfd  sp!, { r3 - r12, pc }

.data
.align
ENTRY(rk312x_pm_slp_cpu_resume)
9:	mov r1,r1

#if 0
	ldr r4, = 0x20068000
	mov r5, #67
	str r5,[r4]
#endif
	setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r1  @ set SVC, irqs off

	MRC p15,0,R1,c0,c0,5
	AND     R1,R1,#0xf
	CMP     R1,#0
	BEQ     cpu0Run

cpu1loop:
	mov r3, #50
	//str r3,[r4]
	WFENE                   // ; wait if it.s locked
	B     cpu1loop           //  ; if any failure, loop

cpu0Run:
	1:  mov r1,r1

	adr r1,9b     // boot ram base
	ldr r5,8f     // resume data  offset ,from ram base
	add r5,r5,r1 // resume data addr

	ldr r3 ,[r5,#(RKPM_BOOTDATA_ARM_ERRATA_818325_F*4)]
	ldr r4, = 0x200080b4 // armvoltage pwm resume
	and r2, r3, #1
	cmp r2, #0
	beq pwm1
	ldr r2, = 0x00100010  //pwm0
	str r2, [r4]
pwm1:
	and r2, r3, #2
	cmp r2, #0
	beq pwm2
	ldr r2, = 0x00400040 //pwm1
	str r2, [r4]
pwm2:
	and r2, r3, #4
	cmp r2, #0
	beq sp_set
	ldr r2, = 0x01000100//pwm2
	str r2, [r4]

sp_set:        //sp
	ldr sp,[r5,#(RKPM_BOOTDATA_CPUSP*4)]   //sp

	ldr r3,[r5,#(RKPM_BOOTDATA_DDR_F*4)]
	//get SLP_DDR_NEED_RES ,if it is 1 ,ddr need to reusme
	cmp r3,#1
	bne res
	ldr r1,[r5,#(RKPM_BOOTDATA_DDRCODE*4)]  // ddr resume code
	ldr r0,[r5,#(RKPM_BOOTDATA_DDRDATA*4)]  //ddr resume data
	blx r1
res:
	1:  mov r1,r1
	//  b 1b
/*****************************************************************************/
dram_resume:
	;//push {lr}
	mov r2,#0x20000000 ;/*cru PA*/
	mov r3,#0x20000000

	str r3,[r2,#0x14];/*PLL no power-down*/

	dsb sy
	mov r2,r2
	mov r2,r2

dpll_lock:
	ldr r3,[r2,#0x14]
	tst r3,#400;/*DPLL lock*/

	bne dpll_lock

	ldr r3,=0x00100010;/*DPLL normal mode*/
	str r3,[r2,#0x40]
	dsb sy

	mov r3,#0x40000
	str r3,[r2,#0xd0];/*enable DDR PHY clock*/
	mov r0,#1

dealyus_uncache:
	mov r1,#5
	mul r0, r0, r1
delay_loop:
	subs r0, r0, #1
	bne delay_loop

	ldr r2,=0x2000a000
	ldr r3,[r2,#0]
	orr r3, r3, #0x4;/*phy soft de-reset dll*/
	str r3,[r2,#0]
	mov r0,#5
	mov r1,#5
	mul r0, r0, r1
delay5us_loop:
	subs r0, r0, #1
	bne delay5us_loop
	orr r3, r3, #0x8;/*phy soft de-reset*/
	str r3,[r2,#0]

	sub r2,r2,#0x2000;/*0x20008000*/
	ldr r3,[r2,#0x300];/*get chip id*/
	sub r3,r3,#0x3100
	subs r3,r3,#0x12
	bne rk3126b_buffer_en;/*RK3126B*/
	ldr r3,=0x40004
 	str r3,[r2,#0x148];/*buffer en*/
	b move_access
rk3126b_buffer_en:
 	ldr r2,=0x2000a000
	mov r3,#0x2
	str r3,[r2,#0x264]

move_access:
	/*move to access status*/
    	ldr r2,=0x20004000
	mov r3, #4
	str r3,[r2,#0x4];/*wake up */
	dsb sy

wait_access:
	ldr r3,[r2,#0x8]
	and r3, r3, #0x7
	cmp r3, #3
	bne wait_access


	ldr r4, = 0x100a000c //printk
	mov r1, #0x0e //msch ce xiao
	str r1,[r4]

	ldr r4, = 0x100a0010 //printk
	mov r1, #0x0e //msch ce xiao

	ldr     pc, [r5,#(RKPM_BOOTDATA_CPUCODE*4)]
8:      .long (0x00+0x700)//RKPM_BOOT_CODE_OFFSET+RKPM_BOOT_CODE_SIZE
ENDPROC(rk312x_pm_slp_cpu_resume)

