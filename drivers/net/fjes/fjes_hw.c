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

			fjes_hw_setup_epbuf(&buf_pair->tx, mac,
					    fjes_support_mtu[0]);
			fjes_hw_setup_epbuf(&buf_pair->rx, mac,
					    fjes_support_mtu[0]);
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

	mutex_init(&hw->hw_info.lock);

	hw->max_epid = fjes_hw_get_max_epid(hw);
	hw->my_epid = fjes_hw_get_my_epid(hw);

	if ((hw->max_epid == 0) || (hw->my_epid >= hw->max_epid))
		return -ENXIO;

	ret = fjes_hw_setup(hw);

	return ret;
}

void fjes_hw_set_irqmask(struct fjes_hw *hw,
			 enum REG_ICTL_MASK intr_mask, bool mask)
{
	if (mask)
		wr32(XSCT_IMS, intr_mask);
	else
		wr32(XSCT_IMC, intr_mask);
}
