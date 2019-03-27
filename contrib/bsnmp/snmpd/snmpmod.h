/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Shteryana Sotirova Shopova
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/snmpd/snmpmod.h,v 1.32 2006/02/14 09:04:20 brandt_h Exp $
 *
 * SNMP daemon data and functions exported to modules.
 */
#ifndef snmpmod_h_
#define snmpmod_h_

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include "asn1.h"
#include "snmp.h"
#include "snmpagent.h"

#define MAX_MOD_ARGS	16

/*
 * These macros help to handle object lists for SNMP tables. They use
 * tail queues to hold the objects in ascending order in the list.
 * ordering can be done either on an integer/unsigned field, an asn_oid
 * or an ordering function.
 */
#define INSERT_OBJECT_OID_LINK_INDEX(PTR, LIST, LINK, INDEX) do {	\
	__typeof (PTR) _lelem;						\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if (asn_compare_oid(&_lelem->INDEX, &(PTR)->INDEX) > 0)	\
			break;						\
	if (_lelem == NULL)						\
		TAILQ_INSERT_TAIL((LIST), (PTR), LINK);			\
	else								\
		TAILQ_INSERT_BEFORE(_lelem, (PTR), LINK);		\
    } while (0)

#define INSERT_OBJECT_INT_LINK_INDEX(PTR, LIST, LINK, INDEX) do {	\
	__typeof (PTR) _lelem;						\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if ((asn_subid_t)_lelem->INDEX > (asn_subid_t)(PTR)->INDEX)\
			break;						\
	if (_lelem == NULL)						\
		TAILQ_INSERT_TAIL((LIST), (PTR), LINK);			\
	else								\
		TAILQ_INSERT_BEFORE(_lelem, (PTR), LINK);		\
    } while (0)

#define	INSERT_OBJECT_FUNC_LINK(PTR, LIST, LINK, FUNC) do {		\
	__typeof (PTR) _lelem;						\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if ((FUNC)(_lelem, (PTR)) > 0)				\
			break;						\
	if (_lelem == NULL)						\
		TAILQ_INSERT_TAIL((LIST), (PTR), LINK);			\
	else								\
		TAILQ_INSERT_BEFORE(_lelem, (PTR), LINK);		\
    } while (0)

#define	INSERT_OBJECT_FUNC_LINK_REV(PTR, LIST, HEAD, LINK, FUNC) do {	\
	__typeof (PTR) _lelem;						\
									\
	TAILQ_FOREACH_REVERSE(_lelem, (LIST), HEAD, LINK)		\
		if ((FUNC)(_lelem, (PTR)) < 0)				\
			break;						\
	if (_lelem == NULL)						\
		TAILQ_INSERT_HEAD((LIST), (PTR), LINK);			\
	else								\
		TAILQ_INSERT_AFTER((LIST), _lelem, (PTR), LINK);	\
    } while (0)

#define FIND_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, LINK, INDEX) ({	\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if (index_compare(OID, SUB, &_lelem->INDEX) == 0)	\
			break;						\
	(_lelem);							\
    })

#define NEXT_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, LINK, INDEX) ({	\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if (index_compare(OID, SUB, &_lelem->INDEX) < 0)	\
			break;						\
	(_lelem);							\
    })

#define FIND_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, LINK, INDEX) ({	\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	if ((OID)->len - SUB != 1)					\
		_lelem = NULL;						\
	else								\
		TAILQ_FOREACH(_lelem, (LIST), LINK)			\
			if ((OID)->subs[SUB] == (asn_subid_t)_lelem->INDEX)\
				break;					\
	(_lelem);							\
    })

#define NEXT_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, LINK, INDEX) ({	\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	if ((OID)->len - SUB == 0)					\
		_lelem = TAILQ_FIRST(LIST);				\
	else								\
		TAILQ_FOREACH(_lelem, (LIST), LINK)			\
			if ((OID)->subs[SUB] < (asn_subid_t)_lelem->INDEX)\
				break;					\
	(_lelem);							\
    })

#define FIND_OBJECT_FUNC_LINK(LIST, OID, SUB, LINK, FUNC) ({		\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if ((FUNC)(OID, SUB, _lelem) == 0)			\
			break;						\
	(_lelem);							\
    })

#define NEXT_OBJECT_FUNC_LINK(LIST, OID, SUB, LINK, FUNC) ({		\
	__typeof (TAILQ_FIRST(LIST)) _lelem;				\
									\
	TAILQ_FOREACH(_lelem, (LIST), LINK)				\
		if ((FUNC)(OID, SUB, _lelem) < 0)			\
			break;						\
	(_lelem);							\
    })

/*
 * Macros for the case where the index field is called 'index'
 */
#define INSERT_OBJECT_OID_LINK(PTR, LIST, LINK)				\
    INSERT_OBJECT_OID_LINK_INDEX(PTR, LIST, LINK, index)

#define INSERT_OBJECT_INT_LINK(PTR, LIST, LINK) do {			\
    INSERT_OBJECT_INT_LINK_INDEX(PTR, LIST, LINK, index)

#define FIND_OBJECT_OID_LINK(LIST, OID, SUB, LINK)			\
    FIND_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, LINK, index)

#define NEXT_OBJECT_OID_LINK(LIST, OID, SUB, LINK)			\
    NEXT_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, LINK, index)

#define FIND_OBJECT_INT_LINK(LIST, OID, SUB, LINK)			\
    FIND_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, LINK, index)

#define NEXT_OBJECT_INT_LINK(LIST, OID, SUB, LINK)			\
    NEXT_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, LINK, index)

/*
 * Macros for the case where the index field is called 'index' and the
 * link field 'link'.
 */
#define INSERT_OBJECT_OID(PTR, LIST)					\
    INSERT_OBJECT_OID_LINK_INDEX(PTR, LIST, link, index)

#define INSERT_OBJECT_INT(PTR, LIST)					\
    INSERT_OBJECT_INT_LINK_INDEX(PTR, LIST, link, index)

#define	INSERT_OBJECT_FUNC_REV(PTR, LIST, HEAD, FUNC)			\
    INSERT_OBJECT_FUNC_LINK_REV(PTR, LIST, HEAD, link, FUNC)

#define FIND_OBJECT_OID(LIST, OID, SUB)					\
    FIND_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, link, index)

#define FIND_OBJECT_INT(LIST, OID, SUB)					\
    FIND_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, link, index)

#define	FIND_OBJECT_FUNC(LIST, OID, SUB, FUNC)				\
    FIND_OBJECT_FUNC_LINK(LIST, OID, SUB, link, FUNC)

#define NEXT_OBJECT_OID(LIST, OID, SUB)					\
    NEXT_OBJECT_OID_LINK_INDEX(LIST, OID, SUB, link, index)

#define NEXT_OBJECT_INT(LIST, OID, SUB)					\
    NEXT_OBJECT_INT_LINK_INDEX(LIST, OID, SUB, link, index)

#define	NEXT_OBJECT_FUNC(LIST, OID, SUB, FUNC)				\
    NEXT_OBJECT_FUNC_LINK(LIST, OID, SUB, link, FUNC)

struct lmodule;

/* The tick when the program was started. This is the absolute time of
 * the start in 100th of a second. */
extern uint64_t start_tick;

/* The tick when the current packet was received. This is the absolute
 * time in 100th of second. */
extern uint64_t this_tick;

/* Get the current absolute time in 100th of a second. */
uint64_t get_ticks(void);

/*
 * Return code for proxy function
 */
enum snmpd_proxy_err {
	/* proxy code will process the PDU */
	SNMPD_PROXY_OK,
	/* proxy code does not process PDU */
	SNMPD_PROXY_REJ,
	/* drop this PDU */
	SNMPD_PROXY_DROP,
	/* drop because of bad community */
	SNMPD_PROXY_BADCOMM,
	/* drop because of bad community use */
	SNMPD_PROXY_BADCOMMUSE
};

/*
 * Input handling
 */
enum snmpd_input_err {
	/* proceed with packet */
	SNMPD_INPUT_OK,
	/* fatal error in packet, ignore it */
	SNMPD_INPUT_FAILED,
	/* value encoding has wrong length in a SET operation */
	SNMPD_INPUT_VALBADLEN,
	/* value encoding is out of range */
	SNMPD_INPUT_VALRANGE,
	/* value has bad encoding */
	SNMPD_INPUT_VALBADENC,
	/* need more data (truncated packet) */
	SNMPD_INPUT_TRUNC,
	/* unknown community */
	SNMPD_INPUT_BAD_COMM,
};

/*
 * Every loadable module must have one of this structures with
 * the external name 'config'.
 */
struct snmp_module {
	/* a comment describing what this module implements */
	const char *comment;

	/* the initialization function */
	int (*init)(struct lmodule *, int argc, char *argv[]);

	/* the finalisation function */
	int (*fini)(void);

	/* the idle function */
	void (*idle)(void);

	/* the dump function */
	void (*dump)(void);

	/* re-configuration function */
	void (*config)(void);

	/* start operation */
	void (*start)(void);

	/* proxy a PDU */
	enum snmpd_proxy_err (*proxy)(struct snmp_pdu *, void *,
	    const struct asn_oid *, const struct sockaddr *, socklen_t,
	    enum snmpd_input_err, int32_t, int);

	/* the tree this module is going to server */
	const struct snmp_node *tree;
	u_int tree_size;

	/* function called, when another module was unloaded/loaded */
	void (*loading)(const struct lmodule *, int);
};

/*
 * Stuff exported to modules
 */

/*
 * The system group.
 */
struct systemg {
	u_char		*descr;
	struct asn_oid	object_id;
	u_char		*contact;
	u_char		*name;
	u_char		*location;
	uint32_t	services;
	uint32_t	or_last_change;
};
extern struct systemg systemg;

/*
 * Community support.
 *
 * We have 2 fixed communities for SNMP read and write access. Modules
 * can create their communities dynamically. They are deleted automatically
 * if the module is unloaded.
 */
#define COMM_INITIALIZE	0
#define COMM_READ	1
#define COMM_WRITE	2

u_int comm_define(u_int, const char *descr, struct lmodule *, const char *str);
struct community *comm_define_ordered(u_int priv, const char *descr,
    struct asn_oid *index, struct lmodule *owner, const char *str);
const char * comm_string(u_int);

/* community for current packet */
extern u_int community;

/*
 * SNMP User-based Security Model data. Modified via the snmp_usm(3) module.
 */
struct snmpd_usmstat {
	uint32_t	unsupported_seclevels;
	uint32_t	not_in_time_windows;
	uint32_t	unknown_users;
	uint32_t	unknown_engine_ids;
	uint32_t	wrong_digests;
	uint32_t	decrypt_errors;
};

extern struct snmpd_usmstat snmpd_usmstats;
struct snmpd_usmstat *bsnmpd_get_usm_stats(void);
void bsnmpd_reset_usm_stats(void);

struct usm_user {
	struct snmp_user		suser;
	uint8_t				user_engine_id[SNMP_ENGINE_ID_SIZ];
	uint32_t			user_engine_len;
	char				user_public[SNMP_ADM_STR32_SIZ];
	uint32_t			user_public_len;
	int32_t				status;
	int32_t				type;
	SLIST_ENTRY(usm_user)		up;
};

SLIST_HEAD(usm_userlist, usm_user);
struct usm_user *usm_first_user(void);
struct usm_user *usm_next_user(struct usm_user *);
struct usm_user *usm_find_user(uint8_t *, uint32_t, char *);
struct usm_user *usm_new_user(uint8_t *, uint32_t, char *);
void usm_delete_user(struct usm_user *);
void usm_flush_users(void);

/* USM user for current packet */
extern struct usm_user *usm_user;

/*
 * SNMP View-based Access Control Model data. Modified via the snmp_vacm(3) module.
 */
struct vacm_group;

struct vacm_user {
	/* Security user name from USM */
	char				secname[SNMP_ADM_STR32_SIZ];
	int32_t				sec_model;
	/* Back pointer to user assigned group name */
	struct vacm_group		*group;
	int32_t				type;
	int32_t				status;
	SLIST_ENTRY(vacm_user)		vvu;
	SLIST_ENTRY(vacm_user)		vvg;
};

SLIST_HEAD(vacm_userlist, vacm_user);

struct vacm_group {
	char				groupname[SNMP_ADM_STR32_SIZ];
	struct vacm_userlist		group_users;
	SLIST_ENTRY(vacm_group)		vge;
};

SLIST_HEAD(vacm_grouplist, vacm_group);

struct vacm_access {
	/* The group name is index, not a column in the table */
	struct vacm_group		*group;
	char				ctx_prefix[SNMP_ADM_STR32_SIZ];
	int32_t				sec_model;
	int32_t				sec_level;
	int32_t				ctx_match;
	struct vacm_view		*read_view;
	struct vacm_view		*write_view;
	struct vacm_view		*notify_view;
	int32_t				type;
	int32_t				status;
	TAILQ_ENTRY(vacm_access)	vva;
};

TAILQ_HEAD(vacm_accesslist, vacm_access);

struct vacm_view {
	char				viewname[SNMP_ADM_STR32_SIZ]; /* key */
	struct asn_oid			subtree; /* key */
	uint8_t				mask[16];
	uint8_t				exclude;
	int32_t				type;
	int32_t				status;
	SLIST_ENTRY(vacm_view)		vvl;
};

SLIST_HEAD(vacm_viewlist, vacm_view);

struct vacm_context {
	/* The ID of the module that registered this context */
	int32_t				regid;
	char				ctxname[SNMP_ADM_STR32_SIZ];
	SLIST_ENTRY(vacm_context)	vcl;
};

SLIST_HEAD(vacm_contextlist, vacm_context);

void vacm_groups_init(void);
struct vacm_user *vacm_first_user(void);
struct vacm_user *vacm_next_user(struct vacm_user *);
struct vacm_user *vacm_new_user(int32_t, char *);
int vacm_delete_user(struct vacm_user *);
int vacm_user_set_group(struct vacm_user *, u_char *, u_int);
struct vacm_access *vacm_first_access_rule(void);
struct vacm_access *vacm_next_access_rule(struct vacm_access *);
struct vacm_access *vacm_new_access_rule(char *, char *, int32_t, int32_t);
int vacm_delete_access_rule(struct vacm_access *);
struct vacm_view *vacm_first_view(void);
struct vacm_view *vacm_next_view(struct vacm_view *);
struct vacm_view *vacm_new_view(char *, struct asn_oid *);
int vacm_delete_view(struct vacm_view *);
struct vacm_context *vacm_first_context(void);
struct vacm_context *vacm_next_context(struct vacm_context *);
struct vacm_context *vacm_add_context(char *, int32_t);
void vacm_flush_contexts(int32_t);

/*
 * RFC 3413 SNMP Management Target & Notification MIB
 */

struct snmpd_target_stats {
	uint32_t			unavail_contexts;
	uint32_t			unknown_contexts;
};

#define	SNMP_UDP_ADDR_SIZ		6
#define	SNMP_TAG_SIZ			(255 + 1)

struct target_address {
	char				name[SNMP_ADM_STR32_SIZ];
	uint8_t				address[SNMP_UDP_ADDR_SIZ];
	int32_t				timeout;
	int32_t				retry;
	char				taglist[SNMP_TAG_SIZ];
	char				paramname[SNMP_ADM_STR32_SIZ];
	int32_t				type;
	int32_t				socket;
	int32_t				status;
	SLIST_ENTRY(target_address)	ta;
};

SLIST_HEAD(target_addresslist, target_address);

struct target_param {
	char				name[SNMP_ADM_STR32_SIZ];
	int32_t				mpmodel;
	int32_t				sec_model;
	char				secname[SNMP_ADM_STR32_SIZ];
	enum snmp_usm_level		sec_level;
	int32_t				type;
	int32_t				status;
	SLIST_ENTRY(target_param)	tp;
};

SLIST_HEAD(target_paramlist, target_param);

struct target_notify {
	char				name[SNMP_ADM_STR32_SIZ];
	char				taglist[SNMP_TAG_SIZ];
	int32_t				notify_type;
	int32_t				type;
	int32_t				status;
	SLIST_ENTRY(target_notify)	tn;
};

SLIST_HEAD(target_notifylist, target_notify);

extern struct snmpd_target_stats snmpd_target_stats;
struct snmpd_target_stats *bsnmpd_get_target_stats(void);
struct target_address *target_first_address(void);
struct target_address *target_next_address(struct target_address *);
struct target_address *target_new_address(char *);
int target_activate_address(struct target_address *);
int target_delete_address(struct target_address *);
struct target_param *target_first_param(void);
struct target_param *target_next_param(struct target_param *);
struct target_param *target_new_param(char *);
int target_delete_param(struct target_param *);
struct target_notify *target_first_notify(void);
struct target_notify *target_next_notify(struct target_notify *);
struct target_notify *target_new_notify(char *);
int target_delete_notify (struct target_notify *);
void target_flush_all(void);

/*
 * Well known OIDs
 */
extern const struct asn_oid oid_zeroDotZero;

/* SNMPv3 Engine Discovery */
extern const struct asn_oid oid_usmUnknownEngineIDs;
extern const struct asn_oid oid_usmNotInTimeWindows;

/*
 * Request ID ranges.
 *
 * A module can request a range of request ids and associate them with a
 * type field. All ranges are deleted if a module is unloaded.
 */
u_int reqid_allocate(int size, struct lmodule *);
int32_t reqid_next(u_int type);
int32_t reqid_base(u_int type);
int reqid_istype(int32_t reqid, u_int type);
u_int reqid_type(int32_t reqid);

/*
 * Timers.
 */
void *timer_start(u_int, void (*)(void *), void *, struct lmodule *);
void *timer_start_repeat(u_int, u_int, void (*)(void *), void *,
    struct lmodule *);
void timer_stop(void *);

/*
 * File descriptors
 */
void *fd_select(int, void (*)(int, void *), void *, struct lmodule *);
void fd_deselect(void *);
void fd_suspend(void *);
int fd_resume(void *);

/*
 * Object resources
 */
u_int or_register(const struct asn_oid *, const char *, struct lmodule *);
void or_unregister(u_int);

/*
 * Buffers
 */
void *buf_alloc(int tx);
size_t buf_size(int tx);

/* decode PDU and find community */
enum snmpd_input_err snmp_input_start(const u_char *, size_t, const char *,
    struct snmp_pdu *, int32_t *, size_t *);

/* process the pdu. returns either _OK or _FAILED */
enum snmpd_input_err snmp_input_finish(struct snmp_pdu *, const u_char *,
    size_t, u_char *, size_t *, const char *, enum snmpd_input_err, int32_t,
    void *);

void snmp_output(struct snmp_pdu *, u_char *, size_t *, const char *);
void snmp_send_port(void *, const struct asn_oid *, struct snmp_pdu *,
	const struct sockaddr *, socklen_t);
enum snmp_code snmp_pdu_auth_access(struct snmp_pdu *, int32_t *);

/* sending traps */
void snmp_send_trap(const struct asn_oid *, ...);

/*
 * Action support
 */
int string_save(struct snmp_value *, struct snmp_context *, ssize_t, u_char **);
void string_commit(struct snmp_context *);
void string_rollback(struct snmp_context *, u_char **);
int string_get(struct snmp_value *, const u_char *, ssize_t);
int string_get_max(struct snmp_value *, const u_char *, ssize_t, size_t);
void string_free(struct snmp_context *);

int ip_save(struct snmp_value *, struct snmp_context *, u_char *);
void ip_rollback(struct snmp_context *, u_char *);
void ip_commit(struct snmp_context *);
int ip_get(struct snmp_value *, u_char *);

int oid_save(struct snmp_value *, struct snmp_context *, struct asn_oid *);
void oid_rollback(struct snmp_context *, struct asn_oid *);
void oid_commit(struct snmp_context *);
int oid_get(struct snmp_value *, const struct asn_oid *);

int index_decode(const struct asn_oid *oid, u_int sub, u_int code, ...);
int index_compare(const struct asn_oid *, u_int, const struct asn_oid *);
int index_compare_off(const struct asn_oid *, u_int, const struct asn_oid *,
    u_int);
void index_append(struct asn_oid *, u_int, const struct asn_oid *);
void index_append_off(struct asn_oid *, u_int, const struct asn_oid *, u_int);

#endif
