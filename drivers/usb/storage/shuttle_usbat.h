/* Driver for SCM Microsystems USB-ATAPI cable
 * Header File
 *
 * Current development and maintenance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *   (c) 2004, 2005 Daniel Drake <dsd@gentoo.org>
 *
 * See shuttle_usbat.c for more explanation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _USB_SHUTTLE_USBAT_H
#define _USB_SHUTTLE_USBAT_H

/* Supported device types */
#define USBAT_DEV_HP8200	0x01
#define USBAT_DEV_FLASH		0x02

#define USBAT_EPP_PORT		0x10
#define USBAT_EPP_REGISTER	0x30
#define USBAT_ATA		0x40
#define USBAT_ISA		0x50

/* Commands (need to be logically OR'd with an access type */
#define USBAT_CMD_READ_REG		0x00
#define USBAT_CMD_WRITE_REG		0x01
#define USBAT_CMD_READ_BLOCK	0x02
#define USBAT_CMD_WRITE_BLOCK	0x03
#define USBAT_CMD_COND_READ_BLOCK	0x04
#define USBAT_CMD_COND_WRITE_BLOCK	0x05
#define USBAT_CMD_WRITE_REGS	0x07

/* Commands (these don't need an access type) */
#define USBAT_CMD_EXEC_CMD	0x80
#define USBAT_CMD_SET_FEAT	0x81
#define USBAT_CMD_UIO		0x82

/* Methods of accessing UIO register */
#define USBAT_UIO_READ	1
#define USBAT_UIO_WRITE	0

/* Qualifier bits */
#define USBAT_QUAL_FCQ	0x20	/* full compare */
#define USBAT_QUAL_ALQ	0x10	/* auto load subcount */

/* USBAT Flash Media status types */
#define USBAT_FLASH_MEDIA_NONE	0
#define USBAT_FLASH_MEDIA_CF	1

/* USBAT Flash Media change types */
#define USBAT_FLASH_MEDIA_SAME	0
#define USBAT_FLASH_MEDIA_CHANGED	1

/* USBAT ATA registers */
#define USBAT_ATA_DATA      0x10  /* read/write data (R/W) */
#define USBAT_ATA_FEATURES  0x11  /* set features (W) */
#define USBAT_ATA_ERROR     0x11  /* error (R) */
#define USBAT_ATA_SECCNT    0x12  /* sector count (R/W) */
#define USBAT_ATA_SECNUM    0x13  /* sector number (R/W) */
#define USBAT_ATA_LBA_ME    0x14  /* cylinder low (R/W) */
#define USBAT_ATA_LBA_HI    0x15  /* cylinder high (R/W) */
#define USBAT_ATA_DEVICE    0x16  /* head/device selection (R/W) */
#define USBAT_ATA_STATUS    0x17  /* device status (R) */
#define USBAT_ATA_CMD       0x17  /* device command (W) */
#define USBAT_ATA_ALTSTATUS 0x0E  /* status (no clear IRQ) (R) */

/* USBAT User I/O Data registers */
#define USBAT_UIO_EPAD		0x80 /* Enable Peripheral Control Signals */
#define USBAT_UIO_CDT		0x40 /* Card Detect (Read Only) */
				     /* CDT = ACKD & !UI1 & !UI0 */
#define USBAT_UIO_1		0x20 /* I/O 1 */
#define USBAT_UIO_0		0x10 /* I/O 0 */
#define USBAT_UIO_EPP_ATA	0x08 /* 1=EPP mode, 0=ATA mode */
#define USBAT_UIO_UI1		0x04 /* Input 1 */
#define USBAT_UIO_UI0		0x02 /* Input 0 */
#define USBAT_UIO_INTR_ACK	0x01 /* Interrupt (ATA/ISA)/Acknowledge (EPP) */

/* USBAT User I/O Enable registers */
#define USBAT_UIO_DRVRST	0x80 /* Reset Peripheral */
#define USBAT_UIO_ACKD		0x40 /* Enable Card Detect */
#define USBAT_UIO_OE1		0x20 /* I/O 1 set=output/clr=input */
				     /* If ACKD=1, set OE1 to 1 also. */
#define USBAT_UIO_OE0		0x10 /* I/O 0 set=output/clr=input */
#define USBAT_UIO_ADPRST	0x01 /* Reset SCM chip */

/* USBAT Features */
#define USBAT_FEAT_ETEN	0x80	/* External trigger enable */
#define USBAT_FEAT_U1	0x08
#define USBAT_FEAT_U0	0x04
#define USBAT_FEAT_ET1	0x02
#define USBAT_FEAT_ET2	0x01

extern int usbat_transport(struct scsi_cmnd *srb, struct us_data *us);
extern int init_usbat_cd(struct us_data *us);
extern int init_usbat_flash(struct us_data *us);
extern int init_usbat_probe(struct us_data *us);

struct usbat_info {
	int devicetype;

	/* Used for Flash readers only */
	unsigned long sectors;     /* total sector count */
	unsigned long ssize;       /* sector size in bytes */

	unsigned char sense_key;
	unsigned long sense_asc;   /* additional sense code */
	unsigned long sense_ascq;  /* additional sense code qualifier */
};

#endif
