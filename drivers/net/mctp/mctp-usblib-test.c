// SPDX-License-Identifier: GPL-2.0
/*
 * mctp-usblib-test.c - MCTP-over-USB (DMTF DSP0283) transport helper library,
 * unit test definitions.
 *
 * Copyright (C) 2026 Code Construct Pty Ltd
 */

#include <linux/array_size.h>
#include <uapi/linux/netdevice.h>
#include <linux/netdevice.h>
#include <kunit/test.h>
#include <linux/if_arp.h>
#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <linux/usb/mctp-usb.h>

#define HDR_LEN sizeof(struct mctp_usb_hdr)

struct tx_buff {
	struct list_head list;

	size_t length;
	u8 data[] __counted_by(length);
};

struct mctp_usblib_test_dev {
	struct net_device *ndev;
	struct mctp_dev *mdev;
	struct sk_buff_head rx_pkts;
};

struct mctp_usblib_test_ctx {
	struct mctp_usblib_test_dev *dev;
	struct list_head tx_xfers;
	struct mctp_route rt;
};

static int mctp_usblib_test_tx_send(struct mctp_usblib_tx_ctx *tx_ctx,
				    void *data, size_t len)
{
	struct mctp_usblib_test_ctx *ctx;
	struct tx_buff *new_node;
	struct net_device *ndev;

	ctx = mctp_usblib_tx_ctx_priv(tx_ctx);
	ndev = ctx->dev->ndev;

	new_node = kzalloc_flex(*new_node, data, len, GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;

	new_node->length = len;
	memcpy(new_node->data, data, len);
	list_add_tail(&new_node->list, &ctx->tx_xfers);

	mctp_usblib_tx_send_complete(tx_ctx, ndev, true);
	return 0;
}

static int mctp_usblib_test_tx_send_fail(struct mctp_usblib_tx_ctx *tx_ctx,
					 void *data, size_t len)
{
	return -ENOMEM;
}

static u8 *mctp_usblib_test_flatten_tx_buff(struct kunit *test,
					    struct list_head *in,
					    size_t *length_out)
{
	struct tx_buff *pos;
	size_t length;
	u8 *buf, *tail;

	KUNIT_ASSERT_TRUE(test, length_out);
	KUNIT_ASSERT_TRUE(test, in);

	length = 0;
	list_for_each_entry(pos, in, list)
		length = size_add(length, pos->length);

	KUNIT_ASSERT_NE(test, length, 0);
	KUNIT_ASSERT_NE(test, length, SIZE_MAX);

	buf = kunit_kzalloc(test, length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);

	tail = buf;
	list_for_each_entry(pos, in, list) {
		memcpy(tail, pos->data, pos->length);
		tail += pos->length;
	}

	*length_out = length;
	return buf;
}

static u8 *mctp_usblib_test_init_buf(struct kunit *test, size_t length)
{
	u8 *buffer;
	size_t i;

	buffer = kunit_kzalloc(test, length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buffer);

	for (i = 0; i < length; i++)
		buffer[i] = i % 256;

	return buffer;
}

static void mctp_usblib_test_fill_head(struct mctp_usb_hdr *head, size_t len)
{
	len += HDR_LEN;
	head->id = cpu_to_be16(MCTP_USB_DMTF_ID);
	head->len = cpu_to_be16(len & MCTP_USB_1_1_PKTLEN_MAX);
}

static struct sk_buff *mctp_usblib_test_init_skb(struct kunit *test,
						 unsigned int length,
						 struct net_device *ndev,
						 void *data)
{
	struct sk_buff *skb;

	skb = __netdev_alloc_skb(ndev, length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, skb);

	skb_put_data(skb, data, length);
	return skb;
}

static netdev_tx_t mctp_usblib_dev_tx(struct sk_buff *skb,
				      struct net_device *ndev)
{
	/* we don't track any TXed packets at present */
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops mctp_test_netdev_ops = {
	.ndo_start_xmit = mctp_usblib_dev_tx,
};

static const u16 ep_maxpacket = 512;
static const mctp_eid_t local_eid = 8;

static void mctp_usblib_dev_setup(struct net_device *ndev)
{
	ndev->type = ARPHRD_MCTP;
	ndev->mtu = 8192;
	ndev->flags = IFF_NOARP;
	ndev->netdev_ops = &mctp_test_netdev_ops;
	ndev->needs_free_netdev = true;
	ndev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;
}

static void mctp_usblib_test_dev_action(void *data)
{
	struct mctp_usblib_test_dev *dev = data;

	skb_queue_purge(&dev->rx_pkts);
	if (dev->mdev)
		mctp_dev_put(dev->mdev);
	unregister_netdev(dev->ndev);
}

static struct mctp_usblib_test_dev *
mctp_usblib_test_create_dev(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct net_device *ndev;
	int rc;

	ndev = alloc_netdev(sizeof(*dev), "mctptest%d", NET_NAME_ENUM,
			    mctp_usblib_dev_setup);
	if (!ndev)
		return NULL;

	dev = netdev_priv(ndev);
	dev->ndev = ndev;
	skb_queue_head_init(&dev->rx_pkts);

	rc = register_netdev(ndev);
	if (rc) {
		free_netdev(ndev);
		return NULL;
	}

	rc = kunit_add_action_or_reset(test, mctp_usblib_test_dev_action, dev);
	if (rc)
		return NULL;

	rcu_read_lock();
	dev->mdev = __mctp_dev_get(ndev);
	if (dev->mdev)
		dev->mdev->net = mctp_default_net(dev_net(ndev));
	rcu_read_unlock();

	if (!dev->mdev)
		return NULL;

	rtnl_lock();
	rc = dev_open(ndev, NULL);
	rtnl_unlock();
	if (rc)
		return NULL;

	return dev;
}

static int mctp_usblib_test_dst_output(struct mctp_dst *dst,
				       struct sk_buff *skb)
{
	struct mctp_usblib_test_dev *dev = netdev_priv(skb->dev);

	skb_queue_tail(&dev->rx_pkts, skb);

	return 0;
}

static void mctp_usblib_test_fini_action(void *data)
{
	struct mctp_usblib_test_ctx *ctx = data;
	struct tx_buff *curr, *temp;

	/* The device will have been destroyed, so ->rt will be unlinked.
	 * Just ensure that the refcount is as expected.
	 */
	KUNIT_EXPECT_TRUE(current->kunit_test,
			  refcount_dec_and_test(&ctx->rt.refs));

	list_for_each_entry_safe(curr, temp, &ctx->tx_xfers, list)
		kfree(curr);
	kfree(ctx);
}

static struct mctp_usblib_test_ctx *mctp_usblib_test_init(struct kunit *test)
{
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_route *rt;
	int rc;

	ctx = kzalloc_obj(*ctx);
	KUNIT_ASSERT_NOT_NULL(test, ctx);

	INIT_LIST_HEAD(&ctx->rt.list);
	rt = &ctx->rt;
	refcount_set(&rt->refs, 1);
	INIT_LIST_HEAD(&ctx->tx_xfers);

	rc = kunit_add_action_or_reset(test, mctp_usblib_test_fini_action, ctx);
	KUNIT_ASSERT_EQ(test, rc, 0);

	ctx->dev = mctp_usblib_test_create_dev(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx->dev);

	rt->min = local_eid;
	rt->max = local_eid;
	rt->dst_type = MCTP_ROUTE_DIRECT;
	rt->type = RTN_LOCAL;
	rt->dev = ctx->dev->mdev;
	rt->output = mctp_usblib_test_dst_output;

	rtnl_lock();
	list_add_rcu(&ctx->rt.list, &init_net.mctp.routes);
	refcount_inc(&rt->refs);
	rtnl_unlock();

	return ctx;
}

/* Init a MCTP-over-USB packet within a buffer. @len is the length of the
 * buffer to write, @payload_len is the reported size of the MCTP-over-USB
 * packet.
 */
static void mctp_usblib_test_init_pkt(void *data, size_t len,
				      size_t payload_len)
{
	struct {
		struct mctp_usb_hdr usb;
		struct mctp_hdr mctp;
	} hdr;

	hdr.usb.id = cpu_to_be16(MCTP_USB_DMTF_ID);
	hdr.usb.len = cpu_to_be16(payload_len);
	hdr.mctp.ver = 1;
	hdr.mctp.dest = local_eid;
	hdr.mctp.src = 0;
	hdr.mctp.flags_seq_tag = 0;

	memcpy(data, &hdr, min(len, sizeof(hdr)));
	if (len > sizeof(hdr))
		memset(data + sizeof(hdr), 0, len - sizeof(hdr));
}

static void action_rx_fini(void *data)
{
	struct mctp_usblib_rx *rx = data;

	mctp_usblib_rx_fini(rx);
	kfree(rx);
}

static struct mctp_usblib_rx *
mctp_usblib_test_rx_init(struct kunit *test, bool span)
{
	struct mctp_usblib_rx *rx;
	int rc;

	rx = kzalloc_obj(*rx);
	KUNIT_ASSERT_NOT_NULL(test, rx);
	rc = kunit_add_action_or_reset(test, action_rx_fini, rx);
	KUNIT_ASSERT_EQ(test, rc, 0);

	rc = mctp_usblib_rx_init(rx, ep_maxpacket, span);
	KUNIT_ASSERT_EQ(test, rc, 0);

	return rx;
}

/* Wrappers for usblib's rx_complete callback, which is intended to be called
 * from atomic context
 */
static int mctp_usblib_test_rx_complete(struct net_device *netdev,
					struct mctp_usblib_rx *rx, size_t len)
{
	int rc;

	local_bh_disable();
	rc = mctp_usblib_rx_complete(netdev, rx, len);
	local_bh_enable();

	return rc;
}

static void action_tx_fini(void *data)
{
	struct mctp_usblib_tx *tx = data;

	mctp_usblib_tx_fini(tx);
	kfree(tx);
}

static struct mctp_usblib_tx *
mctp_usblib_test_tx_init(struct kunit *test,
			 const struct mctp_usblib_tx_ops *ops,
			 void *priv, bool span)
{
	struct mctp_usblib_tx *tx;
	int rc;

	tx = kzalloc_obj(*tx);
	KUNIT_ASSERT_NOT_NULL(test, tx);
	rc = kunit_add_action_or_reset(test, action_tx_fini, tx);
	KUNIT_ASSERT_EQ(test, rc, 0);

	mctp_usblib_tx_init(tx, ops, priv, span);

	return tx;
}

/* Single packet, starting on a transfer boundary, contained entirely within
 * the transfer
 */
static void mctp_usblib_test_rx_single(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	struct sk_buff *skb;
	size_t len;
	void *buf;
	int rc;

	ctx = mctp_usblib_test_init(test);
	dev = ctx->dev;

	rx = mctp_usblib_test_rx_init(test, true);

	rc = mctp_usblib_rx_prepare(dev->ndev, rx,
				    &buf, &len, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);

	/* we should always have a maxpacket of transfer available */
	KUNIT_ASSERT_GE(test, len, ep_maxpacket);

	mctp_usblib_test_init_pkt(buf, 8, 8);

	rc = mctp_usblib_test_rx_complete(dev->ndev, rx, 8);
	KUNIT_ASSERT_EQ(test, rc, 0);

	skb = __skb_dequeue(&dev->rx_pkts);
	KUNIT_EXPECT_NOT_NULL(test, skb);
	if (skb)
		KUNIT_EXPECT_EQ(test, skb->len, 4);
	kfree_skb(skb);
}

struct mctp_usblib_test_pkt_span {
	const char *name;
	size_t n_pkts;
	size_t pkts[6];
	size_t n_xfers;
	size_t xfers[6];
};

static void
mctp_usblib_test_pkt_span_to_desc(const struct mctp_usblib_test_pkt_span *t,
				  char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

static void
mctp_usblib_test_pkt_span_validate(struct kunit *test,
				   const struct mctp_usblib_test_pkt_span *span,
				   size_t *len)
{
	size_t pkt_len = 0, xfer_len = 0;
	unsigned int i;

	for (i = 0; i < span->n_pkts; i++) {
		KUNIT_ASSERT_GE_MSG(test, span->pkts[i], 8,
				    "pkt[%u] len too small (%zu) for %s",
				    i, span->pkts[i], span->name);
		pkt_len += span->pkts[i];
	}

	for (i = 0; i < span->n_xfers; i++)
		xfer_len += span->xfers[i];

	KUNIT_ASSERT_EQ_MSG(test, pkt_len, xfer_len,
			    "invalid pkt_len (%zu) != xfer_len (%zu) for %s",
			    pkt_len, xfer_len, span->name);

	*len = pkt_len;
}

static void mctp_usblib_test_rx_pkt_span(struct kunit *test)
{
	const struct mctp_usblib_test_pkt_span *pkt_span = test->param_value;
	size_t len, xfer_len, off, xfer_off;
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	unsigned int i;
	u8 *pktbuf;
	void *buf;
	int rc;

	mctp_usblib_test_pkt_span_validate(test, pkt_span, &len);
	pktbuf = kunit_kmalloc_array(test, 1, len, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pktbuf);

	/* lay out packets */
	for (off = 0, i = 0; i < pkt_span->n_pkts; i++) {
		len = pkt_span->pkts[i];
		mctp_usblib_test_init_pkt(pktbuf + off, len, len);
		off += len;
	}

	ctx = mctp_usblib_test_init(test);
	dev = ctx->dev;

	rx = mctp_usblib_test_rx_init(test, true);

	/* feed transfers */
	for (off = 0, xfer_off = 0, i = 0; i < pkt_span->n_xfers;) {
		xfer_len = pkt_span->xfers[i] - xfer_off;
		rc = mctp_usblib_rx_prepare(dev->ndev, rx,
					    &buf, &len, GFP_KERNEL);
		KUNIT_ASSERT_EQ(test, rc, 0);

		KUNIT_ASSERT_GE(test, len, ep_maxpacket);

		len = min(len, xfer_len);
		memcpy(buf, pktbuf + off, len);

		if (len == xfer_len) {
			/* whole/end xfer, proceed to next */
			xfer_off = 0;
			i++;
		} else {
			/* partial */
			xfer_off += len;
		}

		rc = mctp_usblib_test_rx_complete(dev->ndev, rx, len);
		KUNIT_ASSERT_EQ(test, rc, 0);
		off += len;
	}

	/* check received packets */
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, pkt_span->n_pkts);
	for (i = 0; ; i++) {
		struct sk_buff *skb = __skb_dequeue(&dev->rx_pkts);

		if (!skb)
			break;

		if (i < pkt_span->n_pkts)
			KUNIT_EXPECT_EQ(test, skb->len, pkt_span->pkts[i] - 4);

		kfree_skb(skb);
	}
}

static const struct mctp_usblib_test_pkt_span mctp_usblib_test_pkt_spans[] = {
	/* One packet completely within a transfer */
	{ "1p1x-complete", 1, { 8 }, 1, { 8 } },
	/* Two small packets combined within one transfer */
	{ "2p1x-combined", 2, { 8, 8 }, 1, { 16 } },
	/* Single packet split over 3 transfers, middle entirely continuation */
	{ "1p3x-split", 1, { 12 }, 3, { 4, 4, 4 } },
	/* A packet split over 5 transfers, splitting on and between each header. */
	{ "1p5x-split", 1, { 12 }, 5, { 3, 1, 1, 3, 4}},
	/* Max-sized single transfer */
	{ "1p1x-large", 1, { 8191 }, 1, { 8191 } },
	/* Two large packets, split at the worst-case for allocation, with a
	 * single byte continuing the span
	 */
	{ "2p2x-large-split", 2, { 8190, 8191 }, 2, { 8191, 8190 } },
	/* Three large packets, split at the worst-case for allocation,
	 * with a single byte continuing each span
	 */
	{ "3p3x-large-split", 3, { 8190, 8191, 8191 }, 3, { 8191, 8191, 8190 } },
};

KUNIT_ARRAY_PARAM(mctp_usblib_test_rx_pkt_span, mctp_usblib_test_pkt_spans,
		  mctp_usblib_test_pkt_span_to_desc);

static void mctp_usblib_test_rx_split_header(struct kunit *test, size_t offset,
					     struct mctp_usblib_test_dev *dev,
					     struct mctp_usblib_rx *rx)
{
	struct sk_buff *skb;
	size_t buflen, len;
	u8 packet[16];
	void *buf;
	int rc;

	len = sizeof(packet);
	mctp_usblib_test_init_pkt(packet, len, len);

	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);

	memcpy(buf, packet, offset);
	mctp_usblib_test_rx_complete(dev->ndev, rx, offset);

	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen,
				    GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);
	KUNIT_ASSERT_EQ(test, dev->rx_pkts.qlen, 0);

	memcpy(buf, packet + offset, len - offset);
	rc = mctp_usblib_test_rx_complete(dev->ndev, rx, len - offset);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, 1);

	skb = __skb_dequeue(&dev->rx_pkts);
	KUNIT_EXPECT_NOT_NULL(test, skb);
	if (skb) {
		KUNIT_EXPECT_EQ(test, skb->len, len - HDR_LEN);
		kfree_skb(skb);
	}
}

static void mctp_usblib_test_rx_header_splits(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;

	ctx = mctp_usblib_test_init(test);
	rx = mctp_usblib_test_rx_init(test, true);
	dev = ctx->dev;

	/* Unrolling here so stack traces point to the invocation with the
	 * failing length.
	 */
	mctp_usblib_test_rx_split_header(test, 1, dev, rx);
	mctp_usblib_test_rx_split_header(test, 2, dev, rx);
	mctp_usblib_test_rx_split_header(test, 3, dev, rx);
	mctp_usblib_test_rx_split_header(test, 4, dev, rx);
	mctp_usblib_test_rx_split_header(test, 5, dev, rx);
	mctp_usblib_test_rx_split_header(test, 6, dev, rx);
	mctp_usblib_test_rx_split_header(test, 7, dev, rx);
	mctp_usblib_test_rx_split_header(test, 8, dev, rx);
}

/* Test the submission of a packet with an impossibly small value in the
 * header's length field. Values less than HDR_LEN are invalid.
 */
static void mctp_usblib_test_rx_short_packet(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	size_t len, buflen;
	u8 pktbuf[12];
	void *buf;
	int rc;

	ctx = mctp_usblib_test_init(test);
	rx = mctp_usblib_test_rx_init(test, true);
	dev = ctx->dev;

	len = sizeof(pktbuf);
	mctp_usblib_test_init_pkt(pktbuf, len, HDR_LEN - 1);

	buflen = 0;
	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);

	memcpy(buf, pktbuf, len);

	rc = mctp_usblib_rx_complete(dev->ndev, rx, len);
	KUNIT_EXPECT_EQ(test, rc, -EPROTO);
	KUNIT_EXPECT_NULL(test, rx->skb);
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, 0);
}

static void mctp_usblib_test_rx_invalid_dmtf_id(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	size_t len, buflen;
	u8 pktbuf[12];
	void *buf;
	int rc;

	ctx = mctp_usblib_test_init(test);
	rx = mctp_usblib_test_rx_init(test, true);
	dev = ctx->dev;

	len = sizeof(pktbuf);
	mctp_usblib_test_init_pkt(pktbuf, len, len);

	/* Make packet DMTF ID invalid */
	pktbuf[1] = ~pktbuf[1];

	buflen = 0;
	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);

	memcpy(buf, pktbuf, len);

	rc = mctp_usblib_rx_complete(dev->ndev, rx, len);
	KUNIT_EXPECT_EQ(test, rc, -EPROTO);
	KUNIT_EXPECT_NULL(test, rx->skb);
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, 0);
}

static void mctp_usblib_test_rx_nonspanning_tiny(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	size_t len, buflen;
	u8 pktbuf[3];
	void *buf;
	int rc;

	ctx = mctp_usblib_test_init(test);
	rx = mctp_usblib_test_rx_init(test, false);
	dev = ctx->dev;

	len = sizeof(pktbuf);
	mctp_usblib_test_init_pkt(pktbuf, len, len);

	buflen = 0;
	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);

	memcpy(buf, pktbuf, len);

	rc = mctp_usblib_rx_complete(dev->ndev, rx, len);
	KUNIT_EXPECT_EQ(test, rc, -ENOMSG);
	KUNIT_EXPECT_NULL(test, rx->skb);
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, 0);
}

static void mctp_usblib_test_rx_nonspanning_partial(struct kunit *test)
{
	struct mctp_usblib_test_dev *dev;
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_rx *rx;
	size_t len, buflen;
	u8 pktbuf[20];
	void *buf;
	int rc;

	ctx = mctp_usblib_test_init(test);
	rx = mctp_usblib_test_rx_init(test, false);
	dev = ctx->dev;

	len = sizeof(pktbuf);
	mctp_usblib_test_init_pkt(pktbuf, len, len + 1);

	buflen = 0;
	rc = mctp_usblib_rx_prepare(dev->ndev, rx, &buf, &buflen, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_GE(test, buflen, len);

	memcpy(buf, pktbuf, len);

	rc = mctp_usblib_rx_complete(dev->ndev, rx, len);
	KUNIT_EXPECT_EQ(test, rc, -EPROTO);
	KUNIT_EXPECT_NULL(test, rx->skb);
	KUNIT_EXPECT_EQ(test, dev->rx_pkts.qlen, 0);
}

static void mctp_usblib_test_tx_pkt_span(struct kunit *test)
{
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_tx_ops ops;
	struct mctp_usblib_tx *tx;
	struct mctp_usb_hdr head;
	struct net_device *ndev;
	struct sk_buff *skb;
	size_t len, tx_len;
	u8 *buf, *flat_tx;
	int rc;

	len = 1000;

	ctx = mctp_usblib_test_init(test);
	ndev = ctx->dev->ndev;

	ops.send = mctp_usblib_test_tx_send;

	tx = mctp_usblib_test_tx_init(test, &ops, ctx, true);

	buf = mctp_usblib_test_init_buf(test, len);
	mctp_usblib_test_fill_head(&head, len);

	skb = mctp_usblib_test_init_skb(test, len, ndev, buf);

	rc = mctp_usblib_tx_push(ndev, tx, skb, false);
	KUNIT_ASSERT_EQ(test, rc, 0);
	KUNIT_ASSERT_FALSE(test, list_empty(&ctx->tx_xfers));

	flat_tx = mctp_usblib_test_flatten_tx_buff(test, &ctx->tx_xfers,
						   &tx_len);
	KUNIT_ASSERT_NOT_NULL(test, flat_tx);

	KUNIT_EXPECT_EQ(test, tx_len, len + HDR_LEN);
	KUNIT_EXPECT_MEMEQ(test, flat_tx, &head, HDR_LEN);
	KUNIT_EXPECT_MEMEQ(test, flat_tx + HDR_LEN, buf, len);
}

static void mctp_usblib_test_tx_failing_send(struct kunit *test)
{
	struct mctp_usblib_test_ctx *ctx;
	struct mctp_usblib_tx_ops ops;
	struct mctp_usblib_tx *tx;
	struct net_device *ndev;
	struct sk_buff *skb;
	size_t len;
	u8 *buf;
	int rc;

	len = 100;

	ctx = mctp_usblib_test_init(test);
	ndev = ctx->dev->ndev;

	ops.send = mctp_usblib_test_tx_send_fail;

	tx = mctp_usblib_test_tx_init(test, &ops, ctx, false);
	buf = mctp_usblib_test_init_buf(test, len);
	skb = mctp_usblib_test_init_skb(test, len, ndev, buf);

	/* Doesn't call ops.send as more packets are expected,
	 * so the push shouldn't fail.
	 */
	rc = mctp_usblib_tx_push(ndev, tx, skb, true);
	KUNIT_ASSERT_EQ(test, rc, 0);

	skb = mctp_usblib_test_init_skb(test, len, ndev, buf);

	/* Calls ops.send as no further packets are expected. */
	rc = mctp_usblib_tx_push(ndev, tx, skb, false);
	KUNIT_EXPECT_EQ(test, rc, 0);
	KUNIT_EXPECT_NULL(test, tx->cur_ctx);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->tx_xfers));
}

/* Test sending multiple packets in the same transfer, followed by one that
 * spans multiple subsequent transfers.
 */
static void mctp_usblib_test_tx_multi_push(struct kunit *test)
{
	struct mctp_usblib_test_ctx *ctx;
	size_t i, max_length, tx_length;
	struct mctp_usblib_tx_ops ops;
	u8 *buf, *flat_tx, *index;
	struct mctp_usblib_tx *tx;
	struct net_device *ndev;
	struct sk_buff *skb;
	const struct {
		size_t len;
		bool more;
	} sends[] = {
		{ 1000, true  },
		{  500, false },
		{ 5000, false },
	};
	int rc;

	static_assert(!sends[ARRAY_SIZE(sends) - 1].more,
		      "The last push must claim there will be no more");

	max_length = 0;
	for (i = 0; i < ARRAY_SIZE(sends); i++) {
		if (sends[i].len > max_length)
			max_length = sends[i].len;
	}

	ctx = mctp_usblib_test_init(test);
	ndev = ctx->dev->ndev;

	ops.send = mctp_usblib_test_tx_send;

	tx = mctp_usblib_test_tx_init(test, &ops, ctx, true);
	buf = mctp_usblib_test_init_buf(test, max_length);

	for (i = 0; i < ARRAY_SIZE(sends); i++) {
		skb = mctp_usblib_test_init_skb(test, sends[i].len, ndev, buf);

		rc = mctp_usblib_tx_push(ndev, tx, skb, sends[i].more);
		KUNIT_ASSERT_EQ(test, rc, 0);
	}
	KUNIT_ASSERT_FALSE(test, list_empty(&ctx->tx_xfers));

	flat_tx = mctp_usblib_test_flatten_tx_buff(test, &ctx->tx_xfers,
						   &tx_length);

	for (i = 0, index = flat_tx; i < ARRAY_SIZE(sends); i++) {
		size_t length_to_check, remaining_bytes;
		struct mctp_usb_hdr head;

		if (index - flat_tx >= tx_length - HDR_LEN)
			break;

		mctp_usblib_test_fill_head(&head, sends[i].len);
		KUNIT_EXPECT_MEMEQ(test, index, &head, HDR_LEN);
		index += HDR_LEN;
		remaining_bytes = tx_length - (index - flat_tx);

		length_to_check = sends[i].len;
		KUNIT_EXPECT_GE(test, remaining_bytes, length_to_check);
		length_to_check = min(remaining_bytes, length_to_check);

		KUNIT_EXPECT_MEMEQ(test, index,
				   buf, length_to_check);

		index += length_to_check;
	}
	KUNIT_EXPECT_EQ(test, i, ARRAY_SIZE(sends));
}

static void mctp_usblib_test_append_failure(struct kunit *test)
{
	struct mctp_usblib_test_ctx *ctx;
	struct rtnl_link_stats64 stats;
	struct mctp_usblib_tx_ops ops;
	struct mctp_usblib_tx *tx;
	struct net_device *ndev;
	struct sk_buff *skb;
	u64 tx_dropped;
	size_t i;
	u8 *buf;
	int rc;

	const size_t packet_count = 3;
	const size_t len = (MCTP_USB_1_0_XFER_SIZE / packet_count) + 1
			   - sizeof(struct mctp_usb_hdr);
	static_assert(len >= MCTP_USB_BTU);

	ctx = mctp_usblib_test_init(test);
	ndev = ctx->dev->ndev;

	ops.send = mctp_usblib_test_tx_send_fail;

	tx = mctp_usblib_test_tx_init(test, &ops, ctx, false);
	buf = mctp_usblib_test_init_buf(test, len);

	for (i = 0; i < packet_count; i++) {
		tx_dropped = dev_get_stats(ndev, &stats)->tx_dropped;
		KUNIT_EXPECT_EQ(test, tx_dropped, 0);
		skb = mctp_usblib_test_init_skb(test, len, ndev, buf);

		rc = mctp_usblib_tx_push(ndev, tx, skb, i != packet_count - 1);
		KUNIT_EXPECT_EQ(test, rc, 0);
		KUNIT_EXPECT_TRUE(test, list_empty(&ctx->tx_xfers));
	}

	tx_dropped = dev_get_stats(ndev, &stats)->tx_dropped;
	KUNIT_EXPECT_EQ(test, tx_dropped, packet_count);
	KUNIT_EXPECT_NULL(test, tx->cur_ctx);
}

static struct kunit_case mctp_usblib_test_cases[] = {
	KUNIT_CASE(mctp_usblib_test_rx_single),
	KUNIT_CASE_PARAM(mctp_usblib_test_rx_pkt_span,
			 mctp_usblib_test_rx_pkt_span_gen_params),
	KUNIT_CASE(mctp_usblib_test_rx_header_splits),
	KUNIT_CASE(mctp_usblib_test_rx_short_packet),
	KUNIT_CASE(mctp_usblib_test_rx_invalid_dmtf_id),
	KUNIT_CASE(mctp_usblib_test_rx_nonspanning_tiny),
	KUNIT_CASE(mctp_usblib_test_rx_nonspanning_partial),
	KUNIT_CASE(mctp_usblib_test_tx_pkt_span),
	KUNIT_CASE(mctp_usblib_test_tx_multi_push),
	KUNIT_CASE(mctp_usblib_test_tx_failing_send),
	KUNIT_CASE(mctp_usblib_test_append_failure),
	{}
};

static struct kunit_suite mctp_usblib_test_suite = {
	.name = "mctp-usblib",
	.test_cases = mctp_usblib_test_cases,
};

kunit_test_suite(mctp_usblib_test_suite);
