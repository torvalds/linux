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

/* $Id: dgnc_trace.c,v 1.1.1.1 2009/05/20 12:19:19 markh Exp $ */

#include <linux/kernel.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/vmalloc.h>

#include "dgnc_driver.h"

#define TRC_TO_CONSOLE 1

/* file level globals */
static char *dgnc_trcbuf;		/* the ringbuffer */

#if defined(TRC_TO_KMEM)
static int dgnc_trcbufi = 0;		/* index of the tilde at the end of */
#endif

#if defined(TRC_TO_KMEM)
static DEFINE_SPINLOCK(dgnc_tracef_lock);
#endif


#if 0

#if !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE)

void dgnc_tracef(const char *fmt, ...)
{
	return;
}

#else /* !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE) */

void dgnc_tracef(const char *fmt, ...)
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
	DGNC_LOCK(dgnc_tracef_lock, flags);
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
			dgnc_trcbuf = (char *) vmalloc(dgnc_trcbuf_size);
			if(!dgnc_trcbuf) {
				failed = TRUE;
				printk("dgnc: tracing init failed!\n");
				return;
			}

			memset(dgnc_trcbuf, '\0',  dgnc_trcbuf_size);
			dgnc_trcbufi = 0;
			initd++;

			printk("dgnc: tracing enabled - " TRC_DTRC
				" 0x%lx 0x%x\n",
				(unsigned long)dgnc_trcbuf,
				dgnc_trcbuf_size);
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

		if (dgnc_trcbufi + lenbuf >= dgnc_trcbuf_size) {
			/* We are wrapping, so wipe out the last tilde. */
			dgnc_trcbuf[dgnc_trcbufi] = '\0';
			/* put the new string at the beginning of the buffer */
			dgnc_trcbufi = 0;
		}

		strcpy(&dgnc_trcbuf[dgnc_trcbufi], buf);
		dgnc_trcbufi += lenbuf;
		dgnc_trcbuf[dgnc_trcbufi] = '~';

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
		strcpy(&dgnc_trcbuf[dgnc_trcbufi], buf);
		dgnc_trcbufi += lenbuf;
		dgnc_trcbuf[dgnc_trcbufi] = '~';
		dgnc_trcbuf[dgnc_trcbufi+1] = '\0';

		/* If we're near the end of the trace buffer... */
		if (dgnc_trcbufi > (dgnc_trcbuf_size/8)*7) {
			/* Wipe out the first eighth to make some more room. */
			strcpy(dgnc_trcbuf, &dgnc_trcbuf[dgnc_trcbuf_size/8]);
			dgnc_trcbufi = strlen(dgnc_trcbuf)-1;
			/* Plop overflow message at the top of the buffer. */
			bcopy(TRC_OVERFLOW, dgnc_trcbuf, strlen(TRC_OVERFLOW));
		}
#  else
#   error "TRC_ON_OVERFLOW_WRAP_AROUND or TRC_ON_OVERFLOW_SHIFT_BUFFER?"
#  endif
	}
	DGNC_UNLOCK(dgnc_tracef_lock, flags);

# endif /* defined(TRC_TO_KMEM) */
}

#endif /* !defined(TRC_TO_KMEM) && !defined(TRC_TO_CONSOLE) */

#endif


/*
 * dgnc_tracer_free()
 *
 *
 */
void dgnc_tracer_free(void)
{
	if(dgnc_trcbuf)
		vfree(dgnc_trcbuf);
}
