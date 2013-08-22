/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE! 
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com. 
 *	Thank you. 
 *
 */

/* $Id: dgap_trace.c,v 1.1 2009/10/23 14:01:57 markh Exp $ */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/vmalloc.h>

#include "dgap_driver.h"

#define TRC_TO_CONSOLE 1

/* file level globals */
static char *dgap_trcbuf;		/* the ringbuffer */

#if defined(TRC_TO_KMEM)
static int dgap_trcbufi = 0;		/* index of the tilde at the end of */
#endif

extern int dgap_trcbuf_size;		/* size of the ringbuffer */

#if defined(TRC_TO_KMEM)
static DEFINE_SPINLOCK(dgap_tracef_lock);
#endif

#if 0

#if !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE)
void dgap_tracef(const char *fmt, ...)
{
	return;
}

#else /* !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE) */

void dgap_tracef(const char *fmt, ...)
{
	va_list	         ap;
	char  	         buf[TRC_MAXMSG+1];
	size_t		 lenbuf;
	int		 i;
	static int	 failed = FALSE;
# if defined(TRC_TO_KMEM)
	unsigned long	 flags;
#endif

	if(failed)
		return;
# if defined(TRC_TO_KMEM)
	DGAP_LOCK(dgap_tracef_lock, flags);
#endif

	/* Format buf using fmt and arguments contained in ap. */
	va_start(ap, fmt);
	i = vsprintf(buf, fmt,  ap);
	va_end(ap);
	lenbuf = strlen(buf);

# if defined(TRC_TO_KMEM)
	{
		static int	 initd=0;

		/*
		 * Now, in addition to (or instead of) printing this stuff out
		 * (which is a buffered operation), also tuck it away into a
		 * corner of memory which can be examined post-crash in kdb.
		 */
		if (!initd) {
			dgap_trcbuf = (char *) vmalloc(dgap_trcbuf_size);
			if(!dgap_trcbuf) {
				failed = TRUE;
				printk("dgap: tracing init failed!\n");
				return;
			}

			memset(dgap_trcbuf, '\0',  dgap_trcbuf_size);
			dgap_trcbufi = 0;
			initd++;

			printk("dgap: tracing enabled - " TRC_DTRC 
				" 0x%lx 0x%x\n",
				(unsigned long)dgap_trcbuf, 
				dgap_trcbuf_size);
		}

#  if defined(TRC_ON_OVERFLOW_WRAP_AROUND)
		/*
		 * This is the less CPU-intensive way to do things.  We simply
		 * wrap around before we fall off the end of the buffer.  A 
		 * tilde (~) demarcates the current end of the trace.
		 *
		 * This method should be used if you are concerned about race
		 * conditions as it is less likely to affect the timing of
		 * things.
		 */

		if (dgap_trcbufi + lenbuf >= dgap_trcbuf_size) {
			/* We are wrapping, so wipe out the last tilde. */
			dgap_trcbuf[dgap_trcbufi] = '\0';
			/* put the new string at the beginning of the buffer */
			dgap_trcbufi = 0;
		}

		strcpy(&dgap_trcbuf[dgap_trcbufi], buf);	
		dgap_trcbufi += lenbuf;
		dgap_trcbuf[dgap_trcbufi] = '~';

#  elif defined(TRC_ON_OVERFLOW_SHIFT_BUFFER)
		/*
		 * This is the more CPU-intensive way to do things.  If we
		 * venture into the last 1/8 of the buffer, we shift the 
		 * last 7/8 of the buffer forward, wiping out the first 1/8.
		 * Advantage: No wrap-around, only truncation from the
		 * beginning.
		 *
		 * This method should not be used if you are concerned about
		 * timing changes affecting the behaviour of the driver (ie,
		 * race conditions).
		 */
		strcpy(&dgap_trcbuf[dgap_trcbufi], buf);
		dgap_trcbufi += lenbuf;
		dgap_trcbuf[dgap_trcbufi] = '~';
		dgap_trcbuf[dgap_trcbufi+1] = '\0';

		/* If we're near the end of the trace buffer... */
		if (dgap_trcbufi > (dgap_trcbuf_size/8)*7) {
			/* Wipe out the first eighth to make some more room. */
			strcpy(dgap_trcbuf, &dgap_trcbuf[dgap_trcbuf_size/8]);
			dgap_trcbufi = strlen(dgap_trcbuf)-1;
			/* Plop overflow message at the top of the buffer. */
			bcopy(TRC_OVERFLOW, dgap_trcbuf, strlen(TRC_OVERFLOW));
		}
#  else
#   error "TRC_ON_OVERFLOW_WRAP_AROUND or TRC_ON_OVERFLOW_SHIFT_BUFFER?"
#  endif
	}
	DGAP_UNLOCK(dgap_tracef_lock, flags);

# endif /* defined(TRC_TO_KMEM) */
}

#endif /* !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE) */

#endif

/*
 * dgap_tracer_free()
 *
 *
 */
void dgap_tracer_free(void)
{
	if(dgap_trcbuf)
		vfree(dgap_trcbuf);
}
