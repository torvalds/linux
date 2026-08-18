/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/drm_ras.yaml */
/* YNL-GEN kernel header */
/* To regenerate run: tools/net/ynl/ynl-regen.sh */

#ifndef _LINUX_DRM_RAS_GEN_H
#define _LINUX_DRM_RAS_GEN_H

#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/drm/drm_ras.h>

int drm_ras_nl_list_nodes_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb);
int drm_ras_nl_get_error_counter_doit(struct sk_buff *skb,
				      struct genl_info *info);
int drm_ras_nl_get_error_counter_dumpit(struct sk_buff *skb,
					struct netlink_callback *cb);
int drm_ras_nl_clear_error_counter_doit(struct sk_buff *skb,
					struct genl_info *info);

extern struct genl_family drm_ras_nl_family;

#endif /* _LINUX_DRM_RAS_GEN_H */
