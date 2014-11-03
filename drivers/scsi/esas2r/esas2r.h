/*
 *  linux/drivers/scsi/esas2r/esas2r.h
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.
 *
 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>

#include "esas2r_log.h"
#include "atioctl.h"
#include "atvda.h"

#ifndef ESAS2R_H
#define ESAS2R_H

/* Global Variables */
extern struct esas2r_adapter *esas2r_adapters[];
extern u8 *esas2r_buffered_ioctl;
extern dma_addr_t esas2r_buffered_ioctl_addr;
extern u32 esas2r_buffered_ioctl_size;
extern struct pci_dev *esas2r_buffered_ioctl_pcid;
#define SGL_PG_SZ_MIN   64
#define SGL_PG_SZ_MAX   1024
extern int sgl_page_size;
#define NUM_SGL_MIN     8
#define NUM_SGL_MAX     2048
extern int num_sg_lists;
#define NUM_REQ_MIN     4
#define NUM_REQ_MAX     256
extern int num_requests;
#define NUM_AE_MIN      2
#define NUM_AE_MAX      8
extern int num_ae_requests;
extern int cmd_per_lun;
extern int can_queue;
extern int esas2r_max_sectors;
extern int sg_tablesize;
extern int interrupt_mode;
extern int num_io_requests;

/* Macro defintions */
#define ESAS2R_MAX_ID        255
#define MAX_ADAPTERS         32
#define ESAS2R_DRVR_NAME     "esas2r"
#define ESAS2R_LONGNAME      "ATTO ExpressSAS 6GB RAID Adapter"
#define ESAS2R_MAX_DEVICES     32
#define ATTONODE_NAME         "ATTONode"
#define ESAS2R_MAJOR_REV       1
#define ESAS2R_MINOR_REV       00
#define ESAS2R_VERSION_STR     DEFINED_NUM_TO_STR(ESAS2R_MAJOR_REV) "." \
	DEFINED_NUM_TO_STR(ESAS2R_MINOR_REV)
#define ESAS2R_COPYRIGHT_YEARS "2001-2013"
#define ESAS2R_DEFAULT_SGL_PAGE_SIZE 384
#define ESAS2R_DEFAULT_CMD_PER_LUN   64
#define ESAS2R_DEFAULT_NUM_SG_LISTS 1024
#define DEFINED_NUM_TO_STR(num) NUM_TO_STR(num)
#define NUM_TO_STR(num) #num

#define ESAS2R_SGL_ALIGN    16
#define ESAS2R_LIST_ALIGN   16
#define ESAS2R_LIST_EXTRA   ESAS2R_NUM_EXTRA
#define ESAS2R_DATA_BUF_LEN         256
#define ESAS2R_DEFAULT_TMO          5000
#define ESAS2R_DISC_BUF_LEN         512
#define ESAS2R_FWCOREDUMP_SZ        0x80000
#define ESAS2R_NUM_PHYS             8
#define ESAS2R_TARG_ID_INV          0xFFFF
#define ESAS2R_INT_STS_MASK         MU_INTSTAT_MASK
#define ESAS2R_INT_ENB_MASK         MU_INTSTAT_MASK
#define ESAS2R_INT_DIS_MASK         0
#define ESAS2R_MAX_TARGETS          256
#define ESAS2R_KOBJ_NAME_LEN        20

/* u16 (WORD) component macros */
#define LOBYTE(w) ((u8)(u16)(w))
#define HIBYTE(w) ((u8)(((u16)(w)) >> 8))
#define MAKEWORD(lo, hi) ((u16)((u8)(lo) | ((u16)(u8)(hi) << 8)))

/* u32 (DWORD) component macros */
#define LOWORD(d) ((u16)(u32)(d))
#define HIWORD(d) ((u16)(((u32)(d)) >> 16))
#define MAKEDWORD(lo, hi) ((u32)((u16)(lo) | ((u32)(u16)(hi) << 16)))

/* macro to get the lowest nonzero bit of a value */
#define LOBIT(x) ((x) & (0 - (x)))

/* These functions are provided to access the chip's control registers.
 * The register is specified by its byte offset from the register base
 * for the adapter.
 */
#define esas2r_read_register_dword(a, reg)                             \
	readl((void __iomem *)a->regs + (reg) + MW_REG_OFFSET_HWREG)

#define esas2r_write_register_dword(a, reg, data)                      \
	writel(data, (void __iomem *)(a->regs + (reg) + MW_REG_OFFSET_HWREG))

#define esas2r_flush_register_dword(a, r) esas2r_read_register_dword(a, r)

/* This function is provided to access the chip's data window.   The
 * register is specified by its byte offset from the window base
 * for the adapter.
 */
#define esas2r_read_data_byte(a, reg)                                  \
	readb((void __iomem *)a->data_window + (reg))

/* ATTO vendor and device Ids */
#define ATTO_VENDOR_ID          0x117C
#define ATTO_DID_INTEL_IOP348   0x002C
#define ATTO_DID_MV_88RC9580    0x0049
#define ATTO_DID_MV_88RC9580TS  0x0066
#define ATTO_DID_MV_88RC9580TSE 0x0067
#define ATTO_DID_MV_88RC9580TL  0x0068

/* ATTO subsystem device Ids */
#define ATTO_SSDID_TBT      0x4000
#define ATTO_TSSC_3808      0x4066
#define ATTO_TSSC_3808E     0x4067
#define ATTO_TLSH_1068      0x4068
#define ATTO_ESAS_R680      0x0049
#define ATTO_ESAS_R608      0x004A
#define ATTO_ESAS_R60F      0x004B
#define ATTO_ESAS_R6F0      0x004C
#define ATTO_ESAS_R644      0x004D
#define ATTO_ESAS_R648      0x004E

/*
 * flash definitions & structures
 * define the code types
 */
#define FBT_CPYR        0xAA00
#define FBT_SETUP       0xAA02
#define FBT_FLASH_VER   0xAA04

/* offsets to various locations in flash */
#define FLS_OFFSET_BOOT (u32)(0x00700000)
#define FLS_OFFSET_NVR  (u32)(0x007C0000)
#define FLS_OFFSET_CPYR FLS_OFFSET_NVR
#define FLS_LENGTH_BOOT (FLS_OFFSET_CPYR - FLS_OFFSET_BOOT)
#define FLS_BLOCK_SIZE  (u32)(0x00020000)
#define FI_NVR_2KB  0x0800
#define FI_NVR_8KB  0x2000
#define FM_BUF_SZ   0x800

/*
 * marvell frey (88R9580) register definitions
 * chip revision identifiers
 */
#define MVR_FREY_B2     0xB2

/*
 * memory window definitions.  window 0 is the data window with definitions
 * of MW_DATA_XXX.  window 1 is the register window with definitions of
 * MW_REG_XXX.
 */
#define MW_REG_WINDOW_SIZE      (u32)(0x00040000)
#define MW_REG_OFFSET_HWREG     (u32)(0x00000000)
#define MW_REG_OFFSET_PCI       (u32)(0x00008000)
#define MW_REG_PCI_HWREG_DELTA  (MW_REG_OFFSET_PCI - MW_REG_OFFSET_HWREG)
#define MW_DATA_WINDOW_SIZE     (u32)(0x00020000)
#define MW_DATA_ADDR_SER_FLASH  (u32)(0xEC000000)
#define MW_DATA_ADDR_SRAM       (u32)(0xF4000000)
#define MW_DATA_ADDR_PAR_FLASH  (u32)(0xFC000000)

/*
 * the following registers are for the communication
 * list interface (AKA message unit (MU))
 */
#define MU_IN_LIST_ADDR_LO      (u32)(0x00004000)
#define MU_IN_LIST_ADDR_HI      (u32)(0x00004004)

#define MU_IN_LIST_WRITE        (u32)(0x00004018)
    #define MU_ILW_TOGGLE       (u32)(0x00004000)

#define MU_IN_LIST_READ         (u32)(0x0000401C)
    #define MU_ILR_TOGGLE       (u32)(0x00004000)
    #define MU_ILIC_LIST        (u32)(0x0000000F)
    #define MU_ILIC_LIST_F0     (u32)(0x00000000)
    #define MU_ILIC_DEST        (u32)(0x00000F00)
    #define MU_ILIC_DEST_DDR    (u32)(0x00000200)
#define MU_IN_LIST_IFC_CONFIG   (u32)(0x00004028)

#define MU_IN_LIST_CONFIG       (u32)(0x0000402C)
    #define MU_ILC_ENABLE       (u32)(0x00000001)
    #define MU_ILC_ENTRY_MASK   (u32)(0x000000F0)
    #define MU_ILC_ENTRY_4_DW   (u32)(0x00000020)
    #define MU_ILC_DYNAMIC_SRC  (u32)(0x00008000)
    #define MU_ILC_NUMBER_MASK  (u32)(0x7FFF0000)
    #define MU_ILC_NUMBER_SHIFT            16

#define MU_OUT_LIST_ADDR_LO     (u32)(0x00004050)
#define MU_OUT_LIST_ADDR_HI     (u32)(0x00004054)

#define MU_OUT_LIST_COPY_PTR_LO (u32)(0x00004058)
#define MU_OUT_LIST_COPY_PTR_HI (u32)(0x0000405C)

#define MU_OUT_LIST_WRITE       (u32)(0x00004068)
    #define MU_OLW_TOGGLE       (u32)(0x00004000)

#define MU_OUT_LIST_COPY        (u32)(0x0000406C)
    #define MU_OLC_TOGGLE       (u32)(0x00004000)
    #define MU_OLC_WRT_PTR      (u32)(0x00003FFF)

#define MU_OUT_LIST_IFC_CONFIG  (u32)(0x00004078)
    #define MU_OLIC_LIST        (u32)(0x0000000F)
    #define MU_OLIC_LIST_F0     (u32)(0x00000000)
    #define MU_OLIC_SOURCE      (u32)(0x00000F00)
    #define MU_OLIC_SOURCE_DDR  (u32)(0x00000200)

#define MU_OUT_LIST_CONFIG      (u32)(0x0000407C)
    #define MU_OLC_ENABLE       (u32)(0x00000001)
    #define MU_OLC_ENTRY_MASK   (u32)(0x000000F0)
    #define MU_OLC_ENTRY_4_DW   (u32)(0x00000020)
    #define MU_OLC_NUMBER_MASK  (u32)(0x7FFF0000)
    #define MU_OLC_NUMBER_SHIFT            16

#define MU_OUT_LIST_INT_STAT    (u32)(0x00004088)
    #define MU_OLIS_INT         (u32)(0x00000001)

#define MU_OUT_LIST_INT_MASK    (u32)(0x0000408C)
    #define MU_OLIS_MASK        (u32)(0x00000001)

/*
 * the maximum size of the communication lists is two greater than the
 * maximum amount of VDA requests.  the extra are to prevent queue overflow.
 */
#define ESAS2R_MAX_NUM_REQS         256
#define ESAS2R_NUM_EXTRA            2
#define ESAS2R_MAX_COMM_LIST_SIZE   (ESAS2R_MAX_NUM_REQS + ESAS2R_NUM_EXTRA)

/*
 * the following registers are for the CPU interface
 */
#define MU_CTL_STATUS_IN        (u32)(0x00010108)
    #define MU_CTL_IN_FULL_RST  (u32)(0x00000020)
#define MU_CTL_STATUS_IN_B2     (u32)(0x00010130)
    #define MU_CTL_IN_FULL_RST2 (u32)(0x80000000)
#define MU_DOORBELL_IN          (u32)(0x00010460)
    #define DRBL_RESET_BUS      (u32)(0x00000002)
    #define DRBL_PAUSE_AE       (u32)(0x00000004)
    #define DRBL_RESUME_AE      (u32)(0x00000008)
    #define DRBL_MSG_IFC_DOWN   (u32)(0x00000010)
    #define DRBL_FLASH_REQ      (u32)(0x00000020)
    #define DRBL_FLASH_DONE     (u32)(0x00000040)
    #define DRBL_FORCE_INT      (u32)(0x00000080)
    #define DRBL_MSG_IFC_INIT   (u32)(0x00000100)
    #define DRBL_POWER_DOWN     (u32)(0x00000200)
    #define DRBL_DRV_VER_1      (u32)(0x00010000)
    #define DRBL_DRV_VER        DRBL_DRV_VER_1
#define MU_DOORBELL_IN_ENB      (u32)(0x00010464)
#define MU_DOORBELL_OUT         (u32)(0x00010480)
 #define DRBL_PANIC_REASON_MASK (u32)(0x00F00000)
    #define DRBL_UNUSED_HANDLER (u32)(0x00100000)
    #define DRBL_UNDEF_INSTR    (u32)(0x00200000)
    #define DRBL_PREFETCH_ABORT (u32)(0x00300000)
    #define DRBL_DATA_ABORT     (u32)(0x00400000)
    #define DRBL_JUMP_TO_ZERO   (u32)(0x00500000)
  #define DRBL_FW_RESET         (u32)(0x00080000)
  #define DRBL_FW_VER_MSK       (u32)(0x00070000)
  #define DRBL_FW_VER_0         (u32)(0x00000000)
  #define DRBL_FW_VER_1         (u32)(0x00010000)
  #define DRBL_FW_VER           DRBL_FW_VER_1
#define MU_DOORBELL_OUT_ENB     (u32)(0x00010484)
    #define DRBL_ENB_MASK       (u32)(0x00F803FF)
#define MU_INT_STATUS_OUT       (u32)(0x00010200)
    #define MU_INTSTAT_POST_OUT (u32)(0x00000010)
    #define MU_INTSTAT_DRBL_IN  (u32)(0x00000100)
    #define MU_INTSTAT_DRBL     (u32)(0x00001000)
    #define MU_INTSTAT_MASK     (u32)(0x00001010)
#define MU_INT_MASK_OUT         (u32)(0x0001020C)

/* PCI express registers accessed via window 1 */
#define MVR_PCI_WIN1_REMAP      (u32)(0x00008438)
    #define MVRPW1R_ENABLE      (u32)(0x00000001)


/* structures */

/* inbound list dynamic source entry */
struct esas2r_inbound_list_source_entry {
	u64 address;
	u32 length;
	#define HWILSE_INTERFACE_F0  0x00000000
	u32 reserved;
};

/* PCI data structure in expansion ROM images */
struct __packed esas2r_boot_header {
	char signature[4];
	u16 vendor_id;
	u16 device_id;
	u16 VPD;
	u16 struct_length;
	u8 struct_revision;
	u8 class_code[3];
	u16 image_length;
	u16 code_revision;
	u8 code_type;
	#define CODE_TYPE_PC    0
	#define CODE_TYPE_OPEN  1
	#define CODE_TYPE_EFI   3
	u8 indicator;
	#define INDICATOR_LAST  0x80
	u8 reserved[2];
};

struct __packed esas2r_boot_image {
	u16 signature;
	u8 reserved[22];
	u16 header_offset;
	u16 pnp_offset;
};

struct __packed esas2r_pc_image {
	u16 signature;
	u8 length;
	u8 entry_point[3];
	u8 checksum;
	u16 image_end;
	u16 min_size;
	u8 rom_flags;
	u8 reserved[12];
	u16 header_offset;
	u16 pnp_offset;
	struct esas2r_boot_header boot_image;
};

struct __packed esas2r_efi_image {
	u16 signature;
	u16 length;
	u32 efi_signature;
	#define EFI_ROM_SIG     0x00000EF1
	u16 image_type;
	#define EFI_IMAGE_APP   10
	#define EFI_IMAGE_BSD   11
	#define EFI_IMAGE_RTD   12
	u16 machine_type;
	#define EFI_MACHINE_IA32 0x014c
	#define EFI_MACHINE_IA64 0x0200
	#define EFI_MACHINE_X64  0x8664
	#define EFI_MACHINE_EBC  0x0EBC
	u16 compression;
	#define EFI_UNCOMPRESSED 0x0000
	#define EFI_COMPRESSED   0x0001
	u8 reserved[8];
	u16 efi_offset;
	u16 header_offset;
	u16 reserved2;
	struct esas2r_boot_header boot_image;
};

struct esas2r_adapter;
struct esas2r_sg_context;
struct esas2r_request;

typedef void (*RQCALLBK)     (struct esas2r_adapter *a,
			      struct esas2r_request *rq);
typedef bool (*RQBUILDSGL)   (struct esas2r_adapter *a,
			      struct esas2r_sg_context *sgc);

struct esas2r_component_header {
	u8 img_type;
	#define CH_IT_FW    0x00
	#define CH_IT_NVR   0x01
	#define CH_IT_BIOS  0x02
	#define CH_IT_MAC   0x03
	#define CH_IT_CFG   0x04
	#define CH_IT_EFI   0x05
	u8 status;
	#define CH_STAT_PENDING 0xff
	#define CH_STAT_FAILED  0x00
	#define CH_STAT_SUCCESS 0x01
	#define CH_STAT_RETRY   0x02
	#define CH_STAT_INVALID 0x03
	u8 pad[2];
	u32 version;
	u32 length;
	u32 image_offset;
};

#define FI_REL_VER_SZ   16

struct esas2r_flash_img_v0 {
	u8 fi_version;
	#define FI_VERSION_0    00
	u8 status;
	u8 adap_typ;
	u8 action;
	u32 length;
	u16 checksum;
	u16 driver_error;
	u16 flags;
	u16 num_comps;
	#define FI_NUM_COMPS_V0 5
	u8 rel_version[FI_REL_VER_SZ];
	struct esas2r_component_header cmp_hdr[FI_NUM_COMPS_V0];
	u8 scratch_buf[FM_BUF_SZ];
};

struct esas2r_flash_img {
	u8 fi_version;
	#define FI_VERSION_1    01
	u8 status;
	#define FI_STAT_SUCCESS  0x00
	#define FI_STAT_FAILED   0x01
	#define FI_STAT_REBOOT   0x02
	#define FI_STAT_ADAPTYP  0x03
	#define FI_STAT_INVALID  0x04
	#define FI_STAT_CHKSUM   0x05
	#define FI_STAT_LENGTH   0x06
	#define FI_STAT_UNKNOWN  0x07
	#define FI_STAT_IMG_VER  0x08
	#define FI_STAT_BUSY     0x09
	#define FI_STAT_DUAL     0x0A
	#define FI_STAT_MISSING  0x0B
	#define FI_STAT_UNSUPP   0x0C
	#define FI_STAT_ERASE    0x0D
	#define FI_STAT_FLASH    0x0E
	#define FI_STAT_DEGRADED 0x0F
	u8 adap_typ;
	#define FI_AT_UNKNWN    0xFF
	#define FI_AT_SUN_LAKE  0x0B
	#define FI_AT_MV_9580   0x0F
	u8 action;
	#define FI_ACT_DOWN     0x00
	#define FI_ACT_UP       0x01
	#define FI_ACT_UPSZ     0x02
	#define FI_ACT_MAX      0x02
	#define FI_ACT_DOWN1    0x80
	u32 length;
	u16 checksum;
	u16 driver_error;
	u16 flags;
	#define FI_FLG_NVR_DEF  0x0001
	u16 num_comps;
	#define FI_NUM_COMPS_V1 6
	u8 rel_version[FI_REL_VER_SZ];
	struct esas2r_component_header cmp_hdr[FI_NUM_COMPS_V1];
	u8 scratch_buf[FM_BUF_SZ];
};

/* definitions for flash script (FS) commands */
struct esas2r_ioctlfs_command {
	u8 command;
	#define ESAS2R_FS_CMD_ERASE    0
	#define ESAS2R_FS_CMD_READ     1
	#define ESAS2R_FS_CMD_BEGINW   2
	#define ESAS2R_FS_CMD_WRITE    3
	#define ESAS2R_FS_CMD_COMMIT   4
	#define ESAS2R_FS_CMD_CANCEL   5
	u8 checksum;
	u8 reserved[2];
	u32 flash_addr;
	u32 length;
	u32 image_offset;
};

struct esas2r_ioctl_fs {
	u8 version;
	#define ESAS2R_FS_VER      0
	u8 status;
	u8 driver_error;
	u8 adap_type;
	#define ESAS2R_FS_AT_ESASRAID2     3
	#define ESAS2R_FS_AT_TSSASRAID2    4
	#define ESAS2R_FS_AT_TSSASRAID2E   5
	#define ESAS2R_FS_AT_TLSASHBA      6
	u8 driver_ver;
	u8 reserved[11];
	struct esas2r_ioctlfs_command command;
	u8 data[1];
};

struct esas2r_sas_nvram {
	u8 signature[4];
	u8 version;
	#define SASNVR_VERSION_0    0x00
	#define SASNVR_VERSION      SASNVR_VERSION_0
	u8 checksum;
	#define SASNVR_CKSUM_SEED   0x5A
	u8 max_lun_for_target;
	u8 pci_latency;
	#define SASNVR_PCILAT_DIS   0x00
	#define SASNVR_PCILAT_MIN   0x10
	#define SASNVR_PCILAT_MAX   0xF8
	u8 options1;
	#define SASNVR1_BOOT_DRVR   0x01
	#define SASNVR1_BOOT_SCAN   0x02
	#define SASNVR1_DIS_PCI_MWI 0x04
	#define SASNVR1_FORCE_ORD_Q 0x08
	#define SASNVR1_CACHELINE_0 0x10
	#define SASNVR1_DIS_DEVSORT 0x20
	#define SASNVR1_PWR_MGT_EN  0x40
	#define SASNVR1_WIDEPORT    0x80
	u8 options2;
	#define SASNVR2_SINGLE_BUS  0x01
	#define SASNVR2_SLOT_BIND   0x02
	#define SASNVR2_EXP_PROG    0x04
	#define SASNVR2_CMDTHR_LUN  0x08
	#define SASNVR2_HEARTBEAT   0x10
	#define SASNVR2_INT_CONNECT 0x20
	#define SASNVR2_SW_MUX_CTRL 0x40
	#define SASNVR2_DISABLE_NCQ 0x80
	u8 int_coalescing;
	#define SASNVR_COAL_DIS     0x00
	#define SASNVR_COAL_LOW     0x01
	#define SASNVR_COAL_MED     0x02
	#define SASNVR_COAL_HI      0x03
	u8 cmd_throttle;
	#define SASNVR_CMDTHR_NONE  0x00
	u8 dev_wait_time;
	u8 dev_wait_count;
	u8 spin_up_delay;
	#define SASNVR_SPINUP_MAX   0x14
	u8 ssp_align_rate;
	u8 sas_addr[8];
	u8 phy_speed[16];
	#define SASNVR_SPEED_AUTO   0x00
	#define SASNVR_SPEED_1_5GB  0x01
	#define SASNVR_SPEED_3GB    0x02
	#define SASNVR_SPEED_6GB    0x03
	#define SASNVR_SPEED_12GB   0x04
	u8 phy_mux[16];
	#define SASNVR_MUX_DISABLED 0x00
	#define SASNVR_MUX_1_5GB    0x01
	#define SASNVR_MUX_3GB      0x02
	#define SASNVR_MUX_6GB      0x03
	u8 phy_flags[16];
	#define SASNVR_PHF_DISABLED 0x01
	#define SASNVR_PHF_RD_ONLY  0x02
	u8 sort_type;
	#define SASNVR_SORT_SAS_ADDR    0x00
	#define SASNVR_SORT_H308_CONN   0x01
	#define SASNVR_SORT_PHY_ID      0x02
	#define SASNVR_SORT_SLOT_ID     0x03
	u8 dpm_reqcmd_lmt;
	u8 dpm_stndby_time;
	u8 dpm_active_time;
	u8 phy_target_id[16];
	#define SASNVR_PTI_DISABLED     0xFF
	u8 virt_ses_mode;
	#define SASNVR_VSMH_DISABLED    0x00
	u8 read_write_mode;
	#define SASNVR_RWM_DEFAULT      0x00
	u8 link_down_to;
	u8 reserved[0xA1];
};

typedef u32 (*PGETPHYSADDR) (struct esas2r_sg_context *sgc, u64 *addr);

struct esas2r_sg_context {
	struct esas2r_adapter *adapter;
	struct esas2r_request *first_req;
	u32 length;
	u8 *cur_offset;
	PGETPHYSADDR get_phys_addr;
	union {
		struct {
			struct atto_vda_sge *curr;
			struct atto_vda_sge *last;
			struct atto_vda_sge *limit;
			struct atto_vda_sge *chain;
		} a64;
		struct {
			struct atto_physical_region_description *curr;
			struct atto_physical_region_description *chain;
			u32 sgl_max_cnt;
			u32 sge_cnt;
		} prd;
	} sge;
	struct scatterlist *cur_sgel;
	u8 *exp_offset;
	int num_sgel;
	int sgel_count;
};

struct esas2r_target {
	u8 flags;
	#define TF_PASS_THRU    0x01
	#define TF_USED         0x02
	u8 new_target_state;
	u8 target_state;
	u8 buffered_target_state;
#define TS_NOT_PRESENT      0x00
#define TS_PRESENT          0x05
#define TS_LUN_CHANGE       0x06
#define TS_INVALID          0xFF
	u32 block_size;
	u32 inter_block;
	u32 inter_byte;
	u16 virt_targ_id;
	u16 phys_targ_id;
	u8 identifier_len;
	u64 sas_addr;
	u8 identifier[60];
	struct atto_vda_ae_lu lu_event;
};

struct esas2r_request {
	struct list_head comp_list;
	struct list_head req_list;
	union atto_vda_req *vrq;
	struct esas2r_mem_desc *vrq_md;
	union {
		void *data_buf;
		union atto_vda_rsp_data *vda_rsp_data;
	};
	u8 *sense_buf;
	struct list_head sg_table_head;
	struct esas2r_mem_desc *sg_table;
	u32 timeout;
	#define RQ_TIMEOUT_S1     0xFFFFFFFF
	#define RQ_TIMEOUT_S2     0xFFFFFFFE
	#define RQ_MAX_TIMEOUT    0xFFFFFFFD
	u16 target_id;
	u8 req_type;
	#define RT_INI_REQ          0x01
	#define RT_DISC_REQ         0x02
	u8 sense_len;
	union atto_vda_func_rsp func_rsp;
	RQCALLBK comp_cb;
	RQCALLBK interrupt_cb;
	void *interrupt_cx;
	u8 flags;
	#define RF_1ST_IBLK_BASE    0x04
	#define RF_FAILURE_OK       0x08
	u8 req_stat;
	u16 vda_req_sz;
	#define RQ_SIZE_DEFAULT   0
	u64 lba;
	RQCALLBK aux_req_cb;
	void *aux_req_cx;
	u32 blk_len;
	u32 max_blk_len;
	union {
		struct scsi_cmnd *cmd;
		u8 *task_management_status_ptr;
	};
};

struct esas2r_flash_context {
	struct esas2r_flash_img *fi;
	RQCALLBK interrupt_cb;
	u8 *sgc_offset;
	u8 *scratch;
	u32 fi_hdr_len;
	u8 task;
	#define     FMTSK_ERASE_BOOT    0
	#define     FMTSK_WRTBIOS       1
	#define     FMTSK_READBIOS      2
	#define     FMTSK_WRTMAC        3
	#define     FMTSK_READMAC       4
	#define     FMTSK_WRTEFI        5
	#define     FMTSK_READEFI       6
	#define     FMTSK_WRTCFG        7
	#define     FMTSK_READCFG       8
	u8 func;
	u16 num_comps;
	u32 cmp_len;
	u32 flsh_addr;
	u32 curr_len;
	u8 comp_typ;
	struct esas2r_sg_context sgc;
};

struct esas2r_disc_context {
	u8 disc_evt;
	#define DCDE_DEV_CHANGE     0x01
	#define DCDE_DEV_SCAN       0x02
	u8 state;
	#define DCS_DEV_RMV         0x00
	#define DCS_DEV_ADD         0x01
	#define DCS_BLOCK_DEV_SCAN  0x02
	#define DCS_RAID_GRP_INFO   0x03
	#define DCS_PART_INFO       0x04
	#define DCS_PT_DEV_INFO     0x05
	#define DCS_PT_DEV_ADDR     0x06
	#define DCS_DISC_DONE       0xFF
	u16 flags;
	#define DCF_DEV_CHANGE      0x0001
	#define DCF_DEV_SCAN        0x0002
	#define DCF_POLLED          0x8000
	u32 interleave;
	u32 block_size;
	u16 dev_ix;
	u8 part_num;
	u8 raid_grp_ix;
	char raid_grp_name[16];
	struct esas2r_target *curr_targ;
	u16 curr_virt_id;
	u16 curr_phys_id;
	u8 scan_gen;
	u8 dev_addr_type;
	u64 sas_addr;
};

struct esas2r_mem_desc {
	struct list_head next_desc;
	void *virt_addr;
	u64 phys_addr;
	void *pad;
	void *esas2r_data;
	u32 esas2r_param;
	u32 size;
};

enum fw_event_type {
	fw_event_null,
	fw_event_lun_change,
	fw_event_present,
	fw_event_not_present,
	fw_event_vda_ae
};

struct esas2r_vda_ae {
	u32 signature;
#define ESAS2R_VDA_EVENT_SIG  0x4154544F
	u8 bus_number;
	u8 devfn;
	u8 pad[2];
	union atto_vda_ae vda_ae;
};

struct esas2r_fw_event_work {
	struct list_head list;
	struct delayed_work work;
	struct esas2r_adapter *a;
	enum fw_event_type type;
	u8 data[sizeof(struct esas2r_vda_ae)];
};

enum state {
	FW_INVALID_ST,
	FW_STATUS_ST,
	FW_COMMAND_ST
};

struct esas2r_firmware {
	enum state state;
	struct esas2r_flash_img header;
	u8 *data;
	u64 phys;
	int orig_len;
	void *header_buff;
	u64 header_buff_phys;
};

struct esas2r_adapter {
	struct esas2r_target targetdb[ESAS2R_MAX_TARGETS];
	struct esas2r_target *targetdb_end;
	unsigned char *regs;
	unsigned char *data_window;
	long flags;
	#define AF_PORT_CHANGE      0
	#define AF_CHPRST_NEEDED    1
	#define AF_CHPRST_PENDING   2
	#define AF_CHPRST_DETECTED  3
	#define AF_BUSRST_NEEDED    4
	#define AF_BUSRST_PENDING   5
	#define AF_BUSRST_DETECTED  6
	#define AF_DISABLED         7
	#define AF_FLASH_LOCK       8
	#define AF_OS_RESET         9
	#define AF_FLASHING         10
	#define AF_POWER_MGT        11
	#define AF_NVR_VALID        12
	#define AF_DEGRADED_MODE    13
	#define AF_DISC_PENDING     14
	#define AF_TASKLET_SCHEDULED    15
	#define AF_HEARTBEAT        16
	#define AF_HEARTBEAT_ENB    17
	#define AF_NOT_PRESENT      18
	#define AF_CHPRST_STARTED   19
	#define AF_FIRST_INIT       20
	#define AF_POWER_DOWN       21
	#define AF_DISC_IN_PROG     22
	#define AF_COMM_LIST_TOGGLE 23
	#define AF_LEGACY_SGE_MODE  24
	#define AF_DISC_POLLED      25
	long flags2;
	#define AF2_SERIAL_FLASH    0
	#define AF2_DEV_SCAN        1
	#define AF2_DEV_CNT_OK      2
	#define AF2_COREDUMP_AVAIL  3
	#define AF2_COREDUMP_SAVED  4
	#define AF2_VDA_POWER_DOWN  5
	#define AF2_THUNDERLINK     6
	#define AF2_THUNDERBOLT     7
	#define AF2_INIT_DONE       8
	#define AF2_INT_PENDING     9
	#define AF2_TIMER_TICK      10
	#define AF2_IRQ_CLAIMED     11
	#define AF2_MSI_ENABLED     12
	atomic_t disable_cnt;
	atomic_t dis_ints_cnt;
	u32 int_stat;
	u32 int_mask;
	u32 volatile *outbound_copy;
	struct list_head avail_request;
	spinlock_t request_lock;
	spinlock_t sg_list_lock;
	spinlock_t queue_lock;
	spinlock_t mem_lock;
	struct list_head free_sg_list_head;
	struct esas2r_mem_desc *sg_list_mds;
	struct list_head active_list;
	struct list_head defer_list;
	struct esas2r_request **req_table;
	union {
		u16 prev_dev_cnt;
		u32 heartbeat_time;
	#define ESAS2R_HEARTBEAT_TIME       (3000)
	};
	u32 chip_uptime;
	#define ESAS2R_CHP_UPTIME_MAX       (60000)
	#define ESAS2R_CHP_UPTIME_CNT       (20000)
	u64 uncached_phys;
	u8 *uncached;
	struct esas2r_sas_nvram *nvram;
	struct esas2r_request general_req;
	u8 init_msg;
	#define ESAS2R_INIT_MSG_START       1
	#define ESAS2R_INIT_MSG_INIT        2
	#define ESAS2R_INIT_MSG_GET_INIT    3
	#define ESAS2R_INIT_MSG_REINIT      4
	u16 cmd_ref_no;
	u32 fw_version;
	u32 fw_build;
	u32 chip_init_time;
	#define ESAS2R_CHPRST_TIME         (180000)
	#define ESAS2R_CHPRST_WAIT_TIME    (2000)
	u32 last_tick_time;
	u32 window_base;
	RQBUILDSGL build_sgl;
	struct esas2r_request *first_ae_req;
	u32 list_size;
	u32 last_write;
	u32 last_read;
	u16 max_vdareq_size;
	u16 disc_wait_cnt;
	struct esas2r_mem_desc inbound_list_md;
	struct esas2r_mem_desc outbound_list_md;
	struct esas2r_disc_context disc_ctx;
	u8 *disc_buffer;
	u32 disc_start_time;
	u32 disc_wait_time;
	u32 flash_ver;
	char flash_rev[16];
	char fw_rev[16];
	char image_type[16];
	struct esas2r_flash_context flash_context;
	u32 num_targets_backend;
	u32 ioctl_tunnel;
	struct tasklet_struct tasklet;
	struct pci_dev *pcid;
	struct Scsi_Host *host;
	unsigned int index;
	char name[32];
	struct timer_list timer;
	struct esas2r_firmware firmware;
	wait_queue_head_t nvram_waiter;
	int nvram_command_done;
	wait_queue_head_t fm_api_waiter;
	int fm_api_command_done;
	wait_queue_head_t vda_waiter;
	int vda_command_done;
	u8 *vda_buffer;
	u64 ppvda_buffer;
#define VDA_BUFFER_HEADER_SZ (offsetof(struct atto_ioctl_vda, data))
#define VDA_MAX_BUFFER_SIZE  (0x40000 + VDA_BUFFER_HEADER_SZ)
	wait_queue_head_t fs_api_waiter;
	int fs_api_command_done;
	u64 ppfs_api_buffer;
	u8 *fs_api_buffer;
	u32 fs_api_buffer_size;
	wait_queue_head_t buffered_ioctl_waiter;
	int buffered_ioctl_done;
	int uncached_size;
	struct workqueue_struct *fw_event_q;
	struct list_head fw_event_list;
	spinlock_t fw_event_lock;
	u8 fw_events_off;                       /* if '1', then ignore events */
	char fw_event_q_name[ESAS2R_KOBJ_NAME_LEN];
	/*
	 * intr_mode stores the interrupt mode currently being used by this
	 * adapter. it is based on the interrupt_mode module parameter, but
	 * can be changed based on the ability (or not) to utilize the
	 * mode requested by the parameter.
	 */
	int intr_mode;
#define INTR_MODE_LEGACY 0
#define INTR_MODE_MSI    1
#define INTR_MODE_MSIX   2
	struct esas2r_sg_context fm_api_sgc;
	u8 *save_offset;
	struct list_head vrq_mds_head;
	struct esas2r_mem_desc *vrq_mds;
	int num_vrqs;
	struct semaphore fm_api_semaphore;
	struct semaphore fs_api_semaphore;
	struct semaphore nvram_semaphore;
	struct atto_ioctl *local_atto_ioctl;
	u8 fw_coredump_buff[ESAS2R_FWCOREDUMP_SZ];
	unsigned int sysfs_fw_created:1;
	unsigned int sysfs_fs_created:1;
	unsigned int sysfs_vda_created:1;
	unsigned int sysfs_hw_created:1;
	unsigned int sysfs_live_nvram_created:1;
	unsigned int sysfs_default_nvram_created:1;
};

/*
 * Function Declarations
 * SCSI functions
 */
int esas2r_release(struct Scsi_Host *);
const char *esas2r_info(struct Scsi_Host *);
int esas2r_write_params(struct esas2r_adapter *a, struct esas2r_request *rq,
			struct esas2r_sas_nvram *data);
int esas2r_ioctl_handler(void *hostdata, int cmd, void __user *arg);
int esas2r_ioctl(struct scsi_device *dev, int cmd, void __user *arg);
u8 handle_hba_ioctl(struct esas2r_adapter *a,
		    struct atto_ioctl *ioctl_hba);
int esas2r_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd);
int esas2r_show_info(struct seq_file *m, struct Scsi_Host *sh);
int esas2r_change_queue_depth(struct scsi_device *dev, int depth, int reason);
long esas2r_proc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);

/* SCSI error handler (eh) functions */
int esas2r_eh_abort(struct scsi_cmnd *cmd);
int esas2r_device_reset(struct scsi_cmnd *cmd);
int esas2r_host_reset(struct scsi_cmnd *cmd);
int esas2r_bus_reset(struct scsi_cmnd *cmd);
int esas2r_target_reset(struct scsi_cmnd *cmd);

/* Internal functions */
int esas2r_init_adapter(struct Scsi_Host *host, struct pci_dev *pcid,
			int index);
int esas2r_cleanup(struct Scsi_Host *host);
int esas2r_read_fw(struct esas2r_adapter *a, char *buf, long off, int count);
int esas2r_write_fw(struct esas2r_adapter *a, const char *buf, long off,
		    int count);
int esas2r_read_vda(struct esas2r_adapter *a, char *buf, long off, int count);
int esas2r_write_vda(struct esas2r_adapter *a, const char *buf, long off,
		     int count);
int esas2r_read_fs(struct esas2r_adapter *a, char *buf, long off, int count);
int esas2r_write_fs(struct esas2r_adapter *a, const char *buf, long off,
		    int count);
void esas2r_adapter_tasklet(unsigned long context);
irqreturn_t esas2r_interrupt(int irq, void *dev_id);
irqreturn_t esas2r_msi_interrupt(int irq, void *dev_id);
void esas2r_kickoff_timer(struct esas2r_adapter *a);
int esas2r_suspend(struct pci_dev *pcid, pm_message_t state);
int esas2r_resume(struct pci_dev *pcid);
void esas2r_fw_event_off(struct esas2r_adapter *a);
void esas2r_fw_event_on(struct esas2r_adapter *a);
bool esas2r_nvram_write(struct esas2r_adapter *a, struct esas2r_request *rq,
			struct esas2r_sas_nvram *nvram);
void esas2r_nvram_get_defaults(struct esas2r_adapter *a,
			       struct esas2r_sas_nvram *nvram);
void esas2r_complete_request_cb(struct esas2r_adapter *a,
				struct esas2r_request *rq);
void esas2r_reset_detected(struct esas2r_adapter *a);
void esas2r_target_state_changed(struct esas2r_adapter *ha, u16 targ_id,
				 u8 state);
int esas2r_req_status_to_error(u8 req_stat);
void esas2r_kill_adapter(int i);
void esas2r_free_request(struct esas2r_adapter *a, struct esas2r_request *rq);
struct esas2r_request *esas2r_alloc_request(struct esas2r_adapter *a);
u32 esas2r_get_uncached_size(struct esas2r_adapter *a);
bool esas2r_init_adapter_struct(struct esas2r_adapter *a,
				void **uncached_area);
bool esas2r_check_adapter(struct esas2r_adapter *a);
bool esas2r_init_adapter_hw(struct esas2r_adapter *a, bool init_poll);
void esas2r_start_request(struct esas2r_adapter *a, struct esas2r_request *rq);
bool esas2r_send_task_mgmt(struct esas2r_adapter *a,
			   struct esas2r_request *rqaux, u8 task_mgt_func);
void esas2r_do_tasklet_tasks(struct esas2r_adapter *a);
void esas2r_adapter_interrupt(struct esas2r_adapter *a);
void esas2r_do_deferred_processes(struct esas2r_adapter *a);
void esas2r_reset_bus(struct esas2r_adapter *a);
void esas2r_reset_adapter(struct esas2r_adapter *a);
void esas2r_timer_tick(struct esas2r_adapter *a);
const char *esas2r_get_model_name(struct esas2r_adapter *a);
const char *esas2r_get_model_name_short(struct esas2r_adapter *a);
u32 esas2r_stall_execution(struct esas2r_adapter *a, u32 start_time,
			   u32 *delay);
void esas2r_build_flash_req(struct esas2r_adapter *a,
			    struct esas2r_request *rq,
			    u8 sub_func,
			    u8 cksum,
			    u32 addr,
			    u32 length);
void esas2r_build_mgt_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u8 sub_func,
			  u8 scan_gen,
			  u16 dev_index,
			  u32 length,
			  void *data);
void esas2r_build_ae_req(struct esas2r_adapter *a, struct esas2r_request *rq);
void esas2r_build_cli_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u32 length,
			  u32 cmd_rsp_len);
void esas2r_build_ioctl_req(struct esas2r_adapter *a,
			    struct esas2r_request *rq,
			    u32 length,
			    u8 sub_func);
void esas2r_build_cfg_req(struct esas2r_adapter *a,
			  struct esas2r_request *rq,
			  u8 sub_func,
			  u32 length,
			  void *data);
void esas2r_power_down(struct esas2r_adapter *a);
bool esas2r_power_up(struct esas2r_adapter *a, bool init_poll);
void esas2r_wait_request(struct esas2r_adapter *a, struct esas2r_request *rq);
u32 esas2r_map_data_window(struct esas2r_adapter *a, u32 addr_lo);
bool esas2r_process_fs_ioctl(struct esas2r_adapter *a,
			     struct esas2r_ioctl_fs *fs,
			     struct esas2r_request *rq,
			     struct esas2r_sg_context *sgc);
bool esas2r_read_flash_block(struct esas2r_adapter *a, void *to, u32 from,
			     u32 size);
bool esas2r_read_mem_block(struct esas2r_adapter *a, void *to, u32 from,
			   u32 size);
bool esas2r_fm_api(struct esas2r_adapter *a, struct esas2r_flash_img *fi,
		   struct esas2r_request *rq, struct esas2r_sg_context *sgc);
void esas2r_force_interrupt(struct esas2r_adapter *a);
void esas2r_local_start_request(struct esas2r_adapter *a,
				struct esas2r_request *rq);
void esas2r_process_adapter_reset(struct esas2r_adapter *a);
void esas2r_complete_request(struct esas2r_adapter *a,
			     struct esas2r_request *rq);
void esas2r_dummy_complete(struct esas2r_adapter *a,
			   struct esas2r_request *rq);
void esas2r_ae_complete(struct esas2r_adapter *a, struct esas2r_request *rq);
void esas2r_start_vda_request(struct esas2r_adapter *a,
			      struct esas2r_request *rq);
bool esas2r_read_flash_rev(struct esas2r_adapter *a);
bool esas2r_read_image_type(struct esas2r_adapter *a);
bool esas2r_nvram_read_direct(struct esas2r_adapter *a);
bool esas2r_nvram_validate(struct esas2r_adapter *a);
void esas2r_nvram_set_defaults(struct esas2r_adapter *a);
bool esas2r_print_flash_rev(struct esas2r_adapter *a);
void esas2r_send_reset_ae(struct esas2r_adapter *a, bool pwr_mgt);
bool esas2r_init_msgs(struct esas2r_adapter *a);
bool esas2r_is_adapter_present(struct esas2r_adapter *a);
void esas2r_nuxi_mgt_data(u8 function, void *data);
void esas2r_nuxi_cfg_data(u8 function, void *data);
void esas2r_nuxi_ae_data(union atto_vda_ae *ae);
void esas2r_reset_chip(struct esas2r_adapter *a);
void esas2r_log_request_failure(struct esas2r_adapter *a,
				struct esas2r_request *rq);
void esas2r_polled_interrupt(struct esas2r_adapter *a);
bool esas2r_ioreq_aborted(struct esas2r_adapter *a, struct esas2r_request *rq,
			  u8 status);
bool esas2r_build_sg_list_sge(struct esas2r_adapter *a,
			      struct esas2r_sg_context *sgc);
bool esas2r_build_sg_list_prd(struct esas2r_adapter *a,
			      struct esas2r_sg_context *sgc);
void esas2r_targ_db_initialize(struct esas2r_adapter *a);
void esas2r_targ_db_remove_all(struct esas2r_adapter *a, bool notify);
void esas2r_targ_db_report_changes(struct esas2r_adapter *a);
struct esas2r_target *esas2r_targ_db_add_raid(struct esas2r_adapter *a,
					      struct esas2r_disc_context *dc);
struct esas2r_target *esas2r_targ_db_add_pthru(struct esas2r_adapter *a,
					       struct esas2r_disc_context *dc,
					       u8 *ident,
					       u8 ident_len);
void esas2r_targ_db_remove(struct esas2r_adapter *a, struct esas2r_target *t);
struct esas2r_target *esas2r_targ_db_find_by_sas_addr(struct esas2r_adapter *a,
						      u64 *sas_addr);
struct esas2r_target *esas2r_targ_db_find_by_ident(struct esas2r_adapter *a,
						   void *identifier,
						   u8 ident_len);
u16 esas2r_targ_db_find_next_present(struct esas2r_adapter *a, u16 target_id);
struct esas2r_target *esas2r_targ_db_find_by_virt_id(struct esas2r_adapter *a,
						     u16 virt_id);
u16 esas2r_targ_db_get_tgt_cnt(struct esas2r_adapter *a);
void esas2r_disc_initialize(struct esas2r_adapter *a);
void esas2r_disc_start_waiting(struct esas2r_adapter *a);
void esas2r_disc_check_for_work(struct esas2r_adapter *a);
void esas2r_disc_check_complete(struct esas2r_adapter *a);
void esas2r_disc_queue_event(struct esas2r_adapter *a, u8 disc_evt);
bool esas2r_disc_start_port(struct esas2r_adapter *a);
void esas2r_disc_local_start_request(struct esas2r_adapter *a,
				     struct esas2r_request *rq);
bool esas2r_set_degraded_mode(struct esas2r_adapter *a, char *error_str);
bool esas2r_process_vda_ioctl(struct esas2r_adapter *a,
			      struct atto_ioctl_vda *vi,
			      struct esas2r_request *rq,
			      struct esas2r_sg_context *sgc);
void esas2r_queue_fw_event(struct esas2r_adapter *a,
			   enum fw_event_type type,
			   void *data,
			   int data_sz);

/* Inline functions */

/* Allocate a chip scatter/gather list entry */
static inline struct esas2r_mem_desc *esas2r_alloc_sgl(struct esas2r_adapter *a)
{
	unsigned long flags;
	struct list_head *sgl;
	struct esas2r_mem_desc *result = NULL;

	spin_lock_irqsave(&a->sg_list_lock, flags);
	if (likely(!list_empty(&a->free_sg_list_head))) {
		sgl = a->free_sg_list_head.next;
		result = list_entry(sgl, struct esas2r_mem_desc, next_desc);
		list_del_init(sgl);
	}
	spin_unlock_irqrestore(&a->sg_list_lock, flags);

	return result;
}

/* Initialize a scatter/gather context */
static inline void esas2r_sgc_init(struct esas2r_sg_context *sgc,
				   struct esas2r_adapter *a,
				   struct esas2r_request *rq,
				   struct atto_vda_sge *first)
{
	sgc->adapter = a;
	sgc->first_req = rq;

	/*
	 * set the limit pointer such that an SGE pointer above this value
	 * would be the first one to overflow the SGL.
	 */
	sgc->sge.a64.limit = (struct atto_vda_sge *)((u8 *)rq->vrq
						     + (sizeof(union
							       atto_vda_req) /
							8)
						     - sizeof(struct
							      atto_vda_sge));
	if (first) {
		sgc->sge.a64.last =
			sgc->sge.a64.curr = first;
		rq->vrq->scsi.sg_list_offset = (u8)
					       ((u8 *)first -
						(u8 *)rq->vrq);
	} else {
		sgc->sge.a64.last =
			sgc->sge.a64.curr = &rq->vrq->scsi.u.sge[0];
		rq->vrq->scsi.sg_list_offset =
			(u8)offsetof(struct atto_vda_scsi_req, u.sge);
	}
	sgc->sge.a64.chain = NULL;
}

static inline void esas2r_rq_init_request(struct esas2r_request *rq,
					  struct esas2r_adapter *a)
{
	union atto_vda_req *vrq = rq->vrq;

	INIT_LIST_HEAD(&rq->sg_table_head);
	rq->data_buf = (void *)(vrq + 1);
	rq->interrupt_cb = NULL;
	rq->comp_cb = esas2r_complete_request_cb;
	rq->flags = 0;
	rq->timeout = 0;
	rq->req_stat = RS_PENDING;
	rq->req_type = RT_INI_REQ;

	/* clear the outbound response */
	rq->func_rsp.dwords[0] = 0;
	rq->func_rsp.dwords[1] = 0;

	/*
	 * clear the size of the VDA request.  esas2r_build_sg_list() will
	 * only allow the size of the request to grow.  there are some
	 * management requests that go through there twice and the second
	 * time through sets a smaller request size.  if this is not modified
	 * at all we'll set it to the size of the entire VDA request.
	 */
	rq->vda_req_sz = RQ_SIZE_DEFAULT;

	/* req_table entry should be NULL at this point - if not, halt */

	if (a->req_table[LOWORD(vrq->scsi.handle)])
		esas2r_bugon();

	/* fill in the table for this handle so we can get back to the
	 * request.
	 */
	a->req_table[LOWORD(vrq->scsi.handle)] = rq;

	/*
	 * add a reference number to the handle to make it unique (until it
	 * wraps of course) while preserving the least significant word
	 */
	vrq->scsi.handle = (a->cmd_ref_no++ << 16) | (u16)vrq->scsi.handle;

	/*
	 * the following formats a SCSI request.  the caller can override as
	 * necessary.  clear_vda_request can be called to clear the VDA
	 * request for another type of request.
	 */
	vrq->scsi.function = VDA_FUNC_SCSI;
	vrq->scsi.sense_len = SENSE_DATA_SZ;

	/* clear out sg_list_offset and chain_offset */
	vrq->scsi.sg_list_offset = 0;
	vrq->scsi.chain_offset = 0;
	vrq->scsi.flags = 0;
	vrq->scsi.reserved = 0;

	/* set the sense buffer to be the data payload buffer */
	vrq->scsi.ppsense_buf
		= cpu_to_le64(rq->vrq_md->phys_addr +
			      sizeof(union atto_vda_req));
}

static inline void esas2r_rq_free_sg_lists(struct esas2r_request *rq,
					   struct esas2r_adapter *a)
{
	unsigned long flags;

	if (list_empty(&rq->sg_table_head))
		return;

	spin_lock_irqsave(&a->sg_list_lock, flags);
	list_splice_tail_init(&rq->sg_table_head, &a->free_sg_list_head);
	spin_unlock_irqrestore(&a->sg_list_lock, flags);
}

static inline void esas2r_rq_destroy_request(struct esas2r_request *rq,
					     struct esas2r_adapter *a)

{
	esas2r_rq_free_sg_lists(rq, a);
	a->req_table[LOWORD(rq->vrq->scsi.handle)] = NULL;
	rq->data_buf = NULL;
}

static inline bool esas2r_is_tasklet_pending(struct esas2r_adapter *a)
{

	return test_bit(AF_BUSRST_NEEDED, &a->flags) ||
	       test_bit(AF_BUSRST_DETECTED, &a->flags) ||
	       test_bit(AF_CHPRST_NEEDED, &a->flags) ||
	       test_bit(AF_CHPRST_DETECTED, &a->flags) ||
	       test_bit(AF_PORT_CHANGE, &a->flags);

}

/*
 * Build the scatter/gather list for an I/O request according to the
 * specifications placed in the esas2r_sg_context.  The caller must initialize
 * struct esas2r_sg_context prior to the initial call by calling
 * esas2r_sgc_init()
 */
static inline bool esas2r_build_sg_list(struct esas2r_adapter *a,
					struct esas2r_request *rq,
					struct esas2r_sg_context *sgc)
{
	if (unlikely(le32_to_cpu(rq->vrq->scsi.length) == 0))
		return true;

	return (*a->build_sgl)(a, sgc);
}

static inline void esas2r_disable_chip_interrupts(struct esas2r_adapter *a)
{
	if (atomic_inc_return(&a->dis_ints_cnt) == 1)
		esas2r_write_register_dword(a, MU_INT_MASK_OUT,
					    ESAS2R_INT_DIS_MASK);
}

static inline void esas2r_enable_chip_interrupts(struct esas2r_adapter *a)
{
	if (atomic_dec_return(&a->dis_ints_cnt) == 0)
		esas2r_write_register_dword(a, MU_INT_MASK_OUT,
					    ESAS2R_INT_ENB_MASK);
}

/* Schedule a TASKLET to perform non-interrupt tasks that may require delays
 * or long completion times.
 */
static inline void esas2r_schedule_tasklet(struct esas2r_adapter *a)
{
	/* make sure we don't schedule twice */
	if (!test_and_set_bit(AF_TASKLET_SCHEDULED, &a->flags))
		tasklet_hi_schedule(&a->tasklet);
}

static inline void esas2r_enable_heartbeat(struct esas2r_adapter *a)
{
	if (!test_bit(AF_DEGRADED_MODE, &a->flags) &&
	    !test_bit(AF_CHPRST_PENDING, &a->flags) &&
	    (a->nvram->options2 & SASNVR2_HEARTBEAT))
		set_bit(AF_HEARTBEAT_ENB, &a->flags);
	else
		clear_bit(AF_HEARTBEAT_ENB, &a->flags);
}

static inline void esas2r_disable_heartbeat(struct esas2r_adapter *a)
{
	clear_bit(AF_HEARTBEAT_ENB, &a->flags);
	clear_bit(AF_HEARTBEAT, &a->flags);
}

/* Set the initial state for resetting the adapter on the next pass through
 * esas2r_do_deferred.
 */
static inline void esas2r_local_reset_adapter(struct esas2r_adapter *a)
{
	esas2r_disable_heartbeat(a);

	set_bit(AF_CHPRST_NEEDED, &a->flags);
	set_bit(AF_CHPRST_PENDING, &a->flags);
	set_bit(AF_DISC_PENDING, &a->flags);
}

/* See if an interrupt is pending on the adapter. */
static inline bool esas2r_adapter_interrupt_pending(struct esas2r_adapter *a)
{
	u32 intstat;

	if (a->int_mask == 0)
		return false;

	intstat = esas2r_read_register_dword(a, MU_INT_STATUS_OUT);

	if ((intstat & a->int_mask) == 0)
		return false;

	esas2r_disable_chip_interrupts(a);

	a->int_stat = intstat;
	a->int_mask = 0;

	return true;
}

static inline u16 esas2r_targ_get_id(struct esas2r_target *t,
				     struct esas2r_adapter *a)
{
	return (u16)(uintptr_t)(t - a->targetdb);
}

/*  Build and start an asynchronous event request */
static inline void esas2r_start_ae_request(struct esas2r_adapter *a,
					   struct esas2r_request *rq)
{
	unsigned long flags;

	esas2r_build_ae_req(a, rq);

	spin_lock_irqsave(&a->queue_lock, flags);
	esas2r_start_vda_request(a, rq);
	spin_unlock_irqrestore(&a->queue_lock, flags);
}

static inline void esas2r_comp_list_drain(struct esas2r_adapter *a,
					  struct list_head *comp_list)
{
	struct esas2r_request *rq;
	struct list_head *element, *next;

	list_for_each_safe(element, next, comp_list) {
		rq = list_entry(element, struct esas2r_request, comp_list);
		list_del_init(element);
		esas2r_complete_request(a, rq);
	}
}

/* sysfs handlers */
extern struct bin_attribute bin_attr_fw;
extern struct bin_attribute bin_attr_fs;
extern struct bin_attribute bin_attr_vda;
extern struct bin_attribute bin_attr_hw;
extern struct bin_attribute bin_attr_live_nvram;
extern struct bin_attribute bin_attr_default_nvram;

#endif /* ESAS2R_H */
