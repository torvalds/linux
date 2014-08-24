/*
 *  linux/drivers/message/fusion/mptbase.h
 *      High performance SCSI + LAN / Fibre Channel device drivers.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
 *
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MPTBASE_H_INCLUDED
#define MPTBASE_H_INCLUDED
/*{-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/mutex.h>

#include "lsi/mpi_type.h"
#include "lsi/mpi.h"		/* Fusion MPI(nterface) basic defs */
#include "lsi/mpi_ioc.h"	/* Fusion MPT IOC(ontroller) defs */
#include "lsi/mpi_cnfg.h"	/* IOC configuration support */
#include "lsi/mpi_init.h"	/* SCSI Host (initiator) protocol support */
#include "lsi/mpi_lan.h"	/* LAN over FC protocol support */
#include "lsi/mpi_raid.h"	/* Integrated Mirroring support */

#include "lsi/mpi_fc.h"		/* Fibre Channel (lowlevel) support */
#include "lsi/mpi_targ.h"	/* SCSI/FCP Target protcol support */
#include "lsi/mpi_tool.h"	/* Tools support */
#include "lsi/mpi_sas.h"	/* SAS support */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#ifndef MODULEAUTHOR
#define MODULEAUTHOR	"LSI Corporation"
#endif

#ifndef COPYRIGHT
#define COPYRIGHT	"Copyright (c) 1999-2008 " MODULEAUTHOR
#endif

#define MPT_LINUX_VERSION_COMMON	"3.04.20"
#define MPT_LINUX_PACKAGE_NAME		"@(#)mptlinux-3.04.20"
#define WHAT_MAGIC_STRING		"@" "(" "#" ")"

#define show_mptmod_ver(s,ver)  \
	printk(KERN_INFO "%s %s\n", s, ver);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Fusion MPT(linux) driver configurable stuff...
 */
#define MPT_MAX_ADAPTERS		18
#define MPT_MAX_PROTOCOL_DRIVERS	16
#define MPT_MAX_CALLBACKNAME_LEN	49
#define MPT_MAX_BUS			1	/* Do not change */
#define MPT_MAX_FC_DEVICES		255
#define MPT_MAX_SCSI_DEVICES		16
#define MPT_LAST_LUN			255
#define MPT_SENSE_BUFFER_ALLOC		64
	/* allow for 256 max sense alloc, but only 255 max request */
#if MPT_SENSE_BUFFER_ALLOC >= 256
#	undef MPT_SENSE_BUFFER_ALLOC
#	define MPT_SENSE_BUFFER_ALLOC	256
#	define MPT_SENSE_BUFFER_SIZE	255
#else
#	define MPT_SENSE_BUFFER_SIZE	MPT_SENSE_BUFFER_ALLOC
#endif

#define MPT_NAME_LENGTH			32
#define MPT_KOBJ_NAME_LEN		20

#define MPT_PROCFS_MPTBASEDIR		"mpt"
						/* chg it to "driver/fusion" ? */
#define MPT_PROCFS_SUMMARY_ALL_NODE		MPT_PROCFS_MPTBASEDIR "/summary"
#define MPT_PROCFS_SUMMARY_ALL_PATHNAME		"/proc/" MPT_PROCFS_SUMMARY_ALL_NODE
#define MPT_FW_REV_MAGIC_ID_STRING		"FwRev="

#define  MPT_MAX_REQ_DEPTH		1023
#define  MPT_DEFAULT_REQ_DEPTH		256
#define  MPT_MIN_REQ_DEPTH		128

#define  MPT_MAX_REPLY_DEPTH		MPT_MAX_REQ_DEPTH
#define  MPT_DEFAULT_REPLY_DEPTH	128
#define  MPT_MIN_REPLY_DEPTH		8
#define  MPT_MAX_REPLIES_PER_ISR	32

#define  MPT_MAX_FRAME_SIZE		128
#define  MPT_DEFAULT_FRAME_SIZE		128

#define  MPT_REPLY_FRAME_SIZE		0x50  /* Must be a multiple of 8 */

#define  MPT_SG_REQ_128_SCALE		1
#define  MPT_SG_REQ_96_SCALE		2
#define  MPT_SG_REQ_64_SCALE		4

#define	 CAN_SLEEP			1
#define  NO_SLEEP			0

#define MPT_COALESCING_TIMEOUT		0x10


/*
 * SCSI transfer rate defines.
 */
#define MPT_ULTRA320			0x08
#define MPT_ULTRA160			0x09
#define MPT_ULTRA2			0x0A
#define MPT_ULTRA			0x0C
#define MPT_FAST			0x19
#define MPT_SCSI			0x32
#define MPT_ASYNC			0xFF

#define MPT_NARROW			0
#define MPT_WIDE			1

#define C0_1030				0x08
#define XL_929				0x01


/*
 *	Try to keep these at 2^N-1
 */
#define MPT_FC_CAN_QUEUE	1024
#define MPT_SCSI_CAN_QUEUE	127
#define MPT_SAS_CAN_QUEUE	127

/*
 * Set the MAX_SGE value based on user input.
 */
#ifdef CONFIG_FUSION_MAX_SGE
#if CONFIG_FUSION_MAX_SGE  < 16
#define MPT_SCSI_SG_DEPTH	16
#elif CONFIG_FUSION_MAX_SGE  > 128
#define MPT_SCSI_SG_DEPTH	128
#else
#define MPT_SCSI_SG_DEPTH	CONFIG_FUSION_MAX_SGE
#endif
#else
#define MPT_SCSI_SG_DEPTH	40
#endif

#ifdef CONFIG_FUSION_MAX_FC_SGE
#if CONFIG_FUSION_MAX_FC_SGE  < 16
#define MPT_SCSI_FC_SG_DEPTH	16
#elif CONFIG_FUSION_MAX_FC_SGE  > 256
#define MPT_SCSI_FC_SG_DEPTH	256
#else
#define MPT_SCSI_FC_SG_DEPTH	CONFIG_FUSION_MAX_FC_SGE
#endif
#else
#define MPT_SCSI_FC_SG_DEPTH	40
#endif

/* debug print string length used for events and iocstatus */
# define EVENT_DESCR_STR_SZ             100

#define MPT_POLLING_INTERVAL		1000	/* in milliseconds */

#ifdef __KERNEL__	/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <linux/proc_fs.h>

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Attempt semi-consistent error & warning msgs across
 * MPT drivers.  NOTE: Users of these macro defs must
 * themselves define their own MYNAM.
 */
#define MYIOC_s_FMT			MYNAM ": %s: "
#define MYIOC_s_DEBUG_FMT		KERN_DEBUG MYNAM ": %s: "
#define MYIOC_s_INFO_FMT		KERN_INFO MYNAM ": %s: "
#define MYIOC_s_NOTE_FMT		KERN_NOTICE MYNAM ": %s: "
#define MYIOC_s_WARN_FMT		KERN_WARNING MYNAM ": %s: WARNING - "
#define MYIOC_s_ERR_FMT			KERN_ERR MYNAM ": %s: ERROR - "

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  ATTO UL4D associated structures and defines
 */
#define ATTOFLAG_DISC     0x0001
#define ATTOFLAG_TAGGED   0x0002
#define ATTOFLAG_WIDE_ENB 0x0008
#define ATTOFLAG_ID_ENB   0x0010
#define ATTOFLAG_LUN_ENB  0x0060

typedef struct _ATTO_DEVICE_INFO
{
	u8	Offset;					/* 00h */
	u8	Period;					/* 01h */
	u16	ATTOFlags;				/* 02h */
} ATTO_DEVICE_INFO, MPI_POINTER PTR_ATTO_DEVICE_INFO,
  ATTODeviceInfo_t, MPI_POINTER pATTODeviceInfo_t;

typedef struct _ATTO_CONFIG_PAGE_SCSI_PORT_2
{
	CONFIG_PAGE_HEADER	Header;			/* 00h */
	u16			PortFlags;		/* 04h */
	u16			Unused1;		/* 06h */
	u32			Unused2;		/* 08h */
	ATTO_DEVICE_INFO	DeviceSettings[16];	/* 0Ch */
} fATTO_CONFIG_PAGE_SCSI_PORT_2, MPI_POINTER PTR_ATTO_CONFIG_PAGE_SCSI_PORT_2,
  ATTO_SCSIPortPage2_t, MPI_POINTER pATTO_SCSIPortPage2_t;


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  MPT protocol driver defs...
 */
typedef enum {
	MPTBASE_DRIVER,		/* MPT base class */
	MPTCTL_DRIVER,		/* MPT ioctl class */
	MPTSPI_DRIVER,		/* MPT SPI host class */
	MPTFC_DRIVER,		/* MPT FC host class */
	MPTSAS_DRIVER,		/* MPT SAS host class */
	MPTLAN_DRIVER,		/* MPT LAN class */
	MPTSTM_DRIVER,		/* MPT SCSI target mode class */
	MPTUNKNOWN_DRIVER
} MPT_DRIVER_CLASS;

struct mpt_pci_driver{
	int  (*probe) (struct pci_dev *dev, const struct pci_device_id *id);
	void (*remove) (struct pci_dev *dev);
};

/*
 *  MPT adapter / port / bus / device info structures...
 */

typedef union _MPT_FRAME_TRACKER {
	struct {
		struct list_head	list;
		u32			 arg1;
		u32			 pad;
		void			*argp1;
	} linkage;
	/*
	 * NOTE: When request frames are free, on the linkage structure
	 * contets are valid.  All other values are invalid.
	 * In particular, do NOT reply on offset [2]
	 * (in words) being the * message context.
	 * The message context must be reset (computed via base address
	 * + an offset) prior to issuing any command.
	 *
	 * NOTE2: On non-32-bit systems, where pointers are LARGE,
	 * using the linkage pointers destroys our sacred MsgContext
	 * field contents.  But we don't care anymore because these
	 * are now reset in mpt_put_msg_frame() just prior to sending
	 * a request off to the IOC.
	 */
	struct {
		u32 __hdr[2];
		/*
		 * The following _MUST_ match the location of the
		 * MsgContext field in the MPT message headers.
		 */
		union {
			u32		 MsgContext;
			struct {
				u16	 req_idx;	/* Request index */
				u8	 cb_idx;	/* callback function index */
				u8	 rsvd;
			} fld;
		} msgctxu;
	} hwhdr;
	/*
	 * Remark: 32 bit identifier:
	 *  31-24: reserved
	 *  23-16: call back index
	 *  15-0 : request index
	 */
} MPT_FRAME_TRACKER;

/*
 *  We might want to view/access a frame as:
 *    1) generic request header
 *    2) SCSIIORequest
 *    3) SCSIIOReply
 *    4) MPIDefaultReply
 *    5) frame tracker
 */
typedef struct _MPT_FRAME_HDR {
	union {
		MPIHeader_t		hdr;
		SCSIIORequest_t		scsireq;
		SCSIIOReply_t		sreply;
		ConfigReply_t		configreply;
		MPIDefaultReply_t	reply;
		MPT_FRAME_TRACKER	frame;
	} u;
} MPT_FRAME_HDR;

#define MPT_REQ_MSGFLAGS_DROPME		0x80

typedef struct _MPT_SGL_HDR {
	SGESimple32_t	 sge[1];
} MPT_SGL_HDR;

typedef struct _MPT_SGL64_HDR {
	SGESimple64_t	 sge[1];
} MPT_SGL64_HDR;

/*
 *  System interface register set
 */

typedef struct _SYSIF_REGS
{
	u32	Doorbell;	/* 00     System<->IOC Doorbell reg  */
	u32	WriteSequence;	/* 04     Write Sequence register    */
	u32	Diagnostic;	/* 08     Diagnostic register        */
	u32	TestBase;	/* 0C     Test Base Address          */
	u32	DiagRwData;	/* 10     Read Write Data (fw download)   */
	u32	DiagRwAddress;	/* 14     Read Write Address (fw download)*/
	u32	Reserved1[6];	/* 18-2F  reserved for future use    */
	u32	IntStatus;	/* 30     Interrupt Status           */
	u32	IntMask;	/* 34     Interrupt Mask             */
	u32	Reserved2[2];	/* 38-3F  reserved for future use    */
	u32	RequestFifo;	/* 40     Request Post/Free FIFO     */
	u32	ReplyFifo;	/* 44     Reply   Post/Free FIFO     */
	u32	RequestHiPriFifo; /* 48   Hi Priority Request FIFO   */
	u32	Reserved3;	/* 4C-4F  reserved for future use    */
	u32	HostIndex;	/* 50     Host Index register        */
	u32	Reserved4[15];	/* 54-8F                             */
	u32	Fubar;		/* 90     For Fubar usage            */
	u32	Reserved5[1050];/* 94-10F8                           */
	u32	Reset_1078;	/* 10FC   Reset 1078                 */
} SYSIF_REGS;

/*
 * NOTE: Use MPI_{DOORBELL,WRITESEQ,DIAG}_xxx defs in lsi/mpi.h
 * in conjunction with SYSIF_REGS accesses!
 */


/*
 *	Dynamic Multi-Pathing specific stuff...
 */

/* VirtTarget negoFlags field */
#define MPT_TARGET_NO_NEGO_WIDE		0x01
#define MPT_TARGET_NO_NEGO_SYNC		0x02
#define MPT_TARGET_NO_NEGO_QAS		0x04
#define MPT_TAPE_NEGO_IDP     		0x08

/*
 *	VirtDevice - FC LUN device or SCSI target device
 */
typedef struct _VirtTarget {
	struct scsi_target	*starget;
	u8			 tflags;
	u8			 ioc_id;
	u8			 id;
	u8			 channel;
	u8			 minSyncFactor;	/* 0xFF is async */
	u8			 maxOffset;	/* 0 if async */
	u8			 maxWidth;	/* 0 if narrow, 1 if wide */
	u8			 negoFlags;	/* bit field, see above */
	u8			 raidVolume;	/* set, if RAID Volume */
	u8			 type;		/* byte 0 of Inquiry data */
	u8			 deleted;	/* target in process of being removed */
	u8			 inDMD;		/* currently in the device
						   removal delay timer */
	u32			 num_luns;
} VirtTarget;

typedef struct _VirtDevice {
	VirtTarget		*vtarget;
	u8			 configured_lun;
	u64			 lun;
} VirtDevice;

/*
 *  Fibre Channel (SCSI) target device and associated defines...
 */
#define MPT_TARGET_DEFAULT_DV_STATUS	0x00
#define MPT_TARGET_FLAGS_VALID_NEGO	0x01
#define MPT_TARGET_FLAGS_VALID_INQUIRY	0x02
#define MPT_TARGET_FLAGS_Q_YES		0x08
#define MPT_TARGET_FLAGS_VALID_56	0x10
#define MPT_TARGET_FLAGS_SAF_TE_ISSUED	0x20
#define MPT_TARGET_FLAGS_RAID_COMPONENT	0x40
#define MPT_TARGET_FLAGS_LED_ON		0x80

/*
 *	IOCTL structure and associated defines
 */

#define MPTCTL_RESET_OK			0x01	/* Issue Bus Reset */

#define MPT_MGMT_STATUS_RF_VALID	0x01	/* The Reply Frame is VALID */
#define MPT_MGMT_STATUS_COMMAND_GOOD	0x02	/* Command Status GOOD */
#define MPT_MGMT_STATUS_PENDING		0x04	/* command is pending */
#define MPT_MGMT_STATUS_DID_IOCRESET	0x08	/* IOC Reset occurred
						   on the current*/
#define MPT_MGMT_STATUS_SENSE_VALID	0x10	/* valid sense info */
#define MPT_MGMT_STATUS_TIMER_ACTIVE	0x20	/* obsolete */
#define MPT_MGMT_STATUS_FREE_MF		0x40	/* free the mf from
						   complete routine */

#define INITIALIZE_MGMT_STATUS(status) \
	status = MPT_MGMT_STATUS_PENDING;
#define CLEAR_MGMT_STATUS(status) \
	status = 0;
#define CLEAR_MGMT_PENDING_STATUS(status) \
	status &= ~MPT_MGMT_STATUS_PENDING;
#define SET_MGMT_MSG_CONTEXT(msg_context, value) \
	msg_context = value;

typedef struct _MPT_MGMT {
	struct mutex		 mutex;
	struct completion	 done;
	u8			 reply[MPT_DEFAULT_FRAME_SIZE]; /* reply frame data */
	u8			 sense[MPT_SENSE_BUFFER_ALLOC];
	u8			 status;	/* current command status */
	int			 completion_code;
	u32			 msg_context;
} MPT_MGMT;

/*
 *  Event Structure and define
 */
#define MPTCTL_EVENT_LOG_SIZE		(0x000000032)
typedef struct _mpt_ioctl_events {
	u32	event;		/* Specified by define above */
	u32	eventContext;	/* Index or counter */
	u32	data[2];	/* First 8 bytes of Event Data */
} MPT_IOCTL_EVENTS;

/*
 * CONFIGPARM status  defines
 */
#define MPT_CONFIG_GOOD		MPI_IOCSTATUS_SUCCESS
#define MPT_CONFIG_ERROR	0x002F

/*
 *	Substructure to store SCSI specific configuration page data
 */
						/* dvStatus defines: */
#define MPT_SCSICFG_USE_NVRAM		0x01	/* WriteSDP1 using NVRAM */
#define MPT_SCSICFG_ALL_IDS		0x02	/* WriteSDP1 to all IDS */
/* #define MPT_SCSICFG_BLK_NEGO		0x10	   WriteSDP1 with WDTR and SDTR disabled */

typedef	struct _SpiCfgData {
	u32		 PortFlags;
	int		*nvram;			/* table of device NVRAM values */
	IOCPage4_t	*pIocPg4;		/* SEP devices addressing */
	dma_addr_t	 IocPg4_dma;		/* Phys Addr of IOCPage4 data */
	int		 IocPg4Sz;		/* IOCPage4 size */
	u8		 minSyncFactor;		/* 0xFF if async */
	u8		 maxSyncOffset;		/* 0 if async */
	u8		 maxBusWidth;		/* 0 if narrow, 1 if wide */
	u8		 busType;		/* SE, LVD, HD */
	u8		 sdp1version;		/* SDP1 version */
	u8		 sdp1length;		/* SDP1 length  */
	u8		 sdp0version;		/* SDP0 version */
	u8		 sdp0length;		/* SDP0 length  */
	u8		 dvScheduled;		/* 1 if scheduled */
	u8		 noQas;			/* Disable QAS for this adapter */
	u8		 Saf_Te;		/* 1 to force all Processors as
						 * SAF-TE if Inquiry data length
						 * is too short to check for SAF-TE
						 */
	u8		 bus_reset;		/* 1 to allow bus reset */
	u8		 rsvd[1];
}SpiCfgData;

typedef	struct _SasCfgData {
	u8		 ptClear;		/* 1 to automatically clear the
						 * persistent table.
						 * 0 to disable
						 * automatic clearing.
						 */
}SasCfgData;

/*
 * Inactive volume link list of raid component data
 * @inactive_list
 */
struct inactive_raid_component_info {
	struct 	 list_head list;
	u8		 volumeID;		/* volume target id */
	u8		 volumeBus;		/* volume channel */
	IOC_3_PHYS_DISK	 d;			/* phys disk info */
};

typedef	struct _RaidCfgData {
	IOCPage2_t	*pIocPg2;		/* table of Raid Volumes */
	IOCPage3_t	*pIocPg3;		/* table of physical disks */
	struct mutex	inactive_list_mutex;
	struct list_head	inactive_list; /* link list for physical
						disk that belong in
						inactive volumes */
}RaidCfgData;

typedef struct _FcCfgData {
	/* will ultimately hold fc_port_page0 also */
	struct {
		FCPortPage1_t	*data;
		dma_addr_t	 dma;
		int		 pg_sz;
	}			 fc_port_page1[2];
} FcCfgData;

#define MPT_RPORT_INFO_FLAGS_REGISTERED	0x01	/* rport registered */
#define MPT_RPORT_INFO_FLAGS_MISSING	0x02	/* missing from DevPage0 scan */

/*
 * data allocated for each fc rport device
 */
struct mptfc_rport_info
{
	struct list_head list;
	struct fc_rport *rport;
	struct scsi_target *starget;
	FCDevicePage0_t pg0;
	u8		flags;
};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*
 * MPT_SCSI_HOST defines - Used by the IOCTL and the SCSI drivers
 * Private to the driver.
 */

#define MPT_HOST_BUS_UNKNOWN		(0xFF)
#define MPT_HOST_TOO_MANY_TM		(0x05)
#define MPT_HOST_NVRAM_INVALID		(0xFFFFFFFF)
#define MPT_HOST_NO_CHAIN		(0xFFFFFFFF)
#define MPT_NVRAM_MASK_TIMEOUT		(0x000000FF)
#define MPT_NVRAM_SYNC_MASK		(0x0000FF00)
#define MPT_NVRAM_SYNC_SHIFT		(8)
#define MPT_NVRAM_DISCONNECT_ENABLE	(0x00010000)
#define MPT_NVRAM_ID_SCAN_ENABLE	(0x00020000)
#define MPT_NVRAM_LUN_SCAN_ENABLE	(0x00040000)
#define MPT_NVRAM_TAG_QUEUE_ENABLE	(0x00080000)
#define MPT_NVRAM_WIDE_DISABLE		(0x00100000)
#define MPT_NVRAM_BOOT_CHOICE		(0x00200000)

typedef enum {
	FC,
	SPI,
	SAS
} BUS_TYPE;

typedef struct _MPT_SCSI_HOST {
	struct _MPT_ADAPTER		 *ioc;
	ushort			  sel_timeout[MPT_MAX_FC_DEVICES];
	char			  *info_kbuf;
	long			  last_queue_full;
	u16			  spi_pending;
	struct list_head	  target_reset_list;
} MPT_SCSI_HOST;

typedef void (*MPT_ADD_SGE)(void *pAddr, u32 flagslength, dma_addr_t dma_addr);
typedef void (*MPT_ADD_CHAIN)(void *pAddr, u8 next, u16 length,
		dma_addr_t dma_addr);
typedef void (*MPT_SCHEDULE_TARGET_RESET)(void *ioc);
typedef void (*MPT_FLUSH_RUNNING_CMDS)(MPT_SCSI_HOST *hd);

/*
 *  Adapter Structure - pci_dev specific. Maximum: MPT_MAX_ADAPTERS
 */
typedef struct _MPT_ADAPTER
{
	int			 id;		/* Unique adapter id N {0,1,2,...} */
	int			 pci_irq;	/* This irq           */
	char			 name[MPT_NAME_LENGTH];	/* "iocN"             */
	const char		 *prod_name;	/* "LSIFC9x9"         */
#ifdef CONFIG_FUSION_LOGGING
	/* used in mpt_display_event_info */
	char			 evStr[EVENT_DESCR_STR_SZ];
#endif
	char			 board_name[16];
	char			 board_assembly[16];
	char			 board_tracer[16];
	u16			 nvdata_version_persistent;
	u16			 nvdata_version_default;
	int			 debug_level;
	u8			 io_missing_delay;
	u16			 device_missing_delay;
	SYSIF_REGS __iomem	*chip;		/* == c8817000 (mmap) */
	SYSIF_REGS __iomem	*pio_chip;	/* Programmed IO (downloadboot) */
	u8			 bus_type;
	u32			 mem_phys;	/* == f4020000 (mmap) */
	u32			 pio_mem_phys;	/* Programmed IO (downloadboot) */
	int			 mem_size;	/* mmap memory size */
	int			 number_of_buses;
	int			 devices_per_bus;
	int			 alloc_total;
	u32			 last_state;
	int			 active;
	u8			*alloc;		/* frames alloc ptr */
	dma_addr_t		 alloc_dma;
	u32			 alloc_sz;
	MPT_FRAME_HDR		*reply_frames;	/* Reply msg frames - rounded up! */
	u32			 reply_frames_low_dma;
	int			 reply_depth;	/* Num Allocated reply frames */
	int			 reply_sz;	/* Reply frame size */
	int			 num_chain;	/* Number of chain buffers */
	MPT_ADD_SGE              add_sge;       /* Pointer to add_sge
						   function */
	MPT_ADD_CHAIN		 add_chain;	/* Pointer to add_chain
						   function */
		/* Pool of buffers for chaining. ReqToChain
		 * and ChainToChain track index of chain buffers.
		 * ChainBuffer (DMA) virt/phys addresses.
		 * FreeChainQ (lock) locking mechanisms.
		 */
	int			*ReqToChain;
	int			*RequestNB;
	int			*ChainToChain;
	u8			*ChainBuffer;
	dma_addr_t		 ChainBufferDMA;
	struct list_head	 FreeChainQ;
	spinlock_t		 FreeChainQlock;
		/* We (host driver) get to manage our own RequestQueue! */
	dma_addr_t		 req_frames_dma;
	MPT_FRAME_HDR		*req_frames;	/* Request msg frames - rounded up! */
	u32			 req_frames_low_dma;
	int			 req_depth;	/* Number of request frames */
	int			 req_sz;	/* Request frame size (bytes) */
	spinlock_t		 FreeQlock;
	struct list_head	 FreeQ;
		/* Pool of SCSI sense buffers for commands coming from
		 * the SCSI mid-layer.  We have one 256 byte sense buffer
		 * for each REQ entry.
		 */
	u8			*sense_buf_pool;
	dma_addr_t		 sense_buf_pool_dma;
	u32			 sense_buf_low_dma;
	u8			*HostPageBuffer; /* SAS - host page buffer support */
	u32			HostPageBuffer_sz;
	dma_addr_t		HostPageBuffer_dma;
	int			 mtrr_reg;
	struct pci_dev		*pcidev;	/* struct pci_dev pointer */
	int			bars;		/* bitmask of BAR's that must be configured */
	int			msi_enable;
	u8			__iomem *memmap;	/* mmap address */
	struct Scsi_Host	*sh;		/* Scsi Host pointer */
	SpiCfgData		spi_data;	/* Scsi config. data */
	RaidCfgData		raid_data;	/* Raid config. data */
	SasCfgData		sas_data;	/* Sas config. data */
	FcCfgData		fc_data;	/* Fc config. data */
	struct proc_dir_entry	*ioc_dentry;
	struct _MPT_ADAPTER	*alt_ioc;	/* ptr to 929 bound adapter port */
	u32			 biosVersion;	/* BIOS version from IO Unit Page 2 */
	int			 eventTypes;	/* Event logging parameters */
	int			 eventContext;	/* Next event context */
	int			 eventLogSize;	/* Max number of cached events */
	struct _mpt_ioctl_events *events;	/* pointer to event log */
	u8			*cached_fw;	/* Pointer to FW */
	dma_addr_t	 	cached_fw_dma;
	int			 hs_reply_idx;
#ifndef MFCNT
	u32			 pad0;
#else
	u32			 mfcnt;
#endif
	u32			 NB_for_64_byte_frame;
	u32			 hs_req[MPT_MAX_FRAME_SIZE/sizeof(u32)];
	u16			 hs_reply[MPT_MAX_FRAME_SIZE/sizeof(u16)];
	IOCFactsReply_t		 facts;
	PortFactsReply_t	 pfacts[2];
	FCPortPage0_t		 fc_port_page0[2];
	LANPage0_t		 lan_cnfg_page0;
	LANPage1_t		 lan_cnfg_page1;

	u8			 ir_firmware; /* =1 if IR firmware detected */
	/*
	 * Description: errata_flag_1064
	 * If a PCIX read occurs within 1 or 2 cycles after the chip receives
	 * a split completion for a read data, an internal address pointer incorrectly
	 * increments by 32 bytes
	 */
	int			 errata_flag_1064;
	int			 aen_event_read_flag; /* flag to indicate event log was read*/
	u8			 FirstWhoInit;
	u8			 upload_fw;	/* If set, do a fw upload */
	u8			 NBShiftFactor;  /* NB Shift Factor based on Block Size (Facts)  */
	u8			 pad1[4];
	u8			 DoneCtx;
	u8			 TaskCtx;
	u8			 InternalCtx;
	struct list_head	 list;
	struct net_device	*netdev;
	struct list_head	 sas_topology;
	struct mutex		 sas_topology_mutex;

	struct workqueue_struct	*fw_event_q;
	struct list_head	 fw_event_list;
	spinlock_t		 fw_event_lock;
	u8			 fw_events_off; /* if '1', then ignore events */
	char 			 fw_event_q_name[MPT_KOBJ_NAME_LEN];

	struct mutex		 sas_discovery_mutex;
	u8			 sas_discovery_runtime;
	u8			 sas_discovery_ignore_events;

	/* port_info object for the host */
	struct mptsas_portinfo	*hba_port_info;
	u64			 hba_port_sas_addr;
	u16			 hba_port_num_phy;
	struct list_head	 sas_device_info_list;
	struct mutex		 sas_device_info_mutex;
	u8			 old_sas_discovery_protocal;
	u8			 sas_discovery_quiesce_io;
	int			 sas_index; /* index refrencing */
	MPT_MGMT		 sas_mgmt;
	MPT_MGMT		 mptbase_cmds; /* for sending config pages */
	MPT_MGMT		 internal_cmds;
	MPT_MGMT		 taskmgmt_cmds;
	MPT_MGMT		 ioctl_cmds;
	spinlock_t		 taskmgmt_lock; /* diagnostic reset lock */
	int			 taskmgmt_in_progress;
	u8			 taskmgmt_quiesce_io;
	u8			 ioc_reset_in_progress;
	u8			 reset_status;
	u8			 wait_on_reset_completion;
	MPT_SCHEDULE_TARGET_RESET schedule_target_reset;
	MPT_FLUSH_RUNNING_CMDS schedule_dead_ioc_flush_running_cmds;
	struct work_struct	 sas_persist_task;

	struct work_struct	 fc_setup_reset_work;
	struct list_head	 fc_rports;
	struct work_struct	 fc_lsc_work;
	u8			 fc_link_speed[2];
	spinlock_t		 fc_rescan_work_lock;
	struct work_struct	 fc_rescan_work;
	char			 fc_rescan_work_q_name[MPT_KOBJ_NAME_LEN];
	struct workqueue_struct *fc_rescan_work_q;

	/* driver forced bus resets count */
	unsigned long		  hard_resets;
	/* fw/external bus resets count */
	unsigned long		  soft_resets;
	/* cmd timeouts */
	unsigned long		  timeouts;

	struct scsi_cmnd	**ScsiLookup;
	spinlock_t		  scsi_lookup_lock;
	u64			dma_mask;
	u32			  broadcast_aen_busy;
	char			 reset_work_q_name[MPT_KOBJ_NAME_LEN];
	struct workqueue_struct *reset_work_q;
	struct delayed_work	 fault_reset_work;

	u8			sg_addr_size;
	u8			in_rescan;
	u8			SGE_size;

} MPT_ADAPTER;

/*
 *  New return value convention:
 *    1 = Ok to free associated request frame
 *    0 = not Ok ...
 */
typedef int (*MPT_CALLBACK)(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req, MPT_FRAME_HDR *reply);
typedef int (*MPT_EVHANDLER)(MPT_ADAPTER *ioc, EventNotificationReply_t *evReply);
typedef int (*MPT_RESETHANDLER)(MPT_ADAPTER *ioc, int reset_phase);
/* reset_phase defs */
#define MPT_IOC_PRE_RESET		0
#define MPT_IOC_POST_RESET		1
#define MPT_IOC_SETUP_RESET		2

/*
 * Invent MPT host event (super-set of MPI Events)
 * Fitted to 1030's 64-byte [max] request frame size
 */
typedef struct _MPT_HOST_EVENT {
	EventNotificationReply_t	 MpiEvent;	/* 8 32-bit words! */
	u32				 pad[6];
	void				*next;
} MPT_HOST_EVENT;

#define MPT_HOSTEVENT_IOC_BRINGUP	0x91
#define MPT_HOSTEVENT_IOC_RECOVER	0x92

/* Define the generic types based on the size
 * of the dma_addr_t type.
 */
typedef struct _mpt_sge {
	u32		FlagsLength;
	dma_addr_t	Address;
} MptSge_t;


#define mpt_msg_flags(ioc) \
	(ioc->sg_addr_size == sizeof(u64)) ?		\
	MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64 : 		\
	MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32

#define MPT_SGE_FLAGS_64_BIT_ADDRESSING \
	(MPI_SGE_FLAGS_64_BIT_ADDRESSING << MPI_SGE_FLAGS_SHIFT)

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Funky (private) macros...
 */
#include "mptdebug.h"

#define MPT_INDEX_2_MFPTR(ioc,idx) \
	(MPT_FRAME_HDR*)( (u8*)(ioc)->req_frames + (ioc)->req_sz * (idx) )

#define MFPTR_2_MPT_INDEX(ioc,mf) \
	(int)( ((u8*)mf - (u8*)(ioc)->req_frames) / (ioc)->req_sz )

#define MPT_INDEX_2_RFPTR(ioc,idx) \
	(MPT_FRAME_HDR*)( (u8*)(ioc)->reply_frames + (ioc)->req_sz * (idx) )

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#define SCSI_STD_SENSE_BYTES    18
#define SCSI_STD_INQUIRY_BYTES  36
#define SCSI_MAX_INQUIRY_BYTES  96

/*
 * MPT_SCSI_HOST defines - Used by the IOCTL and the SCSI drivers
 * Private to the driver.
 */
/* LOCAL structure and fields used when processing
 * internally generated commands. These include:
 * bus scan, dv and config requests.
 */
typedef struct _MPT_LOCAL_REPLY {
	ConfigPageHeader_t header;
	int	completion;
	u8	sense[SCSI_STD_SENSE_BYTES];
	u8	scsiStatus;
	u8	skip;
	u32	pad;
} MPT_LOCAL_REPLY;


/* The TM_STATE variable is used to provide strict single threading of TM
 * requests as well as communicate TM error conditions.
 */
#define TM_STATE_NONE          (0)
#define	TM_STATE_IN_PROGRESS   (1)
#define	TM_STATE_ERROR	       (2)

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	More Dynamic Multi-Pathing stuff...
 */

/* Forward decl, a strange C thing, to prevent gcc compiler warnings */
struct scsi_cmnd;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Generic structure passed to the base mpt_config function.
 */
typedef struct _x_config_parms {
	union {
		ConfigExtendedPageHeader_t	*ehdr;
		ConfigPageHeader_t	*hdr;
	} cfghdr;
	dma_addr_t		 physAddr;
	u32			 pageAddr;	/* properly formatted */
	u16			 status;
	u8			 action;
	u8			 dir;
	u8			 timeout;	/* seconds */
} CONFIGPARMS;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Public entry points...
 */
extern int	 mpt_attach(struct pci_dev *pdev, const struct pci_device_id *id);
extern void	 mpt_detach(struct pci_dev *pdev);
#ifdef CONFIG_PM
extern int	 mpt_suspend(struct pci_dev *pdev, pm_message_t state);
extern int	 mpt_resume(struct pci_dev *pdev);
#endif
extern u8	 mpt_register(MPT_CALLBACK cbfunc, MPT_DRIVER_CLASS dclass,
		char *func_name);
extern void	 mpt_deregister(u8 cb_idx);
extern int	 mpt_event_register(u8 cb_idx, MPT_EVHANDLER ev_cbfunc);
extern void	 mpt_event_deregister(u8 cb_idx);
extern int	 mpt_reset_register(u8 cb_idx, MPT_RESETHANDLER reset_func);
extern void	 mpt_reset_deregister(u8 cb_idx);
extern int	 mpt_device_driver_register(struct mpt_pci_driver * dd_cbfunc, u8 cb_idx);
extern void	 mpt_device_driver_deregister(u8 cb_idx);
extern MPT_FRAME_HDR	*mpt_get_msg_frame(u8 cb_idx, MPT_ADAPTER *ioc);
extern void	 mpt_free_msg_frame(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf);
extern void	 mpt_put_msg_frame(u8 cb_idx, MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf);
extern void	 mpt_put_msg_frame_hi_pri(u8 cb_idx, MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf);

extern int	 mpt_send_handshake_request(u8 cb_idx, MPT_ADAPTER *ioc, int reqBytes, u32 *req, int sleepFlag);
extern int	 mpt_verify_adapter(int iocid, MPT_ADAPTER **iocpp);
extern u32	 mpt_GetIocState(MPT_ADAPTER *ioc, int cooked);
extern void	 mpt_print_ioc_summary(MPT_ADAPTER *ioc, char *buf, int *size, int len, int showlan);
extern int	 mpt_HardResetHandler(MPT_ADAPTER *ioc, int sleepFlag);
extern int	 mpt_Soft_Hard_ResetHandler(MPT_ADAPTER *ioc, int sleepFlag);
extern int	 mpt_config(MPT_ADAPTER *ioc, CONFIGPARMS *cfg);
extern int	 mpt_alloc_fw_memory(MPT_ADAPTER *ioc, int size);
extern void	 mpt_free_fw_memory(MPT_ADAPTER *ioc);
extern int	 mpt_findImVolumes(MPT_ADAPTER *ioc);
extern int	 mptbase_sas_persist_operation(MPT_ADAPTER *ioc, u8 persist_opcode);
extern int	 mpt_raid_phys_disk_pg0(MPT_ADAPTER *ioc, u8 phys_disk_num, pRaidPhysDiskPage0_t phys_disk);
extern int	mpt_raid_phys_disk_pg1(MPT_ADAPTER *ioc, u8 phys_disk_num,
		pRaidPhysDiskPage1_t phys_disk);
extern int	mpt_raid_phys_disk_get_num_paths(MPT_ADAPTER *ioc,
		u8 phys_disk_num);
extern int	 mpt_set_taskmgmt_in_progress_flag(MPT_ADAPTER *ioc);
extern void	 mpt_clear_taskmgmt_in_progress_flag(MPT_ADAPTER *ioc);
extern void     mpt_halt_firmware(MPT_ADAPTER *ioc);


/*
 *  Public data decl's...
 */
extern struct list_head	  ioc_list;
extern int mpt_fwfault_debug;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif		/* } __KERNEL__ */

#ifdef CONFIG_64BIT
#define CAST_U32_TO_PTR(x)	((void *)(u64)x)
#define CAST_PTR_TO_U32(x)	((u32)(u64)x)
#else
#define CAST_U32_TO_PTR(x)	((void *)x)
#define CAST_PTR_TO_U32(x)	((u32)x)
#endif

#define MPT_PROTOCOL_FLAGS_c_c_c_c(pflags) \
	((pflags) & MPI_PORTFACTS_PROTOCOL_INITIATOR)	? 'I' : 'i',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_TARGET)	? 'T' : 't',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_LAN)		? 'L' : 'l',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_LOGBUSADDR)	? 'B' : 'b'

/*
 *  Shifted SGE Defines - Use in SGE with FlagsLength member.
 *  Otherwise, use MPI_xxx defines (refer to "lsi/mpi.h" header).
 *  Defaults: 32 bit SGE, SYSTEM_ADDRESS if direction bit is 0, read
 */
#define MPT_TRANSFER_IOC_TO_HOST		(0x00000000)
#define MPT_TRANSFER_HOST_TO_IOC		(0x04000000)
#define MPT_SGE_FLAGS_LAST_ELEMENT		(0x80000000)
#define MPT_SGE_FLAGS_END_OF_BUFFER		(0x40000000)
#define MPT_SGE_FLAGS_LOCAL_ADDRESS		(0x08000000)
#define MPT_SGE_FLAGS_DIRECTION			(0x04000000)
#define MPT_SGE_FLAGS_END_OF_LIST		(0x01000000)

#define MPT_SGE_FLAGS_TRANSACTION_ELEMENT	(0x00000000)
#define MPT_SGE_FLAGS_SIMPLE_ELEMENT		(0x10000000)
#define MPT_SGE_FLAGS_CHAIN_ELEMENT		(0x30000000)
#define MPT_SGE_FLAGS_ELEMENT_MASK		(0x30000000)

#define MPT_SGE_FLAGS_SSIMPLE_READ \
	(MPT_SGE_FLAGS_LAST_ELEMENT |	\
	 MPT_SGE_FLAGS_END_OF_BUFFER |	\
	 MPT_SGE_FLAGS_END_OF_LIST |	\
	 MPT_SGE_FLAGS_SIMPLE_ELEMENT |	\
	 MPT_TRANSFER_IOC_TO_HOST)
#define MPT_SGE_FLAGS_SSIMPLE_WRITE \
	(MPT_SGE_FLAGS_LAST_ELEMENT |	\
	 MPT_SGE_FLAGS_END_OF_BUFFER |	\
	 MPT_SGE_FLAGS_END_OF_LIST |	\
	 MPT_SGE_FLAGS_SIMPLE_ELEMENT |	\
	 MPT_TRANSFER_HOST_TO_IOC)

/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif

