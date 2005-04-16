/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <signal.h>
#include "time_user.h"
#include "mode.h"
#include "sysdep/signal.h"

void sig_handler(int sig)
{
	struct sigcontext *sc;

	ARCH_GET_SIGCONTEXT(sc, sig);
	CHOOSE_MODE_PROC(sig_handler_common_tt, sig_handler_common_skas,
			 sig, sc);
}

extern int timer_irq_inited;

void alarm_handler(int sig)
{
	struct sigcontext *sc;

	ARCH_GET_SIGCONTEXT(sc, sig);
	if(!timer_irq_inited) return;

	if(sig == SIGALRM)
		switch_timers(0);

	CHOOSE_MODE_PROC(sig_handler_common_tt, sig_handler_common_skas,
			 sig, sc);

	if(sig == SIGALRM)
		switch_timers(1);
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
