/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <setjmp.h>
#include <signal.h>
#include "kern_util.h"
#include "user_util.h"
#include "os.h"

void do_longjmp(void *b, int val)
{
	sigjmp_buf *buf = b;

	siglongjmp(*buf, val);
}
