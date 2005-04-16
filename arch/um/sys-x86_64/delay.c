/*
 * Copyright 2003 PathScale, Inc.
 * Copied from arch/x86_64
 *
 * Licensed under the GPL
 */

#include "asm/processor.h"

void __delay(unsigned long loops)
{
	unsigned long i;

	for(i = 0; i < loops; i++) ;
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
