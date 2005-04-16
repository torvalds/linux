/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <string.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include "sysdep/ptrace.h"
#include "kern_util.h"

void sc_to_sc(void *to_ptr, void *from_ptr)
{
	struct sigcontext *to = to_ptr, *from = from_ptr;

	memcpy(to, from, sizeof(*to) + sizeof(struct _fpstate));
	if(from->fpstate != NULL)
		to->fpstate = (struct _fpstate *) (to + 1);
}

unsigned long *sc_sigmask(void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	return &sc->oldmask;
}

int sc_get_fpregs(unsigned long buf, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	struct _fpstate *from = sc->fpstate, *to = (struct _fpstate *) buf;
	int err = 0;

	if(from == NULL){
		err |= clear_user_proc(&to->cw, sizeof(to->cw));
		err |= clear_user_proc(&to->sw, sizeof(to->sw));
		err |= clear_user_proc(&to->tag, sizeof(to->tag));
		err |= clear_user_proc(&to->ipoff, sizeof(to->ipoff));
		err |= clear_user_proc(&to->cssel, sizeof(to->cssel));
		err |= clear_user_proc(&to->dataoff, sizeof(to->dataoff));
		err |= clear_user_proc(&to->datasel, sizeof(to->datasel));
		err |= clear_user_proc(&to->_st, sizeof(to->_st));
	}
	else {
		err |= copy_to_user_proc(&to->cw, &from->cw, sizeof(to->cw));
		err |= copy_to_user_proc(&to->sw, &from->sw, sizeof(to->sw));
		err |= copy_to_user_proc(&to->tag, &from->tag, 
					 sizeof(to->tag));
		err |= copy_to_user_proc(&to->ipoff, &from->ipoff, 
					 sizeof(to->ipoff));
		err |= copy_to_user_proc(&to->cssel,& from->cssel, 
					 sizeof(to->cssel));
		err |= copy_to_user_proc(&to->dataoff, &from->dataoff, 
				    sizeof(to->dataoff));
		err |= copy_to_user_proc(&to->datasel, &from->datasel, 
				    sizeof(to->datasel));
		err |= copy_to_user_proc(to->_st, from->_st, sizeof(to->_st));
	}
	return(err);
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
