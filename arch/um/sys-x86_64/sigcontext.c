/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "user.h"

void sc_to_sc(void *to_ptr, void *from_ptr)
{
        struct sigcontext *to = to_ptr, *from = from_ptr;
        int size = sizeof(*to); /* + sizeof(struct _fpstate); */

        memcpy(to, from, size);
        if(from->fpstate != NULL)
		to->fpstate = (struct _fpstate *) (to + 1);

	to->fpstate = NULL;
}

unsigned long *sc_sigmask(void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;

	return(&sc->oldmask);
}

/* Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
