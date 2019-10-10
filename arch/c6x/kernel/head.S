; SPDX-License-Identifier: GPL-2.0-only
;
;  Port on Texas Instruments TMS320C6x architecture
;
;  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
;  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
;
#include <linux/linkage.h>
#include <linux/of_fdt.h>
#include <asm/asm-offsets.h>

	__HEAD
ENTRY(_c_int00)
	;; Save magic and pointer
	MV	.S1	A4,A10
	MV	.S2	B4,B10
	MVKL	.S2	__bss_start,B5
	MVKH	.S2	__bss_start,B5
	MVKL	.S2	__bss_stop,B6
	MVKH	.S2	__bss_stop,B6
	SUB	.L2	B6,B5,B6 ; bss size

	;; Set the stack pointer
	MVKL	.S2	current_ksp,B0
	MVKH	.S2	current_ksp,B0
	LDW	.D2T2	*B0,B15

	;; clear bss
	SHR	.S2	B6,3,B0	  ; number of dwords to clear
	ZERO	.L2	B13
	ZERO	.L2	B12
bss_loop:
	BDEC	.S2	bss_loop,B0
	NOP	3
	CMPLT	.L2	B0,0,B1
 [!B1]	STDW	.D2T2	B13:B12,*B5++[1]

	NOP	4
	AND	.D2	~7,B15,B15

	;; Clear GIE and PGIE
	MVC	.S2	CSR,B2
	CLR	.S2	B2,0,1,B2
	MVC	.S2	B2,CSR
	MVC	.S2	TSR,B2
	CLR	.S2	B2,0,1,B2
	MVC	.S2	B2,TSR
	MVC	.S2	ITSR,B2
	CLR	.S2	B2,0,1,B2
	MVC	.S2	B2,ITSR
	MVC	.S2	NTSR,B2
	CLR	.S2	B2,0,1,B2
	MVC	.S2	B2,NTSR

	;; pass DTB pointer to machine_init (or zero if none)
	MVKL	.S1	OF_DT_HEADER,A0
	MVKH	.S1	OF_DT_HEADER,A0
	CMPEQ	.L1	A10,A0,A0
  [A0]	MV	.S1X	B10,A4
  [!A0] MVK	.S1	0,A4

#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	machine_init,A0
	MVKH	.S1	machine_init,A0
	B	.S2X	A0
	ADDKPC  .S2     0f,B3,4
0:
#else
	CALLP	.S2	machine_init,B3
#endif

	;; Jump to Linux init
#ifdef CONFIG_C6X_BIG_KERNEL
	MVKL	.S1	start_kernel,A0
	MVKH	.S1	start_kernel,A0
	B	.S2X	A0
#else
	B	.S2	start_kernel
#endif
	NOP	5
L1:	BNOP	.S2	L1,5
