/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#ifndef __I386_SIGNAL_H_
#define __I386_SIGNAL_H_

#include <signal.h>

#define ARCH_GET_SIGCONTEXT(sc, sig) \
	do sc = (struct sigcontext *) (&sig + 1); while(0)

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
