/*
   3w-9xxx.h -- 3ware 9000 Storage Controller device driver for Linux.

   Written By: Adam Radford <linuxraid@amcc.com>

   Copyright (C) 2004-2005 Applied Micro Circuits Corporation.

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

   Bugs/Comments/Suggestions should be mailed to:
   linuxraid@amcc.com

   For more information, goto:
   http://www.amcc.com
*/

#ifndef _3W_9XXX_H
#define _3W_9XXX_H

/* AEN string type */
typedef struct TAG_twa_message_type {
  unsigned int   code;
  char*          text;
} twa_message_type;

/* AEN strings */
static twa_message_type twa_aen_table[] = {
	{0x0000, "AEN queue empty"},
	{0x0001, "Controller reset occurred"},
	{0x0002, "Degraded unit detected"},
	{0x0003, "Controller error occured"},
	{0x0004, "Background rebuild failed"},
	{0x0005, "Background rebuild done"},
	{0x0006, "Incomplete unit detected"},
	{0x0007, "Background initialize done"},
	{0x0008, "Unclean shutdown detected"},
	{0x0009, "Drive timeout detected"},
	{0x000A, "Drive error detected"},
	{0x000B, "Rebuild started"},
	{0x000C, "Background initialize started"},
	{0x000D, "Entire logical unit was deleted"},
	{0x000E, "Background initialize failed"},
	{0x000F, "SMART attribute exceeded threshold"},
	{0x0010, "Power supply reported AC under range"},
	{0x0011, "Power supply reported DC out of range"},
	{0x0012, "Power supply reported a malfunction"},
	{0x0013, "Power supply predicted malfunction"},
	{0x0014, "Battery charge is below threshold"},
	{0x0015, "Fan speed is below threshold"},
	{0x0016, "Temperature sensor is above threshold"},
	{0x0017, "Power supply was removed"},
	{0x0018, "Power supply was inserted"},
	{0x0019, "Drive was removed from a bay"},
	{0x001A, "Drive was inserted into a bay"},
	{0x001B, "Drive bay cover door was opened"},
	{0x001C, "Drive bay cover door was closed"},
	{0x001D, "Product case was opened"},
	{0x0020, "Prepare for shutdown (power-off)"},
	{0x0021, "Downgrade UDMA mode to lower speed"},
	{0x0022, "Upgrade UDMA mode to higher speed"},
	{0x0023, "Sector repair completed"},
	{0x0024, "Sbuf memory test failed"},
	{0x0025, "Error flushing cached write data to array"},
	{0x0026, "Drive reported data ECC error"},
	{0x0027, "DCB has checksum error"},
	{0x0028, "DCB version is unsupported"},
	{0x0029, "Background verify started"},
	{0x002A, "Background verify failed"},
	{0x002B, "Background verify done"},
	{0x002C, "Bad sector overwritten during rebuild"},
	{0x002D, "Background rebuild error on source drive"},
	{0x002E, "Replace failed because replacement drive too small"},
	{0x002F, "Verify failed because array was never initialized"},
	{0x0030, "Unsupported ATA drive"},
	{0x0031, "Synchronize host/controller time"},
	{0x0032, "Spare capacity is inadequate for some units"},
	{0x0033, "Background migration started"},
	{0x0034, "Background migration failed"},
	{0x0035, "Background migration done"},
	{0x0036, "Verify detected and fixed data/parity mismatch"},
	{0x0037, "SO-DIMM incompatible"},
	{0x0038, "SO-DIMM not detected"},
	{0x0039, "Corrected Sbuf ECC error"},
	{0x003A, "Drive power on reset detected"},
	{0x003B, "Background rebuild paused"},
	{0x003C, "Background initialize paused"},
	{0x003D, "Background verify paused"},
	{0x003E, "Background migration paused"},
	{0x003F, "Corrupt flash file system detected"},
	{0x0040, "Flash file system repaired"},
	{0x0041, "Unit number assignments were lost"},
	{0x0042, "Error during read of primary DCB"},
	{0x0043, "Latent error found in backup DCB"},
	{0x00FC, "Recovered/finished array membership update"},
	{0x00FD, "Handler lockup"},
	{0x00FE, "Retrying PCI transfer"},
	{0x00FF, "AEN queue is full"},
	{0xFFFFFFFF, (char*) 0}
};

/* AEN severity table */
static char *twa_aen_severity_table[] =
{
	"None", "ERROR", "WARNING", "INFO", "DEBUG", (char*) 0
};

/* Error strings */
static twa_message_type twa_error_table[] = {
	{0x0100, "SGL entry contains zero data"},
	{0x0101, "Invalid command opcode"},
	{0x0102, "SGL entry has unaligned address"},
	{0x0103, "SGL size does not match command"},
	{0x0104, "SGL entry has illegal length"},
	{0x0105, "Command packet is not aligned"},
	{0x0106, "Invalid request ID"},
	{0x0107, "Duplicate request ID"},
	{0x0108, "ID not locked"},
	{0x0109, "LBA out of range"},
	{0x010A, "Logical unit not supported"},
	{0x010B, "Parameter table does not exist"},
	{0x010C, "Parameter index does not exist"},
	{0x010D, "Invalid field in CDB"},
	{0x010E, "Specified port has invalid drive"},
	{0x010F, "Parameter item size mismatch"},
	{0x0110, "Failed memory allocation"},
	{0x0111, "Memory request too large"},
	{0x0112, "Out of memory segments"},
	{0x0113, "Invalid address to deallocate"},
	{0x0114, "Out of memory"},
	{0x0115, "Out of heap"},
	{0x0120, "Double degrade"},
	{0x0121, "Drive not degraded"},
	{0x0122, "Reconstruct error"},
	{0x0123, "Replace not accepted"},
	{0x0124, "Replace drive capacity too small"},
	{0x0125, "Sector count not allowed"},
	{0x0126, "No spares left"},
	{0x0127, "Reconstruct error"},
	{0x0128, "Unit is offline"},
	{0x0129, "Cannot update status to DCB"},
	{0x0130, "Invalid stripe handle"},
	{0x0131, "Handle that was not locked"},
	{0x0132, "Handle that was not empty"},
	{0x0133, "Handle has different owner"},
	{0x0140, "IPR has parent"},
	{0x0150, "Illegal Pbuf address alignment"},
	{0x0151, "Illegal Pbuf transfer length"},
	{0x0152, "Illegal Sbuf address alignment"},
	{0x0153, "Illegal Sbuf transfer length"},
	{0x0160, "Command packet too large"},
	{0x0161, "SGL exceeds maximum length"},
	{0x0162, "SGL has too many entries"},
	{0x0170, "Insufficient resources for rebuilder"},
	{0x0171, "Verify error (data != parity)"},
	{0x0180, "Requested segment not in directory of this DCB"},
	{0x0181, "DCB segment has unsupported version"},
	{0x0182, "DCB segment has checksum error"},
	{0x0183, "DCB support (settings) segment invalid"},
	{0x0184, "DCB UDB (unit descriptor block) segment invalid"},
	{0x0185, "DCB GUID (globally unique identifier) segment invalid"},
	{0x01A0, "Could not clear Sbuf"},
	{0x01C0, "Flash identify failed"},
	{0x01C1, "Flash out of bounds"},
	{0x01C2, "Flash verify error"},
	{0x01C3, "Flash file object not found"},
	{0x01C4, "Flash file already present"},
	{0x01C5, "Flash file system full"},
	{0x01C6, "Flash file not present"},
	{0x01C7, "Flash file size error"},
	{0x01C8, "Bad flash file checksum"},
	{0x01CA, "Corrupt flash file system detected"},
	{0x01D0, "Invalid field in parameter list"},
	{0x01D1, "Parameter list length error"},
	{0x01D2, "Parameter item is not changeable"},
	{0x01D3, "Parameter item is not saveable"},
	{0x0200, "UDMA CRC error"},
	{0x0201, "Internal CRC error"},
	{0x0202, "Data ECC error"},
	{0x0203, "ADP level 1 error"},
	{0x0204, "Port timeout"},
	{0x0205, "Drive power on reset"},
	{0x0206, "ADP level 2 error"},
	{0x0207, "Soft reset failed"},
	{0x0208, "Drive not ready"},
	{0x0209, "Unclassified port error"},
	{0x020A, "Drive aborted command"},
	{0x0210, "Internal CRC error"},
	{0x0211, "PCI abort error"},
	{0x0212, "PCI parity error"},
	{0x0213, "Port handler error"},
	{0x0214, "Token interrupt count error"},
	{0x0215, "Timeout waiting for PCI transfer"},
	{0x0216, "Corrected buffer ECC"},
	{0x0217, "Uncorrected buffer ECC"},
	{0x0230, "Unsupported command during flash recovery"},
	{0x0231, "Next image buffer expected"},
	{0x0232, "Binary image architecture incompatible"},
	{0x0233, "Binary image has no signature"},
	{0x0234, "Binary image has bad checksum"},
	{0x0235, "Image downloaded overflowed buffer"},
	{0x0240, "I2C device not found"},
	{0x0241, "I2C transaction aborted"},
	{0x0242, "SO-DIMM parameter(s) incompatible using defaults"},
	{0x0243, "SO-DIMM unsupported"},
	{0x0248, "SPI transfer status error"},
	{0x0249, "SPI transfer timeout error"},
	{0x0250, "Invalid unit descriptor size in CreateUnit"},
	{0x0251, "Unit descriptor size exceeds data buffer in CreateUnit"},
	{0x0252, "Invalid value in CreateUnit descriptor"},
	{0x0253, "Inadequate disk space to support descriptor in CreateUnit"},
	{0x0254, "Unable to create data channel for this unit descriptor"},
	{0x0255, "CreateUnit descriptor specifies a drive already in use"},
	{0x0256, "Unable to write configuration to all disks during CreateUnit"},
	{0x0257, "CreateUnit does not support this descriptor version"},
	{0x0258, "Invalid subunit for RAID 0 or 5 in CreateUnit"},
	{0x0259, "Too many descriptors in CreateUnit"},
	{0x025A, "Invalid configuration specified in CreateUnit descriptor"},
	{0x025B, "Invalid LBA offset specified in CreateUnit descriptor"},
	{0x025C, "Invalid stripelet size specified in CreateUnit descriptor"},
	{0x0260, "SMART attribute exceeded threshold"},
	{0xFFFFFFFF, (char*) 0}
};

/* Control register bit definitions */
#define TW_CONTROL_CLEAR_HOST_INTERRUPT	       0x00080000
#define TW_CONTROL_CLEAR_ATTENTION_INTERRUPT   0x00040000
#define TW_CONTROL_MASK_COMMAND_INTERRUPT      0x00020000
#define TW_CONTROL_MASK_RESPONSE_INTERRUPT     0x00010000
#define TW_CONTROL_UNMASK_COMMAND_INTERRUPT    0x00008000
#define TW_CONTROL_UNMASK_RESPONSE_INTERRUPT   0x00004000
#define TW_CONTROL_CLEAR_ERROR_STATUS	       0x00000200
#define TW_CONTROL_ISSUE_SOFT_RESET	       0x00000100
#define TW_CONTROL_ENABLE_INTERRUPTS	       0x00000080
#define TW_CONTROL_DISABLE_INTERRUPTS	       0x00000040
#define TW_CONTROL_ISSUE_HOST_INTERRUPT	       0x00000020
#define TW_CONTROL_CLEAR_PARITY_ERROR          0x00800000
#define TW_CONTROL_CLEAR_QUEUE_ERROR           0x00400000
#define TW_CONTROL_CLEAR_PCI_ABORT             0x00100000

/* Status register bit definitions */
#define TW_STATUS_MAJOR_VERSION_MASK	       0xF0000000
#define TW_STATUS_MINOR_VERSION_MASK	       0x0F000000
#define TW_STATUS_PCI_PARITY_ERROR	       0x00800000
#define TW_STATUS_QUEUE_ERROR		       0x00400000
#define TW_STATUS_MICROCONTROLLER_ERROR	       0x00200000
#define TW_STATUS_PCI_ABORT		       0x00100000
#define TW_STATUS_HOST_INTERRUPT	       0x00080000
#define TW_STATUS_ATTENTION_INTERRUPT	       0x00040000
#define TW_STATUS_COMMAND_INTERRUPT	       0x00020000
#define TW_STATUS_RESPONSE_INTERRUPT	       0x00010000
#define TW_STATUS_COMMAND_QUEUE_FULL	       0x00008000
#define TW_STATUS_RESPONSE_QUEUE_EMPTY	       0x00004000
#define TW_STATUS_MICROCONTROLLER_READY	       0x00002000
#define TW_STATUS_COMMAND_QUEUE_EMPTY	       0x00001000
#define TW_STATUS_EXPECTED_BITS		       0x00002000
#define TW_STATUS_UNEXPECTED_BITS	       0x00F00000
#define TW_STATUS_VALID_INTERRUPT              0x00DF0000

/* RESPONSE QUEUE BIT DEFINITIONS */
#define TW_RESPONSE_ID_MASK		       0x00000FF0

/* PCI related defines */
#define TW_NUMDEVICES 1
#define TW_PCI_CLEAR_PARITY_ERRORS 0xc100
#define TW_PCI_CLEAR_PCI_ABORT     0x2000

/* Command packet opcodes used by the driver */
#define TW_OP_INIT_CONNECTION 0x1
#define TW_OP_GET_PARAM	      0x12
#define TW_OP_SET_PARAM	      0x13
#define TW_OP_EXECUTE_SCSI    0x10
#define TW_OP_DOWNLOAD_FIRMWARE 0x16
#define TW_OP_RESET             0x1C

/* Asynchronous Event Notification (AEN) codes used by the driver */
#define TW_AEN_QUEUE_EMPTY       0x0000
#define TW_AEN_SOFT_RESET        0x0001
#define TW_AEN_SYNC_TIME_WITH_HOST 0x031
#define TW_AEN_SEVERITY_ERROR    0x1
#define TW_AEN_SEVERITY_DEBUG    0x4
#define TW_AEN_NOT_RETRIEVED 0x1
#define TW_AEN_RETRIEVED 0x2

/* Command state defines */
#define TW_S_INITIAL   0x1  /* Initial state */
#define TW_S_STARTED   0x2  /* Id in use */
#define TW_S_POSTED    0x4  /* Posted to the controller */
#define TW_S_PENDING   0x8  /* Waiting to be posted in isr */
#define TW_S_COMPLETED 0x10 /* Completed by isr */
#define TW_S_FINISHED  0x20 /* I/O completely done */

/* Compatibility defines */
#define TW_9000_ARCH_ID 0x5
#define TW_CURRENT_DRIVER_SRL 30
#define TW_CURRENT_DRIVER_BUILD 80
#define TW_CURRENT_DRIVER_BRANCH 0

/* Phase defines */
#define TW_PHASE_INITIAL 0
#define TW_PHASE_SINGLE  1
#define TW_PHASE_SGLIST  2

/* Misc defines */
#define TW_9550SX_DRAIN_COMPLETED	      0xFFFF
#define TW_SECTOR_SIZE                        512
#define TW_ALIGNMENT_9000                     4  /* 4 bytes */
#define TW_ALIGNMENT_9000_SGL                 0x3
#define TW_MAX_UNITS			      16
#define TW_INIT_MESSAGE_CREDITS		      0x100
#define TW_INIT_COMMAND_PACKET_SIZE	      0x3
#define TW_INIT_COMMAND_PACKET_SIZE_EXTENDED  0x6
#define TW_EXTENDED_INIT_CONNECT	      0x2
#define TW_BUNDLED_FW_SAFE_TO_FLASH	      0x4
#define TW_CTLR_FW_RECOMMENDS_FLASH	      0x8
#define TW_CTLR_FW_COMPATIBLE		      0x2
#define TW_BASE_FW_SRL			      24
#define TW_BASE_FW_BRANCH		      0
#define TW_BASE_FW_BUILD		      1
#define TW_FW_SRL_LUNS_SUPPORTED              28
#define TW_Q_LENGTH			      256
#define TW_Q_START			      0
#define TW_MAX_SLOT			      32
#define TW_MAX_RESET_TRIES		      2
#define TW_MAX_CMDS_PER_LUN		      254
#define TW_MAX_RESPONSE_DRAIN		      256
#define TW_MAX_AEN_DRAIN		      40
#define TW_IN_RESET                           2
#define TW_IN_CHRDEV_IOCTL                    3
#define TW_IN_ATTENTION_LOOP		      4
#define TW_MAX_SECTORS                        256
#define TW_AEN_WAIT_TIME                      1000
#define TW_IOCTL_WAIT_TIME                    (1 * HZ) /* 1 second */
#define TW_MAX_CDB_LEN                        16
#define TW_ISR_DONT_COMPLETE                  2
#define TW_ISR_DONT_RESULT                    3
#define TW_IOCTL_CHRDEV_TIMEOUT               60 /* 60 seconds */
#define TW_IOCTL_CHRDEV_FREE                  -1
#define TW_COMMAND_OFFSET                     128 /* 128 bytes */
#define TW_VERSION_TABLE                      0x0402
#define TW_TIMEKEEP_TABLE		      0x040A
#define TW_INFORMATION_TABLE		      0x0403
#define TW_PARAM_FWVER			      3
#define TW_PARAM_FWVER_LENGTH		      16
#define TW_PARAM_BIOSVER		      4
#define TW_PARAM_BIOSVER_LENGTH		      16
#define TW_PARAM_PORTCOUNT		      3
#define TW_PARAM_PORTCOUNT_LENGTH	      1
#define TW_MIN_SGL_LENGTH                     0x200 /* 512 bytes */
#define TW_MAX_SENSE_LENGTH                   256
#define TW_EVENT_SOURCE_AEN                   0x1000
#define TW_EVENT_SOURCE_COMMAND               0x1001
#define TW_EVENT_SOURCE_PCHIP                 0x1002
#define TW_EVENT_SOURCE_DRIVER                0x1003
#define TW_IOCTL_GET_COMPATIBILITY_INFO	      0x101
#define TW_IOCTL_GET_LAST_EVENT               0x102
#define TW_IOCTL_GET_FIRST_EVENT              0x103
#define TW_IOCTL_GET_NEXT_EVENT               0x104
#define TW_IOCTL_GET_PREVIOUS_EVENT           0x105
#define TW_IOCTL_GET_LOCK                     0x106
#define TW_IOCTL_RELEASE_LOCK                 0x107
#define TW_IOCTL_FIRMWARE_PASS_THROUGH        0x108
#define TW_IOCTL_ERROR_STATUS_NOT_LOCKED      0x1001 // Not locked
#define TW_IOCTL_ERROR_STATUS_LOCKED          0x1002 // Already locked
#define TW_IOCTL_ERROR_STATUS_NO_MORE_EVENTS  0x1003 // No more events
#define TW_IOCTL_ERROR_STATUS_AEN_CLOBBER     0x1004 // AEN clobber occurred
#define TW_IOCTL_ERROR_OS_EFAULT	      -EFAULT // Bad address
#define TW_IOCTL_ERROR_OS_EINTR		      -EINTR  // Interrupted system call
#define TW_IOCTL_ERROR_OS_EINVAL	      -EINVAL // Invalid argument
#define TW_IOCTL_ERROR_OS_ENOMEM	      -ENOMEM // Out of memory
#define TW_IOCTL_ERROR_OS_ERESTARTSYS	      -ERESTARTSYS // Restart system call
#define TW_IOCTL_ERROR_OS_EIO		      -EIO // I/O error
#define TW_IOCTL_ERROR_OS_ENOTTY	      -ENOTTY // Not a typewriter
#define TW_IOCTL_ERROR_OS_ENODEV	      -ENODEV // No such device
#define TW_ALLOCATION_LENGTH		      128
#define TW_SENSE_DATA_LENGTH		      18
#define TW_STATUS_CHECK_CONDITION	      2
#define TW_ERROR_LOGICAL_UNIT_NOT_SUPPORTED   0x10a
#define TW_ERROR_UNIT_OFFLINE                 0x128
#define TW_MESSAGE_SOURCE_CONTROLLER_ERROR    3
#define TW_MESSAGE_SOURCE_CONTROLLER_EVENT    4
#define TW_MESSAGE_SOURCE_LINUX_DRIVER        6
#define TW_DRIVER TW_MESSAGE_SOURCE_LINUX_DRIVER
#define TW_MESSAGE_SOURCE_LINUX_OS            9
#define TW_OS TW_MESSAGE_SOURCE_LINUX_OS
#ifndef PCI_DEVICE_ID_3WARE_9000
#define PCI_DEVICE_ID_3WARE_9000 0x1002
#endif
#ifndef PCI_DEVICE_ID_3WARE_9550SX
#define PCI_DEVICE_ID_3WARE_9550SX 0x1003
#endif

/* Bitmask macros to eliminate bitfields */

/* opcode: 5, reserved: 3 */
#define TW_OPRES_IN(x,y) ((x << 5) | (y & 0x1f))
#define TW_OP_OUT(x) (x & 0x1f)

/* opcode: 5, sgloffset: 3 */
#define TW_OPSGL_IN(x,y) ((x << 5) | (y & 0x1f))
#define TW_SGL_OUT(x) ((x >> 5) & 0x7)

/* severity: 3, reserved: 5 */
#define TW_SEV_OUT(x) (x & 0x7)

/* reserved_1: 4, response_id: 8, reserved_2: 20 */
#define TW_RESID_OUT(x) ((x >> 4) & 0xff)

/* request_id: 12, lun: 4 */
#define TW_REQ_LUN_IN(lun, request_id) (((lun << 12) & 0xf000) | (request_id & 0xfff))
#define TW_LUN_OUT(lun) ((lun >> 12) & 0xf)

/* Macros */
#define TW_CONTROL_REG_ADDR(x) (x->base_addr)
#define TW_STATUS_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + 0x4)
#define TW_COMMAND_QUEUE_REG_ADDR(x) (sizeof(dma_addr_t) > 4 ? ((unsigned char __iomem *)x->base_addr + 0x20) : ((unsigned char __iomem *)x->base_addr + 0x8))
#define TW_RESPONSE_QUEUE_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + 0xC)
#define TW_RESPONSE_QUEUE_REG_ADDR_LARGE(x) ((unsigned char __iomem *)x->base_addr + 0x30)
#define TW_CLEAR_ALL_INTERRUPTS(x) (writel(TW_STATUS_VALID_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_CLEAR_ATTENTION_INTERRUPT(x) (writel(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_CLEAR_HOST_INTERRUPT(x) (writel(TW_CONTROL_CLEAR_HOST_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_DISABLE_INTERRUPTS(x) (writel(TW_CONTROL_DISABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_ENABLE_AND_CLEAR_INTERRUPTS(x) (writel(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT | TW_CONTROL_UNMASK_RESPONSE_INTERRUPT | TW_CONTROL_ENABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_MASK_COMMAND_INTERRUPT(x) (writel(TW_CONTROL_MASK_COMMAND_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_UNMASK_COMMAND_INTERRUPT(x) (writel(TW_CONTROL_UNMASK_COMMAND_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_SOFT_RESET(x) (writel(TW_CONTROL_ISSUE_SOFT_RESET | \
			TW_CONTROL_CLEAR_HOST_INTERRUPT | \
			TW_CONTROL_CLEAR_ATTENTION_INTERRUPT | \
			TW_CONTROL_MASK_COMMAND_INTERRUPT | \
			TW_CONTROL_MASK_RESPONSE_INTERRUPT | \
			TW_CONTROL_CLEAR_ERROR_STATUS | \
			TW_CONTROL_DISABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_PRINTK(h,a,b,c) { \
if (h) \
printk(KERN_WARNING "3w-9xxx: scsi%d: ERROR: (0x%02X:0x%04X): %s.\n",h->host_no,a,b,c); \
else \
printk(KERN_WARNING "3w-9xxx: ERROR: (0x%02X:0x%04X): %s.\n",a,b,c); \
}
#define TW_MAX_LUNS(srl) (srl < TW_FW_SRL_LUNS_SUPPORTED ? 1 : 16)
#define TW_COMMAND_SIZE (sizeof(dma_addr_t) > 4 ? 5 : 4)
#define TW_APACHE_MAX_SGL_LENGTH (sizeof(dma_addr_t) > 4 ? 72 : 109)
#define TW_ESCALADE_MAX_SGL_LENGTH (sizeof(dma_addr_t) > 4 ? 41 : 62)
#define TW_PADDING_LENGTH (sizeof(dma_addr_t) > 4 ? 8 : 0)

#pragma pack(1)

/* Scatter Gather List Entry */
typedef struct TAG_TW_SG_Entry {
	dma_addr_t address;
	u32 length;
} TW_SG_Entry;

/* Command Packet */
typedef struct TW_Command {
	unsigned char opcode__sgloffset;
	unsigned char size;
	unsigned char request_id;
	unsigned char unit__hostid;
	/* Second DWORD */
	unsigned char status;
	unsigned char flags;
	union {
		unsigned short block_count;
		unsigned short parameter_count;
	} byte6_offset;
	union {
		struct {
			u32 lba;
			TW_SG_Entry sgl[TW_ESCALADE_MAX_SGL_LENGTH];
			dma_addr_t padding;
		} io;
		struct {
			TW_SG_Entry sgl[TW_ESCALADE_MAX_SGL_LENGTH];
			u32 padding;
			dma_addr_t padding2;
		} param;
	} byte8_offset;
} TW_Command;

/* Command Packet for 9000+ controllers */
typedef struct TAG_TW_Command_Apache {
	unsigned char opcode__reserved;
	unsigned char unit;
	unsigned short request_id__lunl;
	unsigned char status;
	unsigned char sgl_offset;
	unsigned short sgl_entries__lunh;
	unsigned char cdb[16];
	TW_SG_Entry sg_list[TW_APACHE_MAX_SGL_LENGTH];
	unsigned char padding[TW_PADDING_LENGTH];
} TW_Command_Apache;

/* New command packet header */
typedef struct TAG_TW_Command_Apache_Header {
	unsigned char sense_data[TW_SENSE_DATA_LENGTH];
	struct {
		char reserved[4];
		unsigned short error;
		unsigned char padding;
		unsigned char severity__reserved;
	} status_block;
	unsigned char err_specific_desc[98];
	struct {
		unsigned char size_header;
		unsigned short reserved;
		unsigned char size_sense;
	} header_desc;
} TW_Command_Apache_Header;

/* This struct is a union of the 2 command packets */
typedef struct TAG_TW_Command_Full {
	TW_Command_Apache_Header header;
	union {
		TW_Command oldcommand;
		TW_Command_Apache newcommand;
	} command;
} TW_Command_Full;

/* Initconnection structure */
typedef struct TAG_TW_Initconnect {
	unsigned char opcode__reserved;
	unsigned char size;
	unsigned char request_id;
	unsigned char res2;
	unsigned char status;
	unsigned char flags;
	unsigned short message_credits;
	u32 features;
	unsigned short fw_srl;
	unsigned short fw_arch_id;
	unsigned short fw_branch;
	unsigned short fw_build;
	u32 result;
} TW_Initconnect;

/* Event info structure */
typedef struct TAG_TW_Event
{
	unsigned int sequence_id;
	unsigned int time_stamp_sec;
	unsigned short aen_code;
	unsigned char severity;
	unsigned char retrieved;
	unsigned char repeat_count;
	unsigned char parameter_len;
	unsigned char parameter_data[98];
} TW_Event;

typedef struct TAG_TW_Ioctl_Driver_Command {
	unsigned int control_code;
	unsigned int status;
	unsigned int unique_id;
	unsigned int sequence_id;
	unsigned int os_specific;
	unsigned int buffer_length;
} TW_Ioctl_Driver_Command;

typedef struct TAG_TW_Ioctl_Apache {
	TW_Ioctl_Driver_Command driver_command;
        char padding[488];
	TW_Command_Full firmware_command;
	char data_buffer[1];
} TW_Ioctl_Buf_Apache;

/* Lock structure for ioctl get/release lock */
typedef struct TAG_TW_Lock {
	unsigned long timeout_msec;
	unsigned long time_remaining_msec;
	unsigned long force_flag;
} TW_Lock;

/* GetParam descriptor */
typedef struct {
	unsigned short	table_id;
	unsigned short	parameter_id;
	unsigned short	parameter_size_bytes;
	unsigned short  actual_parameter_size_bytes;
	unsigned char	data[1];
} TW_Param_Apache, *PTW_Param_Apache;

/* Response queue */
typedef union TAG_TW_Response_Queue {
	u32 response_id;
	u32 value;
} TW_Response_Queue;

typedef struct TAG_TW_Info {
	char *buffer;
	int length;
	int offset;
	int position;
} TW_Info;

/* Compatibility information structure */
typedef struct TAG_TW_Compatibility_Info
{
	char driver_version[32];
	unsigned short working_srl;
	unsigned short working_branch;
	unsigned short working_build;
	unsigned short driver_srl_high;
	unsigned short driver_branch_high;
	unsigned short driver_build_high;
	unsigned short driver_srl_low;
	unsigned short driver_branch_low;
	unsigned short driver_build_low;
} TW_Compatibility_Info;

typedef struct TAG_TW_Device_Extension {
	u32                     __iomem *base_addr;
	unsigned long	       	*generic_buffer_virt[TW_Q_LENGTH];
	dma_addr_t	       	generic_buffer_phys[TW_Q_LENGTH];
	TW_Command_Full	       	*command_packet_virt[TW_Q_LENGTH];
	dma_addr_t		command_packet_phys[TW_Q_LENGTH];
	struct pci_dev		*tw_pci_dev;
	struct scsi_cmnd	*srb[TW_Q_LENGTH];
	unsigned char		free_queue[TW_Q_LENGTH];
	unsigned char		free_head;
	unsigned char		free_tail;
	unsigned char		pending_queue[TW_Q_LENGTH];
	unsigned char		pending_head;
	unsigned char		pending_tail;
	int     		state[TW_Q_LENGTH];
	unsigned int		posted_request_count;
	unsigned int		max_posted_request_count;
	unsigned int	        pending_request_count;
	unsigned int		max_pending_request_count;
	unsigned int		max_sgl_entries;
	unsigned int		sgl_entries;
	unsigned int		num_resets;
	unsigned int		sector_count;
	unsigned int		max_sector_count;
	unsigned int		aen_count;
	struct Scsi_Host	*host;
	long			flags;
	int			reset_print;
	TW_Event                *event_queue[TW_Q_LENGTH];
	unsigned char           error_index;
	unsigned char		event_queue_wrapped;
	unsigned int            error_sequence_id;
	int                     ioctl_sem_lock;
	u32                     ioctl_msec;
	int			chrdev_request_id;
	wait_queue_head_t	ioctl_wqueue;
	struct mutex		ioctl_lock;
	char			aen_clobber;
	unsigned short		working_srl;
	unsigned short		working_branch;
	unsigned short		working_build;
} TW_Device_Extension;

#pragma pack()

#endif /* _3W_9XXX_H */

