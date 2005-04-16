/* 
 * Copyright (C) 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "net_user.h"

struct mcast_data {
	char *addr;
	unsigned short port;
	void *mcast_addr;
	int ttl;
	void *dev;
};

extern struct net_user_info mcast_user_info;

extern int mcast_user_write(int fd, void *buf, int len, 
			    struct mcast_data *pri);

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
