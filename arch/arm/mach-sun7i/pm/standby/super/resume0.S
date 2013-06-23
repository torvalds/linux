/***********************************************************************************************************************
;                             Copyright(C), 2012-2015, Newbie Microelectronic Co., Ltd.
;                                                  All Rights Reserved
;
; File Name : resume0.S
;
; Author : yanggq
;
; Version : 1.1.0
;
; Date : 2012.7.11
;
; Description :
;
; Functions list : none.
;
; Others : None at present.
;
;
; History :
;
;  <Author>        <time>       <version>      <description>
;
; gq.Yang       2012.7.11      1.1.0        build the file
;
***********************************************************************************************************************/

#define ARMV7_USR_MODE        (0x10)
#define ARMV7_FIQ_MODE        (0x11)
#define ARMV7_IRQ_MODE        (0x12)
#define ARMV7_SVC_MODE        (0x13)
#define ARMV7_MON_MODE        (0x16)
#define ARMV7_ABT_MODE        (0x17)
#define ARMV7_UND_MODE        (0x1b)
#define ARMV7_SYSTEM_MODE     (0x1f)
#define ARMV7_MODE_MASK       (0x1f)
#define ARMV7_FIQ_MASK        (0x40)
#define ARMV7_IRQ_MASK        (0x80)

//coprocessor CP15
//C1
#define ARMV7_THUMB_MASK    (1<<5)
#define ARMV7_END_MASK      (1<<9)
#define ARMV7_IT_MASK       ((0x3f<<10)|(0x03<<25))
#define ARMV7_GE_MASK       (0x0f<<16)
#define ARMV7_JAVA_MASK     (1<<24)
#define ARMV7_QFLAG_BIT     (1 << 27)
#define ARMV7_CC_V_BIT      (1 << 28)
#define ARMV7_CC_C_BIT      (1 << 29)
#define ARMV7_CC_Z_BIT      (1 << 30)
#define ARMV7_CC_N_BIT      (1 << 31)
#define ARMV7_CC_E_BIT      (1 << 9)
#define ARMV7_C1_M_BIT      ( 1 << 0 )
#define ARMV7_C1_A_BIT      ( 1 << 1 )
#define ARMV7_C1_C_BIT      ( 1 << 2 )
#define ARMV7_C1_Z_BIT      ( 1 << 11 )
#define ARMV7_C1_I_BIT      ( 1 << 12 )

    	.extern resume1_c_part
	.text
	.arm
/*********************************the begin of initializing system********************************/
	.globl _start
_start:
	mrs r0, cpsr
	bic r0, r0, #ARMV7_MODE_MASK
	orr r0, r0, #ARMV7_SVC_MODE
	orr r0, r0, #( ARMV7_IRQ_MASK | ARMV7_FIQ_MASK )    ;// After reset, ARM automaticly disables IRQ and FIQ, and runs in SVC mode.
	bic r0, r0, #ARMV7_CC_E_BIT                         ;// set little-endian
	msr cpsr_c, r0

// configure memory system : disable MMU,cache and write buffer; set little_endian;
	mrc p15, 0, r0, c1, c0
	bic r0, r0, #( ARMV7_C1_M_BIT | ARMV7_C1_C_BIT )  ;// disable MMU, data cache
	bic r0, r0, #( ARMV7_C1_I_BIT | ARMV7_C1_Z_BIT )  ;// disable instruction cache, disable flow prediction
	bic r0, r0, #( ARMV7_C1_A_BIT)                    ;// disable align
	mcr p15, 0, r0, c1, c0

/*------------------------------------------    end    ------------------------------------------*/

// set SP for C language
	bl save_sp_nommu

/**********************************the end of initializing system*********************************/
	bl set_pll
	bl  resume0_c_part

 	b .                                 ;// infinite loop

	.end