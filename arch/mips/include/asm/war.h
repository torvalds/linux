/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle
 * Copyright (C) 2007  Maciej W. Rozycki
 */
#ifndef _ASM_WAR_H
#define _ASM_WAR_H

/*
 * Work around certain R4000 CPU errata (as implemented by GCC):
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed immediately after starting an integer division:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #28
 *   "MIPS R4000MC Errata, Processor Revision 2.2 and 3.0", erratum
 *   #19
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed while an integer multiplication is in progress:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   errata #16 & #28
 *
 * - An integer division may give an incorrect result if started in
 *   a delay slot of a taken branch or a jump:
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #52
 */
#ifdef CONFIG_CPU_R4000_WORKAROUNDS
#define R4000_WAR 1
#else
#define R4000_WAR 0
#endif

/*
 * Work around certain R4400 CPU errata (as implemented by GCC):
 *
 * - A double-word or a variable shift may give an incorrect result
 *   if executed immediately after starting an integer division:
 *   "MIPS R4400MC Errata, Processor Revision 1.0", erratum #10
 *   "MIPS R4400MC Errata, Processor Revision 2.0 & 3.0", erratum #4
 */
#ifdef CONFIG_CPU_R4400_WORKAROUNDS
#define R4400_WAR 1
#else
#define R4400_WAR 0
#endif

/*
 * Work around the "daddi" and "daddiu" CPU errata:
 *
 * - The `daddi' instruction fails to trap on overflow.
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #23
 *
 * - The `daddiu' instruction can produce an incorrect result.
 *   "MIPS R4000PC/SC Errata, Processor Revision 2.2 and 3.0",
 *   erratum #41
 *   "MIPS R4000MC Errata, Processor Revision 2.2 and 3.0", erratum
 *   #15
 *   "MIPS R4400PC/SC Errata, Processor Revision 1.0", erratum #7
 *   "MIPS R4400MC Errata, Processor Revision 1.0", erratum #5
 */
#ifdef CONFIG_CPU_DADDI_WORKAROUNDS
#define DADDI_WAR 1
#else
#define DADDI_WAR 0
#endif

#endif /* _ASM_WAR_H */
