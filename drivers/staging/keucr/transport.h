#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <linux/blkdev.h>

/* Bulk only data structures */

/* command block wrapper */
struct bulk_cb_wrap {
	__le32	Signature;			/* contains 'USBC' */
	__u32	Tag;				/* unique per command id */
	__le32	DataTransferLength;	/* size of data */
	__u8	Flags;				/* direction in bit 0 */
	__u8	Lun;					/* LUN normally 0 */
	__u8	Length;				/* of of the CDB */
	__u8	CDB[16];				/* max command */
};

#define US_BULK_CB_WRAP_LEN	31
#define US_BULK_CB_SIGN		0x43425355	/*spells out USBC */
#define US_BULK_FLAG_IN		1
#define US_BULK_FLAG_OUT	0

/* command status wrapper */
struct bulk_cs_wrap {
	__le32	Signature;		/* should = 'USBS' */
	__u32		Tag;			/* same as original command */
	__le32	Residue;		/* amount not transferred */
	__u8		Status;		/* see below */
	__u8		Filler[18];
};

#define US_BULK_CS_WRAP_LEN	13
#define US_BULK_CS_SIGN		0x53425355	/* spells out 'USBS' */
#define US_BULK_STAT_OK		0
#define US_BULK_STAT_FAIL	1
#define US_BULK_STAT_PHASE	2

/* bulk-only class specific requests */
#define US_BULK_RESET_REQUEST	0xff
#define US_BULK_GET_MAX_LUN	0xfe

/* usb_stor_bulk_transfer_xxx() return codes, in order of severity */
#define USB_STOR_XFER_GOOD	0	/* good transfer                 */
#define USB_STOR_XFER_SHORT	1	/* transferred less than expected */
#define USB_STOR_XFER_STALLED	2	/* endpoint stalled              */
#define USB_STOR_XFER_LONG	3	/* device tried to send too much */
#define USB_STOR_XFER_ERROR	4	/* transfer died in the middle   */

/* Transport return codes */
#define USB_STOR_TRANSPORT_GOOD	0	/* Transport good, command good	*/
#define USB_STOR_TRANSPORT_FAILED 1	/* Transport good, command failed */
#define USB_STOR_TRANSPORT_NO_SENSE 2	/* Command failed, no auto-sense */
#define USB_STOR_TRANSPORT_ERROR 3	/* Transport bad (i.e. device dead) */

/*
 * We used to have USB_STOR_XFER_ABORTED and USB_STOR_TRANSPORT_ABORTED
 * return codes.  But now the transport and low-level transfer routines
 * treat an abort as just another error (-ENOENT for a cancelled URB).
 * It is up to the invoke_transport() function to test for aborts and
 * distinguish them from genuine communication errors.
 */

/* CBI accept device specific command */
#define US_CBI_ADSC		0
extern int usb_stor_Bulk_transport(struct scsi_cmnd *, struct us_data*);
extern int usb_stor_Bulk_max_lun(struct us_data *);
extern int usb_stor_Bulk_reset(struct us_data *);
extern void usb_stor_print_cmd(struct scsi_cmnd *);
extern void usb_stor_invoke_transport(struct scsi_cmnd *, struct us_data*);
extern void usb_stor_stop_transport(struct us_data *);
extern int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout);
extern int usb_stor_clear_halt(struct us_data *us, unsigned int pipe);
extern int usb_stor_bulk_transfer_buf(struct us_data *us, unsigned int pipe,
		void *buf, unsigned int length, unsigned int *act_len);
extern int usb_stor_bulk_transfer_sg(struct us_data *us, unsigned int pipe,
		void *buf, unsigned int length, int use_sg, int *residual);
extern int usb_stor_bulk_srb(struct us_data *us, unsigned int pipe,
		struct scsi_cmnd *srb);
extern int usb_stor_port_reset(struct us_data *us);

/* Protocol handling routines */
enum xfer_buf_dir	{TO_XFER_BUF, FROM_XFER_BUF};
extern unsigned int usb_stor_access_xfer_buf(struct us_data*,
	unsigned char *buffer, unsigned int buflen, struct scsi_cmnd *srb,
	struct scatterlist **, unsigned int *offset, enum xfer_buf_dir dir);
extern void usb_stor_set_xfer_buf(struct us_data*, unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb,
	unsigned int dir);

/*
 * ENE scsi function
 */
extern void ENE_stor_invoke_transport(struct scsi_cmnd *, struct us_data *);
extern int ENE_InitMedia(struct us_data *);
extern int ENE_SMInit(struct us_data *);
extern int ENE_SendScsiCmd(struct us_data*, BYTE, void*, int);
extern int ENE_LoadBinCode(struct us_data*, BYTE);
extern int ENE_Read_BYTE(struct us_data*, WORD index, void *buf);
extern int ENE_Read_Data(struct us_data*, void *buf, unsigned int length);
extern int ENE_Write_Data(struct us_data*, void *buf, unsigned int length);
extern void BuildSenseBuffer(struct scsi_cmnd *, int);

/*
 * ENE scsi function
 */
extern int SM_SCSIIrp(struct us_data *us, struct scsi_cmnd *srb);

#endif
