#ifndef _IEEE1394_HOSTS_H
#define _IEEE1394_HOSTS_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>

struct pci_dev;
struct module;

#include "ieee1394_types.h"
#include "csr.h"

struct hpsb_packet;
struct hpsb_iso;

struct hpsb_host {
	struct list_head host_list;

	void *hostdata;

	atomic_t generation;

	struct list_head pending_packets;
	struct timer_list timeout;
	unsigned long timeout_interval;

	int node_count;      /* number of identified nodes on this bus */
	int selfid_count;    /* total number of SelfIDs received */
	int nodes_active;    /* number of nodes with active link layer */

	nodeid_t node_id;    /* node ID of this host */
	nodeid_t irm_id;     /* ID of this bus' isochronous resource manager */
	nodeid_t busmgr_id;  /* ID of this bus' bus manager */

	/* this nodes state */
	unsigned in_bus_reset:1;
	unsigned is_shutdown:1;
	unsigned resume_packet_sent:1;

	/* this nodes' duties on the bus */
	unsigned is_root:1;
	unsigned is_cycmst:1;
	unsigned is_irm:1;
	unsigned is_busmgr:1;

	int reset_retries;
	quadlet_t *topology_map;
	u8 *speed_map;

	int id;
	struct hpsb_host_driver *driver;
	struct pci_dev *pdev;
	struct device device;
	struct device host_dev;

	struct delayed_work delayed_reset;
	unsigned config_roms:31;
	unsigned update_config_rom:1;

	struct list_head addr_space;
	u64 low_addr_space;	/* upper bound of physical DMA area */
	u64 middle_addr_space;	/* upper bound of posted write area */

	u8 speed[ALL_NODES];	/* speed between each node and local node */

	/* per node tlabel allocation */
	u8 next_tl[ALL_NODES];
	struct { DECLARE_BITMAP(map, 64); } tl_pool[ALL_NODES];

	struct csr_control csr;
};

enum devctl_cmd {
	/* Host is requested to reset its bus and cancel all outstanding async
	 * requests.  If arg == 1, it shall also attempt to become root on the
	 * bus.  Return void. */
	RESET_BUS,

	/* Arg is void, return value is the hardware cycle counter value. */
	GET_CYCLE_COUNTER,

	/* Set the hardware cycle counter to the value in arg, return void.
	 * FIXME - setting is probably not required. */
	SET_CYCLE_COUNTER,

	/* Configure hardware for new bus ID in arg, return void. */
	SET_BUS_ID,

	/* If arg true, start sending cycle start packets, stop if arg == 0.
	 * Return void. */
	ACT_CYCLE_MASTER,

	/* Cancel all outstanding async requests without resetting the bus.
	 * Return void. */
	CANCEL_REQUESTS,
};

enum isoctl_cmd {
	/* rawiso API - see iso.h for the meanings of these commands
	 * (they correspond exactly to the hpsb_iso_* API functions)
	 * INIT = allocate resources
	 * START = begin transmission/reception
	 * STOP = halt transmission/reception
	 * QUEUE/RELEASE = produce/consume packets
	 * SHUTDOWN = deallocate resources
	 */

	XMIT_INIT,
	XMIT_START,
	XMIT_STOP,
	XMIT_QUEUE,
	XMIT_SHUTDOWN,

	RECV_INIT,
	RECV_LISTEN_CHANNEL,   /* multi-channel only */
	RECV_UNLISTEN_CHANNEL, /* multi-channel only */
	RECV_SET_CHANNEL_MASK, /* multi-channel only; arg is a *u64 */
	RECV_START,
	RECV_STOP,
	RECV_RELEASE,
	RECV_SHUTDOWN,
	RECV_FLUSH
};

enum reset_types {
	/* 166 microsecond reset -- only type of reset available on
	   non-1394a capable controllers */
	LONG_RESET,

	/* Short (arbitrated) reset -- only available on 1394a capable
	   controllers */
	SHORT_RESET,

	/* Variants that set force_root before issueing the bus reset */
	LONG_RESET_FORCE_ROOT, SHORT_RESET_FORCE_ROOT,

	/* Variants that clear force_root before issueing the bus reset */
	LONG_RESET_NO_FORCE_ROOT, SHORT_RESET_NO_FORCE_ROOT
};

struct hpsb_host_driver {
	struct module *owner;
	const char *name;

	/* The hardware driver may optionally support a function that is used
	 * to set the hardware ConfigROM if the hardware supports handling
	 * reads to the ConfigROM on its own. */
	void (*set_hw_config_rom)(struct hpsb_host *host,
				  quadlet_t *config_rom);

	/* This function shall implement packet transmission based on
	 * packet->type.  It shall CRC both parts of the packet (unless
	 * packet->type == raw) and do byte-swapping as necessary or instruct
	 * the hardware to do so.  It can return immediately after the packet
	 * was queued for sending.  After sending, hpsb_sent_packet() has to be
	 * called.  Return 0 on success, negative errno on failure.
	 * NOTE: The function must be callable in interrupt context.
	 */
	int (*transmit_packet)(struct hpsb_host *host,
			       struct hpsb_packet *packet);

	/* This function requests miscellanous services from the driver, see
	 * above for command codes and expected actions.  Return -1 for unknown
	 * command, though that should never happen.
	 */
	int (*devctl)(struct hpsb_host *host, enum devctl_cmd command, int arg);

	 /* ISO transmission/reception functions. Return 0 on success, -1
	  * (or -EXXX errno code) on failure. If the low-level driver does not
	  * support the new ISO API, set isoctl to NULL.
	  */
	int (*isoctl)(struct hpsb_iso *iso, enum isoctl_cmd command,
		      unsigned long arg);

	/* This function is mainly to redirect local CSR reads/locks to the iso
	 * management registers (bus manager id, bandwidth available, channels
	 * available) to the hardware registers in OHCI.  reg is 0,1,2,3 for bus
	 * mgr, bwdth avail, ch avail hi, ch avail lo respectively (the same ids
	 * as OHCI uses).  data and compare are the new data and expected data
	 * respectively, return value is the old value.
	 */
	quadlet_t (*hw_csr_reg) (struct hpsb_host *host, int reg,
				 quadlet_t data, quadlet_t compare);
};

struct hpsb_host *hpsb_alloc_host(struct hpsb_host_driver *drv, size_t extra,
				  struct device *dev);
int hpsb_add_host(struct hpsb_host *host);
void hpsb_resume_host(struct hpsb_host *host);
void hpsb_remove_host(struct hpsb_host *host);
int hpsb_update_config_rom_image(struct hpsb_host *host);

#endif /* _IEEE1394_HOSTS_H */
