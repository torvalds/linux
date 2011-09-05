/*
 * arch/arm/include/asm/pgtable-2level-types.h
 *
 * Copyright (C) 1995-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef _ASM_PGTABLE_2LEVEL_TYPES_H
#define _ASM_PGTABLE_2LEVEL_TYPES_H

typedef unsigned long pteval_t;

#undef STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { pteval_t pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd[2]; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd[0])
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef pteval_t pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t[2];
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)	((x)[0])
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgprot(x)     (x)

#endif /* STRICT_MM_TYPECHECKS */

#endif	/* _ASM_PGTABLE_2LEVEL_TYPES_H */
