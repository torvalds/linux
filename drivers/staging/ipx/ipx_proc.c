// SPDX-License-Identifier: GPL-2.0
/*
 *	IPX proc routines
 *
 * 	Copyright(C) Arnaldo Carvalho de Melo <acme@conectiva.com.br>, 2002
 */

#include <linux/init.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/tcp_states.h>
#include <net/ipx.h>

static void *ipx_seq_interface_start(struct seq_file *seq, loff_t *pos)
{
	spin_lock_bh(&ipx_interfaces_lock);
	return seq_list_start_head(&ipx_interfaces, *pos);
}

static void *ipx_seq_interface_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &ipx_interfaces, pos);
}

static void ipx_seq_interface_stop(struct seq_file *seq, void *v)
{
	spin_unlock_bh(&ipx_interfaces_lock);
}

static int ipx_seq_interface_show(struct seq_file *seq, void *v)
{
	struct ipx_interface *i;

	if (v == &ipx_interfaces) {
		seq_puts(seq, "Network    Node_Address   Primary  Device     "
			      "Frame_Type");
#ifdef IPX_REFCNT_DEBUG
		seq_puts(seq, "  refcnt");
#endif
		seq_puts(seq, "\n");
		goto out;
	}

	i = list_entry(v, struct ipx_interface, node);
	seq_printf(seq, "%08X   ", ntohl(i->if_netnum));
	seq_printf(seq, "%02X%02X%02X%02X%02X%02X   ",
			i->if_node[0], i->if_node[1], i->if_node[2],
			i->if_node[3], i->if_node[4], i->if_node[5]);
	seq_printf(seq, "%-9s", i == ipx_primary_net ? "Yes" : "No");
	seq_printf(seq, "%-11s", ipx_device_name(i));
	seq_printf(seq, "%-9s", ipx_frame_name(i->if_dlink_type));
#ifdef IPX_REFCNT_DEBUG
	seq_printf(seq, "%6d", refcount_read(&i->refcnt));
#endif
	seq_puts(seq, "\n");
out:
	return 0;
}

static void *ipx_seq_route_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&ipx_routes_lock);
	return seq_list_start_head(&ipx_routes, *pos);
}

static void *ipx_seq_route_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_list_next(v, &ipx_routes, pos);
}

static void ipx_seq_route_stop(struct seq_file *seq, void *v)
{
	read_unlock_bh(&ipx_routes_lock);
}

static int ipx_seq_route_show(struct seq_file *seq, void *v)
{
	struct ipx_route *rt;

	if (v == &ipx_routes) {
		seq_puts(seq, "Network    Router_Net   Router_Node\n");
		goto out;
	}

	rt = list_entry(v, struct ipx_route, node);

	seq_printf(seq, "%08X   ", ntohl(rt->ir_net));
	if (rt->ir_routed)
		seq_printf(seq, "%08X     %02X%02X%02X%02X%02X%02X\n",
			   ntohl(rt->ir_intrfc->if_netnum),
			   rt->ir_router_node[0], rt->ir_router_node[1],
			   rt->ir_router_node[2], rt->ir_router_node[3],
			   rt->ir_router_node[4], rt->ir_router_node[5]);
	else
		seq_puts(seq, "Directly     Connected\n");
out:
	return 0;
}

static __inline__ struct sock *ipx_get_socket_idx(loff_t pos)
{
	struct sock *s = NULL;
	struct ipx_interface *i;

	list_for_each_entry(i, &ipx_interfaces, node) {
		spin_lock_bh(&i->if_sklist_lock);
		sk_for_each(s, &i->if_sklist) {
			if (!pos)
				break;
			--pos;
		}
		spin_unlock_bh(&i->if_sklist_lock);
		if (!pos) {
			if (s)
				goto found;
			break;
		}
	}
	s = NULL;
found:
	return s;
}

static void *ipx_seq_socket_start(struct seq_file *seq, loff_t *pos)
{
	loff_t l = *pos;

	spin_lock_bh(&ipx_interfaces_lock);
	return l ? ipx_get_socket_idx(--l) : SEQ_START_TOKEN;
}

static void *ipx_seq_socket_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock* sk, *next;
	struct ipx_interface *i;
	struct ipx_sock *ipxs;

	++*pos;
	if (v == SEQ_START_TOKEN) {
		sk = NULL;
		i = ipx_interfaces_head();
		if (!i)
			goto out;
		sk = sk_head(&i->if_sklist);
		if (sk)
			spin_lock_bh(&i->if_sklist_lock);
		goto out;
	}
	sk = v;
	next = sk_next(sk);
	if (next) {
		sk = next;
		goto out;
	}
	ipxs = ipx_sk(sk);
	i = ipxs->intrfc;
	spin_unlock_bh(&i->if_sklist_lock);
	sk = NULL;
	for (;;) {
		if (i->node.next == &ipx_interfaces)
			break;
		i = list_entry(i->node.next, struct ipx_interface, node);
		spin_lock_bh(&i->if_sklist_lock);
		if (!hlist_empty(&i->if_sklist)) {
			sk = sk_head(&i->if_sklist);
			break;
		}
		spin_unlock_bh(&i->if_sklist_lock);
	}
out:
	return sk;
}

static int ipx_seq_socket_show(struct seq_file *seq, void *v)
{
	struct sock *s;
	struct ipx_sock *ipxs;

	if (v == SEQ_START_TOKEN) {
#ifdef CONFIG_IPX_INTERN
		seq_puts(seq, "Local_Address               "
			      "Remote_Address              Tx_Queue  "
			      "Rx_Queue  State  Uid\n");
#else
		seq_puts(seq, "Local_Address  Remote_Address              "
			      "Tx_Queue  Rx_Queue  State  Uid\n");
#endif
		goto out;
	}

	s = v;
	ipxs = ipx_sk(s);
#ifdef CONFIG_IPX_INTERN
	seq_printf(seq, "%08X:%02X%02X%02X%02X%02X%02X:%04X  ",
		   ntohl(ipxs->intrfc->if_netnum),
		   ipxs->node[0], ipxs->node[1], ipxs->node[2], ipxs->node[3],
		   ipxs->node[4], ipxs->node[5], ntohs(ipxs->port));
#else
	seq_printf(seq, "%08X:%04X  ", ntohl(ipxs->intrfc->if_netnum),
		   ntohs(ipxs->port));
#endif	/* CONFIG_IPX_INTERN */
	if (s->sk_state != TCP_ESTABLISHED)
		seq_printf(seq, "%-28s", "Not_Connected");
	else {
		seq_printf(seq, "%08X:%02X%02X%02X%02X%02X%02X:%04X  ",
			   ntohl(ipxs->dest_addr.net),
			   ipxs->dest_addr.node[0], ipxs->dest_addr.node[1],
			   ipxs->dest_addr.node[2], ipxs->dest_addr.node[3],
			   ipxs->dest_addr.node[4], ipxs->dest_addr.node[5],
			   ntohs(ipxs->dest_addr.sock));
	}

	seq_printf(seq, "%08X  %08X  %02X     %03u\n",
		   sk_wmem_alloc_get(s),
		   sk_rmem_alloc_get(s),
		   s->sk_state,
		   from_kuid_munged(seq_user_ns(seq), sock_i_uid(s)));
out:
	return 0;
}

static const struct seq_operations ipx_seq_interface_ops = {
	.start  = ipx_seq_interface_start,
	.next   = ipx_seq_interface_next,
	.stop   = ipx_seq_interface_stop,
	.show   = ipx_seq_interface_show,
};

static const struct seq_operations ipx_seq_route_ops = {
	.start  = ipx_seq_route_start,
	.next   = ipx_seq_route_next,
	.stop   = ipx_seq_route_stop,
	.show   = ipx_seq_route_show,
};

static const struct seq_operations ipx_seq_socket_ops = {
	.start  = ipx_seq_socket_start,
	.next   = ipx_seq_socket_next,
	.stop   = ipx_seq_interface_stop,
	.show   = ipx_seq_socket_show,
};

static struct proc_dir_entry *ipx_proc_dir;

int __init ipx_proc_init(void)
{
	struct proc_dir_entry *p;
	int rc = -ENOMEM;

	ipx_proc_dir = proc_mkdir("ipx", init_net.proc_net);

	if (!ipx_proc_dir)
		goto out;
	p = proc_create_seq("interface", S_IRUGO, ipx_proc_dir,
			&ipx_seq_interface_ops);
	if (!p)
		goto out_interface;

	p = proc_create_seq("route", S_IRUGO, ipx_proc_dir, &ipx_seq_route_ops);
	if (!p)
		goto out_route;

	p = proc_create_seq("socket", S_IRUGO, ipx_proc_dir,
			&ipx_seq_socket_ops);
	if (!p)
		goto out_socket;

	rc = 0;
out:
	return rc;
out_socket:
	remove_proc_entry("route", ipx_proc_dir);
out_route:
	remove_proc_entry("interface", ipx_proc_dir);
out_interface:
	remove_proc_entry("ipx", init_net.proc_net);
	goto out;
}

void __exit ipx_proc_exit(void)
{
	remove_proc_entry("interface", ipx_proc_dir);
	remove_proc_entry("route", ipx_proc_dir);
	remove_proc_entry("socket", ipx_proc_dir);
	remove_proc_entry("ipx", init_net.proc_net);
}

#else /* CONFIG_PROC_FS */

int __init ipx_proc_init(void)
{
	return 0;
}

void __exit ipx_proc_exit(void)
{
}

#endif /* CONFIG_PROC_FS */
