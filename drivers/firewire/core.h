#ifndef _FIREWIRE_CORE_H
#define _FIREWIRE_CORE_H

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
#define PHY_EXTENDED_REGISTERS	0xe0
#define PHY_BUS_SHORT_RESET	0x40
#define PHY_INT_STATUS_BITS	0x3c
#define PHY_ENABLE_ACCEL	0x02
#define PHY_ENABLE_MULTI	0x01
#define PHY_PAGE_SELECT		0xe0

#define BANDWIDTH_AVAILABLE_INITIAL	4915
#define BROADCAST_CHANNEL_INITIAL	(1 << 31 | 31)
#define BROADCAST_CHANNEL_VALID		(1 << 30)

#define CSR_STATE_BIT_CMSTR	(1 << 8)
#define CSR_STATE_BIT_ABDICATE	(1 << 10)

struct fw_card_driver {
	/*
	 * Enable the given card with the given initial config rom.
	 * This function is expected to activate the card, and either
	 * enable the PHY or set the link_on bit and initiate a bus
	 * reset.
	 */
	int (*enable)(struct fw_card *card,
		      const __be32 *config_rom, size_t length);

	int (*read_phy_reg)(struct fw_card *card, int address);
	int (*update_phy_reg)(struct fw_card *card, int address,
			      int clear_bits, int set_bits);

	/*
	 * Update the config rom for an enabled card.  This function
	 * should change the config rom that is presented on the bus
	 * and initiate a bus reset.
	 */
	int (*set_config_rom)(struct fw_card *card,
			      const __be32 *config_rom, size_t length);

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

	u32 (*read_csr)(struct fw_card *card, int csr_offset);
	void (*write_csr)(struct fw_card *card, int csr_offset, u32 value);

	struct fw_iso_context *
	(*allocate_iso_context)(struct fw_card *card,
				int type, int channel, size_t header_size);
	void (*free_iso_context)(struct fw_iso_context *ctx);

	int (*start_iso)(struct fw_iso_context *ctx,
			 s32 cycle, u32 sync, u32 tags);

	int (*set_iso_channels)(struct fw_iso_context *ctx, u64 *channels);

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
int fw_compute_block_crc(__be32 *block);
void fw_schedule_bus_reset(struct fw_card *card, bool delayed, bool short_reset);
void fw_schedule_bm_work(struct fw_card *card, unsigned long delay);

static inline struct fw_card *fw_card_get(struct fw_card *card)
{
	kref_get(&card->kref);

	return card;
}

void fw_card_release(struct kref *kref);

static inline void fw_card_put(struct fw_card *card)
{
	kref_put(&card->kref, fw_card_release);
}


/* -cdev */

extern const struct file_operations fw_device_ops;

void fw_device_cdev_update(struct fw_device *device);
void fw_device_cdev_remove(struct fw_device *device);
void fw_cdev_handle_phy_packet(struct fw_card *card, struct fw_packet *p);


/* -device */

extern struct rw_semaphore fw_device_rwsem;
extern struct idr fw_device_idr;
extern int fw_cdev_major;

struct fw_device *fw_device_get_by_devt(dev_t devt);
int fw_device_set_broadcast_channel(struct device *dev, void *gen);
void fw_node_event(struct fw_card *card, struct fw_node *node, int event);


/* -iso */

int fw_iso_buffer_map(struct fw_iso_buffer *buffer, struct vm_area_struct *vma);
void fw_iso_resource_manage(struct fw_card *card, int generation,
			    u64 channels_mask, int *channel, int *bandwidth,
			    bool allocate, __be32 buffer[2]);


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
	int generation, int self_id_count, u32 *self_ids, bool bm_abdicate);
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
#define TCODE_IS_LINK_INTERNAL(tcode)	((tcode) == 0xe)
#define TCODE_IS_REQUEST(tcode)		(((tcode) &  2) == 0)
#define TCODE_IS_RESPONSE(tcode)	(((tcode) &  2) != 0)
#define TCODE_HAS_REQUEST_DATA(tcode)	(((tcode) & 12) != 4)
#define TCODE_HAS_RESPONSE_DATA(tcode)	(((tcode) & 12) != 0)

#define LOCAL_BUS 0xffc0

void fw_core_handle_request(struct fw_card *card, struct fw_packet *request);
void fw_core_handle_response(struct fw_card *card, struct fw_packet *packet);
int fw_get_response_length(struct fw_request *request);
void fw_fill_response(struct fw_packet *response, u32 *request_header,
		      int rcode, void *payload, size_t length);

#define FW_PHY_CONFIG_NO_NODE_ID	-1
#define FW_PHY_CONFIG_CURRENT_GAP_COUNT	-1
void fw_send_phy_config(struct fw_card *card,
			int node_id, int generation, int gap_count);

static inline bool is_ping_packet(u32 *data)
{
	return (data[0] & 0xc0ffffff) == 0 && ~data[0] == data[1];
}

#endif /* _FIREWIRE_CORE_H */
