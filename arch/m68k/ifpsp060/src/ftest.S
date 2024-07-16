~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
MOTOROLA MICROPROCESSOR & MEMORY TECHNOLOGY GROUP
M68000 Hi-Performance Microprocessor Division
M68060 Software Package
Production Release P1.00 -- October 10, 1994

M68060 Software Package Copyright © 1993, 1994 Motorola Inc.  All rights reserved.

THE SOFTWARE is provided on an "AS IS" basis and without warranty.
To the maximum extent permitted by applicable law,
MOTOROLA DISCLAIMS ALL WARRANTIES WHETHER EXPRESS OR IMPLIED,
INCLUDING IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
and any warranty against infringement with regard to the SOFTWARE
(INCLUDING ANY MODIFIED VERSIONS THEREOF) and any accompanying written materials.

To the maximum extent permitted by applicable law,
IN NO EVENT SHALL MOTOROLA BE LIABLE FOR ANY DAMAGES WHATSOEVER
(INCLUDING WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR OTHER PECUNIARY LOSS)
ARISING OF THE USE OR INABILITY TO USE THE SOFTWARE.
Motorola assumes no responsibility for the maintenance and support of the SOFTWARE.

You are hereby granted a copyright license to use, modify, and distribute the SOFTWARE
so long as this entire notice is retained without alteration in any modified and/or
redistributed versions, and that such modified versions are clearly identified as such.
No licenses are granted by implication, estoppel or otherwise under any patents
or trademarks of Motorola, Inc.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#############################################
set	SREGS,		-64
set	IREGS,		-128
set	IFPREGS,	-224
set	SFPREGS,	-320
set	IFPCREGS,	-332
set	SFPCREGS,	-344
set	ICCR,		-346
set	SCCR,		-348
set	TESTCTR,	-352
set	DATA,		-384

#############################################
TESTTOP:
	bra.l		_060TESTS_
	short		0x0000

	bra.l		_060TESTS_unimp
	short		0x0000

	bra.l		_060TESTS_enable
	short		0x0000

start_str:
	string		"Testing 68060 FPSP started:\n"

start_str_unimp:
	string		"Testing 68060 FPSP unimplemented instruction started:\n"

start_str_enable:
	string		"Testing 68060 FPSP exception enabled started:\n"

pass_str:
	string		"passed\n"

fail_str:
	string		" failed\n"

	align		0x4
chk_test:
	tst.l		%d0
	bne.b		test_fail
test_pass:
	pea		pass_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp
	rts
test_fail:
	mov.l		%d1,-(%sp)
	bsr.l		_print_num
	addq.l		&0x4,%sp

	pea		fail_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp
	rts

#############################################
_060TESTS_:
	link		%a6,&-384

	movm.l		&0x3f3c,-(%sp)
	fmovm.x		&0xff,-(%sp)

	pea		start_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

### effadd
	clr.l		TESTCTR(%a6)
	pea		effadd_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

	bsr.l		effadd_0

	bsr.l		chk_test

### unsupp
	clr.l		TESTCTR(%a6)
	pea		unsupp_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

	bsr.l		unsupp_0

	bsr.l		chk_test

### ovfl non-maskable
	clr.l		TESTCTR(%a6)
	pea		ovfl_nm_str(%pc)
	bsr.l		_print_str
	bsr.l		ovfl_nm_0

	bsr.l		chk_test

### unfl non-maskable
	clr.l		TESTCTR(%a6)
	pea		unfl_nm_str(%pc)
	bsr.l		_print_str
	bsr.l		unfl_nm_0

	bsr.l		chk_test

	movm.l		(%sp)+,&0x3cfc
	fmovm.x		(%sp)+,&0xff

	unlk		%a6
	rts

_060TESTS_unimp:
	link		%a6,&-384

	movm.l		&0x3f3c,-(%sp)
	fmovm.x		&0xff,-(%sp)

	pea		start_str_unimp(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

### unimp
	clr.l		TESTCTR(%a6)
	pea		unimp_str(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

	bsr.l		unimp_0

	bsr.l		chk_test

	movm.l		(%sp)+,&0x3cfc
	fmovm.x		(%sp)+,&0xff

	unlk		%a6
	rts

_060TESTS_enable:
	link		%a6,&-384

	movm.l		&0x3f3c,-(%sp)
	fmovm.x		&0xff,-(%sp)

	pea		start_str_enable(%pc)
	bsr.l		_print_str
	addq.l		&0x4,%sp

### snan
	clr.l		TESTCTR(%a6)
	pea		snan_str(%pc)
	bsr.l		_print_str
	bsr.l		snan_0

	bsr.l		chk_test

### operr
	clr.l		TESTCTR(%a6)
	pea		operr_str(%pc)
	bsr.l		_print_str
	bsr.l		operr_0

	bsr.l		chk_test

### ovfl
	clr.l		TESTCTR(%a6)
	pea		ovfl_str(%pc)
	bsr.l		_print_str
	bsr.l		ovfl_0

	bsr.l		chk_test

### unfl
	clr.l		TESTCTR(%a6)
	pea		unfl_str(%pc)
	bsr.l		_print_str
	bsr.l		unfl_0

	bsr.l		chk_test

### dz
	clr.l		TESTCTR(%a6)
	pea		dz_str(%pc)
	bsr.l		_print_str
	bsr.l		dz_0

	bsr.l		chk_test

### inexact
	clr.l		TESTCTR(%a6)
	pea		inex_str(%pc)
	bsr.l		_print_str
	bsr.l		inex_0

	bsr.l		chk_test

	movm.l		(%sp)+,&0x3cfc
	fmovm.x		(%sp)+,&0xff

	unlk		%a6
	rts

#############################################
#############################################

unimp_str:
	string		"\tUnimplemented FP instructions..."

	align		0x4
unimp_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x40000000,DATA+0x0(%a6)
	mov.l		&0xc90fdaa2,DATA+0x4(%a6)
	mov.l		&0x2168c235,DATA+0x8(%a6)

	mov.w		&0x0000,%cc
unimp_0_pc:
	fsin.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0xbfbf0000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x08000208,IFPCREGS+0x4(%a6)
	lea		unimp_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

unimp_1:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x3ffe0000,DATA+0x0(%a6)
	mov.l		&0xc90fdaa2,DATA+0x4(%a6)
	mov.l		&0x2168c235,DATA+0x8(%a6)

	mov.w		&0x0000,%cc
unimp_1_pc:
	ftan.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x3fff0000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x00000208,IFPCREGS+0x4(%a6)
	lea		unimp_1_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# fmovecr
unimp_2:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc
unimp_2_pc:
	fmovcr.x	&0x31,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x40000000,IFPREGS+0x0(%a6)
	mov.l		&0x935d8ddd,IFPREGS+0x4(%a6)
	mov.l		&0xaaa8ac17,IFPREGS+0x8(%a6)
	mov.l		&0x00000208,IFPCREGS+0x4(%a6)
	lea		unimp_2_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# fscc
unimp_3:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.l		&0x0f000000,%fpsr
	mov.l		&0x00,%d7

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc
unimp_3_pc:
	fsgt		%d7

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0f008080,IFPCREGS+0x4(%a6)
	lea		unimp_3_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# fdbcc
unimp_4:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.l		&0x0f000000,%fpsr
	mov.l		&0x2,%d7

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc
unimp_4_pc:
	fdbgt.w		%d7,unimp_4_pc

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.w		&0xffff,IREGS+28+2(%a6)
	mov.l		&0x0f008080,IFPCREGS+0x4(%a6)
	lea		unimp_4_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# ftrapcc
unimp_5:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.l		&0x0f000000,%fpsr

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc
unimp_5_pc:
	ftpgt.l		&0xabcdef01

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0f008080,IFPCREGS+0x4(%a6)
	lea		unimp_5_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#############################################

effadd_str:
	string		"\tUnimplemented <ea>..."

	align		0x4
effadd_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmov.b		&0x2,%fp0

	mov.w		&0x0000,%cc
effadd_0_pc:
	fmul.x		&0xc00000008000000000000000,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0xc0010000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x08000000,IFPCREGS+0x4(%a6)
	lea		effadd_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

effadd_1:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc
effadd_1_pc:
	fabs.p		&0xc12300012345678912345678,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x3e660000,IFPREGS+0x0(%a6)
	mov.l		&0xd0ed23e8,IFPREGS+0x4(%a6)
	mov.l		&0xd14035bc,IFPREGS+0x8(%a6)
	mov.l		&0x00000108,IFPCREGS+0x4(%a6)
	lea		effadd_1_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovml_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	mov.w		&0x0000,%cc
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmovm.l		&0xffffffffffffffff,%fpcr,%fpsr

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0000fff0,IFPCREGS+0x0(%a6)
	mov.l		&0x0ffffff8,IFPCREGS+0x4(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovml_1:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	mov.w		&0x0000,%cc
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmovm.l		&0xffffffffffffffff,%fpcr,%fpiar

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0000fff0,IFPCREGS+0x0(%a6)
	mov.l		&0xffffffff,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovml_2:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	mov.w		&0x0000,%cc
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmovm.l		&0xffffffffffffffff,%fpsr,%fpiar

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0ffffff8,IFPCREGS+0x4(%a6)
	mov.l		&0xffffffff,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovml_3:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	mov.w		&0x0000,%cc
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmovm.l		&0xffffffffffffffffffffffff,%fpcr,%fpsr,%fpiar

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)
	mov.l		&0x0000fff0,IFPCREGS+0x0(%a6)
	mov.l		&0x0ffffff8,IFPCREGS+0x4(%a6)
	mov.l		&0xffffffff,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# fmovmx dynamic
fmovmx_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.b		&0x1,%fp0
	fmov.b		&0x2,%fp1
	fmov.b		&0x3,%fp2
	fmov.b		&0x4,%fp3
	fmov.b		&0x5,%fp4
	fmov.b		&0x6,%fp5
	fmov.b		&0x7,%fp6
	fmov.b		&0x8,%fp7

	fmov.l		&0x0,%fpiar
	mov.l		&0xffffffaa,%d0

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0xffff,IREGS(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)

	mov.w		&0x0000,%cc

	fmovm.x		%d0,-(%sp)

	mov.w		%cc,SCCR(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	fmov.s		&0x7f800000,%fp1
	fmov.s		&0x7f800000,%fp3
	fmov.s		&0x7f800000,%fp5
	fmov.s		&0x7f800000,%fp7

	fmov.x		(%sp)+,%fp1
	fmov.x		(%sp)+,%fp3
	fmov.x		(%sp)+,%fp5
	fmov.x		(%sp)+,%fp7

	movm.l		&0xffff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovmx_1:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.b		&0x1,%fp0
	fmov.b		&0x2,%fp1
	fmov.b		&0x3,%fp2
	fmov.b		&0x4,%fp3
	fmov.b		&0x5,%fp4
	fmov.b		&0x6,%fp5
	fmov.b		&0x7,%fp6
	fmov.b		&0x8,%fp7

	fmov.x		%fp6,-(%sp)
	fmov.x		%fp4,-(%sp)
	fmov.x		%fp2,-(%sp)
	fmov.x		%fp0,-(%sp)

	fmovm.x		&0xff,IFPREGS(%a6)

	fmov.s		&0x7f800000,%fp6
	fmov.s		&0x7f800000,%fp4
	fmov.s		&0x7f800000,%fp2
	fmov.s		&0x7f800000,%fp0

	fmov.l		&0x0,%fpiar
	fmov.l		&0x0,%fpsr
	mov.l		&0xffffffaa,%d0

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0xffff,IREGS(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.w		&0x0000,%cc

	fmovm.x		(%sp)+,%d0

	mov.w		%cc,SCCR(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	movm.l		&0xffff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

fmovmx_2:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	fmov.b		&0x1,%fp0
	fmov.b		&0x2,%fp1
	fmov.b		&0x3,%fp2
	fmov.b		&0x4,%fp3
	fmov.b		&0x5,%fp4
	fmov.b		&0x6,%fp5
	fmov.b		&0x7,%fp6
	fmov.b		&0x8,%fp7

	fmov.l		&0x0,%fpiar
	mov.l		&0xffffff00,%d0

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0xffff,IREGS(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)

	mov.w		&0x0000,%cc

	fmovm.x		%d0,-(%sp)

	mov.w		%cc,SCCR(%a6)

	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	movm.l		&0xffff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

###########################################################

# This test will take a non-maskable overflow directly.
ovfl_nm_str:
	string		"\tNon-maskable overflow..."

	align		0x4
ovfl_nm_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmov.b		&0x2,%fp0
	mov.l		&0x7ffe0000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)

	mov.w		&0x0000,%cc
ovfl_nm_0_pc:
	fmul.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x7fff0000,IFPREGS+0x0(%a6)
	mov.l		&0x00000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x02001048,IFPCREGS+0x4(%a6)
	lea		ovfl_nm_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

###########################################################

# This test will take an overflow directly.
ovfl_str:
	string		"\tEnabled overflow..."

	align		0x4
ovfl_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00001000,%fpcr
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	fmov.b		&0x2,%fp0
	mov.l		&0x7ffe0000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)

	mov.w		&0x0000,%cc
ovfl_0_pc:
	fmul.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x7fff0000,IFPREGS+0x0(%a6)
	mov.l		&0x00000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x02001048,IFPCREGS+0x4(%a6)
	lea		ovfl_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

# This test will take an underflow directly.
unfl_str:
	string		"\tEnabled underflow..."

	align		0x4
unfl_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00000800,%fpcr
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x00000000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
unfl_0_pc:
	fdiv.b		&0x2,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x00000000,IFPREGS+0x0(%a6)
	mov.l		&0x40000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x00000800,IFPCREGS+0x4(%a6)
	lea		unfl_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

# This test will take a non-maskable underflow directly.
unfl_nm_str:
	string		"\tNon-maskable underflow..."

	align		0x4
unfl_nm_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x00000000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
unfl_nm_0_pc:
	fdiv.b		&0x2,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x00000000,IFPREGS+0x0(%a6)
	mov.l		&0x40000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x00000800,IFPCREGS+0x4(%a6)
	lea		unfl_nm_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

inex_str:
	string		"\tEnabled inexact..."

	align		0x4
inex_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00000200,%fpcr		# enable inexact
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x50000000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
inex_0_pc:
	fadd.b		&0x2,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x50000000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x00000208,IFPCREGS+0x4(%a6)
	lea		inex_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

snan_str:
	string		"\tEnabled SNAN..."

	align		0x4
snan_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00004000,%fpcr		# enable SNAN
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0xffff0000,DATA+0x0(%a6)
	mov.l		&0x00000000,DATA+0x4(%a6)
	mov.l		&0x00000001,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
snan_0_pc:
	fadd.b		&0x2,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0xffff0000,IFPREGS+0x0(%a6)
	mov.l		&0x00000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000001,IFPREGS+0x8(%a6)
	mov.l		&0x09004080,IFPCREGS+0x4(%a6)
	lea		snan_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

operr_str:
	string		"\tEnabled OPERR..."

	align		0x4
operr_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00002000,%fpcr		# enable OPERR
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0xffff0000,DATA+0x0(%a6)
	mov.l		&0x00000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
operr_0_pc:
	fadd.s		&0x7f800000,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0xffff0000,IFPREGS+0x0(%a6)
	mov.l		&0x00000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x01002080,IFPCREGS+0x4(%a6)
	lea		operr_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

dz_str:
	string		"\tEnabled DZ..."

	align		0x4
dz_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmov.l		&0x00000400,%fpcr		# enable DZ
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x40000000,DATA+0x0(%a6)
	mov.l		&0x80000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmovm.x		DATA(%a6),&0x80

	mov.w		&0x0000,%cc
dz_0_pc:
	fdiv.b		&0x0,%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x40000000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x02000410,IFPCREGS+0x4(%a6)
	lea		dz_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

#####################################################################

unsupp_str:
	string		"\tUnimplemented data type/format..."

# an unnormalized number
	align		0x4
unsupp_0:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0xc03f0000,DATA+0x0(%a6)
	mov.l		&0x00000000,DATA+0x4(%a6)
	mov.l		&0x00000001,DATA+0x8(%a6)
	fmov.b		&0x2,%fp0
	mov.w		&0x0000,%cc
unsupp_0_pc:
	fmul.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0xc0010000,IFPREGS+0x0(%a6)
	mov.l		&0x80000000,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x08000000,IFPCREGS+0x4(%a6)
	lea		unsupp_0_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# a denormalized number
unsupp_1:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0x80000000,DATA+0x0(%a6)
	mov.l		&0x01000000,DATA+0x4(%a6)
	mov.l		&0x00000000,DATA+0x8(%a6)
	fmov.l		&0x7fffffff,%fp0

	mov.w		&0x0000,%cc
unsupp_1_pc:
	fmul.x		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x80170000,IFPREGS+0x0(%a6)
	mov.l		&0xfffffffe,IFPREGS+0x4(%a6)
	mov.l		&0x00000000,IFPREGS+0x8(%a6)
	mov.l		&0x08000000,IFPCREGS+0x4(%a6)
	lea		unsupp_1_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

# packed
unsupp_2:
	addq.l		&0x1,TESTCTR(%a6)

	movm.l		DEF_REGS(%pc),&0x3fff
	fmovm.x		DEF_FPREGS(%pc),&0xff
	fmovm.l		DEF_FPCREGS(%pc),%fpcr,%fpsr,%fpiar

	mov.w		&0x0000,ICCR(%a6)
	movm.l		&0x7fff,IREGS(%a6)
	fmovm.x		&0xff,IFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,IFPCREGS(%a6)

	mov.l		&0xc1230001,DATA+0x0(%a6)
	mov.l		&0x23456789,DATA+0x4(%a6)
	mov.l		&0x12345678,DATA+0x8(%a6)

	mov.w		&0x0000,%cc
unsupp_2_pc:
	fabs.p		DATA(%a6),%fp0

	mov.w		%cc,SCCR(%a6)
	movm.l		&0x7fff,SREGS(%a6)
	fmovm.x		&0xff,SFPREGS(%a6)
	fmovm.l		%fpcr,%fpsr,%fpiar,SFPCREGS(%a6)

	mov.l		&0x3e660000,IFPREGS+0x0(%a6)
	mov.l		&0xd0ed23e8,IFPREGS+0x4(%a6)
	mov.l		&0xd14035bc,IFPREGS+0x8(%a6)
	mov.l		&0x00000108,IFPCREGS+0x4(%a6)
	lea		unsupp_2_pc(%pc),%a0
	mov.l		%a0,IFPCREGS+0x8(%a6)

	bsr.l		chkregs
	tst.b		%d0
	bne.l		error

	bsr.l		chkfpregs
	tst.b		%d0
	bne.l		error

	clr.l		%d0
	rts

###########################################################
###########################################################

chkregs:
	lea		IREGS(%a6),%a0
	lea		SREGS(%a6),%a1
	mov.l		&14,%d0
chkregs_loop:
	cmp.l		(%a0)+,(%a1)+
	bne.l		chkregs_error
	dbra.w		%d0,chkregs_loop

	mov.w		ICCR(%a6),%d0
	mov.w		SCCR(%a6),%d1
	cmp.w		%d0,%d1
	bne.l		chkregs_error

	clr.l		%d0
	rts

chkregs_error:
	movq.l		&0x1,%d0
	rts

error:
	mov.l		TESTCTR(%a6),%d1
	movq.l		&0x1,%d0
	rts

chkfpregs:
	lea		IFPREGS(%a6),%a0
	lea		SFPREGS(%a6),%a1
	mov.l		&23,%d0
chkfpregs_loop:
	cmp.l		(%a0)+,(%a1)+
	bne.l		chkfpregs_error
	dbra.w		%d0,chkfpregs_loop

	lea		IFPCREGS(%a6),%a0
	lea		SFPCREGS(%a6),%a1
	cmp.l		(%a0)+,(%a1)+
	bne.l		chkfpregs_error
	cmp.l		(%a0)+,(%a1)+
	bne.l		chkfpregs_error
	cmp.l		(%a0)+,(%a1)+
	bne.l		chkfpregs_error

	clr.l		%d0
	rts

chkfpregs_error:
	movq.l		&0x1,%d0
	rts

DEF_REGS:
	long		0xacacacac, 0xacacacac, 0xacacacac, 0xacacacac
	long		0xacacacac, 0xacacacac, 0xacacacac, 0xacacacac

	long		0xacacacac, 0xacacacac, 0xacacacac, 0xacacacac
	long		0xacacacac, 0xacacacac, 0xacacacac, 0xacacacac

DEF_FPREGS:
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff
	long		0x7fff0000, 0xffffffff, 0xffffffff

DEF_FPCREGS:
	long		0x00000000, 0x00000000, 0x00000000

############################################################

_print_str:
	mov.l		%d0,-(%sp)
	mov.l		(TESTTOP-0x80+0x0,%pc),%d0
	pea		(TESTTOP-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

_print_num:
	mov.l		%d0,-(%sp)
	mov.l		(TESTTOP-0x80+0x4,%pc),%d0
	pea		(TESTTOP-0x80,%pc,%d0)
	mov.l		0x4(%sp),%d0
	rtd		&0x4

############################################################
