/*
 * Implementation of s390 diagnose codes
 *
 * Copyright IBM Corp. 2007
 * Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/module.h>
#include <asm/diag.h>

/*
 * Diagnose 14: Input spool file manipulation
 */
int diag14(unsigned long rx, unsigned long ry1, unsigned long subcode)
{
	register unsigned long _ry1 asm("2") = ry1;
	register unsigned long _ry2 asm("3") = subcode;
	int rc = 0;

	asm volatile(
#ifdef CONFIG_64BIT
		"   sam31\n"
		"   diag    %2,2,0x14\n"
		"   sam64\n"
#else
		"   diag    %2,2,0x14\n"
#endif
		"   ipm     %0\n"
		"   srl     %0,28\n"
		: "=d" (rc), "+d" (_ry2)
		: "d" (rx), "d" (_ry1)
		: "cc");

	return rc;
}
EXPORT_SYMBOL(diag14);

/*
 * Diagnose 210: Get information about a virtual device
 */
int diag210(struct diag210 *addr)
{
	/*
	 * diag 210 needs its data below the 2GB border, so we
	 * use a static data area to be sure
	 */
	static struct diag210 diag210_tmp;
	static DEFINE_SPINLOCK(diag210_lock);
	unsigned long flags;
	int ccode;

	spin_lock_irqsave(&diag210_lock, flags);
	diag210_tmp = *addr;

#ifdef CONFIG_64BIT
	asm volatile(
		"	lhi	%0,-1\n"
		"	sam31\n"
		"	diag	%1,0,0x210\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:	sam64\n"
		EX_TABLE(0b, 1b)
		: "=&d" (ccode) : "a" (&diag210_tmp) : "cc", "memory");
#else
	asm volatile(
		"	lhi	%0,-1\n"
		"	diag	%1,0,0x210\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "=&d" (ccode) : "a" (&diag210_tmp) : "cc", "memory");
#endif

	*addr = diag210_tmp;
	spin_unlock_irqrestore(&diag210_lock, flags);

	return ccode;
}
EXPORT_SYMBOL(diag210);
