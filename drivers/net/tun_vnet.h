/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TUN_VNET_H
#define TUN_VNET_H

/* High bits in flags field are unused. */
#define TUN_VNET_LE     0x80000000
#define TUN_VNET_BE     0x40000000

static inline bool tun_vnet_legacy_is_little_endian(unsigned int flags)
{
	bool be = IS_ENABLED(CONFIG_TUN_VNET_CROSS_LE) &&
		  (flags & TUN_VNET_BE);

	return !be && virtio_legacy_is_little_endian();
}

static inline long tun_get_vnet_be(unsigned int flags, int __user *argp)
{
	int be = !!(flags & TUN_VNET_BE);

	if (!IS_ENABLED(CONFIG_TUN_VNET_CROSS_LE))
		return -EINVAL;

	if (put_user(be, argp))
		return -EFAULT;

	return 0;
}

static inline long tun_set_vnet_be(unsigned int *flags, int __user *argp)
{
	int be;

	if (!IS_ENABLED(CONFIG_TUN_VNET_CROSS_LE))
		return -EINVAL;

	if (get_user(be, argp))
		return -EFAULT;

	if (be)
		*flags |= TUN_VNET_BE;
	else
		*flags &= ~TUN_VNET_BE;

	return 0;
}

static inline bool tun_vnet_is_little_endian(unsigned int flags)
{
	return flags & TUN_VNET_LE || tun_vnet_legacy_is_little_endian(flags);
}

static inline u16 tun_vnet16_to_cpu(unsigned int flags, __virtio16 val)
{
	return __virtio16_to_cpu(tun_vnet_is_little_endian(flags), val);
}

static inline __virtio16 cpu_to_tun_vnet16(unsigned int flags, u16 val)
{
	return __cpu_to_virtio16(tun_vnet_is_little_endian(flags), val);
}

static inline long tun_vnet_ioctl(int *vnet_hdr_sz, unsigned int *flags,
				  unsigned int cmd, int __user *sp)
{
	int s;

	switch (cmd) {
	case TUNGETVNETHDRSZ:
		s = *vnet_hdr_sz;
		if (put_user(s, sp))
			return -EFAULT;
		return 0;

	case TUNSETVNETHDRSZ:
		if (get_user(s, sp))
			return -EFAULT;
		if (s < (int)sizeof(struct virtio_net_hdr))
			return -EINVAL;

		*vnet_hdr_sz = s;
		return 0;

	case TUNGETVNETLE:
		s = !!(*flags & TUN_VNET_LE);
		if (put_user(s, sp))
			return -EFAULT;
		return 0;

	case TUNSETVNETLE:
		if (get_user(s, sp))
			return -EFAULT;
		if (s)
			*flags |= TUN_VNET_LE;
		else
			*flags &= ~TUN_VNET_LE;
		return 0;

	case TUNGETVNETBE:
		return tun_get_vnet_be(*flags, sp);

	case TUNSETVNETBE:
		return tun_set_vnet_be(flags, sp);

	default:
		return -EINVAL;
	}
}

static inline int tun_vnet_hdr_get(int sz, unsigned int flags,
				   struct iov_iter *from,
				   struct virtio_net_hdr *hdr)
{
	u16 hdr_len;

	if (iov_iter_count(from) < sz)
		return -EINVAL;

	if (!copy_from_iter_full(hdr, sizeof(*hdr), from))
		return -EFAULT;

	hdr_len = tun_vnet16_to_cpu(flags, hdr->hdr_len);

	if (hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
		hdr_len = max(tun_vnet16_to_cpu(flags, hdr->csum_start) + tun_vnet16_to_cpu(flags, hdr->csum_offset) + 2, hdr_len);
		hdr->hdr_len = cpu_to_tun_vnet16(flags, hdr_len);
	}

	if (hdr_len > iov_iter_count(from))
		return -EINVAL;

	iov_iter_advance(from, sz - sizeof(*hdr));

	return hdr_len;
}

static inline int tun_vnet_hdr_put(int sz, struct iov_iter *iter,
				   const struct virtio_net_hdr *hdr)
{
	if (unlikely(iov_iter_count(iter) < sz))
		return -EINVAL;

	if (unlikely(copy_to_iter(hdr, sizeof(*hdr), iter) != sizeof(*hdr)))
		return -EFAULT;

	if (iov_iter_zero(sz - sizeof(*hdr), iter) != sz - sizeof(*hdr))
		return -EFAULT;

	return 0;
}

static inline int tun_vnet_hdr_to_skb(unsigned int flags, struct sk_buff *skb,
				      const struct virtio_net_hdr *hdr)
{
	return virtio_net_hdr_to_skb(skb, hdr, tun_vnet_is_little_endian(flags));
}

static inline int tun_vnet_hdr_from_skb(unsigned int flags,
					const struct net_device *dev,
					const struct sk_buff *skb,
					struct virtio_net_hdr *hdr)
{
	int vlan_hlen = skb_vlan_tag_present(skb) ? VLAN_HLEN : 0;

	if (virtio_net_hdr_from_skb(skb, hdr,
				    tun_vnet_is_little_endian(flags), true,
				    vlan_hlen)) {
		struct skb_shared_info *sinfo = skb_shinfo(skb);

		if (net_ratelimit()) {
			netdev_err(dev, "unexpected GSO type: 0x%x, gso_size %d, hdr_len %d\n",
				   sinfo->gso_type, tun_vnet16_to_cpu(flags, hdr->gso_size),
				   tun_vnet16_to_cpu(flags, hdr->hdr_len));
			print_hex_dump(KERN_ERR, "tun: ",
				       DUMP_PREFIX_NONE,
				       16, 1, skb->head,
				       min(tun_vnet16_to_cpu(flags, hdr->hdr_len), 64), true);
		}
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	return 0;
}

#endif /* TUN_VNET_H */
