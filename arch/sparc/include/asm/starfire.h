/*
 * starfire.h: Group all starfire specific code together.
 *
 * Copyright (C) 2000 Anton Blanchard (anton@samba.org)
 */

#ifndef _SPARC64_STARFIRE_H
#define _SPARC64_STARFIRE_H

#ifndef __ASSEMBLY__

extern int this_is_starfire;

void check_if_starfire(void);
int starfire_hard_smp_processor_id(void);
void starfire_hookup(int);
unsigned int starfire_translate(unsigned long imap, unsigned int upaid);

#endif
#endif
