#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <linux/blkdev.h>

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
extern int ENE_SendScsiCmd(struct us_data*, u8, void*, int);
extern int ENE_LoadBinCode(struct us_data*, u8);
extern int ene_read_byte(struct us_data*, u16 index, void *buf);
extern int ENE_Read_Data(struct us_data*, void *buf, unsigned int length);
extern int ENE_Write_Data(struct us_data*, void *buf, unsigned int length);
extern void BuildSenseBuffer(struct scsi_cmnd *, int);

/*
 * ENE scsi function
 */
extern int SM_SCSIIrp(struct us_data *us, struct scsi_cmnd *srb);

#endif
