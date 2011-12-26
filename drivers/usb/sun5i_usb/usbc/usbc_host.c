/*
********************************************************************************************************************
*                                              usb controller
*
*                              (c) Copyright 2007-2009, daniel.China
*										All	Rights Reserved
*
* File Name 	: usbc_host.c
*
* Author 		: daniel
*
* Version 		: 1.0
*
* Date 			: 2009.09.21
*
* Description 	: 适用于sunii平台，USB寄存器原子操作
*
* History 		: 
*
********************************************************************************************************************
*/

#include  "usbc_i.h"


/*
 ***************************************************************************
 *
 * 选择 usb host 的速度类型。如 高速、全速、低速。
 *
 ***************************************************************************
 */
/* 不配置速度类型 */
static void __USBC_Host_TsMode_Default(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_b(USBC_BP_POWER_H_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

/* 配置 high speed */
static void __USBC_Host_TsMode_Hs(__u32 usbc_base_addr)
{
    USBC_REG_set_bit_b(USBC_BP_POWER_H_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

/* 配置 full speed */
static void __USBC_Host_TsMode_Fs(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_b(USBC_BP_POWER_H_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

/* 配置 low speed */
static void __USBC_Host_TsMode_Ls(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_b(USBC_BP_POWER_H_HIGH_SPEED_EN, USBC_REG_PCTL(usbc_base_addr));
}

static void __USBC_Host_ep0_EnablePing(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_DisPing, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_DisablePing(__u32 usbc_base_addr)
{
    USBC_REG_set_bit_w(USBC_BP_CSR0_H_DisPing, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Host_ep0_IsNakTimeOut(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_NAK_Timeout, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ClearNakTimeOut(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_NAK_Timeout, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Host_ep0_IsError(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_Error, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ClearError(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_Error, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_EpType(__u32 usbc_base_addr, __u32 ts_mode)
{
    __u32 reg_val = 0;

	/* config transfer speed */
	switch(ts_mode){
		case USBC_TS_MODE_HS:
			reg_val |= 0x01 << USBC_BP_RXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_FS:
			reg_val |= 0x02 << USBC_BP_RXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_LS:
			reg_val |= 0x03 << USBC_BP_RXTYPE_SPEED;
		break;

		default:
			reg_val = 0;
	}

	USBC_Writeb(reg_val, USBC_REG_EP0TYPE(usbc_base_addr));
}

static void __USBC_Host_ep0_FlushFifo(__u32 usbc_base_addr)
{
    USBC_Writew(1 << USBC_BP_CSR0_H_FlushFIFO, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ConfigEp_Default(__u32 usbc_base_addr)
{
    //--<1>--config ep0 csr
    USBC_Writew(1<<USBC_BP_CSR0_H_FlushFIFO, USBC_REG_CSR0(usbc_base_addr));

	//--<2>--config polling interval
	USBC_Writeb(0x00, USBC_REG_TXINTERVAL(usbc_base_addr));

    /* config ep transfer type */
	USBC_Writeb(0x00, USBC_REG_EP0TYPE(usbc_base_addr));
}

static void __USBC_Host_ep0_ConfigEp(__u32 usbc_base_addr, __u32 ts_mode, __u32 interval)
{
    //--<1>--config ep0 csr
    USBC_Writew(1<<USBC_BP_CSR0_H_FlushFIFO, USBC_REG_CSR0(usbc_base_addr));

	//--<2>--config polling interval
	USBC_Writeb(interval, USBC_REG_NAKLIMIT0(usbc_base_addr));

    /* config ep0 transfer type */
	__USBC_Host_ep0_EpType(usbc_base_addr, ts_mode);
}

static __u32 __USBC_Host_ep0_IsReadDataReady(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_RxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Host_ep0_IsWriteDataReady(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_TxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ReadDataHalf(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_RxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ReadDataComplete(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_RxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_WriteDataHalf(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_CSR0_H_TxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_WriteDataComplete(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_CSR0_H_TxPkRdy, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Host_ep0_IsStall(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_RxStall, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ClearStall(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_CSR0_H_RxStall, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_ClearCSR(__u32 usbc_base_addr)
{
    USBC_Writew(0x00, USBC_REG_CSR0(usbc_base_addr));
}

static __u32 __USBC_Host_ep0_IsReqPktSet(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_CSR0_H_ReqPkt, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_StartInToken(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_CSR0_H_ReqPkt, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_StopInToken(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_CSR0_H_ReqPkt, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_StatusAfterIn(__u32 usbc_base_addr)
{
    __u32 reg_val = 0;
    
    reg_val = USBC_Readw(USBC_REG_CSR0(usbc_base_addr));
    reg_val |= 1 << USBC_BP_CSR0_H_TxPkRdy;
    reg_val |= 1 << USBC_BP_CSR0_H_StatusPkt;
    USBC_Writew(reg_val, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_StatusAfterOut(__u32 usbc_base_addr)
{
    __u32 reg_val = 0;
    
    reg_val = USBC_Readw(USBC_REG_CSR0(usbc_base_addr));
    reg_val |= 1 << USBC_BP_CSR0_H_ReqPkt;
    reg_val |= 1 << USBC_BP_CSR0_H_StatusPkt;
    USBC_Writew(reg_val, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_ep0_SendSetupPkt(__u32 usbc_base_addr)
{
    __u32 reg_val = 0;

    reg_val = USBC_Readw(USBC_REG_CSR0(usbc_base_addr));
    reg_val |= 1 << USBC_BP_CSR0_H_SetupPkt;
    reg_val |= 1 << USBC_BP_CSR0_H_TxPkRdy;
    USBC_Writew(reg_val, USBC_REG_CSR0(usbc_base_addr));
}

static void __USBC_Host_Tx_EpType(__u32 usbc_base_addr, __u32 ep_index, __u32 ts_mode, __u32 ts_type)
{
    __u32 reg_val = 0;

	/* config transfer speed */
	switch(ts_mode){
		case USBC_TS_MODE_HS:
			reg_val |= 0x01 << USBC_BP_TXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_FS:
			reg_val |= 0x02 << USBC_BP_TXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_LS:
			reg_val |= 0x03 << USBC_BP_TXTYPE_SPEED;
		break;

		default:
			reg_val = 0;
	}

    //--<1>--config protocol
    switch(ts_type){
		case USBC_TS_TYPE_ISO:
		    reg_val |= 0x1 << USBC_BP_TXTYPE_PROROCOL;
	    break;

		case USBC_TS_TYPE_BULK:
		    reg_val |= 0x2 << USBC_BP_TXTYPE_PROROCOL; 
		break;

		case USBC_TS_TYPE_INT:
		    reg_val |= 0x3 << USBC_BP_TXTYPE_PROROCOL;
		break;

		default:
		break;
	}

	//--<2>--config target ep number
	reg_val |= ep_index;

	USBC_Writeb(reg_val, USBC_REG_TXTYPE(usbc_base_addr));
}

static void __USBC_Host_Tx_FlushFifo(__u32 usbc_base_addr)
{
    USBC_Writew((1 << USBC_BP_TXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_TXCSR_H_FLUSH_FIFO), 
                USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_ConfigEp_Default(__u32 usbc_base_addr)
{
	//--<1>--config tx_csr, 先刷fifo, 有全部清零
    USBC_Writew((1 << USBC_BP_TXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_TXCSR_H_FLUSH_FIFO), 
                USBC_REG_TXCSR(usbc_base_addr));

	USBC_Writew(0x00, USBC_REG_TXCSR(usbc_base_addr));

	//--<2>--config tx ep max packet
	USBC_Writew(0x00, USBC_REG_TXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	USBC_Writeb(0x00, USBC_REG_TXTYPE(usbc_base_addr));

	//--<4>--config polling interval
	USBC_Writeb(0x00, USBC_REG_TXINTERVAL(usbc_base_addr));
}

static void __USBC_Host_Tx_ConfigEp(__u32 usbc_base_addr, __u32 ep_index, __u32 ts_mode, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt, __u32 interval)
{
    __u16 reg_val = 0;
	__u16 temp = 0;

	//--<1>--config tx_csr
    USBC_Writew((1 << USBC_BP_TXCSR_H_MODE) | (1 << USBC_BP_TXCSR_H_CLEAR_DATA_TOGGLE)
              	| (1 << USBC_BP_TXCSR_H_FLUSH_FIFO), 
                USBC_REG_TXCSR(usbc_base_addr));

	if(is_double_fifo){
		USBC_Writew((1 << USBC_BP_TXCSR_H_MODE) | (1 << USBC_BP_TXCSR_H_CLEAR_DATA_TOGGLE)
                  	| (1 << USBC_BP_TXCSR_H_FLUSH_FIFO), 
                	USBC_REG_TXCSR(usbc_base_addr));
	}

	//--<2>--config tx ep max packet
	reg_val = USBC_Readw(USBC_REG_TXMAXP(usbc_base_addr));
	temp    = ep_MaxPkt & ((1 << USBC_BP_TXMAXP_PACKET_COUNT) - 1);
	reg_val |= temp;
	USBC_Writew(reg_val, USBC_REG_TXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	__USBC_Host_Tx_EpType(usbc_base_addr, ep_index, ts_mode, ts_type);

	//--<4>--config polling interval
	USBC_Writeb(interval, USBC_REG_TXINTERVAL(usbc_base_addr));
}

static void __USBC_Host_Tx_ConfigEpDma(__u32 usbc_base_addr)
{
    __u16 ep_csr = 0;

	//auto_set, tx_mode, dma_tx_en, mode1
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr |= (1 << USBC_BP_TXCSR_H_AUTOSET) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_H_MODE) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_H_DMA_REQ_EN) >> 8;
	ep_csr |= (1 << USBC_BP_TXCSR_H_DMA_REQ_MODE) >> 8;
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));
}

static void __USBC_Host_Tx_ClearEpDma(__u32 usbc_base_addr)
{
    __u16 ep_csr = 0;

	//auto_set, dma_tx_en, mode1
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_TXCSR_H_AUTOSET) >> 8);
	ep_csr &= ~((1 << USBC_BP_TXCSR_H_DMA_REQ_EN) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));

	//DMA_REQ_EN和DMA_REQ_MODE不能在同一个cycle中清除
	ep_csr = USBC_Readb(USBC_REG_TXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_TXCSR_H_DMA_REQ_MODE) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_TXCSR(usbc_base_addr) + 1));
}

static __u32 __USBC_Host_Tx_IsWriteDataReady(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_TXCSR_H_TX_READY, USBC_REG_TXCSR(usbc_base_addr))
    		| USBC_REG_test_bit_w(USBC_BP_TXCSR_H_FIFO_NOT_EMPTY, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_WriteDataHalf(__u32 usbc_base_addr)
{
    USBC_REG_set_bit_w(USBC_BP_TXCSR_H_TX_READY, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_WriteDataComplete(__u32 usbc_base_addr)
{
    USBC_REG_set_bit_w(USBC_BP_TXCSR_H_TX_READY, USBC_REG_TXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Tx_IsNakTimeOut(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_TXCSR_H_NAK_TIMEOUT, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_ClearNakTimeOut(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_TXCSR_H_NAK_TIMEOUT, USBC_REG_TXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Tx_IsError(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_TXCSR_H_ERROR, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_ClearError(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_TXCSR_H_ERROR, USBC_REG_TXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Tx_IsStall(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_TXCSR_H_TX_STALL, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Tx_ClearStall(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_TXCSR_H_TX_STALL, USBC_REG_TXCSR(usbc_base_addr));
}

/*
static void __USBC_Host_Tx_ForbidStall(__u32 usbc_base_addr)
{
    USBC_REG_set_bit_w(USBC_BP_TXCSR_H_RX_STALL, USBC_REG_TXCSR(usbc_base_addr));
}
*/

static void __USBC_Host_Tx_ClearCSR(__u32 usbc_base_addr)
{
    USBC_Writew(0x00, USBC_REG_TXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_EpType(__u32 usbc_base_addr, __u32 ep_index, __u32 ts_mode, __u32 ts_type)
{
    __u32 reg_val = 0;

	/* config transfer speed */
	switch(ts_mode){
		case USBC_TS_MODE_HS:
			reg_val |= 0x01 << USBC_BP_RXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_FS:
			reg_val |= 0x02 << USBC_BP_RXTYPE_SPEED;
		break;
		
		case USBC_TS_MODE_LS:
			reg_val |= 0x03 << USBC_BP_RXTYPE_SPEED;
		break;

		default:
			reg_val = 0;
	}
    
    //--<1>--config protocol
    switch(ts_type){
		case USBC_TS_TYPE_ISO:
		    reg_val |= 0x1 << USBC_BP_RXTYPE_PROROCOL;
	    break;

		case USBC_TS_TYPE_BULK:
		    reg_val |= 0x2 << USBC_BP_RXTYPE_PROROCOL;
		break;

		case USBC_TS_TYPE_INT:
		    reg_val |= 0x3 << USBC_BP_RXTYPE_PROROCOL;
		break;

		default:
		break;
	}

	//--<2>--config target ep number
	reg_val |= ep_index;

	USBC_Writeb(reg_val, USBC_REG_RXTYPE(usbc_base_addr));
}

static void __USBC_Host_Rx_FlushFifo(__u32 usbc_base_addr)
{
    USBC_Writew((1 << USBC_BP_RXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_H_FLUSH_FIFO), 
                USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ConfigEp_Default(__u32 usbc_base_addr)
{
	//--<1>--config rx_csr, 先刷fifo, 有全部清零
    USBC_Writew((1 << USBC_BP_RXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_H_FLUSH_FIFO), 
                USBC_REG_RXCSR(usbc_base_addr));

	USBC_Writew(0x00, USBC_REG_RXCSR(usbc_base_addr));

	//--<2>--config rx ep max packet
	USBC_Writew(0x00, USBC_REG_RXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	USBC_Writeb(0x00, USBC_REG_RXTYPE(usbc_base_addr));

	//--<4>--config polling interval
	USBC_Writeb(0x00, USBC_REG_RXINTERVAL(usbc_base_addr));
}

static void __USBC_Host_Rx_ConfigEp(__u32 usbc_base_addr, __u32 ep_index, __u32 ts_mode, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt, __u32 interval)
{
    __u16 reg_val = 0;
	__u16 temp = 0;

	//--<1>--config rx_csr
    USBC_Writew((1 << USBC_BP_RXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_H_FLUSH_FIFO), 
                USBC_REG_RXCSR(usbc_base_addr));

	if(is_double_fifo){
		USBC_Writew((1 << USBC_BP_RXCSR_H_CLEAR_DATA_TOGGLE) | (1 << USBC_BP_RXCSR_H_FLUSH_FIFO), 
                    USBC_REG_RXCSR(usbc_base_addr));
	}

	//--<2>--config tx ep max packet
	reg_val = USBC_Readw(USBC_REG_RXMAXP(usbc_base_addr));
	temp    = ep_MaxPkt & ((1 << USBC_BP_RXMAXP_PACKET_COUNT) - 1);
	reg_val |= temp;
	USBC_Writew(reg_val, USBC_REG_RXMAXP(usbc_base_addr));

	//--<3>--config ep transfer type
	__USBC_Host_Rx_EpType(usbc_base_addr, ep_index, ts_mode, ts_type);

	//--<4>--config polling interval
	USBC_Writeb(interval, USBC_REG_RXINTERVAL(usbc_base_addr));
}

static void __USBC_Host_Rx_ConfigEpDma(__u32 usbc_base_addr)
{
    __u16 ep_csr = 0;

    //配置dma, auto_clear, dma_rx_en, mode1,
	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr |= (1 << USBC_BP_RXCSR_H_AUTO_CLEAR) >> 8;
	ep_csr |= (1 << USBC_BP_RXCSR_H_AUTO_REQ) >> 8;
//	ep_csr &= ~((1 << USBC_BP_RXCSR_H_DMA_REQ_MODE) >> 8);
	ep_csr |= ((1 << USBC_BP_RXCSR_H_DMA_REQ_MODE) >> 8);
	ep_csr |= (1 << USBC_BP_RXCSR_H_DMA_REQ_EN) >> 8;
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));
}

static void __USBC_Host_Rx_ClearEpDma(__u32 usbc_base_addr)
{
    __u16 ep_csr = 0;

    //auto_clear, dma_rx_en, mode1,
	ep_csr = USBC_Readb(USBC_REG_RXCSR(usbc_base_addr) + 1);
	ep_csr &= ~((1 << USBC_BP_RXCSR_H_AUTO_CLEAR) >> 8);
	ep_csr &= ~((1 << USBC_BP_RXCSR_H_AUTO_REQ) >> 8);
	ep_csr &= ~((1 << USBC_BP_RXCSR_H_DMA_REQ_MODE) >> 8);
	ep_csr &= ~((1 << USBC_BP_RXCSR_H_DMA_REQ_EN) >> 8);
	USBC_Writeb(ep_csr, (USBC_REG_RXCSR(usbc_base_addr) + 1));
}

static __u32 __USBC_Host_Rx_IsReadDataReady(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_RXCSR_H_RX_PKT_READY, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ReadDataHalf(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_RX_PKT_READY, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ReadDataComplete(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_RX_PKT_READY, USBC_REG_RXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Rx_IsNakTimeOut(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_RXCSR_H_NAK_TIMEOUT, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ClearNakTimeOut(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_NAK_TIMEOUT, USBC_REG_RXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Rx_IsError(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_RXCSR_H_ERROR, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ClearError(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_ERROR, USBC_REG_RXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Rx_IsStall(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_RXCSR_H_RX_STALL, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ClearStall(__u32 usbc_base_addr)
{
    USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_RX_STALL, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_ClearCSR(__u32 usbc_base_addr)
{
    USBC_Writew(0x00, USBC_REG_RXCSR(usbc_base_addr));
}

static __u32 __USBC_Host_Rx_IsReqPktSet(__u32 usbc_base_addr)
{
    return USBC_REG_test_bit_w(USBC_BP_RXCSR_H_REQ_PACKET, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_StartInToken(__u32 usbc_base_addr)
{
	USBC_REG_set_bit_w(USBC_BP_RXCSR_H_REQ_PACKET, USBC_REG_RXCSR(usbc_base_addr));
}

static void __USBC_Host_Rx_StopInToken(__u32 usbc_base_addr)
{
	USBC_REG_clear_bit_w(USBC_BP_RXCSR_H_REQ_PACKET, USBC_REG_RXCSR(usbc_base_addr));
}

void USBC_Host_SetFunctionAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(0x00, USBC_REG_TXFADDRx(usbc_otg->base_addr, ep_index));
			USBC_Writeb(0x00, USBC_REG_TXHADDRx(usbc_otg->base_addr, ep_index));
			USBC_Writeb(0x00, USBC_REG_TXHPORTx(usbc_otg->base_addr, ep_index));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(0x00, USBC_REG_RXFADDRx(usbc_otg->base_addr, ep_index));
			USBC_Writeb(0x00, USBC_REG_RXHADDRx(usbc_otg->base_addr, ep_index));
			USBC_Writeb(0x00, USBC_REG_RXHPORTx(usbc_otg->base_addr, ep_index));
		break;

		default:
		break;
	}
}

void USBC_Host_SetFunctionAddress(__hdle hUSB, 
								  __u32 EpType, 
								  __u32 EpIndex,
								  __u32 FunctionAdress,
								  __u32 MultiTT,
								  __u32 HubAddress, 
								  __u32 HubPortNumber)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u8 temp_8 = 0;

	if(usbc_otg == NULL){
		return;
	}

	if(MultiTT){
		temp_8 = 1 << USBC_BP_HADDR_MULTI_TT;
	}

	temp_8 |= HubAddress;

	switch(EpType){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(FunctionAdress, USBC_REG_TXFADDRx(usbc_otg->base_addr, EpIndex));
			USBC_Writeb(temp_8, USBC_REG_TXHADDRx(usbc_otg->base_addr, EpIndex));
			USBC_Writeb(HubPortNumber, USBC_REG_TXHPORTx(usbc_otg->base_addr, EpIndex));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(FunctionAdress, USBC_REG_RXFADDRx(usbc_otg->base_addr, EpIndex));
			USBC_Writeb(temp_8, USBC_REG_RXHADDRx(usbc_otg->base_addr, EpIndex));
			USBC_Writeb(HubPortNumber, USBC_REG_RXHPORTx(usbc_otg->base_addr, EpIndex));
		break;

		default:
		break;
	}
}

void USBC_Host_SetHubAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(0x00, USBC_REG_TXHADDRx(usbc_otg->base_addr, ep_index));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(0x00, USBC_REG_RXHADDRx(usbc_otg->base_addr, ep_index));
		break;

		default:
		break;
	}
}

void USBC_Host_SetHubAddress(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u32 is_mutli_tt, __u8 address)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(((is_mutli_tt << USBC_BP_HADDR_MULTI_TT) | address), USBC_REG_TXHADDRx(usbc_otg->base_addr, ep_index));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(((is_mutli_tt << USBC_BP_HADDR_MULTI_TT) | address), USBC_REG_RXHADDRx(usbc_otg->base_addr, ep_index));
		break;

		default:
		break;
	}
}

void USBC_Host_SetHPortAddress_Deafult(__hdle hUSB, __u32 ep_type, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(0x00, USBC_REG_TXHPORTx(usbc_otg->base_addr, ep_index));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(0x00, USBC_REG_RXHPORTx(usbc_otg->base_addr, ep_index));
		break;

		default:
		break;
	}
}

void USBC_Host_SetHPortAddress(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u8 address)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_TX:
			USBC_Writeb(address, USBC_REG_TXHPORTx(usbc_otg->base_addr, ep_index));
		break;
		
		case USBC_EP_TYPE_RX:
			USBC_Writeb(address, USBC_REG_RXHPORTx(usbc_otg->base_addr, ep_index));
		break;

		default:
		break;
	}
}

__u32 USBC_Host_QueryTransferMode(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return USBC_TS_MODE_UNKOWN;
	}

    if(USBC_REG_test_bit_b(USBC_BP_POWER_H_HIGH_SPEED_FLAG, USBC_REG_PCTL(usbc_otg->base_addr))){
		return USBC_TS_MODE_HS;
	}else{
	    return USBC_TS_MODE_FS;
	}
}

void USBC_Host_ConfigTransferMode(__hdle hUSB, __u32 speed_mode)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	//选择传输速度
    switch(speed_mode){
		case USBC_TS_MODE_HS:
			__USBC_Host_TsMode_Hs(usbc_otg->base_addr);
		break;

		case USBC_TS_MODE_FS:
			__USBC_Host_TsMode_Fs(usbc_otg->base_addr);
		break;

		case USBC_TS_MODE_LS:
			__USBC_Host_TsMode_Ls(usbc_otg->base_addr);
		break;

		default:  //默认hs
			__USBC_Host_TsMode_Default(usbc_otg->base_addr);
	}
}

/* reset usb 端口上的设备, 建议reset时间为100ms */
void USBC_Host_ResetPort(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_POWER_H_RESET, USBC_REG_PCTL(usbc_otg->base_addr));
}

/* USBC_Host_ResetPort和USBC_Host_ClearResetPortFlag应该合并的, 但是在bsp层延时会影响效率 */
void USBC_Host_ClearResetPortFlag(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_clear_bit_b(USBC_BP_POWER_H_RESET, USBC_REG_PCTL(usbc_otg->base_addr));
}

/* resume usb 端口上的设备, 建议resume时间为10ms */
void USBC_Host_RusumePort(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_POWER_H_RESUME, USBC_REG_PCTL(usbc_otg->base_addr));
}

/* USBC_Host_RusumePort和USBC_Host_ClearRusumePortFlag应该合并的, 但是在bsp层延时会影响效率 */
void USBC_Host_ClearRusumePortFlag(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_clear_bit_b(USBC_BP_POWER_H_RESUME, USBC_REG_PCTL(usbc_otg->base_addr));
}

/* usb 端口suspend */
void USBC_Host_SuspendPort(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

    USBC_REG_set_bit_b(USBC_BP_POWER_H_SUSPEND, USBC_REG_PCTL(usbc_otg->base_addr));
}

__u32 USBC_Host_QueryPowerStatus(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	return USBC_Readb(USBC_REG_PCTL(usbc_otg->base_addr));
}


void USBC_Host_StartSession(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	USBC_REG_set_bit_b(USBC_BP_DEVCTL_SESSION, USBC_REG_DEVCTL(usbc_otg->base_addr));
}

void USBC_Host_EndSession(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	USBC_REG_clear_bit_b(USBC_BP_DEVCTL_SESSION, USBC_REG_DEVCTL(usbc_otg->base_addr));
}

/*
***********************************************************************************
*                     USBC_Host_PeripheralType
*
* Description:
*    外部设备的速度类型
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
__u32 USBC_Host_PeripheralType(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
    __u8 reg_val = 0;

	if(usbc_otg == NULL){
		return 0;
	}

	reg_val = USBC_Readb(USBC_REG_DEVCTL(usbc_otg->base_addr));
	if(reg_val & (1 << USBC_BP_DEVCTL_FS_DEV)){
		return USBC_DEVICE_FSDEV;
	}else if(reg_val & (1 << USBC_BP_DEVCTL_LS_DEV)){
	    return USBC_DEVICE_LSDEV;
	}else{
	    return USBC_DEVICE_LSDEV;
	}
}

void USBC_Host_FlushFifo(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_FlushFifo(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_FlushFifo(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_FlushFifo(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_ConfigEp_Default
*
* Description:
*    释放ep所有的资源, 中断除外
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ConfigEp_Default(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ConfigEp_Default(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ConfigEp_Default(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ConfigEp_Default(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Dev_ConfigEp
*
* Description:
*    配置ep, 包括双FIFO、最大传输包等
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  ep类型
*    ep_index       :  input.  目标设备的ep号
*    ts_type        :  input.  传输类型
*    is_double_fifo :  input.  速度模式
*    ep_MaxPkt      :  input.  最大包
*    interval       :  input.  时间间隔
*
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ConfigEp(__hdle hUSB, __u32 ep_type, __u32 ep_index, __u32 ts_mode, __u32 ts_type, __u32 is_double_fifo, __u32 ep_MaxPkt, __u32 interval)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ConfigEp(usbc_otg->base_addr, ts_mode, interval);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ConfigEp(usbc_otg->base_addr, ep_index, ts_mode, ts_type, is_double_fifo, ep_MaxPkt, interval);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ConfigEp(usbc_otg->base_addr, ep_index, ts_mode, ts_type, is_double_fifo, ep_MaxPkt, interval);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_ConfigEpDma
*
* Description:
*    配置ep的dma设置
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ConfigEpDma(__hdle hUSB, __u32 ep_type)
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
			__USBC_Host_Tx_ConfigEpDma(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ConfigEpDma(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_ClearEpDma
*
* Description:
*    清除ep的dma设置
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
* 
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ClearEpDma(__hdle hUSB, __u32 ep_type)
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
			__USBC_Host_Tx_ClearEpDma(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ClearEpDma(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_IsEpNakTimeOut
*
* Description:
*    查询ep是否error
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*    0  :  NAK not timeout
*    1  :  NAK timeout
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsEpNakTimeOut(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsNakTimeOut(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_TX:
			return __USBC_Host_Tx_IsNakTimeOut(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_RX:
			return __USBC_Host_Rx_IsNakTimeOut(usbc_otg->base_addr);

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_ClearEpNakTimeOut
*
* Description:
*    清除ep的error状态
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ClearEpNakTimeOut(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ClearNakTimeOut(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ClearNakTimeOut(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ClearNakTimeOut(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_IsEpError
*
* Description:
*    查询ep是否error
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*    0  :  ep is not error
*    1  :  ep is error
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsEpError(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsError(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_TX:
			return __USBC_Host_Tx_IsError(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_RX:
			return __USBC_Host_Rx_IsError(usbc_otg->base_addr);

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_ClearEpError
*
* Description:
*    清除ep的error状态
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ClearEpError(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ClearError(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ClearError(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ClearError(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_IsEpStall
*
* Description:
*    查询ep是否stall
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*    0  :  ep is not stall
*    1  :  ep is stall
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsEpStall(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsStall(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_TX:
			return __USBC_Host_Tx_IsStall(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_RX:
			return __USBC_Host_Rx_IsStall(usbc_otg->base_addr);

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_ClearEpStall
*
* Description:
*    清除ep的stall状态
*
* Arguments:
*    hUSB           :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type        :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_ClearEpStall(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return ;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ClearStall(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ClearStall(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ClearStall(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

void USBC_Host_ClearEpCSR(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ClearCSR(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_ClearCSR(usbc_otg->base_addr);
		break;

		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ClearCSR(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

static __s32 __USBC_Host_ReadDataHalf(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ReadDataHalf(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			//not support
		    return -1;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ReadDataHalf(usbc_base_addr);
		break;

		default:
		    return -1;
	}

	return 0;
}

static __s32 __USBC_Host_ReadDataComplete(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_ReadDataComplete(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			//not support
		    return -1;
		
		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_ReadDataComplete(usbc_base_addr);
		break;

		default:
		    return -1;
	}

	return 0;
}

static __s32 __USBC_Host_WriteDataHalf(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_WriteDataHalf(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_WriteDataHalf(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			//not support
		    return -1;

		default:
		    return -1;
	}

	return 0;
}

static __s32 __USBC_Host_WriteDataComplete(__u32 usbc_base_addr, __u32 ep_type)
{
	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_WriteDataComplete(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			__USBC_Host_Tx_WriteDataComplete(usbc_base_addr);
		break;
		
		case USBC_EP_TYPE_RX:
			//not support
		    return -1;

		default:
		    return -1;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_IsReadDataReady
*
* Description:
*    查询usb准备读取的数据是否准备好了
*
* Arguments:
*    hUSB     :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type  :  input.  传输类型
*
* Returns:
*  
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsReadDataReady(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsReadDataReady(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_TX:
			//not support
		break;
		
		case USBC_EP_TYPE_RX:
			return __USBC_Host_Rx_IsReadDataReady(usbc_otg->base_addr);

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_IsWriteDataReady
*
* Description:
*    查询fifo是否为空
*
* Arguments:
*    hUSB    :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type :  input.  传输类型
*
* Returns:
*  
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsWriteDataReady(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsWriteDataReady(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_TX:
			return __USBC_Host_Tx_IsWriteDataReady(usbc_otg->base_addr);
		
		case USBC_EP_TYPE_RX:
			//not support
		break;

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_ReadDataStatus
*
* Description:
*    写数据的状况, 是写了一部分, 还是完全写完了
*
* Arguments:
*    hUSB      :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type   :  input.  传输类型
*    complete  :  input.  是否所有的数据都写完了
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*    无
*
***********************************************************************************
*/
__s32 USBC_Host_ReadDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return -1;
	}

	if(complete){
		__USBC_Host_ReadDataComplete(usbc_otg->base_addr, ep_type);
	}else{
	    __USBC_Host_ReadDataHalf(usbc_otg->base_addr, ep_type);
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_WriteDataStatus
*
* Description:
*    写数据的状况, 是写了一部分, 还是完全写完了
*
* Arguments:
*    hUSB      :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type   :  input.  传输类型
*    complete  :  input.  是否所有的数据都写完了
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*    无
*
***********************************************************************************
*/
__s32 USBC_Host_WriteDataStatus(__hdle hUSB, __u32 ep_type, __u32 complete)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return -1;
	}

	if(complete){
		return __USBC_Host_WriteDataComplete(usbc_otg->base_addr, ep_type);
	}else{
	    return __USBC_Host_WriteDataHalf(usbc_otg->base_addr, ep_type);
	}
}

/*
***********************************************************************************
*                     USBC_Host_IsReqPktSet
*
* Description:
*    ReqPkt位是否被设置
*
* Arguments:
*    hUSB      :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type   :  input.  传输类型
* Returns:
*    0  :  成功
*   !0  :  失败
*
* note:
*    无
*
***********************************************************************************
*/
__u32 USBC_Host_IsReqPktSet(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return 0;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			return __USBC_Host_ep0_IsReqPktSet(usbc_otg->base_addr);
		//break;
		
		case USBC_EP_TYPE_TX:
			//not support
		break;

		case USBC_EP_TYPE_RX:
			return __USBC_Host_Rx_IsReqPktSet(usbc_otg->base_addr);
		//break;

		default:
		break;
	}

	return 0;
}

/*
***********************************************************************************
*                     USBC_Host_StartInToken
*
* Description:
*    向设备发in token
*
* Arguments:
*    hUSB      :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type   :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_StartInToken(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_StartInToken(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			//not support
		break;

		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_StartInToken(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}

/*
***********************************************************************************
*                     USBC_Host_StopInToken
*
* Description:
*    停止向设备发in token
*
* Arguments:
*    hUSB      :  input.  USBC_open_otg获得的句柄, 记录了USBC所需要的一些关键数据
*    ep_type   :  input.  传输类型
* Returns:
*
*
* note:
*    无
*
***********************************************************************************
*/
void USBC_Host_StopInToken(__hdle hUSB, __u32 ep_type)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	switch(ep_type){
		case USBC_EP_TYPE_EP0:
			__USBC_Host_ep0_StopInToken(usbc_otg->base_addr);
		break;
		
		case USBC_EP_TYPE_TX:
			//not support
		break;

		case USBC_EP_TYPE_RX:
			__USBC_Host_Rx_StopInToken(usbc_otg->base_addr);
		break;

		default:
		break;
	}
}
/*
void USBC_Host_ConfigRqPktCount(__hdle hUSB, __u32 ep_index, __u32 RqPktCount)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	USBC_Writew(RqPktCount, USBC_REG_RPCOUNTx(usbc_otg->base_addr, ep_index));
}
*/
void USBC_Host_ConfigRqPktCount(__hdle hUSB, __u32 ep_index, __u32 RqPktCount)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;
	__u32 temp = 0;

	if(usbc_otg == NULL){
		return;
	}

	temp = USBC_REG_RPCOUNTx(usbc_otg->base_addr, ep_index);

	USBC_Writew(RqPktCount, temp);
}

void USBC_Host_ClearRqPktCount(__hdle hUSB, __u32 ep_index)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	USBC_Writew(0x00, USBC_REG_RPCOUNTx(usbc_otg->base_addr, ep_index));
}

void USBC_Host_EnablePing(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	__USBC_Host_ep0_EnablePing(usbc_otg->base_addr);
}

void USBC_Host_DisablePing(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	__USBC_Host_ep0_DisablePing(usbc_otg->base_addr);
}

void USBC_Host_SendCtrlStatus(__hdle hUSB, __u32 is_after_in)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

    if(is_after_in){
		__USBC_Host_ep0_StatusAfterIn(usbc_otg->base_addr);
	}else{
		__USBC_Host_ep0_StatusAfterOut(usbc_otg->base_addr);
	}
}

void USBC_Host_SendSetupPkt(__hdle hUSB)
{
    __usbc_otg_t *usbc_otg = (__usbc_otg_t *)hUSB;

	if(usbc_otg == NULL){
		return;
	}

	__USBC_Host_ep0_SendSetupPkt(usbc_otg->base_addr);
}



EXPORT_SYMBOL(USBC_Host_SetFunctionAddress_Deafult);
EXPORT_SYMBOL(USBC_Host_SetFunctionAddress);
EXPORT_SYMBOL(USBC_Host_SetHubAddress_Deafult);
EXPORT_SYMBOL(USBC_Host_SetHubAddress);
EXPORT_SYMBOL(USBC_Host_SetHPortAddress_Deafult);
EXPORT_SYMBOL(USBC_Host_SetHPortAddress);

EXPORT_SYMBOL(USBC_Host_QueryTransferMode);
EXPORT_SYMBOL(USBC_Host_ConfigTransferMode);

EXPORT_SYMBOL(USBC_Host_ResetPort);
EXPORT_SYMBOL(USBC_Host_ClearResetPortFlag);
EXPORT_SYMBOL(USBC_Host_RusumePort);
EXPORT_SYMBOL(USBC_Host_ClearRusumePortFlag);
EXPORT_SYMBOL(USBC_Host_SuspendPort);
EXPORT_SYMBOL(USBC_Host_QueryPowerStatus);

EXPORT_SYMBOL(USBC_Host_EnablePing);
EXPORT_SYMBOL(USBC_Host_DisablePing);
EXPORT_SYMBOL(USBC_Host_IsReqPktSet);
EXPORT_SYMBOL(USBC_Host_StartInToken);
EXPORT_SYMBOL(USBC_Host_StopInToken);
EXPORT_SYMBOL(USBC_Host_SendCtrlStatus);
EXPORT_SYMBOL(USBC_Host_SendSetupPkt);

EXPORT_SYMBOL(USBC_Host_StartSession);
EXPORT_SYMBOL(USBC_Host_EndSession);
EXPORT_SYMBOL(USBC_Host_ConfigRqPktCount);
EXPORT_SYMBOL(USBC_Host_ClearRqPktCount);

EXPORT_SYMBOL(USBC_Host_PeripheralType);

EXPORT_SYMBOL(USBC_Host_FlushFifo);
EXPORT_SYMBOL(USBC_Host_ConfigEp_Default);
EXPORT_SYMBOL(USBC_Host_ConfigEp);
EXPORT_SYMBOL(USBC_Host_ConfigEpDma);
EXPORT_SYMBOL(USBC_Host_ClearEpDma);

EXPORT_SYMBOL(USBC_Host_IsEpStall);
EXPORT_SYMBOL(USBC_Host_ClearEpStall);
EXPORT_SYMBOL(USBC_Host_IsEpNakTimeOut);
EXPORT_SYMBOL(USBC_Host_ClearEpNakTimeOut);
EXPORT_SYMBOL(USBC_Host_IsEpError);
EXPORT_SYMBOL(USBC_Host_ClearEpError);
EXPORT_SYMBOL(USBC_Host_ClearEpCSR);

EXPORT_SYMBOL(USBC_Host_IsReadDataReady);
EXPORT_SYMBOL(USBC_Host_IsWriteDataReady);
EXPORT_SYMBOL(USBC_Host_ReadDataStatus);
EXPORT_SYMBOL(USBC_Host_WriteDataStatus);



