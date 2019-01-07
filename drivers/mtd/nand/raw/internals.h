/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 - Bootlin
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 *
 * Header containing internal definitions to be used only by core files.
 * NAND controller drivers should not include this file.
 */

#ifndef __LINUX_RAWNAND_INTERNALS
#define __LINUX_RAWNAND_INTERNALS

#include <linux/mtd/rawnand.h>

/*
 * NAND Flash Manufacturer ID Codes
 */
#define NAND_MFR_AMD		0x01
#define NAND_MFR_ATO		0x9b
#define NAND_MFR_EON		0x92
#define NAND_MFR_ESMT		0xc8
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_INTEL		0x89
#define NAND_MFR_MACRONIX	0xc2
#define NAND_MFR_MICRON		0x2c
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_SANDISK	0x45
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_WINBOND	0xef

/**
 * struct nand_manufacturer_ops - NAND Manufacturer operations
 * @detect: detect the NAND memory organization and capabilities
 * @init: initialize all vendor specific fields (like the ->read_retry()
 *	  implementation) if any.
 * @cleanup: the ->init() function may have allocated resources, ->cleanup()
 *	     is here to let vendor specific code release those resources.
 * @fixup_onfi_param_page: apply vendor specific fixups to the ONFI parameter
 *			   page. This is called after the checksum is verified.
 */
struct nand_manufacturer_ops {
	void (*detect)(struct nand_chip *chip);
	int (*init)(struct nand_chip *chip);
	void (*cleanup)(struct nand_chip *chip);
	void (*fixup_onfi_param_page)(struct nand_chip *chip,
				      struct nand_onfi_params *p);
};

/**
 * struct nand_manufacturer - NAND Flash Manufacturer structure
 * @name: Manufacturer name
 * @id: manufacturer ID code of device.
 * @ops: manufacturer operations
 */
struct nand_manufacturer {
	int id;
	char *name;
	const struct nand_manufacturer_ops *ops;
};


extern struct nand_flash_dev nand_flash_ids[];

extern const struct nand_manufacturer_ops amd_nand_manuf_ops;
extern const struct nand_manufacturer_ops esmt_nand_manuf_ops;
extern const struct nand_manufacturer_ops hynix_nand_manuf_ops;
extern const struct nand_manufacturer_ops macronix_nand_manuf_ops;
extern const struct nand_manufacturer_ops micron_nand_manuf_ops;
extern const struct nand_manufacturer_ops samsung_nand_manuf_ops;
extern const struct nand_manufacturer_ops toshiba_nand_manuf_ops;

/* Core functions */
const struct nand_manufacturer *nand_get_manufacturer(u8 id);
int nand_markbad_bbm(struct nand_chip *chip, loff_t ofs);
int nand_erase_nand(struct nand_chip *chip, struct erase_info *instr,
		    int allowbbt);
int onfi_fill_data_interface(struct nand_chip *chip,
			     enum nand_data_interface_type type,
			     int timing_mode);
int nand_get_features(struct nand_chip *chip, int addr, u8 *subfeature_param);
int nand_set_features(struct nand_chip *chip, int addr, u8 *subfeature_param);
int nand_read_page_raw_notsupp(struct nand_chip *chip, u8 *buf,
			       int oob_required, int page);
int nand_write_page_raw_notsupp(struct nand_chip *chip, const u8 *buf,
				int oob_required, int page);
int nand_exit_status_op(struct nand_chip *chip);
int nand_read_param_page_op(struct nand_chip *chip, u8 page, void *buf,
			    unsigned int len);
void nand_decode_ext_id(struct nand_chip *chip);
void panic_nand_wait(struct nand_chip *chip, unsigned long timeo);
void sanitize_string(uint8_t *s, size_t len);

static inline bool nand_has_exec_op(struct nand_chip *chip)
{
	if (!chip->controller || !chip->controller->ops ||
	    !chip->controller->ops->exec_op)
		return false;

	return true;
}

static inline int nand_exec_op(struct nand_chip *chip,
			       const struct nand_operation *op)
{
	if (!nand_has_exec_op(chip))
		return -ENOTSUPP;

	if (WARN_ON(op->cs >= chip->numchips))
		return -EINVAL;

	return chip->controller->ops->exec_op(chip, op, false);
}

static inline bool nand_has_setup_data_iface(struct nand_chip *chip)
{
	if (!chip->controller || !chip->controller->ops ||
	    !chip->controller->ops->setup_data_interface)
		return false;

	if (chip->options & NAND_KEEP_TIMINGS)
		return false;

	return true;
}

/* BBT functions */
int nand_markbad_bbt(struct nand_chip *chip, loff_t offs);
int nand_isreserved_bbt(struct nand_chip *chip, loff_t offs);
int nand_isbad_bbt(struct nand_chip *chip, loff_t offs, int allowbbt);

/* Legacy */
void nand_legacy_set_defaults(struct nand_chip *chip);
void nand_legacy_adjust_cmdfunc(struct nand_chip *chip);
int nand_legacy_check_hooks(struct nand_chip *chip);

/* ONFI functions */
u16 onfi_crc16(u16 crc, u8 const *p, size_t len);
int nand_onfi_detect(struct nand_chip *chip);

/* JEDEC functions */
int nand_jedec_detect(struct nand_chip *chip);

#endif /* __LINUX_RAWNAND_INTERNALS */
