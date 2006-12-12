/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/init.h"
#include "mconsole_kern.h"

#ifdef CONFIG_MCONSOLE

extern int gdb_config(char *str);
extern int gdb_remove(int n);

static struct mc_device gdb_mc = {
	.name		= "gdb",
	.config		= gdb_config,
	.remove		= gdb_remove,
};

int gdb_mc_init(void)
{
	mconsole_register_dev(&gdb_mc);
	return(0);
}

__initcall(gdb_mc_init);

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
