/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __MACH_UX500_ID
#define __MACH_UX500_ID

/**
 * struct dbx500_asic_id - fields of the ASIC ID
 * @process: the manufacturing process, 0x40 is 40 nm 0x00 is "standard"
 * @partnumber: hithereto 0x8500 for DB8500
 * @revision: version code in the series
 */
struct dbx500_asic_id {
	u16	partnumber;
	u8	revision;
	u8	process;
};

extern struct dbx500_asic_id dbx500_id;

static inline unsigned int __attribute_const__ dbx500_partnumber(void)
{
	return dbx500_id.partnumber;
}

static inline unsigned int __attribute_const__ dbx500_revision(void)
{
	return dbx500_id.revision;
}

#define ux500_unknown_soc()	BUG()

#endif
