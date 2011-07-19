/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_FIRMWARE_H
#define __INC_FIRMWARE_H

#define RTL8190_CPU_START_OFFSET	0x80

#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	(4*(v/4) - 8 )

typedef enum _firmware_init_step{
	FW_INIT_STEP0_BOOT = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
}firmware_init_step_e;

typedef enum _opt_rst_type{
	OPT_SYSTEM_RESET = 0,
	OPT_FIRMWARE_RESET = 1,
}opt_rst_type_e;

typedef enum _desc_packet_type_e{
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
}desc_packet_type_e;

typedef enum _firmware_source{
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,
}firmware_source_e, *pfirmware_source_e;

typedef enum _firmware_status{
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
}firmware_status_e;

struct fw_seg_container {
	u16	seg_size;
	u8	*seg_ptr;
};//, *pfw_seg_container;

typedef struct _rt_firmware{
	firmware_status_e firmware_status;
	u16		  cmdpacket_frag_thresold;
#define RTL8190_MAX_FIRMWARE_CODE_SIZE	64000
#define MAX_FW_INIT_STEP		3
	u8		  firmware_buf[MAX_FW_INIT_STEP][RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u16		  firmware_buf_size[MAX_FW_INIT_STEP];
} rt_firmware, *prt_firmware;

bool init_firmware(struct net_device *dev);
extern void firmware_init_param(struct net_device *dev);

#endif
