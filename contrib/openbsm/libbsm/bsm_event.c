/*-
 * Copyright (c) 2004 Apple Inc.
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <config/config.h>

#include <bsm/libbsm.h>

#include <string.h>
#ifdef HAVE_PTHREAD_MUTEX_LOCK
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif


/*
 * Parse the contents of the audit_event file to return
 * au_event_ent entries
 */
static FILE		*fp = NULL;
static char		 linestr[AU_LINE_MAX];
static const char	*eventdelim = ":";

#ifdef HAVE_PTHREAD_MUTEX_LOCK
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Parse one line from the audit_event file into the au_event_ent structure.
 */
static struct au_event_ent *
eventfromstr(char *str, struct au_event_ent *e)
{
	char *evno, *evname, *evdesc, *evclass;
	struct au_mask evmask;
	char *last;

	evno = strtok_r(str, eventdelim, &last);
	evname = strtok_r(NULL, eventdelim, &last);
	evdesc = strtok_r(NULL, eventdelim, &last);
	evclass = strtok_r(NULL, eventdelim, &last);

	if ((evno == NULL) || (evname == NULL))
		return (NULL);

	if (strlen(evname) >= AU_EVENT_NAME_MAX)
		return (NULL);

	strlcpy(e->ae_name, evname, AU_EVENT_NAME_MAX);
	if (evdesc != NULL) {
		if (strlen(evdesc) >= AU_EVENT_DESC_MAX)
			return (NULL);
		strlcpy(e->ae_desc, evdesc, AU_EVENT_DESC_MAX);
	} else
		strlcpy(e->ae_desc, "", AU_EVENT_DESC_MAX);

	e->ae_number = atoi(evno);

	/*
	 * Find out the mask that corresponds to the given list of classes.
	 */
	if (evclass != NULL) {
		if (getauditflagsbin(evclass, &evmask) != 0)
			e->ae_class = 0;
		else
			e->ae_class = evmask.am_success;
	} else
		e->ae_class = 0;

	return (e);
}

/*
 * Rewind the audit_event file.
 */
static void
setauevent_locked(void)
{

	if (fp != NULL)
		fseek(fp, 0, SEEK_SET);
}

void
setauevent(void)
{

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	setauevent_locked();
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
}

/*
 * Close the open file pointers.
 */
void
endauevent(void)
{

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	if (fp != NULL) {
		fclose(fp);
		fp = NULL;
	}
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
}

/*
 * Enumerate the au_event_ent entries.
 */
static struct au_event_ent *
getauevent_r_locked(struct au_event_ent *e)
{
	char *nl;

	if ((fp == NULL) && ((fp = fopen(AUDIT_EVENT_FILE, "r")) == NULL))
		return (NULL);

	while (1) {
		if (fgets(linestr, AU_LINE_MAX, fp) == NULL)
			return (NULL);

		/* Remove new lines. */
		if ((nl = strrchr(linestr, '\n')) != NULL)
			*nl = '\0';

		/* Skip comments. */
		if (linestr[0] == '#')
			continue;

		/* Get the next event structure. */
		if (eventfromstr(linestr, e) == NULL)
			return (NULL);
		break;
	}

	return (e);
}

struct au_event_ent *
getauevent_r(struct au_event_ent *e)
{
	struct au_event_ent *ep;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	ep = getauevent_r_locked(e);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (ep);
}

struct au_event_ent *
getauevent(void)
{
	static char event_ent_name[AU_EVENT_NAME_MAX];
	static char event_ent_desc[AU_EVENT_DESC_MAX];
	static struct au_event_ent e;

	bzero(&e, sizeof(e));
	bzero(event_ent_name, sizeof(event_ent_name));
	bzero(event_ent_desc, sizeof(event_ent_desc));
	e.ae_name = event_ent_name;
	e.ae_desc = event_ent_desc;
	return (getauevent_r(&e));
}

/*
 * Search for an audit event structure having the given event name.
 *
 * XXXRW: Why accept NULL name?
 */
static struct au_event_ent *
getauevnam_r_locked(struct au_event_ent *e, const char *name)
{
	char *nl;

	if (name == NULL)
		return (NULL);

	/* Rewind to beginning of the file. */
	setauevent_locked();

	if ((fp == NULL) && ((fp = fopen(AUDIT_EVENT_FILE, "r")) == NULL))
		return (NULL);

	while (fgets(linestr, AU_LINE_MAX, fp) != NULL) {
		/* Remove new lines. */
		if ((nl = strrchr(linestr, '\n')) != NULL)
			*nl = '\0';

		if (eventfromstr(linestr, e) != NULL) {
			if (strcmp(name, e->ae_name) == 0)
				return (e);
		}
	}

	return (NULL);
}

struct au_event_ent *
getauevnam_r(struct au_event_ent *e, const char *name)
{
	struct au_event_ent *ep;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	ep = getauevnam_r_locked(e, name);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (ep);
}

struct au_event_ent *
getauevnam(const char *name)
{
	static char event_ent_name[AU_EVENT_NAME_MAX];
	static char event_ent_desc[AU_EVENT_DESC_MAX];
	static struct au_event_ent e;

	bzero(&e, sizeof(e));
	bzero(event_ent_name, sizeof(event_ent_name));
	bzero(event_ent_desc, sizeof(event_ent_desc));
	e.ae_name = event_ent_name;
	e.ae_desc = event_ent_desc;
	return (getauevnam_r(&e, name));
}

/*
 * Search for an audit event structure having the given event number.
 */
static struct au_event_ent *
getauevnum_r_locked(struct au_event_ent *e, au_event_t event_number)
{
	char *nl;

	/* Rewind to beginning of the file. */
	setauevent_locked();

	if ((fp == NULL) && ((fp = fopen(AUDIT_EVENT_FILE, "r")) == NULL))
		return (NULL);

	while (fgets(linestr, AU_LINE_MAX, fp) != NULL) {
		/* Remove new lines. */
		if ((nl = strrchr(linestr, '\n')) != NULL)
			*nl = '\0';

		if (eventfromstr(linestr, e) != NULL) {
			if (event_number == e->ae_number)
				return (e);
		}
	}

	return (NULL);
}

struct au_event_ent *
getauevnum_r(struct au_event_ent *e, au_event_t event_number)
{
	struct au_event_ent *ep;

#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	ep = getauevnum_r_locked(e, event_number);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	return (ep);
}

struct au_event_ent *
getauevnum(au_event_t event_number)
{
	static char event_ent_name[AU_EVENT_NAME_MAX];
	static char event_ent_desc[AU_EVENT_DESC_MAX];
	static struct au_event_ent e;

	bzero(&e, sizeof(e));
	bzero(event_ent_name, sizeof(event_ent_name));
	bzero(event_ent_desc, sizeof(event_ent_desc));
	e.ae_name = event_ent_name;
	e.ae_desc = event_ent_desc;
	return (getauevnum_r(&e, event_number));
}

/*
 * Search for an audit_event entry with a given event_name and returns the
 * corresponding event number.
 */
au_event_t *
getauevnonam_r(au_event_t *ev, const char *event_name)
{
	static char event_ent_name[AU_EVENT_NAME_MAX];
	static char event_ent_desc[AU_EVENT_DESC_MAX];
	static struct au_event_ent e, *ep;

	bzero(event_ent_name, sizeof(event_ent_name));
	bzero(event_ent_desc, sizeof(event_ent_desc));
	bzero(&e, sizeof(e));
	e.ae_name = event_ent_name;
	e.ae_desc = event_ent_desc;

	ep = getauevnam_r(&e, event_name);
	if (ep == NULL)
		return (NULL);

	*ev = e.ae_number;
	return (ev);
}

au_event_t *
getauevnonam(const char *event_name)
{
	static au_event_t event;

	return (getauevnonam_r(&event, event_name));
}
