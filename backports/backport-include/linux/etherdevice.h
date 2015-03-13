#ifndef _BACKPORT_LINUX_ETHERDEVICE_H
#define _BACKPORT_LINUX_ETHERDEVICE_H
#include_next <linux/etherdevice.h>
#include <linux/version.h>
/*
 * newer kernels include this already and some
 * users rely on getting this indirectly
 */
#include <asm/unaligned.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
#define eth_hw_addr_random LINUX_BACKPORT(eth_hw_addr_random)
static inline void eth_hw_addr_random(struct net_device *dev)
{
	dev->addr_assign_type |= NET_ADDR_RANDOM;
	random_ether_addr(dev->dev_addr);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
#include <linux/random.h>
/**
 * eth_broadcast_addr - Assign broadcast address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the broadcast address to the given address array.
 */
#define eth_broadcast_addr LINUX_BACKPORT(eth_broadcast_addr)
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}

/**
 * eth_random_addr - Generate software assigned random Ethernet address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Generate a random Ethernet address (MAC) that is not multicast
 * and has the local assigned bit set.
 */
#define eth_random_addr LINUX_BACKPORT(eth_random_addr)
static inline void eth_random_addr(u8 *addr)
{
	get_random_bytes(addr, ETH_ALEN);
	addr[0] &= 0xfe;        /* clear multicast bit */
	addr[0] |= 0x02;        /* set local assignment bit (IEEE802) */
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)

/* This backports:
 *
 * commit 6d57e9078e880a3dd232d579f42ac437a8f1ef7b
 * Author: Duan Jiong <djduanjiong@gmail.com>
 * Date:   Sat Sep 8 16:32:28 2012 +0000
 * 
 *     etherdevice: introduce help function eth_zero_addr() 
 */
#define eth_zero_addr LINUX_BACKPORT(eth_zero_addr)
static inline void eth_zero_addr(u8 *addr)
{
	memset(addr, 0x00, ETH_ALEN);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#define ether_addr_equal LINUX_BACKPORT(ether_addr_equal)
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	return !compare_ether_addr(addr1, addr2);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#define eth_prepare_mac_addr_change LINUX_BACKPORT(eth_prepare_mac_addr_change)
extern int eth_prepare_mac_addr_change(struct net_device *dev, void *p);

#define eth_commit_mac_addr_change LINUX_BACKPORT(eth_commit_mac_addr_change)
extern void eth_commit_mac_addr_change(struct net_device *dev, void *p);
#endif /* < 3.9 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,12,0)
/**
 * eth_hw_addr_inherit - Copy dev_addr from another net_device
 * @dst: pointer to net_device to copy dev_addr to
 * @src: pointer to net_device to copy dev_addr from
 *
 * Copy the Ethernet address from one net_device to another along with
 * the address attributes (addr_assign_type).
 */
static inline void eth_hw_addr_inherit(struct net_device *dst,
				       struct net_device *src)
{
	dst->addr_assign_type = src->addr_assign_type;
	memcpy(dst->dev_addr, src->dev_addr, ETH_ALEN);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
/**
 * ether_addr_equal_64bits - Compare two Ethernet addresses
 * @addr1: Pointer to an array of 8 bytes
 * @addr2: Pointer to an other array of 8 bytes
 *
 * Compare two Ethernet addresses, returns true if equal, false otherwise.
 *
 * The function doesn't need any conditional branches and possibly uses
 * word memory accesses on CPU allowing cheap unaligned memory reads.
 * arrays = { byte1, byte2, byte3, byte4, byte5, byte6, pad1, pad2 }
 *
 * Please note that alignment of addr1 & addr2 are only guaranteed to be 16 bits.
 */
#define ether_addr_equal_64bits LINUX_BACKPORT(ether_addr_equal_64bits)
static inline bool ether_addr_equal_64bits(const u8 addr1[6+2],
					   const u8 addr2[6+2])
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	u64 fold = (*(const u64 *)addr1) ^ (*(const u64 *)addr2);

#ifdef __BIG_ENDIAN
	return (fold >> 16) == 0;
#else
	return (fold << 16) == 0;
#endif
#else
	return ether_addr_equal(addr1, addr2);
#endif
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
/**
 * ether_addr_equal_unaligned - Compare two not u16 aligned Ethernet addresses
 * @addr1: Pointer to a six-byte array containing the Ethernet address
 * @addr2: Pointer other six-byte array containing the Ethernet address
 *
 * Compare two Ethernet addresses, returns true if equal
 *
 * Please note: Use only when any Ethernet address may not be u16 aligned.
 */
#define ether_addr_equal_unaligned LINUX_BACKPORT(ether_addr_equal_unaligned)
static inline bool ether_addr_equal_unaligned(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return ether_addr_equal(addr1, addr2);
#else
	return memcmp(addr1, addr2, ETH_ALEN) == 0;
#endif
}

/**
 * ether_addr_copy - Copy an Ethernet address
 * @dst: Pointer to a six-byte array Ethernet address destination
 * @src: Pointer to a six-byte array Ethernet address source
 *
 * Please note: dst & src must both be aligned to u16.
 */
#define ether_addr_copy LINUX_BACKPORT(ether_addr_copy)
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)
#define eth_get_headlen LINUX_BACKPORT(eth_get_headlen)
int eth_get_headlen(unsigned char *data, unsigned int max_len);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
#define eth_skb_pad LINUX_BACKPORT(eth_skb_pad)
/**
 * eth_skb_pad - Pad buffer to mininum number of octets for Ethernet frame
 * @skb: Buffer to pad
 *
 * An Ethernet frame should have a minimum size of 60 bytes.  This function
 * takes short frames and pads them with zeros up to the 60 byte limit.
 */
static inline int eth_skb_pad(struct sk_buff *skb)
{
	return skb_put_padto(skb, ETH_ZLEN);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0) */

#endif /* _BACKPORT_LINUX_ETHERDEVICE_H */
