/*
   3w-sas.h -- LSI 3ware SAS/SATA-RAID Controller device driver for Linux.

   Written By: Adam Radford <linuxraid@lsi.com>

   Copyright (C) 2009 LSI Corporation.

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
   linuxraid@lsi.com

   For more information, goto:
   http://www.lsi.com
*/

#ifndef _3W_SAS_H
#define _3W_SAS_H

/* AEN severity table */
static char *twl_aen_severity_table[] =
{
	"None", "ERROR", "WARNING", "INFO", "DEBUG", NULL
};

/* Liberator register offsets */
#define TWL_STATUS                         0x0  /* Status */
#define TWL_HIBDB                          0x20 /* Inbound doorbell */
#define TWL_HISTAT                         0x30 /* Host interrupt status */
#define TWL_HIMASK                         0x34 /* Host interrupt mask */
#define TWL_HOBDB			   0x9C /* Outbound doorbell */
#define TWL_HOBDBC                         0xA0 /* Outbound doorbell clear */
#define TWL_SCRPD3                         0xBC /* Scratchpad */
#define TWL_HIBQPL                         0xC0 /* Host inbound Q low */
#define TWL_HIBQPH                         0xC4 /* Host inbound Q high */
#define TWL_HOBQPL                         0xC8 /* Host outbound Q low */
#define TWL_HOBQPH                         0xCC /* Host outbound Q high */
#define TWL_HISTATUS_VALID_INTERRUPT	   0xC
#define TWL_HISTATUS_ATTENTION_INTERRUPT   0x4
#define TWL_HISTATUS_RESPONSE_INTERRUPT	   0x8
#define TWL_STATUS_OVERRUN_SUBMIT	   0x2000
#define TWL_ISSUE_SOFT_RESET		   0x100
#define TWL_CONTROLLER_READY		   0x2000
#define TWL_DOORBELL_CONTROLLER_ERROR	   0x200000
#define TWL_DOORBELL_ATTENTION_INTERRUPT   0x40000
#define TWL_PULL_MODE			   0x1

/* Command packet opcodes used by the driver */
#define TW_OP_INIT_CONNECTION 0x1
#define TW_OP_GET_PARAM	      0x12
#define TW_OP_SET_PARAM	      0x13
#define TW_OP_EXECUTE_SCSI    0x10

/* Asynchronous Event Notification (AEN) codes used by the driver */
#define TW_AEN_QUEUE_EMPTY       0x0000
#define TW_AEN_SOFT_RESET        0x0001
#define TW_AEN_SYNC_TIME_WITH_HOST 0x031
#define TW_AEN_SEVERITY_ERROR    0x1
#define TW_AEN_SEVERITY_DEBUG    0x4
#define TW_AEN_NOT_RETRIEVED 0x1

/* Command state defines */
#define TW_S_INITIAL   0x1  /* Initial state */
#define TW_S_STARTED   0x2  /* Id in use */
#define TW_S_POSTED    0x4  /* Posted to the controller */
#define TW_S_COMPLETED 0x8  /* Completed by isr */
#define TW_S_FINISHED  0x10 /* I/O completely done */

/* Compatibility defines */
#define TW_9750_ARCH_ID 10
#define TW_CURRENT_DRIVER_SRL 40
#define TW_CURRENT_DRIVER_BUILD 0
#define TW_CURRENT_DRIVER_BRANCH 0

/* Misc defines */
#define TW_SECTOR_SIZE                        512
#define TW_MAX_UNITS			      32
#define TW_INIT_MESSAGE_CREDITS		      0x100
#define TW_INIT_COMMAND_PACKET_SIZE	      0x3
#define TW_INIT_COMMAND_PACKET_SIZE_EXTENDED  0x6
#define TW_EXTENDED_INIT_CONNECT	      0x2
#define TW_BASE_FW_SRL			      24
#define TW_BASE_FW_BRANCH		      0
#define TW_BASE_FW_BUILD		      1
#define TW_Q_LENGTH			      256
#define TW_Q_START			      0
#define TW_MAX_SLOT			      32
#define TW_MAX_RESET_TRIES		      2
#define TW_MAX_CMDS_PER_LUN		      254
#define TW_MAX_AEN_DRAIN		      255
#define TW_IN_RESET                           2
#define TW_USING_MSI			      3
#define TW_IN_ATTENTION_LOOP		      4
#define TW_MAX_SECTORS                        256
#define TW_MAX_CDB_LEN                        16
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
#define TW_PARAM_MODEL			      8
#define TW_PARAM_MODEL_LENGTH		      16
#define TW_PARAM_PHY_SUMMARY_TABLE	      1
#define TW_PARAM_PHYCOUNT		      2
#define TW_PARAM_PHYCOUNT_LENGTH	      1
#define TW_IOCTL_FIRMWARE_PASS_THROUGH        0x108  // Used by smartmontools
#define TW_ALLOCATION_LENGTH		      128
#define TW_SENSE_DATA_LENGTH		      18
#define TW_ERROR_LOGICAL_UNIT_NOT_SUPPORTED   0x10a
#define TW_ERROR_INVALID_FIELD_IN_CDB	      0x10d
#define TW_ERROR_UNIT_OFFLINE                 0x128
#define TW_MESSAGE_SOURCE_CONTROLLER_ERROR    3
#define TW_MESSAGE_SOURCE_CONTROLLER_EVENT    4
#define TW_DRIVER 			      6
#ifndef PCI_DEVICE_ID_3WARE_9750
#define PCI_DEVICE_ID_3WARE_9750 0x1010
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

/* not_mfa: 1, reserved: 7, status: 8, request_id: 16 */
#define TW_RESID_OUT(x) ((x >> 16) & 0xffff)
#define TW_NOTMFA_OUT(x) (x & 0x1)

/* request_id: 12, lun: 4 */
#define TW_REQ_LUN_IN(lun, request_id) (((lun << 12) & 0xf000) | (request_id & 0xfff))
#define TW_LUN_OUT(lun) ((lun >> 12) & 0xf)

/* Register access macros */
#define TWL_STATUS_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_STATUS)
#define TWL_HOBQPL_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HOBQPL)
#define TWL_HOBQPH_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HOBQPH)
#define TWL_HOBDB_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HOBDB)
#define TWL_HOBDBC_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HOBDBC)
#define TWL_HIMASK_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HIMASK)
#define TWL_HISTAT_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HISTAT)
#define TWL_HIBQPH_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HIBQPH)
#define TWL_HIBQPL_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HIBQPL)
#define TWL_HIBDB_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_HIBDB)
#define TWL_SCRPD3_REG_ADDR(x) ((unsigned char __iomem *)x->base_addr + TWL_SCRPD3)
#define TWL_MASK_INTERRUPTS(x) (writel(~0, TWL_HIMASK_REG_ADDR(tw_dev)))
#define TWL_UNMASK_INTERRUPTS(x) (writel(~TWL_HISTATUS_VALID_INTERRUPT, TWL_HIMASK_REG_ADDR(tw_dev)))
#define TWL_CLEAR_DB_INTERRUPT(x) (writel(~0, TWL_HOBDBC_REG_ADDR(tw_dev)))
#define TWL_SOFT_RESET(x) (writel(TWL_ISSUE_SOFT_RESET, TWL_HIBDB_REG_ADDR(tw_dev)))

/* Macros */
#define TW_PRINTK(h,a,b,c) { \
if (h) \
printk(KERN_WARNING "3w-sas: scsi%d: ERROR: (0x%02X:0x%04X): %s.\n",h->host_no,a,b,c); \
else \
printk(KERN_WARNING "3w-sas: ERROR: (0x%02X:0x%04X): %s.\n",a,b,c); \
}
#define TW_MAX_LUNS 16
#define TW_COMMAND_SIZE (sizeof(dma_addr_t) > 4 ? 6 : 4)
#define TW_LIBERATOR_MAX_SGL_LENGTH (sizeof(dma_addr_t) > 4 ? 46 : 92)
#define TW_LIBERATOR_MAX_SGL_LENGTH_OLD (sizeof(dma_addr_t) > 4 ? 47 : 94)
#define TW_PADDING_LENGTH_LIBERATOR 136
#define TW_PADDING_LENGTH_LIBERATOR_OLD 132
#define TW_CPU_TO_SGL(x) (sizeof(dma_addr_t) > 4 ? cpu_to_le64(x) : cpu_to_le32(x))

#pragma pack(1)

/* SGL entry */
typedef struct TAG_TW_SG_Entry_ISO {
	dma_addr_t address;
	dma_addr_t length;
} TW_SG_Entry_ISO;

/* Old Command Packet with ISO SGL */
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
			TW_SG_Entry_ISO sgl[TW_LIBERATOR_MAX_SGL_LENGTH_OLD];
			unsigned char padding[TW_PADDING_LENGTH_LIBERATOR_OLD];
		} io;
		struct {
			TW_SG_Entry_ISO sgl[TW_LIBERATOR_MAX_SGL_LENGTH_OLD];
			u32 padding;
			unsigned char padding2[TW_PADDING_LENGTH_LIBERATOR_OLD];
		} param;
	} byte8_offset;
} TW_Command;

/* New Command Packet with ISO SGL */
typedef struct TAG_TW_Command_Apache {
	unsigned char opcode__reserved;
	unsigned char unit;
	unsigned short request_id__lunl;
	unsigned char status;
	unsigned char sgl_offset;
	unsigned short sgl_entries__lunh;
	unsigned char cdb[16];
	TW_SG_Entry_ISO sg_list[TW_LIBERATOR_MAX_SGL_LENGTH];
	unsigned char padding[TW_PADDING_LENGTH_LIBERATOR];
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
		unsigned short request_id;
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

/* GetParam descriptor */
typedef struct {
	unsigned short	table_id;
	unsigned short	parameter_id;
	unsigned short	parameter_size_bytes;
	unsigned short  actual_parameter_size_bytes;
	unsigned char	data[1];
} TW_Param_Apache;

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
	unsigned short fw_on_ctlr_srl;
	unsigned short fw_on_ctlr_branch;
	unsigned short fw_on_ctlr_build;
} TW_Compatibility_Info;

#pragma pack()

typedef struct TAG_TW_Device_Extension {
	void                     __iomem *base_addr;
	unsigned long	       	*generic_buffer_virt[TW_Q_LENGTH];
	dma_addr_t	       	generic_buffer_phys[TW_Q_LENGTH];
	TW_Command_Full	       	*command_packet_virt[TW_Q_LENGTH];
	dma_addr_t		command_packet_phys[TW_Q_LENGTH];
	TW_Command_Apache_Header *sense_buffer_virt[TW_Q_LENGTH];
	dma_addr_t		sense_buffer_phys[TW_Q_LENGTH];
	struct pci_dev		*tw_pci_dev;
	struct scsi_cmnd	*srb[TW_Q_LENGTH];
	unsigned char		free_queue[TW_Q_LENGTH];
	unsigned char		free_head;
	unsigned char		free_tail;
	int     		state[TW_Q_LENGTH];
	unsigned int		posted_request_count;
	unsigned int		max_posted_request_count;
	unsigned int		max_sgl_entries;
	unsigned int		sgl_entries;
	unsigned int		num_resets;
	unsigned int		sector_count;
	unsigned int		max_sector_count;
	unsigned int		aen_count;
	struct Scsi_Host	*host;
	long			flags;
	TW_Event                *event_queue[TW_Q_LENGTH];
	unsigned char           error_index;
	unsigned int            error_sequence_id;
	int			chrdev_request_id;
	wait_queue_head_t	ioctl_wqueue;
	struct mutex		ioctl_lock;
	TW_Compatibility_Info	tw_compat_info;
	char			online;
} TW_Device_Extension;

#endif /* _3W_SAS_H */

