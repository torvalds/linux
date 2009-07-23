/*
 * Support for suspend and resume on s390
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 *
 */

#include <asm/system.h>
#include <linux/interrupt.h>

void do_after_copyback(void)
{
	mb();
}

