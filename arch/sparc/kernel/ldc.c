/* ldc.c: Logical Domain Channel link-layer protocol driver.
 *
 * Copyright (C) 2007, 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/bitmap.h>
#include <linux/iommu-common.h>

#include <asm/hypervisor.h>
#include <asm/iommu.h>
#include <asm/page.h>
#include <asm/ldc.h>
#include <asm/mdesc.h>

#define DRV_MODULE_NAME		"ldc"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.1"
#define DRV_MODULE_RELDATE	"July 22, 2008"

#define COOKIE_PGSZ_CODE	0xf000000000000000ULL
#define COOKIE_PGSZ_CODE_SHIFT	60ULL


static char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";
#define LDC_PACKET_SIZE		64

/* Packet header layout for unreliable and reliable mode frames.
 * When in RAW mode, packets are simply straight 64-byte payloads
 * with no headers.
 */
struct ldc_packet {
	u8			type;
#define LDC_CTRL		0x01
#define LDC_DATA		0x02
#define LDC_ERR			0x10

	u8			stype;
#define LDC_INFO		0x01
#define LDC_ACK			0x02
#define LDC_NACK		0x04

	u8			ctrl;
#define LDC_VERS		0x01 /* Link Version		*/
#define LDC_RTS			0x02 /* Request To Send		*/
#define LDC_RTR			0x03 /* Ready To Receive	*/
#define LDC_RDX			0x04 /* Ready for Data eXchange	*/
#define LDC_CTRL_MSK		0x0f

	u8			env;
#define LDC_LEN			0x3f
#define LDC_FRAG_MASK		0xc0
#define LDC_START		0x40
#define LDC_STOP		0x80

	u32			seqid;

	union {
		u8		u_data[LDC_PACKET_SIZE - 8];
		struct {
			u32	pad;
			u32	ackid;
			u8	r_data[LDC_PACKET_SIZE - 8 - 8];
		} r;
	} u;
};

struct ldc_version {
	u16 major;
	u16 minor;
};

/* Ordered from largest major to lowest.  */
static struct ldc_version ver_arr[] = {
	{ .major = 1, .minor = 0 },
};

#define LDC_DEFAULT_MTU			(4 * LDC_PACKET_SIZE)
#define LDC_DEFAULT_NUM_ENTRIES		(PAGE_SIZE / LDC_PACKET_SIZE)

struct ldc_channel;

struct ldc_mode_ops {
	int (*write)(struct ldc_channel *, const void *, unsigned int);
	int (*read)(struct ldc_channel *, void *, unsigned int);
};

static const struct ldc_mode_ops raw_ops;
static const struct ldc_mode_ops nonraw_ops;
static const struct ldc_mode_ops stream_ops;

int ldom_domaining_enabled;

struct ldc_iommu {
	/* Protects ldc_unmap.  */
	spinlock_t			lock;
	struct ldc_mtable_entry		*page_table;
	struct iommu_map_table		iommu_map_table;
};

struct ldc_channel {
	/* Protects all operations that depend upon channel state.  */
	spinlock_t			lock;

	unsigned long			id;

	u8				*mssbuf;
	u32				mssbuf_len;
	u32				mssbuf_off;

	struct ldc_packet		*tx_base;
	unsigned long			tx_head;
	unsigned long			tx_tail;
	unsigned long			tx_num_entries;
	unsigned long			tx_ra;

	unsigned long			tx_acked;

	struct ldc_packet		*rx_base;
	unsigned long			rx_head;
	unsigned long			rx_tail;
	unsigned long			rx_num_entries;
	unsigned long			rx_ra;

	u32				rcv_nxt;
	u32				snd_nxt;

	unsigned long			chan_state;

	struct ldc_channel_config	cfg;
	void				*event_arg;

	const struct ldc_mode_ops	*mops;

	struct ldc_iommu		iommu;

	struct ldc_version		ver;

	u8				hs_state;
#define LDC_HS_CLOSED			0x00
#define LDC_HS_OPEN			0x01
#define LDC_HS_GOTVERS			0x02
#define LDC_HS_SENTRTR			0x03
#define LDC_HS_GOTRTR			0x04
#define LDC_HS_COMPLETE			0x10

	u8				flags;
#define LDC_FLAG_ALLOCED_QUEUES		0x01
#define LDC_FLAG_REGISTERED_QUEUES	0x02
#define LDC_FLAG_REGISTERED_IRQS	0x04
#define LDC_FLAG_RESET			0x10

	u8				mss;
	u8				state;

#define LDC_IRQ_NAME_MAX		32
	char				rx_irq_name[LDC_IRQ_NAME_MAX];
	char				tx_irq_name[LDC_IRQ_NAME_MAX];

	struct hlist_head		mh_list;

	struct hlist_node		list;
};

#define ldcdbg(TYPE, f, a...) \
do {	if (lp->cfg.debug & LDC_DEBUG_##TYPE) \
		printk(KERN_INFO PFX "ID[%lu] " f, lp->id, ## a); \
} while (0)

static const char *state_to_str(u8 state)
{
	switch (state) {
	case LDC_STATE_INVALID:
		return "INVALID";
	case LDC_STATE_INIT:
		return "INIT";
	case LDC_STATE_BOUND:
		return "BOUND";
	case LDC_STATE_READY:
		return "READY";
	case LDC_STATE_CONNECTED:
		return "CONNECTED";
	default:
		return "<UNKNOWN>";
	}
}

static void ldc_set_state(struct ldc_channel *lp, u8 state)
{
	ldcdbg(STATE, "STATE (%s) --> (%s)\n",
	       state_to_str(lp->state),
	       state_to_str(state));

	lp->state = state;
}

static unsigned long __advance(unsigned long off, unsigned long num_entries)
{
	off += LDC_PACKET_SIZE;
	if (off == (num_entries * LDC_PACKET_SIZE))
		off = 0;

	return off;
}

static unsigned long rx_advance(struct ldc_channel *lp, unsigned long off)
{
	return __advance(off, lp->rx_num_entries);
}

static unsigned long tx_advance(struct ldc_channel *lp, unsigned long off)
{
	return __advance(off, lp->tx_num_entries);
}

static struct ldc_packet *handshake_get_tx_packet(struct ldc_channel *lp,
						  unsigned long *new_tail)
{
	struct ldc_packet *p;
	unsigned long t;

	t = tx_advance(lp, lp->tx_tail);
	if (t == lp->tx_head)
		return NULL;

	*new_tail = t;

	p = lp->tx_base;
	return p + (lp->tx_tail / LDC_PACKET_SIZE);
}

/* When we are in reliable or stream mode, have to track the next packet
 * we haven't gotten an ACK for in the TX queue using tx_acked.  We have
 * to be careful not to stomp over the queue past that point.  During
 * the handshake, we don't have TX data packets pending in the queue
 * and that's why handshake_get_tx_packet() need not be mindful of
 * lp->tx_acked.
 */
static unsigned long head_for_data(struct ldc_channel *lp)
{
	if (lp->cfg.mode == LDC_MODE_STREAM)
		return lp->tx_acked;
	return lp->tx_head;
}

static int tx_has_space_for(struct ldc_channel *lp, unsigned int size)
{
	unsigned long limit, tail, new_tail, diff;
	unsigned int mss;

	limit = head_for_data(lp);
	tail = lp->tx_tail;
	new_tail = tx_advance(lp, tail);
	if (new_tail == limit)
		return 0;

	if (limit > new_tail)
		diff = limit - new_tail;
	else
		diff = (limit +
			((lp->tx_num_entries * LDC_PACKET_SIZE) - new_tail));
	diff /= LDC_PACKET_SIZE;
	mss = lp->mss;

	if (diff * mss < size)
		return 0;

	return 1;
}

static struct ldc_packet *data_get_tx_packet(struct ldc_channel *lp,
					     unsigned long *new_tail)
{
	struct ldc_packet *p;
	unsigned long h, t;

	h = head_for_data(lp);
	t = tx_advance(lp, lp->tx_tail);
	if (t == h)
		return NULL;

	*new_tail = t;

	p = lp->tx_base;
	return p + (lp->tx_tail / LDC_PACKET_SIZE);
}

static int set_tx_tail(struct ldc_channel *lp, unsigned long tail)
{
	unsigned long orig_tail = lp->tx_tail;
	int limit = 1000;

	lp->tx_tail = tail;
	while (limit-- > 0) {
		unsigned long err;

		err = sun4v_ldc_tx_set_qtail(lp->id, tail);
		if (!err)
			return 0;

		if (err != HV_EWOULDBLOCK) {
			lp->tx_tail = orig_tail;
			return -EINVAL;
		}
		udelay(1);
	}

	lp->tx_tail = orig_tail;
	return -EBUSY;
}

/* This just updates the head value in the hypervisor using
 * a polling loop with a timeout.  The caller takes care of
 * upating software state representing the head change, if any.
 */
static int __set_rx_head(struct ldc_channel *lp, unsigned long head)
{
	int limit = 1000;

	while (limit-- > 0) {
		unsigned long err;

		err = sun4v_ldc_rx_set_qhead(lp->id, head);
		if (!err)
			return 0;

		if (err != HV_EWOULDBLOCK)
			return -EINVAL;

		udelay(1);
	}

	return -EBUSY;
}

static int send_tx_packet(struct ldc_channel *lp,
			  struct ldc_packet *p,
			  unsigned long new_tail)
{
	BUG_ON(p != (lp->tx_base + (lp->tx_tail / LDC_PACKET_SIZE)));

	return set_tx_tail(lp, new_tail);
}

static struct ldc_packet *handshake_compose_ctrl(struct ldc_channel *lp,
						 u8 stype, u8 ctrl,
						 void *data, int dlen,
						 unsigned long *new_tail)
{
	struct ldc_packet *p = handshake_get_tx_packet(lp, new_tail);

	if (p) {
		memset(p, 0, sizeof(*p));
		p->type = LDC_CTRL;
		p->stype = stype;
		p->ctrl = ctrl;
		if (data)
			memcpy(p->u.u_data, data, dlen);
	}
	return p;
}

static int start_handshake(struct ldc_channel *lp)
{
	struct ldc_packet *p;
	struct ldc_version *ver;
	unsigned long new_tail;

	ver = &ver_arr[0];

	ldcdbg(HS, "SEND VER INFO maj[%u] min[%u]\n",
	       ver->major, ver->minor);

	p = handshake_compose_ctrl(lp, LDC_INFO, LDC_VERS,
				   ver, sizeof(*ver), &new_tail);
	if (p) {
		int err = send_tx_packet(lp, p, new_tail);
		if (!err)
			lp->flags &= ~LDC_FLAG_RESET;
		return err;
	}
	return -EBUSY;
}

static int send_version_nack(struct ldc_channel *lp,
			     u16 major, u16 minor)
{
	struct ldc_packet *p;
	struct ldc_version ver;
	unsigned long new_tail;

	ver.major = major;
	ver.minor = minor;

	p = handshake_compose_ctrl(lp, LDC_NACK, LDC_VERS,
				   &ver, sizeof(ver), &new_tail);
	if (p) {
		ldcdbg(HS, "SEND VER NACK maj[%u] min[%u]\n",
		       ver.major, ver.minor);

		return send_tx_packet(lp, p, new_tail);
	}
	return -EBUSY;
}

static int send_version_ack(struct ldc_channel *lp,
			    struct ldc_version *vp)
{
	struct ldc_packet *p;
	unsigned long new_tail;

	p = handshake_compose_ctrl(lp, LDC_ACK, LDC_VERS,
				   vp, sizeof(*vp), &new_tail);
	if (p) {
		ldcdbg(HS, "SEND VER ACK maj[%u] min[%u]\n",
		       vp->major, vp->minor);

		return send_tx_packet(lp, p, new_tail);
	}
	return -EBUSY;
}

static int send_rts(struct ldc_channel *lp)
{
	struct ldc_packet *p;
	unsigned long new_tail;

	p = handshake_compose_ctrl(lp, LDC_INFO, LDC_RTS, NULL, 0,
				   &new_tail);
	if (p) {
		p->env = lp->cfg.mode;
		p->seqid = 0;
		lp->rcv_nxt = 0;

		ldcdbg(HS, "SEND RTS env[0x%x] seqid[0x%x]\n",
		       p->env, p->seqid);

		return send_tx_packet(lp, p, new_tail);
	}
	return -EBUSY;
}

static int send_rtr(struct ldc_channel *lp)
{
	struct ldc_packet *p;
	unsigned long new_tail;

	p = handshake_compose_ctrl(lp, LDC_INFO, LDC_RTR, NULL, 0,
				   &new_tail);
	if (p) {
		p->env = lp->cfg.mode;
		p->seqid = 0;

		ldcdbg(HS, "SEND RTR env[0x%x] seqid[0x%x]\n",
		       p->env, p->seqid);

		return send_tx_packet(lp, p, new_tail);
	}
	return -EBUSY;
}

static int send_rdx(struct ldc_channel *lp)
{
	struct ldc_packet *p;
	unsigned long new_tail;

	p = handshake_compose_ctrl(lp, LDC_INFO, LDC_RDX, NULL, 0,
				   &new_tail);
	if (p) {
		p->env = 0;
		p->seqid = ++lp->snd_nxt;
		p->u.r.ackid = lp->rcv_nxt;

		ldcdbg(HS, "SEND RDX env[0x%x] seqid[0x%x] ackid[0x%x]\n",
		       p->env, p->seqid, p->u.r.ackid);

		return send_tx_packet(lp, p, new_tail);
	}
	return -EBUSY;
}

static int send_data_nack(struct ldc_channel *lp, struct ldc_packet *data_pkt)
{
	struct ldc_packet *p;
	unsigned long new_tail;
	int err;

	p = data_get_tx_packet(lp, &new_tail);
	if (!p)
		return -EBUSY;
	memset(p, 0, sizeof(*p));
	p->type = data_pkt->type;
	p->stype = LDC_NACK;
	p->ctrl = data_pkt->ctrl & LDC_CTRL_MSK;
	p->seqid = lp->snd_nxt + 1;
	p->u.r.ackid = lp->rcv_nxt;

	ldcdbg(HS, "SEND DATA NACK type[0x%x] ctl[0x%x] seq[0x%x] ack[0x%x]\n",
	       p->type, p->ctrl, p->seqid, p->u.r.ackid);

	err = send_tx_packet(lp, p, new_tail);
	if (!err)
		lp->snd_nxt++;

	return err;
}

static int ldc_abort(struct ldc_channel *lp)
{
	unsigned long hv_err;

	ldcdbg(STATE, "ABORT\n");

	/* We report but do not act upon the hypervisor errors because
	 * there really isn't much we can do if they fail at this point.
	 */
	hv_err = sun4v_ldc_tx_qconf(lp->id, lp->tx_ra, lp->tx_num_entries);
	if (hv_err)
		printk(KERN_ERR PFX "ldc_abort: "
		       "sun4v_ldc_tx_qconf(%lx,%lx,%lx) failed, err=%lu\n",
		       lp->id, lp->tx_ra, lp->tx_num_entries, hv_err);

	hv_err = sun4v_ldc_tx_get_state(lp->id,
					&lp->tx_head,
					&lp->tx_tail,
					&lp->chan_state);
	if (hv_err)
		printk(KERN_ERR PFX "ldc_abort: "
		       "sun4v_ldc_tx_get_state(%lx,...) failed, err=%lu\n",
		       lp->id, hv_err);

	hv_err = sun4v_ldc_rx_qconf(lp->id, lp->rx_ra, lp->rx_num_entries);
	if (hv_err)
		printk(KERN_ERR PFX "ldc_abort: "
		       "sun4v_ldc_rx_qconf(%lx,%lx,%lx) failed, err=%lu\n",
		       lp->id, lp->rx_ra, lp->rx_num_entries, hv_err);

	/* Refetch the RX queue state as well, because we could be invoked
	 * here in the queue processing context.
	 */
	hv_err = sun4v_ldc_rx_get_state(lp->id,
					&lp->rx_head,
					&lp->rx_tail,
					&lp->chan_state);
	if (hv_err)
		printk(KERN_ERR PFX "ldc_abort: "
		       "sun4v_ldc_rx_get_state(%lx,...) failed, err=%lu\n",
		       lp->id, hv_err);

	return -ECONNRESET;
}

static struct ldc_version *find_by_major(u16 major)
{
	struct ldc_version *ret = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ver_arr); i++) {
		struct ldc_version *v = &ver_arr[i];
		if (v->major <= major) {
			ret = v;
			break;
		}
	}
	return ret;
}

static int process_ver_info(struct ldc_channel *lp, struct ldc_version *vp)
{
	struct ldc_version *vap;
	int err;

	ldcdbg(HS, "GOT VERSION INFO major[%x] minor[%x]\n",
	       vp->major, vp->minor);

	if (lp->hs_state == LDC_HS_GOTVERS) {
		lp->hs_state = LDC_HS_OPEN;
		memset(&lp->ver, 0, sizeof(lp->ver));
	}

	vap = find_by_major(vp->major);
	if (!vap) {
		err = send_version_nack(lp, 0, 0);
	} else if (vap->major != vp->major) {
		err = send_version_nack(lp, vap->major, vap->minor);
	} else {
		struct ldc_version ver = *vp;
		if (ver.minor > vap->minor)
			ver.minor = vap->minor;
		err = send_version_ack(lp, &ver);
		if (!err) {
			lp->ver = ver;
			lp->hs_state = LDC_HS_GOTVERS;
		}
	}
	if (err)
		return ldc_abort(lp);

	return 0;
}

static int process_ver_ack(struct ldc_channel *lp, struct ldc_version *vp)
{
	ldcdbg(HS, "GOT VERSION ACK major[%x] minor[%x]\n",
	       vp->major, vp->minor);

	if (lp->hs_state == LDC_HS_GOTVERS) {
		if (lp->ver.major != vp->major ||
		    lp->ver.minor != vp->minor)
			return ldc_abort(lp);
	} else {
		lp->ver = *vp;
		lp->hs_state = LDC_HS_GOTVERS;
	}
	if (send_rts(lp))
		return ldc_abort(lp);
	return 0;
}

static int process_ver_nack(struct ldc_channel *lp, struct ldc_version *vp)
{
	struct ldc_version *vap;
	struct ldc_packet *p;
	unsigned long new_tail;

	if (vp->major == 0 && vp->minor == 0)
		return ldc_abort(lp);

	vap = find_by_major(vp->major);
	if (!vap)
		return ldc_abort(lp);

	p = handshake_compose_ctrl(lp, LDC_INFO, LDC_VERS,
					   vap, sizeof(*vap),
					   &new_tail);
	if (!p)
		return ldc_abort(lp);

	return send_tx_packet(lp, p, new_tail);
}

static int process_version(struct ldc_channel *lp,
			   struct ldc_packet *p)
{
	struct ldc_version *vp;

	vp = (struct ldc_version *) p->u.u_data;

	switch (p->stype) {
	case LDC_INFO:
		return process_ver_info(lp, vp);

	case LDC_ACK:
		return process_ver_ack(lp, vp);

	case LDC_NACK:
		return process_ver_nack(lp, vp);

	default:
		return ldc_abort(lp);
	}
}

static int process_rts(struct ldc_channel *lp,
		       struct ldc_packet *p)
{
	ldcdbg(HS, "GOT RTS stype[%x] seqid[%x] env[%x]\n",
	       p->stype, p->seqid, p->env);

	if (p->stype     != LDC_INFO	   ||
	    lp->hs_state != LDC_HS_GOTVERS ||
	    p->env       != lp->cfg.mode)
		return ldc_abort(lp);

	lp->snd_nxt = p->seqid;
	lp->rcv_nxt = p->seqid;
	lp->hs_state = LDC_HS_SENTRTR;
	if (send_rtr(lp))
		return ldc_abort(lp);

	return 0;
}

static int process_rtr(struct ldc_channel *lp,
		       struct ldc_packet *p)
{
	ldcdbg(HS, "GOT RTR stype[%x] seqid[%x] env[%x]\n",
	       p->stype, p->seqid, p->env);

	if (p->stype     != LDC_INFO ||
	    p->env       != lp->cfg.mode)
		return ldc_abort(lp);

	lp->snd_nxt = p->seqid;
	lp->hs_state = LDC_HS_COMPLETE;
	ldc_set_state(lp, LDC_STATE_CONNECTED);
	send_rdx(lp);

	return LDC_EVENT_UP;
}

static int rx_seq_ok(struct ldc_channel *lp, u32 seqid)
{
	return lp->rcv_nxt + 1 == seqid;
}

static int process_rdx(struct ldc_channel *lp,
		       struct ldc_packet *p)
{
	ldcdbg(HS, "GOT RDX stype[%x] seqid[%x] env[%x] ackid[%x]\n",
	       p->stype, p->seqid, p->env, p->u.r.ackid);

	if (p->stype != LDC_INFO ||
	    !(rx_seq_ok(lp, p->seqid)))
		return ldc_abort(lp);

	lp->rcv_nxt = p->seqid;

	lp->hs_state = LDC_HS_COMPLETE;
	ldc_set_state(lp, LDC_STATE_CONNECTED);

	return LDC_EVENT_UP;
}

static int process_control_frame(struct ldc_channel *lp,
				 struct ldc_packet *p)
{
	switch (p->ctrl) {
	case LDC_VERS:
		return process_version(lp, p);

	case LDC_RTS:
		return process_rts(lp, p);

	case LDC_RTR:
		return process_rtr(lp, p);

	case LDC_RDX:
		return process_rdx(lp, p);

	default:
		return ldc_abort(lp);
	}
}

static int process_error_frame(struct ldc_channel *lp,
			       struct ldc_packet *p)
{
	return ldc_abort(lp);
}

static int process_data_ack(struct ldc_channel *lp,
			    struct ldc_packet *ack)
{
	unsigned long head = lp->tx_acked;
	u32 ackid = ack->u.r.ackid;

	while (1) {
		struct ldc_packet *p = lp->tx_base + (head / LDC_PACKET_SIZE);

		head = tx_advance(lp, head);

		if (p->seqid == ackid) {
			lp->tx_acked = head;
			return 0;
		}
		if (head == lp->tx_tail)
			return ldc_abort(lp);
	}

	return 0;
}

static void send_events(struct ldc_channel *lp, unsigned int event_mask)
{
	if (event_mask & LDC_EVENT_RESET)
		lp->cfg.event(lp->event_arg, LDC_EVENT_RESET);
	if (event_mask & LDC_EVENT_UP)
		lp->cfg.event(lp->event_arg, LDC_EVENT_UP);
	if (event_mask & LDC_EVENT_DATA_READY)
		lp->cfg.event(lp->event_arg, LDC_EVENT_DATA_READY);
}

static irqreturn_t ldc_rx(int irq, void *dev_id)
{
	struct ldc_channel *lp = dev_id;
	unsigned long orig_state, flags;
	unsigned int event_mask;

	spin_lock_irqsave(&lp->lock, flags);

	orig_state = lp->chan_state;

	/* We should probably check for hypervisor errors here and
	 * reset the LDC channel if we get one.
	 */
	sun4v_ldc_rx_get_state(lp->id,
			       &lp->rx_head,
			       &lp->rx_tail,
			       &lp->chan_state);

	ldcdbg(RX, "RX state[0x%02lx:0x%02lx] head[0x%04lx] tail[0x%04lx]\n",
	       orig_state, lp->chan_state, lp->rx_head, lp->rx_tail);

	event_mask = 0;

	if (lp->cfg.mode == LDC_MODE_RAW &&
	    lp->chan_state == LDC_CHANNEL_UP) {
		lp->hs_state = LDC_HS_COMPLETE;
		ldc_set_state(lp, LDC_STATE_CONNECTED);

		event_mask |= LDC_EVENT_UP;

		orig_state = lp->chan_state;
	}

	/* If we are in reset state, flush the RX queue and ignore
	 * everything.
	 */
	if (lp->flags & LDC_FLAG_RESET) {
		(void) __set_rx_head(lp, lp->rx_tail);
		goto out;
	}

	/* Once we finish the handshake, we let the ldc_read()
	 * paths do all of the control frame and state management.
	 * Just trigger the callback.
	 */
	if (lp->hs_state == LDC_HS_COMPLETE) {
handshake_complete:
		if (lp->chan_state != orig_state) {
			unsigned int event = LDC_EVENT_RESET;

			if (lp->chan_state == LDC_CHANNEL_UP)
				event = LDC_EVENT_UP;

			event_mask |= event;
		}
		if (lp->rx_head != lp->rx_tail)
			event_mask |= LDC_EVENT_DATA_READY;

		goto out;
	}

	if (lp->chan_state != orig_state)
		goto out;

	while (lp->rx_head != lp->rx_tail) {
		struct ldc_packet *p;
		unsigned long new;
		int err;

		p = lp->rx_base + (lp->rx_head / LDC_PACKET_SIZE);

		switch (p->type) {
		case LDC_CTRL:
			err = process_control_frame(lp, p);
			if (err > 0)
				event_mask |= err;
			break;

		case LDC_DATA:
			event_mask |= LDC_EVENT_DATA_READY;
			err = 0;
			break;

		case LDC_ERR:
			err = process_error_frame(lp, p);
			break;

		default:
			err = ldc_abort(lp);
			break;
		}

		if (err < 0)
			break;

		new = lp->rx_head;
		new += LDC_PACKET_SIZE;
		if (new == (lp->rx_num_entries * LDC_PACKET_SIZE))
			new = 0;
		lp->rx_head = new;

		err = __set_rx_head(lp, new);
		if (err < 0) {
			(void) ldc_abort(lp);
			break;
		}
		if (lp->hs_state == LDC_HS_COMPLETE)
			goto handshake_complete;
	}

out:
	spin_unlock_irqrestore(&lp->lock, flags);

	send_events(lp, event_mask);

	return IRQ_HANDLED;
}

static irqreturn_t ldc_tx(int irq, void *dev_id)
{
	struct ldc_channel *lp = dev_id;
	unsigned long flags, orig_state;
	unsigned int event_mask = 0;

	spin_lock_irqsave(&lp->lock, flags);

	orig_state = lp->chan_state;

	/* We should probably check for hypervisor errors here and
	 * reset the LDC channel if we get one.
	 */
	sun4v_ldc_tx_get_state(lp->id,
			       &lp->tx_head,
			       &lp->tx_tail,
			       &lp->chan_state);

	ldcdbg(TX, " TX state[0x%02lx:0x%02lx] head[0x%04lx] tail[0x%04lx]\n",
	       orig_state, lp->chan_state, lp->tx_head, lp->tx_tail);

	if (lp->cfg.mode == LDC_MODE_RAW &&
	    lp->chan_state == LDC_CHANNEL_UP) {
		lp->hs_state = LDC_HS_COMPLETE;
		ldc_set_state(lp, LDC_STATE_CONNECTED);

		event_mask |= LDC_EVENT_UP;
	}

	spin_unlock_irqrestore(&lp->lock, flags);

	send_events(lp, event_mask);

	return IRQ_HANDLED;
}

/* XXX ldc_alloc() and ldc_free() needs to run under a mutex so
 * XXX that addition and removal from the ldc_channel_list has
 * XXX atomicity, otherwise the __ldc_channel_exists() check is
 * XXX totally pointless as another thread can slip into ldc_alloc()
 * XXX and add a channel with the same ID.  There also needs to be
 * XXX a spinlock for ldc_channel_list.
 */
static HLIST_HEAD(ldc_channel_list);

static int __ldc_channel_exists(unsigned long id)
{
	struct ldc_channel *lp;

	hlist_for_each_entry(lp, &ldc_channel_list, list) {
		if (lp->id == id)
			return 1;
	}
	return 0;
}

static int alloc_queue(const char *name, unsigned long num_entries,
		       struct ldc_packet **base, unsigned long *ra)
{
	unsigned long size, order;
	void *q;

	size = num_entries * LDC_PACKET_SIZE;
	order = get_order(size);

	q = (void *) __get_free_pages(GFP_KERNEL, order);
	if (!q) {
		printk(KERN_ERR PFX "Alloc of %s queue failed with "
		       "size=%lu order=%lu\n", name, size, order);
		return -ENOMEM;
	}

	memset(q, 0, PAGE_SIZE << order);

	*base = q;
	*ra = __pa(q);

	return 0;
}

static void free_queue(unsigned long num_entries, struct ldc_packet *q)
{
	unsigned long size, order;

	if (!q)
		return;

	size = num_entries * LDC_PACKET_SIZE;
	order = get_order(size);

	free_pages((unsigned long)q, order);
}

static unsigned long ldc_cookie_to_index(u64 cookie, void *arg)
{
	u64 szcode = cookie >> COOKIE_PGSZ_CODE_SHIFT;
	/* struct ldc_iommu *ldc_iommu = (struct ldc_iommu *)arg; */

	cookie &= ~COOKIE_PGSZ_CODE;

	return (cookie >> (13ULL + (szcode * 3ULL)));
}

static void ldc_demap(struct ldc_iommu *iommu, unsigned long id, u64 cookie,
		      unsigned long entry, unsigned long npages)
{
	struct ldc_mtable_entry *base;
	unsigned long i, shift;

	shift = (cookie >> COOKIE_PGSZ_CODE_SHIFT) * 3;
	base = iommu->page_table + entry;
	for (i = 0; i < npages; i++) {
		if (base->cookie)
			sun4v_ldc_revoke(id, cookie + (i << shift),
					 base->cookie);
		base->mte = 0;
	}
}

/* XXX Make this configurable... XXX */
#define LDC_IOTABLE_SIZE	(8 * 1024)

static int ldc_iommu_init(const char *name, struct ldc_channel *lp)
{
	unsigned long sz, num_tsb_entries, tsbsize, order;
	struct ldc_iommu *ldc_iommu = &lp->iommu;
	struct iommu_map_table *iommu = &ldc_iommu->iommu_map_table;
	struct ldc_mtable_entry *table;
	unsigned long hv_err;
	int err;

	num_tsb_entries = LDC_IOTABLE_SIZE;
	tsbsize = num_tsb_entries * sizeof(struct ldc_mtable_entry);
	spin_lock_init(&ldc_iommu->lock);

	sz = num_tsb_entries / 8;
	sz = (sz + 7UL) & ~7UL;
	iommu->map = kzalloc(sz, GFP_KERNEL);
	if (!iommu->map) {
		printk(KERN_ERR PFX "Alloc of arena map failed, sz=%lu\n", sz);
		return -ENOMEM;
	}
	iommu_tbl_pool_init(iommu, num_tsb_entries, PAGE_SHIFT,
			    NULL, false /* no large pool */,
			    1 /* npools */,
			    true /* skip span boundary check */);

	order = get_order(tsbsize);

	table = (struct ldc_mtable_entry *)
		__get_free_pages(GFP_KERNEL, order);
	err = -ENOMEM;
	if (!table) {
		printk(KERN_ERR PFX "Alloc of MTE table failed, "
		       "size=%lu order=%lu\n", tsbsize, order);
		goto out_free_map;
	}

	memset(table, 0, PAGE_SIZE << order);

	ldc_iommu->page_table = table;

	hv_err = sun4v_ldc_set_map_table(lp->id, __pa(table),
					 num_tsb_entries);
	err = -EINVAL;
	if (hv_err)
		goto out_free_table;

	return 0;

out_free_table:
	free_pages((unsigned long) table, order);
	ldc_iommu->page_table = NULL;

out_free_map:
	kfree(iommu->map);
	iommu->map = NULL;

	return err;
}

static void ldc_iommu_release(struct ldc_channel *lp)
{
	struct ldc_iommu *ldc_iommu = &lp->iommu;
	struct iommu_map_table *iommu = &ldc_iommu->iommu_map_table;
	unsigned long num_tsb_entries, tsbsize, order;

	(void) sun4v_ldc_set_map_table(lp->id, 0, 0);

	num_tsb_entries = iommu->poolsize * iommu->nr_pools;
	tsbsize = num_tsb_entries * sizeof(struct ldc_mtable_entry);
	order = get_order(tsbsize);

	free_pages((unsigned long) ldc_iommu->page_table, order);
	ldc_iommu->page_table = NULL;

	kfree(iommu->map);
	iommu->map = NULL;
}

struct ldc_channel *ldc_alloc(unsigned long id,
			      const struct ldc_channel_config *cfgp,
			      void *event_arg,
			      const char *name)
{
	struct ldc_channel *lp;
	const struct ldc_mode_ops *mops;
	unsigned long dummy1, dummy2, hv_err;
	u8 mss, *mssbuf;
	int err;

	err = -ENODEV;
	if (!ldom_domaining_enabled)
		goto out_err;

	err = -EINVAL;
	if (!cfgp)
		goto out_err;
	if (!name)
		goto out_err;

	switch (cfgp->mode) {
	case LDC_MODE_RAW:
		mops = &raw_ops;
		mss = LDC_PACKET_SIZE;
		break;

	case LDC_MODE_UNRELIABLE:
		mops = &nonraw_ops;
		mss = LDC_PACKET_SIZE - 8;
		break;

	case LDC_MODE_STREAM:
		mops = &stream_ops;
		mss = LDC_PACKET_SIZE - 8 - 8;
		break;

	default:
		goto out_err;
	}

	if (!cfgp->event || !event_arg || !cfgp->rx_irq || !cfgp->tx_irq)
		goto out_err;

	hv_err = sun4v_ldc_tx_qinfo(id, &dummy1, &dummy2);
	err = -ENODEV;
	if (hv_err == HV_ECHANNEL)
		goto out_err;

	err = -EEXIST;
	if (__ldc_channel_exists(id))
		goto out_err;

	mssbuf = NULL;

	lp = kzalloc(sizeof(*lp), GFP_KERNEL);
	err = -ENOMEM;
	if (!lp)
		goto out_err;

	spin_lock_init(&lp->lock);

	lp->id = id;

	err = ldc_iommu_init(name, lp);
	if (err)
		goto out_free_ldc;

	lp->mops = mops;
	lp->mss = mss;

	lp->cfg = *cfgp;
	if (!lp->cfg.mtu)
		lp->cfg.mtu = LDC_DEFAULT_MTU;

	if (lp->cfg.mode == LDC_MODE_STREAM) {
		mssbuf = kzalloc(lp->cfg.mtu, GFP_KERNEL);
		if (!mssbuf) {
			err = -ENOMEM;
			goto out_free_iommu;
		}
		lp->mssbuf = mssbuf;
	}

	lp->event_arg = event_arg;

	/* XXX allow setting via ldc_channel_config to override defaults
	 * XXX or use some formula based upon mtu
	 */
	lp->tx_num_entries = LDC_DEFAULT_NUM_ENTRIES;
	lp->rx_num_entries = LDC_DEFAULT_NUM_ENTRIES;

	err = alloc_queue("TX", lp->tx_num_entries,
			  &lp->tx_base, &lp->tx_ra);
	if (err)
		goto out_free_mssbuf;

	err = alloc_queue("RX", lp->rx_num_entries,
			  &lp->rx_base, &lp->rx_ra);
	if (err)
		goto out_free_txq;

	lp->flags |= LDC_FLAG_ALLOCED_QUEUES;

	lp->hs_state = LDC_HS_CLOSED;
	ldc_set_state(lp, LDC_STATE_INIT);

	INIT_HLIST_NODE(&lp->list);
	hlist_add_head(&lp->list, &ldc_channel_list);

	INIT_HLIST_HEAD(&lp->mh_list);

	snprintf(lp->rx_irq_name, LDC_IRQ_NAME_MAX, "%s RX", name);
	snprintf(lp->tx_irq_name, LDC_IRQ_NAME_MAX, "%s TX", name);

	err = request_irq(lp->cfg.rx_irq, ldc_rx, 0,
			  lp->rx_irq_name, lp);
	if (err)
		goto out_free_txq;

	err = request_irq(lp->cfg.tx_irq, ldc_tx, 0,
			  lp->tx_irq_name, lp);
	if (err) {
		free_irq(lp->cfg.rx_irq, lp);
		goto out_free_txq;
	}

	return lp;

out_free_txq:
	free_queue(lp->tx_num_entries, lp->tx_base);

out_free_mssbuf:
	kfree(mssbuf);

out_free_iommu:
	ldc_iommu_release(lp);

out_free_ldc:
	kfree(lp);

out_err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(ldc_alloc);

void ldc_unbind(struct ldc_channel *lp)
{
	if (lp->flags & LDC_FLAG_REGISTERED_IRQS) {
		free_irq(lp->cfg.rx_irq, lp);
		free_irq(lp->cfg.tx_irq, lp);
		lp->flags &= ~LDC_FLAG_REGISTERED_IRQS;
	}

	if (lp->flags & LDC_FLAG_REGISTERED_QUEUES) {
		sun4v_ldc_tx_qconf(lp->id, 0, 0);
		sun4v_ldc_rx_qconf(lp->id, 0, 0);
		lp->flags &= ~LDC_FLAG_REGISTERED_QUEUES;
	}
	if (lp->flags & LDC_FLAG_ALLOCED_QUEUES) {
		free_queue(lp->tx_num_entries, lp->tx_base);
		free_queue(lp->rx_num_entries, lp->rx_base);
		lp->flags &= ~LDC_FLAG_ALLOCED_QUEUES;
	}

	ldc_set_state(lp, LDC_STATE_INIT);
}
EXPORT_SYMBOL(ldc_unbind);

void ldc_free(struct ldc_channel *lp)
{
	ldc_unbind(lp);
	hlist_del(&lp->list);
	kfree(lp->mssbuf);
	ldc_iommu_release(lp);

	kfree(lp);
}
EXPORT_SYMBOL(ldc_free);

/* Bind the channel.  This registers the LDC queues with
 * the hypervisor and puts the channel into a pseudo-listening
 * state.  This does not initiate a handshake, ldc_connect() does
 * that.
 */
int ldc_bind(struct ldc_channel *lp)
{
	unsigned long hv_err, flags;
	int err = -EINVAL;

	if (lp->state != LDC_STATE_INIT)
		return -EINVAL;

	spin_lock_irqsave(&lp->lock, flags);

	enable_irq(lp->cfg.rx_irq);
	enable_irq(lp->cfg.tx_irq);

	lp->flags |= LDC_FLAG_REGISTERED_IRQS;

	err = -ENODEV;
	hv_err = sun4v_ldc_tx_qconf(lp->id, 0, 0);
	if (hv_err)
		goto out_free_irqs;

	hv_err = sun4v_ldc_tx_qconf(lp->id, lp->tx_ra, lp->tx_num_entries);
	if (hv_err)
		goto out_free_irqs;

	hv_err = sun4v_ldc_rx_qconf(lp->id, 0, 0);
	if (hv_err)
		goto out_unmap_tx;

	hv_err = sun4v_ldc_rx_qconf(lp->id, lp->rx_ra, lp->rx_num_entries);
	if (hv_err)
		goto out_unmap_tx;

	lp->flags |= LDC_FLAG_REGISTERED_QUEUES;

	hv_err = sun4v_ldc_tx_get_state(lp->id,
					&lp->tx_head,
					&lp->tx_tail,
					&lp->chan_state);
	err = -EBUSY;
	if (hv_err)
		goto out_unmap_rx;

	lp->tx_acked = lp->tx_head;

	lp->hs_state = LDC_HS_OPEN;
	ldc_set_state(lp, LDC_STATE_BOUND);

	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;

out_unmap_rx:
	lp->flags &= ~LDC_FLAG_REGISTERED_QUEUES;
	sun4v_ldc_rx_qconf(lp->id, 0, 0);

out_unmap_tx:
	sun4v_ldc_tx_qconf(lp->id, 0, 0);

out_free_irqs:
	lp->flags &= ~LDC_FLAG_REGISTERED_IRQS;
	free_irq(lp->cfg.tx_irq, lp);
	free_irq(lp->cfg.rx_irq, lp);

	spin_unlock_irqrestore(&lp->lock, flags);

	return err;
}
EXPORT_SYMBOL(ldc_bind);

int ldc_connect(struct ldc_channel *lp)
{
	unsigned long flags;
	int err;

	if (lp->cfg.mode == LDC_MODE_RAW)
		return -EINVAL;

	spin_lock_irqsave(&lp->lock, flags);

	if (!(lp->flags & LDC_FLAG_ALLOCED_QUEUES) ||
	    !(lp->flags & LDC_FLAG_REGISTERED_QUEUES) ||
	    lp->hs_state != LDC_HS_OPEN)
		err = ((lp->hs_state > LDC_HS_OPEN) ? 0 : -EINVAL);
	else
		err = start_handshake(lp);

	spin_unlock_irqrestore(&lp->lock, flags);

	return err;
}
EXPORT_SYMBOL(ldc_connect);

int ldc_disconnect(struct ldc_channel *lp)
{
	unsigned long hv_err, flags;
	int err;

	if (lp->cfg.mode == LDC_MODE_RAW)
		return -EINVAL;

	if (!(lp->flags & LDC_FLAG_ALLOCED_QUEUES) ||
	    !(lp->flags & LDC_FLAG_REGISTERED_QUEUES))
		return -EINVAL;

	spin_lock_irqsave(&lp->lock, flags);

	err = -ENODEV;
	hv_err = sun4v_ldc_tx_qconf(lp->id, 0, 0);
	if (hv_err)
		goto out_err;

	hv_err = sun4v_ldc_tx_qconf(lp->id, lp->tx_ra, lp->tx_num_entries);
	if (hv_err)
		goto out_err;

	hv_err = sun4v_ldc_rx_qconf(lp->id, 0, 0);
	if (hv_err)
		goto out_err;

	hv_err = sun4v_ldc_rx_qconf(lp->id, lp->rx_ra, lp->rx_num_entries);
	if (hv_err)
		goto out_err;

	ldc_set_state(lp, LDC_STATE_BOUND);
	lp->hs_state = LDC_HS_OPEN;
	lp->flags |= LDC_FLAG_RESET;

	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;

out_err:
	sun4v_ldc_tx_qconf(lp->id, 0, 0);
	sun4v_ldc_rx_qconf(lp->id, 0, 0);
	free_irq(lp->cfg.tx_irq, lp);
	free_irq(lp->cfg.rx_irq, lp);
	lp->flags &= ~(LDC_FLAG_REGISTERED_IRQS |
		       LDC_FLAG_REGISTERED_QUEUES);
	ldc_set_state(lp, LDC_STATE_INIT);

	spin_unlock_irqrestore(&lp->lock, flags);

	return err;
}
EXPORT_SYMBOL(ldc_disconnect);

int ldc_state(struct ldc_channel *lp)
{
	return lp->state;
}
EXPORT_SYMBOL(ldc_state);

static int write_raw(struct ldc_channel *lp, const void *buf, unsigned int size)
{
	struct ldc_packet *p;
	unsigned long new_tail;
	int err;

	if (size > LDC_PACKET_SIZE)
		return -EMSGSIZE;

	p = data_get_tx_packet(lp, &new_tail);
	if (!p)
		return -EAGAIN;

	memcpy(p, buf, size);

	err = send_tx_packet(lp, p, new_tail);
	if (!err)
		err = size;

	return err;
}

static int read_raw(struct ldc_channel *lp, void *buf, unsigned int size)
{
	struct ldc_packet *p;
	unsigned long hv_err, new;
	int err;

	if (size < LDC_PACKET_SIZE)
		return -EINVAL;

	hv_err = sun4v_ldc_rx_get_state(lp->id,
					&lp->rx_head,
					&lp->rx_tail,
					&lp->chan_state);
	if (hv_err)
		return ldc_abort(lp);

	if (lp->chan_state == LDC_CHANNEL_DOWN ||
	    lp->chan_state == LDC_CHANNEL_RESETTING)
		return -ECONNRESET;

	if (lp->rx_head == lp->rx_tail)
		return 0;

	p = lp->rx_base + (lp->rx_head / LDC_PACKET_SIZE);
	memcpy(buf, p, LDC_PACKET_SIZE);

	new = rx_advance(lp, lp->rx_head);
	lp->rx_head = new;

	err = __set_rx_head(lp, new);
	if (err < 0)
		err = -ECONNRESET;
	else
		err = LDC_PACKET_SIZE;

	return err;
}

static const struct ldc_mode_ops raw_ops = {
	.write		=	write_raw,
	.read		=	read_raw,
};

static int write_nonraw(struct ldc_channel *lp, const void *buf,
			unsigned int size)
{
	unsigned long hv_err, tail;
	unsigned int copied;
	u32 seq;
	int err;

	hv_err = sun4v_ldc_tx_get_state(lp->id, &lp->tx_head, &lp->tx_tail,
					&lp->chan_state);
	if (unlikely(hv_err))
		return -EBUSY;

	if (unlikely(lp->chan_state != LDC_CHANNEL_UP))
		return ldc_abort(lp);

	if (!tx_has_space_for(lp, size))
		return -EAGAIN;

	seq = lp->snd_nxt;
	copied = 0;
	tail = lp->tx_tail;
	while (copied < size) {
		struct ldc_packet *p = lp->tx_base + (tail / LDC_PACKET_SIZE);
		u8 *data = ((lp->cfg.mode == LDC_MODE_UNRELIABLE) ?
			    p->u.u_data :
			    p->u.r.r_data);
		int data_len;

		p->type = LDC_DATA;
		p->stype = LDC_INFO;
		p->ctrl = 0;

		data_len = size - copied;
		if (data_len > lp->mss)
			data_len = lp->mss;

		BUG_ON(data_len > LDC_LEN);

		p->env = (data_len |
			  (copied == 0 ? LDC_START : 0) |
			  (data_len == size - copied ? LDC_STOP : 0));

		p->seqid = ++seq;

		ldcdbg(DATA, "SENT DATA [%02x:%02x:%02x:%02x:%08x]\n",
		       p->type,
		       p->stype,
		       p->ctrl,
		       p->env,
		       p->seqid);

		memcpy(data, buf, data_len);
		buf += data_len;
		copied += data_len;

		tail = tx_advance(lp, tail);
	}

	err = set_tx_tail(lp, tail);
	if (!err) {
		lp->snd_nxt = seq;
		err = size;
	}

	return err;
}

static int rx_bad_seq(struct ldc_channel *lp, struct ldc_packet *p,
		      struct ldc_packet *first_frag)
{
	int err;

	if (first_frag)
		lp->rcv_nxt = first_frag->seqid - 1;

	err = send_data_nack(lp, p);
	if (err)
		return err;

	err = __set_rx_head(lp, lp->rx_tail);
	if (err < 0)
		return ldc_abort(lp);

	return 0;
}

static int data_ack_nack(struct ldc_channel *lp, struct ldc_packet *p)
{
	if (p->stype & LDC_ACK) {
		int err = process_data_ack(lp, p);
		if (err)
			return err;
	}
	if (p->stype & LDC_NACK)
		return ldc_abort(lp);

	return 0;
}

static int rx_data_wait(struct ldc_channel *lp, unsigned long cur_head)
{
	unsigned long dummy;
	int limit = 1000;

	ldcdbg(DATA, "DATA WAIT cur_head[%lx] rx_head[%lx] rx_tail[%lx]\n",
	       cur_head, lp->rx_head, lp->rx_tail);
	while (limit-- > 0) {
		unsigned long hv_err;

		hv_err = sun4v_ldc_rx_get_state(lp->id,
						&dummy,
						&lp->rx_tail,
						&lp->chan_state);
		if (hv_err)
			return ldc_abort(lp);

		if (lp->chan_state == LDC_CHANNEL_DOWN ||
		    lp->chan_state == LDC_CHANNEL_RESETTING)
			return -ECONNRESET;

		if (cur_head != lp->rx_tail) {
			ldcdbg(DATA, "DATA WAIT DONE "
			       "head[%lx] tail[%lx] chan_state[%lx]\n",
			       dummy, lp->rx_tail, lp->chan_state);
			return 0;
		}

		udelay(1);
	}
	return -EAGAIN;
}

static int rx_set_head(struct ldc_channel *lp, unsigned long head)
{
	int err = __set_rx_head(lp, head);

	if (err < 0)
		return ldc_abort(lp);

	lp->rx_head = head;
	return 0;
}

static void send_data_ack(struct ldc_channel *lp)
{
	unsigned long new_tail;
	struct ldc_packet *p;

	p = data_get_tx_packet(lp, &new_tail);
	if (likely(p)) {
		int err;

		memset(p, 0, sizeof(*p));
		p->type = LDC_DATA;
		p->stype = LDC_ACK;
		p->ctrl = 0;
		p->seqid = lp->snd_nxt + 1;
		p->u.r.ackid = lp->rcv_nxt;

		err = send_tx_packet(lp, p, new_tail);
		if (!err)
			lp->snd_nxt++;
	}
}

static int read_nonraw(struct ldc_channel *lp, void *buf, unsigned int size)
{
	struct ldc_packet *first_frag;
	unsigned long hv_err, new;
	int err, copied;

	hv_err = sun4v_ldc_rx_get_state(lp->id,
					&lp->rx_head,
					&lp->rx_tail,
					&lp->chan_state);
	if (hv_err)
		return ldc_abort(lp);

	if (lp->chan_state == LDC_CHANNEL_DOWN ||
	    lp->chan_state == LDC_CHANNEL_RESETTING)
		return -ECONNRESET;

	if (lp->rx_head == lp->rx_tail)
		return 0;

	first_frag = NULL;
	copied = err = 0;
	new = lp->rx_head;
	while (1) {
		struct ldc_packet *p;
		int pkt_len;

		BUG_ON(new == lp->rx_tail);
		p = lp->rx_base + (new / LDC_PACKET_SIZE);

		ldcdbg(RX, "RX read pkt[%02x:%02x:%02x:%02x:%08x:%08x] "
		       "rcv_nxt[%08x]\n",
		       p->type,
		       p->stype,
		       p->ctrl,
		       p->env,
		       p->seqid,
		       p->u.r.ackid,
		       lp->rcv_nxt);

		if (unlikely(!rx_seq_ok(lp, p->seqid))) {
			err = rx_bad_seq(lp, p, first_frag);
			copied = 0;
			break;
		}

		if (p->type & LDC_CTRL) {
			err = process_control_frame(lp, p);
			if (err < 0)
				break;
			err = 0;
		}

		lp->rcv_nxt = p->seqid;

		if (!(p->type & LDC_DATA)) {
			new = rx_advance(lp, new);
			goto no_data;
		}
		if (p->stype & (LDC_ACK | LDC_NACK)) {
			err = data_ack_nack(lp, p);
			if (err)
				break;
		}
		if (!(p->stype & LDC_INFO)) {
			new = rx_advance(lp, new);
			err = rx_set_head(lp, new);
			if (err)
				break;
			goto no_data;
		}

		pkt_len = p->env & LDC_LEN;

		/* Every initial packet starts with the START bit set.
		 *
		 * Singleton packets will have both START+STOP set.
		 *
		 * Fragments will have START set in the first frame, STOP
		 * set in the last frame, and neither bit set in middle
		 * frames of the packet.
		 *
		 * Therefore if we are at the beginning of a packet and
		 * we don't see START, or we are in the middle of a fragmented
		 * packet and do see START, we are unsynchronized and should
		 * flush the RX queue.
		 */
		if ((first_frag == NULL && !(p->env & LDC_START)) ||
		    (first_frag != NULL &&  (p->env & LDC_START))) {
			if (!first_frag)
				new = rx_advance(lp, new);

			err = rx_set_head(lp, new);
			if (err)
				break;

			if (!first_frag)
				goto no_data;
		}
		if (!first_frag)
			first_frag = p;

		if (pkt_len > size - copied) {
			/* User didn't give us a big enough buffer,
			 * what to do?  This is a pretty serious error.
			 *
			 * Since we haven't updated the RX ring head to
			 * consume any of the packets, signal the error
			 * to the user and just leave the RX ring alone.
			 *
			 * This seems the best behavior because this allows
			 * a user of the LDC layer to start with a small
			 * RX buffer for ldc_read() calls and use -EMSGSIZE
			 * as a cue to enlarge it's read buffer.
			 */
			err = -EMSGSIZE;
			break;
		}

		/* Ok, we are gonna eat this one.  */
		new = rx_advance(lp, new);

		memcpy(buf,
		       (lp->cfg.mode == LDC_MODE_UNRELIABLE ?
			p->u.u_data : p->u.r.r_data), pkt_len);
		buf += pkt_len;
		copied += pkt_len;

		if (p->env & LDC_STOP)
			break;

no_data:
		if (new == lp->rx_tail) {
			err = rx_data_wait(lp, new);
			if (err)
				break;
		}
	}

	if (!err)
		err = rx_set_head(lp, new);

	if (err && first_frag)
		lp->rcv_nxt = first_frag->seqid - 1;

	if (!err) {
		err = copied;
		if (err > 0 && lp->cfg.mode != LDC_MODE_UNRELIABLE)
			send_data_ack(lp);
	}

	return err;
}

static const struct ldc_mode_ops nonraw_ops = {
	.write		=	write_nonraw,
	.read		=	read_nonraw,
};

static int write_stream(struct ldc_channel *lp, const void *buf,
			unsigned int size)
{
	if (size > lp->cfg.mtu)
		size = lp->cfg.mtu;
	return write_nonraw(lp, buf, size);
}

static int read_stream(struct ldc_channel *lp, void *buf, unsigned int size)
{
	if (!lp->mssbuf_len) {
		int err = read_nonraw(lp, lp->mssbuf, lp->cfg.mtu);
		if (err < 0)
			return err;

		lp->mssbuf_len = err;
		lp->mssbuf_off = 0;
	}

	if (size > lp->mssbuf_len)
		size = lp->mssbuf_len;
	memcpy(buf, lp->mssbuf + lp->mssbuf_off, size);

	lp->mssbuf_off += size;
	lp->mssbuf_len -= size;

	return size;
}

static const struct ldc_mode_ops stream_ops = {
	.write		=	write_stream,
	.read		=	read_stream,
};

int ldc_write(struct ldc_channel *lp, const void *buf, unsigned int size)
{
	unsigned long flags;
	int err;

	if (!buf)
		return -EINVAL;

	if (!size)
		return 0;

	spin_lock_irqsave(&lp->lock, flags);

	if (lp->hs_state != LDC_HS_COMPLETE)
		err = -ENOTCONN;
	else
		err = lp->mops->write(lp, buf, size);

	spin_unlock_irqrestore(&lp->lock, flags);

	return err;
}
EXPORT_SYMBOL(ldc_write);

int ldc_read(struct ldc_channel *lp, void *buf, unsigned int size)
{
	unsigned long flags;
	int err;

	if (!buf)
		return -EINVAL;

	if (!size)
		return 0;

	spin_lock_irqsave(&lp->lock, flags);

	if (lp->hs_state != LDC_HS_COMPLETE)
		err = -ENOTCONN;
	else
		err = lp->mops->read(lp, buf, size);

	spin_unlock_irqrestore(&lp->lock, flags);

	return err;
}
EXPORT_SYMBOL(ldc_read);

static u64 pagesize_code(void)
{
	switch (PAGE_SIZE) {
	default:
	case (8ULL * 1024ULL):
		return 0;
	case (64ULL * 1024ULL):
		return 1;
	case (512ULL * 1024ULL):
		return 2;
	case (4ULL * 1024ULL * 1024ULL):
		return 3;
	case (32ULL * 1024ULL * 1024ULL):
		return 4;
	case (256ULL * 1024ULL * 1024ULL):
		return 5;
	}
}

static u64 make_cookie(u64 index, u64 pgsz_code, u64 page_offset)
{
	return ((pgsz_code << COOKIE_PGSZ_CODE_SHIFT) |
		(index << PAGE_SHIFT) |
		page_offset);
}


static struct ldc_mtable_entry *alloc_npages(struct ldc_iommu *iommu,
					     unsigned long npages)
{
	long entry;

	entry = iommu_tbl_range_alloc(NULL, &iommu->iommu_map_table,
				      npages, NULL, (unsigned long)-1, 0);
	if (unlikely(entry < 0))
		return NULL;

	return iommu->page_table + entry;
}

static u64 perm_to_mte(unsigned int map_perm)
{
	u64 mte_base;

	mte_base = pagesize_code();

	if (map_perm & LDC_MAP_SHADOW) {
		if (map_perm & LDC_MAP_R)
			mte_base |= LDC_MTE_COPY_R;
		if (map_perm & LDC_MAP_W)
			mte_base |= LDC_MTE_COPY_W;
	}
	if (map_perm & LDC_MAP_DIRECT) {
		if (map_perm & LDC_MAP_R)
			mte_base |= LDC_MTE_READ;
		if (map_perm & LDC_MAP_W)
			mte_base |= LDC_MTE_WRITE;
		if (map_perm & LDC_MAP_X)
			mte_base |= LDC_MTE_EXEC;
	}
	if (map_perm & LDC_MAP_IO) {
		if (map_perm & LDC_MAP_R)
			mte_base |= LDC_MTE_IOMMU_R;
		if (map_perm & LDC_MAP_W)
			mte_base |= LDC_MTE_IOMMU_W;
	}

	return mte_base;
}

static int pages_in_region(unsigned long base, long len)
{
	int count = 0;

	do {
		unsigned long new = (base + PAGE_SIZE) & PAGE_MASK;

		len -= (new - base);
		base = new;
		count++;
	} while (len > 0);

	return count;
}

struct cookie_state {
	struct ldc_mtable_entry		*page_table;
	struct ldc_trans_cookie		*cookies;
	u64				mte_base;
	u64				prev_cookie;
	u32				pte_idx;
	u32				nc;
};

static void fill_cookies(struct cookie_state *sp, unsigned long pa,
			 unsigned long off, unsigned long len)
{
	do {
		unsigned long tlen, new = pa + PAGE_SIZE;
		u64 this_cookie;

		sp->page_table[sp->pte_idx].mte = sp->mte_base | pa;

		tlen = PAGE_SIZE;
		if (off)
			tlen = PAGE_SIZE - off;
		if (tlen > len)
			tlen = len;

		this_cookie = make_cookie(sp->pte_idx,
					  pagesize_code(), off);

		off = 0;

		if (this_cookie == sp->prev_cookie) {
			sp->cookies[sp->nc - 1].cookie_size += tlen;
		} else {
			sp->cookies[sp->nc].cookie_addr = this_cookie;
			sp->cookies[sp->nc].cookie_size = tlen;
			sp->nc++;
		}
		sp->prev_cookie = this_cookie + tlen;

		sp->pte_idx++;

		len -= tlen;
		pa = new;
	} while (len > 0);
}

static int sg_count_one(struct scatterlist *sg)
{
	unsigned long base = page_to_pfn(sg_page(sg)) << PAGE_SHIFT;
	long len = sg->length;

	if ((sg->offset | len) & (8UL - 1))
		return -EFAULT;

	return pages_in_region(base + sg->offset, len);
}

static int sg_count_pages(struct scatterlist *sg, int num_sg)
{
	int count;
	int i;

	count = 0;
	for (i = 0; i < num_sg; i++) {
		int err = sg_count_one(sg + i);
		if (err < 0)
			return err;
		count += err;
	}

	return count;
}

int ldc_map_sg(struct ldc_channel *lp,
	       struct scatterlist *sg, int num_sg,
	       struct ldc_trans_cookie *cookies, int ncookies,
	       unsigned int map_perm)
{
	unsigned long i, npages;
	struct ldc_mtable_entry *base;
	struct cookie_state state;
	struct ldc_iommu *iommu;
	int err;
	struct scatterlist *s;

	if (map_perm & ~LDC_MAP_ALL)
		return -EINVAL;

	err = sg_count_pages(sg, num_sg);
	if (err < 0)
		return err;

	npages = err;
	if (err > ncookies)
		return -EMSGSIZE;

	iommu = &lp->iommu;

	base = alloc_npages(iommu, npages);

	if (!base)
		return -ENOMEM;

	state.page_table = iommu->page_table;
	state.cookies = cookies;
	state.mte_base = perm_to_mte(map_perm);
	state.prev_cookie = ~(u64)0;
	state.pte_idx = (base - iommu->page_table);
	state.nc = 0;

	for_each_sg(sg, s, num_sg, i) {
		fill_cookies(&state, page_to_pfn(sg_page(s)) << PAGE_SHIFT,
			     s->offset, s->length);
	}

	return state.nc;
}
EXPORT_SYMBOL(ldc_map_sg);

int ldc_map_single(struct ldc_channel *lp,
		   void *buf, unsigned int len,
		   struct ldc_trans_cookie *cookies, int ncookies,
		   unsigned int map_perm)
{
	unsigned long npages, pa;
	struct ldc_mtable_entry *base;
	struct cookie_state state;
	struct ldc_iommu *iommu;

	if ((map_perm & ~LDC_MAP_ALL) || (ncookies < 1))
		return -EINVAL;

	pa = __pa(buf);
	if ((pa | len) & (8UL - 1))
		return -EFAULT;

	npages = pages_in_region(pa, len);

	iommu = &lp->iommu;

	base = alloc_npages(iommu, npages);

	if (!base)
		return -ENOMEM;

	state.page_table = iommu->page_table;
	state.cookies = cookies;
	state.mte_base = perm_to_mte(map_perm);
	state.prev_cookie = ~(u64)0;
	state.pte_idx = (base - iommu->page_table);
	state.nc = 0;
	fill_cookies(&state, (pa & PAGE_MASK), (pa & ~PAGE_MASK), len);
	BUG_ON(state.nc > ncookies);

	return state.nc;
}
EXPORT_SYMBOL(ldc_map_single);


static void free_npages(unsigned long id, struct ldc_iommu *iommu,
			u64 cookie, u64 size)
{
	unsigned long npages, entry;

	npages = PAGE_ALIGN(((cookie & ~PAGE_MASK) + size)) >> PAGE_SHIFT;

	entry = ldc_cookie_to_index(cookie, iommu);
	ldc_demap(iommu, id, cookie, entry, npages);
	iommu_tbl_range_free(&iommu->iommu_map_table, cookie, npages, entry);
}

void ldc_unmap(struct ldc_channel *lp, struct ldc_trans_cookie *cookies,
	       int ncookies)
{
	struct ldc_iommu *iommu = &lp->iommu;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);
	for (i = 0; i < ncookies; i++) {
		u64 addr = cookies[i].cookie_addr;
		u64 size = cookies[i].cookie_size;

		free_npages(lp->id, iommu, addr, size);
	}
	spin_unlock_irqrestore(&iommu->lock, flags);
}
EXPORT_SYMBOL(ldc_unmap);

int ldc_copy(struct ldc_channel *lp, int copy_dir,
	     void *buf, unsigned int len, unsigned long offset,
	     struct ldc_trans_cookie *cookies, int ncookies)
{
	unsigned int orig_len;
	unsigned long ra;
	int i;

	if (copy_dir != LDC_COPY_IN && copy_dir != LDC_COPY_OUT) {
		printk(KERN_ERR PFX "ldc_copy: ID[%lu] Bad copy_dir[%d]\n",
		       lp->id, copy_dir);
		return -EINVAL;
	}

	ra = __pa(buf);
	if ((ra | len | offset) & (8UL - 1)) {
		printk(KERN_ERR PFX "ldc_copy: ID[%lu] Unaligned buffer "
		       "ra[%lx] len[%x] offset[%lx]\n",
		       lp->id, ra, len, offset);
		return -EFAULT;
	}

	if (lp->hs_state != LDC_HS_COMPLETE ||
	    (lp->flags & LDC_FLAG_RESET)) {
		printk(KERN_ERR PFX "ldc_copy: ID[%lu] Link down hs_state[%x] "
		       "flags[%x]\n", lp->id, lp->hs_state, lp->flags);
		return -ECONNRESET;
	}

	orig_len = len;
	for (i = 0; i < ncookies; i++) {
		unsigned long cookie_raddr = cookies[i].cookie_addr;
		unsigned long this_len = cookies[i].cookie_size;
		unsigned long actual_len;

		if (unlikely(offset)) {
			unsigned long this_off = offset;

			if (this_off > this_len)
				this_off = this_len;

			offset -= this_off;
			this_len -= this_off;
			if (!this_len)
				continue;
			cookie_raddr += this_off;
		}

		if (this_len > len)
			this_len = len;

		while (1) {
			unsigned long hv_err;

			hv_err = sun4v_ldc_copy(lp->id, copy_dir,
						cookie_raddr, ra,
						this_len, &actual_len);
			if (unlikely(hv_err)) {
				printk(KERN_ERR PFX "ldc_copy: ID[%lu] "
				       "HV error %lu\n",
				       lp->id, hv_err);
				if (lp->hs_state != LDC_HS_COMPLETE ||
				    (lp->flags & LDC_FLAG_RESET))
					return -ECONNRESET;
				else
					return -EFAULT;
			}

			cookie_raddr += actual_len;
			ra += actual_len;
			len -= actual_len;
			if (actual_len == this_len)
				break;

			this_len -= actual_len;
		}

		if (!len)
			break;
	}

	/* It is caller policy what to do about short copies.
	 * For example, a networking driver can declare the
	 * packet a runt and drop it.
	 */

	return orig_len - len;
}
EXPORT_SYMBOL(ldc_copy);

void *ldc_alloc_exp_dring(struct ldc_channel *lp, unsigned int len,
			  struct ldc_trans_cookie *cookies, int *ncookies,
			  unsigned int map_perm)
{
	void *buf;
	int err;

	if (len & (8UL - 1))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(len, GFP_ATOMIC);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	err = ldc_map_single(lp, buf, len, cookies, *ncookies, map_perm);
	if (err < 0) {
		kfree(buf);
		return ERR_PTR(err);
	}
	*ncookies = err;

	return buf;
}
EXPORT_SYMBOL(ldc_alloc_exp_dring);

void ldc_free_exp_dring(struct ldc_channel *lp, void *buf, unsigned int len,
			struct ldc_trans_cookie *cookies, int ncookies)
{
	ldc_unmap(lp, cookies, ncookies);
	kfree(buf);
}
EXPORT_SYMBOL(ldc_free_exp_dring);

static int __init ldc_init(void)
{
	unsigned long major, minor;
	struct mdesc_handle *hp;
	const u64 *v;
	int err;
	u64 mp;

	hp = mdesc_grab();
	if (!hp)
		return -ENODEV;

	mp = mdesc_node_by_name(hp, MDESC_NODE_NULL, "platform");
	err = -ENODEV;
	if (mp == MDESC_NODE_NULL)
		goto out;

	v = mdesc_get_property(hp, mp, "domaining-enabled", NULL);
	if (!v)
		goto out;

	major = 1;
	minor = 0;
	if (sun4v_hvapi_register(HV_GRP_LDOM, major, &minor)) {
		printk(KERN_INFO PFX "Could not register LDOM hvapi.\n");
		goto out;
	}

	printk(KERN_INFO "%s", version);

	if (!*v) {
		printk(KERN_INFO PFX "Domaining disabled.\n");
		goto out;
	}
	ldom_domaining_enabled = 1;
	err = 0;

out:
	mdesc_release(hp);
	return err;
}

core_initcall(ldc_init);
