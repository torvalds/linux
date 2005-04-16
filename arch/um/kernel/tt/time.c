/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <sys/time.h>
#include <time_user.h>
#include "process.h"
#include "user.h"

void user_time_init_tt(void)
{
	if(signal(SIGVTALRM, (__sighandler_t) alarm_handler) == SIG_ERR)
		panic("Couldn't set SIGVTALRM handler");
	set_interval(ITIMER_VIRTUAL);
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
