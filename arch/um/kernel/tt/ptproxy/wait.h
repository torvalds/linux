/**********************************************************************
wait.h

Copyright (C) 1999 Lars Brinkhoff.  See the file COPYING for licensing
terms and conditions.
**********************************************************************/

#ifndef __PTPROXY_WAIT_H
#define __PTPROXY_WAIT_H

extern int proxy_wait_return(struct debugger *debugger, pid_t unused);
extern int real_wait_return(struct debugger *debugger);
extern int parent_wait_return(struct debugger *debugger, pid_t unused);

#endif
