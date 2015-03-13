#ifndef __BACKPORT_LINUX_GENETLINK_H
#define __BACKPORT_LINUX_GENETLINK_H
#include_next <linux/genetlink.h>

/* This backports:
 *
 * commit e9412c37082b5c932e83364aaed0c38c2ce33acb
 * Author: Neil Horman <nhorman@tuxdriver.com>
 * Date:   Tue May 29 09:30:41 2012 +0000
 *
 *     genetlink: Build a generic netlink family module alias
 */
#ifndef MODULE_ALIAS_GENL_FAMILY
#define MODULE_ALIAS_GENL_FAMILY(family)\
 MODULE_ALIAS_NET_PF_PROTO_NAME(PF_NETLINK, NETLINK_GENERIC, "-family-" family)
#endif

#endif /* __BACKPORT_LINUX_GENETLINK_H */
