/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SYM_NVRAM_H
#define SYM_NVRAM_H

#include "sym53c8xx.h"

/*
 *	Symbios NVRAM data format
 */
#define SYMBIOS_NVRAM_SIZE 368
#define SYMBIOS_NVRAM_ADDRESS 0x100

struct Symbios_nvram {
/* Header 6 bytes */
	u_short type;		/* 0x0000 */
	u_short byte_count;	/* excluding header/trailer */
	u_short checksum;

/* Controller set up 20 bytes */
	u_char	v_major;	/* 0x00 */
	u_char	v_minor;	/* 0x30 */
	u32	boot_crc;
	u_short	flags;
#define SYMBIOS_SCAM_ENABLE	(1)
#define SYMBIOS_PARITY_ENABLE	(1<<1)
#define SYMBIOS_VERBOSE_MSGS	(1<<2)
#define SYMBIOS_CHS_MAPPING	(1<<3)
#define SYMBIOS_NO_NVRAM	(1<<3)	/* ??? */
	u_short	flags1;
#define SYMBIOS_SCAN_HI_LO	(1)
	u_short	term_state;
#define SYMBIOS_TERM_CANT_PROGRAM	(0)
#define SYMBIOS_TERM_ENABLED		(1)
#define SYMBIOS_TERM_DISABLED		(2)
	u_short	rmvbl_flags;
#define SYMBIOS_RMVBL_NO_SUPPORT	(0)
#define SYMBIOS_RMVBL_BOOT_DEVICE	(1)
#define SYMBIOS_RMVBL_MEDIA_INSTALLED	(2)
	u_char	host_id;
	u_char	num_hba;	/* 0x04 */
	u_char	num_devices;	/* 0x10 */
	u_char	max_scam_devices;	/* 0x04 */
	u_char	num_valid_scam_devices;	/* 0x00 */
	u_char	flags2;
#define SYMBIOS_AVOID_BUS_RESET		(1<<2)

/* Boot order 14 bytes * 4 */
	struct Symbios_host{
		u_short	type;		/* 4:8xx / 0:nok */
		u_short	device_id;	/* PCI device id */
		u_short	vendor_id;	/* PCI vendor id */
		u_char	bus_nr;		/* PCI bus number */
		u_char	device_fn;	/* PCI device/function number << 3*/
		u_short	word8;
		u_short	flags;
#define	SYMBIOS_INIT_SCAN_AT_BOOT	(1)
		u_short	io_port;	/* PCI io_port address */
	} host[4];

/* Targets 8 bytes * 16 */
	struct Symbios_target {
		u_char	flags;
#define SYMBIOS_DISCONNECT_ENABLE	(1)
#define SYMBIOS_SCAN_AT_BOOT_TIME	(1<<1)
#define SYMBIOS_SCAN_LUNS		(1<<2)
#define SYMBIOS_QUEUE_TAGS_ENABLED	(1<<3)
		u_char	rsvd;
		u_char	bus_width;	/* 0x08/0x10 */
		u_char	sync_offset;
		u_short	sync_period;	/* 4*period factor */
		u_short	timeout;
	} target[16];
/* Scam table 8 bytes * 4 */
	struct Symbios_scam {
		u_short	id;
		u_short	method;
#define SYMBIOS_SCAM_DEFAULT_METHOD	(0)
#define SYMBIOS_SCAM_DONT_ASSIGN	(1)
#define SYMBIOS_SCAM_SET_SPECIFIC_ID	(2)
#define SYMBIOS_SCAM_USE_ORDER_GIVEN	(3)
		u_short status;
#define SYMBIOS_SCAM_UNKNOWN		(0)
#define SYMBIOS_SCAM_DEVICE_NOT_FOUND	(1)
#define SYMBIOS_SCAM_ID_NOT_SET		(2)
#define SYMBIOS_SCAM_ID_VALID		(3)
		u_char	target_id;
		u_char	rsvd;
	} scam[4];

	u_char	spare_devices[15*8];
	u_char	trailer[6];		/* 0xfe 0xfe 0x00 0x00 0x00 0x00 */
};
typedef struct Symbios_nvram	Symbios_nvram;
typedef struct Symbios_host	Symbios_host;
typedef struct Symbios_target	Symbios_target;
typedef struct Symbios_scam	Symbios_scam;

/*
 *	Tekram NvRAM data format.
 */
#define TEKRAM_NVRAM_SIZE 64
#define TEKRAM_93C46_NVRAM_ADDRESS 0
#define TEKRAM_24C16_NVRAM_ADDRESS 0x40

struct Tekram_nvram {
	struct Tekram_target {
		u_char	flags;
#define	TEKRAM_PARITY_CHECK		(1)
#define TEKRAM_SYNC_NEGO		(1<<1)
#define TEKRAM_DISCONNECT_ENABLE	(1<<2)
#define	TEKRAM_START_CMD		(1<<3)
#define TEKRAM_TAGGED_COMMANDS		(1<<4)
#define TEKRAM_WIDE_NEGO		(1<<5)
		u_char	sync_index;
		u_short	word2;
	} target[16];
	u_char	host_id;
	u_char	flags;
#define TEKRAM_MORE_THAN_2_DRIVES	(1)
#define TEKRAM_DRIVES_SUP_1GB		(1<<1)
#define	TEKRAM_RESET_ON_POWER_ON	(1<<2)
#define TEKRAM_ACTIVE_NEGATION		(1<<3)
#define TEKRAM_IMMEDIATE_SEEK		(1<<4)
#define	TEKRAM_SCAN_LUNS		(1<<5)
#define	TEKRAM_REMOVABLE_FLAGS		(3<<6)	/* 0: disable; */
						/* 1: boot device; 2:all */
	u_char	boot_delay_index;
	u_char	max_tags_index;
	u_short	flags1;
#define TEKRAM_F2_F6_ENABLED		(1)
	u_short	spare[29];
};
typedef struct Tekram_nvram	Tekram_nvram;
typedef struct Tekram_target	Tekram_target;

#ifndef CONFIG_PARISC
struct pdc_initiator { int dummy; };
#endif

/*
 *  Union of supported NVRAM formats.
 */
struct sym_nvram {
	int type;
#define	SYM_SYMBIOS_NVRAM	(1)
#define	SYM_TEKRAM_NVRAM	(2)
#define SYM_PARISC_PDC		(3)
#if SYM_CONF_NVRAM_SUPPORT
	union {
		Symbios_nvram Symbios;
		Tekram_nvram Tekram;
		struct pdc_initiator parisc;
	} data;
#endif
};

#if SYM_CONF_NVRAM_SUPPORT
void sym_nvram_setup_host(struct Scsi_Host *shost, struct sym_hcb *np, struct sym_nvram *nvram);
void sym_nvram_setup_target (struct sym_hcb *np, int target, struct sym_nvram *nvp);
int sym_read_nvram (struct sym_device *np, struct sym_nvram *nvp);
char *sym_nvram_type(struct sym_nvram *nvp);
#else
static inline void sym_nvram_setup_host(struct Scsi_Host *shost, struct sym_hcb *np, struct sym_nvram *nvram) { }
static inline void sym_nvram_setup_target(struct sym_hcb *np, struct sym_nvram *nvram) { }
static inline int sym_read_nvram(struct sym_device *np, struct sym_nvram *nvp)
{
	nvp->type = 0;
	return 0;
}
static inline char *sym_nvram_type(struct sym_nvram *nvp)
{
	return "No NVRAM";
}
#endif

#endif /* SYM_NVRAM_H */
