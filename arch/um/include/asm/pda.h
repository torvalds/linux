/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __UM_PDA_X86_64_H
#define __UM_PDA_X86_64_H

/* XXX */
struct foo {
	unsigned int __softirq_pending;
	unsigned int __nmi_count;
};

extern struct foo me;

#define read_pda(me) (&me)

#endif

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
