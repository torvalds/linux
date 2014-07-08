#ifndef _FT1000_USB_H_
#define _FT1000_USB_H_

#include "../ft1000.h"
#include "ft1000_ioctl.h"
#define FT1000_DRV_VER      0x01010403

#define  MAX_NUM_APP         6
#define  MAX_MSG_LIMIT       200
#define  NUM_OF_FREE_BUFFERS 1500

#define PSEUDOSZ                16

struct app_info_block {
	u32 nTxMsg;                    /* DPRAM msg sent to DSP with app_id */
	u32 nRxMsg;                    /* DPRAM msg rcv from dsp with app_id */
	u32 nTxMsgReject;              /* DPRAM msg rejected due to DSP doorbell
					* set
					*/
	u32 nRxMsgMiss;                /* DPRAM msg dropped due to overflow */
	struct fown_struct *fileobject;/* Application's file object */
	u16 app_id;                    /* Application id */
	int DspBCMsgFlag;
	int NumOfMsg;                   /* number of messages queued up */
	wait_queue_head_t wait_dpram_msg;
	struct list_head app_sqlist;   /* link list of msgs for applicaton on
					* slow queue
					*/
} __packed;

#define DEBUG(args...) pr_info(args)

#define FALSE           0
#define TRUE            1

#define FT1000_STATUS_CLOSING  0x01

#define DSPBCMSGID              0x10

/* Electrabuzz specific DPRAM mapping */
/* this is used by ft1000_usb driver - isn't that a bug? */
#undef FT1000_DPRAM_RX_BASE
#define FT1000_DPRAM_RX_BASE	0x1800	/* RX AREA (SlowQ) */

/* MEMORY MAP FOR MAGNEMITE */
/* the indexes are swapped comparing to PCMCIA - is it OK or a bug? */
#undef FT1000_MAG_DSP_LED_INDX
#define FT1000_MAG_DSP_LED_INDX		0x1	/* dsp led status for PAD
						 * device
						 */
#undef FT1000_MAG_DSP_CON_STATE_INDX
#define FT1000_MAG_DSP_CON_STATE_INDX	0x0	/* DSP Connection Status Info */

/* Maximum times trying to get ASIC out of reset */
#define MAX_ASIC_RESET_CNT      20

#define MAX_BUF_SIZE            4096

struct ft1000_debug_dirs {
	struct list_head list;
	struct dentry *dent;
	struct dentry *file;
	int int_number;
};

struct ft1000_usb {
	struct usb_device *dev;
	struct net_device *net;

	u32 status;

	struct urb *rx_urb;
	struct urb *tx_urb;

	u8 tx_buf[MAX_BUF_SIZE];
	u8 rx_buf[MAX_BUF_SIZE];

	u8 bulk_in_endpointAddr;
	u8 bulk_out_endpointAddr;

	struct task_struct *pPollThread;
	unsigned char fcodeldr;
	unsigned char bootmode;
	unsigned char usbboot;
	unsigned short dspalive;
	bool fProvComplete;
	bool fCondResetPend;
	bool fAppMsgPend;
	int DeviceCreated;
	int NetDevRegDone;
	u8 CardNumber;
	u8 DeviceName[15];
	struct ft1000_debug_dirs nodes;
	spinlock_t fifo_lock;
	int appcnt;
	struct app_info_block app_info[MAX_NUM_APP];
	u16 DrvMsgPend;
	unsigned short tempbuf[32];
} __packed;


struct dpram_blk {
	struct list_head list;
	u16 *pbuffer;
} __packed;

int ft1000_read_register(struct ft1000_usb *ft1000dev,
			 u16 *Data, u16 nRegIndx);
int ft1000_write_register(struct ft1000_usb *ft1000dev,
			  u16 value, u16 nRegIndx);
int ft1000_read_dpram32(struct ft1000_usb *ft1000dev,
			u16 indx, u8 *buffer, u16 cnt);
int ft1000_write_dpram32(struct ft1000_usb *ft1000dev,
			 u16 indx, u8 *buffer, u16 cnt);
int ft1000_read_dpram16(struct ft1000_usb *ft1000dev,
			u16 indx, u8 *buffer, u8 highlow);
int ft1000_write_dpram16(struct ft1000_usb *ft1000dev,
			 u16 indx, u16 value, u8 highlow);
int fix_ft1000_read_dpram32(struct ft1000_usb *ft1000dev,
			    u16 indx, u8 *buffer);
int fix_ft1000_write_dpram32(struct ft1000_usb *ft1000dev,
			     u16 indx, u8 *buffer);

extern void *pFileStart;
extern size_t FileLength;
extern int numofmsgbuf;

int ft1000_close(struct net_device *dev);
int scram_dnldr(struct ft1000_usb *ft1000dev, void *pFileStart,
		u32  FileLength);

extern struct list_head freercvpool;

/* lock to arbitrate free buffer list for receive command data */
extern spinlock_t free_buff_lock;

int ft1000_create_dev(struct ft1000_usb *dev);
void ft1000_destroy_dev(struct net_device *dev);
extern int card_send_command(struct ft1000_usb *ft1000dev,
			      void *ptempbuffer, int size);

struct dpram_blk *ft1000_get_buffer(struct list_head *bufflist);
void ft1000_free_buffer(struct dpram_blk *pdpram_blk, struct list_head *plist);

int dsp_reload(struct ft1000_usb *ft1000dev);
int init_ft1000_netdev(struct ft1000_usb *ft1000dev);
struct usb_interface;
int reg_ft1000_netdev(struct ft1000_usb *ft1000dev,
		      struct usb_interface *intf);
int ft1000_poll(void *dev_id);

int ft1000_init_proc(struct net_device *dev);
void ft1000_cleanup_proc(struct ft1000_info *info);

#endif  /* _FT1000_USB_H_ */
