/* Driver for USB Mass Storage compliant devices */

#ifndef _USB_H_
#define _USB_H_

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <scsi/scsi_host.h>
#include "common.h"

struct us_data;
struct scsi_cmnd;

/*
 * Unusual device list definitions
 */

struct us_unusual_dev {
	const char *vendorName;
	const char *productName;
	__u8  useProtocol;
	__u8  useTransport;
	int (*initFunction)(struct us_data *);
};

/* EnE HW Register */
#define REG_CARD_STATUS     0xFF83
#define REG_HW_TRAP1        0xFF89

/* SRB Status. Refers /usr/include/wine/wine/wnaspi32.h & SCSI sense key */
#define SS_SUCCESS                  0x00      /* No Sense */
#define SS_NOT_READY                0x02
#define SS_MEDIUM_ERR               0x03
#define SS_HW_ERR                   0x04
#define SS_ILLEGAL_REQUEST          0x05
#define SS_UNIT_ATTENTION           0x06

/* ENE Load FW Pattern */
#define SD_INIT1_PATTERN   1
#define SD_INIT2_PATTERN   2
#define SD_RW_PATTERN      3
#define MS_INIT_PATTERN    4
#define MSP_RW_PATTERN     5
#define MS_RW_PATTERN      6
#define SM_INIT_PATTERN    7
#define SM_RW_PATTERN      8

#define FDIR_WRITE        0
#define FDIR_READ         1

struct keucr_sd_status {
	BYTE    Insert:1;
	BYTE    Ready:1;
	BYTE    MediaChange:1;
	BYTE    IsMMC:1;
	BYTE    HiCapacity:1;
	BYTE    HiSpeed:1;
	BYTE    WtP:1;
	BYTE    Reserved:1;
};

struct keucr_ms_status {
	BYTE    Insert:1;
	BYTE    Ready:1;
	BYTE    MediaChange:1;
	BYTE    IsMSPro:1;
	BYTE    IsMSPHG:1;
	BYTE    Reserved1:1;
	BYTE    WtP:1;
	BYTE    Reserved2:1;
};

struct keucr_sm_status {
	BYTE    Insert:1;
	BYTE    Ready:1;
	BYTE    MediaChange:1;
	BYTE    Reserved:3;
	BYTE    WtP:1;
	BYTE    IsMS:1;
};

/* SD Block Length */
#define SD_BLOCK_LEN		9	/* 2^9 = 512 Bytes,
				The HW maximum read/write data length */

/* Dynamic bitflag definitions (us->dflags): used in set_bit() etc. */
#define US_FLIDX_URB_ACTIVE	0	/* current_urb is in use    */
#define US_FLIDX_SG_ACTIVE	1	/* current_sg is in use     */
#define US_FLIDX_ABORTING	2	/* abort is in progress     */
#define US_FLIDX_DISCONNECTING	3	/* disconnect in progress   */
#define US_FLIDX_RESETTING	4	/* device reset in progress */
#define US_FLIDX_TIMED_OUT	5	/* SCSI midlayer timed out  */
#define US_FLIDX_DONT_SCAN	6	/* don't scan (disconnect)  */


#define USB_STOR_STRING_LEN 32

/*
 * We provide a DMA-mapped I/O buffer for use with small USB transfers.
 * It turns out that CB[I] needs a 12-byte buffer and Bulk-only needs a
 * 31-byte buffer.  But Freecom needs a 64-byte buffer, so that's the
 * size we'll allocate.
 */

#define US_IOBUF_SIZE		64	/* Size of the DMA-mapped I/O buffer */
#define US_SENSE_SIZE		18	/* Size of the autosense data buffer */

typedef int (*trans_cmnd)(struct scsi_cmnd *, struct us_data *);
typedef int (*trans_reset)(struct us_data *);
typedef void (*proto_cmnd)(struct scsi_cmnd *, struct us_data *);
typedef void (*extra_data_destructor)(void *);	/* extra data destructor */
typedef void (*pm_hook)(struct us_data *, int);	/* power management hook */

#define US_SUSPEND	0
#define US_RESUME	1

/* we allocate one of these for every device that we remember */
struct us_data {
	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_mutex to change pusb_dev
	 */
	struct mutex		dev_mutex;	 /* protect pusb_dev */
	struct usb_device	*pusb_dev;	 /* this usb_device */
	struct usb_interface	*pusb_intf;	 /* this interface */
	struct us_unusual_dev   *unusual_dev;	 /* device-filter entry     */
	unsigned long		fflags;		 /* fixed flags from filter */
	unsigned long		dflags;		 /* dynamic atomic bitflags */
	unsigned int		send_bulk_pipe;	 /* cached pipe values */
	unsigned int		recv_bulk_pipe;
	unsigned int		send_ctrl_pipe;
	unsigned int		recv_ctrl_pipe;
	unsigned int		recv_intr_pipe;

	/* information about the device */
	char			*transport_name;
	char			*protocol_name;
	__le32			bcs_signature;
	u8			subclass;
	u8			protocol;
	u8			max_lun;

	u8			ifnum;		 /* interface number   */
	u8			ep_bInterval;	 /* interrupt interval */

	/* function pointers for this device */
	trans_cmnd		transport;	 /* transport function	   */
	trans_reset		transport_reset; /* transport device reset */
	proto_cmnd		proto_handler;	 /* protocol handler	   */

	/* SCSI interfaces */
	struct scsi_cmnd	*srb;		 /* current srb		*/
	unsigned int		tag;		 /* current dCBWTag	*/

	/* control and bulk communications data */
	struct urb		*current_urb;	 /* USB requests	 */
	struct usb_ctrlrequest	*cr;		 /* control requests	 */
	struct usb_sg_request	current_sg;	 /* scatter-gather req.  */
	unsigned char		*iobuf;		 /* I/O buffer		 */
	unsigned char		*sensebuf;	 /* sense data buffer	 */
	dma_addr_t		cr_dma;		 /* buffer DMA addresses */
	dma_addr_t		iobuf_dma;
	struct task_struct	*ctl_thread;	 /* the control thread   */

	/* mutual exclusion and synchronization structures */
	struct completion	cmnd_ready;	 /* to sleep thread on	    */
	struct completion	notify;		 /* thread begin/end	    */
	wait_queue_head_t	delay_wait;	 /* wait during scan, reset */
	struct completion	scanning_done;	 /* wait for scan thread    */

	/* subdriver information */
	void			*extra;		 /* Any extra data          */
	extra_data_destructor	extra_destructor;/* extra data destructor   */
#ifdef CONFIG_PM
	pm_hook			suspend_resume_hook;
#endif
	/* for 6250 code */
	struct keucr_sd_status   SD_Status;
	struct keucr_ms_status   MS_Status;
	struct keucr_sm_status   SM_Status;

	/* ----- SD Control Data ---------------- */
	/* SD_REGISTER SD_Regs; */
	WORD        SD_Block_Mult;
	BYTE        SD_READ_BL_LEN;
	WORD        SD_C_SIZE;
	BYTE        SD_C_SIZE_MULT;

	/* SD/MMC New spec. */
	BYTE        SD_SPEC_VER;
	BYTE        SD_CSD_VER;
	BYTE        SD20_HIGH_CAPACITY;
	DWORD       HC_C_SIZE;
	BYTE        MMC_SPEC_VER;
	BYTE        MMC_BusWidth;
	BYTE        MMC_HIGH_CAPACITY;

	/* ----- MS Control Data ---------------- */
	BOOLEAN             MS_SWWP;
	DWORD               MSP_TotalBlock;
	/* MS_LibControl       MS_Lib; */
	BOOLEAN             MS_IsRWPage;
	WORD                MS_Model;

	/* ----- SM Control Data ---------------- */
	BYTE		SM_DeviceID;
	BYTE		SM_CardID;

	PBYTE		testbuf;
	BYTE		BIN_FLAG;
	DWORD		bl_num;
	int		SrbStatus;

	/* ------Power Managerment --------------- */
	BOOLEAN         Power_IsResum;
};

/* Convert between us_data and the corresponding Scsi_Host */
static inline struct Scsi_Host *us_to_host(struct us_data *us)
{
	return container_of((void *) us, struct Scsi_Host, hostdata);
}
static inline struct us_data *host_to_us(struct Scsi_Host *host)
{
	return (struct us_data *) host->hostdata;
}

/* Function to fill an inquiry response. See usb.c for details */
extern void fill_inquiry_response(struct us_data *us,
	unsigned char *data, unsigned int data_len);

/* The scsi_lock() and scsi_unlock() macros protect the sm_state and the
 * single queue element srb for write access */
#define scsi_unlock(host)	spin_unlock_irq(host->host_lock)
#define scsi_lock(host)		spin_lock_irq(host->host_lock)

#endif
