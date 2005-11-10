/* 
   3w-xxxx.h -- 3ware Storage Controller device driver for Linux.
   
   Written By: Adam Radford <linuxraid@amcc.com>
   Modifications By: Joel Jacobson <linux@3ware.com>
   		     Arnaldo Carvalho de Melo <acme@conectiva.com.br>
                     Brad Strand <linux@3ware.com>

   Copyright (C) 1999-2005 3ware Inc.

   Kernel compatiblity By:	Andre Hedrick <andre@suse.com>
   Non-Copyright (C) 2000	Andre Hedrick <andre@suse.com>

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

#ifndef _3W_XXXX_H
#define _3W_XXXX_H

#include <linux/types.h>

/* AEN strings */
static char *tw_aen_string[] = {
	[0x000] = "INFO: AEN queue empty",
	[0x001] = "INFO: Soft reset occurred",
	[0x002] = "ERROR: Unit degraded: Unit #",
	[0x003] = "ERROR: Controller error",
	[0x004] = "ERROR: Rebuild failed: Unit #",
	[0x005] = "INFO: Rebuild complete: Unit #",
	[0x006] = "ERROR: Incomplete unit detected: Unit #",
	[0x007] = "INFO: Initialization complete: Unit #",
	[0x008] = "WARNING: Unclean shutdown detected: Unit #",
	[0x009] = "WARNING: ATA port timeout: Port #",
	[0x00A] = "ERROR: Drive error: Port #",
	[0x00B] = "INFO: Rebuild started: Unit #",
	[0x00C] = "INFO: Initialization started: Unit #",
	[0x00D] = "ERROR: Logical unit deleted: Unit #",
	[0x00F] = "WARNING: SMART threshold exceeded: Port #",
	[0x021] = "WARNING: ATA UDMA downgrade: Port #",
	[0x021] = "WARNING: ATA UDMA upgrade: Port #",
	[0x023] = "WARNING: Sector repair occurred: Port #",
	[0x024] = "ERROR: SBUF integrity check failure",
	[0x025] = "ERROR: Lost cached write: Port #",
	[0x026] = "ERROR: Drive ECC error detected: Port #",
	[0x027] = "ERROR: DCB checksum error: Port #",
	[0x028] = "ERROR: DCB unsupported version: Port #",
	[0x029] = "INFO: Verify started: Unit #",
	[0x02A] = "ERROR: Verify failed: Port #",
	[0x02B] = "INFO: Verify complete: Unit #",
	[0x02C] = "WARNING: Overwrote bad sector during rebuild: Port #",
	[0x02D] = "ERROR: Encountered bad sector during rebuild: Port #",
	[0x02E] = "ERROR: Replacement drive is too small: Port #",
	[0x02F] = "WARNING: Verify error: Unit not previously initialized: Unit #",
	[0x030] = "ERROR: Drive not supported: Port #"
};

/*
   Sense key lookup table
   Format: ESDC/flags,SenseKey,AdditionalSenseCode,AdditionalSenseCodeQualifier
*/
static unsigned char tw_sense_table[][4] =
{
  /* Codes for newer firmware */
                            // ATA Error                    SCSI Error
  {0x01, 0x03, 0x13, 0x00}, // Address mark not found       Address mark not found for data field
  {0x04, 0x0b, 0x00, 0x00}, // Aborted command              Aborted command
  {0x10, 0x0b, 0x14, 0x00}, // ID not found                 Recorded entity not found
  {0x40, 0x03, 0x11, 0x00}, // Uncorrectable ECC error      Unrecovered read error
  {0x61, 0x04, 0x00, 0x00}, // Device fault                 Hardware error
  {0x84, 0x0b, 0x47, 0x00}, // Data CRC error               SCSI parity error
  {0xd0, 0x0b, 0x00, 0x00}, // Device busy                  Aborted command
  {0xd1, 0x0b, 0x00, 0x00}, // Device busy                  Aborted command
  {0x37, 0x02, 0x04, 0x00}, // Unit offline                 Not ready
  {0x09, 0x02, 0x04, 0x00}, // Unrecovered disk error       Not ready

  /* Codes for older firmware */
                            // 3ware Error                  SCSI Error
  {0x51, 0x0b, 0x00, 0x00}  // Unspecified                  Aborted command
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
#define TW_CONTROL_CLEAR_SBUF_WRITE_ERROR      0x00000008

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
#define TW_STATUS_ALL_INTERRUPTS	       0x000F0000
#define TW_STATUS_CLEARABLE_BITS	       0x00D00000
#define TW_STATUS_EXPECTED_BITS		       0x00002000
#define TW_STATUS_UNEXPECTED_BITS	       0x00F00008
#define TW_STATUS_SBUF_WRITE_ERROR             0x00000008
#define TW_STATUS_VALID_INTERRUPT              0x00DF0008

/* RESPONSE QUEUE BIT DEFINITIONS */
#define TW_RESPONSE_ID_MASK		       0x00000FF0

/* PCI related defines */
#define TW_IO_ADDRESS_RANGE		       0x10
#define TW_DEVICE_NAME			       "3ware Storage Controller"
#define TW_VENDOR_ID (0x13C1)	/* 3ware */
#define TW_DEVICE_ID (0x1000)	/* Storage Controller */
#define TW_DEVICE_ID2 (0x1001)  /* 7000 series controller */
#define TW_NUMDEVICES 2
#define TW_PCI_CLEAR_PARITY_ERRORS 0xc100
#define TW_PCI_CLEAR_PCI_ABORT     0x2000

/* Command packet opcodes */
#define TW_OP_NOP	      0x0
#define TW_OP_INIT_CONNECTION 0x1
#define TW_OP_READ	      0x2
#define TW_OP_WRITE	      0x3
#define TW_OP_VERIFY	      0x4
#define TW_OP_GET_PARAM	      0x12
#define TW_OP_SET_PARAM	      0x13
#define TW_OP_SECTOR_INFO     0x1a
#define TW_OP_AEN_LISTEN      0x1c
#define TW_OP_FLUSH_CACHE     0x0e
#define TW_CMD_PACKET         0x1d
#define TW_CMD_PACKET_WITH_DATA 0x1f

/* Asynchronous Event Notification (AEN) Codes */
#define TW_AEN_QUEUE_EMPTY       0x0000
#define TW_AEN_SOFT_RESET        0x0001
#define TW_AEN_DEGRADED_MIRROR   0x0002
#define TW_AEN_CONTROLLER_ERROR  0x0003
#define TW_AEN_REBUILD_FAIL      0x0004
#define TW_AEN_REBUILD_DONE      0x0005
#define TW_AEN_QUEUE_FULL        0x00ff
#define TW_AEN_TABLE_UNDEFINED   0x15
#define TW_AEN_APORT_TIMEOUT     0x0009
#define TW_AEN_DRIVE_ERROR       0x000A
#define TW_AEN_SMART_FAIL        0x000F
#define TW_AEN_SBUF_FAIL         0x0024

/* Phase defines */
#define TW_PHASE_INITIAL 0
#define TW_PHASE_SINGLE 1
#define TW_PHASE_SGLIST 2

/* Misc defines */
#define TW_ALIGNMENT_6000		      64 /* 64 bytes */
#define TW_ALIGNMENT_7000                     4  /* 4 bytes */
#define TW_MAX_UNITS			      16
#define TW_COMMAND_ALIGNMENT_MASK	      0x1ff
#define TW_INIT_MESSAGE_CREDITS		      0x100
#define TW_INIT_COMMAND_PACKET_SIZE	      0x3
#define TW_POLL_MAX_RETRIES        	      20000
#define TW_MAX_SGL_LENGTH		      62
#define TW_ATA_PASS_SGL_MAX                   60
#define TW_Q_LENGTH			      256
#define TW_Q_START			      0
#define TW_MAX_SLOT			      32
#define TW_MAX_PCI_BUSES		      255
#define TW_MAX_RESET_TRIES		      3
#define TW_UNIT_INFORMATION_TABLE_BASE	      0x300
#define TW_MAX_CMDS_PER_LUN		      254 /* 254 for io, 1 for
                                                     chrdev ioctl, one for
                                                     internal aen post */
#define TW_BLOCK_SIZE			      0x200 /* 512-byte blocks */
#define TW_IOCTL                              0x80
#define TW_UNIT_ONLINE                        1
#define TW_IN_INTR                            1
#define TW_IN_RESET                           2
#define TW_IN_CHRDEV_IOCTL                    3
#define TW_MAX_SECTORS                        256
#define TW_MAX_IOCTL_SECTORS		      512
#define TW_AEN_WAIT_TIME                      1000
#define TW_IOCTL_WAIT_TIME                    (1 * HZ) /* 1 second */
#define TW_ISR_DONT_COMPLETE                  2
#define TW_ISR_DONT_RESULT                    3
#define TW_IOCTL_TIMEOUT                      25 /* 25 seconds */
#define TW_IOCTL_CHRDEV_TIMEOUT               60 /* 60 seconds */
#define TW_IOCTL_CHRDEV_FREE                  -1
#define TW_DMA_MASK			      DMA_32BIT_MASK
#define TW_MAX_CDB_LEN			      16

/* Bitmask macros to eliminate bitfields */

/* opcode: 5, sgloffset: 3 */
#define TW_OPSGL_IN(x,y) ((x << 5) | (y & 0x1f))
#define TW_SGL_OUT(x) ((x >> 5) & 0x7)

/* reserved_1: 4, response_id: 8, reserved_2: 20 */
#define TW_RESID_OUT(x) ((x >> 4) & 0xff)

/* unit: 4, host_id: 4 */
#define TW_UNITHOST_IN(x,y) ((x << 4) | ( y & 0xf))
#define TW_UNIT_OUT(x) (x & 0xf)

/* Macros */
#define TW_CONTROL_REG_ADDR(x) (x->base_addr)
#define TW_STATUS_REG_ADDR(x) (x->base_addr + 0x4)
#define TW_COMMAND_QUEUE_REG_ADDR(x) (x->base_addr + 0x8)
#define TW_RESPONSE_QUEUE_REG_ADDR(x) (x->base_addr + 0xC)
#define TW_CLEAR_ALL_INTERRUPTS(x) (outl(TW_STATUS_VALID_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_CLEAR_ATTENTION_INTERRUPT(x) (outl(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_CLEAR_HOST_INTERRUPT(x) (outl(TW_CONTROL_CLEAR_HOST_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_DISABLE_INTERRUPTS(x) (outl(TW_CONTROL_DISABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_ENABLE_AND_CLEAR_INTERRUPTS(x) (outl(TW_CONTROL_CLEAR_ATTENTION_INTERRUPT | TW_CONTROL_UNMASK_RESPONSE_INTERRUPT | TW_CONTROL_ENABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_MASK_COMMAND_INTERRUPT(x) (outl(TW_CONTROL_MASK_COMMAND_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_UNMASK_COMMAND_INTERRUPT(x) (outl(TW_CONTROL_UNMASK_COMMAND_INTERRUPT, TW_CONTROL_REG_ADDR(x)))
#define TW_SOFT_RESET(x) (outl(TW_CONTROL_ISSUE_SOFT_RESET | \
			TW_CONTROL_CLEAR_HOST_INTERRUPT | \
			TW_CONTROL_CLEAR_ATTENTION_INTERRUPT | \
			TW_CONTROL_MASK_COMMAND_INTERRUPT | \
			TW_CONTROL_MASK_RESPONSE_INTERRUPT | \
			TW_CONTROL_CLEAR_ERROR_STATUS | \
			TW_CONTROL_DISABLE_INTERRUPTS, TW_CONTROL_REG_ADDR(x)))
#define TW_STATUS_ERRORS(x) \
	(((x & TW_STATUS_PCI_ABORT) || \
	(x & TW_STATUS_PCI_PARITY_ERROR) || \
	(x & TW_STATUS_QUEUE_ERROR) || \
	(x & TW_STATUS_MICROCONTROLLER_ERROR)) && \
	(x & TW_STATUS_MICROCONTROLLER_READY))

#ifdef TW_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

#pragma pack(1)

/* Scatter Gather List Entry */
typedef struct TAG_TW_SG_Entry {
	u32 address;
	u32 length;
} TW_SG_Entry;

typedef unsigned char TW_Sector[512];

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
		unsigned short message_credits;
	} byte6;
	union {
		struct {
			u32 lba;
			TW_SG_Entry sgl[TW_MAX_SGL_LENGTH];
			u32 padding;	/* pad to 512 bytes */
		} io;
		struct {
			TW_SG_Entry sgl[TW_MAX_SGL_LENGTH];
			u32 padding[2];
		} param;
		struct {
			u32 response_queue_pointer;
			u32 padding[125];
		} init_connection;
		struct {
			char version[504];
		} ioctl_miniport_version;
	} byte8;
} TW_Command;

#pragma pack()

typedef struct TAG_TW_Ioctl {
	unsigned char opcode;
	unsigned short table_id;
	unsigned char parameter_id;
	unsigned char parameter_size_bytes;
	unsigned char unit_index;
	unsigned char data[1];
} TW_Ioctl;

#pragma pack(1)

/* Structure for new chardev ioctls */
typedef struct TAG_TW_New_Ioctl {
	unsigned int data_buffer_length;
	unsigned char padding [508];
	TW_Command firmware_command;
	char data_buffer[1];
} TW_New_Ioctl;

/* GetParam descriptor */
typedef struct {
	unsigned short	table_id;
	unsigned char	parameter_id;
	unsigned char	parameter_size_bytes;
	unsigned char	data[1];
} TW_Param, *PTW_Param;

/* Response queue */
typedef union TAG_TW_Response_Queue {
	u32 response_id;
	u32 value;
} TW_Response_Queue;

typedef int TW_Cmd_State;

#define TW_S_INITIAL   0x1  /* Initial state */
#define TW_S_STARTED   0x2  /* Id in use */
#define TW_S_POSTED    0x4  /* Posted to the controller */
#define TW_S_PENDING   0x8  /* Waiting to be posted in isr */
#define TW_S_COMPLETED 0x10 /* Completed by isr */
#define TW_S_FINISHED  0x20 /* I/O completely done */
#define TW_START_MASK (TW_S_STARTED | TW_S_POSTED | TW_S_PENDING | TW_S_COMPLETED)

/* Command header for ATA pass-thru */
typedef struct TAG_TW_Passthru
{
	unsigned char opcode__sgloffset;
	unsigned char size;
	unsigned char request_id;
	unsigned char aport__hostid;
	unsigned char status;
	unsigned char flags;
	unsigned short param;
	unsigned short features;
	unsigned short sector_count;
	unsigned short sector_num;
	unsigned short cylinder_lo;
	unsigned short cylinder_hi;
	unsigned char drive_head;
	unsigned char command;
	TW_SG_Entry sg_list[TW_ATA_PASS_SGL_MAX];
	unsigned char padding[12];
} TW_Passthru;

typedef struct TAG_TW_Device_Extension {
	u32			base_addr;
	unsigned long		*alignment_virtual_address[TW_Q_LENGTH];
	unsigned long		alignment_physical_address[TW_Q_LENGTH];
	int			is_unit_present[TW_MAX_UNITS];
	unsigned long		*command_packet_virtual_address[TW_Q_LENGTH];
	unsigned long		command_packet_physical_address[TW_Q_LENGTH];
	struct pci_dev		*tw_pci_dev;
	struct scsi_cmnd	*srb[TW_Q_LENGTH];
	unsigned char		free_queue[TW_Q_LENGTH];
	unsigned char		free_head;
	unsigned char		free_tail;
	unsigned char		pending_queue[TW_Q_LENGTH];
	unsigned char		pending_head;
	unsigned char		pending_tail;
	TW_Cmd_State		state[TW_Q_LENGTH];
	u32			posted_request_count;
	u32			max_posted_request_count;
	u32			request_count_marked_pending;
	u32			pending_request_count;
	u32			max_pending_request_count;
	u32			max_sgl_entries;
	u32			sgl_entries;
	u32			num_resets;
	u32			sector_count;
	u32			max_sector_count;
	u32			aen_count;
	struct Scsi_Host	*host;
	struct semaphore	ioctl_sem;
	unsigned short		aen_queue[TW_Q_LENGTH];
	unsigned char		aen_head;
	unsigned char		aen_tail;
	volatile long		flags; /* long req'd for set_bit --RR */
	int			reset_print;
	volatile int		chrdev_request_id;
	wait_queue_head_t	ioctl_wqueue;
} TW_Device_Extension;

#pragma pack()

#endif /* _3W_XXXX_H */
