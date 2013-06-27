/*
 * drivers/usb/sunxi_usb/usbc/usbc.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * daniel <daniel@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


#include  "usbc_i.h"


static __u32 usbc_base_address[USBC_MAX_CTL_NUM];       /* usb base address */
static __usbc_otg_t usbc_otg_array[USBC_MAX_OPEN_NUM];  /* usbc 内部使用, 用来管理USB端口 */
static __fifo_info_t usbc_info_g;

/*
***********************************************************************************
*                     USBC_GetVbusStatus
*
* Description:
*    获得当前vbus的状态
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*    返回当前vbus的状态
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_GetVbusStatus(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u8 reg_val = 0;

	if(usbc_otg == NULL){
		return 0;
	}

	reg_val = USBC_Readb(USBC_REG_DEVCTL(usbc_otg->base_addr));
	reg_val = reg_val >> USBC_BP_DEVCTL_VBUS;
    switch(reg_val & 0x03){
		case 0x00:
			return USBC_VBUS_STATUS_BELOW_SESSIONEND;
		//break;

		case 0x01:
			return USBC_VBUS_STATUS_ABOVE_SESSIONEND_BELOW_AVALID;
		//break;

		case 0x02:
			return USBC_VBUS_STATUS_ABOVE_AVALID_BELOW_VBUSVALID;
		//break;

		case 0x03:
			return USBC_VBUS_STATUS_ABOVE_VBUSVALID;
		//break;

		default:
			return USBC_VBUS_STATUS_BELOW_SESSIONEND;
	}
}

/*
***********************************************************************************
*                     USBC_OTG_SelectMode
*
* Description:
*    选择设备的类型。当前设备是作device, 还是作host
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_OTG_SelectMode(__hdle hUSB, __u32 mode)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	if(mode == USBC_OTG_HOST){

	}else{

	}
}

/*
***********************************************************************************
*                     USBC_ReadLenFromFifo
*
* Description:
*    本次fifo可以读到的数据长度
*
* Arguments:
*    hUSB     :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type  :  input.  ep的类型, rx 或 tx。
* Returns:
*    返回本次fifo可以读到的数据长度
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_ReadLenFromFifo(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

    switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return USBC_Readw(USBC_REG_COUNT0(usbc_otg->base_addr));
		//break;

		case USBC_EP_TYPE_TX:
			return 0;
		//break;

		case USBC_EP_TYPE_RX:
			return USBC_Readw(USBC_REG_RXCOUNT(usbc_otg->base_addr));
		//break;

		default:
			return 0;
	}
}

/*
***********************************************************************************
*                     USBC_WritePacket
*
* Description:
*    往fifo里面写数据包
*
* Arguments:
*    hUSB    :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    fifo    :  input.  fifo地址.
*    cnt     :  input.  写数据长度
*    buff    :  input.  存放要写的数据
*
* Returns:
*    返回成功写入的长度
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_WritePacket(__hdle hUSB, __u32 fifo, __u32 cnt, void *buff)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 len = 0;
	__u32 i32 = 0;
	__u32 i8  = 0;
	__u8  *buf8  = NULL;
	__u32 *buf32 = NULL;

	if(usbc_otg == NULL || buff == NULL){
		return 0;
	}

    //--<1>--调整数据
	buf32 = buff;
	len   = cnt;

	i32 = len >> 2;
	i8  = len & 0x03;

    //--<2>--处理4字节的部分
	while (i32--){
		USBC_Writel(*buf32++, fifo);
	}

    //--<3>--处理非4字节的部分
	buf8 = (__u8 *)buf32;
	while (i8--){
		USBC_Writeb(*buf8++, fifo);
	}

	return len;
}

/*
***********************************************************************************
*                     USBC_ReadPacket
*
* Description:
*    从fifo里面读数据
*
* Arguments:
*    hUSB    :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    fifo    :  input.  fifo地址.
*    cnt     :  input.  写数据长度
*    buff    :  input.  存放要读的数据
*
* Returns:
*    返回成功读的长度
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_ReadPacket(__hdle hUSB, __u32 fifo, __u32 cnt, void *buff)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 len = 0;
	__u32 i32 = 0;
	__u32 i8  = 0;
	__u8  *buf8  = NULL;
	__u32 *buf32 = NULL;

    if(usbc_otg == NULL || buff == NULL){
		return 0;
	}

	//--<1>--调整数据
	buf32 = buff;
	len   = cnt;

    i32 = len >> 2;
	i8  = len & 0x03;

	//--<2>--处理4字节的部分
	while (i32--){
        *buf32++ = USBC_Readl(fifo);
    }

	//--<3>--处理非4字节的部分
	buf8 = (__u8 *)buf32;
	while (i8--){
        *buf8++ = USBC_Readb(fifo);
    }

	return len;
}

/* 映射SRAM D给usb fifo使用 */
void USBC_ConfigFIFO_Base(__hdle hUSB, __u32 sram_base, __u32 fifo_mode)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __fifo_info_t *usbc_info = &usbc_info_g;
	__u32 reg_value = 0;

	if(usbc_otg == NULL){
		return ;
	}

	if(usbc_otg->port_num == 0){
		reg_value = USBC_Readl(sram_base + 0x04);
		reg_value &= ~(0x03 << 0);
		reg_value |= (1 << 0);
		USBC_Writel(reg_value, (sram_base + 0x04));

		usbc_info->port0_fifo_addr = 0x00;
		usbc_info->port0_fifo_size = (8 * 1024);	//8k
    }

	return ;
}

/* 获得port fifo的起始地址 */
__u32 USBC_GetPortFifoStartAddr(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	if(usbc_otg->port_num == 0){
		return usbc_info_g.port0_fifo_addr;
	}else if(usbc_otg->port_num == 1){
	    return usbc_info_g.port1_fifo_addr;
	}else {
	    return usbc_info_g.port2_fifo_addr;
	}
}

/* 获得port fifo的大小 */
__u32 USBC_GetPortFifoSize(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	if(usbc_otg->port_num == 0){
		return usbc_info_g.port0_fifo_size;
	}else{
	    return usbc_info_g.port1_fifo_size;
	}
}


/*
***********************************************************************************
*                     USBC_SelectFIFO
*
* Description:
*    选择设备的类型。当前设备是作device, 还是作host
*
* Arguments:
*    hUSB     :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_index :  input.  ep号。用来选择相应的fifo
*
* Returns:
*    返回选中的fifo
*
* note:
*    无
*
***********************************************************************************
*/
/*
__u32 USBC_SelectFIFO(__hdle hUSB, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 fifo = 0;

	if(usbc_otg == NULL){
		return 0;
	}

    switch(ep_index){
		case 0:
			fifo = USBC_REG_EPFIFO0(usbc_otg->base_addr);
		break;

		case 1:
			fifo = USBC_REG_EPFIFO1(usbc_otg->base_addr);
		break;

		case 2:
			fifo = USBC_REG_EPFIFO2(usbc_otg->base_addr);
		break;

		case 3:
			fifo = USBC_REG_EPFIFO3(usbc_otg->base_addr);
		break;

		case 4:
			fifo = USBC_REG_EPFIFO4(usbc_otg->base_addr);
		break;

		case 5:
			fifo = USBC_REG_EPFIFO5(usbc_otg->base_addr);
		break;

		default:
			fifo = 0;
	}

	return fifo;
}
*/

__u32 USBC_SelectFIFO(__hdle hUSB, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	return USBC_REG_EPFIFOx(usbc_otg->base_addr, ep_index);
}

static void __USBC_ConfigFifo_TxEp_Default(__u32 usbc_base_addr)
{
	USBC_Writew(0x00, USBC_REG_TXFIFOAD(usbc_base_addr));
	USBC_Writeb(0x00, USBC_REG_TXFIFOSZ(usbc_base_addr));
}

/*
***********************************************************************************
*                     USBC_ConfigFifo_TxEp
*
* Description:
*    配置tx ep 的fifo地址和大小。
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    is_double_fifo :  input.  Whether to use hardware dual fifo
*    fifo_size      :  input.  fifo大小 = 2的fifo_size次方
*    fifo_addr      :  input.  fifo的起始地址 = fifo_addr * 8
*
* Returns:
*    返回成功读的长度
*
* note:
*    无
*
***********************************************************************************
*/
void __USBC_ConfigFifo_TxEp(__u32 usbc_base_addr, __u32 is_double_fifo, __u32 fifo_size, __u32 fifo_addr)
{
    __u32 temp = 0;
    __u32 size = 0;   //fifo_size = (size + 3)的2次方
    __u32 addr = 0;   //fifo_addr = addr * 8

	//--<1>--换算sz, 不满512，以512对齐
	temp = fifo_size + 511;
	temp &= ~511;  //把511后面的清零
	temp >>= 3;
	temp >>= 1;
	while(temp){
		size++;
		temp >>= 1;
	}

	//--<2>--换算addr
	addr = fifo_addr >> 3;

	//--<3>--config fifo addr
	USBC_Writew(addr, USBC_REG_TXFIFOAD(usbc_base_addr));

	//--<4>--config fifo size
	USBC_Writeb((size & 0x0f), USBC_REG_TXFIFOSZ(usbc_base_addr));
	if(is_double_fifo){
		USBC_REG_set_bit_b(USBC_BP_TXFIFOSZ_DPB, USBC_REG_TXFIFOSZ(usbc_base_addr));
	}
}

void __USBC_ConfigFifo_RxEp_Default(__u32 usbc_base_addr)
{
	USBC_Writew(0x00, USBC_REG_RXFIFOAD(usbc_base_addr));
	USBC_Writeb(0x00, USBC_REG_RXFIFOSZ(usbc_base_addr));
}

/*
***********************************************************************************
*                     USBC_ConfigFifo_RxEp
*
* Description:
*    配置tx ep 的fifo地址和大小。
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    is_double_fifo :  input.  是否使用硬件双fifo
*    fifo_size      :  input.  fifo大小 = 2的fifo_size次方
*    fifo_addr      :  input.  fifo的起始地址 = fifo_addr * 8
*
* Returns:
*    返回成功读的长度
*
* note:
*    无
*
***********************************************************************************
*/
void __USBC_ConfigFifo_RxEp(__u32 usbc_base_addr, __u32 is_double_fifo, __u32 fifo_size, __u32 fifo_addr)
{
    __u32 temp = 0;
    __u32 size = 0;   //fifo_size = (size + 3)的2次方
    __u32 addr = 0;   //fifo_addr = addr * 8

	//--<1>--计算sz, 不满512，以512对齐
	temp = fifo_size + 511;
	temp &= ~511;  //把511后面的清零
	temp >>= 3;
	temp >>= 1;
	while(temp){
		size++;
		temp >>= 1;
	}

	//--<2>--换算addr
	addr = fifo_addr >> 3;

	//--<3>--config fifo addr
	USBC_Writew(addr, USBC_REG_RXFIFOAD(usbc_base_addr));

	//--<2>--config fifo size
	USBC_Writeb((size & 0x0f), USBC_REG_RXFIFOSZ(usbc_base_addr));
	if(is_double_fifo){
		USBC_REG_set_bit_b(USBC_BP_RXFIFOSZ_DPB, USBC_REG_RXFIFOSZ(usbc_base_addr));
	}
}

/*
***********************************************************************************
*                     USBC_ConfigFifo_Default
*
* Description:
*    配置ep 的fifo地址和大小。
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*	 ep_type		:  input.  ep的类型
*
* Returns:
*    返回成功读的长度
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_ConfigFifo_Default(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			//not support
		break;

		case USBC_EP_TYPE_TX:
			__USBC_ConfigFifo_TxEp_Default(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
			__USBC_ConfigFifo_RxEp_Default(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_ConfigFifo
*
* Description:
*    配置ep 的fifo地址和大小。
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*	 ep_type		:  input.  ep的类型
*    is_double_fifo :  input.  是否使用硬件双fifo
*    fifo_size      :  input.  fifo大小 = 2的fifo_size次方
*    fifo_addr      :  input.  fifo的起始地址 = fifo_addr * 8
*
* Returns:
*    返回成功读的长度
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_ConfigFifo(__hdle hUSB, __u32 ep_type, __u32 is_double_fifo, __u32 fifo_size, __u32 fifo_addr)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			//not support
		break;

		case USBC_EP_TYPE_TX:
			__USBC_ConfigFifo_TxEp(usbc_otg->base_addr, is_double_fifo, fifo_size, fifo_addr);
		break;

		case USBC_EP_TYPE_RX:
			__USBC_ConfigFifo_RxEp(usbc_otg->base_addr, is_double_fifo, fifo_size, fifo_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_GetLastFrameNumber
*
* Description:
*    获得最后一帧的帧号
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_GetLastFrameNumber(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

    return USBC_Readl(USBC_REG_FRNUM(usbc_otg->base_addr));
}

/*
***********************************************************************************
*                     USBC_GetStatus_Dp
*
* Description:
*    获得dp的状态
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/

__u32 USBC_GetStatus_Dp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 temp = 0;

	if(usbc_otg == NULL){
		return 0;
	}

	temp = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	temp = (temp >> USBC_BP_ISCR_EXT_DP_STATUS) & 0x01;

    return temp;
}



/*
***********************************************************************************
*                     USBC_GetStatus_Dm
*
* Description:
*    获得dm的状态
*
* Arguments:
*    hUSB :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_GetStatus_Dm(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 temp = 0;

	if(usbc_otg == NULL){
		return 0;
	}

	temp = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	temp = (temp >> USBC_BP_ISCR_EXT_DM_STATUS) & 0x01;

    return temp;
}


/*
***********************************************************************************
*                     USBC_GetStatus_Dp
*
* Description:
*    获得dp的状态
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/

__u32 USBC_GetStatus_DpDm(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 temp = 0;
	__u32 dp = 0;
	__u32 dm = 0;


	if(usbc_otg == NULL){
		return 0;
	}

	temp = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	dp = (temp >> USBC_BP_ISCR_EXT_DP_STATUS) & 0x01;
	dm = (temp >> USBC_BP_ISCR_EXT_DM_STATUS) & 0x01;
	return ((dp << 1) | dm);

}

/*
***********************************************************************************
*                     USBC_GetOtgMode_Form_ID
*
* Description:
*    从vendor0 的 id 获得当前OTG的模式
*
* Arguments:
*    hUSB :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*    USBC_OTG_DEVICE / USBC_OTG_HOST
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_GetOtgMode_Form_ID(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 mode = 0;

	if(usbc_otg == NULL){
		return USBC_OTG_DEVICE;
	}

    mode = USBC_REG_test_bit_l(USBC_BP_ISCR_MERGED_ID_STATUS, USBC_REG_ISCR(usbc_otg->base_addr));
    if(mode){
		return USBC_OTG_DEVICE;
	}else{
	    return USBC_OTG_HOST;
	}
}

/*
***********************************************************************************
*                     USBC_GetOtgMode_Form_BDevice
*
* Description:
*    从 OTG Device 的 B-Device 获得当前OTG的模式
*
* Arguments:
*    hUSB :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*    USBC_OTG_DEVICE / USBC_OTG_HOST
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_GetOtgMode_Form_BDevice(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 mode = 0;

	if(usbc_otg == NULL){
		return USBC_OTG_DEVICE;
	}

    mode = USBC_REG_test_bit_b(USBC_BP_DEVCTL_B_DEVICE, USBC_REG_DEVCTL(usbc_otg->base_addr));
    if(mode){
		return USBC_OTG_DEVICE;
	}else{
	    return USBC_OTG_HOST;
	}
}

/*
***********************************************************************************
*                     USBC_SelectBus
*
* Description:
*    选择数据传输的总线方式
*
* Arguments:
*    hUSB     :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    io_type  :  input.  总线方式, pio还是dma.
*    ep_type  :  input.  ep的类型, rx 或 tx。
*    ep_index :  input.  ep号
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_SelectBus(__hdle hUSB, __u32 io_type, __u32 ep_type, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

	if(usbc_otg == NULL){
		return ;
	}

    reg_val = USBC_Readb(USBC_REG_VEND0(usbc_otg->base_addr));
    if(io_type == USBC_IO_TYPE_DMA){
		if(ep_type == USBC_EP_TYPE_TX){
			reg_val |= ((ep_index - 0x01) << 1) << USBC_BP_VEND0_DRQ_SEL;  //drq_sel
			reg_val |= 0x1<<USBC_BP_VEND0_BUS_SEL;   //io_dma
		}else{
		    reg_val |= ((ep_index << 1) - 0x01) << USBC_BP_VEND0_DRQ_SEL;
			reg_val |= 0x1<<USBC_BP_VEND0_BUS_SEL;
		}
	}else{
	    //reg_val &= ~(0x1 << USBC_BP_VEND0_DRQ_SEL);  //清除drq_sel, 选择pio
	    reg_val &= 0x00;  //清除drq_sel, 选择pio
	}

	USBC_Writeb(reg_val, USBC_REG_VEND0(usbc_otg->base_addr));
}

/* 获得tx ep中断标志位 */
static __u32 __USBC_INT_TxPending(__u32 usbc_base_addr)
{
    return (USBC_Readw(USBC_REG_INTTx(usbc_base_addr)));
}

/* 清除tx ep中断标志位 */
static void __USBC_INT_ClearTxPending(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_Writew((1 << ep_index), USBC_REG_INTTx(usbc_base_addr));
}

/* 清除所有tx ep中断标志位 */
static void __USBC_INT_ClearTxPendingAll(__u32 usbc_base_addr)
{
    USBC_Writew(0xffff, USBC_REG_INTTx(usbc_base_addr));
}

/* 获得rx ep中断标志位 */
static __u32 __USBC_INT_RxPending(__u32 usbc_base_addr)
{
    return (USBC_Readw(USBC_REG_INTRx(usbc_base_addr)));
}

/* 清除rx ep中断标志位 */
static void __USBC_INT_ClearRxPending(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_Writew((1 << ep_index), USBC_REG_INTRx(usbc_base_addr));
}

/* 清除rx ep中断标志位 */
static void __USBC_INT_ClearRxPendingAll(__u32 usbc_base_addr)
{
    USBC_Writew(0xffff, USBC_REG_INTRx(usbc_base_addr));
}

/* 开某一个tx ep的中断 */
static void __USBC_INT_EnableTxEp(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_REG_set_bit_w(ep_index, USBC_REG_INTTxE(usbc_base_addr));
}

/* 开某一个rx ep的中断 */
static void __USBC_INT_EnableRxEp(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_REG_set_bit_w(ep_index, USBC_REG_INTRxE(usbc_base_addr));
}

/* 关某一个tx ep的中断 */
static void __USBC_INT_DisableTxEp(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_REG_clear_bit_w(ep_index, USBC_REG_INTTxE(usbc_base_addr));
}

/* 关某一个rx ep的中断 */
static void __USBC_INT_DisableRxEp(__u32 usbc_base_addr, __u8 ep_index)
{
    USBC_REG_clear_bit_w(ep_index, USBC_REG_INTRxE(usbc_base_addr));
}

/* 关所有的tx ep中断 */
static void __USBC_INT_DisableTxAll(__u32 usbc_base_addr)
{
    USBC_Writew(0, USBC_REG_INTTxE(usbc_base_addr));
}

/* 关所有的rx ep中断 */
static void __USBC_INT_DisableRxAll(__u32 usbc_base_addr)
{
    USBC_Writew(0, USBC_REG_INTRxE(usbc_base_addr));
}

/* 获得ep中断标志位 */
__u32 USBC_INT_EpPending(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
		case USBC_EP_TYPE_TX:
		    return __USBC_INT_TxPending(usbc_otg->base_addr);

		case USBC_EP_TYPE_RX:
		    return __USBC_INT_RxPending(usbc_otg->base_addr);

		default:
			return 0;
	}
}

/* 清除ep中断标志位 */
void USBC_INT_ClearEpPending(__hdle hUSB, __u32 ep_type, __u8 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
		case USBC_EP_TYPE_TX:
		    __USBC_INT_ClearTxPending(usbc_otg->base_addr, ep_index);
		break;

		case USBC_EP_TYPE_RX:
		    __USBC_INT_ClearRxPending(usbc_otg->base_addr, ep_index);
		break;

		default:
			break;
	}

	return ;
}

/* 清除ep中断标志位 */
void USBC_INT_ClearEpPendingAll(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
		case USBC_EP_TYPE_TX:
		    __USBC_INT_ClearTxPendingAll(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
		    __USBC_INT_ClearRxPendingAll(usbc_otg->base_addr);
		break;

		default:
			break;
	}

	return ;
}

/* 获得usb misc中断标志位 */
__u32 USBC_INT_MiscPending(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

    return (USBC_Readb(USBC_REG_INTUSB(usbc_otg->base_addr)));
}

/* 清除usb misc中断标志位 */
void USBC_INT_ClearMiscPending(__hdle hUSB, __u32 mask)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_Writeb(mask, USBC_REG_INTUSB(usbc_otg->base_addr));
}

/* 清除所有usb misc中断标志位 */
void USBC_INT_ClearMiscPendingAll(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_Writeb(0xff, USBC_REG_INTUSB(usbc_otg->base_addr));
}

/* 开某一个ep中断 */
void USBC_INT_EnableEp(__hdle hUSB, __u32 ep_type, __u8 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
		    __USBC_INT_EnableTxEp(usbc_otg->base_addr, ep_index);
		break;

		case USBC_EP_TYPE_RX:
		    __USBC_INT_EnableRxEp(usbc_otg->base_addr, ep_index);
		break;

		default:
        break;
	}

	return ;
}

/* 开某一个usb misc中断 */
void USBC_INT_EnableUsbMiscUint(__hdle hUSB, __u32 mask)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

	if(usbc_otg == NULL){
		return ;
	}

	reg_val = USBC_Readb(USBC_REG_INTUSBE(usbc_otg->base_addr));
	reg_val |= mask;
	USBC_Writeb(reg_val, USBC_REG_INTUSBE(usbc_otg->base_addr));
}

/* 关某tx ep的中断 */
void USBC_INT_DisableEp(__hdle hUSB, __u32 ep_type, __u8 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
		    __USBC_INT_DisableTxEp(usbc_otg->base_addr, ep_index);
		break;

		case USBC_EP_TYPE_RX:
		    __USBC_INT_DisableRxEp(usbc_otg->base_addr, ep_index);
		break;

		default:
        break;
	}

	return;
}

/* 关某一个usb misc中断 */
void USBC_INT_DisableUsbMiscUint(__hdle hUSB, __u32 mask)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

	if(usbc_otg == NULL){
		return ;
	}

	reg_val = USBC_Readb(USBC_REG_INTUSBE(usbc_otg->base_addr));
	reg_val &= ~mask;
	USBC_Writeb(reg_val, USBC_REG_INTUSBE(usbc_otg->base_addr));
}

/* 关所有的ep中断 */
void USBC_INT_DisableEpAll(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
		    __USBC_INT_DisableTxAll(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
		    __USBC_INT_DisableRxAll(usbc_otg->base_addr);
		break;

		default:
        break;
	}

	return;
}

/* 关所有的usb misc中断 */
void USBC_INT_DisableUsbMiscAll(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_Writeb(0, USBC_REG_INTUSBE(usbc_otg->base_addr));
}

/* 获得当前活动的ep */
__u32 USBC_GetActiveEp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

    return USBC_Readb(USBC_REG_EPIND(usbc_otg->base_addr));
}

/* 配置当前活动ep */
void USBC_SelectActiveEp(__hdle hUSB, __u8 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	USBC_Writeb(ep_index, USBC_REG_EPIND(usbc_otg->base_addr));
}

/* 加强usb传输信号 */
void USBC_EnhanceSignal(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	return;
}

/* 进入 TestPacket 模式 */
void USBC_EnterMode_TestPacket(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_TMCTL_TEST_PACKET, USBC_REG_TMCTL(usbc_otg->base_addr));
}

/* 进入 Test_K 模式 */
void USBC_EnterMode_Test_K(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_TMCTL_TEST_K, USBC_REG_TMCTL(usbc_otg->base_addr));
}

/* 进入 Test_J 模式 */
void USBC_EnterMode_Test_J(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_TMCTL_TEST_J, USBC_REG_TMCTL(usbc_otg->base_addr));
}

/* 进入 Test_SE0_NAK 模式 */
void USBC_EnterMode_Test_SE0_NAK(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_TMCTL_TEST_SE0_NAK, USBC_REG_TMCTL(usbc_otg->base_addr));
}

/* 清除所有测试模式 */
void USBC_EnterMode_Idle(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_clear_bit_b(USBC_BP_TMCTL_TEST_PACKET, USBC_REG_TMCTL(usbc_otg->base_addr));
	USBC_REG_clear_bit_b(USBC_BP_TMCTL_TEST_K, USBC_REG_TMCTL(usbc_otg->base_addr));
	USBC_REG_clear_bit_b(USBC_BP_TMCTL_TEST_J, USBC_REG_TMCTL(usbc_otg->base_addr));
	USBC_REG_clear_bit_b(USBC_BP_TMCTL_TEST_SE0_NAK, USBC_REG_TMCTL(usbc_otg->base_addr));
}

/* vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位 */
static __u32 __USBC_WakeUp_ClearChangeDetect(__u32 reg_val)
{
    __u32 temp = reg_val;

	temp &= ~(1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT);
	temp &= ~(1 << USBC_BP_ISCR_ID_CHANGE_DETECT);
	temp &= ~(1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT);

	return temp;
}

void USBC_SetWakeUp_Default(__hdle hUSB)
{

	return;
}

void USBC_EnableIdPullUp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u32 reg_val = 0;

    //vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	reg_val |= (1 << USBC_BP_ISCR_ID_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

void USBC_DisableIdPullUp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u32 reg_val = 0;

	//vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	reg_val &= ~(1 << USBC_BP_ISCR_ID_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

void USBC_EnableDpDmPullUp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u32 reg_val = 0;

    //vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	reg_val |= (1 << USBC_BP_ISCR_DPDM_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

void USBC_DisableDpDmPullUp(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u32 reg_val = 0;

	//vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
	reg_val &= ~(1 << USBC_BP_ISCR_DPDM_PULLUP_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

static void __USBC_ForceIdDisable(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//vbus, id, dpdm变化位是写1清零, 因此我们在操作其他bit的时候清除这些位
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

static void __USBC_ForceIdToLow(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//先写00，后写10
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val |= (0x02 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

static void __USBC_ForceIdToHigh(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//先写00，后写10
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	//reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val |= (0x03 << USBC_BP_ISCR_FORCE_ID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

/* force id to (id_type) */
void USBC_ForceId(__hdle hUSB, __u32 id_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

    switch(id_type){
		case USBC_ID_TYPE_HOST:
			__USBC_ForceIdToLow(usbc_otg->base_addr);
		break;

		case USBC_ID_TYPE_DEVICE:
			__USBC_ForceIdToHigh(usbc_otg->base_addr);
		break;

		default:
			__USBC_ForceIdDisable(usbc_otg->base_addr);
	}
}

static void __USBC_ForceVbusValidDisable(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//先写00，后写10
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

static void __USBC_ForceVbusValidToLow(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//先写00，后写10
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val |= (0x02 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

static void __USBC_ForceVbusValidToHigh(__u32 usbc_base_addr)
{
	__u32 reg_val = 0;

	//先写00，后写11
	reg_val = USBC_Readl(USBC_REG_ISCR(usbc_base_addr));
	//reg_val &= ~(0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val |= (0x03 << USBC_BP_ISCR_FORCE_VBUS_VALID);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_base_addr));
}

/* force vbus valid to (id_type) */
void USBC_ForceVbusValid(__hdle hUSB, __u32 vbus_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

    switch(vbus_type){
		case USBC_VBUS_TYPE_LOW:
			__USBC_ForceVbusValidToLow(usbc_otg->base_addr);
		break;

		case USBC_VBUS_TYPE_HIGH:
			__USBC_ForceVbusValidToHigh(usbc_otg->base_addr);
		break;

		default:
			__USBC_ForceVbusValidDisable(usbc_otg->base_addr);
	}
}

void USBC_A_valid_InputSelect(__hdle hUSB, __u32 source)
{

    return;
}

void USBC_EnableUsbLineStateBypass(__hdle hUSB)
{

    return;
}

void USBC_DisableUsbLineStateBypass(__hdle hUSB)
{

    return;
}

void USBC_EnableHosc(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val |= 1 << USBC_BP_ISCR_HOSC_EN;
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 禁用Hosc */
void USBC_DisableHosc(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val &= ~(1 << USBC_BP_ISCR_HOSC_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 查询是否产生 vbus 中断 */
__u32 USBC_IsVbusChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;
	__u32 temp = 0;

    //读取变化位的同时, 写1清除该位
    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));

	temp = reg_val & (1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT);

	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
    reg_val |= 1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT;
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));

	return temp;
}

/* 查询是否产生 id 中断 */
__u32 USBC_IsIdChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;
	__u32 temp = 0;

    //读取变化位的同时, 写1清除该位
    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));

	temp = reg_val & (1 << USBC_BP_ISCR_ID_CHANGE_DETECT);

	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
    reg_val |= 1 << USBC_BP_ISCR_ID_CHANGE_DETECT;
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));

	return temp;
}

/* 查询是否产生 dpdm 中断 */
__u32 USBC_IsDpDmChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;
	__u32 temp = 0;

    //读取变化位的同时, 写1清除该位
    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));

	temp = reg_val & (1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT);

	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
    reg_val |= 1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT;
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));

	return temp;
}

/* 禁用 wake 中断 */
void USBC_DisableWakeIrq(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val &= ~(1 << USBC_BP_ISCR_IRQ_ENABLE);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 禁用 vbus 中断 */
void USBC_DisableVbusChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val &= ~(1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 禁用 id 中断 */
void USBC_DisableIdChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val &= ~(1 << USBC_BP_ISCR_ID_CHANGE_DETECT_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 禁用 dpdm 中断 */
void USBC_DisableDpDmChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val &= ~(1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT_EN);
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 使能 wake 中断 */
void USBC_EnableWakeIrq(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val |= 1 << USBC_BP_ISCR_IRQ_ENABLE;
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 使能 vbus 变化中断 */
void USBC_EnableVbusChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val |= 1 << USBC_BP_ISCR_VBUS_CHANGE_DETECT_EN;
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 使能id变化中断 */
void USBC_EnableIdChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val |= 1 << USBC_BP_ISCR_ID_CHANGE_DETECT_EN;
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 使能dmdp变化中断 */
void USBC_EnableDpDmChange(__hdle hUSB)
{
	__usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

    reg_val = USBC_Readl(USBC_REG_ISCR(usbc_otg->base_addr));
    reg_val |= 1 << USBC_BP_ISCR_DPDM_CHANGE_DETECT_EN;
	reg_val = __USBC_WakeUp_ClearChangeDetect(reg_val);
	USBC_Writel(reg_val, USBC_REG_ISCR(usbc_otg->base_addr));
}

/* 测试模式, 获得寄存器的值 */
__u32 USBC_TestMode_ReadReg(__hdle hUSB, __u32 offset, __u32 reg_width)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 reg_val = 0;

	if(usbc_otg == NULL){
		return reg_val;
	}

    if(reg_width == 8){
		reg_val = USBC_Readb(usbc_otg->base_addr + offset);
	}else if(reg_width == 16){
	    reg_val = USBC_Readw(usbc_otg->base_addr + offset);
	}else if(reg_width == 32){
	    reg_val = USBC_Readl(usbc_otg->base_addr + offset);
	}else{
	    reg_val = 0;
	}

	return reg_val;
}

/*
***********************************************************************************
*                     USBC_open_otg
*
* Description:
*    向bsp申请获得端口号为otg_no的tog使用权
*
* Arguments:
*    otg_no  :  input.  需要使用的TOG端口号, 范围为: 0 ~ USBC_MAX_CTL_NUM
*
* Returns:
*    成功, 返回usbc_otg句柄。失败, 返回NULL
*
* note:
*    无
*
***********************************************************************************
*/
#if 0
__hdle USBC_open_otg(__u32 otg_no)
{
    __usbc_otg_t *usbc_otg = usbc_otg_array;
	__u32 i = 0;

    //--<1>--otg_no不能超过所能支持的范围
    if(otg_no >= USBC_MAX_CTL_NUM){
		return 0;
	}

    //--<2>--在管理数组里找一个空位, 最大支持同时打开8次
    for(i = 0; i < USBC_MAX_OPEN_NUM; i++){
		if(usbc_otg[i].used == 0){
			usbc_otg[i].used      = 1;
			usbc_otg[i].no        = i;
			usbc_otg[i].port_num  = otg_no;
			usbc_otg[i].base_addr = usbc_base_address[otg_no];

			return (__hdle)(&(usbc_otg[i]));
		}
	}

    return 0;
}
#else
__hdle USBC_open_otg(__u32 otg_no)
{
    __usbc_otg_t *usbc_otg = usbc_otg_array;

    //--<1>--otg_no不能超过所能支持的范围
    if(otg_no >= USBC_MAX_CTL_NUM){
		return 0;
	}

    //--<2>--在管理数组里找一个空位, 最大支持同时打开8次
	usbc_otg[otg_no].used      = 1;
	usbc_otg[otg_no].no        = otg_no;
	usbc_otg[otg_no].port_num  = otg_no;
	usbc_otg[otg_no].base_addr = usbc_base_address[otg_no];

	return (__hdle)(&(usbc_otg[otg_no]));
}

#endif

/*
***********************************************************************************
*                     USBC_close_otg
*
* Description:
*    释放tog的使用权
*
* Arguments:
*    hUSB  :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*    无
*
***********************************************************************************
*/
__s32  USBC_close_otg(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return -1;
	}

	memset(usbc_otg, 0, sizeof(__usbc_otg_t));

	return 0;
}


/*
***********************************************************************************
*                           USBC_init
*
* Description:
*
*
* Arguments:
*
*
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*
*
***********************************************************************************
*/
__s32 USBC_init(bsp_usbc_t *usbc)
{
 //   __usbc_otg_t *usbc_otg = usbc_otg_array;
    __u32 i = 0;

//    memset(usbc_base_address, 0, sizeof(usbc_base_address));
//    memset(&usbc_info_g, 0, sizeof(__fifo_info_t));
//    memset(usbc_otg, 0, (USBC_MAX_OPEN_NUM * sizeof(__usbc_otg_t)));

    /* 保存 driver 传进来的 usb 控制器的基址 */
/*
    for(i = 0; i < USBC_MAX_CTL_NUM; i++){
        __u32 port_num = 0;

        port_num = usbc->usbc_info[i].num;
        usbc_base_address[i] = usbc->usbc_info[i].base;
    }
*/
    for(i = 0; i < USBC_MAX_CTL_NUM; i++){
        __u32 port_num = 0;

		if(usbc->usbc_info[i].base){
	        port_num = usbc->usbc_info[i].num;
	        usbc_base_address[i] = usbc->usbc_info[i].base;
		}
    }

    return 0;
}

/*
***********************************************************************************
*                            USBC_exit
*
* Description:
*
*
* Arguments:
*
*
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*
*
***********************************************************************************
*/
__s32 USBC_exit(bsp_usbc_t *usbc)
{
    __usbc_otg_t *usbc_otg = usbc_otg_array;

    memset(&usbc_info_g, 0, sizeof(__fifo_info_t));
    memset(usbc_otg, 0, (USBC_MAX_OPEN_NUM * sizeof(__usbc_otg_t)));
    memset(usbc_base_address, 0, sizeof(usbc_base_address));

    return 0;
}

/* USB传输类型选择, 读写数据等 */
EXPORT_SYMBOL(USBC_OTG_SelectMode);

EXPORT_SYMBOL(USBC_ReadLenFromFifo);
EXPORT_SYMBOL(USBC_WritePacket);
EXPORT_SYMBOL(USBC_ReadPacket);

EXPORT_SYMBOL(USBC_ConfigFIFO_Base);
EXPORT_SYMBOL(USBC_GetPortFifoStartAddr);
EXPORT_SYMBOL(USBC_GetPortFifoSize);
EXPORT_SYMBOL(USBC_SelectFIFO);
EXPORT_SYMBOL(USBC_ConfigFifo_Default);
EXPORT_SYMBOL(USBC_ConfigFifo);

EXPORT_SYMBOL(USBC_SelectBus);

EXPORT_SYMBOL(USBC_GetActiveEp);
EXPORT_SYMBOL(USBC_SelectActiveEp);

EXPORT_SYMBOL(USBC_EnhanceSignal);

EXPORT_SYMBOL(USBC_GetLastFrameNumber);


/* usb 中断操作部分 */
EXPORT_SYMBOL(USBC_INT_EpPending);
EXPORT_SYMBOL(USBC_INT_MiscPending);
EXPORT_SYMBOL(USBC_INT_ClearEpPending);
EXPORT_SYMBOL(USBC_INT_ClearMiscPending);
EXPORT_SYMBOL(USBC_INT_ClearEpPendingAll);
EXPORT_SYMBOL(USBC_INT_ClearMiscPendingAll);

EXPORT_SYMBOL(USBC_INT_EnableEp);
EXPORT_SYMBOL(USBC_INT_EnableUsbMiscUint);

EXPORT_SYMBOL(USBC_INT_DisableEp);
EXPORT_SYMBOL(USBC_INT_DisableUsbMiscUint);

EXPORT_SYMBOL(USBC_INT_DisableEpAll);
EXPORT_SYMBOL(USBC_INT_DisableUsbMiscAll);


/* usb 控制操作部分 */
EXPORT_SYMBOL(USBC_GetVbusStatus);
EXPORT_SYMBOL(USBC_GetStatus_Dp);
EXPORT_SYMBOL(USBC_GetStatus_Dm);
EXPORT_SYMBOL(USBC_GetStatus_DpDm);

EXPORT_SYMBOL(USBC_GetOtgMode_Form_ID);
EXPORT_SYMBOL(USBC_GetOtgMode_Form_BDevice);

EXPORT_SYMBOL(USBC_SetWakeUp_Default);

EXPORT_SYMBOL(USBC_EnableIdPullUp);
EXPORT_SYMBOL(USBC_DisableIdPullUp);
EXPORT_SYMBOL(USBC_EnableDpDmPullUp);
EXPORT_SYMBOL(USBC_DisableDpDmPullUp);

EXPORT_SYMBOL(USBC_ForceId);
EXPORT_SYMBOL(USBC_ForceVbusValid);

EXPORT_SYMBOL(USBC_A_valid_InputSelect);

EXPORT_SYMBOL(USBC_EnableUsbLineStateBypass);
EXPORT_SYMBOL(USBC_DisableUsbLineStateBypass);
EXPORT_SYMBOL(USBC_EnableHosc);
EXPORT_SYMBOL(USBC_DisableHosc);

EXPORT_SYMBOL(USBC_IsVbusChange);
EXPORT_SYMBOL(USBC_IsIdChange);
EXPORT_SYMBOL(USBC_IsDpDmChange);

EXPORT_SYMBOL(USBC_DisableWakeIrq);
EXPORT_SYMBOL(USBC_DisableVbusChange);
EXPORT_SYMBOL(USBC_DisableIdChange);
EXPORT_SYMBOL(USBC_DisableDpDmChange);

EXPORT_SYMBOL(USBC_EnableWakeIrq);
EXPORT_SYMBOL(USBC_EnableVbusChange);
EXPORT_SYMBOL(USBC_EnableIdChange);
EXPORT_SYMBOL(USBC_EnableDpDmChange);

/* usb 测试模式 */
EXPORT_SYMBOL(USBC_EnterMode_TestPacket);
EXPORT_SYMBOL(USBC_EnterMode_Test_K);
EXPORT_SYMBOL(USBC_EnterMode_Test_J);
EXPORT_SYMBOL(USBC_EnterMode_Test_SE0_NAK);
EXPORT_SYMBOL(USBC_EnterMode_Idle);

EXPORT_SYMBOL(USBC_TestMode_ReadReg);

EXPORT_SYMBOL(USBC_open_otg);
EXPORT_SYMBOL(USBC_close_otg);
EXPORT_SYMBOL(USBC_init);
EXPORT_SYMBOL(USBC_exit);



