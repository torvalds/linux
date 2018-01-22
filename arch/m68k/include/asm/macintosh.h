/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACINTOSH_H
#define __ASM_MACINTOSH_H

#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <asm/bootinfo-mac.h>


/*
 *	Apple Macintoshisms
 */

extern void mac_reset(void);
extern void mac_poweroff(void);
extern void mac_init_IRQ(void);

extern void mac_irq_enable(struct irq_data *data);
extern void mac_irq_disable(struct irq_data *data);

/*
 *	Macintosh Table
 */

struct mac_model
{
	short ident;
	char *name;
	char adb_type;
	char via_type;
	char scsi_type;
	char ide_type;
	char scc_type;
	char ether_type;
	char nubus_type;
	char floppy_type;
};

#define MAC_ADB_NONE		0
#define MAC_ADB_II		1
#define MAC_ADB_EGRET		2
#define MAC_ADB_CUDA		3
#define MAC_ADB_PB1		4
#define MAC_ADB_PB2		5
#define MAC_ADB_IOP		6

#define MAC_VIA_II		1
#define MAC_VIA_IICI		2
#define MAC_VIA_QUADRA		3

#define MAC_SCSI_NONE		0
#define MAC_SCSI_OLD		1
#define MAC_SCSI_QUADRA		2
#define MAC_SCSI_QUADRA2	3
#define MAC_SCSI_QUADRA3	4
#define MAC_SCSI_IIFX		5
#define MAC_SCSI_DUO		6
#define MAC_SCSI_LC		7
#define MAC_SCSI_LATE		8

#define MAC_IDE_NONE		0
#define MAC_IDE_QUADRA		1
#define MAC_IDE_PB		2
#define MAC_IDE_BABOON		3

#define MAC_SCC_II		1
#define MAC_SCC_IOP		2
#define MAC_SCC_QUADRA		3
#define MAC_SCC_PSC		4

#define MAC_ETHER_NONE		0
#define MAC_ETHER_SONIC		1
#define MAC_ETHER_MACE		2

#define MAC_NO_NUBUS		0
#define MAC_NUBUS		1

#define MAC_FLOPPY_IWM		0
#define MAC_FLOPPY_SWIM_ADDR1	1
#define MAC_FLOPPY_SWIM_ADDR2	2
#define MAC_FLOPPY_SWIM_IOP	3
#define MAC_FLOPPY_AV		4

extern struct mac_model *macintosh_config;


    /*
     * Internal representation of the Mac hardware, filled in from bootinfo
     */

struct mac_booter_data
{
	unsigned long videoaddr;
	unsigned long videorow;
	unsigned long videodepth;
	unsigned long dimensions;
	unsigned long boottime;
	unsigned long gmtbias;
	unsigned long videological;
	unsigned long sccbase;
	unsigned long id;
	unsigned long memsize;
	unsigned long cpuid;
	unsigned long rombase;
};

extern struct mac_booter_data mac_bi_data;

#endif
