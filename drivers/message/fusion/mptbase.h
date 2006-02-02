/*
 *  linux/drivers/message/fusion/mptbase.h
 *      High performance SCSI + LAN / Fibre Channel device drivers.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2005 LSI Logic Corporation
 *  (mailto:mpt_linux_developer@lsil.com)
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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>

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
#define MODULEAUTHOR	"LSI Logic Corporation"
#endif

#ifndef COPYRIGHT
#define COPYRIGHT	"Copyright (c) 1999-2005 " MODULEAUTHOR
#endif

#define MPT_LINUX_VERSION_COMMON	"3.03.07"
#define MPT_LINUX_PACKAGE_NAME		"@(#)mptlinux-3.03.07"
#define WHAT_MAGIC_STRING		"@" "(" "#" ")"

#define show_mptmod_ver(s,ver)  \
	printk(KERN_INFO "%s %s\n", s, ver);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Fusion MPT(linux) driver configurable stuff...
 */
#define MPT_MAX_ADAPTERS		18
#define MPT_MAX_PROTOCOL_DRIVERS	16
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
#define MPT_FC_CAN_QUEUE	127
#define MPT_SCSI_CAN_QUEUE	127

/*
 * Set the MAX_SGE value based on user input.
 */
#ifdef  CONFIG_FUSION_MAX_SGE
#if     CONFIG_FUSION_MAX_SGE  < 16
#define MPT_SCSI_SG_DEPTH	16
#elif   CONFIG_FUSION_MAX_SGE  > 128
#define MPT_SCSI_SG_DEPTH	128
#else
#define MPT_SCSI_SG_DEPTH	CONFIG_FUSION_MAX_SGE
#endif
#else
#define MPT_SCSI_SG_DEPTH	40
#endif

#ifdef __KERNEL__	/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <linux/proc_fs.h>

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Attempt semi-consistent error & warning msgs across
 * MPT drivers.  NOTE: Users of these macro defs must
 * themselves define their own MYNAM.
 */
#define MYIOC_s_INFO_FMT		KERN_INFO MYNAM ": %s: "
#define MYIOC_s_NOTE_FMT		KERN_NOTICE MYNAM ": %s: "
#define MYIOC_s_WARN_FMT		KERN_WARNING MYNAM ": %s: WARNING - "
#define MYIOC_s_ERR_FMT			KERN_ERR MYNAM ": %s: ERROR - "

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
	u32	Reserved3[2];	/* 48-4F  reserved for future use    */
	u32	HostIndex;	/* 50     Host Index register        */
	u32	Reserved4[15];	/* 54-8F                             */
	u32	Fubar;		/* 90     For Fubar usage            */
	u32	Reserved5[27];	/* 94-FF                             */
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
	u8			 tflags;
	u8			 ioc_id;
	u8			 target_id;
	u8			 bus_id;
	u8			 minSyncFactor;	/* 0xFF is async */
	u8			 maxOffset;	/* 0 if async */
	u8			 maxWidth;	/* 0 if narrow, 1 if wide */
	u8			 negoFlags;	/* bit field, see above */
	u8			 raidVolume;	/* set, if RAID Volume */
	u8			 type;		/* byte 0 of Inquiry data */
	u32			 num_luns;
	u32			 luns[8];		/* Max LUNs is 256 */
	u8			 inq_data[8];
} VirtTarget;

typedef struct _VirtDevice {
	VirtTarget	 	*vtarget;
	u8			 ioc_id;
	u8			 bus_id;
	u8			 target_id;
	u8			 configured_lun;
	u32			 lun;
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

/*
 *	/proc/mpt interface
 */
typedef struct {
	const char	*name;
	mode_t		 mode;
	int		 pad;
	read_proc_t	*read_proc;
	write_proc_t	*write_proc;
} mpt_proc_entry_t;

#define MPT_PROC_READ_RETURN(buf,start,offset,request,eof,len) \
do { \
	len -= offset;			\
	if (len < request) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = request;		\
	*start = buf + offset;		\
	return len;			\
} while (0)


/*
 *	IOCTL structure and associated defines
 */

#define MPT_IOCTL_STATUS_DID_IOCRESET	0x01	/* IOC Reset occurred on the current*/
#define MPT_IOCTL_STATUS_RF_VALID	0x02	/* The Reply Frame is VALID */
#define MPT_IOCTL_STATUS_TIMER_ACTIVE	0x04	/* The timer is running */
#define MPT_IOCTL_STATUS_SENSE_VALID	0x08	/* Sense data is valid */
#define MPT_IOCTL_STATUS_COMMAND_GOOD	0x10	/* Command Status GOOD */
#define MPT_IOCTL_STATUS_TMTIMER_ACTIVE	0x20	/* The TM timer is running */
#define MPT_IOCTL_STATUS_TM_FAILED	0x40	/* User TM request failed */

#define MPTCTL_RESET_OK			0x01	/* Issue Bus Reset */

typedef struct _MPT_IOCTL {
	struct _MPT_ADAPTER	*ioc;
	u8			 ReplyFrame[MPT_DEFAULT_FRAME_SIZE];	/* reply frame data */
	u8			 sense[MPT_SENSE_BUFFER_ALLOC];
	int			 wait_done;	/* wake-up value for this ioc */
	u8			 rsvd;
	u8			 status;	/* current command status */
	u8			 reset;		/* 1 if bus reset allowed */
	u8			 target;	/* target for reset */
	struct mutex		 ioctl_mutex;
} MPT_IOCTL;

#define MPT_SAS_MGMT_STATUS_RF_VALID	0x02	/* The Reply Frame is VALID */
#define MPT_SAS_MGMT_STATUS_COMMAND_GOOD	0x10	/* Command Status GOOD */
#define MPT_SAS_MGMT_STATUS_TM_FAILED	0x40	/* User TM request failed */

typedef struct _MPT_SAS_MGMT {
	struct mutex		 mutex;
	struct completion	 done;
	u8			 reply[MPT_DEFAULT_FRAME_SIZE]; /* reply frame data */
	u8			 status;	/* current command status */
}MPT_SAS_MGMT;

/*
 *  Event Structure and define
 */
#define MPTCTL_EVENT_LOG_SIZE		(0x000000032)
typedef struct _mpt_ioctl_events {
	u32	event;		/* Specified by define above */
	u32	eventContext;	/* Index or counter */
	int	data[2];	/* First 8 bytes of Event Data */
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
#define MPT_SCSICFG_NEGOTIATE		0x01	/* Negotiate on next IO */
#define MPT_SCSICFG_NEED_DV		0x02	/* Schedule DV */
#define MPT_SCSICFG_DV_PENDING		0x04	/* DV on this physical id pending */
#define MPT_SCSICFG_DV_NOT_DONE		0x08	/* DV has not been performed */
#define MPT_SCSICFG_BLK_NEGO		0x10	/* WriteSDP1 with WDTR and SDTR disabled */
#define MPT_SCSICFG_RELOAD_IOC_PG3	0x20	/* IOC Pg 3 data is obsolete */
						/* Args passed to writeSDP1: */
#define MPT_SCSICFG_USE_NVRAM		0x01	/* WriteSDP1 using NVRAM */
#define MPT_SCSICFG_ALL_IDS		0x02	/* WriteSDP1 to all IDS */
/* #define MPT_SCSICFG_BLK_NEGO		0x10	   WriteSDP1 with WDTR and SDTR disabled */

typedef	struct _SpiCfgData {
	u32		 PortFlags;
	int		*nvram;			/* table of device NVRAM values */
	IOCPage4_t	*pIocPg4;		/* SEP devices addressing */
	dma_addr_t	 IocPg4_dma;		/* Phys Addr of IOCPage4 data */
	int		 IocPg4Sz;		/* IOCPage4 size */
	u8		 dvStatus[MPT_MAX_SCSI_DEVICES];
	u8		 minSyncFactor;		/* 0xFF if async */
	u8		 maxSyncOffset;		/* 0 if async */
	u8		 maxBusWidth;		/* 0 if narrow, 1 if wide */
	u8		 busType;		/* SE, LVD, HD */
	u8		 sdp1version;		/* SDP1 version */
	u8		 sdp1length;		/* SDP1 length  */
	u8		 sdp0version;		/* SDP0 version */
	u8		 sdp0length;		/* SDP0 length  */
	u8		 dvScheduled;		/* 1 if scheduled */
	u8		 forceDv;		/* 1 to force DV scheduling */
	u8		 noQas;			/* Disable QAS for this adapter */
	u8		 Saf_Te;		/* 1 to force all Processors as
						 * SAF-TE if Inquiry data length
						 * is too short to check for SAF-TE
						 */
	u8		 mpt_dv;		/* command line option: enhanced=1, basic=0 */
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

typedef	struct _RaidCfgData {
	IOCPage2_t	*pIocPg2;		/* table of Raid Volumes */
	IOCPage3_t	*pIocPg3;		/* table of physical disks */
	int		 isRaid;		/* bit field, 1 if RAID */
}RaidCfgData;

#define MPT_RPORT_INFO_FLAGS_REGISTERED	0x01	/* rport registered */
#define MPT_RPORT_INFO_FLAGS_MISSING	0x02	/* missing from DevPage0 scan */
#define MPT_RPORT_INFO_FLAGS_MAPPED_VDEV 0x04	/* target mapped in vdev */

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
	u8		remap_needed;
};

/*
 *  Adapter Structure - pci_dev specific. Maximum: MPT_MAX_ADAPTERS
 */
typedef struct _MPT_ADAPTER
{
	int			 id;		/* Unique adapter id N {0,1,2,...} */
	int			 pci_irq;	/* This irq           */
	char			 name[MPT_NAME_LENGTH];	/* "iocN"             */
	char			*prod_name;	/* "LSIFC9x9"         */
	SYSIF_REGS __iomem	*chip;		/* == c8817000 (mmap) */
	SYSIF_REGS __iomem	*pio_chip;	/* Programmed IO (downloadboot) */
	u8			 bus_type;
	u32			 mem_phys;	/* == f4020000 (mmap) */
	u32			 pio_mem_phys;	/* Programmed IO (downloadboot) */
	int			 mem_size;	/* mmap memory size */
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
	u8			__iomem *memmap;	/* mmap address */
	struct Scsi_Host	*sh;		/* Scsi Host pointer */
	SpiCfgData		spi_data;	/* Scsi config. data */
	RaidCfgData		raid_data;	/* Raid config. data */
	SasCfgData		sas_data;	/* Sas config. data */
	MPT_IOCTL		*ioctl;		/* ioctl data pointer */
	struct proc_dir_entry	*ioc_dentry;
	struct _MPT_ADAPTER	*alt_ioc;	/* ptr to 929 bound adapter port */
	spinlock_t		 diagLock;	/* diagnostic reset lock */
	int			 diagPending;
	u32			 biosVersion;	/* BIOS version from IO Unit Page 2 */
	int			 eventTypes;	/* Event logging parameters */
	int			 eventContext;	/* Next event context */
	int			 eventLogSize;	/* Max number of cached events */
	struct _mpt_ioctl_events *events;	/* pointer to event log */
	u8			*cached_fw;	/* Pointer to FW */
	dma_addr_t	 	cached_fw_dma;
	struct list_head	 configQ;	/* linked list of config. requests */
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
	struct timer_list	 persist_timer;	/* persist table timer */
	int			 persist_wait_done; /* persist completion flag */
	u8			 persist_reply_frame[MPT_DEFAULT_FRAME_SIZE]; /* persist reply */
	LANPage0_t		 lan_cnfg_page0;
	LANPage1_t		 lan_cnfg_page1;
	/*
	 * Description: errata_flag_1064
	 * If a PCIX read occurs within 1 or 2 cycles after the chip receives
	 * a split completion for a read data, an internal address pointer incorrectly
	 * increments by 32 bytes
	 */
	int			 errata_flag_1064;
	u8			 FirstWhoInit;
	u8			 upload_fw;	/* If set, do a fw upload */
	u8			 reload_fw;	/* Force a FW Reload on next reset */
	u8			 NBShiftFactor;  /* NB Shift Factor based on Block Size (Facts)  */
	u8			 pad1[4];
	int			 DoneCtx;
	int			 TaskCtx;
	int			 InternalCtx;
	spinlock_t		 initializing_hba_lock;
	int 	 		 initializing_hba_lock_flag;
	struct list_head	 list;
	struct net_device	*netdev;
	struct list_head	 sas_topology;
	struct mutex		 sas_topology_mutex;
	MPT_SAS_MGMT		 sas_mgmt;
	int			 num_ports;
	struct work_struct	 mptscsih_persistTask;

	struct list_head	 fc_rports;
	spinlock_t		 fc_rport_lock; /* list and ri flags */
	spinlock_t		 fc_rescan_work_lock;
	int			 fc_rescan_work_count;
	struct work_struct	 fc_rescan_work;

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

#define mpt_addr_size() \
	((sizeof(dma_addr_t) == sizeof(u64)) ? MPI_SGE_FLAGS_64_BIT_ADDRESSING : \
		MPI_SGE_FLAGS_32_BIT_ADDRESSING)

#define mpt_msg_flags() \
	((sizeof(dma_addr_t) == sizeof(u64)) ? MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64 : \
		MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32)

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Funky (private) macros...
 */
#ifdef MPT_DEBUG
#define dprintk(x)  printk x
#else
#define dprintk(x)
#endif

#ifdef MPT_DEBUG_INIT
#define dinitprintk(x)  printk x
#define DBG_DUMP_FW_REQUEST_FRAME(mfp) \
	{	int  i, n = 10;						\
		u32 *m = (u32 *)(mfp);					\
		printk(KERN_INFO " ");					\
		for (i=0; i<n; i++)					\
			printk(" %08x", le32_to_cpu(m[i]));		\
		printk("\n");						\
	}
#else
#define dinitprintk(x)
#define DBG_DUMP_FW_REQUEST_FRAME(mfp)
#endif

#ifdef MPT_DEBUG_EXIT
#define dexitprintk(x)  printk x
#else
#define dexitprintk(x)
#endif

#if defined MPT_DEBUG_FAIL || defined (MPT_DEBUG_SG)
#define dfailprintk(x) printk x
#else
#define dfailprintk(x)
#endif

#ifdef MPT_DEBUG_HANDSHAKE
#define dhsprintk(x)  printk x
#else
#define dhsprintk(x)
#endif

#ifdef MPT_DEBUG_EVENTS
#define devtprintk(x)  printk x
#else
#define devtprintk(x)
#endif

#ifdef MPT_DEBUG_RESET
#define drsprintk(x)  printk x
#else
#define drsprintk(x)
#endif

//#if defined(MPT_DEBUG) || defined(MPT_DEBUG_MSG_FRAME)
#if defined(MPT_DEBUG_MSG_FRAME)
#define dmfprintk(x)  printk x
#define DBG_DUMP_REQUEST_FRAME(mfp) \
	{	int  i, n = 24;						\
		u32 *m = (u32 *)(mfp);					\
		for (i=0; i<n; i++) {					\
			if (i && ((i%8)==0))				\
				printk("\n");				\
			printk("%08x ", le32_to_cpu(m[i]));		\
		}							\
		printk("\n");						\
	}
#else
#define dmfprintk(x)
#define DBG_DUMP_REQUEST_FRAME(mfp)
#endif

#ifdef MPT_DEBUG_IRQ
#define dirqprintk(x)  printk x
#else
#define dirqprintk(x)
#endif

#ifdef MPT_DEBUG_SG
#define dsgprintk(x)  printk x
#else
#define dsgprintk(x)
#endif

#if defined(MPT_DEBUG_DL) || defined(MPT_DEBUG)
#define ddlprintk(x)  printk x
#else
#define ddlprintk(x)
#endif

#ifdef MPT_DEBUG_DV
#define ddvprintk(x)  printk x
#else
#define ddvprintk(x)
#endif

#ifdef MPT_DEBUG_NEGO
#define dnegoprintk(x)  printk x
#else
#define dnegoprintk(x)
#endif

#if defined(MPT_DEBUG_DV) || defined(MPT_DEBUG_DV_TINY)
#define ddvtprintk(x)  printk x
#else
#define ddvtprintk(x)
#endif

#ifdef MPT_DEBUG_IOCTL
#define dctlprintk(x) printk x
#else
#define dctlprintk(x)
#endif

#ifdef MPT_DEBUG_REPLY
#define dreplyprintk(x) printk x
#else
#define dreplyprintk(x)
#endif

#ifdef DMPT_DEBUG_FC
#define dfcprintk(x) printk x
#else
#define dfcprintk(x)
#endif

#ifdef MPT_DEBUG_TM
#define dtmprintk(x) printk x
#define DBG_DUMP_TM_REQUEST_FRAME(mfp) \
	{	u32 *m = (u32 *)(mfp);					\
		int  i, n = 13;						\
		printk("TM_REQUEST:\n");				\
		for (i=0; i<n; i++) {					\
			if (i && ((i%8)==0))				\
				printk("\n");				\
			printk("%08x ", le32_to_cpu(m[i]));		\
		}							\
		printk("\n");						\
	}
#define DBG_DUMP_TM_REPLY_FRAME(mfp) \
	{	u32 *m = (u32 *)(mfp);					\
		int  i, n = (le32_to_cpu(m[0]) & 0x00FF0000) >> 16;	\
		printk("TM_REPLY MessageLength=%d:\n", n);		\
		for (i=0; i<n; i++) {					\
			if (i && ((i%8)==0))				\
				printk("\n");				\
			printk(" %08x", le32_to_cpu(m[i]));		\
		}							\
		printk("\n");						\
	}
#else
#define dtmprintk(x)
#define DBG_DUMP_TM_REQUEST_FRAME(mfp)
#define DBG_DUMP_TM_REPLY_FRAME(mfp)
#endif

#if defined(MPT_DEBUG_CONFIG) || defined(MPT_DEBUG)
#define dcprintk(x) printk x
#else
#define dcprintk(x)
#endif

#if defined(MPT_DEBUG_SCSI) || defined(MPT_DEBUG) || defined(MPT_DEBUG_MSG_FRAME)
#define dsprintk(x) printk x
#else
#define dsprintk(x)
#endif


#define MPT_INDEX_2_MFPTR(ioc,idx) \
	(MPT_FRAME_HDR*)( (u8*)(ioc)->req_frames + (ioc)->req_sz * (idx) )

#define MFPTR_2_MPT_INDEX(ioc,mf) \
	(int)( ((u8*)mf - (u8*)(ioc)->req_frames) / (ioc)->req_sz )

#define MPT_INDEX_2_RFPTR(ioc,idx) \
	(MPT_FRAME_HDR*)( (u8*)(ioc)->reply_frames + (ioc)->req_sz * (idx) )

#if defined(MPT_DEBUG) || defined(MPT_DEBUG_MSG_FRAME)
#define DBG_DUMP_REPLY_FRAME(mfp) \
	{	u32 *m = (u32 *)(mfp);					\
		int  i, n = (le32_to_cpu(m[0]) & 0x00FF0000) >> 16;	\
		printk(KERN_INFO " ");					\
		for (i=0; i<n; i++)					\
			printk(" %08x", le32_to_cpu(m[i]));		\
		printk("\n");						\
	}
#define DBG_DUMP_REQUEST_FRAME_HDR(mfp) \
	{	int  i, n = 3;						\
		u32 *m = (u32 *)(mfp);					\
		printk(KERN_INFO " ");					\
		for (i=0; i<n; i++)					\
			printk(" %08x", le32_to_cpu(m[i]));		\
		printk("\n");						\
	}
#else
#define DBG_DUMP_REPLY_FRAME(mfp)
#define DBG_DUMP_REQUEST_FRAME_HDR(mfp)
#endif


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

/* The TM_STATE variable is used to provide strict single threading of TM
 * requests as well as communicate TM error conditions.
 */
#define TM_STATE_NONE          (0)
#define	TM_STATE_IN_PROGRESS   (1)
#define	TM_STATE_ERROR	       (2)

typedef enum {
	FC,
	SPI,
	SAS
} BUS_TYPE;

typedef struct _MPT_SCSI_HOST {
	MPT_ADAPTER		 *ioc;
	int			  port;
	u32			  pad0;
	struct scsi_cmnd	**ScsiLookup;
	VirtTarget		**Targets;
	MPT_LOCAL_REPLY		 *pLocal;		/* used for internal commands */
	struct timer_list	  timer;
		/* Pool of memory for holding SCpnts before doing
		 * OS callbacks. freeQ is the free pool.
		 */
	u8			  tmPending;
	u8			  resetPending;
	u8			  negoNvram;		/* DV disabled, nego NVRAM */
	u8			  pad1;
	u8                        tmState;
	u8			  rsvd[2];
	MPT_FRAME_HDR		 *cmdPtr;		/* Ptr to nonOS request */
	struct scsi_cmnd	 *abortSCpnt;
	MPT_LOCAL_REPLY		  localReply;		/* internal cmd reply struct */
	unsigned long		  hard_resets;		/* driver forced bus resets count */
	unsigned long		  soft_resets;		/* fw/external bus resets count */
	unsigned long		  timeouts;		/* cmd timeouts */
	ushort			  sel_timeout[MPT_MAX_FC_DEVICES];
	char 			  *info_kbuf;
	wait_queue_head_t	  scandv_waitq;
	int			  scandv_wait_done;
	long			  last_queue_full;
	u8		 	  mpt_pq_filter;
} MPT_SCSI_HOST;

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
	struct list_head	 linkage;	/* linked list */
	struct timer_list	 timer;		/* timer function for this request  */
	union {
		ConfigExtendedPageHeader_t	*ehdr;
		ConfigPageHeader_t	*hdr;
	} cfghdr;
	dma_addr_t		 physAddr;
	int			 wait_done;	/* wait for this request */
	u32			 pageAddr;	/* properly formatted */
	u8			 action;
	u8			 dir;
	u8			 timeout;	/* seconds */
	u8			 pad1;
	u16			 status;
	u16			 pad2;
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
extern int	 mpt_register(MPT_CALLBACK cbfunc, MPT_DRIVER_CLASS dclass);
extern void	 mpt_deregister(int cb_idx);
extern int	 mpt_event_register(int cb_idx, MPT_EVHANDLER ev_cbfunc);
extern void	 mpt_event_deregister(int cb_idx);
extern int	 mpt_reset_register(int cb_idx, MPT_RESETHANDLER reset_func);
extern void	 mpt_reset_deregister(int cb_idx);
extern int	 mpt_device_driver_register(struct mpt_pci_driver * dd_cbfunc, int cb_idx);
extern void	 mpt_device_driver_deregister(int cb_idx);
extern MPT_FRAME_HDR	*mpt_get_msg_frame(int handle, MPT_ADAPTER *ioc);
extern void	 mpt_free_msg_frame(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf);
extern void	 mpt_put_msg_frame(int handle, MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf);
extern void	 mpt_add_sge(char *pAddr, u32 flagslength, dma_addr_t dma_addr);

extern int	 mpt_send_handshake_request(int handle, MPT_ADAPTER *ioc, int reqBytes, u32 *req, int sleepFlag);
extern int	 mpt_verify_adapter(int iocid, MPT_ADAPTER **iocpp);
extern u32	 mpt_GetIocState(MPT_ADAPTER *ioc, int cooked);
extern void	 mpt_print_ioc_summary(MPT_ADAPTER *ioc, char *buf, int *size, int len, int showlan);
extern int	 mpt_HardResetHandler(MPT_ADAPTER *ioc, int sleepFlag);
extern int	 mpt_config(MPT_ADAPTER *ioc, CONFIGPARMS *cfg);
extern int	 mpt_toolbox(MPT_ADAPTER *ioc, CONFIGPARMS *cfg);
extern void	 mpt_alloc_fw_memory(MPT_ADAPTER *ioc, int size);
extern void	 mpt_free_fw_memory(MPT_ADAPTER *ioc);
extern int	 mpt_findImVolumes(MPT_ADAPTER *ioc);
extern int	 mpt_read_ioc_pg_3(MPT_ADAPTER *ioc);
extern int	 mptbase_sas_persist_operation(MPT_ADAPTER *ioc, u8 persist_opcode);
extern int	 mptbase_GetFcPortPage0(MPT_ADAPTER *ioc, int portnum);
extern int	 mpt_alt_ioc_wait(MPT_ADAPTER *ioc);

/*
 *  Public data decl's...
 */
extern struct list_head	  ioc_list;
extern struct proc_dir_entry	*mpt_proc_root_dir;

extern int		  mpt_lan_index;	/* needed by mptlan.c */
extern int		  mpt_stm_index;	/* needed by mptstm.c */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif		/* } __KERNEL__ */

#if defined(__alpha__) || defined(__sparc_v9__) || defined(__ia64__) || defined(__x86_64__)
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
#define MPT_SGE_FLAGS_ADDRESSING		(mpt_addr_size() << MPI_SGE_FLAGS_SHIFT)
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
	 MPT_SGE_FLAGS_ADDRESSING | \
	 MPT_TRANSFER_IOC_TO_HOST)
#define MPT_SGE_FLAGS_SSIMPLE_WRITE \
	(MPT_SGE_FLAGS_LAST_ELEMENT |	\
	 MPT_SGE_FLAGS_END_OF_BUFFER |	\
	 MPT_SGE_FLAGS_END_OF_LIST |	\
	 MPT_SGE_FLAGS_SIMPLE_ELEMENT |	\
	 MPT_SGE_FLAGS_ADDRESSING | \
	 MPT_TRANSFER_HOST_TO_IOC)

/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif

