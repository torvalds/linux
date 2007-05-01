#ifndef IEEE1394_HIGHLEVEL_H
#define IEEE1394_HIGHLEVEL_H

#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

struct module;

#include "ieee1394_types.h"

struct hpsb_host;

/* internal to ieee1394 core */
struct hpsb_address_serve {
	struct list_head host_list;	/* per host list */
	struct list_head hl_list;	/* hpsb_highlevel list */
	struct hpsb_address_ops *op;
	struct hpsb_host *host;
	u64 start;	/* first address handled, quadlet aligned */
	u64 end;	/* first address behind, quadlet aligned */
};

/* Only the following structures are of interest to actual highlevel drivers. */

struct hpsb_highlevel {
	const char *name;

	/* Any of the following pointers can legally be NULL, except for
	 * iso_receive which can only be NULL when you don't request
	 * channels. */

	/* New host initialized.  Will also be called during
	 * hpsb_register_highlevel for all hosts already installed. */
	void (*add_host)(struct hpsb_host *host);

	/* Host about to be removed.  Will also be called during
	 * hpsb_unregister_highlevel once for each host. */
	void (*remove_host)(struct hpsb_host *host);

	/* Host experienced bus reset with possible configuration changes.
	 * Note that this one may occur during interrupt/bottom half handling.
	 * You can not expect to be able to do stock hpsb_reads. */
	void (*host_reset)(struct hpsb_host *host);

	/* An isochronous packet was received.  Channel contains the channel
	 * number for your convenience, it is also contained in the included
	 * packet header (first quadlet, CRCs are missing).  You may get called
	 * for channel/host combinations you did not request. */
	void (*iso_receive)(struct hpsb_host *host, int channel,
			    quadlet_t *data, size_t length);

	/* A write request was received on either the FCP_COMMAND (direction =
	 * 0) or the FCP_RESPONSE (direction = 1) register.  The cts arg
	 * contains the cts field (first byte of data). */
	void (*fcp_request)(struct hpsb_host *host, int nodeid, int direction,
			    int cts, u8 *data, size_t length);

	/* These are initialized by the subsystem when the
	 * hpsb_higlevel is registered. */
	struct list_head hl_list;
	struct list_head irq_list;
	struct list_head addr_list;

	struct list_head host_info_list;
	rwlock_t host_info_lock;
};

struct hpsb_address_ops {
	/*
	 * Null function pointers will make the respective operation complete
	 * with RCODE_TYPE_ERROR.  Makes for easy to implement read-only
	 * registers (just leave everything but read NULL).
	 *
	 * All functions shall return appropriate IEEE 1394 rcodes.
	 */

	/* These functions have to implement block reads for themselves.
	 *
	 * These functions either return a response code or a negative number.
	 * In the first case a response will be generated.  In the latter case,
	 * no response will be sent and the driver which handled the request
	 * will send the response itself. */
	int (*read)(struct hpsb_host *host, int nodeid, quadlet_t *buffer,
		    u64 addr, size_t length, u16 flags);
	int (*write)(struct hpsb_host *host, int nodeid, int destid,
		     quadlet_t *data, u64 addr, size_t length, u16 flags);

	/* Lock transactions: write results of ext_tcode operation into
	 * *store. */
	int (*lock)(struct hpsb_host *host, int nodeid, quadlet_t *store,
		    u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode,
		    u16 flags);
	int (*lock64)(struct hpsb_host *host, int nodeid, octlet_t *store,
		      u64 addr, octlet_t data, octlet_t arg, int ext_tcode,
		      u16 flags);
};

void highlevel_add_host(struct hpsb_host *host);
void highlevel_remove_host(struct hpsb_host *host);
void highlevel_host_reset(struct hpsb_host *host);
int highlevel_read(struct hpsb_host *host, int nodeid, void *data, u64 addr,
		   unsigned int length, u16 flags);
int highlevel_write(struct hpsb_host *host, int nodeid, int destid, void *data,
		    u64 addr, unsigned int length, u16 flags);
int highlevel_lock(struct hpsb_host *host, int nodeid, quadlet_t *store,
		   u64 addr, quadlet_t data, quadlet_t arg, int ext_tcode,
		   u16 flags);
int highlevel_lock64(struct hpsb_host *host, int nodeid, octlet_t *store,
		     u64 addr, octlet_t data, octlet_t arg, int ext_tcode,
		     u16 flags);
void highlevel_iso_receive(struct hpsb_host *host, void *data, size_t length);
void highlevel_fcp_request(struct hpsb_host *host, int nodeid, int direction,
			   void *data, size_t length);

void hpsb_register_highlevel(struct hpsb_highlevel *hl);
void hpsb_unregister_highlevel(struct hpsb_highlevel *hl);

u64 hpsb_allocate_and_register_addrspace(struct hpsb_highlevel *hl,
					 struct hpsb_host *host,
					 struct hpsb_address_ops *ops,
					 u64 size, u64 alignment,
					 u64 start, u64 end);
int hpsb_register_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
			    struct hpsb_address_ops *ops, u64 start, u64 end);
int hpsb_unregister_addrspace(struct hpsb_highlevel *hl, struct hpsb_host *host,
			      u64 start);
int hpsb_listen_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
			unsigned int channel);
void hpsb_unlisten_channel(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   unsigned int channel);

void *hpsb_get_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host);
void *hpsb_create_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   size_t data_size);
void hpsb_destroy_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host);
void hpsb_set_hostinfo_key(struct hpsb_highlevel *hl, struct hpsb_host *host,
			   unsigned long key);
void *hpsb_get_hostinfo_bykey(struct hpsb_highlevel *hl, unsigned long key);
int hpsb_set_hostinfo(struct hpsb_highlevel *hl, struct hpsb_host *host,
		      void *data);

#endif /* IEEE1394_HIGHLEVEL_H */
