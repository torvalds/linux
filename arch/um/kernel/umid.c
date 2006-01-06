/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "asm/errno.h"
#include "init.h"
#include "os.h"
#include "kern.h"
#include "linux/kernel.h"

/* Changed by set_umid_arg */
static int umid_inited = 0;

static int __init set_umid_arg(char *name, int *add)
{
	int err;

	if(umid_inited)
		return 0;

	*add = 0;
	err = set_umid(name);
	if(err == -EEXIST)
		printf("umid '%s' already in use\n", name);
	else if(!err)
		umid_inited = 1;

	return 0;
}

__uml_setup("umid=", set_umid_arg,
"umid=<name>\n"
"    This is used to assign a unique identity to this UML machine and\n"
"    is used for naming the pid file and management console socket.\n\n"
);

