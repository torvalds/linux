/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/kernel.h"
#include "asm/errno.h"
#include "init.h"
#include "os.h"
#include "kern.h"

/* Changed by set_umid_arg and umid_file_name */
int umid_is_random = 0;
static int umid_inited = 0;

static int __init set_umid_arg(char *name, int *add)
{
	int err;

	if(umid_inited)
		return 0;

	*add = 0;
	err = set_umid(name, printf);
	if(err == -EEXIST){
		printf("umid '%s' already in use\n", name);
		umid_is_random = 1;
	}
	else if(!err)
		umid_inited = 1;

	return 0;
}

__uml_setup("umid=", set_umid_arg,
"umid=<name>\n"
"    This is used to assign a unique identity to this UML machine and\n"
"    is used for naming the pid file and management console socket.\n\n"
);

