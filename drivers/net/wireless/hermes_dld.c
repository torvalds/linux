/*
 * Hermes download helper driver.
 *
 * This could be entirely merged into hermes.c.
 *
 * I'm keeping it separate to minimise the amount of merging between
 * kernel upgrades. It also means the memory overhead for drivers that
 * don't need firmware download low.
 *
 * This driver:
 *  - is capable of writing to the volatile area of the hermes device
 *  - is currently not capable of writing to non-volatile areas
 *  - provide helpers to identify and update plugin data
 *  - is not capable of interpreting a fw image directly. That is up to
 *    the main card driver.
 *  - deals with Hermes I devices. It can probably be modified to deal
 *    with Hermes II devices
 *
 * Copyright (C) 2007, David Kilroy
 *
 * Plug data code slightly modified from spectrum_cs driver
 *    Copyright (C) 2002-2005 Pavel Roskin <proski@gnu.org>
 * Portions based on information in wl_lkm_718 Agere driver
 *    COPYRIGHT (C) 2001-2004 by Agere Systems Inc. All Rights Reserved
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include "hermes.h"
#include "hermes_dld.h"

MODULE_DESCRIPTION("Download helper for Lucent Hermes chipset");
MODULE_AUTHOR("David Kilroy <kilroyd@gmail.com>");
MODULE_LICENSE("Dual MPL/GPL");

#define PFX "hermes_dld: "

/*
 * AUX port access.  To unlock the AUX port write the access keys to the
 * PARAM0-2 registers, then write HERMES_AUX_ENABLE to the HERMES_CONTROL
 * register.  Then read it and make sure it's HERMES_AUX_ENABLED.
 */
#define HERMES_AUX_ENABLE	0x8000	/* Enable auxiliary port access */
#define HERMES_AUX_DISABLE	0x4000	/* Disable to auxiliary port access */
#define HERMES_AUX_ENABLED	0xC000	/* Auxiliary port is open */

#define HERMES_AUX_PW0	0xFE01
#define HERMES_AUX_PW1	0xDC23
#define HERMES_AUX_PW2	0xBA45

/* End markers */
#define PDI_END		0x00000000	/* End of PDA */
#define BLOCK_END	0xFFFFFFFF	/* Last image block */

/*
 * The following structures have little-endian fields denoted by
 * the leading underscore.  Don't access them directly - use inline
 * functions defined below.
 */

/*
 * The binary image to be downloaded consists of series of data blocks.
 * Each block has the following structure.
 */
struct dblock {
	__le32 addr;		/* adapter address where to write the block */
	__le16 len;		/* length of the data only, in bytes */
	char data[0];		/* data to be written */
} __attribute__ ((packed));

/*
 * Plug Data References are located in in the image after the last data
 * block.  They refer to areas in the adapter memory where the plug data
 * items with matching ID should be written.
 */
struct pdr {
	__le32 id;		/* record ID */
	__le32 addr;		/* adapter address where to write the data */
	__le32 len;		/* expected length of the data, in bytes */
	char next[0];		/* next PDR starts here */
} __attribute__ ((packed));

/*
 * Plug Data Items are located in the EEPROM read from the adapter by
 * primary firmware.  They refer to the device-specific data that should
 * be plugged into the secondary firmware.
 */
struct pdi {
	__le16 len;		/* length of ID and data, in words */
	__le16 id;		/* record ID */
	char data[0];		/* plug data */
} __attribute__ ((packed));

/* Functions for access to little-endian data */
static inline u32
dblock_addr(const struct dblock *blk)
{
	return le32_to_cpu(blk->addr);
}

static inline u32
dblock_len(const struct dblock *blk)
{
	return le16_to_cpu(blk->len);
}

static inline u32
pdr_id(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->id);
}

static inline u32
pdr_addr(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->addr);
}

static inline u32
pdr_len(const struct pdr *pdr)
{
	return le32_to_cpu(pdr->len);
}

static inline u32
pdi_id(const struct pdi *pdi)
{
	return le16_to_cpu(pdi->id);
}

/* Return length of the data only, in bytes */
static inline u32
pdi_len(const struct pdi *pdi)
{
	return 2 * (le16_to_cpu(pdi->len) - 1);
}

/* Set address of the auxiliary port */
static inline void
spectrum_aux_setaddr(hermes_t *hw, u32 addr)
{
	hermes_write_reg(hw, HERMES_AUXPAGE, (u16) (addr >> 7));
	hermes_write_reg(hw, HERMES_AUXOFFSET, (u16) (addr & 0x7F));
}

/* Open access to the auxiliary port */
static int
spectrum_aux_open(hermes_t *hw)
{
	int i;

	/* Already open? */
	if (hermes_read_reg(hw, HERMES_CONTROL) == HERMES_AUX_ENABLED)
		return 0;

	hermes_write_reg(hw, HERMES_PARAM0, HERMES_AUX_PW0);
	hermes_write_reg(hw, HERMES_PARAM1, HERMES_AUX_PW1);
	hermes_write_reg(hw, HERMES_PARAM2, HERMES_AUX_PW2);
	hermes_write_reg(hw, HERMES_CONTROL, HERMES_AUX_ENABLE);

	for (i = 0; i < 20; i++) {
		udelay(10);
		if (hermes_read_reg(hw, HERMES_CONTROL) ==
		    HERMES_AUX_ENABLED)
			return 0;
	}

	return -EBUSY;
}

/*
 * Scan PDR for the record with the specified RECORD_ID.
 * If it's not found, return NULL.
 */
static struct pdr *
spectrum_find_pdr(struct pdr *first_pdr, u32 record_id)
{
	struct pdr *pdr = first_pdr;

	while (pdr_id(pdr) != PDI_END) {
		/*
		 * PDR area is currently not terminated by PDI_END.
		 * It's followed by CRC records, which have the type
		 * field where PDR has length.  The type can be 0 or 1.
		 */
		if (pdr_len(pdr) < 2)
			return NULL;

		/* If the record ID matches, we are done */
		if (pdr_id(pdr) == record_id)
			return pdr;

		pdr = (struct pdr *) pdr->next;
	}
	return NULL;
}

/* Process one Plug Data Item - find corresponding PDR and plug it */
static int
spectrum_plug_pdi(hermes_t *hw, struct pdr *first_pdr, struct pdi *pdi)
{
	struct pdr *pdr;

	/* Find the PDI corresponding to this PDR */
	pdr = spectrum_find_pdr(first_pdr, pdi_id(pdi));

	/* No match is found, safe to ignore */
	if (!pdr)
		return 0;

	/* Lengths of the data in PDI and PDR must match */
	if (pdi_len(pdi) != pdr_len(pdr))
		return -EINVAL;

	/* do the actual plugging */
	spectrum_aux_setaddr(hw, pdr_addr(pdr));
	hermes_write_bytes(hw, HERMES_AUXDATA, pdi->data, pdi_len(pdi));

	return 0;
}

/* Read PDA from the adapter */
int
spectrum_read_pda(hermes_t *hw, __le16 *pda, int pda_len)
{
	int ret;
	int pda_size;

	/* Issue command to read EEPROM */
	ret = hermes_docmd_wait(hw, HERMES_CMD_READMIF, 0, NULL);
	if (ret)
		return ret;

	/* Open auxiliary port */
	ret = spectrum_aux_open(hw);
	if (ret)
		return ret;

	/* read PDA from EEPROM */
	spectrum_aux_setaddr(hw, PDA_ADDR);
	hermes_read_words(hw, HERMES_AUXDATA, pda, pda_len / 2);

	/* Check PDA length */
	pda_size = le16_to_cpu(pda[0]);
	if (pda_size > pda_len)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(spectrum_read_pda);

/* Parse PDA and write the records into the adapter */
int
spectrum_apply_pda(hermes_t *hw, const struct dblock *first_block,
		   __le16 *pda)
{
	int ret;
	struct pdi *pdi;
	struct pdr *first_pdr;
	const struct dblock *blk = first_block;

	/* Skip all blocks to locate Plug Data References */
	while (dblock_addr(blk) != BLOCK_END)
		blk = (struct dblock *) &blk->data[dblock_len(blk)];

	first_pdr = (struct pdr *) blk;

	/* Go through every PDI and plug them into the adapter */
	pdi = (struct pdi *) (pda + 2);
	while (pdi_id(pdi) != PDI_END) {
		ret = spectrum_plug_pdi(hw, first_pdr, pdi);
		if (ret)
			return ret;

		/* Increment to the next PDI */
		pdi = (struct pdi *) &pdi->data[pdi_len(pdi)];
	}
	return 0;
}
EXPORT_SYMBOL(spectrum_apply_pda);

/* Load firmware blocks into the adapter */
int
spectrum_load_blocks(hermes_t *hw, const struct dblock *first_block)
{
	const struct dblock *blk;
	u32 blkaddr;
	u32 blklen;

	blk = first_block;
	blkaddr = dblock_addr(blk);
	blklen = dblock_len(blk);

	while (dblock_addr(blk) != BLOCK_END) {
		spectrum_aux_setaddr(hw, blkaddr);
		hermes_write_bytes(hw, HERMES_AUXDATA, blk->data,
				   blklen);

		blk = (struct dblock *) &blk->data[blklen];
		blkaddr = dblock_addr(blk);
		blklen = dblock_len(blk);
	}
	return 0;
}
EXPORT_SYMBOL(spectrum_load_blocks);

static int __init init_hermes_dld(void)
{
	return 0;
}

static void __exit exit_hermes_dld(void)
{
}

module_init(init_hermes_dld);
module_exit(exit_hermes_dld);
