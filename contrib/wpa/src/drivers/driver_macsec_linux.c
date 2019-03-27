/*
 * Driver interaction with Linux MACsec kernel module
 * Copyright (c) 2016, Sabrina Dubroca <sd@queasysnail.net> and Red Hat, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/route/link.h>
#include <netlink/route/link/macsec.h>
#include <linux/if_macsec.h>
#include <inttypes.h>

#include "utils/common.h"
#include "utils/eloop.h"
#include "pae/ieee802_1x_kay.h"
#include "driver.h"
#include "driver_wired_common.h"

#define DRV_PREFIX "macsec_linux: "

#define UNUSED_SCI 0xffffffffffffffff

struct cb_arg {
	struct macsec_drv_data *drv;
	u32 *pn;
	int ifindex;
	u8 txsa;
	u8 rxsa;
	u64 rxsci;
};

struct macsec_genl_ctx {
	struct nl_sock *sk;
	int macsec_genl_id;
	struct cb_arg cb_arg;
};

struct macsec_drv_data {
	struct driver_wired_common_data common;
	struct rtnl_link *link;
	struct nl_cache *link_cache;
	struct nl_sock *sk;
	struct macsec_genl_ctx ctx;

	struct netlink_data *netlink;
	struct nl_handle *nl;
	char ifname[IFNAMSIZ + 1];
	int ifi;
	int parent_ifi;

	Boolean created_link;

	Boolean controlled_port_enabled;
	Boolean controlled_port_enabled_set;

	Boolean protect_frames;
	Boolean protect_frames_set;

	Boolean encrypt;
	Boolean encrypt_set;

	Boolean replay_protect;
	Boolean replay_protect_set;

	u32 replay_window;

	u8 encoding_sa;
	Boolean encoding_sa_set;
};


static int dump_callback(struct nl_msg *msg, void *argp);


static struct nl_msg * msg_prepare(enum macsec_nl_commands cmd,
				   const struct macsec_genl_ctx *ctx,
				   unsigned int ifindex)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (!msg) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "failed to alloc message");
		return NULL;
	}

	if (!genlmsg_put(msg, 0, 0, ctx->macsec_genl_id, 0, 0, cmd, 0)) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "failed to put header");
		goto nla_put_failure;
	}

	NLA_PUT_U32(msg, MACSEC_ATTR_IFINDEX, ifindex);

	return msg;

nla_put_failure:
	nlmsg_free(msg);
	return NULL;
}


static int nla_put_rxsc_config(struct nl_msg *msg, u64 sci)
{
	struct nlattr *nest = nla_nest_start(msg, MACSEC_ATTR_RXSC_CONFIG);

	if (!nest)
		return -1;

	NLA_PUT_U64(msg, MACSEC_RXSC_ATTR_SCI, sci);

	nla_nest_end(msg, nest);

	return 0;

nla_put_failure:
	return -1;
}


static int init_genl_ctx(struct macsec_drv_data *drv)
{
	struct macsec_genl_ctx *ctx = &drv->ctx;

	ctx->sk = nl_socket_alloc();
	if (!ctx->sk) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "failed to alloc genl socket");
		return -1;
	}

	if (genl_connect(ctx->sk) < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "connection to genl socket failed");
		goto out_free;
	}

	ctx->macsec_genl_id = genl_ctrl_resolve(ctx->sk, "macsec");
	if (ctx->macsec_genl_id < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "genl resolve failed");
		goto out_free;
	}

	memset(&ctx->cb_arg, 0, sizeof(ctx->cb_arg));
	ctx->cb_arg.drv = drv;

	nl_socket_modify_cb(ctx->sk, NL_CB_VALID, NL_CB_CUSTOM, dump_callback,
			    &ctx->cb_arg);

	return 0;

out_free:
	nl_socket_free(ctx->sk);
	ctx->sk = NULL;
	return -1;
}


static int try_commit(struct macsec_drv_data *drv)
{
	int err;

	if (!drv->sk)
		return 0;

	if (!drv->link)
		return 0;

	if (drv->controlled_port_enabled_set) {
		struct rtnl_link *change = rtnl_link_alloc();

		if (!change)
			return -1;

		rtnl_link_set_name(change, drv->ifname);

		if (drv->controlled_port_enabled)
			rtnl_link_set_flags(change, IFF_UP);
		else
			rtnl_link_unset_flags(change, IFF_UP);

		err = rtnl_link_change(drv->sk, change, change, 0);
		if (err < 0)
			return err;

		rtnl_link_put(change);

		drv->controlled_port_enabled_set = FALSE;
	}

	if (drv->protect_frames_set)
		rtnl_link_macsec_set_protect(drv->link, drv->protect_frames);

	if (drv->encrypt_set)
		rtnl_link_macsec_set_encrypt(drv->link, drv->encrypt);

	if (drv->replay_protect_set) {
		rtnl_link_macsec_set_replay_protect(drv->link,
						    drv->replay_protect);
		if (drv->replay_protect)
			rtnl_link_macsec_set_window(drv->link,
						    drv->replay_window);
	}

	if (drv->encoding_sa_set)
		rtnl_link_macsec_set_encoding_sa(drv->link, drv->encoding_sa);

	err = rtnl_link_add(drv->sk, drv->link, 0);
	if (err < 0)
		return err;

	drv->protect_frames_set = FALSE;
	drv->encrypt_set = FALSE;
	drv->replay_protect_set = FALSE;

	return 0;
}


static void macsec_drv_wpa_deinit(void *priv)
{
	struct macsec_drv_data *drv = priv;

	driver_wired_deinit_common(&drv->common);
	os_free(drv);
}


static int macsec_check_macsec(void)
{
	struct nl_sock *sk;
	int err = -1;

	sk = nl_socket_alloc();
	if (!sk) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "failed to alloc genl socket");
		return -1;
	}

	if (genl_connect(sk) < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "connection to genl socket failed");
		goto out_free;
	}

	if (genl_ctrl_resolve(sk, "macsec") < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "genl resolve failed - macsec kernel module not present?");
		goto out_free;
	}

	err = 0;

out_free:
	nl_socket_free(sk);
	return err;
}


static void * macsec_drv_wpa_init(void *ctx, const char *ifname)
{
	struct macsec_drv_data *drv;

	if (macsec_check_macsec() < 0)
		return NULL;

	drv = os_zalloc(sizeof(*drv));
	if (!drv)
		return NULL;

	if (driver_wired_init_common(&drv->common, ifname, ctx) < 0) {
		os_free(drv);
		return NULL;
	}

	return drv;
}


static int macsec_drv_macsec_init(void *priv, struct macsec_init_params *params)
{
	struct macsec_drv_data *drv = priv;
	int err;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	drv->sk = nl_socket_alloc();
	if (!drv->sk)
		return -1;

	err = nl_connect(drv->sk, NETLINK_ROUTE);
	if (err < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX
			   "Unable to connect NETLINK_ROUTE socket: %s",
			   strerror(errno));
		goto sock;
	}

	err = rtnl_link_alloc_cache(drv->sk, AF_UNSPEC, &drv->link_cache);
	if (err < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "Unable to get link cache: %s",
			   strerror(errno));
		goto sock;
	}

	drv->parent_ifi = rtnl_link_name2i(drv->link_cache, drv->common.ifname);
	if (drv->parent_ifi == 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX
			   "couldn't find ifindex for interface %s",
			   drv->common.ifname);
		goto cache;
	}

	err = init_genl_ctx(drv);
	if (err < 0)
		goto cache;

	return 0;

cache:
	nl_cache_free(drv->link_cache);
	drv->link_cache = NULL;
sock:
	nl_socket_free(drv->sk);
	drv->sk = NULL;
	return -1;
}


static int macsec_drv_macsec_deinit(void *priv)
{
	struct macsec_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	if (drv->sk)
		nl_socket_free(drv->sk);
	drv->sk = NULL;

	if (drv->link_cache)
		nl_cache_free(drv->link_cache);
	drv->link_cache = NULL;

	if (drv->ctx.sk)
		nl_socket_free(drv->ctx.sk);

	return 0;
}


static int macsec_drv_get_capability(void *priv, enum macsec_cap *cap)
{
	wpa_printf(MSG_DEBUG, "%s", __func__);

	*cap = MACSEC_CAP_INTEG_AND_CONF;

	return 0;
}


/**
 * macsec_drv_enable_protect_frames - Set protect frames status
 * @priv: Private driver interface data
 * @enabled: TRUE = protect frames enabled
 *           FALSE = protect frames disabled
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_enable_protect_frames(void *priv, Boolean enabled)
{
	struct macsec_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s -> %s", __func__, enabled ? "TRUE" : "FALSE");

	drv->protect_frames_set = TRUE;
	drv->protect_frames = enabled;

	return try_commit(drv);
}


/**
 * macsec_drv_enable_encrypt - Set protect frames status
 * @priv: Private driver interface data
 * @enabled: TRUE = protect frames enabled
 *           FALSE = protect frames disabled
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_enable_encrypt(void *priv, Boolean enabled)
{
	struct macsec_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s -> %s", __func__, enabled ? "TRUE" : "FALSE");

	drv->encrypt_set = TRUE;
	drv->encrypt = enabled;

	return try_commit(drv);
}


/**
 * macsec_drv_set_replay_protect - Set replay protect status and window size
 * @priv: Private driver interface data
 * @enabled: TRUE = replay protect enabled
 *           FALSE = replay protect disabled
 * @window: replay window size, valid only when replay protect enabled
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_set_replay_protect(void *priv, Boolean enabled,
					 u32 window)
{
	struct macsec_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s -> %s, %u", __func__,
		   enabled ? "TRUE" : "FALSE", window);

	drv->replay_protect_set = TRUE;
	drv->replay_protect = enabled;
	if (enabled)
		drv->replay_window = window;

	return try_commit(drv);
}


/**
 * macsec_drv_set_current_cipher_suite - Set current cipher suite
 * @priv: Private driver interface data
 * @cs: EUI64 identifier
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_set_current_cipher_suite(void *priv, u64 cs)
{
	wpa_printf(MSG_DEBUG, "%s -> %016" PRIx64, __func__, cs);
	return 0;
}


/**
 * macsec_drv_enable_controlled_port - Set controlled port status
 * @priv: Private driver interface data
 * @enabled: TRUE = controlled port enabled
 *           FALSE = controlled port disabled
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_enable_controlled_port(void *priv, Boolean enabled)
{
	struct macsec_drv_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s -> %s", __func__, enabled ? "TRUE" : "FALSE");

	drv->controlled_port_enabled = enabled;
	drv->controlled_port_enabled_set = TRUE;

	return try_commit(drv);
}


static struct nla_policy sa_policy[MACSEC_SA_ATTR_MAX + 1] = {
	[MACSEC_SA_ATTR_AN] = { .type = NLA_U8 },
	[MACSEC_SA_ATTR_ACTIVE] = { .type = NLA_U8 },
	[MACSEC_SA_ATTR_PN] = { .type = NLA_U32 },
	[MACSEC_SA_ATTR_KEYID] = { .type = NLA_BINARY },
};

static struct nla_policy sc_policy[MACSEC_RXSC_ATTR_MAX + 1] = {
	[MACSEC_RXSC_ATTR_SCI] = { .type = NLA_U64 },
	[MACSEC_RXSC_ATTR_ACTIVE] = { .type = NLA_U8 },
	[MACSEC_RXSC_ATTR_SA_LIST] = { .type = NLA_NESTED },
};

static struct nla_policy main_policy[MACSEC_ATTR_MAX + 1] = {
	[MACSEC_ATTR_IFINDEX] = { .type = NLA_U32 },
	[MACSEC_ATTR_SECY] = { .type = NLA_NESTED },
	[MACSEC_ATTR_TXSA_LIST] = { .type = NLA_NESTED },
	[MACSEC_ATTR_RXSC_LIST] = { .type = NLA_NESTED },
};

static int dump_callback(struct nl_msg *msg, void *argp)
{
	struct nlmsghdr *ret_hdr = nlmsg_hdr(msg);
	struct nlattr *tb_msg[MACSEC_ATTR_MAX + 1];
	struct cb_arg *arg = (struct cb_arg *) argp;
	struct genlmsghdr *gnlh = (struct genlmsghdr *) nlmsg_data(ret_hdr);
	int err;

	if (ret_hdr->nlmsg_type != arg->drv->ctx.macsec_genl_id)
		return 0;

	err = nla_parse(tb_msg, MACSEC_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), main_policy);
	if (err < 0)
		return 0;

	if (!tb_msg[MACSEC_ATTR_IFINDEX])
		return 0;

	if (nla_get_u32(tb_msg[MACSEC_ATTR_IFINDEX]) != (u32) arg->ifindex)
		return 0;

	if (arg->txsa < 4 && !tb_msg[MACSEC_ATTR_TXSA_LIST]) {
		return 0;
	} else if (arg->txsa < 4) {
		struct nlattr *nla;
		int rem;

		nla_for_each_nested(nla, tb_msg[MACSEC_ATTR_TXSA_LIST], rem) {
			struct nlattr *tb[MACSEC_SA_ATTR_MAX + 1];

			err = nla_parse_nested(tb, MACSEC_SA_ATTR_MAX, nla,
					       sa_policy);
			if (err < 0)
				continue;
			if (!tb[MACSEC_SA_ATTR_AN])
				continue;
			if (nla_get_u8(tb[MACSEC_SA_ATTR_AN]) != arg->txsa)
				continue;
			if (!tb[MACSEC_SA_ATTR_PN])
				return 0;
			*arg->pn = nla_get_u32(tb[MACSEC_SA_ATTR_PN]);
			return 0;
		}

		return 0;
	}

	if (arg->rxsci == UNUSED_SCI)
		return 0;

	if (tb_msg[MACSEC_ATTR_RXSC_LIST]) {
		struct nlattr *nla;
		int rem;

		nla_for_each_nested(nla, tb_msg[MACSEC_ATTR_RXSC_LIST], rem) {
			struct nlattr *tb[MACSEC_RXSC_ATTR_MAX + 1];

			err = nla_parse_nested(tb, MACSEC_RXSC_ATTR_MAX, nla,
					       sc_policy);
			if (err < 0)
				return 0;
			if (!tb[MACSEC_RXSC_ATTR_SCI])
				continue;
			if (nla_get_u64(tb[MACSEC_RXSC_ATTR_SCI]) != arg->rxsci)
				continue;
			if (!tb[MACSEC_RXSC_ATTR_SA_LIST])
				return 0;

			nla_for_each_nested(nla, tb[MACSEC_RXSC_ATTR_SA_LIST],
					    rem) {
				struct nlattr *tb_sa[MACSEC_SA_ATTR_MAX + 1];

				err = nla_parse_nested(tb_sa,
						       MACSEC_SA_ATTR_MAX, nla,
						       sa_policy);
				if (err < 0)
					continue;
				if (!tb_sa[MACSEC_SA_ATTR_AN])
					continue;
				if (nla_get_u8(tb_sa[MACSEC_SA_ATTR_AN]) !=
				    arg->rxsa)
					continue;
				if (!tb_sa[MACSEC_SA_ATTR_PN])
					return 0;
				*arg->pn =
					nla_get_u32(tb_sa[MACSEC_SA_ATTR_PN]);

				return 0;
			}

			return 0;
		}

		return 0;
	}

	return 0;
}


static int nl_send_recv(struct nl_sock *sk, struct nl_msg *msg)
{
	int ret;

	ret = nl_send_auto_complete(sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "%s: failed to send: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
		return ret;
	}

	ret = nl_recvmsgs_default(sk);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "%s: failed to recv: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

	return ret;
}


static int do_dump(struct macsec_drv_data *drv, u8 txsa, u64 rxsci, u8 rxsa,
		   u32 *pn)
{
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	int ret = 1;

	ctx->cb_arg.ifindex = drv->ifi;
	ctx->cb_arg.rxsci = rxsci;
	ctx->cb_arg.rxsa = rxsa;
	ctx->cb_arg.txsa = txsa;
	ctx->cb_arg.pn = pn;

	msg = nlmsg_alloc();
	if (!msg) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "%s: failed to alloc message",
			   __func__);
		return 1;
	}

	if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, ctx->macsec_genl_id, 0,
			 NLM_F_DUMP, MACSEC_CMD_GET_TXSC, 0)) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "%s: failed to put header",
			   __func__);
		goto out_free_msg;
	}

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0)
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "failed to communicate: %d (%s)",
			   ret, nl_geterror(-ret));

	ctx->cb_arg.pn = NULL;

out_free_msg:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_get_receive_lowest_pn - Get receive lowest PN
 * @priv: Private driver interface data
 * @sa: secure association
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_get_receive_lowest_pn(void *priv, struct receive_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	int err;

	wpa_printf(MSG_DEBUG, DRV_PREFIX "%s", __func__);

	err = do_dump(drv, 0xff, mka_sci_u64(&sa->sc->sci), sa->an,
		      &sa->lowest_pn);
	wpa_printf(MSG_DEBUG, DRV_PREFIX "%s: result %d", __func__,
		   sa->lowest_pn);

	return err;
}


/**
 * macsec_drv_get_transmit_next_pn - Get transmit next PN
 * @priv: Private driver interface data
 * @sa: secure association
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_get_transmit_next_pn(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	int err;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	err = do_dump(drv, sa->an, UNUSED_SCI, 0xff, &sa->next_pn);
	wpa_printf(MSG_DEBUG, DRV_PREFIX "%s: err %d result %d", __func__, err,
		   sa->next_pn);
	return err;
}


/**
 * macsec_drv_set_transmit_next_pn - Set transmit next pn
 * @priv: Private driver interface data
 * @sa: secure association
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_set_transmit_next_pn(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> %d: %d", __func__, sa->an, sa->next_pn);

	msg = msg_prepare(MACSEC_CMD_UPD_TXSA, ctx, drv->ifi);
	if (!msg)
		return ret;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, sa->an);
	NLA_PUT_U32(msg, MACSEC_SA_ATTR_PN, sa->next_pn);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "failed to communicate: %d (%s)",
			   ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


#define SCISTR MACSTR "::%hx"
#define SCI2STR(addr, port) MAC2STR(addr), htons(port)

/**
 * macsec_drv_create_receive_sc - Create secure channel for receiving
 * @priv: Private driver interface data
 * @sc: secure channel
 * @sci_addr: secure channel identifier - address
 * @sci_port: secure channel identifier - port
 * @conf_offset: confidentiality offset (0, 30, or 50)
 * @validation: frame validation policy (0 = Disabled, 1 = Checked,
 *	2 = Strict)
 * Returns: 0 on success, -1 on failure (or if not supported)
 */
static int macsec_drv_create_receive_sc(void *priv, struct receive_sc *sc,
					unsigned int conf_offset,
					int validation)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> " SCISTR, __func__,
		   SCI2STR(sc->sci.addr, sc->sci.port));

	msg = msg_prepare(MACSEC_CMD_ADD_RXSC, ctx, drv->ifi);
	if (!msg)
		return ret;

	if (nla_put_rxsc_config(msg, mka_sci_u64(&sc->sci)))
		goto nla_put_failure;

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_delete_receive_sc - Delete secure connection for receiving
 * @priv: private driver interface data from init()
 * @sc: secure channel
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_delete_receive_sc(void *priv, struct receive_sc *sc)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> " SCISTR, __func__,
		   SCI2STR(sc->sci.addr, sc->sci.port));

	msg = msg_prepare(MACSEC_CMD_DEL_RXSC, ctx, drv->ifi);
	if (!msg)
		return ret;

	if (nla_put_rxsc_config(msg, mka_sci_u64(&sc->sci)))
		goto nla_put_failure;

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_create_receive_sa - Create secure association for receive
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_create_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> %d on " SCISTR, __func__, sa->an,
		   SCI2STR(sa->sc->sci.addr, sa->sc->sci.port));

	msg = msg_prepare(MACSEC_CMD_ADD_RXSA, ctx, drv->ifi);
	if (!msg)
		return ret;

	if (nla_put_rxsc_config(msg, mka_sci_u64(&sa->sc->sci)))
		goto nla_put_failure;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, sa->an);
	NLA_PUT_U8(msg, MACSEC_SA_ATTR_ACTIVE, sa->enable_receive);
	NLA_PUT_U32(msg, MACSEC_SA_ATTR_PN, sa->next_pn);
	NLA_PUT(msg, MACSEC_SA_ATTR_KEYID, sizeof(sa->pkey->key_identifier),
		&sa->pkey->key_identifier);
	NLA_PUT(msg, MACSEC_SA_ATTR_KEY, sa->pkey->key_len, sa->pkey->key);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_delete_receive_sa - Delete secure association for receive
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_delete_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> %d on " SCISTR, __func__, sa->an,
		   SCI2STR(sa->sc->sci.addr, sa->sc->sci.port));

	msg = msg_prepare(MACSEC_CMD_DEL_RXSA, ctx, drv->ifi);
	if (!msg)
		return ret;

	if (nla_put_rxsc_config(msg, mka_sci_u64(&sa->sc->sci)))
		goto nla_put_failure;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, sa->an);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


static int set_active_rx_sa(const struct macsec_genl_ctx *ctx, int ifindex,
			    u64 sci, unsigned char an, Boolean state)
{
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	msg = msg_prepare(MACSEC_CMD_UPD_RXSA, ctx, ifindex);
	if (!msg)
		return ret;

	if (nla_put_rxsc_config(msg, sci))
		goto nla_put_failure;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, an);
	NLA_PUT_U8(msg, MACSEC_SA_ATTR_ACTIVE, !!state);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0)
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_enable_receive_sa - Enable the SA for receive
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_enable_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;

	wpa_printf(MSG_DEBUG, "%s -> %d on " SCISTR, __func__, sa->an,
		   SCI2STR(sa->sc->sci.addr, sa->sc->sci.port));

	return set_active_rx_sa(ctx, drv->ifi, mka_sci_u64(&sa->sc->sci),
				sa->an, TRUE);
}


/**
 * macsec_drv_disable_receive_sa - Disable SA for receive
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_disable_receive_sa(void *priv, struct receive_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;

	wpa_printf(MSG_DEBUG, "%s -> %d on " SCISTR, __func__, sa->an,
		   SCI2STR(sa->sc->sci.addr, sa->sc->sci.port));

	return set_active_rx_sa(ctx, drv->ifi, mka_sci_u64(&sa->sc->sci),
				sa->an, FALSE);
}


static struct rtnl_link * lookup_sc(struct nl_cache *cache, int parent, u64 sci)
{
	struct rtnl_link *needle;
	void *match;

	needle = rtnl_link_macsec_alloc();
	if (!needle)
		return NULL;

	rtnl_link_set_link(needle, parent);
	rtnl_link_macsec_set_sci(needle, sci);

	match = nl_cache_find(cache, (struct nl_object *) needle);
	rtnl_link_put(needle);

	return (struct rtnl_link *) match;
}


/**
 * macsec_drv_create_transmit_sc - Create secure connection for transmit
 * @priv: private driver interface data from init()
 * @sc: secure channel
 * @conf_offset: confidentiality offset
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_create_transmit_sc(
	void *priv, struct transmit_sc *sc,
	unsigned int conf_offset)
{
	struct macsec_drv_data *drv = priv;
	struct rtnl_link *link;
	char *ifname;
	u64 sci;
	int err;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	if (!drv->sk) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "NULL rtnl socket");
		return -1;
	}

	link = rtnl_link_macsec_alloc();
	if (!link) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "couldn't allocate link");
		return -1;
	}

	rtnl_link_set_link(link, drv->parent_ifi);

	sci = mka_sci_u64(&sc->sci);
	rtnl_link_macsec_set_sci(link, sci);

	drv->created_link = TRUE;

	err = rtnl_link_add(drv->sk, link, NLM_F_CREATE);
	if (err == -NLE_BUSY) {
		wpa_printf(MSG_INFO,
			   DRV_PREFIX "link already exists, using it");
		drv->created_link = FALSE;
	} else if (err < 0) {
		rtnl_link_put(link);
		wpa_printf(MSG_ERROR, DRV_PREFIX "couldn't create link: err %d",
			   err);
		return err;
	}

	rtnl_link_put(link);

	nl_cache_refill(drv->sk, drv->link_cache);
	link = lookup_sc(drv->link_cache, drv->parent_ifi, sci);
	if (!link) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "couldn't find link");
		return -1;
	}

	drv->ifi = rtnl_link_get_ifindex(link);
	ifname = rtnl_link_get_name(link);
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));
	rtnl_link_put(link);

	drv->link = rtnl_link_macsec_alloc();
	if (!drv->link) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "couldn't allocate link");
		return -1;
	}

	rtnl_link_set_name(drv->link, drv->ifname);

	/* In case some settings have already been done but we couldn't apply
	 * them. */
	return try_commit(drv);
}


/**
 * macsec_drv_delete_transmit_sc - Delete secure connection for transmit
 * @priv: private driver interface data from init()
 * @sc: secure channel
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_delete_transmit_sc(void *priv, struct transmit_sc *sc)
{
	struct macsec_drv_data *drv = priv;
	int err;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	if (!drv->sk)
		return 0;

	if (!drv->created_link) {
		rtnl_link_put(drv->link);
		drv->link = NULL;
		wpa_printf(MSG_DEBUG, DRV_PREFIX
			   "we didn't create the link, leave it alone");
		return 0;
	}

	err = rtnl_link_delete(drv->sk, drv->link);
	if (err < 0)
		wpa_printf(MSG_ERROR, DRV_PREFIX "couldn't delete link");
	rtnl_link_put(drv->link);
	drv->link = NULL;

	return err;
}


/**
 * macsec_drv_create_transmit_sa - Create secure association for transmit
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_create_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> %d", __func__, sa->an);

	msg = msg_prepare(MACSEC_CMD_ADD_TXSA, ctx, drv->ifi);
	if (!msg)
		return ret;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, sa->an);
	NLA_PUT_U32(msg, MACSEC_SA_ATTR_PN, sa->next_pn);
	NLA_PUT(msg, MACSEC_SA_ATTR_KEYID, sizeof(sa->pkey->key_identifier),
		&sa->pkey->key_identifier);
	NLA_PUT(msg, MACSEC_SA_ATTR_KEY, sa->pkey->key_len, sa->pkey->key);
	NLA_PUT_U8(msg, MACSEC_SA_ATTR_ACTIVE, sa->enable_transmit);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_delete_transmit_sa - Delete secure association for transmit
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_delete_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	wpa_printf(MSG_DEBUG, "%s -> %d", __func__, sa->an);

	msg = msg_prepare(MACSEC_CMD_DEL_TXSA, ctx, drv->ifi);
	if (!msg)
		return ret;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, sa->an);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


static int set_active_tx_sa(const struct macsec_genl_ctx *ctx, int ifindex,
			    unsigned char an, Boolean state)
{
	struct nl_msg *msg;
	struct nlattr *nest;
	int ret = -1;

	msg = msg_prepare(MACSEC_CMD_UPD_TXSA, ctx, ifindex);
	if (!msg)
		return ret;

	nest = nla_nest_start(msg, MACSEC_ATTR_SA_CONFIG);
	if (!nest)
		goto nla_put_failure;

	NLA_PUT_U8(msg, MACSEC_SA_ATTR_AN, an);
	NLA_PUT_U8(msg, MACSEC_SA_ATTR_ACTIVE, !!state);

	nla_nest_end(msg, nest);

	ret = nl_send_recv(ctx->sk, msg);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   DRV_PREFIX "%s: failed to communicate: %d (%s)",
			   __func__, ret, nl_geterror(-ret));
	}

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}


/**
 * macsec_drv_enable_transmit_sa - Enable SA for transmit
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_enable_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;
	int ret;

	wpa_printf(MSG_DEBUG, "%s -> %d", __func__, sa->an);

	ret = set_active_tx_sa(ctx, drv->ifi, sa->an, TRUE);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, DRV_PREFIX "failed to enable txsa");
		return ret;
	}

	drv->encoding_sa_set = TRUE;
	drv->encoding_sa = sa->an;

	return try_commit(drv);
}


/**
 * macsec_drv_disable_transmit_sa - Disable SA for transmit
 * @priv: private driver interface data from init()
 * @sa: secure association
 * Returns: 0 on success, -1 on failure
 */
static int macsec_drv_disable_transmit_sa(void *priv, struct transmit_sa *sa)
{
	struct macsec_drv_data *drv = priv;
	struct macsec_genl_ctx *ctx = &drv->ctx;

	wpa_printf(MSG_DEBUG, "%s -> %d", __func__, sa->an);

	return set_active_tx_sa(ctx, drv->ifi, sa->an, FALSE);
}


const struct wpa_driver_ops wpa_driver_macsec_linux_ops = {
	.name = "macsec_linux",
	.desc = "MACsec Ethernet driver for Linux",
	.get_ssid = driver_wired_get_ssid,
	.get_bssid = driver_wired_get_bssid,
	.get_capa = driver_wired_get_capa,
	.init = macsec_drv_wpa_init,
	.deinit = macsec_drv_wpa_deinit,

	.macsec_init = macsec_drv_macsec_init,
	.macsec_deinit = macsec_drv_macsec_deinit,
	.macsec_get_capability = macsec_drv_get_capability,
	.enable_protect_frames = macsec_drv_enable_protect_frames,
	.enable_encrypt = macsec_drv_enable_encrypt,
	.set_replay_protect = macsec_drv_set_replay_protect,
	.set_current_cipher_suite = macsec_drv_set_current_cipher_suite,
	.enable_controlled_port = macsec_drv_enable_controlled_port,
	.get_receive_lowest_pn = macsec_drv_get_receive_lowest_pn,
	.get_transmit_next_pn = macsec_drv_get_transmit_next_pn,
	.set_transmit_next_pn = macsec_drv_set_transmit_next_pn,
	.create_receive_sc = macsec_drv_create_receive_sc,
	.delete_receive_sc = macsec_drv_delete_receive_sc,
	.create_receive_sa = macsec_drv_create_receive_sa,
	.delete_receive_sa = macsec_drv_delete_receive_sa,
	.enable_receive_sa = macsec_drv_enable_receive_sa,
	.disable_receive_sa = macsec_drv_disable_receive_sa,
	.create_transmit_sc = macsec_drv_create_transmit_sc,
	.delete_transmit_sc = macsec_drv_delete_transmit_sc,
	.create_transmit_sa = macsec_drv_create_transmit_sa,
	.delete_transmit_sa = macsec_drv_delete_transmit_sa,
	.enable_transmit_sa = macsec_drv_enable_transmit_sa,
	.disable_transmit_sa = macsec_drv_disable_transmit_sa,
};
