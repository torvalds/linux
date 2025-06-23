/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *	mpls tunnel api
 *
 *	Authors:
 *		Roopa Prabhu <roopa@cumulusnetworks.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_MPLS_IPTUNNEL_H
#define _LINUX_MPLS_IPTUNNEL_H

/* MPLS tunnel attributes
 * [RTA_ENCAP] = {
 *     [MPLS_IPTUNNEL_DST]
 *     [MPLS_IPTUNNEL_TTL]
 * }
 */
enum {
	MPLS_IPTUNNEL_UNSPEC,
	MPLS_IPTUNNEL_DST,
	MPLS_IPTUNNEL_TTL,
	__MPLS_IPTUNNEL_MAX,
};
#define MPLS_IPTUNNEL_MAX (__MPLS_IPTUNNEL_MAX - 1)

#endif /* _LINUX_MPLS_IPTUNNEL_H */
