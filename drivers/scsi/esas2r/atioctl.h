/*  linux/drivers/scsi/esas2r/atioctl.h
 *      ATTO IOCTL Handling
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

#include "atvda.h"

#ifndef ATIOCTL_H
#define ATIOCTL_H

#define EXPRESS_IOCTL_SIGNATURE        "Express"
#define EXPRESS_IOCTL_SIGNATURE_SIZE   8

/* structure definitions for IOCTls */

struct __packed atto_express_ioctl_header {
	u8 signature[EXPRESS_IOCTL_SIGNATURE_SIZE];
	u8 return_code;

#define IOCTL_SUCCESS               0
#define IOCTL_ERR_INVCMD          101
#define IOCTL_INIT_FAILED         102
#define IOCTL_NOT_IMPLEMENTED     103
#define IOCTL_BAD_CHANNEL         104
#define IOCTL_TARGET_OVERRUN      105
#define IOCTL_TARGET_NOT_ENABLED  106
#define IOCTL_BAD_FLASH_IMGTYPE   107
#define IOCTL_OUT_OF_RESOURCES    108
#define IOCTL_GENERAL_ERROR       109
#define IOCTL_INVALID_PARAM       110

	u8 channel;
	u8 retries;
	u8 pad[5];
};

/*
 * NOTE - if channel == 0xFF, the request is
 * handled on the adapter it came in on.
 */
#define MAX_NODE_NAMES  256

struct __packed atto_firmware_rw_request {
	u8 function;
	#define FUNC_FW_DOWNLOAD        0x09
	#define FUNC_FW_UPLOAD          0x12

	u8 img_type;
	#define FW_IMG_FW               0x01
	#define FW_IMG_BIOS             0x02
	#define FW_IMG_NVR              0x03
	#define FW_IMG_RAW              0x04
	#define FW_IMG_FM_API           0x05
	#define FW_IMG_FS_API           0x06

	u8 pad[2];
	u32 img_offset;
	u32 img_size;
	u8 image[0x80000];
};

struct __packed atto_param_rw_request {
	u16 code;
	char data_buffer[512];
};

#define MAX_CHANNEL 256

struct __packed atto_channel_list {
	u32 num_channels;
	u8 channel[MAX_CHANNEL];
};

struct __packed atto_channel_info {
	u8 major_rev;
	u8 minor_rev;
	u8 IRQ;
	u8 revision_id;
	u8 pci_bus;
	u8 pci_dev_func;
	u8 core_rev;
	u8 host_no;
	u16 device_id;
	u16 vendor_id;
	u16 ven_dev_id;
	u8 pad[3];
	u32 hbaapi_rev;
};

/*
 * CSMI control codes
 * class independent
 */
#define CSMI_CC_GET_DRVR_INFO        1
#define CSMI_CC_GET_CNTLR_CFG        2
#define CSMI_CC_GET_CNTLR_STS        3
#define CSMI_CC_FW_DOWNLOAD          4

/* RAID class */
#define CSMI_CC_GET_RAID_INFO        10
#define CSMI_CC_GET_RAID_CFG         11

/* HBA class */
#define CSMI_CC_GET_PHY_INFO         20
#define CSMI_CC_SET_PHY_INFO         21
#define CSMI_CC_GET_LINK_ERRORS      22
#define CSMI_CC_SMP_PASSTHRU         23
#define CSMI_CC_SSP_PASSTHRU         24
#define CSMI_CC_STP_PASSTHRU         25
#define CSMI_CC_GET_SATA_SIG         26
#define CSMI_CC_GET_SCSI_ADDR        27
#define CSMI_CC_GET_DEV_ADDR         28
#define CSMI_CC_TASK_MGT             29
#define CSMI_CC_GET_CONN_INFO        30

/* PHY class */
#define CSMI_CC_PHY_CTRL             60

/*
 * CSMI status codes
 * class independent
 */
#define CSMI_STS_SUCCESS             0
#define CSMI_STS_FAILED              1
#define CSMI_STS_BAD_CTRL_CODE       2
#define CSMI_STS_INV_PARAM           3
#define CSMI_STS_WRITE_ATTEMPTED     4

/* RAID class */
#define CSMI_STS_INV_RAID_SET        1000

/* HBA class */
#define CSMI_STS_PHY_CHANGED         CSMI_STS_SUCCESS
#define CSMI_STS_PHY_UNCHANGEABLE    2000
#define CSMI_STS_INV_LINK_RATE       2001
#define CSMI_STS_INV_PHY             2002
#define CSMI_STS_INV_PHY_FOR_PORT    2003
#define CSMI_STS_PHY_UNSELECTABLE    2004
#define CSMI_STS_SELECT_PHY_OR_PORT  2005
#define CSMI_STS_INV_PORT            2006
#define CSMI_STS_PORT_UNSELECTABLE   2007
#define CSMI_STS_CONNECTION_FAILED   2008
#define CSMI_STS_NO_SATA_DEV         2009
#define CSMI_STS_NO_SATA_SIGNATURE   2010
#define CSMI_STS_SCSI_EMULATION      2011
#define CSMI_STS_NOT_AN_END_DEV      2012
#define CSMI_STS_NO_SCSI_ADDR        2013
#define CSMI_STS_NO_DEV_ADDR         2014

/* CSMI class independent structures */
struct atto_csmi_get_driver_info {
	char name[81];
	char description[81];
	u16 major_rev;
	u16 minor_rev;
	u16 build_rev;
	u16 release_rev;
	u16 csmi_major_rev;
	u16 csmi_minor_rev;
	#define CSMI_MAJOR_REV_0_81      0
	#define CSMI_MINOR_REV_0_81      81

	#define CSMI_MAJOR_REV           CSMI_MAJOR_REV_0_81
	#define CSMI_MINOR_REV           CSMI_MINOR_REV_0_81
};

struct atto_csmi_get_pci_bus_addr {
	u8 bus_num;
	u8 device_num;
	u8 function_num;
	u8 reserved;
};

struct atto_csmi_get_cntlr_cfg {
	u32 base_io_addr;

	struct {
		u32 base_memaddr_lo;
		u32 base_memaddr_hi;
	};

	u32 board_id;
	u16 slot_num;
	#define CSMI_SLOT_NUM_UNKNOWN    0xFFFF

	u8 cntlr_class;
	#define CSMI_CNTLR_CLASS_HBA     5

	u8 io_bus_type;
	#define CSMI_BUS_TYPE_PCI        3
	#define CSMI_BUS_TYPE_PCMCIA     4

	union {
		struct atto_csmi_get_pci_bus_addr pci_addr;
		u8 reserved[32];
	};

	char serial_num[81];
	u16 major_rev;
	u16 minor_rev;
	u16 build_rev;
	u16 release_rev;
	u16 bios_major_rev;
	u16 bios_minor_rev;
	u16 bios_build_rev;
	u16 bios_release_rev;
	u32 cntlr_flags;
	#define CSMI_CNTLRF_SAS_HBA      0x00000001
	#define CSMI_CNTLRF_SAS_RAID     0x00000002
	#define CSMI_CNTLRF_SATA_HBA     0x00000004
	#define CSMI_CNTLRF_SATA_RAID    0x00000008
	#define CSMI_CNTLRF_FWD_SUPPORT  0x00010000
	#define CSMI_CNTLRF_FWD_ONLINE   0x00020000
	#define CSMI_CNTLRF_FWD_SRESET   0x00040000
	#define CSMI_CNTLRF_FWD_HRESET   0x00080000
	#define CSMI_CNTLRF_FWD_RROM     0x00100000

	u16 rrom_major_rev;
	u16 rrom_minor_rev;
	u16 rrom_build_rev;
	u16 rrom_release_rev;
	u16 rrom_biosmajor_rev;
	u16 rrom_biosminor_rev;
	u16 rrom_biosbuild_rev;
	u16 rrom_biosrelease_rev;
	u8 reserved2[7];
};

struct atto_csmi_get_cntlr_sts {
	u32 status;
	#define CSMI_CNTLR_STS_GOOD          1
	#define CSMI_CNTLR_STS_FAILED        2
	#define CSMI_CNTLR_STS_OFFLINE       3
	#define CSMI_CNTLR_STS_POWEROFF      4

	u32 offline_reason;
	#define CSMI_OFFLINE_NO_REASON       0
	#define CSMI_OFFLINE_INITIALIZING    1
	#define CSMI_OFFLINE_BUS_DEGRADED    2
	#define CSMI_OFFLINE_BUS_FAILURE     3

	u8 reserved[28];
};

struct atto_csmi_fw_download {
	u32 buffer_len;
	u32 download_flags;
	#define CSMI_FWDF_VALIDATE       0x00000001
	#define CSMI_FWDF_SOFT_RESET     0x00000002
	#define CSMI_FWDF_HARD_RESET     0x00000004

	u8 reserved[32];
	u16 status;
	#define CSMI_FWD_STS_SUCCESS     0
	#define CSMI_FWD_STS_FAILED      1
	#define CSMI_FWD_STS_USING_RROM  2
	#define CSMI_FWD_STS_REJECT      3
	#define CSMI_FWD_STS_DOWNREV     4

	u16 severity;
	#define CSMI_FWD_SEV_INFO        0
	#define CSMI_FWD_SEV_WARNING     1
	#define CSMI_FWD_SEV_ERROR       2
	#define CSMI_FWD_SEV_FATAL       3

};

/* CSMI RAID class structures */
struct atto_csmi_get_raid_info {
	u32 num_raid_sets;
	u32 max_drivesper_set;
	u8 reserved[92];
};

struct atto_csmi_raid_drives {
	char model[40];
	char firmware[8];
	char serial_num[40];
	u8 sas_addr[8];
	u8 lun[8];
	u8 drive_sts;
	#define CSMI_DRV_STS_OK          0
	#define CSMI_DRV_STS_REBUILDING  1
	#define CSMI_DRV_STS_FAILED      2
	#define CSMI_DRV_STS_DEGRADED    3

	u8 drive_usage;
	#define CSMI_DRV_USE_NOT_USED    0
	#define CSMI_DRV_USE_MEMBER      1
	#define CSMI_DRV_USE_SPARE       2

	u8 reserved[30]; /* spec says 22 */
};

struct atto_csmi_get_raid_cfg {
	u32 raid_set_index;
	u32 capacity;
	u32 stripe_size;
	u8 raid_type;
	u8 status;
	u8 information;
	u8 drive_cnt;
	u8 reserved[20];

	struct atto_csmi_raid_drives drives[1];
};

/* CSMI HBA class structures */
struct atto_csmi_phy_entity {
	u8 ident_frame[0x1C];
	u8 port_id;
	u8 neg_link_rate;
	u8 min_link_rate;
	u8 max_link_rate;
	u8 phy_change_cnt;
	u8 auto_discover;
	#define CSMI_DISC_NOT_SUPPORTED  0x00
	#define CSMI_DISC_NOT_STARTED    0x01
	#define CSMI_DISC_IN_PROGRESS    0x02
	#define CSMI_DISC_COMPLETE       0x03
	#define CSMI_DISC_ERROR          0x04

	u8 reserved[2];
	u8 attach_ident_frame[0x1C];
};

struct atto_csmi_get_phy_info {
	u8 number_of_phys;
	u8 reserved[3];
	struct atto_csmi_phy_entity
		phy[32];
};

struct atto_csmi_set_phy_info {
	u8 phy_id;
	u8 neg_link_rate;
	#define CSMI_NEG_RATE_NEGOTIATE  0x00
	#define CSMI_NEG_RATE_PHY_DIS    0x01

	u8 prog_minlink_rate;
	u8 prog_maxlink_rate;
	u8 signal_class;
	#define CSMI_SIG_CLASS_UNKNOWN   0x00
	#define CSMI_SIG_CLASS_DIRECT    0x01
	#define CSMI_SIG_CLASS_SERVER    0x02
	#define CSMI_SIG_CLASS_ENCLOSURE 0x03

	u8 reserved[3];
};

struct atto_csmi_get_link_errors {
	u8 phy_id;
	u8 reset_cnts;
	#define CSMI_RESET_CNTS_NO       0x00
	#define CSMI_RESET_CNTS_YES      0x01

	u8 reserved[2];
	u32 inv_dw_cnt;
	u32 disp_err_cnt;
	u32 loss_ofdw_sync_cnt;
	u32 phy_reseterr_cnt;

	/*
	 * The following field has been added by ATTO for ease of
	 * implementation of additional statistics.  Drivers must validate
	 * the length of the IOCTL payload prior to filling them in so CSMI
	 * complaint applications function correctly.
	 */

	u32 crc_err_cnt;
};

struct atto_csmi_smp_passthru {
	u8 phy_id;
	u8 port_id;
	u8 conn_rate;
	u8 reserved;
	u8 dest_sas_addr[8];
	u32 req_len;
	u8 smp_req[1020];
	u8 conn_sts;
	u8 reserved2[3];
	u32 rsp_len;
	u8 smp_rsp[1020];
};

struct atto_csmi_ssp_passthru_sts {
	u8 conn_sts;
	u8 reserved[3];
	u8 data_present;
	u8 status;
	u16 rsp_length;
	u8 rsp[256];
	u32 data_bytes;
};

struct atto_csmi_ssp_passthru {
	u8 phy_id;
	u8 port_id;
	u8 conn_rate;
	u8 reserved;
	u8 dest_sas_addr[8];
	u8 lun[8];
	u8 cdb_len;
	u8 add_cdb_len;
	u8 reserved2[2];
	u8 cdb[16];
	u32 flags;
	#define CSMI_SSPF_DD_READ        0x00000001
	#define CSMI_SSPF_DD_WRITE       0x00000002
	#define CSMI_SSPF_DD_UNSPECIFIED 0x00000004
	#define CSMI_SSPF_TA_SIMPLE      0x00000000
	#define CSMI_SSPF_TA_HEAD_OF_Q   0x00000010
	#define CSMI_SSPF_TA_ORDERED     0x00000020
	#define CSMI_SSPF_TA_ACA         0x00000040

	u8 add_cdb[24];
	u32 data_len;

	struct atto_csmi_ssp_passthru_sts sts;
};

struct atto_csmi_stp_passthru_sts {
	u8 conn_sts;
	u8 reserved[3];
	u8 sts_fis[20];
	u32 scr[16];
	u32 data_bytes;
};

struct atto_csmi_stp_passthru {
	u8 phy_id;
	u8 port_id;
	u8 conn_rate;
	u8 reserved;
	u8 dest_sas_addr[8];
	u8 reserved2[4];
	u8 command_fis[20];
	u32 flags;
	#define CSMI_STPF_DD_READ        0x00000001
	#define CSMI_STPF_DD_WRITE       0x00000002
	#define CSMI_STPF_DD_UNSPECIFIED 0x00000004
	#define CSMI_STPF_PIO            0x00000010
	#define CSMI_STPF_DMA            0x00000020
	#define CSMI_STPF_PACKET         0x00000040
	#define CSMI_STPF_DMA_QUEUED     0x00000080
	#define CSMI_STPF_EXECUTE_DIAG   0x00000100
	#define CSMI_STPF_RESET_DEVICE   0x00000200

	u32 data_len;

	struct atto_csmi_stp_passthru_sts sts;
};

struct atto_csmi_get_sata_sig {
	u8 phy_id;
	u8 reserved[3];
	u8 reg_dth_fis[20];
};

struct atto_csmi_get_scsi_addr {
	u8 sas_addr[8];
	u8 sas_lun[8];
	u8 host_index;
	u8 path_id;
	u8 target_id;
	u8 lun;
};

struct atto_csmi_get_dev_addr {
	u8 host_index;
	u8 path_id;
	u8 target_id;
	u8 lun;
	u8 sas_addr[8];
	u8 sas_lun[8];
};

struct atto_csmi_task_mgmt {
	u8 host_index;
	u8 path_id;
	u8 target_id;
	u8 lun;
	u32 flags;
	#define CSMI_TMF_TASK_IU         0x00000001
	#define CSMI_TMF_HARD_RST        0x00000002
	#define CSMI_TMF_SUPPRESS_RSLT   0x00000004

	u32 queue_tag;
	u32 reserved;
	u8 task_mgt_func;
	u8 reserved2[7];
	u32 information;
	#define CSMI_TM_INFO_TEST        1
	#define CSMI_TM_INFO_EXCEEDED    2
	#define CSMI_TM_INFO_DEMAND      3
	#define CSMI_TM_INFO_TRIGGER     4

	struct atto_csmi_ssp_passthru_sts sts;

};

struct atto_csmi_get_conn_info {
	u32 pinout;
	#define CSMI_CON_UNKNOWN         0x00000001
	#define CSMI_CON_SFF_8482        0x00000002
	#define CSMI_CON_SFF_8470_LANE_1 0x00000100
	#define CSMI_CON_SFF_8470_LANE_2 0x00000200
	#define CSMI_CON_SFF_8470_LANE_3 0x00000400
	#define CSMI_CON_SFF_8470_LANE_4 0x00000800
	#define CSMI_CON_SFF_8484_LANE_1 0x00010000
	#define CSMI_CON_SFF_8484_LANE_2 0x00020000
	#define CSMI_CON_SFF_8484_LANE_3 0x00040000
	#define CSMI_CON_SFF_8484_LANE_4 0x00080000

	u8 connector[16];
	u8 location;
	#define CSMI_CON_INTERNAL        0x02
	#define CSMI_CON_EXTERNAL        0x04
	#define CSMI_CON_SWITCHABLE      0x08
	#define CSMI_CON_AUTO            0x10

	u8 reserved[15];
};

/* CSMI PHY class structures */
struct atto_csmi_character {
	u8 type_flags;
	#define CSMI_CTF_POS_DISP        0x01
	#define CSMI_CTF_NEG_DISP        0x02
	#define CSMI_CTF_CTRL_CHAR       0x04

	u8 value;
};

struct atto_csmi_pc_ctrl {
	u8 type;
	#define CSMI_PC_TYPE_UNDEFINED   0x00
	#define CSMI_PC_TYPE_SATA        0x01
	#define CSMI_PC_TYPE_SAS         0x02
	u8 rate;
	u8 reserved[6];
	u32 vendor_unique[8];
	u32 tx_flags;
	#define CSMI_PC_TXF_PREEMP_DIS   0x00000001

	signed char tx_amplitude;
	signed char tx_preemphasis;
	signed char tx_slew_rate;
	signed char tx_reserved[13];
	u8 tx_vendor_unique[64];
	u32 rx_flags;
	#define CSMI_PC_RXF_EQ_DIS       0x00000001

	signed char rx_threshold;
	signed char rx_equalization_gain;
	signed char rx_reserved[14];
	u8 rx_vendor_unique[64];
	u32 pattern_flags;
	#define CSMI_PC_PATF_FIXED       0x00000001
	#define CSMI_PC_PATF_DIS_SCR     0x00000002
	#define CSMI_PC_PATF_DIS_ALIGN   0x00000004
	#define CSMI_PC_PATF_DIS_SSC     0x00000008

	u8 fixed_pattern;
	#define CSMI_PC_FP_CJPAT         0x00000001
	#define CSMI_PC_FP_ALIGN         0x00000002

	u8 user_pattern_len;
	u8 pattern_reserved[6];

	struct atto_csmi_character user_pattern_buffer[16];
};

struct atto_csmi_phy_ctrl {
	u32 function;
	#define CSMI_PC_FUNC_GET_SETUP   0x00000100

	u8 phy_id;
	u16 len_of_cntl;
	u8 num_of_cntls;
	u8 reserved[4];
	u32 link_flags;
	#define CSMI_PHY_ACTIVATE_CTRL   0x00000001
	#define CSMI_PHY_UPD_SPINUP_RATE 0x00000002
	#define CSMI_PHY_AUTO_COMWAKE    0x00000004

	u8 spinup_rate;
	u8 link_reserved[7];
	u32 vendor_unique[8];

	struct atto_csmi_pc_ctrl control[1];
};

union atto_ioctl_csmi {
	struct atto_csmi_get_driver_info drvr_info;
	struct atto_csmi_get_cntlr_cfg cntlr_cfg;
	struct atto_csmi_get_cntlr_sts cntlr_sts;
	struct atto_csmi_fw_download fw_dwnld;
	struct atto_csmi_get_raid_info raid_info;
	struct atto_csmi_get_raid_cfg raid_cfg;
	struct atto_csmi_get_phy_info get_phy_info;
	struct atto_csmi_set_phy_info set_phy_info;
	struct atto_csmi_get_link_errors link_errs;
	struct atto_csmi_smp_passthru smp_pass_thru;
	struct atto_csmi_ssp_passthru ssp_pass_thru;
	struct atto_csmi_stp_passthru stp_pass_thru;
	struct atto_csmi_task_mgmt tsk_mgt;
	struct atto_csmi_get_sata_sig sata_sig;
	struct atto_csmi_get_scsi_addr scsi_addr;
	struct atto_csmi_get_dev_addr dev_addr;
	struct atto_csmi_get_conn_info conn_info[32];
	struct atto_csmi_phy_ctrl phy_ctrl;
};

struct atto_csmi {
	u32 control_code;
	u32 status;
	union atto_ioctl_csmi data;
};

struct atto_module_info {
	void *adapter;
	void *pci_dev;
	void *scsi_host;
	unsigned short host_no;
	union {
		struct {
			u64 node_name;
			u64 port_name;
		};
		u64 sas_addr;
	};
};

#define ATTO_FUNC_GET_ADAP_INFO      0x00
#define ATTO_VER_GET_ADAP_INFO0      0
#define ATTO_VER_GET_ADAP_INFO       ATTO_VER_GET_ADAP_INFO0

struct __packed atto_hba_get_adapter_info {

	struct {
		u16 vendor_id;
		u16 device_id;
		u16 ss_vendor_id;
		u16 ss_device_id;
		u8 class_code[3];
		u8 rev_id;
		u8 bus_num;
		u8 dev_num;
		u8 func_num;
		u8 link_width_max;
		u8 link_width_curr;
	    #define ATTO_GAI_PCILW_UNKNOWN   0x00

		u8 link_speed_max;
		u8 link_speed_curr;
	    #define ATTO_GAI_PCILS_UNKNOWN   0x00
	    #define ATTO_GAI_PCILS_GEN1      0x01
	    #define ATTO_GAI_PCILS_GEN2      0x02
	    #define ATTO_GAI_PCILS_GEN3      0x03

		u8 interrupt_mode;
	    #define ATTO_GAI_PCIIM_UNKNOWN   0x00
	    #define ATTO_GAI_PCIIM_LEGACY    0x01
	    #define ATTO_GAI_PCIIM_MSI       0x02
	    #define ATTO_GAI_PCIIM_MSIX      0x03

		u8 msi_vector_cnt;
		u8 reserved[19];
	} pci;

	u8 adap_type;
	#define ATTO_GAI_AT_EPCIU320     0x00
	#define ATTO_GAI_AT_ESASRAID     0x01
	#define ATTO_GAI_AT_ESASRAID2    0x02
	#define ATTO_GAI_AT_ESASHBA      0x03
	#define ATTO_GAI_AT_ESASHBA2     0x04
	#define ATTO_GAI_AT_CELERITY     0x05
	#define ATTO_GAI_AT_CELERITY8    0x06
	#define ATTO_GAI_AT_FASTFRAME    0x07
	#define ATTO_GAI_AT_ESASHBA3     0x08
	#define ATTO_GAI_AT_CELERITY16   0x09
	#define ATTO_GAI_AT_TLSASHBA     0x0A
	#define ATTO_GAI_AT_ESASHBA4     0x0B

	u8 adap_flags;
	#define ATTO_GAI_AF_DEGRADED     0x01
	#define ATTO_GAI_AF_SPT_SUPP     0x02
	#define ATTO_GAI_AF_DEVADDR_SUPP 0x04
	#define ATTO_GAI_AF_PHYCTRL_SUPP 0x08
	#define ATTO_GAI_AF_TEST_SUPP    0x10
	#define ATTO_GAI_AF_DIAG_SUPP    0x20
	#define ATTO_GAI_AF_VIRT_SES     0x40
	#define ATTO_GAI_AF_CONN_CTRL    0x80

	u8 num_ports;
	u8 num_phys;
	u8 drvr_rev_major;
	u8 drvr_rev_minor;
	u8 drvr_revsub_minor;
	u8 drvr_rev_build;
	char drvr_rev_ascii[16];
	char drvr_name[32];
	char firmware_rev[16];
	char flash_rev[16];
	char model_name_short[16];
	char model_name[32];
	u32 num_targets;
	u32 num_targsper_bus;
	u32 num_lunsper_targ;
	u8 num_busses;
	u8 num_connectors;
	u8 adap_flags2;
	#define ATTO_GAI_AF2_FCOE_SUPP       0x01
	#define ATTO_GAI_AF2_NIC_SUPP        0x02
	#define ATTO_GAI_AF2_LOCATE_SUPP     0x04
	#define ATTO_GAI_AF2_ADAP_CTRL_SUPP  0x08
	#define ATTO_GAI_AF2_DEV_INFO_SUPP   0x10
	#define ATTO_GAI_AF2_NPIV_SUPP       0x20
	#define ATTO_GAI_AF2_MP_SUPP         0x40

	u8 num_temp_sensors;
	u32 num_targets_backend;
	u32 tunnel_flags;
	#define ATTO_GAI_TF_MEM_RW           0x00000001
	#define ATTO_GAI_TF_TRACE            0x00000002
	#define ATTO_GAI_TF_SCSI_PASS_THRU   0x00000004
	#define ATTO_GAI_TF_GET_DEV_ADDR     0x00000008
	#define ATTO_GAI_TF_PHY_CTRL         0x00000010
	#define ATTO_GAI_TF_CONN_CTRL        0x00000020
	#define ATTO_GAI_TF_GET_DEV_INFO     0x00000040

	u8 reserved3[0x138];
};

#define ATTO_FUNC_GET_ADAP_ADDR      0x01
#define ATTO_VER_GET_ADAP_ADDR0      0
#define ATTO_VER_GET_ADAP_ADDR       ATTO_VER_GET_ADAP_ADDR0

struct __packed atto_hba_get_adapter_address {

	u8 addr_type;
	#define ATTO_GAA_AT_PORT         0x00
	#define ATTO_GAA_AT_NODE         0x01
	#define ATTO_GAA_AT_CURR_MAC     0x02
	#define ATTO_GAA_AT_PERM_MAC     0x03
	#define ATTO_GAA_AT_VNIC         0x04

	u8 port_id;
	u16 addr_len;
	u8 address[256];
};

#define ATTO_FUNC_MEM_RW             0x02
#define ATTO_VER_MEM_RW0             0
#define ATTO_VER_MEM_RW              ATTO_VER_MEM_RW0

struct __packed atto_hba_memory_read_write {
	u8 mem_func;
	u8 mem_type;
	union {
		u8 pci_index;
		u8 i2c_dev;
	};
	u8 i2c_status;
	u32 length;
	u64 address;
	u8 reserved[48];

};

#define ATTO_FUNC_TRACE              0x03
#define ATTO_VER_TRACE0              0
#define ATTO_VER_TRACE1              1
#define ATTO_VER_TRACE               ATTO_VER_TRACE1

struct __packed atto_hba_trace {
	u8 trace_func;
	#define ATTO_TRC_TF_GET_INFO     0x00
	#define ATTO_TRC_TF_ENABLE       0x01
	#define ATTO_TRC_TF_DISABLE      0x02
	#define ATTO_TRC_TF_SET_MASK     0x03
	#define ATTO_TRC_TF_UPLOAD       0x04
	#define ATTO_TRC_TF_RESET        0x05

	u8 trace_type;
	#define ATTO_TRC_TT_DRIVER       0x00
	#define ATTO_TRC_TT_FWCOREDUMP   0x01

	u8 reserved[2];
	u32 current_offset;
	u32 total_length;
	u32 trace_mask;
	u8 reserved2[48];
};

#define ATTO_FUNC_SCSI_PASS_THRU     0x04
#define ATTO_VER_SCSI_PASS_THRU0     0
#define ATTO_VER_SCSI_PASS_THRU      ATTO_VER_SCSI_PASS_THRU0

struct __packed atto_hba_scsi_pass_thru {
	u8 cdb[32];
	u8 cdb_length;
	u8 req_status;
	#define ATTO_SPT_RS_SUCCESS      0x00
	#define ATTO_SPT_RS_FAILED       0x01
	#define ATTO_SPT_RS_OVERRUN      0x02
	#define ATTO_SPT_RS_UNDERRUN     0x03
	#define ATTO_SPT_RS_NO_DEVICE    0x04
	#define ATTO_SPT_RS_NO_LUN       0x05
	#define ATTO_SPT_RS_TIMEOUT      0x06
	#define ATTO_SPT_RS_BUS_RESET    0x07
	#define ATTO_SPT_RS_ABORTED      0x08
	#define ATTO_SPT_RS_BUSY         0x09
	#define ATTO_SPT_RS_DEGRADED     0x0A

	u8 scsi_status;
	u8 sense_length;
	u32 flags;
	#define ATTO_SPTF_DATA_IN    0x00000001
	#define ATTO_SPTF_DATA_OUT   0x00000002
	#define ATTO_SPTF_SIMPLE_Q   0x00000004
	#define ATTO_SPTF_HEAD_OF_Q  0x00000008
	#define ATTO_SPTF_ORDERED_Q  0x00000010

	u32 timeout;
	u32 target_id;
	u8 lun[8];
	u32 residual_length;
	u8 sense_data[0xFC];
	u8 reserved[0x28];
};

#define ATTO_FUNC_GET_DEV_ADDR       0x05
#define ATTO_VER_GET_DEV_ADDR0       0
#define ATTO_VER_GET_DEV_ADDR        ATTO_VER_GET_DEV_ADDR0

struct __packed atto_hba_get_device_address {
	u8 addr_type;
	#define ATTO_GDA_AT_PORT         0x00
	#define ATTO_GDA_AT_NODE         0x01
	#define ATTO_GDA_AT_MAC          0x02
	#define ATTO_GDA_AT_PORTID       0x03
	#define ATTO_GDA_AT_UNIQUE       0x04

	u8 reserved;
	u16 addr_len;
	u32 target_id;
	u8 address[256];
};

/* The following functions are supported by firmware but do not have any
 * associated driver structures
 */
#define ATTO_FUNC_PHY_CTRL           0x06
#define ATTO_FUNC_CONN_CTRL          0x0C
#define ATTO_FUNC_ADAP_CTRL          0x0E
#define ATTO_VER_ADAP_CTRL0          0
#define ATTO_VER_ADAP_CTRL           ATTO_VER_ADAP_CTRL0

struct __packed atto_hba_adap_ctrl {
	u8 adap_func;
	#define ATTO_AC_AF_HARD_RST      0x00
	#define ATTO_AC_AF_GET_STATE     0x01
	#define ATTO_AC_AF_GET_TEMP      0x02

	u8 adap_state;
	#define ATTO_AC_AS_UNKNOWN       0x00
	#define ATTO_AC_AS_OK            0x01
	#define ATTO_AC_AS_RST_SCHED     0x02
	#define ATTO_AC_AS_RST_IN_PROG   0x03
	#define ATTO_AC_AS_RST_DISC      0x04
	#define ATTO_AC_AS_DEGRADED      0x05
	#define ATTO_AC_AS_DISABLED      0x06
	#define ATTO_AC_AS_TEMP          0x07

	u8 reserved[2];

	union {
		struct {
			u8 temp_sensor;
			u8 temp_state;

	#define ATTO_AC_TS_UNSUPP        0x00
	#define ATTO_AC_TS_UNKNOWN       0x01
	#define ATTO_AC_TS_INIT_FAILED   0x02
	#define ATTO_AC_TS_NORMAL        0x03
	#define ATTO_AC_TS_OUT_OF_RANGE  0x04
	#define ATTO_AC_TS_FAULT         0x05

			signed short temp_value;
			signed short temp_lower_lim;
			signed short temp_upper_lim;
			char temp_desc[32];
			u8 reserved2[20];
		};
	};
};

#define ATTO_FUNC_GET_DEV_INFO       0x0F
#define ATTO_VER_GET_DEV_INFO0       0
#define ATTO_VER_GET_DEV_INFO        ATTO_VER_GET_DEV_INFO0

struct __packed atto_hba_sas_device_info {

    #define ATTO_SDI_MAX_PHYS_WIDE_PORT  16

	u8 phy_id[ATTO_SDI_MAX_PHYS_WIDE_PORT]; /* IDs of parent exp/adapt */
	#define ATTO_SDI_PHY_ID_INV      ATTO_SAS_PHY_ID_INV
	u32 exp_target_id;
	u32 sas_port_mask;
	u8 sas_level;
	#define ATTO_SDI_SAS_LVL_INV     0xFF

	u8 slot_num;
	#define ATTO_SDI_SLOT_NUM_INV    ATTO_SLOT_NUM_INV

	u8 dev_type;
	#define ATTO_SDI_DT_END_DEVICE   0
	#define ATTO_SDI_DT_EXPANDER     1
	#define ATTO_SDI_DT_PORT_MULT    2

	u8 ini_flags;
	u8 tgt_flags;
	u8 link_rate; /* SMP_RATE_XXX */
	u8 loc_flags;
	#define ATTO_SDI_LF_DIRECT       0x01
	#define ATTO_SDI_LF_EXPANDER     0x02
	#define ATTO_SDI_LF_PORT_MULT    0x04
	u8 pm_port;
	u8 reserved[0x60];
};

union atto_hba_device_info {
	struct atto_hba_sas_device_info sas_dev_info;
};

struct __packed atto_hba_get_device_info {
	u32 target_id;
	u8 info_type;
	#define ATTO_GDI_IT_UNKNOWN      0x00
	#define ATTO_GDI_IT_SAS          0x01
	#define ATTO_GDI_IT_FC           0x02
	#define ATTO_GDI_IT_FCOE         0x03

	u8 reserved[11];
	union atto_hba_device_info dev_info;
};

struct atto_ioctl {
	u8 version;
	u8 function; /* ATTO_FUNC_XXX */
	u8 status;
#define ATTO_STS_SUCCESS         0x00
#define ATTO_STS_FAILED          0x01
#define ATTO_STS_INV_VERSION     0x02
#define ATTO_STS_OUT_OF_RSRC     0x03
#define ATTO_STS_INV_FUNC        0x04
#define ATTO_STS_UNSUPPORTED     0x05
#define ATTO_STS_INV_ADAPTER     0x06
#define ATTO_STS_INV_DRVR_VER    0x07
#define ATTO_STS_INV_PARAM       0x08
#define ATTO_STS_TIMEOUT         0x09
#define ATTO_STS_NOT_APPL        0x0A
#define ATTO_STS_DEGRADED        0x0B

	u8 flags;
	#define HBAF_TUNNEL      0x01

	u32 data_length;
	u8 reserved2[56];

	union {
		u8 byte[1];
		struct atto_hba_get_adapter_info get_adap_info;
		struct atto_hba_get_adapter_address get_adap_addr;
		struct atto_hba_scsi_pass_thru scsi_pass_thru;
		struct atto_hba_get_device_address get_dev_addr;
		struct atto_hba_adap_ctrl adap_ctrl;
		struct atto_hba_get_device_info get_dev_info;
		struct atto_hba_trace trace;
	} data;

};

struct __packed atto_ioctl_vda_scsi_cmd {

    #define ATTO_VDA_SCSI_VER0   0
    #define ATTO_VDA_SCSI_VER    ATTO_VDA_SCSI_VER0

	u8 cdb[16];
	u32 flags;
	u32 data_length;
	u32 residual_length;
	u16 target_id;
	u8 sense_len;
	u8 scsi_stat;
	u8 reserved[8];
	u8 sense_data[80];
};

struct __packed atto_ioctl_vda_flash_cmd {

    #define ATTO_VDA_FLASH_VER0  0
    #define ATTO_VDA_FLASH_VER   ATTO_VDA_FLASH_VER0

	u32 flash_addr;
	u32 data_length;
	u8 sub_func;
	u8 reserved[15];

	union {
		struct {
			u32 flash_size;
			u32 page_size;
			u8 prod_info[32];
		} info;

		struct {
			char file_name[16]; /* 8.3 fname, NULL term, wc=* */
			u32 file_size;
		} file;
	} data;

};

struct __packed atto_ioctl_vda_diag_cmd {

    #define ATTO_VDA_DIAG_VER0   0
    #define ATTO_VDA_DIAG_VER    ATTO_VDA_DIAG_VER0

	u64 local_addr;
	u32 data_length;
	u8 sub_func;
	u8 flags;
	u8 reserved[3];
};

struct __packed atto_ioctl_vda_cli_cmd {

    #define ATTO_VDA_CLI_VER0    0
    #define ATTO_VDA_CLI_VER     ATTO_VDA_CLI_VER0

	u32 cmd_rsp_len;
};

struct __packed atto_ioctl_vda_smp_cmd {

    #define ATTO_VDA_SMP_VER0    0
    #define ATTO_VDA_SMP_VER     ATTO_VDA_SMP_VER0

	u64 dest;
	u32 cmd_rsp_len;
};

struct __packed atto_ioctl_vda_cfg_cmd {

    #define ATTO_VDA_CFG_VER0    0
    #define ATTO_VDA_CFG_VER     ATTO_VDA_CFG_VER0

	u32 data_length;
	u8 cfg_func;
	u8 reserved[11];

	union {
		u8 bytes[112];
		struct atto_vda_cfg_init init;
	} data;

};

struct __packed atto_ioctl_vda_mgt_cmd {

    #define ATTO_VDA_MGT_VER0    0
    #define ATTO_VDA_MGT_VER     ATTO_VDA_MGT_VER0

	u8 mgt_func;
	u8 scan_generation;
	u16 dev_index;
	u32 data_length;
	u8 reserved[8];
	union {
		u8 bytes[112];
		struct atto_vda_devinfo dev_info;
		struct atto_vda_grp_info grp_info;
		struct atto_vdapart_info part_info;
		struct atto_vda_dh_info dh_info;
		struct atto_vda_metrics_info metrics_info;
		struct atto_vda_schedule_info sched_info;
		struct atto_vda_n_vcache_info nvcache_info;
		struct atto_vda_buzzer_info buzzer_info;
		struct atto_vda_adapter_info adapter_info;
		struct atto_vda_temp_info temp_info;
		struct atto_vda_fan_info fan_info;
	} data;
};

struct __packed atto_ioctl_vda_gsv_cmd {

    #define ATTO_VDA_GSV_VER0    0
    #define ATTO_VDA_GSV_VER     ATTO_VDA_GSV_VER0

	u8 rsp_len;
	u8 reserved[7];
	u8 version_info[];
	#define ATTO_VDA_VER_UNSUPPORTED 0xFF

};

struct __packed atto_ioctl_vda {
	u8 version;
	u8 function;    /* VDA_FUNC_XXXX */
	u8 status;      /* ATTO_STS_XXX */
	u8 vda_status;  /* RS_XXX (if status == ATTO_STS_SUCCESS) */
	u32 data_length;
	u8 reserved[8];

	union {
		struct atto_ioctl_vda_scsi_cmd scsi;
		struct atto_ioctl_vda_flash_cmd flash;
		struct atto_ioctl_vda_diag_cmd diag;
		struct atto_ioctl_vda_cli_cmd cli;
		struct atto_ioctl_vda_smp_cmd smp;
		struct atto_ioctl_vda_cfg_cmd cfg;
		struct atto_ioctl_vda_mgt_cmd mgt;
		struct atto_ioctl_vda_gsv_cmd gsv;
		u8 cmd_info[256];
	} cmd;

	union {
		u8 data[1];
		struct atto_vda_devinfo2 dev_info2;
	} data;

};

struct __packed atto_ioctl_smp {
	u8 version;
	#define ATTO_SMP_VERSION0        0
	#define ATTO_SMP_VERSION1        1
	#define ATTO_SMP_VERSION2        2
	#define ATTO_SMP_VERSION         ATTO_SMP_VERSION2

	u8 function;
#define ATTO_SMP_FUNC_DISC_SMP           0x00
#define ATTO_SMP_FUNC_DISC_TARG          0x01
#define ATTO_SMP_FUNC_SEND_CMD           0x02
#define ATTO_SMP_FUNC_DISC_TARG_DIRECT   0x03
#define ATTO_SMP_FUNC_SEND_CMD_DIRECT    0x04
#define ATTO_SMP_FUNC_DISC_SMP_DIRECT    0x05

	u8 status;      /* ATTO_STS_XXX */
	u8 smp_status;  /* if status == ATTO_STS_SUCCESS */
	#define ATTO_SMP_STS_SUCCESS     0x00
	#define ATTO_SMP_STS_FAILURE     0x01
	#define ATTO_SMP_STS_RESCAN      0x02
	#define ATTO_SMP_STS_NOT_FOUND   0x03

	u16 target_id;
	u8 phy_id;
	u8 dev_index;
	u64 smp_sas_addr;
	u64 targ_sas_addr;
	u32 req_length;
	u32 rsp_length;
	u8 flags;
	#define ATTO_SMPF_ROOT_EXP       0x01 /* expander direct attached */

	u8 reserved[31];

	union {
		u8 byte[1];
		u32 dword[1];
	} data;

};

struct __packed atto_express_ioctl {
	struct atto_express_ioctl_header header;

	union {
		struct atto_firmware_rw_request fwrw;
		struct atto_param_rw_request prw;
		struct atto_channel_list chanlist;
		struct atto_channel_info chaninfo;
		struct atto_ioctl ioctl_hba;
		struct atto_module_info modinfo;
		struct atto_ioctl_vda ioctl_vda;
		struct atto_ioctl_smp ioctl_smp;
		struct atto_csmi csmi;

	} data;
};

/* The struct associated with the code is listed after the definition */
#define EXPRESS_IOCTL_MIN             0x4500
#define EXPRESS_IOCTL_RW_FIRMWARE     0x4500            /* FIRMWARERW    */
#define EXPRESS_IOCTL_READ_PARAMS     0x4501            /* PARAMRW       */
#define EXPRESS_IOCTL_WRITE_PARAMS    0x4502            /* PARAMRW       */
#define EXPRESS_IOCTL_FC_API          0x4503            /* internal      */
#define EXPRESS_IOCTL_GET_CHANNELS    0x4504            /* CHANNELLIST   */
#define EXPRESS_IOCTL_CHAN_INFO       0x4505            /* CHANNELINFO   */
#define EXPRESS_IOCTL_DEFAULT_PARAMS  0x4506            /* PARAMRW       */
#define EXPRESS_ADDR_MEMORY           0x4507            /* MEMADDR       */
#define EXPRESS_RW_MEMORY             0x4508            /* MEMRW         */
#define EXPRESS_TSDK_DUMP             0x4509            /* TSDKDUMP      */
#define EXPRESS_IOCTL_SMP             0x450A            /* IOCTL_SMP     */
#define EXPRESS_CSMI                  0x450B            /* CSMI          */
#define EXPRESS_IOCTL_HBA             0x450C            /* IOCTL_HBA     */
#define EXPRESS_IOCTL_VDA             0x450D            /* IOCTL_VDA     */
#define EXPRESS_IOCTL_GET_ID          0x450E            /* GET_ID        */
#define EXPRESS_IOCTL_GET_MOD_INFO    0x450F            /* MODULE_INFO   */
#define EXPRESS_IOCTL_MAX             0x450F

#endif
