#define DRV_NAME "advansys"
#define ASC_VERSION "3.4"	/* AdvanSys Driver Version */

/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 *
 * Copyright (c) 1995-2000 Advanced System Products, Inc.
 * Copyright (c) 2000-2001 ConnectCom Solutions, Inc.
 * Copyright (c) 2007 Matthew Wilcox <matthew@wil.cx>
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * As of March 8, 2000 Advanced System Products, Inc. (AdvanSys)
 * changed its name to ConnectCom Solutions, Inc.
 * On June 18, 2001 Initio Corp. acquired ConnectCom's SCSI assets
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/isa.h>
#include <linux/eisa.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/dma.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>

/* FIXME:
 *
 *  1. Although all of the necessary command mapping places have the
 *     appropriate dma_map.. APIs, the driver still processes its internal
 *     queue using bus_to_virt() and virt_to_bus() which are illegal under
 *     the API.  The entire queue processing structure will need to be
 *     altered to fix this.
 *  2. Need to add memory mapping workaround. Test the memory mapping.
 *     If it doesn't work revert to I/O port access. Can a test be done
 *     safely?
 *  3. Handle an interrupt not working. Keep an interrupt counter in
 *     the interrupt handler. In the timeout function if the interrupt
 *     has not occurred then print a message and run in polled mode.
 *  4. Need to add support for target mode commands, cf. CAM XPT.
 *  5. check DMA mapping functions for failure
 *  6. Use scsi_transport_spi
 *  7. advansys_info is not safe against multiple simultaneous callers
 *  8. Add module_param to override ISA/VLB ioport array
 */
#warning this driver is still not properly converted to the DMA API

/* Enable driver /proc statistics. */
#define ADVANSYS_STATS

/* Enable driver tracing. */
#undef ADVANSYS_DEBUG

/*
 * Portable Data Types
 *
 * Any instance where a 32-bit long or pointer type is assumed
 * for precision or HW defined structures, the following define
 * types must be used. In Linux the char, short, and int types
 * are all consistent at 8, 16, and 32 bits respectively. Pointers
 * and long types are 64 bits on Alpha and UltraSPARC.
 */
#define ASC_PADDR __u32		/* Physical/Bus address data type. */
#define ASC_VADDR __u32		/* Virtual address data type. */
#define ASC_DCNT  __u32		/* Unsigned Data count type. */
#define ASC_SDCNT __s32		/* Signed Data count type. */

typedef unsigned char uchar;

#ifndef TRUE
#define TRUE     (1)
#endif
#ifndef FALSE
#define FALSE    (0)
#endif

#define ERR      (-1)
#define UW_ERR   (uint)(0xFFFF)
#define isodd_word(val)   ((((uint)val) & (uint)0x0001) != 0)

#define PCI_VENDOR_ID_ASP		0x10cd
#define PCI_DEVICE_ID_ASP_1200A		0x1100
#define PCI_DEVICE_ID_ASP_ABP940	0x1200
#define PCI_DEVICE_ID_ASP_ABP940U	0x1300
#define PCI_DEVICE_ID_ASP_ABP940UW	0x2300
#define PCI_DEVICE_ID_38C0800_REV1	0x2500
#define PCI_DEVICE_ID_38C1600_REV1	0x2700

/*
 * Enable CC_VERY_LONG_SG_LIST to support up to 64K element SG lists.
 * The SRB structure will have to be changed and the ASC_SRB2SCSIQ()
 * macro re-defined to be able to obtain a ASC_SCSI_Q pointer from the
 * SRB structure.
 */
#define CC_VERY_LONG_SG_LIST 0
#define ASC_SRB2SCSIQ(srb_ptr)  (srb_ptr)

#define PortAddr                 unsigned int	/* port address size  */
#define inp(port)                inb(port)
#define outp(port, byte)         outb((byte), (port))

#define inpw(port)               inw(port)
#define outpw(port, word)        outw((word), (port))

#define ASC_MAX_SG_QUEUE    7
#define ASC_MAX_SG_LIST     255

#define ASC_CS_TYPE  unsigned short

#define ASC_IS_ISA          (0x0001)
#define ASC_IS_ISAPNP       (0x0081)
#define ASC_IS_EISA         (0x0002)
#define ASC_IS_PCI          (0x0004)
#define ASC_IS_PCI_ULTRA    (0x0104)
#define ASC_IS_PCMCIA       (0x0008)
#define ASC_IS_MCA          (0x0020)
#define ASC_IS_VL           (0x0040)
#define ASC_IS_WIDESCSI_16  (0x0100)
#define ASC_IS_WIDESCSI_32  (0x0200)
#define ASC_IS_BIG_ENDIAN   (0x8000)

#define ASC_CHIP_MIN_VER_VL      (0x01)
#define ASC_CHIP_MAX_VER_VL      (0x07)
#define ASC_CHIP_MIN_VER_PCI     (0x09)
#define ASC_CHIP_MAX_VER_PCI     (0x0F)
#define ASC_CHIP_VER_PCI_BIT     (0x08)
#define ASC_CHIP_MIN_VER_ISA     (0x11)
#define ASC_CHIP_MIN_VER_ISA_PNP (0x21)
#define ASC_CHIP_MAX_VER_ISA     (0x27)
#define ASC_CHIP_VER_ISA_BIT     (0x30)
#define ASC_CHIP_VER_ISAPNP_BIT  (0x20)
#define ASC_CHIP_VER_ASYN_BUG    (0x21)
#define ASC_CHIP_VER_PCI             0x08
#define ASC_CHIP_VER_PCI_ULTRA_3150  (ASC_CHIP_VER_PCI | 0x02)
#define ASC_CHIP_VER_PCI_ULTRA_3050  (ASC_CHIP_VER_PCI | 0x03)
#define ASC_CHIP_MIN_VER_EISA (0x41)
#define ASC_CHIP_MAX_VER_EISA (0x47)
#define ASC_CHIP_VER_EISA_BIT (0x40)
#define ASC_CHIP_LATEST_VER_EISA   ((ASC_CHIP_MIN_VER_EISA - 1) + 3)
#define ASC_MAX_VL_DMA_COUNT    (0x07FFFFFFL)
#define ASC_MAX_PCI_DMA_COUNT   (0xFFFFFFFFL)
#define ASC_MAX_ISA_DMA_COUNT   (0x00FFFFFFL)

#define ASC_SCSI_ID_BITS  3
#define ASC_SCSI_TIX_TYPE     uchar
#define ASC_ALL_DEVICE_BIT_SET  0xFF
#define ASC_SCSI_BIT_ID_TYPE  uchar
#define ASC_MAX_TID       7
#define ASC_MAX_LUN       7
#define ASC_SCSI_WIDTH_BIT_SET  0xFF
#define ASC_MAX_SENSE_LEN   32
#define ASC_MIN_SENSE_LEN   14
#define ASC_SCSI_RESET_HOLD_TIME_US  60

/*
 * Narrow boards only support 12-byte commands, while wide boards
 * extend to 16-byte commands.
 */
#define ASC_MAX_CDB_LEN     12
#define ADV_MAX_CDB_LEN     16

#define MS_SDTR_LEN    0x03
#define MS_WDTR_LEN    0x02

#define ASC_SG_LIST_PER_Q   7
#define QS_FREE        0x00
#define QS_READY       0x01
#define QS_DISC1       0x02
#define QS_DISC2       0x04
#define QS_BUSY        0x08
#define QS_ABORTED     0x40
#define QS_DONE        0x80
#define QC_NO_CALLBACK   0x01
#define QC_SG_SWAP_QUEUE 0x02
#define QC_SG_HEAD       0x04
#define QC_DATA_IN       0x08
#define QC_DATA_OUT      0x10
#define QC_URGENT        0x20
#define QC_MSG_OUT       0x40
#define QC_REQ_SENSE     0x80
#define QCSG_SG_XFER_LIST  0x02
#define QCSG_SG_XFER_MORE  0x04
#define QCSG_SG_XFER_END   0x08
#define QD_IN_PROGRESS       0x00
#define QD_NO_ERROR          0x01
#define QD_ABORTED_BY_HOST   0x02
#define QD_WITH_ERROR        0x04
#define QD_INVALID_REQUEST   0x80
#define QD_INVALID_HOST_NUM  0x81
#define QD_INVALID_DEVICE    0x82
#define QD_ERR_INTERNAL      0xFF
#define QHSTA_NO_ERROR               0x00
#define QHSTA_M_SEL_TIMEOUT          0x11
#define QHSTA_M_DATA_OVER_RUN        0x12
#define QHSTA_M_DATA_UNDER_RUN       0x12
#define QHSTA_M_UNEXPECTED_BUS_FREE  0x13
#define QHSTA_M_BAD_BUS_PHASE_SEQ    0x14
#define QHSTA_D_QDONE_SG_LIST_CORRUPTED 0x21
#define QHSTA_D_ASC_DVC_ERROR_CODE_SET  0x22
#define QHSTA_D_HOST_ABORT_FAILED       0x23
#define QHSTA_D_EXE_SCSI_Q_FAILED       0x24
#define QHSTA_D_EXE_SCSI_Q_BUSY_TIMEOUT 0x25
#define QHSTA_D_ASPI_NO_BUF_POOL        0x26
#define QHSTA_M_WTM_TIMEOUT         0x41
#define QHSTA_M_BAD_CMPL_STATUS_IN  0x42
#define QHSTA_M_NO_AUTO_REQ_SENSE   0x43
#define QHSTA_M_AUTO_REQ_SENSE_FAIL 0x44
#define QHSTA_M_TARGET_STATUS_BUSY  0x45
#define QHSTA_M_BAD_TAG_CODE        0x46
#define QHSTA_M_BAD_QUEUE_FULL_OR_BUSY  0x47
#define QHSTA_M_HUNG_REQ_SCSI_BUS_RESET 0x48
#define QHSTA_D_LRAM_CMP_ERROR        0x81
#define QHSTA_M_MICRO_CODE_ERROR_HALT 0xA1
#define ASC_FLAG_SCSIQ_REQ        0x01
#define ASC_FLAG_BIOS_SCSIQ_REQ   0x02
#define ASC_FLAG_BIOS_ASYNC_IO    0x04
#define ASC_FLAG_SRB_LINEAR_ADDR  0x08
#define ASC_FLAG_WIN16            0x10
#define ASC_FLAG_WIN32            0x20
#define ASC_FLAG_ISA_OVER_16MB    0x40
#define ASC_FLAG_DOS_VM_CALLBACK  0x80
#define ASC_TAG_FLAG_EXTRA_BYTES               0x10
#define ASC_TAG_FLAG_DISABLE_DISCONNECT        0x04
#define ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX  0x08
#define ASC_TAG_FLAG_DISABLE_CHK_COND_INT_HOST 0x40
#define ASC_SCSIQ_CPY_BEG              4
#define ASC_SCSIQ_SGHD_CPY_BEG         2
#define ASC_SCSIQ_B_FWD                0
#define ASC_SCSIQ_B_BWD                1
#define ASC_SCSIQ_B_STATUS             2
#define ASC_SCSIQ_B_QNO                3
#define ASC_SCSIQ_B_CNTL               4
#define ASC_SCSIQ_B_SG_QUEUE_CNT       5
#define ASC_SCSIQ_D_DATA_ADDR          8
#define ASC_SCSIQ_D_DATA_CNT          12
#define ASC_SCSIQ_B_SENSE_LEN         20
#define ASC_SCSIQ_DONE_INFO_BEG       22
#define ASC_SCSIQ_D_SRBPTR            22
#define ASC_SCSIQ_B_TARGET_IX         26
#define ASC_SCSIQ_B_CDB_LEN           28
#define ASC_SCSIQ_B_TAG_CODE          29
#define ASC_SCSIQ_W_VM_ID             30
#define ASC_SCSIQ_DONE_STATUS         32
#define ASC_SCSIQ_HOST_STATUS         33
#define ASC_SCSIQ_SCSI_STATUS         34
#define ASC_SCSIQ_CDB_BEG             36
#define ASC_SCSIQ_DW_REMAIN_XFER_ADDR 56
#define ASC_SCSIQ_DW_REMAIN_XFER_CNT  60
#define ASC_SCSIQ_B_FIRST_SG_WK_QP    48
#define ASC_SCSIQ_B_SG_WK_QP          49
#define ASC_SCSIQ_B_SG_WK_IX          50
#define ASC_SCSIQ_W_ALT_DC1           52
#define ASC_SCSIQ_B_LIST_CNT          6
#define ASC_SCSIQ_B_CUR_LIST_CNT      7
#define ASC_SGQ_B_SG_CNTL             4
#define ASC_SGQ_B_SG_HEAD_QP          5
#define ASC_SGQ_B_SG_LIST_CNT         6
#define ASC_SGQ_B_SG_CUR_LIST_CNT     7
#define ASC_SGQ_LIST_BEG              8
#define ASC_DEF_SCSI1_QNG    4
#define ASC_MAX_SCSI1_QNG    4
#define ASC_DEF_SCSI2_QNG    16
#define ASC_MAX_SCSI2_QNG    32
#define ASC_TAG_CODE_MASK    0x23
#define ASC_STOP_REQ_RISC_STOP      0x01
#define ASC_STOP_ACK_RISC_STOP      0x03
#define ASC_STOP_CLEAN_UP_BUSY_Q    0x10
#define ASC_STOP_CLEAN_UP_DISC_Q    0x20
#define ASC_STOP_HOST_REQ_RISC_HALT 0x40
#define ASC_TIDLUN_TO_IX(tid, lun)  (ASC_SCSI_TIX_TYPE)((tid) + ((lun)<<ASC_SCSI_ID_BITS))
#define ASC_TID_TO_TARGET_ID(tid)   (ASC_SCSI_BIT_ID_TYPE)(0x01 << (tid))
#define ASC_TIX_TO_TARGET_ID(tix)   (0x01 << ((tix) & ASC_MAX_TID))
#define ASC_TIX_TO_TID(tix)         ((tix) & ASC_MAX_TID)
#define ASC_TID_TO_TIX(tid)         ((tid) & ASC_MAX_TID)
#define ASC_TIX_TO_LUN(tix)         (((tix) >> ASC_SCSI_ID_BITS) & ASC_MAX_LUN)
#define ASC_QNO_TO_QADDR(q_no)      ((ASC_QADR_BEG)+((int)(q_no) << 6))

typedef struct asc_scsiq_1 {
	uchar status;
	uchar q_no;
	uchar cntl;
	uchar sg_queue_cnt;
	uchar target_id;
	uchar target_lun;
	ASC_PADDR data_addr;
	ASC_DCNT data_cnt;
	ASC_PADDR sense_addr;
	uchar sense_len;
	uchar extra_bytes;
} ASC_SCSIQ_1;

typedef struct asc_scsiq_2 {
	ASC_VADDR srb_ptr;
	uchar target_ix;
	uchar flag;
	uchar cdb_len;
	uchar tag_code;
	ushort vm_id;
} ASC_SCSIQ_2;

typedef struct asc_scsiq_3 {
	uchar done_stat;
	uchar host_stat;
	uchar scsi_stat;
	uchar scsi_msg;
} ASC_SCSIQ_3;

typedef struct asc_scsiq_4 {
	uchar cdb[ASC_MAX_CDB_LEN];
	uchar y_first_sg_list_qp;
	uchar y_working_sg_qp;
	uchar y_working_sg_ix;
	uchar y_res;
	ushort x_req_count;
	ushort x_reconnect_rtn;
	ASC_PADDR x_saved_data_addr;
	ASC_DCNT x_saved_data_cnt;
} ASC_SCSIQ_4;

typedef struct asc_q_done_info {
	ASC_SCSIQ_2 d2;
	ASC_SCSIQ_3 d3;
	uchar q_status;
	uchar q_no;
	uchar cntl;
	uchar sense_len;
	uchar extra_bytes;
	uchar res;
	ASC_DCNT remain_bytes;
} ASC_QDONE_INFO;

typedef struct asc_sg_list {
	ASC_PADDR addr;
	ASC_DCNT bytes;
} ASC_SG_LIST;

typedef struct asc_sg_head {
	ushort entry_cnt;
	ushort queue_cnt;
	ushort entry_to_copy;
	ushort res;
	ASC_SG_LIST sg_list[0];
} ASC_SG_HEAD;

typedef struct asc_scsi_q {
	ASC_SCSIQ_1 q1;
	ASC_SCSIQ_2 q2;
	uchar *cdbptr;
	ASC_SG_HEAD *sg_head;
	ushort remain_sg_entry_cnt;
	ushort next_sg_index;
} ASC_SCSI_Q;

typedef struct asc_scsi_req_q {
	ASC_SCSIQ_1 r1;
	ASC_SCSIQ_2 r2;
	uchar *cdbptr;
	ASC_SG_HEAD *sg_head;
	uchar *sense_ptr;
	ASC_SCSIQ_3 r3;
	uchar cdb[ASC_MAX_CDB_LEN];
	uchar sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_REQ_Q;

typedef struct asc_scsi_bios_req_q {
	ASC_SCSIQ_1 r1;
	ASC_SCSIQ_2 r2;
	uchar *cdbptr;
	ASC_SG_HEAD *sg_head;
	uchar *sense_ptr;
	ASC_SCSIQ_3 r3;
	uchar cdb[ASC_MAX_CDB_LEN];
	uchar sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_BIOS_REQ_Q;

typedef struct asc_risc_q {
	uchar fwd;
	uchar bwd;
	ASC_SCSIQ_1 i1;
	ASC_SCSIQ_2 i2;
	ASC_SCSIQ_3 i3;
	ASC_SCSIQ_4 i4;
} ASC_RISC_Q;

typedef struct asc_sg_list_q {
	uchar seq_no;
	uchar q_no;
	uchar cntl;
	uchar sg_head_qp;
	uchar sg_list_cnt;
	uchar sg_cur_list_cnt;
} ASC_SG_LIST_Q;

typedef struct asc_risc_sg_list_q {
	uchar fwd;
	uchar bwd;
	ASC_SG_LIST_Q sg;
	ASC_SG_LIST sg_list[7];
} ASC_RISC_SG_LIST_Q;

#define ASCQ_ERR_Q_STATUS             0x0D
#define ASCQ_ERR_CUR_QNG              0x17
#define ASCQ_ERR_SG_Q_LINKS           0x18
#define ASCQ_ERR_ISR_RE_ENTRY         0x1A
#define ASCQ_ERR_CRITICAL_RE_ENTRY    0x1B
#define ASCQ_ERR_ISR_ON_CRITICAL      0x1C

/*
 * Warning code values are set in ASC_DVC_VAR  'warn_code'.
 */
#define ASC_WARN_NO_ERROR             0x0000
#define ASC_WARN_IO_PORT_ROTATE       0x0001
#define ASC_WARN_EEPROM_CHKSUM        0x0002
#define ASC_WARN_IRQ_MODIFIED         0x0004
#define ASC_WARN_AUTO_CONFIG          0x0008
#define ASC_WARN_CMD_QNG_CONFLICT     0x0010
#define ASC_WARN_EEPROM_RECOVER       0x0020
#define ASC_WARN_CFG_MSW_RECOVER      0x0040

/*
 * Error code values are set in {ASC/ADV}_DVC_VAR  'err_code'.
 */
#define ASC_IERR_NO_CARRIER		0x0001	/* No more carrier memory */
#define ASC_IERR_MCODE_CHKSUM		0x0002	/* micro code check sum error */
#define ASC_IERR_SET_PC_ADDR		0x0004
#define ASC_IERR_START_STOP_CHIP	0x0008	/* start/stop chip failed */
#define ASC_IERR_ILLEGAL_CONNECTION	0x0010	/* Illegal cable connection */
#define ASC_IERR_SINGLE_END_DEVICE	0x0020	/* SE device on DIFF bus */
#define ASC_IERR_REVERSED_CABLE		0x0040	/* Narrow flat cable reversed */
#define ASC_IERR_SET_SCSI_ID		0x0080	/* set SCSI ID failed */
#define ASC_IERR_HVD_DEVICE		0x0100	/* HVD device on LVD port */
#define ASC_IERR_BAD_SIGNATURE		0x0200	/* signature not found */
#define ASC_IERR_NO_BUS_TYPE		0x0400
#define ASC_IERR_BIST_PRE_TEST		0x0800	/* BIST pre-test error */
#define ASC_IERR_BIST_RAM_TEST		0x1000	/* BIST RAM test error */
#define ASC_IERR_BAD_CHIPTYPE		0x2000	/* Invalid chip_type setting */

#define ASC_DEF_MAX_TOTAL_QNG   (0xF0)
#define ASC_MIN_TAG_Q_PER_DVC   (0x04)
#define ASC_MIN_FREE_Q        (0x02)
#define ASC_MIN_TOTAL_QNG     ((ASC_MAX_SG_QUEUE)+(ASC_MIN_FREE_Q))
#define ASC_MAX_TOTAL_QNG 240
#define ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG 16
#define ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG   8
#define ASC_MAX_PCI_INRAM_TOTAL_QNG  20
#define ASC_MAX_INRAM_TAG_QNG   16
#define ASC_IOADR_GAP   0x10
#define ASC_SYN_MAX_OFFSET         0x0F
#define ASC_DEF_SDTR_OFFSET        0x0F
#define ASC_SDTR_ULTRA_PCI_10MB_INDEX  0x02
#define ASYN_SDTR_DATA_FIX_PCI_REV_AB 0x41

/* The narrow chip only supports a limited selection of transfer rates.
 * These are encoded in the range 0..7 or 0..15 depending whether the chip
 * is Ultra-capable or not.  These tables let us convert from one to the other.
 */
static const unsigned char asc_syn_xfer_period[8] = {
	25, 30, 35, 40, 50, 60, 70, 85
};

static const unsigned char asc_syn_ultra_xfer_period[16] = {
	12, 19, 25, 32, 38, 44, 50, 57, 63, 69, 75, 82, 88, 94, 100, 107
};

typedef struct ext_msg {
	uchar msg_type;
	uchar msg_len;
	uchar msg_req;
	union {
		struct {
			uchar sdtr_xfer_period;
			uchar sdtr_req_ack_offset;
		} sdtr;
		struct {
			uchar wdtr_width;
		} wdtr;
		struct {
			uchar mdp_b3;
			uchar mdp_b2;
			uchar mdp_b1;
			uchar mdp_b0;
		} mdp;
	} u_ext_msg;
	uchar res;
} EXT_MSG;

#define xfer_period     u_ext_msg.sdtr.sdtr_xfer_period
#define req_ack_offset  u_ext_msg.sdtr.sdtr_req_ack_offset
#define wdtr_width      u_ext_msg.wdtr.wdtr_width
#define mdp_b3          u_ext_msg.mdp_b3
#define mdp_b2          u_ext_msg.mdp_b2
#define mdp_b1          u_ext_msg.mdp_b1
#define mdp_b0          u_ext_msg.mdp_b0

typedef struct asc_dvc_cfg {
	ASC_SCSI_BIT_ID_TYPE can_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE cmd_qng_enabled;
	ASC_SCSI_BIT_ID_TYPE disc_enable;
	ASC_SCSI_BIT_ID_TYPE sdtr_enable;
	uchar chip_scsi_id;
	uchar isa_dma_speed;
	uchar isa_dma_channel;
	uchar chip_version;
	ushort mcode_date;
	ushort mcode_version;
	uchar max_tag_qng[ASC_MAX_TID + 1];
	uchar sdtr_period_offset[ASC_MAX_TID + 1];
	uchar adapter_info[6];
} ASC_DVC_CFG;

#define ASC_DEF_DVC_CNTL       0xFFFF
#define ASC_DEF_CHIP_SCSI_ID   7
#define ASC_DEF_ISA_DMA_SPEED  4
#define ASC_INIT_STATE_BEG_GET_CFG   0x0001
#define ASC_INIT_STATE_END_GET_CFG   0x0002
#define ASC_INIT_STATE_BEG_SET_CFG   0x0004
#define ASC_INIT_STATE_END_SET_CFG   0x0008
#define ASC_INIT_STATE_BEG_LOAD_MC   0x0010
#define ASC_INIT_STATE_END_LOAD_MC   0x0020
#define ASC_INIT_STATE_BEG_INQUIRY   0x0040
#define ASC_INIT_STATE_END_INQUIRY   0x0080
#define ASC_INIT_RESET_SCSI_DONE     0x0100
#define ASC_INIT_STATE_WITHOUT_EEP   0x8000
#define ASC_BUG_FIX_IF_NOT_DWB       0x0001
#define ASC_BUG_FIX_ASYN_USE_SYN     0x0002
#define ASC_MIN_TAGGED_CMD  7
#define ASC_MAX_SCSI_RESET_WAIT      30
#define ASC_OVERRUN_BSIZE		64

struct asc_dvc_var;		/* Forward Declaration. */

typedef struct asc_dvc_var {
	PortAddr iop_base;
	ushort err_code;
	ushort dvc_cntl;
	ushort bug_fix_cntl;
	ushort bus_type;
	ASC_SCSI_BIT_ID_TYPE init_sdtr;
	ASC_SCSI_BIT_ID_TYPE sdtr_done;
	ASC_SCSI_BIT_ID_TYPE use_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE unit_not_ready;
	ASC_SCSI_BIT_ID_TYPE queue_full_or_busy;
	ASC_SCSI_BIT_ID_TYPE start_motor;
	uchar *overrun_buf;
	dma_addr_t overrun_dma;
	uchar scsi_reset_wait;
	uchar chip_no;
	char is_in_int;
	uchar max_total_qng;
	uchar cur_total_qng;
	uchar in_critical_cnt;
	uchar last_q_shortage;
	ushort init_state;
	uchar cur_dvc_qng[ASC_MAX_TID + 1];
	uchar max_dvc_qng[ASC_MAX_TID + 1];
	ASC_SCSI_Q *scsiq_busy_head[ASC_MAX_TID + 1];
	ASC_SCSI_Q *scsiq_busy_tail[ASC_MAX_TID + 1];
	const uchar *sdtr_period_tbl;
	ASC_DVC_CFG *cfg;
	ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer_always;
	char redo_scam;
	ushort res2;
	uchar dos_int13_table[ASC_MAX_TID + 1];
	ASC_DCNT max_dma_count;
	ASC_SCSI_BIT_ID_TYPE no_scam;
	ASC_SCSI_BIT_ID_TYPE pci_fix_asyn_xfer;
	uchar min_sdtr_index;
	uchar max_sdtr_index;
	struct asc_board *drv_ptr;
	int ptr_map_count;
	void **ptr_map;
	ASC_DCNT uc_break;
} ASC_DVC_VAR;

typedef struct asc_dvc_inq_info {
	uchar type[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_DVC_INQ_INFO;

typedef struct asc_cap_info {
	ASC_DCNT lba;
	ASC_DCNT blk_size;
} ASC_CAP_INFO;

typedef struct asc_cap_info_array {
	ASC_CAP_INFO cap_info[ASC_MAX_TID + 1][ASC_MAX_LUN + 1];
} ASC_CAP_INFO_ARRAY;

#define ASC_MCNTL_NO_SEL_TIMEOUT  (ushort)0x0001
#define ASC_MCNTL_NULL_TARGET     (ushort)0x0002
#define ASC_CNTL_INITIATOR         (ushort)0x0001
#define ASC_CNTL_BIOS_GT_1GB       (ushort)0x0002
#define ASC_CNTL_BIOS_GT_2_DISK    (ushort)0x0004
#define ASC_CNTL_BIOS_REMOVABLE    (ushort)0x0008
#define ASC_CNTL_NO_SCAM           (ushort)0x0010
#define ASC_CNTL_INT_MULTI_Q       (ushort)0x0080
#define ASC_CNTL_NO_LUN_SUPPORT    (ushort)0x0040
#define ASC_CNTL_NO_VERIFY_COPY    (ushort)0x0100
#define ASC_CNTL_RESET_SCSI        (ushort)0x0200
#define ASC_CNTL_INIT_INQUIRY      (ushort)0x0400
#define ASC_CNTL_INIT_VERBOSE      (ushort)0x0800
#define ASC_CNTL_SCSI_PARITY       (ushort)0x1000
#define ASC_CNTL_BURST_MODE        (ushort)0x2000
#define ASC_CNTL_SDTR_ENABLE_ULTRA (ushort)0x4000
#define ASC_EEP_DVC_CFG_BEG_VL    2
#define ASC_EEP_MAX_DVC_ADDR_VL   15
#define ASC_EEP_DVC_CFG_BEG      32
#define ASC_EEP_MAX_DVC_ADDR     45
#define ASC_EEP_MAX_RETRY        20

/*
 * These macros keep the chip SCSI id and ISA DMA speed
 * bitfields in board order. C bitfields aren't portable
 * between big and little-endian platforms so they are
 * not used.
 */

#define ASC_EEP_GET_CHIP_ID(cfg)    ((cfg)->id_speed & 0x0f)
#define ASC_EEP_GET_DMA_SPD(cfg)    (((cfg)->id_speed & 0xf0) >> 4)
#define ASC_EEP_SET_CHIP_ID(cfg, sid) \
   ((cfg)->id_speed = ((cfg)->id_speed & 0xf0) | ((sid) & ASC_MAX_TID))
#define ASC_EEP_SET_DMA_SPD(cfg, spd) \
   ((cfg)->id_speed = ((cfg)->id_speed & 0x0f) | ((spd) & 0x0f) << 4)

typedef struct asceep_config {
	ushort cfg_lsw;
	ushort cfg_msw;
	uchar init_sdtr;
	uchar disc_enable;
	uchar use_cmd_qng;
	uchar start_motor;
	uchar max_total_qng;
	uchar max_tag_qng;
	uchar bios_scan;
	uchar power_up_wait;
	uchar no_scam;
	uchar id_speed;		/* low order 4 bits is chip scsi id */
	/* high order 4 bits is isa dma speed */
	uchar dos_int13_table[ASC_MAX_TID + 1];
	uchar adapter_info[6];
	ushort cntl;
	ushort chksum;
} ASCEEP_CONFIG;

#define ASC_EEP_CMD_READ          0x80
#define ASC_EEP_CMD_WRITE         0x40
#define ASC_EEP_CMD_WRITE_ABLE    0x30
#define ASC_EEP_CMD_WRITE_DISABLE 0x00
#define ASCV_MSGOUT_BEG         0x0000
#define ASCV_MSGOUT_SDTR_PERIOD (ASCV_MSGOUT_BEG+3)
#define ASCV_MSGOUT_SDTR_OFFSET (ASCV_MSGOUT_BEG+4)
#define ASCV_BREAK_SAVED_CODE   (ushort)0x0006
#define ASCV_MSGIN_BEG          (ASCV_MSGOUT_BEG+8)
#define ASCV_MSGIN_SDTR_PERIOD  (ASCV_MSGIN_BEG+3)
#define ASCV_MSGIN_SDTR_OFFSET  (ASCV_MSGIN_BEG+4)
#define ASCV_SDTR_DATA_BEG      (ASCV_MSGIN_BEG+8)
#define ASCV_SDTR_DONE_BEG      (ASCV_SDTR_DATA_BEG+8)
#define ASCV_MAX_DVC_QNG_BEG    (ushort)0x0020
#define ASCV_BREAK_ADDR           (ushort)0x0028
#define ASCV_BREAK_NOTIFY_COUNT   (ushort)0x002A
#define ASCV_BREAK_CONTROL        (ushort)0x002C
#define ASCV_BREAK_HIT_COUNT      (ushort)0x002E

#define ASCV_ASCDVC_ERR_CODE_W  (ushort)0x0030
#define ASCV_MCODE_CHKSUM_W   (ushort)0x0032
#define ASCV_MCODE_SIZE_W     (ushort)0x0034
#define ASCV_STOP_CODE_B      (ushort)0x0036
#define ASCV_DVC_ERR_CODE_B   (ushort)0x0037
#define ASCV_OVERRUN_PADDR_D  (ushort)0x0038
#define ASCV_OVERRUN_BSIZE_D  (ushort)0x003C
#define ASCV_HALTCODE_W       (ushort)0x0040
#define ASCV_CHKSUM_W         (ushort)0x0042
#define ASCV_MC_DATE_W        (ushort)0x0044
#define ASCV_MC_VER_W         (ushort)0x0046
#define ASCV_NEXTRDY_B        (ushort)0x0048
#define ASCV_DONENEXT_B       (ushort)0x0049
#define ASCV_USE_TAGGED_QNG_B (ushort)0x004A
#define ASCV_SCSIBUSY_B       (ushort)0x004B
#define ASCV_Q_DONE_IN_PROGRESS_B  (ushort)0x004C
#define ASCV_CURCDB_B         (ushort)0x004D
#define ASCV_RCLUN_B          (ushort)0x004E
#define ASCV_BUSY_QHEAD_B     (ushort)0x004F
#define ASCV_DISC1_QHEAD_B    (ushort)0x0050
#define ASCV_DISC_ENABLE_B    (ushort)0x0052
#define ASCV_CAN_TAGGED_QNG_B (ushort)0x0053
#define ASCV_HOSTSCSI_ID_B    (ushort)0x0055
#define ASCV_MCODE_CNTL_B     (ushort)0x0056
#define ASCV_NULL_TARGET_B    (ushort)0x0057
#define ASCV_FREE_Q_HEAD_W    (ushort)0x0058
#define ASCV_DONE_Q_TAIL_W    (ushort)0x005A
#define ASCV_FREE_Q_HEAD_B    (ushort)(ASCV_FREE_Q_HEAD_W+1)
#define ASCV_DONE_Q_TAIL_B    (ushort)(ASCV_DONE_Q_TAIL_W+1)
#define ASCV_HOST_FLAG_B      (ushort)0x005D
#define ASCV_TOTAL_READY_Q_B  (ushort)0x0064
#define ASCV_VER_SERIAL_B     (ushort)0x0065
#define ASCV_HALTCODE_SAVED_W (ushort)0x0066
#define ASCV_WTM_FLAG_B       (ushort)0x0068
#define ASCV_RISC_FLAG_B      (ushort)0x006A
#define ASCV_REQ_SG_LIST_QP   (ushort)0x006B
#define ASC_HOST_FLAG_IN_ISR        0x01
#define ASC_HOST_FLAG_ACK_INT       0x02
#define ASC_RISC_FLAG_GEN_INT      0x01
#define ASC_RISC_FLAG_REQ_SG_LIST  0x02
#define IOP_CTRL         (0x0F)
#define IOP_STATUS       (0x0E)
#define IOP_INT_ACK      IOP_STATUS
#define IOP_REG_IFC      (0x0D)
#define IOP_SYN_OFFSET    (0x0B)
#define IOP_EXTRA_CONTROL (0x0D)
#define IOP_REG_PC        (0x0C)
#define IOP_RAM_ADDR      (0x0A)
#define IOP_RAM_DATA      (0x08)
#define IOP_EEP_DATA      (0x06)
#define IOP_EEP_CMD       (0x07)
#define IOP_VERSION       (0x03)
#define IOP_CONFIG_HIGH   (0x04)
#define IOP_CONFIG_LOW    (0x02)
#define IOP_SIG_BYTE      (0x01)
#define IOP_SIG_WORD      (0x00)
#define IOP_REG_DC1      (0x0E)
#define IOP_REG_DC0      (0x0C)
#define IOP_REG_SB       (0x0B)
#define IOP_REG_DA1      (0x0A)
#define IOP_REG_DA0      (0x08)
#define IOP_REG_SC       (0x09)
#define IOP_DMA_SPEED    (0x07)
#define IOP_REG_FLAG     (0x07)
#define IOP_FIFO_H       (0x06)
#define IOP_FIFO_L       (0x04)
#define IOP_REG_ID       (0x05)
#define IOP_REG_QP       (0x03)
#define IOP_REG_IH       (0x02)
#define IOP_REG_IX       (0x01)
#define IOP_REG_AX       (0x00)
#define IFC_REG_LOCK      (0x00)
#define IFC_REG_UNLOCK    (0x09)
#define IFC_WR_EN_FILTER  (0x10)
#define IFC_RD_NO_EEPROM  (0x10)
#define IFC_SLEW_RATE     (0x20)
#define IFC_ACT_NEG       (0x40)
#define IFC_INP_FILTER    (0x80)
#define IFC_INIT_DEFAULT  (IFC_ACT_NEG | IFC_REG_UNLOCK)
#define SC_SEL   (uchar)(0x80)
#define SC_BSY   (uchar)(0x40)
#define SC_ACK   (uchar)(0x20)
#define SC_REQ   (uchar)(0x10)
#define SC_ATN   (uchar)(0x08)
#define SC_IO    (uchar)(0x04)
#define SC_CD    (uchar)(0x02)
#define SC_MSG   (uchar)(0x01)
#define SEC_SCSI_CTL         (uchar)(0x80)
#define SEC_ACTIVE_NEGATE    (uchar)(0x40)
#define SEC_SLEW_RATE        (uchar)(0x20)
#define SEC_ENABLE_FILTER    (uchar)(0x10)
#define ASC_HALT_EXTMSG_IN     (ushort)0x8000
#define ASC_HALT_CHK_CONDITION (ushort)0x8100
#define ASC_HALT_SS_QUEUE_FULL (ushort)0x8200
#define ASC_HALT_DISABLE_ASYN_USE_SYN_FIX  (ushort)0x8300
#define ASC_HALT_ENABLE_ASYN_USE_SYN_FIX   (ushort)0x8400
#define ASC_HALT_SDTR_REJECTED (ushort)0x4000
#define ASC_HALT_HOST_COPY_SG_LIST_TO_RISC ( ushort )0x2000
#define ASC_MAX_QNO        0xF8
#define ASC_DATA_SEC_BEG   (ushort)0x0080
#define ASC_DATA_SEC_END   (ushort)0x0080
#define ASC_CODE_SEC_BEG   (ushort)0x0080
#define ASC_CODE_SEC_END   (ushort)0x0080
#define ASC_QADR_BEG       (0x4000)
#define ASC_QADR_USED      (ushort)(ASC_MAX_QNO * 64)
#define ASC_QADR_END       (ushort)0x7FFF
#define ASC_QLAST_ADR      (ushort)0x7FC0
#define ASC_QBLK_SIZE      0x40
#define ASC_BIOS_DATA_QBEG 0xF8
#define ASC_MIN_ACTIVE_QNO 0x01
#define ASC_QLINK_END      0xFF
#define ASC_EEPROM_WORDS   0x10
#define ASC_MAX_MGS_LEN    0x10
#define ASC_BIOS_ADDR_DEF  0xDC00
#define ASC_BIOS_SIZE      0x3800
#define ASC_BIOS_RAM_OFF   0x3800
#define ASC_BIOS_RAM_SIZE  0x800
#define ASC_BIOS_MIN_ADDR  0xC000
#define ASC_BIOS_MAX_ADDR  0xEC00
#define ASC_BIOS_BANK_SIZE 0x0400
#define ASC_MCODE_START_ADDR  0x0080
#define ASC_CFG0_HOST_INT_ON    0x0020
#define ASC_CFG0_BIOS_ON        0x0040
#define ASC_CFG0_VERA_BURST_ON  0x0080
#define ASC_CFG0_SCSI_PARITY_ON 0x0800
#define ASC_CFG1_SCSI_TARGET_ON 0x0080
#define ASC_CFG1_LRAM_8BITS_ON  0x0800
#define ASC_CFG_MSW_CLR_MASK    0x3080
#define CSW_TEST1             (ASC_CS_TYPE)0x8000
#define CSW_AUTO_CONFIG       (ASC_CS_TYPE)0x4000
#define CSW_RESERVED1         (ASC_CS_TYPE)0x2000
#define CSW_IRQ_WRITTEN       (ASC_CS_TYPE)0x1000
#define CSW_33MHZ_SELECTED    (ASC_CS_TYPE)0x0800
#define CSW_TEST2             (ASC_CS_TYPE)0x0400
#define CSW_TEST3             (ASC_CS_TYPE)0x0200
#define CSW_RESERVED2         (ASC_CS_TYPE)0x0100
#define CSW_DMA_DONE          (ASC_CS_TYPE)0x0080
#define CSW_FIFO_RDY          (ASC_CS_TYPE)0x0040
#define CSW_EEP_READ_DONE     (ASC_CS_TYPE)0x0020
#define CSW_HALTED            (ASC_CS_TYPE)0x0010
#define CSW_SCSI_RESET_ACTIVE (ASC_CS_TYPE)0x0008
#define CSW_PARITY_ERR        (ASC_CS_TYPE)0x0004
#define CSW_SCSI_RESET_LATCH  (ASC_CS_TYPE)0x0002
#define CSW_INT_PENDING       (ASC_CS_TYPE)0x0001
#define CIW_CLR_SCSI_RESET_INT (ASC_CS_TYPE)0x1000
#define CIW_INT_ACK      (ASC_CS_TYPE)0x0100
#define CIW_TEST1        (ASC_CS_TYPE)0x0200
#define CIW_TEST2        (ASC_CS_TYPE)0x0400
#define CIW_SEL_33MHZ    (ASC_CS_TYPE)0x0800
#define CIW_IRQ_ACT      (ASC_CS_TYPE)0x1000
#define CC_CHIP_RESET   (uchar)0x80
#define CC_SCSI_RESET   (uchar)0x40
#define CC_HALT         (uchar)0x20
#define CC_SINGLE_STEP  (uchar)0x10
#define CC_DMA_ABLE     (uchar)0x08
#define CC_TEST         (uchar)0x04
#define CC_BANK_ONE     (uchar)0x02
#define CC_DIAG         (uchar)0x01
#define ASC_1000_ID0W      0x04C1
#define ASC_1000_ID0W_FIX  0x00C1
#define ASC_1000_ID1B      0x25
#define ASC_EISA_REV_IOP_MASK  (0x0C83)
#define ASC_EISA_CFG_IOP_MASK  (0x0C86)
#define ASC_GET_EISA_SLOT(iop)  (PortAddr)((iop) & 0xF000)
#define INS_HALTINT        (ushort)0x6281
#define INS_HALT           (ushort)0x6280
#define INS_SINT           (ushort)0x6200
#define INS_RFLAG_WTM      (ushort)0x7380
#define ASC_MC_SAVE_CODE_WSIZE  0x500
#define ASC_MC_SAVE_DATA_WSIZE  0x40

typedef struct asc_mc_saved {
	ushort data[ASC_MC_SAVE_DATA_WSIZE];
	ushort code[ASC_MC_SAVE_CODE_WSIZE];
} ASC_MC_SAVED;

#define AscGetQDoneInProgress(port)         AscReadLramByte((port), ASCV_Q_DONE_IN_PROGRESS_B)
#define AscPutQDoneInProgress(port, val)    AscWriteLramByte((port), ASCV_Q_DONE_IN_PROGRESS_B, val)
#define AscGetVarFreeQHead(port)            AscReadLramWord((port), ASCV_FREE_Q_HEAD_W)
#define AscGetVarDoneQTail(port)            AscReadLramWord((port), ASCV_DONE_Q_TAIL_W)
#define AscPutVarFreeQHead(port, val)       AscWriteLramWord((port), ASCV_FREE_Q_HEAD_W, val)
#define AscPutVarDoneQTail(port, val)       AscWriteLramWord((port), ASCV_DONE_Q_TAIL_W, val)
#define AscGetRiscVarFreeQHead(port)        AscReadLramByte((port), ASCV_NEXTRDY_B)
#define AscGetRiscVarDoneQTail(port)        AscReadLramByte((port), ASCV_DONENEXT_B)
#define AscPutRiscVarFreeQHead(port, val)   AscWriteLramByte((port), ASCV_NEXTRDY_B, val)
#define AscPutRiscVarDoneQTail(port, val)   AscWriteLramByte((port), ASCV_DONENEXT_B, val)
#define AscPutMCodeSDTRDoneAtID(port, id, data)  AscWriteLramByte((port), (ushort)((ushort)ASCV_SDTR_DONE_BEG+(ushort)id), (data))
#define AscGetMCodeSDTRDoneAtID(port, id)        AscReadLramByte((port), (ushort)((ushort)ASCV_SDTR_DONE_BEG+(ushort)id))
#define AscPutMCodeInitSDTRAtID(port, id, data)  AscWriteLramByte((port), (ushort)((ushort)ASCV_SDTR_DATA_BEG+(ushort)id), data)
#define AscGetMCodeInitSDTRAtID(port, id)        AscReadLramByte((port), (ushort)((ushort)ASCV_SDTR_DATA_BEG+(ushort)id))
#define AscGetChipSignatureByte(port)     (uchar)inp((port)+IOP_SIG_BYTE)
#define AscGetChipSignatureWord(port)     (ushort)inpw((port)+IOP_SIG_WORD)
#define AscGetChipVerNo(port)             (uchar)inp((port)+IOP_VERSION)
#define AscGetChipCfgLsw(port)            (ushort)inpw((port)+IOP_CONFIG_LOW)
#define AscGetChipCfgMsw(port)            (ushort)inpw((port)+IOP_CONFIG_HIGH)
#define AscSetChipCfgLsw(port, data)      outpw((port)+IOP_CONFIG_LOW, data)
#define AscSetChipCfgMsw(port, data)      outpw((port)+IOP_CONFIG_HIGH, data)
#define AscGetChipEEPCmd(port)            (uchar)inp((port)+IOP_EEP_CMD)
#define AscSetChipEEPCmd(port, data)      outp((port)+IOP_EEP_CMD, data)
#define AscGetChipEEPData(port)           (ushort)inpw((port)+IOP_EEP_DATA)
#define AscSetChipEEPData(port, data)     outpw((port)+IOP_EEP_DATA, data)
#define AscGetChipLramAddr(port)          (ushort)inpw((PortAddr)((port)+IOP_RAM_ADDR))
#define AscSetChipLramAddr(port, addr)    outpw((PortAddr)((port)+IOP_RAM_ADDR), addr)
#define AscGetChipLramData(port)          (ushort)inpw((port)+IOP_RAM_DATA)
#define AscSetChipLramData(port, data)    outpw((port)+IOP_RAM_DATA, data)
#define AscGetChipIFC(port)               (uchar)inp((port)+IOP_REG_IFC)
#define AscSetChipIFC(port, data)          outp((port)+IOP_REG_IFC, data)
#define AscGetChipStatus(port)            (ASC_CS_TYPE)inpw((port)+IOP_STATUS)
#define AscSetChipStatus(port, cs_val)    outpw((port)+IOP_STATUS, cs_val)
#define AscGetChipControl(port)           (uchar)inp((port)+IOP_CTRL)
#define AscSetChipControl(port, cc_val)   outp((port)+IOP_CTRL, cc_val)
#define AscGetChipSyn(port)               (uchar)inp((port)+IOP_SYN_OFFSET)
#define AscSetChipSyn(port, data)         outp((port)+IOP_SYN_OFFSET, data)
#define AscSetPCAddr(port, data)          outpw((port)+IOP_REG_PC, data)
#define AscGetPCAddr(port)                (ushort)inpw((port)+IOP_REG_PC)
#define AscIsIntPending(port)             (AscGetChipStatus(port) & (CSW_INT_PENDING | CSW_SCSI_RESET_LATCH))
#define AscGetChipScsiID(port)            ((AscGetChipCfgLsw(port) >> 8) & ASC_MAX_TID)
#define AscGetExtraControl(port)          (uchar)inp((port)+IOP_EXTRA_CONTROL)
#define AscSetExtraControl(port, data)    outp((port)+IOP_EXTRA_CONTROL, data)
#define AscReadChipAX(port)               (ushort)inpw((port)+IOP_REG_AX)
#define AscWriteChipAX(port, data)        outpw((port)+IOP_REG_AX, data)
#define AscReadChipIX(port)               (uchar)inp((port)+IOP_REG_IX)
#define AscWriteChipIX(port, data)        outp((port)+IOP_REG_IX, data)
#define AscReadChipIH(port)               (ushort)inpw((port)+IOP_REG_IH)
#define AscWriteChipIH(port, data)        outpw((port)+IOP_REG_IH, data)
#define AscReadChipQP(port)               (uchar)inp((port)+IOP_REG_QP)
#define AscWriteChipQP(port, data)        outp((port)+IOP_REG_QP, data)
#define AscReadChipFIFO_L(port)           (ushort)inpw((port)+IOP_REG_FIFO_L)
#define AscWriteChipFIFO_L(port, data)    outpw((port)+IOP_REG_FIFO_L, data)
#define AscReadChipFIFO_H(port)           (ushort)inpw((port)+IOP_REG_FIFO_H)
#define AscWriteChipFIFO_H(port, data)    outpw((port)+IOP_REG_FIFO_H, data)
#define AscReadChipDmaSpeed(port)         (uchar)inp((port)+IOP_DMA_SPEED)
#define AscWriteChipDmaSpeed(port, data)  outp((port)+IOP_DMA_SPEED, data)
#define AscReadChipDA0(port)              (ushort)inpw((port)+IOP_REG_DA0)
#define AscWriteChipDA0(port)             outpw((port)+IOP_REG_DA0, data)
#define AscReadChipDA1(port)              (ushort)inpw((port)+IOP_REG_DA1)
#define AscWriteChipDA1(port)             outpw((port)+IOP_REG_DA1, data)
#define AscReadChipDC0(port)              (ushort)inpw((port)+IOP_REG_DC0)
#define AscWriteChipDC0(port)             outpw((port)+IOP_REG_DC0, data)
#define AscReadChipDC1(port)              (ushort)inpw((port)+IOP_REG_DC1)
#define AscWriteChipDC1(port)             outpw((port)+IOP_REG_DC1, data)
#define AscReadChipDvcID(port)            (uchar)inp((port)+IOP_REG_ID)
#define AscWriteChipDvcID(port, data)     outp((port)+IOP_REG_ID, data)

/*
 * Portable Data Types
 *
 * Any instance where a 32-bit long or pointer type is assumed
 * for precision or HW defined structures, the following define
 * types must be used. In Linux the char, short, and int types
 * are all consistent at 8, 16, and 32 bits respectively. Pointers
 * and long types are 64 bits on Alpha and UltraSPARC.
 */
#define ADV_PADDR __u32		/* Physical address data type. */
#define ADV_VADDR __u32		/* Virtual address data type. */
#define ADV_DCNT  __u32		/* Unsigned Data count type. */
#define ADV_SDCNT __s32		/* Signed Data count type. */

/*
 * These macros are used to convert a virtual address to a
 * 32-bit value. This currently can be used on Linux Alpha
 * which uses 64-bit virtual address but a 32-bit bus address.
 * This is likely to break in the future, but doing this now
 * will give us time to change the HW and FW to handle 64-bit
 * addresses.
 */
#define ADV_VADDR_TO_U32   virt_to_bus
#define ADV_U32_TO_VADDR   bus_to_virt

#define AdvPortAddr  void __iomem *	/* Virtual memory address size */

/*
 * Define Adv Library required memory access macros.
 */
#define ADV_MEM_READB(addr) readb(addr)
#define ADV_MEM_READW(addr) readw(addr)
#define ADV_MEM_WRITEB(addr, byte) writeb(byte, addr)
#define ADV_MEM_WRITEW(addr, word) writew(word, addr)
#define ADV_MEM_WRITEDW(addr, dword) writel(dword, addr)

#define ADV_CARRIER_COUNT (ASC_DEF_MAX_HOST_QNG + 15)

/*
 * Define total number of simultaneous maximum element scatter-gather
 * request blocks per wide adapter. ASC_DEF_MAX_HOST_QNG (253) is the
 * maximum number of outstanding commands per wide host adapter. Each
 * command uses one or more ADV_SG_BLOCK each with 15 scatter-gather
 * elements. Allow each command to have at least one ADV_SG_BLOCK structure.
 * This allows about 15 commands to have the maximum 17 ADV_SG_BLOCK
 * structures or 255 scatter-gather elements.
 */
#define ADV_TOT_SG_BLOCK        ASC_DEF_MAX_HOST_QNG

/*
 * Define maximum number of scatter-gather elements per request.
 */
#define ADV_MAX_SG_LIST         255
#define NO_OF_SG_PER_BLOCK              15

#define ADV_EEP_DVC_CFG_BEGIN           (0x00)
#define ADV_EEP_DVC_CFG_END             (0x15)
#define ADV_EEP_DVC_CTL_BEGIN           (0x16)	/* location of OEM name */
#define ADV_EEP_MAX_WORD_ADDR           (0x1E)

#define ADV_EEP_DELAY_MS                100

#define ADV_EEPROM_BIG_ENDIAN          0x8000	/* EEPROM Bit 15 */
#define ADV_EEPROM_BIOS_ENABLE         0x4000	/* EEPROM Bit 14 */
/*
 * For the ASC3550 Bit 13 is Termination Polarity control bit.
 * For later ICs Bit 13 controls whether the CIS (Card Information
 * Service Section) is loaded from EEPROM.
 */
#define ADV_EEPROM_TERM_POL            0x2000	/* EEPROM Bit 13 */
#define ADV_EEPROM_CIS_LD              0x2000	/* EEPROM Bit 13 */
/*
 * ASC38C1600 Bit 11
 *
 * If EEPROM Bit 11 is 0 for Function 0, then Function 0 will specify
 * INT A in the PCI Configuration Space Int Pin field. If it is 1, then
 * Function 0 will specify INT B.
 *
 * If EEPROM Bit 11 is 0 for Function 1, then Function 1 will specify
 * INT B in the PCI Configuration Space Int Pin field. If it is 1, then
 * Function 1 will specify INT A.
 */
#define ADV_EEPROM_INTAB               0x0800	/* EEPROM Bit 11 */

typedef struct adveep_3550_config {
	/* Word Offset, Description */

	ushort cfg_lsw;		/* 00 power up initialization */
	/*  bit 13 set - Term Polarity Control */
	/*  bit 14 set - BIOS Enable */
	/*  bit 15 set - Big Endian Mode */
	ushort cfg_msw;		/* 01 unused      */
	ushort disc_enable;	/* 02 disconnect enable */
	ushort wdtr_able;	/* 03 Wide DTR able */
	ushort sdtr_able;	/* 04 Synchronous DTR able */
	ushort start_motor;	/* 05 send start up motor */
	ushort tagqng_able;	/* 06 tag queuing able */
	ushort bios_scan;	/* 07 BIOS device control */
	ushort scam_tolerant;	/* 08 no scam */

	uchar adapter_scsi_id;	/* 09 Host Adapter ID */
	uchar bios_boot_delay;	/*    power up wait */

	uchar scsi_reset_delay;	/* 10 reset delay */
	uchar bios_id_lun;	/*    first boot device scsi id & lun */
	/*    high nibble is lun */
	/*    low nibble is scsi id */

	uchar termination;	/* 11 0 - automatic */
	/*    1 - low off / high off */
	/*    2 - low off / high on */
	/*    3 - low on  / high on */
	/*    There is no low on  / high off */

	uchar reserved1;	/*    reserved byte (not used) */

	ushort bios_ctrl;	/* 12 BIOS control bits */
	/*  bit 0  BIOS don't act as initiator. */
	/*  bit 1  BIOS > 1 GB support */
	/*  bit 2  BIOS > 2 Disk Support */
	/*  bit 3  BIOS don't support removables */
	/*  bit 4  BIOS support bootable CD */
	/*  bit 5  BIOS scan enabled */
	/*  bit 6  BIOS support multiple LUNs */
	/*  bit 7  BIOS display of message */
	/*  bit 8  SCAM disabled */
	/*  bit 9  Reset SCSI bus during init. */
	/*  bit 10 */
	/*  bit 11 No verbose initialization. */
	/*  bit 12 SCSI parity enabled */
	/*  bit 13 */
	/*  bit 14 */
	/*  bit 15 */
	ushort ultra_able;	/* 13 ULTRA speed able */
	ushort reserved2;	/* 14 reserved */
	uchar max_host_qng;	/* 15 maximum host queuing */
	uchar max_dvc_qng;	/*    maximum per device queuing */
	ushort dvc_cntl;	/* 16 control bit for driver */
	ushort bug_fix;		/* 17 control bit for bug fix */
	ushort serial_number_word1;	/* 18 Board serial number word 1 */
	ushort serial_number_word2;	/* 19 Board serial number word 2 */
	ushort serial_number_word3;	/* 20 Board serial number word 3 */
	ushort check_sum;	/* 21 EEP check sum */
	uchar oem_name[16];	/* 22 OEM name */
	ushort dvc_err_code;	/* 30 last device driver error code */
	ushort adv_err_code;	/* 31 last uc and Adv Lib error code */
	ushort adv_err_addr;	/* 32 last uc error address */
	ushort saved_dvc_err_code;	/* 33 saved last dev. driver error code   */
	ushort saved_adv_err_code;	/* 34 saved last uc and Adv Lib error code */
	ushort saved_adv_err_addr;	/* 35 saved last uc error address         */
	ushort num_of_err;	/* 36 number of error */
} ADVEEP_3550_CONFIG;

typedef struct adveep_38C0800_config {
	/* Word Offset, Description */

	ushort cfg_lsw;		/* 00 power up initialization */
	/*  bit 13 set - Load CIS */
	/*  bit 14 set - BIOS Enable */
	/*  bit 15 set - Big Endian Mode */
	ushort cfg_msw;		/* 01 unused      */
	ushort disc_enable;	/* 02 disconnect enable */
	ushort wdtr_able;	/* 03 Wide DTR able */
	ushort sdtr_speed1;	/* 04 SDTR Speed TID 0-3 */
	ushort start_motor;	/* 05 send start up motor */
	ushort tagqng_able;	/* 06 tag queuing able */
	ushort bios_scan;	/* 07 BIOS device control */
	ushort scam_tolerant;	/* 08 no scam */

	uchar adapter_scsi_id;	/* 09 Host Adapter ID */
	uchar bios_boot_delay;	/*    power up wait */

	uchar scsi_reset_delay;	/* 10 reset delay */
	uchar bios_id_lun;	/*    first boot device scsi id & lun */
	/*    high nibble is lun */
	/*    low nibble is scsi id */

	uchar termination_se;	/* 11 0 - automatic */
	/*    1 - low off / high off */
	/*    2 - low off / high on */
	/*    3 - low on  / high on */
	/*    There is no low on  / high off */

	uchar termination_lvd;	/* 11 0 - automatic */
	/*    1 - low off / high off */
	/*    2 - low off / high on */
	/*    3 - low on  / high on */
	/*    There is no low on  / high off */

	ushort bios_ctrl;	/* 12 BIOS control bits */
	/*  bit 0  BIOS don't act as initiator. */
	/*  bit 1  BIOS > 1 GB support */
	/*  bit 2  BIOS > 2 Disk Support */
	/*  bit 3  BIOS don't support removables */
	/*  bit 4  BIOS support bootable CD */
	/*  bit 5  BIOS scan enabled */
	/*  bit 6  BIOS support multiple LUNs */
	/*  bit 7  BIOS display of message */
	/*  bit 8  SCAM disabled */
	/*  bit 9  Reset SCSI bus during init. */
	/*  bit 10 */
	/*  bit 11 No verbose initialization. */
	/*  bit 12 SCSI parity enabled */
	/*  bit 13 */
	/*  bit 14 */
	/*  bit 15 */
	ushort sdtr_speed2;	/* 13 SDTR speed TID 4-7 */
	ushort sdtr_speed3;	/* 14 SDTR speed TID 8-11 */
	uchar max_host_qng;	/* 15 maximum host queueing */
	uchar max_dvc_qng;	/*    maximum per device queuing */
	ushort dvc_cntl;	/* 16 control bit for driver */
	ushort sdtr_speed4;	/* 17 SDTR speed 4 TID 12-15 */
	ushort serial_number_word1;	/* 18 Board serial number word 1 */
	ushort serial_number_word2;	/* 19 Board serial number word 2 */
	ushort serial_number_word3;	/* 20 Board serial number word 3 */
	ushort check_sum;	/* 21 EEP check sum */
	uchar oem_name[16];	/* 22 OEM name */
	ushort dvc_err_code;	/* 30 last device driver error code */
	ushort adv_err_code;	/* 31 last uc and Adv Lib error code */
	ushort adv_err_addr;	/* 32 last uc error address */
	ushort saved_dvc_err_code;	/* 33 saved last dev. driver error code   */
	ushort saved_adv_err_code;	/* 34 saved last uc and Adv Lib error code */
	ushort saved_adv_err_addr;	/* 35 saved last uc error address         */
	ushort reserved36;	/* 36 reserved */
	ushort reserved37;	/* 37 reserved */
	ushort reserved38;	/* 38 reserved */
	ushort reserved39;	/* 39 reserved */
	ushort reserved40;	/* 40 reserved */
	ushort reserved41;	/* 41 reserved */
	ushort reserved42;	/* 42 reserved */
	ushort reserved43;	/* 43 reserved */
	ushort reserved44;	/* 44 reserved */
	ushort reserved45;	/* 45 reserved */
	ushort reserved46;	/* 46 reserved */
	ushort reserved47;	/* 47 reserved */
	ushort reserved48;	/* 48 reserved */
	ushort reserved49;	/* 49 reserved */
	ushort reserved50;	/* 50 reserved */
	ushort reserved51;	/* 51 reserved */
	ushort reserved52;	/* 52 reserved */
	ushort reserved53;	/* 53 reserved */
	ushort reserved54;	/* 54 reserved */
	ushort reserved55;	/* 55 reserved */
	ushort cisptr_lsw;	/* 56 CIS PTR LSW */
	ushort cisprt_msw;	/* 57 CIS PTR MSW */
	ushort subsysvid;	/* 58 SubSystem Vendor ID */
	ushort subsysid;	/* 59 SubSystem ID */
	ushort reserved60;	/* 60 reserved */
	ushort reserved61;	/* 61 reserved */
	ushort reserved62;	/* 62 reserved */
	ushort reserved63;	/* 63 reserved */
} ADVEEP_38C0800_CONFIG;

typedef struct adveep_38C1600_config {
	/* Word Offset, Description */

	ushort cfg_lsw;		/* 00 power up initialization */
	/*  bit 11 set - Func. 0 INTB, Func. 1 INTA */
	/*       clear - Func. 0 INTA, Func. 1 INTB */
	/*  bit 13 set - Load CIS */
	/*  bit 14 set - BIOS Enable */
	/*  bit 15 set - Big Endian Mode */
	ushort cfg_msw;		/* 01 unused */
	ushort disc_enable;	/* 02 disconnect enable */
	ushort wdtr_able;	/* 03 Wide DTR able */
	ushort sdtr_speed1;	/* 04 SDTR Speed TID 0-3 */
	ushort start_motor;	/* 05 send start up motor */
	ushort tagqng_able;	/* 06 tag queuing able */
	ushort bios_scan;	/* 07 BIOS device control */
	ushort scam_tolerant;	/* 08 no scam */

	uchar adapter_scsi_id;	/* 09 Host Adapter ID */
	uchar bios_boot_delay;	/*    power up wait */

	uchar scsi_reset_delay;	/* 10 reset delay */
	uchar bios_id_lun;	/*    first boot device scsi id & lun */
	/*    high nibble is lun */
	/*    low nibble is scsi id */

	uchar termination_se;	/* 11 0 - automatic */
	/*    1 - low off / high off */
	/*    2 - low off / high on */
	/*    3 - low on  / high on */
	/*    There is no low on  / high off */

	uchar termination_lvd;	/* 11 0 - automatic */
	/*    1 - low off / high off */
	/*    2 - low off / high on */
	/*    3 - low on  / high on */
	/*    There is no low on  / high off */

	ushort bios_ctrl;	/* 12 BIOS control bits */
	/*  bit 0  BIOS don't act as initiator. */
	/*  bit 1  BIOS > 1 GB support */
	/*  bit 2  BIOS > 2 Disk Support */
	/*  bit 3  BIOS don't support removables */
	/*  bit 4  BIOS support bootable CD */
	/*  bit 5  BIOS scan enabled */
	/*  bit 6  BIOS support multiple LUNs */
	/*  bit 7  BIOS display of message */
	/*  bit 8  SCAM disabled */
	/*  bit 9  Reset SCSI bus during init. */
	/*  bit 10 Basic Integrity Checking disabled */
	/*  bit 11 No verbose initialization. */
	/*  bit 12 SCSI parity enabled */
	/*  bit 13 AIPP (Asyn. Info. Ph. Prot.) dis. */
	/*  bit 14 */
	/*  bit 15 */
	ushort sdtr_speed2;	/* 13 SDTR speed TID 4-7 */
	ushort sdtr_speed3;	/* 14 SDTR speed TID 8-11 */
	uchar max_host_qng;	/* 15 maximum host queueing */
	uchar max_dvc_qng;	/*    maximum per device queuing */
	ushort dvc_cntl;	/* 16 control bit for driver */
	ushort sdtr_speed4;	/* 17 SDTR speed 4 TID 12-15 */
	ushort serial_number_word1;	/* 18 Board serial number word 1 */
	ushort serial_number_word2;	/* 19 Board serial number word 2 */
	ushort serial_number_word3;	/* 20 Board serial number word 3 */
	ushort check_sum;	/* 21 EEP check sum */
	uchar oem_name[16];	/* 22 OEM name */
	ushort dvc_err_code;	/* 30 last device driver error code */
	ushort adv_err_code;	/* 31 last uc and Adv Lib error code */
	ushort adv_err_addr;	/* 32 last uc error address */
	ushort saved_dvc_err_code;	/* 33 saved last dev. driver error code   */
	ushort saved_adv_err_code;	/* 34 saved last uc and Adv Lib error code */
	ushort saved_adv_err_addr;	/* 35 saved last uc error address         */
	ushort reserved36;	/* 36 reserved */
	ushort reserved37;	/* 37 reserved */
	ushort reserved38;	/* 38 reserved */
	ushort reserved39;	/* 39 reserved */
	ushort reserved40;	/* 40 reserved */
	ushort reserved41;	/* 41 reserved */
	ushort reserved42;	/* 42 reserved */
	ushort reserved43;	/* 43 reserved */
	ushort reserved44;	/* 44 reserved */
	ushort reserved45;	/* 45 reserved */
	ushort reserved46;	/* 46 reserved */
	ushort reserved47;	/* 47 reserved */
	ushort reserved48;	/* 48 reserved */
	ushort reserved49;	/* 49 reserved */
	ushort reserved50;	/* 50 reserved */
	ushort reserved51;	/* 51 reserved */
	ushort reserved52;	/* 52 reserved */
	ushort reserved53;	/* 53 reserved */
	ushort reserved54;	/* 54 reserved */
	ushort reserved55;	/* 55 reserved */
	ushort cisptr_lsw;	/* 56 CIS PTR LSW */
	ushort cisprt_msw;	/* 57 CIS PTR MSW */
	ushort subsysvid;	/* 58 SubSystem Vendor ID */
	ushort subsysid;	/* 59 SubSystem ID */
	ushort reserved60;	/* 60 reserved */
	ushort reserved61;	/* 61 reserved */
	ushort reserved62;	/* 62 reserved */
	ushort reserved63;	/* 63 reserved */
} ADVEEP_38C1600_CONFIG;

/*
 * EEPROM Commands
 */
#define ASC_EEP_CMD_DONE             0x0200

/* bios_ctrl */
#define BIOS_CTRL_BIOS               0x0001
#define BIOS_CTRL_EXTENDED_XLAT      0x0002
#define BIOS_CTRL_GT_2_DISK          0x0004
#define BIOS_CTRL_BIOS_REMOVABLE     0x0008
#define BIOS_CTRL_BOOTABLE_CD        0x0010
#define BIOS_CTRL_MULTIPLE_LUN       0x0040
#define BIOS_CTRL_DISPLAY_MSG        0x0080
#define BIOS_CTRL_NO_SCAM            0x0100
#define BIOS_CTRL_RESET_SCSI_BUS     0x0200
#define BIOS_CTRL_INIT_VERBOSE       0x0800
#define BIOS_CTRL_SCSI_PARITY        0x1000
#define BIOS_CTRL_AIPP_DIS           0x2000

#define ADV_3550_MEMSIZE   0x2000	/* 8 KB Internal Memory */

#define ADV_38C0800_MEMSIZE  0x4000	/* 16 KB Internal Memory */

/*
 * XXX - Since ASC38C1600 Rev.3 has a local RAM failure issue, there is
 * a special 16K Adv Library and Microcode version. After the issue is
 * resolved, should restore 32K support.
 *
 * #define ADV_38C1600_MEMSIZE  0x8000L   * 32 KB Internal Memory *
 */
#define ADV_38C1600_MEMSIZE  0x4000	/* 16 KB Internal Memory */

/*
 * Byte I/O register address from base of 'iop_base'.
 */
#define IOPB_INTR_STATUS_REG    0x00
#define IOPB_CHIP_ID_1          0x01
#define IOPB_INTR_ENABLES       0x02
#define IOPB_CHIP_TYPE_REV      0x03
#define IOPB_RES_ADDR_4         0x04
#define IOPB_RES_ADDR_5         0x05
#define IOPB_RAM_DATA           0x06
#define IOPB_RES_ADDR_7         0x07
#define IOPB_FLAG_REG           0x08
#define IOPB_RES_ADDR_9         0x09
#define IOPB_RISC_CSR           0x0A
#define IOPB_RES_ADDR_B         0x0B
#define IOPB_RES_ADDR_C         0x0C
#define IOPB_RES_ADDR_D         0x0D
#define IOPB_SOFT_OVER_WR       0x0E
#define IOPB_RES_ADDR_F         0x0F
#define IOPB_MEM_CFG            0x10
#define IOPB_RES_ADDR_11        0x11
#define IOPB_GPIO_DATA          0x12
#define IOPB_RES_ADDR_13        0x13
#define IOPB_FLASH_PAGE         0x14
#define IOPB_RES_ADDR_15        0x15
#define IOPB_GPIO_CNTL          0x16
#define IOPB_RES_ADDR_17        0x17
#define IOPB_FLASH_DATA         0x18
#define IOPB_RES_ADDR_19        0x19
#define IOPB_RES_ADDR_1A        0x1A
#define IOPB_RES_ADDR_1B        0x1B
#define IOPB_RES_ADDR_1C        0x1C
#define IOPB_RES_ADDR_1D        0x1D
#define IOPB_RES_ADDR_1E        0x1E
#define IOPB_RES_ADDR_1F        0x1F
#define IOPB_DMA_CFG0           0x20
#define IOPB_DMA_CFG1           0x21
#define IOPB_TICKLE             0x22
#define IOPB_DMA_REG_WR         0x23
#define IOPB_SDMA_STATUS        0x24
#define IOPB_SCSI_BYTE_CNT      0x25
#define IOPB_HOST_BYTE_CNT      0x26
#define IOPB_BYTE_LEFT_TO_XFER  0x27
#define IOPB_BYTE_TO_XFER_0     0x28
#define IOPB_BYTE_TO_XFER_1     0x29
#define IOPB_BYTE_TO_XFER_2     0x2A
#define IOPB_BYTE_TO_XFER_3     0x2B
#define IOPB_ACC_GRP            0x2C
#define IOPB_RES_ADDR_2D        0x2D
#define IOPB_DEV_ID             0x2E
#define IOPB_RES_ADDR_2F        0x2F
#define IOPB_SCSI_DATA          0x30
#define IOPB_RES_ADDR_31        0x31
#define IOPB_RES_ADDR_32        0x32
#define IOPB_SCSI_DATA_HSHK     0x33
#define IOPB_SCSI_CTRL          0x34
#define IOPB_RES_ADDR_35        0x35
#define IOPB_RES_ADDR_36        0x36
#define IOPB_RES_ADDR_37        0x37
#define IOPB_RAM_BIST           0x38
#define IOPB_PLL_TEST           0x39
#define IOPB_PCI_INT_CFG        0x3A
#define IOPB_RES_ADDR_3B        0x3B
#define IOPB_RFIFO_CNT          0x3C
#define IOPB_RES_ADDR_3D        0x3D
#define IOPB_RES_ADDR_3E        0x3E
#define IOPB_RES_ADDR_3F        0x3F

/*
 * Word I/O register address from base of 'iop_base'.
 */
#define IOPW_CHIP_ID_0          0x00	/* CID0  */
#define IOPW_CTRL_REG           0x02	/* CC    */
#define IOPW_RAM_ADDR           0x04	/* LA    */
#define IOPW_RAM_DATA           0x06	/* LD    */
#define IOPW_RES_ADDR_08        0x08
#define IOPW_RISC_CSR           0x0A	/* CSR   */
#define IOPW_SCSI_CFG0          0x0C	/* CFG0  */
#define IOPW_SCSI_CFG1          0x0E	/* CFG1  */
#define IOPW_RES_ADDR_10        0x10
#define IOPW_SEL_MASK           0x12	/* SM    */
#define IOPW_RES_ADDR_14        0x14
#define IOPW_FLASH_ADDR         0x16	/* FA    */
#define IOPW_RES_ADDR_18        0x18
#define IOPW_EE_CMD             0x1A	/* EC    */
#define IOPW_EE_DATA            0x1C	/* ED    */
#define IOPW_SFIFO_CNT          0x1E	/* SFC   */
#define IOPW_RES_ADDR_20        0x20
#define IOPW_Q_BASE             0x22	/* QB    */
#define IOPW_QP                 0x24	/* QP    */
#define IOPW_IX                 0x26	/* IX    */
#define IOPW_SP                 0x28	/* SP    */
#define IOPW_PC                 0x2A	/* PC    */
#define IOPW_RES_ADDR_2C        0x2C
#define IOPW_RES_ADDR_2E        0x2E
#define IOPW_SCSI_DATA          0x30	/* SD    */
#define IOPW_SCSI_DATA_HSHK     0x32	/* SDH   */
#define IOPW_SCSI_CTRL          0x34	/* SC    */
#define IOPW_HSHK_CFG           0x36	/* HCFG  */
#define IOPW_SXFR_STATUS        0x36	/* SXS   */
#define IOPW_SXFR_CNTL          0x38	/* SXL   */
#define IOPW_SXFR_CNTH          0x3A	/* SXH   */
#define IOPW_RES_ADDR_3C        0x3C
#define IOPW_RFIFO_DATA         0x3E	/* RFD   */

/*
 * Doubleword I/O register address from base of 'iop_base'.
 */
#define IOPDW_RES_ADDR_0         0x00
#define IOPDW_RAM_DATA           0x04
#define IOPDW_RES_ADDR_8         0x08
#define IOPDW_RES_ADDR_C         0x0C
#define IOPDW_RES_ADDR_10        0x10
#define IOPDW_COMMA              0x14
#define IOPDW_COMMB              0x18
#define IOPDW_RES_ADDR_1C        0x1C
#define IOPDW_SDMA_ADDR0         0x20
#define IOPDW_SDMA_ADDR1         0x24
#define IOPDW_SDMA_COUNT         0x28
#define IOPDW_SDMA_ERROR         0x2C
#define IOPDW_RDMA_ADDR0         0x30
#define IOPDW_RDMA_ADDR1         0x34
#define IOPDW_RDMA_COUNT         0x38
#define IOPDW_RDMA_ERROR         0x3C

#define ADV_CHIP_ID_BYTE         0x25
#define ADV_CHIP_ID_WORD         0x04C1

#define ADV_INTR_ENABLE_HOST_INTR                   0x01
#define ADV_INTR_ENABLE_SEL_INTR                    0x02
#define ADV_INTR_ENABLE_DPR_INTR                    0x04
#define ADV_INTR_ENABLE_RTA_INTR                    0x08
#define ADV_INTR_ENABLE_RMA_INTR                    0x10
#define ADV_INTR_ENABLE_RST_INTR                    0x20
#define ADV_INTR_ENABLE_DPE_INTR                    0x40
#define ADV_INTR_ENABLE_GLOBAL_INTR                 0x80

#define ADV_INTR_STATUS_INTRA            0x01
#define ADV_INTR_STATUS_INTRB            0x02
#define ADV_INTR_STATUS_INTRC            0x04

#define ADV_RISC_CSR_STOP           (0x0000)
#define ADV_RISC_TEST_COND          (0x2000)
#define ADV_RISC_CSR_RUN            (0x4000)
#define ADV_RISC_CSR_SINGLE_STEP    (0x8000)

#define ADV_CTRL_REG_HOST_INTR      0x0100
#define ADV_CTRL_REG_SEL_INTR       0x0200
#define ADV_CTRL_REG_DPR_INTR       0x0400
#define ADV_CTRL_REG_RTA_INTR       0x0800
#define ADV_CTRL_REG_RMA_INTR       0x1000
#define ADV_CTRL_REG_RES_BIT14      0x2000
#define ADV_CTRL_REG_DPE_INTR       0x4000
#define ADV_CTRL_REG_POWER_DONE     0x8000
#define ADV_CTRL_REG_ANY_INTR       0xFF00

#define ADV_CTRL_REG_CMD_RESET             0x00C6
#define ADV_CTRL_REG_CMD_WR_IO_REG         0x00C5
#define ADV_CTRL_REG_CMD_RD_IO_REG         0x00C4
#define ADV_CTRL_REG_CMD_WR_PCI_CFG_SPACE  0x00C3
#define ADV_CTRL_REG_CMD_RD_PCI_CFG_SPACE  0x00C2

#define ADV_TICKLE_NOP                      0x00
#define ADV_TICKLE_A                        0x01
#define ADV_TICKLE_B                        0x02
#define ADV_TICKLE_C                        0x03

#define AdvIsIntPending(port) \
    (AdvReadWordRegister(port, IOPW_CTRL_REG) & ADV_CTRL_REG_HOST_INTR)

/*
 * SCSI_CFG0 Register bit definitions
 */
#define TIMER_MODEAB    0xC000	/* Watchdog, Second, and Select. Timer Ctrl. */
#define PARITY_EN       0x2000	/* Enable SCSI Parity Error detection */
#define EVEN_PARITY     0x1000	/* Select Even Parity */
#define WD_LONG         0x0800	/* Watchdog Interval, 1: 57 min, 0: 13 sec */
#define QUEUE_128       0x0400	/* Queue Size, 1: 128 byte, 0: 64 byte */
#define PRIM_MODE       0x0100	/* Primitive SCSI mode */
#define SCAM_EN         0x0080	/* Enable SCAM selection */
#define SEL_TMO_LONG    0x0040	/* Sel/Resel Timeout, 1: 400 ms, 0: 1.6 ms */
#define CFRM_ID         0x0020	/* SCAM id sel. confirm., 1: fast, 0: 6.4 ms */
#define OUR_ID_EN       0x0010	/* Enable OUR_ID bits */
#define OUR_ID          0x000F	/* SCSI ID */

/*
 * SCSI_CFG1 Register bit definitions
 */
#define BIG_ENDIAN      0x8000	/* Enable Big Endian Mode MIO:15, EEP:15 */
#define TERM_POL        0x2000	/* Terminator Polarity Ctrl. MIO:13, EEP:13 */
#define SLEW_RATE       0x1000	/* SCSI output buffer slew rate */
#define FILTER_SEL      0x0C00	/* Filter Period Selection */
#define  FLTR_DISABLE    0x0000	/* Input Filtering Disabled */
#define  FLTR_11_TO_20NS 0x0800	/* Input Filtering 11ns to 20ns */
#define  FLTR_21_TO_39NS 0x0C00	/* Input Filtering 21ns to 39ns */
#define ACTIVE_DBL      0x0200	/* Disable Active Negation */
#define DIFF_MODE       0x0100	/* SCSI differential Mode (Read-Only) */
#define DIFF_SENSE      0x0080	/* 1: No SE cables, 0: SE cable (Read-Only) */
#define TERM_CTL_SEL    0x0040	/* Enable TERM_CTL_H and TERM_CTL_L */
#define TERM_CTL        0x0030	/* External SCSI Termination Bits */
#define  TERM_CTL_H      0x0020	/* Enable External SCSI Upper Termination */
#define  TERM_CTL_L      0x0010	/* Enable External SCSI Lower Termination */
#define CABLE_DETECT    0x000F	/* External SCSI Cable Connection Status */

/*
 * Addendum for ASC-38C0800 Chip
 *
 * The ASC-38C1600 Chip uses the same definitions except that the
 * bus mode override bits [12:10] have been moved to byte register
 * offset 0xE (IOPB_SOFT_OVER_WR) bits [12:10]. The [12:10] bits in
 * SCSI_CFG1 are read-only and always available. Bit 14 (DIS_TERM_DRV)
 * is not needed. The [12:10] bits in IOPB_SOFT_OVER_WR are write-only.
 * Also each ASC-38C1600 function or channel uses only cable bits [5:4]
 * and [1:0]. Bits [14], [7:6], [3:2] are unused.
 */
#define DIS_TERM_DRV    0x4000	/* 1: Read c_det[3:0], 0: cannot read */
#define HVD_LVD_SE      0x1C00	/* Device Detect Bits */
#define  HVD             0x1000	/* HVD Device Detect */
#define  LVD             0x0800	/* LVD Device Detect */
#define  SE              0x0400	/* SE Device Detect */
#define TERM_LVD        0x00C0	/* LVD Termination Bits */
#define  TERM_LVD_HI     0x0080	/* Enable LVD Upper Termination */
#define  TERM_LVD_LO     0x0040	/* Enable LVD Lower Termination */
#define TERM_SE         0x0030	/* SE Termination Bits */
#define  TERM_SE_HI      0x0020	/* Enable SE Upper Termination */
#define  TERM_SE_LO      0x0010	/* Enable SE Lower Termination */
#define C_DET_LVD       0x000C	/* LVD Cable Detect Bits */
#define  C_DET3          0x0008	/* Cable Detect for LVD External Wide */
#define  C_DET2          0x0004	/* Cable Detect for LVD Internal Wide */
#define C_DET_SE        0x0003	/* SE Cable Detect Bits */
#define  C_DET1          0x0002	/* Cable Detect for SE Internal Wide */
#define  C_DET0          0x0001	/* Cable Detect for SE Internal Narrow */

#define CABLE_ILLEGAL_A 0x7
    /* x 0 0 0  | on  on | Illegal (all 3 connectors are used) */

#define CABLE_ILLEGAL_B 0xB
    /* 0 x 0 0  | on  on | Illegal (all 3 connectors are used) */

/*
 * MEM_CFG Register bit definitions
 */
#define BIOS_EN         0x40	/* BIOS Enable MIO:14,EEP:14 */
#define FAST_EE_CLK     0x20	/* Diagnostic Bit */
#define RAM_SZ          0x1C	/* Specify size of RAM to RISC */
#define  RAM_SZ_2KB      0x00	/* 2 KB */
#define  RAM_SZ_4KB      0x04	/* 4 KB */
#define  RAM_SZ_8KB      0x08	/* 8 KB */
#define  RAM_SZ_16KB     0x0C	/* 16 KB */
#define  RAM_SZ_32KB     0x10	/* 32 KB */
#define  RAM_SZ_64KB     0x14	/* 64 KB */

/*
 * DMA_CFG0 Register bit definitions
 *
 * This register is only accessible to the host.
 */
#define BC_THRESH_ENB   0x80	/* PCI DMA Start Conditions */
#define FIFO_THRESH     0x70	/* PCI DMA FIFO Threshold */
#define  FIFO_THRESH_16B  0x00	/* 16 bytes */
#define  FIFO_THRESH_32B  0x20	/* 32 bytes */
#define  FIFO_THRESH_48B  0x30	/* 48 bytes */
#define  FIFO_THRESH_64B  0x40	/* 64 bytes */
#define  FIFO_THRESH_80B  0x50	/* 80 bytes (default) */
#define  FIFO_THRESH_96B  0x60	/* 96 bytes */
#define  FIFO_THRESH_112B 0x70	/* 112 bytes */
#define START_CTL       0x0C	/* DMA start conditions */
#define  START_CTL_TH    0x00	/* Wait threshold level (default) */
#define  START_CTL_ID    0x04	/* Wait SDMA/SBUS idle */
#define  START_CTL_THID  0x08	/* Wait threshold and SDMA/SBUS idle */
#define  START_CTL_EMFU  0x0C	/* Wait SDMA FIFO empty/full */
#define READ_CMD        0x03	/* Memory Read Method */
#define  READ_CMD_MR     0x00	/* Memory Read */
#define  READ_CMD_MRL    0x02	/* Memory Read Long */
#define  READ_CMD_MRM    0x03	/* Memory Read Multiple (default) */

/*
 * ASC-38C0800 RAM BIST Register bit definitions
 */
#define RAM_TEST_MODE         0x80
#define PRE_TEST_MODE         0x40
#define NORMAL_MODE           0x00
#define RAM_TEST_DONE         0x10
#define RAM_TEST_STATUS       0x0F
#define  RAM_TEST_HOST_ERROR   0x08
#define  RAM_TEST_INTRAM_ERROR 0x04
#define  RAM_TEST_RISC_ERROR   0x02
#define  RAM_TEST_SCSI_ERROR   0x01
#define  RAM_TEST_SUCCESS      0x00
#define PRE_TEST_VALUE        0x05
#define NORMAL_VALUE          0x00

/*
 * ASC38C1600 Definitions
 *
 * IOPB_PCI_INT_CFG Bit Field Definitions
 */

#define INTAB_LD        0x80	/* Value loaded from EEPROM Bit 11. */

/*
 * Bit 1 can be set to change the interrupt for the Function to operate in
 * Totem Pole mode. By default Bit 1 is 0 and the interrupt operates in
 * Open Drain mode. Both functions of the ASC38C1600 must be set to the same
 * mode, otherwise the operating mode is undefined.
 */
#define TOTEMPOLE       0x02

/*
 * Bit 0 can be used to change the Int Pin for the Function. The value is
 * 0 by default for both Functions with Function 0 using INT A and Function
 * B using INT B. For Function 0 if set, INT B is used. For Function 1 if set,
 * INT A is used.
 *
 * EEPROM Word 0 Bit 11 for each Function may change the initial Int Pin
 * value specified in the PCI Configuration Space.
 */
#define INTAB           0x01

/*
 * Adv Library Status Definitions
 */
#define ADV_TRUE        1
#define ADV_FALSE       0
#define ADV_SUCCESS     1
#define ADV_BUSY        0
#define ADV_ERROR       (-1)

/*
 * ADV_DVC_VAR 'warn_code' values
 */
#define ASC_WARN_BUSRESET_ERROR         0x0001	/* SCSI Bus Reset error */
#define ASC_WARN_EEPROM_CHKSUM          0x0002	/* EEP check sum error */
#define ASC_WARN_EEPROM_TERMINATION     0x0004	/* EEP termination bad field */
#define ASC_WARN_ERROR                  0xFFFF	/* ADV_ERROR return */

#define ADV_MAX_TID                     15	/* max. target identifier */
#define ADV_MAX_LUN                     7	/* max. logical unit number */

/*
 * Fixed locations of microcode operating variables.
 */
#define ASC_MC_CODE_BEGIN_ADDR          0x0028	/* microcode start address */
#define ASC_MC_CODE_END_ADDR            0x002A	/* microcode end address */
#define ASC_MC_CODE_CHK_SUM             0x002C	/* microcode code checksum */
#define ASC_MC_VERSION_DATE             0x0038	/* microcode version */
#define ASC_MC_VERSION_NUM              0x003A	/* microcode number */
#define ASC_MC_BIOSMEM                  0x0040	/* BIOS RISC Memory Start */
#define ASC_MC_BIOSLEN                  0x0050	/* BIOS RISC Memory Length */
#define ASC_MC_BIOS_SIGNATURE           0x0058	/* BIOS Signature 0x55AA */
#define ASC_MC_BIOS_VERSION             0x005A	/* BIOS Version (2 bytes) */
#define ASC_MC_SDTR_SPEED1              0x0090	/* SDTR Speed for TID 0-3 */
#define ASC_MC_SDTR_SPEED2              0x0092	/* SDTR Speed for TID 4-7 */
#define ASC_MC_SDTR_SPEED3              0x0094	/* SDTR Speed for TID 8-11 */
#define ASC_MC_SDTR_SPEED4              0x0096	/* SDTR Speed for TID 12-15 */
#define ASC_MC_CHIP_TYPE                0x009A
#define ASC_MC_INTRB_CODE               0x009B
#define ASC_MC_WDTR_ABLE                0x009C
#define ASC_MC_SDTR_ABLE                0x009E
#define ASC_MC_TAGQNG_ABLE              0x00A0
#define ASC_MC_DISC_ENABLE              0x00A2
#define ASC_MC_IDLE_CMD_STATUS          0x00A4
#define ASC_MC_IDLE_CMD                 0x00A6
#define ASC_MC_IDLE_CMD_PARAMETER       0x00A8
#define ASC_MC_DEFAULT_SCSI_CFG0        0x00AC
#define ASC_MC_DEFAULT_SCSI_CFG1        0x00AE
#define ASC_MC_DEFAULT_MEM_CFG          0x00B0
#define ASC_MC_DEFAULT_SEL_MASK         0x00B2
#define ASC_MC_SDTR_DONE                0x00B6
#define ASC_MC_NUMBER_OF_QUEUED_CMD     0x00C0
#define ASC_MC_NUMBER_OF_MAX_CMD        0x00D0
#define ASC_MC_DEVICE_HSHK_CFG_TABLE    0x0100
#define ASC_MC_CONTROL_FLAG             0x0122	/* Microcode control flag. */
#define ASC_MC_WDTR_DONE                0x0124
#define ASC_MC_CAM_MODE_MASK            0x015E	/* CAM mode TID bitmask. */
#define ASC_MC_ICQ                      0x0160
#define ASC_MC_IRQ                      0x0164
#define ASC_MC_PPR_ABLE                 0x017A

/*
 * BIOS LRAM variable absolute offsets.
 */
#define BIOS_CODESEG    0x54
#define BIOS_CODELEN    0x56
#define BIOS_SIGNATURE  0x58
#define BIOS_VERSION    0x5A

/*
 * Microcode Control Flags
 *
 * Flags set by the Adv Library in RISC variable 'control_flag' (0x122)
 * and handled by the microcode.
 */
#define CONTROL_FLAG_IGNORE_PERR        0x0001	/* Ignore DMA Parity Errors */
#define CONTROL_FLAG_ENABLE_AIPP        0x0002	/* Enabled AIPP checking. */

/*
 * ASC_MC_DEVICE_HSHK_CFG_TABLE microcode table or HSHK_CFG register format
 */
#define HSHK_CFG_WIDE_XFR       0x8000
#define HSHK_CFG_RATE           0x0F00
#define HSHK_CFG_OFFSET         0x001F

#define ASC_DEF_MAX_HOST_QNG    0xFD	/* Max. number of host commands (253) */
#define ASC_DEF_MIN_HOST_QNG    0x10	/* Min. number of host commands (16) */
#define ASC_DEF_MAX_DVC_QNG     0x3F	/* Max. number commands per device (63) */
#define ASC_DEF_MIN_DVC_QNG     0x04	/* Min. number commands per device (4) */

#define ASC_QC_DATA_CHECK  0x01	/* Require ASC_QC_DATA_OUT set or clear. */
#define ASC_QC_DATA_OUT    0x02	/* Data out DMA transfer. */
#define ASC_QC_START_MOTOR 0x04	/* Send auto-start motor before request. */
#define ASC_QC_NO_OVERRUN  0x08	/* Don't report overrun. */
#define ASC_QC_FREEZE_TIDQ 0x10	/* Freeze TID queue after request. XXX TBD */

#define ASC_QSC_NO_DISC     0x01	/* Don't allow disconnect for request. */
#define ASC_QSC_NO_TAGMSG   0x02	/* Don't allow tag queuing for request. */
#define ASC_QSC_NO_SYNC     0x04	/* Don't use Synch. transfer on request. */
#define ASC_QSC_NO_WIDE     0x08	/* Don't use Wide transfer on request. */
#define ASC_QSC_REDO_DTR    0x10	/* Renegotiate WDTR/SDTR before request. */
/*
 * Note: If a Tag Message is to be sent and neither ASC_QSC_HEAD_TAG or
 * ASC_QSC_ORDERED_TAG is set, then a Simple Tag Message (0x20) is used.
 */
#define ASC_QSC_HEAD_TAG    0x40	/* Use Head Tag Message (0x21). */
#define ASC_QSC_ORDERED_TAG 0x80	/* Use Ordered Tag Message (0x22). */

/*
 * All fields here are accessed by the board microcode and need to be
 * little-endian.
 */
typedef struct adv_carr_t {
	ADV_VADDR carr_va;	/* Carrier Virtual Address */
	ADV_PADDR carr_pa;	/* Carrier Physical Address */
	ADV_VADDR areq_vpa;	/* ASC_SCSI_REQ_Q Virtual or Physical Address */
	/*
	 * next_vpa [31:4]            Carrier Virtual or Physical Next Pointer
	 *
	 * next_vpa [3:1]             Reserved Bits
	 * next_vpa [0]               Done Flag set in Response Queue.
	 */
	ADV_VADDR next_vpa;
} ADV_CARR_T;

/*
 * Mask used to eliminate low 4 bits of carrier 'next_vpa' field.
 */
#define ASC_NEXT_VPA_MASK       0xFFFFFFF0

#define ASC_RQ_DONE             0x00000001
#define ASC_RQ_GOOD             0x00000002
#define ASC_CQ_STOPPER          0x00000000

#define ASC_GET_CARRP(carrp) ((carrp) & ASC_NEXT_VPA_MASK)

#define ADV_CARRIER_NUM_PAGE_CROSSING \
    (((ADV_CARRIER_COUNT * sizeof(ADV_CARR_T)) + (PAGE_SIZE - 1))/PAGE_SIZE)

#define ADV_CARRIER_BUFSIZE \
    ((ADV_CARRIER_COUNT + ADV_CARRIER_NUM_PAGE_CROSSING) * sizeof(ADV_CARR_T))

/*
 * ASC_SCSI_REQ_Q 'a_flag' definitions
 *
 * The Adv Library should limit use to the lower nibble (4 bits) of
 * a_flag. Drivers are free to use the upper nibble (4 bits) of a_flag.
 */
#define ADV_POLL_REQUEST                0x01	/* poll for request completion */
#define ADV_SCSIQ_DONE                  0x02	/* request done */
#define ADV_DONT_RETRY                  0x08	/* don't do retry */

#define ADV_CHIP_ASC3550          0x01	/* Ultra-Wide IC */
#define ADV_CHIP_ASC38C0800       0x02	/* Ultra2-Wide/LVD IC */
#define ADV_CHIP_ASC38C1600       0x03	/* Ultra3-Wide/LVD2 IC */

/*
 * Adapter temporary configuration structure
 *
 * This structure can be discarded after initialization. Don't add
 * fields here needed after initialization.
 *
 * Field naming convention:
 *
 *  *_enable indicates the field enables or disables a feature. The
 *  value of the field is never reset.
 */
typedef struct adv_dvc_cfg {
	ushort disc_enable;	/* enable disconnection */
	uchar chip_version;	/* chip version */
	uchar termination;	/* Term. Ctrl. bits 6-5 of SCSI_CFG1 register */
	ushort control_flag;	/* Microcode Control Flag */
	ushort mcode_date;	/* Microcode date */
	ushort mcode_version;	/* Microcode version */
	ushort serial1;		/* EEPROM serial number word 1 */
	ushort serial2;		/* EEPROM serial number word 2 */
	ushort serial3;		/* EEPROM serial number word 3 */
} ADV_DVC_CFG;

struct adv_dvc_var;
struct adv_scsi_req_q;

typedef struct asc_sg_block {
	uchar reserved1;
	uchar reserved2;
	uchar reserved3;
	uchar sg_cnt;		/* Valid entries in block. */
	ADV_PADDR sg_ptr;	/* Pointer to next sg block. */
	struct {
		ADV_PADDR sg_addr;	/* SG element address. */
		ADV_DCNT sg_count;	/* SG element count. */
	} sg_list[NO_OF_SG_PER_BLOCK];
} ADV_SG_BLOCK;

/*
 * ADV_SCSI_REQ_Q - microcode request structure
 *
 * All fields in this structure up to byte 60 are used by the microcode.
 * The microcode makes assumptions about the size and ordering of fields
 * in this structure. Do not change the structure definition here without
 * coordinating the change with the microcode.
 *
 * All fields accessed by microcode must be maintained in little_endian
 * order.
 */
typedef struct adv_scsi_req_q {
	uchar cntl;		/* Ucode flags and state (ASC_MC_QC_*). */
	uchar target_cmd;
	uchar target_id;	/* Device target identifier. */
	uchar target_lun;	/* Device target logical unit number. */
	ADV_PADDR data_addr;	/* Data buffer physical address. */
	ADV_DCNT data_cnt;	/* Data count. Ucode sets to residual. */
	ADV_PADDR sense_addr;
	ADV_PADDR carr_pa;
	uchar mflag;
	uchar sense_len;
	uchar cdb_len;		/* SCSI CDB length. Must <= 16 bytes. */
	uchar scsi_cntl;
	uchar done_status;	/* Completion status. */
	uchar scsi_status;	/* SCSI status byte. */
	uchar host_status;	/* Ucode host status. */
	uchar sg_working_ix;
	uchar cdb[12];		/* SCSI CDB bytes 0-11. */
	ADV_PADDR sg_real_addr;	/* SG list physical address. */
	ADV_PADDR scsiq_rptr;
	uchar cdb16[4];		/* SCSI CDB bytes 12-15. */
	ADV_VADDR scsiq_ptr;
	ADV_VADDR carr_va;
	/*
	 * End of microcode structure - 60 bytes. The rest of the structure
	 * is used by the Adv Library and ignored by the microcode.
	 */
	ADV_VADDR srb_ptr;
	ADV_SG_BLOCK *sg_list_ptr;	/* SG list virtual address. */
	char *vdata_addr;	/* Data buffer virtual address. */
	uchar a_flag;
	uchar pad[2];		/* Pad out to a word boundary. */
} ADV_SCSI_REQ_Q;

/*
 * The following two structures are used to process Wide Board requests.
 *
 * The ADV_SCSI_REQ_Q structure in adv_req_t is passed to the Adv Library
 * and microcode with the ADV_SCSI_REQ_Q field 'srb_ptr' pointing to the
 * adv_req_t. The adv_req_t structure 'cmndp' field in turn points to the
 * Mid-Level SCSI request structure.
 *
 * Zero or more ADV_SG_BLOCK are used with each ADV_SCSI_REQ_Q. Each
 * ADV_SG_BLOCK structure holds 15 scatter-gather elements. Under Linux
 * up to 255 scatter-gather elements may be used per request or
 * ADV_SCSI_REQ_Q.
 *
 * Both structures must be 32 byte aligned.
 */
typedef struct adv_sgblk {
	ADV_SG_BLOCK sg_block;	/* Sgblock structure. */
	uchar align[32];	/* Sgblock structure padding. */
	struct adv_sgblk *next_sgblkp;	/* Next scatter-gather structure. */
} adv_sgblk_t;

typedef struct adv_req {
	ADV_SCSI_REQ_Q scsi_req_q;	/* Adv Library request structure. */
	uchar align[32];	/* Request structure padding. */
	struct scsi_cmnd *cmndp;	/* Mid-Level SCSI command pointer. */
	adv_sgblk_t *sgblkp;	/* Adv Library scatter-gather pointer. */
	struct adv_req *next_reqp;	/* Next Request Structure. */
} adv_req_t;

/*
 * Adapter operation variable structure.
 *
 * One structure is required per host adapter.
 *
 * Field naming convention:
 *
 *  *_able indicates both whether a feature should be enabled or disabled
 *  and whether a device isi capable of the feature. At initialization
 *  this field may be set, but later if a device is found to be incapable
 *  of the feature, the field is cleared.
 */
typedef struct adv_dvc_var {
	AdvPortAddr iop_base;	/* I/O port address */
	ushort err_code;	/* fatal error code */
	ushort bios_ctrl;	/* BIOS control word, EEPROM word 12 */
	ushort wdtr_able;	/* try WDTR for a device */
	ushort sdtr_able;	/* try SDTR for a device */
	ushort ultra_able;	/* try SDTR Ultra speed for a device */
	ushort sdtr_speed1;	/* EEPROM SDTR Speed for TID 0-3   */
	ushort sdtr_speed2;	/* EEPROM SDTR Speed for TID 4-7   */
	ushort sdtr_speed3;	/* EEPROM SDTR Speed for TID 8-11  */
	ushort sdtr_speed4;	/* EEPROM SDTR Speed for TID 12-15 */
	ushort tagqng_able;	/* try tagged queuing with a device */
	ushort ppr_able;	/* PPR message capable per TID bitmask. */
	uchar max_dvc_qng;	/* maximum number of tagged commands per device */
	ushort start_motor;	/* start motor command allowed */
	uchar scsi_reset_wait;	/* delay in seconds after scsi bus reset */
	uchar chip_no;		/* should be assigned by caller */
	uchar max_host_qng;	/* maximum number of Q'ed command allowed */
	ushort no_scam;		/* scam_tolerant of EEPROM */
	struct asc_board *drv_ptr;	/* driver pointer to private structure */
	uchar chip_scsi_id;	/* chip SCSI target ID */
	uchar chip_type;
	uchar bist_err_code;
	ADV_CARR_T *carrier_buf;
	ADV_CARR_T *carr_freelist;	/* Carrier free list. */
	ADV_CARR_T *icq_sp;	/* Initiator command queue stopper pointer. */
	ADV_CARR_T *irq_sp;	/* Initiator response queue stopper pointer. */
	ushort carr_pending_cnt;	/* Count of pending carriers. */
	struct adv_req *orig_reqp;	/* adv_req_t memory block. */
	/*
	 * Note: The following fields will not be used after initialization. The
	 * driver may discard the buffer after initialization is done.
	 */
	ADV_DVC_CFG *cfg;	/* temporary configuration structure  */
} ADV_DVC_VAR;

/*
 * Microcode idle loop commands
 */
#define IDLE_CMD_COMPLETED           0
#define IDLE_CMD_STOP_CHIP           0x0001
#define IDLE_CMD_STOP_CHIP_SEND_INT  0x0002
#define IDLE_CMD_SEND_INT            0x0004
#define IDLE_CMD_ABORT               0x0008
#define IDLE_CMD_DEVICE_RESET        0x0010
#define IDLE_CMD_SCSI_RESET_START    0x0020	/* Assert SCSI Bus Reset */
#define IDLE_CMD_SCSI_RESET_END      0x0040	/* Deassert SCSI Bus Reset */
#define IDLE_CMD_SCSIREQ             0x0080

#define IDLE_CMD_STATUS_SUCCESS      0x0001
#define IDLE_CMD_STATUS_FAILURE      0x0002

/*
 * AdvSendIdleCmd() flag definitions.
 */
#define ADV_NOWAIT     0x01

/*
 * Wait loop time out values.
 */
#define SCSI_WAIT_100_MSEC           100UL	/* 100 milliseconds */
#define SCSI_US_PER_MSEC             1000	/* microseconds per millisecond */
#define SCSI_MAX_RETRY               10	/* retry count */

#define ADV_ASYNC_RDMA_FAILURE          0x01	/* Fatal RDMA failure. */
#define ADV_ASYNC_SCSI_BUS_RESET_DET    0x02	/* Detected SCSI Bus Reset. */
#define ADV_ASYNC_CARRIER_READY_FAILURE 0x03	/* Carrier Ready failure. */
#define ADV_RDMA_IN_CARR_AND_Q_INVALID  0x04	/* RDMAed-in data invalid. */

#define ADV_HOST_SCSI_BUS_RESET      0x80	/* Host Initiated SCSI Bus Reset. */

/* Read byte from a register. */
#define AdvReadByteRegister(iop_base, reg_off) \
     (ADV_MEM_READB((iop_base) + (reg_off)))

/* Write byte to a register. */
#define AdvWriteByteRegister(iop_base, reg_off, byte) \
     (ADV_MEM_WRITEB((iop_base) + (reg_off), (byte)))

/* Read word (2 bytes) from a register. */
#define AdvReadWordRegister(iop_base, reg_off) \
     (ADV_MEM_READW((iop_base) + (reg_off)))

/* Write word (2 bytes) to a register. */
#define AdvWriteWordRegister(iop_base, reg_off, word) \
     (ADV_MEM_WRITEW((iop_base) + (reg_off), (word)))

/* Write dword (4 bytes) to a register. */
#define AdvWriteDWordRegister(iop_base, reg_off, dword) \
     (ADV_MEM_WRITEDW((iop_base) + (reg_off), (dword)))

/* Read byte from LRAM. */
#define AdvReadByteLram(iop_base, addr, byte) \
do { \
    ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)); \
    (byte) = ADV_MEM_READB((iop_base) + IOPB_RAM_DATA); \
} while (0)

/* Write byte to LRAM. */
#define AdvWriteByteLram(iop_base, addr, byte) \
    (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
     ADV_MEM_WRITEB((iop_base) + IOPB_RAM_DATA, (byte)))

/* Read word (2 bytes) from LRAM. */
#define AdvReadWordLram(iop_base, addr, word) \
do { \
    ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)); \
    (word) = (ADV_MEM_READW((iop_base) + IOPW_RAM_DATA)); \
} while (0)

/* Write word (2 bytes) to LRAM. */
#define AdvWriteWordLram(iop_base, addr, word) \
    (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
     ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, (word)))

/* Write little-endian double word (4 bytes) to LRAM */
/* Because of unspecified C language ordering don't use auto-increment. */
#define AdvWriteDWordLramNoSwap(iop_base, addr, dword) \
    ((ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr)), \
      ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, \
                     cpu_to_le16((ushort) ((dword) & 0xFFFF)))), \
     (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_ADDR, (addr) + 2), \
      ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, \
                     cpu_to_le16((ushort) ((dword >> 16) & 0xFFFF)))))

/* Read word (2 bytes) from LRAM assuming that the address is already set. */
#define AdvReadWordAutoIncLram(iop_base) \
     (ADV_MEM_READW((iop_base) + IOPW_RAM_DATA))

/* Write word (2 bytes) to LRAM assuming that the address is already set. */
#define AdvWriteWordAutoIncLram(iop_base, word) \
     (ADV_MEM_WRITEW((iop_base) + IOPW_RAM_DATA, (word)))

/*
 * Define macro to check for Condor signature.
 *
 * Evaluate to ADV_TRUE if a Condor chip is found the specified port
 * address 'iop_base'. Otherwise evalue to ADV_FALSE.
 */
#define AdvFindSignature(iop_base) \
    (((AdvReadByteRegister((iop_base), IOPB_CHIP_ID_1) == \
    ADV_CHIP_ID_BYTE) && \
     (AdvReadWordRegister((iop_base), IOPW_CHIP_ID_0) == \
    ADV_CHIP_ID_WORD)) ?  ADV_TRUE : ADV_FALSE)

/*
 * Define macro to Return the version number of the chip at 'iop_base'.
 *
 * The second parameter 'bus_type' is currently unused.
 */
#define AdvGetChipVersion(iop_base, bus_type) \
    AdvReadByteRegister((iop_base), IOPB_CHIP_TYPE_REV)

/*
 * Abort an SRB in the chip's RISC Memory. The 'srb_ptr' argument must
 * match the ASC_SCSI_REQ_Q 'srb_ptr' field.
 *
 * If the request has not yet been sent to the device it will simply be
 * aborted from RISC memory. If the request is disconnected it will be
 * aborted on reselection by sending an Abort Message to the target ID.
 *
 * Return value:
 *      ADV_TRUE(1) - Queue was successfully aborted.
 *      ADV_FALSE(0) - Queue was not found on the active queue list.
 */
#define AdvAbortQueue(asc_dvc, scsiq) \
        AdvSendIdleCmd((asc_dvc), (ushort) IDLE_CMD_ABORT, \
                       (ADV_DCNT) (scsiq))

/*
 * Send a Bus Device Reset Message to the specified target ID.
 *
 * All outstanding commands will be purged if sending the
 * Bus Device Reset Message is successful.
 *
 * Return Value:
 *      ADV_TRUE(1) - All requests on the target are purged.
 *      ADV_FALSE(0) - Couldn't issue Bus Device Reset Message; Requests
 *                     are not purged.
 */
#define AdvResetDevice(asc_dvc, target_id) \
        AdvSendIdleCmd((asc_dvc), (ushort) IDLE_CMD_DEVICE_RESET, \
                    (ADV_DCNT) (target_id))

/*
 * SCSI Wide Type definition.
 */
#define ADV_SCSI_BIT_ID_TYPE   ushort

/*
 * AdvInitScsiTarget() 'cntl_flag' options.
 */
#define ADV_SCAN_LUN           0x01
#define ADV_CAPINFO_NOLUN      0x02

/*
 * Convert target id to target id bit mask.
 */
#define ADV_TID_TO_TIDMASK(tid)   (0x01 << ((tid) & ADV_MAX_TID))

/*
 * ASC_SCSI_REQ_Q 'done_status' and 'host_status' return values.
 */

#define QD_NO_STATUS         0x00	/* Request not completed yet. */
#define QD_NO_ERROR          0x01
#define QD_ABORTED_BY_HOST   0x02
#define QD_WITH_ERROR        0x04

#define QHSTA_NO_ERROR              0x00
#define QHSTA_M_SEL_TIMEOUT         0x11
#define QHSTA_M_DATA_OVER_RUN       0x12
#define QHSTA_M_UNEXPECTED_BUS_FREE 0x13
#define QHSTA_M_QUEUE_ABORTED       0x15
#define QHSTA_M_SXFR_SDMA_ERR       0x16	/* SXFR_STATUS SCSI DMA Error */
#define QHSTA_M_SXFR_SXFR_PERR      0x17	/* SXFR_STATUS SCSI Bus Parity Error */
#define QHSTA_M_RDMA_PERR           0x18	/* RISC PCI DMA parity error */
#define QHSTA_M_SXFR_OFF_UFLW       0x19	/* SXFR_STATUS Offset Underflow */
#define QHSTA_M_SXFR_OFF_OFLW       0x20	/* SXFR_STATUS Offset Overflow */
#define QHSTA_M_SXFR_WD_TMO         0x21	/* SXFR_STATUS Watchdog Timeout */
#define QHSTA_M_SXFR_DESELECTED     0x22	/* SXFR_STATUS Deselected */
/* Note: QHSTA_M_SXFR_XFR_OFLW is identical to QHSTA_M_DATA_OVER_RUN. */
#define QHSTA_M_SXFR_XFR_OFLW       0x12	/* SXFR_STATUS Transfer Overflow */
#define QHSTA_M_SXFR_XFR_PH_ERR     0x24	/* SXFR_STATUS Transfer Phase Error */
#define QHSTA_M_SXFR_UNKNOWN_ERROR  0x25	/* SXFR_STATUS Unknown Error */
#define QHSTA_M_SCSI_BUS_RESET      0x30	/* Request aborted from SBR */
#define QHSTA_M_SCSI_BUS_RESET_UNSOL 0x31	/* Request aborted from unsol. SBR */
#define QHSTA_M_BUS_DEVICE_RESET    0x32	/* Request aborted from BDR */
#define QHSTA_M_DIRECTION_ERR       0x35	/* Data Phase mismatch */
#define QHSTA_M_DIRECTION_ERR_HUNG  0x36	/* Data Phase mismatch and bus hang */
#define QHSTA_M_WTM_TIMEOUT         0x41
#define QHSTA_M_BAD_CMPL_STATUS_IN  0x42
#define QHSTA_M_NO_AUTO_REQ_SENSE   0x43
#define QHSTA_M_AUTO_REQ_SENSE_FAIL 0x44
#define QHSTA_M_INVALID_DEVICE      0x45	/* Bad target ID */
#define QHSTA_M_FROZEN_TIDQ         0x46	/* TID Queue frozen. */
#define QHSTA_M_SGBACKUP_ERROR      0x47	/* Scatter-Gather backup error */

/* Return the address that is aligned at the next doubleword >= to 'addr'. */
#define ADV_8BALIGN(addr)      (((ulong) (addr) + 0x7) & ~0x7)
#define ADV_16BALIGN(addr)     (((ulong) (addr) + 0xF) & ~0xF)
#define ADV_32BALIGN(addr)     (((ulong) (addr) + 0x1F) & ~0x1F)

/*
 * Total contiguous memory needed for driver SG blocks.
 *
 * ADV_MAX_SG_LIST must be defined by a driver. It is the maximum
 * number of scatter-gather elements the driver supports in a
 * single request.
 */

#define ADV_SG_LIST_MAX_BYTE_SIZE \
         (sizeof(ADV_SG_BLOCK) * \
          ((ADV_MAX_SG_LIST + (NO_OF_SG_PER_BLOCK - 1))/NO_OF_SG_PER_BLOCK))

/* struct asc_board flags */
#define ASC_IS_WIDE_BOARD       0x04	/* AdvanSys Wide Board */

#define ASC_NARROW_BOARD(boardp) (((boardp)->flags & ASC_IS_WIDE_BOARD) == 0)

#define NO_ISA_DMA              0xff	/* No ISA DMA Channel Used */

#define ASC_INFO_SIZE           128	/* advansys_info() line size */

#ifdef CONFIG_PROC_FS
/* /proc/scsi/advansys/[0...] related definitions */
#define ASC_PRTBUF_SIZE         2048
#define ASC_PRTLINE_SIZE        160

#define ASC_PRT_NEXT() \
    if (cp) { \
        totlen += len; \
        leftlen -= len; \
        if (leftlen == 0) { \
            return totlen; \
        } \
        cp += len; \
    }
#endif /* CONFIG_PROC_FS */

/* Asc Library return codes */
#define ASC_TRUE        1
#define ASC_FALSE       0
#define ASC_NOERROR     1
#define ASC_BUSY        0
#define ASC_ERROR       (-1)

/* struct scsi_cmnd function return codes */
#define STATUS_BYTE(byte)   (byte)
#define MSG_BYTE(byte)      ((byte) << 8)
#define HOST_BYTE(byte)     ((byte) << 16)
#define DRIVER_BYTE(byte)   ((byte) << 24)

#define ASC_STATS(shost, counter) ASC_STATS_ADD(shost, counter, 1)
#ifndef ADVANSYS_STATS
#define ASC_STATS_ADD(shost, counter, count)
#else /* ADVANSYS_STATS */
#define ASC_STATS_ADD(shost, counter, count) \
	(((struct asc_board *) shost_priv(shost))->asc_stats.counter += (count))
#endif /* ADVANSYS_STATS */

/* If the result wraps when calculating tenths, return 0. */
#define ASC_TENTHS(num, den) \
    (((10 * ((num)/(den))) > (((num) * 10)/(den))) ? \
    0 : ((((num) * 10)/(den)) - (10 * ((num)/(den)))))

/*
 * Display a message to the console.
 */
#define ASC_PRINT(s) \
    { \
        printk("advansys: "); \
        printk(s); \
    }

#define ASC_PRINT1(s, a1) \
    { \
        printk("advansys: "); \
        printk((s), (a1)); \
    }

#define ASC_PRINT2(s, a1, a2) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2)); \
    }

#define ASC_PRINT3(s, a1, a2, a3) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2), (a3)); \
    }

#define ASC_PRINT4(s, a1, a2, a3, a4) \
    { \
        printk("advansys: "); \
        printk((s), (a1), (a2), (a3), (a4)); \
    }

#ifndef ADVANSYS_DEBUG

#define ASC_DBG(lvl, s...)
#define ASC_DBG_PRT_SCSI_HOST(lvl, s)
#define ASC_DBG_PRT_ASC_SCSI_Q(lvl, scsiqp)
#define ASC_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp)
#define ASC_DBG_PRT_ASC_QDONE_INFO(lvl, qdone)
#define ADV_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp)
#define ASC_DBG_PRT_HEX(lvl, name, start, length)
#define ASC_DBG_PRT_CDB(lvl, cdb, len)
#define ASC_DBG_PRT_SENSE(lvl, sense, len)
#define ASC_DBG_PRT_INQUIRY(lvl, inq, len)

#else /* ADVANSYS_DEBUG */

/*
 * Debugging Message Levels:
 * 0: Errors Only
 * 1: High-Level Tracing
 * 2-N: Verbose Tracing
 */

#define ASC_DBG(lvl, format, arg...) {					\
	if (asc_dbglvl >= (lvl))					\
		printk(KERN_DEBUG "%s: %s: " format, DRV_NAME,		\
			__func__ , ## arg);				\
}

#define ASC_DBG_PRT_SCSI_HOST(lvl, s) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_scsi_host(s); \
        } \
    }

#define ASC_DBG_PRT_ASC_SCSI_Q(lvl, scsiqp) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_asc_scsi_q(scsiqp); \
        } \
    }

#define ASC_DBG_PRT_ASC_QDONE_INFO(lvl, qdone) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_asc_qdone_info(qdone); \
        } \
    }

#define ASC_DBG_PRT_ADV_SCSI_REQ_Q(lvl, scsiqp) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_adv_scsi_req_q(scsiqp); \
        } \
    }

#define ASC_DBG_PRT_HEX(lvl, name, start, length) \
    { \
        if (asc_dbglvl >= (lvl)) { \
            asc_prt_hex((name), (start), (length)); \
        } \
    }

#define ASC_DBG_PRT_CDB(lvl, cdb, len) \
        ASC_DBG_PRT_HEX((lvl), "CDB", (uchar *) (cdb), (len));

#define ASC_DBG_PRT_SENSE(lvl, sense, len) \
        ASC_DBG_PRT_HEX((lvl), "SENSE", (uchar *) (sense), (len));

#define ASC_DBG_PRT_INQUIRY(lvl, inq, len) \
        ASC_DBG_PRT_HEX((lvl), "INQUIRY", (uchar *) (inq), (len));
#endif /* ADVANSYS_DEBUG */

#ifdef ADVANSYS_STATS

/* Per board statistics structure */
struct asc_stats {
	/* Driver Entrypoint Statistics */
	ADV_DCNT queuecommand;	/* # calls to advansys_queuecommand() */
	ADV_DCNT reset;		/* # calls to advansys_eh_bus_reset() */
	ADV_DCNT biosparam;	/* # calls to advansys_biosparam() */
	ADV_DCNT interrupt;	/* # advansys_interrupt() calls */
	ADV_DCNT callback;	/* # calls to asc/adv_isr_callback() */
	ADV_DCNT done;		/* # calls to request's scsi_done function */
	ADV_DCNT build_error;	/* # asc/adv_build_req() ASC_ERROR returns. */
	ADV_DCNT adv_build_noreq;	/* # adv_build_req() adv_req_t alloc. fail. */
	ADV_DCNT adv_build_nosg;	/* # adv_build_req() adv_sgblk_t alloc. fail. */
	/* AscExeScsiQueue()/AdvExeScsiQueue() Statistics */
	ADV_DCNT exe_noerror;	/* # ASC_NOERROR returns. */
	ADV_DCNT exe_busy;	/* # ASC_BUSY returns. */
	ADV_DCNT exe_error;	/* # ASC_ERROR returns. */
	ADV_DCNT exe_unknown;	/* # unknown returns. */
	/* Data Transfer Statistics */
	ADV_DCNT xfer_cnt;	/* # I/O requests received */
	ADV_DCNT xfer_elem;	/* # scatter-gather elements */
	ADV_DCNT xfer_sect;	/* # 512-byte blocks */
};
#endif /* ADVANSYS_STATS */

/*
 * Structure allocated for each board.
 *
 * This structure is allocated by scsi_host_alloc() at the end
 * of the 'Scsi_Host' structure starting at the 'hostdata'
 * field. It is guaranteed to be allocated from DMA-able memory.
 */
struct asc_board {
	struct device *dev;
	uint flags;		/* Board flags */
	unsigned int irq;
	union {
		ASC_DVC_VAR asc_dvc_var;	/* Narrow board */
		ADV_DVC_VAR adv_dvc_var;	/* Wide board */
	} dvc_var;
	union {
		ASC_DVC_CFG asc_dvc_cfg;	/* Narrow board */
		ADV_DVC_CFG adv_dvc_cfg;	/* Wide board */
	} dvc_cfg;
	ushort asc_n_io_port;	/* Number I/O ports. */
	ADV_SCSI_BIT_ID_TYPE init_tidmask;	/* Target init./valid mask */
	ushort reqcnt[ADV_MAX_TID + 1];	/* Starvation request count */
	ADV_SCSI_BIT_ID_TYPE queue_full;	/* Queue full mask */
	ushort queue_full_cnt[ADV_MAX_TID + 1];	/* Queue full count */
	union {
		ASCEEP_CONFIG asc_eep;	/* Narrow EEPROM config. */
		ADVEEP_3550_CONFIG adv_3550_eep;	/* 3550 EEPROM config. */
		ADVEEP_38C0800_CONFIG adv_38C0800_eep;	/* 38C0800 EEPROM config. */
		ADVEEP_38C1600_CONFIG adv_38C1600_eep;	/* 38C1600 EEPROM config. */
	} eep_config;
	ulong last_reset;	/* Saved last reset time */
	/* /proc/scsi/advansys/[0...] */
	char *prtbuf;		/* /proc print buffer */
#ifdef ADVANSYS_STATS
	struct asc_stats asc_stats;	/* Board statistics */
#endif				/* ADVANSYS_STATS */
	/*
	 * The following fields are used only for Narrow Boards.
	 */
	uchar sdtr_data[ASC_MAX_TID + 1];	/* SDTR information */
	/*
	 * The following fields are used only for Wide Boards.
	 */
	void __iomem *ioremap_addr;	/* I/O Memory remap address. */
	ushort ioport;		/* I/O Port address. */
	adv_req_t *adv_reqp;	/* Request structures. */
	adv_sgblk_t *adv_sgblkp;	/* Scatter-gather structures. */
	ushort bios_signature;	/* BIOS Signature. */
	ushort bios_version;	/* BIOS Version. */
	ushort bios_codeseg;	/* BIOS Code Segment. */
	ushort bios_codelen;	/* BIOS Code Segment Length. */
};

#define asc_dvc_to_board(asc_dvc) container_of(asc_dvc, struct asc_board, \
							dvc_var.asc_dvc_var)
#define adv_dvc_to_board(adv_dvc) container_of(adv_dvc, struct asc_board, \
							dvc_var.adv_dvc_var)
#define adv_dvc_to_pdev(adv_dvc) to_pci_dev(adv_dvc_to_board(adv_dvc)->dev)

#ifdef ADVANSYS_DEBUG
static int asc_dbglvl = 3;

/*
 * asc_prt_asc_dvc_var()
 */
static void asc_prt_asc_dvc_var(ASC_DVC_VAR *h)
{
	printk("ASC_DVC_VAR at addr 0x%lx\n", (ulong)h);

	printk(" iop_base 0x%x, err_code 0x%x, dvc_cntl 0x%x, bug_fix_cntl "
	       "%d,\n", h->iop_base, h->err_code, h->dvc_cntl, h->bug_fix_cntl);

	printk(" bus_type %d, init_sdtr 0x%x,\n", h->bus_type,
		(unsigned)h->init_sdtr);

	printk(" sdtr_done 0x%x, use_tagged_qng 0x%x, unit_not_ready 0x%x, "
	       "chip_no 0x%x,\n", (unsigned)h->sdtr_done,
	       (unsigned)h->use_tagged_qng, (unsigned)h->unit_not_ready,
	       (unsigned)h->chip_no);

	printk(" queue_full_or_busy 0x%x, start_motor 0x%x, scsi_reset_wait "
	       "%u,\n", (unsigned)h->queue_full_or_busy,
	       (unsigned)h->start_motor, (unsigned)h->scsi_reset_wait);

	printk(" is_in_int %u, max_total_qng %u, cur_total_qng %u, "
	       "in_critical_cnt %u,\n", (unsigned)h->is_in_int,
	       (unsigned)h->max_total_qng, (unsigned)h->cur_total_qng,
	       (unsigned)h->in_critical_cnt);

	printk(" last_q_shortage %u, init_state 0x%x, no_scam 0x%x, "
	       "pci_fix_asyn_xfer 0x%x,\n", (unsigned)h->last_q_shortage,
	       (unsigned)h->init_state, (unsigned)h->no_scam,
	       (unsigned)h->pci_fix_asyn_xfer);

	printk(" cfg 0x%lx\n", (ulong)h->cfg);
}

/*
 * asc_prt_asc_dvc_cfg()
 */
static void asc_prt_asc_dvc_cfg(ASC_DVC_CFG *h)
{
	printk("ASC_DVC_CFG at addr 0x%lx\n", (ulong)h);

	printk(" can_tagged_qng 0x%x, cmd_qng_enabled 0x%x,\n",
	       h->can_tagged_qng, h->cmd_qng_enabled);
	printk(" disc_enable 0x%x, sdtr_enable 0x%x,\n",
	       h->disc_enable, h->sdtr_enable);

	printk(" chip_scsi_id %d, isa_dma_speed %d, isa_dma_channel %d, "
		"chip_version %d,\n", h->chip_scsi_id, h->isa_dma_speed,
		h->isa_dma_channel, h->chip_version);

	printk(" mcode_date 0x%x, mcode_version %d\n",
		h->mcode_date, h->mcode_version);
}

/*
 * asc_prt_adv_dvc_var()
 *
 * Display an ADV_DVC_VAR structure.
 */
static void asc_prt_adv_dvc_var(ADV_DVC_VAR *h)
{
	printk(" ADV_DVC_VAR at addr 0x%lx\n", (ulong)h);

	printk("  iop_base 0x%lx, err_code 0x%x, ultra_able 0x%x\n",
	       (ulong)h->iop_base, h->err_code, (unsigned)h->ultra_able);

	printk("  sdtr_able 0x%x, wdtr_able 0x%x\n",
	       (unsigned)h->sdtr_able, (unsigned)h->wdtr_able);

	printk("  start_motor 0x%x, scsi_reset_wait 0x%x\n",
	       (unsigned)h->start_motor, (unsigned)h->scsi_reset_wait);

	printk("  max_host_qng %u, max_dvc_qng %u, carr_freelist 0x%lxn\n",
	       (unsigned)h->max_host_qng, (unsigned)h->max_dvc_qng,
	       (ulong)h->carr_freelist);

	printk("  icq_sp 0x%lx, irq_sp 0x%lx\n",
	       (ulong)h->icq_sp, (ulong)h->irq_sp);

	printk("  no_scam 0x%x, tagqng_able 0x%x\n",
	       (unsigned)h->no_scam, (unsigned)h->tagqng_able);

	printk("  chip_scsi_id 0x%x, cfg 0x%lx\n",
	       (unsigned)h->chip_scsi_id, (ulong)h->cfg);
}

/*
 * asc_prt_adv_dvc_cfg()
 *
 * Display an ADV_DVC_CFG structure.
 */
static void asc_prt_adv_dvc_cfg(ADV_DVC_CFG *h)
{
	printk(" ADV_DVC_CFG at addr 0x%lx\n", (ulong)h);

	printk("  disc_enable 0x%x, termination 0x%x\n",
	       h->disc_enable, h->termination);

	printk("  chip_version 0x%x, mcode_date 0x%x\n",
	       h->chip_version, h->mcode_date);

	printk("  mcode_version 0x%x, control_flag 0x%x\n",
	       h->mcode_version, h->control_flag);
}

/*
 * asc_prt_scsi_host()
 */
static void asc_prt_scsi_host(struct Scsi_Host *s)
{
	struct asc_board *boardp = shost_priv(s);

	printk("Scsi_Host at addr 0x%p, device %s\n", s, dev_name(boardp->dev));
	printk(" host_busy %u, host_no %d, last_reset %d,\n",
	       s->host_busy, s->host_no, (unsigned)s->last_reset);

	printk(" base 0x%lx, io_port 0x%lx, irq %d,\n",
	       (ulong)s->base, (ulong)s->io_port, boardp->irq);

	printk(" dma_channel %d, this_id %d, can_queue %d,\n",
	       s->dma_channel, s->this_id, s->can_queue);

	printk(" cmd_per_lun %d, sg_tablesize %d, unchecked_isa_dma %d\n",
	       s->cmd_per_lun, s->sg_tablesize, s->unchecked_isa_dma);

	if (ASC_NARROW_BOARD(boardp)) {
		asc_prt_asc_dvc_var(&boardp->dvc_var.asc_dvc_var);
		asc_prt_asc_dvc_cfg(&boardp->dvc_cfg.asc_dvc_cfg);
	} else {
		asc_prt_adv_dvc_var(&boardp->dvc_var.adv_dvc_var);
		asc_prt_adv_dvc_cfg(&boardp->dvc_cfg.adv_dvc_cfg);
	}
}

/*
 * asc_prt_hex()
 *
 * Print hexadecimal output in 4 byte groupings 32 bytes
 * or 8 double-words per line.
 */
static void asc_prt_hex(char *f, uchar *s, int l)
{
	int i;
	int j;
	int k;
	int m;

	printk("%s: (%d bytes)\n", f, l);

	for (i = 0; i < l; i += 32) {

		/* Display a maximum of 8 double-words per line. */
		if ((k = (l - i) / 4) >= 8) {
			k = 8;
			m = 0;
		} else {
			m = (l - i) % 4;
		}

		for (j = 0; j < k; j++) {
			printk(" %2.2X%2.2X%2.2X%2.2X",
			       (unsigned)s[i + (j * 4)],
			       (unsigned)s[i + (j * 4) + 1],
			       (unsigned)s[i + (j * 4) + 2],
			       (unsigned)s[i + (j * 4) + 3]);
		}

		switch (m) {
		case 0:
		default:
			break;
		case 1:
			printk(" %2.2X", (unsigned)s[i + (j * 4)]);
			break;
		case 2:
			printk(" %2.2X%2.2X",
			       (unsigned)s[i + (j * 4)],
			       (unsigned)s[i + (j * 4) + 1]);
			break;
		case 3:
			printk(" %2.2X%2.2X%2.2X",
			       (unsigned)s[i + (j * 4) + 1],
			       (unsigned)s[i + (j * 4) + 2],
			       (unsigned)s[i + (j * 4) + 3]);
			break;
		}

		printk("\n");
	}
}

/*
 * asc_prt_asc_scsi_q()
 */
static void asc_prt_asc_scsi_q(ASC_SCSI_Q *q)
{
	ASC_SG_HEAD *sgp;
	int i;

	printk("ASC_SCSI_Q at addr 0x%lx\n", (ulong)q);

	printk
	    (" target_ix 0x%x, target_lun %u, srb_ptr 0x%lx, tag_code 0x%x,\n",
	     q->q2.target_ix, q->q1.target_lun, (ulong)q->q2.srb_ptr,
	     q->q2.tag_code);

	printk
	    (" data_addr 0x%lx, data_cnt %lu, sense_addr 0x%lx, sense_len %u,\n",
	     (ulong)le32_to_cpu(q->q1.data_addr),
	     (ulong)le32_to_cpu(q->q1.data_cnt),
	     (ulong)le32_to_cpu(q->q1.sense_addr), q->q1.sense_len);

	printk(" cdbptr 0x%lx, cdb_len %u, sg_head 0x%lx, sg_queue_cnt %u\n",
	       (ulong)q->cdbptr, q->q2.cdb_len,
	       (ulong)q->sg_head, q->q1.sg_queue_cnt);

	if (q->sg_head) {
		sgp = q->sg_head;
		printk("ASC_SG_HEAD at addr 0x%lx\n", (ulong)sgp);
		printk(" entry_cnt %u, queue_cnt %u\n", sgp->entry_cnt,
		       sgp->queue_cnt);
		for (i = 0; i < sgp->entry_cnt; i++) {
			printk(" [%u]: addr 0x%lx, bytes %lu\n",
			       i, (ulong)le32_to_cpu(sgp->sg_list[i].addr),
			       (ulong)le32_to_cpu(sgp->sg_list[i].bytes));
		}

	}
}

/*
 * asc_prt_asc_qdone_info()
 */
static void asc_prt_asc_qdone_info(ASC_QDONE_INFO *q)
{
	printk("ASC_QDONE_INFO at addr 0x%lx\n", (ulong)q);
	printk(" srb_ptr 0x%lx, target_ix %u, cdb_len %u, tag_code %u,\n",
	       (ulong)q->d2.srb_ptr, q->d2.target_ix, q->d2.cdb_len,
	       q->d2.tag_code);
	printk
	    (" done_stat 0x%x, host_stat 0x%x, scsi_stat 0x%x, scsi_msg 0x%x\n",
	     q->d3.done_stat, q->d3.host_stat, q->d3.scsi_stat, q->d3.scsi_msg);
}

/*
 * asc_prt_adv_sgblock()
 *
 * Display an ADV_SG_BLOCK structure.
 */
static void asc_prt_adv_sgblock(int sgblockno, ADV_SG_BLOCK *b)
{
	int i;

	printk(" ASC_SG_BLOCK at addr 0x%lx (sgblockno %d)\n",
	       (ulong)b, sgblockno);
	printk("  sg_cnt %u, sg_ptr 0x%lx\n",
	       b->sg_cnt, (ulong)le32_to_cpu(b->sg_ptr));
	BUG_ON(b->sg_cnt > NO_OF_SG_PER_BLOCK);
	if (b->sg_ptr != 0)
		BUG_ON(b->sg_cnt != NO_OF_SG_PER_BLOCK);
	for (i = 0; i < b->sg_cnt; i++) {
		printk("  [%u]: sg_addr 0x%lx, sg_count 0x%lx\n",
		       i, (ulong)b->sg_list[i].sg_addr,
		       (ulong)b->sg_list[i].sg_count);
	}
}

/*
 * asc_prt_adv_scsi_req_q()
 *
 * Display an ADV_SCSI_REQ_Q structure.
 */
static void asc_prt_adv_scsi_req_q(ADV_SCSI_REQ_Q *q)
{
	int sg_blk_cnt;
	struct asc_sg_block *sg_ptr;

	printk("ADV_SCSI_REQ_Q at addr 0x%lx\n", (ulong)q);

	printk("  target_id %u, target_lun %u, srb_ptr 0x%lx, a_flag 0x%x\n",
	       q->target_id, q->target_lun, (ulong)q->srb_ptr, q->a_flag);

	printk("  cntl 0x%x, data_addr 0x%lx, vdata_addr 0x%lx\n",
	       q->cntl, (ulong)le32_to_cpu(q->data_addr), (ulong)q->vdata_addr);

	printk("  data_cnt %lu, sense_addr 0x%lx, sense_len %u,\n",
	       (ulong)le32_to_cpu(q->data_cnt),
	       (ulong)le32_to_cpu(q->sense_addr), q->sense_len);

	printk
	    ("  cdb_len %u, done_status 0x%x, host_status 0x%x, scsi_status 0x%x\n",
	     q->cdb_len, q->done_status, q->host_status, q->scsi_status);

	printk("  sg_working_ix 0x%x, target_cmd %u\n",
	       q->sg_working_ix, q->target_cmd);

	printk("  scsiq_rptr 0x%lx, sg_real_addr 0x%lx, sg_list_ptr 0x%lx\n",
	       (ulong)le32_to_cpu(q->scsiq_rptr),
	       (ulong)le32_to_cpu(q->sg_real_addr), (ulong)q->sg_list_ptr);

	/* Display the request's ADV_SG_BLOCK structures. */
	if (q->sg_list_ptr != NULL) {
		sg_blk_cnt = 0;
		while (1) {
			/*
			 * 'sg_ptr' is a physical address. Convert it to a virtual
			 * address by indexing 'sg_blk_cnt' into the virtual address
			 * array 'sg_list_ptr'.
			 *
			 * XXX - Assumes all SG physical blocks are virtually contiguous.
			 */
			sg_ptr =
			    &(((ADV_SG_BLOCK *)(q->sg_list_ptr))[sg_blk_cnt]);
			asc_prt_adv_sgblock(sg_blk_cnt, sg_ptr);
			if (sg_ptr->sg_ptr == 0) {
				break;
			}
			sg_blk_cnt++;
		}
	}
}
#endif /* ADVANSYS_DEBUG */

/*
 * The advansys chip/microcode contains a 32-bit identifier for each command
 * known as the 'srb'.  I don't know what it stands for.  The driver used
 * to encode the scsi_cmnd pointer by calling virt_to_bus and retrieve it
 * with bus_to_virt.  Now the driver keeps a per-host map of integers to
 * pointers.  It auto-expands when full, unless it can't allocate memory.
 * Note that an srb of 0 is treated specially by the chip/firmware, hence
 * the return of i+1 in this routine, and the corresponding subtraction in
 * the inverse routine.
 */
#define BAD_SRB 0
static u32 advansys_ptr_to_srb(struct asc_dvc_var *asc_dvc, void *ptr)
{
	int i;
	void **new_ptr;

	for (i = 0; i < asc_dvc->ptr_map_count; i++) {
		if (!asc_dvc->ptr_map[i])
			goto out;
	}

	if (asc_dvc->ptr_map_count == 0)
		asc_dvc->ptr_map_count = 1;
	else
		asc_dvc->ptr_map_count *= 2;

	new_ptr = krealloc(asc_dvc->ptr_map,
			asc_dvc->ptr_map_count * sizeof(void *), GFP_ATOMIC);
	if (!new_ptr)
		return BAD_SRB;
	asc_dvc->ptr_map = new_ptr;
 out:
	ASC_DBG(3, "Putting ptr %p into array offset %d\n", ptr, i);
	asc_dvc->ptr_map[i] = ptr;
	return i + 1;
}

static void * advansys_srb_to_ptr(struct asc_dvc_var *asc_dvc, u32 srb)
{
	void *ptr;

	srb--;
	if (srb >= asc_dvc->ptr_map_count) {
		printk("advansys: bad SRB %u, max %u\n", srb,
							asc_dvc->ptr_map_count);
		return NULL;
	}
	ptr = asc_dvc->ptr_map[srb];
	asc_dvc->ptr_map[srb] = NULL;
	ASC_DBG(3, "Returning ptr %p from array offset %d\n", ptr, srb);
	return ptr;
}

/*
 * advansys_info()
 *
 * Return suitable for printing on the console with the argument
 * adapter's configuration information.
 *
 * Note: The information line should not exceed ASC_INFO_SIZE bytes,
 * otherwise the static 'info' array will be overrun.
 */
static const char *advansys_info(struct Scsi_Host *shost)
{
	static char info[ASC_INFO_SIZE];
	struct asc_board *boardp = shost_priv(shost);
	ASC_DVC_VAR *asc_dvc_varp;
	ADV_DVC_VAR *adv_dvc_varp;
	char *busname;
	char *widename = NULL;

	if (ASC_NARROW_BOARD(boardp)) {
		asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
		ASC_DBG(1, "begin\n");
		if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
			if ((asc_dvc_varp->bus_type & ASC_IS_ISAPNP) ==
			    ASC_IS_ISAPNP) {
				busname = "ISA PnP";
			} else {
				busname = "ISA";
			}
			sprintf(info,
				"AdvanSys SCSI %s: %s: IO 0x%lX-0x%lX, IRQ 0x%X, DMA 0x%X",
				ASC_VERSION, busname,
				(ulong)shost->io_port,
				(ulong)shost->io_port + ASC_IOADR_GAP - 1,
				boardp->irq, shost->dma_channel);
		} else {
			if (asc_dvc_varp->bus_type & ASC_IS_VL) {
				busname = "VL";
			} else if (asc_dvc_varp->bus_type & ASC_IS_EISA) {
				busname = "EISA";
			} else if (asc_dvc_varp->bus_type & ASC_IS_PCI) {
				if ((asc_dvc_varp->bus_type & ASC_IS_PCI_ULTRA)
				    == ASC_IS_PCI_ULTRA) {
					busname = "PCI Ultra";
				} else {
					busname = "PCI";
				}
			} else {
				busname = "?";
				shost_printk(KERN_ERR, shost, "unknown bus "
					"type %d\n", asc_dvc_varp->bus_type);
			}
			sprintf(info,
				"AdvanSys SCSI %s: %s: IO 0x%lX-0x%lX, IRQ 0x%X",
				ASC_VERSION, busname, (ulong)shost->io_port,
				(ulong)shost->io_port + ASC_IOADR_GAP - 1,
				boardp->irq);
		}
	} else {
		/*
		 * Wide Adapter Information
		 *
		 * Memory-mapped I/O is used instead of I/O space to access
		 * the adapter, but display the I/O Port range. The Memory
		 * I/O address is displayed through the driver /proc file.
		 */
		adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
		if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
			widename = "Ultra-Wide";
		} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
			widename = "Ultra2-Wide";
		} else {
			widename = "Ultra3-Wide";
		}
		sprintf(info,
			"AdvanSys SCSI %s: PCI %s: PCIMEM 0x%lX-0x%lX, IRQ 0x%X",
			ASC_VERSION, widename, (ulong)adv_dvc_varp->iop_base,
			(ulong)adv_dvc_varp->iop_base + boardp->asc_n_io_port - 1, boardp->irq);
	}
	BUG_ON(strlen(info) >= ASC_INFO_SIZE);
	ASC_DBG(1, "end\n");
	return info;
}

#ifdef CONFIG_PROC_FS
/*
 * asc_prt_line()
 *
 * If 'cp' is NULL print to the console, otherwise print to a buffer.
 *
 * Return 0 if printing to the console, otherwise return the number of
 * bytes written to the buffer.
 *
 * Note: If any single line is greater than ASC_PRTLINE_SIZE bytes the stack
 * will be corrupted. 's[]' is defined to be ASC_PRTLINE_SIZE bytes.
 */
static int asc_prt_line(char *buf, int buflen, char *fmt, ...)
{
	va_list args;
	int ret;
	char s[ASC_PRTLINE_SIZE];

	va_start(args, fmt);
	ret = vsprintf(s, fmt, args);
	BUG_ON(ret >= ASC_PRTLINE_SIZE);
	if (buf == NULL) {
		(void)printk(s);
		ret = 0;
	} else {
		ret = min(buflen, ret);
		memcpy(buf, s, ret);
	}
	va_end(args);
	return ret;
}

/*
 * asc_prt_board_devices()
 *
 * Print driver information for devices attached to the board.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_board_devices(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	int leftlen;
	int totlen;
	int len;
	int chip_scsi_id;
	int i;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nDevice Information for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	if (ASC_NARROW_BOARD(boardp)) {
		chip_scsi_id = boardp->dvc_cfg.asc_dvc_cfg.chip_scsi_id;
	} else {
		chip_scsi_id = boardp->dvc_var.adv_dvc_var.chip_scsi_id;
	}

	len = asc_prt_line(cp, leftlen, "Target IDs Detected:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if (boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) {
			len = asc_prt_line(cp, leftlen, " %X,", i);
			ASC_PRT_NEXT();
		}
	}
	len = asc_prt_line(cp, leftlen, " (%X=Host Adapter)\n", chip_scsi_id);
	ASC_PRT_NEXT();

	return totlen;
}

/*
 * Display Wide Board BIOS Information.
 */
static int asc_prt_adv_bios(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	int leftlen;
	int totlen;
	int len;
	ushort major, minor, letter;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen, "\nROM BIOS Version: ");
	ASC_PRT_NEXT();

	/*
	 * If the BIOS saved a valid signature, then fill in
	 * the BIOS code segment base address.
	 */
	if (boardp->bios_signature != 0x55AA) {
		len = asc_prt_line(cp, leftlen, "Disabled or Pre-3.1\n");
		ASC_PRT_NEXT();
		len = asc_prt_line(cp, leftlen,
				   "BIOS either disabled or Pre-3.1. If it is pre-3.1, then a newer version\n");
		ASC_PRT_NEXT();
		len = asc_prt_line(cp, leftlen,
				   "can be found at the ConnectCom FTP site: ftp://ftp.connectcom.net/pub\n");
		ASC_PRT_NEXT();
	} else {
		major = (boardp->bios_version >> 12) & 0xF;
		minor = (boardp->bios_version >> 8) & 0xF;
		letter = (boardp->bios_version & 0xFF);

		len = asc_prt_line(cp, leftlen, "%d.%d%c\n",
				   major, minor,
				   letter >= 26 ? '?' : letter + 'A');
		ASC_PRT_NEXT();

		/*
		 * Current available ROM BIOS release is 3.1I for UW
		 * and 3.2I for U2W. This code doesn't differentiate
		 * UW and U2W boards.
		 */
		if (major < 3 || (major <= 3 && minor < 1) ||
		    (major <= 3 && minor <= 1 && letter < ('I' - 'A'))) {
			len = asc_prt_line(cp, leftlen,
					   "Newer version of ROM BIOS is available at the ConnectCom FTP site:\n");
			ASC_PRT_NEXT();
			len = asc_prt_line(cp, leftlen,
					   "ftp://ftp.connectcom.net/pub\n");
			ASC_PRT_NEXT();
		}
	}

	return totlen;
}

/*
 * Add serial number to information bar if signature AAh
 * is found in at bit 15-9 (7 bits) of word 1.
 *
 * Serial Number consists fo 12 alpha-numeric digits.
 *
 *       1 - Product type (A,B,C,D..)  Word0: 15-13 (3 bits)
 *       2 - MFG Location (A,B,C,D..)  Word0: 12-10 (3 bits)
 *     3-4 - Product ID (0-99)         Word0: 9-0 (10 bits)
 *       5 - Product revision (A-J)    Word0:  "         "
 *
 *           Signature                 Word1: 15-9 (7 bits)
 *       6 - Year (0-9)                Word1: 8-6 (3 bits) & Word2: 15 (1 bit)
 *     7-8 - Week of the year (1-52)   Word1: 5-0 (6 bits)
 *
 *    9-12 - Serial Number (A001-Z999) Word2: 14-0 (15 bits)
 *
 * Note 1: Only production cards will have a serial number.
 *
 * Note 2: Signature is most significant 7 bits (0xFE).
 *
 * Returns ASC_TRUE if serial number found, otherwise returns ASC_FALSE.
 */
static int asc_get_eeprom_string(ushort *serialnum, uchar *cp)
{
	ushort w, num;

	if ((serialnum[1] & 0xFE00) != ((ushort)0xAA << 8)) {
		return ASC_FALSE;
	} else {
		/*
		 * First word - 6 digits.
		 */
		w = serialnum[0];

		/* Product type - 1st digit. */
		if ((*cp = 'A' + ((w & 0xE000) >> 13)) == 'H') {
			/* Product type is P=Prototype */
			*cp += 0x8;
		}
		cp++;

		/* Manufacturing location - 2nd digit. */
		*cp++ = 'A' + ((w & 0x1C00) >> 10);

		/* Product ID - 3rd, 4th digits. */
		num = w & 0x3FF;
		*cp++ = '0' + (num / 100);
		num %= 100;
		*cp++ = '0' + (num / 10);

		/* Product revision - 5th digit. */
		*cp++ = 'A' + (num % 10);

		/*
		 * Second word
		 */
		w = serialnum[1];

		/*
		 * Year - 6th digit.
		 *
		 * If bit 15 of third word is set, then the
		 * last digit of the year is greater than 7.
		 */
		if (serialnum[2] & 0x8000) {
			*cp++ = '8' + ((w & 0x1C0) >> 6);
		} else {
			*cp++ = '0' + ((w & 0x1C0) >> 6);
		}

		/* Week of year - 7th, 8th digits. */
		num = w & 0x003F;
		*cp++ = '0' + num / 10;
		num %= 10;
		*cp++ = '0' + num;

		/*
		 * Third word
		 */
		w = serialnum[2] & 0x7FFF;

		/* Serial number - 9th digit. */
		*cp++ = 'A' + (w / 1000);

		/* 10th, 11th, 12th digits. */
		num = w % 1000;
		*cp++ = '0' + num / 100;
		num %= 100;
		*cp++ = '0' + num / 10;
		num %= 10;
		*cp++ = '0' + num;

		*cp = '\0';	/* Null Terminate the string. */
		return ASC_TRUE;
	}
}

/*
 * asc_prt_asc_board_eeprom()
 *
 * Print board EEPROM configuration.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_asc_board_eeprom(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	ASC_DVC_VAR *asc_dvc_varp;
	int leftlen;
	int totlen;
	int len;
	ASCEEP_CONFIG *ep;
	int i;
#ifdef CONFIG_ISA
	int isa_dma_speed[] = { 10, 8, 7, 6, 5, 4, 3, 2 };
#endif /* CONFIG_ISA */
	uchar serialstr[13];

	asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
	ep = &boardp->eep_config.asc_eep;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nEEPROM Settings for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	if (asc_get_eeprom_string((ushort *)&ep->adapter_info[0], serialstr)
	    == ASC_TRUE) {
		len =
		    asc_prt_line(cp, leftlen, " Serial Number: %s\n",
				 serialstr);
		ASC_PRT_NEXT();
	} else {
		if (ep->adapter_info[5] == 0xBB) {
			len = asc_prt_line(cp, leftlen,
					   " Default Settings Used for EEPROM-less Adapter.\n");
			ASC_PRT_NEXT();
		} else {
			len = asc_prt_line(cp, leftlen,
					   " Serial Number Signature Not Present.\n");
			ASC_PRT_NEXT();
		}
	}

	len = asc_prt_line(cp, leftlen,
			   " Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
			   ASC_EEP_GET_CHIP_ID(ep), ep->max_total_qng,
			   ep->max_tag_qng);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " cntl 0x%x, no_scam 0x%x\n", ep->cntl, ep->no_scam);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Target ID:           ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %d", i);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Disconnects:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (ep->
				    disc_enable & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Command Queuing:     ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (ep->
				    use_cmd_qng & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Start Motor:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (ep->
				    start_motor & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Synchronous Transfer:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (ep->
				    init_sdtr & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

#ifdef CONFIG_ISA
	if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
		len = asc_prt_line(cp, leftlen,
				   " Host ISA DMA speed:   %d MB/S\n",
				   isa_dma_speed[ASC_EEP_GET_DMA_SPD(ep)]);
		ASC_PRT_NEXT();
	}
#endif /* CONFIG_ISA */

	return totlen;
}

/*
 * asc_prt_adv_board_eeprom()
 *
 * Print board EEPROM configuration.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_adv_board_eeprom(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	ADV_DVC_VAR *adv_dvc_varp;
	int leftlen;
	int totlen;
	int len;
	int i;
	char *termstr;
	uchar serialstr[13];
	ADVEEP_3550_CONFIG *ep_3550 = NULL;
	ADVEEP_38C0800_CONFIG *ep_38C0800 = NULL;
	ADVEEP_38C1600_CONFIG *ep_38C1600 = NULL;
	ushort word;
	ushort *wordp;
	ushort sdtr_speed = 0;

	adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		ep_3550 = &boardp->eep_config.adv_3550_eep;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		ep_38C0800 = &boardp->eep_config.adv_38C0800_eep;
	} else {
		ep_38C1600 = &boardp->eep_config.adv_38C1600_eep;
	}

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nEEPROM Settings for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		wordp = &ep_3550->serial_number_word1;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		wordp = &ep_38C0800->serial_number_word1;
	} else {
		wordp = &ep_38C1600->serial_number_word1;
	}

	if (asc_get_eeprom_string(wordp, serialstr) == ASC_TRUE) {
		len =
		    asc_prt_line(cp, leftlen, " Serial Number: %s\n",
				 serialstr);
		ASC_PRT_NEXT();
	} else {
		len = asc_prt_line(cp, leftlen,
				   " Serial Number Signature Not Present.\n");
		ASC_PRT_NEXT();
	}

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		len = asc_prt_line(cp, leftlen,
				   " Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
				   ep_3550->adapter_scsi_id,
				   ep_3550->max_host_qng, ep_3550->max_dvc_qng);
		ASC_PRT_NEXT();
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		len = asc_prt_line(cp, leftlen,
				   " Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
				   ep_38C0800->adapter_scsi_id,
				   ep_38C0800->max_host_qng,
				   ep_38C0800->max_dvc_qng);
		ASC_PRT_NEXT();
	} else {
		len = asc_prt_line(cp, leftlen,
				   " Host SCSI ID: %u, Host Queue Size: %u, Device Queue Size: %u\n",
				   ep_38C1600->adapter_scsi_id,
				   ep_38C1600->max_host_qng,
				   ep_38C1600->max_dvc_qng);
		ASC_PRT_NEXT();
	}
	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		word = ep_3550->termination;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		word = ep_38C0800->termination_lvd;
	} else {
		word = ep_38C1600->termination_lvd;
	}
	switch (word) {
	case 1:
		termstr = "Low Off/High Off";
		break;
	case 2:
		termstr = "Low Off/High On";
		break;
	case 3:
		termstr = "Low On/High On";
		break;
	default:
	case 0:
		termstr = "Automatic";
		break;
	}

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		len = asc_prt_line(cp, leftlen,
				   " termination: %u (%s), bios_ctrl: 0x%x\n",
				   ep_3550->termination, termstr,
				   ep_3550->bios_ctrl);
		ASC_PRT_NEXT();
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		len = asc_prt_line(cp, leftlen,
				   " termination: %u (%s), bios_ctrl: 0x%x\n",
				   ep_38C0800->termination_lvd, termstr,
				   ep_38C0800->bios_ctrl);
		ASC_PRT_NEXT();
	} else {
		len = asc_prt_line(cp, leftlen,
				   " termination: %u (%s), bios_ctrl: 0x%x\n",
				   ep_38C1600->termination_lvd, termstr,
				   ep_38C1600->bios_ctrl);
		ASC_PRT_NEXT();
	}

	len = asc_prt_line(cp, leftlen, " Target ID:           ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %X", i);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		word = ep_3550->disc_enable;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		word = ep_38C0800->disc_enable;
	} else {
		word = ep_38C1600->disc_enable;
	}
	len = asc_prt_line(cp, leftlen, " Disconnects:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		word = ep_3550->tagqng_able;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		word = ep_38C0800->tagqng_able;
	} else {
		word = ep_38C1600->tagqng_able;
	}
	len = asc_prt_line(cp, leftlen, " Command Queuing:     ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		word = ep_3550->start_motor;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		word = ep_38C0800->start_motor;
	} else {
		word = ep_38C1600->start_motor;
	}
	len = asc_prt_line(cp, leftlen, " Start Motor:         ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		len = asc_prt_line(cp, leftlen, " Synchronous Transfer:");
		ASC_PRT_NEXT();
		for (i = 0; i <= ADV_MAX_TID; i++) {
			len = asc_prt_line(cp, leftlen, " %c",
					   (ep_3550->
					    sdtr_able & ADV_TID_TO_TIDMASK(i)) ?
					   'Y' : 'N');
			ASC_PRT_NEXT();
		}
		len = asc_prt_line(cp, leftlen, "\n");
		ASC_PRT_NEXT();
	}

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		len = asc_prt_line(cp, leftlen, " Ultra Transfer:      ");
		ASC_PRT_NEXT();
		for (i = 0; i <= ADV_MAX_TID; i++) {
			len = asc_prt_line(cp, leftlen, " %c",
					   (ep_3550->
					    ultra_able & ADV_TID_TO_TIDMASK(i))
					   ? 'Y' : 'N');
			ASC_PRT_NEXT();
		}
		len = asc_prt_line(cp, leftlen, "\n");
		ASC_PRT_NEXT();
	}

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
		word = ep_3550->wdtr_able;
	} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
		word = ep_38C0800->wdtr_able;
	} else {
		word = ep_38C1600->wdtr_able;
	}
	len = asc_prt_line(cp, leftlen, " Wide Transfer:       ");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		len = asc_prt_line(cp, leftlen, " %c",
				   (word & ADV_TID_TO_TIDMASK(i)) ? 'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800 ||
	    adv_dvc_varp->chip_type == ADV_CHIP_ASC38C1600) {
		len = asc_prt_line(cp, leftlen,
				   " Synchronous Transfer Speed (Mhz):\n  ");
		ASC_PRT_NEXT();
		for (i = 0; i <= ADV_MAX_TID; i++) {
			char *speed_str;

			if (i == 0) {
				sdtr_speed = adv_dvc_varp->sdtr_speed1;
			} else if (i == 4) {
				sdtr_speed = adv_dvc_varp->sdtr_speed2;
			} else if (i == 8) {
				sdtr_speed = adv_dvc_varp->sdtr_speed3;
			} else if (i == 12) {
				sdtr_speed = adv_dvc_varp->sdtr_speed4;
			}
			switch (sdtr_speed & ADV_MAX_TID) {
			case 0:
				speed_str = "Off";
				break;
			case 1:
				speed_str = "  5";
				break;
			case 2:
				speed_str = " 10";
				break;
			case 3:
				speed_str = " 20";
				break;
			case 4:
				speed_str = " 40";
				break;
			case 5:
				speed_str = " 80";
				break;
			default:
				speed_str = "Unk";
				break;
			}
			len = asc_prt_line(cp, leftlen, "%X:%s ", i, speed_str);
			ASC_PRT_NEXT();
			if (i == 7) {
				len = asc_prt_line(cp, leftlen, "\n  ");
				ASC_PRT_NEXT();
			}
			sdtr_speed >>= 4;
		}
		len = asc_prt_line(cp, leftlen, "\n");
		ASC_PRT_NEXT();
	}

	return totlen;
}

/*
 * asc_prt_driver_conf()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_driver_conf(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	int leftlen;
	int totlen;
	int len;
	int chip_scsi_id;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nLinux Driver Configuration and Information for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " host_busy %u, last_reset %u, max_id %u, max_lun %u, max_channel %u\n",
			   shost->host_busy, shost->last_reset, shost->max_id,
			   shost->max_lun, shost->max_channel);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " unique_id %d, can_queue %d, this_id %d, sg_tablesize %u, cmd_per_lun %u\n",
			   shost->unique_id, shost->can_queue, shost->this_id,
			   shost->sg_tablesize, shost->cmd_per_lun);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " unchecked_isa_dma %d, use_clustering %d\n",
			   shost->unchecked_isa_dma, shost->use_clustering);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " flags 0x%x, last_reset 0x%x, jiffies 0x%x, asc_n_io_port 0x%x\n",
			   boardp->flags, boardp->last_reset, jiffies,
			   boardp->asc_n_io_port);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " io_port 0x%x\n", shost->io_port);
	ASC_PRT_NEXT();

	if (ASC_NARROW_BOARD(boardp)) {
		chip_scsi_id = boardp->dvc_cfg.asc_dvc_cfg.chip_scsi_id;
	} else {
		chip_scsi_id = boardp->dvc_var.adv_dvc_var.chip_scsi_id;
	}

	return totlen;
}

/*
 * asc_prt_asc_board_info()
 *
 * Print dynamic board configuration information.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_asc_board_info(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	int chip_scsi_id;
	int leftlen;
	int totlen;
	int len;
	ASC_DVC_VAR *v;
	ASC_DVC_CFG *c;
	int i;
	int renegotiate = 0;

	v = &boardp->dvc_var.asc_dvc_var;
	c = &boardp->dvc_cfg.asc_dvc_cfg;
	chip_scsi_id = c->chip_scsi_id;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nAsc Library Configuration and Statistics for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " chip_version %u, mcode_date 0x%x, "
			   "mcode_version 0x%x, err_code %u\n",
			   c->chip_version, c->mcode_date, c->mcode_version,
			   v->err_code);
	ASC_PRT_NEXT();

	/* Current number of commands waiting for the host. */
	len = asc_prt_line(cp, leftlen,
			   " Total Command Pending: %d\n", v->cur_total_qng);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Command Queuing:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %X:%c",
				   i,
				   (v->
				    use_tagged_qng & ADV_TID_TO_TIDMASK(i)) ?
				   'Y' : 'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	/* Current number of commands waiting for a device. */
	len = asc_prt_line(cp, leftlen, " Command Queue Pending:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %X:%u", i, v->cur_dvc_qng[i]);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	/* Current limit on number of commands that can be sent to a device. */
	len = asc_prt_line(cp, leftlen, " Command Queue Limit:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %X:%u", i, v->max_dvc_qng[i]);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	/* Indicate whether the device has returned queue full status. */
	len = asc_prt_line(cp, leftlen, " Command Queue Full:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}
		if (boardp->queue_full & ADV_TID_TO_TIDMASK(i)) {
			len = asc_prt_line(cp, leftlen, " %X:Y-%d",
					   i, boardp->queue_full_cnt[i]);
		} else {
			len = asc_prt_line(cp, leftlen, " %X:N", i);
		}
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Synchronous Transfer:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}
		len = asc_prt_line(cp, leftlen, " %X:%c",
				   i,
				   (v->
				    sdtr_done & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	for (i = 0; i <= ASC_MAX_TID; i++) {
		uchar syn_period_ix;

		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0) ||
		    ((v->init_sdtr & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		len = asc_prt_line(cp, leftlen, "  %X:", i);
		ASC_PRT_NEXT();

		if ((boardp->sdtr_data[i] & ASC_SYN_MAX_OFFSET) == 0) {
			len = asc_prt_line(cp, leftlen, " Asynchronous");
			ASC_PRT_NEXT();
		} else {
			syn_period_ix =
			    (boardp->sdtr_data[i] >> 4) & (v->max_sdtr_index -
							   1);

			len = asc_prt_line(cp, leftlen,
					   " Transfer Period Factor: %d (%d.%d Mhz),",
					   v->sdtr_period_tbl[syn_period_ix],
					   250 /
					   v->sdtr_period_tbl[syn_period_ix],
					   ASC_TENTHS(250,
						      v->
						      sdtr_period_tbl
						      [syn_period_ix]));
			ASC_PRT_NEXT();

			len = asc_prt_line(cp, leftlen, " REQ/ACK Offset: %d",
					   boardp->
					   sdtr_data[i] & ASC_SYN_MAX_OFFSET);
			ASC_PRT_NEXT();
		}

		if ((v->sdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
			len = asc_prt_line(cp, leftlen, "*\n");
			renegotiate = 1;
		} else {
			len = asc_prt_line(cp, leftlen, "\n");
		}
		ASC_PRT_NEXT();
	}

	if (renegotiate) {
		len = asc_prt_line(cp, leftlen,
				   " * = Re-negotiation pending before next command.\n");
		ASC_PRT_NEXT();
	}

	return totlen;
}

/*
 * asc_prt_adv_board_info()
 *
 * Print dynamic board configuration information.
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_adv_board_info(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	int leftlen;
	int totlen;
	int len;
	int i;
	ADV_DVC_VAR *v;
	ADV_DVC_CFG *c;
	AdvPortAddr iop_base;
	ushort chip_scsi_id;
	ushort lramword;
	uchar lrambyte;
	ushort tagqng_able;
	ushort sdtr_able, wdtr_able;
	ushort wdtr_done, sdtr_done;
	ushort period = 0;
	int renegotiate = 0;

	v = &boardp->dvc_var.adv_dvc_var;
	c = &boardp->dvc_cfg.adv_dvc_cfg;
	iop_base = v->iop_base;
	chip_scsi_id = v->chip_scsi_id;

	leftlen = cplen;
	totlen = len = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nAdv Library Configuration and Statistics for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " iop_base 0x%lx, cable_detect: %X, err_code %u\n",
			   v->iop_base,
			   AdvReadWordRegister(iop_base,
					       IOPW_SCSI_CFG1) & CABLE_DETECT,
			   v->err_code);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " chip_version %u, mcode_date 0x%x, "
			   "mcode_version 0x%x\n", c->chip_version,
			   c->mcode_date, c->mcode_version);
	ASC_PRT_NEXT();

	AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	len = asc_prt_line(cp, leftlen, " Queuing Enabled:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		len = asc_prt_line(cp, leftlen, " %X:%c",
				   i,
				   (tagqng_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Queue Limit:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + i,
				lrambyte);

		len = asc_prt_line(cp, leftlen, " %X:%d", i, lrambyte);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen, " Command Pending:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_QUEUED_CMD + i,
				lrambyte);

		len = asc_prt_line(cp, leftlen, " %X:%d", i, lrambyte);
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
	len = asc_prt_line(cp, leftlen, " Wide Enabled:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		len = asc_prt_line(cp, leftlen, " %X:%c",
				   i,
				   (wdtr_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	AdvReadWordLram(iop_base, ASC_MC_WDTR_DONE, wdtr_done);
	len = asc_prt_line(cp, leftlen, " Transfer Bit Width:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		AdvReadWordLram(iop_base,
				ASC_MC_DEVICE_HSHK_CFG_TABLE + (2 * i),
				lramword);

		len = asc_prt_line(cp, leftlen, " %X:%d",
				   i, (lramword & 0x8000) ? 16 : 8);
		ASC_PRT_NEXT();

		if ((wdtr_able & ADV_TID_TO_TIDMASK(i)) &&
		    (wdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
			len = asc_prt_line(cp, leftlen, "*");
			ASC_PRT_NEXT();
			renegotiate = 1;
		}
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	len = asc_prt_line(cp, leftlen, " Synchronous Enabled:");
	ASC_PRT_NEXT();
	for (i = 0; i <= ADV_MAX_TID; i++) {
		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		len = asc_prt_line(cp, leftlen, " %X:%c",
				   i,
				   (sdtr_able & ADV_TID_TO_TIDMASK(i)) ? 'Y' :
				   'N');
		ASC_PRT_NEXT();
	}
	len = asc_prt_line(cp, leftlen, "\n");
	ASC_PRT_NEXT();

	AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, sdtr_done);
	for (i = 0; i <= ADV_MAX_TID; i++) {

		AdvReadWordLram(iop_base,
				ASC_MC_DEVICE_HSHK_CFG_TABLE + (2 * i),
				lramword);
		lramword &= ~0x8000;

		if ((chip_scsi_id == i) ||
		    ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(i)) == 0) ||
		    ((sdtr_able & ADV_TID_TO_TIDMASK(i)) == 0)) {
			continue;
		}

		len = asc_prt_line(cp, leftlen, "  %X:", i);
		ASC_PRT_NEXT();

		if ((lramword & 0x1F) == 0) {	/* Check for REQ/ACK Offset 0. */
			len = asc_prt_line(cp, leftlen, " Asynchronous");
			ASC_PRT_NEXT();
		} else {
			len =
			    asc_prt_line(cp, leftlen,
					 " Transfer Period Factor: ");
			ASC_PRT_NEXT();

			if ((lramword & 0x1F00) == 0x1100) {	/* 80 Mhz */
				len =
				    asc_prt_line(cp, leftlen, "9 (80.0 Mhz),");
				ASC_PRT_NEXT();
			} else if ((lramword & 0x1F00) == 0x1000) {	/* 40 Mhz */
				len =
				    asc_prt_line(cp, leftlen, "10 (40.0 Mhz),");
				ASC_PRT_NEXT();
			} else {	/* 20 Mhz or below. */

				period = (((lramword >> 8) * 25) + 50) / 4;

				if (period == 0) {	/* Should never happen. */
					len =
					    asc_prt_line(cp, leftlen,
							 "%d (? Mhz), ");
					ASC_PRT_NEXT();
				} else {
					len = asc_prt_line(cp, leftlen,
							   "%d (%d.%d Mhz),",
							   period, 250 / period,
							   ASC_TENTHS(250,
								      period));
					ASC_PRT_NEXT();
				}
			}

			len = asc_prt_line(cp, leftlen, " REQ/ACK Offset: %d",
					   lramword & 0x1F);
			ASC_PRT_NEXT();
		}

		if ((sdtr_done & ADV_TID_TO_TIDMASK(i)) == 0) {
			len = asc_prt_line(cp, leftlen, "*\n");
			renegotiate = 1;
		} else {
			len = asc_prt_line(cp, leftlen, "\n");
		}
		ASC_PRT_NEXT();
	}

	if (renegotiate) {
		len = asc_prt_line(cp, leftlen,
				   " * = Re-negotiation pending before next command.\n");
		ASC_PRT_NEXT();
	}

	return totlen;
}

/*
 * asc_proc_copy()
 *
 * Copy proc information to a read buffer taking into account the current
 * read offset in the file and the remaining space in the read buffer.
 */
static int
asc_proc_copy(off_t advoffset, off_t offset, char *curbuf, int leftlen,
	      char *cp, int cplen)
{
	int cnt = 0;

	ASC_DBG(2, "offset %d, advoffset %d, cplen %d\n",
		 (unsigned)offset, (unsigned)advoffset, cplen);
	if (offset <= advoffset) {
		/* Read offset below current offset, copy everything. */
		cnt = min(cplen, leftlen);
		ASC_DBG(2, "curbuf 0x%lx, cp 0x%lx, cnt %d\n",
			 (ulong)curbuf, (ulong)cp, cnt);
		memcpy(curbuf, cp, cnt);
	} else if (offset < advoffset + cplen) {
		/* Read offset within current range, partial copy. */
		cnt = (advoffset + cplen) - offset;
		cp = (cp + cplen) - cnt;
		cnt = min(cnt, leftlen);
		ASC_DBG(2, "curbuf 0x%lx, cp 0x%lx, cnt %d\n",
			 (ulong)curbuf, (ulong)cp, cnt);
		memcpy(curbuf, cp, cnt);
	}
	return cnt;
}

#ifdef ADVANSYS_STATS
/*
 * asc_prt_board_stats()
 *
 * Note: no single line should be greater than ASC_PRTLINE_SIZE,
 * cf. asc_prt_line().
 *
 * Return the number of characters copied into 'cp'. No more than
 * 'cplen' characters will be copied to 'cp'.
 */
static int asc_prt_board_stats(struct Scsi_Host *shost, char *cp, int cplen)
{
	struct asc_board *boardp = shost_priv(shost);
	struct asc_stats *s = &boardp->asc_stats;

	int leftlen = cplen;
	int len, totlen = 0;

	len = asc_prt_line(cp, leftlen,
			   "\nLinux Driver Statistics for AdvanSys SCSI Host %d:\n",
			   shost->host_no);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " queuecommand %lu, reset %lu, biosparam %lu, interrupt %lu\n",
			   s->queuecommand, s->reset, s->biosparam,
			   s->interrupt);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " callback %lu, done %lu, build_error %lu, build_noreq %lu, build_nosg %lu\n",
			   s->callback, s->done, s->build_error,
			   s->adv_build_noreq, s->adv_build_nosg);
	ASC_PRT_NEXT();

	len = asc_prt_line(cp, leftlen,
			   " exe_noerror %lu, exe_busy %lu, exe_error %lu, exe_unknown %lu\n",
			   s->exe_noerror, s->exe_busy, s->exe_error,
			   s->exe_unknown);
	ASC_PRT_NEXT();

	/*
	 * Display data transfer statistics.
	 */
	if (s->xfer_cnt > 0) {
		len = asc_prt_line(cp, leftlen, " xfer_cnt %lu, xfer_elem %lu, ",
				   s->xfer_cnt, s->xfer_elem);
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "xfer_bytes %lu.%01lu kb\n",
				   s->xfer_sect / 2, ASC_TENTHS(s->xfer_sect, 2));
		ASC_PRT_NEXT();

		/* Scatter gather transfer statistics */
		len = asc_prt_line(cp, leftlen, " avg_num_elem %lu.%01lu, ",
				   s->xfer_elem / s->xfer_cnt,
				   ASC_TENTHS(s->xfer_elem, s->xfer_cnt));
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "avg_elem_size %lu.%01lu kb, ",
				   (s->xfer_sect / 2) / s->xfer_elem,
				   ASC_TENTHS((s->xfer_sect / 2), s->xfer_elem));
		ASC_PRT_NEXT();

		len = asc_prt_line(cp, leftlen, "avg_xfer_size %lu.%01lu kb\n",
				   (s->xfer_sect / 2) / s->xfer_cnt,
				   ASC_TENTHS((s->xfer_sect / 2), s->xfer_cnt));
		ASC_PRT_NEXT();
	}

	return totlen;
}
#endif /* ADVANSYS_STATS */

/*
 * advansys_proc_info() - /proc/scsi/advansys/{0,1,2,3,...}
 *
 * *buffer: I/O buffer
 * **start: if inout == FALSE pointer into buffer where user read should start
 * offset: current offset into a /proc/scsi/advansys/[0...] file
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * inout: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written to a
 * /proc/scsi/advansys/[0...] file.
 *
 * Note: This function uses the per board buffer 'prtbuf' which is
 * allocated when the board is initialized in advansys_detect(). The
 * buffer is ASC_PRTBUF_SIZE bytes. The function asc_proc_copy() is
 * used to write to the buffer. The way asc_proc_copy() is written
 * if 'prtbuf' is too small it will not be overwritten. Instead the
 * user just won't get all the available statistics.
 */
static int
advansys_proc_info(struct Scsi_Host *shost, char *buffer, char **start,
		   off_t offset, int length, int inout)
{
	struct asc_board *boardp = shost_priv(shost);
	char *cp;
	int cplen;
	int cnt;
	int totcnt;
	int leftlen;
	char *curbuf;
	off_t advoffset;

	ASC_DBG(1, "begin\n");

	/*
	 * User write not supported.
	 */
	if (inout == TRUE)
		return -ENOSYS;

	/*
	 * User read of /proc/scsi/advansys/[0...] file.
	 */

	/* Copy read data starting at the beginning of the buffer. */
	*start = buffer;
	curbuf = buffer;
	advoffset = 0;
	totcnt = 0;
	leftlen = length;

	/*
	 * Get board configuration information.
	 *
	 * advansys_info() returns the board string from its own static buffer.
	 */
	cp = (char *)advansys_info(shost);
	strcat(cp, "\n");
	cplen = strlen(cp);
	/* Copy board information. */
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	/*
	 * Display Wide Board BIOS Information.
	 */
	if (!ASC_NARROW_BOARD(boardp)) {
		cp = boardp->prtbuf;
		cplen = asc_prt_adv_bios(shost, cp, ASC_PRTBUF_SIZE);
		BUG_ON(cplen >= ASC_PRTBUF_SIZE);
		cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp,
				  cplen);
		totcnt += cnt;
		leftlen -= cnt;
		if (leftlen == 0) {
			ASC_DBG(1, "totcnt %d\n", totcnt);
			return totcnt;
		}
		advoffset += cplen;
		curbuf += cnt;
	}

	/*
	 * Display driver information for each device attached to the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_devices(shost, cp, ASC_PRTBUF_SIZE);
	BUG_ON(cplen >= ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	/*
	 * Display EEPROM configuration for the board.
	 */
	cp = boardp->prtbuf;
	if (ASC_NARROW_BOARD(boardp)) {
		cplen = asc_prt_asc_board_eeprom(shost, cp, ASC_PRTBUF_SIZE);
	} else {
		cplen = asc_prt_adv_board_eeprom(shost, cp, ASC_PRTBUF_SIZE);
	}
	BUG_ON(cplen >= ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	/*
	 * Display driver configuration and information for the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_driver_conf(shost, cp, ASC_PRTBUF_SIZE);
	BUG_ON(cplen >= ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

#ifdef ADVANSYS_STATS
	/*
	 * Display driver statistics for the board.
	 */
	cp = boardp->prtbuf;
	cplen = asc_prt_board_stats(shost, cp, ASC_PRTBUF_SIZE);
	BUG_ON(cplen >= ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;
#endif /* ADVANSYS_STATS */

	/*
	 * Display Asc Library dynamic configuration information
	 * for the board.
	 */
	cp = boardp->prtbuf;
	if (ASC_NARROW_BOARD(boardp)) {
		cplen = asc_prt_asc_board_info(shost, cp, ASC_PRTBUF_SIZE);
	} else {
		cplen = asc_prt_adv_board_info(shost, cp, ASC_PRTBUF_SIZE);
	}
	BUG_ON(cplen >= ASC_PRTBUF_SIZE);
	cnt = asc_proc_copy(advoffset, offset, curbuf, leftlen, cp, cplen);
	totcnt += cnt;
	leftlen -= cnt;
	if (leftlen == 0) {
		ASC_DBG(1, "totcnt %d\n", totcnt);
		return totcnt;
	}
	advoffset += cplen;
	curbuf += cnt;

	ASC_DBG(1, "totcnt %d\n", totcnt);

	return totcnt;
}
#endif /* CONFIG_PROC_FS */

static void asc_scsi_done(struct scsi_cmnd *scp)
{
	scsi_dma_unmap(scp);
	ASC_STATS(scp->device->host, done);
	scp->scsi_done(scp);
}

static void AscSetBank(PortAddr iop_base, uchar bank)
{
	uchar val;

	val = AscGetChipControl(iop_base) &
	    (~
	     (CC_SINGLE_STEP | CC_TEST | CC_DIAG | CC_SCSI_RESET |
	      CC_CHIP_RESET));
	if (bank == 1) {
		val |= CC_BANK_ONE;
	} else if (bank == 2) {
		val |= CC_DIAG | CC_BANK_ONE;
	} else {
		val &= ~CC_BANK_ONE;
	}
	AscSetChipControl(iop_base, val);
}

static void AscSetChipIH(PortAddr iop_base, ushort ins_code)
{
	AscSetBank(iop_base, 1);
	AscWriteChipIH(iop_base, ins_code);
	AscSetBank(iop_base, 0);
}

static int AscStartChip(PortAddr iop_base)
{
	AscSetChipControl(iop_base, 0);
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
		return (0);
	}
	return (1);
}

static int AscStopChip(PortAddr iop_base)
{
	uchar cc_val;

	cc_val =
	    AscGetChipControl(iop_base) &
	    (~(CC_SINGLE_STEP | CC_TEST | CC_DIAG));
	AscSetChipControl(iop_base, (uchar)(cc_val | CC_HALT));
	AscSetChipIH(iop_base, INS_HALT);
	AscSetChipIH(iop_base, INS_RFLAG_WTM);
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) == 0) {
		return (0);
	}
	return (1);
}

static int AscIsChipHalted(PortAddr iop_base)
{
	if ((AscGetChipStatus(iop_base) & CSW_HALTED) != 0) {
		if ((AscGetChipControl(iop_base) & CC_HALT) != 0) {
			return (1);
		}
	}
	return (0);
}

static int AscResetChipAndScsiBus(ASC_DVC_VAR *asc_dvc)
{
	PortAddr iop_base;
	int i = 10;

	iop_base = asc_dvc->iop_base;
	while ((AscGetChipStatus(iop_base) & CSW_SCSI_RESET_ACTIVE)
	       && (i-- > 0)) {
		mdelay(100);
	}
	AscStopChip(iop_base);
	AscSetChipControl(iop_base, CC_CHIP_RESET | CC_SCSI_RESET | CC_HALT);
	udelay(60);
	AscSetChipIH(iop_base, INS_RFLAG_WTM);
	AscSetChipIH(iop_base, INS_HALT);
	AscSetChipControl(iop_base, CC_CHIP_RESET | CC_HALT);
	AscSetChipControl(iop_base, CC_HALT);
	mdelay(200);
	AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
	AscSetChipStatus(iop_base, 0);
	return (AscIsChipHalted(iop_base));
}

static int AscFindSignature(PortAddr iop_base)
{
	ushort sig_word;

	ASC_DBG(1, "AscGetChipSignatureByte(0x%x) 0x%x\n",
		 iop_base, AscGetChipSignatureByte(iop_base));
	if (AscGetChipSignatureByte(iop_base) == (uchar)ASC_1000_ID1B) {
		ASC_DBG(1, "AscGetChipSignatureWord(0x%x) 0x%x\n",
			 iop_base, AscGetChipSignatureWord(iop_base));
		sig_word = AscGetChipSignatureWord(iop_base);
		if ((sig_word == (ushort)ASC_1000_ID0W) ||
		    (sig_word == (ushort)ASC_1000_ID0W_FIX)) {
			return (1);
		}
	}
	return (0);
}

static void AscEnableInterrupt(PortAddr iop_base)
{
	ushort cfg;

	cfg = AscGetChipCfgLsw(iop_base);
	AscSetChipCfgLsw(iop_base, cfg | ASC_CFG0_HOST_INT_ON);
}

static void AscDisableInterrupt(PortAddr iop_base)
{
	ushort cfg;

	cfg = AscGetChipCfgLsw(iop_base);
	AscSetChipCfgLsw(iop_base, cfg & (~ASC_CFG0_HOST_INT_ON));
}

static uchar AscReadLramByte(PortAddr iop_base, ushort addr)
{
	unsigned char byte_data;
	unsigned short word_data;

	if (isodd_word(addr)) {
		AscSetChipLramAddr(iop_base, addr - 1);
		word_data = AscGetChipLramData(iop_base);
		byte_data = (word_data >> 8) & 0xFF;
	} else {
		AscSetChipLramAddr(iop_base, addr);
		word_data = AscGetChipLramData(iop_base);
		byte_data = word_data & 0xFF;
	}
	return byte_data;
}

static ushort AscReadLramWord(PortAddr iop_base, ushort addr)
{
	ushort word_data;

	AscSetChipLramAddr(iop_base, addr);
	word_data = AscGetChipLramData(iop_base);
	return (word_data);
}

#if CC_VERY_LONG_SG_LIST
static ASC_DCNT AscReadLramDWord(PortAddr iop_base, ushort addr)
{
	ushort val_low, val_high;
	ASC_DCNT dword_data;

	AscSetChipLramAddr(iop_base, addr);
	val_low = AscGetChipLramData(iop_base);
	val_high = AscGetChipLramData(iop_base);
	dword_data = ((ASC_DCNT) val_high << 16) | (ASC_DCNT) val_low;
	return (dword_data);
}
#endif /* CC_VERY_LONG_SG_LIST */

static void
AscMemWordSetLram(PortAddr iop_base, ushort s_addr, ushort set_wval, int words)
{
	int i;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < words; i++) {
		AscSetChipLramData(iop_base, set_wval);
	}
}

static void AscWriteLramWord(PortAddr iop_base, ushort addr, ushort word_val)
{
	AscSetChipLramAddr(iop_base, addr);
	AscSetChipLramData(iop_base, word_val);
}

static void AscWriteLramByte(PortAddr iop_base, ushort addr, uchar byte_val)
{
	ushort word_data;

	if (isodd_word(addr)) {
		addr--;
		word_data = AscReadLramWord(iop_base, addr);
		word_data &= 0x00FF;
		word_data |= (((ushort)byte_val << 8) & 0xFF00);
	} else {
		word_data = AscReadLramWord(iop_base, addr);
		word_data &= 0xFF00;
		word_data |= ((ushort)byte_val & 0x00FF);
	}
	AscWriteLramWord(iop_base, addr, word_data);
}

/*
 * Copy 2 bytes to LRAM.
 *
 * The source data is assumed to be in little-endian order in memory
 * and is maintained in little-endian order when written to LRAM.
 */
static void
AscMemWordCopyPtrToLram(PortAddr iop_base, ushort s_addr,
			const uchar *s_buffer, int words)
{
	int i;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < 2 * words; i += 2) {
		/*
		 * On a little-endian system the second argument below
		 * produces a little-endian ushort which is written to
		 * LRAM in little-endian order. On a big-endian system
		 * the second argument produces a big-endian ushort which
		 * is "transparently" byte-swapped by outpw() and written
		 * in little-endian order to LRAM.
		 */
		outpw(iop_base + IOP_RAM_DATA,
		      ((ushort)s_buffer[i + 1] << 8) | s_buffer[i]);
	}
}

/*
 * Copy 4 bytes to LRAM.
 *
 * The source data is assumed to be in little-endian order in memory
 * and is maintained in little-endian order when writen to LRAM.
 */
static void
AscMemDWordCopyPtrToLram(PortAddr iop_base,
			 ushort s_addr, uchar *s_buffer, int dwords)
{
	int i;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < 4 * dwords; i += 4) {
		outpw(iop_base + IOP_RAM_DATA, ((ushort)s_buffer[i + 1] << 8) | s_buffer[i]);	/* LSW */
		outpw(iop_base + IOP_RAM_DATA, ((ushort)s_buffer[i + 3] << 8) | s_buffer[i + 2]);	/* MSW */
	}
}

/*
 * Copy 2 bytes from LRAM.
 *
 * The source data is assumed to be in little-endian order in LRAM
 * and is maintained in little-endian order when written to memory.
 */
static void
AscMemWordCopyPtrFromLram(PortAddr iop_base,
			  ushort s_addr, uchar *d_buffer, int words)
{
	int i;
	ushort word;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < 2 * words; i += 2) {
		word = inpw(iop_base + IOP_RAM_DATA);
		d_buffer[i] = word & 0xff;
		d_buffer[i + 1] = (word >> 8) & 0xff;
	}
}

static ASC_DCNT AscMemSumLramWord(PortAddr iop_base, ushort s_addr, int words)
{
	ASC_DCNT sum;
	int i;

	sum = 0L;
	for (i = 0; i < words; i++, s_addr += 2) {
		sum += AscReadLramWord(iop_base, s_addr);
	}
	return (sum);
}

static ushort AscInitLram(ASC_DVC_VAR *asc_dvc)
{
	uchar i;
	ushort s_addr;
	PortAddr iop_base;
	ushort warn_code;

	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	AscMemWordSetLram(iop_base, ASC_QADR_BEG, 0,
			  (ushort)(((int)(asc_dvc->max_total_qng + 2 + 1) *
				    64) >> 1));
	i = ASC_MIN_ACTIVE_QNO;
	s_addr = ASC_QADR_BEG + ASC_QBLK_SIZE;
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_FWD),
			 (uchar)(i + 1));
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_BWD),
			 (uchar)(asc_dvc->max_total_qng));
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_QNO),
			 (uchar)i);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i < asc_dvc->max_total_qng; i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_FWD),
				 (uchar)(i + 1));
		AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_BWD),
				 (uchar)(i - 1));
		AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_QNO),
				 (uchar)i);
	}
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_FWD),
			 (uchar)ASC_QLINK_END);
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_BWD),
			 (uchar)(asc_dvc->max_total_qng - 1));
	AscWriteLramByte(iop_base, (ushort)(s_addr + ASC_SCSIQ_B_QNO),
			 (uchar)asc_dvc->max_total_qng);
	i++;
	s_addr += ASC_QBLK_SIZE;
	for (; i <= (uchar)(asc_dvc->max_total_qng + 3);
	     i++, s_addr += ASC_QBLK_SIZE) {
		AscWriteLramByte(iop_base,
				 (ushort)(s_addr + (ushort)ASC_SCSIQ_B_FWD), i);
		AscWriteLramByte(iop_base,
				 (ushort)(s_addr + (ushort)ASC_SCSIQ_B_BWD), i);
		AscWriteLramByte(iop_base,
				 (ushort)(s_addr + (ushort)ASC_SCSIQ_B_QNO), i);
	}
	return warn_code;
}

static ASC_DCNT
AscLoadMicroCode(PortAddr iop_base, ushort s_addr,
		 const uchar *mcode_buf, ushort mcode_size)
{
	ASC_DCNT chksum;
	ushort mcode_word_size;
	ushort mcode_chksum;

	/* Write the microcode buffer starting at LRAM address 0. */
	mcode_word_size = (ushort)(mcode_size >> 1);
	AscMemWordSetLram(iop_base, s_addr, 0, mcode_word_size);
	AscMemWordCopyPtrToLram(iop_base, s_addr, mcode_buf, mcode_word_size);

	chksum = AscMemSumLramWord(iop_base, s_addr, mcode_word_size);
	ASC_DBG(1, "chksum 0x%lx\n", (ulong)chksum);
	mcode_chksum = (ushort)AscMemSumLramWord(iop_base,
						 (ushort)ASC_CODE_SEC_BEG,
						 (ushort)((mcode_size -
							   s_addr - (ushort)
							   ASC_CODE_SEC_BEG) /
							  2));
	ASC_DBG(1, "mcode_chksum 0x%lx\n", (ulong)mcode_chksum);
	AscWriteLramWord(iop_base, ASCV_MCODE_CHKSUM_W, mcode_chksum);
	AscWriteLramWord(iop_base, ASCV_MCODE_SIZE_W, mcode_size);
	return chksum;
}

static void AscInitQLinkVar(ASC_DVC_VAR *asc_dvc)
{
	PortAddr iop_base;
	int i;
	ushort lram_addr;

	iop_base = asc_dvc->iop_base;
	AscPutRiscVarFreeQHead(iop_base, 1);
	AscPutRiscVarDoneQTail(iop_base, asc_dvc->max_total_qng);
	AscPutVarFreeQHead(iop_base, 1);
	AscPutVarDoneQTail(iop_base, asc_dvc->max_total_qng);
	AscWriteLramByte(iop_base, ASCV_BUSY_QHEAD_B,
			 (uchar)((int)asc_dvc->max_total_qng + 1));
	AscWriteLramByte(iop_base, ASCV_DISC1_QHEAD_B,
			 (uchar)((int)asc_dvc->max_total_qng + 2));
	AscWriteLramByte(iop_base, (ushort)ASCV_TOTAL_READY_Q_B,
			 asc_dvc->max_total_qng);
	AscWriteLramWord(iop_base, ASCV_ASCDVC_ERR_CODE_W, 0);
	AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, 0);
	AscWriteLramByte(iop_base, ASCV_SCSIBUSY_B, 0);
	AscWriteLramByte(iop_base, ASCV_WTM_FLAG_B, 0);
	AscPutQDoneInProgress(iop_base, 0);
	lram_addr = ASC_QADR_BEG;
	for (i = 0; i < 32; i++, lram_addr += 2) {
		AscWriteLramWord(iop_base, lram_addr, 0);
	}
}

static ushort AscInitMicroCodeVar(ASC_DVC_VAR *asc_dvc)
{
	int i;
	ushort warn_code;
	PortAddr iop_base;
	ASC_PADDR phy_addr;
	ASC_DCNT phy_size;
	struct asc_board *board = asc_dvc_to_board(asc_dvc);

	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	for (i = 0; i <= ASC_MAX_TID; i++) {
		AscPutMCodeInitSDTRAtID(iop_base, i,
					asc_dvc->cfg->sdtr_period_offset[i]);
	}

	AscInitQLinkVar(asc_dvc);
	AscWriteLramByte(iop_base, ASCV_DISC_ENABLE_B,
			 asc_dvc->cfg->disc_enable);
	AscWriteLramByte(iop_base, ASCV_HOSTSCSI_ID_B,
			 ASC_TID_TO_TARGET_ID(asc_dvc->cfg->chip_scsi_id));

	/* Ensure overrun buffer is aligned on an 8 byte boundary. */
	BUG_ON((unsigned long)asc_dvc->overrun_buf & 7);
	asc_dvc->overrun_dma = dma_map_single(board->dev, asc_dvc->overrun_buf,
					ASC_OVERRUN_BSIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(board->dev, asc_dvc->overrun_dma)) {
		warn_code = -ENOMEM;
		goto err_dma_map;
	}
	phy_addr = cpu_to_le32(asc_dvc->overrun_dma);
	AscMemDWordCopyPtrToLram(iop_base, ASCV_OVERRUN_PADDR_D,
				 (uchar *)&phy_addr, 1);
	phy_size = cpu_to_le32(ASC_OVERRUN_BSIZE);
	AscMemDWordCopyPtrToLram(iop_base, ASCV_OVERRUN_BSIZE_D,
				 (uchar *)&phy_size, 1);

	asc_dvc->cfg->mcode_date =
	    AscReadLramWord(iop_base, (ushort)ASCV_MC_DATE_W);
	asc_dvc->cfg->mcode_version =
	    AscReadLramWord(iop_base, (ushort)ASCV_MC_VER_W);

	AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
	if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
		asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
		warn_code = UW_ERR;
		goto err_mcode_start;
	}
	if (AscStartChip(iop_base) != 1) {
		asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
		warn_code = UW_ERR;
		goto err_mcode_start;
	}

	return warn_code;

err_mcode_start:
	dma_unmap_single(board->dev, asc_dvc->overrun_dma,
			 ASC_OVERRUN_BSIZE, DMA_FROM_DEVICE);
err_dma_map:
	asc_dvc->overrun_dma = 0;
	return warn_code;
}

static ushort AscInitAsc1000Driver(ASC_DVC_VAR *asc_dvc)
{
	const struct firmware *fw;
	const char fwname[] = "advansys/mcode.bin";
	int err;
	unsigned long chksum;
	ushort warn_code;
	PortAddr iop_base;

	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	if ((asc_dvc->dvc_cntl & ASC_CNTL_RESET_SCSI) &&
	    !(asc_dvc->init_state & ASC_INIT_RESET_SCSI_DONE)) {
		AscResetChipAndScsiBus(asc_dvc);
		mdelay(asc_dvc->scsi_reset_wait * 1000); /* XXX: msleep? */
	}
	asc_dvc->init_state |= ASC_INIT_STATE_BEG_LOAD_MC;
	if (asc_dvc->err_code != 0)
		return UW_ERR;
	if (!AscFindSignature(asc_dvc->iop_base)) {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
		return warn_code;
	}
	AscDisableInterrupt(iop_base);
	warn_code |= AscInitLram(asc_dvc);
	if (asc_dvc->err_code != 0)
		return UW_ERR;

	err = request_firmware(&fw, fwname, asc_dvc->drv_ptr->dev);
	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fwname, err);
		asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
		return err;
	}
	if (fw->size < 4) {
		printk(KERN_ERR "Bogus length %zu in image \"%s\"\n",
		       fw->size, fwname);
		release_firmware(fw);
		asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
		return -EINVAL;
	}
	chksum = (fw->data[3] << 24) | (fw->data[2] << 16) |
		 (fw->data[1] << 8) | fw->data[0];
	ASC_DBG(1, "_asc_mcode_chksum 0x%lx\n", (ulong)chksum);
	if (AscLoadMicroCode(iop_base, 0, &fw->data[4],
			     fw->size - 4) != chksum) {
		asc_dvc->err_code |= ASC_IERR_MCODE_CHKSUM;
		release_firmware(fw);
		return warn_code;
	}
	release_firmware(fw);
	warn_code |= AscInitMicroCodeVar(asc_dvc);
	if (!asc_dvc->overrun_dma)
		return warn_code;
	asc_dvc->init_state |= ASC_INIT_STATE_END_LOAD_MC;
	AscEnableInterrupt(iop_base);
	return warn_code;
}

/*
 * Load the Microcode
 *
 * Write the microcode image to RISC memory starting at address 0.
 *
 * The microcode is stored compressed in the following format:
 *
 *  254 word (508 byte) table indexed by byte code followed
 *  by the following byte codes:
 *
 *    1-Byte Code:
 *      00: Emit word 0 in table.
 *      01: Emit word 1 in table.
 *      .
 *      FD: Emit word 253 in table.
 *
 *    Multi-Byte Code:
 *      FE WW WW: (3 byte code) Word to emit is the next word WW WW.
 *      FF BB WW WW: (4 byte code) Emit BB count times next word WW WW.
 *
 * Returns 0 or an error if the checksum doesn't match
 */
static int AdvLoadMicrocode(AdvPortAddr iop_base, const unsigned char *buf,
			    int size, int memsize, int chksum)
{
	int i, j, end, len = 0;
	ADV_DCNT sum;

	AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

	for (i = 253 * 2; i < size; i++) {
		if (buf[i] == 0xff) {
			unsigned short word = (buf[i + 3] << 8) | buf[i + 2];
			for (j = 0; j < buf[i + 1]; j++) {
				AdvWriteWordAutoIncLram(iop_base, word);
				len += 2;
			}
			i += 3;
		} else if (buf[i] == 0xfe) {
			unsigned short word = (buf[i + 2] << 8) | buf[i + 1];
			AdvWriteWordAutoIncLram(iop_base, word);
			i += 2;
			len += 2;
		} else {
			unsigned int off = buf[i] * 2;
			unsigned short word = (buf[off + 1] << 8) | buf[off];
			AdvWriteWordAutoIncLram(iop_base, word);
			len += 2;
		}
	}

	end = len;

	while (len < memsize) {
		AdvWriteWordAutoIncLram(iop_base, 0);
		len += 2;
	}

	/* Verify the microcode checksum. */
	sum = 0;
	AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, 0);

	for (len = 0; len < end; len += 2) {
		sum += AdvReadWordAutoIncLram(iop_base);
	}

	if (sum != chksum)
		return ASC_IERR_MCODE_CHKSUM;

	return 0;
}

static void AdvBuildCarrierFreelist(struct adv_dvc_var *asc_dvc)
{
	ADV_CARR_T *carrp;
	ADV_SDCNT buf_size;
	ADV_PADDR carr_paddr;

	carrp = (ADV_CARR_T *) ADV_16BALIGN(asc_dvc->carrier_buf);
	asc_dvc->carr_freelist = NULL;
	if (carrp == asc_dvc->carrier_buf) {
		buf_size = ADV_CARRIER_BUFSIZE;
	} else {
		buf_size = ADV_CARRIER_BUFSIZE - sizeof(ADV_CARR_T);
	}

	do {
		/* Get physical address of the carrier 'carrp'. */
		carr_paddr = cpu_to_le32(virt_to_bus(carrp));

		buf_size -= sizeof(ADV_CARR_T);

		carrp->carr_pa = carr_paddr;
		carrp->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(carrp));

		/*
		 * Insert the carrier at the beginning of the freelist.
		 */
		carrp->next_vpa =
			cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
		asc_dvc->carr_freelist = carrp;

		carrp++;
	} while (buf_size > 0);
}

/*
 * Send an idle command to the chip and wait for completion.
 *
 * Command completion is polled for once per microsecond.
 *
 * The function can be called from anywhere including an interrupt handler.
 * But the function is not re-entrant, so it uses the DvcEnter/LeaveCritical()
 * functions to prevent reentrancy.
 *
 * Return Values:
 *   ADV_TRUE - command completed successfully
 *   ADV_FALSE - command failed
 *   ADV_ERROR - command timed out
 */
static int
AdvSendIdleCmd(ADV_DVC_VAR *asc_dvc,
	       ushort idle_cmd, ADV_DCNT idle_cmd_parameter)
{
	int result;
	ADV_DCNT i, j;
	AdvPortAddr iop_base;

	iop_base = asc_dvc->iop_base;

	/*
	 * Clear the idle command status which is set by the microcode
	 * to a non-zero value to indicate when the command is completed.
	 * The non-zero result is one of the IDLE_CMD_STATUS_* values
	 */
	AdvWriteWordLram(iop_base, ASC_MC_IDLE_CMD_STATUS, (ushort)0);

	/*
	 * Write the idle command value after the idle command parameter
	 * has been written to avoid a race condition. If the order is not
	 * followed, the microcode may process the idle command before the
	 * parameters have been written to LRAM.
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IDLE_CMD_PARAMETER,
				cpu_to_le32(idle_cmd_parameter));
	AdvWriteWordLram(iop_base, ASC_MC_IDLE_CMD, idle_cmd);

	/*
	 * Tickle the RISC to tell it to process the idle command.
	 */
	AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_B);
	if (asc_dvc->chip_type == ADV_CHIP_ASC3550) {
		/*
		 * Clear the tickle value. In the ASC-3550 the RISC flag
		 * command 'clr_tickle_b' does not work unless the host
		 * value is cleared.
		 */
		AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_NOP);
	}

	/* Wait for up to 100 millisecond for the idle command to timeout. */
	for (i = 0; i < SCSI_WAIT_100_MSEC; i++) {
		/* Poll once each microsecond for command completion. */
		for (j = 0; j < SCSI_US_PER_MSEC; j++) {
			AdvReadWordLram(iop_base, ASC_MC_IDLE_CMD_STATUS,
					result);
			if (result != 0)
				return result;
			udelay(1);
		}
	}

	BUG();		/* The idle command should never timeout. */
	return ADV_ERROR;
}

/*
 * Reset SCSI Bus and purge all outstanding requests.
 *
 * Return Value:
 *      ADV_TRUE(1) -   All requests are purged and SCSI Bus is reset.
 *      ADV_FALSE(0) -  Microcode command failed.
 *      ADV_ERROR(-1) - Microcode command timed-out. Microcode or IC
 *                      may be hung which requires driver recovery.
 */
static int AdvResetSB(ADV_DVC_VAR *asc_dvc)
{
	int status;

	/*
	 * Send the SCSI Bus Reset idle start idle command which asserts
	 * the SCSI Bus Reset signal.
	 */
	status = AdvSendIdleCmd(asc_dvc, (ushort)IDLE_CMD_SCSI_RESET_START, 0L);
	if (status != ADV_TRUE) {
		return status;
	}

	/*
	 * Delay for the specified SCSI Bus Reset hold time.
	 *
	 * The hold time delay is done on the host because the RISC has no
	 * microsecond accurate timer.
	 */
	udelay(ASC_SCSI_RESET_HOLD_TIME_US);

	/*
	 * Send the SCSI Bus Reset end idle command which de-asserts
	 * the SCSI Bus Reset signal and purges any pending requests.
	 */
	status = AdvSendIdleCmd(asc_dvc, (ushort)IDLE_CMD_SCSI_RESET_END, 0L);
	if (status != ADV_TRUE) {
		return status;
	}

	mdelay(asc_dvc->scsi_reset_wait * 1000);	/* XXX: msleep? */

	return status;
}

/*
 * Initialize the ASC-3550.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
static int AdvInitAsc3550Driver(ADV_DVC_VAR *asc_dvc)
{
	const struct firmware *fw;
	const char fwname[] = "advansys/3550.bin";
	AdvPortAddr iop_base;
	ushort warn_code;
	int begin_addr;
	int end_addr;
	ushort code_sum;
	int word;
	int i;
	int err;
	unsigned long chksum;
	ushort scsi_cfg1;
	uchar tid;
	ushort bios_mem[ASC_MC_BIOSLEN / 2];	/* BIOS RISC Memory 0x40-0x8F. */
	ushort wdtr_able = 0, sdtr_able, tagqng_able;
	uchar max_cmd[ADV_MAX_TID + 1];

	/* If there is already an error, don't continue. */
	if (asc_dvc->err_code != 0)
		return ADV_ERROR;

	/*
	 * The caller must set 'chip_type' to ADV_CHIP_ASC3550.
	 */
	if (asc_dvc->chip_type != ADV_CHIP_ASC3550) {
		asc_dvc->err_code = ASC_IERR_BAD_CHIPTYPE;
		return ADV_ERROR;
	}

	warn_code = 0;
	iop_base = asc_dvc->iop_base;

	/*
	 * Save the RISC memory BIOS region before writing the microcode.
	 * The BIOS may already be loaded and using its RISC LRAM region
	 * so its region must be saved and restored.
	 *
	 * Note: This code makes the assumption, which is currently true,
	 * that a chip reset does not clear RISC LRAM.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				bios_mem[i]);
	}

	/*
	 * Save current per TID negotiated values.
	 */
	if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM) / 2] == 0x55AA) {
		ushort bios_version, major, minor;

		bios_version =
		    bios_mem[(ASC_MC_BIOS_VERSION - ASC_MC_BIOSMEM) / 2];
		major = (bios_version >> 12) & 0xF;
		minor = (bios_version >> 8) & 0xF;
		if (major < 3 || (major == 3 && minor == 1)) {
			/* BIOS 3.1 and earlier location of 'wdtr_able' variable. */
			AdvReadWordLram(iop_base, 0x120, wdtr_able);
		} else {
			AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
		}
	}
	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
				max_cmd[tid]);
	}

	err = request_firmware(&fw, fwname, asc_dvc->drv_ptr->dev);
	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fwname, err);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return err;
	}
	if (fw->size < 4) {
		printk(KERN_ERR "Bogus length %zu in image \"%s\"\n",
		       fw->size, fwname);
		release_firmware(fw);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return -EINVAL;
	}
	chksum = (fw->data[3] << 24) | (fw->data[2] << 16) |
		 (fw->data[1] << 8) | fw->data[0];
	asc_dvc->err_code = AdvLoadMicrocode(iop_base, &fw->data[4],
					     fw->size - 4, ADV_3550_MEMSIZE,
					     chksum);
	release_firmware(fw);
	if (asc_dvc->err_code)
		return ADV_ERROR;

	/*
	 * Restore the RISC memory BIOS region.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				 bios_mem[i]);
	}

	/*
	 * Calculate and write the microcode code checksum to the microcode
	 * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
	 */
	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
	AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
	code_sum = 0;
	AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
	for (word = begin_addr; word < end_addr; word += 2) {
		code_sum += AdvReadWordAutoIncLram(iop_base);
	}
	AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

	/*
	 * Read and save microcode version and date.
	 */
	AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE,
			asc_dvc->cfg->mcode_date);
	AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM,
			asc_dvc->cfg->mcode_version);

	/*
	 * Set the chip type to indicate the ASC3550.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC3550);

	/*
	 * If the PCI Configuration Command Register "Parity Error Response
	 * Control" Bit was clear (0), then set the microcode variable
	 * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
	 * to ignore DMA parity errors.
	 */
	if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR) {
		AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
		word |= CONTROL_FLAG_IGNORE_PERR;
		AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
	}

	/*
	 * For ASC-3550, setting the START_CTL_EMFU [3:2] bits sets a FIFO
	 * threshold of 128 bytes. This register is only accessible to the host.
	 */
	AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
			     START_CTL_EMFU | READ_CMD_MRM);

	/*
	 * Microcode operating variables for WDTR, SDTR, and command tag
	 * queuing will be set in slave_configure() based on what a
	 * device reports it is capable of in Inquiry byte 7.
	 *
	 * If SCSI Bus Resets have been disabled, then directly set
	 * SDTR and WDTR from the EEPROM configuration. This will allow
	 * the BIOS and warm boot to work without a SCSI bus hang on
	 * the Inquiry caused by host and target mismatched DTR values.
	 * Without the SCSI Bus Reset, before an Inquiry a device can't
	 * be assumed to be in Asynchronous, Narrow mode.
	 */
	if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0) {
		AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE,
				 asc_dvc->wdtr_able);
		AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE,
				 asc_dvc->sdtr_able);
	}

	/*
	 * Set microcode operating variables for SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 based on the ULTRA EEPROM per TID
	 * bitmask. These values determine the maximum SDTR speed negotiated
	 * with a device.
	 *
	 * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
	 * without determining here whether the device supports SDTR.
	 *
	 * 4-bit speed  SDTR speed name
	 * ===========  ===============
	 * 0000b (0x0)  SDTR disabled
	 * 0001b (0x1)  5 Mhz
	 * 0010b (0x2)  10 Mhz
	 * 0011b (0x3)  20 Mhz (Ultra)
	 * 0100b (0x4)  40 Mhz (LVD/Ultra2)
	 * 0101b (0x5)  80 Mhz (LVD2/Ultra3)
	 * 0110b (0x6)  Undefined
	 * .
	 * 1111b (0xF)  Undefined
	 */
	word = 0;
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		if (ADV_TID_TO_TIDMASK(tid) & asc_dvc->ultra_able) {
			/* Set Ultra speed for TID 'tid'. */
			word |= (0x3 << (4 * (tid % 4)));
		} else {
			/* Set Fast speed for TID 'tid'. */
			word |= (0x2 << (4 * (tid % 4)));
		}
		if (tid == 3) {	/* Check if done with sdtr_speed1. */
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, word);
			word = 0;
		} else if (tid == 7) {	/* Check if done with sdtr_speed2. */
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, word);
			word = 0;
		} else if (tid == 11) {	/* Check if done with sdtr_speed3. */
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, word);
			word = 0;
		} else if (tid == 15) {	/* Check if done with sdtr_speed4. */
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, word);
			/* End of loop. */
		}
	}

	/*
	 * Set microcode operating variable for the disconnect per TID bitmask.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE,
			 asc_dvc->cfg->disc_enable);

	/*
	 * Set SCSI_CFG0 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG0 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
			 PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
			 asc_dvc->chip_scsi_id);

	/*
	 * Determine SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */

	/* Read current SCSI_CFG1 Register value. */
	scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

	/*
	 * If all three connectors are in use, return an error.
	 */
	if ((scsi_cfg1 & CABLE_ILLEGAL_A) == 0 ||
	    (scsi_cfg1 & CABLE_ILLEGAL_B) == 0) {
		asc_dvc->err_code |= ASC_IERR_ILLEGAL_CONNECTION;
		return ADV_ERROR;
	}

	/*
	 * If the internal narrow cable is reversed all of the SCSI_CTRL
	 * register signals will be set. Check for and return an error if
	 * this condition is found.
	 */
	if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07) {
		asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
		return ADV_ERROR;
	}

	/*
	 * If this is a differential board and a single-ended device
	 * is attached to one of the connectors, return an error.
	 */
	if ((scsi_cfg1 & DIFF_MODE) && (scsi_cfg1 & DIFF_SENSE) == 0) {
		asc_dvc->err_code |= ASC_IERR_SINGLE_END_DEVICE;
		return ADV_ERROR;
	}

	/*
	 * If automatic termination control is enabled, then set the
	 * termination value based on a table listed in a_condor.h.
	 *
	 * If manual termination was specified with an EEPROM setting
	 * then 'termination' was set-up in AdvInitFrom3550EEPROM() and
	 * is ready to be 'ored' into SCSI_CFG1.
	 */
	if (asc_dvc->cfg->termination == 0) {
		/*
		 * The software always controls termination by setting TERM_CTL_SEL.
		 * If TERM_CTL_SEL were set to 0, the hardware would set termination.
		 */
		asc_dvc->cfg->termination |= TERM_CTL_SEL;

		switch (scsi_cfg1 & CABLE_DETECT) {
			/* TERM_CTL_H: on, TERM_CTL_L: on */
		case 0x3:
		case 0x7:
		case 0xB:
		case 0xD:
		case 0xE:
		case 0xF:
			asc_dvc->cfg->termination |= (TERM_CTL_H | TERM_CTL_L);
			break;

			/* TERM_CTL_H: on, TERM_CTL_L: off */
		case 0x1:
		case 0x5:
		case 0x9:
		case 0xA:
		case 0xC:
			asc_dvc->cfg->termination |= TERM_CTL_H;
			break;

			/* TERM_CTL_H: off, TERM_CTL_L: off */
		case 0x2:
		case 0x6:
			break;
		}
	}

	/*
	 * Clear any set TERM_CTL_H and TERM_CTL_L bits.
	 */
	scsi_cfg1 &= ~TERM_CTL;

	/*
	 * Invert the TERM_CTL_H and TERM_CTL_L bits and then
	 * set 'scsi_cfg1'. The TERM_POL bit does not need to be
	 * referenced, because the hardware internally inverts
	 * the Termination High and Low bits if TERM_POL is set.
	 */
	scsi_cfg1 |= (TERM_CTL_SEL | (~asc_dvc->cfg->termination & TERM_CTL));

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set filter value and possibly modified termination control
	 * bits in the Microcode SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1,
			 FLTR_DISABLE | scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-3550 has 8KB internal memory.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
			 BIOS_EN | RAM_SZ_8KB);

	/*
	 * Set SEL_MASK Microcode Default Value
	 *
	 * The microcode will set the SEL_MASK register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
			 ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

	AdvBuildCarrierFreelist(asc_dvc);

	/*
	 * Set-up the Host->RISC Initiator Command Queue (ICQ).
	 */

	if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

	/*
	 * The first command issued will be placed in the stopper carrier.
	 */
	asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC ICQ physical address start value.
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);

	/*
	 * Set-up the RISC->Host Initiator Response Queue (IRQ).
	 */
	if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

	/*
	 * The first command completed by the RISC will be placed in
	 * the stopper.
	 *
	 * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
	 * completed the RISC will set the ASC_RQ_STOPPER bit.
	 */
	asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC IRQ physical address start value.
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
	asc_dvc->carr_pending_cnt = 0;

	AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
			     (ADV_INTR_ENABLE_HOST_INTR |
			      ADV_INTR_ENABLE_GLOBAL_INTR));

	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
	AdvWriteWordRegister(iop_base, IOPW_PC, word);

	/* finally, finally, gentlemen, start your engine */
	AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

	/*
	 * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
	 * Resets should be performed. The RISC has to be running
	 * to issue a SCSI Bus Reset.
	 */
	if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) {
		/*
		 * If the BIOS Signature is present in memory, restore the
		 * BIOS Handshake Configuration Table and do not perform
		 * a SCSI Bus Reset.
		 */
		if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM) / 2] ==
		    0x55AA) {
			/*
			 * Restore per TID negotiated values.
			 */
			AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE,
					 tagqng_able);
			for (tid = 0; tid <= ADV_MAX_TID; tid++) {
				AdvWriteByteLram(iop_base,
						 ASC_MC_NUMBER_OF_MAX_CMD + tid,
						 max_cmd[tid]);
			}
		} else {
			if (AdvResetSB(asc_dvc) != ADV_TRUE) {
				warn_code = ASC_WARN_BUSRESET_ERROR;
			}
		}
	}

	return warn_code;
}

/*
 * Initialize the ASC-38C0800.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
static int AdvInitAsc38C0800Driver(ADV_DVC_VAR *asc_dvc)
{
	const struct firmware *fw;
	const char fwname[] = "advansys/38C0800.bin";
	AdvPortAddr iop_base;
	ushort warn_code;
	int begin_addr;
	int end_addr;
	ushort code_sum;
	int word;
	int i;
	int err;
	unsigned long chksum;
	ushort scsi_cfg1;
	uchar byte;
	uchar tid;
	ushort bios_mem[ASC_MC_BIOSLEN / 2];	/* BIOS RISC Memory 0x40-0x8F. */
	ushort wdtr_able, sdtr_able, tagqng_able;
	uchar max_cmd[ADV_MAX_TID + 1];

	/* If there is already an error, don't continue. */
	if (asc_dvc->err_code != 0)
		return ADV_ERROR;

	/*
	 * The caller must set 'chip_type' to ADV_CHIP_ASC38C0800.
	 */
	if (asc_dvc->chip_type != ADV_CHIP_ASC38C0800) {
		asc_dvc->err_code = ASC_IERR_BAD_CHIPTYPE;
		return ADV_ERROR;
	}

	warn_code = 0;
	iop_base = asc_dvc->iop_base;

	/*
	 * Save the RISC memory BIOS region before writing the microcode.
	 * The BIOS may already be loaded and using its RISC LRAM region
	 * so its region must be saved and restored.
	 *
	 * Note: This code makes the assumption, which is currently true,
	 * that a chip reset does not clear RISC LRAM.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				bios_mem[i]);
	}

	/*
	 * Save current per TID negotiated values.
	 */
	AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
				max_cmd[tid]);
	}

	/*
	 * RAM BIST (RAM Built-In Self Test)
	 *
	 * Address : I/O base + offset 0x38h register (byte).
	 * Function: Bit 7-6(RW) : RAM mode
	 *                          Normal Mode   : 0x00
	 *                          Pre-test Mode : 0x40
	 *                          RAM Test Mode : 0x80
	 *           Bit 5       : unused
	 *           Bit 4(RO)   : Done bit
	 *           Bit 3-0(RO) : Status
	 *                          Host Error    : 0x08
	 *                          Int_RAM Error : 0x04
	 *                          RISC Error    : 0x02
	 *                          SCSI Error    : 0x01
	 *                          No Error      : 0x00
	 *
	 * Note: RAM BIST code should be put right here, before loading the
	 * microcode and after saving the RISC memory BIOS region.
	 */

	/*
	 * LRAM Pre-test
	 *
	 * Write PRE_TEST_MODE (0x40) to register and wait for 10 milliseconds.
	 * If Done bit not set or low nibble not PRE_TEST_VALUE (0x05), return
	 * an error. Reset to NORMAL_MODE (0x00) and do again. If cannot reset
	 * to NORMAL_MODE, return an error too.
	 */
	for (i = 0; i < 2; i++) {
		AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, PRE_TEST_MODE);
		mdelay(10);	/* Wait for 10ms before reading back. */
		byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
		if ((byte & RAM_TEST_DONE) == 0
		    || (byte & 0x0F) != PRE_TEST_VALUE) {
			asc_dvc->err_code = ASC_IERR_BIST_PRE_TEST;
			return ADV_ERROR;
		}

		AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);
		mdelay(10);	/* Wait for 10ms before reading back. */
		if (AdvReadByteRegister(iop_base, IOPB_RAM_BIST)
		    != NORMAL_VALUE) {
			asc_dvc->err_code = ASC_IERR_BIST_PRE_TEST;
			return ADV_ERROR;
		}
	}

	/*
	 * LRAM Test - It takes about 1.5 ms to run through the test.
	 *
	 * Write RAM_TEST_MODE (0x80) to register and wait for 10 milliseconds.
	 * If Done bit not set or Status not 0, save register byte, set the
	 * err_code, and return an error.
	 */
	AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, RAM_TEST_MODE);
	mdelay(10);	/* Wait for 10ms before checking status. */

	byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
	if ((byte & RAM_TEST_DONE) == 0 || (byte & RAM_TEST_STATUS) != 0) {
		/* Get here if Done bit not set or Status not 0. */
		asc_dvc->bist_err_code = byte;	/* for BIOS display message */
		asc_dvc->err_code = ASC_IERR_BIST_RAM_TEST;
		return ADV_ERROR;
	}

	/* We need to reset back to normal mode after LRAM test passes. */
	AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);

	err = request_firmware(&fw, fwname, asc_dvc->drv_ptr->dev);
	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fwname, err);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return err;
	}
	if (fw->size < 4) {
		printk(KERN_ERR "Bogus length %zu in image \"%s\"\n",
		       fw->size, fwname);
		release_firmware(fw);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return -EINVAL;
	}
	chksum = (fw->data[3] << 24) | (fw->data[2] << 16) |
		 (fw->data[1] << 8) | fw->data[0];
	asc_dvc->err_code = AdvLoadMicrocode(iop_base, &fw->data[4],
					     fw->size - 4, ADV_38C0800_MEMSIZE,
					     chksum);
	release_firmware(fw);
	if (asc_dvc->err_code)
		return ADV_ERROR;

	/*
	 * Restore the RISC memory BIOS region.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				 bios_mem[i]);
	}

	/*
	 * Calculate and write the microcode code checksum to the microcode
	 * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
	 */
	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
	AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
	code_sum = 0;
	AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
	for (word = begin_addr; word < end_addr; word += 2) {
		code_sum += AdvReadWordAutoIncLram(iop_base);
	}
	AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

	/*
	 * Read microcode version and date.
	 */
	AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE,
			asc_dvc->cfg->mcode_date);
	AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM,
			asc_dvc->cfg->mcode_version);

	/*
	 * Set the chip type to indicate the ASC38C0800.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC38C0800);

	/*
	 * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
	 * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
	 * cable detection and then we are able to read C_DET[3:0].
	 *
	 * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
	 * Microcode Default Value' section below.
	 */
	scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);
	AdvWriteWordRegister(iop_base, IOPW_SCSI_CFG1,
			     scsi_cfg1 | DIS_TERM_DRV);

	/*
	 * If the PCI Configuration Command Register "Parity Error Response
	 * Control" Bit was clear (0), then set the microcode variable
	 * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
	 * to ignore DMA parity errors.
	 */
	if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR) {
		AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
		word |= CONTROL_FLAG_IGNORE_PERR;
		AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
	}

	/*
	 * For ASC-38C0800, set FIFO_THRESH_80B [6:4] bits and START_CTL_TH [3:2]
	 * bits for the default FIFO threshold.
	 *
	 * Note: ASC-38C0800 FIFO threshold has been changed to 256 bytes.
	 *
	 * For DMA Errata #4 set the BC_THRESH_ENB bit.
	 */
	AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
			     BC_THRESH_ENB | FIFO_THRESH_80B | START_CTL_TH |
			     READ_CMD_MRM);

	/*
	 * Microcode operating variables for WDTR, SDTR, and command tag
	 * queuing will be set in slave_configure() based on what a
	 * device reports it is capable of in Inquiry byte 7.
	 *
	 * If SCSI Bus Resets have been disabled, then directly set
	 * SDTR and WDTR from the EEPROM configuration. This will allow
	 * the BIOS and warm boot to work without a SCSI bus hang on
	 * the Inquiry caused by host and target mismatched DTR values.
	 * Without the SCSI Bus Reset, before an Inquiry a device can't
	 * be assumed to be in Asynchronous, Narrow mode.
	 */
	if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0) {
		AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE,
				 asc_dvc->wdtr_able);
		AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE,
				 asc_dvc->sdtr_able);
	}

	/*
	 * Set microcode operating variables for DISC and SDTR_SPEED1,
	 * SDTR_SPEED2, SDTR_SPEED3, and SDTR_SPEED4 based on the EEPROM
	 * configuration values.
	 *
	 * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
	 * without determining here whether the device supports SDTR.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE,
			 asc_dvc->cfg->disc_enable);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, asc_dvc->sdtr_speed1);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, asc_dvc->sdtr_speed2);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, asc_dvc->sdtr_speed3);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, asc_dvc->sdtr_speed4);

	/*
	 * Set SCSI_CFG0 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG0 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
			 PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
			 asc_dvc->chip_scsi_id);

	/*
	 * Determine SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */

	/* Read current SCSI_CFG1 Register value. */
	scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

	/*
	 * If the internal narrow cable is reversed all of the SCSI_CTRL
	 * register signals will be set. Check for and return an error if
	 * this condition is found.
	 */
	if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07) {
		asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
		return ADV_ERROR;
	}

	/*
	 * All kind of combinations of devices attached to one of four
	 * connectors are acceptable except HVD device attached. For example,
	 * LVD device can be attached to SE connector while SE device attached
	 * to LVD connector.  If LVD device attached to SE connector, it only
	 * runs up to Ultra speed.
	 *
	 * If an HVD device is attached to one of LVD connectors, return an
	 * error.  However, there is no way to detect HVD device attached to
	 * SE connectors.
	 */
	if (scsi_cfg1 & HVD) {
		asc_dvc->err_code = ASC_IERR_HVD_DEVICE;
		return ADV_ERROR;
	}

	/*
	 * If either SE or LVD automatic termination control is enabled, then
	 * set the termination value based on a table listed in a_condor.h.
	 *
	 * If manual termination was specified with an EEPROM setting then
	 * 'termination' was set-up in AdvInitFrom38C0800EEPROM() and is ready
	 * to be 'ored' into SCSI_CFG1.
	 */
	if ((asc_dvc->cfg->termination & TERM_SE) == 0) {
		/* SE automatic termination control is enabled. */
		switch (scsi_cfg1 & C_DET_SE) {
			/* TERM_SE_HI: on, TERM_SE_LO: on */
		case 0x1:
		case 0x2:
		case 0x3:
			asc_dvc->cfg->termination |= TERM_SE;
			break;

			/* TERM_SE_HI: on, TERM_SE_LO: off */
		case 0x0:
			asc_dvc->cfg->termination |= TERM_SE_HI;
			break;
		}
	}

	if ((asc_dvc->cfg->termination & TERM_LVD) == 0) {
		/* LVD automatic termination control is enabled. */
		switch (scsi_cfg1 & C_DET_LVD) {
			/* TERM_LVD_HI: on, TERM_LVD_LO: on */
		case 0x4:
		case 0x8:
		case 0xC:
			asc_dvc->cfg->termination |= TERM_LVD;
			break;

			/* TERM_LVD_HI: off, TERM_LVD_LO: off */
		case 0x0:
			break;
		}
	}

	/*
	 * Clear any set TERM_SE and TERM_LVD bits.
	 */
	scsi_cfg1 &= (~TERM_SE & ~TERM_LVD);

	/*
	 * Invert the TERM_SE and TERM_LVD bits and then set 'scsi_cfg1'.
	 */
	scsi_cfg1 |= (~asc_dvc->cfg->termination & 0xF0);

	/*
	 * Clear BIG_ENDIAN, DIS_TERM_DRV, Terminator Polarity and HVD/LVD/SE
	 * bits and set possibly modified termination control bits in the
	 * Microcode SCSI_CFG1 Register Value.
	 */
	scsi_cfg1 &= (~BIG_ENDIAN & ~DIS_TERM_DRV & ~TERM_POL & ~HVD_LVD_SE);

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set possibly modified termination control and reset DIS_TERM_DRV
	 * bits in the Microcode SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-38C0800 has 16KB internal memory.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
			 BIOS_EN | RAM_SZ_16KB);

	/*
	 * Set SEL_MASK Microcode Default Value
	 *
	 * The microcode will set the SEL_MASK register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
			 ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

	AdvBuildCarrierFreelist(asc_dvc);

	/*
	 * Set-up the Host->RISC Initiator Command Queue (ICQ).
	 */

	if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

	/*
	 * The first command issued will be placed in the stopper carrier.
	 */
	asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC ICQ physical address start value.
	 * carr_pa is LE, must be native before write
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);

	/*
	 * Set-up the RISC->Host Initiator Response Queue (IRQ).
	 */
	if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

	/*
	 * The first command completed by the RISC will be placed in
	 * the stopper.
	 *
	 * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
	 * completed the RISC will set the ASC_RQ_STOPPER bit.
	 */
	asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC IRQ physical address start value.
	 *
	 * carr_pa is LE, must be native before write *
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
	asc_dvc->carr_pending_cnt = 0;

	AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
			     (ADV_INTR_ENABLE_HOST_INTR |
			      ADV_INTR_ENABLE_GLOBAL_INTR));

	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
	AdvWriteWordRegister(iop_base, IOPW_PC, word);

	/* finally, finally, gentlemen, start your engine */
	AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

	/*
	 * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
	 * Resets should be performed. The RISC has to be running
	 * to issue a SCSI Bus Reset.
	 */
	if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) {
		/*
		 * If the BIOS Signature is present in memory, restore the
		 * BIOS Handshake Configuration Table and do not perform
		 * a SCSI Bus Reset.
		 */
		if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM) / 2] ==
		    0x55AA) {
			/*
			 * Restore per TID negotiated values.
			 */
			AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE,
					 tagqng_able);
			for (tid = 0; tid <= ADV_MAX_TID; tid++) {
				AdvWriteByteLram(iop_base,
						 ASC_MC_NUMBER_OF_MAX_CMD + tid,
						 max_cmd[tid]);
			}
		} else {
			if (AdvResetSB(asc_dvc) != ADV_TRUE) {
				warn_code = ASC_WARN_BUSRESET_ERROR;
			}
		}
	}

	return warn_code;
}

/*
 * Initialize the ASC-38C1600.
 *
 * On failure set the ASC_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Needed after initialization for error recovery.
 */
static int AdvInitAsc38C1600Driver(ADV_DVC_VAR *asc_dvc)
{
	const struct firmware *fw;
	const char fwname[] = "advansys/38C1600.bin";
	AdvPortAddr iop_base;
	ushort warn_code;
	int begin_addr;
	int end_addr;
	ushort code_sum;
	long word;
	int i;
	int err;
	unsigned long chksum;
	ushort scsi_cfg1;
	uchar byte;
	uchar tid;
	ushort bios_mem[ASC_MC_BIOSLEN / 2];	/* BIOS RISC Memory 0x40-0x8F. */
	ushort wdtr_able, sdtr_able, ppr_able, tagqng_able;
	uchar max_cmd[ASC_MAX_TID + 1];

	/* If there is already an error, don't continue. */
	if (asc_dvc->err_code != 0) {
		return ADV_ERROR;
	}

	/*
	 * The caller must set 'chip_type' to ADV_CHIP_ASC38C1600.
	 */
	if (asc_dvc->chip_type != ADV_CHIP_ASC38C1600) {
		asc_dvc->err_code = ASC_IERR_BAD_CHIPTYPE;
		return ADV_ERROR;
	}

	warn_code = 0;
	iop_base = asc_dvc->iop_base;

	/*
	 * Save the RISC memory BIOS region before writing the microcode.
	 * The BIOS may already be loaded and using its RISC LRAM region
	 * so its region must be saved and restored.
	 *
	 * Note: This code makes the assumption, which is currently true,
	 * that a chip reset does not clear RISC LRAM.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvReadWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				bios_mem[i]);
	}

	/*
	 * Save current per TID negotiated values.
	 */
	AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
	AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ASC_MAX_TID; tid++) {
		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
				max_cmd[tid]);
	}

	/*
	 * RAM BIST (Built-In Self Test)
	 *
	 * Address : I/O base + offset 0x38h register (byte).
	 * Function: Bit 7-6(RW) : RAM mode
	 *                          Normal Mode   : 0x00
	 *                          Pre-test Mode : 0x40
	 *                          RAM Test Mode : 0x80
	 *           Bit 5       : unused
	 *           Bit 4(RO)   : Done bit
	 *           Bit 3-0(RO) : Status
	 *                          Host Error    : 0x08
	 *                          Int_RAM Error : 0x04
	 *                          RISC Error    : 0x02
	 *                          SCSI Error    : 0x01
	 *                          No Error      : 0x00
	 *
	 * Note: RAM BIST code should be put right here, before loading the
	 * microcode and after saving the RISC memory BIOS region.
	 */

	/*
	 * LRAM Pre-test
	 *
	 * Write PRE_TEST_MODE (0x40) to register and wait for 10 milliseconds.
	 * If Done bit not set or low nibble not PRE_TEST_VALUE (0x05), return
	 * an error. Reset to NORMAL_MODE (0x00) and do again. If cannot reset
	 * to NORMAL_MODE, return an error too.
	 */
	for (i = 0; i < 2; i++) {
		AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, PRE_TEST_MODE);
		mdelay(10);	/* Wait for 10ms before reading back. */
		byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
		if ((byte & RAM_TEST_DONE) == 0
		    || (byte & 0x0F) != PRE_TEST_VALUE) {
			asc_dvc->err_code = ASC_IERR_BIST_PRE_TEST;
			return ADV_ERROR;
		}

		AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);
		mdelay(10);	/* Wait for 10ms before reading back. */
		if (AdvReadByteRegister(iop_base, IOPB_RAM_BIST)
		    != NORMAL_VALUE) {
			asc_dvc->err_code = ASC_IERR_BIST_PRE_TEST;
			return ADV_ERROR;
		}
	}

	/*
	 * LRAM Test - It takes about 1.5 ms to run through the test.
	 *
	 * Write RAM_TEST_MODE (0x80) to register and wait for 10 milliseconds.
	 * If Done bit not set or Status not 0, save register byte, set the
	 * err_code, and return an error.
	 */
	AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, RAM_TEST_MODE);
	mdelay(10);	/* Wait for 10ms before checking status. */

	byte = AdvReadByteRegister(iop_base, IOPB_RAM_BIST);
	if ((byte & RAM_TEST_DONE) == 0 || (byte & RAM_TEST_STATUS) != 0) {
		/* Get here if Done bit not set or Status not 0. */
		asc_dvc->bist_err_code = byte;	/* for BIOS display message */
		asc_dvc->err_code = ASC_IERR_BIST_RAM_TEST;
		return ADV_ERROR;
	}

	/* We need to reset back to normal mode after LRAM test passes. */
	AdvWriteByteRegister(iop_base, IOPB_RAM_BIST, NORMAL_MODE);

	err = request_firmware(&fw, fwname, asc_dvc->drv_ptr->dev);
	if (err) {
		printk(KERN_ERR "Failed to load image \"%s\" err %d\n",
		       fwname, err);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return err;
	}
	if (fw->size < 4) {
		printk(KERN_ERR "Bogus length %zu in image \"%s\"\n",
		       fw->size, fwname);
		release_firmware(fw);
		asc_dvc->err_code = ASC_IERR_MCODE_CHKSUM;
		return -EINVAL;
	}
	chksum = (fw->data[3] << 24) | (fw->data[2] << 16) |
		 (fw->data[1] << 8) | fw->data[0];
	asc_dvc->err_code = AdvLoadMicrocode(iop_base, &fw->data[4],
					     fw->size - 4, ADV_38C1600_MEMSIZE,
					     chksum);
	release_firmware(fw);
	if (asc_dvc->err_code)
		return ADV_ERROR;

	/*
	 * Restore the RISC memory BIOS region.
	 */
	for (i = 0; i < ASC_MC_BIOSLEN / 2; i++) {
		AdvWriteWordLram(iop_base, ASC_MC_BIOSMEM + (2 * i),
				 bios_mem[i]);
	}

	/*
	 * Calculate and write the microcode code checksum to the microcode
	 * code checksum location ASC_MC_CODE_CHK_SUM (0x2C).
	 */
	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, begin_addr);
	AdvReadWordLram(iop_base, ASC_MC_CODE_END_ADDR, end_addr);
	code_sum = 0;
	AdvWriteWordRegister(iop_base, IOPW_RAM_ADDR, begin_addr);
	for (word = begin_addr; word < end_addr; word += 2) {
		code_sum += AdvReadWordAutoIncLram(iop_base);
	}
	AdvWriteWordLram(iop_base, ASC_MC_CODE_CHK_SUM, code_sum);

	/*
	 * Read microcode version and date.
	 */
	AdvReadWordLram(iop_base, ASC_MC_VERSION_DATE,
			asc_dvc->cfg->mcode_date);
	AdvReadWordLram(iop_base, ASC_MC_VERSION_NUM,
			asc_dvc->cfg->mcode_version);

	/*
	 * Set the chip type to indicate the ASC38C1600.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_CHIP_TYPE, ADV_CHIP_ASC38C1600);

	/*
	 * Write 1 to bit 14 'DIS_TERM_DRV' in the SCSI_CFG1 register.
	 * When DIS_TERM_DRV set to 1, C_DET[3:0] will reflect current
	 * cable detection and then we are able to read C_DET[3:0].
	 *
	 * Note: We will reset DIS_TERM_DRV to 0 in the 'Set SCSI_CFG1
	 * Microcode Default Value' section below.
	 */
	scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);
	AdvWriteWordRegister(iop_base, IOPW_SCSI_CFG1,
			     scsi_cfg1 | DIS_TERM_DRV);

	/*
	 * If the PCI Configuration Command Register "Parity Error Response
	 * Control" Bit was clear (0), then set the microcode variable
	 * 'control_flag' CONTROL_FLAG_IGNORE_PERR flag to tell the microcode
	 * to ignore DMA parity errors.
	 */
	if (asc_dvc->cfg->control_flag & CONTROL_FLAG_IGNORE_PERR) {
		AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
		word |= CONTROL_FLAG_IGNORE_PERR;
		AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
	}

	/*
	 * If the BIOS control flag AIPP (Asynchronous Information
	 * Phase Protection) disable bit is not set, then set the firmware
	 * 'control_flag' CONTROL_FLAG_ENABLE_AIPP bit to enable
	 * AIPP checking and encoding.
	 */
	if ((asc_dvc->bios_ctrl & BIOS_CTRL_AIPP_DIS) == 0) {
		AdvReadWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
		word |= CONTROL_FLAG_ENABLE_AIPP;
		AdvWriteWordLram(iop_base, ASC_MC_CONTROL_FLAG, word);
	}

	/*
	 * For ASC-38C1600 use DMA_CFG0 default values: FIFO_THRESH_80B [6:4],
	 * and START_CTL_TH [3:2].
	 */
	AdvWriteByteRegister(iop_base, IOPB_DMA_CFG0,
			     FIFO_THRESH_80B | START_CTL_TH | READ_CMD_MRM);

	/*
	 * Microcode operating variables for WDTR, SDTR, and command tag
	 * queuing will be set in slave_configure() based on what a
	 * device reports it is capable of in Inquiry byte 7.
	 *
	 * If SCSI Bus Resets have been disabled, then directly set
	 * SDTR and WDTR from the EEPROM configuration. This will allow
	 * the BIOS and warm boot to work without a SCSI bus hang on
	 * the Inquiry caused by host and target mismatched DTR values.
	 * Without the SCSI Bus Reset, before an Inquiry a device can't
	 * be assumed to be in Asynchronous, Narrow mode.
	 */
	if ((asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) == 0) {
		AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE,
				 asc_dvc->wdtr_able);
		AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE,
				 asc_dvc->sdtr_able);
	}

	/*
	 * Set microcode operating variables for DISC and SDTR_SPEED1,
	 * SDTR_SPEED2, SDTR_SPEED3, and SDTR_SPEED4 based on the EEPROM
	 * configuration values.
	 *
	 * The SDTR per TID bitmask overrides the SDTR_SPEED1, SDTR_SPEED2,
	 * SDTR_SPEED3, and SDTR_SPEED4 values so it is safe to set them
	 * without determining here whether the device supports SDTR.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DISC_ENABLE,
			 asc_dvc->cfg->disc_enable);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED1, asc_dvc->sdtr_speed1);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED2, asc_dvc->sdtr_speed2);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED3, asc_dvc->sdtr_speed3);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_SPEED4, asc_dvc->sdtr_speed4);

	/*
	 * Set SCSI_CFG0 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG0 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG0,
			 PARITY_EN | QUEUE_128 | SEL_TMO_LONG | OUR_ID_EN |
			 asc_dvc->chip_scsi_id);

	/*
	 * Calculate SCSI_CFG1 Microcode Default Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 *
	 * Each ASC-38C1600 function has only two cable detect bits.
	 * The bus mode override bits are in IOPB_SOFT_OVER_WR.
	 */
	scsi_cfg1 = AdvReadWordRegister(iop_base, IOPW_SCSI_CFG1);

	/*
	 * If the cable is reversed all of the SCSI_CTRL register signals
	 * will be set. Check for and return an error if this condition is
	 * found.
	 */
	if ((AdvReadWordRegister(iop_base, IOPW_SCSI_CTRL) & 0x3F07) == 0x3F07) {
		asc_dvc->err_code |= ASC_IERR_REVERSED_CABLE;
		return ADV_ERROR;
	}

	/*
	 * Each ASC-38C1600 function has two connectors. Only an HVD device
	 * can not be connected to either connector. An LVD device or SE device
	 * may be connected to either connecor. If an SE device is connected,
	 * then at most Ultra speed (20 Mhz) can be used on both connectors.
	 *
	 * If an HVD device is attached, return an error.
	 */
	if (scsi_cfg1 & HVD) {
		asc_dvc->err_code |= ASC_IERR_HVD_DEVICE;
		return ADV_ERROR;
	}

	/*
	 * Each function in the ASC-38C1600 uses only the SE cable detect and
	 * termination because there are two connectors for each function. Each
	 * function may use either LVD or SE mode. Corresponding the SE automatic
	 * termination control EEPROM bits are used for each function. Each
	 * function has its own EEPROM. If SE automatic control is enabled for
	 * the function, then set the termination value based on a table listed
	 * in a_condor.h.
	 *
	 * If manual termination is specified in the EEPROM for the function,
	 * then 'termination' was set-up in AscInitFrom38C1600EEPROM() and is
	 * ready to be 'ored' into SCSI_CFG1.
	 */
	if ((asc_dvc->cfg->termination & TERM_SE) == 0) {
		struct pci_dev *pdev = adv_dvc_to_pdev(asc_dvc);
		/* SE automatic termination control is enabled. */
		switch (scsi_cfg1 & C_DET_SE) {
			/* TERM_SE_HI: on, TERM_SE_LO: on */
		case 0x1:
		case 0x2:
		case 0x3:
			asc_dvc->cfg->termination |= TERM_SE;
			break;

		case 0x0:
			if (PCI_FUNC(pdev->devfn) == 0) {
				/* Function 0 - TERM_SE_HI: off, TERM_SE_LO: off */
			} else {
				/* Function 1 - TERM_SE_HI: on, TERM_SE_LO: off */
				asc_dvc->cfg->termination |= TERM_SE_HI;
			}
			break;
		}
	}

	/*
	 * Clear any set TERM_SE bits.
	 */
	scsi_cfg1 &= ~TERM_SE;

	/*
	 * Invert the TERM_SE bits and then set 'scsi_cfg1'.
	 */
	scsi_cfg1 |= (~asc_dvc->cfg->termination & TERM_SE);

	/*
	 * Clear Big Endian and Terminator Polarity bits and set possibly
	 * modified termination control bits in the Microcode SCSI_CFG1
	 * Register Value.
	 *
	 * Big Endian bit is not used even on big endian machines.
	 */
	scsi_cfg1 &= (~BIG_ENDIAN & ~DIS_TERM_DRV & ~TERM_POL);

	/*
	 * Set SCSI_CFG1 Microcode Default Value
	 *
	 * Set possibly modified termination control bits in the Microcode
	 * SCSI_CFG1 Register Value.
	 *
	 * The microcode will set the SCSI_CFG1 register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SCSI_CFG1, scsi_cfg1);

	/*
	 * Set MEM_CFG Microcode Default Value
	 *
	 * The microcode will set the MEM_CFG register using this value
	 * after it is started below.
	 *
	 * MEM_CFG may be accessed as a word or byte, but only bits 0-7
	 * are defined.
	 *
	 * ASC-38C1600 has 32KB internal memory.
	 *
	 * XXX - Since ASC38C1600 Rev.3 has a Local RAM failure issue, we come
	 * out a special 16K Adv Library and Microcode version. After the issue
	 * resolved, we should turn back to the 32K support. Both a_condor.h and
	 * mcode.sas files also need to be updated.
	 *
	 * AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
	 *  BIOS_EN | RAM_SZ_32KB);
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_MEM_CFG,
			 BIOS_EN | RAM_SZ_16KB);

	/*
	 * Set SEL_MASK Microcode Default Value
	 *
	 * The microcode will set the SEL_MASK register using this value
	 * after it is started below.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_DEFAULT_SEL_MASK,
			 ADV_TID_TO_TIDMASK(asc_dvc->chip_scsi_id));

	AdvBuildCarrierFreelist(asc_dvc);

	/*
	 * Set-up the Host->RISC Initiator Command Queue (ICQ).
	 */
	if ((asc_dvc->icq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->icq_sp->next_vpa));

	/*
	 * The first command issued will be placed in the stopper carrier.
	 */
	asc_dvc->icq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC ICQ physical address start value. Initialize the
	 * COMMA register to the same value otherwise the RISC will
	 * prematurely detect a command is available.
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_ICQ, asc_dvc->icq_sp->carr_pa);
	AdvWriteDWordRegister(iop_base, IOPDW_COMMA,
			      le32_to_cpu(asc_dvc->icq_sp->carr_pa));

	/*
	 * Set-up the RISC->Host Initiator Response Queue (IRQ).
	 */
	if ((asc_dvc->irq_sp = asc_dvc->carr_freelist) == NULL) {
		asc_dvc->err_code |= ASC_IERR_NO_CARRIER;
		return ADV_ERROR;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->next_vpa));

	/*
	 * The first command completed by the RISC will be placed in
	 * the stopper.
	 *
	 * Note: Set 'next_vpa' to ASC_CQ_STOPPER. When the request is
	 * completed the RISC will set the ASC_RQ_STOPPER bit.
	 */
	asc_dvc->irq_sp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Set RISC IRQ physical address start value.
	 */
	AdvWriteDWordLramNoSwap(iop_base, ASC_MC_IRQ, asc_dvc->irq_sp->carr_pa);
	asc_dvc->carr_pending_cnt = 0;

	AdvWriteByteRegister(iop_base, IOPB_INTR_ENABLES,
			     (ADV_INTR_ENABLE_HOST_INTR |
			      ADV_INTR_ENABLE_GLOBAL_INTR));
	AdvReadWordLram(iop_base, ASC_MC_CODE_BEGIN_ADDR, word);
	AdvWriteWordRegister(iop_base, IOPW_PC, word);

	/* finally, finally, gentlemen, start your engine */
	AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_RUN);

	/*
	 * Reset the SCSI Bus if the EEPROM indicates that SCSI Bus
	 * Resets should be performed. The RISC has to be running
	 * to issue a SCSI Bus Reset.
	 */
	if (asc_dvc->bios_ctrl & BIOS_CTRL_RESET_SCSI_BUS) {
		/*
		 * If the BIOS Signature is present in memory, restore the
		 * per TID microcode operating variables.
		 */
		if (bios_mem[(ASC_MC_BIOS_SIGNATURE - ASC_MC_BIOSMEM) / 2] ==
		    0x55AA) {
			/*
			 * Restore per TID negotiated values.
			 */
			AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
			AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
			AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE,
					 tagqng_able);
			for (tid = 0; tid <= ASC_MAX_TID; tid++) {
				AdvWriteByteLram(iop_base,
						 ASC_MC_NUMBER_OF_MAX_CMD + tid,
						 max_cmd[tid]);
			}
		} else {
			if (AdvResetSB(asc_dvc) != ADV_TRUE) {
				warn_code = ASC_WARN_BUSRESET_ERROR;
			}
		}
	}

	return warn_code;
}

/*
 * Reset chip and SCSI Bus.
 *
 * Return Value:
 *      ADV_TRUE(1) -   Chip re-initialization and SCSI Bus Reset successful.
 *      ADV_FALSE(0) -  Chip re-initialization and SCSI Bus Reset failure.
 */
static int AdvResetChipAndSB(ADV_DVC_VAR *asc_dvc)
{
	int status;
	ushort wdtr_able, sdtr_able, tagqng_able;
	ushort ppr_able = 0;
	uchar tid, max_cmd[ADV_MAX_TID + 1];
	AdvPortAddr iop_base;
	ushort bios_sig;

	iop_base = asc_dvc->iop_base;

	/*
	 * Save current per TID negotiated values.
	 */
	AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600) {
		AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
	}
	AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		AdvReadByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
				max_cmd[tid]);
	}

	/*
	 * Force the AdvInitAsc3550/38C0800Driver() function to
	 * perform a SCSI Bus Reset by clearing the BIOS signature word.
	 * The initialization functions assumes a SCSI Bus Reset is not
	 * needed if the BIOS signature word is present.
	 */
	AdvReadWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, bios_sig);
	AdvWriteWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, 0);

	/*
	 * Stop chip and reset it.
	 */
	AdvWriteWordRegister(iop_base, IOPW_RISC_CSR, ADV_RISC_CSR_STOP);
	AdvWriteWordRegister(iop_base, IOPW_CTRL_REG, ADV_CTRL_REG_CMD_RESET);
	mdelay(100);
	AdvWriteWordRegister(iop_base, IOPW_CTRL_REG,
			     ADV_CTRL_REG_CMD_WR_IO_REG);

	/*
	 * Reset Adv Library error code, if any, and try
	 * re-initializing the chip.
	 */
	asc_dvc->err_code = 0;
	if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600) {
		status = AdvInitAsc38C1600Driver(asc_dvc);
	} else if (asc_dvc->chip_type == ADV_CHIP_ASC38C0800) {
		status = AdvInitAsc38C0800Driver(asc_dvc);
	} else {
		status = AdvInitAsc3550Driver(asc_dvc);
	}

	/* Translate initialization return value to status value. */
	if (status == 0) {
		status = ADV_TRUE;
	} else {
		status = ADV_FALSE;
	}

	/*
	 * Restore the BIOS signature word.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_BIOS_SIGNATURE, bios_sig);

	/*
	 * Restore per TID negotiated values.
	 */
	AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, wdtr_able);
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, sdtr_able);
	if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600) {
		AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, ppr_able);
	}
	AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE, tagqng_able);
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		AdvWriteByteLram(iop_base, ASC_MC_NUMBER_OF_MAX_CMD + tid,
				 max_cmd[tid]);
	}

	return status;
}

/*
 * adv_async_callback() - Adv Library asynchronous event callback function.
 */
static void adv_async_callback(ADV_DVC_VAR *adv_dvc_varp, uchar code)
{
	switch (code) {
	case ADV_ASYNC_SCSI_BUS_RESET_DET:
		/*
		 * The firmware detected a SCSI Bus reset.
		 */
		ASC_DBG(0, "ADV_ASYNC_SCSI_BUS_RESET_DET\n");
		break;

	case ADV_ASYNC_RDMA_FAILURE:
		/*
		 * Handle RDMA failure by resetting the SCSI Bus and
		 * possibly the chip if it is unresponsive. Log the error
		 * with a unique code.
		 */
		ASC_DBG(0, "ADV_ASYNC_RDMA_FAILURE\n");
		AdvResetChipAndSB(adv_dvc_varp);
		break;

	case ADV_HOST_SCSI_BUS_RESET:
		/*
		 * Host generated SCSI bus reset occurred.
		 */
		ASC_DBG(0, "ADV_HOST_SCSI_BUS_RESET\n");
		break;

	default:
		ASC_DBG(0, "unknown code 0x%x\n", code);
		break;
	}
}

/*
 * adv_isr_callback() - Second Level Interrupt Handler called by AdvISR().
 *
 * Callback function for the Wide SCSI Adv Library.
 */
static void adv_isr_callback(ADV_DVC_VAR *adv_dvc_varp, ADV_SCSI_REQ_Q *scsiqp)
{
	struct asc_board *boardp;
	adv_req_t *reqp;
	adv_sgblk_t *sgblkp;
	struct scsi_cmnd *scp;
	struct Scsi_Host *shost;
	ADV_DCNT resid_cnt;

	ASC_DBG(1, "adv_dvc_varp 0x%lx, scsiqp 0x%lx\n",
		 (ulong)adv_dvc_varp, (ulong)scsiqp);
	ASC_DBG_PRT_ADV_SCSI_REQ_Q(2, scsiqp);

	/*
	 * Get the adv_req_t structure for the command that has been
	 * completed. The adv_req_t structure actually contains the
	 * completed ADV_SCSI_REQ_Q structure.
	 */
	reqp = (adv_req_t *)ADV_U32_TO_VADDR(scsiqp->srb_ptr);
	ASC_DBG(1, "reqp 0x%lx\n", (ulong)reqp);
	if (reqp == NULL) {
		ASC_PRINT("adv_isr_callback: reqp is NULL\n");
		return;
	}

	/*
	 * Get the struct scsi_cmnd structure and Scsi_Host structure for the
	 * command that has been completed.
	 *
	 * Note: The adv_req_t request structure and adv_sgblk_t structure,
	 * if any, are dropped, because a board structure pointer can not be
	 * determined.
	 */
	scp = reqp->cmndp;
	ASC_DBG(1, "scp 0x%p\n", scp);
	if (scp == NULL) {
		ASC_PRINT
		    ("adv_isr_callback: scp is NULL; adv_req_t dropped.\n");
		return;
	}
	ASC_DBG_PRT_CDB(2, scp->cmnd, scp->cmd_len);

	shost = scp->device->host;
	ASC_STATS(shost, callback);
	ASC_DBG(1, "shost 0x%p\n", shost);

	boardp = shost_priv(shost);
	BUG_ON(adv_dvc_varp != &boardp->dvc_var.adv_dvc_var);

	/*
	 * 'done_status' contains the command's ending status.
	 */
	switch (scsiqp->done_status) {
	case QD_NO_ERROR:
		ASC_DBG(2, "QD_NO_ERROR\n");
		scp->result = 0;

		/*
		 * Check for an underrun condition.
		 *
		 * If there was no error and an underrun condition, then
		 * then return the number of underrun bytes.
		 */
		resid_cnt = le32_to_cpu(scsiqp->data_cnt);
		if (scsi_bufflen(scp) != 0 && resid_cnt != 0 &&
		    resid_cnt <= scsi_bufflen(scp)) {
			ASC_DBG(1, "underrun condition %lu bytes\n",
				 (ulong)resid_cnt);
			scsi_set_resid(scp, resid_cnt);
		}
		break;

	case QD_WITH_ERROR:
		ASC_DBG(2, "QD_WITH_ERROR\n");
		switch (scsiqp->host_status) {
		case QHSTA_NO_ERROR:
			if (scsiqp->scsi_status == SAM_STAT_CHECK_CONDITION) {
				ASC_DBG(2, "SAM_STAT_CHECK_CONDITION\n");
				ASC_DBG_PRT_SENSE(2, scp->sense_buffer,
						  SCSI_SENSE_BUFFERSIZE);
				/*
				 * Note: The 'status_byte()' macro used by
				 * target drivers defined in scsi.h shifts the
				 * status byte returned by host drivers right
				 * by 1 bit.  This is why target drivers also
				 * use right shifted status byte definitions.
				 * For instance target drivers use
				 * CHECK_CONDITION, defined to 0x1, instead of
				 * the SCSI defined check condition value of
				 * 0x2. Host drivers are supposed to return
				 * the status byte as it is defined by SCSI.
				 */
				scp->result = DRIVER_BYTE(DRIVER_SENSE) |
				    STATUS_BYTE(scsiqp->scsi_status);
			} else {
				scp->result = STATUS_BYTE(scsiqp->scsi_status);
			}
			break;

		default:
			/* Some other QHSTA error occurred. */
			ASC_DBG(1, "host_status 0x%x\n", scsiqp->host_status);
			scp->result = HOST_BYTE(DID_BAD_TARGET);
			break;
		}
		break;

	case QD_ABORTED_BY_HOST:
		ASC_DBG(1, "QD_ABORTED_BY_HOST\n");
		scp->result =
		    HOST_BYTE(DID_ABORT) | STATUS_BYTE(scsiqp->scsi_status);
		break;

	default:
		ASC_DBG(1, "done_status 0x%x\n", scsiqp->done_status);
		scp->result =
		    HOST_BYTE(DID_ERROR) | STATUS_BYTE(scsiqp->scsi_status);
		break;
	}

	/*
	 * If the 'init_tidmask' bit isn't already set for the target and the
	 * current request finished normally, then set the bit for the target
	 * to indicate that a device is present.
	 */
	if ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(scp->device->id)) == 0 &&
	    scsiqp->done_status == QD_NO_ERROR &&
	    scsiqp->host_status == QHSTA_NO_ERROR) {
		boardp->init_tidmask |= ADV_TID_TO_TIDMASK(scp->device->id);
	}

	asc_scsi_done(scp);

	/*
	 * Free all 'adv_sgblk_t' structures allocated for the request.
	 */
	while ((sgblkp = reqp->sgblkp) != NULL) {
		/* Remove 'sgblkp' from the request list. */
		reqp->sgblkp = sgblkp->next_sgblkp;

		/* Add 'sgblkp' to the board free list. */
		sgblkp->next_sgblkp = boardp->adv_sgblkp;
		boardp->adv_sgblkp = sgblkp;
	}

	/*
	 * Free the adv_req_t structure used with the command by adding
	 * it back to the board free list.
	 */
	reqp->next_reqp = boardp->adv_reqp;
	boardp->adv_reqp = reqp;

	ASC_DBG(1, "done\n");
}

/*
 * Adv Library Interrupt Service Routine
 *
 *  This function is called by a driver's interrupt service routine.
 *  The function disables and re-enables interrupts.
 *
 *  When a microcode idle command is completed, the ADV_DVC_VAR
 *  'idle_cmd_done' field is set to ADV_TRUE.
 *
 *  Note: AdvISR() can be called when interrupts are disabled or even
 *  when there is no hardware interrupt condition present. It will
 *  always check for completed idle commands and microcode requests.
 *  This is an important feature that shouldn't be changed because it
 *  allows commands to be completed from polling mode loops.
 *
 * Return:
 *   ADV_TRUE(1) - interrupt was pending
 *   ADV_FALSE(0) - no interrupt was pending
 */
static int AdvISR(ADV_DVC_VAR *asc_dvc)
{
	AdvPortAddr iop_base;
	uchar int_stat;
	ushort target_bit;
	ADV_CARR_T *free_carrp;
	ADV_VADDR irq_next_vpa;
	ADV_SCSI_REQ_Q *scsiq;

	iop_base = asc_dvc->iop_base;

	/* Reading the register clears the interrupt. */
	int_stat = AdvReadByteRegister(iop_base, IOPB_INTR_STATUS_REG);

	if ((int_stat & (ADV_INTR_STATUS_INTRA | ADV_INTR_STATUS_INTRB |
			 ADV_INTR_STATUS_INTRC)) == 0) {
		return ADV_FALSE;
	}

	/*
	 * Notify the driver of an asynchronous microcode condition by
	 * calling the adv_async_callback function. The function
	 * is passed the microcode ASC_MC_INTRB_CODE byte value.
	 */
	if (int_stat & ADV_INTR_STATUS_INTRB) {
		uchar intrb_code;

		AdvReadByteLram(iop_base, ASC_MC_INTRB_CODE, intrb_code);

		if (asc_dvc->chip_type == ADV_CHIP_ASC3550 ||
		    asc_dvc->chip_type == ADV_CHIP_ASC38C0800) {
			if (intrb_code == ADV_ASYNC_CARRIER_READY_FAILURE &&
			    asc_dvc->carr_pending_cnt != 0) {
				AdvWriteByteRegister(iop_base, IOPB_TICKLE,
						     ADV_TICKLE_A);
				if (asc_dvc->chip_type == ADV_CHIP_ASC3550) {
					AdvWriteByteRegister(iop_base,
							     IOPB_TICKLE,
							     ADV_TICKLE_NOP);
				}
			}
		}

		adv_async_callback(asc_dvc, intrb_code);
	}

	/*
	 * Check if the IRQ stopper carrier contains a completed request.
	 */
	while (((irq_next_vpa =
		 le32_to_cpu(asc_dvc->irq_sp->next_vpa)) & ASC_RQ_DONE) != 0) {
		/*
		 * Get a pointer to the newly completed ADV_SCSI_REQ_Q structure.
		 * The RISC will have set 'areq_vpa' to a virtual address.
		 *
		 * The firmware will have copied the ASC_SCSI_REQ_Q.scsiq_ptr
		 * field to the carrier ADV_CARR_T.areq_vpa field. The conversion
		 * below complements the conversion of ASC_SCSI_REQ_Q.scsiq_ptr'
		 * in AdvExeScsiQueue().
		 */
		scsiq = (ADV_SCSI_REQ_Q *)
		    ADV_U32_TO_VADDR(le32_to_cpu(asc_dvc->irq_sp->areq_vpa));

		/*
		 * Request finished with good status and the queue was not
		 * DMAed to host memory by the firmware. Set all status fields
		 * to indicate good status.
		 */
		if ((irq_next_vpa & ASC_RQ_GOOD) != 0) {
			scsiq->done_status = QD_NO_ERROR;
			scsiq->host_status = scsiq->scsi_status = 0;
			scsiq->data_cnt = 0L;
		}

		/*
		 * Advance the stopper pointer to the next carrier
		 * ignoring the lower four bits. Free the previous
		 * stopper carrier.
		 */
		free_carrp = asc_dvc->irq_sp;
		asc_dvc->irq_sp = (ADV_CARR_T *)
		    ADV_U32_TO_VADDR(ASC_GET_CARRP(irq_next_vpa));

		free_carrp->next_vpa =
		    cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->carr_freelist));
		asc_dvc->carr_freelist = free_carrp;
		asc_dvc->carr_pending_cnt--;

		target_bit = ADV_TID_TO_TIDMASK(scsiq->target_id);

		/*
		 * Clear request microcode control flag.
		 */
		scsiq->cntl = 0;

		/*
		 * Notify the driver of the completed request by passing
		 * the ADV_SCSI_REQ_Q pointer to its callback function.
		 */
		scsiq->a_flag |= ADV_SCSIQ_DONE;
		adv_isr_callback(asc_dvc, scsiq);
		/*
		 * Note: After the driver callback function is called, 'scsiq'
		 * can no longer be referenced.
		 *
		 * Fall through and continue processing other completed
		 * requests...
		 */
	}
	return ADV_TRUE;
}

static int AscSetLibErrorCode(ASC_DVC_VAR *asc_dvc, ushort err_code)
{
	if (asc_dvc->err_code == 0) {
		asc_dvc->err_code = err_code;
		AscWriteLramWord(asc_dvc->iop_base, ASCV_ASCDVC_ERR_CODE_W,
				 err_code);
	}
	return err_code;
}

static void AscAckInterrupt(PortAddr iop_base)
{
	uchar host_flag;
	uchar risc_flag;
	ushort loop;

	loop = 0;
	do {
		risc_flag = AscReadLramByte(iop_base, ASCV_RISC_FLAG_B);
		if (loop++ > 0x7FFF) {
			break;
		}
	} while ((risc_flag & ASC_RISC_FLAG_GEN_INT) != 0);
	host_flag =
	    AscReadLramByte(iop_base,
			    ASCV_HOST_FLAG_B) & (~ASC_HOST_FLAG_ACK_INT);
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
			 (uchar)(host_flag | ASC_HOST_FLAG_ACK_INT));
	AscSetChipStatus(iop_base, CIW_INT_ACK);
	loop = 0;
	while (AscGetChipStatus(iop_base) & CSW_INT_PENDING) {
		AscSetChipStatus(iop_base, CIW_INT_ACK);
		if (loop++ > 3) {
			break;
		}
	}
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
}

static uchar AscGetSynPeriodIndex(ASC_DVC_VAR *asc_dvc, uchar syn_time)
{
	const uchar *period_table;
	int max_index;
	int min_index;
	int i;

	period_table = asc_dvc->sdtr_period_tbl;
	max_index = (int)asc_dvc->max_sdtr_index;
	min_index = (int)asc_dvc->min_sdtr_index;
	if ((syn_time <= period_table[max_index])) {
		for (i = min_index; i < (max_index - 1); i++) {
			if (syn_time <= period_table[i]) {
				return (uchar)i;
			}
		}
		return (uchar)max_index;
	} else {
		return (uchar)(max_index + 1);
	}
}

static uchar
AscMsgOutSDTR(ASC_DVC_VAR *asc_dvc, uchar sdtr_period, uchar sdtr_offset)
{
	EXT_MSG sdtr_buf;
	uchar sdtr_period_index;
	PortAddr iop_base;

	iop_base = asc_dvc->iop_base;
	sdtr_buf.msg_type = EXTENDED_MESSAGE;
	sdtr_buf.msg_len = MS_SDTR_LEN;
	sdtr_buf.msg_req = EXTENDED_SDTR;
	sdtr_buf.xfer_period = sdtr_period;
	sdtr_offset &= ASC_SYN_MAX_OFFSET;
	sdtr_buf.req_ack_offset = sdtr_offset;
	sdtr_period_index = AscGetSynPeriodIndex(asc_dvc, sdtr_period);
	if (sdtr_period_index <= asc_dvc->max_sdtr_index) {
		AscMemWordCopyPtrToLram(iop_base, ASCV_MSGOUT_BEG,
					(uchar *)&sdtr_buf,
					sizeof(EXT_MSG) >> 1);
		return ((sdtr_period_index << 4) | sdtr_offset);
	} else {
		sdtr_buf.req_ack_offset = 0;
		AscMemWordCopyPtrToLram(iop_base, ASCV_MSGOUT_BEG,
					(uchar *)&sdtr_buf,
					sizeof(EXT_MSG) >> 1);
		return 0;
	}
}

static uchar
AscCalSDTRData(ASC_DVC_VAR *asc_dvc, uchar sdtr_period, uchar syn_offset)
{
	uchar byte;
	uchar sdtr_period_ix;

	sdtr_period_ix = AscGetSynPeriodIndex(asc_dvc, sdtr_period);
	if (sdtr_period_ix > asc_dvc->max_sdtr_index)
		return 0xFF;
	byte = (sdtr_period_ix << 4) | (syn_offset & ASC_SYN_MAX_OFFSET);
	return byte;
}

static int AscSetChipSynRegAtID(PortAddr iop_base, uchar id, uchar sdtr_data)
{
	ASC_SCSI_BIT_ID_TYPE org_id;
	int i;
	int sta = TRUE;

	AscSetBank(iop_base, 1);
	org_id = AscReadChipDvcID(iop_base);
	for (i = 0; i <= ASC_MAX_TID; i++) {
		if (org_id == (0x01 << i))
			break;
	}
	org_id = (ASC_SCSI_BIT_ID_TYPE) i;
	AscWriteChipDvcID(iop_base, id);
	if (AscReadChipDvcID(iop_base) == (0x01 << id)) {
		AscSetBank(iop_base, 0);
		AscSetChipSyn(iop_base, sdtr_data);
		if (AscGetChipSyn(iop_base) != sdtr_data) {
			sta = FALSE;
		}
	} else {
		sta = FALSE;
	}
	AscSetBank(iop_base, 1);
	AscWriteChipDvcID(iop_base, org_id);
	AscSetBank(iop_base, 0);
	return (sta);
}

static void AscSetChipSDTR(PortAddr iop_base, uchar sdtr_data, uchar tid_no)
{
	AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
	AscPutMCodeSDTRDoneAtID(iop_base, tid_no, sdtr_data);
}

static int AscIsrChipHalted(ASC_DVC_VAR *asc_dvc)
{
	EXT_MSG ext_msg;
	EXT_MSG out_msg;
	ushort halt_q_addr;
	int sdtr_accept;
	ushort int_halt_code;
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;
	PortAddr iop_base;
	uchar tag_code;
	uchar q_status;
	uchar halt_qp;
	uchar sdtr_data;
	uchar target_ix;
	uchar q_cntl, tid_no;
	uchar cur_dvc_qng;
	uchar asyn_sdtr;
	uchar scsi_status;
	struct asc_board *boardp;

	BUG_ON(!asc_dvc->drv_ptr);
	boardp = asc_dvc->drv_ptr;

	iop_base = asc_dvc->iop_base;
	int_halt_code = AscReadLramWord(iop_base, ASCV_HALTCODE_W);

	halt_qp = AscReadLramByte(iop_base, ASCV_CURCDB_B);
	halt_q_addr = ASC_QNO_TO_QADDR(halt_qp);
	target_ix = AscReadLramByte(iop_base,
				    (ushort)(halt_q_addr +
					     (ushort)ASC_SCSIQ_B_TARGET_IX));
	q_cntl = AscReadLramByte(iop_base,
			    (ushort)(halt_q_addr + (ushort)ASC_SCSIQ_B_CNTL));
	tid_no = ASC_TIX_TO_TID(target_ix);
	target_id = (uchar)ASC_TID_TO_TARGET_ID(tid_no);
	if (asc_dvc->pci_fix_asyn_xfer & target_id) {
		asyn_sdtr = ASYN_SDTR_DATA_FIX_PCI_REV_AB;
	} else {
		asyn_sdtr = 0;
	}
	if (int_halt_code == ASC_HALT_DISABLE_ASYN_USE_SYN_FIX) {
		if (asc_dvc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iop_base, 0, tid_no);
			boardp->sdtr_data[tid_no] = 0;
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_ENABLE_ASYN_USE_SYN_FIX) {
		if (asc_dvc->pci_fix_asyn_xfer & target_id) {
			AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
			boardp->sdtr_data[tid_no] = asyn_sdtr;
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_EXTMSG_IN) {
		AscMemWordCopyPtrFromLram(iop_base,
					  ASCV_MSGIN_BEG,
					  (uchar *)&ext_msg,
					  sizeof(EXT_MSG) >> 1);

		if (ext_msg.msg_type == EXTENDED_MESSAGE &&
		    ext_msg.msg_req == EXTENDED_SDTR &&
		    ext_msg.msg_len == MS_SDTR_LEN) {
			sdtr_accept = TRUE;
			if ((ext_msg.req_ack_offset > ASC_SYN_MAX_OFFSET)) {

				sdtr_accept = FALSE;
				ext_msg.req_ack_offset = ASC_SYN_MAX_OFFSET;
			}
			if ((ext_msg.xfer_period <
			     asc_dvc->sdtr_period_tbl[asc_dvc->min_sdtr_index])
			    || (ext_msg.xfer_period >
				asc_dvc->sdtr_period_tbl[asc_dvc->
							 max_sdtr_index])) {
				sdtr_accept = FALSE;
				ext_msg.xfer_period =
				    asc_dvc->sdtr_period_tbl[asc_dvc->
							     min_sdtr_index];
			}
			if (sdtr_accept) {
				sdtr_data =
				    AscCalSDTRData(asc_dvc, ext_msg.xfer_period,
						   ext_msg.req_ack_offset);
				if ((sdtr_data == 0xFF)) {

					q_cntl |= QC_MSG_OUT;
					asc_dvc->init_sdtr &= ~target_id;
					asc_dvc->sdtr_done &= ~target_id;
					AscSetChipSDTR(iop_base, asyn_sdtr,
						       tid_no);
					boardp->sdtr_data[tid_no] = asyn_sdtr;
				}
			}
			if (ext_msg.req_ack_offset == 0) {

				q_cntl &= ~QC_MSG_OUT;
				asc_dvc->init_sdtr &= ~target_id;
				asc_dvc->sdtr_done &= ~target_id;
				AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
			} else {
				if (sdtr_accept && (q_cntl & QC_MSG_OUT)) {
					q_cntl &= ~QC_MSG_OUT;
					asc_dvc->sdtr_done |= target_id;
					asc_dvc->init_sdtr |= target_id;
					asc_dvc->pci_fix_asyn_xfer &=
					    ~target_id;
					sdtr_data =
					    AscCalSDTRData(asc_dvc,
							   ext_msg.xfer_period,
							   ext_msg.
							   req_ack_offset);
					AscSetChipSDTR(iop_base, sdtr_data,
						       tid_no);
					boardp->sdtr_data[tid_no] = sdtr_data;
				} else {
					q_cntl |= QC_MSG_OUT;
					AscMsgOutSDTR(asc_dvc,
						      ext_msg.xfer_period,
						      ext_msg.req_ack_offset);
					asc_dvc->pci_fix_asyn_xfer &=
					    ~target_id;
					sdtr_data =
					    AscCalSDTRData(asc_dvc,
							   ext_msg.xfer_period,
							   ext_msg.
							   req_ack_offset);
					AscSetChipSDTR(iop_base, sdtr_data,
						       tid_no);
					boardp->sdtr_data[tid_no] = sdtr_data;
					asc_dvc->sdtr_done |= target_id;
					asc_dvc->init_sdtr |= target_id;
				}
			}

			AscWriteLramByte(iop_base,
					 (ushort)(halt_q_addr +
						  (ushort)ASC_SCSIQ_B_CNTL),
					 q_cntl);
			AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
			return (0);
		} else if (ext_msg.msg_type == EXTENDED_MESSAGE &&
			   ext_msg.msg_req == EXTENDED_WDTR &&
			   ext_msg.msg_len == MS_WDTR_LEN) {

			ext_msg.wdtr_width = 0;
			AscMemWordCopyPtrToLram(iop_base,
						ASCV_MSGOUT_BEG,
						(uchar *)&ext_msg,
						sizeof(EXT_MSG) >> 1);
			q_cntl |= QC_MSG_OUT;
			AscWriteLramByte(iop_base,
					 (ushort)(halt_q_addr +
						  (ushort)ASC_SCSIQ_B_CNTL),
					 q_cntl);
			AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
			return (0);
		} else {

			ext_msg.msg_type = MESSAGE_REJECT;
			AscMemWordCopyPtrToLram(iop_base,
						ASCV_MSGOUT_BEG,
						(uchar *)&ext_msg,
						sizeof(EXT_MSG) >> 1);
			q_cntl |= QC_MSG_OUT;
			AscWriteLramByte(iop_base,
					 (ushort)(halt_q_addr +
						  (ushort)ASC_SCSIQ_B_CNTL),
					 q_cntl);
			AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
			return (0);
		}
	} else if (int_halt_code == ASC_HALT_CHK_CONDITION) {

		q_cntl |= QC_REQ_SENSE;

		if ((asc_dvc->init_sdtr & target_id) != 0) {

			asc_dvc->sdtr_done &= ~target_id;

			sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
			q_cntl |= QC_MSG_OUT;
			AscMsgOutSDTR(asc_dvc,
				      asc_dvc->
				      sdtr_period_tbl[(sdtr_data >> 4) &
						      (uchar)(asc_dvc->
							      max_sdtr_index -
							      1)],
				      (uchar)(sdtr_data & (uchar)
					      ASC_SYN_MAX_OFFSET));
		}

		AscWriteLramByte(iop_base,
				 (ushort)(halt_q_addr +
					  (ushort)ASC_SCSIQ_B_CNTL), q_cntl);

		tag_code = AscReadLramByte(iop_base,
					   (ushort)(halt_q_addr + (ushort)
						    ASC_SCSIQ_B_TAG_CODE));
		tag_code &= 0xDC;
		if ((asc_dvc->pci_fix_asyn_xfer & target_id)
		    && !(asc_dvc->pci_fix_asyn_xfer_always & target_id)
		    ) {

			tag_code |= (ASC_TAG_FLAG_DISABLE_DISCONNECT
				     | ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX);

		}
		AscWriteLramByte(iop_base,
				 (ushort)(halt_q_addr +
					  (ushort)ASC_SCSIQ_B_TAG_CODE),
				 tag_code);

		q_status = AscReadLramByte(iop_base,
					   (ushort)(halt_q_addr + (ushort)
						    ASC_SCSIQ_B_STATUS));
		q_status |= (QS_READY | QS_BUSY);
		AscWriteLramByte(iop_base,
				 (ushort)(halt_q_addr +
					  (ushort)ASC_SCSIQ_B_STATUS),
				 q_status);

		scsi_busy = AscReadLramByte(iop_base, (ushort)ASCV_SCSIBUSY_B);
		scsi_busy &= ~target_id;
		AscWriteLramByte(iop_base, (ushort)ASCV_SCSIBUSY_B, scsi_busy);

		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_SDTR_REJECTED) {

		AscMemWordCopyPtrFromLram(iop_base,
					  ASCV_MSGOUT_BEG,
					  (uchar *)&out_msg,
					  sizeof(EXT_MSG) >> 1);

		if ((out_msg.msg_type == EXTENDED_MESSAGE) &&
		    (out_msg.msg_len == MS_SDTR_LEN) &&
		    (out_msg.msg_req == EXTENDED_SDTR)) {

			asc_dvc->init_sdtr &= ~target_id;
			asc_dvc->sdtr_done &= ~target_id;
			AscSetChipSDTR(iop_base, asyn_sdtr, tid_no);
			boardp->sdtr_data[tid_no] = asyn_sdtr;
		}
		q_cntl &= ~QC_MSG_OUT;
		AscWriteLramByte(iop_base,
				 (ushort)(halt_q_addr +
					  (ushort)ASC_SCSIQ_B_CNTL), q_cntl);
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	} else if (int_halt_code == ASC_HALT_SS_QUEUE_FULL) {

		scsi_status = AscReadLramByte(iop_base,
					      (ushort)((ushort)halt_q_addr +
						       (ushort)
						       ASC_SCSIQ_SCSI_STATUS));
		cur_dvc_qng =
		    AscReadLramByte(iop_base,
				    (ushort)((ushort)ASC_QADR_BEG +
					     (ushort)target_ix));
		if ((cur_dvc_qng > 0) && (asc_dvc->cur_dvc_qng[tid_no] > 0)) {

			scsi_busy = AscReadLramByte(iop_base,
						    (ushort)ASCV_SCSIBUSY_B);
			scsi_busy |= target_id;
			AscWriteLramByte(iop_base,
					 (ushort)ASCV_SCSIBUSY_B, scsi_busy);
			asc_dvc->queue_full_or_busy |= target_id;

			if (scsi_status == SAM_STAT_TASK_SET_FULL) {
				if (cur_dvc_qng > ASC_MIN_TAGGED_CMD) {
					cur_dvc_qng -= 1;
					asc_dvc->max_dvc_qng[tid_no] =
					    cur_dvc_qng;

					AscWriteLramByte(iop_base,
							 (ushort)((ushort)
								  ASCV_MAX_DVC_QNG_BEG
								  + (ushort)
								  tid_no),
							 cur_dvc_qng);

					/*
					 * Set the device queue depth to the
					 * number of active requests when the
					 * QUEUE FULL condition was encountered.
					 */
					boardp->queue_full |= target_id;
					boardp->queue_full_cnt[tid_no] =
					    cur_dvc_qng;
				}
			}
		}
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	}
#if CC_VERY_LONG_SG_LIST
	else if (int_halt_code == ASC_HALT_HOST_COPY_SG_LIST_TO_RISC) {
		uchar q_no;
		ushort q_addr;
		uchar sg_wk_q_no;
		uchar first_sg_wk_q_no;
		ASC_SCSI_Q *scsiq;	/* Ptr to driver request. */
		ASC_SG_HEAD *sg_head;	/* Ptr to driver SG request. */
		ASC_SG_LIST_Q scsi_sg_q;	/* Structure written to queue. */
		ushort sg_list_dwords;
		ushort sg_entry_cnt;
		uchar next_qp;
		int i;

		q_no = AscReadLramByte(iop_base, (ushort)ASCV_REQ_SG_LIST_QP);
		if (q_no == ASC_QLINK_END)
			return 0;

		q_addr = ASC_QNO_TO_QADDR(q_no);

		/*
		 * Convert the request's SRB pointer to a host ASC_SCSI_REQ
		 * structure pointer using a macro provided by the driver.
		 * The ASC_SCSI_REQ pointer provides a pointer to the
		 * host ASC_SG_HEAD structure.
		 */
		/* Read request's SRB pointer. */
		scsiq = (ASC_SCSI_Q *)
		    ASC_SRB2SCSIQ(ASC_U32_TO_VADDR(AscReadLramDWord(iop_base,
								    (ushort)
								    (q_addr +
								     ASC_SCSIQ_D_SRBPTR))));

		/*
		 * Get request's first and working SG queue.
		 */
		sg_wk_q_no = AscReadLramByte(iop_base,
					     (ushort)(q_addr +
						      ASC_SCSIQ_B_SG_WK_QP));

		first_sg_wk_q_no = AscReadLramByte(iop_base,
						   (ushort)(q_addr +
							    ASC_SCSIQ_B_FIRST_SG_WK_QP));

		/*
		 * Reset request's working SG queue back to the
		 * first SG queue.
		 */
		AscWriteLramByte(iop_base,
				 (ushort)(q_addr +
					  (ushort)ASC_SCSIQ_B_SG_WK_QP),
				 first_sg_wk_q_no);

		sg_head = scsiq->sg_head;

		/*
		 * Set sg_entry_cnt to the number of SG elements
		 * that will be completed on this interrupt.
		 *
		 * Note: The allocated SG queues contain ASC_MAX_SG_LIST - 1
		 * SG elements. The data_cnt and data_addr fields which
		 * add 1 to the SG element capacity are not used when
		 * restarting SG handling after a halt.
		 */
		if (scsiq->remain_sg_entry_cnt > (ASC_MAX_SG_LIST - 1)) {
			sg_entry_cnt = ASC_MAX_SG_LIST - 1;

			/*
			 * Keep track of remaining number of SG elements that
			 * will need to be handled on the next interrupt.
			 */
			scsiq->remain_sg_entry_cnt -= (ASC_MAX_SG_LIST - 1);
		} else {
			sg_entry_cnt = scsiq->remain_sg_entry_cnt;
			scsiq->remain_sg_entry_cnt = 0;
		}

		/*
		 * Copy SG elements into the list of allocated SG queues.
		 *
		 * Last index completed is saved in scsiq->next_sg_index.
		 */
		next_qp = first_sg_wk_q_no;
		q_addr = ASC_QNO_TO_QADDR(next_qp);
		scsi_sg_q.sg_head_qp = q_no;
		scsi_sg_q.cntl = QCSG_SG_XFER_LIST;
		for (i = 0; i < sg_head->queue_cnt; i++) {
			scsi_sg_q.seq_no = i + 1;
			if (sg_entry_cnt > ASC_SG_LIST_PER_Q) {
				sg_list_dwords = (uchar)(ASC_SG_LIST_PER_Q * 2);
				sg_entry_cnt -= ASC_SG_LIST_PER_Q;
				/*
				 * After very first SG queue RISC FW uses next
				 * SG queue first element then checks sg_list_cnt
				 * against zero and then decrements, so set
				 * sg_list_cnt 1 less than number of SG elements
				 * in each SG queue.
				 */
				scsi_sg_q.sg_list_cnt = ASC_SG_LIST_PER_Q - 1;
				scsi_sg_q.sg_cur_list_cnt =
				    ASC_SG_LIST_PER_Q - 1;
			} else {
				/*
				 * This is the last SG queue in the list of
				 * allocated SG queues. If there are more
				 * SG elements than will fit in the allocated
				 * queues, then set the QCSG_SG_XFER_MORE flag.
				 */
				if (scsiq->remain_sg_entry_cnt != 0) {
					scsi_sg_q.cntl |= QCSG_SG_XFER_MORE;
				} else {
					scsi_sg_q.cntl |= QCSG_SG_XFER_END;
				}
				/* equals sg_entry_cnt * 2 */
				sg_list_dwords = sg_entry_cnt << 1;
				scsi_sg_q.sg_list_cnt = sg_entry_cnt - 1;
				scsi_sg_q.sg_cur_list_cnt = sg_entry_cnt - 1;
				sg_entry_cnt = 0;
			}

			scsi_sg_q.q_no = next_qp;
			AscMemWordCopyPtrToLram(iop_base,
						q_addr + ASC_SCSIQ_SGHD_CPY_BEG,
						(uchar *)&scsi_sg_q,
						sizeof(ASC_SG_LIST_Q) >> 1);

			AscMemDWordCopyPtrToLram(iop_base,
						 q_addr + ASC_SGQ_LIST_BEG,
						 (uchar *)&sg_head->
						 sg_list[scsiq->next_sg_index],
						 sg_list_dwords);

			scsiq->next_sg_index += ASC_SG_LIST_PER_Q;

			/*
			 * If the just completed SG queue contained the
			 * last SG element, then no more SG queues need
			 * to be written.
			 */
			if (scsi_sg_q.cntl & QCSG_SG_XFER_END) {
				break;
			}

			next_qp = AscReadLramByte(iop_base,
						  (ushort)(q_addr +
							   ASC_SCSIQ_B_FWD));
			q_addr = ASC_QNO_TO_QADDR(next_qp);
		}

		/*
		 * Clear the halt condition so the RISC will be restarted
		 * after the return.
		 */
		AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0);
		return (0);
	}
#endif /* CC_VERY_LONG_SG_LIST */
	return (0);
}

/*
 * void
 * DvcGetQinfo(PortAddr iop_base, ushort s_addr, uchar *inbuf, int words)
 *
 * Calling/Exit State:
 *    none
 *
 * Description:
 *     Input an ASC_QDONE_INFO structure from the chip
 */
static void
DvcGetQinfo(PortAddr iop_base, ushort s_addr, uchar *inbuf, int words)
{
	int i;
	ushort word;

	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < 2 * words; i += 2) {
		if (i == 10) {
			continue;
		}
		word = inpw(iop_base + IOP_RAM_DATA);
		inbuf[i] = word & 0xff;
		inbuf[i + 1] = (word >> 8) & 0xff;
	}
	ASC_DBG_PRT_HEX(2, "DvcGetQinfo", inbuf, 2 * words);
}

static uchar
_AscCopyLramScsiDoneQ(PortAddr iop_base,
		      ushort q_addr,
		      ASC_QDONE_INFO *scsiq, ASC_DCNT max_dma_count)
{
	ushort _val;
	uchar sg_queue_cnt;

	DvcGetQinfo(iop_base,
		    q_addr + ASC_SCSIQ_DONE_INFO_BEG,
		    (uchar *)scsiq,
		    (sizeof(ASC_SCSIQ_2) + sizeof(ASC_SCSIQ_3)) / 2);

	_val = AscReadLramWord(iop_base,
			       (ushort)(q_addr + (ushort)ASC_SCSIQ_B_STATUS));
	scsiq->q_status = (uchar)_val;
	scsiq->q_no = (uchar)(_val >> 8);
	_val = AscReadLramWord(iop_base,
			       (ushort)(q_addr + (ushort)ASC_SCSIQ_B_CNTL));
	scsiq->cntl = (uchar)_val;
	sg_queue_cnt = (uchar)(_val >> 8);
	_val = AscReadLramWord(iop_base,
			       (ushort)(q_addr +
					(ushort)ASC_SCSIQ_B_SENSE_LEN));
	scsiq->sense_len = (uchar)_val;
	scsiq->extra_bytes = (uchar)(_val >> 8);

	/*
	 * Read high word of remain bytes from alternate location.
	 */
	scsiq->remain_bytes = (((ADV_DCNT)AscReadLramWord(iop_base,
							  (ushort)(q_addr +
								   (ushort)
								   ASC_SCSIQ_W_ALT_DC1)))
			       << 16);
	/*
	 * Read low word of remain bytes from original location.
	 */
	scsiq->remain_bytes += AscReadLramWord(iop_base,
					       (ushort)(q_addr + (ushort)
							ASC_SCSIQ_DW_REMAIN_XFER_CNT));

	scsiq->remain_bytes &= max_dma_count;
	return sg_queue_cnt;
}

/*
 * asc_isr_callback() - Second Level Interrupt Handler called by AscISR().
 *
 * Interrupt callback function for the Narrow SCSI Asc Library.
 */
static void asc_isr_callback(ASC_DVC_VAR *asc_dvc_varp, ASC_QDONE_INFO *qdonep)
{
	struct asc_board *boardp;
	struct scsi_cmnd *scp;
	struct Scsi_Host *shost;

	ASC_DBG(1, "asc_dvc_varp 0x%p, qdonep 0x%p\n", asc_dvc_varp, qdonep);
	ASC_DBG_PRT_ASC_QDONE_INFO(2, qdonep);

	scp = advansys_srb_to_ptr(asc_dvc_varp, qdonep->d2.srb_ptr);
	if (!scp)
		return;

	ASC_DBG_PRT_CDB(2, scp->cmnd, scp->cmd_len);

	shost = scp->device->host;
	ASC_STATS(shost, callback);
	ASC_DBG(1, "shost 0x%p\n", shost);

	boardp = shost_priv(shost);
	BUG_ON(asc_dvc_varp != &boardp->dvc_var.asc_dvc_var);

	dma_unmap_single(boardp->dev, scp->SCp.dma_handle,
			 SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
	/*
	 * 'qdonep' contains the command's ending status.
	 */
	switch (qdonep->d3.done_stat) {
	case QD_NO_ERROR:
		ASC_DBG(2, "QD_NO_ERROR\n");
		scp->result = 0;

		/*
		 * Check for an underrun condition.
		 *
		 * If there was no error and an underrun condition, then
		 * return the number of underrun bytes.
		 */
		if (scsi_bufflen(scp) != 0 && qdonep->remain_bytes != 0 &&
		    qdonep->remain_bytes <= scsi_bufflen(scp)) {
			ASC_DBG(1, "underrun condition %u bytes\n",
				 (unsigned)qdonep->remain_bytes);
			scsi_set_resid(scp, qdonep->remain_bytes);
		}
		break;

	case QD_WITH_ERROR:
		ASC_DBG(2, "QD_WITH_ERROR\n");
		switch (qdonep->d3.host_stat) {
		case QHSTA_NO_ERROR:
			if (qdonep->d3.scsi_stat == SAM_STAT_CHECK_CONDITION) {
				ASC_DBG(2, "SAM_STAT_CHECK_CONDITION\n");
				ASC_DBG_PRT_SENSE(2, scp->sense_buffer,
						  SCSI_SENSE_BUFFERSIZE);
				/*
				 * Note: The 'status_byte()' macro used by
				 * target drivers defined in scsi.h shifts the
				 * status byte returned by host drivers right
				 * by 1 bit.  This is why target drivers also
				 * use right shifted status byte definitions.
				 * For instance target drivers use
				 * CHECK_CONDITION, defined to 0x1, instead of
				 * the SCSI defined check condition value of
				 * 0x2. Host drivers are supposed to return
				 * the status byte as it is defined by SCSI.
				 */
				scp->result = DRIVER_BYTE(DRIVER_SENSE) |
				    STATUS_BYTE(qdonep->d3.scsi_stat);
			} else {
				scp->result = STATUS_BYTE(qdonep->d3.scsi_stat);
			}
			break;

		default:
			/* QHSTA error occurred */
			ASC_DBG(1, "host_stat 0x%x\n", qdonep->d3.host_stat);
			scp->result = HOST_BYTE(DID_BAD_TARGET);
			break;
		}
		break;

	case QD_ABORTED_BY_HOST:
		ASC_DBG(1, "QD_ABORTED_BY_HOST\n");
		scp->result =
		    HOST_BYTE(DID_ABORT) | MSG_BYTE(qdonep->d3.
						    scsi_msg) |
		    STATUS_BYTE(qdonep->d3.scsi_stat);
		break;

	default:
		ASC_DBG(1, "done_stat 0x%x\n", qdonep->d3.done_stat);
		scp->result =
		    HOST_BYTE(DID_ERROR) | MSG_BYTE(qdonep->d3.
						    scsi_msg) |
		    STATUS_BYTE(qdonep->d3.scsi_stat);
		break;
	}

	/*
	 * If the 'init_tidmask' bit isn't already set for the target and the
	 * current request finished normally, then set the bit for the target
	 * to indicate that a device is present.
	 */
	if ((boardp->init_tidmask & ADV_TID_TO_TIDMASK(scp->device->id)) == 0 &&
	    qdonep->d3.done_stat == QD_NO_ERROR &&
	    qdonep->d3.host_stat == QHSTA_NO_ERROR) {
		boardp->init_tidmask |= ADV_TID_TO_TIDMASK(scp->device->id);
	}

	asc_scsi_done(scp);
}

static int AscIsrQDone(ASC_DVC_VAR *asc_dvc)
{
	uchar next_qp;
	uchar n_q_used;
	uchar sg_list_qp;
	uchar sg_queue_cnt;
	uchar q_cnt;
	uchar done_q_tail;
	uchar tid_no;
	ASC_SCSI_BIT_ID_TYPE scsi_busy;
	ASC_SCSI_BIT_ID_TYPE target_id;
	PortAddr iop_base;
	ushort q_addr;
	ushort sg_q_addr;
	uchar cur_target_qng;
	ASC_QDONE_INFO scsiq_buf;
	ASC_QDONE_INFO *scsiq;
	int false_overrun;

	iop_base = asc_dvc->iop_base;
	n_q_used = 1;
	scsiq = (ASC_QDONE_INFO *)&scsiq_buf;
	done_q_tail = (uchar)AscGetVarDoneQTail(iop_base);
	q_addr = ASC_QNO_TO_QADDR(done_q_tail);
	next_qp = AscReadLramByte(iop_base,
				  (ushort)(q_addr + (ushort)ASC_SCSIQ_B_FWD));
	if (next_qp != ASC_QLINK_END) {
		AscPutVarDoneQTail(iop_base, next_qp);
		q_addr = ASC_QNO_TO_QADDR(next_qp);
		sg_queue_cnt = _AscCopyLramScsiDoneQ(iop_base, q_addr, scsiq,
						     asc_dvc->max_dma_count);
		AscWriteLramByte(iop_base,
				 (ushort)(q_addr +
					  (ushort)ASC_SCSIQ_B_STATUS),
				 (uchar)(scsiq->
					 q_status & (uchar)~(QS_READY |
							     QS_ABORTED)));
		tid_no = ASC_TIX_TO_TID(scsiq->d2.target_ix);
		target_id = ASC_TIX_TO_TARGET_ID(scsiq->d2.target_ix);
		if ((scsiq->cntl & QC_SG_HEAD) != 0) {
			sg_q_addr = q_addr;
			sg_list_qp = next_qp;
			for (q_cnt = 0; q_cnt < sg_queue_cnt; q_cnt++) {
				sg_list_qp = AscReadLramByte(iop_base,
							     (ushort)(sg_q_addr
								      + (ushort)
								      ASC_SCSIQ_B_FWD));
				sg_q_addr = ASC_QNO_TO_QADDR(sg_list_qp);
				if (sg_list_qp == ASC_QLINK_END) {
					AscSetLibErrorCode(asc_dvc,
							   ASCQ_ERR_SG_Q_LINKS);
					scsiq->d3.done_stat = QD_WITH_ERROR;
					scsiq->d3.host_stat =
					    QHSTA_D_QDONE_SG_LIST_CORRUPTED;
					goto FATAL_ERR_QDONE;
				}
				AscWriteLramByte(iop_base,
						 (ushort)(sg_q_addr + (ushort)
							  ASC_SCSIQ_B_STATUS),
						 QS_FREE);
			}
			n_q_used = sg_queue_cnt + 1;
			AscPutVarDoneQTail(iop_base, sg_list_qp);
		}
		if (asc_dvc->queue_full_or_busy & target_id) {
			cur_target_qng = AscReadLramByte(iop_base,
							 (ushort)((ushort)
								  ASC_QADR_BEG
								  + (ushort)
								  scsiq->d2.
								  target_ix));
			if (cur_target_qng < asc_dvc->max_dvc_qng[tid_no]) {
				scsi_busy = AscReadLramByte(iop_base, (ushort)
							    ASCV_SCSIBUSY_B);
				scsi_busy &= ~target_id;
				AscWriteLramByte(iop_base,
						 (ushort)ASCV_SCSIBUSY_B,
						 scsi_busy);
				asc_dvc->queue_full_or_busy &= ~target_id;
			}
		}
		if (asc_dvc->cur_total_qng >= n_q_used) {
			asc_dvc->cur_total_qng -= n_q_used;
			if (asc_dvc->cur_dvc_qng[tid_no] != 0) {
				asc_dvc->cur_dvc_qng[tid_no]--;
			}
		} else {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CUR_QNG);
			scsiq->d3.done_stat = QD_WITH_ERROR;
			goto FATAL_ERR_QDONE;
		}
		if ((scsiq->d2.srb_ptr == 0UL) ||
		    ((scsiq->q_status & QS_ABORTED) != 0)) {
			return (0x11);
		} else if (scsiq->q_status == QS_DONE) {
			false_overrun = FALSE;
			if (scsiq->extra_bytes != 0) {
				scsiq->remain_bytes +=
				    (ADV_DCNT)scsiq->extra_bytes;
			}
			if (scsiq->d3.done_stat == QD_WITH_ERROR) {
				if (scsiq->d3.host_stat ==
				    QHSTA_M_DATA_OVER_RUN) {
					if ((scsiq->
					     cntl & (QC_DATA_IN | QC_DATA_OUT))
					    == 0) {
						scsiq->d3.done_stat =
						    QD_NO_ERROR;
						scsiq->d3.host_stat =
						    QHSTA_NO_ERROR;
					} else if (false_overrun) {
						scsiq->d3.done_stat =
						    QD_NO_ERROR;
						scsiq->d3.host_stat =
						    QHSTA_NO_ERROR;
					}
				} else if (scsiq->d3.host_stat ==
					   QHSTA_M_HUNG_REQ_SCSI_BUS_RESET) {
					AscStopChip(iop_base);
					AscSetChipControl(iop_base,
							  (uchar)(CC_SCSI_RESET
								  | CC_HALT));
					udelay(60);
					AscSetChipControl(iop_base, CC_HALT);
					AscSetChipStatus(iop_base,
							 CIW_CLR_SCSI_RESET_INT);
					AscSetChipStatus(iop_base, 0);
					AscSetChipControl(iop_base, 0);
				}
			}
			if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
				asc_isr_callback(asc_dvc, scsiq);
			} else {
				if ((AscReadLramByte(iop_base,
						     (ushort)(q_addr + (ushort)
							      ASC_SCSIQ_CDB_BEG))
				     == START_STOP)) {
					asc_dvc->unit_not_ready &= ~target_id;
					if (scsiq->d3.done_stat != QD_NO_ERROR) {
						asc_dvc->start_motor &=
						    ~target_id;
					}
				}
			}
			return (1);
		} else {
			AscSetLibErrorCode(asc_dvc, ASCQ_ERR_Q_STATUS);
 FATAL_ERR_QDONE:
			if ((scsiq->cntl & QC_NO_CALLBACK) == 0) {
				asc_isr_callback(asc_dvc, scsiq);
			}
			return (0x80);
		}
	}
	return (0);
}

static int AscISR(ASC_DVC_VAR *asc_dvc)
{
	ASC_CS_TYPE chipstat;
	PortAddr iop_base;
	ushort saved_ram_addr;
	uchar ctrl_reg;
	uchar saved_ctrl_reg;
	int int_pending;
	int status;
	uchar host_flag;

	iop_base = asc_dvc->iop_base;
	int_pending = FALSE;

	if (AscIsIntPending(iop_base) == 0)
		return int_pending;

	if ((asc_dvc->init_state & ASC_INIT_STATE_END_LOAD_MC) == 0) {
		return ERR;
	}
	if (asc_dvc->in_critical_cnt != 0) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_ON_CRITICAL);
		return ERR;
	}
	if (asc_dvc->is_in_int) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_ISR_RE_ENTRY);
		return ERR;
	}
	asc_dvc->is_in_int = TRUE;
	ctrl_reg = AscGetChipControl(iop_base);
	saved_ctrl_reg = ctrl_reg & (~(CC_SCSI_RESET | CC_CHIP_RESET |
				       CC_SINGLE_STEP | CC_DIAG | CC_TEST));
	chipstat = AscGetChipStatus(iop_base);
	if (chipstat & CSW_SCSI_RESET_LATCH) {
		if (!(asc_dvc->bus_type & (ASC_IS_VL | ASC_IS_EISA))) {
			int i = 10;
			int_pending = TRUE;
			asc_dvc->sdtr_done = 0;
			saved_ctrl_reg &= (uchar)(~CC_HALT);
			while ((AscGetChipStatus(iop_base) &
				CSW_SCSI_RESET_ACTIVE) && (i-- > 0)) {
				mdelay(100);
			}
			AscSetChipControl(iop_base, (CC_CHIP_RESET | CC_HALT));
			AscSetChipControl(iop_base, CC_HALT);
			AscSetChipStatus(iop_base, CIW_CLR_SCSI_RESET_INT);
			AscSetChipStatus(iop_base, 0);
			chipstat = AscGetChipStatus(iop_base);
		}
	}
	saved_ram_addr = AscGetChipLramAddr(iop_base);
	host_flag = AscReadLramByte(iop_base,
				    ASCV_HOST_FLAG_B) &
	    (uchar)(~ASC_HOST_FLAG_IN_ISR);
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B,
			 (uchar)(host_flag | (uchar)ASC_HOST_FLAG_IN_ISR));
	if ((chipstat & CSW_INT_PENDING) || (int_pending)) {
		AscAckInterrupt(iop_base);
		int_pending = TRUE;
		if ((chipstat & CSW_HALTED) && (ctrl_reg & CC_SINGLE_STEP)) {
			if (AscIsrChipHalted(asc_dvc) == ERR) {
				goto ISR_REPORT_QDONE_FATAL_ERROR;
			} else {
				saved_ctrl_reg &= (uchar)(~CC_HALT);
			}
		} else {
 ISR_REPORT_QDONE_FATAL_ERROR:
			if ((asc_dvc->dvc_cntl & ASC_CNTL_INT_MULTI_Q) != 0) {
				while (((status =
					 AscIsrQDone(asc_dvc)) & 0x01) != 0) {
				}
			} else {
				do {
					if ((status =
					     AscIsrQDone(asc_dvc)) == 1) {
						break;
					}
				} while (status == 0x11);
			}
			if ((status & 0x80) != 0)
				int_pending = ERR;
		}
	}
	AscWriteLramByte(iop_base, ASCV_HOST_FLAG_B, host_flag);
	AscSetChipLramAddr(iop_base, saved_ram_addr);
	AscSetChipControl(iop_base, saved_ctrl_reg);
	asc_dvc->is_in_int = FALSE;
	return int_pending;
}

/*
 * advansys_reset()
 *
 * Reset the bus associated with the command 'scp'.
 *
 * This function runs its own thread. Interrupts must be blocked but
 * sleeping is allowed and no locking other than for host structures is
 * required. Returns SUCCESS or FAILED.
 */
static int advansys_reset(struct scsi_cmnd *scp)
{
	struct Scsi_Host *shost = scp->device->host;
	struct asc_board *boardp = shost_priv(shost);
	unsigned long flags;
	int status;
	int ret = SUCCESS;

	ASC_DBG(1, "0x%p\n", scp);

	ASC_STATS(shost, reset);

	scmd_printk(KERN_INFO, scp, "SCSI bus reset started...\n");

	if (ASC_NARROW_BOARD(boardp)) {
		ASC_DVC_VAR *asc_dvc = &boardp->dvc_var.asc_dvc_var;

		/* Reset the chip and SCSI bus. */
		ASC_DBG(1, "before AscInitAsc1000Driver()\n");
		status = AscInitAsc1000Driver(asc_dvc);

		/* Refer to ASC_IERR_* definitions for meaning of 'err_code'. */
		if (asc_dvc->err_code || !asc_dvc->overrun_dma) {
			scmd_printk(KERN_INFO, scp, "SCSI bus reset error: "
				    "0x%x, status: 0x%x\n", asc_dvc->err_code,
				    status);
			ret = FAILED;
		} else if (status) {
			scmd_printk(KERN_INFO, scp, "SCSI bus reset warning: "
				    "0x%x\n", status);
		} else {
			scmd_printk(KERN_INFO, scp, "SCSI bus reset "
				    "successful\n");
		}

		ASC_DBG(1, "after AscInitAsc1000Driver()\n");
		spin_lock_irqsave(shost->host_lock, flags);
	} else {
		/*
		 * If the suggest reset bus flags are set, then reset the bus.
		 * Otherwise only reset the device.
		 */
		ADV_DVC_VAR *adv_dvc = &boardp->dvc_var.adv_dvc_var;

		/*
		 * Reset the target's SCSI bus.
		 */
		ASC_DBG(1, "before AdvResetChipAndSB()\n");
		switch (AdvResetChipAndSB(adv_dvc)) {
		case ASC_TRUE:
			scmd_printk(KERN_INFO, scp, "SCSI bus reset "
				    "successful\n");
			break;
		case ASC_FALSE:
		default:
			scmd_printk(KERN_INFO, scp, "SCSI bus reset error\n");
			ret = FAILED;
			break;
		}
		spin_lock_irqsave(shost->host_lock, flags);
		AdvISR(adv_dvc);
	}

	/* Save the time of the most recently completed reset. */
	boardp->last_reset = jiffies;
	spin_unlock_irqrestore(shost->host_lock, flags);

	ASC_DBG(1, "ret %d\n", ret);

	return ret;
}

/*
 * advansys_biosparam()
 *
 * Translate disk drive geometry if the "BIOS greater than 1 GB"
 * support is enabled for a drive.
 *
 * ip (information pointer) is an int array with the following definition:
 * ip[0]: heads
 * ip[1]: sectors
 * ip[2]: cylinders
 */
static int
advansys_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		   sector_t capacity, int ip[])
{
	struct asc_board *boardp = shost_priv(sdev->host);

	ASC_DBG(1, "begin\n");
	ASC_STATS(sdev->host, biosparam);
	if (ASC_NARROW_BOARD(boardp)) {
		if ((boardp->dvc_var.asc_dvc_var.dvc_cntl &
		     ASC_CNTL_BIOS_GT_1GB) && capacity > 0x200000) {
			ip[0] = 255;
			ip[1] = 63;
		} else {
			ip[0] = 64;
			ip[1] = 32;
		}
	} else {
		if ((boardp->dvc_var.adv_dvc_var.bios_ctrl &
		     BIOS_CTRL_EXTENDED_XLAT) && capacity > 0x200000) {
			ip[0] = 255;
			ip[1] = 63;
		} else {
			ip[0] = 64;
			ip[1] = 32;
		}
	}
	ip[2] = (unsigned long)capacity / (ip[0] * ip[1]);
	ASC_DBG(1, "end\n");
	return 0;
}

/*
 * First-level interrupt handler.
 *
 * 'dev_id' is a pointer to the interrupting adapter's Scsi_Host.
 */
static irqreturn_t advansys_interrupt(int irq, void *dev_id)
{
	struct Scsi_Host *shost = dev_id;
	struct asc_board *boardp = shost_priv(shost);
	irqreturn_t result = IRQ_NONE;

	ASC_DBG(2, "boardp 0x%p\n", boardp);
	spin_lock(shost->host_lock);
	if (ASC_NARROW_BOARD(boardp)) {
		if (AscIsIntPending(shost->io_port)) {
			result = IRQ_HANDLED;
			ASC_STATS(shost, interrupt);
			ASC_DBG(1, "before AscISR()\n");
			AscISR(&boardp->dvc_var.asc_dvc_var);
		}
	} else {
		ASC_DBG(1, "before AdvISR()\n");
		if (AdvISR(&boardp->dvc_var.adv_dvc_var)) {
			result = IRQ_HANDLED;
			ASC_STATS(shost, interrupt);
		}
	}
	spin_unlock(shost->host_lock);

	ASC_DBG(1, "end\n");
	return result;
}

static int AscHostReqRiscHalt(PortAddr iop_base)
{
	int count = 0;
	int sta = 0;
	uchar saved_stop_code;

	if (AscIsChipHalted(iop_base))
		return (1);
	saved_stop_code = AscReadLramByte(iop_base, ASCV_STOP_CODE_B);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
			 ASC_STOP_HOST_REQ_RISC_HALT | ASC_STOP_REQ_RISC_STOP);
	do {
		if (AscIsChipHalted(iop_base)) {
			sta = 1;
			break;
		}
		mdelay(100);
	} while (count++ < 20);
	AscWriteLramByte(iop_base, ASCV_STOP_CODE_B, saved_stop_code);
	return (sta);
}

static int
AscSetRunChipSynRegAtID(PortAddr iop_base, uchar tid_no, uchar sdtr_data)
{
	int sta = FALSE;

	if (AscHostReqRiscHalt(iop_base)) {
		sta = AscSetChipSynRegAtID(iop_base, tid_no, sdtr_data);
		AscStartChip(iop_base);
	}
	return sta;
}

static void AscAsyncFix(ASC_DVC_VAR *asc_dvc, struct scsi_device *sdev)
{
	char type = sdev->type;
	ASC_SCSI_BIT_ID_TYPE tid_bits = 1 << sdev->id;

	if (!(asc_dvc->bug_fix_cntl & ASC_BUG_FIX_ASYN_USE_SYN))
		return;
	if (asc_dvc->init_sdtr & tid_bits)
		return;

	if ((type == TYPE_ROM) && (strncmp(sdev->vendor, "HP ", 3) == 0))
		asc_dvc->pci_fix_asyn_xfer_always |= tid_bits;

	asc_dvc->pci_fix_asyn_xfer |= tid_bits;
	if ((type == TYPE_PROCESSOR) || (type == TYPE_SCANNER) ||
	    (type == TYPE_ROM) || (type == TYPE_TAPE))
		asc_dvc->pci_fix_asyn_xfer &= ~tid_bits;

	if (asc_dvc->pci_fix_asyn_xfer & tid_bits)
		AscSetRunChipSynRegAtID(asc_dvc->iop_base, sdev->id,
					ASYN_SDTR_DATA_FIX_PCI_REV_AB);
}

static void
advansys_narrow_slave_configure(struct scsi_device *sdev, ASC_DVC_VAR *asc_dvc)
{
	ASC_SCSI_BIT_ID_TYPE tid_bit = 1 << sdev->id;
	ASC_SCSI_BIT_ID_TYPE orig_use_tagged_qng = asc_dvc->use_tagged_qng;

	if (sdev->lun == 0) {
		ASC_SCSI_BIT_ID_TYPE orig_init_sdtr = asc_dvc->init_sdtr;
		if ((asc_dvc->cfg->sdtr_enable & tid_bit) && sdev->sdtr) {
			asc_dvc->init_sdtr |= tid_bit;
		} else {
			asc_dvc->init_sdtr &= ~tid_bit;
		}

		if (orig_init_sdtr != asc_dvc->init_sdtr)
			AscAsyncFix(asc_dvc, sdev);
	}

	if (sdev->tagged_supported) {
		if (asc_dvc->cfg->cmd_qng_enabled & tid_bit) {
			if (sdev->lun == 0) {
				asc_dvc->cfg->can_tagged_qng |= tid_bit;
				asc_dvc->use_tagged_qng |= tid_bit;
			}
			scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG,
						asc_dvc->max_dvc_qng[sdev->id]);
		}
	} else {
		if (sdev->lun == 0) {
			asc_dvc->cfg->can_tagged_qng &= ~tid_bit;
			asc_dvc->use_tagged_qng &= ~tid_bit;
		}
		scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);
	}

	if ((sdev->lun == 0) &&
	    (orig_use_tagged_qng != asc_dvc->use_tagged_qng)) {
		AscWriteLramByte(asc_dvc->iop_base, ASCV_DISC_ENABLE_B,
				 asc_dvc->cfg->disc_enable);
		AscWriteLramByte(asc_dvc->iop_base, ASCV_USE_TAGGED_QNG_B,
				 asc_dvc->use_tagged_qng);
		AscWriteLramByte(asc_dvc->iop_base, ASCV_CAN_TAGGED_QNG_B,
				 asc_dvc->cfg->can_tagged_qng);

		asc_dvc->max_dvc_qng[sdev->id] =
					asc_dvc->cfg->max_tag_qng[sdev->id];
		AscWriteLramByte(asc_dvc->iop_base,
				 (ushort)(ASCV_MAX_DVC_QNG_BEG + sdev->id),
				 asc_dvc->max_dvc_qng[sdev->id]);
	}
}

/*
 * Wide Transfers
 *
 * If the EEPROM enabled WDTR for the device and the device supports wide
 * bus (16 bit) transfers, then turn on the device's 'wdtr_able' bit and
 * write the new value to the microcode.
 */
static void
advansys_wide_enable_wdtr(AdvPortAddr iop_base, unsigned short tidmask)
{
	unsigned short cfg_word;
	AdvReadWordLram(iop_base, ASC_MC_WDTR_ABLE, cfg_word);
	if ((cfg_word & tidmask) != 0)
		return;

	cfg_word |= tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_WDTR_ABLE, cfg_word);

	/*
	 * Clear the microcode SDTR and WDTR negotiation done indicators for
	 * the target to cause it to negotiate with the new setting set above.
	 * WDTR when accepted causes the target to enter asynchronous mode, so
	 * SDTR must be negotiated.
	 */
	AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
	cfg_word &= ~tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
	AdvReadWordLram(iop_base, ASC_MC_WDTR_DONE, cfg_word);
	cfg_word &= ~tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_WDTR_DONE, cfg_word);
}

/*
 * Synchronous Transfers
 *
 * If the EEPROM enabled SDTR for the device and the device
 * supports synchronous transfers, then turn on the device's
 * 'sdtr_able' bit. Write the new value to the microcode.
 */
static void
advansys_wide_enable_sdtr(AdvPortAddr iop_base, unsigned short tidmask)
{
	unsigned short cfg_word;
	AdvReadWordLram(iop_base, ASC_MC_SDTR_ABLE, cfg_word);
	if ((cfg_word & tidmask) != 0)
		return;

	cfg_word |= tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_ABLE, cfg_word);

	/*
	 * Clear the microcode "SDTR negotiation" done indicator for the
	 * target to cause it to negotiate with the new setting set above.
	 */
	AdvReadWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
	cfg_word &= ~tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_SDTR_DONE, cfg_word);
}

/*
 * PPR (Parallel Protocol Request) Capable
 *
 * If the device supports DT mode, then it must be PPR capable.
 * The PPR message will be used in place of the SDTR and WDTR
 * messages to negotiate synchronous speed and offset, transfer
 * width, and protocol options.
 */
static void advansys_wide_enable_ppr(ADV_DVC_VAR *adv_dvc,
				AdvPortAddr iop_base, unsigned short tidmask)
{
	AdvReadWordLram(iop_base, ASC_MC_PPR_ABLE, adv_dvc->ppr_able);
	adv_dvc->ppr_able |= tidmask;
	AdvWriteWordLram(iop_base, ASC_MC_PPR_ABLE, adv_dvc->ppr_able);
}

static void
advansys_wide_slave_configure(struct scsi_device *sdev, ADV_DVC_VAR *adv_dvc)
{
	AdvPortAddr iop_base = adv_dvc->iop_base;
	unsigned short tidmask = 1 << sdev->id;

	if (sdev->lun == 0) {
		/*
		 * Handle WDTR, SDTR, and Tag Queuing. If the feature
		 * is enabled in the EEPROM and the device supports the
		 * feature, then enable it in the microcode.
		 */

		if ((adv_dvc->wdtr_able & tidmask) && sdev->wdtr)
			advansys_wide_enable_wdtr(iop_base, tidmask);
		if ((adv_dvc->sdtr_able & tidmask) && sdev->sdtr)
			advansys_wide_enable_sdtr(iop_base, tidmask);
		if (adv_dvc->chip_type == ADV_CHIP_ASC38C1600 && sdev->ppr)
			advansys_wide_enable_ppr(adv_dvc, iop_base, tidmask);

		/*
		 * Tag Queuing is disabled for the BIOS which runs in polled
		 * mode and would see no benefit from Tag Queuing. Also by
		 * disabling Tag Queuing in the BIOS devices with Tag Queuing
		 * bugs will at least work with the BIOS.
		 */
		if ((adv_dvc->tagqng_able & tidmask) &&
		    sdev->tagged_supported) {
			unsigned short cfg_word;
			AdvReadWordLram(iop_base, ASC_MC_TAGQNG_ABLE, cfg_word);
			cfg_word |= tidmask;
			AdvWriteWordLram(iop_base, ASC_MC_TAGQNG_ABLE,
					 cfg_word);
			AdvWriteByteLram(iop_base,
					 ASC_MC_NUMBER_OF_MAX_CMD + sdev->id,
					 adv_dvc->max_dvc_qng);
		}
	}

	if ((adv_dvc->tagqng_able & tidmask) && sdev->tagged_supported) {
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG,
					adv_dvc->max_dvc_qng);
	} else {
		scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);
	}
}

/*
 * Set the number of commands to queue per device for the
 * specified host adapter.
 */
static int advansys_slave_configure(struct scsi_device *sdev)
{
	struct asc_board *boardp = shost_priv(sdev->host);

	if (ASC_NARROW_BOARD(boardp))
		advansys_narrow_slave_configure(sdev,
						&boardp->dvc_var.asc_dvc_var);
	else
		advansys_wide_slave_configure(sdev,
						&boardp->dvc_var.adv_dvc_var);

	return 0;
}

static __le32 advansys_get_sense_buffer_dma(struct scsi_cmnd *scp)
{
	struct asc_board *board = shost_priv(scp->device->host);
	scp->SCp.dma_handle = dma_map_single(board->dev, scp->sense_buffer,
					     SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
	dma_cache_sync(board->dev, scp->sense_buffer,
		       SCSI_SENSE_BUFFERSIZE, DMA_FROM_DEVICE);
	return cpu_to_le32(scp->SCp.dma_handle);
}

static int asc_build_req(struct asc_board *boardp, struct scsi_cmnd *scp,
			struct asc_scsi_q *asc_scsi_q)
{
	struct asc_dvc_var *asc_dvc = &boardp->dvc_var.asc_dvc_var;
	int use_sg;

	memset(asc_scsi_q, 0, sizeof(*asc_scsi_q));

	/*
	 * Point the ASC_SCSI_Q to the 'struct scsi_cmnd'.
	 */
	asc_scsi_q->q2.srb_ptr = advansys_ptr_to_srb(asc_dvc, scp);
	if (asc_scsi_q->q2.srb_ptr == BAD_SRB) {
		scp->result = HOST_BYTE(DID_SOFT_ERROR);
		return ASC_ERROR;
	}

	/*
	 * Build the ASC_SCSI_Q request.
	 */
	asc_scsi_q->cdbptr = &scp->cmnd[0];
	asc_scsi_q->q2.cdb_len = scp->cmd_len;
	asc_scsi_q->q1.target_id = ASC_TID_TO_TARGET_ID(scp->device->id);
	asc_scsi_q->q1.target_lun = scp->device->lun;
	asc_scsi_q->q2.target_ix =
	    ASC_TIDLUN_TO_IX(scp->device->id, scp->device->lun);
	asc_scsi_q->q1.sense_addr = advansys_get_sense_buffer_dma(scp);
	asc_scsi_q->q1.sense_len = SCSI_SENSE_BUFFERSIZE;

	/*
	 * If there are any outstanding requests for the current target,
	 * then every 255th request send an ORDERED request. This heuristic
	 * tries to retain the benefit of request sorting while preventing
	 * request starvation. 255 is the max number of tags or pending commands
	 * a device may have outstanding.
	 *
	 * The request count is incremented below for every successfully
	 * started request.
	 *
	 */
	if ((asc_dvc->cur_dvc_qng[scp->device->id] > 0) &&
	    (boardp->reqcnt[scp->device->id] % 255) == 0) {
		asc_scsi_q->q2.tag_code = MSG_ORDERED_TAG;
	} else {
		asc_scsi_q->q2.tag_code = MSG_SIMPLE_TAG;
	}

	/* Build ASC_SCSI_Q */
	use_sg = scsi_dma_map(scp);
	if (use_sg != 0) {
		int sgcnt;
		struct scatterlist *slp;
		struct asc_sg_head *asc_sg_head;

		if (use_sg > scp->device->host->sg_tablesize) {
			scmd_printk(KERN_ERR, scp, "use_sg %d > "
				"sg_tablesize %d\n", use_sg,
				scp->device->host->sg_tablesize);
			scsi_dma_unmap(scp);
			scp->result = HOST_BYTE(DID_ERROR);
			return ASC_ERROR;
		}

		asc_sg_head = kzalloc(sizeof(asc_scsi_q->sg_head) +
			use_sg * sizeof(struct asc_sg_list), GFP_ATOMIC);
		if (!asc_sg_head) {
			scsi_dma_unmap(scp);
			scp->result = HOST_BYTE(DID_SOFT_ERROR);
			return ASC_ERROR;
		}

		asc_scsi_q->q1.cntl |= QC_SG_HEAD;
		asc_scsi_q->sg_head = asc_sg_head;
		asc_scsi_q->q1.data_cnt = 0;
		asc_scsi_q->q1.data_addr = 0;
		/* This is a byte value, otherwise it would need to be swapped. */
		asc_sg_head->entry_cnt = asc_scsi_q->q1.sg_queue_cnt = use_sg;
		ASC_STATS_ADD(scp->device->host, xfer_elem,
			      asc_sg_head->entry_cnt);

		/*
		 * Convert scatter-gather list into ASC_SG_HEAD list.
		 */
		scsi_for_each_sg(scp, slp, use_sg, sgcnt) {
			asc_sg_head->sg_list[sgcnt].addr =
			    cpu_to_le32(sg_dma_address(slp));
			asc_sg_head->sg_list[sgcnt].bytes =
			    cpu_to_le32(sg_dma_len(slp));
			ASC_STATS_ADD(scp->device->host, xfer_sect,
				      DIV_ROUND_UP(sg_dma_len(slp), 512));
		}
	}

	ASC_STATS(scp->device->host, xfer_cnt);

	ASC_DBG_PRT_ASC_SCSI_Q(2, asc_scsi_q);
	ASC_DBG_PRT_CDB(1, scp->cmnd, scp->cmd_len);

	return ASC_NOERROR;
}

/*
 * Build scatter-gather list for Adv Library (Wide Board).
 *
 * Additional ADV_SG_BLOCK structures will need to be allocated
 * if the total number of scatter-gather elements exceeds
 * NO_OF_SG_PER_BLOCK (15). The ADV_SG_BLOCK structures are
 * assumed to be physically contiguous.
 *
 * Return:
 *      ADV_SUCCESS(1) - SG List successfully created
 *      ADV_ERROR(-1) - SG List creation failed
 */
static int
adv_get_sglist(struct asc_board *boardp, adv_req_t *reqp, struct scsi_cmnd *scp,
	       int use_sg)
{
	adv_sgblk_t *sgblkp;
	ADV_SCSI_REQ_Q *scsiqp;
	struct scatterlist *slp;
	int sg_elem_cnt;
	ADV_SG_BLOCK *sg_block, *prev_sg_block;
	ADV_PADDR sg_block_paddr;
	int i;

	scsiqp = (ADV_SCSI_REQ_Q *)ADV_32BALIGN(&reqp->scsi_req_q);
	slp = scsi_sglist(scp);
	sg_elem_cnt = use_sg;
	prev_sg_block = NULL;
	reqp->sgblkp = NULL;

	for (;;) {
		/*
		 * Allocate a 'adv_sgblk_t' structure from the board free
		 * list. One 'adv_sgblk_t' structure holds NO_OF_SG_PER_BLOCK
		 * (15) scatter-gather elements.
		 */
		if ((sgblkp = boardp->adv_sgblkp) == NULL) {
			ASC_DBG(1, "no free adv_sgblk_t\n");
			ASC_STATS(scp->device->host, adv_build_nosg);

			/*
			 * Allocation failed. Free 'adv_sgblk_t' structures
			 * already allocated for the request.
			 */
			while ((sgblkp = reqp->sgblkp) != NULL) {
				/* Remove 'sgblkp' from the request list. */
				reqp->sgblkp = sgblkp->next_sgblkp;

				/* Add 'sgblkp' to the board free list. */
				sgblkp->next_sgblkp = boardp->adv_sgblkp;
				boardp->adv_sgblkp = sgblkp;
			}
			return ASC_BUSY;
		}

		/* Complete 'adv_sgblk_t' board allocation. */
		boardp->adv_sgblkp = sgblkp->next_sgblkp;
		sgblkp->next_sgblkp = NULL;

		/*
		 * Get 8 byte aligned virtual and physical addresses
		 * for the allocated ADV_SG_BLOCK structure.
		 */
		sg_block = (ADV_SG_BLOCK *)ADV_8BALIGN(&sgblkp->sg_block);
		sg_block_paddr = virt_to_bus(sg_block);

		/*
		 * Check if this is the first 'adv_sgblk_t' for the
		 * request.
		 */
		if (reqp->sgblkp == NULL) {
			/* Request's first scatter-gather block. */
			reqp->sgblkp = sgblkp;

			/*
			 * Set ADV_SCSI_REQ_T ADV_SG_BLOCK virtual and physical
			 * address pointers.
			 */
			scsiqp->sg_list_ptr = sg_block;
			scsiqp->sg_real_addr = cpu_to_le32(sg_block_paddr);
		} else {
			/* Request's second or later scatter-gather block. */
			sgblkp->next_sgblkp = reqp->sgblkp;
			reqp->sgblkp = sgblkp;

			/*
			 * Point the previous ADV_SG_BLOCK structure to
			 * the newly allocated ADV_SG_BLOCK structure.
			 */
			prev_sg_block->sg_ptr = cpu_to_le32(sg_block_paddr);
		}

		for (i = 0; i < NO_OF_SG_PER_BLOCK; i++) {
			sg_block->sg_list[i].sg_addr =
					cpu_to_le32(sg_dma_address(slp));
			sg_block->sg_list[i].sg_count =
					cpu_to_le32(sg_dma_len(slp));
			ASC_STATS_ADD(scp->device->host, xfer_sect,
				      DIV_ROUND_UP(sg_dma_len(slp), 512));

			if (--sg_elem_cnt == 0) {	/* Last ADV_SG_BLOCK and scatter-gather entry. */
				sg_block->sg_cnt = i + 1;
				sg_block->sg_ptr = 0L;	/* Last ADV_SG_BLOCK in list. */
				return ADV_SUCCESS;
			}
			slp++;
		}
		sg_block->sg_cnt = NO_OF_SG_PER_BLOCK;
		prev_sg_block = sg_block;
	}
}

/*
 * Build a request structure for the Adv Library (Wide Board).
 *
 * If an adv_req_t can not be allocated to issue the request,
 * then return ASC_BUSY. If an error occurs, then return ASC_ERROR.
 *
 * Multi-byte fields in the ASC_SCSI_REQ_Q that are used by the
 * microcode for DMA addresses or math operations are byte swapped
 * to little-endian order.
 */
static int
adv_build_req(struct asc_board *boardp, struct scsi_cmnd *scp,
	      ADV_SCSI_REQ_Q **adv_scsiqpp)
{
	adv_req_t *reqp;
	ADV_SCSI_REQ_Q *scsiqp;
	int i;
	int ret;
	int use_sg;

	/*
	 * Allocate an adv_req_t structure from the board to execute
	 * the command.
	 */
	if (boardp->adv_reqp == NULL) {
		ASC_DBG(1, "no free adv_req_t\n");
		ASC_STATS(scp->device->host, adv_build_noreq);
		return ASC_BUSY;
	} else {
		reqp = boardp->adv_reqp;
		boardp->adv_reqp = reqp->next_reqp;
		reqp->next_reqp = NULL;
	}

	/*
	 * Get 32-byte aligned ADV_SCSI_REQ_Q and ADV_SG_BLOCK pointers.
	 */
	scsiqp = (ADV_SCSI_REQ_Q *)ADV_32BALIGN(&reqp->scsi_req_q);

	/*
	 * Initialize the structure.
	 */
	scsiqp->cntl = scsiqp->scsi_cntl = scsiqp->done_status = 0;

	/*
	 * Set the ADV_SCSI_REQ_Q 'srb_ptr' to point to the adv_req_t structure.
	 */
	scsiqp->srb_ptr = ADV_VADDR_TO_U32(reqp);

	/*
	 * Set the adv_req_t 'cmndp' to point to the struct scsi_cmnd structure.
	 */
	reqp->cmndp = scp;

	/*
	 * Build the ADV_SCSI_REQ_Q request.
	 */

	/* Set CDB length and copy it to the request structure.  */
	scsiqp->cdb_len = scp->cmd_len;
	/* Copy first 12 CDB bytes to cdb[]. */
	for (i = 0; i < scp->cmd_len && i < 12; i++) {
		scsiqp->cdb[i] = scp->cmnd[i];
	}
	/* Copy last 4 CDB bytes, if present, to cdb16[]. */
	for (; i < scp->cmd_len; i++) {
		scsiqp->cdb16[i - 12] = scp->cmnd[i];
	}

	scsiqp->target_id = scp->device->id;
	scsiqp->target_lun = scp->device->lun;

	scsiqp->sense_addr = cpu_to_le32(virt_to_bus(&scp->sense_buffer[0]));
	scsiqp->sense_len = SCSI_SENSE_BUFFERSIZE;

	/* Build ADV_SCSI_REQ_Q */

	use_sg = scsi_dma_map(scp);
	if (use_sg == 0) {
		/* Zero-length transfer */
		reqp->sgblkp = NULL;
		scsiqp->data_cnt = 0;
		scsiqp->vdata_addr = NULL;

		scsiqp->data_addr = 0;
		scsiqp->sg_list_ptr = NULL;
		scsiqp->sg_real_addr = 0;
	} else {
		if (use_sg > ADV_MAX_SG_LIST) {
			scmd_printk(KERN_ERR, scp, "use_sg %d > "
				   "ADV_MAX_SG_LIST %d\n", use_sg,
				   scp->device->host->sg_tablesize);
			scsi_dma_unmap(scp);
			scp->result = HOST_BYTE(DID_ERROR);

			/*
			 * Free the 'adv_req_t' structure by adding it back
			 * to the board free list.
			 */
			reqp->next_reqp = boardp->adv_reqp;
			boardp->adv_reqp = reqp;

			return ASC_ERROR;
		}

		scsiqp->data_cnt = cpu_to_le32(scsi_bufflen(scp));

		ret = adv_get_sglist(boardp, reqp, scp, use_sg);
		if (ret != ADV_SUCCESS) {
			/*
			 * Free the adv_req_t structure by adding it back to
			 * the board free list.
			 */
			reqp->next_reqp = boardp->adv_reqp;
			boardp->adv_reqp = reqp;

			return ret;
		}

		ASC_STATS_ADD(scp->device->host, xfer_elem, use_sg);
	}

	ASC_STATS(scp->device->host, xfer_cnt);

	ASC_DBG_PRT_ADV_SCSI_REQ_Q(2, scsiqp);
	ASC_DBG_PRT_CDB(1, scp->cmnd, scp->cmd_len);

	*adv_scsiqpp = scsiqp;

	return ASC_NOERROR;
}

static int AscSgListToQueue(int sg_list)
{
	int n_sg_list_qs;

	n_sg_list_qs = ((sg_list - 1) / ASC_SG_LIST_PER_Q);
	if (((sg_list - 1) % ASC_SG_LIST_PER_Q) != 0)
		n_sg_list_qs++;
	return n_sg_list_qs + 1;
}

static uint
AscGetNumOfFreeQueue(ASC_DVC_VAR *asc_dvc, uchar target_ix, uchar n_qs)
{
	uint cur_used_qs;
	uint cur_free_qs;
	ASC_SCSI_BIT_ID_TYPE target_id;
	uchar tid_no;

	target_id = ASC_TIX_TO_TARGET_ID(target_ix);
	tid_no = ASC_TIX_TO_TID(target_ix);
	if ((asc_dvc->unit_not_ready & target_id) ||
	    (asc_dvc->queue_full_or_busy & target_id)) {
		return 0;
	}
	if (n_qs == 1) {
		cur_used_qs = (uint) asc_dvc->cur_total_qng +
		    (uint) asc_dvc->last_q_shortage + (uint) ASC_MIN_FREE_Q;
	} else {
		cur_used_qs = (uint) asc_dvc->cur_total_qng +
		    (uint) ASC_MIN_FREE_Q;
	}
	if ((uint) (cur_used_qs + n_qs) <= (uint) asc_dvc->max_total_qng) {
		cur_free_qs = (uint) asc_dvc->max_total_qng - cur_used_qs;
		if (asc_dvc->cur_dvc_qng[tid_no] >=
		    asc_dvc->max_dvc_qng[tid_no]) {
			return 0;
		}
		return cur_free_qs;
	}
	if (n_qs > 1) {
		if ((n_qs > asc_dvc->last_q_shortage)
		    && (n_qs <= (asc_dvc->max_total_qng - ASC_MIN_FREE_Q))) {
			asc_dvc->last_q_shortage = n_qs;
		}
	}
	return 0;
}

static uchar AscAllocFreeQueue(PortAddr iop_base, uchar free_q_head)
{
	ushort q_addr;
	uchar next_qp;
	uchar q_status;

	q_addr = ASC_QNO_TO_QADDR(free_q_head);
	q_status = (uchar)AscReadLramByte(iop_base,
					  (ushort)(q_addr +
						   ASC_SCSIQ_B_STATUS));
	next_qp = AscReadLramByte(iop_base, (ushort)(q_addr + ASC_SCSIQ_B_FWD));
	if (((q_status & QS_READY) == 0) && (next_qp != ASC_QLINK_END))
		return next_qp;
	return ASC_QLINK_END;
}

static uchar
AscAllocMultipleFreeQueue(PortAddr iop_base, uchar free_q_head, uchar n_free_q)
{
	uchar i;

	for (i = 0; i < n_free_q; i++) {
		free_q_head = AscAllocFreeQueue(iop_base, free_q_head);
		if (free_q_head == ASC_QLINK_END)
			break;
	}
	return free_q_head;
}

/*
 * void
 * DvcPutScsiQ(PortAddr iop_base, ushort s_addr, uchar *outbuf, int words)
 *
 * Calling/Exit State:
 *    none
 *
 * Description:
 *     Output an ASC_SCSI_Q structure to the chip
 */
static void
DvcPutScsiQ(PortAddr iop_base, ushort s_addr, uchar *outbuf, int words)
{
	int i;

	ASC_DBG_PRT_HEX(2, "DvcPutScsiQ", outbuf, 2 * words);
	AscSetChipLramAddr(iop_base, s_addr);
	for (i = 0; i < 2 * words; i += 2) {
		if (i == 4 || i == 20) {
			continue;
		}
		outpw(iop_base + IOP_RAM_DATA,
		      ((ushort)outbuf[i + 1] << 8) | outbuf[i]);
	}
}

static int AscPutReadyQueue(ASC_DVC_VAR *asc_dvc, ASC_SCSI_Q *scsiq, uchar q_no)
{
	ushort q_addr;
	uchar tid_no;
	uchar sdtr_data;
	uchar syn_period_ix;
	uchar syn_offset;
	PortAddr iop_base;

	iop_base = asc_dvc->iop_base;
	if (((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) &&
	    ((asc_dvc->sdtr_done & scsiq->q1.target_id) == 0)) {
		tid_no = ASC_TIX_TO_TID(scsiq->q2.target_ix);
		sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
		syn_period_ix =
		    (sdtr_data >> 4) & (asc_dvc->max_sdtr_index - 1);
		syn_offset = sdtr_data & ASC_SYN_MAX_OFFSET;
		AscMsgOutSDTR(asc_dvc,
			      asc_dvc->sdtr_period_tbl[syn_period_ix],
			      syn_offset);
		scsiq->q1.cntl |= QC_MSG_OUT;
	}
	q_addr = ASC_QNO_TO_QADDR(q_no);
	if ((scsiq->q1.target_id & asc_dvc->use_tagged_qng) == 0) {
		scsiq->q2.tag_code &= ~MSG_SIMPLE_TAG;
	}
	scsiq->q1.status = QS_FREE;
	AscMemWordCopyPtrToLram(iop_base,
				q_addr + ASC_SCSIQ_CDB_BEG,
				(uchar *)scsiq->cdbptr, scsiq->q2.cdb_len >> 1);

	DvcPutScsiQ(iop_base,
		    q_addr + ASC_SCSIQ_CPY_BEG,
		    (uchar *)&scsiq->q1.cntl,
		    ((sizeof(ASC_SCSIQ_1) + sizeof(ASC_SCSIQ_2)) / 2) - 1);
	AscWriteLramWord(iop_base,
			 (ushort)(q_addr + (ushort)ASC_SCSIQ_B_STATUS),
			 (ushort)(((ushort)scsiq->q1.
				   q_no << 8) | (ushort)QS_READY));
	return 1;
}

static int
AscPutReadySgListQueue(ASC_DVC_VAR *asc_dvc, ASC_SCSI_Q *scsiq, uchar q_no)
{
	int sta;
	int i;
	ASC_SG_HEAD *sg_head;
	ASC_SG_LIST_Q scsi_sg_q;
	ASC_DCNT saved_data_addr;
	ASC_DCNT saved_data_cnt;
	PortAddr iop_base;
	ushort sg_list_dwords;
	ushort sg_index;
	ushort sg_entry_cnt;
	ushort q_addr;
	uchar next_qp;

	iop_base = asc_dvc->iop_base;
	sg_head = scsiq->sg_head;
	saved_data_addr = scsiq->q1.data_addr;
	saved_data_cnt = scsiq->q1.data_cnt;
	scsiq->q1.data_addr = (ASC_PADDR) sg_head->sg_list[0].addr;
	scsiq->q1.data_cnt = (ASC_DCNT) sg_head->sg_list[0].bytes;
#if CC_VERY_LONG_SG_LIST
	/*
	 * If sg_head->entry_cnt is greater than ASC_MAX_SG_LIST
	 * then not all SG elements will fit in the allocated queues.
	 * The rest of the SG elements will be copied when the RISC
	 * completes the SG elements that fit and halts.
	 */
	if (sg_head->entry_cnt > ASC_MAX_SG_LIST) {
		/*
		 * Set sg_entry_cnt to be the number of SG elements that
		 * will fit in the allocated SG queues. It is minus 1, because
		 * the first SG element is handled above. ASC_MAX_SG_LIST is
		 * already inflated by 1 to account for this. For example it
		 * may be 50 which is 1 + 7 queues * 7 SG elements.
		 */
		sg_entry_cnt = ASC_MAX_SG_LIST - 1;

		/*
		 * Keep track of remaining number of SG elements that will
		 * need to be handled from a_isr.c.
		 */
		scsiq->remain_sg_entry_cnt =
		    sg_head->entry_cnt - ASC_MAX_SG_LIST;
	} else {
#endif /* CC_VERY_LONG_SG_LIST */
		/*
		 * Set sg_entry_cnt to be the number of SG elements that
		 * will fit in the allocated SG queues. It is minus 1, because
		 * the first SG element is handled above.
		 */
		sg_entry_cnt = sg_head->entry_cnt - 1;
#if CC_VERY_LONG_SG_LIST
	}
#endif /* CC_VERY_LONG_SG_LIST */
	if (sg_entry_cnt != 0) {
		scsiq->q1.cntl |= QC_SG_HEAD;
		q_addr = ASC_QNO_TO_QADDR(q_no);
		sg_index = 1;
		scsiq->q1.sg_queue_cnt = sg_head->queue_cnt;
		scsi_sg_q.sg_head_qp = q_no;
		scsi_sg_q.cntl = QCSG_SG_XFER_LIST;
		for (i = 0; i < sg_head->queue_cnt; i++) {
			scsi_sg_q.seq_no = i + 1;
			if (sg_entry_cnt > ASC_SG_LIST_PER_Q) {
				sg_list_dwords = (uchar)(ASC_SG_LIST_PER_Q * 2);
				sg_entry_cnt -= ASC_SG_LIST_PER_Q;
				if (i == 0) {
					scsi_sg_q.sg_list_cnt =
					    ASC_SG_LIST_PER_Q;
					scsi_sg_q.sg_cur_list_cnt =
					    ASC_SG_LIST_PER_Q;
				} else {
					scsi_sg_q.sg_list_cnt =
					    ASC_SG_LIST_PER_Q - 1;
					scsi_sg_q.sg_cur_list_cnt =
					    ASC_SG_LIST_PER_Q - 1;
				}
			} else {
#if CC_VERY_LONG_SG_LIST
				/*
				 * This is the last SG queue in the list of
				 * allocated SG queues. If there are more
				 * SG elements than will fit in the allocated
				 * queues, then set the QCSG_SG_XFER_MORE flag.
				 */
				if (sg_head->entry_cnt > ASC_MAX_SG_LIST) {
					scsi_sg_q.cntl |= QCSG_SG_XFER_MORE;
				} else {
#endif /* CC_VERY_LONG_SG_LIST */
					scsi_sg_q.cntl |= QCSG_SG_XFER_END;
#if CC_VERY_LONG_SG_LIST
				}
#endif /* CC_VERY_LONG_SG_LIST */
				sg_list_dwords = sg_entry_cnt << 1;
				if (i == 0) {
					scsi_sg_q.sg_list_cnt = sg_entry_cnt;
					scsi_sg_q.sg_cur_list_cnt =
					    sg_entry_cnt;
				} else {
					scsi_sg_q.sg_list_cnt =
					    sg_entry_cnt - 1;
					scsi_sg_q.sg_cur_list_cnt =
					    sg_entry_cnt - 1;
				}
				sg_entry_cnt = 0;
			}
			next_qp = AscReadLramByte(iop_base,
						  (ushort)(q_addr +
							   ASC_SCSIQ_B_FWD));
			scsi_sg_q.q_no = next_qp;
			q_addr = ASC_QNO_TO_QADDR(next_qp);
			AscMemWordCopyPtrToLram(iop_base,
						q_addr + ASC_SCSIQ_SGHD_CPY_BEG,
						(uchar *)&scsi_sg_q,
						sizeof(ASC_SG_LIST_Q) >> 1);
			AscMemDWordCopyPtrToLram(iop_base,
						 q_addr + ASC_SGQ_LIST_BEG,
						 (uchar *)&sg_head->
						 sg_list[sg_index],
						 sg_list_dwords);
			sg_index += ASC_SG_LIST_PER_Q;
			scsiq->next_sg_index = sg_index;
		}
	} else {
		scsiq->q1.cntl &= ~QC_SG_HEAD;
	}
	sta = AscPutReadyQueue(asc_dvc, scsiq, q_no);
	scsiq->q1.data_addr = saved_data_addr;
	scsiq->q1.data_cnt = saved_data_cnt;
	return (sta);
}

static int
AscSendScsiQueue(ASC_DVC_VAR *asc_dvc, ASC_SCSI_Q *scsiq, uchar n_q_required)
{
	PortAddr iop_base;
	uchar free_q_head;
	uchar next_qp;
	uchar tid_no;
	uchar target_ix;
	int sta;

	iop_base = asc_dvc->iop_base;
	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	sta = 0;
	free_q_head = (uchar)AscGetVarFreeQHead(iop_base);
	if (n_q_required > 1) {
		next_qp = AscAllocMultipleFreeQueue(iop_base, free_q_head,
						    (uchar)n_q_required);
		if (next_qp != ASC_QLINK_END) {
			asc_dvc->last_q_shortage = 0;
			scsiq->sg_head->queue_cnt = n_q_required - 1;
			scsiq->q1.q_no = free_q_head;
			sta = AscPutReadySgListQueue(asc_dvc, scsiq,
						     free_q_head);
		}
	} else if (n_q_required == 1) {
		next_qp = AscAllocFreeQueue(iop_base, free_q_head);
		if (next_qp != ASC_QLINK_END) {
			scsiq->q1.q_no = free_q_head;
			sta = AscPutReadyQueue(asc_dvc, scsiq, free_q_head);
		}
	}
	if (sta == 1) {
		AscPutVarFreeQHead(iop_base, next_qp);
		asc_dvc->cur_total_qng += n_q_required;
		asc_dvc->cur_dvc_qng[tid_no]++;
	}
	return sta;
}

#define ASC_SYN_OFFSET_ONE_DISABLE_LIST  16
static uchar _syn_offset_one_disable_cmd[ASC_SYN_OFFSET_ONE_DISABLE_LIST] = {
	INQUIRY,
	REQUEST_SENSE,
	READ_CAPACITY,
	READ_TOC,
	MODE_SELECT,
	MODE_SENSE,
	MODE_SELECT_10,
	MODE_SENSE_10,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF,
	0xFF
};

static int AscExeScsiQueue(ASC_DVC_VAR *asc_dvc, ASC_SCSI_Q *scsiq)
{
	PortAddr iop_base;
	int sta;
	int n_q_required;
	int disable_syn_offset_one_fix;
	int i;
	ASC_PADDR addr;
	ushort sg_entry_cnt = 0;
	ushort sg_entry_cnt_minus_one = 0;
	uchar target_ix;
	uchar tid_no;
	uchar sdtr_data;
	uchar extra_bytes;
	uchar scsi_cmd;
	uchar disable_cmd;
	ASC_SG_HEAD *sg_head;
	ASC_DCNT data_cnt;

	iop_base = asc_dvc->iop_base;
	sg_head = scsiq->sg_head;
	if (asc_dvc->err_code != 0)
		return (ERR);
	scsiq->q1.q_no = 0;
	if ((scsiq->q2.tag_code & ASC_TAG_FLAG_EXTRA_BYTES) == 0) {
		scsiq->q1.extra_bytes = 0;
	}
	sta = 0;
	target_ix = scsiq->q2.target_ix;
	tid_no = ASC_TIX_TO_TID(target_ix);
	n_q_required = 1;
	if (scsiq->cdbptr[0] == REQUEST_SENSE) {
		if ((asc_dvc->init_sdtr & scsiq->q1.target_id) != 0) {
			asc_dvc->sdtr_done &= ~scsiq->q1.target_id;
			sdtr_data = AscGetMCodeInitSDTRAtID(iop_base, tid_no);
			AscMsgOutSDTR(asc_dvc,
				      asc_dvc->
				      sdtr_period_tbl[(sdtr_data >> 4) &
						      (uchar)(asc_dvc->
							      max_sdtr_index -
							      1)],
				      (uchar)(sdtr_data & (uchar)
					      ASC_SYN_MAX_OFFSET));
			scsiq->q1.cntl |= (QC_MSG_OUT | QC_URGENT);
		}
	}
	if (asc_dvc->in_critical_cnt != 0) {
		AscSetLibErrorCode(asc_dvc, ASCQ_ERR_CRITICAL_RE_ENTRY);
		return (ERR);
	}
	asc_dvc->in_critical_cnt++;
	if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
		if ((sg_entry_cnt = sg_head->entry_cnt) == 0) {
			asc_dvc->in_critical_cnt--;
			return (ERR);
		}
#if !CC_VERY_LONG_SG_LIST
		if (sg_entry_cnt > ASC_MAX_SG_LIST) {
			asc_dvc->in_critical_cnt--;
			return (ERR);
		}
#endif /* !CC_VERY_LONG_SG_LIST */
		if (sg_entry_cnt == 1) {
			scsiq->q1.data_addr =
			    (ADV_PADDR)sg_head->sg_list[0].addr;
			scsiq->q1.data_cnt =
			    (ADV_DCNT)sg_head->sg_list[0].bytes;
			scsiq->q1.cntl &= ~(QC_SG_HEAD | QC_SG_SWAP_QUEUE);
		}
		sg_entry_cnt_minus_one = sg_entry_cnt - 1;
	}
	scsi_cmd = scsiq->cdbptr[0];
	disable_syn_offset_one_fix = FALSE;
	if ((asc_dvc->pci_fix_asyn_xfer & scsiq->q1.target_id) &&
	    !(asc_dvc->pci_fix_asyn_xfer_always & scsiq->q1.target_id)) {
		if (scsiq->q1.cntl & QC_SG_HEAD) {
			data_cnt = 0;
			for (i = 0; i < sg_entry_cnt; i++) {
				data_cnt +=
				    (ADV_DCNT)le32_to_cpu(sg_head->sg_list[i].
							  bytes);
			}
		} else {
			data_cnt = le32_to_cpu(scsiq->q1.data_cnt);
		}
		if (data_cnt != 0UL) {
			if (data_cnt < 512UL) {
				disable_syn_offset_one_fix = TRUE;
			} else {
				for (i = 0; i < ASC_SYN_OFFSET_ONE_DISABLE_LIST;
				     i++) {
					disable_cmd =
					    _syn_offset_one_disable_cmd[i];
					if (disable_cmd == 0xFF) {
						break;
					}
					if (scsi_cmd == disable_cmd) {
						disable_syn_offset_one_fix =
						    TRUE;
						break;
					}
				}
			}
		}
	}
	if (disable_syn_offset_one_fix) {
		scsiq->q2.tag_code &= ~MSG_SIMPLE_TAG;
		scsiq->q2.tag_code |= (ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX |
				       ASC_TAG_FLAG_DISABLE_DISCONNECT);
	} else {
		scsiq->q2.tag_code &= 0x27;
	}
	if ((scsiq->q1.cntl & QC_SG_HEAD) != 0) {
		if (asc_dvc->bug_fix_cntl) {
			if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if ((scsi_cmd == READ_6) ||
				    (scsi_cmd == READ_10)) {
					addr =
					    (ADV_PADDR)le32_to_cpu(sg_head->
								   sg_list
								   [sg_entry_cnt_minus_one].
								   addr) +
					    (ADV_DCNT)le32_to_cpu(sg_head->
								  sg_list
								  [sg_entry_cnt_minus_one].
								  bytes);
					extra_bytes =
					    (uchar)((ushort)addr & 0x0003);
					if ((extra_bytes != 0)
					    &&
					    ((scsiq->q2.
					      tag_code &
					      ASC_TAG_FLAG_EXTRA_BYTES)
					     == 0)) {
						scsiq->q2.tag_code |=
						    ASC_TAG_FLAG_EXTRA_BYTES;
						scsiq->q1.extra_bytes =
						    extra_bytes;
						data_cnt =
						    le32_to_cpu(sg_head->
								sg_list
								[sg_entry_cnt_minus_one].
								bytes);
						data_cnt -=
						    (ASC_DCNT) extra_bytes;
						sg_head->
						    sg_list
						    [sg_entry_cnt_minus_one].
						    bytes =
						    cpu_to_le32(data_cnt);
					}
				}
			}
		}
		sg_head->entry_to_copy = sg_head->entry_cnt;
#if CC_VERY_LONG_SG_LIST
		/*
		 * Set the sg_entry_cnt to the maximum possible. The rest of
		 * the SG elements will be copied when the RISC completes the
		 * SG elements that fit and halts.
		 */
		if (sg_entry_cnt > ASC_MAX_SG_LIST) {
			sg_entry_cnt = ASC_MAX_SG_LIST;
		}
#endif /* CC_VERY_LONG_SG_LIST */
		n_q_required = AscSgListToQueue(sg_entry_cnt);
		if ((AscGetNumOfFreeQueue(asc_dvc, target_ix, n_q_required) >=
		     (uint) n_q_required)
		    || ((scsiq->q1.cntl & QC_URGENT) != 0)) {
			if ((sta =
			     AscSendScsiQueue(asc_dvc, scsiq,
					      n_q_required)) == 1) {
				asc_dvc->in_critical_cnt--;
				return (sta);
			}
		}
	} else {
		if (asc_dvc->bug_fix_cntl) {
			if (asc_dvc->bug_fix_cntl & ASC_BUG_FIX_IF_NOT_DWB) {
				if ((scsi_cmd == READ_6) ||
				    (scsi_cmd == READ_10)) {
					addr =
					    le32_to_cpu(scsiq->q1.data_addr) +
					    le32_to_cpu(scsiq->q1.data_cnt);
					extra_bytes =
					    (uchar)((ushort)addr & 0x0003);
					if ((extra_bytes != 0)
					    &&
					    ((scsiq->q2.
					      tag_code &
					      ASC_TAG_FLAG_EXTRA_BYTES)
					     == 0)) {
						data_cnt =
						    le32_to_cpu(scsiq->q1.
								data_cnt);
						if (((ushort)data_cnt & 0x01FF)
						    == 0) {
							scsiq->q2.tag_code |=
							    ASC_TAG_FLAG_EXTRA_BYTES;
							data_cnt -= (ASC_DCNT)
							    extra_bytes;
							scsiq->q1.data_cnt =
							    cpu_to_le32
							    (data_cnt);
							scsiq->q1.extra_bytes =
							    extra_bytes;
						}
					}
				}
			}
		}
		n_q_required = 1;
		if ((AscGetNumOfFreeQueue(asc_dvc, target_ix, 1) >= 1) ||
		    ((scsiq->q1.cntl & QC_URGENT) != 0)) {
			if ((sta = AscSendScsiQueue(asc_dvc, scsiq,
						    n_q_required)) == 1) {
				asc_dvc->in_critical_cnt--;
				return (sta);
			}
		}
	}
	asc_dvc->in_critical_cnt--;
	return (sta);
}

/*
 * AdvExeScsiQueue() - Send a request to the RISC microcode program.
 *
 *   Allocate a carrier structure, point the carrier to the ADV_SCSI_REQ_Q,
 *   add the carrier to the ICQ (Initiator Command Queue), and tickle the
 *   RISC to notify it a new command is ready to be executed.
 *
 * If 'done_status' is not set to QD_DO_RETRY, then 'error_retry' will be
 * set to SCSI_MAX_RETRY.
 *
 * Multi-byte fields in the ASC_SCSI_REQ_Q that are used by the microcode
 * for DMA addresses or math operations are byte swapped to little-endian
 * order.
 *
 * Return:
 *      ADV_SUCCESS(1) - The request was successfully queued.
 *      ADV_BUSY(0) -    Resource unavailable; Retry again after pending
 *                       request completes.
 *      ADV_ERROR(-1) -  Invalid ADV_SCSI_REQ_Q request structure
 *                       host IC error.
 */
static int AdvExeScsiQueue(ADV_DVC_VAR *asc_dvc, ADV_SCSI_REQ_Q *scsiq)
{
	AdvPortAddr iop_base;
	ADV_PADDR req_paddr;
	ADV_CARR_T *new_carrp;

	/*
	 * The ADV_SCSI_REQ_Q 'target_id' field should never exceed ADV_MAX_TID.
	 */
	if (scsiq->target_id > ADV_MAX_TID) {
		scsiq->host_status = QHSTA_M_INVALID_DEVICE;
		scsiq->done_status = QD_WITH_ERROR;
		return ADV_ERROR;
	}

	iop_base = asc_dvc->iop_base;

	/*
	 * Allocate a carrier ensuring at least one carrier always
	 * remains on the freelist and initialize fields.
	 */
	if ((new_carrp = asc_dvc->carr_freelist) == NULL) {
		return ADV_BUSY;
	}
	asc_dvc->carr_freelist = (ADV_CARR_T *)
	    ADV_U32_TO_VADDR(le32_to_cpu(new_carrp->next_vpa));
	asc_dvc->carr_pending_cnt++;

	/*
	 * Set the carrier to be a stopper by setting 'next_vpa'
	 * to the stopper value. The current stopper will be changed
	 * below to point to the new stopper.
	 */
	new_carrp->next_vpa = cpu_to_le32(ASC_CQ_STOPPER);

	/*
	 * Clear the ADV_SCSI_REQ_Q done flag.
	 */
	scsiq->a_flag &= ~ADV_SCSIQ_DONE;

	req_paddr = virt_to_bus(scsiq);
	BUG_ON(req_paddr & 31);
	/* Wait for assertion before making little-endian */
	req_paddr = cpu_to_le32(req_paddr);

	/* Save virtual and physical address of ADV_SCSI_REQ_Q and carrier. */
	scsiq->scsiq_ptr = cpu_to_le32(ADV_VADDR_TO_U32(scsiq));
	scsiq->scsiq_rptr = req_paddr;

	scsiq->carr_va = cpu_to_le32(ADV_VADDR_TO_U32(asc_dvc->icq_sp));
	/*
	 * Every ADV_CARR_T.carr_pa is byte swapped to little-endian
	 * order during initialization.
	 */
	scsiq->carr_pa = asc_dvc->icq_sp->carr_pa;

	/*
	 * Use the current stopper to send the ADV_SCSI_REQ_Q command to
	 * the microcode. The newly allocated stopper will become the new
	 * stopper.
	 */
	asc_dvc->icq_sp->areq_vpa = req_paddr;

	/*
	 * Set the 'next_vpa' pointer for the old stopper to be the
	 * physical address of the new stopper. The RISC can only
	 * follow physical addresses.
	 */
	asc_dvc->icq_sp->next_vpa = new_carrp->carr_pa;

	/*
	 * Set the host adapter stopper pointer to point to the new carrier.
	 */
	asc_dvc->icq_sp = new_carrp;

	if (asc_dvc->chip_type == ADV_CHIP_ASC3550 ||
	    asc_dvc->chip_type == ADV_CHIP_ASC38C0800) {
		/*
		 * Tickle the RISC to tell it to read its Command Queue Head pointer.
		 */
		AdvWriteByteRegister(iop_base, IOPB_TICKLE, ADV_TICKLE_A);
		if (asc_dvc->chip_type == ADV_CHIP_ASC3550) {
			/*
			 * Clear the tickle value. In the ASC-3550 the RISC flag
			 * command 'clr_tickle_a' does not work unless the host
			 * value is cleared.
			 */
			AdvWriteByteRegister(iop_base, IOPB_TICKLE,
					     ADV_TICKLE_NOP);
		}
	} else if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600) {
		/*
		 * Notify the RISC a carrier is ready by writing the physical
		 * address of the new carrier stopper to the COMMA register.
		 */
		AdvWriteDWordRegister(iop_base, IOPDW_COMMA,
				      le32_to_cpu(new_carrp->carr_pa));
	}

	return ADV_SUCCESS;
}

/*
 * Execute a single 'Scsi_Cmnd'.
 */
static int asc_execute_scsi_cmnd(struct scsi_cmnd *scp)
{
	int ret, err_code;
	struct asc_board *boardp = shost_priv(scp->device->host);

	ASC_DBG(1, "scp 0x%p\n", scp);

	if (ASC_NARROW_BOARD(boardp)) {
		ASC_DVC_VAR *asc_dvc = &boardp->dvc_var.asc_dvc_var;
		struct asc_scsi_q asc_scsi_q;

		/* asc_build_req() can not return ASC_BUSY. */
		ret = asc_build_req(boardp, scp, &asc_scsi_q);
		if (ret == ASC_ERROR) {
			ASC_STATS(scp->device->host, build_error);
			return ASC_ERROR;
		}

		ret = AscExeScsiQueue(asc_dvc, &asc_scsi_q);
		kfree(asc_scsi_q.sg_head);
		err_code = asc_dvc->err_code;
	} else {
		ADV_DVC_VAR *adv_dvc = &boardp->dvc_var.adv_dvc_var;
		ADV_SCSI_REQ_Q *adv_scsiqp;

		switch (adv_build_req(boardp, scp, &adv_scsiqp)) {
		case ASC_NOERROR:
			ASC_DBG(3, "adv_build_req ASC_NOERROR\n");
			break;
		case ASC_BUSY:
			ASC_DBG(1, "adv_build_req ASC_BUSY\n");
			/*
			 * The asc_stats fields 'adv_build_noreq' and
			 * 'adv_build_nosg' count wide board busy conditions.
			 * They are updated in adv_build_req and
			 * adv_get_sglist, respectively.
			 */
			return ASC_BUSY;
		case ASC_ERROR:
		default:
			ASC_DBG(1, "adv_build_req ASC_ERROR\n");
			ASC_STATS(scp->device->host, build_error);
			return ASC_ERROR;
		}

		ret = AdvExeScsiQueue(adv_dvc, adv_scsiqp);
		err_code = adv_dvc->err_code;
	}

	switch (ret) {
	case ASC_NOERROR:
		ASC_STATS(scp->device->host, exe_noerror);
		/*
		 * Increment monotonically increasing per device
		 * successful request counter. Wrapping doesn't matter.
		 */
		boardp->reqcnt[scp->device->id]++;
		ASC_DBG(1, "ExeScsiQueue() ASC_NOERROR\n");
		break;
	case ASC_BUSY:
		ASC_STATS(scp->device->host, exe_busy);
		break;
	case ASC_ERROR:
		scmd_printk(KERN_ERR, scp, "ExeScsiQueue() ASC_ERROR, "
			"err_code 0x%x\n", err_code);
		ASC_STATS(scp->device->host, exe_error);
		scp->result = HOST_BYTE(DID_ERROR);
		break;
	default:
		scmd_printk(KERN_ERR, scp, "ExeScsiQueue() unknown, "
			"err_code 0x%x\n", err_code);
		ASC_STATS(scp->device->host, exe_unknown);
		scp->result = HOST_BYTE(DID_ERROR);
		break;
	}

	ASC_DBG(1, "end\n");
	return ret;
}

/*
 * advansys_queuecommand() - interrupt-driven I/O entrypoint.
 *
 * This function always returns 0. Command return status is saved
 * in the 'scp' result field.
 */
static int
advansys_queuecommand(struct scsi_cmnd *scp, void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *shost = scp->device->host;
	int asc_res, result = 0;

	ASC_STATS(shost, queuecommand);
	scp->scsi_done = done;

	asc_res = asc_execute_scsi_cmnd(scp);

	switch (asc_res) {
	case ASC_NOERROR:
		break;
	case ASC_BUSY:
		result = SCSI_MLQUEUE_HOST_BUSY;
		break;
	case ASC_ERROR:
	default:
		asc_scsi_done(scp);
		break;
	}

	return result;
}

static ushort __devinit AscGetEisaChipCfg(PortAddr iop_base)
{
	PortAddr eisa_cfg_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
	    (PortAddr) (ASC_EISA_CFG_IOP_MASK);
	return inpw(eisa_cfg_iop);
}

/*
 * Return the BIOS address of the adapter at the specified
 * I/O port and with the specified bus type.
 */
static unsigned short __devinit
AscGetChipBiosAddress(PortAddr iop_base, unsigned short bus_type)
{
	unsigned short cfg_lsw;
	unsigned short bios_addr;

	/*
	 * The PCI BIOS is re-located by the motherboard BIOS. Because
	 * of this the driver can not determine where a PCI BIOS is
	 * loaded and executes.
	 */
	if (bus_type & ASC_IS_PCI)
		return 0;

	if ((bus_type & ASC_IS_EISA) != 0) {
		cfg_lsw = AscGetEisaChipCfg(iop_base);
		cfg_lsw &= 0x000F;
		bios_addr = ASC_BIOS_MIN_ADDR + cfg_lsw * ASC_BIOS_BANK_SIZE;
		return bios_addr;
	}

	cfg_lsw = AscGetChipCfgLsw(iop_base);

	/*
	 *  ISA PnP uses the top bit as the 32K BIOS flag
	 */
	if (bus_type == ASC_IS_ISAPNP)
		cfg_lsw &= 0x7FFF;
	bios_addr = ASC_BIOS_MIN_ADDR + (cfg_lsw >> 12) * ASC_BIOS_BANK_SIZE;
	return bios_addr;
}

static uchar __devinit AscSetChipScsiID(PortAddr iop_base, uchar new_host_id)
{
	ushort cfg_lsw;

	if (AscGetChipScsiID(iop_base) == new_host_id) {
		return (new_host_id);
	}
	cfg_lsw = AscGetChipCfgLsw(iop_base);
	cfg_lsw &= 0xF8FF;
	cfg_lsw |= (ushort)((new_host_id & ASC_MAX_TID) << 8);
	AscSetChipCfgLsw(iop_base, cfg_lsw);
	return (AscGetChipScsiID(iop_base));
}

static unsigned char __devinit AscGetChipScsiCtrl(PortAddr iop_base)
{
	unsigned char sc;

	AscSetBank(iop_base, 1);
	sc = inp(iop_base + IOP_REG_SC);
	AscSetBank(iop_base, 0);
	return sc;
}

static unsigned char __devinit
AscGetChipVersion(PortAddr iop_base, unsigned short bus_type)
{
	if (bus_type & ASC_IS_EISA) {
		PortAddr eisa_iop;
		unsigned char revision;
		eisa_iop = (PortAddr) ASC_GET_EISA_SLOT(iop_base) |
		    (PortAddr) ASC_EISA_REV_IOP_MASK;
		revision = inp(eisa_iop);
		return ASC_CHIP_MIN_VER_EISA - 1 + revision;
	}
	return AscGetChipVerNo(iop_base);
}

#ifdef CONFIG_ISA
static void __devinit AscEnableIsaDma(uchar dma_channel)
{
	if (dma_channel < 4) {
		outp(0x000B, (ushort)(0xC0 | dma_channel));
		outp(0x000A, dma_channel);
	} else if (dma_channel < 8) {
		outp(0x00D6, (ushort)(0xC0 | (dma_channel - 4)));
		outp(0x00D4, (ushort)(dma_channel - 4));
	}
}
#endif /* CONFIG_ISA */

static int AscStopQueueExe(PortAddr iop_base)
{
	int count = 0;

	if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) == 0) {
		AscWriteLramByte(iop_base, ASCV_STOP_CODE_B,
				 ASC_STOP_REQ_RISC_STOP);
		do {
			if (AscReadLramByte(iop_base, ASCV_STOP_CODE_B) &
			    ASC_STOP_ACK_RISC_STOP) {
				return (1);
			}
			mdelay(100);
		} while (count++ < 20);
	}
	return (0);
}

static ASC_DCNT __devinit AscGetMaxDmaCount(ushort bus_type)
{
	if (bus_type & ASC_IS_ISA)
		return ASC_MAX_ISA_DMA_COUNT;
	else if (bus_type & (ASC_IS_EISA | ASC_IS_VL))
		return ASC_MAX_VL_DMA_COUNT;
	return ASC_MAX_PCI_DMA_COUNT;
}

#ifdef CONFIG_ISA
static ushort __devinit AscGetIsaDmaChannel(PortAddr iop_base)
{
	ushort channel;

	channel = AscGetChipCfgLsw(iop_base) & 0x0003;
	if (channel == 0x03)
		return (0);
	else if (channel == 0x00)
		return (7);
	return (channel + 4);
}

static ushort __devinit AscSetIsaDmaChannel(PortAddr iop_base, ushort dma_channel)
{
	ushort cfg_lsw;
	uchar value;

	if ((dma_channel >= 5) && (dma_channel <= 7)) {
		if (dma_channel == 7)
			value = 0x00;
		else
			value = dma_channel - 4;
		cfg_lsw = AscGetChipCfgLsw(iop_base) & 0xFFFC;
		cfg_lsw |= value;
		AscSetChipCfgLsw(iop_base, cfg_lsw);
		return (AscGetIsaDmaChannel(iop_base));
	}
	return 0;
}

static uchar __devinit AscGetIsaDmaSpeed(PortAddr iop_base)
{
	uchar speed_value;

	AscSetBank(iop_base, 1);
	speed_value = AscReadChipDmaSpeed(iop_base);
	speed_value &= 0x07;
	AscSetBank(iop_base, 0);
	return speed_value;
}

static uchar __devinit AscSetIsaDmaSpeed(PortAddr iop_base, uchar speed_value)
{
	speed_value &= 0x07;
	AscSetBank(iop_base, 1);
	AscWriteChipDmaSpeed(iop_base, speed_value);
	AscSetBank(iop_base, 0);
	return AscGetIsaDmaSpeed(iop_base);
}
#endif /* CONFIG_ISA */

static ushort __devinit AscInitAscDvcVar(ASC_DVC_VAR *asc_dvc)
{
	int i;
	PortAddr iop_base;
	ushort warn_code;
	uchar chip_version;

	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	asc_dvc->err_code = 0;
	if ((asc_dvc->bus_type &
	     (ASC_IS_ISA | ASC_IS_PCI | ASC_IS_EISA | ASC_IS_VL)) == 0) {
		asc_dvc->err_code |= ASC_IERR_NO_BUS_TYPE;
	}
	AscSetChipControl(iop_base, CC_HALT);
	AscSetChipStatus(iop_base, 0);
	asc_dvc->bug_fix_cntl = 0;
	asc_dvc->pci_fix_asyn_xfer = 0;
	asc_dvc->pci_fix_asyn_xfer_always = 0;
	/* asc_dvc->init_state initalized in AscInitGetConfig(). */
	asc_dvc->sdtr_done = 0;
	asc_dvc->cur_total_qng = 0;
	asc_dvc->is_in_int = 0;
	asc_dvc->in_critical_cnt = 0;
	asc_dvc->last_q_shortage = 0;
	asc_dvc->use_tagged_qng = 0;
	asc_dvc->no_scam = 0;
	asc_dvc->unit_not_ready = 0;
	asc_dvc->queue_full_or_busy = 0;
	asc_dvc->redo_scam = 0;
	asc_dvc->res2 = 0;
	asc_dvc->min_sdtr_index = 0;
	asc_dvc->cfg->can_tagged_qng = 0;
	asc_dvc->cfg->cmd_qng_enabled = 0;
	asc_dvc->dvc_cntl = ASC_DEF_DVC_CNTL;
	asc_dvc->init_sdtr = 0;
	asc_dvc->max_total_qng = ASC_DEF_MAX_TOTAL_QNG;
	asc_dvc->scsi_reset_wait = 3;
	asc_dvc->start_motor = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->max_dma_count = AscGetMaxDmaCount(asc_dvc->bus_type);
	asc_dvc->cfg->sdtr_enable = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->cfg->disc_enable = ASC_SCSI_WIDTH_BIT_SET;
	asc_dvc->cfg->chip_scsi_id = ASC_DEF_CHIP_SCSI_ID;
	chip_version = AscGetChipVersion(iop_base, asc_dvc->bus_type);
	asc_dvc->cfg->chip_version = chip_version;
	asc_dvc->sdtr_period_tbl = asc_syn_xfer_period;
	asc_dvc->max_sdtr_index = 7;
	if ((asc_dvc->bus_type & ASC_IS_PCI) &&
	    (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3150)) {
		asc_dvc->bus_type = ASC_IS_PCI_ULTRA;
		asc_dvc->sdtr_period_tbl = asc_syn_ultra_xfer_period;
		asc_dvc->max_sdtr_index = 15;
		if (chip_version == ASC_CHIP_VER_PCI_ULTRA_3150) {
			AscSetExtraControl(iop_base,
					   (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
		} else if (chip_version >= ASC_CHIP_VER_PCI_ULTRA_3050) {
			AscSetExtraControl(iop_base,
					   (SEC_ACTIVE_NEGATE |
					    SEC_ENABLE_FILTER));
		}
	}
	if (asc_dvc->bus_type == ASC_IS_PCI) {
		AscSetExtraControl(iop_base,
				   (SEC_ACTIVE_NEGATE | SEC_SLEW_RATE));
	}

	asc_dvc->cfg->isa_dma_speed = ASC_DEF_ISA_DMA_SPEED;
#ifdef CONFIG_ISA
	if ((asc_dvc->bus_type & ASC_IS_ISA) != 0) {
		if (chip_version >= ASC_CHIP_MIN_VER_ISA_PNP) {
			AscSetChipIFC(iop_base, IFC_INIT_DEFAULT);
			asc_dvc->bus_type = ASC_IS_ISAPNP;
		}
		asc_dvc->cfg->isa_dma_channel =
		    (uchar)AscGetIsaDmaChannel(iop_base);
	}
#endif /* CONFIG_ISA */
	for (i = 0; i <= ASC_MAX_TID; i++) {
		asc_dvc->cur_dvc_qng[i] = 0;
		asc_dvc->max_dvc_qng[i] = ASC_MAX_SCSI1_QNG;
		asc_dvc->scsiq_busy_head[i] = (ASC_SCSI_Q *)0L;
		asc_dvc->scsiq_busy_tail[i] = (ASC_SCSI_Q *)0L;
		asc_dvc->cfg->max_tag_qng[i] = ASC_MAX_INRAM_TAG_QNG;
	}
	return warn_code;
}

static int __devinit AscWriteEEPCmdReg(PortAddr iop_base, uchar cmd_reg)
{
	int retry;

	for (retry = 0; retry < ASC_EEP_MAX_RETRY; retry++) {
		unsigned char read_back;
		AscSetChipEEPCmd(iop_base, cmd_reg);
		mdelay(1);
		read_back = AscGetChipEEPCmd(iop_base);
		if (read_back == cmd_reg)
			return 1;
	}
	return 0;
}

static void __devinit AscWaitEEPRead(void)
{
	mdelay(1);
}

static ushort __devinit AscReadEEPWord(PortAddr iop_base, uchar addr)
{
	ushort read_wval;
	uchar cmd_reg;

	AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
	AscWaitEEPRead();
	cmd_reg = addr | ASC_EEP_CMD_READ;
	AscWriteEEPCmdReg(iop_base, cmd_reg);
	AscWaitEEPRead();
	read_wval = AscGetChipEEPData(iop_base);
	AscWaitEEPRead();
	return read_wval;
}

static ushort __devinit
AscGetEEPConfig(PortAddr iop_base, ASCEEP_CONFIG *cfg_buf, ushort bus_type)
{
	ushort wval;
	ushort sum;
	ushort *wbuf;
	int cfg_beg;
	int cfg_end;
	int uchar_end_in_config = ASC_EEP_MAX_DVC_ADDR - 2;
	int s_addr;

	wbuf = (ushort *)cfg_buf;
	sum = 0;
	/* Read two config words; Byte-swapping done by AscReadEEPWord(). */
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		*wbuf = AscReadEEPWord(iop_base, (uchar)s_addr);
		sum += *wbuf;
	}
	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}
	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
		wval = AscReadEEPWord(iop_base, (uchar)s_addr);
		if (s_addr <= uchar_end_in_config) {
			/*
			 * Swap all char fields - must unswap bytes already swapped
			 * by AscReadEEPWord().
			 */
			*wbuf = le16_to_cpu(wval);
		} else {
			/* Don't swap word field at the end - cntl field. */
			*wbuf = wval;
		}
		sum += wval;	/* Checksum treats all EEPROM data as words. */
	}
	/*
	 * Read the checksum word which will be compared against 'sum'
	 * by the caller. Word field already swapped.
	 */
	*wbuf = AscReadEEPWord(iop_base, (uchar)s_addr);
	return sum;
}

static int __devinit AscTestExternalLram(ASC_DVC_VAR *asc_dvc)
{
	PortAddr iop_base;
	ushort q_addr;
	ushort saved_word;
	int sta;

	iop_base = asc_dvc->iop_base;
	sta = 0;
	q_addr = ASC_QNO_TO_QADDR(241);
	saved_word = AscReadLramWord(iop_base, q_addr);
	AscSetChipLramAddr(iop_base, q_addr);
	AscSetChipLramData(iop_base, 0x55AA);
	mdelay(10);
	AscSetChipLramAddr(iop_base, q_addr);
	if (AscGetChipLramData(iop_base) == 0x55AA) {
		sta = 1;
		AscWriteLramWord(iop_base, q_addr, saved_word);
	}
	return (sta);
}

static void __devinit AscWaitEEPWrite(void)
{
	mdelay(20);
}

static int __devinit AscWriteEEPDataReg(PortAddr iop_base, ushort data_reg)
{
	ushort read_back;
	int retry;

	retry = 0;
	while (TRUE) {
		AscSetChipEEPData(iop_base, data_reg);
		mdelay(1);
		read_back = AscGetChipEEPData(iop_base);
		if (read_back == data_reg) {
			return (1);
		}
		if (retry++ > ASC_EEP_MAX_RETRY) {
			return (0);
		}
	}
}

static ushort __devinit
AscWriteEEPWord(PortAddr iop_base, uchar addr, ushort word_val)
{
	ushort read_wval;

	read_wval = AscReadEEPWord(iop_base, addr);
	if (read_wval != word_val) {
		AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_ABLE);
		AscWaitEEPRead();
		AscWriteEEPDataReg(iop_base, word_val);
		AscWaitEEPRead();
		AscWriteEEPCmdReg(iop_base,
				  (uchar)((uchar)ASC_EEP_CMD_WRITE | addr));
		AscWaitEEPWrite();
		AscWriteEEPCmdReg(iop_base, ASC_EEP_CMD_WRITE_DISABLE);
		AscWaitEEPRead();
		return (AscReadEEPWord(iop_base, addr));
	}
	return (read_wval);
}

static int __devinit
AscSetEEPConfigOnce(PortAddr iop_base, ASCEEP_CONFIG *cfg_buf, ushort bus_type)
{
	int n_error;
	ushort *wbuf;
	ushort word;
	ushort sum;
	int s_addr;
	int cfg_beg;
	int cfg_end;
	int uchar_end_in_config = ASC_EEP_MAX_DVC_ADDR - 2;

	wbuf = (ushort *)cfg_buf;
	n_error = 0;
	sum = 0;
	/* Write two config words; AscWriteEEPWord() will swap bytes. */
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		sum += *wbuf;
		if (*wbuf != AscWriteEEPWord(iop_base, (uchar)s_addr, *wbuf)) {
			n_error++;
		}
	}
	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}
	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
		if (s_addr <= uchar_end_in_config) {
			/*
			 * This is a char field. Swap char fields before they are
			 * swapped again by AscWriteEEPWord().
			 */
			word = cpu_to_le16(*wbuf);
			if (word !=
			    AscWriteEEPWord(iop_base, (uchar)s_addr, word)) {
				n_error++;
			}
		} else {
			/* Don't swap word field at the end - cntl field. */
			if (*wbuf !=
			    AscWriteEEPWord(iop_base, (uchar)s_addr, *wbuf)) {
				n_error++;
			}
		}
		sum += *wbuf;	/* Checksum calculated from word values. */
	}
	/* Write checksum word. It will be swapped by AscWriteEEPWord(). */
	*wbuf = sum;
	if (sum != AscWriteEEPWord(iop_base, (uchar)s_addr, sum)) {
		n_error++;
	}

	/* Read EEPROM back again. */
	wbuf = (ushort *)cfg_buf;
	/*
	 * Read two config words; Byte-swapping done by AscReadEEPWord().
	 */
	for (s_addr = 0; s_addr < 2; s_addr++, wbuf++) {
		if (*wbuf != AscReadEEPWord(iop_base, (uchar)s_addr)) {
			n_error++;
		}
	}
	if (bus_type & ASC_IS_VL) {
		cfg_beg = ASC_EEP_DVC_CFG_BEG_VL;
		cfg_end = ASC_EEP_MAX_DVC_ADDR_VL;
	} else {
		cfg_beg = ASC_EEP_DVC_CFG_BEG;
		cfg_end = ASC_EEP_MAX_DVC_ADDR;
	}
	for (s_addr = cfg_beg; s_addr <= (cfg_end - 1); s_addr++, wbuf++) {
		if (s_addr <= uchar_end_in_config) {
			/*
			 * Swap all char fields. Must unswap bytes already swapped
			 * by AscReadEEPWord().
			 */
			word =
			    le16_to_cpu(AscReadEEPWord
					(iop_base, (uchar)s_addr));
		} else {
			/* Don't swap word field at the end - cntl field. */
			word = AscReadEEPWord(iop_base, (uchar)s_addr);
		}
		if (*wbuf != word) {
			n_error++;
		}
	}
	/* Read checksum; Byte swapping not needed. */
	if (AscReadEEPWord(iop_base, (uchar)s_addr) != sum) {
		n_error++;
	}
	return n_error;
}

static int __devinit
AscSetEEPConfig(PortAddr iop_base, ASCEEP_CONFIG *cfg_buf, ushort bus_type)
{
	int retry;
	int n_error;

	retry = 0;
	while (TRUE) {
		if ((n_error = AscSetEEPConfigOnce(iop_base, cfg_buf,
						   bus_type)) == 0) {
			break;
		}
		if (++retry > ASC_EEP_MAX_RETRY) {
			break;
		}
	}
	return n_error;
}

static ushort __devinit AscInitFromEEP(ASC_DVC_VAR *asc_dvc)
{
	ASCEEP_CONFIG eep_config_buf;
	ASCEEP_CONFIG *eep_config;
	PortAddr iop_base;
	ushort chksum;
	ushort warn_code;
	ushort cfg_msw, cfg_lsw;
	int i;
	int write_eep = 0;

	iop_base = asc_dvc->iop_base;
	warn_code = 0;
	AscWriteLramWord(iop_base, ASCV_HALTCODE_W, 0x00FE);
	AscStopQueueExe(iop_base);
	if ((AscStopChip(iop_base) == FALSE) ||
	    (AscGetChipScsiCtrl(iop_base) != 0)) {
		asc_dvc->init_state |= ASC_INIT_RESET_SCSI_DONE;
		AscResetChipAndScsiBus(asc_dvc);
		mdelay(asc_dvc->scsi_reset_wait * 1000); /* XXX: msleep? */
	}
	if (AscIsChipHalted(iop_base) == FALSE) {
		asc_dvc->err_code |= ASC_IERR_START_STOP_CHIP;
		return (warn_code);
	}
	AscSetPCAddr(iop_base, ASC_MCODE_START_ADDR);
	if (AscGetPCAddr(iop_base) != ASC_MCODE_START_ADDR) {
		asc_dvc->err_code |= ASC_IERR_SET_PC_ADDR;
		return (warn_code);
	}
	eep_config = (ASCEEP_CONFIG *)&eep_config_buf;
	cfg_msw = AscGetChipCfgMsw(iop_base);
	cfg_lsw = AscGetChipCfgLsw(iop_base);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= ~ASC_CFG_MSW_CLR_MASK;
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		AscSetChipCfgMsw(iop_base, cfg_msw);
	}
	chksum = AscGetEEPConfig(iop_base, eep_config, asc_dvc->bus_type);
	ASC_DBG(1, "chksum 0x%x\n", chksum);
	if (chksum == 0) {
		chksum = 0xaa55;
	}
	if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
		if (asc_dvc->cfg->chip_version == 3) {
			if (eep_config->cfg_lsw != cfg_lsw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_lsw =
				    AscGetChipCfgLsw(iop_base);
			}
			if (eep_config->cfg_msw != cfg_msw) {
				warn_code |= ASC_WARN_EEPROM_RECOVER;
				eep_config->cfg_msw =
				    AscGetChipCfgMsw(iop_base);
			}
		}
	}
	eep_config->cfg_msw &= ~ASC_CFG_MSW_CLR_MASK;
	eep_config->cfg_lsw |= ASC_CFG0_HOST_INT_ON;
	ASC_DBG(1, "eep_config->chksum 0x%x\n", eep_config->chksum);
	if (chksum != eep_config->chksum) {
		if (AscGetChipVersion(iop_base, asc_dvc->bus_type) ==
		    ASC_CHIP_VER_PCI_ULTRA_3050) {
			ASC_DBG(1, "chksum error ignored; EEPROM-less board\n");
			eep_config->init_sdtr = 0xFF;
			eep_config->disc_enable = 0xFF;
			eep_config->start_motor = 0xFF;
			eep_config->use_cmd_qng = 0;
			eep_config->max_total_qng = 0xF0;
			eep_config->max_tag_qng = 0x20;
			eep_config->cntl = 0xBFFF;
			ASC_EEP_SET_CHIP_ID(eep_config, 7);
			eep_config->no_scam = 0;
			eep_config->adapter_info[0] = 0;
			eep_config->adapter_info[1] = 0;
			eep_config->adapter_info[2] = 0;
			eep_config->adapter_info[3] = 0;
			eep_config->adapter_info[4] = 0;
			/* Indicate EEPROM-less board. */
			eep_config->adapter_info[5] = 0xBB;
		} else {
			ASC_PRINT
			    ("AscInitFromEEP: EEPROM checksum error; Will try to re-write EEPROM.\n");
			write_eep = 1;
			warn_code |= ASC_WARN_EEPROM_CHKSUM;
		}
	}
	asc_dvc->cfg->sdtr_enable = eep_config->init_sdtr;
	asc_dvc->cfg->disc_enable = eep_config->disc_enable;
	asc_dvc->cfg->cmd_qng_enabled = eep_config->use_cmd_qng;
	asc_dvc->cfg->isa_dma_speed = ASC_EEP_GET_DMA_SPD(eep_config);
	asc_dvc->start_motor = eep_config->start_motor;
	asc_dvc->dvc_cntl = eep_config->cntl;
	asc_dvc->no_scam = eep_config->no_scam;
	asc_dvc->cfg->adapter_info[0] = eep_config->adapter_info[0];
	asc_dvc->cfg->adapter_info[1] = eep_config->adapter_info[1];
	asc_dvc->cfg->adapter_info[2] = eep_config->adapter_info[2];
	asc_dvc->cfg->adapter_info[3] = eep_config->adapter_info[3];
	asc_dvc->cfg->adapter_info[4] = eep_config->adapter_info[4];
	asc_dvc->cfg->adapter_info[5] = eep_config->adapter_info[5];
	if (!AscTestExternalLram(asc_dvc)) {
		if (((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) ==
		     ASC_IS_PCI_ULTRA)) {
			eep_config->max_total_qng =
			    ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng =
			    ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG;
		} else {
			eep_config->cfg_msw |= 0x0800;
			cfg_msw |= 0x0800;
			AscSetChipCfgMsw(iop_base, cfg_msw);
			eep_config->max_total_qng = ASC_MAX_PCI_INRAM_TOTAL_QNG;
			eep_config->max_tag_qng = ASC_MAX_INRAM_TAG_QNG;
		}
	} else {
	}
	if (eep_config->max_total_qng < ASC_MIN_TOTAL_QNG) {
		eep_config->max_total_qng = ASC_MIN_TOTAL_QNG;
	}
	if (eep_config->max_total_qng > ASC_MAX_TOTAL_QNG) {
		eep_config->max_total_qng = ASC_MAX_TOTAL_QNG;
	}
	if (eep_config->max_tag_qng > eep_config->max_total_qng) {
		eep_config->max_tag_qng = eep_config->max_total_qng;
	}
	if (eep_config->max_tag_qng < ASC_MIN_TAG_Q_PER_DVC) {
		eep_config->max_tag_qng = ASC_MIN_TAG_Q_PER_DVC;
	}
	asc_dvc->max_total_qng = eep_config->max_total_qng;
	if ((eep_config->use_cmd_qng & eep_config->disc_enable) !=
	    eep_config->use_cmd_qng) {
		eep_config->disc_enable = eep_config->use_cmd_qng;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	ASC_EEP_SET_CHIP_ID(eep_config,
			    ASC_EEP_GET_CHIP_ID(eep_config) & ASC_MAX_TID);
	asc_dvc->cfg->chip_scsi_id = ASC_EEP_GET_CHIP_ID(eep_config);
	if (((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) &&
	    !(asc_dvc->dvc_cntl & ASC_CNTL_SDTR_ENABLE_ULTRA)) {
		asc_dvc->min_sdtr_index = ASC_SDTR_ULTRA_PCI_10MB_INDEX;
	}

	for (i = 0; i <= ASC_MAX_TID; i++) {
		asc_dvc->dos_int13_table[i] = eep_config->dos_int13_table[i];
		asc_dvc->cfg->max_tag_qng[i] = eep_config->max_tag_qng;
		asc_dvc->cfg->sdtr_period_offset[i] =
		    (uchar)(ASC_DEF_SDTR_OFFSET |
			    (asc_dvc->min_sdtr_index << 4));
	}
	eep_config->cfg_msw = AscGetChipCfgMsw(iop_base);
	if (write_eep) {
		if ((i = AscSetEEPConfig(iop_base, eep_config,
				     asc_dvc->bus_type)) != 0) {
			ASC_PRINT1
			    ("AscInitFromEEP: Failed to re-write EEPROM with %d errors.\n",
			     i);
		} else {
			ASC_PRINT
			    ("AscInitFromEEP: Successfully re-wrote EEPROM.\n");
		}
	}
	return (warn_code);
}

static int __devinit AscInitGetConfig(struct Scsi_Host *shost)
{
	struct asc_board *board = shost_priv(shost);
	ASC_DVC_VAR *asc_dvc = &board->dvc_var.asc_dvc_var;
	unsigned short warn_code = 0;

	asc_dvc->init_state = ASC_INIT_STATE_BEG_GET_CFG;
	if (asc_dvc->err_code != 0)
		return asc_dvc->err_code;

	if (AscFindSignature(asc_dvc->iop_base)) {
		warn_code |= AscInitAscDvcVar(asc_dvc);
		warn_code |= AscInitFromEEP(asc_dvc);
		asc_dvc->init_state |= ASC_INIT_STATE_END_GET_CFG;
		if (asc_dvc->scsi_reset_wait > ASC_MAX_SCSI_RESET_WAIT)
			asc_dvc->scsi_reset_wait = ASC_MAX_SCSI_RESET_WAIT;
	} else {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
	}

	switch (warn_code) {
	case 0:	/* No error */
		break;
	case ASC_WARN_IO_PORT_ROTATE:
		shost_printk(KERN_WARNING, shost, "I/O port address "
				"modified\n");
		break;
	case ASC_WARN_AUTO_CONFIG:
		shost_printk(KERN_WARNING, shost, "I/O port increment switch "
				"enabled\n");
		break;
	case ASC_WARN_EEPROM_CHKSUM:
		shost_printk(KERN_WARNING, shost, "EEPROM checksum error\n");
		break;
	case ASC_WARN_IRQ_MODIFIED:
		shost_printk(KERN_WARNING, shost, "IRQ modified\n");
		break;
	case ASC_WARN_CMD_QNG_CONFLICT:
		shost_printk(KERN_WARNING, shost, "tag queuing enabled w/o "
				"disconnects\n");
		break;
	default:
		shost_printk(KERN_WARNING, shost, "unknown warning: 0x%x\n",
				warn_code);
		break;
	}

	if (asc_dvc->err_code != 0)
		shost_printk(KERN_ERR, shost, "error 0x%x at init_state "
			"0x%x\n", asc_dvc->err_code, asc_dvc->init_state);

	return asc_dvc->err_code;
}

static int __devinit AscInitSetConfig(struct pci_dev *pdev, struct Scsi_Host *shost)
{
	struct asc_board *board = shost_priv(shost);
	ASC_DVC_VAR *asc_dvc = &board->dvc_var.asc_dvc_var;
	PortAddr iop_base = asc_dvc->iop_base;
	unsigned short cfg_msw;
	unsigned short warn_code = 0;

	asc_dvc->init_state |= ASC_INIT_STATE_BEG_SET_CFG;
	if (asc_dvc->err_code != 0)
		return asc_dvc->err_code;
	if (!AscFindSignature(asc_dvc->iop_base)) {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
		return asc_dvc->err_code;
	}

	cfg_msw = AscGetChipCfgMsw(iop_base);
	if ((cfg_msw & ASC_CFG_MSW_CLR_MASK) != 0) {
		cfg_msw &= ~ASC_CFG_MSW_CLR_MASK;
		warn_code |= ASC_WARN_CFG_MSW_RECOVER;
		AscSetChipCfgMsw(iop_base, cfg_msw);
	}
	if ((asc_dvc->cfg->cmd_qng_enabled & asc_dvc->cfg->disc_enable) !=
	    asc_dvc->cfg->cmd_qng_enabled) {
		asc_dvc->cfg->disc_enable = asc_dvc->cfg->cmd_qng_enabled;
		warn_code |= ASC_WARN_CMD_QNG_CONFLICT;
	}
	if (AscGetChipStatus(iop_base) & CSW_AUTO_CONFIG) {
		warn_code |= ASC_WARN_AUTO_CONFIG;
	}
#ifdef CONFIG_PCI
	if (asc_dvc->bus_type & ASC_IS_PCI) {
		cfg_msw &= 0xFFC0;
		AscSetChipCfgMsw(iop_base, cfg_msw);
		if ((asc_dvc->bus_type & ASC_IS_PCI_ULTRA) == ASC_IS_PCI_ULTRA) {
		} else {
			if ((pdev->device == PCI_DEVICE_ID_ASP_1200A) ||
			    (pdev->device == PCI_DEVICE_ID_ASP_ABP940)) {
				asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_IF_NOT_DWB;
				asc_dvc->bug_fix_cntl |=
				    ASC_BUG_FIX_ASYN_USE_SYN;
			}
		}
	} else
#endif /* CONFIG_PCI */
	if (asc_dvc->bus_type == ASC_IS_ISAPNP) {
		if (AscGetChipVersion(iop_base, asc_dvc->bus_type)
		    == ASC_CHIP_VER_ASYN_BUG) {
			asc_dvc->bug_fix_cntl |= ASC_BUG_FIX_ASYN_USE_SYN;
		}
	}
	if (AscSetChipScsiID(iop_base, asc_dvc->cfg->chip_scsi_id) !=
	    asc_dvc->cfg->chip_scsi_id) {
		asc_dvc->err_code |= ASC_IERR_SET_SCSI_ID;
	}
#ifdef CONFIG_ISA
	if (asc_dvc->bus_type & ASC_IS_ISA) {
		AscSetIsaDmaChannel(iop_base, asc_dvc->cfg->isa_dma_channel);
		AscSetIsaDmaSpeed(iop_base, asc_dvc->cfg->isa_dma_speed);
	}
#endif /* CONFIG_ISA */

	asc_dvc->init_state |= ASC_INIT_STATE_END_SET_CFG;

	switch (warn_code) {
	case 0:	/* No error. */
		break;
	case ASC_WARN_IO_PORT_ROTATE:
		shost_printk(KERN_WARNING, shost, "I/O port address "
				"modified\n");
		break;
	case ASC_WARN_AUTO_CONFIG:
		shost_printk(KERN_WARNING, shost, "I/O port increment switch "
				"enabled\n");
		break;
	case ASC_WARN_EEPROM_CHKSUM:
		shost_printk(KERN_WARNING, shost, "EEPROM checksum error\n");
		break;
	case ASC_WARN_IRQ_MODIFIED:
		shost_printk(KERN_WARNING, shost, "IRQ modified\n");
		break;
	case ASC_WARN_CMD_QNG_CONFLICT:
		shost_printk(KERN_WARNING, shost, "tag queuing w/o "
				"disconnects\n");
		break;
	default:
		shost_printk(KERN_WARNING, shost, "unknown warning: 0x%x\n",
				warn_code);
		break;
	}

	if (asc_dvc->err_code != 0)
		shost_printk(KERN_ERR, shost, "error 0x%x at init_state "
			"0x%x\n", asc_dvc->err_code, asc_dvc->init_state);

	return asc_dvc->err_code;
}

/*
 * EEPROM Configuration.
 *
 * All drivers should use this structure to set the default EEPROM
 * configuration. The BIOS now uses this structure when it is built.
 * Additional structure information can be found in a_condor.h where
 * the structure is defined.
 *
 * The *_Field_IsChar structs are needed to correct for endianness.
 * These values are read from the board 16 bits at a time directly
 * into the structs. Because some fields are char, the values will be
 * in the wrong order. The *_Field_IsChar tells when to flip the
 * bytes. Data read and written to PCI memory is automatically swapped
 * on big-endian platforms so char fields read as words are actually being
 * unswapped on big-endian platforms.
 */
static ADVEEP_3550_CONFIG Default_3550_EEPROM_Config __devinitdata = {
	ADV_EEPROM_BIOS_ENABLE,	/* cfg_lsw */
	0x0000,			/* cfg_msw */
	0xFFFF,			/* disc_enable */
	0xFFFF,			/* wdtr_able */
	0xFFFF,			/* sdtr_able */
	0xFFFF,			/* start_motor */
	0xFFFF,			/* tagqng_able */
	0xFFFF,			/* bios_scan */
	0,			/* scam_tolerant */
	7,			/* adapter_scsi_id */
	0,			/* bios_boot_delay */
	3,			/* scsi_reset_delay */
	0,			/* bios_id_lun */
	0,			/* termination */
	0,			/* reserved1 */
	0xFFE7,			/* bios_ctrl */
	0xFFFF,			/* ultra_able */
	0,			/* reserved2 */
	ASC_DEF_MAX_HOST_QNG,	/* max_host_qng */
	ASC_DEF_MAX_DVC_QNG,	/* max_dvc_qng */
	0,			/* dvc_cntl */
	0,			/* bug_fix */
	0,			/* serial_number_word1 */
	0,			/* serial_number_word2 */
	0,			/* serial_number_word3 */
	0,			/* check_sum */
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	,			/* oem_name[16] */
	0,			/* dvc_err_code */
	0,			/* adv_err_code */
	0,			/* adv_err_addr */
	0,			/* saved_dvc_err_code */
	0,			/* saved_adv_err_code */
	0,			/* saved_adv_err_addr */
	0			/* num_of_err */
};

static ADVEEP_3550_CONFIG ADVEEP_3550_Config_Field_IsChar __devinitdata = {
	0,			/* cfg_lsw */
	0,			/* cfg_msw */
	0,			/* -disc_enable */
	0,			/* wdtr_able */
	0,			/* sdtr_able */
	0,			/* start_motor */
	0,			/* tagqng_able */
	0,			/* bios_scan */
	0,			/* scam_tolerant */
	1,			/* adapter_scsi_id */
	1,			/* bios_boot_delay */
	1,			/* scsi_reset_delay */
	1,			/* bios_id_lun */
	1,			/* termination */
	1,			/* reserved1 */
	0,			/* bios_ctrl */
	0,			/* ultra_able */
	0,			/* reserved2 */
	1,			/* max_host_qng */
	1,			/* max_dvc_qng */
	0,			/* dvc_cntl */
	0,			/* bug_fix */
	0,			/* serial_number_word1 */
	0,			/* serial_number_word2 */
	0,			/* serial_number_word3 */
	0,			/* check_sum */
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
	,			/* oem_name[16] */
	0,			/* dvc_err_code */
	0,			/* adv_err_code */
	0,			/* adv_err_addr */
	0,			/* saved_dvc_err_code */
	0,			/* saved_adv_err_code */
	0,			/* saved_adv_err_addr */
	0			/* num_of_err */
};

static ADVEEP_38C0800_CONFIG Default_38C0800_EEPROM_Config __devinitdata = {
	ADV_EEPROM_BIOS_ENABLE,	/* 00 cfg_lsw */
	0x0000,			/* 01 cfg_msw */
	0xFFFF,			/* 02 disc_enable */
	0xFFFF,			/* 03 wdtr_able */
	0x4444,			/* 04 sdtr_speed1 */
	0xFFFF,			/* 05 start_motor */
	0xFFFF,			/* 06 tagqng_able */
	0xFFFF,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	7,			/* 09 adapter_scsi_id */
	0,			/*    bios_boot_delay */
	3,			/* 10 scsi_reset_delay */
	0,			/*    bios_id_lun */
	0,			/* 11 termination_se */
	0,			/*    termination_lvd */
	0xFFE7,			/* 12 bios_ctrl */
	0x4444,			/* 13 sdtr_speed2 */
	0x4444,			/* 14 sdtr_speed3 */
	ASC_DEF_MAX_HOST_QNG,	/* 15 max_host_qng */
	ASC_DEF_MAX_DVC_QNG,	/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	0x4444,			/* 17 sdtr_speed4 */
	0,			/* 18 serial_number_word1 */
	0,			/* 19 serial_number_word2 */
	0,			/* 20 serial_number_word3 */
	0,			/* 21 check_sum */
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	,			/* 22-29 oem_name[16] */
	0,			/* 30 dvc_err_code */
	0,			/* 31 adv_err_code */
	0,			/* 32 adv_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adv_err_code */
	0,			/* 35 saved_adv_err_addr */
	0,			/* 36 reserved */
	0,			/* 37 reserved */
	0,			/* 38 reserved */
	0,			/* 39 reserved */
	0,			/* 40 reserved */
	0,			/* 41 reserved */
	0,			/* 42 reserved */
	0,			/* 43 reserved */
	0,			/* 44 reserved */
	0,			/* 45 reserved */
	0,			/* 46 reserved */
	0,			/* 47 reserved */
	0,			/* 48 reserved */
	0,			/* 49 reserved */
	0,			/* 50 reserved */
	0,			/* 51 reserved */
	0,			/* 52 reserved */
	0,			/* 53 reserved */
	0,			/* 54 reserved */
	0,			/* 55 reserved */
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	PCI_VENDOR_ID_ASP,	/* 58 subsysvid */
	PCI_DEVICE_ID_38C0800_REV1,	/* 59 subsysid */
	0,			/* 60 reserved */
	0,			/* 61 reserved */
	0,			/* 62 reserved */
	0			/* 63 reserved */
};

static ADVEEP_38C0800_CONFIG ADVEEP_38C0800_Config_Field_IsChar __devinitdata = {
	0,			/* 00 cfg_lsw */
	0,			/* 01 cfg_msw */
	0,			/* 02 disc_enable */
	0,			/* 03 wdtr_able */
	0,			/* 04 sdtr_speed1 */
	0,			/* 05 start_motor */
	0,			/* 06 tagqng_able */
	0,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	1,			/* 09 adapter_scsi_id */
	1,			/*    bios_boot_delay */
	1,			/* 10 scsi_reset_delay */
	1,			/*    bios_id_lun */
	1,			/* 11 termination_se */
	1,			/*    termination_lvd */
	0,			/* 12 bios_ctrl */
	0,			/* 13 sdtr_speed2 */
	0,			/* 14 sdtr_speed3 */
	1,			/* 15 max_host_qng */
	1,			/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	0,			/* 17 sdtr_speed4 */
	0,			/* 18 serial_number_word1 */
	0,			/* 19 serial_number_word2 */
	0,			/* 20 serial_number_word3 */
	0,			/* 21 check_sum */
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
	,			/* 22-29 oem_name[16] */
	0,			/* 30 dvc_err_code */
	0,			/* 31 adv_err_code */
	0,			/* 32 adv_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adv_err_code */
	0,			/* 35 saved_adv_err_addr */
	0,			/* 36 reserved */
	0,			/* 37 reserved */
	0,			/* 38 reserved */
	0,			/* 39 reserved */
	0,			/* 40 reserved */
	0,			/* 41 reserved */
	0,			/* 42 reserved */
	0,			/* 43 reserved */
	0,			/* 44 reserved */
	0,			/* 45 reserved */
	0,			/* 46 reserved */
	0,			/* 47 reserved */
	0,			/* 48 reserved */
	0,			/* 49 reserved */
	0,			/* 50 reserved */
	0,			/* 51 reserved */
	0,			/* 52 reserved */
	0,			/* 53 reserved */
	0,			/* 54 reserved */
	0,			/* 55 reserved */
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	0,			/* 58 subsysvid */
	0,			/* 59 subsysid */
	0,			/* 60 reserved */
	0,			/* 61 reserved */
	0,			/* 62 reserved */
	0			/* 63 reserved */
};

static ADVEEP_38C1600_CONFIG Default_38C1600_EEPROM_Config __devinitdata = {
	ADV_EEPROM_BIOS_ENABLE,	/* 00 cfg_lsw */
	0x0000,			/* 01 cfg_msw */
	0xFFFF,			/* 02 disc_enable */
	0xFFFF,			/* 03 wdtr_able */
	0x5555,			/* 04 sdtr_speed1 */
	0xFFFF,			/* 05 start_motor */
	0xFFFF,			/* 06 tagqng_able */
	0xFFFF,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	7,			/* 09 adapter_scsi_id */
	0,			/*    bios_boot_delay */
	3,			/* 10 scsi_reset_delay */
	0,			/*    bios_id_lun */
	0,			/* 11 termination_se */
	0,			/*    termination_lvd */
	0xFFE7,			/* 12 bios_ctrl */
	0x5555,			/* 13 sdtr_speed2 */
	0x5555,			/* 14 sdtr_speed3 */
	ASC_DEF_MAX_HOST_QNG,	/* 15 max_host_qng */
	ASC_DEF_MAX_DVC_QNG,	/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	0x5555,			/* 17 sdtr_speed4 */
	0,			/* 18 serial_number_word1 */
	0,			/* 19 serial_number_word2 */
	0,			/* 20 serial_number_word3 */
	0,			/* 21 check_sum */
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
	,			/* 22-29 oem_name[16] */
	0,			/* 30 dvc_err_code */
	0,			/* 31 adv_err_code */
	0,			/* 32 adv_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adv_err_code */
	0,			/* 35 saved_adv_err_addr */
	0,			/* 36 reserved */
	0,			/* 37 reserved */
	0,			/* 38 reserved */
	0,			/* 39 reserved */
	0,			/* 40 reserved */
	0,			/* 41 reserved */
	0,			/* 42 reserved */
	0,			/* 43 reserved */
	0,			/* 44 reserved */
	0,			/* 45 reserved */
	0,			/* 46 reserved */
	0,			/* 47 reserved */
	0,			/* 48 reserved */
	0,			/* 49 reserved */
	0,			/* 50 reserved */
	0,			/* 51 reserved */
	0,			/* 52 reserved */
	0,			/* 53 reserved */
	0,			/* 54 reserved */
	0,			/* 55 reserved */
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	PCI_VENDOR_ID_ASP,	/* 58 subsysvid */
	PCI_DEVICE_ID_38C1600_REV1,	/* 59 subsysid */
	0,			/* 60 reserved */
	0,			/* 61 reserved */
	0,			/* 62 reserved */
	0			/* 63 reserved */
};

static ADVEEP_38C1600_CONFIG ADVEEP_38C1600_Config_Field_IsChar __devinitdata = {
	0,			/* 00 cfg_lsw */
	0,			/* 01 cfg_msw */
	0,			/* 02 disc_enable */
	0,			/* 03 wdtr_able */
	0,			/* 04 sdtr_speed1 */
	0,			/* 05 start_motor */
	0,			/* 06 tagqng_able */
	0,			/* 07 bios_scan */
	0,			/* 08 scam_tolerant */
	1,			/* 09 adapter_scsi_id */
	1,			/*    bios_boot_delay */
	1,			/* 10 scsi_reset_delay */
	1,			/*    bios_id_lun */
	1,			/* 11 termination_se */
	1,			/*    termination_lvd */
	0,			/* 12 bios_ctrl */
	0,			/* 13 sdtr_speed2 */
	0,			/* 14 sdtr_speed3 */
	1,			/* 15 max_host_qng */
	1,			/*    max_dvc_qng */
	0,			/* 16 dvc_cntl */
	0,			/* 17 sdtr_speed4 */
	0,			/* 18 serial_number_word1 */
	0,			/* 19 serial_number_word2 */
	0,			/* 20 serial_number_word3 */
	0,			/* 21 check_sum */
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
	,			/* 22-29 oem_name[16] */
	0,			/* 30 dvc_err_code */
	0,			/* 31 adv_err_code */
	0,			/* 32 adv_err_addr */
	0,			/* 33 saved_dvc_err_code */
	0,			/* 34 saved_adv_err_code */
	0,			/* 35 saved_adv_err_addr */
	0,			/* 36 reserved */
	0,			/* 37 reserved */
	0,			/* 38 reserved */
	0,			/* 39 reserved */
	0,			/* 40 reserved */
	0,			/* 41 reserved */
	0,			/* 42 reserved */
	0,			/* 43 reserved */
	0,			/* 44 reserved */
	0,			/* 45 reserved */
	0,			/* 46 reserved */
	0,			/* 47 reserved */
	0,			/* 48 reserved */
	0,			/* 49 reserved */
	0,			/* 50 reserved */
	0,			/* 51 reserved */
	0,			/* 52 reserved */
	0,			/* 53 reserved */
	0,			/* 54 reserved */
	0,			/* 55 reserved */
	0,			/* 56 cisptr_lsw */
	0,			/* 57 cisprt_msw */
	0,			/* 58 subsysvid */
	0,			/* 59 subsysid */
	0,			/* 60 reserved */
	0,			/* 61 reserved */
	0,			/* 62 reserved */
	0			/* 63 reserved */
};

#ifdef CONFIG_PCI
/*
 * Wait for EEPROM command to complete
 */
static void __devinit AdvWaitEEPCmd(AdvPortAddr iop_base)
{
	int eep_delay_ms;

	for (eep_delay_ms = 0; eep_delay_ms < ADV_EEP_DELAY_MS; eep_delay_ms++) {
		if (AdvReadWordRegister(iop_base, IOPW_EE_CMD) &
		    ASC_EEP_CMD_DONE) {
			break;
		}
		mdelay(1);
	}
	if ((AdvReadWordRegister(iop_base, IOPW_EE_CMD) & ASC_EEP_CMD_DONE) ==
	    0)
		BUG();
}

/*
 * Read the EEPROM from specified location
 */
static ushort __devinit AdvReadEEPWord(AdvPortAddr iop_base, int eep_word_addr)
{
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
			     ASC_EEP_CMD_READ | eep_word_addr);
	AdvWaitEEPCmd(iop_base);
	return AdvReadWordRegister(iop_base, IOPW_EE_DATA);
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
static void __devinit
AdvSet3550EEPConfig(AdvPortAddr iop_base, ADVEEP_3550_CONFIG *cfg_buf)
{
	ushort *wbuf;
	ushort addr, chksum;
	ushort *charfields;

	wbuf = (ushort *)cfg_buf;
	charfields = (ushort *)&ADVEEP_3550_Config_Field_IsChar;
	chksum = 0;

	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
	AdvWaitEEPCmd(iop_base);

	/*
	 * Write EEPROM from word 0 to word 20.
	 */
	for (addr = ADV_EEP_DVC_CFG_BEGIN;
	     addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		chksum += *wbuf;	/* Checksum is calculated from word values. */
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
		mdelay(ADV_EEP_DELAY_MS);
	}

	/*
	 * Write EEPROM checksum at word 21.
	 */
	AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
	AdvWaitEEPCmd(iop_base);
	wbuf++;
	charfields++;

	/*
	 * Write EEPROM OEM name at words 22 to 29.
	 */
	for (addr = ADV_EEP_DVC_CTL_BEGIN;
	     addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
	}
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
	AdvWaitEEPCmd(iop_base);
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
static void __devinit
AdvSet38C0800EEPConfig(AdvPortAddr iop_base, ADVEEP_38C0800_CONFIG *cfg_buf)
{
	ushort *wbuf;
	ushort *charfields;
	ushort addr, chksum;

	wbuf = (ushort *)cfg_buf;
	charfields = (ushort *)&ADVEEP_38C0800_Config_Field_IsChar;
	chksum = 0;

	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
	AdvWaitEEPCmd(iop_base);

	/*
	 * Write EEPROM from word 0 to word 20.
	 */
	for (addr = ADV_EEP_DVC_CFG_BEGIN;
	     addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		chksum += *wbuf;	/* Checksum is calculated from word values. */
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
		mdelay(ADV_EEP_DELAY_MS);
	}

	/*
	 * Write EEPROM checksum at word 21.
	 */
	AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
	AdvWaitEEPCmd(iop_base);
	wbuf++;
	charfields++;

	/*
	 * Write EEPROM OEM name at words 22 to 29.
	 */
	for (addr = ADV_EEP_DVC_CTL_BEGIN;
	     addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
	}
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
	AdvWaitEEPCmd(iop_base);
}

/*
 * Write the EEPROM from 'cfg_buf'.
 */
static void __devinit
AdvSet38C1600EEPConfig(AdvPortAddr iop_base, ADVEEP_38C1600_CONFIG *cfg_buf)
{
	ushort *wbuf;
	ushort *charfields;
	ushort addr, chksum;

	wbuf = (ushort *)cfg_buf;
	charfields = (ushort *)&ADVEEP_38C1600_Config_Field_IsChar;
	chksum = 0;

	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_ABLE);
	AdvWaitEEPCmd(iop_base);

	/*
	 * Write EEPROM from word 0 to word 20.
	 */
	for (addr = ADV_EEP_DVC_CFG_BEGIN;
	     addr < ADV_EEP_DVC_CFG_END; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		chksum += *wbuf;	/* Checksum is calculated from word values. */
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
		mdelay(ADV_EEP_DELAY_MS);
	}

	/*
	 * Write EEPROM checksum at word 21.
	 */
	AdvWriteWordRegister(iop_base, IOPW_EE_DATA, chksum);
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE | addr);
	AdvWaitEEPCmd(iop_base);
	wbuf++;
	charfields++;

	/*
	 * Write EEPROM OEM name at words 22 to 29.
	 */
	for (addr = ADV_EEP_DVC_CTL_BEGIN;
	     addr < ADV_EEP_MAX_WORD_ADDR; addr++, wbuf++) {
		ushort word;

		if (*charfields++) {
			word = cpu_to_le16(*wbuf);
		} else {
			word = *wbuf;
		}
		AdvWriteWordRegister(iop_base, IOPW_EE_DATA, word);
		AdvWriteWordRegister(iop_base, IOPW_EE_CMD,
				     ASC_EEP_CMD_WRITE | addr);
		AdvWaitEEPCmd(iop_base);
	}
	AdvWriteWordRegister(iop_base, IOPW_EE_CMD, ASC_EEP_CMD_WRITE_DISABLE);
	AdvWaitEEPCmd(iop_base);
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
static ushort __devinit
AdvGet3550EEPConfig(AdvPortAddr iop_base, ADVEEP_3550_CONFIG *cfg_buf)
{
	ushort wval, chksum;
	ushort *wbuf;
	int eep_addr;
	ushort *charfields;

	charfields = (ushort *)&ADVEEP_3550_Config_Field_IsChar;
	wbuf = (ushort *)cfg_buf;
	chksum = 0;

	for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
	     eep_addr < ADV_EEP_DVC_CFG_END; eep_addr++, wbuf++) {
		wval = AdvReadEEPWord(iop_base, eep_addr);
		chksum += wval;	/* Checksum is calculated from word values. */
		if (*charfields++) {
			*wbuf = le16_to_cpu(wval);
		} else {
			*wbuf = wval;
		}
	}
	/* Read checksum word. */
	*wbuf = AdvReadEEPWord(iop_base, eep_addr);
	wbuf++;
	charfields++;

	/* Read rest of EEPROM not covered by the checksum. */
	for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
	     eep_addr < ADV_EEP_MAX_WORD_ADDR; eep_addr++, wbuf++) {
		*wbuf = AdvReadEEPWord(iop_base, eep_addr);
		if (*charfields++) {
			*wbuf = le16_to_cpu(*wbuf);
		}
	}
	return chksum;
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
static ushort __devinit
AdvGet38C0800EEPConfig(AdvPortAddr iop_base, ADVEEP_38C0800_CONFIG *cfg_buf)
{
	ushort wval, chksum;
	ushort *wbuf;
	int eep_addr;
	ushort *charfields;

	charfields = (ushort *)&ADVEEP_38C0800_Config_Field_IsChar;
	wbuf = (ushort *)cfg_buf;
	chksum = 0;

	for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
	     eep_addr < ADV_EEP_DVC_CFG_END; eep_addr++, wbuf++) {
		wval = AdvReadEEPWord(iop_base, eep_addr);
		chksum += wval;	/* Checksum is calculated from word values. */
		if (*charfields++) {
			*wbuf = le16_to_cpu(wval);
		} else {
			*wbuf = wval;
		}
	}
	/* Read checksum word. */
	*wbuf = AdvReadEEPWord(iop_base, eep_addr);
	wbuf++;
	charfields++;

	/* Read rest of EEPROM not covered by the checksum. */
	for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
	     eep_addr < ADV_EEP_MAX_WORD_ADDR; eep_addr++, wbuf++) {
		*wbuf = AdvReadEEPWord(iop_base, eep_addr);
		if (*charfields++) {
			*wbuf = le16_to_cpu(*wbuf);
		}
	}
	return chksum;
}

/*
 * Read EEPROM configuration into the specified buffer.
 *
 * Return a checksum based on the EEPROM configuration read.
 */
static ushort __devinit
AdvGet38C1600EEPConfig(AdvPortAddr iop_base, ADVEEP_38C1600_CONFIG *cfg_buf)
{
	ushort wval, chksum;
	ushort *wbuf;
	int eep_addr;
	ushort *charfields;

	charfields = (ushort *)&ADVEEP_38C1600_Config_Field_IsChar;
	wbuf = (ushort *)cfg_buf;
	chksum = 0;

	for (eep_addr = ADV_EEP_DVC_CFG_BEGIN;
	     eep_addr < ADV_EEP_DVC_CFG_END; eep_addr++, wbuf++) {
		wval = AdvReadEEPWord(iop_base, eep_addr);
		chksum += wval;	/* Checksum is calculated from word values. */
		if (*charfields++) {
			*wbuf = le16_to_cpu(wval);
		} else {
			*wbuf = wval;
		}
	}
	/* Read checksum word. */
	*wbuf = AdvReadEEPWord(iop_base, eep_addr);
	wbuf++;
	charfields++;

	/* Read rest of EEPROM not covered by the checksum. */
	for (eep_addr = ADV_EEP_DVC_CTL_BEGIN;
	     eep_addr < ADV_EEP_MAX_WORD_ADDR; eep_addr++, wbuf++) {
		*wbuf = AdvReadEEPWord(iop_base, eep_addr);
		if (*charfields++) {
			*wbuf = le16_to_cpu(*wbuf);
		}
	}
	return chksum;
}

/*
 * Read the board's EEPROM configuration. Set fields in ADV_DVC_VAR and
 * ADV_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
static int __devinit AdvInitFrom3550EEP(ADV_DVC_VAR *asc_dvc)
{
	AdvPortAddr iop_base;
	ushort warn_code;
	ADVEEP_3550_CONFIG eep_config;

	iop_base = asc_dvc->iop_base;

	warn_code = 0;

	/*
	 * Read the board's EEPROM configuration.
	 *
	 * Set default values if a bad checksum is found.
	 */
	if (AdvGet3550EEPConfig(iop_base, &eep_config) != eep_config.check_sum) {
		warn_code |= ASC_WARN_EEPROM_CHKSUM;

		/*
		 * Set EEPROM default values.
		 */
		memcpy(&eep_config, &Default_3550_EEPROM_Config,
			sizeof(ADVEEP_3550_CONFIG));

		/*
		 * Assume the 6 byte board serial number that was read from
		 * EEPROM is correct even if the EEPROM checksum failed.
		 */
		eep_config.serial_number_word3 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);

		eep_config.serial_number_word2 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);

		eep_config.serial_number_word1 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

		AdvSet3550EEPConfig(iop_base, &eep_config);
	}
	/*
	 * Set ASC_DVC_VAR and ASC_DVC_CFG variables from the
	 * EEPROM configuration that was read.
	 *
	 * This is the mapping of EEPROM fields to Adv Library fields.
	 */
	asc_dvc->wdtr_able = eep_config.wdtr_able;
	asc_dvc->sdtr_able = eep_config.sdtr_able;
	asc_dvc->ultra_able = eep_config.ultra_able;
	asc_dvc->tagqng_able = eep_config.tagqng_able;
	asc_dvc->cfg->disc_enable = eep_config.disc_enable;
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
	asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ADV_MAX_TID);
	asc_dvc->start_motor = eep_config.start_motor;
	asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
	asc_dvc->bios_ctrl = eep_config.bios_ctrl;
	asc_dvc->no_scam = eep_config.scam_tolerant;
	asc_dvc->cfg->serial1 = eep_config.serial_number_word1;
	asc_dvc->cfg->serial2 = eep_config.serial_number_word2;
	asc_dvc->cfg->serial3 = eep_config.serial_number_word3;

	/*
	 * Set the host maximum queuing (max. 253, min. 16) and the per device
	 * maximum queuing (max. 63, min. 4).
	 */
	if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG) {
		eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
	} else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_host_qng == 0) {
			eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
		} else {
			eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
		}
	}

	if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG) {
		eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
	} else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_dvc_qng == 0) {
			eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
		} else {
			eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
		}
	}

	/*
	 * If 'max_dvc_qng' is greater than 'max_host_qng', then
	 * set 'max_dvc_qng' to 'max_host_qng'.
	 */
	if (eep_config.max_dvc_qng > eep_config.max_host_qng) {
		eep_config.max_dvc_qng = eep_config.max_host_qng;
	}

	/*
	 * Set ADV_DVC_VAR 'max_host_qng' and ADV_DVC_VAR 'max_dvc_qng'
	 * values based on possibly adjusted EEPROM values.
	 */
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;

	/*
	 * If the EEPROM 'termination' field is set to automatic (0), then set
	 * the ADV_DVC_CFG 'termination' field to automatic also.
	 *
	 * If the termination is specified with a non-zero 'termination'
	 * value check that a legal value is set and set the ADV_DVC_CFG
	 * 'termination' field appropriately.
	 */
	if (eep_config.termination == 0) {
		asc_dvc->cfg->termination = 0;	/* auto termination */
	} else {
		/* Enable manual control with low off / high off. */
		if (eep_config.termination == 1) {
			asc_dvc->cfg->termination = TERM_CTL_SEL;

			/* Enable manual control with low off / high on. */
		} else if (eep_config.termination == 2) {
			asc_dvc->cfg->termination = TERM_CTL_SEL | TERM_CTL_H;

			/* Enable manual control with low on / high on. */
		} else if (eep_config.termination == 3) {
			asc_dvc->cfg->termination =
			    TERM_CTL_SEL | TERM_CTL_H | TERM_CTL_L;
		} else {
			/*
			 * The EEPROM 'termination' field contains a bad value. Use
			 * automatic termination instead.
			 */
			asc_dvc->cfg->termination = 0;
			warn_code |= ASC_WARN_EEPROM_TERMINATION;
		}
	}

	return warn_code;
}

/*
 * Read the board's EEPROM configuration. Set fields in ADV_DVC_VAR and
 * ADV_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
static int __devinit AdvInitFrom38C0800EEP(ADV_DVC_VAR *asc_dvc)
{
	AdvPortAddr iop_base;
	ushort warn_code;
	ADVEEP_38C0800_CONFIG eep_config;
	uchar tid, termination;
	ushort sdtr_speed = 0;

	iop_base = asc_dvc->iop_base;

	warn_code = 0;

	/*
	 * Read the board's EEPROM configuration.
	 *
	 * Set default values if a bad checksum is found.
	 */
	if (AdvGet38C0800EEPConfig(iop_base, &eep_config) !=
	    eep_config.check_sum) {
		warn_code |= ASC_WARN_EEPROM_CHKSUM;

		/*
		 * Set EEPROM default values.
		 */
		memcpy(&eep_config, &Default_38C0800_EEPROM_Config,
			sizeof(ADVEEP_38C0800_CONFIG));

		/*
		 * Assume the 6 byte board serial number that was read from
		 * EEPROM is correct even if the EEPROM checksum failed.
		 */
		eep_config.serial_number_word3 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);

		eep_config.serial_number_word2 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);

		eep_config.serial_number_word1 =
		    AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

		AdvSet38C0800EEPConfig(iop_base, &eep_config);
	}
	/*
	 * Set ADV_DVC_VAR and ADV_DVC_CFG variables from the
	 * EEPROM configuration that was read.
	 *
	 * This is the mapping of EEPROM fields to Adv Library fields.
	 */
	asc_dvc->wdtr_able = eep_config.wdtr_able;
	asc_dvc->sdtr_speed1 = eep_config.sdtr_speed1;
	asc_dvc->sdtr_speed2 = eep_config.sdtr_speed2;
	asc_dvc->sdtr_speed3 = eep_config.sdtr_speed3;
	asc_dvc->sdtr_speed4 = eep_config.sdtr_speed4;
	asc_dvc->tagqng_able = eep_config.tagqng_able;
	asc_dvc->cfg->disc_enable = eep_config.disc_enable;
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
	asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ADV_MAX_TID);
	asc_dvc->start_motor = eep_config.start_motor;
	asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
	asc_dvc->bios_ctrl = eep_config.bios_ctrl;
	asc_dvc->no_scam = eep_config.scam_tolerant;
	asc_dvc->cfg->serial1 = eep_config.serial_number_word1;
	asc_dvc->cfg->serial2 = eep_config.serial_number_word2;
	asc_dvc->cfg->serial3 = eep_config.serial_number_word3;

	/*
	 * For every Target ID if any of its 'sdtr_speed[1234]' bits
	 * are set, then set an 'sdtr_able' bit for it.
	 */
	asc_dvc->sdtr_able = 0;
	for (tid = 0; tid <= ADV_MAX_TID; tid++) {
		if (tid == 0) {
			sdtr_speed = asc_dvc->sdtr_speed1;
		} else if (tid == 4) {
			sdtr_speed = asc_dvc->sdtr_speed2;
		} else if (tid == 8) {
			sdtr_speed = asc_dvc->sdtr_speed3;
		} else if (tid == 12) {
			sdtr_speed = asc_dvc->sdtr_speed4;
		}
		if (sdtr_speed & ADV_MAX_TID) {
			asc_dvc->sdtr_able |= (1 << tid);
		}
		sdtr_speed >>= 4;
	}

	/*
	 * Set the host maximum queuing (max. 253, min. 16) and the per device
	 * maximum queuing (max. 63, min. 4).
	 */
	if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG) {
		eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
	} else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_host_qng == 0) {
			eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
		} else {
			eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
		}
	}

	if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG) {
		eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
	} else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_dvc_qng == 0) {
			eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
		} else {
			eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
		}
	}

	/*
	 * If 'max_dvc_qng' is greater than 'max_host_qng', then
	 * set 'max_dvc_qng' to 'max_host_qng'.
	 */
	if (eep_config.max_dvc_qng > eep_config.max_host_qng) {
		eep_config.max_dvc_qng = eep_config.max_host_qng;
	}

	/*
	 * Set ADV_DVC_VAR 'max_host_qng' and ADV_DVC_VAR 'max_dvc_qng'
	 * values based on possibly adjusted EEPROM values.
	 */
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;

	/*
	 * If the EEPROM 'termination' field is set to automatic (0), then set
	 * the ADV_DVC_CFG 'termination' field to automatic also.
	 *
	 * If the termination is specified with a non-zero 'termination'
	 * value check that a legal value is set and set the ADV_DVC_CFG
	 * 'termination' field appropriately.
	 */
	if (eep_config.termination_se == 0) {
		termination = 0;	/* auto termination for SE */
	} else {
		/* Enable manual control with low off / high off. */
		if (eep_config.termination_se == 1) {
			termination = 0;

			/* Enable manual control with low off / high on. */
		} else if (eep_config.termination_se == 2) {
			termination = TERM_SE_HI;

			/* Enable manual control with low on / high on. */
		} else if (eep_config.termination_se == 3) {
			termination = TERM_SE;
		} else {
			/*
			 * The EEPROM 'termination_se' field contains a bad value.
			 * Use automatic termination instead.
			 */
			termination = 0;
			warn_code |= ASC_WARN_EEPROM_TERMINATION;
		}
	}

	if (eep_config.termination_lvd == 0) {
		asc_dvc->cfg->termination = termination;	/* auto termination for LVD */
	} else {
		/* Enable manual control with low off / high off. */
		if (eep_config.termination_lvd == 1) {
			asc_dvc->cfg->termination = termination;

			/* Enable manual control with low off / high on. */
		} else if (eep_config.termination_lvd == 2) {
			asc_dvc->cfg->termination = termination | TERM_LVD_HI;

			/* Enable manual control with low on / high on. */
		} else if (eep_config.termination_lvd == 3) {
			asc_dvc->cfg->termination = termination | TERM_LVD;
		} else {
			/*
			 * The EEPROM 'termination_lvd' field contains a bad value.
			 * Use automatic termination instead.
			 */
			asc_dvc->cfg->termination = termination;
			warn_code |= ASC_WARN_EEPROM_TERMINATION;
		}
	}

	return warn_code;
}

/*
 * Read the board's EEPROM configuration. Set fields in ASC_DVC_VAR and
 * ASC_DVC_CFG based on the EEPROM settings. The chip is stopped while
 * all of this is done.
 *
 * On failure set the ASC_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 *
 * Note: Chip is stopped on entry.
 */
static int __devinit AdvInitFrom38C1600EEP(ADV_DVC_VAR *asc_dvc)
{
	AdvPortAddr iop_base;
	ushort warn_code;
	ADVEEP_38C1600_CONFIG eep_config;
	uchar tid, termination;
	ushort sdtr_speed = 0;

	iop_base = asc_dvc->iop_base;

	warn_code = 0;

	/*
	 * Read the board's EEPROM configuration.
	 *
	 * Set default values if a bad checksum is found.
	 */
	if (AdvGet38C1600EEPConfig(iop_base, &eep_config) !=
	    eep_config.check_sum) {
		struct pci_dev *pdev = adv_dvc_to_pdev(asc_dvc);
		warn_code |= ASC_WARN_EEPROM_CHKSUM;

		/*
		 * Set EEPROM default values.
		 */
		memcpy(&eep_config, &Default_38C1600_EEPROM_Config,
			sizeof(ADVEEP_38C1600_CONFIG));

		if (PCI_FUNC(pdev->devfn) != 0) {
			u8 ints;
			/*
			 * Disable Bit 14 (BIOS_ENABLE) to fix SPARC Ultra 60
			 * and old Mac system booting problem. The Expansion
			 * ROM must be disabled in Function 1 for these systems
			 */
			eep_config.cfg_lsw &= ~ADV_EEPROM_BIOS_ENABLE;
			/*
			 * Clear the INTAB (bit 11) if the GPIO 0 input
			 * indicates the Function 1 interrupt line is wired
			 * to INTB.
			 *
			 * Set/Clear Bit 11 (INTAB) from the GPIO bit 0 input:
			 *   1 - Function 1 interrupt line wired to INT A.
			 *   0 - Function 1 interrupt line wired to INT B.
			 *
			 * Note: Function 0 is always wired to INTA.
			 * Put all 5 GPIO bits in input mode and then read
			 * their input values.
			 */
			AdvWriteByteRegister(iop_base, IOPB_GPIO_CNTL, 0);
			ints = AdvReadByteRegister(iop_base, IOPB_GPIO_DATA);
			if ((ints & 0x01) == 0)
				eep_config.cfg_lsw &= ~ADV_EEPROM_INTAB;
		}

		/*
		 * Assume the 6 byte board serial number that was read from
		 * EEPROM is correct even if the EEPROM checksum failed.
		 */
		eep_config.serial_number_word3 =
			AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 1);
		eep_config.serial_number_word2 =
			AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 2);
		eep_config.serial_number_word1 =
			AdvReadEEPWord(iop_base, ADV_EEP_DVC_CFG_END - 3);

		AdvSet38C1600EEPConfig(iop_base, &eep_config);
	}

	/*
	 * Set ASC_DVC_VAR and ASC_DVC_CFG variables from the
	 * EEPROM configuration that was read.
	 *
	 * This is the mapping of EEPROM fields to Adv Library fields.
	 */
	asc_dvc->wdtr_able = eep_config.wdtr_able;
	asc_dvc->sdtr_speed1 = eep_config.sdtr_speed1;
	asc_dvc->sdtr_speed2 = eep_config.sdtr_speed2;
	asc_dvc->sdtr_speed3 = eep_config.sdtr_speed3;
	asc_dvc->sdtr_speed4 = eep_config.sdtr_speed4;
	asc_dvc->ppr_able = 0;
	asc_dvc->tagqng_able = eep_config.tagqng_able;
	asc_dvc->cfg->disc_enable = eep_config.disc_enable;
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;
	asc_dvc->chip_scsi_id = (eep_config.adapter_scsi_id & ASC_MAX_TID);
	asc_dvc->start_motor = eep_config.start_motor;
	asc_dvc->scsi_reset_wait = eep_config.scsi_reset_delay;
	asc_dvc->bios_ctrl = eep_config.bios_ctrl;
	asc_dvc->no_scam = eep_config.scam_tolerant;

	/*
	 * For every Target ID if any of its 'sdtr_speed[1234]' bits
	 * are set, then set an 'sdtr_able' bit for it.
	 */
	asc_dvc->sdtr_able = 0;
	for (tid = 0; tid <= ASC_MAX_TID; tid++) {
		if (tid == 0) {
			sdtr_speed = asc_dvc->sdtr_speed1;
		} else if (tid == 4) {
			sdtr_speed = asc_dvc->sdtr_speed2;
		} else if (tid == 8) {
			sdtr_speed = asc_dvc->sdtr_speed3;
		} else if (tid == 12) {
			sdtr_speed = asc_dvc->sdtr_speed4;
		}
		if (sdtr_speed & ASC_MAX_TID) {
			asc_dvc->sdtr_able |= (1 << tid);
		}
		sdtr_speed >>= 4;
	}

	/*
	 * Set the host maximum queuing (max. 253, min. 16) and the per device
	 * maximum queuing (max. 63, min. 4).
	 */
	if (eep_config.max_host_qng > ASC_DEF_MAX_HOST_QNG) {
		eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
	} else if (eep_config.max_host_qng < ASC_DEF_MIN_HOST_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_host_qng == 0) {
			eep_config.max_host_qng = ASC_DEF_MAX_HOST_QNG;
		} else {
			eep_config.max_host_qng = ASC_DEF_MIN_HOST_QNG;
		}
	}

	if (eep_config.max_dvc_qng > ASC_DEF_MAX_DVC_QNG) {
		eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
	} else if (eep_config.max_dvc_qng < ASC_DEF_MIN_DVC_QNG) {
		/* If the value is zero, assume it is uninitialized. */
		if (eep_config.max_dvc_qng == 0) {
			eep_config.max_dvc_qng = ASC_DEF_MAX_DVC_QNG;
		} else {
			eep_config.max_dvc_qng = ASC_DEF_MIN_DVC_QNG;
		}
	}

	/*
	 * If 'max_dvc_qng' is greater than 'max_host_qng', then
	 * set 'max_dvc_qng' to 'max_host_qng'.
	 */
	if (eep_config.max_dvc_qng > eep_config.max_host_qng) {
		eep_config.max_dvc_qng = eep_config.max_host_qng;
	}

	/*
	 * Set ASC_DVC_VAR 'max_host_qng' and ASC_DVC_VAR 'max_dvc_qng'
	 * values based on possibly adjusted EEPROM values.
	 */
	asc_dvc->max_host_qng = eep_config.max_host_qng;
	asc_dvc->max_dvc_qng = eep_config.max_dvc_qng;

	/*
	 * If the EEPROM 'termination' field is set to automatic (0), then set
	 * the ASC_DVC_CFG 'termination' field to automatic also.
	 *
	 * If the termination is specified with a non-zero 'termination'
	 * value check that a legal value is set and set the ASC_DVC_CFG
	 * 'termination' field appropriately.
	 */
	if (eep_config.termination_se == 0) {
		termination = 0;	/* auto termination for SE */
	} else {
		/* Enable manual control with low off / high off. */
		if (eep_config.termination_se == 1) {
			termination = 0;

			/* Enable manual control with low off / high on. */
		} else if (eep_config.termination_se == 2) {
			termination = TERM_SE_HI;

			/* Enable manual control with low on / high on. */
		} else if (eep_config.termination_se == 3) {
			termination = TERM_SE;
		} else {
			/*
			 * The EEPROM 'termination_se' field contains a bad value.
			 * Use automatic termination instead.
			 */
			termination = 0;
			warn_code |= ASC_WARN_EEPROM_TERMINATION;
		}
	}

	if (eep_config.termination_lvd == 0) {
		asc_dvc->cfg->termination = termination;	/* auto termination for LVD */
	} else {
		/* Enable manual control with low off / high off. */
		if (eep_config.termination_lvd == 1) {
			asc_dvc->cfg->termination = termination;

			/* Enable manual control with low off / high on. */
		} else if (eep_config.termination_lvd == 2) {
			asc_dvc->cfg->termination = termination | TERM_LVD_HI;

			/* Enable manual control with low on / high on. */
		} else if (eep_config.termination_lvd == 3) {
			asc_dvc->cfg->termination = termination | TERM_LVD;
		} else {
			/*
			 * The EEPROM 'termination_lvd' field contains a bad value.
			 * Use automatic termination instead.
			 */
			asc_dvc->cfg->termination = termination;
			warn_code |= ASC_WARN_EEPROM_TERMINATION;
		}
	}

	return warn_code;
}

/*
 * Initialize the ADV_DVC_VAR structure.
 *
 * On failure set the ADV_DVC_VAR field 'err_code' and return ADV_ERROR.
 *
 * For a non-fatal error return a warning code. If there are no warnings
 * then 0 is returned.
 */
static int __devinit
AdvInitGetConfig(struct pci_dev *pdev, struct Scsi_Host *shost)
{
	struct asc_board *board = shost_priv(shost);
	ADV_DVC_VAR *asc_dvc = &board->dvc_var.adv_dvc_var;
	unsigned short warn_code = 0;
	AdvPortAddr iop_base = asc_dvc->iop_base;
	u16 cmd;
	int status;

	asc_dvc->err_code = 0;

	/*
	 * Save the state of the PCI Configuration Command Register
	 * "Parity Error Response Control" Bit. If the bit is clear (0),
	 * in AdvInitAsc3550/38C0800Driver() tell the microcode to ignore
	 * DMA parity errors.
	 */
	asc_dvc->cfg->control_flag = 0;
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if ((cmd & PCI_COMMAND_PARITY) == 0)
		asc_dvc->cfg->control_flag |= CONTROL_FLAG_IGNORE_PERR;

	asc_dvc->cfg->chip_version =
	    AdvGetChipVersion(iop_base, asc_dvc->bus_type);

	ASC_DBG(1, "iopb_chip_id_1: 0x%x 0x%x\n",
		 (ushort)AdvReadByteRegister(iop_base, IOPB_CHIP_ID_1),
		 (ushort)ADV_CHIP_ID_BYTE);

	ASC_DBG(1, "iopw_chip_id_0: 0x%x 0x%x\n",
		 (ushort)AdvReadWordRegister(iop_base, IOPW_CHIP_ID_0),
		 (ushort)ADV_CHIP_ID_WORD);

	/*
	 * Reset the chip to start and allow register writes.
	 */
	if (AdvFindSignature(iop_base) == 0) {
		asc_dvc->err_code = ASC_IERR_BAD_SIGNATURE;
		return ADV_ERROR;
	} else {
		/*
		 * The caller must set 'chip_type' to a valid setting.
		 */
		if (asc_dvc->chip_type != ADV_CHIP_ASC3550 &&
		    asc_dvc->chip_type != ADV_CHIP_ASC38C0800 &&
		    asc_dvc->chip_type != ADV_CHIP_ASC38C1600) {
			asc_dvc->err_code |= ASC_IERR_BAD_CHIPTYPE;
			return ADV_ERROR;
		}

		/*
		 * Reset Chip.
		 */
		AdvWriteWordRegister(iop_base, IOPW_CTRL_REG,
				     ADV_CTRL_REG_CMD_RESET);
		mdelay(100);
		AdvWriteWordRegister(iop_base, IOPW_CTRL_REG,
				     ADV_CTRL_REG_CMD_WR_IO_REG);

		if (asc_dvc->chip_type == ADV_CHIP_ASC38C1600) {
			status = AdvInitFrom38C1600EEP(asc_dvc);
		} else if (asc_dvc->chip_type == ADV_CHIP_ASC38C0800) {
			status = AdvInitFrom38C0800EEP(asc_dvc);
		} else {
			status = AdvInitFrom3550EEP(asc_dvc);
		}
		warn_code |= status;
	}

	if (warn_code != 0)
		shost_printk(KERN_WARNING, shost, "warning: 0x%x\n", warn_code);

	if (asc_dvc->err_code)
		shost_printk(KERN_ERR, shost, "error code 0x%x\n",
				asc_dvc->err_code);

	return asc_dvc->err_code;
}
#endif

static struct scsi_host_template advansys_template = {
	.proc_name = DRV_NAME,
#ifdef CONFIG_PROC_FS
	.proc_info = advansys_proc_info,
#endif
	.name = DRV_NAME,
	.info = advansys_info,
	.queuecommand = advansys_queuecommand,
	.eh_bus_reset_handler = advansys_reset,
	.bios_param = advansys_biosparam,
	.slave_configure = advansys_slave_configure,
	/*
	 * Because the driver may control an ISA adapter 'unchecked_isa_dma'
	 * must be set. The flag will be cleared in advansys_board_found
	 * for non-ISA adapters.
	 */
	.unchecked_isa_dma = 1,
	/*
	 * All adapters controlled by this driver are capable of large
	 * scatter-gather lists. According to the mid-level SCSI documentation
	 * this obviates any performance gain provided by setting
	 * 'use_clustering'. But empirically while CPU utilization is increased
	 * by enabling clustering, I/O throughput increases as well.
	 */
	.use_clustering = ENABLE_CLUSTERING,
};

static int __devinit advansys_wide_init_chip(struct Scsi_Host *shost)
{
	struct asc_board *board = shost_priv(shost);
	struct adv_dvc_var *adv_dvc = &board->dvc_var.adv_dvc_var;
	int req_cnt = 0;
	adv_req_t *reqp = NULL;
	int sg_cnt = 0;
	adv_sgblk_t *sgp;
	int warn_code, err_code;

	/*
	 * Allocate buffer carrier structures. The total size
	 * is about 4 KB, so allocate all at once.
	 */
	adv_dvc->carrier_buf = kmalloc(ADV_CARRIER_BUFSIZE, GFP_KERNEL);
	ASC_DBG(1, "carrier_buf 0x%p\n", adv_dvc->carrier_buf);

	if (!adv_dvc->carrier_buf)
		goto kmalloc_failed;

	/*
	 * Allocate up to 'max_host_qng' request structures for the Wide
	 * board. The total size is about 16 KB, so allocate all at once.
	 * If the allocation fails decrement and try again.
	 */
	for (req_cnt = adv_dvc->max_host_qng; req_cnt > 0; req_cnt--) {
		reqp = kmalloc(sizeof(adv_req_t) * req_cnt, GFP_KERNEL);

		ASC_DBG(1, "reqp 0x%p, req_cnt %d, bytes %lu\n", reqp, req_cnt,
			 (ulong)sizeof(adv_req_t) * req_cnt);

		if (reqp)
			break;
	}

	if (!reqp)
		goto kmalloc_failed;

	adv_dvc->orig_reqp = reqp;

	/*
	 * Allocate up to ADV_TOT_SG_BLOCK request structures for
	 * the Wide board. Each structure is about 136 bytes.
	 */
	board->adv_sgblkp = NULL;
	for (sg_cnt = 0; sg_cnt < ADV_TOT_SG_BLOCK; sg_cnt++) {
		sgp = kmalloc(sizeof(adv_sgblk_t), GFP_KERNEL);

		if (!sgp)
			break;

		sgp->next_sgblkp = board->adv_sgblkp;
		board->adv_sgblkp = sgp;

	}

	ASC_DBG(1, "sg_cnt %d * %lu = %lu bytes\n", sg_cnt, sizeof(adv_sgblk_t),
		 sizeof(adv_sgblk_t) * sg_cnt);

	if (!board->adv_sgblkp)
		goto kmalloc_failed;

	/*
	 * Point 'adv_reqp' to the request structures and
	 * link them together.
	 */
	req_cnt--;
	reqp[req_cnt].next_reqp = NULL;
	for (; req_cnt > 0; req_cnt--) {
		reqp[req_cnt - 1].next_reqp = &reqp[req_cnt];
	}
	board->adv_reqp = &reqp[0];

	if (adv_dvc->chip_type == ADV_CHIP_ASC3550) {
		ASC_DBG(2, "AdvInitAsc3550Driver()\n");
		warn_code = AdvInitAsc3550Driver(adv_dvc);
	} else if (adv_dvc->chip_type == ADV_CHIP_ASC38C0800) {
		ASC_DBG(2, "AdvInitAsc38C0800Driver()\n");
		warn_code = AdvInitAsc38C0800Driver(adv_dvc);
	} else {
		ASC_DBG(2, "AdvInitAsc38C1600Driver()\n");
		warn_code = AdvInitAsc38C1600Driver(adv_dvc);
	}
	err_code = adv_dvc->err_code;

	if (warn_code || err_code) {
		shost_printk(KERN_WARNING, shost, "error: warn 0x%x, error "
			"0x%x\n", warn_code, err_code);
	}

	goto exit;

 kmalloc_failed:
	shost_printk(KERN_ERR, shost, "error: kmalloc() failed\n");
	err_code = ADV_ERROR;
 exit:
	return err_code;
}

static void advansys_wide_free_mem(struct asc_board *board)
{
	struct adv_dvc_var *adv_dvc = &board->dvc_var.adv_dvc_var;
	kfree(adv_dvc->carrier_buf);
	adv_dvc->carrier_buf = NULL;
	kfree(adv_dvc->orig_reqp);
	adv_dvc->orig_reqp = board->adv_reqp = NULL;
	while (board->adv_sgblkp) {
		adv_sgblk_t *sgp = board->adv_sgblkp;
		board->adv_sgblkp = sgp->next_sgblkp;
		kfree(sgp);
	}
}

static int __devinit advansys_board_found(struct Scsi_Host *shost,
					  unsigned int iop, int bus_type)
{
	struct pci_dev *pdev;
	struct asc_board *boardp = shost_priv(shost);
	ASC_DVC_VAR *asc_dvc_varp = NULL;
	ADV_DVC_VAR *adv_dvc_varp = NULL;
	int share_irq, warn_code, ret;

	pdev = (bus_type == ASC_IS_PCI) ? to_pci_dev(boardp->dev) : NULL;

	if (ASC_NARROW_BOARD(boardp)) {
		ASC_DBG(1, "narrow board\n");
		asc_dvc_varp = &boardp->dvc_var.asc_dvc_var;
		asc_dvc_varp->bus_type = bus_type;
		asc_dvc_varp->drv_ptr = boardp;
		asc_dvc_varp->cfg = &boardp->dvc_cfg.asc_dvc_cfg;
		asc_dvc_varp->iop_base = iop;
	} else {
#ifdef CONFIG_PCI
		adv_dvc_varp = &boardp->dvc_var.adv_dvc_var;
		adv_dvc_varp->drv_ptr = boardp;
		adv_dvc_varp->cfg = &boardp->dvc_cfg.adv_dvc_cfg;
		if (pdev->device == PCI_DEVICE_ID_ASP_ABP940UW) {
			ASC_DBG(1, "wide board ASC-3550\n");
			adv_dvc_varp->chip_type = ADV_CHIP_ASC3550;
		} else if (pdev->device == PCI_DEVICE_ID_38C0800_REV1) {
			ASC_DBG(1, "wide board ASC-38C0800\n");
			adv_dvc_varp->chip_type = ADV_CHIP_ASC38C0800;
		} else {
			ASC_DBG(1, "wide board ASC-38C1600\n");
			adv_dvc_varp->chip_type = ADV_CHIP_ASC38C1600;
		}

		boardp->asc_n_io_port = pci_resource_len(pdev, 1);
		boardp->ioremap_addr = pci_ioremap_bar(pdev, 1);
		if (!boardp->ioremap_addr) {
			shost_printk(KERN_ERR, shost, "ioremap(%lx, %d) "
					"returned NULL\n",
					(long)pci_resource_start(pdev, 1),
					boardp->asc_n_io_port);
			ret = -ENODEV;
			goto err_shost;
		}
		adv_dvc_varp->iop_base = (AdvPortAddr)boardp->ioremap_addr;
		ASC_DBG(1, "iop_base: 0x%p\n", adv_dvc_varp->iop_base);

		/*
		 * Even though it isn't used to access wide boards, other
		 * than for the debug line below, save I/O Port address so
		 * that it can be reported.
		 */
		boardp->ioport = iop;

		ASC_DBG(1, "iopb_chip_id_1 0x%x, iopw_chip_id_0 0x%x\n",
				(ushort)inp(iop + 1), (ushort)inpw(iop));
#endif /* CONFIG_PCI */
	}

#ifdef CONFIG_PROC_FS
	/*
	 * Allocate buffer for printing information from
	 * /proc/scsi/advansys/[0...].
	 */
	boardp->prtbuf = kmalloc(ASC_PRTBUF_SIZE, GFP_KERNEL);
	if (!boardp->prtbuf) {
		shost_printk(KERN_ERR, shost, "kmalloc(%d) returned NULL\n",
				ASC_PRTBUF_SIZE);
		ret = -ENOMEM;
		goto err_unmap;
	}
#endif /* CONFIG_PROC_FS */

	if (ASC_NARROW_BOARD(boardp)) {
		/*
		 * Set the board bus type and PCI IRQ before
		 * calling AscInitGetConfig().
		 */
		switch (asc_dvc_varp->bus_type) {
#ifdef CONFIG_ISA
		case ASC_IS_ISA:
			shost->unchecked_isa_dma = TRUE;
			share_irq = 0;
			break;
		case ASC_IS_VL:
			shost->unchecked_isa_dma = FALSE;
			share_irq = 0;
			break;
		case ASC_IS_EISA:
			shost->unchecked_isa_dma = FALSE;
			share_irq = IRQF_SHARED;
			break;
#endif /* CONFIG_ISA */
#ifdef CONFIG_PCI
		case ASC_IS_PCI:
			shost->unchecked_isa_dma = FALSE;
			share_irq = IRQF_SHARED;
			break;
#endif /* CONFIG_PCI */
		default:
			shost_printk(KERN_ERR, shost, "unknown adapter type: "
					"%d\n", asc_dvc_varp->bus_type);
			shost->unchecked_isa_dma = TRUE;
			share_irq = 0;
			break;
		}

		/*
		 * NOTE: AscInitGetConfig() may change the board's
		 * bus_type value. The bus_type value should no
		 * longer be used. If the bus_type field must be
		 * referenced only use the bit-wise AND operator "&".
		 */
		ASC_DBG(2, "AscInitGetConfig()\n");
		ret = AscInitGetConfig(shost) ? -ENODEV : 0;
	} else {
#ifdef CONFIG_PCI
		/*
		 * For Wide boards set PCI information before calling
		 * AdvInitGetConfig().
		 */
		shost->unchecked_isa_dma = FALSE;
		share_irq = IRQF_SHARED;
		ASC_DBG(2, "AdvInitGetConfig()\n");

		ret = AdvInitGetConfig(pdev, shost) ? -ENODEV : 0;
#endif /* CONFIG_PCI */
	}

	if (ret)
		goto err_free_proc;

	/*
	 * Save the EEPROM configuration so that it can be displayed
	 * from /proc/scsi/advansys/[0...].
	 */
	if (ASC_NARROW_BOARD(boardp)) {

		ASCEEP_CONFIG *ep;

		/*
		 * Set the adapter's target id bit in the 'init_tidmask' field.
		 */
		boardp->init_tidmask |=
		    ADV_TID_TO_TIDMASK(asc_dvc_varp->cfg->chip_scsi_id);

		/*
		 * Save EEPROM settings for the board.
		 */
		ep = &boardp->eep_config.asc_eep;

		ep->init_sdtr = asc_dvc_varp->cfg->sdtr_enable;
		ep->disc_enable = asc_dvc_varp->cfg->disc_enable;
		ep->use_cmd_qng = asc_dvc_varp->cfg->cmd_qng_enabled;
		ASC_EEP_SET_DMA_SPD(ep, asc_dvc_varp->cfg->isa_dma_speed);
		ep->start_motor = asc_dvc_varp->start_motor;
		ep->cntl = asc_dvc_varp->dvc_cntl;
		ep->no_scam = asc_dvc_varp->no_scam;
		ep->max_total_qng = asc_dvc_varp->max_total_qng;
		ASC_EEP_SET_CHIP_ID(ep, asc_dvc_varp->cfg->chip_scsi_id);
		/* 'max_tag_qng' is set to the same value for every device. */
		ep->max_tag_qng = asc_dvc_varp->cfg->max_tag_qng[0];
		ep->adapter_info[0] = asc_dvc_varp->cfg->adapter_info[0];
		ep->adapter_info[1] = asc_dvc_varp->cfg->adapter_info[1];
		ep->adapter_info[2] = asc_dvc_varp->cfg->adapter_info[2];
		ep->adapter_info[3] = asc_dvc_varp->cfg->adapter_info[3];
		ep->adapter_info[4] = asc_dvc_varp->cfg->adapter_info[4];
		ep->adapter_info[5] = asc_dvc_varp->cfg->adapter_info[5];

		/*
		 * Modify board configuration.
		 */
		ASC_DBG(2, "AscInitSetConfig()\n");
		ret = AscInitSetConfig(pdev, shost) ? -ENODEV : 0;
		if (ret)
			goto err_free_proc;
	} else {
		ADVEEP_3550_CONFIG *ep_3550;
		ADVEEP_38C0800_CONFIG *ep_38C0800;
		ADVEEP_38C1600_CONFIG *ep_38C1600;

		/*
		 * Save Wide EEP Configuration Information.
		 */
		if (adv_dvc_varp->chip_type == ADV_CHIP_ASC3550) {
			ep_3550 = &boardp->eep_config.adv_3550_eep;

			ep_3550->adapter_scsi_id = adv_dvc_varp->chip_scsi_id;
			ep_3550->max_host_qng = adv_dvc_varp->max_host_qng;
			ep_3550->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
			ep_3550->termination = adv_dvc_varp->cfg->termination;
			ep_3550->disc_enable = adv_dvc_varp->cfg->disc_enable;
			ep_3550->bios_ctrl = adv_dvc_varp->bios_ctrl;
			ep_3550->wdtr_able = adv_dvc_varp->wdtr_able;
			ep_3550->sdtr_able = adv_dvc_varp->sdtr_able;
			ep_3550->ultra_able = adv_dvc_varp->ultra_able;
			ep_3550->tagqng_able = adv_dvc_varp->tagqng_able;
			ep_3550->start_motor = adv_dvc_varp->start_motor;
			ep_3550->scsi_reset_delay =
			    adv_dvc_varp->scsi_reset_wait;
			ep_3550->serial_number_word1 =
			    adv_dvc_varp->cfg->serial1;
			ep_3550->serial_number_word2 =
			    adv_dvc_varp->cfg->serial2;
			ep_3550->serial_number_word3 =
			    adv_dvc_varp->cfg->serial3;
		} else if (adv_dvc_varp->chip_type == ADV_CHIP_ASC38C0800) {
			ep_38C0800 = &boardp->eep_config.adv_38C0800_eep;

			ep_38C0800->adapter_scsi_id =
			    adv_dvc_varp->chip_scsi_id;
			ep_38C0800->max_host_qng = adv_dvc_varp->max_host_qng;
			ep_38C0800->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
			ep_38C0800->termination_lvd =
			    adv_dvc_varp->cfg->termination;
			ep_38C0800->disc_enable =
			    adv_dvc_varp->cfg->disc_enable;
			ep_38C0800->bios_ctrl = adv_dvc_varp->bios_ctrl;
			ep_38C0800->wdtr_able = adv_dvc_varp->wdtr_able;
			ep_38C0800->tagqng_able = adv_dvc_varp->tagqng_able;
			ep_38C0800->sdtr_speed1 = adv_dvc_varp->sdtr_speed1;
			ep_38C0800->sdtr_speed2 = adv_dvc_varp->sdtr_speed2;
			ep_38C0800->sdtr_speed3 = adv_dvc_varp->sdtr_speed3;
			ep_38C0800->sdtr_speed4 = adv_dvc_varp->sdtr_speed4;
			ep_38C0800->tagqng_able = adv_dvc_varp->tagqng_able;
			ep_38C0800->start_motor = adv_dvc_varp->start_motor;
			ep_38C0800->scsi_reset_delay =
			    adv_dvc_varp->scsi_reset_wait;
			ep_38C0800->serial_number_word1 =
			    adv_dvc_varp->cfg->serial1;
			ep_38C0800->serial_number_word2 =
			    adv_dvc_varp->cfg->serial2;
			ep_38C0800->serial_number_word3 =
			    adv_dvc_varp->cfg->serial3;
		} else {
			ep_38C1600 = &boardp->eep_config.adv_38C1600_eep;

			ep_38C1600->adapter_scsi_id =
			    adv_dvc_varp->chip_scsi_id;
			ep_38C1600->max_host_qng = adv_dvc_varp->max_host_qng;
			ep_38C1600->max_dvc_qng = adv_dvc_varp->max_dvc_qng;
			ep_38C1600->termination_lvd =
			    adv_dvc_varp->cfg->termination;
			ep_38C1600->disc_enable =
			    adv_dvc_varp->cfg->disc_enable;
			ep_38C1600->bios_ctrl = adv_dvc_varp->bios_ctrl;
			ep_38C1600->wdtr_able = adv_dvc_varp->wdtr_able;
			ep_38C1600->tagqng_able = adv_dvc_varp->tagqng_able;
			ep_38C1600->sdtr_speed1 = adv_dvc_varp->sdtr_speed1;
			ep_38C1600->sdtr_speed2 = adv_dvc_varp->sdtr_speed2;
			ep_38C1600->sdtr_speed3 = adv_dvc_varp->sdtr_speed3;
			ep_38C1600->sdtr_speed4 = adv_dvc_varp->sdtr_speed4;
			ep_38C1600->tagqng_able = adv_dvc_varp->tagqng_able;
			ep_38C1600->start_motor = adv_dvc_varp->start_motor;
			ep_38C1600->scsi_reset_delay =
			    adv_dvc_varp->scsi_reset_wait;
			ep_38C1600->serial_number_word1 =
			    adv_dvc_varp->cfg->serial1;
			ep_38C1600->serial_number_word2 =
			    adv_dvc_varp->cfg->serial2;
			ep_38C1600->serial_number_word3 =
			    adv_dvc_varp->cfg->serial3;
		}

		/*
		 * Set the adapter's target id bit in the 'init_tidmask' field.
		 */
		boardp->init_tidmask |=
		    ADV_TID_TO_TIDMASK(adv_dvc_varp->chip_scsi_id);
	}

	/*
	 * Channels are numbered beginning with 0. For AdvanSys one host
	 * structure supports one channel. Multi-channel boards have a
	 * separate host structure for each channel.
	 */
	shost->max_channel = 0;
	if (ASC_NARROW_BOARD(boardp)) {
		shost->max_id = ASC_MAX_TID + 1;
		shost->max_lun = ASC_MAX_LUN + 1;
		shost->max_cmd_len = ASC_MAX_CDB_LEN;

		shost->io_port = asc_dvc_varp->iop_base;
		boardp->asc_n_io_port = ASC_IOADR_GAP;
		shost->this_id = asc_dvc_varp->cfg->chip_scsi_id;

		/* Set maximum number of queues the adapter can handle. */
		shost->can_queue = asc_dvc_varp->max_total_qng;
	} else {
		shost->max_id = ADV_MAX_TID + 1;
		shost->max_lun = ADV_MAX_LUN + 1;
		shost->max_cmd_len = ADV_MAX_CDB_LEN;

		/*
		 * Save the I/O Port address and length even though
		 * I/O ports are not used to access Wide boards.
		 * Instead the Wide boards are accessed with
		 * PCI Memory Mapped I/O.
		 */
		shost->io_port = iop;

		shost->this_id = adv_dvc_varp->chip_scsi_id;

		/* Set maximum number of queues the adapter can handle. */
		shost->can_queue = adv_dvc_varp->max_host_qng;
	}

	/*
	 * Following v1.3.89, 'cmd_per_lun' is no longer needed
	 * and should be set to zero.
	 *
	 * But because of a bug introduced in v1.3.89 if the driver is
	 * compiled as a module and 'cmd_per_lun' is zero, the Mid-Level
	 * SCSI function 'allocate_device' will panic. To allow the driver
	 * to work as a module in these kernels set 'cmd_per_lun' to 1.
	 *
	 * Note: This is wrong.  cmd_per_lun should be set to the depth
	 * you want on untagged devices always.
	 #ifdef MODULE
	 */
	shost->cmd_per_lun = 1;
/* #else
            shost->cmd_per_lun = 0;
#endif */

	/*
	 * Set the maximum number of scatter-gather elements the
	 * adapter can handle.
	 */
	if (ASC_NARROW_BOARD(boardp)) {
		/*
		 * Allow two commands with 'sg_tablesize' scatter-gather
		 * elements to be executed simultaneously. This value is
		 * the theoretical hardware limit. It may be decreased
		 * below.
		 */
		shost->sg_tablesize =
		    (((asc_dvc_varp->max_total_qng - 2) / 2) *
		     ASC_SG_LIST_PER_Q) + 1;
	} else {
		shost->sg_tablesize = ADV_MAX_SG_LIST;
	}

	/*
	 * The value of 'sg_tablesize' can not exceed the SCSI
	 * mid-level driver definition of SG_ALL. SG_ALL also
	 * must not be exceeded, because it is used to define the
	 * size of the scatter-gather table in 'struct asc_sg_head'.
	 */
	if (shost->sg_tablesize > SG_ALL) {
		shost->sg_tablesize = SG_ALL;
	}

	ASC_DBG(1, "sg_tablesize: %d\n", shost->sg_tablesize);

	/* BIOS start address. */
	if (ASC_NARROW_BOARD(boardp)) {
		shost->base = AscGetChipBiosAddress(asc_dvc_varp->iop_base,
						    asc_dvc_varp->bus_type);
	} else {
		/*
		 * Fill-in BIOS board variables. The Wide BIOS saves
		 * information in LRAM that is used by the driver.
		 */
		AdvReadWordLram(adv_dvc_varp->iop_base,
				BIOS_SIGNATURE, boardp->bios_signature);
		AdvReadWordLram(adv_dvc_varp->iop_base,
				BIOS_VERSION, boardp->bios_version);
		AdvReadWordLram(adv_dvc_varp->iop_base,
				BIOS_CODESEG, boardp->bios_codeseg);
		AdvReadWordLram(adv_dvc_varp->iop_base,
				BIOS_CODELEN, boardp->bios_codelen);

		ASC_DBG(1, "bios_signature 0x%x, bios_version 0x%x\n",
			 boardp->bios_signature, boardp->bios_version);

		ASC_DBG(1, "bios_codeseg 0x%x, bios_codelen 0x%x\n",
			 boardp->bios_codeseg, boardp->bios_codelen);

		/*
		 * If the BIOS saved a valid signature, then fill in
		 * the BIOS code segment base address.
		 */
		if (boardp->bios_signature == 0x55AA) {
			/*
			 * Convert x86 realmode code segment to a linear
			 * address by shifting left 4.
			 */
			shost->base = ((ulong)boardp->bios_codeseg << 4);
		} else {
			shost->base = 0;
		}
	}

	/*
	 * Register Board Resources - I/O Port, DMA, IRQ
	 */

	/* Register DMA Channel for Narrow boards. */
	shost->dma_channel = NO_ISA_DMA;	/* Default to no ISA DMA. */
#ifdef CONFIG_ISA
	if (ASC_NARROW_BOARD(boardp)) {
		/* Register DMA channel for ISA bus. */
		if (asc_dvc_varp->bus_type & ASC_IS_ISA) {
			shost->dma_channel = asc_dvc_varp->cfg->isa_dma_channel;
			ret = request_dma(shost->dma_channel, DRV_NAME);
			if (ret) {
				shost_printk(KERN_ERR, shost, "request_dma() "
						"%d failed %d\n",
						shost->dma_channel, ret);
				goto err_free_proc;
			}
			AscEnableIsaDma(shost->dma_channel);
		}
	}
#endif /* CONFIG_ISA */

	/* Register IRQ Number. */
	ASC_DBG(2, "request_irq(%d, %p)\n", boardp->irq, shost);

	ret = request_irq(boardp->irq, advansys_interrupt, share_irq,
			  DRV_NAME, shost);

	if (ret) {
		if (ret == -EBUSY) {
			shost_printk(KERN_ERR, shost, "request_irq(): IRQ 0x%x "
					"already in use\n", boardp->irq);
		} else if (ret == -EINVAL) {
			shost_printk(KERN_ERR, shost, "request_irq(): IRQ 0x%x "
					"not valid\n", boardp->irq);
		} else {
			shost_printk(KERN_ERR, shost, "request_irq(): IRQ 0x%x "
					"failed with %d\n", boardp->irq, ret);
		}
		goto err_free_dma;
	}

	/*
	 * Initialize board RISC chip and enable interrupts.
	 */
	if (ASC_NARROW_BOARD(boardp)) {
		ASC_DBG(2, "AscInitAsc1000Driver()\n");

		asc_dvc_varp->overrun_buf = kzalloc(ASC_OVERRUN_BSIZE, GFP_KERNEL);
		if (!asc_dvc_varp->overrun_buf) {
			ret = -ENOMEM;
			goto err_free_irq;
		}
		warn_code = AscInitAsc1000Driver(asc_dvc_varp);

		if (warn_code || asc_dvc_varp->err_code) {
			shost_printk(KERN_ERR, shost, "error: init_state 0x%x, "
					"warn 0x%x, error 0x%x\n",
					asc_dvc_varp->init_state, warn_code,
					asc_dvc_varp->err_code);
			if (!asc_dvc_varp->overrun_dma) {
				ret = -ENODEV;
				goto err_free_mem;
			}
		}
	} else {
		if (advansys_wide_init_chip(shost)) {
			ret = -ENODEV;
			goto err_free_mem;
		}
	}

	ASC_DBG_PRT_SCSI_HOST(2, shost);

	ret = scsi_add_host(shost, boardp->dev);
	if (ret)
		goto err_free_mem;

	scsi_scan_host(shost);
	return 0;

 err_free_mem:
	if (ASC_NARROW_BOARD(boardp)) {
		if (asc_dvc_varp->overrun_dma)
			dma_unmap_single(boardp->dev, asc_dvc_varp->overrun_dma,
					 ASC_OVERRUN_BSIZE, DMA_FROM_DEVICE);
		kfree(asc_dvc_varp->overrun_buf);
	} else
		advansys_wide_free_mem(boardp);
 err_free_irq:
	free_irq(boardp->irq, shost);
 err_free_dma:
#ifdef CONFIG_ISA
	if (shost->dma_channel != NO_ISA_DMA)
		free_dma(shost->dma_channel);
#endif
 err_free_proc:
	kfree(boardp->prtbuf);
 err_unmap:
	if (boardp->ioremap_addr)
		iounmap(boardp->ioremap_addr);
 err_shost:
	return ret;
}

/*
 * advansys_release()
 *
 * Release resources allocated for a single AdvanSys adapter.
 */
static int advansys_release(struct Scsi_Host *shost)
{
	struct asc_board *board = shost_priv(shost);
	ASC_DBG(1, "begin\n");
	scsi_remove_host(shost);
	free_irq(board->irq, shost);
#ifdef CONFIG_ISA
	if (shost->dma_channel != NO_ISA_DMA) {
		ASC_DBG(1, "free_dma()\n");
		free_dma(shost->dma_channel);
	}
#endif
	if (ASC_NARROW_BOARD(board)) {
		dma_unmap_single(board->dev,
					board->dvc_var.asc_dvc_var.overrun_dma,
					ASC_OVERRUN_BSIZE, DMA_FROM_DEVICE);
		kfree(board->dvc_var.asc_dvc_var.overrun_buf);
	} else {
		iounmap(board->ioremap_addr);
		advansys_wide_free_mem(board);
	}
	kfree(board->prtbuf);
	scsi_host_put(shost);
	ASC_DBG(1, "end\n");
	return 0;
}

#define ASC_IOADR_TABLE_MAX_IX  11

static PortAddr _asc_def_iop_base[ASC_IOADR_TABLE_MAX_IX] = {
	0x100, 0x0110, 0x120, 0x0130, 0x140, 0x0150, 0x0190,
	0x0210, 0x0230, 0x0250, 0x0330
};

/*
 * The ISA IRQ number is found in bits 2 and 3 of the CfgLsw.  It decodes as:
 * 00: 10
 * 01: 11
 * 10: 12
 * 11: 15
 */
static unsigned int __devinit advansys_isa_irq_no(PortAddr iop_base)
{
	unsigned short cfg_lsw = AscGetChipCfgLsw(iop_base);
	unsigned int chip_irq = ((cfg_lsw >> 2) & 0x03) + 10;
	if (chip_irq == 13)
		chip_irq = 15;
	return chip_irq;
}

static int __devinit advansys_isa_probe(struct device *dev, unsigned int id)
{
	int err = -ENODEV;
	PortAddr iop_base = _asc_def_iop_base[id];
	struct Scsi_Host *shost;
	struct asc_board *board;

	if (!request_region(iop_base, ASC_IOADR_GAP, DRV_NAME)) {
		ASC_DBG(1, "I/O port 0x%x busy\n", iop_base);
		return -ENODEV;
	}
	ASC_DBG(1, "probing I/O port 0x%x\n", iop_base);
	if (!AscFindSignature(iop_base))
		goto release_region;
	if (!(AscGetChipVersion(iop_base, ASC_IS_ISA) & ASC_CHIP_VER_ISA_BIT))
		goto release_region;

	err = -ENOMEM;
	shost = scsi_host_alloc(&advansys_template, sizeof(*board));
	if (!shost)
		goto release_region;

	board = shost_priv(shost);
	board->irq = advansys_isa_irq_no(iop_base);
	board->dev = dev;

	err = advansys_board_found(shost, iop_base, ASC_IS_ISA);
	if (err)
		goto free_host;

	dev_set_drvdata(dev, shost);
	return 0;

 free_host:
	scsi_host_put(shost);
 release_region:
	release_region(iop_base, ASC_IOADR_GAP);
	return err;
}

static int __devexit advansys_isa_remove(struct device *dev, unsigned int id)
{
	int ioport = _asc_def_iop_base[id];
	advansys_release(dev_get_drvdata(dev));
	release_region(ioport, ASC_IOADR_GAP);
	return 0;
}

static struct isa_driver advansys_isa_driver = {
	.probe		= advansys_isa_probe,
	.remove		= __devexit_p(advansys_isa_remove),
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRV_NAME,
	},
};

/*
 * The VLB IRQ number is found in bits 2 to 4 of the CfgLsw.  It decodes as:
 * 000: invalid
 * 001: 10
 * 010: 11
 * 011: 12
 * 100: invalid
 * 101: 14
 * 110: 15
 * 111: invalid
 */
static unsigned int __devinit advansys_vlb_irq_no(PortAddr iop_base)
{
	unsigned short cfg_lsw = AscGetChipCfgLsw(iop_base);
	unsigned int chip_irq = ((cfg_lsw >> 2) & 0x07) + 9;
	if ((chip_irq < 10) || (chip_irq == 13) || (chip_irq > 15))
		return 0;
	return chip_irq;
}

static int __devinit advansys_vlb_probe(struct device *dev, unsigned int id)
{
	int err = -ENODEV;
	PortAddr iop_base = _asc_def_iop_base[id];
	struct Scsi_Host *shost;
	struct asc_board *board;

	if (!request_region(iop_base, ASC_IOADR_GAP, DRV_NAME)) {
		ASC_DBG(1, "I/O port 0x%x busy\n", iop_base);
		return -ENODEV;
	}
	ASC_DBG(1, "probing I/O port 0x%x\n", iop_base);
	if (!AscFindSignature(iop_base))
		goto release_region;
	/*
	 * I don't think this condition can actually happen, but the old
	 * driver did it, and the chances of finding a VLB setup in 2007
	 * to do testing with is slight to none.
	 */
	if (AscGetChipVersion(iop_base, ASC_IS_VL) > ASC_CHIP_MAX_VER_VL)
		goto release_region;

	err = -ENOMEM;
	shost = scsi_host_alloc(&advansys_template, sizeof(*board));
	if (!shost)
		goto release_region;

	board = shost_priv(shost);
	board->irq = advansys_vlb_irq_no(iop_base);
	board->dev = dev;

	err = advansys_board_found(shost, iop_base, ASC_IS_VL);
	if (err)
		goto free_host;

	dev_set_drvdata(dev, shost);
	return 0;

 free_host:
	scsi_host_put(shost);
 release_region:
	release_region(iop_base, ASC_IOADR_GAP);
	return -ENODEV;
}

static struct isa_driver advansys_vlb_driver = {
	.probe		= advansys_vlb_probe,
	.remove		= __devexit_p(advansys_isa_remove),
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "advansys_vlb",
	},
};

static struct eisa_device_id advansys_eisa_table[] __devinitdata = {
	{ "ABP7401" },
	{ "ABP7501" },
	{ "" }
};

MODULE_DEVICE_TABLE(eisa, advansys_eisa_table);

/*
 * EISA is a little more tricky than PCI; each EISA device may have two
 * channels, and this driver is written to make each channel its own Scsi_Host
 */
struct eisa_scsi_data {
	struct Scsi_Host *host[2];
};

/*
 * The EISA IRQ number is found in bits 8 to 10 of the CfgLsw.  It decodes as:
 * 000: 10
 * 001: 11
 * 010: 12
 * 011: invalid
 * 100: 14
 * 101: 15
 * 110: invalid
 * 111: invalid
 */
static unsigned int __devinit advansys_eisa_irq_no(struct eisa_device *edev)
{
	unsigned short cfg_lsw = inw(edev->base_addr + 0xc86);
	unsigned int chip_irq = ((cfg_lsw >> 8) & 0x07) + 10;
	if ((chip_irq == 13) || (chip_irq > 15))
		return 0;
	return chip_irq;
}

static int __devinit advansys_eisa_probe(struct device *dev)
{
	int i, ioport, irq = 0;
	int err;
	struct eisa_device *edev = to_eisa_device(dev);
	struct eisa_scsi_data *data;

	err = -ENOMEM;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto fail;
	ioport = edev->base_addr + 0xc30;

	err = -ENODEV;
	for (i = 0; i < 2; i++, ioport += 0x20) {
		struct asc_board *board;
		struct Scsi_Host *shost;
		if (!request_region(ioport, ASC_IOADR_GAP, DRV_NAME)) {
			printk(KERN_WARNING "Region %x-%x busy\n", ioport,
			       ioport + ASC_IOADR_GAP - 1);
			continue;
		}
		if (!AscFindSignature(ioport)) {
			release_region(ioport, ASC_IOADR_GAP);
			continue;
		}

		/*
		 * I don't know why we need to do this for EISA chips, but
		 * not for any others.  It looks to be equivalent to
		 * AscGetChipCfgMsw, but I may have overlooked something,
		 * so I'm not converting it until I get an EISA board to
		 * test with.
		 */
		inw(ioport + 4);

		if (!irq)
			irq = advansys_eisa_irq_no(edev);

		err = -ENOMEM;
		shost = scsi_host_alloc(&advansys_template, sizeof(*board));
		if (!shost)
			goto release_region;

		board = shost_priv(shost);
		board->irq = irq;
		board->dev = dev;

		err = advansys_board_found(shost, ioport, ASC_IS_EISA);
		if (!err) {
			data->host[i] = shost;
			continue;
		}

		scsi_host_put(shost);
 release_region:
		release_region(ioport, ASC_IOADR_GAP);
		break;
	}

	if (err)
		goto free_data;
	dev_set_drvdata(dev, data);
	return 0;

 free_data:
	kfree(data->host[0]);
	kfree(data->host[1]);
	kfree(data);
 fail:
	return err;
}

static __devexit int advansys_eisa_remove(struct device *dev)
{
	int i;
	struct eisa_scsi_data *data = dev_get_drvdata(dev);

	for (i = 0; i < 2; i++) {
		int ioport;
		struct Scsi_Host *shost = data->host[i];
		if (!shost)
			continue;
		ioport = shost->io_port;
		advansys_release(shost);
		release_region(ioport, ASC_IOADR_GAP);
	}

	kfree(data);
	return 0;
}

static struct eisa_driver advansys_eisa_driver = {
	.id_table =		advansys_eisa_table,
	.driver = {
		.name =		DRV_NAME,
		.probe =	advansys_eisa_probe,
		.remove =	__devexit_p(advansys_eisa_remove),
	}
};

/* PCI Devices supported by this driver */
static struct pci_device_id advansys_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_1200A,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940U,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_ASP_ABP940UW,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_38C0800_REV1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ASP, PCI_DEVICE_ID_38C1600_REV1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{}
};

MODULE_DEVICE_TABLE(pci, advansys_pci_tbl);

static void __devinit advansys_set_latency(struct pci_dev *pdev)
{
	if ((pdev->device == PCI_DEVICE_ID_ASP_1200A) ||
	    (pdev->device == PCI_DEVICE_ID_ASP_ABP940)) {
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0);
	} else {
		u8 latency;
		pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &latency);
		if (latency < 0x20)
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x20);
	}
}

static int __devinit
advansys_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err, ioport;
	struct Scsi_Host *shost;
	struct asc_board *board;

	err = pci_enable_device(pdev);
	if (err)
		goto fail;
	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto disable_device;
	pci_set_master(pdev);
	advansys_set_latency(pdev);

	err = -ENODEV;
	if (pci_resource_len(pdev, 0) == 0)
		goto release_region;

	ioport = pci_resource_start(pdev, 0);

	err = -ENOMEM;
	shost = scsi_host_alloc(&advansys_template, sizeof(*board));
	if (!shost)
		goto release_region;

	board = shost_priv(shost);
	board->irq = pdev->irq;
	board->dev = &pdev->dev;

	if (pdev->device == PCI_DEVICE_ID_ASP_ABP940UW ||
	    pdev->device == PCI_DEVICE_ID_38C0800_REV1 ||
	    pdev->device == PCI_DEVICE_ID_38C1600_REV1) {
		board->flags |= ASC_IS_WIDE_BOARD;
	}

	err = advansys_board_found(shost, ioport, ASC_IS_PCI);
	if (err)
		goto free_host;

	pci_set_drvdata(pdev, shost);
	return 0;

 free_host:
	scsi_host_put(shost);
 release_region:
	pci_release_regions(pdev);
 disable_device:
	pci_disable_device(pdev);
 fail:
	return err;
}

static void __devexit advansys_pci_remove(struct pci_dev *pdev)
{
	advansys_release(pci_get_drvdata(pdev));
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver advansys_pci_driver = {
	.name =		DRV_NAME,
	.id_table =	advansys_pci_tbl,
	.probe =	advansys_pci_probe,
	.remove =	__devexit_p(advansys_pci_remove),
};

static int __init advansys_init(void)
{
	int error;

	error = isa_register_driver(&advansys_isa_driver,
				    ASC_IOADR_TABLE_MAX_IX);
	if (error)
		goto fail;

	error = isa_register_driver(&advansys_vlb_driver,
				    ASC_IOADR_TABLE_MAX_IX);
	if (error)
		goto unregister_isa;

	error = eisa_driver_register(&advansys_eisa_driver);
	if (error)
		goto unregister_vlb;

	error = pci_register_driver(&advansys_pci_driver);
	if (error)
		goto unregister_eisa;

	return 0;

 unregister_eisa:
	eisa_driver_unregister(&advansys_eisa_driver);
 unregister_vlb:
	isa_unregister_driver(&advansys_vlb_driver);
 unregister_isa:
	isa_unregister_driver(&advansys_isa_driver);
 fail:
	return error;
}

static void __exit advansys_exit(void)
{
	pci_unregister_driver(&advansys_pci_driver);
	eisa_driver_unregister(&advansys_eisa_driver);
	isa_unregister_driver(&advansys_vlb_driver);
	isa_unregister_driver(&advansys_isa_driver);
}

module_init(advansys_init);
module_exit(advansys_exit);

MODULE_LICENSE("GPL");
MODULE_FIRMWARE("advansys/mcode.bin");
MODULE_FIRMWARE("advansys/3550.bin");
MODULE_FIRMWARE("advansys/38C0800.bin");
MODULE_FIRMWARE("advansys/38C1600.bin");
