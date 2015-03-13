#ifndef __BACKPORT_NET_SOCK_H
#define __BACKPORT_NET_SOCK_H
#include_next <net/sock.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#include <backport/magic.h>

#define sk_for_each3(__sk, node, list) \
	hlist_for_each_entry(__sk, node, list, sk_node)

#define sk_for_each_safe4(__sk, node, tmp, list) \
	hlist_for_each_entry_safe(__sk, node, tmp, list, sk_node)

#define sk_for_each2(__sk, list) \
	hlist_for_each_entry(__sk, list, sk_node)

#define sk_for_each_safe3(__sk, tmp, list) \
	hlist_for_each_entry_safe(__sk, tmp, list, sk_node)

#undef sk_for_each
#define sk_for_each(...) \
	macro_dispatcher(sk_for_each, __VA_ARGS__)(__VA_ARGS__)
#undef sk_for_each_safe
#define sk_for_each_safe(...) \
	macro_dispatcher(sk_for_each_safe, __VA_ARGS__)(__VA_ARGS__)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
/*
 * backport SOCK_SELECT_ERR_QUEUE -- see commit
 * "net: add option to enable error queue packets waking select"
 *
 * Adding 14 to SOCK_QUEUE_SHRUNK will reach a bet that can't be
 * set on older kernels, so sock_flag() will always return false.
 */
#define SOCK_SELECT_ERR_QUEUE (SOCK_QUEUE_SHRUNK + 14)
#endif

#endif /* __BACKPORT_NET_SOCK_H */
