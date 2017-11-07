// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus Host Device
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 */

#ifndef __HD_H
#define __HD_H

struct gb_host_device;
struct gb_message;

struct gb_hd_driver {
	size_t	hd_priv_size;

	int (*cport_allocate)(struct gb_host_device *hd, int cport_id,
				unsigned long flags);
	void (*cport_release)(struct gb_host_device *hd, u16 cport_id);
	int (*cport_enable)(struct gb_host_device *hd, u16 cport_id,
				unsigned long flags);
	int (*cport_disable)(struct gb_host_device *hd, u16 cport_id);
	int (*cport_connected)(struct gb_host_device *hd, u16 cport_id);
	int (*cport_flush)(struct gb_host_device *hd, u16 cport_id);
	int (*cport_shutdown)(struct gb_host_device *hd, u16 cport_id,
				u8 phase, unsigned int timeout);
	int (*cport_quiesce)(struct gb_host_device *hd, u16 cport_id,
				size_t peer_space, unsigned int timeout);
	int (*cport_clear)(struct gb_host_device *hd, u16 cport_id);

	int (*message_send)(struct gb_host_device *hd, u16 dest_cport_id,
			struct gb_message *message, gfp_t gfp_mask);
	void (*message_cancel)(struct gb_message *message);
	int (*latency_tag_enable)(struct gb_host_device *hd, u16 cport_id);
	int (*latency_tag_disable)(struct gb_host_device *hd, u16 cport_id);
	int (*output)(struct gb_host_device *hd, void *req, u16 size, u8 cmd,
		      bool async);
};

struct gb_host_device {
	struct device dev;
	int bus_id;
	const struct gb_hd_driver *driver;

	struct list_head modules;
	struct list_head connections;
	struct ida cport_id_map;

	/* Number of CPorts supported by the UniPro IP */
	size_t num_cports;

	/* Host device buffer constraints */
	size_t buffer_size_max;

	struct gb_svc *svc;
	/* Private data for the host driver */
	unsigned long hd_priv[0] __aligned(sizeof(s64));
};
#define to_gb_host_device(d) container_of(d, struct gb_host_device, dev)

int gb_hd_cport_reserve(struct gb_host_device *hd, u16 cport_id);
void gb_hd_cport_release_reserved(struct gb_host_device *hd, u16 cport_id);
int gb_hd_cport_allocate(struct gb_host_device *hd, int cport_id,
					unsigned long flags);
void gb_hd_cport_release(struct gb_host_device *hd, u16 cport_id);

struct gb_host_device *gb_hd_create(struct gb_hd_driver *driver,
					struct device *parent,
					size_t buffer_size_max,
					size_t num_cports);
int gb_hd_add(struct gb_host_device *hd);
void gb_hd_del(struct gb_host_device *hd);
void gb_hd_shutdown(struct gb_host_device *hd);
void gb_hd_put(struct gb_host_device *hd);
int gb_hd_output(struct gb_host_device *hd, void *req, u16 size, u8 cmd,
		 bool in_irq);

int gb_hd_init(void);
void gb_hd_exit(void);

#endif	/* __HD_H */
