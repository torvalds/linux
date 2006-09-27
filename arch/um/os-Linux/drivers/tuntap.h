/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_TUNTAP_H
#define __UM_TUNTAP_H

#include "net_user.h"

struct tuntap_data {
	char *dev_name;
	int fixed_config;
	char *gate_addr;
	int fd;
	void *dev;
};

extern const struct net_user_info tuntap_user_info;

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
