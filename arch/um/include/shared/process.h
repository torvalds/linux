/* 
 * Copyright (C) 2000 - 2008 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <signal.h>

extern void sig_handler(int sig, struct sigcontext *sc);
extern void alarm_handler(int sig, struct sigcontext *sc);

#endif
