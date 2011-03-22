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
		MOV r0,#0
		mcr p15, 0, r0, c7, c10, 5
                mcr p15, 0, r0, c7, c10, 4
		isb
		dsb
		nop	
		nop
		nop
		
		adr	r7,__regs

		ldr	r8,[r7,#0x10]  @uart1 reg
		adr   r3 , __dbg_info
2:		
		ldrb	r1,[r3],#1
		cmp r1,#0
		strne r1,[r8,#0]
		bne	2b

		mov	r0,#0x20000
		bl __rb_delay

wait:	
		@b wait
		@arm slow mod.
		ldr	r8,[r7,#0xc]  @CRU reg
		ldr	r9,[r8,#0x10]
		bic	r9,r9,#0x3      @CPU slow mode
		bic r9,r9,#(0x3<<2) @Peri slow mode
		str	r9,[r8,#0]
		dsb
		mov	r0,#0x10000
		bl __rb_delay
		
		@recover SPI clk
		ldr	r9,[r8,#0x0c] @CRU_PPLL_CON bit 15=1, Peri PLL power down
		bic r9,r9,#(0x1<<15)
		str r9,[r8,#0x0c]
		dsb
		mov	r0,#0x10000
		bl __rb_delay
		
		ldr r9,[r8,#0x2c] @CRU_CLKSEL6_CON bit [8:2] = 7, bit [1:0] =0
		bic r9,r9,#(0x7F<<2)
		bic r9,r9,#(0x3)
		orr r9,r9,#(0x7<<2)
		str r9,[r8,#0x2c]
		
		@recover eMMC clk
		ldr r9,[r8,#0x30] @CRU_CLKSEL7_CON bit [23:18] = 0x17, bit [1:0] = 0
		bic r9,r9,#(0x3F<<18)
		bic r9,r9,#0x3
		orr r9,r9,#(0x17<<18)
		str r9,[r8,#0x30]
		
		@recover UART1 clk
		ldr r9,[r8,#0x34] @CRU_CLKSEL8_CON bit [21:20] = 0x2, bit [19:14] = 0, bit [2:0] = 0
		bic r9,r9,#(0x3<<20)
		bic r9,r9,#(0x3F<<14)
		bic r9,r9,#0x7
		orr r9,r9,#(0x2<<20)
		str r9,[r8,#0x34]
		
		@clk enable
		ldr r9,[r8,#0x5c]    @CRU_CLKGATE0_CON
		bic r9,r9,#(0x1<<31) @GRF clk enable
		bic r9,r9,#(0x1<<30) @PMU clk enable
		bic r9,r9,#(0x1<<27) @DEBUG clk enable
		bic r9,r9,#(0x7<<18) @DDR clk enable
		bic r9,r9,#(0x1<<14) @mask rom clk enable
		bic r9,r9,#(0x1<<12) @Int Mem clk enable
		bic r9,r9,#(0x1<<11) @GIC clk enable
		bic r9,r9,#(0xFF)    @CPU clk enable
		bic r9,r9,#(0x1<<8)
		str r9,[r8,#0x5c]
		
		ldr r9,[r8,#0x60]    @CRU_CLKGATE1_CON
		bic r9,r9,#(0x3<<25) @USB clk enable
		bic r9,r9,#(0x3<<23) @eMMC clk enable
		bic r9,r9,#(0x1<<16) @NANC clk enable
		bic r9,r9,#(0x1<<6)  @DDR PERIPH AXI clk enable
		bic r9,r9,#0x17      @PERIPH clk enable
		str r9,[r8,#0x60]
		
		ldr r9,[r8,#0x64]    @CRU_CLKGATE2_CON
		bic r9,r9,#(0x1<<26) @JTAG clk enable
		bic r9,r9,#(0x1<<15) @SPI0 clk enable
		bic r9,r9,#0x1       @UART1 clk enable
		str r9,[r8,#0x64]
		
		@soft de-reset
		ldr r9,[r8,#0x6c]    @CRU_SOFTRST0_CON
		bic r9,r9,#(0x1<<25) @ARM core DEBUG soft de-reset
		bic r9,r9,#(0x1<<12) @mask rom soft de-reset
		bic r9,r9,#(0x1<<9)  @Int Mem soft de-reset
		bic r9,r9,#(0x1<<8)  @GIC soft de-reset
		bic r9,r9,#0x3F      @CPU soft de-reset
		str r9,[r8,#0x6c]
		
		ldr r9,[r8,#0x70]    @CRU_SOFTRST1_CON
		bic r9,r9,#(0x1<<28) @UART1 soft de-reset
		bic r9,r9,#(0x1<<25) @SPI0 soft de-reset
		bic r9,r9,#(0x7<<16) @USB0 soft de-reset
		bic r9,r9,#(0x1<<15) @EMMC soft de-reset
		bic r9,r9,#(0x1<<9)  @NANC soft de-reset
		bic r9,r9,#0x3F      @PERIPH soft de-reset
		str r9,[r8,#0x70]
		
		ldr r9,[r8,#0x74]    @CRU_SOFTRST2_CON
		bic r9,r9,#(0x1F<<8) @DDR soft de-reset
		str r9,[r8,#0x74]
		
		@ unremap, and axi.
		ldr	r8,[r7,#0]  @GRF reg
		ldr	r9,[r8,#0xc0]
		bic	r9,r9,#(1<<21)
		str	r9,[r8,#0xc0]

		mov	r9,#0
		ldr	r8,[r7,#4] @CPU_AXI_BUS0
		str	r9,[r8,#0]
		
		ldr	r8,[r7,#8] @AXI1
		str	r9,[r8,#0]
		dsb
		
		@eMMC register recover
		ldr	r8,[r7,#0]      @GRF reg
		ldr r9,[r8,#0xbc]   @GRF_SOC_CON0
		bic r9,r9,#(0x1<<9) @emmc_and_boot_en control=0
		str r9,[r8,#0xbc]		
		
		ldr	r8,[r7,#14]     @eMMC reg
		mov r9,#0
		str r9,[r8,#0xc]    @SDMMC_CLKSRC=0, clk_source=clock divider 0
		str r9,[r8,#0x18]   @SDMMC_CTYPE=0, card_width=1 bit mode
		mov r9,#0x200
		str r9,[r8,#0x1c]   @SDMMC_BLKSIZ=0x200, Block size=512

		ldr	r8,[r7,#0x10] @uart1_reg
		adr   r0 , __dbg_info1
3:		
		ldrb	r1,[r0],#1
		cmp r1,#0
		strne r1,[r8,#0]
		bne	3b

		mov	r0,#0x10000
		bl __rb_delay

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
	.long		RK29_GRF_PHYS   	@ 0x20008000 , unremap 
	.long		RK29_CPU_AXI_BUS0_PHYS 	@ 0x15000000
	.long		RK29_AXI1_PHYS		@ 0x10000000
	.long		RK29_CRU_PHYS		@ 0x20000000
	.long		RK29_UART1_PHYS		@ 0x20060000 , printk for debug.
	.long   RK29_EMMC_PHYS    @ 0x1021C000
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

