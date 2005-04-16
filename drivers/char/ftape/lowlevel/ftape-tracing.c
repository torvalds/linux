/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
 *                (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-tracing.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:27 $
 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>
#include "../lowlevel/ftape-tracing.h"

/*      Global vars.
 */
/*      tracing
 *      set it to:     to log :
 *       0              bugs
 *       1              + errors
 *       2              + warnings
 *       3              + information
 *       4              + more information
 *       5              + program flow
 *       6              + fdc/dma info
 *       7              + data flow
 *       8              + everything else
 */
ft_trace_t ftape_tracing = ft_t_info; /* Default level: information and up */
int  ftape_function_nest_level;

/*      Local vars.
 */
static __u8 trace_id;
static char spacing[] = "*                              ";

void ftape_trace_call(const char *file, const char *name)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (ftape_function_nest_level < 0) {
		printk(KERN_INFO "function nest level (%d) < 0\n",
		       ftape_function_nest_level);
		ftape_function_nest_level = 0;
	}
	if (ftape_function_nest_level < sizeof(spacing)) {
		indent = (spacing +
			  sizeof(spacing) - 1 -
			  ftape_function_nest_level);
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d]%s+%s (%s)\n",
	       (int) trace_id++, indent, file, name);
}

void ftape_trace_exit(const char *file, const char *name)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (ftape_function_nest_level < 0) {
		printk(KERN_INFO "function nest level (%d) < 0\n", ftape_function_nest_level);
		ftape_function_nest_level = 0;
	}
	if (ftape_function_nest_level < sizeof(spacing)) {
		indent = (spacing +
			  sizeof(spacing) - 1 -
			  ftape_function_nest_level);
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d]%s-%s (%s)\n",
	       (int) trace_id++, indent, file, name);
}

void ftape_trace_log(const char *file, const char *function)
{
	char *indent;

	/*    Since printk seems not to work with "%*s" format
	 *    we'll use this work-around.
	 */
	if (ftape_function_nest_level < 0) {
		printk(KERN_INFO "function nest level (%d) < 0\n", ftape_function_nest_level);
		ftape_function_nest_level = 0;
	}
	if (ftape_function_nest_level < sizeof(spacing)) {
		indent = (spacing + 
			  sizeof(spacing) - 1 - 
			  ftape_function_nest_level);
	} else {
		indent = spacing;
	}
	printk(KERN_INFO "[%03d]%s%s (%s) - ", 
	       (int) trace_id++, indent, file, function);
}
