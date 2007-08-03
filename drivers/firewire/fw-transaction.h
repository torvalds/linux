/*
 * Copyright (C) 2003-2006 Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __fw_transaction_h
#define __fw_transaction_h

#include <linux/device.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/firewire-constants.h>

#define TCODE_IS_READ_REQUEST(tcode)	(((tcode) & ~1) == 4)
#define TCODE_IS_BLOCK_PACKET(tcode)	(((tcode) &  1) != 0)
#define TCODE_IS_REQUEST(tcode)		(((tcode) &  2) == 0)
#define TCODE_IS_RESPONSE(tcode)	(((tcode) &  2) != 0)
#define TCODE_HAS_REQUEST_DATA(tcode)	(((tcode) & 12) != 4)
#define TCODE_HAS_RESPONSE_DATA(tcode)	(((tcode) & 12) != 0)

#define LOCAL_BUS 0xffc0

#define SELFID_PORT_CHILD	0x3
#define SELFID_PORT_PARENT	0x2
#define SELFID_PORT_NCONN	0x1
#define SELFID_PORT_NONE	0x0

#define PHY_PACKET_CONFIG	0x0
#define PHY_PACKET_LINK_ON	0x1
#define PHY_PACKET_SELF_ID	0x2

/* Bit fields _within_ the PHY registers. */
#define PHY_LINK_ACTIVE		0x80
#define PHY_CONTENDER		0x40
#define PHY_BUS_RESET		0x40
#define PHY_BUS_SHORT_RESET	0x40

#define CSR_REGISTER_BASE		0xfffff0000000ULL

/* register offsets relative to CSR_REGISTER_BASE */
#define CSR_STATE_CLEAR			0x0
#define CSR_STATE_SET			0x4
#define CSR_NODE_IDS			0x8
#define CSR_RESET_START			0xc
#define CSR_SPLIT_TIMEOUT_HI		0x18
#define CSR_SPLIT_TIMEOUT_LO		0x1c
#define CSR_CYCLE_TIME			0x200
#define CSR_BUS_TIME			0x204
#define CSR_BUSY_TIMEOUT		0x210
#define CSR_BUS_MANAGER_ID		0x21c
#define CSR_BANDWIDTH_AVAILABLE		0x220
#define CSR_CHANNELS_AVAILABLE		0x224
#define CSR_CHANNELS_AVAILABLE_HI	0x224
#define CSR_CHANNELS_AVAILABLE_LO	0x228
#define CSR_BROADCAST_CHANNEL		0x234
#define CSR_CONFIG_ROM			0x400
#define CSR_CONFIG_ROM_END		0x800
#define CSR_FCP_COMMAND			0xB00
#define CSR_FCP_RESPONSE		0xD00
#define CSR_FCP_END			0xF00
#define CSR_TOPOLOGY_MAP		0x1000
#define CSR_TOPOLOGY_MAP_END		0x1400
#define CSR_SPEED_MAP			0x2000
#define CSR_SPEED_MAP_END		0x3000

#define fw_notify(s, args...) printk(KERN_NOTICE KBUILD_MODNAME ": " s, ## args)
#define fw_error(s, args...) printk(KERN_ERR KBUILD_MODNAME ": " s, ## args)

static inline void
fw_memcpy_from_be32(void *_dst, void *_src, size_t size)
{
	u32 *dst = _dst;
	u32 *src = _src;
	int i;

	for (i = 0; i < size / 4; i++)
		dst[i] = cpu_to_be32(src[i]);
}

static inline void
fw_memcpy_to_be32(void *_dst, void *_src, size_t size)
{
	fw_memcpy_from_be32(_dst, _src, size);
}

struct fw_card;
struct fw_packet;
struct fw_node;
struct fw_request;

struct fw_descriptor {
	struct list_head link;
	size_t length;
	u32 immediate;
	u32 key;
	const u32 *data;
};

int fw_core_add_descriptor(struct fw_descriptor *desc);
void fw_core_remove_descriptor(struct fw_descriptor *desc);

typedef void (*fw_packet_callback_t)(struct fw_packet *packet,
				     struct fw_card *card, int status);

typedef void (*fw_transaction_callback_t)(struct fw_card *card, int rcode,
					  void *data,
					  size_t length,
					  void *callback_data);

/*
 * Important note:  The callback must guarantee that either fw_send_response()
 * or kfree() is called on the @request.
 */
typedef void (*fw_address_callback_t)(struct fw_card *card,
				      struct fw_request *request,
				      int tcode, int destination, int source,
				      int generation, int speed,
				      unsigned long long offset,
				      void *data, size_t length,
				      void *callback_data);

typedef void (*fw_bus_reset_callback_t)(struct fw_card *handle,
					int node_id, int generation,
					u32 *self_ids,
					int self_id_count,
					void *callback_data);

struct fw_packet {
	int speed;
	int generation;
	u32 header[4];
	size_t header_length;
	void *payload;
	size_t payload_length;
	u32 timestamp;

	/*
	 * This callback is called when the packet transmission has
	 * completed; for successful transmission, the status code is
	 * the ack received from the destination, otherwise it's a
	 * negative errno: ENOMEM, ESTALE, ETIMEDOUT, ENODEV, EIO.
	 * The callback can be called from tasklet context and thus
	 * must never block.
	 */
	fw_packet_callback_t callback;
	int ack;
	struct list_head link;
	void *driver_data;
};

struct fw_transaction {
	int node_id; /* The generation is implied; it is always the current. */
	int tlabel;
	int timestamp;
	struct list_head link;

	struct fw_packet packet;

	/*
	 * The data passed to the callback is valid only during the
	 * callback.
	 */
	fw_transaction_callback_t callback;
	void *callback_data;
};

static inline struct fw_packet *
fw_packet(struct list_head *l)
{
	return list_entry(l, struct fw_packet, link);
}

struct fw_address_handler {
	u64 offset;
	size_t length;
	fw_address_callback_t address_callback;
	void *callback_data;
	struct list_head link;
};


struct fw_address_region {
	u64 start;
	u64 end;
};

extern const struct fw_address_region fw_low_memory_region;
extern const struct fw_address_region fw_high_memory_region;
extern const struct fw_address_region fw_private_region;
extern const struct fw_address_region fw_csr_region;
extern const struct fw_address_region fw_unit_space_region;

int fw_core_add_address_handler(struct fw_address_handler *handler,
				const struct fw_address_region *region);
void fw_core_remove_address_handler(struct fw_address_handler *handler);
void fw_fill_response(struct fw_packet *response, u32 *request_header,
		      int rcode, void *payload, size_t length);
void fw_send_response(struct fw_card *card,
		      struct fw_request *request, int rcode);

extern struct bus_type fw_bus_type;

struct fw_card {
	const struct fw_card_driver *driver;
	struct device *device;
	struct kref kref;

	int node_id;
	int generation;
	/* This is the generation used for timestamping incoming requests. */
	int request_generation;
	int current_tlabel, tlabel_mask;
	struct list_head transaction_list;
	struct timer_list flush_timer;
	unsigned long reset_jiffies;

	unsigned long long guid;
	unsigned max_receive;
	int link_speed;
	int config_rom_generation;

	/*
	 * We need to store up to 4 self ID for a maximum of 63
	 * devices plus 3 words for the topology map header.
	 */
	int self_id_count;
	u32 topology_map[252 + 3];

	spinlock_t lock; /* Take this lock when handling the lists in
			  * this struct. */
	struct fw_node *local_node;
	struct fw_node *root_node;
	struct fw_node *irm_node;
	int color;
	int gap_count;
	bool beta_repeaters_present;

	int index;

	struct list_head link;

	/* Work struct for BM duties. */
	struct delayed_work work;
	int bm_retries;
	int bm_generation;
};

struct fw_card *fw_card_get(struct fw_card *card);
void fw_card_put(struct fw_card *card);

/*
 * The iso packet format allows for an immediate header/payload part
 * stored in 'header' immediately after the packet info plus an
 * indirect payload part that is pointer to by the 'payload' field.
 * Applications can use one or the other or both to implement simple
 * low-bandwidth streaming (e.g. audio) or more advanced
 * scatter-gather streaming (e.g. assembling video frame automatically).
 */

struct fw_iso_packet {
	u16 payload_length;	/* Length of indirect payload. */
	u32 interrupt : 1;	/* Generate interrupt on this packet */
	u32 skip : 1;		/* Set to not send packet at all. */
	u32 tag : 2;
	u32 sy : 4;
	u32 header_length : 8;	/* Length of immediate header. */
	u32 header[0];
};

#define FW_ISO_CONTEXT_TRANSMIT	0
#define FW_ISO_CONTEXT_RECEIVE	1

#define FW_ISO_CONTEXT_MATCH_TAG0	 1
#define FW_ISO_CONTEXT_MATCH_TAG1	 2
#define FW_ISO_CONTEXT_MATCH_TAG2	 4
#define FW_ISO_CONTEXT_MATCH_TAG3	 8
#define FW_ISO_CONTEXT_MATCH_ALL_TAGS	15

struct fw_iso_context;

typedef void (*fw_iso_callback_t)(struct fw_iso_context *context,
				  u32 cycle,
				  size_t header_length,
				  void *header,
				  void *data);

/*
 * An iso buffer is just a set of pages mapped for DMA in the
 * specified direction.  Since the pages are to be used for DMA, they
 * are not mapped into the kernel virtual address space.  We store the
 * DMA address in the page private. The helper function
 * fw_iso_buffer_map() will map the pages into a given vma.
 */

struct fw_iso_buffer {
	enum dma_data_direction direction;
	struct page **pages;
	int page_count;
};

struct fw_iso_context {
	struct fw_card *card;
	int type;
	int channel;
	int speed;
	size_t header_size;
	fw_iso_callback_t callback;
	void *callback_data;
};

int
fw_iso_buffer_init(struct fw_iso_buffer *buffer,
		   struct fw_card *card,
		   int page_count,
		   enum dma_data_direction direction);
int
fw_iso_buffer_map(struct fw_iso_buffer *buffer, struct vm_area_struct *vma);
void
fw_iso_buffer_destroy(struct fw_iso_buffer *buffer, struct fw_card *card);

struct fw_iso_context *
fw_iso_context_create(struct fw_card *card, int type,
		      int channel, int speed, size_t header_size,
		      fw_iso_callback_t callback, void *callback_data);

void
fw_iso_context_destroy(struct fw_iso_context *ctx);

int
fw_iso_context_queue(struct fw_iso_context *ctx,
		     struct fw_iso_packet *packet,
		     struct fw_iso_buffer *buffer,
		     unsigned long payload);

int
fw_iso_context_start(struct fw_iso_context *ctx,
		     int cycle, int sync, int tags);

int
fw_iso_context_stop(struct fw_iso_context *ctx);

struct fw_card_driver {
	const char *name;

	/*
	 * Enable the given card with the given initial config rom.
	 * This function is expected to activate the card, and either
	 * enable the PHY or set the link_on bit and initiate a bus
	 * reset.
	 */
	int (*enable)(struct fw_card *card, u32 *config_rom, size_t length);

	int (*update_phy_reg)(struct fw_card *card, int address,
			      int clear_bits, int set_bits);

	/*
	 * Update the config rom for an enabled card.  This function
	 * should change the config rom that is presented on the bus
	 * an initiate a bus reset.
	 */
	int (*set_config_rom)(struct fw_card *card,
			      u32 *config_rom, size_t length);

	void (*send_request)(struct fw_card *card, struct fw_packet *packet);
	void (*send_response)(struct fw_card *card, struct fw_packet *packet);
	/* Calling cancel is valid once a packet has been submitted. */
	int (*cancel_packet)(struct fw_card *card, struct fw_packet *packet);

	/*
	 * Allow the specified node ID to do direct DMA out and in of
	 * host memory.  The card will disable this for all node when
	 * a bus reset happens, so driver need to reenable this after
	 * bus reset.  Returns 0 on success, -ENODEV if the card
	 * doesn't support this, -ESTALE if the generation doesn't
	 * match.
	 */
	int (*enable_phys_dma)(struct fw_card *card,
			       int node_id, int generation);

	u64 (*get_bus_time)(struct fw_card *card);

	struct fw_iso_context *
	(*allocate_iso_context)(struct fw_card *card,
				int type, size_t header_size);
	void (*free_iso_context)(struct fw_iso_context *ctx);

	int (*start_iso)(struct fw_iso_context *ctx,
			 s32 cycle, u32 sync, u32 tags);

	int (*queue_iso)(struct fw_iso_context *ctx,
			 struct fw_iso_packet *packet,
			 struct fw_iso_buffer *buffer,
			 unsigned long payload);

	int (*stop_iso)(struct fw_iso_context *ctx);
};

int
fw_core_initiate_bus_reset(struct fw_card *card, int short_reset);

void
fw_send_request(struct fw_card *card, struct fw_transaction *t,
		int tcode, int node_id, int generation, int speed,
		unsigned long long offset,
		void *data, size_t length,
		fw_transaction_callback_t callback, void *callback_data);

int fw_cancel_transaction(struct fw_card *card,
			  struct fw_transaction *transaction);

void fw_flush_transactions(struct fw_card *card);

void fw_send_phy_config(struct fw_card *card,
			int node_id, int generation, int gap_count);

/*
 * Called by the topology code to inform the device code of node
 * activity; found, lost, or updated nodes.
 */
void
fw_node_event(struct fw_card *card, struct fw_node *node, int event);

/* API used by card level drivers */

void
fw_card_initialize(struct fw_card *card, const struct fw_card_driver *driver,
		   struct device *device);
int
fw_card_add(struct fw_card *card,
	    u32 max_receive, u32 link_speed, u64 guid);

void
fw_core_remove_card(struct fw_card *card);

void
fw_core_handle_bus_reset(struct fw_card *card,
			 int node_id, int generation,
			 int self_id_count, u32 *self_ids);
void
fw_core_handle_request(struct fw_card *card, struct fw_packet *request);

void
fw_core_handle_response(struct fw_card *card, struct fw_packet *packet);

#endif /* __fw_transaction_h */
