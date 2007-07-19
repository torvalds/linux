/*
 *  linux/drivers/message/fusion/mptioctl.h
 *      Fusion MPT misc device (ioctl) driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2007 LSI Logic Corporation
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

#ifndef MPTCTL_H_INCLUDED
#define MPTCTL_H_INCLUDED
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/



/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *
 */
#define MPT_MISCDEV_BASENAME            "mptctl"
#define MPT_MISCDEV_PATHNAME            "/dev/" MPT_MISCDEV_BASENAME

#define MPT_PRODUCT_LENGTH              12

/*
 *  Generic MPT Control IOCTLs and structures
 */
#define MPT_MAGIC_NUMBER	'm'

#define MPTRWPERF		_IOWR(MPT_MAGIC_NUMBER,0,struct mpt_raw_r_w)

#define MPTFWDOWNLOAD		_IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer)
#define MPTCOMMAND		_IOWR(MPT_MAGIC_NUMBER,20,struct mpt_ioctl_command)

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
#define MPTFWDOWNLOAD32		_IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer32)
#define MPTCOMMAND32		_IOWR(MPT_MAGIC_NUMBER,20,struct mpt_ioctl_command32)
#endif

#define MPTIOCINFO		_IOWR(MPT_MAGIC_NUMBER,17,struct mpt_ioctl_iocinfo)
#define MPTIOCINFO1		_IOWR(MPT_MAGIC_NUMBER,17,struct mpt_ioctl_iocinfo_rev0)
#define MPTIOCINFO2		_IOWR(MPT_MAGIC_NUMBER,17,struct mpt_ioctl_iocinfo_rev1)
#define MPTTARGETINFO		_IOWR(MPT_MAGIC_NUMBER,18,struct mpt_ioctl_targetinfo)
#define MPTTEST			_IOWR(MPT_MAGIC_NUMBER,19,struct mpt_ioctl_test)
#define MPTEVENTQUERY		_IOWR(MPT_MAGIC_NUMBER,21,struct mpt_ioctl_eventquery)
#define MPTEVENTENABLE		_IOWR(MPT_MAGIC_NUMBER,22,struct mpt_ioctl_eventenable)
#define MPTEVENTREPORT		_IOWR(MPT_MAGIC_NUMBER,23,struct mpt_ioctl_eventreport)
#define MPTHARDRESET		_IOWR(MPT_MAGIC_NUMBER,24,struct mpt_ioctl_diag_reset)
#define MPTFWREPLACE		_IOWR(MPT_MAGIC_NUMBER,25,struct mpt_ioctl_replace_fw)

/*
 * SPARC PLATFORM REMARKS:
 * IOCTL data structures that contain pointers
 * will have different sizes in the driver and applications
 * (as the app. will not use 8-byte pointers).
 * Apps should use MPTFWDOWNLOAD and MPTCOMMAND.
 * The driver will convert data from
 * mpt_fw_xfer32 (mpt_ioctl_command32) to mpt_fw_xfer (mpt_ioctl_command)
 * internally.
 *
 * If data structures change size, must handle as in IOCGETINFO.
 */
struct mpt_fw_xfer {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 fwlen;
	void		__user *bufp;	/* Pointer to firmware buffer */
};

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
struct mpt_fw_xfer32 {
	unsigned int iocnum;
	unsigned int fwlen;
	u32 bufp;
};
#endif	/*}*/

/*
 *  IOCTL header structure.
 *  iocnum - must be defined.
 *  port - must be defined for all IOCTL commands other than MPTIOCINFO
 *  maxDataSize - ignored on MPTCOMMAND commands
 *		- ignored on MPTFWREPLACE commands
 *		- on query commands, reports the maximum number of bytes to be returned
 *		  to the host driver (count includes the header).
 *		  That is, set to sizeof(struct mpt_ioctl_iocinfo) for fixed sized commands.
 *		  Set to sizeof(struct mpt_ioctl_targetinfo) + datasize for variable
 *			sized commands. (MPTTARGETINFO, MPTEVENTREPORT)
 */
typedef struct _mpt_ioctl_header {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 port;		/* IOC port number */
	int		 maxDataSize;	/* Maximum Num. bytes to transfer on read */
} mpt_ioctl_header;

/*
 * Issue a diagnostic reset
 */
struct mpt_ioctl_diag_reset {
	mpt_ioctl_header hdr;
};


/*
 *  PCI bus/device/function information structure.
 */
struct mpt_ioctl_pci_info {
	union {
		struct {
			unsigned int  deviceNumber   :  5;
			unsigned int  functionNumber :  3;
			unsigned int  busNumber      : 24;
		} bits;
		unsigned int  asUlong;
	} u;
};

struct mpt_ioctl_pci_info2 {
	union {
		struct {
			unsigned int  deviceNumber   :  5;
			unsigned int  functionNumber :  3;
			unsigned int  busNumber      : 24;
		} bits;
		unsigned int  asUlong;
	} u;
  int segmentID;
};

/*
 *  Adapter Information Page
 *  Read only.
 *  Data starts at offset 0xC
 */
#define MPT_IOCTL_INTERFACE_SCSI	(0x00)
#define MPT_IOCTL_INTERFACE_FC		(0x01)
#define MPT_IOCTL_INTERFACE_FC_IP	(0x02)
#define MPT_IOCTL_INTERFACE_SAS		(0x03)
#define MPT_IOCTL_VERSION_LENGTH	(32)

struct mpt_ioctl_iocinfo {
	mpt_ioctl_header hdr;
	int		 adapterType;	/* SCSI or FCP */
	int		 port;		/* port number */
	int		 pciId;		/* PCI Id. */
	int		 hwRev;		/* hardware revision */
	int		 subSystemDevice;	/* PCI subsystem Device ID */
	int		 subSystemVendor;	/* PCI subsystem Vendor ID */
	int		 numDevices;		/* number of devices */
	int		 FWVersion;		/* FW Version (integer) */
	int		 BIOSVersion;		/* BIOS Version (integer) */
	char		 driverVersion[MPT_IOCTL_VERSION_LENGTH];	/* Driver Version (string) */
	char		 busChangeEvent;
	char		 hostId;
	char		 rsvd[2];
	struct mpt_ioctl_pci_info2  pciInfo; /* Added Rev 2 */
};

struct mpt_ioctl_iocinfo_rev1 {
	mpt_ioctl_header hdr;
	int		 adapterType;	/* SCSI or FCP */
	int		 port;		/* port number */
	int		 pciId;		/* PCI Id. */
	int		 hwRev;		/* hardware revision */
	int		 subSystemDevice;	/* PCI subsystem Device ID */
	int		 subSystemVendor;	/* PCI subsystem Vendor ID */
	int		 numDevices;		/* number of devices */
	int		 FWVersion;		/* FW Version (integer) */
	int		 BIOSVersion;		/* BIOS Version (integer) */
	char		 driverVersion[MPT_IOCTL_VERSION_LENGTH];	/* Driver Version (string) */
	char		 busChangeEvent;
	char		 hostId;
	char		 rsvd[2];
	struct mpt_ioctl_pci_info  pciInfo; /* Added Rev 1 */
};

/* Original structure, must always accept these
 * IOCTLs. 4 byte pads can occur based on arch with
 * above structure. Wish to re-align, but cannot.
 */
struct mpt_ioctl_iocinfo_rev0 {
	mpt_ioctl_header hdr;
	int		 adapterType;	/* SCSI or FCP */
	int		 port;		/* port number */
	int		 pciId;		/* PCI Id. */
	int		 hwRev;		/* hardware revision */
	int		 subSystemDevice;	/* PCI subsystem Device ID */
	int		 subSystemVendor;	/* PCI subsystem Vendor ID */
	int		 numDevices;		/* number of devices */
	int		 FWVersion;		/* FW Version (integer) */
	int		 BIOSVersion;		/* BIOS Version (integer) */
	char		 driverVersion[MPT_IOCTL_VERSION_LENGTH];	/* Driver Version (string) */
	char		 busChangeEvent;
	char		 hostId;
	char		 rsvd[2];
};

/*
 * Device Information Page
 * Report the number of, and ids of, all targets
 * on this IOC.  The ids array is a packed structure
 * of the known targetInfo.
 * bits 31-24: reserved
 *      23-16: LUN
 *      15- 8: Bus Number
 *       7- 0: Target ID
 */
struct mpt_ioctl_targetinfo {
	mpt_ioctl_header hdr;
	int		 numDevices;	/* Num targets on this ioc */
	int		 targetInfo[1];
};


/*
 * Event reporting IOCTL's.  These IOCTL's will
 * use the following defines:
 */
struct mpt_ioctl_eventquery {
	mpt_ioctl_header hdr;
	unsigned short	 eventEntries;
	unsigned short	 reserved;
	unsigned int	 eventTypes;
};

struct mpt_ioctl_eventenable {
	mpt_ioctl_header hdr;
	unsigned int	 eventTypes;
};

#ifndef __KERNEL__
typedef struct {
	uint	event;
	uint	eventContext;
	uint	data[2];
} MPT_IOCTL_EVENTS;
#endif

struct mpt_ioctl_eventreport {
	mpt_ioctl_header	hdr;
	MPT_IOCTL_EVENTS	eventData[1];
};

#define MPT_MAX_NAME	32
struct mpt_ioctl_test {
	mpt_ioctl_header hdr;
	u8		 name[MPT_MAX_NAME];
	int		 chip_type;
	u8		 product [MPT_PRODUCT_LENGTH];
};

/* Replace the FW image cached in host driver memory
 * newImageSize - image size in bytes
 * newImage - first byte of the new image
 */
typedef struct mpt_ioctl_replace_fw {
	mpt_ioctl_header hdr;
	int		 newImageSize;
	u8		 newImage[1];
} mpt_ioctl_replace_fw_t;

/* General MPT Pass through data strucutre
 *
 * iocnum
 * timeout - in seconds, command timeout. If 0, set by driver to
 *		default value.
 * replyFrameBufPtr - reply location
 * dataInBufPtr - destination for read
 * dataOutBufPtr - data source for write
 * senseDataPtr - sense data location
 * maxReplyBytes - maximum number of reply bytes to be sent to app.
 * dataInSize - num bytes for data transfer in (read)
 * dataOutSize - num bytes for data transfer out (write)
 * dataSgeOffset - offset in words from the start of the request message
 *		to the first SGL
 * MF[1];
 *
 * Remark:  Some config pages have bi-directional transfer,
 * both a read and a write. The basic structure allows for
 * a bidirectional set up. Normal messages will have one or
 * both of these buffers NULL.
 */
struct mpt_ioctl_command {
	mpt_ioctl_header hdr;
	int		timeout;	/* optional (seconds) */
	char		__user *replyFrameBufPtr;
	char		__user *dataInBufPtr;
	char		__user *dataOutBufPtr;
	char		__user *senseDataPtr;
	int		maxReplyBytes;
	int		dataInSize;
	int		dataOutSize;
	int		maxSenseBytes;
	int		dataSgeOffset;
	char		MF[1];
};

/*
 * SPARC PLATFORM: See earlier remark.
 */
#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
struct mpt_ioctl_command32 {
	mpt_ioctl_header hdr;
	int	timeout;
	u32	replyFrameBufPtr;
	u32	dataInBufPtr;
	u32	dataOutBufPtr;
	u32	senseDataPtr;
	int	maxReplyBytes;
	int	dataInSize;
	int	dataOutSize;
	int	maxSenseBytes;
	int	dataSgeOffset;
	char	MF[1];
};
#endif	/*}*/


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#define CPQFCTS_IOC_MAGIC 'Z'
#define HP_IOC_MAGIC 'Z'
#define HP_GETHOSTINFO		_IOR(HP_IOC_MAGIC, 20, hp_host_info_t)
#define HP_GETHOSTINFO1		_IOR(HP_IOC_MAGIC, 20, hp_host_info_rev0_t)
#define HP_GETTARGETINFO	_IOR(HP_IOC_MAGIC, 21, hp_target_info_t)

typedef struct _hp_header {
	unsigned int iocnum;
	unsigned int host;
	unsigned int channel;
	unsigned int id;
	unsigned int lun;
} hp_header_t;

/*
 *  Header:
 *  iocnum 	required (input)
 *  host 	ignored
 *  channe	ignored
 *  id		ignored
 *  lun		ignored
 */
typedef struct _hp_host_info {
	hp_header_t	 hdr;
	u16		 vendor;
	u16		 device;
	u16		 subsystem_vendor;
	u16		 subsystem_id;
	u8		 devfn;
	u8		 bus;
	ushort		 host_no;		/* SCSI Host number, if scsi driver not loaded*/
	u8		 fw_version[16];	/* string */
	u8		 serial_number[24];	/* string */
	u32		 ioc_status;
	u32		 bus_phys_width;
	u32		 base_io_addr;
	u32		 rsvd;
	unsigned int	 hard_resets;		/* driver initiated resets */
	unsigned int	 soft_resets;		/* ioc, external resets */
	unsigned int	 timeouts;		/* num timeouts */
} hp_host_info_t;

/* replace ulongs with uints, need to preserve backwards
 * compatibility.
 */
typedef struct _hp_host_info_rev0 {
	hp_header_t	 hdr;
	u16		 vendor;
	u16		 device;
	u16		 subsystem_vendor;
	u16		 subsystem_id;
	u8		 devfn;
	u8		 bus;
	ushort		 host_no;		/* SCSI Host number, if scsi driver not loaded*/
	u8		 fw_version[16];	/* string */
	u8		 serial_number[24];	/* string */
	u32		 ioc_status;
	u32		 bus_phys_width;
	u32		 base_io_addr;
	u32		 rsvd;
	unsigned long	 hard_resets;		/* driver initiated resets */
	unsigned long	 soft_resets;		/* ioc, external resets */
	unsigned long	 timeouts;		/* num timeouts */
} hp_host_info_rev0_t;

/*
 *  Header:
 *  iocnum 	required (input)
 *  host 	required
 *  channel	required	(bus number)
 *  id		required
 *  lun		ignored
 *
 *  All error values between 0 and 0xFFFF in size.
 */
typedef struct _hp_target_info {
	hp_header_t	 hdr;
	u32 parity_errors;
	u32 phase_errors;
	u32 select_timeouts;
	u32 message_rejects;
	u32 negotiated_speed;
	u8  negotiated_width;
	u8  rsvd[7];				/* 8 byte alignment */
} hp_target_info_t;

#define HP_STATUS_OTHER		1
#define HP_STATUS_OK		2
#define HP_STATUS_FAILED	3

#define HP_BUS_WIDTH_UNK	1
#define HP_BUS_WIDTH_8		2
#define HP_BUS_WIDTH_16		3
#define HP_BUS_WIDTH_32		4

#define HP_DEV_SPEED_ASYNC	2
#define HP_DEV_SPEED_FAST	3
#define HP_DEV_SPEED_ULTRA	4
#define HP_DEV_SPEED_ULTRA2	5
#define HP_DEV_SPEED_ULTRA160	6
#define HP_DEV_SPEED_SCSI1	7
#define HP_DEV_SPEED_ULTRA320	8

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#endif

