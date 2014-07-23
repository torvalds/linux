/*
 * Various register offset definitions for debuggers, core file
 * examiners and whatnot.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 Ralf Baechle
 * Copyright (C) 1995, 1999 Silicon Graphics
 */
#ifndef __ASM_MIPS_REG_H
#define __ASM_MIPS_REG_H

#define MIPS32_EF_R0		6
#define MIPS32_EF_R1		7
#define MIPS32_EF_R2		8
#define MIPS32_EF_R3		9
#define MIPS32_EF_R4		10
#define MIPS32_EF_R5		11
#define MIPS32_EF_R6		12
#define MIPS32_EF_R7		13
#define MIPS32_EF_R8		14
#define MIPS32_EF_R9		15
#define MIPS32_EF_R10		16
#define MIPS32_EF_R11		17
#define MIPS32_EF_R12		18
#define MIPS32_EF_R13		19
#define MIPS32_EF_R14		20
#define MIPS32_EF_R15		21
#define MIPS32_EF_R16		22
#define MIPS32_EF_R17		23
#define MIPS32_EF_R18		24
#define MIPS32_EF_R19		25
#define MIPS32_EF_R20		26
#define MIPS32_EF_R21		27
#define MIPS32_EF_R22		28
#define MIPS32_EF_R23		29
#define MIPS32_EF_R24		30
#define MIPS32_EF_R25		31

/*
 * k0/k1 unsaved
 */
#define MIPS32_EF_R26		32
#define MIPS32_EF_R27		33

#define MIPS32_EF_R28		34
#define MIPS32_EF_R29		35
#define MIPS32_EF_R30		36
#define MIPS32_EF_R31		37

/*
 * Saved special registers
 */
#define MIPS32_EF_LO		38
#define MIPS32_EF_HI		39

#define MIPS32_EF_CP0_EPC	40
#define MIPS32_EF_CP0_BADVADDR	41
#define MIPS32_EF_CP0_STATUS	42
#define MIPS32_EF_CP0_CAUSE	43
#define MIPS32_EF_UNUSED0	44

#define MIPS32_EF_SIZE		180

#define MIPS64_EF_R0		0
#define MIPS64_EF_R1		1
#define MIPS64_EF_R2		2
#define MIPS64_EF_R3		3
#define MIPS64_EF_R4		4
#define MIPS64_EF_R5		5
#define MIPS64_EF_R6		6
#define MIPS64_EF_R7		7
#define MIPS64_EF_R8		8
#define MIPS64_EF_R9		9
#define MIPS64_EF_R10		10
#define MIPS64_EF_R11		11
#define MIPS64_EF_R12		12
#define MIPS64_EF_R13		13
#define MIPS64_EF_R14		14
#define MIPS64_EF_R15		15
#define MIPS64_EF_R16		16
#define MIPS64_EF_R17		17
#define MIPS64_EF_R18		18
#define MIPS64_EF_R19		19
#define MIPS64_EF_R20		20
#define MIPS64_EF_R21		21
#define MIPS64_EF_R22		22
#define MIPS64_EF_R23		23
#define MIPS64_EF_R24		24
#define MIPS64_EF_R25		25

/*
 * k0/k1 unsaved
 */
#define MIPS64_EF_R26		26
#define MIPS64_EF_R27		27


#define MIPS64_EF_R28		28
#define MIPS64_EF_R29		29
#define MIPS64_EF_R30		30
#define MIPS64_EF_R31		31

/*
 * Saved special registers
 */
#define MIPS64_EF_LO		32
#define MIPS64_EF_HI		33

#define MIPS64_EF_CP0_EPC	34
#define MIPS64_EF_CP0_BADVADDR	35
#define MIPS64_EF_CP0_STATUS	36
#define MIPS64_EF_CP0_CAUSE	37

#define MIPS64_EF_SIZE		304	/* size in bytes */

#if defined(CONFIG_32BIT)

#define EF_R0			MIPS32_EF_R0
#define EF_R1			MIPS32_EF_R1
#define EF_R2			MIPS32_EF_R2
#define EF_R3			MIPS32_EF_R3
#define EF_R4			MIPS32_EF_R4
#define EF_R5			MIPS32_EF_R5
#define EF_R6			MIPS32_EF_R6
#define EF_R7			MIPS32_EF_R7
#define EF_R8			MIPS32_EF_R8
#define EF_R9			MIPS32_EF_R9
#define EF_R10			MIPS32_EF_R10
#define EF_R11			MIPS32_EF_R11
#define EF_R12			MIPS32_EF_R12
#define EF_R13			MIPS32_EF_R13
#define EF_R14			MIPS32_EF_R14
#define EF_R15			MIPS32_EF_R15
#define EF_R16			MIPS32_EF_R16
#define EF_R17			MIPS32_EF_R17
#define EF_R18			MIPS32_EF_R18
#define EF_R19			MIPS32_EF_R19
#define EF_R20			MIPS32_EF_R20
#define EF_R21			MIPS32_EF_R21
#define EF_R22			MIPS32_EF_R22
#define EF_R23			MIPS32_EF_R23
#define EF_R24			MIPS32_EF_R24
#define EF_R25			MIPS32_EF_R25
#define EF_R26			MIPS32_EF_R26
#define EF_R27			MIPS32_EF_R27
#define EF_R28			MIPS32_EF_R28
#define EF_R29			MIPS32_EF_R29
#define EF_R30			MIPS32_EF_R30
#define EF_R31			MIPS32_EF_R31
#define EF_LO			MIPS32_EF_LO
#define EF_HI			MIPS32_EF_HI
#define EF_CP0_EPC		MIPS32_EF_CP0_EPC
#define EF_CP0_BADVADDR		MIPS32_EF_CP0_BADVADDR
#define EF_CP0_STATUS		MIPS32_EF_CP0_STATUS
#define EF_CP0_CAUSE		MIPS32_EF_CP0_CAUSE
#define EF_UNUSED0		MIPS32_EF_UNUSED0
#define EF_SIZE			MIPS32_EF_SIZE

#elif defined(CONFIG_64BIT)

#define EF_R0			MIPS64_EF_R0
#define EF_R1			MIPS64_EF_R1
#define EF_R2			MIPS64_EF_R2
#define EF_R3			MIPS64_EF_R3
#define EF_R4			MIPS64_EF_R4
#define EF_R5			MIPS64_EF_R5
#define EF_R6			MIPS64_EF_R6
#define EF_R7			MIPS64_EF_R7
#define EF_R8			MIPS64_EF_R8
#define EF_R9			MIPS64_EF_R9
#define EF_R10			MIPS64_EF_R10
#define EF_R11			MIPS64_EF_R11
#define EF_R12			MIPS64_EF_R12
#define EF_R13			MIPS64_EF_R13
#define EF_R14			MIPS64_EF_R14
#define EF_R15			MIPS64_EF_R15
#define EF_R16			MIPS64_EF_R16
#define EF_R17			MIPS64_EF_R17
#define EF_R18			MIPS64_EF_R18
#define EF_R19			MIPS64_EF_R19
#define EF_R20			MIPS64_EF_R20
#define EF_R21			MIPS64_EF_R21
#define EF_R22			MIPS64_EF_R22
#define EF_R23			MIPS64_EF_R23
#define EF_R24			MIPS64_EF_R24
#define EF_R25			MIPS64_EF_R25
#define EF_R26			MIPS64_EF_R26
#define EF_R27			MIPS64_EF_R27
#define EF_R28			MIPS64_EF_R28
#define EF_R29			MIPS64_EF_R29
#define EF_R30			MIPS64_EF_R30
#define EF_R31			MIPS64_EF_R31
#define EF_LO			MIPS64_EF_LO
#define EF_HI			MIPS64_EF_HI
#define EF_CP0_EPC		MIPS64_EF_CP0_EPC
#define EF_CP0_BADVADDR		MIPS64_EF_CP0_BADVADDR
#define EF_CP0_STATUS		MIPS64_EF_CP0_STATUS
#define EF_CP0_CAUSE		MIPS64_EF_CP0_CAUSE
#define EF_SIZE			MIPS64_EF_SIZE

#endif /* CONFIG_64BIT */

#endif /* __ASM_MIPS_REG_H */
