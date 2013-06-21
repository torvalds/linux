#ifndef _MEM_CONTEXT_H
#define _MEM_CONTEXT_H

/*
 * Copyright (c) 2011-2015 yanggq.young@allwinnertech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
 
/* The following architectures are known to be CORTEX_A9 */
#if defined(CONFIG_ARCH_SUN6I) || defined(CONFIG_ARCH_SUN7I)
#define CORTEX_A7
#elif defined(CONFIG_ARCH_SUN4I)
/* Assume CORTEX_A8 as default */
#define CORTEX_A8
#elif defined(CONFIG_ARCH_SUN5I)
#define CORTEX_A8
#endif

/*
 * Image of the saved processor state
 *
 * coprocessor 15 registers(RW)
 */
struct saved_context {
/*
 * FIXME: Only support for Cortex A8 and Cortex A9 now
 */
	/* CR0 */
	__u32 cssr;	/* Cache Size Selection */
	/* CR1 */
#ifdef CORTEX_A8
	__u32 cr;		/* Control */
	__u32 acr;	/* Auxiliary Control Register*/	
	__u32 cacr;	/* Coprocessor Access Control */
	__u32 sccfgr;	/* Secure Config Register*/	
	__u32 scdbgenblr;	/* Secure Debug Enable Register*/
	__u32 nonscacctrlr;/* Nonsecure Access Control Register*/		
#elif defined(CORTEX_A9)
	__u32 cr;
	__u32 actlr;
	__u32 cacr;
	__u32 sder;
	__u32 vcr;
#elif defined(CORTEX_A7)
	__u32 cr;		/* Control */
	__u32 acr;	/* Auxiliary Control Register*/	
	__u32 cacr;	/* Coprocessor Access Control */
	__u32 sccfgr;	/* Secure Config Register*/	
	__u32 scdbgenblr;	/* Secure Debug Enable Register*/
	__u32 nonscacctrlr;/* Nonsecure Access Control Register*/
#endif

	/* CR2 */
	__u32 ttb_0r;	/* Translation Table Base 0 */
	__u32 ttb_1r;	/* Translation Table Base 1 */
	__u32 ttbcr;	/* Translation Talbe Base Control */
	/* CR3 */
	__u32 dacr;	/* Domain Access Control */
	/* CR5 */
	__u32 d_fsr;	/* Data Fault Status */
	__u32 i_fsr;	/* Instruction Fault Status */
	__u32 d_afsr;	/* Data Auxilirary Fault Status */	 ;
	__u32 i_afsr;	/* Instruction Auxilirary Fault Status */;
	/* CR6 */
	__u32 d_far;	/* Data Fault Address */
	__u32 i_far;	/* Instruction Fault Address */
	/* CR7 */
	__u32 par;	/* Physical Address */
	/* CR9 */	/* FIXME: Are they necessary? */
	__u32 pmcontrolr;	/* Performance Monitor Control */
	__u32 cesr;	/* Count Enable Set */
	__u32 cecr;	/* Count Enable Clear */
	__u32 ofsr;	/* Overflow Flag Status */
#ifdef CORTEX_A8
	__u32 sir;	/* Software Increment */
#elif defined(CORTEX_A7)
	__u32 sir;	/* Software Increment */
#endif
	__u32 pcsr;	/* Performance Counter Selection */
	__u32 ccr;	/* Cycle Count */
	__u32 esr;	/* Event Selection */
	__u32 pmcountr;	/* Performance Monitor Count */
	__u32 uer;	/* User Enable */
	__u32 iesr;	/* Interrupt Enable Set */
	__u32 iecr;	/* Interrupt Enable Clear */
#ifdef CORTEX_A8
	__u32 l2clr;	/* L2 Cache Lockdown */
	__u32 l2cauxctrlr;	/* L2 Cache Auxiliary Control */
#elif defined(CORTEX_A7)

#endif

	/* CR10 */	
#ifdef CORTEX_A8
	__u32 d_tlblr;	/* Data TLB Lockdown Register */
	__u32 i_tlblr;	/* Instruction TLB Lockdown Register */
#elif defined(CORTEX_A7)
#endif
	__u32 prrr;	/* Primary Region Remap Register */
	__u32 nrrr;	/* Normal Memory Remap Register */
	
	/* CR11 */
#ifdef CORTEX_A8
	__u32 pleuar;	/* PLE User Accessibility */
	__u32 plecnr;	/* PLE Channel Number */
	__u32 plecr;	/* PLE Control */
	__u32 pleisar;	/* PLE Internal Start Address */
	__u32 pleiear;	/* PLE Internal End Address */
	__u32 plecidr;	/* PLE Context ID */
#elif defined(CORTEX_A7)
#endif

	/* CR12 */
#ifdef CORTEX_A8
	__u32 snsvbar;	/* Secure or Nonsecure Vector Base Address */
	__u32 monvecbar; /*Monitor Vector Base*/	
#elif defined(CORTEX_A9)
	__u32 vbar;
	__u32 mvbar;
	__u32 vir;
#elif defined(CORTEX_A7)
	__u32 vbar;
	__u32 mvbar;
	__u32 isr;
#endif

	/* CR13 */
	__u32 fcse;	/* FCSE PID */
	__u32 cid;	/* Context ID */
	__u32 urwtpid;	/* User read/write Thread and Process ID */
	__u32 urotpid;	/* User read-only Thread and Process ID */
	__u32 potpid;	/* Privileged only Thread and Process ID */
	
	/* CR15 */
#ifdef CORTEX_A9
	__u32 mtlbar;
#endif
} __attribute__((packed));

void __save_processor_state(struct saved_context *ctxt);
void __restore_processor_state(struct saved_context *ctxt);
void set_copro_default(void);

#endif /*_MEM_CONTEXT_H*/
