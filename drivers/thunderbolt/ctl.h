/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - control channel and configuration commands
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#ifndef _TB_CFG
#define _TB_CFG

#include <linux/kref.h>
#include <linux/thunderbolt.h>

#include "nhi.h"
#include "tb_msgs.h"

/* control channel */
struct tb_ctl;

typedef bool (*event_cb)(void *data, enum tb_cfg_pkg_type type,
			 const void *buf, size_t size);

struct tb_ctl *tb_ctl_alloc(struct tb_nhi *nhi, int timeout_msec, event_cb cb,
			    void *cb_data);
void tb_ctl_start(struct tb_ctl *ctl);
void tb_ctl_stop(struct tb_ctl *ctl);
void tb_ctl_free(struct tb_ctl *ctl);

/* configuration commands */

struct tb_cfg_result {
	u64 response_route;
	u32 response_port; /*
			    * If err = 1 then this is the port that send the
			    * error.
			    * If err = 0 and if this was a cfg_read/write then
			    * this is the the upstream port of the responding
			    * switch.
			    * Otherwise the field is set to zero.
			    */
	int err; /* negative errors, 0 for success, 1 for tb errors */
	enum tb_cfg_error tb_error; /* valid if err == 1 */
};

struct ctl_pkg {
	struct tb_ctl *ctl;
	void *buffer;
	struct ring_frame frame;
};

/**
 * struct tb_cfg_request - Control channel request
 * @kref: Reference count
 * @ctl: Pointer to the control channel structure. Only set when the
 *	 request is queued.
 * @request_size: Size of the request packet (in bytes)
 * @request_type: Type of the request packet
 * @response: Response is stored here
 * @response_size: Maximum size of one response packet
 * @response_type: Expected type of the response packet
 * @npackets: Number of packets expected to be returned with this request
 * @match: Function used to match the incoming packet
 * @copy: Function used to copy the incoming packet to @response
 * @callback: Callback called when the request is finished successfully
 * @callback_data: Data to be passed to @callback
 * @flags: Flags for the request
 * @work: Work item used to complete the request
 * @result: Result after the request has been completed
 * @list: Requests are queued using this field
 *
 * An arbitrary request over Thunderbolt control channel. For standard
 * control channel message, one should use tb_cfg_read/write() and
 * friends if possible.
 */
struct tb_cfg_request {
	struct kref kref;
	struct tb_ctl *ctl;
	const void *request;
	size_t request_size;
	enum tb_cfg_pkg_type request_type;
	void *response;
	size_t response_size;
	enum tb_cfg_pkg_type response_type;
	size_t npackets;
	bool (*match)(const struct tb_cfg_request *req,
		      const struct ctl_pkg *pkg);
	bool (*copy)(struct tb_cfg_request *req, const struct ctl_pkg *pkg);
	void (*callback)(void *callback_data);
	void *callback_data;
	unsigned long flags;
	struct work_struct work;
	struct tb_cfg_result result;
	struct list_head list;
};

#define TB_CFG_REQUEST_ACTIVE		0
#define TB_CFG_REQUEST_CANCELED		1

struct tb_cfg_request *tb_cfg_request_alloc(void);
void tb_cfg_request_get(struct tb_cfg_request *req);
void tb_cfg_request_put(struct tb_cfg_request *req);
int tb_cfg_request(struct tb_ctl *ctl, struct tb_cfg_request *req,
		   void (*callback)(void *), void *callback_data);
void tb_cfg_request_cancel(struct tb_cfg_request *req, int err);
struct tb_cfg_result tb_cfg_request_sync(struct tb_ctl *ctl,
			struct tb_cfg_request *req, int timeout_msec);

static inline u64 tb_cfg_get_route(const struct tb_cfg_header *header)
{
	return (u64) header->route_hi << 32 | header->route_lo;
}

static inline struct tb_cfg_header tb_cfg_make_header(u64 route)
{
	struct tb_cfg_header header = {
		.route_hi = route >> 32,
		.route_lo = route,
	};
	/* check for overflow, route_hi is not 32 bits! */
	WARN_ON(tb_cfg_get_route(&header) != route);
	return header;
}

int tb_cfg_ack_plug(struct tb_ctl *ctl, u64 route, u32 port, bool unplug);
struct tb_cfg_result tb_cfg_reset(struct tb_ctl *ctl, u64 route);
struct tb_cfg_result tb_cfg_read_raw(struct tb_ctl *ctl, void *buffer,
				     u64 route, u32 port,
				     enum tb_cfg_space space, u32 offset,
				     u32 length, int timeout_msec);
struct tb_cfg_result tb_cfg_write_raw(struct tb_ctl *ctl, const void *buffer,
				      u64 route, u32 port,
				      enum tb_cfg_space space, u32 offset,
				      u32 length, int timeout_msec);
int tb_cfg_read(struct tb_ctl *ctl, void *buffer, u64 route, u32 port,
		enum tb_cfg_space space, u32 offset, u32 length);
int tb_cfg_write(struct tb_ctl *ctl, const void *buffer, u64 route, u32 port,
		 enum tb_cfg_space space, u32 offset, u32 length);
int tb_cfg_get_upstream_port(struct tb_ctl *ctl, u64 route);


#endif
