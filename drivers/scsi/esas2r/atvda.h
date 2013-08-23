/*  linux/drivers/scsi/esas2r/atvda.h
 *       ATTO VDA interface definitions
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  NO WARRANTY
 *  THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 *  LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 *  solely responsible for determining the appropriateness of using and
 *  distributing the Program and assumes all risks associated with its
 *  exercise of rights under this Agreement, including but not limited to
 *  the risks and costs of program errors, damage to or loss of data,
 *  programs or equipment, and unavailability or interruption of operations.
 *
 *  DISCLAIMER OF LIABILITY
 *  NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 *  HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/


#ifndef ATVDA_H
#define ATVDA_H

struct __packed atto_dev_addr {
	u64 dev_port;
	u64 hba_port;
	u8 lun;
	u8 flags;
	   #define VDA_DEVADDRF_SATA   0x01
	   #define VDA_DEVADDRF_SSD    0x02
	u8 link_speed; /* VDALINKSPEED_xxx */
	u8 pad[1];
};

/* dev_addr2 was added for 64-bit alignment */

struct __packed atto_dev_addr2 {
	u64 dev_port;
	u64 hba_port;
	u8 lun;
	u8 flags;
	u8 link_speed;
	u8 pad[5];
};

struct __packed atto_vda_sge {
	u32 length;
	u64 address;
};


/* VDA request function codes */

#define VDA_FUNC_SCSI     0x00
#define VDA_FUNC_FLASH    0x01
#define VDA_FUNC_DIAG     0x02
#define VDA_FUNC_AE       0x03
#define VDA_FUNC_CLI      0x04
#define VDA_FUNC_IOCTL    0x05
#define VDA_FUNC_CFG      0x06
#define VDA_FUNC_MGT      0x07
#define VDA_FUNC_GSV      0x08


/* VDA request status values.  for host driver considerations, values for
 * SCSI requests start at zero.  other requests may use these values as well. */

#define RS_SUCCESS          0x00        /*! successful completion            */
#define RS_INV_FUNC         0x01        /*! invalid command function         */
#define RS_BUSY             0x02        /*! insufficient resources           */
#define RS_SEL              0x03        /*! no target at target_id           */
#define RS_NO_LUN           0x04        /*! invalid LUN                      */
#define RS_TIMEOUT          0x05        /*! request timeout                  */
#define RS_OVERRUN          0x06        /*! data overrun                     */
#define RS_UNDERRUN         0x07        /*! data underrun                    */
#define RS_SCSI_ERROR       0x08        /*! SCSI error occurred              */
#define RS_ABORTED          0x0A        /*! command aborted                  */
#define RS_RESID_MISM       0x0B        /*! residual length incorrect        */
#define RS_TM_FAILED        0x0C        /*! task management failed           */
#define RS_RESET            0x0D        /*! aborted due to bus reset         */
#define RS_ERR_DMA_SG       0x0E        /*! error reading SG list            */
#define RS_ERR_DMA_DATA     0x0F        /*! error transferring data          */
#define RS_UNSUPPORTED      0x10        /*! unsupported request              */
#define RS_SEL2             0x70        /*! internal generated RS_SEL        */
#define RS_VDA_BASE         0x80        /*! base of VDA-specific errors      */
#define RS_MGT_BASE         0x80        /*! base of VDA management errors    */
#define RS_SCAN_FAIL        (RS_MGT_BASE + 0x00)
#define RS_DEV_INVALID      (RS_MGT_BASE + 0x01)
#define RS_DEV_ASSIGNED     (RS_MGT_BASE + 0x02)
#define RS_DEV_REMOVE       (RS_MGT_BASE + 0x03)
#define RS_DEV_LOST         (RS_MGT_BASE + 0x04)
#define RS_SCAN_GEN         (RS_MGT_BASE + 0x05)
#define RS_GRP_INVALID      (RS_MGT_BASE + 0x08)
#define RS_GRP_EXISTS       (RS_MGT_BASE + 0x09)
#define RS_GRP_LIMIT        (RS_MGT_BASE + 0x0A)
#define RS_GRP_INTLV        (RS_MGT_BASE + 0x0B)
#define RS_GRP_SPAN         (RS_MGT_BASE + 0x0C)
#define RS_GRP_TYPE         (RS_MGT_BASE + 0x0D)
#define RS_GRP_MEMBERS      (RS_MGT_BASE + 0x0E)
#define RS_GRP_COMMIT       (RS_MGT_BASE + 0x0F)
#define RS_GRP_REBUILD      (RS_MGT_BASE + 0x10)
#define RS_GRP_REBUILD_TYPE (RS_MGT_BASE + 0x11)
#define RS_GRP_BLOCK_SIZE   (RS_MGT_BASE + 0x12)
#define RS_CFG_SAVE         (RS_MGT_BASE + 0x14)
#define RS_PART_LAST        (RS_MGT_BASE + 0x18)
#define RS_ELEM_INVALID     (RS_MGT_BASE + 0x19)
#define RS_PART_MAPPED      (RS_MGT_BASE + 0x1A)
#define RS_PART_TARGET      (RS_MGT_BASE + 0x1B)
#define RS_PART_LUN         (RS_MGT_BASE + 0x1C)
#define RS_PART_DUP         (RS_MGT_BASE + 0x1D)
#define RS_PART_NOMAP       (RS_MGT_BASE + 0x1E)
#define RS_PART_MAX         (RS_MGT_BASE + 0x1F)
#define RS_PART_CAP         (RS_MGT_BASE + 0x20)
#define RS_PART_STATE       (RS_MGT_BASE + 0x21)
#define RS_TEST_IN_PROG     (RS_MGT_BASE + 0x22)
#define RS_METRICS_ERROR    (RS_MGT_BASE + 0x23)
#define RS_HS_ERROR         (RS_MGT_BASE + 0x24)
#define RS_NO_METRICS_TEST  (RS_MGT_BASE + 0x25)
#define RS_BAD_PARAM        (RS_MGT_BASE + 0x26)
#define RS_GRP_MEMBER_SIZE  (RS_MGT_BASE + 0x27)
#define RS_FLS_BASE         0xB0        /*! base of VDA errors               */
#define RS_FLS_ERR_AREA     (RS_FLS_BASE + 0x00)
#define RS_FLS_ERR_BUSY     (RS_FLS_BASE + 0x01)
#define RS_FLS_ERR_RANGE    (RS_FLS_BASE + 0x02)
#define RS_FLS_ERR_BEGIN    (RS_FLS_BASE + 0x03)
#define RS_FLS_ERR_CHECK    (RS_FLS_BASE + 0x04)
#define RS_FLS_ERR_FAIL     (RS_FLS_BASE + 0x05)
#define RS_FLS_ERR_RSRC     (RS_FLS_BASE + 0x06)
#define RS_FLS_ERR_NOFILE   (RS_FLS_BASE + 0x07)
#define RS_FLS_ERR_FSIZE    (RS_FLS_BASE + 0x08)
#define RS_CFG_BASE         0xC0        /*! base of VDA configuration errors */
#define RS_CFG_ERR_BUSY     (RS_CFG_BASE + 0)
#define RS_CFG_ERR_SGE      (RS_CFG_BASE + 1)
#define RS_CFG_ERR_DATE     (RS_CFG_BASE + 2)
#define RS_CFG_ERR_TIME     (RS_CFG_BASE + 3)
#define RS_DEGRADED         0xFB        /*! degraded mode                    */
#define RS_CLI_INTERNAL     0xFC        /*! VDA CLI internal error           */
#define RS_VDA_INTERNAL     0xFD        /*! catch-all                        */
#define RS_PENDING          0xFE        /*! pending, not started             */
#define RS_STARTED          0xFF        /*! started                          */


/* flash request subfunctions.  these are used in both the IOCTL and the
 * driver-firmware interface (VDA_FUNC_FLASH). */

#define VDA_FLASH_BEGINW  0x00
#define VDA_FLASH_READ    0x01
#define VDA_FLASH_WRITE   0x02
#define VDA_FLASH_COMMIT  0x03
#define VDA_FLASH_CANCEL  0x04
#define VDA_FLASH_INFO    0x05
#define VDA_FLASH_FREAD   0x06
#define VDA_FLASH_FWRITE  0x07
#define VDA_FLASH_FINFO   0x08


/* IOCTL request subfunctions.  these identify the payload type for
 * VDA_FUNC_IOCTL.
 */

#define VDA_IOCTL_HBA     0x00
#define VDA_IOCTL_CSMI    0x01
#define VDA_IOCTL_SMP     0x02

struct __packed atto_vda_devinfo {
	struct atto_dev_addr dev_addr;
	u8 vendor_id[8];
	u8 product_id[16];
	u8 revision[4];
	u64 capacity;
	u32 block_size;
	u8 dev_type;

	union {
		u8 dev_status;
	    #define VDADEVSTAT_INVALID   0x00
	    #define VDADEVSTAT_CORRUPT   VDADEVSTAT_INVALID
	    #define VDADEVSTAT_ASSIGNED  0x01
	    #define VDADEVSTAT_SPARE     0x02
	    #define VDADEVSTAT_UNAVAIL   0x03
	    #define VDADEVSTAT_PT_MAINT  0x04
	    #define VDADEVSTAT_LCLSPARE  0x05
	    #define VDADEVSTAT_UNUSEABLE 0x06
	    #define VDADEVSTAT_AVAIL     0xFF

		u8 op_ctrl;
	    #define VDA_DEV_OP_CTRL_START   0x01
	    #define VDA_DEV_OP_CTRL_HALT    0x02
	    #define VDA_DEV_OP_CTRL_RESUME  0x03
	    #define VDA_DEV_OP_CTRL_CANCEL  0x04
	};

	u8 member_state;
	#define VDAMBRSTATE_ONLINE   0x00
	#define VDAMBRSTATE_DEGRADED 0x01
	#define VDAMBRSTATE_UNAVAIL  0x02
	#define VDAMBRSTATE_FAULTED  0x03
	#define VDAMBRSTATE_MISREAD  0x04
	#define VDAMBRSTATE_INCOMPAT 0x05

	u8 operation;
	#define VDAOP_NONE           0x00
	#define VDAOP_REBUILD        0x01
	#define VDAOP_ERASE          0x02
	#define VDAOP_PATTERN        0x03
	#define VDAOP_CONVERSION     0x04
	#define VDAOP_FULL_INIT      0x05
	#define VDAOP_QUICK_INIT     0x06
	#define VDAOP_SECT_SCAN      0x07
	#define VDAOP_SECT_SCAN_PARITY      0x08
	#define VDAOP_SECT_SCAN_PARITY_FIX  0x09
	#define VDAOP_RECOV_REBUILD  0x0A

	u8 op_status;
	#define VDAOPSTAT_OK         0x00
	#define VDAOPSTAT_FAULTED    0x01
	#define VDAOPSTAT_HALTED     0x02
	#define VDAOPSTAT_INT        0x03

	u8 progress; /* 0 - 100% */
	u16 ses_dev_index;
	#define VDASESDI_INVALID     0xFFFF

	u8 serial_no[32];

	union {
		u16 target_id;
	#define VDATGTID_INVALID     0xFFFF

		u16 features_mask;
	};

	u16 lun;
	u16 features;
	#define VDADEVFEAT_ENC_SERV  0x0001
	#define VDADEVFEAT_IDENT     0x0002
	#define VDADEVFEAT_DH_SUPP   0x0004
	#define VDADEVFEAT_PHYS_ID   0x0008

	u8 ses_element_id;
	u8 link_speed;
	#define VDALINKSPEED_UNKNOWN 0x00
	#define VDALINKSPEED_1GB     0x01
	#define VDALINKSPEED_1_5GB   0x02
	#define VDALINKSPEED_2GB     0x03
	#define VDALINKSPEED_3GB     0x04
	#define VDALINKSPEED_4GB     0x05
	#define VDALINKSPEED_6GB     0x06
	#define VDALINKSPEED_8GB     0x07

	u16 phys_target_id;
	u8 reserved[2];
};


/*! struct atto_vda_devinfo2 is a replacement for atto_vda_devinfo.  it
 * extends beyond the 0x70 bytes allowed in atto_vda_mgmt_req; therefore,
 * the entire structure is DMaed between the firmware and host buffer and
 * the data will always be in little endian format.
 */

struct __packed atto_vda_devinfo2 {
	struct atto_dev_addr dev_addr;
	u8 vendor_id[8];
	u8 product_id[16];
	u8 revision[4];
	u64 capacity;
	u32 block_size;
	u8 dev_type;
	u8 dev_status;
	u8 member_state;
	u8 operation;
	u8 op_status;
	u8 progress;
	u16 ses_dev_index;
	u8 serial_no[32];
	union {
		u16 target_id;
		u16 features_mask;
	};

	u16 lun;
	u16 features;
	u8 ses_element_id;
	u8 link_speed;
	u16 phys_target_id;
	u8 reserved[2];

/* This is where fields specific to struct atto_vda_devinfo2 begin.  Note
 * that the structure version started at one so applications that unionize this
 * structure with atto_vda_dev_info can differentiate them if desired.
 */

	u8 version;
	#define VDADEVINFO_VERSION0         0x00
	#define VDADEVINFO_VERSION1         0x01
	#define VDADEVINFO_VERSION2         0x02
	#define VDADEVINFO_VERSION3         0x03
	#define VDADEVINFO_VERSION          VDADEVINFO_VERSION3

	u8 reserved2[3];

	/* sector scanning fields */

	u32 ss_curr_errors;
	u64 ss_curr_scanned;
	u32 ss_curr_recvrd;
	u32 ss_scan_length;
	u32 ss_total_errors;
	u32 ss_total_recvrd;
	u32 ss_num_scans;

	/* grp_name was added in version 2 of this structure. */

	char grp_name[15];
	u8 reserved3[4];

	/* dev_addr_list was added in version 3 of this structure. */

	u8 num_dev_addr;
	struct atto_dev_addr2 dev_addr_list[8];
};


struct __packed atto_vda_grp_info {
	u8 grp_index;
	#define VDA_MAX_RAID_GROUPS         32

	char grp_name[15];
	u64 capacity;
	u32 block_size;
	u32 interleave;
	u8 type;
	#define VDA_GRP_TYPE_RAID0          0
	#define VDA_GRP_TYPE_RAID1          1
	#define VDA_GRP_TYPE_RAID4          4
	#define VDA_GRP_TYPE_RAID5          5
	#define VDA_GRP_TYPE_RAID6          6
	#define VDA_GRP_TYPE_RAID10         10
	#define VDA_GRP_TYPE_RAID40         40
	#define VDA_GRP_TYPE_RAID50         50
	#define VDA_GRP_TYPE_RAID60         60
	#define VDA_GRP_TYPE_DVRAID_HS      252
	#define VDA_GRP_TYPE_DVRAID_NOHS    253
	#define VDA_GRP_TYPE_JBOD           254
	#define VDA_GRP_TYPE_SPARE          255

	union {
		u8 status;
	    #define VDA_GRP_STAT_INVALID  0x00
	    #define VDA_GRP_STAT_NEW      0x01
	    #define VDA_GRP_STAT_WAITING  0x02
	    #define VDA_GRP_STAT_ONLINE   0x03
	    #define VDA_GRP_STAT_DEGRADED 0x04
	    #define VDA_GRP_STAT_OFFLINE  0x05
	    #define VDA_GRP_STAT_DELETED  0x06
	    #define VDA_GRP_STAT_RECOV_BASIC    0x07
	    #define VDA_GRP_STAT_RECOV_EXTREME  0x08

		u8 op_ctrl;
	    #define VDA_GRP_OP_CTRL_START   0x01
	    #define VDA_GRP_OP_CTRL_HALT    0x02
	    #define VDA_GRP_OP_CTRL_RESUME  0x03
	    #define VDA_GRP_OP_CTRL_CANCEL  0x04
	};

	u8 rebuild_state;
	#define VDA_RBLD_NONE      0x00
	#define VDA_RBLD_REBUILD   0x01
	#define VDA_RBLD_ERASE     0x02
	#define VDA_RBLD_PATTERN   0x03
	#define VDA_RBLD_CONV      0x04
	#define VDA_RBLD_FULL_INIT 0x05
	#define VDA_RBLD_QUICK_INIT 0x06
	#define VDA_RBLD_SECT_SCAN 0x07
	#define VDA_RBLD_SECT_SCAN_PARITY     0x08
	#define VDA_RBLD_SECT_SCAN_PARITY_FIX 0x09
	#define VDA_RBLD_RECOV_REBUILD 0x0A
	#define VDA_RBLD_RECOV_BASIC   0x0B
	#define VDA_RBLD_RECOV_EXTREME 0x0C

	u8 span_depth;
	u8 progress;
	u8 mirror_width;
	u8 stripe_width;
	u8 member_cnt;

	union {
		u16 members[32];
	#define VDA_MEMBER_MISSING  0xFFFF
	#define VDA_MEMBER_NEW      0xFFFE
		u16 features_mask;
	};

	u16 features;
	#define VDA_GRP_FEAT_HOTSWAP    0x0001
	#define VDA_GRP_FEAT_SPDRD_MASK 0x0006
	#define VDA_GRP_FEAT_SPDRD_DIS  0x0000
	#define VDA_GRP_FEAT_SPDRD_ENB  0x0002
	#define VDA_GRP_FEAT_SPDRD_AUTO 0x0004
	#define VDA_GRP_FEAT_IDENT      0x0008
	#define VDA_GRP_FEAT_RBLDPRI_MASK 0x0030
	#define VDA_GRP_FEAT_RBLDPRI_LOW  0x0010
	#define VDA_GRP_FEAT_RBLDPRI_SAME 0x0020
	#define VDA_GRP_FEAT_RBLDPRI_HIGH 0x0030
	#define VDA_GRP_FEAT_WRITE_CACHE  0x0040
	#define VDA_GRP_FEAT_RBLD_RESUME  0x0080
	#define VDA_GRP_FEAT_SECT_RESUME  0x0100
	#define VDA_GRP_FEAT_INIT_RESUME  0x0200
	#define VDA_GRP_FEAT_SSD          0x0400
	#define VDA_GRP_FEAT_BOOT_DEV     0x0800

	/*
	 * for backward compatibility, a prefetch value of zero means the
	 * setting is ignored/unsupported.  therefore, the firmware supported
	 * 0-6 values are incremented to 1-7.
	 */

	u8 prefetch;
	u8 op_status;
	#define VDAGRPOPSTAT_MASK       0x0F
	#define VDAGRPOPSTAT_INVALID    0x00
	#define VDAGRPOPSTAT_OK         0x01
	#define VDAGRPOPSTAT_FAULTED    0x02
	#define VDAGRPOPSTAT_HALTED     0x03
	#define VDAGRPOPSTAT_INT        0x04
	#define VDAGRPOPPROC_MASK       0xF0
	#define VDAGRPOPPROC_STARTABLE  0x10
	#define VDAGRPOPPROC_CANCELABLE 0x20
	#define VDAGRPOPPROC_RESUMABLE  0x40
	#define VDAGRPOPPROC_HALTABLE   0x80
	u8 over_provision;
	u8 reserved[3];

};


struct __packed atto_vdapart_info {
	u8 part_no;
	#define VDA_MAX_PARTITIONS   128

	char grp_name[15];
	u64 part_size;
	u64 start_lba;
	u32 block_size;
	u16 target_id;
	u8 LUN;
	char serial_no[41];
	u8 features;
	#define VDAPI_FEAT_WRITE_CACHE   0x01

	u8 reserved[7];
};


struct __packed atto_vda_dh_info {
	u8 req_type;
	#define VDADH_RQTYPE_CACHE      0x01
	#define VDADH_RQTYPE_FETCH      0x02
	#define VDADH_RQTYPE_SET_STAT   0x03
	#define VDADH_RQTYPE_GET_STAT   0x04

	u8 req_qual;
	#define VDADH_RQQUAL_SMART      0x01
	#define VDADH_RQQUAL_MEDDEF     0x02
	#define VDADH_RQQUAL_INFOEXC    0x04

	u8 num_smart_attribs;
	u8 status;
	#define VDADH_STAT_DISABLE      0x00
	#define VDADH_STAT_ENABLE       0x01

	u32 med_defect_cnt;
	u32 info_exc_cnt;
	u8 smart_status;
	#define VDADH_SMARTSTAT_OK      0x00
	#define VDADH_SMARTSTAT_ERR     0x01

	u8 reserved[35];
	struct atto_vda_sge sge[1];
};


struct __packed atto_vda_dh_smart {
	u8 attrib_id;
	u8 current_val;
	u8 worst;
	u8 threshold;
	u8 raw_data[6];
	u8 raw_attrib_status;
	#define VDADHSM_RAWSTAT_PREFAIL_WARRANTY        0x01
	#define VDADHSM_RAWSTAT_ONLINE_COLLECTION       0x02
	#define VDADHSM_RAWSTAT_PERFORMANCE_ATTR        0x04
	#define VDADHSM_RAWSTAT_ERROR_RATE_ATTR         0x08
	#define VDADHSM_RAWSTAT_EVENT_COUNT_ATTR        0x10
	#define VDADHSM_RAWSTAT_SELF_PRESERVING_ATTR    0x20

	u8 calc_attrib_status;
	#define VDADHSM_CALCSTAT_UNKNOWN                0x00
	#define VDADHSM_CALCSTAT_GOOD                   0x01
	#define VDADHSM_CALCSTAT_PREFAIL                0x02
	#define VDADHSM_CALCSTAT_OLDAGE                 0x03

	u8 reserved[4];
};


struct __packed atto_vda_metrics_info {
	u8 data_version;
	#define VDAMET_VERSION0         0x00
	#define VDAMET_VERSION          VDAMET_VERSION0

	u8 metrics_action;
	#define VDAMET_METACT_NONE      0x00
	#define VDAMET_METACT_START     0x01
	#define VDAMET_METACT_STOP      0x02
	#define VDAMET_METACT_RETRIEVE  0x03
	#define VDAMET_METACT_CLEAR     0x04

	u8 test_action;
	#define VDAMET_TSTACT_NONE              0x00
	#define VDAMET_TSTACT_STRT_INIT         0x01
	#define VDAMET_TSTACT_STRT_READ         0x02
	#define VDAMET_TSTACT_STRT_VERIFY       0x03
	#define VDAMET_TSTACT_STRT_INIT_VERIFY  0x04
	#define VDAMET_TSTACT_STOP              0x05

	u8 num_dev_indexes;
	#define VDAMET_ALL_DEVICES      0xFF

	u16 dev_indexes[32];
	u8 reserved[12];
	struct atto_vda_sge sge[1];
};


struct __packed atto_vda_metrics_data {
	u16 dev_index;
	u16 length;
	#define VDAMD_LEN_LAST          0x8000
	#define VDAMD_LEN_MASK          0x0FFF

	u32 flags;
	#define VDAMDF_RUN          0x00000007
	#define VDAMDF_RUN_READ     0x00000001
	#define VDAMDF_RUN_WRITE    0x00000002
	#define VDAMDF_RUN_ALL      0x00000004
	#define VDAMDF_READ         0x00000010
	#define VDAMDF_WRITE        0x00000020
	#define VDAMDF_ALL          0x00000040
	#define VDAMDF_DRIVETEST    0x40000000
	#define VDAMDF_NEW          0x80000000

	u64 total_read_data;
	u64 total_write_data;
	u64 total_read_io;
	u64 total_write_io;
	u64 read_start_time;
	u64 read_stop_time;
	u64 write_start_time;
	u64 write_stop_time;
	u64 read_maxio_time;
	u64 wpvdadmetricsdatarite_maxio_time;
	u64 read_totalio_time;
	u64 write_totalio_time;
	u64 read_total_errs;
	u64 write_total_errs;
	u64 read_recvrd_errs;
	u64 write_recvrd_errs;
	u64 miscompares;
};


struct __packed atto_vda_schedule_info {
	u8 schedule_type;
	#define VDASI_SCHTYPE_ONETIME   0x01
	#define VDASI_SCHTYPE_DAILY     0x02
	#define VDASI_SCHTYPE_WEEKLY    0x03

	u8 operation;
	#define VDASI_OP_NONE           0x00
	#define VDASI_OP_CREATE         0x01
	#define VDASI_OP_CANCEL         0x02

	u8 hour;
	u8 minute;
	u8 day;
	#define VDASI_DAY_NONE          0x00

	u8 progress;
	#define VDASI_PROG_NONE         0xFF

	u8 event_type;
	#define VDASI_EVTTYPE_SECT_SCAN             0x01
	#define VDASI_EVTTYPE_SECT_SCAN_PARITY      0x02
	#define VDASI_EVTTYPE_SECT_SCAN_PARITY_FIX  0x03

	u8 recurrences;
	#define VDASI_RECUR_FOREVER     0x00

	u32 id;
	#define VDASI_ID_NONE           0x00

	char grp_name[15];
	u8 reserved[85];
};


struct __packed atto_vda_n_vcache_info {
	u8 super_cap_status;
	#define VDANVCI_SUPERCAP_NOT_PRESENT       0x00
	#define VDANVCI_SUPERCAP_FULLY_CHARGED     0x01
	#define VDANVCI_SUPERCAP_NOT_CHARGED       0x02

	u8 nvcache_module_status;
	#define VDANVCI_NVCACHEMODULE_NOT_PRESENT  0x00
	#define VDANVCI_NVCACHEMODULE_PRESENT      0x01

	u8 protection_mode;
	#define VDANVCI_PROTMODE_HI_PROTECT        0x00
	#define VDANVCI_PROTMODE_HI_PERFORM        0x01

	u8 reserved[109];
};


struct __packed atto_vda_buzzer_info {
	u8 status;
	#define VDABUZZI_BUZZER_OFF           0x00
	#define VDABUZZI_BUZZER_ON            0x01
	#define VDABUZZI_BUZZER_LAST          0x02

	u8 reserved[3];
	u32 duration;
	#define VDABUZZI_DURATION_INDEFINITE  0xffffffff

	u8 reserved2[104];
};


struct  __packed atto_vda_adapter_info {
	u8 version;
	#define VDAADAPINFO_VERSION0         0x00
	#define VDAADAPINFO_VERSION          VDAADAPINFO_VERSION0

	u8 reserved;
	signed short utc_offset;
	u32 utc_time;
	u32 features;
	#define VDA_ADAP_FEAT_IDENT     0x0001
	#define VDA_ADAP_FEAT_BUZZ_ERR  0x0002
	#define VDA_ADAP_FEAT_UTC_TIME  0x0004

	u32 valid_features;
	char active_config[33];
	u8 temp_count;
	u8 fan_count;
	u8 reserved3[61];
};


struct __packed atto_vda_temp_info {
	u8 temp_index;
	u8 max_op_temp;
	u8 min_op_temp;
	u8 op_temp_warn;
	u8 temperature;
	u8 type;
	#define VDA_TEMP_TYPE_CPU  1

	u8 reserved[106];
};


struct __packed atto_vda_fan_info {
	u8 fan_index;
	u8 status;
	#define VDA_FAN_STAT_UNKNOWN 0
	#define VDA_FAN_STAT_NORMAL  1
	#define VDA_FAN_STAT_FAIL    2

	u16 crit_pvdafaninfothreshold;
	u16 warn_threshold;
	u16 speed;
	u8 reserved[104];
};


/* VDA management commands */

#define VDAMGT_DEV_SCAN         0x00
#define VDAMGT_DEV_INFO         0x01
#define VDAMGT_DEV_CLEAN        0x02
#define VDAMGT_DEV_IDENTIFY     0x03
#define VDAMGT_DEV_IDENTSTOP    0x04
#define VDAMGT_DEV_PT_INFO      0x05
#define VDAMGT_DEV_FEATURES     0x06
#define VDAMGT_DEV_PT_FEATURES  0x07
#define VDAMGT_DEV_HEALTH_REQ   0x08
#define VDAMGT_DEV_METRICS      0x09
#define VDAMGT_DEV_INFO2        0x0A
#define VDAMGT_DEV_OPERATION    0x0B
#define VDAMGT_DEV_INFO2_BYADDR 0x0C
#define VDAMGT_GRP_INFO         0x10
#define VDAMGT_GRP_CREATE       0x11
#define VDAMGT_GRP_DELETE       0x12
#define VDAMGT_ADD_STORAGE      0x13
#define VDAMGT_MEMBER_ADD       0x14
#define VDAMGT_GRP_COMMIT       0x15
#define VDAMGT_GRP_REBUILD      0x16
#define VDAMGT_GRP_COMMIT_INIT  0x17
#define VDAMGT_QUICK_RAID       0x18
#define VDAMGT_GRP_FEATURES     0x19
#define VDAMGT_GRP_COMMIT_INIT_AUTOMAP  0x1A
#define VDAMGT_QUICK_RAID_INIT_AUTOMAP  0x1B
#define VDAMGT_GRP_OPERATION    0x1C
#define VDAMGT_CFG_SAVE         0x20
#define VDAMGT_LAST_ERROR       0x21
#define VDAMGT_ADAP_INFO        0x22
#define VDAMGT_ADAP_FEATURES    0x23
#define VDAMGT_TEMP_INFO        0x24
#define VDAMGT_FAN_INFO         0x25
#define VDAMGT_PART_INFO        0x30
#define VDAMGT_PART_MAP         0x31
#define VDAMGT_PART_UNMAP       0x32
#define VDAMGT_PART_AUTOMAP     0x33
#define VDAMGT_PART_SPLIT       0x34
#define VDAMGT_PART_MERGE       0x35
#define VDAMGT_SPARE_LIST       0x40
#define VDAMGT_SPARE_ADD        0x41
#define VDAMGT_SPARE_REMOVE     0x42
#define VDAMGT_LOCAL_SPARE_ADD  0x43
#define VDAMGT_SCHEDULE_EVENT   0x50
#define VDAMGT_SCHEDULE_INFO    0x51
#define VDAMGT_NVCACHE_INFO     0x60
#define VDAMGT_NVCACHE_SET      0x61
#define VDAMGT_BUZZER_INFO      0x70
#define VDAMGT_BUZZER_SET       0x71


struct __packed atto_vda_ae_hdr {
	u8 bylength;
	u8 byflags;
	#define VDAAE_HDRF_EVENT_ACK    0x01

	u8 byversion;
	#define VDAAE_HDR_VER_0         0

	u8 bytype;
	#define VDAAE_HDR_TYPE_RAID     1
	#define VDAAE_HDR_TYPE_LU       2
	#define VDAAE_HDR_TYPE_DISK     3
	#define VDAAE_HDR_TYPE_RESET    4
	#define VDAAE_HDR_TYPE_LOG_INFO 5
	#define VDAAE_HDR_TYPE_LOG_WARN 6
	#define VDAAE_HDR_TYPE_LOG_CRIT 7
	#define VDAAE_HDR_TYPE_LOG_FAIL 8
	#define VDAAE_HDR_TYPE_NVC      9
	#define VDAAE_HDR_TYPE_TLG_INFO 10
	#define VDAAE_HDR_TYPE_TLG_WARN 11
	#define VDAAE_HDR_TYPE_TLG_CRIT 12
	#define VDAAE_HDR_TYPE_PWRMGT   13
	#define VDAAE_HDR_TYPE_MUTE     14
	#define VDAAE_HDR_TYPE_DEV      15
};


struct  __packed atto_vda_ae_raid {
	struct atto_vda_ae_hdr hdr;
	u32 dwflags;
	#define VDAAE_GROUP_STATE   0x00000001
	#define VDAAE_RBLD_STATE    0x00000002
	#define VDAAE_RBLD_PROG     0x00000004
	#define VDAAE_MEMBER_CHG    0x00000008
	#define VDAAE_PART_CHG      0x00000010
	#define VDAAE_MEM_STATE_CHG 0x00000020

	u8 bygroup_state;
	#define VDAAE_RAID_INVALID  0
	#define VDAAE_RAID_NEW      1
	#define VDAAE_RAID_WAITING  2
	#define VDAAE_RAID_ONLINE   3
	#define VDAAE_RAID_DEGRADED 4
	#define VDAAE_RAID_OFFLINE  5
	#define VDAAE_RAID_DELETED  6
	#define VDAAE_RAID_BASIC    7
	#define VDAAE_RAID_EXTREME  8
	#define VDAAE_RAID_UNKNOWN  9

	u8 byrebuild_state;
	#define VDAAE_RBLD_NONE       0
	#define VDAAE_RBLD_REBUILD    1
	#define VDAAE_RBLD_ERASE      2
	#define VDAAE_RBLD_PATTERN    3
	#define VDAAE_RBLD_CONV       4
	#define VDAAE_RBLD_FULL_INIT  5
	#define VDAAE_RBLD_QUICK_INIT 6
	#define VDAAE_RBLD_SECT_SCAN  7
	#define VDAAE_RBLD_SECT_SCAN_PARITY     8
	#define VDAAE_RBLD_SECT_SCAN_PARITY_FIX 9
	#define VDAAE_RBLD_RECOV_REBUILD 10
	#define VDAAE_RBLD_UNKNOWN    11

	u8 byrebuild_progress;
	u8 op_status;
	#define VDAAE_GRPOPSTAT_MASK       0x0F
	#define VDAAE_GRPOPSTAT_INVALID    0x00
	#define VDAAE_GRPOPSTAT_OK         0x01
	#define VDAAE_GRPOPSTAT_FAULTED    0x02
	#define VDAAE_GRPOPSTAT_HALTED     0x03
	#define VDAAE_GRPOPSTAT_INT        0x04
	#define VDAAE_GRPOPPROC_MASK       0xF0
	#define VDAAE_GRPOPPROC_STARTABLE  0x10
	#define VDAAE_GRPOPPROC_CANCELABLE 0x20
	#define VDAAE_GRPOPPROC_RESUMABLE  0x40
	#define VDAAE_GRPOPPROC_HALTABLE   0x80
	char acname[15];
	u8 byreserved;
	u8 byreserved2[0x80 - 0x1C];
};


struct __packed atto_vda_ae_lu_tgt_lun {
	u16 wtarget_id;
	u8 bylun;
	u8 byreserved;
};


struct __packed atto_vda_ae_lu_tgt_lun_raid {
	u16 wtarget_id;
	u8 bylun;
	u8 byreserved;
	u32 dwinterleave;
	u32 dwblock_size;
};


struct __packed atto_vda_ae_lu {
	struct atto_vda_ae_hdr hdr;
	u32 dwevent;
	#define VDAAE_LU_DISC        0x00000001
	#define VDAAE_LU_LOST        0x00000002
	#define VDAAE_LU_STATE       0x00000004
	#define VDAAE_LU_PASSTHROUGH 0x10000000
	#define VDAAE_LU_PHYS_ID     0x20000000

	u8 bystate;
	#define VDAAE_LU_UNDEFINED        0
	#define VDAAE_LU_NOT_PRESENT      1
	#define VDAAE_LU_OFFLINE          2
	#define VDAAE_LU_ONLINE           3
	#define VDAAE_LU_DEGRADED         4
	#define VDAAE_LU_FACTORY_DISABLED 5
	#define VDAAE_LU_DELETED          6
	#define VDAAE_LU_BUSSCAN          7
	#define VDAAE_LU_UNKNOWN          8

	u8 byreserved;
	u16 wphys_target_id;

	union {
		struct atto_vda_ae_lu_tgt_lun tgtlun;
		struct atto_vda_ae_lu_tgt_lun_raid tgtlun_raid;
	} id;
};


struct __packed atto_vda_ae_disk {
	struct atto_vda_ae_hdr hdr;
};


#define VDAAE_LOG_STRSZ 64

struct __packed atto_vda_ae_log {
	struct atto_vda_ae_hdr hdr;
	char aclog_ascii[VDAAE_LOG_STRSZ];
};


#define VDAAE_TLG_STRSZ 56

struct __packed atto_vda_ae_timestamp_log {
	struct atto_vda_ae_hdr hdr;
	u32 dwtimestamp;
	char aclog_ascii[VDAAE_TLG_STRSZ];
};


struct __packed atto_vda_ae_nvc {
	struct atto_vda_ae_hdr hdr;
};


struct __packed atto_vda_ae_dev {
	struct atto_vda_ae_hdr hdr;
	struct atto_dev_addr devaddr;
};


union atto_vda_ae {
	struct atto_vda_ae_hdr hdr;
	struct atto_vda_ae_disk disk;
	struct atto_vda_ae_lu lu;
	struct atto_vda_ae_raid raid;
	struct atto_vda_ae_log log;
	struct atto_vda_ae_timestamp_log tslog;
	struct atto_vda_ae_nvc nvcache;
	struct atto_vda_ae_dev dev;
};


struct __packed atto_vda_date_and_time {
	u8 flags;
	#define VDA_DT_DAY_MASK   0x07
	#define VDA_DT_DAY_NONE   0x00
	#define VDA_DT_DAY_SUN    0x01
	#define VDA_DT_DAY_MON    0x02
	#define VDA_DT_DAY_TUE    0x03
	#define VDA_DT_DAY_WED    0x04
	#define VDA_DT_DAY_THU    0x05
	#define VDA_DT_DAY_FRI    0x06
	#define VDA_DT_DAY_SAT    0x07
	#define VDA_DT_PM         0x40
	#define VDA_DT_MILITARY   0x80

	u8 seconds;
	u8 minutes;
	u8 hours;
	u8 day;
	u8 month;
	u16 year;
};

#define SGE_LEN_LIMIT   0x003FFFFF      /*! mask of segment length            */
#define SGE_LEN_MAX     0x003FF000      /*! maximum segment length            */
#define SGE_LAST        0x01000000      /*! last entry                        */
#define SGE_ADDR_64     0x04000000      /*! 64-bit addressing flag            */
#define SGE_CHAIN       0x80000000      /*! chain descriptor flag             */
#define SGE_CHAIN_LEN   0x0000FFFF      /*! mask of length in chain entries   */
#define SGE_CHAIN_SZ    0x00FF0000      /*! mask of size of chained buffer    */


struct __packed atto_vda_cfg_init {
	struct atto_vda_date_and_time date_time;
	u32 sgl_page_size;
	u32 vda_version;
	u32 fw_version;
	u32 fw_build;
	u32 fw_release;
	u32 epoch_time;
	u32 ioctl_tunnel;
	#define VDA_ITF_MEM_RW           0x00000001
	#define VDA_ITF_TRACE            0x00000002
	#define VDA_ITF_SCSI_PASS_THRU   0x00000004
	#define VDA_ITF_GET_DEV_ADDR     0x00000008
	#define VDA_ITF_PHY_CTRL         0x00000010
	#define VDA_ITF_CONN_CTRL        0x00000020
	#define VDA_ITF_GET_DEV_INFO     0x00000040

	u32 num_targets_backend;
	u8 reserved[0x48];
};


/* configuration commands */

#define VDA_CFG_INIT          0x00
#define VDA_CFG_GET_INIT      0x01
#define VDA_CFG_GET_INIT2     0x02


/*! physical region descriptor (PRD) aka scatter/gather entry */

struct __packed atto_physical_region_description {
	u64 address;
	u32 ctl_len;
	#define PRD_LEN_LIMIT       0x003FFFFF
	#define PRD_LEN_MAX         0x003FF000
	#define PRD_NXT_PRD_CNT     0x0000007F
	#define PRD_CHAIN           0x01000000
	#define PRD_DATA            0x00000000
	#define PRD_INT_SEL         0xF0000000
	  #define PRD_INT_SEL_F0    0x00000000
	  #define PRD_INT_SEL_F1    0x40000000
	  #define PRD_INT_SEL_F2    0x80000000
	  #define PRD_INT_SEL_F3    0xc0000000
	  #define PRD_INT_SEL_SRAM  0x10000000
	  #define PRD_INT_SEL_PBSR  0x20000000

};

/* Request types. NOTE that ALL requests have the same layout for the first
 * few bytes.
 */
struct __packed atto_vda_req_header {
	u32 length;
	u8 function;
	u8 variable1;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
};


#define FCP_CDB_SIZE    16

struct __packed atto_vda_scsi_req {
	u32 length;
	u8 function;  /* VDA_FUNC_SCSI */
	u8 sense_len;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
	u32 flags;
     #define FCP_CMND_LUN_MASK    0x000000FF
     #define FCP_CMND_TA_MASK     0x00000700
      #define FCP_CMND_TA_SIMPL_Q 0x00000000
      #define FCP_CMND_TA_HEAD_Q  0x00000100
      #define FCP_CMND_TA_ORDRD_Q 0x00000200
      #define FCP_CMND_TA_ACA     0x00000400
     #define FCP_CMND_PRI_MASK    0x00007800
     #define FCP_CMND_TM_MASK     0x00FF0000
      #define FCP_CMND_ATS        0x00020000
      #define FCP_CMND_CTS        0x00040000
      #define FCP_CMND_LRS        0x00100000
      #define FCP_CMND_TRS        0x00200000
      #define FCP_CMND_CLA        0x00400000
      #define FCP_CMND_TRM        0x00800000
     #define FCP_CMND_DATA_DIR    0x03000000
      #define FCP_CMND_WRD        0x01000000
      #define FCP_CMND_RDD        0x02000000

	u8 cdb[FCP_CDB_SIZE];
	union {
		struct __packed {
			u64 ppsense_buf;
			u16 target_id;
			u8 iblk_cnt_prd;
			u8 reserved;
		};

		struct atto_physical_region_description sense_buff_prd;
	};

	union {
		struct atto_vda_sge sge[1];

		u32 abort_handle;
		u32 dwords[245];
		struct atto_physical_region_description prd[1];
	} u;
};


struct __packed atto_vda_flash_req {
	u32 length;
	u8 function; /* VDA_FUNC_FLASH */
	u8 sub_func;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
	u32 flash_addr;
	u8 checksum;
	u8 rsvd[3];

	union {
		struct {
			char file_name[16]; /* 8.3 fname, NULL term, wc=* */
			struct atto_vda_sge sge[1];
		} file;

		struct atto_vda_sge sge[1];
		struct atto_physical_region_description prde[2];
	} data;
};


struct __packed atto_vda_diag_req {
	u32 length;
	u8 function; /* VDA_FUNC_DIAG */
	u8 sub_func;
	#define VDA_DIAG_STATUS   0x00
	#define VDA_DIAG_RESET    0x01
	#define VDA_DIAG_PAUSE    0x02
	#define VDA_DIAG_RESUME   0x03
	#define VDA_DIAG_READ     0x04
	#define VDA_DIAG_WRITE    0x05

	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
	u32 rsvd;
	u64 local_addr;
	struct atto_vda_sge sge[1];
};


struct __packed atto_vda_ae_req {
	u32 length;
	u8 function; /* VDA_FUNC_AE */
	u8 reserved1;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;

	union {
		struct atto_vda_sge sge[1];
		struct atto_physical_region_description prde[1];
	};
};


struct __packed atto_vda_cli_req {
	u32 length;
	u8 function; /* VDA_FUNC_CLI */
	u8 reserved1;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
	u32 cmd_rsp_len;
	struct atto_vda_sge sge[1];
};


struct __packed atto_vda_ioctl_req {
	u32 length;
	u8 function; /* VDA_FUNC_IOCTL */
	u8 sub_func;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;

	union {
		struct atto_vda_sge reserved_sge;
		struct atto_physical_region_description reserved_prde;
	};

	union {
		struct {
			u32 ctrl_code;
			u16 target_id;
			u8 lun;
			u8 reserved;
		} csmi;
	};

	union {
		struct atto_vda_sge sge[1];
		struct atto_physical_region_description prde[1];
	};
};


struct __packed atto_vda_cfg_req {
	u32 length;
	u8 function; /* VDA_FUNC_CFG */
	u8 sub_func;
	u8 rsvd1;
	u8 sg_list_offset;
	u32 handle;

	union {
		u8 bytes[116];
		struct atto_vda_cfg_init init;
		struct atto_vda_sge sge;
		struct atto_physical_region_description prde;
	} data;
};


struct __packed atto_vda_mgmt_req {
	u32 length;
	u8 function; /* VDA_FUNC_MGT */
	u8 mgt_func;
	u8 chain_offset;
	u8 sg_list_offset;
	u32 handle;
	u8 scan_generation;
	u8 payld_sglst_offset;
	u16 dev_index;
	u32 payld_length;
	u32 pad;
	union {
		struct atto_vda_sge sge[2];
		struct atto_physical_region_description prde[2];
	};
	struct atto_vda_sge payld_sge[1];
};


union atto_vda_req {
	struct atto_vda_scsi_req scsi;
	struct atto_vda_flash_req flash;
	struct atto_vda_diag_req diag;
	struct atto_vda_ae_req ae;
	struct atto_vda_cli_req cli;
	struct atto_vda_ioctl_req ioctl;
	struct atto_vda_cfg_req cfg;
	struct atto_vda_mgmt_req mgt;
	u8 bytes[1024];
};

/* Outbound response structures */

struct __packed atto_vda_scsi_rsp {
	u8 scsi_stat;
	u8 sense_len;
	u8 rsvd[2];
	u32 residual_length;
};

struct __packed atto_vda_flash_rsp {
	u32 file_size;
};

struct __packed atto_vda_ae_rsp {
	u32 length;
};

struct __packed atto_vda_cli_rsp {
	u32 cmd_rsp_len;
};

struct __packed atto_vda_ioctl_rsp {
	union {
		struct {
			u32 csmi_status;
			u16 target_id;
			u8 lun;
			u8 reserved;
		} csmi;
	};
};

struct __packed atto_vda_cfg_rsp {
	u16 vda_version;
	u16 fw_release;
	u32 fw_build;
};

struct __packed atto_vda_mgmt_rsp {
	u32 length;
	u16 dev_index;
	u8 scan_generation;
};

union atto_vda_func_rsp {
	struct atto_vda_scsi_rsp scsi_rsp;
	struct atto_vda_flash_rsp flash_rsp;
	struct atto_vda_ae_rsp ae_rsp;
	struct atto_vda_cli_rsp cli_rsp;
	struct atto_vda_ioctl_rsp ioctl_rsp;
	struct atto_vda_cfg_rsp cfg_rsp;
	struct atto_vda_mgmt_rsp mgt_rsp;
	u32 dwords[2];
};

struct __packed atto_vda_ob_rsp {
	u32 handle;
	u8 req_stat;
	u8 rsvd[3];

	union atto_vda_func_rsp
		func_rsp;
};

struct __packed atto_vda_ae_data {
	u8 event_data[256];
};

struct __packed atto_vda_mgmt_data {
	union {
		u8 bytes[112];
		struct atto_vda_devinfo dev_info;
		struct atto_vda_grp_info grp_info;
		struct atto_vdapart_info part_info;
		struct atto_vda_dh_info dev_health_info;
		struct atto_vda_metrics_info metrics_info;
		struct atto_vda_schedule_info sched_info;
		struct atto_vda_n_vcache_info nvcache_info;
		struct atto_vda_buzzer_info buzzer_info;
	} data;
};

union atto_vda_rsp_data {
	struct atto_vda_ae_data ae_data;
	struct atto_vda_mgmt_data mgt_data;
	u8 sense_data[252];
	#define SENSE_DATA_SZ   252;
	u8 bytes[256];
};

#endif
