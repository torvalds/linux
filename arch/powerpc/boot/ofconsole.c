// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OF console routines
 *
 * Copyright (C) Paul Mackerras 1997.
 */
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"

#include "of.h"

static unsigned int of_stdout_handle;

static int of_console_open(void)
{
	void *devp;

	if (((devp = of_finddevice("/chosen")) != NULL)
	    && (of_getprop(devp, "stdout", &of_stdout_handle,
			   sizeof(of_stdout_handle))
		== sizeof(of_stdout_handle))) {
		of_stdout_handle = be32_to_cpu(of_stdout_handle);
		return 0;
	}

	return -1;
}

static void of_console_write(const char *buf, int len)
{
	of_call_prom("write", 3, 1, of_stdout_handle, buf, len);
}

void of_console_init(void)
{
	console_ops.open = of_console_open;
	console_ops.write = of_console_write;
}
