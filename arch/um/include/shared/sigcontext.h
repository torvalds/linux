/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UML_SIGCONTEXT_H__
#define __UML_SIGCONTEXT_H__

#include "sysdep/sigcontext.h"

extern int sc_size(void *data);
extern void sc_to_sc(void *to_ptr, void *from_ptr);

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
