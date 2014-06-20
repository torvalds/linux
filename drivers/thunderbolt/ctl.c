/*
 * Thunderbolt Cactus Ridge driver - control channel and configuration commands
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dmapool.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>

#include "ctl.h"


struct ctl_pkg {
	struct tb_ctl *ctl;
	void *buffer;
	struct ring_frame frame;
};

#define TB_CTL_RX_PKG_COUNT 10

/**
 * struct tb_cfg - thunderbolt control channel
 */
struct tb_ctl {
	struct tb_nhi *nhi;
	struct tb_ring *tx;
	struct tb_ring *rx;

	struct dma_pool *frame_pool;
	struct ctl_pkg *rx_packets[TB_CTL_RX_PKG_COUNT];
	DECLARE_KFIFO(response_fifo, struct ctl_pkg*, 16);
	struct completion response_ready;

	hotplug_cb callback;
	void *callback_data;
};


#define tb_ctl_WARN(ctl, format, arg...) \
	dev_WARN(&(ctl)->nhi->pdev->dev, format, ## arg)

#define tb_ctl_err(ctl, format, arg...) \
	dev_err(&(ctl)->nhi->pdev->dev, format, ## arg)

#define tb_ctl_warn(ctl, format, arg...) \
	dev_warn(&(ctl)->nhi->pdev->dev, format, ## arg)

#define tb_ctl_info(ctl, format, arg...) \
	dev_info(&(ctl)->nhi->pdev->dev, format, ## arg)


/* configuration packets definitions */

enum tb_cfg_pkg_type {
	TB_CFG_PKG_READ = 1,
	TB_CFG_PKG_WRITE = 2,
	TB_CFG_PKG_ERROR = 3,
	TB_CFG_PKG_NOTIFY_ACK = 4,
	TB_CFG_PKG_EVENT = 5,
	TB_CFG_PKG_XDOMAIN_REQ = 6,
	TB_CFG_PKG_XDOMAIN_RESP = 7,
	TB_CFG_PKG_OVERRIDE = 8,
	TB_CFG_PKG_RESET = 9,
	TB_CFG_PKG_PREPARE_TO_SLEEP = 0xd,
};

/* common header */
struct tb_cfg_header {
	u32 route_hi:22;
	u32 unknown:10; /* highest order bit is set on replies */
	u32 route_lo;
} __packed;

/* additional header for read/write packets */
struct tb_cfg_address {
	u32 offset:13; /* in dwords */
	u32 length:6; /* in dwords */
	u32 port:6;
	enum tb_cfg_space space:2;
	u32 seq:2; /* sequence number  */
	u32 zero:3;
} __packed;

/* TB_CFG_PKG_READ, response for TB_CFG_PKG_WRITE */
struct cfg_read_pkg {
	struct tb_cfg_header header;
	struct tb_cfg_address addr;
} __packed;

/* TB_CFG_PKG_WRITE, response for TB_CFG_PKG_READ */
struct cfg_write_pkg {
	struct tb_cfg_header header;
	struct tb_cfg_address addr;
	u32 data[64]; /* maximum size, tb_cfg_address.length has 6 bits */
} __packed;

/* TB_CFG_PKG_ERROR */
struct cfg_error_pkg {
	struct tb_cfg_header header;
	enum tb_cfg_error error:4;
	u32 zero1:4;
	u32 port:6;
	u32 zero2:2; /* Both should be zero, still they are different fields. */
	u32 zero3:16;
} __packed;

/* TB_CFG_PKG_EVENT */
struct cfg_event_pkg {
	struct tb_cfg_header header;
	u32 port:6;
	u32 zero:25;
	bool unplug:1;
} __packed;

/* TB_CFG_PKG_RESET */
struct cfg_reset_pkg {
	struct tb_cfg_header header;
} __packed;

/* TB_CFG_PKG_PREPARE_TO_SLEEP */
struct cfg_pts_pkg {
	struct tb_cfg_header header;
	u32 data;
} __packed;


/* utility functions */

static u64 get_route(struct tb_cfg_header header)
{
	return (u64) header.route_hi << 32 | header.route_lo;
}

static struct tb_cfg_header make_header(u64 route)
{
	struct tb_cfg_header header = {
		.route_hi = route >> 32,
		.route_lo = route,
	};
	/* check for overflow, route_hi is not 32 bits! */
	WARN_ON(get_route(header) != route);
	return header;
}

static int check_header(struct ctl_pkg *pkg, u32 len, enum tb_cfg_pkg_type type,
			u64 route)
{
	struct tb_cfg_header *header = pkg->buffer;

	/* check frame, TODO: frame flags */
	if (WARN(len != pkg->frame.size,
			"wrong framesize (expected %#x, got %#x)\n",
			len, pkg->frame.size))
		return -EIO;
	if (WARN(type != pkg->frame.eof, "wrong eof (expected %#x, got %#x)\n",
			type, pkg->frame.eof))
		return -EIO;
	if (WARN(pkg->frame.sof, "wrong sof (expected 0x0, got %#x)\n",
			pkg->frame.sof))
		return -EIO;

	/* check header */
	if (WARN(header->unknown != 1 << 9,
			"header->unknown is %#x\n", header->unknown))
		return -EIO;
	if (WARN(route != get_route(*header),
			"wrong route (expected %llx, got %llx)",
			route, get_route(*header)))
		return -EIO;
	return 0;
}

static int check_config_address(struct tb_cfg_address addr,
				enum tb_cfg_space space, u32 offset,
				u32 length)
{
	if (WARN(addr.zero, "addr.zero is %#x\n", addr.zero))
		return -EIO;
	if (WARN(space != addr.space, "wrong space (expected %x, got %x\n)",
			space, addr.space))
		return -EIO;
	if (WARN(offset != addr.offset, "wrong offset (expected %x, got %x\n)",
			offset, addr.offset))
		return -EIO;
	if (WARN(length != addr.length, "wrong space (expected %x, got %x\n)",
			length, addr.length))
		return -EIO;
	if (WARN(addr.seq, "addr.seq is %#x\n", addr.seq))
		return -EIO;
	/*
	 * We cannot check addr->port as it is set to the upstream port of the
	 * sender.
	 */
	return 0;
}

static struct tb_cfg_result decode_error(struct ctl_pkg *response)
{
	struct cfg_error_pkg *pkg = response->buffer;
	struct tb_cfg_result res = { 0 };
	res.response_route = get_route(pkg->header);
	res.response_port = 0;
	res.err = check_header(response, sizeof(*pkg), TB_CFG_PKG_ERROR,
			       get_route(pkg->header));
	if (res.err)
		return res;

	WARN(pkg->zero1, "pkg->zero1 is %#x\n", pkg->zero1);
	WARN(pkg->zero2, "pkg->zero1 is %#x\n", pkg->zero1);
	WARN(pkg->zero3, "pkg->zero1 is %#x\n", pkg->zero1);
	res.err = 1;
	res.tb_error = pkg->error;
	res.response_port = pkg->port;
	return res;

}

static struct tb_cfg_result parse_header(struct ctl_pkg *pkg, u32 len,
					 enum tb_cfg_pkg_type type, u64 route)
{
	struct tb_cfg_header *header = pkg->buffer;
	struct tb_cfg_result res = { 0 };

	if (pkg->frame.eof == TB_CFG_PKG_ERROR)
		return decode_error(pkg);

	res.response_port = 0; /* will be updated later for cfg_read/write */
	res.response_route = get_route(*header);
	res.err = check_header(pkg, len, type, route);
	return res;
}

static void tb_cfg_print_error(struct tb_ctl *ctl,
			       const struct tb_cfg_result *res)
{
	WARN_ON(res->err != 1);
	switch (res->tb_error) {
	case TB_CFG_ERROR_PORT_NOT_CONNECTED:
		/* Port is not connected. This can happen during surprise
		 * removal. Do not warn. */
		return;
	case TB_CFG_ERROR_INVALID_CONFIG_SPACE:
		/*
		 * Invalid cfg_space/offset/length combination in
		 * cfg_read/cfg_write.
		 */
		tb_ctl_WARN(ctl,
			"CFG_ERROR(%llx:%x): Invalid config space of offset\n",
			res->response_route, res->response_port);
		return;
	case TB_CFG_ERROR_NO_SUCH_PORT:
		/*
		 * - The route contains a non-existent port.
		 * - The route contains a non-PHY port (e.g. PCIe).
		 * - The port in cfg_read/cfg_write does not exist.
		 */
		tb_ctl_WARN(ctl, "CFG_ERROR(%llx:%x): Invalid port\n",
			res->response_route, res->response_port);
		return;
	case TB_CFG_ERROR_LOOP:
		tb_ctl_WARN(ctl, "CFG_ERROR(%llx:%x): Route contains a loop\n",
			res->response_route, res->response_port);
		return;
	default:
		/* 5,6,7,9 and 11 are also valid error codes */
		tb_ctl_WARN(ctl, "CFG_ERROR(%llx:%x): Unknown error\n",
			res->response_route, res->response_port);
		return;
	}
}

static void cpu_to_be32_array(__be32 *dst, u32 *src, size_t len)
{
	int i;
	for (i = 0; i < len; i++)
		dst[i] = cpu_to_be32(src[i]);
}

static void be32_to_cpu_array(u32 *dst, __be32 *src, size_t len)
{
	int i;
	for (i = 0; i < len; i++)
		dst[i] = be32_to_cpu(src[i]);
}

static __be32 tb_crc(void *data, size_t len)
{
	return cpu_to_be32(~__crc32c_le(~0, data, len));
}

static void tb_ctl_pkg_free(struct ctl_pkg *pkg)
{
	if (pkg) {
		dma_pool_free(pkg->ctl->frame_pool,
			      pkg->buffer, pkg->frame.buffer_phy);
		kfree(pkg);
	}
}

static struct ctl_pkg *tb_ctl_pkg_alloc(struct tb_ctl *ctl)
{
	struct ctl_pkg *pkg = kzalloc(sizeof(*pkg), GFP_KERNEL);
	if (!pkg)
		return NULL;
	pkg->ctl = ctl;
	pkg->buffer = dma_pool_alloc(ctl->frame_pool, GFP_KERNEL,
				     &pkg->frame.buffer_phy);
	if (!pkg->buffer) {
		kfree(pkg);
		return NULL;
	}
	return pkg;
}


/* RX/TX handling */

static void tb_ctl_tx_callback(struct tb_ring *ring, struct ring_frame *frame,
			       bool canceled)
{
	struct ctl_pkg *pkg = container_of(frame, typeof(*pkg), frame);
	tb_ctl_pkg_free(pkg);
}

/**
 * tb_cfg_tx() - transmit a packet on the control channel
 *
 * len must be a multiple of four.
 *
 * Return: Returns 0 on success or an error code on failure.
 */
static int tb_ctl_tx(struct tb_ctl *ctl, void *data, size_t len,
		     enum tb_cfg_pkg_type type)
{
	int res;
	struct ctl_pkg *pkg;
	if (len % 4 != 0) { /* required for le->be conversion */
		tb_ctl_WARN(ctl, "TX: invalid size: %zu\n", len);
		return -EINVAL;
	}
	if (len > TB_FRAME_SIZE - 4) { /* checksum is 4 bytes */
		tb_ctl_WARN(ctl, "TX: packet too large: %zu/%d\n",
			    len, TB_FRAME_SIZE - 4);
		return -EINVAL;
	}
	pkg = tb_ctl_pkg_alloc(ctl);
	if (!pkg)
		return -ENOMEM;
	pkg->frame.callback = tb_ctl_tx_callback;
	pkg->frame.size = len + 4;
	pkg->frame.sof = type;
	pkg->frame.eof = type;
	cpu_to_be32_array(pkg->buffer, data, len / 4);
	*(u32 *) (pkg->buffer + len) = tb_crc(pkg->buffer, len);

	res = ring_tx(ctl->tx, &pkg->frame);
	if (res) /* ring is stopped */
		tb_ctl_pkg_free(pkg);
	return res;
}

/**
 * tb_ctl_handle_plug_event() - acknowledge a plug event, invoke ctl->callback
 */
static void tb_ctl_handle_plug_event(struct tb_ctl *ctl,
				     struct ctl_pkg *response)
{
	struct cfg_event_pkg *pkg = response->buffer;
	u64 route = get_route(pkg->header);

	if (check_header(response, sizeof(*pkg), TB_CFG_PKG_EVENT, route)) {
		tb_ctl_warn(ctl, "malformed TB_CFG_PKG_EVENT\n");
		return;
	}

	if (tb_cfg_error(ctl, route, pkg->port, TB_CFG_ERROR_ACK_PLUG_EVENT))
		tb_ctl_warn(ctl, "could not ack plug event on %llx:%x\n",
			    route, pkg->port);
	WARN(pkg->zero, "pkg->zero is %#x\n", pkg->zero);
	ctl->callback(ctl->callback_data, route, pkg->port, pkg->unplug);
}

static void tb_ctl_rx_submit(struct ctl_pkg *pkg)
{
	ring_rx(pkg->ctl->rx, &pkg->frame); /*
					     * We ignore failures during stop.
					     * All rx packets are referenced
					     * from ctl->rx_packets, so we do
					     * not loose them.
					     */
}

static void tb_ctl_rx_callback(struct tb_ring *ring, struct ring_frame *frame,
			       bool canceled)
{
	struct ctl_pkg *pkg = container_of(frame, typeof(*pkg), frame);

	if (canceled)
		return; /*
			 * ring is stopped, packet is referenced from
			 * ctl->rx_packets.
			 */

	if (frame->size < 4 || frame->size % 4 != 0) {
		tb_ctl_err(pkg->ctl, "RX: invalid size %#x, dropping packet\n",
			   frame->size);
		goto rx;
	}

	frame->size -= 4; /* remove checksum */
	if (*(u32 *) (pkg->buffer + frame->size)
			!= tb_crc(pkg->buffer, frame->size)) {
		tb_ctl_err(pkg->ctl,
			   "RX: checksum mismatch, dropping packet\n");
		goto rx;
	}
	be32_to_cpu_array(pkg->buffer, pkg->buffer, frame->size / 4);

	if (frame->eof == TB_CFG_PKG_EVENT) {
		tb_ctl_handle_plug_event(pkg->ctl, pkg);
		goto rx;
	}
	if (!kfifo_put(&pkg->ctl->response_fifo, pkg)) {
		tb_ctl_err(pkg->ctl, "RX: fifo is full\n");
		goto rx;
	}
	complete(&pkg->ctl->response_ready);
	return;
rx:
	tb_ctl_rx_submit(pkg);
}

/**
 * tb_ctl_rx() - receive a packet from the control channel
 */
static struct tb_cfg_result tb_ctl_rx(struct tb_ctl *ctl, void *buffer,
				      size_t length, int timeout_msec,
				      u64 route, enum tb_cfg_pkg_type type)
{
	struct tb_cfg_result res;
	struct ctl_pkg *pkg;

	if (!wait_for_completion_timeout(&ctl->response_ready,
					 msecs_to_jiffies(timeout_msec))) {
		tb_ctl_WARN(ctl, "RX: timeout\n");
		return (struct tb_cfg_result) { .err = -ETIMEDOUT };
	}
	if (!kfifo_get(&ctl->response_fifo, &pkg)) {
		tb_ctl_WARN(ctl, "empty kfifo\n");
		return (struct tb_cfg_result) { .err = -EIO };
	}

	res = parse_header(pkg, length, type, route);
	if (!res.err)
		memcpy(buffer, pkg->buffer, length);
	tb_ctl_rx_submit(pkg);
	return res;
}


/* public interface, alloc/start/stop/free */

/**
 * tb_ctl_alloc() - allocate a control channel
 *
 * cb will be invoked once for every hot plug event.
 *
 * Return: Returns a pointer on success or NULL on failure.
 */
struct tb_ctl *tb_ctl_alloc(struct tb_nhi *nhi, hotplug_cb cb, void *cb_data)
{
	int i;
	struct tb_ctl *ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return NULL;
	ctl->nhi = nhi;
	ctl->callback = cb;
	ctl->callback_data = cb_data;

	init_completion(&ctl->response_ready);
	INIT_KFIFO(ctl->response_fifo);
	ctl->frame_pool = dma_pool_create("thunderbolt_ctl", &nhi->pdev->dev,
					 TB_FRAME_SIZE, 4, 0);
	if (!ctl->frame_pool)
		goto err;

	ctl->tx = ring_alloc_tx(nhi, 0, 10);
	if (!ctl->tx)
		goto err;

	ctl->rx = ring_alloc_rx(nhi, 0, 10);
	if (!ctl->rx)
		goto err;

	for (i = 0; i < TB_CTL_RX_PKG_COUNT; i++) {
		ctl->rx_packets[i] = tb_ctl_pkg_alloc(ctl);
		if (!ctl->rx_packets[i])
			goto err;
		ctl->rx_packets[i]->frame.callback = tb_ctl_rx_callback;
	}

	tb_ctl_info(ctl, "control channel created\n");
	return ctl;
err:
	tb_ctl_free(ctl);
	return NULL;
}

/**
 * tb_ctl_free() - free a control channel
 *
 * Must be called after tb_ctl_stop.
 *
 * Must NOT be called from ctl->callback.
 */
void tb_ctl_free(struct tb_ctl *ctl)
{
	int i;
	if (ctl->rx)
		ring_free(ctl->rx);
	if (ctl->tx)
		ring_free(ctl->tx);

	/* free RX packets */
	for (i = 0; i < TB_CTL_RX_PKG_COUNT; i++)
		tb_ctl_pkg_free(ctl->rx_packets[i]);


	if (ctl->frame_pool)
		dma_pool_destroy(ctl->frame_pool);
	kfree(ctl);
}

/**
 * tb_cfg_start() - start/resume the control channel
 */
void tb_ctl_start(struct tb_ctl *ctl)
{
	int i;
	tb_ctl_info(ctl, "control channel starting...\n");
	ring_start(ctl->tx); /* is used to ack hotplug packets, start first */
	ring_start(ctl->rx);
	for (i = 0; i < TB_CTL_RX_PKG_COUNT; i++)
		tb_ctl_rx_submit(ctl->rx_packets[i]);
}

/**
 * control() - pause the control channel
 *
 * All invocations of ctl->callback will have finished after this method
 * returns.
 *
 * Must NOT be called from ctl->callback.
 */
void tb_ctl_stop(struct tb_ctl *ctl)
{
	ring_stop(ctl->rx);
	ring_stop(ctl->tx);

	if (!kfifo_is_empty(&ctl->response_fifo))
		tb_ctl_WARN(ctl, "dangling response in response_fifo\n");
	kfifo_reset(&ctl->response_fifo);
	tb_ctl_info(ctl, "control channel stopped\n");
}

/* public interface, commands */

/**
 * tb_cfg_error() - send error packet
 *
 * Return: Returns 0 on success or an error code on failure.
 */
int tb_cfg_error(struct tb_ctl *ctl, u64 route, u32 port,
		 enum tb_cfg_error error)
{
	struct cfg_error_pkg pkg = {
		.header = make_header(route),
		.port = port,
		.error = error,
	};
	tb_ctl_info(ctl, "resetting error on %llx:%x.\n", route, port);
	return tb_ctl_tx(ctl, &pkg, sizeof(pkg), TB_CFG_PKG_ERROR);
}

/**
 * tb_cfg_reset() - send a reset packet and wait for a response
 *
 * If the switch at route is incorrectly configured then we will not receive a
 * reply (even though the switch will reset). The caller should check for
 * -ETIMEDOUT and attempt to reconfigure the switch.
 */
struct tb_cfg_result tb_cfg_reset(struct tb_ctl *ctl, u64 route,
				  int timeout_msec)
{
	int err;
	struct cfg_reset_pkg request = { .header = make_header(route) };
	struct tb_cfg_header reply;

	err = tb_ctl_tx(ctl, &request, sizeof(request), TB_CFG_PKG_RESET);
	if (err)
		return (struct tb_cfg_result) { .err = err };

	return tb_ctl_rx(ctl, &reply, sizeof(reply), timeout_msec, route,
			 TB_CFG_PKG_RESET);
}

/**
 * tb_cfg_read() - read from config space into buffer
 *
 * Offset and length are in dwords.
 */
struct tb_cfg_result tb_cfg_read_raw(struct tb_ctl *ctl, void *buffer,
		u64 route, u32 port, enum tb_cfg_space space,
		u32 offset, u32 length, int timeout_msec)
{
	struct tb_cfg_result res = { 0 };
	struct cfg_read_pkg request = {
		.header = make_header(route),
		.addr = {
			.port = port,
			.space = space,
			.offset = offset,
			.length = length,
		},
	};
	struct cfg_write_pkg reply;

	res.err = tb_ctl_tx(ctl, &request, sizeof(request), TB_CFG_PKG_READ);
	if (res.err)
		return res;

	res = tb_ctl_rx(ctl, &reply, 12 + 4 * length, timeout_msec, route,
			TB_CFG_PKG_READ);
	if (res.err)
		return res;

	res.response_port = reply.addr.port;
	res.err = check_config_address(reply.addr, space, offset, length);
	if (!res.err)
		memcpy(buffer, &reply.data, 4 * length);
	return res;
}

/**
 * tb_cfg_write() - write from buffer into config space
 *
 * Offset and length are in dwords.
 */
struct tb_cfg_result tb_cfg_write_raw(struct tb_ctl *ctl, void *buffer,
		u64 route, u32 port, enum tb_cfg_space space,
		u32 offset, u32 length, int timeout_msec)
{
	struct tb_cfg_result res = { 0 };
	struct cfg_write_pkg request = {
		.header = make_header(route),
		.addr = {
			.port = port,
			.space = space,
			.offset = offset,
			.length = length,
		},
	};
	struct cfg_read_pkg reply;

	memcpy(&request.data, buffer, length * 4);

	res.err = tb_ctl_tx(ctl, &request, 12 + 4 * length, TB_CFG_PKG_WRITE);
	if (res.err)
		return res;

	res = tb_ctl_rx(ctl, &reply, sizeof(reply), timeout_msec, route,
			TB_CFG_PKG_WRITE);
	if (res.err)
		return res;

	res.response_port = reply.addr.port;
	res.err = check_config_address(reply.addr, space, offset, length);
	return res;
}

int tb_cfg_read(struct tb_ctl *ctl, void *buffer, u64 route, u32 port,
		enum tb_cfg_space space, u32 offset, u32 length)
{
	struct tb_cfg_result res = tb_cfg_read_raw(ctl, buffer, route, port,
			space, offset, length, TB_CFG_DEFAULT_TIMEOUT);
	if (res.err == 1) {
		tb_cfg_print_error(ctl, &res);
		return -EIO;
	}
	WARN(res.err, "tb_cfg_read: %d\n", res.err);
	return res.err;
}

int tb_cfg_write(struct tb_ctl *ctl, void *buffer, u64 route, u32 port,
		 enum tb_cfg_space space, u32 offset, u32 length)
{
	struct tb_cfg_result res = tb_cfg_write_raw(ctl, buffer, route, port,
			space, offset, length, TB_CFG_DEFAULT_TIMEOUT);
	if (res.err == 1) {
		tb_cfg_print_error(ctl, &res);
		return -EIO;
	}
	WARN(res.err, "tb_cfg_write: %d\n", res.err);
	return res.err;
}

/**
 * tb_cfg_get_upstream_port() - get upstream port number of switch at route
 *
 * Reads the first dword from the switches TB_CFG_SWITCH config area and
 * returns the port number from which the reply originated.
 *
 * Return: Returns the upstream port number on success or an error code on
 * failure.
 */
int tb_cfg_get_upstream_port(struct tb_ctl *ctl, u64 route)
{
	u32 dummy;
	struct tb_cfg_result res = tb_cfg_read_raw(ctl, &dummy, route, 0,
						   TB_CFG_SWITCH, 0, 1,
						   TB_CFG_DEFAULT_TIMEOUT);
	if (res.err == 1)
		return -EIO;
	if (res.err)
		return res.err;
	return res.response_port;
}
