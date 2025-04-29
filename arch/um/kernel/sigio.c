// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 */

#include <linux/interrupt.h>
#include <irq_kern.h>
#include <os.h>
#include <sigio.h>

/* These are called from os-Linux/sigio.c to protect its pollfds arrays. */
static DEFINE_MUTEX(sigio_mutex);

void sigio_lock(void)
{
	mutex_lock(&sigio_mutex);
}

void sigio_unlock(void)
{
	mutex_unlock(&sigio_mutex);
}
