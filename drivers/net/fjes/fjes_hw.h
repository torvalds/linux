/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#ifndef FJES_HW_H_
#define FJES_HW_H_

#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/vmalloc.h>

#include "fjes_regs.h"

struct fjes_hw;

#define EP_BUFFER_SUPPORT_VLAN_MAX 4
#define EP_BUFFER_INFO_SIZE 4096

#define FJES_DEVICE_RESET_TIMEOUT  ((17 + 1) * 3 * 8) /* sec */
#define FJES_COMMAND_REQ_TIMEOUT  ((5 + 1) * 3 * 8) /* sec */
#define FJES_COMMAND_REQ_BUFF_TIMEOUT	(60 * 3) /* sec */
#define FJES_COMMAND_EPSTOP_WAIT_TIMEOUT	(1) /* sec */

#define FJES_CMD_REQ_ERR_INFO_PARAM  (0x0001)
#define FJES_CMD_REQ_ERR_INFO_STATUS (0x0002)

#define FJES_CMD_REQ_RES_CODE_NORMAL (0)
#define FJES_CMD_REQ_RES_CODE_BUSY   (1)

#define FJES_ZONING_STATUS_DISABLE	(0x00)
#define FJES_ZONING_STATUS_ENABLE	(0x01)
#define FJES_ZONING_STATUS_INVALID	(0xFF)

#define FJES_ZONING_ZONE_TYPE_NONE (0xFF)

#define FJES_TX_DELAY_SEND_NONE		(0)
#define FJES_TX_DELAY_SEND_PENDING	(1)

#define FJES_RX_STOP_REQ_NONE		(0x0)
#define FJES_RX_STOP_REQ_DONE		(0x1)
#define FJES_RX_STOP_REQ_REQUEST	(0x2)
#define FJES_RX_POLL_WORK		(0x4)
#define FJES_RX_MTU_CHANGING_DONE	(0x8)

#define EP_BUFFER_SIZE \
	(((sizeof(union ep_buffer_info) + (128 * (64 * 1024))) \
		/ EP_BUFFER_INFO_SIZE) * EP_BUFFER_INFO_SIZE)

#define EP_RING_NUM(buffer_size, frame_size) \
		(u32)((buffer_size) / (frame_size))
#define EP_RING_INDEX(_num, _max) (((_num) + (_max)) % (_max))
#define EP_RING_INDEX_INC(_num, _max) \
	((_num) = EP_RING_INDEX((_num) + 1, (_max)))
#define EP_RING_FULL(_head, _tail, _max)				\
	(0 == EP_RING_INDEX(((_tail) - (_head)), (_max)))
#define EP_RING_EMPTY(_head, _tail, _max) \
	(1 == EP_RING_INDEX(((_tail) - (_head)), (_max)))

#define FJES_MTU_TO_BUFFER_SIZE(mtu) \
	(ETH_HLEN + VLAN_HLEN + (mtu) + ETH_FCS_LEN)
#define FJES_MTU_TO_FRAME_SIZE(mtu) \
	(sizeof(struct esmem_frame) + FJES_MTU_TO_BUFFER_SIZE(mtu))
#define FJES_MTU_DEFINE(size) \
	((size) - sizeof(struct esmem_frame) - \
	(ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN))

#define FJES_DEV_COMMAND_INFO_REQ_LEN	(4)
#define FJES_DEV_COMMAND_INFO_RES_LEN(epnum) (8 + 2 * (epnum))
#define FJES_DEV_COMMAND_SHARE_BUFFER_REQ_LEN(txb, rxb) \
	(24 + (8 * ((txb) / EP_BUFFER_INFO_SIZE + (rxb) / EP_BUFFER_INFO_SIZE)))
#define FJES_DEV_COMMAND_SHARE_BUFFER_RES_LEN	(8)
#define FJES_DEV_COMMAND_UNSHARE_BUFFER_REQ_LEN	(8)
#define FJES_DEV_COMMAND_UNSHARE_BUFFER_RES_LEN	(8)

#define FJES_DEV_REQ_BUF_SIZE(maxep) \
	FJES_DEV_COMMAND_SHARE_BUFFER_REQ_LEN(EP_BUFFER_SIZE, EP_BUFFER_SIZE)
#define FJES_DEV_RES_BUF_SIZE(maxep) \
	FJES_DEV_COMMAND_INFO_RES_LEN(maxep)

/* Frame & MTU */
struct esmem_frame {
	__le32 frame_size;
	u8 frame_data[];
};

/* EP partner status */
enum ep_partner_status {
	EP_PARTNER_UNSHARE,
	EP_PARTNER_SHARED,
	EP_PARTNER_WAITING,
	EP_PARTNER_COMPLETE,
	EP_PARTNER_STATUS_MAX,
};

/* shared status region */
struct fjes_device_shared_info {
	int epnum;
	u8 ep_status[];
};

/* structures for command control request data*/
union fjes_device_command_req {
	struct {
		__le32 length;
	} info;
	struct {
		__le32 length;
		__le32 epid;
		__le64 buffer[];
	} share_buffer;
	struct {
		__le32 length;
		__le32 epid;
	} unshare_buffer;
	struct {
		__le32 length;
		__le32 mode;
		__le64 buffer_len;
		__le64 buffer[];
	} start_trace;
	struct {
		__le32 length;
	} stop_trace;
};

/* structures for command control response data */
union fjes_device_command_res {
	struct {
		__le32 length;
		__le32 code;
		struct {
			u8 es_status;
			u8 zone;
		} info[];
	} info;
	struct {
		__le32 length;
		__le32 code;
	} share_buffer;
	struct {
		__le32 length;
		__le32 code;
	} unshare_buffer;
	struct {
		__le32 length;
		__le32 code;
	} start_trace;
	struct {
		__le32 length;
		__le32 code;
	} stop_trace;
};

/* request command type */
enum fjes_dev_command_request_type {
	FJES_CMD_REQ_INFO		= 0x0001,
	FJES_CMD_REQ_SHARE_BUFFER	= 0x0002,
	FJES_CMD_REQ_UNSHARE_BUFFER	= 0x0004,
};

/* parameter for command control */
struct fjes_device_command_param {
	u32 req_len;
	phys_addr_t req_start;
	u32 res_len;
	phys_addr_t res_start;
	phys_addr_t share_start;
};

/* error code for command control */
enum fjes_dev_command_response_e {
	FJES_CMD_STATUS_UNKNOWN,
	FJES_CMD_STATUS_NORMAL,
	FJES_CMD_STATUS_TIMEOUT,
	FJES_CMD_STATUS_ERROR_PARAM,
	FJES_CMD_STATUS_ERROR_STATUS,
};

/* EP buffer information */
union ep_buffer_info {
	u8 raw[EP_BUFFER_INFO_SIZE];

	struct _ep_buffer_info_common_t {
		u32 version;
	} common;

	struct _ep_buffer_info_v1_t {
		u32 version;
		u32 info_size;

		u32 buffer_size;
		u16 count_max;

		u16 _rsv_1;

		u32 frame_max;
		u8 mac_addr[ETH_ALEN];

		u16 _rsv_2;
		u32 _rsv_3;

		u16 tx_status;
		u16 rx_status;

		u32 head;
		u32 tail;

		u16 vlan_id[EP_BUFFER_SUPPORT_VLAN_MAX];

	} v1i;

};

/* buffer pair for Extended Partition */
struct ep_share_mem_info {
	struct epbuf_handler {
		void *buffer;
		size_t size;
		union ep_buffer_info *info;
		u8 *ring;
	} tx, rx;

	struct rtnl_link_stats64 net_stats;

	u16 tx_status_work;

	u8 es_status;
	u8 zone;
};

struct es_device_trace {
	u32 record_num;
	u32 current_record;
	u32 status_flag;
	u32 _rsv;

	struct {
			u16 epid;
			u16 dir_offset;
			u32 data;
			u64 tsc;
	} record[];
};

struct fjes_hw_info {
	struct fjes_device_shared_info *share;
	union fjes_device_command_req *req_buf;
	u64 req_buf_size;
	union fjes_device_command_res *res_buf;
	u64 res_buf_size;

	int *my_epid;
	int *max_epid;

	struct es_device_trace *trace;
	u64 trace_size;

	struct mutex lock; /* buffer lock*/

	unsigned long buffer_share_bit;
	unsigned long buffer_unshare_reserve_bit;
};

struct fjes_hw {
	void *back;

	unsigned long txrx_stop_req_bit;
	unsigned long epstop_req_bit;
	struct work_struct update_zone_task;
	struct work_struct epstop_task;

	int my_epid;
	int max_epid;

	struct ep_share_mem_info *ep_shm_info;

	struct fjes_hw_resource {
		u64 start;
		u64 size;
		int irq;
	} hw_res;

	u8 *base;

	struct fjes_hw_info hw_info;

	spinlock_t rx_status_lock; /* spinlock for rx_status */
};

int fjes_hw_init(struct fjes_hw *);
void fjes_hw_exit(struct fjes_hw *);
int fjes_hw_reset(struct fjes_hw *);
int fjes_hw_request_info(struct fjes_hw *);
int fjes_hw_register_buff_addr(struct fjes_hw *, int,
			       struct ep_share_mem_info *);
int fjes_hw_unregister_buff_addr(struct fjes_hw *, int);
void fjes_hw_init_command_registers(struct fjes_hw *,
				    struct fjes_device_command_param *);
void fjes_hw_setup_epbuf(struct epbuf_handler *, u8 *, u32);
int fjes_hw_raise_interrupt(struct fjes_hw *, int, enum REG_ICTL_MASK);
void fjes_hw_set_irqmask(struct fjes_hw *, enum REG_ICTL_MASK, bool);
u32 fjes_hw_capture_interrupt_status(struct fjes_hw *);
void fjes_hw_raise_epstop(struct fjes_hw *);
int fjes_hw_wait_epstop(struct fjes_hw *);
enum ep_partner_status
	fjes_hw_get_partner_ep_status(struct fjes_hw *, int);

bool fjes_hw_epid_is_same_zone(struct fjes_hw *, int);
int fjes_hw_epid_is_shared(struct fjes_device_shared_info *, int);
bool fjes_hw_check_epbuf_version(struct epbuf_handler *, u32);
bool fjes_hw_check_mtu(struct epbuf_handler *, u32);
bool fjes_hw_check_vlan_id(struct epbuf_handler *, u16);
bool fjes_hw_set_vlan_id(struct epbuf_handler *, u16);
void fjes_hw_del_vlan_id(struct epbuf_handler *, u16);
bool fjes_hw_epbuf_rx_is_empty(struct epbuf_handler *);
void *fjes_hw_epbuf_rx_curpkt_get_addr(struct epbuf_handler *, size_t *);
void fjes_hw_epbuf_rx_curpkt_drop(struct epbuf_handler *);
int fjes_hw_epbuf_tx_pkt_send(struct epbuf_handler *, void *, size_t);

#endif /* FJES_HW_H_ */
