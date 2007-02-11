/*
 * IEEE 1394 for Linux
 *
 * Low level (host adapter) management.
 *
 * Copyright (C) 1999 Andreas E. Bombe
 * Copyright (C) 1999 Emanuel Pirker
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>

#include "csr1212.h"
#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "nodemgr.h"
#include "csr.h"
#include "config_roms.h"


static void delayed_reset_bus(struct work_struct *work)
{
	struct hpsb_host *host =
		container_of(work, struct hpsb_host, delayed_reset.work);
	int generation = host->csr.generation + 1;

	/* The generation field rolls over to 2 rather than 0 per IEEE
	 * 1394a-2000. */
	if (generation > 0xf || generation < 2)
		generation = 2;

	CSR_SET_BUS_INFO_GENERATION(host->csr.rom, generation);
	if (csr1212_generate_csr_image(host->csr.rom) != CSR1212_SUCCESS) {
		/* CSR image creation failed.
		 * Reset generation field and do not issue a bus reset. */
		CSR_SET_BUS_INFO_GENERATION(host->csr.rom,
					    host->csr.generation);
		return;
	}

	host->csr.generation = generation;

	host->update_config_rom = 0;
	if (host->driver->set_hw_config_rom)
		host->driver->set_hw_config_rom(host,
						host->csr.rom->bus_info_data);

	host->csr.gen_timestamp[host->csr.generation] = jiffies;
	hpsb_reset_bus(host, SHORT_RESET);
}

static int dummy_transmit_packet(struct hpsb_host *h, struct hpsb_packet *p)
{
	return 0;
}

static int dummy_devctl(struct hpsb_host *h, enum devctl_cmd c, int arg)
{
	return -1;
}

static int dummy_isoctl(struct hpsb_iso *iso, enum isoctl_cmd command,
			unsigned long arg)
{
	return -1;
}

static struct hpsb_host_driver dummy_driver = {
	.transmit_packet = dummy_transmit_packet,
	.devctl =	   dummy_devctl,
	.isoctl =	   dummy_isoctl
};

static int alloc_hostnum_cb(struct hpsb_host *host, void *__data)
{
	int *hostnum = __data;

	if (host->id == *hostnum)
		return 1;

	return 0;
}

/*
 * The pending_packet_queue is special in that it's processed
 * from hardirq context too (such as hpsb_bus_reset()). Hence
 * split the lock class from the usual networking skb-head
 * lock class by using a separate key for it:
 */
static struct lock_class_key pending_packet_queue_key;

static DEFINE_MUTEX(host_num_alloc);

/**
 * hpsb_alloc_host - allocate a new host controller.
 * @drv: the driver that will manage the host controller
 * @extra: number of extra bytes to allocate for the driver
 *
 * Allocate a &hpsb_host and initialize the general subsystem specific
 * fields.  If the driver needs to store per host data, as drivers
 * usually do, the amount of memory required can be specified by the
 * @extra parameter.  Once allocated, the driver should initialize the
 * driver specific parts, enable the controller and make it available
 * to the general subsystem using hpsb_add_host().
 *
 * Return Value: a pointer to the &hpsb_host if successful, %NULL if
 * no memory was available.
 */
struct hpsb_host *hpsb_alloc_host(struct hpsb_host_driver *drv, size_t extra,
				  struct device *dev)
{
	struct hpsb_host *h;
	int i;
	int hostnum = 0;

	h = kzalloc(sizeof(*h) + extra, GFP_KERNEL);
	if (!h)
		return NULL;

	h->csr.rom = csr1212_create_csr(&csr_bus_ops, CSR_BUS_INFO_SIZE, h);
	if (!h->csr.rom)
		goto fail;

	h->hostdata = h + 1;
	h->driver = drv;

	skb_queue_head_init(&h->pending_packet_queue);
	lockdep_set_class(&h->pending_packet_queue.lock,
			   &pending_packet_queue_key);
	INIT_LIST_HEAD(&h->addr_space);

	for (i = 2; i < 16; i++)
		h->csr.gen_timestamp[i] = jiffies - 60 * HZ;

	atomic_set(&h->generation, 0);

	INIT_DELAYED_WORK(&h->delayed_reset, delayed_reset_bus);
	
	init_timer(&h->timeout);
	h->timeout.data = (unsigned long) h;
	h->timeout.function = abort_timedouts;
	h->timeout_interval = HZ / 20; /* 50ms, half of minimum SPLIT_TIMEOUT */

	h->topology_map = h->csr.topology_map + 3;
	h->speed_map = (u8 *)(h->csr.speed_map + 2);

	mutex_lock(&host_num_alloc);
	while (nodemgr_for_each_host(&hostnum, alloc_hostnum_cb))
		hostnum++;
	mutex_unlock(&host_num_alloc);
	h->id = hostnum;

	memcpy(&h->device, &nodemgr_dev_template_host, sizeof(h->device));
	h->device.parent = dev;
	snprintf(h->device.bus_id, BUS_ID_SIZE, "fw-host%d", h->id);

	h->class_dev.dev = &h->device;
	h->class_dev.class = &hpsb_host_class;
	snprintf(h->class_dev.class_id, BUS_ID_SIZE, "fw-host%d", h->id);

	if (device_register(&h->device))
		goto fail;
	if (class_device_register(&h->class_dev)) {
		device_unregister(&h->device);
		goto fail;
	}
	get_device(&h->device);

	return h;

fail:
	kfree(h);
	return NULL;
}

int hpsb_add_host(struct hpsb_host *host)
{
	if (hpsb_default_host_entry(host))
		return -ENOMEM;
	hpsb_add_extra_config_roms(host);
	highlevel_add_host(host);
	return 0;
}

void hpsb_resume_host(struct hpsb_host *host)
{
	if (host->driver->set_hw_config_rom)
		host->driver->set_hw_config_rom(host,
						host->csr.rom->bus_info_data);
	host->driver->devctl(host, RESET_BUS, SHORT_RESET);
}

void hpsb_remove_host(struct hpsb_host *host)
{
	host->is_shutdown = 1;

	cancel_delayed_work(&host->delayed_reset);
	flush_scheduled_work();

	host->driver = &dummy_driver;
	highlevel_remove_host(host);
	hpsb_remove_extra_config_roms(host);

	class_device_unregister(&host->class_dev);
	device_unregister(&host->device);
}

int hpsb_update_config_rom_image(struct hpsb_host *host)
{
	unsigned long reset_delay;
	int next_gen = host->csr.generation + 1;

	if (!host->update_config_rom)
		return -EINVAL;

	if (next_gen > 0xf)
		next_gen = 2;

	/* Stop the delayed interrupt, we're about to change the config rom and
	 * it would be a waste to do a bus reset twice. */
	cancel_delayed_work(&host->delayed_reset);

	/* IEEE 1394a-2000 prohibits using the same generation number
	 * twice in a 60 second period. */
	if (time_before(jiffies, host->csr.gen_timestamp[next_gen] + 60 * HZ))
		/* Wait 60 seconds from the last time this generation number was
		 * used. */
		reset_delay =
			(60 * HZ) + host->csr.gen_timestamp[next_gen] - jiffies;
	else
		/* Wait 1 second in case some other code wants to change the
		 * Config ROM in the near future. */
		reset_delay = HZ;

	PREPARE_DELAYED_WORK(&host->delayed_reset, delayed_reset_bus);
	schedule_delayed_work(&host->delayed_reset, reset_delay);

	return 0;
}
