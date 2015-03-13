#ifndef __BACKPORT_LINUX_NET_H
#define __BACKPORT_LINUX_NET_H
#include_next <linux/net.h>
#include <linux/static_key.h>

/* This backports:
 *
 * commit 2033e9bf06f07e049bbc77e9452856df846714cc -- from v3.5
 * Author: Neil Horman <nhorman@tuxdriver.com>
 * Date:   Tue May 29 09:30:40 2012 +0000
 *
 *     net: add MODULE_ALIAS_NET_PF_PROTO_NAME
 */
#ifndef MODULE_ALIAS_NET_PF_PROTO_NAME
#define MODULE_ALIAS_NET_PF_PROTO_NAME(pf, proto, name) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto) \
		     name)
#endif

#ifndef net_ratelimited_function
#define net_ratelimited_function(function, ...)			\
do {								\
	if (net_ratelimit())					\
		function(__VA_ARGS__);				\
} while (0)

#define net_emerg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_emerg, fmt, ##__VA_ARGS__)
#define net_alert_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_alert, fmt, ##__VA_ARGS__)
#define net_crit_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_crit, fmt, ##__VA_ARGS__)
#define net_err_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_err, fmt, ##__VA_ARGS__)
#define net_notice_ratelimited(fmt, ...)			\
	net_ratelimited_function(pr_notice, fmt, ##__VA_ARGS__)
#define net_warn_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_warn, fmt, ##__VA_ARGS__)
#define net_info_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_info, fmt, ##__VA_ARGS__)
#define net_dbg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_debug, fmt, ##__VA_ARGS__)
#endif

#ifndef DECLARE_SOCKADDR
#define DECLARE_SOCKADDR(type, dst, src)	\
	type dst = ({ __sockaddr_check_size(sizeof(*dst)); (type) src; })
#endif

/*
 * Avoid backporting this if a distro did the work already, this
 * takes the check a bit further than just using LINUX_BACKPORT()
 * namespace, curious if any distro will hit a wall with this.
 * Also curious if any distro will be daring enough to even try
 * to backport this to a release older than 3.5.
 */
#ifndef ___NET_RANDOM_STATIC_KEY_INIT
/*
 * Backporting this before 3.5 is extremely tricky -- I tried, due
 * to the fact that it relies on static keys, which were refactored
 * and optimized through a series of generation of patches from jump
 * labels. These in turn have also been optimized through kernel revisions
 * and have architecture specific code, which if you commit to backporting
 * may affect tracing. My recommendation is that if you have a need for
 * static keys you just require at least 3.5 to remain sane.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0) && !defined(net_get_random_once)
#define __BACKPORT_NET_GET_RANDOM_ONCE 1
#endif
#endif /* ___NET_RANDOM_STATIC_KEY_INIT */

#ifdef __BACKPORT_NET_GET_RANDOM_ONCE
#define __net_get_random_once LINUX_BACKPORT(__net_get_random_once)
bool __net_get_random_once(void *buf, int nbytes, bool *done,
			   struct static_key *done_key);

#ifdef HAVE_JUMP_LABEL
#define ___NET_RANDOM_STATIC_KEY_INIT ((struct static_key) \
		{ .enabled = ATOMIC_INIT(0), .entries = (void *)1 })
#else /* !HAVE_JUMP_LABEL */
#define ___NET_RANDOM_STATIC_KEY_INIT STATIC_KEY_INIT_FALSE
#endif /* HAVE_JUMP_LABEL */

#define net_get_random_once(buf, nbytes)				\
	({								\
		bool ___ret = false;					\
		static bool ___done = false;				\
		static struct static_key ___done_key =			\
			___NET_RANDOM_STATIC_KEY_INIT;			\
		if (!static_key_true(&___done_key))			\
			___ret = __net_get_random_once(buf,		\
						       nbytes,		\
						       &___done,	\
						       &___done_key);	\
		___ret;							\
	})

#endif /* __BACKPORT_NET_GET_RANDOM_ONCE */

#endif /* __BACKPORT_LINUX_NET_H */
