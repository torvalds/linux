/*
 * Thomas Horsten <thh@lasat.com>
 * Copyright (C) 2000 LASAT Networks A/S.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Routines specific to the LASAT boards
 */
#include <linux/types.h>
#include <linux/crc32.h>
#include <asm/lasat/lasat.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include "at93c.h"
/* New model description table */
#include "lasat_models.h"

#define EEPROM_CRC(data, len) (~crc32(~0, data, len))

struct lasat_info lasat_board_info;

void update_bcastaddr(void);

int EEPROMRead(unsigned int pos, unsigned char *data, int len)
{
	int i;

	for (i = 0; i < len; i++)
		*data++ = at93c_read(pos++);

	return 0;
}

int EEPROMWrite(unsigned int pos, unsigned char *data, int len)
{
	int i;

	for (i = 0; i < len; i++)
		at93c_write(pos++, *data++);

	return 0;
}

static void init_flash_sizes(void)
{
	unsigned long *lb = lasat_board_info.li_flashpart_base;
	unsigned long *ls = lasat_board_info.li_flashpart_size;
	int i;

	ls[LASAT_MTD_BOOTLOADER] = 0x40000;
	ls[LASAT_MTD_SERVICE] = 0xC0000;
	ls[LASAT_MTD_NORMAL] = 0x100000;

	if (mips_machtype == MACH_LASAT_100) {
		lasat_board_info.li_flash_base = 0x1e000000;

		lb[LASAT_MTD_BOOTLOADER] = 0x1e400000;

		if (lasat_board_info.li_flash_size > 0x200000) {
			ls[LASAT_MTD_CONFIG] = 0x100000;
			ls[LASAT_MTD_FS] = 0x500000;
		}
	} else {
		lasat_board_info.li_flash_base = 0x10000000;

		if (lasat_board_info.li_flash_size < 0x1000000) {
			lb[LASAT_MTD_BOOTLOADER] = 0x10000000;
			ls[LASAT_MTD_CONFIG] = 0x100000;
			if (lasat_board_info.li_flash_size >= 0x400000)
				ls[LASAT_MTD_FS] =
				     lasat_board_info.li_flash_size - 0x300000;
		}
	}

	for (i = 1; i < LASAT_MTD_LAST; i++)
		lb[i] = lb[i-1] + ls[i-1];
}

int lasat_init_board_info(void)
{
	int c;
	unsigned long crc;
	unsigned long cfg0, cfg1;
	const struct product_info   *ppi;
	int i_n_base_models = N_BASE_MODELS;
	const char * const * i_txt_base_models = txt_base_models;
	int i_n_prids = N_PRIDS;

	memset(&lasat_board_info, 0, sizeof(lasat_board_info));

	/* First read the EEPROM info */
	EEPROMRead(0, (unsigned char *)&lasat_board_info.li_eeprom_info,
		   sizeof(struct lasat_eeprom_struct));

	/* Check the CRC */
	crc = EEPROM_CRC((unsigned char *)(&lasat_board_info.li_eeprom_info),
		    sizeof(struct lasat_eeprom_struct) - 4);

	if (crc != lasat_board_info.li_eeprom_info.crc32) {
		printk(KERN_WARNING "WARNING...\nWARNING...\nEEPROM CRC does "
		       "not match calculated, attempting to soldier on...\n");
	}

	if (lasat_board_info.li_eeprom_info.version != LASAT_EEPROM_VERSION) {
		printk(KERN_WARNING "WARNING...\nWARNING...\nEEPROM version "
		       "%d, wanted version %d, attempting to soldier on...\n",
		       (unsigned int)lasat_board_info.li_eeprom_info.version,
		       LASAT_EEPROM_VERSION);
	}

	cfg0 = lasat_board_info.li_eeprom_info.cfg[0];
	cfg1 = lasat_board_info.li_eeprom_info.cfg[1];

	if (LASAT_W0_DSCTYPE(cfg0) != 1) {
		printk(KERN_WARNING "WARNING...\nWARNING...\n"
		       "Invalid configuration read from EEPROM, attempting to "
		       "soldier on...");
	}
	/* We have a valid configuration */

	switch (LASAT_W0_SDRAMBANKSZ(cfg0)) {
	case 0:
		lasat_board_info.li_memsize = 0x0800000;
		break;
	case 1:
		lasat_board_info.li_memsize = 0x1000000;
		break;
	case 2:
		lasat_board_info.li_memsize = 0x2000000;
		break;
	case 3:
		lasat_board_info.li_memsize = 0x4000000;
		break;
	case 4:
		lasat_board_info.li_memsize = 0x8000000;
		break;
	default:
		lasat_board_info.li_memsize = 0;
	}

	switch (LASAT_W0_SDRAMBANKS(cfg0)) {
	case 0:
		break;
	case 1:
		lasat_board_info.li_memsize *= 2;
		break;
	default:
		break;
	}

	switch (LASAT_W0_BUSSPEED(cfg0)) {
	case 0x0:
		lasat_board_info.li_bus_hz = 60000000;
		break;
	case 0x1:
		lasat_board_info.li_bus_hz = 66000000;
		break;
	case 0x2:
		lasat_board_info.li_bus_hz = 66666667;
		break;
	case 0x3:
		lasat_board_info.li_bus_hz = 80000000;
		break;
	case 0x4:
		lasat_board_info.li_bus_hz = 83333333;
		break;
	case 0x5:
		lasat_board_info.li_bus_hz = 100000000;
		break;
	}

	switch (LASAT_W0_CPUCLK(cfg0)) {
	case 0x0:
		lasat_board_info.li_cpu_hz =
			lasat_board_info.li_bus_hz;
		break;
	case 0x1:
		lasat_board_info.li_cpu_hz =
			lasat_board_info.li_bus_hz +
			(lasat_board_info.li_bus_hz >> 1);
		break;
	case 0x2:
		lasat_board_info.li_cpu_hz =
			lasat_board_info.li_bus_hz +
			lasat_board_info.li_bus_hz;
		break;
	case 0x3:
		lasat_board_info.li_cpu_hz =
			lasat_board_info.li_bus_hz +
			lasat_board_info.li_bus_hz +
			(lasat_board_info.li_bus_hz >> 1);
		break;
	case 0x4:
		lasat_board_info.li_cpu_hz =
			lasat_board_info.li_bus_hz +
			lasat_board_info.li_bus_hz +
			lasat_board_info.li_bus_hz;
		break;
	}

	/* Flash size */
	switch (LASAT_W1_FLASHSIZE(cfg1)) {
	case 0:
		lasat_board_info.li_flash_size = 0x200000;
		break;
	case 1:
		lasat_board_info.li_flash_size = 0x400000;
		break;
	case 2:
		lasat_board_info.li_flash_size = 0x800000;
		break;
	case 3:
		lasat_board_info.li_flash_size = 0x1000000;
		break;
	case 4:
		lasat_board_info.li_flash_size = 0x2000000;
		break;
	}

	init_flash_sizes();

	lasat_board_info.li_bmid = LASAT_W0_BMID(cfg0);
	lasat_board_info.li_prid = lasat_board_info.li_eeprom_info.prid;
	if (lasat_board_info.li_prid == 0xffff || lasat_board_info.li_prid == 0)
		lasat_board_info.li_prid = lasat_board_info.li_bmid;

	/* Base model stuff */
	if (lasat_board_info.li_bmid > i_n_base_models)
		lasat_board_info.li_bmid = i_n_base_models;
	strcpy(lasat_board_info.li_bmstr,
	       i_txt_base_models[lasat_board_info.li_bmid]);

	/* Product ID dependent values */
	c = lasat_board_info.li_prid;
	if (c >= i_n_prids) {
		strcpy(lasat_board_info.li_namestr, "Unknown Model");
		strcpy(lasat_board_info.li_typestr, "Unknown Type");
	} else {
		ppi = &vendor_info_table[0].vi_product_info[c];
		strcpy(lasat_board_info.li_namestr, ppi->pi_name);
		if (ppi->pi_type)
			strcpy(lasat_board_info.li_typestr, ppi->pi_type);
		else
			sprintf(lasat_board_info.li_typestr, "%d", 10 * c);
	}

#if defined(CONFIG_INET) && defined(CONFIG_SYSCTL)
	update_bcastaddr();
#endif

	return 0;
}

void lasat_write_eeprom_info(void)
{
	unsigned long crc;

	/* Generate the CRC */
	crc = EEPROM_CRC((unsigned char *)(&lasat_board_info.li_eeprom_info),
		    sizeof(struct lasat_eeprom_struct) - 4);
	lasat_board_info.li_eeprom_info.crc32 = crc;

	/* Write the EEPROM info */
	EEPROMWrite(0, (unsigned char *)&lasat_board_info.li_eeprom_info,
		    sizeof(struct lasat_eeprom_struct));
}
