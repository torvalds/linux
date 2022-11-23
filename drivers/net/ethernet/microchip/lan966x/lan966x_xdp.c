// SPDX-License-Identifier: GPL-2.0+

#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/filter.h>

#include "lan966x_main.h"

static int lan966x_xdp_setup(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	struct bpf_prog *old_prog;
	bool old_xdp, new_xdp;
	int err;

	if (!lan966x->fdma) {
		NL_SET_ERR_MSG_MOD(xdp->extack,
				   "Allow to set xdp only when using fdma");
		return -EOPNOTSUPP;
	}

	old_xdp = lan966x_xdp_present(lan966x);
	old_prog = xchg(&port->xdp_prog, xdp->prog);
	new_xdp = lan966x_xdp_present(lan966x);

	if (old_xdp == new_xdp)
		goto out;

	err = lan966x_fdma_reload_page_pool(lan966x);
	if (err) {
		xchg(&port->xdp_prog, old_prog);
		return err;
	}

out:
	if (old_prog)
		bpf_prog_put(old_prog);

	return 0;
}

int lan966x_xdp(struct net_device *dev, struct netdev_bpf *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return lan966x_xdp_setup(dev, xdp);
	default:
		return -EINVAL;
	}
}

int lan966x_xdp_xmit(struct net_device *dev,
		     int n,
		     struct xdp_frame **frames,
		     u32 flags)
{
	struct lan966x_port *port = netdev_priv(dev);
	int nxmit = 0;

	for (int i = 0; i < n; ++i) {
		struct xdp_frame *xdpf = frames[i];
		int err;

		err = lan966x_fdma_xmit_xdpf(port, xdpf, NULL, true);
		if (err)
			break;

		nxmit++;
	}

	return nxmit;
}

int lan966x_xdp_run(struct lan966x_port *port, struct page *page, u32 data_len)
{
	struct bpf_prog *xdp_prog = port->xdp_prog;
	struct lan966x *lan966x = port->lan966x;
	struct xdp_frame *xdpf;
	struct xdp_buff xdp;
	u32 act;

	xdp_init_buff(&xdp, PAGE_SIZE << lan966x->rx.page_order,
		      &port->xdp_rxq);
	xdp_prepare_buff(&xdp, page_address(page),
			 IFH_LEN_BYTES + XDP_PACKET_HEADROOM,
			 data_len - IFH_LEN_BYTES, false);
	act = bpf_prog_run_xdp(xdp_prog, &xdp);
	switch (act) {
	case XDP_PASS:
		return FDMA_PASS;
	case XDP_TX:
		xdpf = xdp_convert_buff_to_frame(&xdp);
		if (!xdpf)
			return FDMA_DROP;

		return lan966x_fdma_xmit_xdpf(port, xdpf, page, false) ?
		       FDMA_DROP : FDMA_TX;
	case XDP_REDIRECT:
		if (xdp_do_redirect(port->dev, &xdp, xdp_prog))
			return FDMA_DROP;

		return FDMA_REDIRECT;
	default:
		bpf_warn_invalid_xdp_action(port->dev, xdp_prog, act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(port->dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
		return FDMA_DROP;
	}
}

bool lan966x_xdp_present(struct lan966x *lan966x)
{
	for (int p = 0; p < lan966x->num_phys_ports; ++p) {
		if (!lan966x->ports[p])
			continue;

		if (lan966x_xdp_port_present(lan966x->ports[p]))
			return true;
	}

	return false;
}

int lan966x_xdp_port_init(struct lan966x_port *port)
{
	struct lan966x *lan966x = port->lan966x;

	return xdp_rxq_info_reg(&port->xdp_rxq, port->dev, 0,
				lan966x->napi.napi_id);
}

void lan966x_xdp_port_deinit(struct lan966x_port *port)
{
	if (xdp_rxq_info_is_reg(&port->xdp_rxq))
		xdp_rxq_info_unreg(&port->xdp_rxq);
}
