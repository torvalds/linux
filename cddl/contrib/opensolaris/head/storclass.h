/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef	_STORCLASS_H
#define	_STORCLASS_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* SVr4.0 1.6 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *   STORAGE CLASSES
 */

#define	C_EFCN		-1	/* physical end of function */
#define	C_NULL		0
#define	C_AUTO		1	/* automatic variable */
#define	C_EXT		2	/* external symbol */
#define	C_STAT		3	/* static */
#define	C_REG		4	/* register variable */
#define	C_EXTDEF	5	/* external definition */
#define	C_LABEL		6	/* label */
#define	C_ULABEL	7	/* undefined label */
#define	C_MOS		8	/* member of structure */
#define	C_ARG		9	/* function argument */
#define	C_STRTAG	10	/* structure tag */
#define	C_MOU		11	/* member of union */
#define	C_UNTAG		12	/* union tag */
#define	C_TPDEF		13	/* type definition */
#define	C_USTATIC	14	/* undefined static */
#define	C_ENTAG		15	/* enumeration tag */
#define	C_MOE		16	/* member of enumeration */
#define	C_REGPARM	17	/* register parameter */
#define	C_FIELD		18	/* bit field */
#define	C_BLOCK		100	/* ".bb" or ".eb" */
#define	C_FCN		101	/* ".bf" or ".ef" */
#define	C_EOS		102	/* end of structure */
#define	C_FILE		103	/* file name */

/*
 * The following storage class is a "dummy" used only by STS
 * for line number entries reformatted as symbol table entries
 */

#define	C_LINE		104
#define	C_ALIAS	105	/* duplicate tag */
#define	C_HIDDEN	106	/* special storage class for external */
				/* symbols in dmert public libraries */
#define	 C_SHADOW	107	/* shadow symbol */

#ifdef	__cplusplus
}
#endif

#endif	/* _STORCLASS_H */
