#include <linux/linkage.h>
#include <asm/memory.h>
#include <asm/glue.h>
#include <asm/vfpmacros.h>
#include <asm/thread_notify.h>
#include <asm/ptrace.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>

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

