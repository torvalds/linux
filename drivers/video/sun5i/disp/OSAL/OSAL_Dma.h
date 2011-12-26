/*
*************************************************************************************
*                         			eBsp
*					   Operation System Adapter Layer
*
*				(c) Copyright 2006-2010, All winners Co,Ld.
*							All	Rights Reserved
*
* File Name 	: OSAL_Dma.h
*
* Author 		: javen
*
* Description 	: Dma操作
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   	2010-09-07          1.0         create this word
*		holi			2010-12-04			1.1			调整的参数部分，完全走CSP_para这条路
*************************************************************************************
*/
#ifndef  __OSAL_DMA_H__
#define  __OSAL_DMA_H__



//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------

typedef __s32 (*DmaCallback)( void *pArg);

/*
*******************************************************************************
*                     OSAL_DmaRequest
*
* Description:
*    申请DMA通道。
*
* Parameters:
*	 user_name 	:	模块名，方便统计
*    DmaType  	:  	input. DMA类型。Normal or Dedicated
* 
* Return value:
*    成功返回DMA句柄，失败返回NULL。
*
* note:
*    void
*
*******************************************************************************
*/
__hdle OSAL_DmaRequest(u8 * user_name ,__u32 DmaType);

/*
*******************************************************************************
*                     OSAL_DmaRelease
*
* Description:
*    申请DMA通道。
*
* Parameters:
*    hDMA ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaRelease(__hdle hDMA);


/*
*******************************************************************************
*                     OSAL_DmaEnableINT
*
* Description:
*    使能DMA中断
*
* Parameters:
*    hDMA 	    :  input. cspRequestDma申请的句柄。
*    IrqType    :  input. 传输类型。end_irq or half_irq。
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaEnableINT(__hdle hDMA, __s32 IrqType);

/*
*******************************************************************************
*                     OSAL_DmaDisableINT
*
* Description:
*    禁止DMA中断
*
* Parameters:
*    hDMA 	    :  input. cspRequestDma申请的句柄。
*    IrqType    :  input. 传输类型。end_irq or half_irq。
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaDisableINT(__hdle hDMA, __s32 IrqType);

/*
*******************************************************************************
*                     eBsp_DmaRegIrq
*
* Description:
*    注册中断处理函数。
*
* Parameters:
*    hDMA 	    :  input. cspRequestDma申请的句柄。
*    IrqType    :  input. 中断类型。end_irq or half_irq。
*    pCallBack  :  input. 中断回调函数。
*    pArg		:  input. 中断回调函数的参数。
* 
* Return value:
*    成功返回DMA句柄，失败返回NULL。
*
* note:
*    回调函数的原型：typedef void (*DmaCallback)(void *pArg);
*
*******************************************************************************
*/
__s32 OSAL_DmaRegIrq(__hdle hDMA, __u32 IrqType, DmaCallback pCallBack, void *pArg); 

/*
*******************************************************************************
*                     FunctionName
*
* Description:
*    注销中断处理函数。
*
* Parameters:
*    hDMA 	    :  input. cspRequestDma申请的句柄。
*    IrqType    :  input. 传输类型。end_irq or half_irq。
*    pCallBack  :  input. 中断回调函数。
* 
* Return value:
*    成功返回DMA句柄，失败返回NULL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaUnRegIrq(__hdle hDMA, __u32 IrqType, DmaCallback pCallBack);

/*
*******************************************************************************
*                     OSAL_DmaConfig
*
* Description:
*    配置DMA 通道，常用配置。
*
* Parameters:
*    hDMA 	     :  input. cspRequestDma申请的句柄。
*    p_cfg       :  input.  DMA配置。,实际数据结构请参数struct CSP_dma_config{}
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaConfig(__hdle hDMA, void * p_cfg);

/*
*******************************************************************************
*                     OSAL_DmaStart
*
* Description:
*    开始 DMA 传输。
*
* Parameters:
*    hDMA 	 		 :  input. cspRequestDma申请的句柄。
*    SrcAddr		 :  input. 源地址
*    DestAddr		 :  input. 目标地址
*    TransferLength  :  input. 传输长度
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaStart(__hdle hDMA, __u32 SrcAddr, __u32 DestAddr, __u32 TransferLength);

/*
*******************************************************************************
*                     OSAL_DmaStop
*
* Description:
*    停止本次DMA 传输。
*
* Parameters:
*    hDMA ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaStop(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaRestart
*
* Description:
*    重新上一次DMA传输。
*
* Parameters:
*    hDMA 	： input. cspRequestDma申请的句柄。
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaRestart(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaQueryChannelNo
*
* Description:
*    查询DMA的通道号。
*
* Parameters:
*    hDMA  ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    返回DMA通道号。
*
* note:
*    void
*
*******************************************************************************
*/
__u32 OSAL_DmaQueryChannelNo(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaQueryStatus
*
* Description:
*    查询DMA的通道的状态，Busy or Idle。
*
* Parameters:
*    hDMA ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    返回当前DMA通道的状态。1：busy，0：idle。
*
* note:
*    void
*
*******************************************************************************
*/
__u32 OSAL_DmaQueryStatus(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaQueryLeftCount
*
* Description:
*    查询DMA的剩余字节数。
*
* Parameters:
*    hDMA  :  input. cspRequestDma申请的句柄。
* 
* Return value:
*    返回当前DMA的剩余字节数。
*
* note:
*    void
*
*******************************************************************************
*/
__u32 OSAL_DmaQueryLeftCount(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaQueryConfig
*
* Description:
*    查询DMA通道的配置。
*
* Parameters:
*    hDMA 	   :  input. cspRequestDma申请的句柄。
*    RegAddr   :  input. 寄存器地址
*    RegWidth  :  input. 寄存器宽度
*    RegValue  :  output. 寄存器值
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaQueryConfig(__hdle hDMA, __u32 RegAddr, __u32 RegWidth, __u32 *RegValue);

/*
*******************************************************************************
*                     eBsp_DmaPause
*
* Description:
*    暂停DMA传输。
*
* Parameters:
*    hDMA  ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaPause(__hdle hDMA);

/*
*******************************************************************************
*                     eBsp_DmaProceed
*
* Description:
*    继续csp_DmaPause 暂停的DMA传输。
*
* Parameters:
*    hDMA  ： input. cspRequestDma申请的句柄。
* 
* Return value:
*    成功返回EBSP_OK，失败返回EBSP_FAIL。
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaProceed(__hdle hDMA);

/*
*******************************************************************************
*                     OSAL_DmaChangeMode
*
* Description:
*    切换 DMA 的传输模式。
*
* Parameters:
*    hDMA  ： input. cspRequestDma申请的句柄。
*    mode  :  input. 传输模式
* 
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 OSAL_DmaChangeMode(__hdle hDMA, __s32 mode);

#endif   //__OSAL_DMA_H__

