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

#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	(4*(v/4) - 8)

#define RTL8192E_BOOT_IMG_FW	"RTL8192E/boot.img"
#define RTL8192E_MAIN_IMG_FW	"RTL8192E/main.img"
#define RTL8192E_DATA_IMG_FW	"RTL8192E/data.img"

enum firmware_init_step {
	FW_INIT_STEP0_BOOT = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
};

enum opt_rst_type {
	OPT_SYSTEM_RESET = 0,
	OPT_FIRMWARE_RESET = 1,
};

enum desc_packet_type {
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
};

enum firmware_status {
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
};

struct fw_seg_container {
	u16	seg_size;
	u8	*seg_ptr;
};

struct rt_firmware {
	enum firmware_status firmware_status;
	u16		  cmdpacket_frag_thresold;
#define RTL8190_MAX_FIRMWARE_CODE_SIZE	64000
#define MAX_FW_INIT_STEP		3
	u8 firmware_buf[MAX_FW_INIT_STEP][RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u16		  firmware_buf_size[MAX_FW_INIT_STEP];
};

bool init_firmware(struct net_device *dev);
extern void firmware_init_param(struct net_device *dev);

#endif
