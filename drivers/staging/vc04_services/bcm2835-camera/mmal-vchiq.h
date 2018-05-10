/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Broadcom BM2835 V4L2 driver
 *
 * Copyright © 2013 Raspberry Pi (Trading) Ltd.
 *
 * Authors: Vincent Sanders <vincent.sanders@collabora.co.uk>
 *          Dave Stevenson <dsteve@broadcom.com>
 *          Simon Mellor <simellor@broadcom.com>
 *          Luke Diamand <luked@broadcom.com>
 *
 * MMAL interface to VCHIQ message passing
 */

#ifndef MMAL_VCHIQ_H
#define MMAL_VCHIQ_H

#include "mmal-msg-format.h"

#define MAX_PORT_COUNT 4

/* Maximum size of the format extradata. */
#define MMAL_FORMAT_EXTRADATA_MAX_SIZE 128

struct vchiq_mmal_instance;

enum vchiq_mmal_es_type {
	MMAL_ES_TYPE_UNKNOWN,     /**< Unknown elementary stream type */
	MMAL_ES_TYPE_CONTROL,     /**< Elementary stream of control commands */
	MMAL_ES_TYPE_AUDIO,       /**< Audio elementary stream */
	MMAL_ES_TYPE_VIDEO,       /**< Video elementary stream */
	MMAL_ES_TYPE_SUBPICTURE   /**< Sub-picture elementary stream */
};

/* rectangle, used lots so it gets its own struct */
struct vchiq_mmal_rect {
	s32 x;
	s32 y;
	s32 width;
	s32 height;
};

struct vchiq_mmal_port_buffer {
	unsigned int num; /* number of buffers */
	u32 size; /* size of buffers */
	u32 alignment; /* alignment of buffers */
};

struct vchiq_mmal_port;

typedef void (*vchiq_mmal_buffer_cb)(
		struct vchiq_mmal_instance  *instance,
		struct vchiq_mmal_port *port,
		int status, struct mmal_buffer *buffer,
		unsigned long length, u32 mmal_flags, s64 dts, s64 pts);

struct vchiq_mmal_port {
	bool enabled;
	u32 handle;
	u32 type; /* port type, cached to use on port info set */
	u32 index; /* port index, cached to use on port info set */

	/* component port belongs to, allows simple deref */
	struct vchiq_mmal_component *component;

	struct vchiq_mmal_port *connected; /* port conencted to */

	/* buffer info */
	struct vchiq_mmal_port_buffer minimum_buffer;
	struct vchiq_mmal_port_buffer recommended_buffer;
	struct vchiq_mmal_port_buffer current_buffer;

	/* stream format */
	struct mmal_es_format_local format;
	/* elementary stream format */
	union mmal_es_specific_format es;

	/* data buffers to fill */
	struct list_head buffers;
	/* lock to serialise adding and removing buffers from list */
	spinlock_t slock;
	/* callback on buffer completion */
	vchiq_mmal_buffer_cb buffer_cb;
	/* callback context */
	void *cb_ctx;
};

struct vchiq_mmal_component {
	bool enabled;
	u32 handle;  /* VideoCore handle for component */
	u32 inputs;  /* Number of input ports */
	u32 outputs; /* Number of output ports */
	u32 clocks;  /* Number of clock ports */
	struct vchiq_mmal_port control; /* control port */
	struct vchiq_mmal_port input[MAX_PORT_COUNT]; /* input ports */
	struct vchiq_mmal_port output[MAX_PORT_COUNT]; /* output ports */
	struct vchiq_mmal_port clock[MAX_PORT_COUNT]; /* clock ports */
};

int vchiq_mmal_init(struct vchiq_mmal_instance **out_instance);
int vchiq_mmal_finalise(struct vchiq_mmal_instance *instance);

/* Initialise a mmal component and its ports
 *
 */
int vchiq_mmal_component_init(
		struct vchiq_mmal_instance *instance,
		const char *name,
		struct vchiq_mmal_component **component_out);

int vchiq_mmal_component_finalise(
		struct vchiq_mmal_instance *instance,
		struct vchiq_mmal_component *component);

int vchiq_mmal_component_enable(
		struct vchiq_mmal_instance *instance,
		struct vchiq_mmal_component *component);

int vchiq_mmal_component_disable(
		struct vchiq_mmal_instance *instance,
		struct vchiq_mmal_component *component);

/* enable a mmal port
 *
 * enables a port and if a buffer callback provided enque buffer
 * headers as appropriate for the port.
 */
int vchiq_mmal_port_enable(
		struct vchiq_mmal_instance *instance,
		struct vchiq_mmal_port *port,
		vchiq_mmal_buffer_cb buffer_cb);

/* disable a port
 *
 * disable a port will dequeue any pending buffers
 */
int vchiq_mmal_port_disable(struct vchiq_mmal_instance *instance,
			   struct vchiq_mmal_port *port);

int vchiq_mmal_port_parameter_set(struct vchiq_mmal_instance *instance,
				  struct vchiq_mmal_port *port,
				  u32 parameter,
				  void *value,
				  u32 value_size);

int vchiq_mmal_port_parameter_get(struct vchiq_mmal_instance *instance,
				  struct vchiq_mmal_port *port,
				  u32 parameter,
				  void *value,
				  u32 *value_size);

int vchiq_mmal_port_set_format(struct vchiq_mmal_instance *instance,
			       struct vchiq_mmal_port *port);

int vchiq_mmal_port_connect_tunnel(struct vchiq_mmal_instance *instance,
			    struct vchiq_mmal_port *src,
			    struct vchiq_mmal_port *dst);

int vchiq_mmal_version(struct vchiq_mmal_instance *instance,
		       u32 *major_out,
		       u32 *minor_out);

int vchiq_mmal_submit_buffer(struct vchiq_mmal_instance *instance,
			     struct vchiq_mmal_port *port,
			     struct mmal_buffer *buf);

int mmal_vchi_buffer_init(struct vchiq_mmal_instance *instance,
			  struct mmal_buffer *buf);
int mmal_vchi_buffer_cleanup(struct mmal_buffer *buf);
#endif /* MMAL_VCHIQ_H */
