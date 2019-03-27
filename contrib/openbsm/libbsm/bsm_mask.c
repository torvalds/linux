/*-
 * Copyright (c) 2004 Apple Inc.
 * Copyright (c) 2005 Robert N. M. Watson
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

#include <sys/types.h>

#include <config/config.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else /* !HAVE_FULL_QUEUE_H */
#include <compat/queue.h>
#endif /* !HAVE_FULL_QUEUE_H */

#include <bsm/libbsm.h>

#ifdef HAVE_PTHREAD_MUTEX_LOCK
#include <pthread.h>
#endif
#include <stdlib.h>
#include <string.h>

/* MT-Safe */
#ifdef HAVE_PTHREAD_MUTEX_LOCK
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int		firsttime = 1;

/*
 * XXX ev_cache, once created, sticks around until the calling program exits.
 * This may or may not be a problem as far as absolute memory usage goes, but
 * at least there don't appear to be any leaks in using the cache.
 *
 * XXXRW: Note that despite (mutex), load_event_table() could race with
 * other consumers of the getauevents() API.
 */
struct audit_event_map {
	char				 ev_name[AU_EVENT_NAME_MAX];
	char				 ev_desc[AU_EVENT_DESC_MAX];
	struct au_event_ent		 ev;
	LIST_ENTRY(audit_event_map)	 ev_list;
};
static LIST_HEAD(, audit_event_map)	ev_cache;

static struct audit_event_map *
audit_event_map_alloc(void)
{
	struct audit_event_map *aemp;

	aemp = malloc(sizeof(*aemp));
	if (aemp == NULL)
		return (aemp);
	bzero(aemp, sizeof(*aemp));
	aemp->ev.ae_name = aemp->ev_name;
	aemp->ev.ae_desc = aemp->ev_desc;
	return (aemp);
}

static void
audit_event_map_free(struct audit_event_map *aemp)
{

	free(aemp);
}

/*
 * When reading into the cache fails, we need to flush the entire cache to
 * prevent it from containing some but not all records.
 */
static void
flush_cache(void)
{
	struct audit_event_map *aemp;

	/* XXX: Would assert 'mutex'. */

	while ((aemp = LIST_FIRST(&ev_cache)) != NULL) {
		LIST_REMOVE(aemp, ev_list);
		audit_event_map_free(aemp);
	}
}

static int
load_event_table(void)
{
	struct audit_event_map *aemp;
	struct au_event_ent *ep;

	/*
	 * XXX: Would assert 'mutex'.
	 * Loading of the cache happens only once; dont check if cache is
	 * already loaded.
	 */
	LIST_INIT(&ev_cache);
	setauevent();	/* Rewind to beginning of entries. */
	do {
		aemp = audit_event_map_alloc();
		if (aemp == NULL) {
			flush_cache();
			return (-1);
		}
		ep = getauevent_r(&aemp->ev);
		if (ep != NULL)
			LIST_INSERT_HEAD(&ev_cache, aemp, ev_list);
		else
			audit_event_map_free(aemp);
	} while (ep != NULL);
	return (1);
}

/*
 * Read the event with the matching event number from the cache.
 */
static struct au_event_ent *
read_from_cache(au_event_t event)
{
	struct audit_event_map *elem;

	/* XXX: Would assert 'mutex'. */

	LIST_FOREACH(elem, &ev_cache, ev_list) {
		if (elem->ev.ae_number == event)
			return (&elem->ev);
	}

	return (NULL);
}

/*
 * Check if the audit event is preselected against the preselection mask.
 */
int
au_preselect(au_event_t event, au_mask_t *mask_p, int sorf, int flag)
{
	struct au_event_ent *ev;
	au_class_t effmask = 0;

	if (mask_p == NULL)
		return (-1);


#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_lock(&mutex);
#endif
	if (firsttime) {
		firsttime = 0;
		if ( -1 == load_event_table()) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			return (-1);
		}
	}
	switch (flag) {
	case AU_PRS_REREAD:
		flush_cache();
		if (load_event_table() == -1) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
			pthread_mutex_unlock(&mutex);
#endif
			return (-1);
		}
		ev = read_from_cache(event);
		break;
	case AU_PRS_USECACHE:
		ev = read_from_cache(event);
		break;
	default:
		ev = NULL;
	}
	if (ev == NULL) {
#ifdef HAVE_PTHREAD_MUTEX_LOCK
		pthread_mutex_unlock(&mutex);
#endif
		return (-1);
	}
	if (sorf & AU_PRS_SUCCESS)
		effmask |= (mask_p->am_success & ev->ae_class);
	if (sorf & AU_PRS_FAILURE)
		effmask |= (mask_p->am_failure & ev->ae_class);
#ifdef HAVE_PTHREAD_MUTEX_LOCK
	pthread_mutex_unlock(&mutex);
#endif
	if (effmask != 0)
		return (1);
	return (0);
}
