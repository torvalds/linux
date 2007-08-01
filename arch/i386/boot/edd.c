/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/edd.c
 *
 * Get EDD BIOS disk information
 */

#include "boot.h"
#include <linux/edd.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)

struct edd_dapa {
	u8	pkt_size;
	u8	rsvd;
	u16	sector_cnt;
	u16	buf_off, buf_seg;
	u64	lba;
	u64	buf_lin_addr;
};

/*
 * Read the MBR (first sector) from a specific device.
 */
static int read_mbr(u8 devno, void *buf)
{
	struct edd_dapa dapa;
	u16 ax, bx, cx, dx, si;

	memset(&dapa, 0, sizeof dapa);
	dapa.pkt_size = sizeof(dapa);
	dapa.sector_cnt = 1;
	dapa.buf_off = (size_t)buf;
	dapa.buf_seg = ds();
	/* dapa.lba = 0; */

	ax = 0x4200;		/* Extended Read */
	si = (size_t)&dapa;
	dx = devno;
	asm("pushfl; stc; int $0x13; setc %%al; popfl"
	    : "+a" (ax), "+S" (si), "+d" (dx)
	    : "m" (dapa)
	    : "ebx", "ecx", "edi", "memory");

	if (!(u8)ax)
		return 0;	/* OK */

	ax = 0x0201;		/* Legacy Read, one sector */
	cx = 0x0001;		/* Sector 0-0-1 */
	dx = devno;
	bx = (size_t)buf;
	asm("pushfl; stc; int $0x13; setc %%al; popfl"
	    : "+a" (ax), "+c" (cx), "+d" (dx), "+b" (bx)
	    : : "esi", "edi", "memory");

	return -(u8)ax;		/* 0 or -1 */
}

static u32 read_mbr_sig(u8 devno, struct edd_info *ei)
{
	int sector_size;
	char *mbrbuf_ptr, *mbrbuf_end;
	u32 mbrsig;
	u32 buf_base, mbr_base;
	extern char _end[];

	sector_size = ei->params.bytes_per_sector;
	if (!sector_size)
		sector_size = 512; /* Best available guess */

	/* Produce a naturally aligned buffer on the heap */
	buf_base = (ds() << 4) + (u32)&_end;
	mbr_base = (buf_base+sector_size-1) & ~(sector_size-1);
	mbrbuf_ptr = _end + (mbr_base-buf_base);
	mbrbuf_end = mbrbuf_ptr + sector_size;

	/* Make sure we actually have space on the heap... */
	if (!(boot_params.hdr.loadflags & CAN_USE_HEAP))
		return 0;
	if (mbrbuf_end > (char *)(size_t)boot_params.hdr.heap_end_ptr)
		return 0;

	if (read_mbr(devno, mbrbuf_ptr))
		return 0;

	mbrsig = *(u32 *)&mbrbuf_ptr[EDD_MBR_SIG_OFFSET];
	return mbrsig;
}

static int get_edd_info(u8 devno, struct edd_info *ei)
{
	u16 ax, bx, cx, dx, di;

	memset(ei, 0, sizeof *ei);

	/* Check Extensions Present */

	ax = 0x4100;
	bx = EDDMAGIC1;
	dx = devno;
	asm("pushfl; stc; int $0x13; setc %%al; popfl"
	    : "+a" (ax), "+b" (bx), "=c" (cx), "+d" (dx)
	    : : "esi", "edi");

	if ((u8)ax)
		return -1;	/* No extended information */

	if (bx != EDDMAGIC2)
		return -1;

	ei->device  = devno;
	ei->version = ax >> 8;	/* EDD version number */
	ei->interface_support = cx; /* EDD functionality subsets */

	/* Extended Get Device Parameters */

	ei->params.length = sizeof(ei->params);
	ax = 0x4800;
	dx = devno;
	asm("pushfl; int $0x13; popfl"
	    : "+a" (ax), "+d" (dx)
	    : "S" (&ei->params)
	    : "ebx", "ecx", "edi");

	/* Get legacy CHS parameters */

	/* Ralf Brown recommends setting ES:DI to 0:0 */
	ax = 0x0800;
	dx = devno;
	di = 0;
	asm("pushw %%es; "
	    "movw %%di,%%es; "
	    "pushfl; stc; int $0x13; setc %%al; popfl; "
	    "popw %%es"
	    : "+a" (ax), "=b" (bx), "=c" (cx), "+d" (dx), "+D" (di)
	    : : "esi");

	if ((u8)ax == 0) {
		ei->legacy_max_cylinder = (cx >> 8) + ((cx & 0xc0) << 2);
		ei->legacy_max_head = dx >> 8;
		ei->legacy_sectors_per_track = cx & 0x3f;
	}

	return 0;
}

void query_edd(void)
{
	char eddarg[8];
	int do_mbr = 1;
	int do_edd = 1;
	int devno;
	struct edd_info ei, *edp;

	if (cmdline_find_option("edd", eddarg, sizeof eddarg) > 0) {
		if (!strcmp(eddarg, "skipmbr") || !strcmp(eddarg, "skip"))
			do_mbr = 0;
		else if (!strcmp(eddarg, "off"))
			do_edd = 0;
	}

	edp = (struct edd_info *)boot_params.eddbuf;

	if (!do_edd)
		return;

	for (devno = 0x80; devno < 0x80+EDD_MBR_SIG_MAX; devno++) {
		/*
		 * Scan the BIOS-supported hard disks and query EDD
		 * information...
		 */
		get_edd_info(devno, &ei);

		if (boot_params.eddbuf_entries < EDDMAXNR) {
			memcpy(edp, &ei, sizeof ei);
			edp++;
			boot_params.eddbuf_entries++;
		}

		if (do_mbr) {
			u32 mbr_sig;
			mbr_sig = read_mbr_sig(devno, &ei);
			boot_params.edd_mbr_sig_buffer[devno-0x80] = mbr_sig;
		}
	}
}

#endif
