#ifndef _FIREWIRE_CORE_H
#define _FIREWIRE_CORE_H

#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <linux/mm_types.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/atomic.h>

struct device;
struct fw_card;
struct fw_device;
struct fw_iso_buffer;
struct fw_iso_context;
struct fw_iso_packet;
struct fw_node;
struct fw_packet;


/* -card */

/* bitfields within the PHY registers */
#define PHY_LINK_ACTIVE		0x80
#define PHY_CONTENDER		0x40
#define PHY_BUS_RESET		0x40
#define PHY_BUS_SHORT_RESET	0x40

#define BANDWIDTH_AVAILABLE_INITIAL	4915
#define BROADCAST_CHANNEL_INITIAL	(1 << 31 | 31)
#define BROADCAST_CHANNEL_VALID		(1 << 30)

struct fw_card_driver {
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
				int type, int channel, size_t header_size);
	void (*free_iso_context)(struct fw_iso_context *ctx);

	int (*start_iso)(struct fw_iso_context *ctx,
			 s32 cycle, u32 sync, u32 tags);

	int (*queue_iso)(struct fw_iso_context *ctx,
			 struct fw_iso_packet *packet,
			 struct fw_iso_buffer *buffer,
			 unsigned long payload);

	int (*stop_iso)(struct fw_iso_context *ctx);
};

void fw_card_initialize(struct fw_card *card,
		const struct fw_card_driver *driver, struct device *device);
int fw_card_add(struct fw_card *card,
		u32 max_receive, u32 link_speed, u64 guid);
void fw_core_remove_card(struct fw_card *card);
int fw_core_initiate_bus_reset(struct fw_card *card, int short_reset);
int fw_compute_block_crc(u32 *block);
void fw_schedule_bm_work(struct fw_card *card, unsigned long delay);

struct fw_descriptor {
	struct list_head link;
	size_t length;
	u32 immediate;
	u32 key;
	const u32 *data;
};

int fw_core_add_descriptor(struct fw_descriptor *desc);
void fw_core_remove_descriptor(struct fw_descriptor *desc);


/* -cdev */

extern const struct file_operations fw_device_ops;

void fw_device_cdev_update(struct fw_device *device);
void fw_device_cdev_remove(struct fw_device *device);


/* -device */

extern struct rw_semaphore fw_device_rwsem;
extern struct idr fw_device_idr;
extern int fw_cdev_major;

struct fw_device *fw_device_get_by_devt(dev_t devt);
int fw_device_set_broadcast_channel(struct device *dev, void *gen);
void fw_node_event(struct fw_card *card, struct fw_node *node, int event);


/* -iso */

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
	u32 interrupt:1;	/* Generate interrupt on this packet */
	u32 skip:1;		/* Set to not send packet at all. */
	u32 tag:2;
	u32 sy:4;
	u32 header_length:8;	/* Length of immediate header. */
	u32 header[0];
};

#define FW_ISO_CONTEXT_TRANSMIT	0
#define FW_ISO_CONTEXT_RECEIVE	1

#define FW_ISO_CONTEXT_MATCH_TAG0	 1
#define FW_ISO_CONTEXT_MATCH_TAG1	 2
#define FW_ISO_CONTEXT_MATCH_TAG2	 4
#define FW_ISO_CONTEXT_MATCH_TAG3	 8
#define FW_ISO_CONTEXT_MATCH_ALL_TAGS	15

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

typedef void (*fw_iso_callback_t)(struct fw_iso_context *context,
				  u32 cycle, size_t header_length,
				  void *header, void *data);

struct fw_iso_context {
	struct fw_card *card;
	int type;
	int channel;
	int speed;
	size_t header_size;
	fw_iso_callback_t callback;
	void *callback_data;
};

int fw_iso_buffer_init(struct fw_iso_buffer *buffer, struct fw_card *card,
		       int page_count, enum dma_data_direction direction);
int fw_iso_buffer_map(struct fw_iso_buffer *buffer, struct vm_area_struct *vma);
void fw_iso_buffer_destroy(struct fw_iso_buffer *buffer, struct fw_card *card);

struct fw_iso_context *fw_iso_context_create(struct fw_card *card,
		int type, int channel, int speed, size_t header_size,
		fw_iso_callback_t callback, void *callback_data);
int fw_iso_context_queue(struct fw_iso_context *ctx,
			 struct fw_iso_packet *packet,
			 struct fw_iso_buffer *buffer,
			 unsigned long payload);
int fw_iso_context_start(struct fw_iso_context *ctx,
			 int cycle, int sync, int tags);
int fw_iso_context_stop(struct fw_iso_context *ctx);
void fw_iso_context_destroy(struct fw_iso_context *ctx);

void fw_iso_resource_manage(struct fw_card *card, int generation,
		u64 channels_mask, int *channel, int *bandwidth, bool allocate);


/* -topology */

enum {
	FW_NODE_CREATED,
	FW_NODE_UPDATED,
	FW_NODE_DESTROYED,
	FW_NODE_LINK_ON,
	FW_NODE_LINK_OFF,
	FW_NODE_INITIATED_RESET,
};

struct fw_node {
	u16 node_id;
	u8 color;
	u8 port_count;
	u8 link_on:1;
	u8 initiated_reset:1;
	u8 b_path:1;
	u8 phy_speed:2;	/* As in the self ID packet. */
	u8 max_speed:2;	/* Minimum of all phy-speeds on the path from the
			 * local node to this node. */
	u8 max_depth:4;	/* Maximum depth to any leaf node */
	u8 max_hops:4;	/* Max hops in this sub tree */
	atomic_t ref_count;

	/* For serializing node topology into a list. */
	struct list_head link;

	/* Upper layer specific data. */
	void *data;

	struct fw_node *ports[0];
};

static inline struct fw_node *fw_node_get(struct fw_node *node)
{
	atomic_inc(&node->ref_count);

	return node;
}

static inline void fw_node_put(struct fw_node *node)
{
	if (atomic_dec_and_test(&node->ref_count))
		kfree(node);
}

void fw_core_handle_bus_reset(struct fw_card *card, int node_id,
			      int generation, int self_id_count, u32 *self_ids);
void fw_destroy_nodes(struct fw_card *card);

/*
 * Check whether new_generation is the immediate successor of old_generation.
 * Take counter roll-over at 255 (as per OHCI) into account.
 */
static inline bool is_next_generation(int new_generation, int old_generation)
{
	return (new_generation & 0xff) == ((old_generation + 1) & 0xff);
}


/* -transaction */

#define TCODE_IS_READ_REQUEST(tcode)	(((tcode) & ~1) == 4)
#define TCODE_IS_BLOCK_PACKET(tcode)	(((tcode) &  1) != 0)
#define TCODE_IS_REQUEST(tcode)		(((tcode) &  2) == 0)
#define TCODE_IS_RESPONSE(tcode)	(((tcode) &  2) != 0)
#define TCODE_HAS_REQUEST_DATA(tcode)	(((tcode) & 12) != 4)
#define TCODE_HAS_RESPONSE_DATA(tcode)	(((tcode) & 12) != 0)

#define LOCAL_BUS 0xffc0

void fw_core_handle_request(struct fw_card *card, struct fw_packet *request);
void fw_core_handle_response(struct fw_card *card, struct fw_packet *packet);
void fw_fill_response(struct fw_packet *response, u32 *request_header,
		      int rcode, void *payload, size_t length);
void fw_flush_transactions(struct fw_card *card);
void fw_send_phy_config(struct fw_card *card,
			int node_id, int generation, int gap_count);

static inline int fw_stream_packet_destination_id(int tag, int channel, int sy)
{
	return tag << 14 | channel << 8 | sy;
}

#endif /* _FIREWIRE_CORE_H */
