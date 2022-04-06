// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * Copyright 2021 Marvell. All rights reserved.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/errno.h>

#include <net/tcp.h>

#include <linux/qed/qed_nvmetcp_ip_services_if.h>

#define QED_IP_RESOL_TIMEOUT  4

int qed_route_ipv4(struct sockaddr_storage *local_addr,
		   struct sockaddr_storage *remote_addr,
		   struct sockaddr *hardware_address,
		   struct net_device **ndev)
{
	struct neighbour *neigh = NULL;
	__be32 *loc_ip, *rem_ip;
	struct rtable *rt;
	int rc = -ENXIO;
	int retry;

	loc_ip = &((struct sockaddr_in *)local_addr)->sin_addr.s_addr;
	rem_ip = &((struct sockaddr_in *)remote_addr)->sin_addr.s_addr;
	*ndev = NULL;
	rt = ip_route_output(&init_net, *rem_ip, *loc_ip, 0/*tos*/, 0/*oif*/);
	if (IS_ERR(rt)) {
		pr_err("lookup route failed\n");
		rc = PTR_ERR(rt);
		goto return_err;
	}

	neigh = dst_neigh_lookup(&rt->dst, rem_ip);
	if (!neigh) {
		rc = -ENOMEM;
		ip_rt_put(rt);
		goto return_err;
	}

	*ndev = rt->dst.dev;
	ip_rt_put(rt);

	/* If not resolved, kick-off state machine towards resolution */
	if (!(neigh->nud_state & NUD_VALID))
		neigh_event_send(neigh, NULL);

	/* query neighbor until resolved or timeout */
	retry = QED_IP_RESOL_TIMEOUT;
	while (!(neigh->nud_state & NUD_VALID) && retry > 0) {
		msleep(1000);
		retry--;
	}

	if (neigh->nud_state & NUD_VALID) {
		/* copy resolved MAC address */
		neigh_ha_snapshot(hardware_address->sa_data, neigh, *ndev);
		hardware_address->sa_family = (*ndev)->type;
		rc = 0;
	}

	neigh_release(neigh);
	if (!(*loc_ip)) {
		*loc_ip = inet_select_addr(*ndev, *rem_ip, RT_SCOPE_UNIVERSE);
		local_addr->ss_family = AF_INET;
	}

return_err:

	return rc;
}
EXPORT_SYMBOL(qed_route_ipv4);

int qed_route_ipv6(struct sockaddr_storage *local_addr,
		   struct sockaddr_storage *remote_addr,
		   struct sockaddr *hardware_address,
		   struct net_device **ndev)
{
	struct neighbour *neigh = NULL;
	struct dst_entry *dst;
	struct flowi6 fl6;
	int rc = -ENXIO;
	int retry;

	memset(&fl6, 0, sizeof(fl6));
	fl6.saddr = ((struct sockaddr_in6 *)local_addr)->sin6_addr;
	fl6.daddr = ((struct sockaddr_in6 *)remote_addr)->sin6_addr;
	dst = ip6_route_output(&init_net, NULL, &fl6);
	if (!dst || dst->error) {
		if (dst) {
			dst_release(dst);
			pr_err("lookup route failed %d\n", dst->error);
		}

		goto out;
	}

	neigh = dst_neigh_lookup(dst, &fl6.daddr);
	if (neigh) {
		*ndev = ip6_dst_idev(dst)->dev;

		/* If not resolved, kick-off state machine towards resolution */
		if (!(neigh->nud_state & NUD_VALID))
			neigh_event_send(neigh, NULL);

		/* query neighbor until resolved or timeout */
		retry = QED_IP_RESOL_TIMEOUT;
		while (!(neigh->nud_state & NUD_VALID) && retry > 0) {
			msleep(1000);
			retry--;
		}

		if (neigh->nud_state & NUD_VALID) {
			neigh_ha_snapshot((u8 *)hardware_address->sa_data,
					  neigh, *ndev);
			hardware_address->sa_family = (*ndev)->type;
			rc = 0;
		}

		neigh_release(neigh);

		if (ipv6_addr_any(&fl6.saddr)) {
			if (ipv6_dev_get_saddr(dev_net(*ndev), *ndev,
					       &fl6.daddr, 0, &fl6.saddr)) {
				pr_err("Unable to find source IP address\n");
				goto out;
			}

			local_addr->ss_family = AF_INET6;
			((struct sockaddr_in6 *)local_addr)->sin6_addr =
								fl6.saddr;
		}
	}

	dst_release(dst);

out:

	return rc;
}
EXPORT_SYMBOL(qed_route_ipv6);

void qed_vlan_get_ndev(struct net_device **ndev, u16 *vlan_id)
{
	if (is_vlan_dev(*ndev)) {
		*vlan_id = vlan_dev_vlan_id(*ndev);
		*ndev = vlan_dev_real_dev(*ndev);
	}
}
EXPORT_SYMBOL(qed_vlan_get_ndev);

struct pci_dev *qed_validate_ndev(struct net_device *ndev)
{
	struct net_device *upper;
	struct pci_dev *pdev;

	for_each_pci_dev(pdev) {
		if (pdev->driver &&
		    !strcmp(pdev->driver->name, "qede")) {
			upper = pci_get_drvdata(pdev);
			if (upper->ifindex == ndev->ifindex)
				return pdev;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(qed_validate_ndev);

__be16 qed_get_in_port(struct sockaddr_storage *sa)
{
	return sa->ss_family == AF_INET
		? ((struct sockaddr_in *)sa)->sin_port
		: ((struct sockaddr_in6 *)sa)->sin6_port;
}
EXPORT_SYMBOL(qed_get_in_port);

int qed_fetch_tcp_port(struct sockaddr_storage local_ip_addr,
		       struct socket **sock, u16 *port)
{
	struct sockaddr_storage sa;
	int rc = 0;

	rc = sock_create(local_ip_addr.ss_family, SOCK_STREAM, IPPROTO_TCP,
			 sock);
	if (rc) {
		pr_warn("failed to create socket: %d\n", rc);
		goto err;
	}

	(*sock)->sk->sk_allocation = GFP_KERNEL;
	sk_set_memalloc((*sock)->sk);

	rc = kernel_bind(*sock, (struct sockaddr *)&local_ip_addr,
			 sizeof(local_ip_addr));

	if (rc) {
		pr_warn("failed to bind socket: %d\n", rc);
		goto err_sock;
	}

	rc = kernel_getsockname(*sock, (struct sockaddr *)&sa);
	if (rc < 0) {
		pr_warn("getsockname() failed: %d\n", rc);
		goto err_sock;
	}

	*port = ntohs(qed_get_in_port(&sa));

	return 0;

err_sock:
	sock_release(*sock);
	sock = NULL;
err:

	return rc;
}
EXPORT_SYMBOL(qed_fetch_tcp_port);

void qed_return_tcp_port(struct socket *sock)
{
	if (sock && sock->sk) {
		tcp_set_state(sock->sk, TCP_CLOSE);
		sock_release(sock);
	}
}
EXPORT_SYMBOL(qed_return_tcp_port);
