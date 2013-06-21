/*                                                                                   
 * mem support specific for ARM                                              
 * based on hibernate support specific for ARM                                                                                  
 * Copyright (C) 2010 Nokia Corporation                                              
 * Copyright (C) 2010 Texas Instruments, Inc.                                        
 * Copyright (C) 2006 Rafael J. Wysocki <rjw <at> sisk.pl>                           
 *                                                                                   
 * Contact: Hiroshi DOYU <Hiroshi.DOYU <at> nokia.com>                               
 *                                                                                   
 * License terms: GNU General Public License (GPL) version 2                         
 */                                                                                  
                                                                                     
#include <linux/linkage.h>    
/*
 * function: mem_arch_suspend
 * discription: save main part of general purpose register.
 */                                                       
	.text 
	.globl mem_arch_suspend  
mem_arch_suspend:  
	push    {r0-r3}                                                                                                                             
	/*                                                                                
	 * Save current program status register                                           
	 */                                                                               
	ldr     r3, .Lsaved_cpsr_svc                                                          
	mrs     r0, cpsr                                                                  
	str     r0, [r3]                                                                  
                                                                                     
	/*                                                                                
	 * Change to system(user) mode                                                    
	 */                                                                               
	mov     r1, r0                                                                    
	orr     r1, r1, #0x1f                                                             
	msr     cpsr_c, r1                                                                
                                                                            
	/*                                                                                
	 * Save User context                                                              
	 */                                                                               
	ldr     r3, .Lsaved_context_r13_sys                                                    
	stmia   r3, {r13-r14}                                                              

	/*                                                                                
	 * Change to fiq mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x11                                                             
	msr     cpsr_c, r1
		 
	 /*                                                                                
	 * Save fiq context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_fiq                                                    
	stmia   r3, {r8-r14}
	ldr     r3, .Lsaved_spsr_fiq                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3] 
	
	 /*                                                                                
	 * Change to abt mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x17                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Save abt context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_abt                                                    
	stmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_abt                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3]
	
	 /*                                                                                
	 * Change to irq mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x12                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Save irq context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_irq                                                    
	stmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_irq                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3]
	
	 /*                                                                               
	 * Change to und mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x1b                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Save und context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_und                                                    
	stmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_und                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3]
	
	 /*                                                                                
	 * Change to mon mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x16                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Save mon context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_mon                                                    
	stmia   r3, {r13-r14}
	ldr     r3, .Lsaved_spsr_mon                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3]
		                                                                             
	/*                                                                                
	 * Go back to original SVC mode                                                   
	 */                                                                               
	msr     cpsr_c, r0                                                                
                                                                                     
	/*                                                                                
	 * Save SVC context                                                               
	 */                                                                               
	ldr     r3, .Lsaved_context_r12_svc                                               
	stmia   r3, {r12-r13}                                                             
	ldr     r3, .Lsaved_spsr_svc                                                      
	mrs     r1, spsr                                                                  
	str     r1, [r3] 
	                                                                 
        pop     {r0-r3}                                                                                                                                                                                                                 
	/*                                                                                
	 *  return                                                   
	 */                                                                                
	mov     pc, lr                                                                 

/*
 * function: mem_arch_resume
 * discription: retore main part of general purpose register.
 */ 	
	.align	4                                                                 
        .globl  mem_arch_resume                                                                           
mem_arch_resume:
	push    {r0-r3}
	
	mrs     r0, cpsr                                                            
 	/*                                                                                
	 * Restore SVC context                                                            
	 */                                                                               
	ldr     r3, .Lsaved_context_r12_svc                                               
	ldmia   r3, {r12-r13}                                                             
	ldr     r3, .Lsaved_spsr_svc                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1                                                             
                                                                                     
        /*                                                                                
	 * Change to system(user) mode                                                    
	 */                                                                               
	mov     r1, r0                                                                    
	orr     r1, r1, #0x1f                                                             
	msr     cpsr_c, r1                                                                
                                                                                     
	/*                                                                                
	 * Restore User context                                                           
	 */                                                                               
	ldr     r3, .Lsaved_context_r13_sys                                                    
	ldmia   r3, {r13-r14}
	
	/*                                                                                
	 * Change to fiq mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x11                                                             
	msr     cpsr_c, r1
		  
	 /*                                                                                
	 * Restore fiq context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_fiq                                               
	ldmia   r3, {r8-r14}                                                             
	ldr     r3, .Lsaved_spsr_fiq                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1 
	
	 /*                                                                                
	 * Change to abt mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x17                                                             
	msr     cpsr_c, r1
	
	  /*                                                                                
	 * Restore abt context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_abt                                               
	ldmia   r3, {r13-r14}                                                             
	ldr     r3, .Lsaved_spsr_abt                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1
	 /*                                                                                
	 * Change to irq mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x12                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Restore irq context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_irq                                               
	ldmia   r3, {r13-r14}                                                             
	ldr     r3, .Lsaved_spsr_irq                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1 
	
	 /*                                                                                
	 * Change to und mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x1b                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Restore und context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_und                                               
	ldmia   r3, {r13-r14}                                                             
	ldr     r3, .Lsaved_spsr_und                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1 
	
	 /*                                                                                
	 * Change to mon mode                                                    
	 */ 
	mov     r1, r0
	bic	r1, r1, #0x1f                                                                    
	orr     r1, r1, #0x16                                                             
	msr     cpsr_c, r1
	
	 /*                                                                                
	 * Restore mon context                                                              
	 */ 
	ldr     r3, .Lsaved_context_r13_mon                                               
	ldmia   r3, {r13-r14}                                                             
	ldr     r3, .Lsaved_spsr_mon                                                      
	ldr     r1, [r3]                                                                  
	msr     spsr_cxsf, r1 
	
	/*                                                                                
	 * Restore CPSR, and Change to svc mode                                                    
	 */ 	                                                              
	ldr     r3, .Lsaved_cpsr_svc                                                          
	ldr     r1, [r3]                                                                  
	msr     cpsr_cxsf, r1    
                                                                                                                                                                          
	/*                                                                                
	 * Flush TLB (Invalidate unified TLB unlocked entries)                            
	 */                                                                               
	mov     r1, #0                                                                    
	mcr     p15, 0, r1, c8, c7, 0                                                     
        
        pop     {r0-r3} 
                                                                                     
	/* Set the return value */                                                        
	mov	r0, #0                                                                        
                                                                                     
	/* return*/                                                      
        mov     pc, lr                                                                    
                                                         
	.align	4     
		
ENTRY(mem_clear_runtime_context)
	push    {r0-r3}
	/*                                                                                
	 * Save empty context for clear r0 - r10                                                               
	 */ 
	ldr     r3, .Lsaved_empty_context_svc
	mov	r0, #0                                               
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4
	str     r0, [r3], #0x4

 	/*                                                                                
	 * clear SVC context                                                            
	 */                                                                               
	ldr     r3, .Lsaved_empty_context_svc                                               
	ldmia   r3, {r0-r10} 
	
	pop     {r0-r3}   
	/* return*/                                                      
        mov     pc, lr  
ENDPROC(mem_clear_runtime_context)                                                          
	.align	4 

	.align	4  
	.globl save_runtime_context
save_runtime_context:
	/*save r0-r13 register*/   
	stmia   r0, {r0 - r13}
	mov	pc, lr
                                                

	.align	4  
	.globl clear_reg_context
clear_reg_context:
	/*clear r0-r11 register*/   
	mov	r0, #0  
	mov	r1, #0 
	mov	r2, #0  
	mov	r3, #0
	mov	r4, #0  
	mov	r5, #0
	mov	r6, #0  
	mov	r7, #0
	mov	r8, #0  
	mov	r9, #0
	mov	r10,#0
	mov	r11,#0
	mov	r12,#0
	mov	pc, lr
                               
.Lsaved_context_r13_sys:	.long	saved_context_r13_sys                                  
.Lsaved_cpsr_svc:		.long	saved_cpsr_svc                                            
.Lsaved_context_r12_svc: 	.long	saved_context_r12_svc 
.Lsaved_spsr_svc:		.long	saved_spsr_svc
.Lsaved_context_r13_fiq: 	.long	saved_context_r13_fiq                             
.Lsaved_spsr_fiq:		.long	saved_spsr_fiq
.Lsaved_context_r13_abt: 	.long	saved_context_r13_abt                             
.Lsaved_spsr_abt:		.long	saved_spsr_abt
.Lsaved_context_r13_irq: 	.long	saved_context_r13_irq                             
.Lsaved_spsr_irq:		.long	saved_spsr_irq
.Lsaved_context_r13_und: 	.long	saved_context_r13_und
.Lsaved_spsr_und:		.long	saved_spsr_und
.Lsaved_context_r13_mon: 	.long	saved_context_r13_mon
.Lsaved_spsr_mon:		.long	saved_spsr_mon
.Lsaved_empty_context_svc:	.long	saved_empty_context_svc




                                        