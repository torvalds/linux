// SPDX-License-Identifier: GPL-2.0-or-later
/*
* Filename: config.c
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*/

#include <linux/types.h>
#include <linux/crc32.h>
#include <linux/swab.h>

#include "rsxx_priv.h"
#include "rsxx_cfg.h"

static void initialize_config(struct rsxx_card_cfg *cfg)
{
	cfg->hdr.version = RSXX_CFG_VERSION;

	cfg->data.block_size        = RSXX_HW_BLK_SIZE;
	cfg->data.stripe_size       = RSXX_HW_BLK_SIZE;
	cfg->data.vendor_id         = RSXX_VENDOR_ID_IBM;
	cfg->data.cache_order       = (-1);
	cfg->data.intr_coal.mode    = RSXX_INTR_COAL_DISABLED;
	cfg->data.intr_coal.count   = 0;
	cfg->data.intr_coal.latency = 0;
}

static u32 config_data_crc32(struct rsxx_card_cfg *cfg)
{
	/*
	 * Return the compliment of the CRC to ensure compatibility
	 * (i.e. this is how early rsxx drivers did it.)
	 */

	return ~crc32(~0, &cfg->data, sizeof(cfg->data));
}


/*----------------- Config Byte Swap Functions -------------------*/
static void config_hdr_be_to_cpu(struct card_cfg_hdr *hdr)
{
	hdr->version = be32_to_cpu((__force __be32) hdr->version);
	hdr->crc     = be32_to_cpu((__force __be32) hdr->crc);
}

static void config_hdr_cpu_to_be(struct card_cfg_hdr *hdr)
{
	hdr->version = (__force u32) cpu_to_be32(hdr->version);
	hdr->crc     = (__force u32) cpu_to_be32(hdr->crc);
}

static void config_data_swab(struct rsxx_card_cfg *cfg)
{
	u32 *data = (u32 *) &cfg->data;
	int i;

	for (i = 0; i < (sizeof(cfg->data) / 4); i++)
		data[i] = swab32(data[i]);
}

static void config_data_le_to_cpu(struct rsxx_card_cfg *cfg)
{
	u32 *data = (u32 *) &cfg->data;
	int i;

	for (i = 0; i < (sizeof(cfg->data) / 4); i++)
		data[i] = le32_to_cpu((__force __le32) data[i]);
}

static void config_data_cpu_to_le(struct rsxx_card_cfg *cfg)
{
	u32 *data = (u32 *) &cfg->data;
	int i;

	for (i = 0; i < (sizeof(cfg->data) / 4); i++)
		data[i] = (__force u32) cpu_to_le32(data[i]);
}


/*----------------- Config Operations ------------------*/
static int rsxx_save_config(struct rsxx_cardinfo *card)
{
	struct rsxx_card_cfg cfg;
	int st;

	memcpy(&cfg, &card->config, sizeof(cfg));

	if (unlikely(cfg.hdr.version != RSXX_CFG_VERSION)) {
		dev_err(CARD_TO_DEV(card),
			"Cannot save config with invalid version %d\n",
			cfg.hdr.version);
		return -EINVAL;
	}

	/* Convert data to little endian for the CRC calculation. */
	config_data_cpu_to_le(&cfg);

	cfg.hdr.crc = config_data_crc32(&cfg);

	/*
	 * Swap the data from little endian to big endian so it can be
	 * stored.
	 */
	config_data_swab(&cfg);
	config_hdr_cpu_to_be(&cfg.hdr);

	st = rsxx_creg_write(card, CREG_ADD_CONFIG, sizeof(cfg), &cfg, 1);
	if (st)
		return st;

	return 0;
}

int rsxx_load_config(struct rsxx_cardinfo *card)
{
	int st;
	u32 crc;

	st = rsxx_creg_read(card, CREG_ADD_CONFIG, sizeof(card->config),
				&card->config, 1);
	if (st) {
		dev_err(CARD_TO_DEV(card),
			"Failed reading card config.\n");
		return st;
	}

	config_hdr_be_to_cpu(&card->config.hdr);

	if (card->config.hdr.version == RSXX_CFG_VERSION) {
		/*
		 * We calculate the CRC with the data in little endian, because
		 * early drivers did not take big endian CPUs into account.
		 * The data is always stored in big endian, so we need to byte
		 * swap it before calculating the CRC.
		 */

		config_data_swab(&card->config);

		/* Check the CRC */
		crc = config_data_crc32(&card->config);
		if (crc != card->config.hdr.crc) {
			dev_err(CARD_TO_DEV(card),
				"Config corruption detected!\n");
			dev_info(CARD_TO_DEV(card),
				"CRC (sb x%08x is x%08x)\n",
				card->config.hdr.crc, crc);
			return -EIO;
		}

		/* Convert the data to CPU byteorder */
		config_data_le_to_cpu(&card->config);

	} else if (card->config.hdr.version != 0) {
		dev_err(CARD_TO_DEV(card),
			"Invalid config version %d.\n",
			card->config.hdr.version);
		/*
		 * Config version changes require special handling from the
		 * user
		 */
		return -EINVAL;
	} else {
		dev_info(CARD_TO_DEV(card),
			"Initializing card configuration.\n");
		initialize_config(&card->config);
		st = rsxx_save_config(card);
		if (st)
			return st;
	}

	card->config_valid = 1;

	dev_dbg(CARD_TO_DEV(card), "version:     x%08x\n",
		card->config.hdr.version);
	dev_dbg(CARD_TO_DEV(card), "crc:         x%08x\n",
		card->config.hdr.crc);
	dev_dbg(CARD_TO_DEV(card), "block_size:  x%08x\n",
		card->config.data.block_size);
	dev_dbg(CARD_TO_DEV(card), "stripe_size: x%08x\n",
		card->config.data.stripe_size);
	dev_dbg(CARD_TO_DEV(card), "vendor_id:   x%08x\n",
		card->config.data.vendor_id);
	dev_dbg(CARD_TO_DEV(card), "cache_order: x%08x\n",
		card->config.data.cache_order);
	dev_dbg(CARD_TO_DEV(card), "mode:        x%08x\n",
		card->config.data.intr_coal.mode);
	dev_dbg(CARD_TO_DEV(card), "count:       x%08x\n",
		card->config.data.intr_coal.count);
	dev_dbg(CARD_TO_DEV(card), "latency:     x%08x\n",
		 card->config.data.intr_coal.latency);

	return 0;
}

