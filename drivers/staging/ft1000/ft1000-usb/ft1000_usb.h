#ifndef _FT1000_USB_H_
#define _FT1000_USB_H_

/*Jim*/
#include "ft1000_ioctl.h"
#define FT1000_DRV_VER      0x01010403

#define  MODESZ              2
#define  MAX_NUM_APP         6
#define  MAX_MSG_LIMIT       200
#define  NUM_OF_FREE_BUFFERS 1500

// Driver message types
#define MEDIA_STATE        0x0010
#define DSP_PROVISION      0x0030
#define DSP_INIT_MSG       0x0050
#define DSP_STORE_INFO       0x0070
#define DSP_GET_INFO         0x0071
#define GET_DRV_ERR_RPT_MSG  0x0073
#define RSP_DRV_ERR_RPT_MSG  0x0074


// Size of DPRAM Command
#define MAX_CMD_SQSIZE          1780
#define SLOWQ_TYPE              0
#define PSEUDOSZ                16
#define DSP_QID_OFFSET          4


// MEMORY MAP FOR ELECTRABUZZ ASIC
#define	FT1000_REG_DFIFO_STAT	0x0008	// Downlink FIFO status register
#define	FT1000_REG_DPRAM_DATA	0x000C	// DPRAM VALUE in DPRAM ADDR

#define FT1000_DSP_LED          0xFFA  // dsp led status for PAD device

#define FT1000_MAG_DSP_LED               0x3FE   // dsp led status for PAD device
#define FT1000_MAG_DSP_LED_INDX          0x1     // dsp led status for PAD device

#define  SUCCESS             0x00


#define DRIVERID                0x00

// Driver Error Messages for DSP
#define DSP_CONDRESET_INFO   0x7ef2
#define DSP_HB_INFO          0x7ef0

// Magnemite specific defines
#define hi_mag                  0x6968  // Byte swap hi to avoid additional system call
#define ho_mag                  0x6f68  // Byte swap ho to avoid additional system call



struct media_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u16 state;
	u32 ip_addr;
        u32 net_mask;
	u32 gateway;
	u32 dns_1;
	u32 dns_2;
} __attribute__ ((packed));

struct dsp_init_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u8 DspVer[DSPVERSZ];        // DSP version number
	u8 HwSerNum[HWSERNUMSZ];    // Hardware Serial Number
	u8 Sku[SKUSZ];              // SKU
	u8 eui64[EUISZ];            // EUI64
	u8 ProductMode[MODESZ];     // Product Mode (Market/Production)
	u8 RfCalVer[CALVERSZ];      // Rf Calibration version
	u8 RfCalDate[CALDATESZ];    // Rf Calibration date
} __attribute__ ((packed));


struct app_info_block {
	u32 nTxMsg;                    // DPRAM msg sent to DSP with app_id
	u32 nRxMsg;                    // DPRAM msg rcv from dsp with app_id
	u32 nTxMsgReject;              // DPRAM msg rejected due to DSP doorbell set
	u32 nRxMsgMiss;                // DPRAM msg dropped due to overflow
	struct fown_struct *fileobject;// Application's file object
	u16 app_id;                    // Application id
	int DspBCMsgFlag;
	int NumOfMsg;                   // number of messages queued up
	wait_queue_head_t wait_dpram_msg;
	struct list_head app_sqlist;   // link list of msgs for applicaton on slow queue
} __attribute__((packed));

struct prov_record {
	struct list_head list;
	u8 *pprov_data;
};

/*end of Jim*/
#define DEBUG(args...) printk(KERN_INFO args)

#define FALSE           0
#define TRUE            1

#define STATUS_SUCCESS  0
#define STATUS_FAILURE   0x1001

#define FT1000_STATUS_CLOSING  0x01

#define LARGE_TIMEOUT   5000

#define MAX_DSP_SESS_REC        1024

#define MAX_NUM_CARDS        32

#define DSPVERSZ                4
#define HWSERNUMSZ              16
#define SKUSZ                   20
#define EUISZ                   8
#define CALVERSZ                2
#define CALDATESZ               6
#define MODESZ                  2

#define DSPID                   0x20
#define HOSTID                  0x10

#define DSPOAM                  0x80
#define DSPAIRID                0x90

#define DRIVERID                0x00
#define FMM                     0x10
#define NETWORKID               0x20
#define AUTOLNCHID              0x30
#define DSPLPBKID               0x40

#define DSPBCMSGID              0x10

#define ENET_MAX_SIZE           1514
#define ENET_HEADER_SIZE        14


#define CIS_NET_ADDR_OFFSET 0xff0

// MAGNEMITE specific

#define FT1000_REG_MAG_UFDR 		0x0000	// Uplink FIFO Data Register.

#define FT1000_REG_MAG_UFDRL		0x0000	// Uplink FIFO Data Register low-word.

#define FT1000_REG_MAG_UFDRH		0x0002  // Uplink FIFO Data Register high-word.

#define FT1000_REG_MAG_UFER			0x0004  // Uplink FIFO End Register

#define FT1000_REG_MAG_UFSR			0x0006  // Uplink FIFO Status Register

#define FT1000_REG_MAG_DFR			0x0008	// Downlink FIFO Register

#define FT1000_REG_MAG_DFRL			0x0008	// Downlink FIFO Register low-word

#define FT1000_REG_MAG_DFRH			0x000a  // Downlink FIFO Register high-word

#define FT1000_REG_MAG_DFSR			0x000c  // Downlink FIFO Status Register

#define FT1000_REG_MAG_DPDATA		0x0010  // Dual Port RAM Indirect Data Register

#define FT1000_REG_MAG_DPDATAL		0x0010  // Dual Port RAM Indirect Data Register low-word

#define FT1000_REG_MAG_DPDATAH		0x0012  // Dual Port RAM Indirect Data Register high-word

#define	FT1000_REG_MAG_WATERMARK	0x002c	// Supv. Control Reg.			LLC register

#define FT1000_REG_MAG_VERSION		0x0030	// LLC Version					LLC register



// Common

#define	FT1000_REG_DPRAM_ADDR	0x000E	// DPRAM ADDRESS when card in IO mode

#define	FT1000_REG_SUP_CTRL		0x0020	// Supv. Control Reg.			LLC register

#define	FT1000_REG_SUP_STAT		0x0022	// Supv. Status Reg				LLC register

#define	FT1000_REG_RESET		0x0024	// Reset Reg					LLC register

#define	FT1000_REG_SUP_ISR		0x0026	// Supv ISR						LLC register

#define	FT1000_REG_SUP_IMASK	0x0028	// Supervisor Interrupt Mask	LLC register

#define	FT1000_REG_DOORBELL		0x002a	// Door Bell Reg				LLC register

#define FT1000_REG_ASIC_ID      0x002e  // ASIC Identification Number

										// (Electrabuzz=0 Magnemite=TBD)



// DSP doorbells

#define FT1000_DB_DPRAM_RX		0x0001	// this value indicates that DSP has

                                        //      data for host in DPRAM SlowQ

#define FT1000_DB_DNLD_RX       0x0002  // Downloader handshake doorbell

#define FT1000_ASIC_RESET_REQ   0x0004

#define FT1000_DSP_ASIC_RESET   0x0008



#define FT1000_DB_COND_RESET    0x0010



// Host doorbells

#define FT1000_DB_DPRAM_TX		0x0100	// this value indicates that host has

                                        //      data for DSP in DPRAM.

#define FT1000_DB_DNLD_TX       0x0200  // Downloader handshake doorbell

#define FT1000_ASIC_RESET_DSP   0x0400

#define FT1000_DB_HB            0x1000  // this value indicates that supervisor



// Electrabuzz specific DPRAM mapping                                        //      has a heartbeat message for DSP.

#define FT1000_DPRAM_BASE		0x1000	//  0x0000 to 0x07FF	DPRAM	2Kx16 - R/W from PCMCIA or DSP

#define FT1000_DPRAM_TX_BASE	0x1002	//  TX AREA (SlowQ)

#define FT1000_DPRAM_RX_BASE	0x1800	//  RX AREA (SlowQ)

#define FT1000_DPRAM_SIZE       0x1000  //  4K bytes



#define FT1000_DRV_DEBUG        0x17E0  // Debug area for driver

#define FT1000_FIFO_LEN         0x17FC  // total length for DSP FIFO tracking

#define FT1000_HI_HO            0x17FE  // heartbeat with HI/HO

#define FT1000_DSP_STATUS       0x1FFE  // dsp status - non-zero is a request to reset dsp



#define FT1000_DSP_CON_STATE    0x1FF8  // DSP Connection Status Info

#define FT1000_DSP_LEDS         0x1FFA  // DSP LEDS for rcv pwr strength, Rx data, Tx data

#define DSP_TIMESTAMP           0x1FFC  // dsp timestamp

#define DSP_TIMESTAMP_DIFF      0x1FFA  // difference of dsp timestamp in DPRAM and Pseudo header.



#define FT1000_DPRAM_FEFE    	0x1002	// Dsp Downloader handshake location



#define FT1000_DSP_TIMER0       0x1FF0

#define FT1000_DSP_TIMER1       0x1FF2

#define FT1000_DSP_TIMER2       0x1FF4

#define FT1000_DSP_TIMER3       0x1FF6



// MEMORY MAP FOR MAGNEMITE

#define FT1000_DPRAM_MAG_TX_BASE	 	 0x0000	 //  TX AREA (SlowQ)

#define FT1000_DPRAM_MAG_RX_BASE		 0x0200	 //  RX AREA (SlowQ)



#define FT1000_MAG_FIFO_LEN              0x1FF   // total length for DSP FIFO tracking

#define FT1000_MAG_FIFO_LEN_INDX         0x1     // low-word index

#define FT1000_MAG_HI_HO                 0x1FF   // heartbeat with HI/HO

#define FT1000_MAG_HI_HO_INDX            0x0     // high-word index

#define FT1000_MAG_DSP_LEDS              0x3FE   // dsp led status for PAD device

#define FT1000_MAG_DSP_LEDS_INDX         0x1     // dsp led status for PAD device



#define FT1000_MAG_DSP_CON_STATE         0x3FE   // DSP Connection Status Info

#define FT1000_MAG_DSP_CON_STATE_INDX    0x0     // DSP Connection Status Info



#define FT1000_MAG_DPRAM_FEFE            0x000   // location for dsp ready indicator

#define FT1000_MAG_DPRAM_FEFE_INDX       0x0     // location for dsp ready indicator



#define FT1000_MAG_DSP_TIMER0            0x3FC

#define FT1000_MAG_DSP_TIMER0_INDX       0x1



#define FT1000_MAG_DSP_TIMER1            0x3FC

#define FT1000_MAG_DSP_TIMER1_INDX       0x0



#define FT1000_MAG_DSP_TIMER2            0x3FD

#define FT1000_MAG_DSP_TIMER2_INDX       0x1



#define FT1000_MAG_DSP_TIMER3            0x3FD

#define FT1000_MAG_DSP_TIMER3_INDX       0x0



#define FT1000_MAG_TOTAL_LEN             0x200

#define FT1000_MAG_TOTAL_LEN_INDX        0x1



#define FT1000_MAG_PH_LEN                0x200

#define FT1000_MAG_PH_LEN_INDX           0x0



#define FT1000_MAG_PORT_ID               0x201

#define FT1000_MAG_PORT_ID_INDX          0x0



//

// Constants for the FT1000_REG_SUP_ISR

//

// Indicate the cause of an interrupt.

//

// SUPERVISOR ISR BIT MAPS



#define ISR_EMPTY			(u8)0x00 	 // no bits set in ISR

#define ISR_DOORBELL_ACK	(u8)0x01		 //  the doorbell i sent has been recieved.

#define ISR_DOORBELL_PEND	(u8)0x02 	 //  doorbell for me

#define ISR_RCV				(u8)0x04 	 // packet received with no errors

#define ISR_WATERMARK		(u8)0x08 	 //



// Interrupt mask register defines

// note these are different from the ISR BIT MAPS.

#define ISR_MASK_NONE			0x0000

#define ISR_MASK_DOORBELL_ACK	0x0001

#define ISR_MASK_DOORBELL_PEND	0x0002

#define ISR_MASK_RCV			0x0004

#define ISR_MASK_WATERMARK		0x0008 	  // Normally we will only mask the watermark interrupt when we want to enable interrupts.

#define ISR_MASK_ALL			0xffff



#define HOST_INTF_LE            0x0000    // Host interface little endian

#define HOST_INTF_BE            0x0001    // Host interface big endian



#define ISR_DEFAULT_MASK		0x7ff9



#define hi                      0x6869

#define ho                      0x686f



#define FT1000_ASIC_RESET       0x80     // COR value for soft reset to PCMCIA core

#define FT1000_ASIC_BITS        0x51     // Bits set in COR register under normal operation

#define FT1000_ASIC_MAG_BITS    0x55     // Bits set in COR register under normal operation



#define FT1000_COR_OFFSET       0x100



#define ELECTRABUZZ_ID			0        // ASIC ID for ELECTRABUZZ

#define MAGNEMITE_ID			0x1a01   // ASIC ID for MAGNEMITE



// Maximum times trying to get ASIC out of reset

#define MAX_ASIC_RESET_CNT      20



#define DSP_RESET_BIT           0x1

#define ASIC_RESET_BIT          0x2

#define DSP_UNENCRYPTED         0x4

#define DSP_ENCRYPTED           0x8

#define EFUSE_MEM_DISABLE       0x0040


#define MAX_BUF_SIZE            4096

struct drv_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u8  data[0];
} __attribute__ ((packed));

struct ft1000_device
{
	struct usb_device *dev;
	struct net_device *net;

	u32 status;

	struct urb *rx_urb;
	struct urb *tx_urb;

	u8 tx_buf[MAX_BUF_SIZE];
	u8 rx_buf[MAX_BUF_SIZE];

	u8 bulk_in_endpointAddr;
	u8 bulk_out_endpointAddr;

	//struct ft1000_ethernet_configuration configuration;

//	struct net_device_stats stats; //mbelian
} __attribute__ ((packed));

struct ft1000_debug_dirs {
	struct list_head list;
	struct dentry *dent;
	struct dentry *file;
	int int_number;
};

struct ft1000_info {
    struct ft1000_device *pFt1000Dev;
    struct net_device_stats stats;

    struct task_struct *pPollThread;

    unsigned char fcodeldr;
    unsigned char bootmode;
	unsigned char usbboot;
    unsigned short dspalive;
    u16 ASIC_ID;
    bool fProvComplete;
    bool fCondResetPend;
    bool fAppMsgPend;
    char *pfwimg;
    int fwimgsz;
    u16 DrvErrNum;
    u8  *pTestImage;
    u16 AsicID;
    unsigned long TestImageIndx;
    unsigned long TestImageSz;
    u8  TestImageEnable;
    u8  TestImageReady;
    int ASICResetNum;
    int DspAsicReset;
    int PktIntfErr;
    int DSPResetNum;
    int NumIOCTLBufs;
    int IOCTLBufLvl;
    int DeviceCreated;
    int CardReady;
    int NetDevRegDone;
    u8 CardNumber;
    u8 DeviceName[15];
    struct ft1000_debug_dirs nodes;
    int registered;
    int mediastate;
    int dhcpflg;
    u16 packetseqnum;
    u8 squeseqnum;                 // sequence number on slow queue
    spinlock_t dpram_lock;
    spinlock_t fifo_lock;
    u16 CurrentInterruptEnableMask;
    int InterruptsEnabled;
    u16 fifo_cnt;
    u8 DspVer[DSPVERSZ];        // DSP version number
    u8 HwSerNum[HWSERNUMSZ];    // Hardware Serial Number
    u8 Sku[SKUSZ];              // SKU
    u8 eui64[EUISZ];            // EUI64
    time_t ConTm;               // Connection Time
    u8 ProductMode[MODESZ];
    u8 RfCalVer[CALVERSZ];
    u8 RfCalDate[CALDATESZ];
    u16 DSP_TIME[4];
    u16 ProgSnr;
    u16 LedStat;	//mbelian
    u16 ConStat;	//mbelian
    u16 ProgConStat;
    struct list_head prov_list;
    int appcnt;
	struct app_info_block app_info[MAX_NUM_APP];
    u16 DSPInfoBlklen;
    u16 DrvMsgPend;
    int (*ft1000_reset)(struct net_device *dev);
    u16 DSPInfoBlk[MAX_DSP_SESS_REC];
    union {
        u16 Rec[MAX_DSP_SESS_REC];
        u32 MagRec[MAX_DSP_SESS_REC/2];
    } DSPSess;
	unsigned short tempbuf[32];
	char netdevname[IFNAMSIZ];
	struct proc_dir_entry *ft1000_proc_dir; //mbelian
};


struct dpram_blk {
    struct list_head list;
    u16 *pbuffer;
} __attribute__ ((packed));

u16 ft1000_read_register(struct ft1000_device *ft1000dev, u16* Data, u16 nRegIndx);
u16 ft1000_write_register(struct ft1000_device *ft1000dev, u16 value, u16 nRegIndx);
u16 ft1000_read_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer, u16 cnt);
u16 ft1000_write_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer, u16 cnt);
u16 ft1000_read_dpram16(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer, u8 highlow);
u16 ft1000_write_dpram16(struct ft1000_device *ft1000dev, u16 indx, u16 value, u8 highlow);
u16 fix_ft1000_read_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer);
u16 fix_ft1000_write_dpram32(struct ft1000_device *ft1000dev, u16 indx, u8 *buffer);

extern void *pFileStart;
extern size_t FileLength;
extern int numofmsgbuf;

int ft1000_close (struct net_device *dev);
u16 scram_dnldr(struct ft1000_device *ft1000dev, void *pFileStart, u32  FileLength);

extern struct list_head freercvpool;
extern spinlock_t free_buff_lock;   // lock to arbitrate free buffer list for receive command data

int ft1000_create_dev(struct ft1000_device *dev);
void ft1000_destroy_dev(struct net_device *dev);
extern void CardSendCommand(struct ft1000_device *ft1000dev, void *ptempbuffer, int size);

struct dpram_blk *ft1000_get_buffer(struct list_head *bufflist);
void ft1000_free_buffer(struct dpram_blk *pdpram_blk, struct list_head *plist);

char *getfw (char *fn, size_t *pimgsz);

int dsp_reload(struct ft1000_device *ft1000dev);
u16 init_ft1000_netdev(struct ft1000_device *ft1000dev);
struct usb_interface;
int reg_ft1000_netdev(struct ft1000_device *ft1000dev, struct usb_interface *intf);
int ft1000_poll(void* dev_id);

int ft1000_init_proc(struct net_device *dev);
void ft1000_cleanup_proc(struct ft1000_info *info);



#endif
