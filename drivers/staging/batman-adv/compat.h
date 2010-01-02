/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 *
 * This file contains macros for maintaining compatibility with older versions
 * of the Linux kernel.
 */

#include <linux/version.h>	/* LINUX_VERSION_CODE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 22)

#define skb_set_network_header(_skb, _offset) \
	do { (_skb)->nh.raw = (_skb)->data + (_offset); } while (0)

#define skb_reset_mac_header(_skb) \
	do { (_skb)->mac.raw = (_skb)->data; } while (0)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define skb_mac_header(_skb) \
    ((_skb)->mac.raw)

#define skb_network_header(_skb) \
    ((_skb)->nh.raw)

#define skb_mac_header(_skb) \
    ((_skb)->mac.raw)

#endif /* < KERNEL_VERSION(2,6,22) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)

static inline int skb_clone_writable(struct sk_buff *skb, unsigned int len)
{
	/* skb->hdr_len not available, just "not writable" to enforce a copy */
	return 0;
}

#define cancel_delayed_work_sync(wq) cancel_rearming_delayed_work(wq)

#endif /* < KERNEL_VERSION(2, 6, 23) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)

#define strict_strtoul(cp, base, res) \
	({ \
	int ret = 0; \
	char *endp; \
	*res = simple_strtoul(cp, &endp, base); \
	if (cp == endp) \
		ret = -EINVAL; \
	ret; \
})

#endif /* < KERNEL_VERSION(2, 6, 25) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26)

static const char hex_asc[] = "0123456789abcdef";
#define hex_asc_lo(x)   hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)   hex_asc[((x) & 0xf0) >> 4]
static inline char *pack_hex_byte(char *buf, u8 byte)
{
    *buf++ = hex_asc_hi(byte);
    *buf++ = hex_asc_lo(byte);
    return buf;
}

#define device_create(_cls, _parent, _devt, _device, _fmt) \
	class_device_create(_cls, _parent, _devt, _device, _fmt)

#define device_destroy(_cls, _device) \
	class_device_destroy(_cls, _device)

#endif /* < KERNEL_VERSION(2, 6, 26) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27)

#ifndef dereference_function_descriptor
#define dereference_function_descriptor(p) (p)
#endif

#ifndef device_create
#define device_create(_cls, _parent, _devt, _device, _fmt) \
	device_create_drvdata(_cls, _parent, _devt, _device, _fmt)
#endif

#endif /* < KERNEL_VERSION(2, 6, 27) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 29)

asmlinkage int bat_printk(const char *fmt, ...);
#define printk bat_printk

static inline struct net_device_stats *dev_get_stats(struct net_device *dev)
{
	if (dev->get_stats)
		return dev->get_stats(dev);
	else
		return NULL;
}

#endif /* < KERNEL_VERSION(2, 6, 29) */
