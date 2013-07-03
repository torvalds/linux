#include <mach/platform.h>
#include "./pm_config.h"

/**-----------------------------stack point address in sram-----------------------------------------*/
#define SP_IN_SRAM	0xf000b3fc //32k
#define SP_IN_SRAM_PA   0x0000b3fc //32k
/*save_sp*/
/*save_sp_nommu*/
/*restore_sp*/
/*get_sp*/

    .text
    .globl save_sp
save_sp:
    mov r0, r13
    ldr  r13, =SP_IN_SRAM
    mov pc,lr

    .text
    .globl save_sp_nommu
save_sp_nommu:
    mov r0, r13
    ldr  r13, =SP_IN_SRAM_PA
    mov pc,lr
    
    .text
    .globl restore_sp
restore_sp:
    mov r13, r0
    mov pc,lr
    
    .text
    .globl get_sp
get_sp:
    mov r0, r13
    mov pc,lr
    
/*--------------------------------cache related api: ----------------------------------------------- */
/*invalidate_dcache*/
/*invalidate_icache*/
/*flush_dcache*/
/*flush_icache*/
/*disable_cache*/
/*disable_dcache*/
/*disable_icache*/
/*disable_l2cache*/
/*enable_cache*/
/*enable_icache*/

	.align	4  
	.text
	.globl invalidate_dcache	        /*can not use push and pop, because inval will discard the data in the stack*/
invalidate_dcache:
	/* Corrupted registers: r0-r7, r9-r11 (r6 only in Thumb mode) */
	dmb					/* ensure ordering with previous memory accesses */
	MRC     p15, 1, r0, c0, c0, 1       /*read clidr                              */
	ANDS    r3, r0, #0x7000000          /*extract loc from clidr                  */
	MOV     r3, r3, lsr #23             /*left align loc bit field                */
	BEQ     inv_finished                    /*if loc is 0, then no need to clean      */
	mov     r10, #0                     /*start clean at cache level 0            */
inv_loop1:                                   
	ADD     r2, r10, r10, lsr #1        /*work out 3x current cache level         */
	MOV     r1, r0, lsr r2              /*extract cache type bits from clidr      */
	AND     r1, r1, #7                  /*mask of the bits for current cache only */
	CMP     r1, #2                      /*see what cache we have at this level    */
	BLT     inv_skip                        /*skip if no cache, or just i-cache       */
	MCR     p15, 2, r10, c0, c0, 0      /*select current cache level in cssr      */
	ISB                                 /*isb to sych the new cssr&csidr          */
	MRC     p15, 1, r1, c0, c0, 0       /*read the new csidr                      */
	AND     r2, r1, #7                  /*extract the length of the cache lines   */
	ADD     r2, r2, #4                  /*add 4 (line length offset)              */
	LDR     r4, =0x3ff                  
	ANDS    r4, r4, r1, lsr #3          /*find maximum number on the way size     */
	CLZ     r5, r4                      /*find bit position of way size increment */
	LDR     r7, =0x7fff               
	ANDS    r7, r7, r1, lsr #13         /*extract max number of the index size    */
inv_loop2:                                  
	MOV     r9, r4                      /*create working copy of max way size     */
inv_loop3:                                  
	ORR     r11, r10, r9, lsl r5        /*factor way and cache number into r11    */
	ORR     r11, r11, r7, lsl r2        /*factor index number into r11            */
	MCR     p15, 0, r11, c7, c6, 2	      /*invalidate by set/way                  */
	SUBS    r9, r9, #1                  /*decrement the way                       */
	BGE     inv_loop3                       /*                                        */
	SUBS    r7, r7, #1                  /*decrement the index                     */
	BGE     inv_loop2                       /*                                        */
inv_skip:                                   /*                                        */
	ADD     r10, r10, #2                /*increment cache number                  */
	CMP     r3, r10                     /*                                        */
	BGT     inv_loop1                       /*                                        */
inv_finished:                                /*                                        */
	MOV     r10, #0                     /*swith back to cache level 0             */
	
	MCR     p15, 2, r10, c0, c0, 0      /*select current cache level in cssr      */
	dsb
	ISB                                 /*                                        */
	
	MOV     pc, lr                      /*                                        */
    
    	.text                                                                        
	.globl invalidate_icache                                                          
invalidate_icache:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c1, 0		@ invalidate I-cache inner shareable
	mcr	p15, 0, r0, c7, c5, 0		@ I+BTB cache invalidate
	dsb
	ISB  
	mov	pc, lr
	
    .text
    .globl flush_dcache
flush_dcache:
    push    {r0-r12} 
    dmb					/* ensure ordering with previous memory accesses */
    MRC     p15, 1, r0, c0, c0, 1       /*read clidr                              */
    ANDS    r3, r0, #0x7000000          /*extract loc from clidr                  */
    MOV     r3, r3, lsr #23             /*left align loc bit field                */
    BEQ     finished                    /*if loc is 0, then no need to clean      */
    mov     r10, #0                     /*start clean at cache level 0            */
loop1:                                   
    ADD     r2, r10, r10, lsr #1        /*work out 3x current cache level         */
    MOV     r1, r0, lsr r2              /*extract cache type bits from clidr      */
    AND     r1, r1, #7                  /*mask of the bits for current cache only */
    CMP     r1, #2                      /*see what cache we have at this level    */
    BLT     skip                        /*skip if no cache, or just i-cache       */
    MCR     p15, 2, r10, c0, c0, 0      /*select current cache level in cssr      */
    ISB                                 /*isb to sych the new cssr&csidr          */
    MRC     p15, 1, r1, c0, c0, 0       /*read the new csidr                      */
    AND     r2, r1, #7                  /*extract the length of the cache lines   */
    ADD     r2, r2, #4                  /*add 4 (line length offset)              */
    LDR     r4, =0x3ff                  
    ANDS    r4, r4, r1, lsr #3          /*find maximum number on the way size     */
    CLZ     r5, r4                      /*find bit position of way size increment */
    LDR     r7, =0x7fff               
    ANDS    r7, r7, r1, lsr #13         /*extract max number of the index size    */
loop2:                                  
    MOV     r9, r4                      /*create working copy of max way size     */
loop3:                                  
    ORR     r11, r10, r9, lsl r5        /*factor way and cache number into r11    */
    ORR     r11, r11, r7, lsl r2        /*factor index number into r11            */
    MCR     p15, 0, r11, c7, c14, 2	      /*clean & invalidate by set/way                  */
    SUBS    r9, r9, #1                  /*decrement the way                       */
    BGE     loop3                       /*                                        */
    SUBS    r7, r7, #1                  /*decrement the index                     */
    BGE     loop2                       /*                                        */
skip:                                   /*                                        */
    ADD     r10, r10, #2                /*increment cache number                  */
    CMP     r3, r10                     /*                                        */
    BGT     loop1                       /*                                        */
finished:                                /*                                        */
    MOV     r10, #0                     /*swith back to cache level 0             */

    MCR     p15, 2, r10, c0, c0, 0      /*select current cache level in cssr      */
    dsb
    ISB                                 /*                                        */
    pop    {r0-r12} 
    MOV     pc, lr                      /*                                        */
   
                                     
    .text                                                                        
    .globl flush_icache                                                          
flush_icache:
    push    {r0-r3}                                                                    
    MOV     r0, #0                                                               
    MCR     p15, 0, r0, c7, c5, 0       /*Instruction cache invalidate all to PoU    */
    MCR     p15, 0, r0, c7, c1, 0 	/*Instruction cache invalidate all to PoUa Inner Shareable*/
    MCR     p15, 0, r0, c7, c1, 6	/*Branch predictor invalidate all Inner Shareable*/
    ISB
    dsb
    pop     {r0-r3}
    MOV     pc, lr
    
    .text                                                                        
    .globl disable_cache                                                          
disable_cache:  
    push    {r0-r3}                                                                   
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    BIC	    r0, r0, #0x1 << 12
    BIC	    r0, r0, #0x1 << 2
    MCR     p15, 0, r0, c1, c0, 0       /*disable cache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

    .text                                                                        
    .globl disable_dcache                                                          
disable_dcache: 
    push    {r0-r3}                                                                    
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    BIC	    r0, r0, #(0x1 << 2)
    MCR     p15, 0, r0, c1, c0, 0       /*disable dcache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

    .text                                                                        
    .globl disable_icache                                                          
disable_icache: 
    push    {r0-r3}                                                                    
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    BIC	    r0, r0, #(0x1 << 12)
    MCR     p15, 0, r0, c1, c0, 0       /*disable icache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

  
    .text                                                                        
    .globl disable_l2cache                                                          
disable_l2cache:  
    push    {r0-r3}                                                                   
    MRC     p15, 0, r0, c1, c0, 1       /*read acr                  */
    BIC	    r0, r0, #0x1 << 1
    MCR     p15, 0, r0, c1, c0, 1       /*disable l2cache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

    .text                                                                        
    .globl enable_cache                                                          
enable_cache:           
    push    {r0-r3}                                                          
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    ORR	    r0, r0, #0x1 << 12
    ORR	    r0, r0, #0x1 << 2
    MCR     p15, 0, r0, c1, c0, 0       /*enable cache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr
        
    .text                                                                        
    .globl enable_icache                                                          
enable_icache: 
    push    {r0-r3}                                                                    
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    ORR	    r0, r0, #(0x1 << 12)
    MCR     p15, 0, r0, c1, c0, 0       /*disable dcache                  */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

/*--------------------------------------prediction----------------------------------------*/
/*invalidate_branch_predictor*/
/*disable_program_flow_prediction*/
/*enable_program_flow_prediction*/

    .text                                                                        
    .globl invalidate_branch_predictor                                                          
invalidate_branch_predictor:
    push    {r0-r3} 
    MOV	    r0, #0                                                                    
    MCR     p15, 0, r0, c7, c5, 6       /*(invalidate entire branch predictor array)*/
    ISB
    pop     {r0-r3}
    MOV     pc, lr
       
         
    .text                                                                        
    .globl disable_program_flow_prediction                                                          
disable_program_flow_prediction:    
    push    {r0-r3}                                                                 
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    BIC	    r0, r0, #0x800
    MCR     p15, 0, r0, c1, c0, 0       /*disable  program_flow_prediction                 */
    ISB
    pop     {r0-r3}
    MOV     pc, lr

    .text                                                                        
    .globl enable_program_flow_prediction                                                          
enable_program_flow_prediction:  
    push    {r0-r3}                                                                   
    MRC     p15, 0, r0, c1, c0, 0       /*read cr                  */
    ORR	    r0, r0, #0x800
    MCR     p15, 0, r0, c1, c0, 0       /*disable  program_flow_prediction                 */
    ISB
    pop     {r0-r3}
    MOV     pc, lr
        
/*-------------------------------------tlb related api:----------------------------------------*/
/*mem_flush_tlb*/
/*mem_preload_tlb*/

	.text
	.globl mem_flush_tlb
mem_flush_tlb:
	push    {r0-r3}
	MOV     r0, #0 
	/*instruction entire instruction tlb*/
	mcr p15, 0, r0, c8, c5, 0
	/* invalid entire data tlb */
	mcr p15, 0, r0, c8, c6, 0
	/*invalidate entire unified TLB inner shareable*/
	mcr p15, 0, r0, c8, c7, 0
	dsb
    ISB      
	pop     {r0-r3}
	mov pc,lr
	
    .text
    .globl mem_preload_tlb
mem_preload_tlb:
	push    {r0-r3}
	/*16k*/
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A1_BASE)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A1_BASE + 0x1000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A1_BASE + 0x2000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A1_BASE + 0x3000)
	ldr r1, [r0]
	
	/*16k*/
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A2_BASE)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A2_BASE + 0x1000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A2_BASE + 0x2000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A2_BASE + 0x3000)
	ldr r1, [r0]
	
	/*16k*/
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A3_BASE)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A3_BASE + 0x1000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A3_BASE + 0x2000)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_SRAM_A3_BASE + 0x3000)
	ldr r1, [r0]

	ldr r0, =IO_ADDRESS(SW_PA_PORTC_IO_BASE)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_UART0_IO_BASE)
	ldr r1, [r0]
	ldr r0, =IO_ADDRESS(SW_PA_TIMERC_IO_BASE)
	ldr r1, [r0]
	
	dsb
	isb
	
	pop     {r0-r3}
	mov pc,lr
	           
/*--------------------------------------mmu----------------------------------------*/
/*disable_mmu*/
/*enable_mmu*/

    .text
    .globl disable_mmu
disable_mmu:
		/*read cr*/
                MRC p15,0,r1,c1,c0,0
                BIC r1, #0x1000
                BIC r1, #0x0005
                b __turn_mmu_off
                .align 5
                
                .type __turn_mmu_off, %function
__turn_mmu_off:
                /*write cr: disable cache and mmu*/
                MCR p15,0,r1,c1,c0,0
		/*read id reg*/
                mrc p15, 0, r3, c0, c0, 0 
                mov r3, r3
                mov r3, r3 
                /*return*/
                mov pc, lr

    .text
    .globl enable_mmu
enable_mmu:
		/*read cr*/
                MRC p15,0,r1,c1,c0,0
                ORR r1, #0x1000
                ORR r1, #0x0005
                b __turn_mmu_on
                .align 5
                
                .type __turn_mmu_on, %function
__turn_mmu_on:
                /*write cr: enable cache and mmu*/
                MCR p15,0,r1,c1,c0,0
		/*read id reg*/
                mrc p15, 0, r3, c0, c0, 0 
                mov r3, r3
                mov r3, r3 
                /*return*/
                mov pc, lr
                
/*----------------------------------------pc related api:---------------------------------------*/
/*jump_to_suspend*/
/*jump_to_resume*/
/*jump_to_resume0*/
/*jump_to_resume0_nommu*/

	.text 
	.globl jump_to_suspend  
jump_to_suspend:
	/*enable 0x0000 <-->  0x0000 mapping */
	/*write ttbr0*/  
	mcr p15, 0, r0, c2, c0, 0
        bl mem_flush_tlb
	dsb
	isb
	
	mov pc, r1
	
	.align	4  
	.text
	.globl jump_to_resume
jump_to_resume:
	/*before jump to resume:
	 * 1st: invalidate the data
	 * 2nd: restore r0-r13.
	 * 3rd: jump (para 1).
	 */                                                                          
	/* Set the return pointer */                                                 
	mov     r12, r0
	mov 	r8, r1
	bl	invalidate_dcache
	mov	r1, r8
	mov	lr, r12
	ldmia   r1, {r0 - r13}                                                   
        mov     pc, lr

	.align	4  
	.globl jump_to_resume0
jump_to_resume0: 
        /* Set the return pointer */                                                        
	mov     lr, r0
	mov     pc, lr

	.align	4  

    .text
    .globl mem_preload_tlb_nommu
mem_preload_tlb_nommu:
    push    {r0-r3}
 
    pop     {r0-r3}
    mov pc,lr
