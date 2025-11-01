// SPDX-License-Identifier: GPL-2.0

#include <linux/ip.h>
#include <linux/skbuff.h>
#include <net/ip6_checksum.h>
#include <net/psp.h>
#include <net/sock.h>

#include "netdevsim.h"

void nsim_psp_handle_ext(struct sk_buff *skb, struct skb_ext *psp_ext)
{
	if (psp_ext)
		__skb_ext_set(skb, SKB_EXT_PSP, psp_ext);
}

enum skb_drop_reason
nsim_do_psp(struct sk_buff *skb, struct netdevsim *ns,
	    struct netdevsim *peer_ns, struct skb_ext **psp_ext)
{
	enum skb_drop_reason rc = 0;
	struct psp_assoc *pas;
	struct net *net;
	void **ptr;

	rcu_read_lock();
	pas = psp_skb_get_assoc_rcu(skb);
	if (!pas) {
		rc = SKB_NOT_DROPPED_YET;
		goto out_unlock;
	}

	if (!skb_transport_header_was_set(skb)) {
		rc = SKB_DROP_REASON_PSP_OUTPUT;
		goto out_unlock;
	}

	ptr = psp_assoc_drv_data(pas);
	if (*ptr != ns) {
		rc = SKB_DROP_REASON_PSP_OUTPUT;
		goto out_unlock;
	}

	net = sock_net(skb->sk);
	if (!psp_dev_encapsulate(net, skb, pas->tx.spi, pas->version, 0)) {
		rc = SKB_DROP_REASON_PSP_OUTPUT;
		goto out_unlock;
	}

	/* Now pretend we just received this frame */
	if (peer_ns->psp.dev->config.versions & (1 << pas->version)) {
		bool strip_icv = false;
		u8 generation;

		/* We cheat a bit and put the generation in the key.
		 * In real life if generation was too old, then decryption would
		 * fail. Here, we just make it so a bad key causes a bad
		 * generation too, and psp_sk_rx_policy_check() will fail.
		 */
		generation = pas->tx.key[0];

		skb_ext_reset(skb);
		skb->mac_len = ETH_HLEN;
		if (psp_dev_rcv(skb, peer_ns->psp.dev->id, generation,
				strip_icv)) {
			rc = SKB_DROP_REASON_PSP_OUTPUT;
			goto out_unlock;
		}

		*psp_ext = skb->extensions;
		refcount_inc(&(*psp_ext)->refcnt);
		skb->decrypted = 1;
	} else {
		struct ipv6hdr *ip6h __maybe_unused;
		struct iphdr *iph;
		struct udphdr *uh;
		__wsum csum;

		/* Do not decapsulate. Receive the skb with the udp and psp
		 * headers still there as if this is a normal udp packet.
		 * psp_dev_encapsulate() sets udp checksum to 0, so we need to
		 * provide a valid checksum here, so the skb isn't dropped.
		 */
		uh = udp_hdr(skb);
		csum = skb_checksum(skb, skb_transport_offset(skb),
				    ntohs(uh->len), 0);

		switch (skb->protocol) {
		case htons(ETH_P_IP):
			iph = ip_hdr(skb);
			uh->check = udp_v4_check(ntohs(uh->len), iph->saddr,
						 iph->daddr, csum);
			break;
#if IS_ENABLED(CONFIG_IPV6)
		case htons(ETH_P_IPV6):
			ip6h = ipv6_hdr(skb);
			uh->check = udp_v6_check(ntohs(uh->len), &ip6h->saddr,
						 &ip6h->daddr, csum);
			break;
#endif
		}

		uh->check	= uh->check ?: CSUM_MANGLED_0;
		skb->ip_summed	= CHECKSUM_NONE;
	}

out_unlock:
	rcu_read_unlock();
	return rc;
}

static int
nsim_psp_set_config(struct psp_dev *psd, struct psp_dev_config *conf,
		    struct netlink_ext_ack *extack)
{
	return 0;
}

static int
nsim_rx_spi_alloc(struct psp_dev *psd, u32 version,
		  struct psp_key_parsed *assoc,
		  struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = psd->drv_priv;
	unsigned int new;
	int i;

	new = ++ns->psp.spi & PSP_SPI_KEY_ID;
	if (psd->generation & 1)
		new |= PSP_SPI_KEY_PHASE;

	assoc->spi = cpu_to_be32(new);
	assoc->key[0] = psd->generation;
	for (i = 1; i < PSP_MAX_KEY; i++)
		assoc->key[i] = ns->psp.spi + i;

	return 0;
}

static int nsim_assoc_add(struct psp_dev *psd, struct psp_assoc *pas,
			  struct netlink_ext_ack *extack)
{
	struct netdevsim *ns = psd->drv_priv;
	void **ptr = psp_assoc_drv_data(pas);

	/* Copy drv_priv from psd to assoc */
	*ptr = psd->drv_priv;
	ns->psp.assoc_cnt++;

	return 0;
}

static int nsim_key_rotate(struct psp_dev *psd, struct netlink_ext_ack *extack)
{
	return 0;
}

static void nsim_assoc_del(struct psp_dev *psd, struct psp_assoc *pas)
{
	struct netdevsim *ns = psd->drv_priv;
	void **ptr = psp_assoc_drv_data(pas);

	*ptr = NULL;
	ns->psp.assoc_cnt--;
}

static struct psp_dev_ops nsim_psp_ops = {
	.set_config	= nsim_psp_set_config,
	.rx_spi_alloc	= nsim_rx_spi_alloc,
	.tx_key_add	= nsim_assoc_add,
	.tx_key_del	= nsim_assoc_del,
	.key_rotate	= nsim_key_rotate,
};

static struct psp_dev_caps nsim_psp_caps = {
	.versions = 1 << PSP_VERSION_HDR0_AES_GCM_128 |
		    1 << PSP_VERSION_HDR0_AES_GMAC_128 |
		    1 << PSP_VERSION_HDR0_AES_GCM_256 |
		    1 << PSP_VERSION_HDR0_AES_GMAC_256,
	.assoc_drv_spc = sizeof(void *),
};

void nsim_psp_uninit(struct netdevsim *ns)
{
	if (!IS_ERR(ns->psp.dev))
		psp_dev_unregister(ns->psp.dev);
	WARN_ON(ns->psp.assoc_cnt);
}

static ssize_t
nsim_psp_rereg_write(struct file *file, const char __user *data, size_t count,
		     loff_t *ppos)
{
	struct netdevsim *ns = file->private_data;
	int err;

	nsim_psp_uninit(ns);

	ns->psp.dev = psp_dev_create(ns->netdev, &nsim_psp_ops,
				     &nsim_psp_caps, ns);
	err = PTR_ERR_OR_ZERO(ns->psp.dev);
	return err ?: count;
}

static const struct file_operations nsim_psp_rereg_fops = {
	.open = simple_open,
	.write = nsim_psp_rereg_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

int nsim_psp_init(struct netdevsim *ns)
{
	struct dentry *ddir = ns->nsim_dev_port->ddir;
	int err;

	ns->psp.dev = psp_dev_create(ns->netdev, &nsim_psp_ops,
				     &nsim_psp_caps, ns);
	err = PTR_ERR_OR_ZERO(ns->psp.dev);
	if (err)
		return err;

	debugfs_create_file("psp_rereg", 0200, ddir, ns, &nsim_psp_rereg_fops);
	return 0;
}
