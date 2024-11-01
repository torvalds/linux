/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ATP870U_H
#define _ATP870U_H

#include <linux/types.h>
#include <linux/kdev_t.h>

/* I/O Port */

#define MAX_CDB		12
#define MAX_SENSE	14
#define qcnt		32
#define ATP870U_SCATTER	128

#define MAX_ADAPTER	8
#define MAX_SCSI_ID	16
#define ATP870U_MAX_SECTORS 128

#define ATP885_DEVID 0x808A
#define ATP880_DEVID1 0x8080
#define ATP880_DEVID2 0x8081

//#define ED_DBGP

struct atp_unit
{
	unsigned long baseport;
	unsigned long ioport[2];
	unsigned long pciport[2];
	unsigned char last_cmd[2];
	unsigned char in_snd[2];
	unsigned char in_int[2];
	unsigned char quhd[2];
	unsigned char quend[2];
	unsigned char global_map[2];
	unsigned char host_id[2];
	unsigned int working[2];
	unsigned short wide_id[2];
	unsigned short active_id[2];
	unsigned short ultra_map[2];
	unsigned short async[2];
	unsigned char sp[2][16];
	unsigned char r1f[2][16];
	struct scsi_cmnd *quereq[2][qcnt];
	struct atp_id
	{
		unsigned char dirct;
		unsigned char devsp;
		unsigned char devtype;
		unsigned long tran_len;
		unsigned long last_len;
		unsigned char *prd_pos;
		unsigned char *prd_table;	/* Kernel address of PRD table */
		dma_addr_t prd_bus;		/* Bus address of PRD */
		dma_addr_t prdaddr;		/* Dynamically updated in driver */
		struct scsi_cmnd *curr_req;
	} id[2][16];
	struct Scsi_Host *host;
	struct pci_dev *pdev;
	unsigned int unit;
};

#endif
