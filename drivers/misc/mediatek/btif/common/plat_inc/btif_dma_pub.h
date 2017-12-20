/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __HAL_BTIFD_DMA_PUB_H_
#define __HAL_BTIFD_DMA_PUB_H_

#include <linux/dma-mapping.h>

#include "plat_common.h"

typedef enum _ENUM_DMA_CTRL_ {
	DMA_CTRL_DISABLE = 0,
	DMA_CTRL_ENABLE = DMA_CTRL_DISABLE + 1,
	DMA_CTRL_BOTH,
} ENUM_DMA_CTRL;

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_info_get
* DESCRIPTION
*  get btif tx dma channel's information
* PARAMETERS
* dma_dir        [IN]         DMA's direction
* RETURNS
*  pointer to btif dma's information structure
*****************************************************************************/
P_MTK_DMA_INFO_STR hal_btif_dma_info_get(ENUM_DMA_DIR dma_dir);

/*****************************************************************************
* FUNCTION
*  hal_btif_dma_hw_init
* DESCRIPTION
*  control clock output enable/disable of DMA module
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_dma_hw_init(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_btif_clk_ctrl
* DESCRIPTION
*  control clock output enable/disable of DMA module
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_dma_clk_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_CLOCK_CTRL flag);

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_ctrl
* DESCRIPTION
* enable/disable Tx DMA channel
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* ctrl_id          [IN]        enable/disable ID
* dma_dir        [IN]         DMA's direction
* RETURNS
*  0 means success; negative means fail
*****************************************************************************/
int hal_btif_dma_ctrl(P_MTK_DMA_INFO_STR p_dma_info, ENUM_DMA_CTRL ctrl_id);

/*****************************************************************************
* FUNCTION
*  hal_btif_dma_rx_cb_reg
* DESCRIPTION
* register rx callback function to dma module
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* rx_cb           [IN]        function pointer to btif
* RETURNS
*  0 means success; negative means fail
*****************************************************************************/
int hal_btif_dma_rx_cb_reg(P_MTK_DMA_INFO_STR p_dma_info,
			   dma_rx_buf_write rx_cb);

/*****************************************************************************
* FUNCTION
*  hal_tx_vfifo_reset
* DESCRIPTION
*  reset tx virtual fifo information, except memory information
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* dma_dir  [IN]         DMA's direction
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_btif_vfifo_reset(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_tx_dma_irq_handler
* DESCRIPTION
*  lower level tx interrupt handler
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_tx_dma_irq_handler(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_dma_send_data
* DESCRIPTION
*  send data through btif in DMA mode
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN]        pointer to rx data buffer
* max_len  [IN]        tx buffer length
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_dma_send_data(P_MTK_DMA_INFO_STR p_dma_info,
		      const unsigned char *p_buf, const unsigned int buf_len);

/*****************************************************************************
* FUNCTION
*  hal_dma_is_tx_complete
* DESCRIPTION
*  get tx complete flag
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  true means tx complete, false means tx in process
*****************************************************************************/
bool hal_dma_is_tx_complete(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_dma_get_ava_room
* DESCRIPTION
*  get tx available room
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  available room  size
*****************************************************************************/
int hal_dma_get_ava_room(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_dma_is_tx_allow
* DESCRIPTION
*  is tx operation allowed by DMA
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* RETURNS
*  true if tx operation is allowed; false if tx is not allowed
*****************************************************************************/
bool hal_dma_is_tx_allow(P_MTK_DMA_INFO_STR p_dma_info);

/*****************************************************************************
* FUNCTION
*  hal_rx_dma_irq_handler
* DESCRIPTION
*  lower level rx interrupt handler
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* p_buf     [IN/OUT] pointer to rx data buffer
* max_len  [IN]        max length of rx buffer
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_rx_dma_irq_handler(P_MTK_DMA_INFO_STR p_dma_info,
			   unsigned char *p_buf, const unsigned int max_len);

/*****************************************************************************
* FUNCTION
*  hal_dma_dump_reg
* DESCRIPTION
*  dump BTIF module's information when needed
* PARAMETERS
* p_dma_info   [IN]        pointer to BTIF dma channel's information
* flag             [IN]        register id flag
* RETURNS
*  0 means success, negative means fail
*****************************************************************************/
int hal_dma_dump_reg(P_MTK_DMA_INFO_STR p_dma_info, ENUM_BTIF_REG_ID flag);

int hal_dma_pm_ops(P_MTK_DMA_INFO_STR p_dma_info, MTK_BTIF_PM_OPID opid);

#endif /*__HAL_BTIFD_DMA_PUB_H_*/
