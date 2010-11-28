/***************************************************************************
                          dpti.h  -  description
                             -------------------
    begin                : Thu Sep 7 2000
    copyright            : (C) 2001 by Adaptec

    See Documentation/scsi/dpti.txt for history, notes, license info
    and credits
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _DPT_H
#define _DPT_H

#define MAX_TO_IOP_MESSAGES   (255)
#define MAX_FROM_IOP_MESSAGES (255)


/*
 * SCSI interface function Prototypes
 */

static int adpt_detect(struct scsi_host_template * sht);
static int adpt_queue(struct Scsi_Host *h, struct scsi_cmnd * cmd);
static int adpt_abort(struct scsi_cmnd * cmd);
static int adpt_reset(struct scsi_cmnd* cmd);
static int adpt_release(struct Scsi_Host *host);
static int adpt_slave_configure(struct scsi_device *);

static const char *adpt_info(struct Scsi_Host *pSHost);
static int adpt_bios_param(struct scsi_device * sdev, struct block_device *dev,
		sector_t, int geom[]);

static int adpt_bus_reset(struct scsi_cmnd* cmd);
static int adpt_device_reset(struct scsi_cmnd* cmd);


/*
 * struct scsi_host_template (see scsi/scsi_host.h)
 */

#define DPT_DRIVER_NAME	"Adaptec I2O RAID"

#ifndef HOSTS_C

#include "dpt/sys_info.h"
#include <linux/wait.h>
#include "dpt/dpti_i2o.h"
#include "dpt/dpti_ioctl.h"

#define DPT_I2O_VERSION "2.4 Build 5go"
#define DPT_VERSION     2
#define DPT_REVISION    '4'
#define DPT_SUBREVISION '5'
#define DPT_BETA	""
#define DPT_MONTH      8 
#define DPT_DAY        7
#define DPT_YEAR        (2001-1980)

#define DPT_DRIVER	"dpt_i2o"
#define DPTI_I2O_MAJOR	(151)
#define DPT_ORGANIZATION_ID (0x1B)        /* For Private Messages */
#define DPTI_MAX_HBA	(16)
#define MAX_CHANNEL     (5)	// Maximum Channel # Supported
#define MAX_ID        	(128)	// Maximum Target ID Supported

/* Sizes in 4 byte words */
#define REPLY_FRAME_SIZE  (17)
#define MAX_MESSAGE_SIZE  (128)
#define SG_LIST_ELEMENTS  (56)

#define EMPTY_QUEUE           0xffffffff
#define I2O_INTERRUPT_PENDING_B   (0x08)

#define PCI_DPT_VENDOR_ID         (0x1044)	// DPT PCI Vendor ID
#define PCI_DPT_DEVICE_ID         (0xA501)	// DPT PCI I2O Device ID
#define PCI_DPT_RAPTOR_DEVICE_ID  (0xA511)	

/* Debugging macro from Linux Device Drivers - Rubini */
#undef PDEBUG
#ifdef DEBUG
//TODO add debug level switch
#  define PDEBUG(fmt, args...)  printk(KERN_DEBUG "dpti: " fmt, ##args)
#  define PDEBUGV(fmt, args...) printk(KERN_DEBUG "dpti: " fmt, ##args)
#else
# define PDEBUG(fmt, args...) /* not debugging: nothing */
# define PDEBUGV(fmt, args...) /* not debugging: nothing */
#endif

#define PERROR(fmt, args...) printk(KERN_ERR fmt, ##args)
#define PWARN(fmt, args...) printk(KERN_WARNING fmt, ##args)
#define PINFO(fmt, args...) printk(KERN_INFO fmt, ##args)
#define PCRIT(fmt, args...) printk(KERN_CRIT fmt, ##args)

#define SHUTDOWN_SIGS	(sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM))

// Command timeouts
#define FOREVER			(0)
#define TMOUT_INQUIRY 		(20)
#define TMOUT_FLUSH		(360/45)
#define TMOUT_ABORT		(30)
#define TMOUT_SCSI		(300)
#define TMOUT_IOPRESET		(360)
#define TMOUT_GETSTATUS		(15)
#define TMOUT_INITOUTBOUND	(15)
#define TMOUT_LCT		(360)


#define I2O_SCSI_DEVICE_DSC_MASK                0x00FF

#define I2O_DETAIL_STATUS_UNSUPPORTED_FUNCTION  0x000A

#define I2O_SCSI_DSC_MASK                   0xFF00
#define I2O_SCSI_DSC_SUCCESS                0x0000
#define I2O_SCSI_DSC_REQUEST_ABORTED        0x0200
#define I2O_SCSI_DSC_UNABLE_TO_ABORT        0x0300
#define I2O_SCSI_DSC_COMPLETE_WITH_ERROR    0x0400
#define I2O_SCSI_DSC_ADAPTER_BUSY           0x0500
#define I2O_SCSI_DSC_REQUEST_INVALID        0x0600
#define I2O_SCSI_DSC_PATH_INVALID           0x0700
#define I2O_SCSI_DSC_DEVICE_NOT_PRESENT     0x0800
#define I2O_SCSI_DSC_UNABLE_TO_TERMINATE    0x0900
#define I2O_SCSI_DSC_SELECTION_TIMEOUT      0x0A00
#define I2O_SCSI_DSC_COMMAND_TIMEOUT        0x0B00
#define I2O_SCSI_DSC_MR_MESSAGE_RECEIVED    0x0D00
#define I2O_SCSI_DSC_SCSI_BUS_RESET         0x0E00
#define I2O_SCSI_DSC_PARITY_ERROR_FAILURE   0x0F00
#define I2O_SCSI_DSC_AUTOSENSE_FAILED       0x1000
#define I2O_SCSI_DSC_NO_ADAPTER             0x1100
#define I2O_SCSI_DSC_DATA_OVERRUN           0x1200
#define I2O_SCSI_DSC_UNEXPECTED_BUS_FREE    0x1300
#define I2O_SCSI_DSC_SEQUENCE_FAILURE       0x1400
#define I2O_SCSI_DSC_REQUEST_LENGTH_ERROR   0x1500
#define I2O_SCSI_DSC_PROVIDE_FAILURE        0x1600
#define I2O_SCSI_DSC_BDR_MESSAGE_SENT       0x1700
#define I2O_SCSI_DSC_REQUEST_TERMINATED     0x1800
#define I2O_SCSI_DSC_IDE_MESSAGE_SENT       0x3300
#define I2O_SCSI_DSC_RESOURCE_UNAVAILABLE   0x3400
#define I2O_SCSI_DSC_UNACKNOWLEDGED_EVENT   0x3500
#define I2O_SCSI_DSC_MESSAGE_RECEIVED       0x3600
#define I2O_SCSI_DSC_INVALID_CDB            0x3700
#define I2O_SCSI_DSC_LUN_INVALID            0x3800
#define I2O_SCSI_DSC_SCSI_TID_INVALID       0x3900
#define I2O_SCSI_DSC_FUNCTION_UNAVAILABLE   0x3A00
#define I2O_SCSI_DSC_NO_NEXUS               0x3B00
#define I2O_SCSI_DSC_SCSI_IID_INVALID       0x3C00
#define I2O_SCSI_DSC_CDB_RECEIVED           0x3D00
#define I2O_SCSI_DSC_LUN_ALREADY_ENABLED    0x3E00
#define I2O_SCSI_DSC_BUS_BUSY               0x3F00
#define I2O_SCSI_DSC_QUEUE_FROZEN           0x4000


#ifndef TRUE
#define TRUE                  1
#define FALSE                 0
#endif

#define HBA_FLAGS_INSTALLED_B       0x00000001	// Adapter Was Installed
#define HBA_FLAGS_BLINKLED_B        0x00000002	// Adapter In Blink LED State
#define HBA_FLAGS_IN_RESET	0x00000040	/* in reset */
#define HBA_HOSTRESET_FAILED	0x00000080	/* adpt_resethost failed */


// Device state flags
#define DPTI_DEV_ONLINE    0x00
#define DPTI_DEV_UNSCANNED 0x01
#define DPTI_DEV_RESET	   0x02
#define DPTI_DEV_OFFLINE   0x04


struct adpt_device {
	struct adpt_device* next_lun;
	u32	flags;
	u32	type;
	u32	capacity;
	u32	block_size;
	u8	scsi_channel;
	u8	scsi_id;
	u8 	scsi_lun;
	u8	state;
	u16	tid;
	struct i2o_device* pI2o_dev;
	struct scsi_device *pScsi_dev;
};

struct adpt_channel {
	struct adpt_device* device[MAX_ID];	/* used as an array of 128 scsi ids */
	u8	scsi_id;
	u8	type;
	u16	tid;
	u32	state;
	struct i2o_device* pI2o_dev;
};

// HBA state flags
#define DPTI_STATE_RESET	(0x01)
#define DPTI_STATE_IOCTL	(0x02)

typedef struct _adpt_hba {
	struct _adpt_hba *next;
	struct pci_dev *pDev;
	struct Scsi_Host *host;
	u32 state;
	spinlock_t state_lock;
	int unit;
	int host_no;		/* SCSI host number */
	u8 initialized;
	u8 in_use;		/* is the management node open*/

	char name[32];
	char detail[55];

	void __iomem *base_addr_virt;
	void __iomem *msg_addr_virt;
	ulong base_addr_phys;
	void __iomem *post_port;
	void __iomem *reply_port;
	void __iomem *irq_mask;
	u16  post_count;
	u32  post_fifo_size;
	u32  reply_fifo_size;
	u32* reply_pool;
	dma_addr_t reply_pool_pa;
	u32  sg_tablesize;	// Scatter/Gather List Size.       
	u8  top_scsi_channel;
	u8  top_scsi_id;
	u8  top_scsi_lun;
	u8  dma64;

	i2o_status_block* status_block;
	dma_addr_t status_block_pa;
	i2o_hrt* hrt;
	dma_addr_t hrt_pa;
	i2o_lct* lct;
	dma_addr_t lct_pa;
	uint lct_size;
	struct i2o_device* devices;
	struct adpt_channel channel[MAX_CHANNEL];
	struct proc_dir_entry* proc_entry;	/* /proc dir */

	void __iomem *FwDebugBuffer_P;	// Virtual Address Of FW Debug Buffer
	u32   FwDebugBufferSize;	// FW Debug Buffer Size In Bytes
	void __iomem *FwDebugStrLength_P;// Virtual Addr Of FW Debug String Len
	void __iomem *FwDebugFlags_P;	// Virtual Address Of FW Debug Flags 
	void __iomem *FwDebugBLEDflag_P;// Virtual Addr Of FW Debug BLED
	void __iomem *FwDebugBLEDvalue_P;// Virtual Addr Of FW Debug BLED
	u32 FwDebugFlags;
	u32 *ioctl_reply_context[4];
} adpt_hba;

struct sg_simple_element {
   u32  flag_count;
   u32 addr_bus;
}; 

/*
 * Function Prototypes
 */

static void adpt_i2o_sys_shutdown(void);
static int adpt_init(void);
static int adpt_i2o_build_sys_table(void);
static irqreturn_t adpt_isr(int irq, void *dev_id);

static void adpt_i2o_report_hba_unit(adpt_hba* pHba, struct i2o_device *d);
static int adpt_i2o_query_scalar(adpt_hba* pHba, int tid, 
			int group, int field, void *buf, int buflen);
#ifdef DEBUG
static const char *adpt_i2o_get_class_name(int class);
#endif
static int adpt_i2o_issue_params(int cmd, adpt_hba* pHba, int tid, 
		  void *opblk, dma_addr_t opblk_pa, int oplen,
		  void *resblk, dma_addr_t resblk_pa, int reslen);
static int adpt_i2o_post_wait(adpt_hba* pHba, u32* msg, int len, int timeout);
static int adpt_i2o_lct_get(adpt_hba* pHba);
static int adpt_i2o_parse_lct(adpt_hba* pHba);
static int adpt_i2o_activate_hba(adpt_hba* pHba);
static int adpt_i2o_enable_hba(adpt_hba* pHba);
static int adpt_i2o_install_device(adpt_hba* pHba, struct i2o_device *d);
static s32 adpt_i2o_post_this(adpt_hba* pHba, u32* data, int len);
static s32 adpt_i2o_quiesce_hba(adpt_hba* pHba);
static s32 adpt_i2o_status_get(adpt_hba* pHba);
static s32 adpt_i2o_init_outbound_q(adpt_hba* pHba);
static s32 adpt_i2o_hrt_get(adpt_hba* pHba);
static s32 adpt_scsi_to_i2o(adpt_hba* pHba, struct scsi_cmnd* cmd, struct adpt_device* dptdevice);
static s32 adpt_i2o_to_scsi(void __iomem *reply, struct scsi_cmnd* cmd);
static s32 adpt_scsi_host_alloc(adpt_hba* pHba,struct scsi_host_template * sht);
static s32 adpt_hba_reset(adpt_hba* pHba);
static s32 adpt_i2o_reset_hba(adpt_hba* pHba);
static s32 adpt_rescan(adpt_hba* pHba);
static s32 adpt_i2o_reparse_lct(adpt_hba* pHba);
static s32 adpt_send_nop(adpt_hba*pHba,u32 m);
static void adpt_i2o_delete_hba(adpt_hba* pHba);
static void adpt_inquiry(adpt_hba* pHba);
static void adpt_fail_posted_scbs(adpt_hba* pHba);
static struct adpt_device* adpt_find_device(adpt_hba* pHba, u32 chan, u32 id, u32 lun);
static int adpt_install_hba(struct scsi_host_template* sht, struct pci_dev* pDev) ;
static int adpt_i2o_online_hba(adpt_hba* pHba);
static void adpt_i2o_post_wait_complete(u32, int);
static int adpt_i2o_systab_send(adpt_hba* pHba);

static int adpt_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg);
static int adpt_open(struct inode *inode, struct file *file);
static int adpt_close(struct inode *inode, struct file *file);


#ifdef UARTDELAY
static void adpt_delay(int millisec);
#endif

#define PRINT_BUFFER_SIZE     512

#define HBA_FLAGS_DBG_FLAGS_MASK         0xffff0000	// Mask for debug flags
#define HBA_FLAGS_DBG_KERNEL_PRINT_B     0x00010000	// Kernel Debugger Print
#define HBA_FLAGS_DBG_FW_PRINT_B         0x00020000	// Firmware Debugger Print
#define HBA_FLAGS_DBG_FUNCTION_ENTRY_B   0x00040000	// Function Entry Point
#define HBA_FLAGS_DBG_FUNCTION_EXIT_B    0x00080000	// Function Exit
#define HBA_FLAGS_DBG_ERROR_B            0x00100000	// Error Conditions
#define HBA_FLAGS_DBG_INIT_B             0x00200000	// Init Prints
#define HBA_FLAGS_DBG_OS_COMMANDS_B      0x00400000	// OS Command Info
#define HBA_FLAGS_DBG_SCAN_B             0x00800000	// Device Scan

#define FW_DEBUG_STR_LENGTH_OFFSET 0
#define FW_DEBUG_FLAGS_OFFSET      4
#define FW_DEBUG_BLED_OFFSET       8

#define FW_DEBUG_FLAGS_NO_HEADERS_B    0x01
#endif				/* !HOSTS_C */
#endif				/* _DPT_H */
