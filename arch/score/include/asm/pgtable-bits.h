#ifndef _ASM_SCORE_PGTABLE_BITS_H
#define _ASM_SCORE_PGTABLE_BITS_H

#define _PAGE_ACCESSED			(1<<5)	/* implemented in software */
#define _PAGE_READ			(1<<6)	/* implemented in software */
#define _PAGE_WRITE			(1<<7)	/* implemented in software */
#define _PAGE_PRESENT			(1<<9)	/* implemented in software */
#define _PAGE_MODIFIED			(1<<10)	/* implemented in software */

#define _PAGE_GLOBAL			(1<<0)
#define _PAGE_VALID			(1<<1)
#define _PAGE_SILENT_READ		(1<<1)	/* synonym */
#define _PAGE_DIRTY			(1<<2)	/* Write bit */
#define _PAGE_SILENT_WRITE		(1<<2)
#define _PAGE_CACHE			(1<<3)	/* cache */
#define _CACHE_MASK			(1<<3)
#define _PAGE_BUFFERABLE		(1<<4)	/*Fallow Spec. */

#define __READABLE	(_PAGE_READ | _PAGE_SILENT_READ | _PAGE_ACCESSED)
#define __WRITEABLE	(_PAGE_WRITE | _PAGE_SILENT_WRITE | _PAGE_MODIFIED)
#define _PAGE_CHG_MASK			\
	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_MODIFIED | _PAGE_CACHE)

#endif /* _ASM_SCORE_PGTABLE_BITS_H */
