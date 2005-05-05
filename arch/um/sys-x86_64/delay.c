/*
 * Copyright 2003 PathScale, Inc.
 * Copied from arch/x86_64
 *
 * Licensed under the GPL
 */

#include "linux/delay.h"
#include "asm/processor.h"
#include "asm/param.h"

void __delay(unsigned long loops)
{
	unsigned long i;

	for(i = 0; i < loops; i++) ;
}

void __udelay(unsigned long usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
	for(i=0;i<n;i++) ;
}

void __const_udelay(unsigned long usecs)
{
	int i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
	for(i=0;i<n;i++) ;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
