/*
 * Hermes download helper.
 *
 * This helper:
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

#define PFX "hermes_dld: "

/* End markers used in dblocks */
#define PDI_END		0x00000000	/* End of PDA */
#define BLOCK_END	0xFFFFFFFF	/* Last image block */
#define TEXT_END	0x1A		/* End of text header */

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
	char data[];		/* data to be written */
} __packed;

/*
 * Plug Data References are located in the image after the last data
 * block.  They refer to areas in the adapter memory where the plug data
 * items with matching ID should be written.
 */
struct pdr {
	__le32 id;		/* record ID */
	__le32 addr;		/* adapter address where to write the data */
	__le32 len;		/* expected length of the data, in bytes */
	char next[];		/* next PDR starts here */
} __packed;

/*
 * Plug Data Items are located in the EEPROM read from the adapter by
 * primary firmware.  They refer to the device-specific data that should
 * be plugged into the secondary firmware.
 */
struct pdi {
	__le16 len;		/* length of ID and data, in words */
	__le16 id;		/* record ID */
	char data[];		/* plug data */
} __packed;

/*** FW data block access functions ***/

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

/*** PDR Access functions ***/

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

/*** PDI Access functions ***/

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

/*** Plug Data Functions ***/

/*
 * Scan PDR for the record with the specified RECORD_ID.
 * If it's not found, return NULL.
 */
static const struct pdr *
hermes_find_pdr(const struct pdr *first_pdr, u32 record_id, const void *end)
{
	const struct pdr *pdr = first_pdr;

	end -= sizeof(struct pdr);

	while (((void *) pdr <= end) &&
	       (pdr_id(pdr) != PDI_END)) {
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

/* Scan production data items for a particular entry */
static const struct pdi *
hermes_find_pdi(const struct pdi *first_pdi, u32 record_id, const void *end)
{
	const struct pdi *pdi = first_pdi;

	end -= sizeof(struct pdi);

	while (((void *) pdi <= end) &&
	       (pdi_id(pdi) != PDI_END)) {

		/* If the record ID matches, we are done */
		if (pdi_id(pdi) == record_id)
			return pdi;

		pdi = (struct pdi *) &pdi->data[pdi_len(pdi)];
	}
	return NULL;
}

/* Process one Plug Data Item - find corresponding PDR and plug it */
static int
hermes_plug_pdi(struct hermes *hw, const struct pdr *first_pdr,
		const struct pdi *pdi, const void *pdr_end)
{
	const struct pdr *pdr;

	/* Find the PDR corresponding to this PDI */
	pdr = hermes_find_pdr(first_pdr, pdi_id(pdi), pdr_end);

	/* No match is found, safe to ignore */
	if (!pdr)
		return 0;

	/* Lengths of the data in PDI and PDR must match */
	if (pdi_len(pdi) != pdr_len(pdr))
		return -EINVAL;

	/* do the actual plugging */
	hw->ops->program(hw, pdi->data, pdr_addr(pdr), pdi_len(pdi));

	return 0;
}

/* Parse PDA and write the records into the adapter
 *
 * Attempt to write every records that is in the specified pda
 * which also has a valid production data record for the firmware.
 */
int hermes_apply_pda(struct hermes *hw,
		     const char *first_pdr,
		     const void *pdr_end,
		     const __le16 *pda,
		     const void *pda_end)
{
	int ret;
	const struct pdi *pdi;
	const struct pdr *pdr;

	pdr = (const struct pdr *) first_pdr;
	pda_end -= sizeof(struct pdi);

	/* Go through every PDI and plug them into the adapter */
	pdi = (const struct pdi *) (pda + 2);
	while (((void *) pdi <= pda_end) &&
	       (pdi_id(pdi) != PDI_END)) {
		ret = hermes_plug_pdi(hw, pdr, pdi, pdr_end);
		if (ret)
			return ret;

		/* Increment to the next PDI */
		pdi = (const struct pdi *) &pdi->data[pdi_len(pdi)];
	}
	return 0;
}

/* Identify the total number of bytes in all blocks
 * including the header data.
 */
size_t
hermes_blocks_length(const char *first_block, const void *end)
{
	const struct dblock *blk = (const struct dblock *) first_block;
	int total_len = 0;
	int len;

	end -= sizeof(*blk);

	/* Skip all blocks to locate Plug Data References
	 * (Spectrum CS) */
	while (((void *) blk <= end) &&
	       (dblock_addr(blk) != BLOCK_END)) {
		len = dblock_len(blk);
		total_len += sizeof(*blk) + len;
		blk = (struct dblock *) &blk->data[len];
	}

	return total_len;
}

/*** Hermes programming ***/

/* Program the data blocks */
int hermes_program(struct hermes *hw, const char *first_block, const void *end)
{
	const struct dblock *blk;
	u32 blkaddr;
	u32 blklen;
	int err = 0;

	blk = (const struct dblock *) first_block;

	if ((void *) blk > (end - sizeof(*blk)))
		return -EIO;

	blkaddr = dblock_addr(blk);
	blklen = dblock_len(blk);

	while ((blkaddr != BLOCK_END) &&
	       (((void *) blk + blklen) <= end)) {
		pr_debug(PFX "Programming block of length %d "
			 "to address 0x%08x\n", blklen, blkaddr);

		err = hw->ops->program(hw, blk->data, blkaddr, blklen);
		if (err)
			break;

		blk = (const struct dblock *) &blk->data[blklen];

		if ((void *) blk > (end - sizeof(*blk)))
			return -EIO;

		blkaddr = dblock_addr(blk);
		blklen = dblock_len(blk);
	}
	return err;
}

/*** Default plugging data for Hermes I ***/
/* Values from wl_lkm_718/hcf/dhf.c */

#define DEFINE_DEFAULT_PDR(pid, length, data)				\
static const struct {							\
	__le16 len;							\
	__le16 id;							\
	u8 val[length];							\
} __packed default_pdr_data_##pid = {			\
	cpu_to_le16((sizeof(default_pdr_data_##pid)/			\
				sizeof(__le16)) - 1),			\
	cpu_to_le16(pid),						\
	data								\
}

#define DEFAULT_PDR(pid) default_pdr_data_##pid

/*  HWIF Compatibility */
DEFINE_DEFAULT_PDR(0x0005, 10, "\x00\x00\x06\x00\x01\x00\x01\x00\x01\x00");

/* PPPPSign */
DEFINE_DEFAULT_PDR(0x0108, 4, "\x00\x00\x00\x00");

/* PPPPProf */
DEFINE_DEFAULT_PDR(0x0109, 10, "\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00");

/* Antenna diversity */
DEFINE_DEFAULT_PDR(0x0150, 2, "\x00\x3F");

/* Modem VCO band Set-up */
DEFINE_DEFAULT_PDR(0x0160, 28,
		   "\x00\x00\x00\x00\x00\x00\x00\x00"
		   "\x00\x00\x00\x00\x00\x00\x00\x00"
		   "\x00\x00\x00\x00\x00\x00\x00\x00"
		   "\x00\x00\x00\x00");

/* Modem Rx Gain Table Values */
DEFINE_DEFAULT_PDR(0x0161, 256,
		   "\x3F\x01\x3F\01\x3F\x01\x3F\x01"
		   "\x3F\x01\x3F\01\x3F\x01\x3F\x01"
		   "\x3F\x01\x3F\01\x3F\x01\x3F\x01"
		   "\x3F\x01\x3F\01\x3F\x01\x3F\x01"
		   "\x3F\x01\x3E\01\x3E\x01\x3D\x01"
		   "\x3D\x01\x3C\01\x3C\x01\x3B\x01"
		   "\x3B\x01\x3A\01\x3A\x01\x39\x01"
		   "\x39\x01\x38\01\x38\x01\x37\x01"
		   "\x37\x01\x36\01\x36\x01\x35\x01"
		   "\x35\x01\x34\01\x34\x01\x33\x01"
		   "\x33\x01\x32\x01\x32\x01\x31\x01"
		   "\x31\x01\x30\x01\x30\x01\x7B\x01"
		   "\x7B\x01\x7A\x01\x7A\x01\x79\x01"
		   "\x79\x01\x78\x01\x78\x01\x77\x01"
		   "\x77\x01\x76\x01\x76\x01\x75\x01"
		   "\x75\x01\x74\x01\x74\x01\x73\x01"
		   "\x73\x01\x72\x01\x72\x01\x71\x01"
		   "\x71\x01\x70\x01\x70\x01\x68\x01"
		   "\x68\x01\x67\x01\x67\x01\x66\x01"
		   "\x66\x01\x65\x01\x65\x01\x57\x01"
		   "\x57\x01\x56\x01\x56\x01\x55\x01"
		   "\x55\x01\x54\x01\x54\x01\x53\x01"
		   "\x53\x01\x52\x01\x52\x01\x51\x01"
		   "\x51\x01\x50\x01\x50\x01\x48\x01"
		   "\x48\x01\x47\x01\x47\x01\x46\x01"
		   "\x46\x01\x45\x01\x45\x01\x44\x01"
		   "\x44\x01\x43\x01\x43\x01\x42\x01"
		   "\x42\x01\x41\x01\x41\x01\x40\x01"
		   "\x40\x01\x40\x01\x40\x01\x40\x01"
		   "\x40\x01\x40\x01\x40\x01\x40\x01"
		   "\x40\x01\x40\x01\x40\x01\x40\x01"
		   "\x40\x01\x40\x01\x40\x01\x40\x01");

/* Write PDA according to certain rules.
 *
 * For every production data record, look for a previous setting in
 * the pda, and use that.
 *
 * For certain records, use defaults if they are not found in pda.
 */
int hermes_apply_pda_with_defaults(struct hermes *hw,
				   const char *first_pdr,
				   const void *pdr_end,
				   const __le16 *pda,
				   const void *pda_end)
{
	const struct pdr *pdr = (const struct pdr *) first_pdr;
	const struct pdi *first_pdi = (const struct pdi *) &pda[2];
	const struct pdi *pdi;
	const struct pdi *default_pdi = NULL;
	const struct pdi *outdoor_pdi;
	int record_id;

	pdr_end -= sizeof(struct pdr);

	while (((void *) pdr <= pdr_end) &&
	       (pdr_id(pdr) != PDI_END)) {
		/*
		 * For spectrum_cs firmwares,
		 * PDR area is currently not terminated by PDI_END.
		 * It's followed by CRC records, which have the type
		 * field where PDR has length.  The type can be 0 or 1.
		 */
		if (pdr_len(pdr) < 2)
			break;
		record_id = pdr_id(pdr);

		pdi = hermes_find_pdi(first_pdi, record_id, pda_end);
		if (pdi)
			pr_debug(PFX "Found record 0x%04x at %p\n",
				 record_id, pdi);

		switch (record_id) {
		case 0x110: /* Modem REFDAC values */
		case 0x120: /* Modem VGDAC values */
			outdoor_pdi = hermes_find_pdi(first_pdi, record_id + 1,
						      pda_end);
			default_pdi = NULL;
			if (outdoor_pdi) {
				pdi = outdoor_pdi;
				pr_debug(PFX
					 "Using outdoor record 0x%04x at %p\n",
					 record_id + 1, pdi);
			}
			break;
		case 0x5: /*  HWIF Compatibility */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0005);
			break;
		case 0x108: /* PPPPSign */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0108);
			break;
		case 0x109: /* PPPPProf */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0109);
			break;
		case 0x150: /* Antenna diversity */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0150);
			break;
		case 0x160: /* Modem VCO band Set-up */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0160);
			break;
		case 0x161: /* Modem Rx Gain Table Values */
			default_pdi = (struct pdi *) &DEFAULT_PDR(0x0161);
			break;
		default:
			default_pdi = NULL;
			break;
		}
		if (!pdi && default_pdi) {
			/* Use default */
			pdi = default_pdi;
			pr_debug(PFX "Using default record 0x%04x at %p\n",
				 record_id, pdi);
		}

		if (pdi) {
			/* Lengths of the data in PDI and PDR must match */
			if ((pdi_len(pdi) == pdr_len(pdr)) &&
			    ((void *) pdi->data + pdi_len(pdi) < pda_end)) {
				/* do the actual plugging */
				hw->ops->program(hw, pdi->data, pdr_addr(pdr),
						 pdi_len(pdi));
			}
		}

		pdr++;
	}
	return 0;
}
