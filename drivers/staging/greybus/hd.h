/*
 * Greybus Host Device
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __HD_H
#define __HD_H

struct gb_host_device;
struct gb_message;

struct gb_hd_driver {
	size_t	hd_priv_size;

	int (*cport_enable)(struct gb_host_device *hd, u16 cport_id);
	int (*cport_disable)(struct gb_host_device *hd, u16 cport_id);
	int (*message_send)(struct gb_host_device *hd, u16 dest_cport_id,
			struct gb_message *message, gfp_t gfp_mask);
	void (*message_cancel)(struct gb_message *message);
	int (*latency_tag_enable)(struct gb_host_device *hd, u16 cport_id);
	int (*latency_tag_disable)(struct gb_host_device *hd, u16 cport_id);
};

struct gb_host_device {
	struct kref kref;
	struct device *parent;
	const struct gb_hd_driver *driver;

	struct list_head interfaces;
	struct list_head connections;
	struct ida cport_id_map;

	/* Number of CPorts supported by the UniPro IP */
	size_t num_cports;

	/* Host device buffer constraints */
	size_t buffer_size_max;

	struct gb_endo *endo;
	struct gb_connection *initial_svc_connection;
	struct gb_svc *svc;

	/* Private data for the host driver */
	unsigned long hd_priv[0] __aligned(sizeof(s64));
};

struct gb_host_device *gb_hd_create(struct gb_hd_driver *driver,
					struct device *parent,
					size_t buffer_size_max,
					size_t num_cports);
int gb_hd_add(struct gb_host_device *hd);
void gb_hd_del(struct gb_host_device *hd);
void gb_hd_put(struct gb_host_device *hd);

#endif	/* __HD_H */
