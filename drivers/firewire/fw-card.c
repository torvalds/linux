/*						-*- c-basic-offset: 8 -*-
 *
 * fw-card.c - card level functions
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"

/* The lib/crc16.c implementation uses the standard (0x8005)
 * polynomial, but we need the ITU-T (or CCITT) polynomial (0x1021).
 * The implementation below works on an array of host-endian u32
 * words, assuming they'll be transmited msb first. */
static u16
crc16_itu_t(const u32 *buffer, size_t length)
{
	int shift, i;
	u32 data;
	u16 sum, crc = 0;

	for (i = 0; i < length; i++) {
		data = *buffer++;
		for (shift = 28; shift >= 0; shift -= 4 ) {
			sum = ((crc >> 12) ^ (data >> shift)) & 0xf;
			crc = (crc << 4) ^ (sum << 12) ^ (sum << 5) ^ (sum);
		}
		crc &= 0xffff;
	}

	return crc;
}

static LIST_HEAD(card_list);

static LIST_HEAD(descriptor_list);
static int descriptor_count;

#define bib_crc(v)		((v) <<  0)
#define bib_crc_length(v)	((v) << 16)
#define bib_info_length(v)	((v) << 24)

#define bib_link_speed(v)	((v) <<  0)
#define bib_generation(v)	((v) <<  4)
#define bib_max_rom(v)		((v) <<  8)
#define bib_max_receive(v)	((v) << 12)
#define bib_cyc_clk_acc(v)	((v) << 16)
#define bib_pmc			((1) << 27)
#define bib_bmc			((1) << 28)
#define bib_isc			((1) << 29)
#define bib_cmc			((1) << 30)
#define bib_imc			((1) << 31)

static u32 *
generate_config_rom (struct fw_card *card, size_t *config_rom_length)
{
	struct fw_descriptor *desc;
	static u32 config_rom[256];
	int i, j, length;

	/* Initialize contents of config rom buffer.  On the OHCI
	 * controller, block reads to the config rom accesses the host
	 * memory, but quadlet read access the hardware bus info block
	 * registers.  That's just crack, but it means we should make
	 * sure the contents of bus info block in host memory mathces
	 * the version stored in the OHCI registers. */

	memset(config_rom, 0, sizeof config_rom);
	config_rom[0] = bib_crc_length(4) | bib_info_length(4) | bib_crc(0);
	config_rom[1] = 0x31333934;

	config_rom[2] =
		bib_link_speed(card->link_speed) |
		bib_generation(card->config_rom_generation++ % 14 + 2) |
		bib_max_rom(2) |
		bib_max_receive(card->max_receive) |
		bib_isc | bib_cmc | bib_imc;
	config_rom[3] = card->guid >> 32;
	config_rom[4] = card->guid;

	/* Generate root directory. */
	i = 5;
	config_rom[i++] = 0;
	config_rom[i++] = 0x0c0083c0; /* node capabilities */
	config_rom[i++] = 0x03d00d1e; /* vendor id */
	j = i + descriptor_count;

	/* Generate root directory entries for descriptors. */
	list_for_each_entry (desc, &descriptor_list, link) {
		config_rom[i] = desc->key | (j - i);
		i++;
		j += desc->length;
	}

	/* Update root directory length. */
	config_rom[5] = (i - 5 - 1) << 16;

	/* End of root directory, now copy in descriptors. */
	list_for_each_entry (desc, &descriptor_list, link) {
		memcpy(&config_rom[i], desc->data, desc->length * 4);
		i += desc->length;
	}

	/* Calculate CRCs for all blocks in the config rom.  This
	 * assumes that CRC length and info length are identical for
	 * the bus info block, which is always the case for this
	 * implementation. */
	for (i = 0; i < j; i += length + 1) {
		length = (config_rom[i] >> 16) & 0xff;
		config_rom[i] |= crc16_itu_t(&config_rom[i + 1], length);
	}

	*config_rom_length = j;

	return config_rom;
}

static void
update_config_roms (void)
{
	struct fw_card *card;
	u32 *config_rom;
	size_t length;

	list_for_each_entry (card, &card_list, link) {
		config_rom = generate_config_rom(card, &length);
		card->driver->set_config_rom(card, config_rom, length);
	}
}

int
fw_core_add_descriptor (struct fw_descriptor *desc)
{
	size_t i;

	/* Check descriptor is valid; the length of all blocks in the
	 * descriptor has to add up to exactly the length of the
	 * block. */
	i = 0;
	while (i < desc->length)
		i += (desc->data[i] >> 16) + 1;

	if (i != desc->length)
		return -1;

	down_write(&fw_bus_type.subsys.rwsem);

	list_add_tail (&desc->link, &descriptor_list);
	descriptor_count++;
	update_config_roms();

	up_write(&fw_bus_type.subsys.rwsem);

	return 0;
}
EXPORT_SYMBOL(fw_core_add_descriptor);

void
fw_core_remove_descriptor (struct fw_descriptor *desc)
{
	down_write(&fw_bus_type.subsys.rwsem);

	list_del(&desc->link);
	descriptor_count--;
	update_config_roms();

	up_write(&fw_bus_type.subsys.rwsem);
}
EXPORT_SYMBOL(fw_core_remove_descriptor);

static void
fw_card_irm_work(struct work_struct *work)
{
	struct fw_card *card =
		container_of(work, struct fw_card, work.work);
	struct fw_device *root;
	unsigned long flags;
	int new_irm_id, generation;

	/* FIXME: This simple bus management unconditionally picks a
	 * cycle master if the current root can't do it.  We need to
	 * not do this if there is a bus manager already.  Also, some
	 * hubs set the contender bit, which is bogus, so we should
	 * probably do a little sanity check on the IRM (like, read
	 * the bandwidth register) if it's not us. */

	spin_lock_irqsave(&card->lock, flags);

	generation = card->generation;
	root = card->root_node->data;

	if (root == NULL)
		/* Either link_on is false, or we failed to read the
		 * config rom.  In either case, pick another root. */
		new_irm_id = card->local_node->node_id;
	else if (root->state != FW_DEVICE_RUNNING)
		/* If we haven't probed this device yet, bail out now
		 * and let's try again once that's done. */
		new_irm_id = -1;
	else if (root->config_rom[2] & bib_cmc)
		/* FIXME: I suppose we should set the cmstr bit in the
		 * STATE_CLEAR register of this node, as described in
		 * 1394-1995, 8.4.2.6.  Also, send out a force root
		 * packet for this node. */
		new_irm_id = -1;
	else
		/* Current root has an active link layer and we
		 * successfully read the config rom, but it's not
		 * cycle master capable. */
		new_irm_id = card->local_node->node_id;

	if (card->irm_retries++ > 5)
		new_irm_id = -1;

	spin_unlock_irqrestore(&card->lock, flags);

	if (new_irm_id > 0) {
		fw_notify("Trying to become root (card %d)\n", card->index);
		fw_send_force_root(card, new_irm_id, generation);
		fw_core_initiate_bus_reset(card, 1);
	}
}

static void
release_card(struct device *device)
{
	struct fw_card *card =
		container_of(device, struct fw_card, card_device);

	kfree(card);
}

static void
flush_timer_callback(unsigned long data)
{
	struct fw_card *card = (struct fw_card *)data;

	fw_flush_transactions(card);
}

void
fw_card_initialize(struct fw_card *card, const struct fw_card_driver *driver,
		   struct device *device)
{
	static int index;

	card->index = index++;
	card->driver = driver;
	card->device = device;
	card->current_tlabel = 0;
	card->tlabel_mask = 0;
	card->color = 0;

	INIT_LIST_HEAD(&card->transaction_list);
	spin_lock_init(&card->lock);
	setup_timer(&card->flush_timer,
		    flush_timer_callback, (unsigned long)card);

	card->local_node = NULL;

	INIT_DELAYED_WORK(&card->work, fw_card_irm_work);

	card->card_device.bus     = &fw_bus_type;
	card->card_device.release = release_card;
	card->card_device.parent  = card->device;
	snprintf(card->card_device.bus_id, sizeof card->card_device.bus_id,
		 "fwcard%d", card->index);

	device_initialize(&card->card_device);
}
EXPORT_SYMBOL(fw_card_initialize);

int
fw_card_add(struct fw_card *card,
	    u32 max_receive, u32 link_speed, u64 guid)
{
	int retval;
	u32 *config_rom;
	size_t length;

	card->max_receive = max_receive;
	card->link_speed = link_speed;
	card->guid = guid;

	/* FIXME: add #define's for phy registers. */
	/* Activate link_on bit and contender bit in our self ID packets.*/
	if (card->driver->update_phy_reg(card, 4, 0, 0x80 | 0x40) < 0)
		return -EIO;

	retval = device_add(&card->card_device);
	if (retval < 0) {
		fw_error("Failed to register card device.");
		return retval;
	}

	/* The subsystem grabs a reference when the card is added and
	 * drops it when the driver calls fw_core_remove_card. */
	fw_card_get(card);

	down_write(&fw_bus_type.subsys.rwsem);
	config_rom = generate_config_rom (card, &length);
	list_add_tail(&card->link, &card_list);
	up_write(&fw_bus_type.subsys.rwsem);

	return card->driver->enable(card, config_rom, length);
}
EXPORT_SYMBOL(fw_card_add);


/* The next few functions implements a dummy driver that use once a
 * card driver shuts down an fw_card.  This allows the driver to
 * cleanly unload, as all IO to the card will be handled by the dummy
 * driver instead of calling into the (possibly) unloaded module.  The
 * dummy driver just fails all IO. */

static int
dummy_enable(struct fw_card *card, u32 *config_rom, size_t length)
{
	BUG();
	return -1;
}

static int
dummy_update_phy_reg(struct fw_card *card, int address,
		     int clear_bits, int set_bits)
{
	return -ENODEV;
}

static int
dummy_set_config_rom(struct fw_card *card,
		     u32 *config_rom, size_t length)
{
	/* We take the card out of card_list before setting the dummy
	 * driver, so this should never get called. */
	BUG();
	return -1;
}

static void
dummy_send_request(struct fw_card *card, struct fw_packet *packet)
{
	packet->callback(packet, card, -ENODEV);
}

static void
dummy_send_response(struct fw_card *card, struct fw_packet *packet)
{
	packet->callback(packet, card, -ENODEV);
}

static int
dummy_enable_phys_dma(struct fw_card *card,
		      int node_id, int generation)
{
	return -ENODEV;
}

static struct fw_card_driver dummy_driver = {
	.name            = "dummy",
	.enable          = dummy_enable,
	.update_phy_reg  = dummy_update_phy_reg,
	.set_config_rom  = dummy_set_config_rom,
	.send_request    = dummy_send_request,
	.send_response   = dummy_send_response,
	.enable_phys_dma = dummy_enable_phys_dma,
};

void
fw_core_remove_card(struct fw_card *card)
{
	card->driver->update_phy_reg(card, 4, 0x80 | 0x40, 0);
	fw_core_initiate_bus_reset(card, 1);

	down_write(&fw_bus_type.subsys.rwsem);
	list_del(&card->link);
	up_write(&fw_bus_type.subsys.rwsem);

	/* Set up the dummy driver. */
	card->driver = &dummy_driver;

	fw_flush_transactions(card);

	fw_destroy_nodes(card);

	/* This also drops the subsystem reference. */
	device_unregister(&card->card_device);
}
EXPORT_SYMBOL(fw_core_remove_card);

struct fw_card *
fw_card_get(struct fw_card *card)
{
	get_device(&card->card_device);

	return card;
}
EXPORT_SYMBOL(fw_card_get);

/* An assumption for fw_card_put() is that the card driver allocates
 * the fw_card struct with kalloc and that it has been shut down
 * before the last ref is dropped. */
void
fw_card_put(struct fw_card *card)
{
	put_device(&card->card_device);
}
EXPORT_SYMBOL(fw_card_put);

int
fw_core_initiate_bus_reset(struct fw_card *card, int short_reset)
{
	return card->driver->update_phy_reg(card, short_reset ? 5 : 1, 0, 0x40);
}
EXPORT_SYMBOL(fw_core_initiate_bus_reset);
