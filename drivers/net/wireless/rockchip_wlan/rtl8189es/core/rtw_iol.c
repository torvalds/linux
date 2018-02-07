/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *                                        
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include <drv_types.h>

#ifdef CONFIG_IOL
struct xmit_frame	*rtw_IOL_accquire_xmit_frame(ADAPTER *adapter)
{
	struct xmit_frame	*xmit_frame;
	struct xmit_buf	*xmitbuf;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv = &(adapter->xmitpriv);

#if 1
	if ((xmit_frame = rtw_alloc_xmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s rtw_alloc_xmitframe return null\n", __FUNCTION__);
		goto exit;
	}
	
	if ((xmitbuf = rtw_alloc_xmitbuf(pxmitpriv)) == NULL)
	{
		DBG_871X("%s rtw_alloc_xmitbuf return null\n", __FUNCTION__);
		rtw_free_xmitframe(pxmitpriv, xmit_frame);
		xmit_frame=NULL;
		goto exit;
	}
	
	xmit_frame->frame_tag = MGNT_FRAMETAG;
	xmit_frame->pxmitbuf = xmitbuf;
	xmit_frame->buf_addr = xmitbuf->pbuf;
	xmitbuf->priv_data = xmit_frame;

	pattrib = &xmit_frame->attrib;
	update_mgntframe_attrib(adapter, pattrib);
	pattrib->qsel = QSLT_BEACON;//Beacon	
	pattrib->subtype = WIFI_BEACON;	
	pattrib->pktlen = pattrib->last_txcmdsz = 0;

#else
	if ((xmit_frame = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		DBG_871X("%s alloc_mgtxmitframe return null\n", __FUNCTION__);
	}
	else {
		pattrib = &xmit_frame->attrib;
		update_mgntframe_attrib(adapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = pattrib->last_txcmdsz = 0;
	}
#endif

exit:
	return xmit_frame;
}


int rtw_IOL_append_cmds(struct xmit_frame *xmit_frame, u8 *IOL_cmds, u32 cmd_len)
{
	struct pkt_attrib	*pattrib = &xmit_frame->attrib;
	u16 buf_offset;
	u32 ori_len;

	buf_offset = TXDESC_OFFSET;
	ori_len = buf_offset+pattrib->pktlen;

	//check if the io_buf can accommodate new cmds
	if(ori_len + cmd_len + 8 > MAX_XMITBUF_SZ) {
		DBG_871X("%s %u is large than MAX_XMITBUF_SZ:%u, can't accommodate new cmds\n", __FUNCTION__
			, ori_len + cmd_len + 8, MAX_XMITBUF_SZ);
		return _FAIL;
	}

	_rtw_memcpy(xmit_frame->buf_addr + buf_offset + pattrib->pktlen, IOL_cmds, cmd_len);
	pattrib->pktlen += cmd_len;
	pattrib->last_txcmdsz += cmd_len;

	//DBG_871X("%s ori:%u + cmd_len:%u = %u\n", __FUNCTION__, ori_len, cmd_len, buf_offset+pattrib->pktlen);
	
	return _SUCCESS;
}

bool rtw_IOL_applied(ADAPTER *adapter)
{	
	if(1 == adapter->registrypriv.fw_iol)
		return _TRUE;

#ifdef CONFIG_USB_HCI
	if((2 == adapter->registrypriv.fw_iol) && (IS_FULL_SPEED_USB(adapter)))
		return _TRUE;
#endif

	return _FALSE;
}

int rtw_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{
	return rtw_hal_iol_cmd(adapter, xmit_frame, max_wating_ms,bndy_cnt);
}

#ifdef CONFIG_IOL_NEW_GENERATION
int rtw_IOL_append_LLT_cmd(struct xmit_frame *xmit_frame, u8 page_boundary)
{
	return _SUCCESS;
}
int _rtw_IOL_append_WB_cmd(struct xmit_frame *xmit_frame, u16 addr, u8 value, u8 mask)
{
	struct ioreg_cfg cmd = {8,IOREG_CMD_WB_REG,0x0, 0x0,0x0};

	//RTW_PUT_LE16((u8*)&cmd.address, addr);	
	//RTW_PUT_LE32((u8*)&cmd.value, (u32)value);	
	cmd.address = cpu_to_le16(addr);	
	cmd.data = cpu_to_le32(value);
	
	if(mask!=0xFF)
	{
		cmd.length = 12;
		//RTW_PUT_LE32((u8*)&cmd.mask, (u32)mask);	
		cmd.mask = cpu_to_le32(mask);
	}	
		
	//DBG_871X("%s addr:0x%04x,value:0x%08x,mask:0x%08x\n", __FUNCTION__, addr,value,mask);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, cmd.length);

}
int _rtw_IOL_append_WW_cmd(struct xmit_frame *xmit_frame, u16 addr, u16 value, u16 mask)
{
	struct ioreg_cfg cmd = {8,IOREG_CMD_WW_REG,0x0, 0x0,0x0};

	//RTW_PUT_LE16((u8*)&cmd.address, addr);	
	//RTW_PUT_LE32((u8*)&cmd.value, (u32)value);	
	cmd.address = cpu_to_le16(addr);	
	cmd.data = cpu_to_le32(value);
	
	if(mask!=0xFFFF)
	{
		cmd.length = 12;
		//RTW_PUT_LE32((u8*)&cmd.mask, (u32)mask);	
		cmd.mask =  cpu_to_le32(mask);
	}	
		
	//DBG_871X("%s addr:0x%04x,value:0x%08x,mask:0x%08x\n", __FUNCTION__, addr,value,mask);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, cmd.length);
	
}
int _rtw_IOL_append_WD_cmd(struct xmit_frame *xmit_frame, u16 addr, u32 value, u32 mask)
{
	struct ioreg_cfg cmd = {8,IOREG_CMD_WD_REG,0x0, 0x0,0x0};

	//RTW_PUT_LE16((u8*)&cmd.address, addr);	
	//RTW_PUT_LE32((u8*)&cmd.value, (u32)value);	
	cmd.address = cpu_to_le16(addr);	
	cmd.data = cpu_to_le32(value);
	
	if(mask!=0xFFFFFFFF)
	{
		cmd.length = 12;
		//RTW_PUT_LE32((u8*)&cmd.mask, (u32)mask);	
		cmd.mask =  cpu_to_le32(mask);
	}	
		
	//DBG_871X("%s addr:0x%04x,value:0x%08x,mask:0x%08x\n", __FU2NCTION__, addr,value,mask);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, cmd.length);
	
}

int _rtw_IOL_append_WRF_cmd(struct xmit_frame *xmit_frame, u8 rf_path, u16 addr, u32 value, u32 mask)
{
	struct ioreg_cfg cmd = {8,IOREG_CMD_W_RF,0x0, 0x0,0x0};

	//RTW_PUT_LE16((u8*)&cmd.address, addr);	
	//RTW_PUT_LE32((u8*)&cmd.value, (u32)value);	
	cmd.address = (rf_path<<8) |((addr) &0xFF);	
	cmd.data = cpu_to_le32(value);
	
	if(mask!=0x000FFFFF)
	{
		cmd.length = 12;
		//RTW_PUT_LE32((u8*)&cmd.mask, (u32)mask);	
		cmd.mask =  cpu_to_le32(mask);
	}	
		
	//DBG_871X("%s rf_path:0x%02x addr:0x%04x,value:0x%08x,mask:0x%08x\n", __FU2NCTION__,rf_path, addr,value,mask);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, cmd.length);
	
}



int rtw_IOL_append_DELAY_US_cmd(struct xmit_frame *xmit_frame, u16 us)
{
	struct ioreg_cfg cmd = {4,IOREG_CMD_DELAY_US,0x0, 0x0,0x0};
	//RTW_PUT_LE16((u8*)&cmd.address, us);	
	cmd.address = cpu_to_le16(us);	

	//DBG_871X("%s %u\n", __FUNCTION__, us);
	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 4);
}

int rtw_IOL_append_DELAY_MS_cmd(struct xmit_frame *xmit_frame, u16 ms)
{
	struct ioreg_cfg cmd = {4,IOREG_CMD_DELAY_US,0x0, 0x0,0x0};

	//RTW_PUT_LE16((u8*)&cmd.address, ms);	
	cmd.address = cpu_to_le16(ms);	

	//DBG_871X("%s %u\n", __FUNCTION__, ms);
	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 4);
}
int rtw_IOL_append_END_cmd(struct xmit_frame *xmit_frame)
{	
	struct ioreg_cfg cmd = {4,IOREG_CMD_END,0xFFFF, 0xFF,0x0};
	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 4);

}

u8 rtw_IOL_cmd_boundary_handle(struct xmit_frame *pxmit_frame)
{	
	u8 is_cmd_bndy = _FALSE;
	if(((pxmit_frame->attrib.pktlen+32)%256) + 8 >= 256){
		rtw_IOL_append_END_cmd(pxmit_frame);
		pxmit_frame->attrib.pktlen = ((((pxmit_frame->attrib.pktlen+32)/256)+1)*256 );
		
		//printk("==> %s, pktlen(%d)\n",__FUNCTION__,pxmit_frame->attrib.pktlen);
		pxmit_frame->attrib.last_txcmdsz = pxmit_frame->attrib.pktlen;
		is_cmd_bndy = _TRUE;	
	}
	return is_cmd_bndy;
}

void rtw_IOL_cmd_buf_dump(ADAPTER *Adapter,int buf_len,u8 *pbuf)
{
	int i;
	int j=1;
	
	printk("###### %s ######\n",__FUNCTION__);
	for(i=0;i< buf_len;i++){
		printk("%02x-",*(pbuf+i));
			
		if(j%32 ==0) printk("\n");
		j++;
	}
	printk("\n");
	printk("============= ioreg_cmd len = %d =============== \n",buf_len);	
}


#else //CONFIG_IOL_NEW_GENERATION
int rtw_IOL_append_LLT_cmd(struct xmit_frame *xmit_frame, u8 page_boundary)
{	
	IOL_CMD cmd = {0x0, IOL_CMD_LLT, 0x0, 0x0};
	
	RTW_PUT_BE32((u8*)&cmd.value, (u32)page_boundary);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

int _rtw_IOL_append_WB_cmd(struct xmit_frame *xmit_frame, u16 addr, u8 value)
{
	IOL_CMD cmd = {0x0, IOL_CMD_WB_REG, 0x0, 0x0};
	
	RTW_PUT_BE16((u8*)&cmd.address, (u16)addr);
	RTW_PUT_BE32((u8*)&cmd.value, (u32)value);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

int _rtw_IOL_append_WW_cmd(struct xmit_frame *xmit_frame, u16 addr, u16 value)
{
	IOL_CMD cmd = {0x0, IOL_CMD_WW_REG, 0x0, 0x0};
	
	RTW_PUT_BE16((u8*)&cmd.address, (u16)addr);
	RTW_PUT_BE32((u8*)&cmd.value, (u32)value);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

int _rtw_IOL_append_WD_cmd(struct xmit_frame *xmit_frame, u16 addr, u32 value)
{
	IOL_CMD cmd = {0x0, IOL_CMD_WD_REG, 0x0, 0x0};
	u8* pos = (u8 *)&cmd;
	
	RTW_PUT_BE16((u8*)&cmd.address, (u16)addr);
	RTW_PUT_BE32((u8*)&cmd.value, (u32)value);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

#ifdef DBG_IO
int dbg_rtw_IOL_append_WB_cmd(struct xmit_frame *xmit_frame, u16 addr, u8 value, const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 1))
		DBG_871X("DBG_IO %s:%d IOL_WB(0x%04x, 0x%02x)\n", caller, line, addr, value);

	return _rtw_IOL_append_WB_cmd(xmit_frame, addr, value);
}

int dbg_rtw_IOL_append_WW_cmd(struct xmit_frame *xmit_frame, u16 addr, u16 value, const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 2))
		DBG_871X("DBG_IO %s:%d IOL_WW(0x%04x, 0x%04x)\n", caller, line, addr, value);

	return _rtw_IOL_append_WW_cmd(xmit_frame, addr, value);
}

int dbg_rtw_IOL_append_WD_cmd(struct xmit_frame *xmit_frame, u16 addr, u32 value, const char *caller, const int line)
{
	if (match_write_sniff_ranges(addr, 4))
		DBG_871X("DBG_IO %s:%d IOL_WD(0x%04x, 0x%08x)\n", caller, line, addr, value);

	return _rtw_IOL_append_WD_cmd(xmit_frame, addr, value);
}
#endif

int rtw_IOL_append_DELAY_US_cmd(struct xmit_frame *xmit_frame, u16 us)
{
	IOL_CMD cmd = {0x0, IOL_CMD_DELAY_US, 0x0, 0x0};
	
	RTW_PUT_BE32((u8*)&cmd.value, (u32)us);

	//DBG_871X("%s %u\n", __FUNCTION__, us);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

int rtw_IOL_append_DELAY_MS_cmd(struct xmit_frame *xmit_frame, u16 ms)
{
	IOL_CMD cmd = {0x0, IOL_CMD_DELAY_MS, 0x0, 0x0};
	
	RTW_PUT_BE32((u8*)&cmd.value, (u32)ms);

	//DBG_871X("%s %u\n", __FUNCTION__, ms);

	return rtw_IOL_append_cmds(xmit_frame, (u8*)&cmd, 8);
}

int rtw_IOL_append_END_cmd(struct xmit_frame *xmit_frame)
{	
	IOL_CMD end_cmd = {0x0, IOL_CMD_END, 0x0, 0x0};


	return rtw_IOL_append_cmds(xmit_frame, (u8*)&end_cmd, 8);

}

int rtw_IOL_exec_cmd_array_sync(PADAPTER adapter, u8 *IOL_cmds, u32 cmd_num, u32 max_wating_ms)
{
	struct xmit_frame	*xmit_frame;

	if((xmit_frame=rtw_IOL_accquire_xmit_frame(adapter)) == NULL)
		return _FAIL;

	if(rtw_IOL_append_cmds(xmit_frame, IOL_cmds, cmd_num<<3) == _FAIL)
		return _FAIL;

	return rtw_IOL_exec_cmds_sync(adapter, xmit_frame, max_wating_ms,0);
}

int rtw_IOL_exec_empty_cmds_sync(ADAPTER *adapter, u32 max_wating_ms)
{
	IOL_CMD end_cmd = {0x0, IOL_CMD_END, 0x0, 0x0};
	return rtw_IOL_exec_cmd_array_sync(adapter, (u8*)&end_cmd, 1, max_wating_ms);
}
#endif //CONFIG_IOL_NEW_GENERATION




#endif //CONFIG_IOL

