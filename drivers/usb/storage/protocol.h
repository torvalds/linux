/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for USB Mass Storage compliant devices
 * Protocol Functions Header File
 *
 * Current development and maintenance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 */

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

/* Protocol handling routines */
extern void usb_stor_pad12_command(struct scsi_cmnd*, struct us_data*);
extern void usb_stor_ufi_command(struct scsi_cmnd*, struct us_data*);
extern void usb_stor_transparent_scsi_command(struct scsi_cmnd*,
		struct us_data*);

/* struct scsi_cmnd transfer buffer access utilities */
enum xfer_buf_dir	{TO_XFER_BUF, FROM_XFER_BUF};

extern unsigned int usb_stor_access_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb, struct scatterlist **,
	unsigned int *offset, enum xfer_buf_dir dir);

extern void usb_stor_set_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb);
#endif
