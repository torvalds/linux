/* viohs.c: LDOM Virtual I/O handshake helper layer.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>

#include <asm/ldc.h>
#include <asm/vio.h>

int vio_ldc_send(struct vio_driver_state *vio, void *data, int len)
{
	int err, limit = 1000;

	err = -EINVAL;
	while (limit-- > 0) {
		err = ldc_write(vio->lp, data, len);
		if (!err || (err != -EAGAIN))
			break;
		udelay(1);
	}

	return err;
}
EXPORT_SYMBOL(vio_ldc_send);

static int send_ctrl(struct vio_driver_state *vio,
		     struct vio_msg_tag *tag, int len)
{
	tag->sid = vio_send_sid(vio);
	return vio_ldc_send(vio, tag, len);
}

static void init_tag(struct vio_msg_tag *tag, u8 type, u8 stype, u16 stype_env)
{
	tag->type = type;
	tag->stype = stype;
	tag->stype_env = stype_env;
}

static int send_version(struct vio_driver_state *vio, u16 major, u16 minor)
{
	struct vio_ver_info pkt;

	vio->_local_sid = (u32) sched_clock();

	memset(&pkt, 0, sizeof(pkt));
	init_tag(&pkt.tag, VIO_TYPE_CTRL, VIO_SUBTYPE_INFO, VIO_VER_INFO);
	pkt.major = major;
	pkt.minor = minor;
	pkt.dev_class = vio->dev_class;

	viodbg(HS, "SEND VERSION INFO maj[%u] min[%u] devclass[%u]\n",
	       major, minor, vio->dev_class);

	return send_ctrl(vio, &pkt.tag, sizeof(pkt));
}

static int start_handshake(struct vio_driver_state *vio)
{
	int err;

	viodbg(HS, "START HANDSHAKE\n");

	vio->hs_state = VIO_HS_INVALID;

	err = send_version(vio,
			   vio->ver_table[0].major,
			   vio->ver_table[0].minor);
	if (err < 0)
		return err;

	return 0;
}

static void flush_rx_dring(struct vio_driver_state *vio)
{
	struct vio_dring_state *dr;
	u64 ident;

	BUG_ON(!(vio->dr_state & VIO_DR_STATE_RXREG));

	dr = &vio->drings[VIO_DRIVER_RX_RING];
	ident = dr->ident;

	BUG_ON(!vio->desc_buf);
	kfree(vio->desc_buf);
	vio->desc_buf = NULL;

	memset(dr, 0, sizeof(*dr));
	dr->ident = ident;
}

void vio_link_state_change(struct vio_driver_state *vio, int event)
{
	if (event == LDC_EVENT_UP) {
		vio->hs_state = VIO_HS_INVALID;

		switch (vio->dev_class) {
		case VDEV_NETWORK:
		case VDEV_NETWORK_SWITCH:
			vio->dr_state = (VIO_DR_STATE_TXREQ |
					 VIO_DR_STATE_RXREQ);
			break;

		case VDEV_DISK:
			vio->dr_state = VIO_DR_STATE_TXREQ;
			break;
		case VDEV_DISK_SERVER:
			vio->dr_state = VIO_DR_STATE_RXREQ;
			break;
		}
		start_handshake(vio);
	} else if (event == LDC_EVENT_RESET) {
		vio->hs_state = VIO_HS_INVALID;

		if (vio->dr_state & VIO_DR_STATE_RXREG)
			flush_rx_dring(vio);

		vio->dr_state = 0x00;
		memset(&vio->ver, 0, sizeof(vio->ver));

		ldc_disconnect(vio->lp);
	}
}
EXPORT_SYMBOL(vio_link_state_change);

static int handshake_failure(struct vio_driver_state *vio)
{
	struct vio_dring_state *dr;

	/* XXX Put policy here...  Perhaps start a timer to fire
	 * XXX in 100 ms, which will bring the link up and retry
	 * XXX the handshake.
	 */

	viodbg(HS, "HANDSHAKE FAILURE\n");

	vio->dr_state &= ~(VIO_DR_STATE_TXREG |
			   VIO_DR_STATE_RXREG);

	dr = &vio->drings[VIO_DRIVER_RX_RING];
	memset(dr, 0, sizeof(*dr));

	kfree(vio->desc_buf);
	vio->desc_buf = NULL;
	vio->desc_buf_len = 0;

	vio->hs_state = VIO_HS_INVALID;

	return -ECONNRESET;
}

static int process_unknown(struct vio_driver_state *vio, void *arg)
{
	struct vio_msg_tag *pkt = arg;

	viodbg(HS, "UNKNOWN CONTROL [%02x:%02x:%04x:%08x]\n",
	       pkt->type, pkt->stype, pkt->stype_env, pkt->sid);

	printk(KERN_ERR "vio: ID[%lu] Resetting connection.\n",
	       vio->vdev->channel_id);

	ldc_disconnect(vio->lp);

	return -ECONNRESET;
}

static int send_dreg(struct vio_driver_state *vio)
{
	struct vio_dring_state *dr = &vio->drings[VIO_DRIVER_TX_RING];
	union {
		struct vio_dring_register pkt;
		char all[sizeof(struct vio_dring_register) +
			 (sizeof(struct ldc_trans_cookie) *
			  dr->ncookies)];
	} u;
	int i;

	memset(&u, 0, sizeof(u));
	init_tag(&u.pkt.tag, VIO_TYPE_CTRL, VIO_SUBTYPE_INFO, VIO_DRING_REG);
	u.pkt.dring_ident = 0;
	u.pkt.num_descr = dr->num_entries;
	u.pkt.descr_size = dr->entry_size;
	u.pkt.options = VIO_TX_DRING;
	u.pkt.num_cookies = dr->ncookies;

	viodbg(HS, "SEND DRING_REG INFO ndesc[%u] dsz[%u] opt[0x%x] "
	       "ncookies[%u]\n",
	       u.pkt.num_descr, u.pkt.descr_size, u.pkt.options,
	       u.pkt.num_cookies);

	for (i = 0; i < dr->ncookies; i++) {
		u.pkt.cookies[i] = dr->cookies[i];

		viodbg(HS, "DRING COOKIE(%d) [%016llx:%016llx]\n",
		       i,
		       (unsigned long long) u.pkt.cookies[i].cookie_addr,
		       (unsigned long long) u.pkt.cookies[i].cookie_size);
	}

	return send_ctrl(vio, &u.pkt.tag, sizeof(u));
}

static int send_rdx(struct vio_driver_state *vio)
{
	struct vio_rdx pkt;

	memset(&pkt, 0, sizeof(pkt));

	init_tag(&pkt.tag, VIO_TYPE_CTRL, VIO_SUBTYPE_INFO, VIO_RDX);

	viodbg(HS, "SEND RDX INFO\n");

	return send_ctrl(vio, &pkt.tag, sizeof(pkt));
}

static int send_attr(struct vio_driver_state *vio)
{
	if (!vio->ops)
		return -EINVAL;

	return vio->ops->send_attr(vio);
}

static struct vio_version *find_by_major(struct vio_driver_state *vio,
					 u16 major)
{
	struct vio_version *ret = NULL;
	int i;

	for (i = 0; i < vio->ver_table_entries; i++) {
		struct vio_version *v = &vio->ver_table[i];
		if (v->major <= major) {
			ret = v;
			break;
		}
	}
	return ret;
}

static int process_ver_info(struct vio_driver_state *vio,
			    struct vio_ver_info *pkt)
{
	struct vio_version *vap;
	int err;

	viodbg(HS, "GOT VERSION INFO maj[%u] min[%u] devclass[%u]\n",
	       pkt->major, pkt->minor, pkt->dev_class);

	if (vio->hs_state != VIO_HS_INVALID) {
		/* XXX Perhaps invoke start_handshake? XXX */
		memset(&vio->ver, 0, sizeof(vio->ver));
		vio->hs_state = VIO_HS_INVALID;
	}

	vap = find_by_major(vio, pkt->major);

	vio->_peer_sid = pkt->tag.sid;

	if (!vap) {
		pkt->tag.stype = VIO_SUBTYPE_NACK;
		pkt->major = 0;
		pkt->minor = 0;
		viodbg(HS, "SEND VERSION NACK maj[0] min[0]\n");
		err = send_ctrl(vio, &pkt->tag, sizeof(*pkt));
	} else if (vap->major != pkt->major) {
		pkt->tag.stype = VIO_SUBTYPE_NACK;
		pkt->major = vap->major;
		pkt->minor = vap->minor;
		viodbg(HS, "SEND VERSION NACK maj[%u] min[%u]\n",
		       pkt->major, pkt->minor);
		err = send_ctrl(vio, &pkt->tag, sizeof(*pkt));
	} else {
		struct vio_version ver = {
			.major = pkt->major,
			.minor = pkt->minor,
		};
		if (ver.minor > vap->minor)
			ver.minor = vap->minor;
		pkt->minor = ver.minor;
		pkt->tag.stype = VIO_SUBTYPE_ACK;
		pkt->dev_class = vio->dev_class;
		viodbg(HS, "SEND VERSION ACK maj[%u] min[%u]\n",
		       pkt->major, pkt->minor);
		err = send_ctrl(vio, &pkt->tag, sizeof(*pkt));
		if (err > 0) {
			vio->ver = ver;
			vio->hs_state = VIO_HS_GOTVERS;
		}
	}
	if (err < 0)
		return handshake_failure(vio);

	return 0;
}

static int process_ver_ack(struct vio_driver_state *vio,
			   struct vio_ver_info *pkt)
{
	viodbg(HS, "GOT VERSION ACK maj[%u] min[%u] devclass[%u]\n",
	       pkt->major, pkt->minor, pkt->dev_class);

	if (vio->hs_state & VIO_HS_GOTVERS) {
		if (vio->ver.major != pkt->major ||
		    vio->ver.minor != pkt->minor) {
			pkt->tag.stype = VIO_SUBTYPE_NACK;
			(void) send_ctrl(vio, &pkt->tag, sizeof(*pkt));
			return handshake_failure(vio);
		}
	} else {
		vio->ver.major = pkt->major;
		vio->ver.minor = pkt->minor;
		vio->hs_state = VIO_HS_GOTVERS;
	}

	switch (vio->dev_class) {
	case VDEV_NETWORK:
	case VDEV_DISK:
		if (send_attr(vio) < 0)
			return handshake_failure(vio);
		break;

	default:
		break;
	}

	return 0;
}

static int process_ver_nack(struct vio_driver_state *vio,
			    struct vio_ver_info *pkt)
{
	struct vio_version *nver;

	viodbg(HS, "GOT VERSION NACK maj[%u] min[%u] devclass[%u]\n",
	       pkt->major, pkt->minor, pkt->dev_class);

	if (pkt->major == 0 && pkt->minor == 0)
		return handshake_failure(vio);
	nver = find_by_major(vio, pkt->major);
	if (!nver)
		return handshake_failure(vio);

	if (send_version(vio, nver->major, nver->minor) < 0)
		return handshake_failure(vio);

	return 0;
}

static int process_ver(struct vio_driver_state *vio, struct vio_ver_info *pkt)
{
	switch (pkt->tag.stype) {
	case VIO_SUBTYPE_INFO:
		return process_ver_info(vio, pkt);

	case VIO_SUBTYPE_ACK:
		return process_ver_ack(vio, pkt);

	case VIO_SUBTYPE_NACK:
		return process_ver_nack(vio, pkt);

	default:
		return handshake_failure(vio);
	}
}

static int process_attr(struct vio_driver_state *vio, void *pkt)
{
	int err;

	if (!(vio->hs_state & VIO_HS_GOTVERS))
		return handshake_failure(vio);

	if (!vio->ops)
		return 0;

	err = vio->ops->handle_attr(vio, pkt);
	if (err < 0) {
		return handshake_failure(vio);
	} else {
		vio->hs_state |= VIO_HS_GOT_ATTR;

		if ((vio->dr_state & VIO_DR_STATE_TXREQ) &&
		    !(vio->hs_state & VIO_HS_SENT_DREG)) {
			if (send_dreg(vio) < 0)
				return handshake_failure(vio);

			vio->hs_state |= VIO_HS_SENT_DREG;
		}
	}

	return 0;
}

static int all_drings_registered(struct vio_driver_state *vio)
{
	int need_rx, need_tx;

	need_rx = (vio->dr_state & VIO_DR_STATE_RXREQ);
	need_tx = (vio->dr_state & VIO_DR_STATE_TXREQ);

	if (need_rx &&
	    !(vio->dr_state & VIO_DR_STATE_RXREG))
		return 0;

	if (need_tx &&
	    !(vio->dr_state & VIO_DR_STATE_TXREG))
		return 0;

	return 1;
}

static int process_dreg_info(struct vio_driver_state *vio,
			     struct vio_dring_register *pkt)
{
	struct vio_dring_state *dr;
	int i, len;

	viodbg(HS, "GOT DRING_REG INFO ident[%llx] "
	       "ndesc[%u] dsz[%u] opt[0x%x] ncookies[%u]\n",
	       (unsigned long long) pkt->dring_ident,
	       pkt->num_descr, pkt->descr_size, pkt->options,
	       pkt->num_cookies);

	if (!(vio->dr_state & VIO_DR_STATE_RXREQ))
		goto send_nack;

	if (vio->dr_state & VIO_DR_STATE_RXREG)
		goto send_nack;

	/* v1.6 and higher, ACK with desired, supported mode, or NACK */
	if (vio_version_after_eq(vio, 1, 6)) {
		if (!(pkt->options & VIO_TX_DRING))
			goto send_nack;
		pkt->options = VIO_TX_DRING;
	}

	BUG_ON(vio->desc_buf);

	vio->desc_buf = kzalloc(pkt->descr_size, GFP_ATOMIC);
	if (!vio->desc_buf)
		goto send_nack;

	vio->desc_buf_len = pkt->descr_size;

	dr = &vio->drings[VIO_DRIVER_RX_RING];

	dr->num_entries = pkt->num_descr;
	dr->entry_size = pkt->descr_size;
	dr->ncookies = pkt->num_cookies;
	for (i = 0; i < dr->ncookies; i++) {
		dr->cookies[i] = pkt->cookies[i];

		viodbg(HS, "DRING COOKIE(%d) [%016llx:%016llx]\n",
		       i,
		       (unsigned long long)
		       pkt->cookies[i].cookie_addr,
		       (unsigned long long)
		       pkt->cookies[i].cookie_size);
	}

	pkt->tag.stype = VIO_SUBTYPE_ACK;
	pkt->dring_ident = ++dr->ident;

	viodbg(HS, "SEND DRING_REG ACK ident[%llx] "
	       "ndesc[%u] dsz[%u] opt[0x%x] ncookies[%u]\n",
	       (unsigned long long) pkt->dring_ident,
	       pkt->num_descr, pkt->descr_size, pkt->options,
	       pkt->num_cookies);

	len = (sizeof(*pkt) +
	       (dr->ncookies * sizeof(struct ldc_trans_cookie)));
	if (send_ctrl(vio, &pkt->tag, len) < 0)
		goto send_nack;

	vio->dr_state |= VIO_DR_STATE_RXREG;

	return 0;

send_nack:
	pkt->tag.stype = VIO_SUBTYPE_NACK;
	viodbg(HS, "SEND DRING_REG NACK\n");
	(void) send_ctrl(vio, &pkt->tag, sizeof(*pkt));

	return handshake_failure(vio);
}

static int process_dreg_ack(struct vio_driver_state *vio,
			    struct vio_dring_register *pkt)
{
	struct vio_dring_state *dr;

	viodbg(HS, "GOT DRING_REG ACK ident[%llx] "
	       "ndesc[%u] dsz[%u] opt[0x%x] ncookies[%u]\n",
	       (unsigned long long) pkt->dring_ident,
	       pkt->num_descr, pkt->descr_size, pkt->options,
	       pkt->num_cookies);

	dr = &vio->drings[VIO_DRIVER_TX_RING];

	if (!(vio->dr_state & VIO_DR_STATE_TXREQ))
		return handshake_failure(vio);

	dr->ident = pkt->dring_ident;
	vio->dr_state |= VIO_DR_STATE_TXREG;

	if (all_drings_registered(vio)) {
		if (send_rdx(vio) < 0)
			return handshake_failure(vio);
		vio->hs_state = VIO_HS_SENT_RDX;
	}
	return 0;
}

static int process_dreg_nack(struct vio_driver_state *vio,
			     struct vio_dring_register *pkt)
{
	viodbg(HS, "GOT DRING_REG NACK ident[%llx] "
	       "ndesc[%u] dsz[%u] opt[0x%x] ncookies[%u]\n",
	       (unsigned long long) pkt->dring_ident,
	       pkt->num_descr, pkt->descr_size, pkt->options,
	       pkt->num_cookies);

	return handshake_failure(vio);
}

static int process_dreg(struct vio_driver_state *vio,
			struct vio_dring_register *pkt)
{
	if (!(vio->hs_state & VIO_HS_GOTVERS))
		return handshake_failure(vio);

	switch (pkt->tag.stype) {
	case VIO_SUBTYPE_INFO:
		return process_dreg_info(vio, pkt);

	case VIO_SUBTYPE_ACK:
		return process_dreg_ack(vio, pkt);

	case VIO_SUBTYPE_NACK:
		return process_dreg_nack(vio, pkt);

	default:
		return handshake_failure(vio);
	}
}

static int process_dunreg(struct vio_driver_state *vio,
			  struct vio_dring_unregister *pkt)
{
	struct vio_dring_state *dr = &vio->drings[VIO_DRIVER_RX_RING];

	viodbg(HS, "GOT DRING_UNREG\n");

	if (pkt->dring_ident != dr->ident)
		return 0;

	vio->dr_state &= ~VIO_DR_STATE_RXREG;

	memset(dr, 0, sizeof(*dr));

	kfree(vio->desc_buf);
	vio->desc_buf = NULL;
	vio->desc_buf_len = 0;

	return 0;
}

static int process_rdx_info(struct vio_driver_state *vio, struct vio_rdx *pkt)
{
	viodbg(HS, "GOT RDX INFO\n");

	pkt->tag.stype = VIO_SUBTYPE_ACK;
	viodbg(HS, "SEND RDX ACK\n");
	if (send_ctrl(vio, &pkt->tag, sizeof(*pkt)) < 0)
		return handshake_failure(vio);

	vio->hs_state |= VIO_HS_SENT_RDX_ACK;
	return 0;
}

static int process_rdx_ack(struct vio_driver_state *vio, struct vio_rdx *pkt)
{
	viodbg(HS, "GOT RDX ACK\n");

	if (!(vio->hs_state & VIO_HS_SENT_RDX))
		return handshake_failure(vio);

	vio->hs_state |= VIO_HS_GOT_RDX_ACK;
	return 0;
}

static int process_rdx_nack(struct vio_driver_state *vio, struct vio_rdx *pkt)
{
	viodbg(HS, "GOT RDX NACK\n");

	return handshake_failure(vio);
}

static int process_rdx(struct vio_driver_state *vio, struct vio_rdx *pkt)
{
	if (!all_drings_registered(vio))
		handshake_failure(vio);

	switch (pkt->tag.stype) {
	case VIO_SUBTYPE_INFO:
		return process_rdx_info(vio, pkt);

	case VIO_SUBTYPE_ACK:
		return process_rdx_ack(vio, pkt);

	case VIO_SUBTYPE_NACK:
		return process_rdx_nack(vio, pkt);

	default:
		return handshake_failure(vio);
	}
}

int vio_control_pkt_engine(struct vio_driver_state *vio, void *pkt)
{
	struct vio_msg_tag *tag = pkt;
	u8 prev_state = vio->hs_state;
	int err;

	switch (tag->stype_env) {
	case VIO_VER_INFO:
		err = process_ver(vio, pkt);
		break;

	case VIO_ATTR_INFO:
		err = process_attr(vio, pkt);
		break;

	case VIO_DRING_REG:
		err = process_dreg(vio, pkt);
		break;

	case VIO_DRING_UNREG:
		err = process_dunreg(vio, pkt);
		break;

	case VIO_RDX:
		err = process_rdx(vio, pkt);
		break;

	default:
		err = process_unknown(vio, pkt);
		break;
	}

	if (!err &&
	    vio->hs_state != prev_state &&
	    (vio->hs_state & VIO_HS_COMPLETE)) {
		if (vio->ops)
			vio->ops->handshake_complete(vio);
	}

	return err;
}
EXPORT_SYMBOL(vio_control_pkt_engine);

void vio_conn_reset(struct vio_driver_state *vio)
{
}
EXPORT_SYMBOL(vio_conn_reset);

/* The issue is that the Solaris virtual disk server just mirrors the
 * SID values it gets from the client peer.  So we work around that
 * here in vio_{validate,send}_sid() so that the drivers don't need
 * to be aware of this crap.
 */
int vio_validate_sid(struct vio_driver_state *vio, struct vio_msg_tag *tp)
{
	u32 sid;

	/* Always let VERSION+INFO packets through unchecked, they
	 * define the new SID.
	 */
	if (tp->type == VIO_TYPE_CTRL &&
	    tp->stype == VIO_SUBTYPE_INFO &&
	    tp->stype_env == VIO_VER_INFO)
		return 0;

	/* Ok, now figure out which SID to use.  */
	switch (vio->dev_class) {
	case VDEV_NETWORK:
	case VDEV_NETWORK_SWITCH:
	case VDEV_DISK_SERVER:
	default:
		sid = vio->_peer_sid;
		break;

	case VDEV_DISK:
		sid = vio->_local_sid;
		break;
	}

	if (sid == tp->sid)
		return 0;
	viodbg(DATA, "BAD SID tag->sid[%08x] peer_sid[%08x] local_sid[%08x]\n",
	       tp->sid, vio->_peer_sid, vio->_local_sid);
	return -EINVAL;
}
EXPORT_SYMBOL(vio_validate_sid);

u32 vio_send_sid(struct vio_driver_state *vio)
{
	switch (vio->dev_class) {
	case VDEV_NETWORK:
	case VDEV_NETWORK_SWITCH:
	case VDEV_DISK:
	default:
		return vio->_local_sid;

	case VDEV_DISK_SERVER:
		return vio->_peer_sid;
	}
}
EXPORT_SYMBOL(vio_send_sid);

int vio_ldc_alloc(struct vio_driver_state *vio,
			 struct ldc_channel_config *base_cfg,
			 void *event_arg)
{
	struct ldc_channel_config cfg = *base_cfg;
	struct ldc_channel *lp;

	cfg.tx_irq = vio->vdev->tx_irq;
	cfg.rx_irq = vio->vdev->rx_irq;

	lp = ldc_alloc(vio->vdev->channel_id, &cfg, event_arg, vio->name);
	if (IS_ERR(lp))
		return PTR_ERR(lp);

	vio->lp = lp;

	return 0;
}
EXPORT_SYMBOL(vio_ldc_alloc);

void vio_ldc_free(struct vio_driver_state *vio)
{
	ldc_free(vio->lp);
	vio->lp = NULL;

	kfree(vio->desc_buf);
	vio->desc_buf = NULL;
	vio->desc_buf_len = 0;
}
EXPORT_SYMBOL(vio_ldc_free);

void vio_port_up(struct vio_driver_state *vio)
{
	unsigned long flags;
	int err, state;

	spin_lock_irqsave(&vio->lock, flags);

	state = ldc_state(vio->lp);

	err = 0;
	if (state == LDC_STATE_INIT) {
		err = ldc_bind(vio->lp);
		if (err)
			printk(KERN_WARNING "%s: Port %lu bind failed, "
			       "err=%d\n",
			       vio->name, vio->vdev->channel_id, err);
	}

	if (!err) {
		if (ldc_mode(vio->lp) == LDC_MODE_RAW)
			ldc_set_state(vio->lp, LDC_STATE_CONNECTED);
		else
			err = ldc_connect(vio->lp);

		if (err)
			printk(KERN_WARNING "%s: Port %lu connect failed, "
			       "err=%d\n",
			       vio->name, vio->vdev->channel_id, err);
	}
	if (err) {
		unsigned long expires = jiffies + HZ;

		expires = round_jiffies(expires);
		mod_timer(&vio->timer, expires);
	}

	spin_unlock_irqrestore(&vio->lock, flags);
}
EXPORT_SYMBOL(vio_port_up);

static void vio_port_timer(unsigned long _arg)
{
	struct vio_driver_state *vio = (struct vio_driver_state *) _arg;

	vio_port_up(vio);
}

int vio_driver_init(struct vio_driver_state *vio, struct vio_dev *vdev,
		    u8 dev_class, struct vio_version *ver_table,
		    int ver_table_size, struct vio_driver_ops *ops,
		    char *name)
{
	switch (dev_class) {
	case VDEV_NETWORK:
	case VDEV_NETWORK_SWITCH:
	case VDEV_DISK:
	case VDEV_DISK_SERVER:
		break;

	default:
		return -EINVAL;
	}

	if (!ops || !ops->send_attr || !ops->handle_attr ||
	    !ops->handshake_complete)
		return -EINVAL;

	if (!ver_table || ver_table_size < 0)
		return -EINVAL;

	if (!name)
		return -EINVAL;

	spin_lock_init(&vio->lock);

	vio->name = name;

	vio->dev_class = dev_class;
	vio->vdev = vdev;

	vio->ver_table = ver_table;
	vio->ver_table_entries = ver_table_size;

	vio->ops = ops;

	setup_timer(&vio->timer, vio_port_timer, (unsigned long) vio);

	return 0;
}
EXPORT_SYMBOL(vio_driver_init);
