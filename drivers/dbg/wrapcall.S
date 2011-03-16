#include <linux/linkage.h>
#include <asm/memory.h>
#include <asm/glue.h>
#include <asm/vfpmacros.h>
#include <asm/thread_notify.h>
#include <asm/ptrace.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <mach/rk29_iomap.h>

#__scu_call_wrap:
#
# call to the defined funcion.
# r0: func argument array , max = 6
# r1: argument num.
# r2: the function number.

#;	EXPORT __scu_call_wrap
#;	
#;	CODE32
#;	AREA   ||funwrap||, CODE, READONLY	
ENTRY(__scu_call_wrap)
	STMFD    r13!,{r4,r5,r6,lr}
	mov		r6,r2
	ldmia	r0, {r0-r5}
	STMFD    r13!,{r4,r5}
	mov	lr , pc
	mov pc , r6
	add     r13 , r13 ,#8
	LDMFD    r13!,{r4,r5,r6,pc}

/**
 * 20091126,HSL@RK,change to get syscall struct pt_regs *.
 * way: search stack frame for lr = ret_fast_syscall.
 * code from bachtrace.S -- __backtrace.
 * 20110126,not support at 2.6.32(no fp)
 * 20110127,support at debug.c,use c funtion.
 */
#define frame	r4
#define sv_fp	r5
#define sv_pc	r6
#define mask	r7
#define offset	r8
/*      
ENTRY(__scu_get_usr_regs)
    		mov r0, #0
		mov	pc, lr
*/

/*
 * 20091215,continue running from break point.
 * r0 is the struct pt_regs *.
 */
ENTRY(__scu_bk_continue)
    	mov r12, r0	 
    	ldr	r0, [r12, #S_PSR]		
	msr	spsr_cxsf, r0
	ldmia	r12, {r0 - pc}^			@ load r0 - pc, cpsr

ENTRY(__run)
    		mov r0, r0
		mov	pc, lr


/* 20110212,HSL@RK, USE parm0 for debug.
  *
  */
ENTRY(__rb)
		mov	r7,r0
/*		adr   r0 , __rb_info
		adr	r8, __prk
		ldr	r8,[r8,#0]
		adr	r1 , __rb
		mov	lr , pc
		bx	r8
		mov	r0,#0x10000
		bl __rb_delay
*/		
		mov	r8,r7
/*		adr   r0 , __prk_info
1:		
		ldrb	r1,[r0],#1
		cmp r1,#0
		strne r1,[r8,#0]
		bne	1b
		mov	r0,#0x20000
		bl __rb_delay*/
		
		MRC p15,0,r0,c1,c0,0
		BIC r0,r0,#(1<<0)	   @disable mmu
		BIC r0,r0,#(1<<13)    @set vector to 0x00000000
		BIC r0,r0,#(1<<12)	   @disable I CACHE
		BIC r0,r0,#(1<<2)	   @disable D DACHE
        BIC r0,r0,#(1<<11)	   @disable Z
        BIC r0,r0,#(1<<28)	   @disable TRE
		MCR p15,0,r0,c1,c0,0
		isb
		dsb
		nop	
		nop
		nop
		
		adr	r7,__regs

		ldr	r8,[r7,#0x10]
/*		adr   r3 , __dbg_info
2:		
		ldrb	r1,[r3],#1
		cmp r1,#0
		strne r1,[r8,#0]
		bne	2b

		mov	r0,#0x20000
		bl __rb_delay*/

wait:	
		@b wait
		@arm slow mod.
		ldr	r8,[r7,#0xc]
		ldr	r9,[r8,#0]
		bic	r9,r9,#3
		str	r9,[r8,#0]
		dsb
		mov	r0,#0x10000
		bl __rb_delay
		
		@ unremap, and axi.
		ldr	r8,[r7,#0]
		ldr	r9,[r8,#0]
		bic	r9,r9,#(1<<21)
		str	r9,[r8,#0]

		mov	r9,#0
		ldr	r8,[r7,#4]
		str	r9,[r8,#0]
		
		ldr	r8,[r7,#8]
		str	r9,[r8,#0]
		dsb
		

		ldr	r8,[r7,#0x10]
/*		adr   r0 , __dbg_info1
3:		
		ldrb	r1,[r0],#1
		cmp r1,#0
		strne r1,[r8,#0]
		bne	3b

		mov	r0,#0x10000
		bl __rb_delay
*/
        mov r4, #0
		mov	pc, r4
		
__prk:
	.long		printk
__rb_info:
	.asciz	 "at reboot function,pc=0x%x\n"
__prk_info:	
	.asciz	"after printk!\n\r"	
__dbg_info:	
	.asciz	"AFTER DIS MMU\n\r"
__dbg_info1:	
	.asciz	"LAST JUMP TO 0\n\r"	
	.align
__regs:
	.long		RK29_GRF_PHYS+0xc0	@ 0x20008000 , unremap 
	.long		RK29_CPU_AXI_BUS0_PHYS 	@ 0x15000000
	.long		RK29_AXI1_PHYS		@ 0x10000000
	.long		RK29_CRU_PHYS		@ 0x20000000
	.long		RK29_UART1_PHYS		@ 0x20060000 , printk for debug.
	.align

__rb_delay:
	subs	r0, r0, #1
	bhi	__rb_delay
	mov	pc, lr
	
#if FIQ_ENABLE
        .align
ENTRY(rk28_fiq_handle)
        mrs r8, spsr
        adr r9,__fiq_save
        stmia r9 , {r0-r8,lr}
        mov   r0,r9     @save addr.
        mov r5,#(SVC_MODE|PSR_I_BIT|PSR_F_BIT)
        msr  cpsr_cxsf,r5       @ to svc mod.disable irq,fiq.
        sub r1, sp, #(S_FRAME_SIZE)

        add     r2 , r1 , #32   @ r0--r7.
        stmia  r2,{r8-lr}       @ the svc sp not change here.
        ldmia  r0,{r3-r10}
        stmia  r1,{r3-r10}
        ldr      r9,[r0,#36]     @get fiq lr.
        sub    r9,r9,#4
        str      r9 , [r1,#S_PC]
        ldr      r10,[r0,#32]     @get fiq spsr.
        str      r10, [r1,#S_PSR]
        mov    r5,r1
        mov    sp,r5    @stack frame 
        mov    r0,r5
        ldr      r11,1f
        blx      r11
        b        3f
1:
                .long           rk28_debug_fiq
3:                
        msr  spsr_cxsf,r10
        ldmia r5, {r0 - pc}^
__fiq_save:                @for save fiq spsr r0-r7,spsr,lr.
                .long     0                
                .previous
#endif

