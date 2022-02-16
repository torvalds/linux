// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/rtw_iol.h"

struct xmit_frame	*rtw_IOL_accquire_xmit_frame(struct adapter  *adapter)
{
	struct xmit_frame	*xmit_frame;
	struct xmit_buf	*xmitbuf;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv = &adapter->xmitpriv;

	xmit_frame = rtw_alloc_xmitframe(pxmitpriv);
	if (!xmit_frame) {
		return NULL;
	}

	xmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (!xmitbuf) {
		rtw_free_xmitframe(pxmitpriv, xmit_frame);
		return NULL;
	}

	xmit_frame->frame_tag = MGNT_FRAMETAG;
	xmit_frame->pxmitbuf = xmitbuf;
	xmit_frame->buf_addr = xmitbuf->pbuf;
	xmitbuf->priv_data = xmit_frame;

	pattrib = &xmit_frame->attrib;
	update_mgntframe_attrib(adapter, pattrib);
	pattrib->qsel = 0x10;/* Beacon */
	pattrib->subtype = WIFI_BEACON;
	pattrib->pktlen = 0;
	pattrib->last_txcmdsz = 0;

	return xmit_frame;
}

int rtw_IOL_append_cmds(struct xmit_frame *xmit_frame, u8 *IOL_cmds, u32 cmd_len)
{
	struct pkt_attrib	*pattrib = &xmit_frame->attrib;
	u16 buf_offset;
	u32 ori_len;

	buf_offset = TXDESC_OFFSET;
	ori_len = buf_offset + pattrib->pktlen;

	/* check if the io_buf can accommodate new cmds */
	if (ori_len + cmd_len + 8 > MAX_XMITBUF_SZ) {
		return _FAIL;
	}

	memcpy(xmit_frame->buf_addr + buf_offset + pattrib->pktlen, IOL_cmds, cmd_len);
	pattrib->pktlen += cmd_len;
	pattrib->last_txcmdsz += cmd_len;

	return _SUCCESS;
}

bool rtw_IOL_applied(struct adapter  *adapter)
{
	if (1 == adapter->registrypriv.fw_iol)
		return true;

	if ((2 == adapter->registrypriv.fw_iol) &&
	    (adapter_to_dvobj(adapter)->pusbdev->speed != USB_SPEED_HIGH))
		return true;

	return false;
}

int _rtw_IOL_append_WB_cmd(struct xmit_frame *xmit_frame, u16 addr, u8 value, u8 mask)
{
	struct ioreg_cfg cmd = {8, IOREG_CMD_WB_REG, 0x0, 0x0, 0x0};

	cmd.address = cpu_to_le16(addr);
	cmd.data = cpu_to_le32(value);

	if (mask != 0xFF) {
		cmd.length = 12;
		cmd.mask = cpu_to_le32(mask);
	}
	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, cmd.length);
}

int _rtw_IOL_append_WW_cmd(struct xmit_frame *xmit_frame, u16 addr, u16 value, u16 mask)
{
	struct ioreg_cfg cmd = {8, IOREG_CMD_WW_REG, 0x0, 0x0, 0x0};

	cmd.address = cpu_to_le16(addr);
	cmd.data = cpu_to_le32(value);

	if (mask != 0xFFFF) {
		cmd.length = 12;
		cmd.mask =  cpu_to_le32(mask);
	}
	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, cmd.length);
}

int _rtw_IOL_append_WD_cmd(struct xmit_frame *xmit_frame, u16 addr, u32 value, u32 mask)
{
	struct ioreg_cfg cmd = {8, IOREG_CMD_WD_REG, 0x0, 0x0, 0x0};

	cmd.address = cpu_to_le16(addr);
	cmd.data = cpu_to_le32(value);

	if (mask != 0xFFFFFFFF) {
		cmd.length = 12;
		cmd.mask =  cpu_to_le32(mask);
	}
	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, cmd.length);
}

int _rtw_IOL_append_WRF_cmd(struct xmit_frame *xmit_frame, u8 rf_path, u16 addr, u32 value, u32 mask)
{
	struct ioreg_cfg cmd = {8, IOREG_CMD_W_RF, 0x0, 0x0, 0x0};

	cmd.address = cpu_to_le16((rf_path << 8) | ((addr) & 0xFF));
	cmd.data = cpu_to_le32(value);

	if (mask != 0x000FFFFF) {
		cmd.length = 12;
		cmd.mask =  cpu_to_le32(mask);
	}
	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, cmd.length);
}

int rtw_IOL_append_DELAY_US_cmd(struct xmit_frame *xmit_frame, u16 us)
{
	struct ioreg_cfg cmd = {4, IOREG_CMD_DELAY_US, 0x0, 0x0, 0x0};
	cmd.address = cpu_to_le16(us);

	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, 4);
}

int rtw_IOL_append_DELAY_MS_cmd(struct xmit_frame *xmit_frame, u16 ms)
{
	struct ioreg_cfg cmd = {4, IOREG_CMD_DELAY_US, 0x0, 0x0, 0x0};

	cmd.address = cpu_to_le16(ms);
	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, 4);
}

int rtw_IOL_append_END_cmd(struct xmit_frame *xmit_frame)
{
	struct ioreg_cfg cmd = {4, IOREG_CMD_END, cpu_to_le16(0xFFFF), cpu_to_le32(0xFF), 0x0};

	return rtw_IOL_append_cmds(xmit_frame, (u8 *)&cmd, 4);
}

u8 rtw_IOL_cmd_boundary_handle(struct xmit_frame *pxmit_frame)
{
	u8 is_cmd_bndy = false;
	if (((pxmit_frame->attrib.pktlen + 32) % 256) + 8 >= 256) {
		rtw_IOL_append_END_cmd(pxmit_frame);
		pxmit_frame->attrib.pktlen = ((((pxmit_frame->attrib.pktlen + 32) / 256) + 1) * 256);

		pxmit_frame->attrib.last_txcmdsz = pxmit_frame->attrib.pktlen;
		is_cmd_bndy = true;
	}
	return is_cmd_bndy;
}
