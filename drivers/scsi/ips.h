/*****************************************************************************/
/* ips.h -- driver for the Adaptec / IBM ServeRAID controller                */
/*                                                                           */
/* Written By: Keith Mitchell, IBM Corporation                               */
/*             Jack Hammer, Adaptec, Inc.                                    */
/*             David Jeffery, Adaptec, Inc.                                  */
/*                                                                           */
/* Copyright (C) 1999 IBM Corporation                                        */
/* Copyright (C) 2003 Adaptec, Inc.                                          */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* NO WARRANTY                                                               */
/* THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR        */
/* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT      */
/* LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,      */
/* MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is    */
/* solely responsible for determining the appropriateness of using and       */
/* distributing the Program and assumes all risks associated with its        */
/* exercise of rights under this Agreement, including but not limited to     */
/* the risks and costs of program errors, damage to or loss of data,         */
/* programs or equipment, and unavailability or interruption of operations.  */
/*                                                                           */
/* DISCLAIMER OF LIABILITY                                                   */
/* NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY   */
/* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL        */
/* DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND   */
/* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR     */
/* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE    */
/* USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED  */
/* HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES             */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/* Bugs/Comments/Suggestions should be mailed to:                            */
/*      ipslinux@adaptec.com                                                 */
/*                                                                           */
/*****************************************************************************/

#ifndef _IPS_H_
   #define _IPS_H_

#include <linux/nmi.h>
   #include <asm/uaccess.h>
   #include <asm/io.h>

   /*
    * Some handy macros
    */
   #define IPS_HA(x)                   ((ips_ha_t *) x->hostdata)
   #define IPS_COMMAND_ID(ha, scb)     (int) (scb - ha->scbs)
   #define IPS_IS_TROMBONE(ha)         (((ha->pcidev->device == IPS_DEVICEID_COPPERHEAD) && \
                                         (ha->pcidev->revision >= IPS_REVID_TROMBONE32) && \
                                         (ha->pcidev->revision <= IPS_REVID_TROMBONE64)) ? 1 : 0)
   #define IPS_IS_CLARINET(ha)         (((ha->pcidev->device == IPS_DEVICEID_COPPERHEAD) && \
                                         (ha->pcidev->revision >= IPS_REVID_CLARINETP1) && \
                                         (ha->pcidev->revision <= IPS_REVID_CLARINETP3)) ? 1 : 0)
   #define IPS_IS_MORPHEUS(ha)         (ha->pcidev->device == IPS_DEVICEID_MORPHEUS)
   #define IPS_IS_MARCO(ha)            (ha->pcidev->device == IPS_DEVICEID_MARCO)
   #define IPS_USE_I2O_DELIVER(ha)     ((IPS_IS_MORPHEUS(ha) || \
                                         (IPS_IS_TROMBONE(ha) && \
                                          (ips_force_i2o))) ? 1 : 0)
   #define IPS_USE_MEMIO(ha)           ((IPS_IS_MORPHEUS(ha) || \
                                         ((IPS_IS_TROMBONE(ha) || IPS_IS_CLARINET(ha)) && \
                                          (ips_force_memio))) ? 1 : 0)

    #define IPS_HAS_ENH_SGLIST(ha)    (IPS_IS_MORPHEUS(ha) || IPS_IS_MARCO(ha))
    #define IPS_USE_ENH_SGLIST(ha)    ((ha)->flags & IPS_HA_ENH_SG)
    #define IPS_SGLIST_SIZE(ha)       (IPS_USE_ENH_SGLIST(ha) ? \
                                         sizeof(IPS_ENH_SG_LIST) : sizeof(IPS_STD_SG_LIST))

  #define IPS_PRINTK(level, pcidev, format, arg...)                 \
            dev_printk(level , &((pcidev)->dev) , format , ## arg)

   #define MDELAY(n)			\
	do {				\
		mdelay(n);		\
		touch_nmi_watchdog();	\
	} while (0)

   #ifndef min
      #define min(x,y) ((x) < (y) ? x : y)
   #endif

   #ifndef __iomem       /* For clean compiles in earlier kernels without __iomem annotations */
      #define __iomem
   #endif

   #define pci_dma_hi32(a)         ((a >> 16) >> 16)
   #define pci_dma_lo32(a)         (a & 0xffffffff)

   #if (BITS_PER_LONG > 32) || defined(CONFIG_HIGHMEM64G)
      #define IPS_ENABLE_DMA64        (1)
   #else
      #define IPS_ENABLE_DMA64        (0)
   #endif

   /*
    * Adapter address map equates
    */
   #define IPS_REG_HISR                 0x08    /* Host Interrupt Status Reg   */
   #define IPS_REG_CCSAR                0x10    /* Cmd Channel System Addr Reg */
   #define IPS_REG_CCCR                 0x14    /* Cmd Channel Control Reg     */
   #define IPS_REG_SQHR                 0x20    /* Status Q Head Reg           */
   #define IPS_REG_SQTR                 0x24    /* Status Q Tail Reg           */
   #define IPS_REG_SQER                 0x28    /* Status Q End Reg            */
   #define IPS_REG_SQSR                 0x2C    /* Status Q Start Reg          */
   #define IPS_REG_SCPR                 0x05    /* Subsystem control port reg  */
   #define IPS_REG_ISPR                 0x06    /* interrupt status port reg   */
   #define IPS_REG_CBSP                 0x07    /* CBSP register               */
   #define IPS_REG_FLAP                 0x18    /* Flash address port          */
   #define IPS_REG_FLDP                 0x1C    /* Flash data port             */
   #define IPS_REG_NDAE                 0x38    /* Anaconda 64 NDAE Register   */
   #define IPS_REG_I2O_INMSGQ           0x40    /* I2O Inbound Message Queue   */
   #define IPS_REG_I2O_OUTMSGQ          0x44    /* I2O Outbound Message Queue  */
   #define IPS_REG_I2O_HIR              0x30    /* I2O Interrupt Status        */
   #define IPS_REG_I960_IDR             0x20    /* i960 Inbound Doorbell       */
   #define IPS_REG_I960_MSG0            0x18    /* i960 Outbound Reg 0         */
   #define IPS_REG_I960_MSG1            0x1C    /* i960 Outbound Reg 1         */
   #define IPS_REG_I960_OIMR            0x34    /* i960 Oubound Int Mask Reg   */

   /*
    * Adapter register bit equates
    */
   #define IPS_BIT_GHI                  0x04    /* HISR General Host Interrupt */
   #define IPS_BIT_SQO                  0x02    /* HISR Status Q Overflow      */
   #define IPS_BIT_SCE                  0x01    /* HISR Status Channel Enqueue */
   #define IPS_BIT_SEM                  0x08    /* CCCR Semaphore Bit          */
   #define IPS_BIT_ILE                  0x10    /* CCCR ILE Bit                */
   #define IPS_BIT_START_CMD            0x101A  /* CCCR Start Command Channel  */
   #define IPS_BIT_START_STOP           0x0002  /* CCCR Start/Stop Bit         */
   #define IPS_BIT_RST                  0x80    /* SCPR Reset Bit              */
   #define IPS_BIT_EBM                  0x02    /* SCPR Enable Bus Master      */
   #define IPS_BIT_EI                   0x80    /* HISR Enable Interrupts      */
   #define IPS_BIT_OP                   0x01    /* OP bit in CBSP              */
   #define IPS_BIT_I2O_OPQI             0x08    /* General Host Interrupt      */
   #define IPS_BIT_I960_MSG0I           0x01    /* Message Register 0 Interrupt*/
   #define IPS_BIT_I960_MSG1I           0x02    /* Message Register 1 Interrupt*/

   /*
    * Adapter Command ID Equates
    */
   #define IPS_CMD_GET_LD_INFO          0x19
   #define IPS_CMD_GET_SUBSYS           0x40
   #define IPS_CMD_READ_CONF            0x38
   #define IPS_CMD_RW_NVRAM_PAGE        0xBC
   #define IPS_CMD_READ                 0x02
   #define IPS_CMD_WRITE                0x03
   #define IPS_CMD_FFDC                 0xD7
   #define IPS_CMD_ENQUIRY              0x05
   #define IPS_CMD_FLUSH                0x0A
   #define IPS_CMD_READ_SG              0x82
   #define IPS_CMD_WRITE_SG             0x83
   #define IPS_CMD_DCDB                 0x04
   #define IPS_CMD_DCDB_SG              0x84
   #define IPS_CMD_EXTENDED_DCDB 	    0x95
   #define IPS_CMD_EXTENDED_DCDB_SG	    0x96
   #define IPS_CMD_CONFIG_SYNC          0x58
   #define IPS_CMD_ERROR_TABLE          0x17
   #define IPS_CMD_DOWNLOAD             0x20
   #define IPS_CMD_RW_BIOSFW            0x22
   #define IPS_CMD_GET_VERSION_INFO     0xC6
   #define IPS_CMD_RESET_CHANNEL        0x1A

   /*
    * Adapter Equates
    */
   #define IPS_CSL                      0xFF
   #define IPS_POCL                     0x30
   #define IPS_NORM_STATE               0x00
   #define IPS_MAX_ADAPTER_TYPES        3
   #define IPS_MAX_ADAPTERS             16
   #define IPS_MAX_IOCTL                1
   #define IPS_MAX_IOCTL_QUEUE          8
   #define IPS_MAX_QUEUE                128
   #define IPS_BLKSIZE                  512
   #define IPS_MAX_SG                   17
   #define IPS_MAX_LD                   8
   #define IPS_MAX_CHANNELS             4
   #define IPS_MAX_TARGETS              15
   #define IPS_MAX_CHUNKS               16
   #define IPS_MAX_CMDS                 128
   #define IPS_MAX_XFER                 0x10000
   #define IPS_NVRAM_P5_SIG             0xFFDDBB99
   #define IPS_MAX_POST_BYTES           0x02
   #define IPS_MAX_CONFIG_BYTES         0x02
   #define IPS_GOOD_POST_STATUS         0x80
   #define IPS_SEM_TIMEOUT              2000
   #define IPS_IOCTL_COMMAND            0x0D
   #define IPS_INTR_ON                  0
   #define IPS_INTR_IORL                1
   #define IPS_FFDC                     99
   #define IPS_ADAPTER_ID               0xF
   #define IPS_VENDORID_IBM             0x1014
   #define IPS_VENDORID_ADAPTEC         0x9005
   #define IPS_DEVICEID_COPPERHEAD      0x002E
   #define IPS_DEVICEID_MORPHEUS        0x01BD
   #define IPS_DEVICEID_MARCO           0x0250
   #define IPS_SUBDEVICEID_4M           0x01BE
   #define IPS_SUBDEVICEID_4L           0x01BF
   #define IPS_SUBDEVICEID_4MX          0x0208
   #define IPS_SUBDEVICEID_4LX          0x020E
   #define IPS_SUBDEVICEID_5I2          0x0259
   #define IPS_SUBDEVICEID_5I1          0x0258
   #define IPS_SUBDEVICEID_6M           0x0279
   #define IPS_SUBDEVICEID_6I           0x028C
   #define IPS_SUBDEVICEID_7k           0x028E
   #define IPS_SUBDEVICEID_7M           0x028F
   #define IPS_IOCTL_SIZE               8192
   #define IPS_STATUS_SIZE              4
   #define IPS_STATUS_Q_SIZE            (IPS_MAX_CMDS+1) * IPS_STATUS_SIZE
   #define IPS_IMAGE_SIZE               500 * 1024
   #define IPS_MEMMAP_SIZE              128
   #define IPS_ONE_MSEC                 1
   #define IPS_ONE_SEC                  1000

   /*
    * Geometry Settings
    */
   #define IPS_COMP_HEADS               128
   #define IPS_COMP_SECTORS             32
   #define IPS_NORM_HEADS               254
   #define IPS_NORM_SECTORS             63

   /*
    * Adapter Basic Status Codes
    */
   #define IPS_BASIC_STATUS_MASK        0xFF
   #define IPS_GSC_STATUS_MASK          0x0F
   #define IPS_CMD_SUCCESS              0x00
   #define IPS_CMD_RECOVERED_ERROR      0x01
   #define IPS_INVAL_OPCO               0x03
   #define IPS_INVAL_CMD_BLK            0x04
   #define IPS_INVAL_PARM_BLK           0x05
   #define IPS_BUSY                     0x08
   #define IPS_CMD_CMPLT_WERROR         0x0C
   #define IPS_LD_ERROR                 0x0D
   #define IPS_CMD_TIMEOUT              0x0E
   #define IPS_PHYS_DRV_ERROR           0x0F

   /*
    * Adapter Extended Status Equates
    */
   #define IPS_ERR_SEL_TO               0xF0
   #define IPS_ERR_OU_RUN               0xF2
   #define IPS_ERR_HOST_RESET           0xF7
   #define IPS_ERR_DEV_RESET            0xF8
   #define IPS_ERR_RECOVERY             0xFC
   #define IPS_ERR_CKCOND               0xFF

   /*
    * Operating System Defines
    */
   #define IPS_OS_WINDOWS_NT            0x01
   #define IPS_OS_NETWARE               0x02
   #define IPS_OS_OPENSERVER            0x03
   #define IPS_OS_UNIXWARE              0x04
   #define IPS_OS_SOLARIS               0x05
   #define IPS_OS_OS2                   0x06
   #define IPS_OS_LINUX                 0x07
   #define IPS_OS_FREEBSD               0x08

   /*
    * Adapter Revision ID's
    */
   #define IPS_REVID_SERVERAID          0x02
   #define IPS_REVID_NAVAJO             0x03
   #define IPS_REVID_SERVERAID2         0x04
   #define IPS_REVID_CLARINETP1         0x05
   #define IPS_REVID_CLARINETP2         0x07
   #define IPS_REVID_CLARINETP3         0x0D
   #define IPS_REVID_TROMBONE32         0x0F
   #define IPS_REVID_TROMBONE64         0x10

   /*
    * NVRAM Page 5 Adapter Defines
    */
   #define IPS_ADTYPE_SERVERAID         0x01
   #define IPS_ADTYPE_SERVERAID2        0x02
   #define IPS_ADTYPE_NAVAJO            0x03
   #define IPS_ADTYPE_KIOWA             0x04
   #define IPS_ADTYPE_SERVERAID3        0x05
   #define IPS_ADTYPE_SERVERAID3L       0x06
   #define IPS_ADTYPE_SERVERAID4H       0x07
   #define IPS_ADTYPE_SERVERAID4M       0x08
   #define IPS_ADTYPE_SERVERAID4L       0x09
   #define IPS_ADTYPE_SERVERAID4MX      0x0A
   #define IPS_ADTYPE_SERVERAID4LX      0x0B
   #define IPS_ADTYPE_SERVERAID5I2      0x0C
   #define IPS_ADTYPE_SERVERAID5I1      0x0D
   #define IPS_ADTYPE_SERVERAID6M       0x0E
   #define IPS_ADTYPE_SERVERAID6I       0x0F
   #define IPS_ADTYPE_SERVERAID7t       0x10
   #define IPS_ADTYPE_SERVERAID7k       0x11
   #define IPS_ADTYPE_SERVERAID7M       0x12

   /*
    * Adapter Command/Status Packet Definitions
    */
   #define IPS_SUCCESS                  0x01 /* Successfully completed       */
   #define IPS_SUCCESS_IMM              0x02 /* Success - Immediately        */
   #define IPS_FAILURE                  0x04 /* Completed with Error         */

   /*
    * Logical Drive Equates
    */
   #define IPS_LD_OFFLINE               0x02
   #define IPS_LD_OKAY                  0x03
   #define IPS_LD_FREE                  0x00
   #define IPS_LD_SYS                   0x06
   #define IPS_LD_CRS                   0x24

   /*
    * DCDB Table Equates
    */
   #define IPS_NO_DISCONNECT            0x00
   #define IPS_DISCONNECT_ALLOWED       0x80
   #define IPS_NO_AUTO_REQSEN           0x40
   #define IPS_DATA_NONE                0x00
   #define IPS_DATA_UNK                 0x00
   #define IPS_DATA_IN                  0x01
   #define IPS_DATA_OUT                 0x02
   #define IPS_TRANSFER64K              0x08
   #define IPS_NOTIMEOUT                0x00
   #define IPS_TIMEOUT10                0x10
   #define IPS_TIMEOUT60                0x20
   #define IPS_TIMEOUT20M               0x30

   /*
    * SCSI Inquiry Data Flags
    */
   #define IPS_SCSI_INQ_TYPE_DASD       0x00
   #define IPS_SCSI_INQ_TYPE_PROCESSOR  0x03
   #define IPS_SCSI_INQ_LU_CONNECTED    0x00
   #define IPS_SCSI_INQ_RD_REV2         0x02
   #define IPS_SCSI_INQ_REV2            0x02
   #define IPS_SCSI_INQ_REV3            0x03
   #define IPS_SCSI_INQ_Address16       0x01
   #define IPS_SCSI_INQ_Address32       0x02
   #define IPS_SCSI_INQ_MedChanger      0x08
   #define IPS_SCSI_INQ_MultiPort       0x10
   #define IPS_SCSI_INQ_EncServ         0x40
   #define IPS_SCSI_INQ_SoftReset       0x01
   #define IPS_SCSI_INQ_CmdQue          0x02
   #define IPS_SCSI_INQ_Linked          0x08
   #define IPS_SCSI_INQ_Sync            0x10
   #define IPS_SCSI_INQ_WBus16          0x20
   #define IPS_SCSI_INQ_WBus32          0x40
   #define IPS_SCSI_INQ_RelAdr          0x80

   /*
    * SCSI Request Sense Data Flags
    */
   #define IPS_SCSI_REQSEN_VALID        0x80
   #define IPS_SCSI_REQSEN_CURRENT_ERR  0x70
   #define IPS_SCSI_REQSEN_NO_SENSE     0x00

   /*
    * SCSI Mode Page Equates
    */
   #define IPS_SCSI_MP3_SoftSector      0x01
   #define IPS_SCSI_MP3_HardSector      0x02
   #define IPS_SCSI_MP3_Removeable      0x04
   #define IPS_SCSI_MP3_AllocateSurface 0x08

   /*
    * HA Flags
    */

   #define IPS_HA_ENH_SG                0x1

   /*
    * SCB Flags
    */
   #define IPS_SCB_MAP_SG               0x00008
   #define IPS_SCB_MAP_SINGLE           0X00010

   /*
    * Passthru stuff
    */
   #define IPS_COPPUSRCMD              (('C'<<8) | 65)
   #define IPS_COPPIOCCMD              (('C'<<8) | 66)
   #define IPS_NUMCTRLS                (('C'<<8) | 68)
   #define IPS_CTRLINFO                (('C'<<8) | 69)

   /* flashing defines */
   #define IPS_FW_IMAGE                0x00
   #define IPS_BIOS_IMAGE              0x01
   #define IPS_WRITE_FW                0x01
   #define IPS_WRITE_BIOS              0x02
   #define IPS_ERASE_BIOS              0x03
   #define IPS_BIOS_HEADER             0xC0

   /* time oriented stuff */
   #define IPS_IS_LEAP_YEAR(y)           (((y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0))) ? 1 : 0)
   #define IPS_NUM_LEAP_YEARS_THROUGH(y) ((y) / 4 - (y) / 100 + (y) / 400)

   #define IPS_SECS_MIN                 60
   #define IPS_SECS_HOUR                3600
   #define IPS_SECS_8HOURS              28800
   #define IPS_SECS_DAY                 86400
   #define IPS_DAYS_NORMAL_YEAR         365
   #define IPS_DAYS_LEAP_YEAR           366
   #define IPS_EPOCH_YEAR               1970

   /*
    * Scsi_Host Template
    */
   static int ips_proc_info(struct Scsi_Host *, char *, char **, off_t, int, int);
   static int ips_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		sector_t capacity, int geom[]);
   static int ips_slave_configure(struct scsi_device *SDptr);

/*
 * Raid Command Formats
 */
typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  log_drv;
   uint8_t  sg_count;
   uint32_t lba;
   uint32_t sg_addr;
   uint16_t sector_count;
   uint8_t  segment_4G;
   uint8_t  enhanced_sg;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_IO_CMD, *PIPS_IO_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint16_t reserved;
   uint32_t reserved2;
   uint32_t buffer_addr;
   uint32_t reserved3;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_LD_CMD, *PIPS_LD_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  reserved;
   uint8_t  reserved2;
   uint32_t reserved3;
   uint32_t buffer_addr;
   uint32_t reserved4;
} IPS_IOCTL_CMD, *PIPS_IOCTL_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  channel;
   uint8_t  reserved3;
   uint8_t  reserved4;
   uint8_t  reserved5;
   uint8_t  reserved6;
   uint8_t  reserved7;
   uint8_t  reserved8;
   uint8_t  reserved9;
   uint8_t  reserved10;
   uint8_t  reserved11;
   uint8_t  reserved12;
   uint8_t  reserved13;
   uint8_t  reserved14;
   uint8_t  adapter_flag;
} IPS_RESET_CMD, *PIPS_RESET_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint16_t reserved;
   uint32_t reserved2;
   uint32_t dcdb_address;
   uint16_t reserved3;
   uint8_t  segment_4G;
   uint8_t  enhanced_sg;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_DCDB_CMD, *PIPS_DCDB_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  channel;
   uint8_t  source_target;
   uint32_t reserved;
   uint32_t reserved2;
   uint32_t reserved3;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_CS_CMD, *PIPS_CS_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  log_drv;
   uint8_t  control;
   uint32_t reserved;
   uint32_t reserved2;
   uint32_t reserved3;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_US_CMD, *PIPS_US_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  reserved;
   uint8_t  state;
   uint32_t reserved2;
   uint32_t reserved3;
   uint32_t reserved4;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_FC_CMD, *PIPS_FC_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  reserved;
   uint8_t  desc;
   uint32_t reserved2;
   uint32_t buffer_addr;
   uint32_t reserved3;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_STATUS_CMD, *PIPS_STATUS_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  page;
   uint8_t  write;
   uint32_t reserved;
   uint32_t buffer_addr;
   uint32_t reserved2;
   uint32_t ccsar;
   uint32_t cccr;
} IPS_NVRAM_CMD, *PIPS_NVRAM_CMD;

typedef struct
{
    uint8_t  op_code;
    uint8_t  command_id;
    uint16_t reserved;
    uint32_t count;
    uint32_t buffer_addr;
    uint32_t reserved2;
} IPS_VERSION_INFO, *PIPS_VERSION_INFO;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  reset_count;
   uint8_t  reset_type;
   uint8_t  second;
   uint8_t  minute;
   uint8_t  hour;
   uint8_t  day;
   uint8_t  reserved1[4];
   uint8_t  month;
   uint8_t  yearH;
   uint8_t  yearL;
   uint8_t  reserved2;
} IPS_FFDC_CMD, *PIPS_FFDC_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  type;
   uint8_t  direction;
   uint32_t count;
   uint32_t buffer_addr;
   uint8_t  total_packets;
   uint8_t  packet_num;
   uint16_t reserved;
} IPS_FLASHFW_CMD, *PIPS_FLASHFW_CMD;

typedef struct {
   uint8_t  op_code;
   uint8_t  command_id;
   uint8_t  type;
   uint8_t  direction;
   uint32_t count;
   uint32_t buffer_addr;
   uint32_t offset;
} IPS_FLASHBIOS_CMD, *PIPS_FLASHBIOS_CMD;

typedef union {
   IPS_IO_CMD         basic_io;
   IPS_LD_CMD         logical_info;
   IPS_IOCTL_CMD      ioctl_info;
   IPS_DCDB_CMD       dcdb;
   IPS_CS_CMD         config_sync;
   IPS_US_CMD         unlock_stripe;
   IPS_FC_CMD         flush_cache;
   IPS_STATUS_CMD     status;
   IPS_NVRAM_CMD      nvram;
   IPS_FFDC_CMD       ffdc;
   IPS_FLASHFW_CMD    flashfw;
   IPS_FLASHBIOS_CMD  flashbios;
   IPS_VERSION_INFO   version_info;
   IPS_RESET_CMD      reset;
} IPS_HOST_COMMAND, *PIPS_HOST_COMMAND;

typedef struct {
   uint8_t  logical_id;
   uint8_t  reserved;
   uint8_t  raid_level;
   uint8_t  state;
   uint32_t sector_count;
} IPS_DRIVE_INFO, *PIPS_DRIVE_INFO;

typedef struct {
   uint8_t       no_of_log_drive;
   uint8_t       reserved[3];
   IPS_DRIVE_INFO drive_info[IPS_MAX_LD];
} IPS_LD_INFO, *PIPS_LD_INFO;

typedef struct {
   uint8_t   device_address;
   uint8_t   cmd_attribute;
   uint16_t  transfer_length;
   uint32_t  buffer_pointer;
   uint8_t   cdb_length;
   uint8_t   sense_length;
   uint8_t   sg_count;
   uint8_t   reserved;
   uint8_t   scsi_cdb[12];
   uint8_t   sense_info[64];
   uint8_t   scsi_status;
   uint8_t   reserved2[3];
} IPS_DCDB_TABLE, *PIPS_DCDB_TABLE;

typedef struct {
   uint8_t   device_address;
   uint8_t   cmd_attribute;
   uint8_t   cdb_length;
   uint8_t   reserved_for_LUN;
   uint32_t  transfer_length;
   uint32_t  buffer_pointer;
   uint16_t  sg_count;
   uint8_t   sense_length;
   uint8_t   scsi_status;
   uint32_t  reserved;
   uint8_t   scsi_cdb[16];
   uint8_t   sense_info[56];
} IPS_DCDB_TABLE_TAPE, *PIPS_DCDB_TABLE_TAPE;

typedef union {
   struct {
      volatile uint8_t  reserved;
      volatile uint8_t  command_id;
      volatile uint8_t  basic_status;
      volatile uint8_t  extended_status;
   } fields;

   volatile uint32_t    value;
} IPS_STATUS, *PIPS_STATUS;

typedef struct {
   IPS_STATUS           status[IPS_MAX_CMDS + 1];
   volatile PIPS_STATUS p_status_start;
   volatile PIPS_STATUS p_status_end;
   volatile PIPS_STATUS p_status_tail;
   volatile uint32_t    hw_status_start;
   volatile uint32_t    hw_status_tail;
} IPS_ADAPTER, *PIPS_ADAPTER;

typedef struct {
   uint8_t  ucLogDriveCount;
   uint8_t  ucMiscFlag;
   uint8_t  ucSLTFlag;
   uint8_t  ucBSTFlag;
   uint8_t  ucPwrChgCnt;
   uint8_t  ucWrongAdrCnt;
   uint8_t  ucUnidentCnt;
   uint8_t  ucNVramDevChgCnt;
   uint8_t  CodeBlkVersion[8];
   uint8_t  BootBlkVersion[8];
   uint32_t ulDriveSize[IPS_MAX_LD];
   uint8_t  ucConcurrentCmdCount;
   uint8_t  ucMaxPhysicalDevices;
   uint16_t usFlashRepgmCount;
   uint8_t  ucDefunctDiskCount;
   uint8_t  ucRebuildFlag;
   uint8_t  ucOfflineLogDrvCount;
   uint8_t  ucCriticalDrvCount;
   uint16_t usConfigUpdateCount;
   uint8_t  ucBlkFlag;
   uint8_t  reserved;
   uint16_t usAddrDeadDisk[IPS_MAX_CHANNELS * (IPS_MAX_TARGETS + 1)];
} IPS_ENQ, *PIPS_ENQ;

typedef struct {
   uint8_t  ucInitiator;
   uint8_t  ucParameters;
   uint8_t  ucMiscFlag;
   uint8_t  ucState;
   uint32_t ulBlockCount;
   uint8_t  ucDeviceId[28];
} IPS_DEVSTATE, *PIPS_DEVSTATE;

typedef struct {
   uint8_t  ucChn;
   uint8_t  ucTgt;
   uint16_t ucReserved;
   uint32_t ulStartSect;
   uint32_t ulNoOfSects;
} IPS_CHUNK, *PIPS_CHUNK;

typedef struct {
   uint16_t ucUserField;
   uint8_t  ucState;
   uint8_t  ucRaidCacheParam;
   uint8_t  ucNoOfChunkUnits;
   uint8_t  ucStripeSize;
   uint8_t  ucParams;
   uint8_t  ucReserved;
   uint32_t ulLogDrvSize;
   IPS_CHUNK chunk[IPS_MAX_CHUNKS];
} IPS_LD, *PIPS_LD;

typedef struct {
   uint8_t  board_disc[8];
   uint8_t  processor[8];
   uint8_t  ucNoChanType;
   uint8_t  ucNoHostIntType;
   uint8_t  ucCompression;
   uint8_t  ucNvramType;
   uint32_t ulNvramSize;
} IPS_HARDWARE, *PIPS_HARDWARE;

typedef struct {
   uint8_t        ucLogDriveCount;
   uint8_t        ucDateD;
   uint8_t        ucDateM;
   uint8_t        ucDateY;
   uint8_t        init_id[4];
   uint8_t        host_id[12];
   uint8_t        time_sign[8];
   uint32_t       UserOpt;
   uint16_t       user_field;
   uint8_t        ucRebuildRate;
   uint8_t        ucReserve;
   IPS_HARDWARE   hardware_disc;
   IPS_LD         logical_drive[IPS_MAX_LD];
   IPS_DEVSTATE   dev[IPS_MAX_CHANNELS][IPS_MAX_TARGETS+1];
   uint8_t        reserved[512];
} IPS_CONF, *PIPS_CONF;

typedef struct {
   uint32_t  signature;
   uint8_t   reserved1;
   uint8_t   adapter_slot;
   uint16_t  adapter_type;
   uint8_t   ctrl_bios[8];
   uint8_t   versioning;                   /* 1 = Versioning Supported, else 0 */
   uint8_t   version_mismatch;             /* 1 = Versioning MisMatch,  else 0 */
   uint8_t   reserved2;
   uint8_t   operating_system;
   uint8_t   driver_high[4];
   uint8_t   driver_low[4];
   uint8_t   BiosCompatibilityID[8];
   uint8_t   ReservedForOS2[8];
   uint8_t   bios_high[4];                 /* Adapter's Flashed BIOS Version   */
   uint8_t   bios_low[4];
   uint8_t   adapter_order[16];            /* BIOS Telling us the Sort Order   */
   uint8_t   Filler[60];
} IPS_NVRAM_P5, *PIPS_NVRAM_P5;

/*--------------------------------------------------------------------------*/
/* Data returned from a GetVersion Command                                  */
/*--------------------------------------------------------------------------*/

                                             /* SubSystem Parameter[4]      */
#define  IPS_GET_VERSION_SUPPORT 0x00018000  /* Mask for Versioning Support */

typedef struct
{
   uint32_t  revision;
   uint8_t   bootBlkVersion[32];
   uint8_t   bootBlkAttributes[4];
   uint8_t   codeBlkVersion[32];
   uint8_t   biosVersion[32];
   uint8_t   biosAttributes[4];
   uint8_t   compatibilityId[32];
   uint8_t   reserved[4];
} IPS_VERSION_DATA;


typedef struct _IPS_SUBSYS {
   uint32_t  param[128];
} IPS_SUBSYS, *PIPS_SUBSYS;

/**
 ** SCSI Structures
 **/

/*
 * Inquiry Data Format
 */
typedef struct {
   uint8_t   DeviceType;
   uint8_t   DeviceTypeQualifier;
   uint8_t   Version;
   uint8_t   ResponseDataFormat;
   uint8_t   AdditionalLength;
   uint8_t   Reserved;
   uint8_t   Flags[2];
   uint8_t   VendorId[8];
   uint8_t   ProductId[16];
   uint8_t   ProductRevisionLevel[4];
   uint8_t   Reserved2;                                  /* Provides NULL terminator to name */
} IPS_SCSI_INQ_DATA, *PIPS_SCSI_INQ_DATA;

/*
 * Read Capacity Data Format
 */
typedef struct {
   uint32_t lba;
   uint32_t len;
} IPS_SCSI_CAPACITY;

/*
 * Request Sense Data Format
 */
typedef struct {
   uint8_t  ResponseCode;
   uint8_t  SegmentNumber;
   uint8_t  Flags;
   uint8_t  Information[4];
   uint8_t  AdditionalLength;
   uint8_t  CommandSpecific[4];
   uint8_t  AdditionalSenseCode;
   uint8_t  AdditionalSenseCodeQual;
   uint8_t  FRUCode;
   uint8_t  SenseKeySpecific[3];
} IPS_SCSI_REQSEN;

/*
 * Sense Data Format - Page 3
 */
typedef struct {
   uint8_t  PageCode;
   uint8_t  PageLength;
   uint16_t TracksPerZone;
   uint16_t AltSectorsPerZone;
   uint16_t AltTracksPerZone;
   uint16_t AltTracksPerVolume;
   uint16_t SectorsPerTrack;
   uint16_t BytesPerSector;
   uint16_t Interleave;
   uint16_t TrackSkew;
   uint16_t CylinderSkew;
   uint8_t  flags;
   uint8_t  reserved[3];
} IPS_SCSI_MODE_PAGE3;

/*
 * Sense Data Format - Page 4
 */
typedef struct {
   uint8_t  PageCode;
   uint8_t  PageLength;
   uint16_t CylindersHigh;
   uint8_t  CylindersLow;
   uint8_t  Heads;
   uint16_t WritePrecompHigh;
   uint8_t  WritePrecompLow;
   uint16_t ReducedWriteCurrentHigh;
   uint8_t  ReducedWriteCurrentLow;
   uint16_t StepRate;
   uint16_t LandingZoneHigh;
   uint8_t  LandingZoneLow;
   uint8_t  flags;
   uint8_t  RotationalOffset;
   uint8_t  Reserved;
   uint16_t MediumRotationRate;
   uint8_t  Reserved2[2];
} IPS_SCSI_MODE_PAGE4;

/*
 * Sense Data Format - Page 8
 */
typedef struct {
   uint8_t  PageCode;
   uint8_t  PageLength;
   uint8_t  flags;
   uint8_t  RetentPrio;
   uint16_t DisPrefetchLen;
   uint16_t MinPrefetchLen;
   uint16_t MaxPrefetchLen;
   uint16_t MaxPrefetchCeiling;
} IPS_SCSI_MODE_PAGE8;

/*
 * Sense Data Format - Block Descriptor (DASD)
 */
typedef struct {
   uint32_t NumberOfBlocks;
   uint8_t  DensityCode;
   uint16_t BlockLengthHigh;
   uint8_t  BlockLengthLow;
} IPS_SCSI_MODE_PAGE_BLKDESC;

/*
 * Sense Data Format - Mode Page Header
 */
typedef struct {
   uint8_t  DataLength;
   uint8_t  MediumType;
   uint8_t  Reserved;
   uint8_t  BlockDescLength;
} IPS_SCSI_MODE_PAGE_HEADER;

typedef struct {
   IPS_SCSI_MODE_PAGE_HEADER  hdr;
   IPS_SCSI_MODE_PAGE_BLKDESC blkdesc;

   union {
      IPS_SCSI_MODE_PAGE3 pg3;
      IPS_SCSI_MODE_PAGE4 pg4;
      IPS_SCSI_MODE_PAGE8 pg8;
   } pdata;
} IPS_SCSI_MODE_PAGE_DATA;

/*
 * Scatter Gather list format
 */
typedef struct ips_sglist {
   uint32_t address;
   uint32_t length;
} IPS_STD_SG_LIST;

typedef struct ips_enh_sglist {
   uint32_t address_lo;
   uint32_t address_hi;
   uint32_t length;
   uint32_t reserved;
} IPS_ENH_SG_LIST;

typedef union {
   void             *list;
   IPS_STD_SG_LIST  *std_list;
   IPS_ENH_SG_LIST  *enh_list;
} IPS_SG_LIST;

typedef struct _IPS_INFOSTR {
   char *buffer;
   int   length;
   int   offset;
   int   pos;
   int   localpos;
} IPS_INFOSTR;

typedef struct {
   char *option_name;
   int  *option_flag;
   int   option_value;
} IPS_OPTION;

/*
 * Status Info
 */
typedef struct ips_stat {
   uint32_t residue_len;
   void     *scb_addr;
   uint8_t  padding[12 - sizeof(void *)];
} ips_stat_t;

/*
 * SCB Queue Format
 */
typedef struct ips_scb_queue {
   struct ips_scb *head;
   struct ips_scb *tail;
   int             count;
} ips_scb_queue_t;

/*
 * Wait queue_format
 */
typedef struct ips_wait_queue {
	struct scsi_cmnd *head;
	struct scsi_cmnd *tail;
	int count;
} ips_wait_queue_t;

typedef struct ips_copp_wait_item {
	struct scsi_cmnd *scsi_cmd;
	struct ips_copp_wait_item *next;
} ips_copp_wait_item_t;

typedef struct ips_copp_queue {
   struct ips_copp_wait_item *head;
   struct ips_copp_wait_item *tail;
   int                        count;
} ips_copp_queue_t;

/* forward decl for host structure */
struct ips_ha;

typedef struct {
   int       (*reset)(struct ips_ha *);
   int       (*issue)(struct ips_ha *, struct ips_scb *);
   int       (*isinit)(struct ips_ha *);
   int       (*isintr)(struct ips_ha *);
   int       (*init)(struct ips_ha *);
   int       (*erasebios)(struct ips_ha *);
   int       (*programbios)(struct ips_ha *, char *, uint32_t, uint32_t);
   int       (*verifybios)(struct ips_ha *, char *, uint32_t, uint32_t);
   void      (*statinit)(struct ips_ha *);
   int       (*intr)(struct ips_ha *);
   void      (*enableint)(struct ips_ha *);
   uint32_t (*statupd)(struct ips_ha *);
} ips_hw_func_t;

typedef struct ips_ha {
   uint8_t            ha_id[IPS_MAX_CHANNELS+1];
   uint32_t           dcdb_active[IPS_MAX_CHANNELS];
   uint32_t           io_addr;            /* Base I/O address           */
   uint8_t            ntargets;           /* Number of targets          */
   uint8_t            nbus;               /* Number of buses            */
   uint8_t            nlun;               /* Number of Luns             */
   uint16_t           ad_type;            /* Adapter type               */
   uint16_t           host_num;           /* Adapter number             */
   uint32_t           max_xfer;           /* Maximum Xfer size          */
   uint32_t           max_cmds;           /* Max concurrent commands    */
   uint32_t           num_ioctl;          /* Number of Ioctls           */
   ips_stat_t         sp;                 /* Status packer pointer      */
   struct ips_scb    *scbs;               /* Array of all CCBS          */
   struct ips_scb    *scb_freelist;       /* SCB free list              */
   ips_wait_queue_t   scb_waitlist;       /* Pending SCB list           */
   ips_copp_queue_t   copp_waitlist;      /* Pending PT list            */
   ips_scb_queue_t    scb_activelist;     /* Active SCB list            */
   IPS_IO_CMD        *dummy;              /* dummy command              */
   IPS_ADAPTER       *adapt;              /* Adapter status area        */
   IPS_LD_INFO       *logical_drive_info; /* Adapter Logical Drive Info */
   dma_addr_t         logical_drive_info_dma_addr; /* Logical Drive Info DMA Address */
   IPS_ENQ           *enq;                /* Adapter Enquiry data       */
   IPS_CONF          *conf;               /* Adapter config data        */
   IPS_NVRAM_P5      *nvram;              /* NVRAM page 5 data          */
   IPS_SUBSYS        *subsys;             /* Subsystem parameters       */
   char              *ioctl_data;         /* IOCTL data area            */
   uint32_t           ioctl_datasize;     /* IOCTL data size            */
   uint32_t           cmd_in_progress;    /* Current command in progress*/
   int                flags;              /*                            */
   uint8_t            waitflag;           /* are we waiting for cmd     */
   uint8_t            active;
   int                ioctl_reset;        /* IOCTL Requested Reset Flag */
   uint16_t           reset_count;        /* number of resets           */
   time_t             last_ffdc;          /* last time we sent ffdc info*/
   uint8_t            slot_num;           /* PCI Slot Number            */
   int                ioctl_len;          /* size of ioctl buffer       */
   dma_addr_t         ioctl_busaddr;      /* dma address of ioctl buffer*/
   uint8_t            bios_version[8];    /* BIOS Revision              */
   uint32_t           mem_addr;           /* Memory mapped address      */
   uint32_t           io_len;             /* Size of IO Address         */
   uint32_t           mem_len;            /* Size of memory address     */
   char              __iomem *mem_ptr;    /* Memory mapped Ptr          */
   char              __iomem *ioremap_ptr;/* ioremapped memory pointer  */
   ips_hw_func_t      func;               /* hw function pointers       */
   struct pci_dev    *pcidev;             /* PCI device handle          */
   char              *flash_data;         /* Save Area for flash data   */
   int                flash_len;          /* length of flash buffer     */
   u32                flash_datasize;     /* Save Area for flash data size */
   dma_addr_t         flash_busaddr;      /* dma address of flash buffer*/
   dma_addr_t         enq_busaddr;        /* dma address of enq struct  */
   uint8_t            requires_esl;       /* Requires an EraseStripeLock */
} ips_ha_t;

typedef void (*ips_scb_callback) (ips_ha_t *, struct ips_scb *);

/*
 * SCB Format
 */
typedef struct ips_scb {
   IPS_HOST_COMMAND  cmd;
   IPS_DCDB_TABLE    dcdb;
   uint8_t           target_id;
   uint8_t           bus;
   uint8_t           lun;
   uint8_t           cdb[12];
   uint32_t          scb_busaddr;
   uint32_t          old_data_busaddr;  // Obsolete, but kept for old utility compatibility
   uint32_t          timeout;
   uint8_t           basic_status;
   uint8_t           extended_status;
   uint8_t           breakup;
   uint8_t           sg_break;
   uint32_t          data_len;
   uint32_t          sg_len;
   uint32_t          flags;
   uint32_t          op_code;
   IPS_SG_LIST       sg_list;
   struct scsi_cmnd *scsi_cmd;
   struct ips_scb   *q_next;
   ips_scb_callback  callback;
   uint32_t          sg_busaddr;
   int               sg_count;
   dma_addr_t        data_busaddr;
} ips_scb_t;

typedef struct ips_scb_pt {
   IPS_HOST_COMMAND  cmd;
   IPS_DCDB_TABLE    dcdb;
   uint8_t           target_id;
   uint8_t           bus;
   uint8_t           lun;
   uint8_t           cdb[12];
   uint32_t          scb_busaddr;
   uint32_t          data_busaddr;
   uint32_t          timeout;
   uint8_t           basic_status;
   uint8_t           extended_status;
   uint16_t          breakup;
   uint32_t          data_len;
   uint32_t          sg_len;
   uint32_t          flags;
   uint32_t          op_code;
   IPS_SG_LIST      *sg_list;
   struct scsi_cmnd *scsi_cmd;
   struct ips_scb   *q_next;
   ips_scb_callback  callback;
} ips_scb_pt_t;

/*
 * Passthru Command Format
 */
typedef struct {
   uint8_t       CoppID[4];
   uint32_t      CoppCmd;
   uint32_t      PtBuffer;
   uint8_t      *CmdBuffer;
   uint32_t      CmdBSize;
   ips_scb_pt_t  CoppCP;
   uint32_t      TimeOut;
   uint8_t       BasicStatus;
   uint8_t       ExtendedStatus;
   uint8_t       AdapterType;
   uint8_t       reserved;
} ips_passthru_t;

#endif

/* The Version Information below gets created by SED during the build process. */
/* Do not modify the next line; it's what SED is looking for to do the insert. */
/* Version Info                                                                */
/*************************************************************************
*
* VERSION.H -- version numbers and copyright notices in various formats
*
*************************************************************************/

#define IPS_VER_MAJOR 7
#define IPS_VER_MAJOR_STRING __stringify(IPS_VER_MAJOR)
#define IPS_VER_MINOR 12
#define IPS_VER_MINOR_STRING __stringify(IPS_VER_MINOR)
#define IPS_VER_BUILD 05
#define IPS_VER_BUILD_STRING __stringify(IPS_VER_BUILD)
#define IPS_VER_STRING IPS_VER_MAJOR_STRING "." \
		IPS_VER_MINOR_STRING "." IPS_VER_BUILD_STRING
#define IPS_RELEASE_ID 0x00020000
#define IPS_BUILD_IDENT 761
#define IPS_LEGALCOPYRIGHT_STRING "(C) Copyright IBM Corp. 1994, 2002. All Rights Reserved."
#define IPS_ADAPTECCOPYRIGHT_STRING "(c) Copyright Adaptec, Inc. 2002 to 2004. All Rights Reserved."
#define IPS_DELLCOPYRIGHT_STRING "(c) Copyright Dell 2004. All Rights Reserved."
#define IPS_NT_LEGALCOPYRIGHT_STRING "(C) Copyright IBM Corp. 1994, 2002."

/* Version numbers for various adapters */
#define IPS_VER_SERVERAID1 "2.25.01"
#define IPS_VER_SERVERAID2 "2.88.13"
#define IPS_VER_NAVAJO "2.88.13"
#define IPS_VER_SERVERAID3 "6.10.24"
#define IPS_VER_SERVERAID4H "7.12.02"
#define IPS_VER_SERVERAID4MLx "7.12.02"
#define IPS_VER_SARASOTA "7.12.02"
#define IPS_VER_MARCO "7.12.02"
#define IPS_VER_SEBRING "7.12.02"
#define IPS_VER_KEYWEST "7.12.02"

/* Compatability IDs for various adapters */
#define IPS_COMPAT_UNKNOWN ""
#define IPS_COMPAT_CURRENT "KW710"
#define IPS_COMPAT_SERVERAID1 "2.25.01"
#define IPS_COMPAT_SERVERAID2 "2.88.13"
#define IPS_COMPAT_NAVAJO  "2.88.13"
#define IPS_COMPAT_KIOWA "2.88.13"
#define IPS_COMPAT_SERVERAID3H  "SB610"
#define IPS_COMPAT_SERVERAID3L  "SB610"
#define IPS_COMPAT_SERVERAID4H  "KW710"
#define IPS_COMPAT_SERVERAID4M  "KW710"
#define IPS_COMPAT_SERVERAID4L  "KW710"
#define IPS_COMPAT_SERVERAID4Mx "KW710"
#define IPS_COMPAT_SERVERAID4Lx "KW710"
#define IPS_COMPAT_SARASOTA     "KW710"
#define IPS_COMPAT_MARCO        "KW710"
#define IPS_COMPAT_SEBRING      "KW710"
#define IPS_COMPAT_TAMPA        "KW710"
#define IPS_COMPAT_KEYWEST      "KW710"
#define IPS_COMPAT_BIOS "KW710"

#define IPS_COMPAT_MAX_ADAPTER_TYPE 18
#define IPS_COMPAT_ID_LENGTH 8

#define IPS_DEFINE_COMPAT_TABLE(tablename) \
   char tablename[IPS_COMPAT_MAX_ADAPTER_TYPE] [IPS_COMPAT_ID_LENGTH] = { \
      IPS_COMPAT_UNKNOWN, \
      IPS_COMPAT_SERVERAID1, \
      IPS_COMPAT_SERVERAID2, \
      IPS_COMPAT_NAVAJO, \
      IPS_COMPAT_KIOWA, \
      IPS_COMPAT_SERVERAID3H, \
      IPS_COMPAT_SERVERAID3L, \
      IPS_COMPAT_SERVERAID4H, \
      IPS_COMPAT_SERVERAID4M, \
      IPS_COMPAT_SERVERAID4L, \
      IPS_COMPAT_SERVERAID4Mx, \
      IPS_COMPAT_SERVERAID4Lx, \
      IPS_COMPAT_SARASOTA,         /* one-channel variety of SARASOTA */  \
      IPS_COMPAT_SARASOTA,         /* two-channel variety of SARASOTA */  \
      IPS_COMPAT_MARCO, \
      IPS_COMPAT_SEBRING, \
      IPS_COMPAT_TAMPA, \
      IPS_COMPAT_KEYWEST \
   }


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
