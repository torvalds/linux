/*
 * tslib/tslib.c
 *
 * This file is placed under the LGPL.  Please see the file
 * COPYING for more details.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/device.h>
//#include <asm/typedef.h>
#include <mach/iomux.h>
#include <asm/uaccess.h>
#include <asm/types.h>
#include <asm/io.h>
#include <asm/delay.h>
#include "tslib.h"

struct tslib_info *g_tslib_inf = NULL;

int sqr(int x)
{
	return x * x;
}

int tslib_init(struct tslib_info *info, void *raw_read)
{
	struct tslib_info *tslib_inf = info;
	struct tslib_variance *var = NULL;
	struct tslib_dejitter *djt = NULL;

	if(raw_read == NULL)
		return -1;
	
	memset(tslib_inf, 0, sizeof(struct tslib_info));

	var = kmalloc(sizeof(struct tslib_variance), GFP_KERNEL);
	if (var == NULL)
		goto failed1;
	memset(var, 0, sizeof(struct tslib_variance));

	djt = kmalloc(sizeof(struct tslib_dejitter), GFP_KERNEL);
	if (djt == NULL)
		goto failed2;

	memset(djt, 0, sizeof(struct tslib_dejitter));

	var->flags = 0;
	var->delta = sqr(VARIANCE_DELTA);

	djt->head = 0;
	djt->delta = sqr(DEJITTER_DELTA);

	tslib_inf->raw_read = raw_read;
	tslib_inf->var = var;
	tslib_inf->djt = djt;

	g_tslib_inf = tslib_inf;

	return 0;
	
failed2:
	kfree(var);
failed1:
	return -1;
}
