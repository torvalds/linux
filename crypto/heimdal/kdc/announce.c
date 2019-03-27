/*
 * Copyright (c) 2008 Apple Inc.  All Rights Reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of Apple Inc. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Apple Inc. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include "kdc_locl.h"

#if defined(__APPLE__) && defined(HAVE_GCD)

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>
#include <SystemConfiguration/SCDynamicStoreCopySpecific.h>
#include <SystemConfiguration/SCDynamicStoreKey.h>

#include <dispatch/dispatch.h>

#include <asl.h>
#include <resolv.h>

#include <dns_sd.h>
#include <err.h>

static krb5_kdc_configuration *announce_config;
static krb5_context announce_context;

struct entry {
    DNSRecordRef recordRef;
    char *domain;
    char *realm;
#define F_EXISTS 1
#define F_PUSH 2
    int flags;
    struct entry *next;
};

/* #define REGISTER_SRV_RR */

static struct entry *g_entries = NULL;
static CFStringRef g_hostname = NULL;
static DNSServiceRef g_dnsRef = NULL;
static SCDynamicStoreRef g_store = NULL;
static dispatch_queue_t g_queue = NULL;

#define LOG(...) asl_log(NULL, NULL, ASL_LEVEL_INFO, __VA_ARGS__)

static void create_dns_sd(void);
static void destroy_dns_sd(void);
static void update_all(SCDynamicStoreRef, CFArrayRef, void *);


/* parameters */
static CFStringRef NetworkChangedKey_BackToMyMac = CFSTR("Setup:/Network/BackToMyMac");


static char *
CFString2utf8(CFStringRef string)
{
    size_t size;
    char *str;

    size = 1 + CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
    str = malloc(size);
    if (str == NULL)
	return NULL;

    if (CFStringGetCString(string, str, size, kCFStringEncodingUTF8) == false) {
	free(str);
	return NULL;
    }
    return str;
}

/*
 *
 */

static void
retry_timer(void)
{
    dispatch_source_t s;
    dispatch_time_t t;

    s = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
			       0, 0, g_queue);
    t = dispatch_time(DISPATCH_TIME_NOW, 5ull * NSEC_PER_SEC);
    dispatch_source_set_timer(s, t, 0, NSEC_PER_SEC);
    dispatch_source_set_event_handler(s, ^{
	    create_dns_sd();
	    dispatch_release(s);
	});
    dispatch_resume(s);
}

/*
 *
 */

static void
create_dns_sd(void)
{
    DNSServiceErrorType error;
    dispatch_source_t s;

    error = DNSServiceCreateConnection(&g_dnsRef);
    if (error) {
	retry_timer();
	return;
    }

    dispatch_suspend(g_queue);

    s = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,
			       DNSServiceRefSockFD(g_dnsRef),
			       0, g_queue);

    dispatch_source_set_event_handler(s, ^{
	    DNSServiceErrorType ret = DNSServiceProcessResult(g_dnsRef);
	    /* on error tear down and set timer to recreate */
	    if (ret != kDNSServiceErr_NoError && ret != kDNSServiceErr_Transient) {
		dispatch_source_cancel(s);
	    }
	});

    dispatch_source_set_cancel_handler(s, ^{
	    destroy_dns_sd();
	    retry_timer();
	    dispatch_release(s);
	});

    dispatch_resume(s);

    /* Do the first update ourself */
    update_all(g_store, NULL, NULL);
    dispatch_resume(g_queue);
}

static void
domain_add(const char *domain, const char *realm, int flag)
{
    struct entry *e;

    for (e = g_entries; e != NULL; e = e->next) {
	if (strcmp(domain, e->domain) == 0 && strcmp(realm, e->realm) == 0) {
	    e->flags |= flag;
	    return;
	}
    }

    LOG("Adding realm %s to domain %s", realm, domain);

    e = calloc(1, sizeof(*e));
    if (e == NULL)
	return;
    e->domain = strdup(domain);
    e->realm = strdup(realm);
    if (e->domain == NULL || e->realm == NULL) {
	free(e->domain);
	free(e->realm);
	free(e);
	return;
    }
    e->flags = flag | F_PUSH; /* if we allocate, we push */
    e->next = g_entries;
    g_entries = e;
}

struct addctx {
    int flags;
    const char *realm;
};

static void
domains_add(const void *key, const void *value, void *context)
{
    char *str = CFString2utf8((CFStringRef)value);
    struct addctx *ctx = context;

    if (str == NULL)
	return;
    if (str[0] != '\0')
	domain_add(str, ctx->realm, F_EXISTS | ctx->flags);
    free(str);
}


static void
dnsCallback(DNSServiceRef sdRef __attribute__((unused)),
	    DNSRecordRef RecordRef __attribute__((unused)),
	    DNSServiceFlags flags __attribute__((unused)),
	    DNSServiceErrorType errorCode __attribute__((unused)),
	    void *context __attribute__((unused)))
{
}

#ifdef REGISTER_SRV_RR

/*
 * Register DNS SRV rr for the realm.
 */

static const char *register_names[2] = {
    "_kerberos._tcp",
    "_kerberos._udp"
};

static struct {
    DNSRecordRef *val;
    size_t len;
} srvRefs = { NULL, 0 };

static void
register_srv(const char *realm, const char *hostname, int port)
{
    unsigned char target[1024];
    int i;
    int size;

    /* skip registering LKDC realms */
    if (strncmp(realm, "LKDC:", 5) == 0)
	return;

    /* encode SRV-RR */
    target[0] = 0; /* priority */
    target[1] = 0; /* priority */
    target[2] = 0; /* weight */
    target[3] = 0; /* weigth */
    target[4] = (port >> 8) & 0xff; /* port */
    target[5] = (port >> 0) & 0xff; /* port */

    size = dn_comp(hostname, target + 6, sizeof(target) - 6, NULL, NULL);
    if (size < 0)
	return;

    size += 6;

    LOG("register SRV rr for realm %s hostname %s:%d", realm, hostname, port);

    for (i = 0; i < sizeof(register_names)/sizeof(register_names[0]); i++) {
	char name[kDNSServiceMaxDomainName];
	DNSServiceErrorType error;
	void *ptr;

	ptr = realloc(srvRefs.val, sizeof(srvRefs.val[0]) * (srvRefs.len + 1));
	if (ptr == NULL)
	    errx(1, "malloc: out of memory");
	srvRefs.val = ptr;

	DNSServiceConstructFullName(name, NULL, register_names[i], realm);

	error = DNSServiceRegisterRecord(g_dnsRef,
					 &srvRefs.val[srvRefs.len],
					 kDNSServiceFlagsUnique | kDNSServiceFlagsShareConnection,
					 0,
					 name,
					 kDNSServiceType_SRV,
					 kDNSServiceClass_IN,
					 size,
					 target,
					 0,
					 dnsCallback,
					 NULL);
	if (error) {
	    LOG("Failed to register SRV rr for realm %s: %d", realm, error);
	} else
	    srvRefs.len++;
    }
}

static void
unregister_srv_realms(void)
{
    if (g_dnsRef) {
	for (i = 0; i < srvRefs.len; i++)
	    DNSServiceRemoveRecord(g_dnsRef, srvRefs.val[i], 0);
    }
    free(srvRefs.val);
    srvRefs.len = 0;
    srvRefs.val = NULL;
}

static void
register_srv_realms(CFStringRef host)
{
    krb5_error_code ret;
    char *hostname;
    size_t i;

    /* first unregister old names */

    hostname = CFString2utf8(host);
    if (hostname == NULL)
	return;

    for(i = 0; i < announce_config->num_db; i++) {
	char **realms, **r;

	if (announce_config->db[i]->hdb_get_realms == NULL)
	    continue;

	ret = (announce_config->db[i]->hdb_get_realms)(announce_context, &realms);
	if (ret == 0) {
	    for (r = realms; r && *r; r++)
		register_srv(*r, hostname, 88);
	    krb5_free_host_realm(announce_context, realms);
	}
    }

    free(hostname);
}
#endif /* REGISTER_SRV_RR */

static void
update_dns(void)
{
    DNSServiceErrorType error;
    struct entry **e = &g_entries;
    char *hostname;

    hostname = CFString2utf8(g_hostname);
    if (hostname == NULL)
	return;

    while (*e != NULL) {
	/* remove if this wasn't updated */
	if (((*e)->flags & F_EXISTS) == 0) {
	    struct entry *drop = *e;
	    *e = (*e)->next;

	    LOG("Deleting realm %s from domain %s",
		drop->realm, drop->domain);

	    if (drop->recordRef && g_dnsRef)
		DNSServiceRemoveRecord(g_dnsRef, drop->recordRef, 0);
	    free(drop->domain);
	    free(drop->realm);
	    free(drop);
	    continue;
	}
	if ((*e)->flags & F_PUSH) {
	    struct entry *update = *e;
	    char *dnsdata, *name;
	    size_t len;

	    len = strlen(update->realm);
	    asprintf(&dnsdata, "%c%s", (int)len, update->realm);
	    if (dnsdata == NULL)
		errx(1, "malloc");

	    asprintf(&name, "_kerberos.%s.%s", hostname, update->domain);
	    if (name == NULL)
		errx(1, "malloc");

	    if (update->recordRef)
		DNSServiceRemoveRecord(g_dnsRef, update->recordRef, 0);

	    error = DNSServiceRegisterRecord(g_dnsRef,
					     &update->recordRef,
					     kDNSServiceFlagsShared | kDNSServiceFlagsAllowRemoteQuery,
					     0,
					     name,
					     kDNSServiceType_TXT,
					     kDNSServiceClass_IN,
					     len+1,
					     dnsdata,
					     0,
					     dnsCallback,
					     NULL);
	    free(name);
	    free(dnsdata);
	    if (error)
		errx(1, "failure to update entry for %s/%s",
		     update->domain, update->realm);
	}
	e = &(*e)->next;
    }
    free(hostname);
}

static void
update_entries(SCDynamicStoreRef store, const char *realm, int flags)
{
    CFDictionaryRef btmm;

    /* we always announce in the local domain */
    domain_add("local", realm, F_EXISTS | flags);

    /* announce btmm */
    btmm = SCDynamicStoreCopyValue(store, NetworkChangedKey_BackToMyMac);
    if (btmm) {
	struct addctx addctx;

	addctx.flags = flags;
	addctx.realm = realm;

	CFDictionaryApplyFunction(btmm, domains_add, &addctx);
	CFRelease(btmm);
    }
}

static void
update_all(SCDynamicStoreRef store, CFArrayRef changedKeys, void *info)
{
    struct entry *e;
    CFStringRef host;
    int i, flags = 0;

    LOG("something changed, running update");

    host = SCDynamicStoreCopyLocalHostName(store);
    if (host == NULL)
	return;

    if (g_hostname == NULL || CFStringCompare(host, g_hostname, 0) != kCFCompareEqualTo) {
	if (g_hostname)
	    CFRelease(g_hostname);
	g_hostname = CFRetain(host);
	flags = F_PUSH; /* if hostname has changed, force push */

#ifdef REGISTER_SRV_RR
	register_srv_realms(g_hostname);
#endif
    }

    for (e = g_entries; e != NULL; e = e->next)
	e->flags &= ~(F_EXISTS|F_PUSH);

    for(i = 0; i < announce_config->num_db; i++) {
	krb5_error_code ret;
	char **realms, **r;

	if (announce_config->db[i]->hdb_get_realms == NULL)
	    continue;

	ret = (announce_config->db[i]->hdb_get_realms)(announce_context, announce_config->db[i], &realms);
	if (ret == 0) {
	    for (r = realms; r && *r; r++)
		update_entries(store, *r, flags);
	    krb5_free_host_realm(announce_context, realms);
	}
    }

    update_dns();

    CFRelease(host);
}

static void
delete_all(void)
{
    struct entry *e;

    for (e = g_entries; e != NULL; e = e->next)
	e->flags &= ~(F_EXISTS|F_PUSH);

    update_dns();
    if (g_entries != NULL)
	errx(1, "Failed to remove all bonjour entries");
}

static void
destroy_dns_sd(void)
{
    if (g_dnsRef == NULL)
	return;

    delete_all();
#ifdef REGISTER_SRV_RR
    unregister_srv_realms();
#endif

    DNSServiceRefDeallocate(g_dnsRef);
    g_dnsRef = NULL;
}


static SCDynamicStoreRef
register_notification(void)
{
    SCDynamicStoreRef store;
    CFStringRef computerNameKey;
    CFMutableArrayRef keys;

    computerNameKey = SCDynamicStoreKeyCreateHostNames(kCFAllocatorDefault);

    store = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("Network watcher"),
				 update_all, NULL);
    if (store == NULL)
	errx(1, "SCDynamicStoreCreate");

    keys = CFArrayCreateMutable(kCFAllocatorDefault, 2, &kCFTypeArrayCallBacks);
    if (keys == NULL)
	errx(1, "CFArrayCreateMutable");

    CFArrayAppendValue(keys, computerNameKey);
    CFArrayAppendValue(keys, NetworkChangedKey_BackToMyMac);

    if (SCDynamicStoreSetNotificationKeys(store, keys, NULL) == false)
	errx(1, "SCDynamicStoreSetNotificationKeys");

    CFRelease(computerNameKey);
    CFRelease(keys);

    if (!SCDynamicStoreSetDispatchQueue(store, g_queue))
	errx(1, "SCDynamicStoreSetDispatchQueue");

    return store;
}
#endif

void
bonjour_announce(krb5_context context, krb5_kdc_configuration *config)
{
#if defined(__APPLE__) && defined(HAVE_GCD)
    g_queue = dispatch_queue_create("com.apple.kdc_announce", NULL);
    if (!g_queue)
	errx(1, "dispatch_queue_create");

    g_store = register_notification();
    announce_config = config;
    announce_context = context;

    create_dns_sd();
#endif
}
