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

#include "fjes_hw.h"
#include "fjes.h"
#include "fjes_trace.h"

static void fjes_hw_update_zone_task(struct work_struct *);
static void fjes_hw_epstop_task(struct work_struct *);

/* supported MTU list */
const u32 fjes_support_mtu[] = {
	FJES_MTU_DEFINE(8 * 1024),
	FJES_MTU_DEFINE(16 * 1024),
	FJES_MTU_DEFINE(32 * 1024),
	FJES_MTU_DEFINE(64 * 1024),
	0
};

u32 fjes_hw_rd32(struct fjes_hw *hw, u32 reg)
{
	u8 *base = hw->base;
	u32 value = 0;

	value = readl(&base[reg]);

	return value;
}

static u8 *fjes_hw_iomap(struct fjes_hw *hw)
{
	u8 *base;

	if (!request_mem_region(hw->hw_res.start, hw->hw_res.size,
				fjes_driver_name)) {
		pr_err("request_mem_region failed\n");
		return NULL;
	}

	base = (u8 *)ioremap_nocache(hw->hw_res.start, hw->hw_res.size);

	return base;
}

static void fjes_hw_iounmap(struct fjes_hw *hw)
{
	iounmap(hw->base);
	release_mem_region(hw->hw_res.start, hw->hw_res.size);
}

int fjes_hw_reset(struct fjes_hw *hw)
{
	union REG_DCTL dctl;
	int timeout;

	dctl.reg = 0;
	dctl.bits.reset = 1;
	wr32(XSCT_DCTL, dctl.reg);

	timeout = FJES_DEVICE_RESET_TIMEOUT * 1000;
	dctl.reg = rd32(XSCT_DCTL);
	while ((dctl.bits.reset == 1) && (timeout > 0)) {
		msleep(1000);
		dctl.reg = rd32(XSCT_DCTL);
		timeout -= 1000;
	}

	return timeout > 0 ? 0 : -EIO;
}

static int fjes_hw_get_max_epid(struct fjes_hw *hw)
{
	union REG_MAX_EP info;

	info.reg = rd32(XSCT_MAX_EP);

	return info.bits.maxep;
}

static int fjes_hw_get_my_epid(struct fjes_hw *hw)
{
	union REG_OWNER_EPID info;

	info.reg = rd32(XSCT_OWNER_EPID);

	return info.bits.epid;
}

static int fjes_hw_alloc_shared_status_region(struct fjes_hw *hw)
{
	size_t size;

	size = sizeof(struct fjes_device_shared_info) +
	    (sizeof(u8) * hw->max_epid);
	hw->hw_info.share = kzalloc(size, GFP_KERNEL);
	if (!hw->hw_info.share)
		return -ENOMEM;

	hw->hw_info.share->epnum = hw->max_epid;

	return 0;
}

static void fjes_hw_free_shared_status_region(struct fjes_hw *hw)
{
	kfree(hw->hw_info.share);
	hw->hw_info.share = NULL;
}

static int fjes_hw_alloc_epbuf(struct epbuf_handler *epbh)
{
	void *mem;

	mem = vzalloc(EP_BUFFER_SIZE);
	if (!mem)
		return -ENOMEM;

	epbh->buffer = mem;
	epbh->size = EP_BUFFER_SIZE;

	epbh->info = (union ep_buffer_info *)mem;
	epbh->ring = (u8 *)(mem + sizeof(union ep_buffer_info));

	return 0;
}

static void fjes_hw_free_epbuf(struct epbuf_handler *epbh)
{
	vfree(epbh->buffer);
	epbh->buffer = NULL;
	epbh->size = 0;

	epbh->info = NULL;
	epbh->ring = NULL;
}

void fjes_hw_setup_epbuf(struct epbuf_handler *epbh, u8 *mac_addr, u32 mtu)
{
	union ep_buffer_info *info = epbh->info;
	u16 vlan_id[EP_BUFFER_SUPPORT_VLAN_MAX];
	int i;

	for (i = 0; i < EP_BUFFER_SUPPORT_VLAN_MAX; i++)
		vlan_id[i] = info->v1i.vlan_id[i];

	memset(info, 0, sizeof(union ep_buffer_info));

	info->v1i.version = 0;  /* version 0 */

	for (i = 0; i < ETH_ALEN; i++)
		info->v1i.mac_addr[i] = mac_addr[i];

	info->v1i.head = 0;
	info->v1i.tail = 1;

	info->v1i.info_size = sizeof(union ep_buffer_info);
	info->v1i.buffer_size = epbh->size - info->v1i.info_size;

	info->v1i.frame_max = FJES_MTU_TO_FRAME_SIZE(mtu);
	info->v1i.count_max =
	    EP_RING_NUM(info->v1i.buffer_size, info->v1i.frame_max);

	for (i = 0; i < EP_BUFFER_SUPPORT_VLAN_MAX; i++)
		info->v1i.vlan_id[i] = vlan_id[i];

	info->v1i.rx_status |= FJES_RX_MTU_CHANGING_DONE;
}

void
fjes_hw_init_command_registers(struct fjes_hw *hw,
			       struct fjes_device_command_param *param)
{
	/* Request Buffer length */
	wr32(XSCT_REQBL, (__le32)(param->req_len));
	/* Response Buffer Length */
	wr32(XSCT_RESPBL, (__le32)(param->res_len));

	/* Request Buffer Address */
	wr32(XSCT_REQBAL,
	     (__le32)(param->req_start & GENMASK_ULL(31, 0)));
	wr32(XSCT_REQBAH,
	     (__le32)((param->req_start & GENMASK_ULL(63, 32)) >> 32));

	/* Response Buffer Address */
	wr32(XSCT_RESPBAL,
	     (__le32)(param->res_start & GENMASK_ULL(31, 0)));
	wr32(XSCT_RESPBAH,
	     (__le32)((param->res_start & GENMASK_ULL(63, 32)) >> 32));

	/* Share status address */
	wr32(XSCT_SHSTSAL,
	     (__le32)(param->share_start & GENMASK_ULL(31, 0)));
	wr32(XSCT_SHSTSAH,
	     (__le32)((param->share_start & GENMASK_ULL(63, 32)) >> 32));
}

static int fjes_hw_setup(struct fjes_hw *hw)
{
	u8 mac[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	struct fjes_device_command_param param;
	struct ep_share_mem_info *buf_pair;
	unsigned long flags;
	size_t mem_size;
	int result;
	int epidx;
	void *buf;

	hw->hw_info.max_epid = &hw->max_epid;
	hw->hw_info.my_epid = &hw->my_epid;

	buf = kcalloc(hw->max_epid, sizeof(struct ep_share_mem_info),
		      GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	hw->ep_shm_info = (struct ep_share_mem_info *)buf;

	mem_size = FJES_DEV_REQ_BUF_SIZE(hw->max_epid);
	hw->hw_info.req_buf = kzalloc(mem_size, GFP_KERNEL);
	if (!(hw->hw_info.req_buf))
		return -ENOMEM;

	hw->hw_info.req_buf_size = mem_size;

	mem_size = FJES_DEV_RES_BUF_SIZE(hw->max_epid);
	hw->hw_info.res_buf = kzalloc(mem_size, GFP_KERNEL);
	if (!(hw->hw_info.res_buf))
		return -ENOMEM;

	hw->hw_info.res_buf_size = mem_size;

	result = fjes_hw_alloc_shared_status_region(hw);
	if (result)
		return result;

	hw->hw_info.buffer_share_bit = 0;
	hw->hw_info.buffer_unshare_reserve_bit = 0;

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx != hw->my_epid) {
			buf_pair = &hw->ep_shm_info[epidx];

			result = fjes_hw_alloc_epbuf(&buf_pair->tx);
			if (result)
				return result;

			result = fjes_hw_alloc_epbuf(&buf_pair->rx);
			if (result)
				return result;

			spin_lock_irqsave(&hw->rx_status_lock, flags);
			fjes_hw_setup_epbuf(&buf_pair->tx, mac,
					    fjes_support_mtu[0]);
			fjes_hw_setup_epbuf(&buf_pair->rx, mac,
					    fjes_support_mtu[0]);
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);
		}
	}

	memset(&param, 0, sizeof(param));

	param.req_len = hw->hw_info.req_buf_size;
	param.req_start = __pa(hw->hw_info.req_buf);
	param.res_len = hw->hw_info.res_buf_size;
	param.res_start = __pa(hw->hw_info.res_buf);

	param.share_start = __pa(hw->hw_info.share->ep_status);

	fjes_hw_init_command_registers(hw, &param);

	return 0;
}

static void fjes_hw_cleanup(struct fjes_hw *hw)
{
	int epidx;

	if (!hw->ep_shm_info)
		return;

	fjes_hw_free_shared_status_region(hw);

	kfree(hw->hw_info.req_buf);
	hw->hw_info.req_buf = NULL;

	kfree(hw->hw_info.res_buf);
	hw->hw_info.res_buf = NULL;

	for (epidx = 0; epidx < hw->max_epid ; epidx++) {
		if (epidx == hw->my_epid)
			continue;
		fjes_hw_free_epbuf(&hw->ep_shm_info[epidx].tx);
		fjes_hw_free_epbuf(&hw->ep_shm_info[epidx].rx);
	}

	kfree(hw->ep_shm_info);
	hw->ep_shm_info = NULL;
}

int fjes_hw_init(struct fjes_hw *hw)
{
	int ret;

	hw->base = fjes_hw_iomap(hw);
	if (!hw->base)
		return -EIO;

	ret = fjes_hw_reset(hw);
	if (ret)
		return ret;

	fjes_hw_set_irqmask(hw, REG_ICTL_MASK_ALL, true);

	INIT_WORK(&hw->update_zone_task, fjes_hw_update_zone_task);
	INIT_WORK(&hw->epstop_task, fjes_hw_epstop_task);

	mutex_init(&hw->hw_info.lock);
	spin_lock_init(&hw->rx_status_lock);

	hw->max_epid = fjes_hw_get_max_epid(hw);
	hw->my_epid = fjes_hw_get_my_epid(hw);

	if ((hw->max_epid == 0) || (hw->my_epid >= hw->max_epid))
		return -ENXIO;

	ret = fjes_hw_setup(hw);

	hw->hw_info.trace = vzalloc(FJES_DEBUG_BUFFER_SIZE);
	hw->hw_info.trace_size = FJES_DEBUG_BUFFER_SIZE;

	return ret;
}

void fjes_hw_exit(struct fjes_hw *hw)
{
	int ret;

	if (hw->base) {

		if (hw->debug_mode) {
			/* disable debug mode */
			mutex_lock(&hw->hw_info.lock);
			fjes_hw_stop_debug(hw);
			mutex_unlock(&hw->hw_info.lock);
		}
		vfree(hw->hw_info.trace);
		hw->hw_info.trace = NULL;
		hw->hw_info.trace_size = 0;
		hw->debug_mode = 0;

		ret = fjes_hw_reset(hw);
		if (ret)
			pr_err("%s: reset error", __func__);

		fjes_hw_iounmap(hw);
		hw->base = NULL;
	}

	fjes_hw_cleanup(hw);

	cancel_work_sync(&hw->update_zone_task);
	cancel_work_sync(&hw->epstop_task);
}

static enum fjes_dev_command_response_e
fjes_hw_issue_request_command(struct fjes_hw *hw,
			      enum fjes_dev_command_request_type type)
{
	enum fjes_dev_command_response_e ret = FJES_CMD_STATUS_UNKNOWN;
	union REG_CR cr;
	union REG_CS cs;
	int timeout = FJES_COMMAND_REQ_TIMEOUT * 1000;

	cr.reg = 0;
	cr.bits.req_start = 1;
	cr.bits.req_code = type;
	wr32(XSCT_CR, cr.reg);
	cr.reg = rd32(XSCT_CR);

	if (cr.bits.error == 0) {
		timeout = FJES_COMMAND_REQ_TIMEOUT * 1000;
		cs.reg = rd32(XSCT_CS);

		while ((cs.bits.complete != 1) && timeout > 0) {
			msleep(1000);
			cs.reg = rd32(XSCT_CS);
			timeout -= 1000;
		}

		if (cs.bits.complete == 1)
			ret = FJES_CMD_STATUS_NORMAL;
		else if (timeout <= 0)
			ret = FJES_CMD_STATUS_TIMEOUT;

	} else {
		switch (cr.bits.err_info) {
		case FJES_CMD_REQ_ERR_INFO_PARAM:
			ret = FJES_CMD_STATUS_ERROR_PARAM;
			break;
		case FJES_CMD_REQ_ERR_INFO_STATUS:
			ret = FJES_CMD_STATUS_ERROR_STATUS;
			break;
		default:
			ret = FJES_CMD_STATUS_UNKNOWN;
			break;
		}
	}

	trace_fjes_hw_issue_request_command(&cr, &cs, timeout, ret);

	return ret;
}

int fjes_hw_request_info(struct fjes_hw *hw)
{
	union fjes_device_command_req *req_buf = hw->hw_info.req_buf;
	union fjes_device_command_res *res_buf = hw->hw_info.res_buf;
	enum fjes_dev_command_response_e ret;
	int result;

	memset(req_buf, 0, hw->hw_info.req_buf_size);
	memset(res_buf, 0, hw->hw_info.res_buf_size);

	req_buf->info.length = FJES_DEV_COMMAND_INFO_REQ_LEN;

	res_buf->info.length = 0;
	res_buf->info.code = 0;

	ret = fjes_hw_issue_request_command(hw, FJES_CMD_REQ_INFO);
	trace_fjes_hw_request_info(hw, res_buf);

	result = 0;

	if (FJES_DEV_COMMAND_INFO_RES_LEN((*hw->hw_info.max_epid)) !=
		res_buf->info.length) {
		trace_fjes_hw_request_info_err("Invalid res_buf");
		result = -ENOMSG;
	} else if (ret == FJES_CMD_STATUS_NORMAL) {
		switch (res_buf->info.code) {
		case FJES_CMD_REQ_RES_CODE_NORMAL:
			result = 0;
			break;
		default:
			result = -EPERM;
			break;
		}
	} else {
		switch (ret) {
		case FJES_CMD_STATUS_UNKNOWN:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_TIMEOUT:
			trace_fjes_hw_request_info_err("Timeout");
			result = -EBUSY;
			break;
		case FJES_CMD_STATUS_ERROR_PARAM:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_ERROR_STATUS:
			result = -EPERM;
			break;
		default:
			result = -EPERM;
			break;
		}
	}

	return result;
}

int fjes_hw_register_buff_addr(struct fjes_hw *hw, int dest_epid,
			       struct ep_share_mem_info *buf_pair)
{
	union fjes_device_command_req *req_buf = hw->hw_info.req_buf;
	union fjes_device_command_res *res_buf = hw->hw_info.res_buf;
	enum fjes_dev_command_response_e ret;
	int page_count;
	int timeout;
	int i, idx;
	void *addr;
	int result;

	if (test_bit(dest_epid, &hw->hw_info.buffer_share_bit))
		return 0;

	memset(req_buf, 0, hw->hw_info.req_buf_size);
	memset(res_buf, 0, hw->hw_info.res_buf_size);

	req_buf->share_buffer.length = FJES_DEV_COMMAND_SHARE_BUFFER_REQ_LEN(
						buf_pair->tx.size,
						buf_pair->rx.size);
	req_buf->share_buffer.epid = dest_epid;

	idx = 0;
	req_buf->share_buffer.buffer[idx++] = buf_pair->tx.size;
	page_count = buf_pair->tx.size / EP_BUFFER_INFO_SIZE;
	for (i = 0; i < page_count; i++) {
		addr = ((u8 *)(buf_pair->tx.buffer)) +
				(i * EP_BUFFER_INFO_SIZE);
		req_buf->share_buffer.buffer[idx++] =
				(__le64)(page_to_phys(vmalloc_to_page(addr)) +
						offset_in_page(addr));
	}

	req_buf->share_buffer.buffer[idx++] = buf_pair->rx.size;
	page_count = buf_pair->rx.size / EP_BUFFER_INFO_SIZE;
	for (i = 0; i < page_count; i++) {
		addr = ((u8 *)(buf_pair->rx.buffer)) +
				(i * EP_BUFFER_INFO_SIZE);
		req_buf->share_buffer.buffer[idx++] =
				(__le64)(page_to_phys(vmalloc_to_page(addr)) +
						offset_in_page(addr));
	}

	res_buf->share_buffer.length = 0;
	res_buf->share_buffer.code = 0;

	trace_fjes_hw_register_buff_addr_req(req_buf, buf_pair);

	ret = fjes_hw_issue_request_command(hw, FJES_CMD_REQ_SHARE_BUFFER);

	timeout = FJES_COMMAND_REQ_BUFF_TIMEOUT * 1000;
	while ((ret == FJES_CMD_STATUS_NORMAL) &&
	       (res_buf->share_buffer.length ==
		FJES_DEV_COMMAND_SHARE_BUFFER_RES_LEN) &&
	       (res_buf->share_buffer.code == FJES_CMD_REQ_RES_CODE_BUSY) &&
	       (timeout > 0)) {
			msleep(200 + hw->my_epid * 20);
			timeout -= (200 + hw->my_epid * 20);

			res_buf->share_buffer.length = 0;
			res_buf->share_buffer.code = 0;

			ret = fjes_hw_issue_request_command(
					hw, FJES_CMD_REQ_SHARE_BUFFER);
	}

	result = 0;

	trace_fjes_hw_register_buff_addr(res_buf, timeout);

	if (res_buf->share_buffer.length !=
			FJES_DEV_COMMAND_SHARE_BUFFER_RES_LEN) {
		trace_fjes_hw_register_buff_addr_err("Invalid res_buf");
		result = -ENOMSG;
	} else if (ret == FJES_CMD_STATUS_NORMAL) {
		switch (res_buf->share_buffer.code) {
		case FJES_CMD_REQ_RES_CODE_NORMAL:
			result = 0;
			set_bit(dest_epid, &hw->hw_info.buffer_share_bit);
			break;
		case FJES_CMD_REQ_RES_CODE_BUSY:
			trace_fjes_hw_register_buff_addr_err("Busy Timeout");
			result = -EBUSY;
			break;
		default:
			result = -EPERM;
			break;
		}
	} else {
		switch (ret) {
		case FJES_CMD_STATUS_UNKNOWN:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_TIMEOUT:
			trace_fjes_hw_register_buff_addr_err("Timeout");
			result = -EBUSY;
			break;
		case FJES_CMD_STATUS_ERROR_PARAM:
		case FJES_CMD_STATUS_ERROR_STATUS:
		default:
			result = -EPERM;
			break;
		}
	}

	return result;
}

int fjes_hw_unregister_buff_addr(struct fjes_hw *hw, int dest_epid)
{
	union fjes_device_command_req *req_buf = hw->hw_info.req_buf;
	union fjes_device_command_res *res_buf = hw->hw_info.res_buf;
	struct fjes_device_shared_info *share = hw->hw_info.share;
	enum fjes_dev_command_response_e ret;
	int timeout;
	int result;

	if (!hw->base)
		return -EPERM;

	if (!req_buf || !res_buf || !share)
		return -EPERM;

	if (!test_bit(dest_epid, &hw->hw_info.buffer_share_bit))
		return 0;

	memset(req_buf, 0, hw->hw_info.req_buf_size);
	memset(res_buf, 0, hw->hw_info.res_buf_size);

	req_buf->unshare_buffer.length =
			FJES_DEV_COMMAND_UNSHARE_BUFFER_REQ_LEN;
	req_buf->unshare_buffer.epid = dest_epid;

	res_buf->unshare_buffer.length = 0;
	res_buf->unshare_buffer.code = 0;

	trace_fjes_hw_unregister_buff_addr_req(req_buf);
	ret = fjes_hw_issue_request_command(hw, FJES_CMD_REQ_UNSHARE_BUFFER);

	timeout = FJES_COMMAND_REQ_BUFF_TIMEOUT * 1000;
	while ((ret == FJES_CMD_STATUS_NORMAL) &&
	       (res_buf->unshare_buffer.length ==
		FJES_DEV_COMMAND_UNSHARE_BUFFER_RES_LEN) &&
	       (res_buf->unshare_buffer.code ==
		FJES_CMD_REQ_RES_CODE_BUSY) &&
	       (timeout > 0)) {
		msleep(200 + hw->my_epid * 20);
		timeout -= (200 + hw->my_epid * 20);

		res_buf->unshare_buffer.length = 0;
		res_buf->unshare_buffer.code = 0;

		ret =
		fjes_hw_issue_request_command(hw, FJES_CMD_REQ_UNSHARE_BUFFER);
	}

	result = 0;

	trace_fjes_hw_unregister_buff_addr(res_buf, timeout);

	if (res_buf->unshare_buffer.length !=
			FJES_DEV_COMMAND_UNSHARE_BUFFER_RES_LEN) {
		trace_fjes_hw_unregister_buff_addr_err("Invalid res_buf");
		result = -ENOMSG;
	} else if (ret == FJES_CMD_STATUS_NORMAL) {
		switch (res_buf->unshare_buffer.code) {
		case FJES_CMD_REQ_RES_CODE_NORMAL:
			result = 0;
			clear_bit(dest_epid, &hw->hw_info.buffer_share_bit);
			break;
		case FJES_CMD_REQ_RES_CODE_BUSY:
			trace_fjes_hw_unregister_buff_addr_err("Busy Timeout");
			result = -EBUSY;
			break;
		default:
			result = -EPERM;
			break;
		}
	} else {
		switch (ret) {
		case FJES_CMD_STATUS_UNKNOWN:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_TIMEOUT:
			trace_fjes_hw_unregister_buff_addr_err("Timeout");
			result = -EBUSY;
			break;
		case FJES_CMD_STATUS_ERROR_PARAM:
		case FJES_CMD_STATUS_ERROR_STATUS:
		default:
			result = -EPERM;
			break;
		}
	}

	return result;
}

int fjes_hw_raise_interrupt(struct fjes_hw *hw, int dest_epid,
			    enum REG_ICTL_MASK  mask)
{
	u32 ig = mask | dest_epid;

	wr32(XSCT_IG, cpu_to_le32(ig));

	return 0;
}

u32 fjes_hw_capture_interrupt_status(struct fjes_hw *hw)
{
	u32 cur_is;

	cur_is = rd32(XSCT_IS);

	return cur_is;
}

void fjes_hw_set_irqmask(struct fjes_hw *hw,
			 enum REG_ICTL_MASK intr_mask, bool mask)
{
	if (mask)
		wr32(XSCT_IMS, intr_mask);
	else
		wr32(XSCT_IMC, intr_mask);
}

bool fjes_hw_epid_is_same_zone(struct fjes_hw *hw, int epid)
{
	if (epid >= hw->max_epid)
		return false;

	if ((hw->ep_shm_info[epid].es_status !=
			FJES_ZONING_STATUS_ENABLE) ||
		(hw->ep_shm_info[hw->my_epid].zone ==
			FJES_ZONING_ZONE_TYPE_NONE))
		return false;
	else
		return (hw->ep_shm_info[epid].zone ==
				hw->ep_shm_info[hw->my_epid].zone);
}

int fjes_hw_epid_is_shared(struct fjes_device_shared_info *share,
			   int dest_epid)
{
	int value = false;

	if (dest_epid < share->epnum)
		value = share->ep_status[dest_epid];

	return value;
}

static bool fjes_hw_epid_is_stop_requested(struct fjes_hw *hw, int src_epid)
{
	return test_bit(src_epid, &hw->txrx_stop_req_bit);
}

static bool fjes_hw_epid_is_stop_process_done(struct fjes_hw *hw, int src_epid)
{
	return (hw->ep_shm_info[src_epid].tx.info->v1i.rx_status &
			FJES_RX_STOP_REQ_DONE);
}

enum ep_partner_status
fjes_hw_get_partner_ep_status(struct fjes_hw *hw, int epid)
{
	enum ep_partner_status status;

	if (fjes_hw_epid_is_shared(hw->hw_info.share, epid)) {
		if (fjes_hw_epid_is_stop_requested(hw, epid)) {
			status = EP_PARTNER_WAITING;
		} else {
			if (fjes_hw_epid_is_stop_process_done(hw, epid))
				status = EP_PARTNER_COMPLETE;
			else
				status = EP_PARTNER_SHARED;
		}
	} else {
		status = EP_PARTNER_UNSHARE;
	}

	return status;
}

void fjes_hw_raise_epstop(struct fjes_hw *hw)
{
	enum ep_partner_status status;
	unsigned long flags;
	int epidx;

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		status = fjes_hw_get_partner_ep_status(hw, epidx);
		switch (status) {
		case EP_PARTNER_SHARED:
			fjes_hw_raise_interrupt(hw, epidx,
						REG_ICTL_MASK_TXRX_STOP_REQ);
			hw->ep_shm_info[epidx].ep_stats.send_intr_unshare += 1;
			break;
		default:
			break;
		}

		set_bit(epidx, &hw->hw_info.buffer_unshare_reserve_bit);
		set_bit(epidx, &hw->txrx_stop_req_bit);

		spin_lock_irqsave(&hw->rx_status_lock, flags);
		hw->ep_shm_info[epidx].tx.info->v1i.rx_status |=
				FJES_RX_STOP_REQ_REQUEST;
		spin_unlock_irqrestore(&hw->rx_status_lock, flags);
	}
}

int fjes_hw_wait_epstop(struct fjes_hw *hw)
{
	enum ep_partner_status status;
	union ep_buffer_info *info;
	int wait_time = 0;
	int epidx;

	while (hw->hw_info.buffer_unshare_reserve_bit &&
	       (wait_time < FJES_COMMAND_EPSTOP_WAIT_TIMEOUT * 1000)) {
		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			if (epidx == hw->my_epid)
				continue;
			status = fjes_hw_epid_is_shared(hw->hw_info.share,
							epidx);
			info = hw->ep_shm_info[epidx].rx.info;
			if ((!status ||
			     (info->v1i.rx_status &
			      FJES_RX_STOP_REQ_DONE)) &&
			    test_bit(epidx,
				     &hw->hw_info.buffer_unshare_reserve_bit)) {
				clear_bit(epidx,
					  &hw->hw_info.buffer_unshare_reserve_bit);
			}
		}

		msleep(100);
		wait_time += 100;
	}

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;
		if (test_bit(epidx, &hw->hw_info.buffer_unshare_reserve_bit))
			clear_bit(epidx,
				  &hw->hw_info.buffer_unshare_reserve_bit);
	}

	return (wait_time < FJES_COMMAND_EPSTOP_WAIT_TIMEOUT * 1000)
			? 0 : -EBUSY;
}

bool fjes_hw_check_epbuf_version(struct epbuf_handler *epbh, u32 version)
{
	union ep_buffer_info *info = epbh->info;

	return (info->common.version == version);
}

bool fjes_hw_check_mtu(struct epbuf_handler *epbh, u32 mtu)
{
	union ep_buffer_info *info = epbh->info;

	return ((info->v1i.frame_max == FJES_MTU_TO_FRAME_SIZE(mtu)) &&
		info->v1i.rx_status & FJES_RX_MTU_CHANGING_DONE);
}

bool fjes_hw_check_vlan_id(struct epbuf_handler *epbh, u16 vlan_id)
{
	union ep_buffer_info *info = epbh->info;
	bool ret = false;
	int i;

	if (vlan_id == 0) {
		ret = true;
	} else {
		for (i = 0; i < EP_BUFFER_SUPPORT_VLAN_MAX; i++) {
			if (vlan_id == info->v1i.vlan_id[i]) {
				ret = true;
				break;
			}
		}
	}
	return ret;
}

bool fjes_hw_set_vlan_id(struct epbuf_handler *epbh, u16 vlan_id)
{
	union ep_buffer_info *info = epbh->info;
	int i;

	for (i = 0; i < EP_BUFFER_SUPPORT_VLAN_MAX; i++) {
		if (info->v1i.vlan_id[i] == 0) {
			info->v1i.vlan_id[i] = vlan_id;
			return true;
		}
	}
	return false;
}

void fjes_hw_del_vlan_id(struct epbuf_handler *epbh, u16 vlan_id)
{
	union ep_buffer_info *info = epbh->info;
	int i;

	if (0 != vlan_id) {
		for (i = 0; i < EP_BUFFER_SUPPORT_VLAN_MAX; i++) {
			if (vlan_id == info->v1i.vlan_id[i])
				info->v1i.vlan_id[i] = 0;
		}
	}
}

bool fjes_hw_epbuf_rx_is_empty(struct epbuf_handler *epbh)
{
	union ep_buffer_info *info = epbh->info;

	if (!(info->v1i.rx_status & FJES_RX_MTU_CHANGING_DONE))
		return true;

	if (info->v1i.count_max == 0)
		return true;

	return EP_RING_EMPTY(info->v1i.head, info->v1i.tail,
			     info->v1i.count_max);
}

void *fjes_hw_epbuf_rx_curpkt_get_addr(struct epbuf_handler *epbh,
				       size_t *psize)
{
	union ep_buffer_info *info = epbh->info;
	struct esmem_frame *ring_frame;
	void *frame;

	ring_frame = (struct esmem_frame *)&(epbh->ring[EP_RING_INDEX
					     (info->v1i.head,
					      info->v1i.count_max) *
					     info->v1i.frame_max]);

	*psize = (size_t)ring_frame->frame_size;

	frame = ring_frame->frame_data;

	return frame;
}

void fjes_hw_epbuf_rx_curpkt_drop(struct epbuf_handler *epbh)
{
	union ep_buffer_info *info = epbh->info;

	if (fjes_hw_epbuf_rx_is_empty(epbh))
		return;

	EP_RING_INDEX_INC(epbh->info->v1i.head, info->v1i.count_max);
}

int fjes_hw_epbuf_tx_pkt_send(struct epbuf_handler *epbh,
			      void *frame, size_t size)
{
	union ep_buffer_info *info = epbh->info;
	struct esmem_frame *ring_frame;

	if (EP_RING_FULL(info->v1i.head, info->v1i.tail, info->v1i.count_max))
		return -ENOBUFS;

	ring_frame = (struct esmem_frame *)&(epbh->ring[EP_RING_INDEX
					     (info->v1i.tail - 1,
					      info->v1i.count_max) *
					     info->v1i.frame_max]);

	ring_frame->frame_size = size;
	memcpy((void *)(ring_frame->frame_data), (void *)frame, size);

	EP_RING_INDEX_INC(epbh->info->v1i.tail, info->v1i.count_max);

	return 0;
}

static void fjes_hw_update_zone_task(struct work_struct *work)
{
	struct fjes_hw *hw = container_of(work,
			struct fjes_hw, update_zone_task);

	struct my_s {u8 es_status; u8 zone; } *info;
	union fjes_device_command_res *res_buf;
	enum ep_partner_status pstatus;

	struct fjes_adapter *adapter;
	struct net_device *netdev;
	unsigned long flags;

	ulong unshare_bit = 0;
	ulong share_bit = 0;
	ulong irq_bit = 0;

	int epidx;
	int ret;

	adapter = (struct fjes_adapter *)hw->back;
	netdev = adapter->netdev;
	res_buf = hw->hw_info.res_buf;
	info = (struct my_s *)&res_buf->info.info;

	mutex_lock(&hw->hw_info.lock);

	ret = fjes_hw_request_info(hw);
	switch (ret) {
	case -ENOMSG:
	case -EBUSY:
	default:
		if (!work_pending(&adapter->force_close_task)) {
			adapter->force_reset = true;
			schedule_work(&adapter->force_close_task);
		}
		break;

	case 0:

		for (epidx = 0; epidx < hw->max_epid; epidx++) {
			if (epidx == hw->my_epid) {
				hw->ep_shm_info[epidx].es_status =
					info[epidx].es_status;
				hw->ep_shm_info[epidx].zone =
					info[epidx].zone;
				continue;
			}

			pstatus = fjes_hw_get_partner_ep_status(hw, epidx);
			switch (pstatus) {
			case EP_PARTNER_UNSHARE:
			default:
				if ((info[epidx].zone !=
					FJES_ZONING_ZONE_TYPE_NONE) &&
				    (info[epidx].es_status ==
					FJES_ZONING_STATUS_ENABLE) &&
				    (info[epidx].zone ==
					info[hw->my_epid].zone))
					set_bit(epidx, &share_bit);
				else
					set_bit(epidx, &unshare_bit);
				break;

			case EP_PARTNER_COMPLETE:
			case EP_PARTNER_WAITING:
				if ((info[epidx].zone ==
					FJES_ZONING_ZONE_TYPE_NONE) ||
				    (info[epidx].es_status !=
					FJES_ZONING_STATUS_ENABLE) ||
				    (info[epidx].zone !=
					info[hw->my_epid].zone)) {
					set_bit(epidx,
						&adapter->unshare_watch_bitmask);
					set_bit(epidx,
						&hw->hw_info.buffer_unshare_reserve_bit);
				}
				break;

			case EP_PARTNER_SHARED:
				if ((info[epidx].zone ==
					FJES_ZONING_ZONE_TYPE_NONE) ||
				    (info[epidx].es_status !=
					FJES_ZONING_STATUS_ENABLE) ||
				    (info[epidx].zone !=
					info[hw->my_epid].zone))
					set_bit(epidx, &irq_bit);
				break;
			}

			hw->ep_shm_info[epidx].es_status =
				info[epidx].es_status;
			hw->ep_shm_info[epidx].zone = info[epidx].zone;
		}
		break;
	}

	mutex_unlock(&hw->hw_info.lock);

	for (epidx = 0; epidx < hw->max_epid; epidx++) {
		if (epidx == hw->my_epid)
			continue;

		if (test_bit(epidx, &share_bit)) {
			spin_lock_irqsave(&hw->rx_status_lock, flags);
			fjes_hw_setup_epbuf(&hw->ep_shm_info[epidx].tx,
					    netdev->dev_addr, netdev->mtu);
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);

			mutex_lock(&hw->hw_info.lock);

			ret = fjes_hw_register_buff_addr(
				hw, epidx, &hw->ep_shm_info[epidx]);

			switch (ret) {
			case 0:
				break;
			case -ENOMSG:
			case -EBUSY:
			default:
				if (!work_pending(&adapter->force_close_task)) {
					adapter->force_reset = true;
					schedule_work(
					  &adapter->force_close_task);
				}
				break;
			}
			mutex_unlock(&hw->hw_info.lock);

			hw->ep_shm_info[epidx].ep_stats
					      .com_regist_buf_exec += 1;
		}

		if (test_bit(epidx, &unshare_bit)) {
			mutex_lock(&hw->hw_info.lock);

			ret = fjes_hw_unregister_buff_addr(hw, epidx);

			switch (ret) {
			case 0:
				break;
			case -ENOMSG:
			case -EBUSY:
			default:
				if (!work_pending(&adapter->force_close_task)) {
					adapter->force_reset = true;
					schedule_work(
					  &adapter->force_close_task);
				}
				break;
			}

			mutex_unlock(&hw->hw_info.lock);

			hw->ep_shm_info[epidx].ep_stats
					      .com_unregist_buf_exec += 1;

			if (ret == 0) {
				spin_lock_irqsave(&hw->rx_status_lock, flags);
				fjes_hw_setup_epbuf(
					&hw->ep_shm_info[epidx].tx,
					netdev->dev_addr, netdev->mtu);
				spin_unlock_irqrestore(&hw->rx_status_lock,
						       flags);
			}
		}

		if (test_bit(epidx, &irq_bit)) {
			fjes_hw_raise_interrupt(hw, epidx,
						REG_ICTL_MASK_TXRX_STOP_REQ);

			hw->ep_shm_info[epidx].ep_stats.send_intr_unshare += 1;

			set_bit(epidx, &hw->txrx_stop_req_bit);
			spin_lock_irqsave(&hw->rx_status_lock, flags);
			hw->ep_shm_info[epidx].tx.
				info->v1i.rx_status |=
					FJES_RX_STOP_REQ_REQUEST;
			spin_unlock_irqrestore(&hw->rx_status_lock, flags);
			set_bit(epidx, &hw->hw_info.buffer_unshare_reserve_bit);
		}
	}

	if (irq_bit || adapter->unshare_watch_bitmask) {
		if (!work_pending(&adapter->unshare_watch_task))
			queue_work(adapter->control_wq,
				   &adapter->unshare_watch_task);
	}
}

static void fjes_hw_epstop_task(struct work_struct *work)
{
	struct fjes_hw *hw = container_of(work, struct fjes_hw, epstop_task);
	struct fjes_adapter *adapter = (struct fjes_adapter *)hw->back;
	unsigned long flags;

	ulong remain_bit;
	int epid_bit;

	while ((remain_bit = hw->epstop_req_bit)) {
		for (epid_bit = 0; remain_bit; remain_bit >>= 1, epid_bit++) {
			if (remain_bit & 1) {
				spin_lock_irqsave(&hw->rx_status_lock, flags);
				hw->ep_shm_info[epid_bit].
					tx.info->v1i.rx_status |=
						FJES_RX_STOP_REQ_DONE;
				spin_unlock_irqrestore(&hw->rx_status_lock,
						       flags);

				clear_bit(epid_bit, &hw->epstop_req_bit);
				set_bit(epid_bit,
					&adapter->unshare_watch_bitmask);

				if (!work_pending(&adapter->unshare_watch_task))
					queue_work(
						adapter->control_wq,
						&adapter->unshare_watch_task);
			}
		}
	}
}

int fjes_hw_start_debug(struct fjes_hw *hw)
{
	union fjes_device_command_req *req_buf = hw->hw_info.req_buf;
	union fjes_device_command_res *res_buf = hw->hw_info.res_buf;
	enum fjes_dev_command_response_e ret;
	int page_count;
	int result = 0;
	void *addr;
	int i;

	if (!hw->hw_info.trace)
		return -EPERM;
	memset(hw->hw_info.trace, 0, FJES_DEBUG_BUFFER_SIZE);

	memset(req_buf, 0, hw->hw_info.req_buf_size);
	memset(res_buf, 0, hw->hw_info.res_buf_size);

	req_buf->start_trace.length =
		FJES_DEV_COMMAND_START_DBG_REQ_LEN(hw->hw_info.trace_size);
	req_buf->start_trace.mode = hw->debug_mode;
	req_buf->start_trace.buffer_len = hw->hw_info.trace_size;
	page_count = hw->hw_info.trace_size / FJES_DEBUG_PAGE_SIZE;
	for (i = 0; i < page_count; i++) {
		addr = ((u8 *)hw->hw_info.trace) + i * FJES_DEBUG_PAGE_SIZE;
		req_buf->start_trace.buffer[i] =
			(__le64)(page_to_phys(vmalloc_to_page(addr)) +
			offset_in_page(addr));
	}

	res_buf->start_trace.length = 0;
	res_buf->start_trace.code = 0;

	trace_fjes_hw_start_debug_req(req_buf);
	ret = fjes_hw_issue_request_command(hw, FJES_CMD_REQ_START_DEBUG);
	trace_fjes_hw_start_debug(res_buf);

	if (res_buf->start_trace.length !=
		FJES_DEV_COMMAND_START_DBG_RES_LEN) {
		result = -ENOMSG;
		trace_fjes_hw_start_debug_err("Invalid res_buf");
	} else if (ret == FJES_CMD_STATUS_NORMAL) {
		switch (res_buf->start_trace.code) {
		case FJES_CMD_REQ_RES_CODE_NORMAL:
			result = 0;
			break;
		default:
			result = -EPERM;
			break;
		}
	} else {
		switch (ret) {
		case FJES_CMD_STATUS_UNKNOWN:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_TIMEOUT:
			trace_fjes_hw_start_debug_err("Busy Timeout");
			result = -EBUSY;
			break;
		case FJES_CMD_STATUS_ERROR_PARAM:
		case FJES_CMD_STATUS_ERROR_STATUS:
		default:
			result = -EPERM;
			break;
		}
	}

	return result;
}

int fjes_hw_stop_debug(struct fjes_hw *hw)
{
	union fjes_device_command_req *req_buf = hw->hw_info.req_buf;
	union fjes_device_command_res *res_buf = hw->hw_info.res_buf;
	enum fjes_dev_command_response_e ret;
	int result = 0;

	if (!hw->hw_info.trace)
		return -EPERM;

	memset(req_buf, 0, hw->hw_info.req_buf_size);
	memset(res_buf, 0, hw->hw_info.res_buf_size);
	req_buf->stop_trace.length = FJES_DEV_COMMAND_STOP_DBG_REQ_LEN;

	res_buf->stop_trace.length = 0;
	res_buf->stop_trace.code = 0;

	ret = fjes_hw_issue_request_command(hw, FJES_CMD_REQ_STOP_DEBUG);
	trace_fjes_hw_stop_debug(res_buf);

	if (res_buf->stop_trace.length != FJES_DEV_COMMAND_STOP_DBG_RES_LEN) {
		trace_fjes_hw_stop_debug_err("Invalid res_buf");
		result = -ENOMSG;
	} else if (ret == FJES_CMD_STATUS_NORMAL) {
		switch (res_buf->stop_trace.code) {
		case FJES_CMD_REQ_RES_CODE_NORMAL:
			result = 0;
			hw->debug_mode = 0;
			break;
		default:
			result = -EPERM;
			break;
		}
	} else {
		switch (ret) {
		case FJES_CMD_STATUS_UNKNOWN:
			result = -EPERM;
			break;
		case FJES_CMD_STATUS_TIMEOUT:
			result = -EBUSY;
			trace_fjes_hw_stop_debug_err("Busy Timeout");
			break;
		case FJES_CMD_STATUS_ERROR_PARAM:
		case FJES_CMD_STATUS_ERROR_STATUS:
		default:
			result = -EPERM;
			break;
		}
	}

	return result;
}
