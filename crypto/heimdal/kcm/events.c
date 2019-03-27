/*
 * Copyright (c) 2005, PADL Software Pty Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "kcm_locl.h"

RCSID("$Id$");

/* thread-safe in case we multi-thread later */
static HEIMDAL_MUTEX events_mutex = HEIMDAL_MUTEX_INITIALIZER;
static kcm_event *events_head = NULL;
static time_t last_run = 0;

static char *action_strings[] = {
	"NONE", "ACQUIRE_CREDS", "RENEW_CREDS",
	"DESTROY_CREDS", "DESTROY_EMPTY_CACHE" };

krb5_error_code
kcm_enqueue_event(krb5_context context,
		  kcm_event *event)
{
    krb5_error_code ret;

    if (event->action == KCM_EVENT_NONE) {
	return 0;
    }

    HEIMDAL_MUTEX_lock(&events_mutex);
    ret = kcm_enqueue_event_internal(context, event);
    HEIMDAL_MUTEX_unlock(&events_mutex);

    return ret;
}

static void
print_times(time_t time, char buf[64])
{
    if (time)
	strftime(buf, 64, "%m-%dT%H:%M", gmtime(&time));
    else
	strlcpy(buf, "never", 64);
}

static void
log_event(kcm_event *event, char *msg)
{
    char fire_time[64], expire_time[64];

    print_times(event->fire_time, fire_time);
    print_times(event->expire_time, expire_time);

    kcm_log(7, "%s event %08x: fire_time %s fire_count %d expire_time %s "
	    "backoff_time %d action %s cache %s",
	    msg, event, fire_time, event->fire_count, expire_time,
	    event->backoff_time, action_strings[event->action],
	    event->ccache->name);
}

krb5_error_code
kcm_enqueue_event_internal(krb5_context context,
			   kcm_event *event)
{
    kcm_event **e;

    if (event->action == KCM_EVENT_NONE)
	return 0;

    for (e = &events_head; *e != NULL; e = &(*e)->next)
	;

    *e = (kcm_event *)malloc(sizeof(kcm_event));
    if (*e == NULL) {
	return KRB5_CC_NOMEM;
    }

    (*e)->valid = 1;
    (*e)->fire_time = event->fire_time;
    (*e)->fire_count = 0;
    (*e)->expire_time = event->expire_time;
    (*e)->backoff_time = event->backoff_time;

    (*e)->action = event->action;

    kcm_retain_ccache(context, event->ccache);
    (*e)->ccache = event->ccache;
    (*e)->next = NULL;

    log_event(*e, "enqueuing");

    return 0;
}

/*
 * Dump events list on SIGUSR2
 */
krb5_error_code
kcm_debug_events(krb5_context context)
{
    kcm_event *e;

    for (e = events_head; e != NULL; e = e->next)
	log_event(e, "debug");

    return 0;
}

krb5_error_code
kcm_enqueue_event_relative(krb5_context context,
			   kcm_event *event)
{
    krb5_error_code ret;
    kcm_event e;

    e = *event;
    e.backoff_time = e.fire_time;
    e.fire_time += time(NULL);

    ret = kcm_enqueue_event(context, &e);

    return ret;
}

static krb5_error_code
kcm_remove_event_internal(krb5_context context,
			  kcm_event **e)
{
    kcm_event *next;

    next = (*e)->next;

    (*e)->valid = 0;
    (*e)->fire_time = 0;
    (*e)->fire_count = 0;
    (*e)->expire_time = 0;
    (*e)->backoff_time = 0;
    kcm_release_ccache(context, (*e)->ccache);
    (*e)->next = NULL;
    free(*e);

    *e = next;

    return 0;
}

static int
is_primary_credential_p(krb5_context context,
			kcm_ccache ccache,
			krb5_creds *newcred)
{
    krb5_flags whichfields;

    if (ccache->client == NULL)
	return 0;

    if (newcred->client == NULL ||
	!krb5_principal_compare(context, ccache->client, newcred->client))
	return 0;

    /* XXX just checks whether it's the first credential in the cache */
    if (ccache->creds == NULL)
	return 0;

    whichfields = KRB5_TC_MATCH_KEYTYPE | KRB5_TC_MATCH_FLAGS_EXACT |
		  KRB5_TC_MATCH_TIMES_EXACT | KRB5_TC_MATCH_AUTHDATA |
		  KRB5_TC_MATCH_2ND_TKT | KRB5_TC_MATCH_IS_SKEY;

    return krb5_compare_creds(context, whichfields, newcred, &ccache->creds->cred);
}

/*
 * Setup default events for a new credential
 */
static krb5_error_code
kcm_ccache_make_default_event(krb5_context context,
			      kcm_event *event,
			      krb5_creds *newcred)
{
    krb5_error_code ret = 0;
    kcm_ccache ccache = event->ccache;

    event->fire_time = 0;
    event->expire_time = 0;
    event->backoff_time = KCM_EVENT_DEFAULT_BACKOFF_TIME;

    if (newcred == NULL) {
	/* no creds, must be acquire creds request */
	if ((ccache->flags & KCM_MASK_KEY_PRESENT) == 0) {
	    kcm_log(0, "Cannot acquire credentials without a key");
	    return KRB5_FCC_INTERNAL;
	}

	event->fire_time = time(NULL); /* right away */
	event->action = KCM_EVENT_ACQUIRE_CREDS;
    } else if (is_primary_credential_p(context, ccache, newcred)) {
	if (newcred->flags.b.renewable) {
	    event->action = KCM_EVENT_RENEW_CREDS;
	    ccache->flags |= KCM_FLAGS_RENEWABLE;
	} else {
	    if (ccache->flags & KCM_MASK_KEY_PRESENT)
		event->action = KCM_EVENT_ACQUIRE_CREDS;
	    else
		event->action = KCM_EVENT_NONE;
	    ccache->flags &= ~(KCM_FLAGS_RENEWABLE);
	}
	/* requeue with some slop factor */
	event->fire_time = newcred->times.endtime - KCM_EVENT_QUEUE_INTERVAL;
    } else {
	event->action = KCM_EVENT_NONE;
    }

    return ret;
}

krb5_error_code
kcm_ccache_enqueue_default(krb5_context context,
			   kcm_ccache ccache,
			   krb5_creds *newcred)
{
    kcm_event event;
    krb5_error_code ret;

    memset(&event, 0, sizeof(event));
    event.ccache = ccache;

    ret = kcm_ccache_make_default_event(context, &event, newcred);
    if (ret)
	return ret;

    ret = kcm_enqueue_event_internal(context, &event);
    if (ret)
	return ret;

    return 0;
}

krb5_error_code
kcm_remove_event(krb5_context context,
		 kcm_event *event)
{
    krb5_error_code ret;
    kcm_event **e;
    int found = 0;

    log_event(event, "removing");

    HEIMDAL_MUTEX_lock(&events_mutex);
    for (e = &events_head; *e != NULL; e = &(*e)->next) {
	if (event == *e) {
	    *e = event->next;
	    found++;
	    break;
	}
    }

    if (!found) {
	ret = KRB5_CC_NOTFOUND;
	goto out;
    }

    ret = kcm_remove_event_internal(context, &event);

out:
    HEIMDAL_MUTEX_unlock(&events_mutex);

    return ret;
}

krb5_error_code
kcm_cleanup_events(krb5_context context,
		   kcm_ccache ccache)
{
    kcm_event **e;

    KCM_ASSERT_VALID(ccache);

    HEIMDAL_MUTEX_lock(&events_mutex);

    for (e = &events_head; *e != NULL; e = &(*e)->next) {
	if ((*e)->valid && (*e)->ccache == ccache) {
	    kcm_remove_event_internal(context, e);
	}
	if (*e == NULL)
	    break;
    }

    HEIMDAL_MUTEX_unlock(&events_mutex);

    return 0;
}

static krb5_error_code
kcm_fire_event(krb5_context context,
	       kcm_event **e)
{
    kcm_event *event;
    krb5_error_code ret;
    krb5_creds *credp = NULL;
    int oneshot = 1;

    event = *e;

    switch (event->action) {
    case KCM_EVENT_ACQUIRE_CREDS:
	ret = kcm_ccache_acquire(context, event->ccache, &credp);
	oneshot = 0;
	break;
    case KCM_EVENT_RENEW_CREDS:
	ret = kcm_ccache_refresh(context, event->ccache, &credp);
	if (ret == KRB5KRB_AP_ERR_TKT_EXPIRED) {
	    ret = kcm_ccache_acquire(context, event->ccache, &credp);
	}
	oneshot = 0;
	break;
    case KCM_EVENT_DESTROY_CREDS:
	ret = kcm_ccache_destroy(context, event->ccache->name);
	break;
    case KCM_EVENT_DESTROY_EMPTY_CACHE:
	ret = kcm_ccache_destroy_if_empty(context, event->ccache);
	break;
    default:
	ret = KRB5_FCC_INTERNAL;
	break;
    }

    event->fire_count++;

    if (ret) {
	/* Reschedule failed event for another time */
	event->fire_time += event->backoff_time;
	if (event->backoff_time < KCM_EVENT_MAX_BACKOFF_TIME)
	    event->backoff_time *= 2;

	/* Remove it if it would never get executed */
	if (event->expire_time &&
	    event->fire_time > event->expire_time)
	    kcm_remove_event_internal(context, e);
    } else {
	if (!oneshot) {
	    char *cpn;

	    if (krb5_unparse_name(context, event->ccache->client,
				  &cpn))
		cpn = NULL;

	    kcm_log(0, "%s credentials in cache %s for principal %s",
		    (event->action == KCM_EVENT_ACQUIRE_CREDS) ?
			"Acquired" : "Renewed",
		    event->ccache->name,
		    (cpn != NULL) ? cpn : "<none>");

	    if (cpn != NULL)
		free(cpn);

	    /* Succeeded, but possibly replaced with another event */
	    ret = kcm_ccache_make_default_event(context, event, credp);
	    if (ret || event->action == KCM_EVENT_NONE)
		oneshot = 1;
	    else
		log_event(event, "requeuing");
	}
	if (oneshot)
	    kcm_remove_event_internal(context, e);
    }

    return ret;
}

krb5_error_code
kcm_run_events(krb5_context context, time_t now)
{
    krb5_error_code ret;
    kcm_event **e;

    HEIMDAL_MUTEX_lock(&events_mutex);

    /* Only run event queue every N seconds */
    if (now < last_run + KCM_EVENT_QUEUE_INTERVAL) {
	HEIMDAL_MUTEX_unlock(&events_mutex);
	return 0;
    }

    /* go through events list, fire and expire */
    for (e = &events_head; *e != NULL; e = &(*e)->next) {
	if ((*e)->valid == 0)
	    continue;

	if (now >= (*e)->fire_time) {
	    ret = kcm_fire_event(context, e);
	    if (ret) {
		kcm_log(1, "Could not fire event for cache %s: %s",
			(*e)->ccache->name, krb5_get_err_text(context, ret));
	    }
	} else if ((*e)->expire_time && now >= (*e)->expire_time) {
	    ret = kcm_remove_event_internal(context, e);
	    if (ret) {
		kcm_log(1, "Could not expire event for cache %s: %s",
			(*e)->ccache->name, krb5_get_err_text(context, ret));
	    }
	}

	if (*e == NULL)
	    break;
    }

    last_run = now;

    HEIMDAL_MUTEX_unlock(&events_mutex);

    return 0;
}

