#ifndef __BACKPORT_NET_NETLINK_H
#define __BACKPORT_NET_NETLINK_H
#include_next <net/netlink.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
/**
 * nla_put_s8 - Add a s8 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
#define nla_put_s8 LINUX_BACKPORT(nla_put_s8)
static inline int nla_put_s8(struct sk_buff *skb, int attrtype, s8 value)
{
	return nla_put(skb, attrtype, sizeof(s8), &value);
}

/**
 * nla_put_s16 - Add a s16 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
#define nla_put_s16 LINUX_BACKPORT(nla_put_s16)
static inline int nla_put_s16(struct sk_buff *skb, int attrtype, s16 value)
{
	return nla_put(skb, attrtype, sizeof(s16), &value);
}

/**
 * nla_put_s32 - Add a s32 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
#define nla_put_s32 LINUX_BACKPORT(nla_put_s32)
static inline int nla_put_s32(struct sk_buff *skb, int attrtype, s32 value)
{
	return nla_put(skb, attrtype, sizeof(s32), &value);
}

/**
 * nla_put_s64 - Add a s64 netlink attribute to a socket buffer
 * @skb: socket buffer to add attribute to
 * @attrtype: attribute type
 * @value: numeric value
 */
#define nla_put_s64 LINUX_BACKPORT(nla_put_s64)
static inline int nla_put_s64(struct sk_buff *skb, int attrtype, s64 value)
{
	return nla_put(skb, attrtype, sizeof(s64), &value);
}

/**
 * nla_get_s32 - return payload of s32 attribute
 * @nla: s32 netlink attribute
 */
#define nla_get_s32 LINUX_BACKPORT(nla_get_s32)
static inline s32 nla_get_s32(const struct nlattr *nla)
{
	return *(s32 *) nla_data(nla);
}

/**
 * nla_get_s16 - return payload of s16 attribute
 * @nla: s16 netlink attribute
 */
#define nla_get_s16 LINUX_BACKPORT(nla_get_s16)
static inline s16 nla_get_s16(const struct nlattr *nla)
{
	return *(s16 *) nla_data(nla);
}

/**
 * nla_get_s8 - return payload of s8 attribute
 * @nla: s8 netlink attribute
 */
#define nla_get_s8 LINUX_BACKPORT(nla_get_s8)
static inline s8 nla_get_s8(const struct nlattr *nla)
{
	return *(s8 *) nla_data(nla);
}

/**
 * nla_get_s64 - return payload of s64 attribute
 * @nla: s64 netlink attribute
 */
#define nla_get_s64 LINUX_BACKPORT(nla_get_s64)
static inline s64 nla_get_s64(const struct nlattr *nla)
{
	s64 tmp;

	nla_memcpy(&tmp, nla, sizeof(tmp));

	return tmp;
}
#endif /* < 3.7.0 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))
/*
 * This backports:
 * commit 569a8fc38367dfafd87454f27ac646c8e6b54bca
 * Author: David S. Miller <davem@davemloft.net>
 * Date:   Thu Mar 29 23:18:53 2012 -0400
 *
 *     netlink: Add nla_put_be{16,32,64}() helpers.
 */

#define nla_put_be16 LINUX_BACKPORT(nla_put_be16)
static inline int nla_put_be16(struct sk_buff *skb, int attrtype, __be16 value)
{
	return nla_put(skb, attrtype, sizeof(__be16), &value);
}

#define nla_put_be32 LINUX_BACKPORT(nla_put_be32)
static inline int nla_put_be32(struct sk_buff *skb, int attrtype, __be32 value)
{
	return nla_put(skb, attrtype, sizeof(__be32), &value);
}

#define nla_put_be64 LINUX_BACKPORT(nla_put_be64)
static inline int nla_put_be64(struct sk_buff *skb, int attrtype, __be64 value)
{
	return nla_put(skb, attrtype, sizeof(__be64), &value);
}
#endif /* < 3.5 */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0))
#define NLA_S8 (NLA_BINARY + 1)
#define NLA_S16 (NLA_BINARY + 2)
#define NLA_S32 (NLA_BINARY + 3)
#define NLA_S64 (NLA_BINARY + 4)
#define __NLA_TYPE_MAX (NLA_BINARY + 5)

#undef NLA_TYPE_MAX
#define NLA_TYPE_MAX (__NLA_TYPE_MAX - 1)
#endif

#endif /* __BACKPORT_NET_NETLINK_H */
