// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2005-2007  Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/crc-itu-t.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <linux/atomic.h>
#include <asm/byteorder.h>

#include "core.h"

#define define_fw_printk_level(func, kern_level)		\
void func(const struct fw_card *card, const char *fmt, ...)	\
{								\
	struct va_format vaf;					\
	va_list args;						\
								\
	va_start(args, fmt);					\
	vaf.fmt = fmt;						\
	vaf.va = &args;						\
	printk(kern_level KBUILD_MODNAME " %s: %pV",		\
	       dev_name(card->device), &vaf);			\
	va_end(args);						\
}
define_fw_printk_level(fw_err, KERN_ERR);
define_fw_printk_level(fw_notice, KERN_NOTICE);

int fw_compute_block_crc(__be32 *block)
{
	int length;
	u16 crc;

	length = (be32_to_cpu(block[0]) >> 16) & 0xff;
	crc = crc_itu_t(0, (u8 *)&block[1], length * 4);
	*block |= cpu_to_be32(crc);

	return length;
}

static DEFINE_MUTEX(card_mutex);
static LIST_HEAD(card_list);

static LIST_HEAD(descriptor_list);
static int descriptor_count;

static __be32 tmp_config_rom[256];
/* ROM header, bus info block, root dir header, capabilities = 7 quadlets */
static size_t config_rom_length = 1 + 4 + 1 + 1;

#define BIB_CRC(v)		((v) <<  0)
#define BIB_CRC_LENGTH(v)	((v) << 16)
#define BIB_INFO_LENGTH(v)	((v) << 24)
#define BIB_BUS_NAME		0x31333934 /* "1394" */
#define BIB_LINK_SPEED(v)	((v) <<  0)
#define BIB_GENERATION(v)	((v) <<  4)
#define BIB_MAX_ROM(v)		((v) <<  8)
#define BIB_MAX_RECEIVE(v)	((v) << 12)
#define BIB_CYC_CLK_ACC(v)	((v) << 16)
#define BIB_PMC			((1) << 27)
#define BIB_BMC			((1) << 28)
#define BIB_ISC			((1) << 29)
#define BIB_CMC			((1) << 30)
#define BIB_IRMC		((1) << 31)
#define NODE_CAPABILITIES	0x0c0083c0 /* per IEEE 1394 clause 8.3.2.6.5.2 */

/*
 * IEEE-1394 specifies a default SPLIT_TIMEOUT value of 800 cycles (100 ms),
 * but we have to make it longer because there are many devices whose firmware
 * is just too slow for that.
 */
#define DEFAULT_SPLIT_TIMEOUT	(2 * 8000)

#define CANON_OUI		0x000085

static void generate_config_rom(struct fw_card *card, __be32 *config_rom)
{
	struct fw_descriptor *desc;
	int i, j, k, length;

	/*
	 * Initialize contents of config rom buffer.  On the OHCI
	 * controller, block reads to the config rom accesses the host
	 * memory, but quadlet read access the hardware bus info block
	 * registers.  That's just crack, but it means we should make
	 * sure the contents of bus info block in host memory matches
	 * the version stored in the OHCI registers.
	 */

	config_rom[0] = cpu_to_be32(
		BIB_CRC_LENGTH(4) | BIB_INFO_LENGTH(4) | BIB_CRC(0));
	config_rom[1] = cpu_to_be32(BIB_BUS_NAME);
	config_rom[2] = cpu_to_be32(
		BIB_LINK_SPEED(card->link_speed) |
		BIB_GENERATION(card->config_rom_generation++ % 14 + 2) |
		BIB_MAX_ROM(2) |
		BIB_MAX_RECEIVE(card->max_receive) |
		BIB_BMC | BIB_ISC | BIB_CMC | BIB_IRMC);
	config_rom[3] = cpu_to_be32(card->guid >> 32);
	config_rom[4] = cpu_to_be32(card->guid);

	/* Generate root directory. */
	config_rom[6] = cpu_to_be32(NODE_CAPABILITIES);
	i = 7;
	j = 7 + descriptor_count;

	/* Generate root directory entries for descriptors. */
	list_for_each_entry (desc, &descriptor_list, link) {
		if (desc->immediate > 0)
			config_rom[i++] = cpu_to_be32(desc->immediate);
		config_rom[i] = cpu_to_be32(desc->key | (j - i));
		i++;
		j += desc->length;
	}

	/* Update root directory length. */
	config_rom[5] = cpu_to_be32((i - 5 - 1) << 16);

	/* End of root directory, now copy in descriptors. */
	list_for_each_entry (desc, &descriptor_list, link) {
		for (k = 0; k < desc->length; k++)
			config_rom[i + k] = cpu_to_be32(desc->data[k]);
		i += desc->length;
	}

	/* Calculate CRCs for all blocks in the config rom.  This
	 * assumes that CRC length and info length are identical for
	 * the bus info block, which is always the case for this
	 * implementation. */
	for (i = 0; i < j; i += length + 1)
		length = fw_compute_block_crc(config_rom + i);

	WARN_ON(j != config_rom_length);
}

static void update_config_roms(void)
{
	struct fw_card *card;

	list_for_each_entry (card, &card_list, link) {
		generate_config_rom(card, tmp_config_rom);
		card->driver->set_config_rom(card, tmp_config_rom,
					     config_rom_length);
	}
}

static size_t required_space(struct fw_descriptor *desc)
{
	/* descriptor + entry into root dir + optional immediate entry */
	return desc->length + 1 + (desc->immediate > 0 ? 1 : 0);
}

int fw_core_add_descriptor(struct fw_descriptor *desc)
{
	size_t i;
	int ret;

	/*
	 * Check descriptor is valid; the length of all blocks in the
	 * descriptor has to add up to exactly the length of the
	 * block.
	 */
	i = 0;
	while (i < desc->length)
		i += (desc->data[i] >> 16) + 1;

	if (i != desc->length)
		return -EINVAL;

	mutex_lock(&card_mutex);

	if (config_rom_length + required_space(desc) > 256) {
		ret = -EBUSY;
	} else {
		list_add_tail(&desc->link, &descriptor_list);
		config_rom_length += required_space(desc);
		descriptor_count++;
		if (desc->immediate > 0)
			descriptor_count++;
		update_config_roms();
		ret = 0;
	}

	mutex_unlock(&card_mutex);

	return ret;
}
EXPORT_SYMBOL(fw_core_add_descriptor);

void fw_core_remove_descriptor(struct fw_descriptor *desc)
{
	mutex_lock(&card_mutex);

	list_del(&desc->link);
	config_rom_length -= required_space(desc);
	descriptor_count--;
	if (desc->immediate > 0)
		descriptor_count--;
	update_config_roms();

	mutex_unlock(&card_mutex);
}
EXPORT_SYMBOL(fw_core_remove_descriptor);

static int reset_bus(struct fw_card *card, bool short_reset)
{
	int reg = short_reset ? 5 : 1;
	int bit = short_reset ? PHY_BUS_SHORT_RESET : PHY_BUS_RESET;

	return card->driver->update_phy_reg(card, reg, 0, bit);
}

void fw_schedule_bus_reset(struct fw_card *card, bool delayed, bool short_reset)
{
	/* We don't try hard to sort out requests of long vs. short resets. */
	card->br_short = short_reset;

	/* Use an arbitrary short delay to combine multiple reset requests. */
	fw_card_get(card);
	if (!queue_delayed_work(fw_workqueue, &card->br_work,
				delayed ? DIV_ROUND_UP(HZ, 100) : 0))
		fw_card_put(card);
}
EXPORT_SYMBOL(fw_schedule_bus_reset);

static void br_work(struct work_struct *work)
{
	struct fw_card *card = container_of(work, struct fw_card, br_work.work);

	/* Delay for 2s after last reset per IEEE 1394 clause 8.2.1. */
	if (card->reset_jiffies != 0 &&
	    time_before64(get_jiffies_64(), card->reset_jiffies + 2 * HZ)) {
		if (!queue_delayed_work(fw_workqueue, &card->br_work, 2 * HZ))
			fw_card_put(card);
		return;
	}

	fw_send_phy_config(card, FW_PHY_CONFIG_NO_NODE_ID, card->generation,
			   FW_PHY_CONFIG_CURRENT_GAP_COUNT);
	reset_bus(card, card->br_short);
	fw_card_put(card);
}

static void allocate_broadcast_channel(struct fw_card *card, int generation)
{
	int channel, bandwidth = 0;

	if (!card->broadcast_channel_allocated) {
		fw_iso_resource_manage(card, generation, 1ULL << 31,
				       &channel, &bandwidth, true);
		if (channel != 31) {
			fw_notice(card, "failed to allocate broadcast channel\n");
			return;
		}
		card->broadcast_channel_allocated = true;
	}

	device_for_each_child(card->device, (void *)(long)generation,
			      fw_device_set_broadcast_channel);
}

static const char gap_count_table[] = {
	63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40
};

void fw_schedule_bm_work(struct fw_card *card, unsigned long delay)
{
	fw_card_get(card);
	if (!schedule_delayed_work(&card->bm_work, delay))
		fw_card_put(card);
}

static void bm_work(struct work_struct *work)
{
	struct fw_card *card = container_of(work, struct fw_card, bm_work.work);
	struct fw_device *root_device, *irm_device;
	struct fw_node *root_node;
	int root_id, new_root_id, irm_id, bm_id, local_id;
	int gap_count, generation, grace, rcode;
	bool do_reset = false;
	bool root_device_is_running;
	bool root_device_is_cmc;
	bool irm_is_1394_1995_only;
	bool keep_this_irm;
	__be32 transaction_data[2];

	spin_lock_irq(&card->lock);

	if (card->local_node == NULL) {
		spin_unlock_irq(&card->lock);
		goto out_put_card;
	}

	generation = card->generation;

	root_node = card->root_node;
	fw_node_get(root_node);
	root_device = root_node->data;
	root_device_is_running = root_device &&
			atomic_read(&root_device->state) == FW_DEVICE_RUNNING;
	root_device_is_cmc = root_device && root_device->cmc;

	irm_device = card->irm_node->data;
	irm_is_1394_1995_only = irm_device && irm_device->config_rom &&
			(irm_device->config_rom[2] & 0x000000f0) == 0;

	/* Canon MV5i works unreliably if it is not root node. */
	keep_this_irm = irm_device && irm_device->config_rom &&
			irm_device->config_rom[3] >> 8 == CANON_OUI;

	root_id  = root_node->node_id;
	irm_id   = card->irm_node->node_id;
	local_id = card->local_node->node_id;

	grace = time_after64(get_jiffies_64(),
			     card->reset_jiffies + DIV_ROUND_UP(HZ, 8));

	if ((is_next_generation(generation, card->bm_generation) &&
	     !card->bm_abdicate) ||
	    (card->bm_generation != generation && grace)) {
		/*
		 * This first step is to figure out who is IRM and
		 * then try to become bus manager.  If the IRM is not
		 * well defined (e.g. does not have an active link
		 * layer or does not responds to our lock request, we
		 * will have to do a little vigilante bus management.
		 * In that case, we do a goto into the gap count logic
		 * so that when we do the reset, we still optimize the
		 * gap count.  That could well save a reset in the
		 * next generation.
		 */

		if (!card->irm_node->link_on) {
			new_root_id = local_id;
			fw_notice(card, "%s, making local node (%02x) root\n",
				  "IRM has link off", new_root_id);
			goto pick_me;
		}

		if (irm_is_1394_1995_only && !keep_this_irm) {
			new_root_id = local_id;
			fw_notice(card, "%s, making local node (%02x) root\n",
				  "IRM is not 1394a compliant", new_root_id);
			goto pick_me;
		}

		transaction_data[0] = cpu_to_be32(0x3f);
		transaction_data[1] = cpu_to_be32(local_id);

		spin_unlock_irq(&card->lock);

		rcode = fw_run_transaction(card, TCODE_LOCK_COMPARE_SWAP,
				irm_id, generation, SCODE_100,
				CSR_REGISTER_BASE + CSR_BUS_MANAGER_ID,
				transaction_data, 8);

		if (rcode == RCODE_GENERATION)
			/* Another bus reset, BM work has been rescheduled. */
			goto out;

		bm_id = be32_to_cpu(transaction_data[0]);

		spin_lock_irq(&card->lock);
		if (rcode == RCODE_COMPLETE && generation == card->generation)
			card->bm_node_id =
			    bm_id == 0x3f ? local_id : 0xffc0 | bm_id;
		spin_unlock_irq(&card->lock);

		if (rcode == RCODE_COMPLETE && bm_id != 0x3f) {
			/* Somebody else is BM.  Only act as IRM. */
			if (local_id == irm_id)
				allocate_broadcast_channel(card, generation);

			goto out;
		}

		if (rcode == RCODE_SEND_ERROR) {
			/*
			 * We have been unable to send the lock request due to
			 * some local problem.  Let's try again later and hope
			 * that the problem has gone away by then.
			 */
			fw_schedule_bm_work(card, DIV_ROUND_UP(HZ, 8));
			goto out;
		}

		spin_lock_irq(&card->lock);

		if (rcode != RCODE_COMPLETE && !keep_this_irm) {
			/*
			 * The lock request failed, maybe the IRM
			 * isn't really IRM capable after all. Let's
			 * do a bus reset and pick the local node as
			 * root, and thus, IRM.
			 */
			new_root_id = local_id;
			fw_notice(card, "BM lock failed (%s), making local node (%02x) root\n",
				  fw_rcode_string(rcode), new_root_id);
			goto pick_me;
		}
	} else if (card->bm_generation != generation) {
		/*
		 * We weren't BM in the last generation, and the last
		 * bus reset is less than 125ms ago.  Reschedule this job.
		 */
		spin_unlock_irq(&card->lock);
		fw_schedule_bm_work(card, DIV_ROUND_UP(HZ, 8));
		goto out;
	}

	/*
	 * We're bus manager for this generation, so next step is to
	 * make sure we have an active cycle master and do gap count
	 * optimization.
	 */
	card->bm_generation = generation;

	if (root_device == NULL) {
		/*
		 * Either link_on is false, or we failed to read the
		 * config rom.  In either case, pick another root.
		 */
		new_root_id = local_id;
	} else if (!root_device_is_running) {
		/*
		 * If we haven't probed this device yet, bail out now
		 * and let's try again once that's done.
		 */
		spin_unlock_irq(&card->lock);
		goto out;
	} else if (root_device_is_cmc) {
		/*
		 * We will send out a force root packet for this
		 * node as part of the gap count optimization.
		 */
		new_root_id = root_id;
	} else {
		/*
		 * Current root has an active link layer and we
		 * successfully read the config rom, but it's not
		 * cycle master capable.
		 */
		new_root_id = local_id;
	}

 pick_me:
	/*
	 * Pick a gap count from 1394a table E-1.  The table doesn't cover
	 * the typically much larger 1394b beta repeater delays though.
	 */
	if (!card->beta_repeaters_present &&
	    root_node->max_hops < ARRAY_SIZE(gap_count_table))
		gap_count = gap_count_table[root_node->max_hops];
	else
		gap_count = 63;

	/*
	 * Finally, figure out if we should do a reset or not.  If we have
	 * done less than 5 resets with the same physical topology and we
	 * have either a new root or a new gap count setting, let's do it.
	 */

	if (card->bm_retries++ < 5 &&
	    (card->gap_count != gap_count || new_root_id != root_id))
		do_reset = true;

	spin_unlock_irq(&card->lock);

	if (do_reset) {
		fw_notice(card, "phy config: new root=%x, gap_count=%d\n",
			  new_root_id, gap_count);
		fw_send_phy_config(card, new_root_id, generation, gap_count);
		reset_bus(card, true);
		/* Will allocate broadcast channel after the reset. */
		goto out;
	}

	if (root_device_is_cmc) {
		/*
		 * Make sure that the cycle master sends cycle start packets.
		 */
		transaction_data[0] = cpu_to_be32(CSR_STATE_BIT_CMSTR);
		rcode = fw_run_transaction(card, TCODE_WRITE_QUADLET_REQUEST,
				root_id, generation, SCODE_100,
				CSR_REGISTER_BASE + CSR_STATE_SET,
				transaction_data, 4);
		if (rcode == RCODE_GENERATION)
			goto out;
	}

	if (local_id == irm_id)
		allocate_broadcast_channel(card, generation);

 out:
	fw_node_put(root_node);
 out_put_card:
	fw_card_put(card);
}

void fw_card_initialize(struct fw_card *card,
			const struct fw_card_driver *driver,
			struct device *device)
{
	static atomic_t index = ATOMIC_INIT(-1);

	card->index = atomic_inc_return(&index);
	card->driver = driver;
	card->device = device;
	card->current_tlabel = 0;
	card->tlabel_mask = 0;
	card->split_timeout_hi = DEFAULT_SPLIT_TIMEOUT / 8000;
	card->split_timeout_lo = (DEFAULT_SPLIT_TIMEOUT % 8000) << 19;
	card->split_timeout_cycles = DEFAULT_SPLIT_TIMEOUT;
	card->split_timeout_jiffies =
			DIV_ROUND_UP(DEFAULT_SPLIT_TIMEOUT * HZ, 8000);
	card->color = 0;
	card->broadcast_channel = BROADCAST_CHANNEL_INITIAL;

	kref_init(&card->kref);
	init_completion(&card->done);
	INIT_LIST_HEAD(&card->transaction_list);
	INIT_LIST_HEAD(&card->phy_receiver_list);
	spin_lock_init(&card->lock);

	card->local_node = NULL;

	INIT_DELAYED_WORK(&card->br_work, br_work);
	INIT_DELAYED_WORK(&card->bm_work, bm_work);
}
EXPORT_SYMBOL(fw_card_initialize);

int fw_card_add(struct fw_card *card,
		u32 max_receive, u32 link_speed, u64 guid)
{
	int ret;

	card->max_receive = max_receive;
	card->link_speed = link_speed;
	card->guid = guid;

	mutex_lock(&card_mutex);

	generate_config_rom(card, tmp_config_rom);
	ret = card->driver->enable(card, tmp_config_rom, config_rom_length);
	if (ret == 0)
		list_add_tail(&card->link, &card_list);

	mutex_unlock(&card_mutex);

	return ret;
}
EXPORT_SYMBOL(fw_card_add);

/*
 * The next few functions implement a dummy driver that is used once a card
 * driver shuts down an fw_card.  This allows the driver to cleanly unload,
 * as all IO to the card will be handled (and failed) by the dummy driver
 * instead of calling into the module.  Only functions for iso context
 * shutdown still need to be provided by the card driver.
 *
 * .read/write_csr() should never be called anymore after the dummy driver
 * was bound since they are only used within request handler context.
 * .set_config_rom() is never called since the card is taken out of card_list
 * before switching to the dummy driver.
 */

static int dummy_read_phy_reg(struct fw_card *card, int address)
{
	return -ENODEV;
}

static int dummy_update_phy_reg(struct fw_card *card, int address,
				int clear_bits, int set_bits)
{
	return -ENODEV;
}

static void dummy_send_request(struct fw_card *card, struct fw_packet *packet)
{
	packet->callback(packet, card, RCODE_CANCELLED);
}

static void dummy_send_response(struct fw_card *card, struct fw_packet *packet)
{
	packet->callback(packet, card, RCODE_CANCELLED);
}

static int dummy_cancel_packet(struct fw_card *card, struct fw_packet *packet)
{
	return -ENOENT;
}

static int dummy_enable_phys_dma(struct fw_card *card,
				 int node_id, int generation)
{
	return -ENODEV;
}

static struct fw_iso_context *dummy_allocate_iso_context(struct fw_card *card,
				int type, int channel, size_t header_size)
{
	return ERR_PTR(-ENODEV);
}

static int dummy_start_iso(struct fw_iso_context *ctx,
			   s32 cycle, u32 sync, u32 tags)
{
	return -ENODEV;
}

static int dummy_set_iso_channels(struct fw_iso_context *ctx, u64 *channels)
{
	return -ENODEV;
}

static int dummy_queue_iso(struct fw_iso_context *ctx, struct fw_iso_packet *p,
			   struct fw_iso_buffer *buffer, unsigned long payload)
{
	return -ENODEV;
}

static void dummy_flush_queue_iso(struct fw_iso_context *ctx)
{
}

static int dummy_flush_iso_completions(struct fw_iso_context *ctx)
{
	return -ENODEV;
}

static const struct fw_card_driver dummy_driver_template = {
	.read_phy_reg		= dummy_read_phy_reg,
	.update_phy_reg		= dummy_update_phy_reg,
	.send_request		= dummy_send_request,
	.send_response		= dummy_send_response,
	.cancel_packet		= dummy_cancel_packet,
	.enable_phys_dma	= dummy_enable_phys_dma,
	.allocate_iso_context	= dummy_allocate_iso_context,
	.start_iso		= dummy_start_iso,
	.set_iso_channels	= dummy_set_iso_channels,
	.queue_iso		= dummy_queue_iso,
	.flush_queue_iso	= dummy_flush_queue_iso,
	.flush_iso_completions	= dummy_flush_iso_completions,
};

void fw_card_release(struct kref *kref)
{
	struct fw_card *card = container_of(kref, struct fw_card, kref);

	complete(&card->done);
}
EXPORT_SYMBOL_GPL(fw_card_release);

void fw_core_remove_card(struct fw_card *card)
{
	struct fw_card_driver dummy_driver = dummy_driver_template;
	unsigned long flags;

	card->driver->update_phy_reg(card, 4,
				     PHY_LINK_ACTIVE | PHY_CONTENDER, 0);
	fw_schedule_bus_reset(card, false, true);

	mutex_lock(&card_mutex);
	list_del_init(&card->link);
	mutex_unlock(&card_mutex);

	/* Switch off most of the card driver interface. */
	dummy_driver.free_iso_context	= card->driver->free_iso_context;
	dummy_driver.stop_iso		= card->driver->stop_iso;
	card->driver = &dummy_driver;

	spin_lock_irqsave(&card->lock, flags);
	fw_destroy_nodes(card);
	spin_unlock_irqrestore(&card->lock, flags);

	/* Wait for all users, especially device workqueue jobs, to finish. */
	fw_card_put(card);
	wait_for_completion(&card->done);

	WARN_ON(!list_empty(&card->transaction_list));
}
EXPORT_SYMBOL(fw_core_remove_card);
