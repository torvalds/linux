/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Bernhard Reutner-Fischer adjusted for busybox
 */
//config:config TC
//config:	bool "tc (3.1 kb)"
//config:	default y
//config:	help
//config:	Show / manipulate traffic control settings
//config:
//config:config FEATURE_TC_INGRESS
//config:	bool "Enable ingress"
//config:	default y
//config:	depends on TC

//applet:IF_TC(APPLET(tc, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TC) += tc.o

//usage:#define tc_trivial_usage
/* //usage: "[OPTIONS] OBJECT CMD [dev STRING]" */
//usage:	"OBJECT CMD [dev STRING]"
//usage:#define tc_full_usage "\n\n"
//usage:	"OBJECT: qdisc|class|filter\n"
//usage:	"CMD: add|del|change|replace|show\n"
//usage:	"\n"
//usage:	"qdisc [handle QHANDLE] [root|"IF_FEATURE_TC_INGRESS("ingress|")"parent CLASSID]\n"
/* //usage: "[estimator INTERVAL TIME_CONSTANT]\n" */
//usage:	"	[[QDISC_KIND] [help|OPTIONS]]\n"
//usage:	"	QDISC_KIND := [p|b]fifo|tbf|prio|cbq|red|etc.\n"
//usage:	"qdisc show [dev STRING]"IF_FEATURE_TC_INGRESS(" [ingress]")"\n"
//usage:	"class [classid CLASSID] [root|parent CLASSID]\n"
//usage:	"	[[QDISC_KIND] [help|OPTIONS] ]\n"
//usage:	"class show [ dev STRING ] [root|parent CLASSID]\n"
//usage:	"filter [pref PRIO] [protocol PROTO]\n"
/* //usage: "\t[estimator INTERVAL TIME_CONSTANT]\n" */
//usage:	"	[root|classid CLASSID] [handle FILTERID]\n"
//usage:	"	[[FILTER_TYPE] [help|OPTIONS]]\n"
//usage:	"filter show [dev STRING] [root|parent CLASSID]"

#include "libbb.h"
#include "common_bufsiz.h"

#include "libiproute/utils.h"
#include "libiproute/ip_common.h"
#include "libiproute/rt_names.h"
#include <linux/pkt_sched.h> /* for the TC_H_* macros */

/* This is the deprecated multiqueue interface */
#ifndef TCA_PRIO_MAX
enum
{
	TCA_PRIO_UNSPEC,
	TCA_PRIO_MQ,
	__TCA_PRIO_MAX
};
#define TCA_PRIO_MAX    (__TCA_PRIO_MAX - 1)
#endif

#define parse_rtattr_nested(tb, max, rta) \
	(parse_rtattr((tb), (max), RTA_DATA(rta), RTA_PAYLOAD(rta)))

/* nullifies tb on error */
#define __parse_rtattr_nested_compat(tb, max, rta, len) \
({ \
	if ((RTA_PAYLOAD(rta) >= len) \
	 && (RTA_PAYLOAD(rta) >= RTA_ALIGN(len) + sizeof(struct rtattr)) \
	) { \
		rta = RTA_DATA(rta) + RTA_ALIGN(len); \
		parse_rtattr_nested(tb, max, rta); \
	} else \
		memset(tb, 0, sizeof(struct rtattr *) * (max + 1)); \
})

#define parse_rtattr_nested_compat(tb, max, rta, data, len) \
({ \
	data = RTA_PAYLOAD(rta) >= len ? RTA_DATA(rta) : NULL; \
	__parse_rtattr_nested_compat(tb, max, rta, len); \
})

#define show_details (0) /* not implemented. Does anyone need it? */
#define use_iec (0) /* not currently documented in the upstream manpage */


struct globals {
	int filter_ifindex;
	uint32_t filter_qdisc;
	uint32_t filter_parent;
	uint32_t filter_prio;
	uint32_t filter_proto;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define filter_ifindex (G.filter_ifindex)
#define filter_qdisc (G.filter_qdisc)
#define filter_parent (G.filter_parent)
#define filter_prio (G.filter_prio)
#define filter_proto (G.filter_proto)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)

/* Allocates a buffer containing the name of a class id.
 * The caller must free the returned memory.  */
static char* print_tc_classid(uint32_t cid)
{
#if 0 /* IMPOSSIBLE */
	if (cid == TC_H_ROOT)
		return xasprintf("root");
	else
#endif
	if (cid == TC_H_UNSPEC)
		return xasprintf("none");
	else if (TC_H_MAJ(cid) == 0)
		return xasprintf(":%x", TC_H_MIN(cid));
	else if (TC_H_MIN(cid) == 0)
		return xasprintf("%x:", TC_H_MAJ(cid)>>16);
	else
		return xasprintf("%x:%x", TC_H_MAJ(cid)>>16, TC_H_MIN(cid));
}

/* Get a qdisc handle.  Return 0 on success, !0 otherwise.  */
static int get_qdisc_handle(uint32_t *h, const char *str) {
	uint32_t maj;
	char *p;

	maj = TC_H_UNSPEC;
	if (strcmp(str, "none") == 0)
		goto ok;
	maj = strtoul(str, &p, 16);
	if (p == str)
		return 1;
	maj <<= 16;
	if (*p != ':' && *p != '\0')
		return 1;
 ok:
	*h = maj;
	return 0;
}

/* Get class ID.  Return 0 on success, !0 otherwise.  */
static int get_tc_classid(uint32_t *h, const char *str) {
	uint32_t maj, min;
	char *p;

	maj = TC_H_ROOT;
	if (strcmp(str, "root") == 0)
		goto ok;
	maj = TC_H_UNSPEC;
	if (strcmp(str, "none") == 0)
		goto ok;
	maj = strtoul(str, &p, 16);
	if (p == str) {
		if (*p != ':')
			return 1;
		maj = 0;
	}
	if (*p == ':') {
		if (maj >= (1<<16))
			return 1;
		maj <<= 16;
		str = p + 1;
		min = strtoul(str, &p, 16);
//FIXME: check for "" too?
		if (*p != '\0' || min >= (1<<16))
			return 1;
		maj |= min;
	} else if (*p != 0)
		return 1;
 ok:
	*h = maj;
	return 0;
}

static void print_rate(char *buf, int len, uint32_t rate)
{
	double tmp = (double)rate*8;

	if (use_iec) {
		if (tmp >= 1000*1024*1024)
			snprintf(buf, len, "%.0fMibit", tmp/(1024*1024));
		else if (tmp >= 1000*1024)
			snprintf(buf, len, "%.0fKibit", tmp/1024);
		else
			snprintf(buf, len, "%.0fbit", tmp);
	} else {
		if (tmp >= 1000*1000000)
			snprintf(buf, len, "%.0fMbit", tmp/1000000);
		else if (tmp >= 1000*1000)
			snprintf(buf, len, "%.0fKbit", tmp/1000);
		else
			snprintf(buf, len, "%.0fbit",  tmp);
	}
}

#if 0
/* This is "pfifo_fast".  */
static int prio_parse_opt(int argc, char **argv, struct nlmsghdr *n)
{
	return 0;
}
#endif
static int prio_print_opt(struct rtattr *opt)
{
	int i;
	struct tc_prio_qopt *qopt;
	struct rtattr *tb[TCA_PRIO_MAX+1];

	if (opt == NULL)
		return 0;
	parse_rtattr_nested_compat(tb, TCA_PRIO_MAX, opt, qopt, sizeof(*qopt));
	if (tb == NULL)
		return 0;
	printf("bands %u priomap ", qopt->bands);
	for (i=0; i<=TC_PRIO_MAX; i++)
		printf(" %d", qopt->priomap[i]);

	if (tb[TCA_PRIO_MQ])
		printf(" multiqueue: o%s ",
		    *(unsigned char *)RTA_DATA(tb[TCA_PRIO_MQ]) ? "n" : "ff");

	return 0;
}

#if 0
/* Class Based Queue */
static int cbq_parse_opt(int argc, char **argv, struct nlmsghdr *n)
{
	return 0;
}
#endif
static int cbq_print_opt(struct rtattr *opt)
{
	struct rtattr *tb[TCA_CBQ_MAX+1];
	struct tc_ratespec *r = NULL;
	struct tc_cbq_lssopt *lss = NULL;
	struct tc_cbq_wrropt *wrr = NULL;
	struct tc_cbq_fopt *fopt = NULL;
	struct tc_cbq_ovl *ovl = NULL;
	const char *const error = "CBQ: too short %s opt";
	char buf[64];

	if (opt == NULL)
		goto done;
	parse_rtattr_nested(tb, TCA_CBQ_MAX, opt);

	if (tb[TCA_CBQ_RATE]) {
		if (RTA_PAYLOAD(tb[TCA_CBQ_RATE]) < sizeof(*r))
			bb_error_msg(error, "rate");
		else
			r = RTA_DATA(tb[TCA_CBQ_RATE]);
	}
	if (tb[TCA_CBQ_LSSOPT]) {
		if (RTA_PAYLOAD(tb[TCA_CBQ_LSSOPT]) < sizeof(*lss))
			bb_error_msg(error, "lss");
		else
			lss = RTA_DATA(tb[TCA_CBQ_LSSOPT]);
	}
	if (tb[TCA_CBQ_WRROPT]) {
		if (RTA_PAYLOAD(tb[TCA_CBQ_WRROPT]) < sizeof(*wrr))
			bb_error_msg(error, "wrr");
		else
			wrr = RTA_DATA(tb[TCA_CBQ_WRROPT]);
	}
	if (tb[TCA_CBQ_FOPT]) {
		if (RTA_PAYLOAD(tb[TCA_CBQ_FOPT]) < sizeof(*fopt))
			bb_error_msg(error, "fopt");
		else
			fopt = RTA_DATA(tb[TCA_CBQ_FOPT]);
	}
	if (tb[TCA_CBQ_OVL_STRATEGY]) {
		if (RTA_PAYLOAD(tb[TCA_CBQ_OVL_STRATEGY]) < sizeof(*ovl))
			bb_error_msg("CBQ: too short overlimit strategy %u/%u",
				(unsigned) RTA_PAYLOAD(tb[TCA_CBQ_OVL_STRATEGY]),
				(unsigned) sizeof(*ovl));
		else
			ovl = RTA_DATA(tb[TCA_CBQ_OVL_STRATEGY]);
	}

	if (r) {
		print_rate(buf, sizeof(buf), r->rate);
		printf("rate %s ", buf);
		if (show_details) {
			printf("cell %ub ", 1<<r->cell_log);
			if (r->mpu)
				printf("mpu %ub ", r->mpu);
			if (r->overhead)
				printf("overhead %ub ", r->overhead);
		}
	}
	if (lss && lss->flags) {
		bool comma = false;
		bb_putchar('(');
		if (lss->flags&TCF_CBQ_LSS_BOUNDED) {
			printf("bounded");
			comma = true;
		}
		if (lss->flags&TCF_CBQ_LSS_ISOLATED) {
			if (comma)
				bb_putchar(',');
			printf("isolated");
		}
		printf(") ");
	}
	if (wrr) {
		if (wrr->priority != TC_CBQ_MAXPRIO)
			printf("prio %u", wrr->priority);
		else
			printf("prio no-transmit");
		if (show_details) {
			printf("/%u ", wrr->cpriority);
			if (wrr->weight != 1) {
				print_rate(buf, sizeof(buf), wrr->weight);
				printf("weight %s ", buf);
			}
			if (wrr->allot)
				printf("allot %ub ", wrr->allot);
		}
	}
 done:
	return 0;
}

static FAST_FUNC int print_qdisc(
		const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *hdr,
		void *arg UNUSED_PARAM)
{
	struct tcmsg *msg = NLMSG_DATA(hdr);
	int len = hdr->nlmsg_len;
	struct rtattr * tb[TCA_MAX+1];
	char *name;

	if (hdr->nlmsg_type != RTM_NEWQDISC && hdr->nlmsg_type != RTM_DELQDISC) {
		/* bb_error_msg("not a qdisc"); */
		return 0; /* ??? mimic upstream; should perhaps return -1 */
	}
	len -= NLMSG_LENGTH(sizeof(*msg));
	if (len < 0) {
		/* bb_error_msg("wrong len %d", len); */
		return -1;
	}
	/* not the desired interface? */
	if (filter_ifindex && filter_ifindex != msg->tcm_ifindex)
		return 0;
	memset (tb, 0, sizeof(tb));
	parse_rtattr(tb, TCA_MAX, TCA_RTA(msg), len);
	if (tb[TCA_KIND] == NULL) {
		/* bb_error_msg("%s: NULL kind", "qdisc"); */
		return -1;
	}
	if (hdr->nlmsg_type == RTM_DELQDISC)
		printf("deleted ");
	name = (char*)RTA_DATA(tb[TCA_KIND]);
	printf("qdisc %s %x: ", name, msg->tcm_handle>>16);
	if (filter_ifindex == 0)
		printf("dev %s ", ll_index_to_name(msg->tcm_ifindex));
	if (msg->tcm_parent == TC_H_ROOT)
		printf("root ");
	else if (msg->tcm_parent) {
		char *classid = print_tc_classid(msg->tcm_parent);
		printf("parent %s ", classid);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(classid);
	}
	if (msg->tcm_info != 1)
		printf("refcnt %d ", msg->tcm_info);
	if (tb[TCA_OPTIONS]) {
		static const char _q_[] ALIGN1 = "pfifo_fast\0""cbq\0";
		int qqq = index_in_strings(_q_, name);
		if (qqq == 0) { /* pfifo_fast aka prio */
			prio_print_opt(tb[TCA_OPTIONS]);
		} else if (qqq == 1) { /* class based queuing */
			cbq_print_opt(tb[TCA_OPTIONS]);
		} else
			bb_error_msg("unknown %s", name);
	}
	bb_putchar('\n');
	return 0;
}

static FAST_FUNC int print_class(
		const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *hdr,
		void *arg UNUSED_PARAM)
{
	struct tcmsg *msg = NLMSG_DATA(hdr);
	int len = hdr->nlmsg_len;
	struct rtattr * tb[TCA_MAX+1];
	char *name, *classid;

	/*XXX Eventually factor out common code */

	if (hdr->nlmsg_type != RTM_NEWTCLASS && hdr->nlmsg_type != RTM_DELTCLASS) {
		/* bb_error_msg("not a class"); */
		return 0; /* ??? mimic upstream; should perhaps return -1 */
	}
	len -= NLMSG_LENGTH(sizeof(*msg));
	if (len < 0) {
		/* bb_error_msg("wrong len %d", len); */
		return -1;
	}
	/* not the desired interface? */
	if (filter_qdisc && TC_H_MAJ(msg->tcm_handle^filter_qdisc))
		return 0;
	memset (tb, 0, sizeof(tb));
	parse_rtattr(tb, TCA_MAX, TCA_RTA(msg), len);
	if (tb[TCA_KIND] == NULL) {
		/* bb_error_msg("%s: NULL kind", "class"); */
		return -1;
	}
	if (hdr->nlmsg_type == RTM_DELTCLASS)
		printf("deleted ");

	name = (char*)RTA_DATA(tb[TCA_KIND]);
	classid = !msg->tcm_handle ? NULL : print_tc_classid(
				filter_qdisc ? TC_H_MIN(msg->tcm_parent) : msg->tcm_parent);
	printf ("class %s %s", name, classid);
	if (ENABLE_FEATURE_CLEAN_UP)
		free(classid);

	if (filter_ifindex == 0)
		printf("dev %s ", ll_index_to_name(msg->tcm_ifindex));
	if (msg->tcm_parent == TC_H_ROOT)
		printf("root ");
	else if (msg->tcm_parent) {
		classid = print_tc_classid(filter_qdisc ?
				TC_H_MIN(msg->tcm_parent) : msg->tcm_parent);
		printf("parent %s ", classid);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(classid);
	}
	if (msg->tcm_info)
		printf("leaf %x ", msg->tcm_info >> 16);
	/* Do that get_qdisc_kind(RTA_DATA(tb[TCA_KIND])).  */
	if (tb[TCA_OPTIONS]) {
		static const char _q_[] ALIGN1 = "pfifo_fast\0""cbq\0";
		int qqq = index_in_strings(_q_, name);
		if (qqq == 0) { /* pfifo_fast aka prio */
			/* nothing. */ /*prio_print_opt(tb[TCA_OPTIONS]);*/
		} else if (qqq == 1) { /* class based queuing */
			/* cbq_print_copt() is identical to cbq_print_opt(). */
			cbq_print_opt(tb[TCA_OPTIONS]);
		} else
			bb_error_msg("unknown %s", name);
	}
	bb_putchar('\n');

	return 0;
}

static FAST_FUNC int print_filter(
		const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *hdr UNUSED_PARAM,
		void *arg UNUSED_PARAM)
{
	return 0;
}

int tc_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tc_main(int argc UNUSED_PARAM, char **argv)
{
	static const char objects[] ALIGN1 =
		"qdisc\0""class\0""filter\0"
		;
	enum { OBJ_qdisc = 0, OBJ_class, OBJ_filter };
	static const char commands[] ALIGN1 =
		"add\0""delete\0""change\0"
		"link\0" /* only qdisc */
		"replace\0"
		"show\0""list\0"
		;
	enum {
		CMD_add = 0, CMD_del, CMD_change,
		CMD_link,
		CMD_replace,
		CMD_show
	};
	static const char args[] ALIGN1 =
		"dev\0" /* qdisc, class, filter */
		"root\0" /* class, filter */
		"parent\0" /* class, filter */
		"qdisc\0" /* class */
		"handle\0" /* change: qdisc, class(classid) list: filter */
		"classid\0" /* change: for class use "handle" */
		"preference\0""priority\0""protocol\0" /* filter */
		;
	enum {
		ARG_dev = 0,
		ARG_root,
		ARG_parent,
		ARG_qdisc,
		ARG_handle,
		ARG_classid,
		ARG_pref, ARG_prio, ARG_proto
	};
	struct rtnl_handle rth;
	struct tcmsg msg;
	int ret, obj, cmd, arg;
	char *dev = NULL;

	INIT_G();

	if (!*++argv)
		bb_show_usage();
	xrtnl_open(&rth);
	ret = EXIT_SUCCESS;

	obj = index_in_substrings(objects, *argv++);

	if (obj < 0)
		bb_show_usage();
	if (!*argv)
		cmd = CMD_show; /* list is the default */
	else {
		cmd = index_in_substrings(commands, *argv);
		if (cmd < 0)
			invarg_1_to_2(*argv, argv[-1]);
		argv++;
	}

	memset(&msg, 0, sizeof(msg));
	if (AF_UNSPEC != 0)
		msg.tcm_family = AF_UNSPEC;
	ll_init_map(&rth);

	while (*argv) {
		arg = index_in_substrings(args, *argv);
		if (arg == ARG_dev) {
			NEXT_ARG();
			if (dev)
				duparg2("dev", *argv);
			dev = *argv++;
			msg.tcm_ifindex = xll_name_to_index(dev);
			if (cmd >= CMD_show)
				filter_ifindex = msg.tcm_ifindex;
		} else
		if ((arg == ARG_qdisc && obj == OBJ_class && cmd >= CMD_show)
		 || (arg == ARG_handle && obj == OBJ_qdisc && cmd == CMD_change)
		) {
			NEXT_ARG();
			/* We don't care about duparg2("qdisc handle",*argv) for now */
			if (get_qdisc_handle(&filter_qdisc, *argv))
				invarg_1_to_2(*argv, "qdisc");
		} else
		if (obj != OBJ_qdisc
		 && (arg == ARG_root
		    || arg == ARG_parent
		    || (obj == OBJ_filter && arg >= ARG_pref)
		    )
		) {
			/* nothing */
		} else {
			invarg_1_to_2(*argv, "command");
		}
		NEXT_ARG();
		if (arg == ARG_root) {
			if (msg.tcm_parent)
				duparg("parent", *argv);
			msg.tcm_parent = TC_H_ROOT;
			if (obj == OBJ_filter)
				filter_parent = TC_H_ROOT;
		} else
		if (arg == ARG_parent) {
			uint32_t handle;
			if (msg.tcm_parent)
				duparg(*argv, "parent");
			if (get_tc_classid(&handle, *argv))
				invarg_1_to_2(*argv, "parent");
			msg.tcm_parent = handle;
			if (obj == OBJ_filter)
				filter_parent = handle;
		} else
		if (arg == ARG_handle) { /* filter::list */
			if (msg.tcm_handle)
				duparg(*argv, "handle");
			/* reject LONG_MIN || LONG_MAX */
			/* TODO: for fw
			slash = strchr(handle, '/');
			if (slash != NULL)
				*slash = '\0';
			 */
			msg.tcm_handle = get_u32(*argv, "handle");
			/* if (slash) {if (get_u32(uint32_t &mask, slash+1, NULL)) inv mask; addattr32(n, MAX_MSG, TCA_FW_MASK, mask); */
		} else
		if (arg == ARG_classid
		 && obj == OBJ_class
		 && cmd == CMD_change
		) {
			/* TODO */
		} else
		if (arg == ARG_pref || arg == ARG_prio) { /* filter::list */
			if (filter_prio)
				duparg(*argv, "priority");
			filter_prio = get_u32(*argv, "priority");
		} else
		if (arg == ARG_proto) { /* filter::list */
			uint16_t tmp;
			if (filter_proto)
				duparg(*argv, "protocol");
			if (ll_proto_a2n(&tmp, *argv))
				invarg_1_to_2(*argv, "protocol");
			filter_proto = tmp;
		}
	}

	if (cmd >= CMD_show) { /* show or list */
		if (obj == OBJ_filter)
			msg.tcm_info = TC_H_MAKE(filter_prio<<16, filter_proto);
		if (rtnl_dump_request(&rth, obj == OBJ_qdisc ? RTM_GETQDISC :
						obj == OBJ_class ? RTM_GETTCLASS : RTM_GETTFILTER,
						&msg, sizeof(msg)) < 0)
			bb_simple_perror_msg_and_die("can't send dump request");

		xrtnl_dump_filter(&rth, obj == OBJ_qdisc ? print_qdisc :
						obj == OBJ_class ? print_class : print_filter,
						NULL);
	}
	if (ENABLE_FEATURE_CLEAN_UP) {
		rtnl_close(&rth);
	}
	return ret;
}
