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
#include <trace/events/firewire.h>

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

	guard(mutex)(&card_mutex);

	if (config_rom_length + required_space(desc) > 256)
		return -EBUSY;

	list_add_tail(&desc->link, &descriptor_list);
	config_rom_length += required_space(desc);
	descriptor_count++;
	if (desc->immediate > 0)
		descriptor_count++;
	update_config_roms();

	return 0;
}
EXPORT_SYMBOL(fw_core_add_descriptor);

void fw_core_remove_descriptor(struct fw_descriptor *desc)
{
	guard(mutex)(&card_mutex);

	list_del(&desc->link);
	config_rom_length -= required_space(desc);
	descriptor_count--;
	if (desc->immediate > 0)
		descriptor_count--;
	update_config_roms();
}
EXPORT_SYMBOL(fw_core_remove_descriptor);

static int reset_bus(struct fw_card *card, bool short_reset)
{
	int reg = short_reset ? 5 : 1;
	int bit = short_reset ? PHY_BUS_SHORT_RESET : PHY_BUS_RESET;

	trace_bus_reset_initiate(card->index, card->generation, short_reset);

	return card->driver->update_phy_reg(card, reg, 0, bit);
}

void fw_schedule_bus_reset(struct fw_card *card, bool delayed, bool short_reset)
{
	trace_bus_reset_schedule(card->index, card->generation, short_reset);

	/* We don't try hard to sort out requests of long vs. short resets. */
	card->br_short = short_reset;

	/* Use an arbitrary short delay to combine multiple reset requests. */
	fw_card_get(card);
	if (!queue_delayed_work(fw_workqueue, &card->br_work, delayed ? msecs_to_jiffies(10) : 0))
		fw_card_put(card);
}
EXPORT_SYMBOL(fw_schedule_bus_reset);

static void br_work(struct work_struct *work)
{
	struct fw_card *card = from_work(card, work, br_work.work);

	/* Delay for 2s after last reset per IEEE 1394 clause 8.2.1. */
	if (card->reset_jiffies != 0 &&
	    time_is_after_jiffies64(card->reset_jiffies + secs_to_jiffies(2))) {
		trace_bus_reset_postpone(card->index, card->generation, card->br_short);

		if (!queue_delayed_work(fw_workqueue, &card->br_work, secs_to_jiffies(2)))
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

void fw_schedule_bm_work(struct fw_card *card, unsigned long delay)
{
	fw_card_get(card);
	if (!schedule_delayed_work(&card->bm_work, delay))
		fw_card_put(card);
}

enum bm_contention_outcome {
	// The bus management contention window is not expired.
	BM_CONTENTION_OUTCOME_WITHIN_WINDOW = 0,
	// The IRM node has link off.
	BM_CONTENTION_OUTCOME_IRM_HAS_LINK_OFF,
	// The IRM node complies IEEE 1394:1994 only.
	BM_CONTENTION_OUTCOME_IRM_COMPLIES_1394_1995_ONLY,
	// Another bus reset, BM work has been rescheduled.
	BM_CONTENTION_OUTCOME_AT_NEW_GENERATION,
	// We have been unable to send the lock request to IRM node due to some local problem.
	BM_CONTENTION_OUTCOME_LOCAL_PROBLEM_AT_TRANSACTION,
	// The lock request failed, maybe the IRM isn't really IRM capable after all.
	BM_CONTENTION_OUTCOME_IRM_IS_NOT_CAPABLE_FOR_IRM,
	// Somebody else is BM.
	BM_CONTENTION_OUTCOME_IRM_HOLDS_ANOTHER_NODE_AS_BM,
	// The local node succeeds after contending for bus manager.
	BM_CONTENTION_OUTCOME_IRM_HOLDS_LOCAL_NODE_AS_BM,
};

static enum bm_contention_outcome contend_for_bm(struct fw_card *card)
__must_hold(&card->lock)
{
	int generation = card->generation;
	int local_id = card->local_node->node_id;
	__be32 data[2] = {
		cpu_to_be32(BUS_MANAGER_ID_NOT_REGISTERED),
		cpu_to_be32(local_id),
	};
	bool grace = time_is_before_jiffies64(card->reset_jiffies + msecs_to_jiffies(125));
	bool irm_is_1394_1995_only = false;
	bool keep_this_irm = false;
	struct fw_node *irm_node;
	struct fw_device *irm_device;
	int irm_node_id;
	int rcode;

	lockdep_assert_held(&card->lock);

	if (!grace) {
		if (!is_next_generation(generation, card->bm_generation) || card->bm_abdicate)
			return BM_CONTENTION_OUTCOME_WITHIN_WINDOW;
	}

	irm_node = card->irm_node;
	if (!irm_node->link_on) {
		fw_notice(card, "IRM has link off, making local node (%02x) root\n", local_id);
		return BM_CONTENTION_OUTCOME_IRM_HAS_LINK_OFF;
	}

	irm_device = fw_node_get_device(irm_node);
	if (irm_device && irm_device->config_rom) {
		irm_is_1394_1995_only = (irm_device->config_rom[2] & 0x000000f0) == 0;

		// Canon MV5i works unreliably if it is not root node.
		keep_this_irm = irm_device->config_rom[3] >> 8 == CANON_OUI;
	}

	if (irm_is_1394_1995_only && !keep_this_irm) {
		fw_notice(card, "IRM is not 1394a compliant, making local node (%02x) root\n",
			  local_id);
		return BM_CONTENTION_OUTCOME_IRM_COMPLIES_1394_1995_ONLY;
	}

	irm_node_id = irm_node->node_id;

	spin_unlock_irq(&card->lock);

	rcode = fw_run_transaction(card, TCODE_LOCK_COMPARE_SWAP, irm_node_id, generation,
				   SCODE_100, CSR_REGISTER_BASE + CSR_BUS_MANAGER_ID, data,
				   sizeof(data));

	spin_lock_irq(&card->lock);

	switch (rcode) {
	case RCODE_GENERATION:
		return BM_CONTENTION_OUTCOME_AT_NEW_GENERATION;
	case RCODE_SEND_ERROR:
		return BM_CONTENTION_OUTCOME_LOCAL_PROBLEM_AT_TRANSACTION;
	case RCODE_COMPLETE:
	{
		int bm_id = be32_to_cpu(data[0]);

		// Used by cdev layer for "struct fw_cdev_event_bus_reset".
		if (bm_id != BUS_MANAGER_ID_NOT_REGISTERED)
			card->bm_node_id = 0xffc0 & bm_id;
		else
			card->bm_node_id = local_id;

		if (bm_id != BUS_MANAGER_ID_NOT_REGISTERED)
			return BM_CONTENTION_OUTCOME_IRM_HOLDS_ANOTHER_NODE_AS_BM;
		else
			return BM_CONTENTION_OUTCOME_IRM_HOLDS_LOCAL_NODE_AS_BM;
	}
	default:
		if (!keep_this_irm) {
			fw_notice(card, "BM lock failed (%s), making local node (%02x) root\n",
				  fw_rcode_string(rcode), local_id);
			return BM_CONTENTION_OUTCOME_IRM_COMPLIES_1394_1995_ONLY;
		} else {
			return BM_CONTENTION_OUTCOME_IRM_IS_NOT_CAPABLE_FOR_IRM;
		}
	}
}

DEFINE_FREE(node_unref, struct fw_node *, if (_T) fw_node_put(_T))
DEFINE_FREE(card_unref, struct fw_card *, if (_T) fw_card_put(_T))

static void bm_work(struct work_struct *work)
{
	static const char gap_count_table[] = {
		63, 5, 7, 8, 10, 13, 16, 18, 21, 24, 26, 29, 32, 35, 37, 40
	};
	struct fw_card *card __free(card_unref) = from_work(card, work, bm_work.work);
	struct fw_node *root_node __free(node_unref) = NULL;
	int root_id, new_root_id, irm_id, local_id;
	int expected_gap_count, generation;
	bool stand_for_root = false;

	spin_lock_irq(&card->lock);

	if (card->local_node == NULL) {
		spin_unlock_irq(&card->lock);
		return;
	}

	generation = card->generation;

	root_node = fw_node_get(card->root_node);

	root_id  = root_node->node_id;
	irm_id   = card->irm_node->node_id;
	local_id = card->local_node->node_id;

	if (card->bm_generation != generation) {
		enum bm_contention_outcome result = contend_for_bm(card);

		switch (result) {
		case BM_CONTENTION_OUTCOME_WITHIN_WINDOW:
			spin_unlock_irq(&card->lock);
			fw_schedule_bm_work(card, msecs_to_jiffies(125));
			return;
		case BM_CONTENTION_OUTCOME_IRM_HAS_LINK_OFF:
			stand_for_root = true;
			break;
		case BM_CONTENTION_OUTCOME_IRM_COMPLIES_1394_1995_ONLY:
			stand_for_root = true;
			break;
		case BM_CONTENTION_OUTCOME_AT_NEW_GENERATION:
			// BM work has been rescheduled.
			spin_unlock_irq(&card->lock);
			return;
		case BM_CONTENTION_OUTCOME_LOCAL_PROBLEM_AT_TRANSACTION:
			// Let's try again later and hope that the local problem has gone away by
			// then.
			spin_unlock_irq(&card->lock);
			fw_schedule_bm_work(card, msecs_to_jiffies(125));
			return;
		case BM_CONTENTION_OUTCOME_IRM_IS_NOT_CAPABLE_FOR_IRM:
			// Let's do a bus reset and pick the local node as root, and thus, IRM.
			stand_for_root = true;
			break;
		case BM_CONTENTION_OUTCOME_IRM_HOLDS_ANOTHER_NODE_AS_BM:
			if (local_id == irm_id) {
				// Only acts as IRM.
				spin_unlock_irq(&card->lock);
				allocate_broadcast_channel(card, generation);
				spin_lock_irq(&card->lock);
			}
			fallthrough;
		case BM_CONTENTION_OUTCOME_IRM_HOLDS_LOCAL_NODE_AS_BM:
		default:
			card->bm_generation = generation;
			break;
		}
	}

	// We're bus manager for this generation, so next step is to make sure we have an active
	// cycle master and do gap count optimization.
	if (!stand_for_root) {
		if (card->gap_count == GAP_COUNT_MISMATCHED) {
			// If self IDs have inconsistent gap counts, do a
			// bus reset ASAP. The config rom read might never
			// complete, so don't wait for it. However, still
			// send a PHY configuration packet prior to the
			// bus reset. The PHY configuration packet might
			// fail, but 1394-2008 8.4.5.2 explicitly permits
			// it in this case, so it should be safe to try.
			stand_for_root = true;

			// We must always send a bus reset if the gap count
			// is inconsistent, so bypass the 5-reset limit.
			card->bm_retries = 0;
		} else {
			// Now investigate root node.
			struct fw_device *root_device = fw_node_get_device(root_node);

			if (root_device == NULL) {
				// Either link_on is false, or we failed to read the
				// config rom.  In either case, pick another root.
				stand_for_root = true;
			} else {
				bool root_device_is_running =
					atomic_read(&root_device->state) == FW_DEVICE_RUNNING;

				if (!root_device_is_running) {
					// If we haven't probed this device yet, bail out now
					// and let's try again once that's done.
					spin_unlock_irq(&card->lock);
					return;
				} else if (!root_device->cmc) {
					// Current root has an active link layer and we
					// successfully read the config rom, but it's not
					// cycle master capable.
					stand_for_root = true;
				}
			}
		}
	}

	if (stand_for_root) {
		new_root_id = local_id;
	} else {
		// We will send out a force root packet for this node as part of the gap count
		// optimization on behalf of the node.
		new_root_id = root_id;
	}

	/*
	 * Pick a gap count from 1394a table E-1.  The table doesn't cover
	 * the typically much larger 1394b beta repeater delays though.
	 */
	if (!card->beta_repeaters_present &&
	    root_node->max_hops < ARRAY_SIZE(gap_count_table))
		expected_gap_count = gap_count_table[root_node->max_hops];
	else
		expected_gap_count = 63;

	// Finally, figure out if we should do a reset or not. If we have done less than 5 resets
	// with the same physical topology and we have either a new root or a new gap count
	// setting, let's do it.
	if (card->bm_retries++ < 5 && (card->gap_count != expected_gap_count || new_root_id != root_id)) {
		int card_gap_count = card->gap_count;

		spin_unlock_irq(&card->lock);

		fw_notice(card, "phy config: new root=%x, gap_count=%d\n",
			  new_root_id, expected_gap_count);
		fw_send_phy_config(card, new_root_id, generation, expected_gap_count);
		/*
		 * Where possible, use a short bus reset to minimize
		 * disruption to isochronous transfers. But in the event
		 * of a gap count inconsistency, use a long bus reset.
		 *
		 * As noted in 1394a 8.4.6.2, nodes on a mixed 1394/1394a bus
		 * may set different gap counts after a bus reset. On a mixed
		 * 1394/1394a bus, a short bus reset can get doubled. Some
		 * nodes may treat the double reset as one bus reset and others
		 * may treat it as two, causing a gap count inconsistency
		 * again. Using a long bus reset prevents this.
		 */
		reset_bus(card, card_gap_count != 0);
		/* Will allocate broadcast channel after the reset. */
	} else {
		struct fw_device *root_device = fw_node_get_device(root_node);

		spin_unlock_irq(&card->lock);

		if (root_device && root_device->cmc) {
			// Make sure that the cycle master sends cycle start packets.
			__be32 data = cpu_to_be32(CSR_STATE_BIT_CMSTR);
			int rcode = fw_run_transaction(card, TCODE_WRITE_QUADLET_REQUEST,
					root_id, generation, SCODE_100,
					CSR_REGISTER_BASE + CSR_STATE_SET,
					&data, sizeof(data));
			if (rcode == RCODE_GENERATION)
				return;
		}

		if (local_id == irm_id)
			allocate_broadcast_channel(card, generation);
	}
}

void fw_card_initialize(struct fw_card *card,
			const struct fw_card_driver *driver,
			struct device *device)
{
	static atomic_t index = ATOMIC_INIT(-1);

	card->index = atomic_inc_return(&index);
	card->driver = driver;
	card->device = device;

	card->transactions.current_tlabel = 0;
	card->transactions.tlabel_mask = 0;
	INIT_LIST_HEAD(&card->transactions.list);
	spin_lock_init(&card->transactions.lock);

	spin_lock_init(&card->topology_map.lock);

	card->split_timeout.hi = DEFAULT_SPLIT_TIMEOUT / 8000;
	card->split_timeout.lo = (DEFAULT_SPLIT_TIMEOUT % 8000) << 19;
	card->split_timeout.cycles = DEFAULT_SPLIT_TIMEOUT;
	card->split_timeout.jiffies = isoc_cycles_to_jiffies(DEFAULT_SPLIT_TIMEOUT);
	spin_lock_init(&card->split_timeout.lock);

	card->color = 0;
	card->broadcast_channel = BROADCAST_CHANNEL_INITIAL;

	kref_init(&card->kref);
	init_completion(&card->done);

	spin_lock_init(&card->lock);

	card->local_node = NULL;

	INIT_DELAYED_WORK(&card->br_work, br_work);
	INIT_DELAYED_WORK(&card->bm_work, bm_work);
}
EXPORT_SYMBOL(fw_card_initialize);

DEFINE_FREE(workqueue_destroy, struct workqueue_struct *, if (_T) destroy_workqueue(_T))

int fw_card_add(struct fw_card *card, u32 max_receive, u32 link_speed, u64 guid,
		unsigned int supported_isoc_contexts)
{
	struct workqueue_struct *isoc_wq __free(workqueue_destroy) = NULL;
	struct workqueue_struct *async_wq __free(workqueue_destroy) = NULL;
	int ret;

	// This workqueue should be:
	//  * != WQ_BH			Sleepable.
	//  * == WQ_UNBOUND		Any core can process data for isoc context. The
	//				implementation of unit protocol could consumes the core
	//				longer somehow.
	//  * != WQ_MEM_RECLAIM		Not used for any backend of block device.
	//  * == WQ_FREEZABLE		Isochronous communication is at regular interval in real
	//				time, thus should be drained if possible at freeze phase.
	//  * == WQ_HIGHPRI		High priority to process semi-realtime timestamped data.
	//  * == WQ_SYSFS		Parameters are available via sysfs.
	//  * max_active == n_it + n_ir	A hardIRQ could notify events for multiple isochronous
	//				contexts if they are scheduled to the same cycle.
	isoc_wq = alloc_workqueue("firewire-isoc-card%u",
				  WQ_UNBOUND | WQ_FREEZABLE | WQ_HIGHPRI | WQ_SYSFS,
				  supported_isoc_contexts, card->index);
	if (!isoc_wq)
		return -ENOMEM;

	// This workqueue should be:
	//  * != WQ_BH			Sleepable.
	//  * == WQ_UNBOUND		Any core can process data for asynchronous context.
	//  * == WQ_MEM_RECLAIM		Used for any backend of block device.
	//  * == WQ_FREEZABLE		The target device would not be available when being freezed.
	//  * == WQ_HIGHPRI		High priority to process semi-realtime timestamped data.
	//  * == WQ_SYSFS		Parameters are available via sysfs.
	//  * max_active == 4		A hardIRQ could notify events for a pair of requests and
	//				response AR/AT contexts.
	async_wq = alloc_workqueue("firewire-async-card%u",
				   WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_FREEZABLE | WQ_HIGHPRI | WQ_SYSFS,
				   4, card->index);
	if (!async_wq)
		return -ENOMEM;

	card->isoc_wq = isoc_wq;
	card->async_wq = async_wq;
	card->max_receive = max_receive;
	card->link_speed = link_speed;
	card->guid = guid;

	scoped_guard(mutex, &card_mutex) {
		generate_config_rom(card, tmp_config_rom);
		ret = card->driver->enable(card, tmp_config_rom, config_rom_length);
		if (ret < 0) {
			card->isoc_wq = NULL;
			card->async_wq = NULL;
			return ret;
		}
		retain_and_null_ptr(isoc_wq);
		retain_and_null_ptr(async_wq);

		list_add_tail(&card->link, &card_list);
	}

	return 0;
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

static u32 dummy_read_csr(struct fw_card *card, int csr_offset)
{
	return 0;
}

static void dummy_write_csr(struct fw_card *card, int csr_offset, u32 value)
{
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
	.read_csr		= dummy_read_csr,
	.write_csr		= dummy_write_csr,
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

	might_sleep();

	card->driver->update_phy_reg(card, 4,
				     PHY_LINK_ACTIVE | PHY_CONTENDER, 0);
	fw_schedule_bus_reset(card, false, true);

	scoped_guard(mutex, &card_mutex)
		list_del_init(&card->link);

	/* Switch off most of the card driver interface. */
	dummy_driver.free_iso_context	= card->driver->free_iso_context;
	dummy_driver.stop_iso		= card->driver->stop_iso;
	card->driver = &dummy_driver;
	drain_workqueue(card->isoc_wq);
	drain_workqueue(card->async_wq);

	scoped_guard(spinlock_irqsave, &card->lock)
		fw_destroy_nodes(card);

	/* Wait for all users, especially device workqueue jobs, to finish. */
	fw_card_put(card);
	wait_for_completion(&card->done);

	destroy_workqueue(card->isoc_wq);
	destroy_workqueue(card->async_wq);

	WARN_ON(!list_empty(&card->transactions.list));
}
EXPORT_SYMBOL(fw_core_remove_card);

/**
 * fw_card_read_cycle_time: read from Isochronous Cycle Timer Register of 1394 OHCI in MMIO region
 *			    for controller card.
 * @card: The instance of card for 1394 OHCI controller.
 * @cycle_time: The mutual reference to value of cycle time for the read operation.
 *
 * Read value from Isochronous Cycle Timer Register of 1394 OHCI in MMIO region for the given
 * controller card. This function accesses the region without any lock primitives or IRQ mask.
 * When returning successfully, the content of @value argument has value aligned to host endianness,
 * formetted by CYCLE_TIME CSR Register of IEEE 1394 std.
 *
 * Context: Any context.
 * Return:
 * * 0 - Read successfully.
 * * -ENODEV - The controller is unavailable due to being removed or unbound.
 */
int fw_card_read_cycle_time(struct fw_card *card, u32 *cycle_time)
{
	if (card->driver->read_csr == dummy_read_csr)
		return -ENODEV;

	// It's possible to switch to dummy driver between the above and the below. This is the best
	// effort to return -ENODEV.
	*cycle_time = card->driver->read_csr(card, CSR_CYCLE_TIME);
	return 0;
}
EXPORT_SYMBOL_GPL(fw_card_read_cycle_time);
