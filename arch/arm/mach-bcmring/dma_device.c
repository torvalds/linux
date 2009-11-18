/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*   @file   dma_device.c
*
*   @brief  private array of DMA_DeviceAttribute_t
*/
/****************************************************************************/

DMA_DeviceAttribute_t DMA_gDeviceAttribute[DMA_NUM_DEVICE_ENTRIES] = {
	[DMA_DEVICE_MEM_TO_MEM] =	/* MEM 2 MEM */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "mem-to-mem",
	 .config = {
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_MEM,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,

		    },
	 },
	[DMA_DEVICE_VPM_MEM_TO_MEM] =	/* VPM */
	{
	 .flags = DMA_DEVICE_FLAG_IS_DEDICATED | DMA_DEVICE_FLAG_NO_ISR,
	 .name = "vpm",
	 .dedicatedController = 0,
	 .dedicatedChannel = 0,
	 /* reserve DMA0:0 for VPM */
	 },
	[DMA_DEVICE_NAND_MEM_TO_MEM] =	/* NAND */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "nand",
	 .config = {
		    .srcPeripheralPort = 0,
		    .dstPeripheralPort = 0,
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_6,
		    },
	 },
	[DMA_DEVICE_PIF_MEM_TO_DEV] =	/* PIF TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1
	 | DMA_DEVICE_FLAG_ALLOW_LARGE_FIFO
	 | DMA_DEVICE_FLAG_ALLOC_DMA1_FIRST | DMA_DEVICE_FLAG_PORT_PER_DMAC,
	 .name = "pif_tx",
	 .dmacPort = {14, 5},
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    /* dstPeripheralPort          = 5 or 14 */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    .maxDataPerBlock = 16256,
		    },
	 },
	[DMA_DEVICE_PIF_DEV_TO_MEM] =	/* PIF RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1
	 | DMA_DEVICE_FLAG_ALLOW_LARGE_FIFO
	 /* DMA_DEVICE_FLAG_ALLOC_DMA1_FIRST */
	 | DMA_DEVICE_FLAG_PORT_PER_DMAC,
	 .name = "pif_rx",
	 .dmacPort = {14, 5},
	 .config = {
		    /* srcPeripheralPort          = 5 or 14 */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    .maxDataPerBlock = 16256,
		    },
	 },
	[DMA_DEVICE_I2S0_DEV_TO_MEM] =	/* I2S RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "i2s0_rx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: I2S0 */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_16,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_0,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_I2S0_MEM_TO_DEV] =	/* I2S TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "i2s0_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 1,	/* DST: I2S0 */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_16,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_0,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_I2S1_DEV_TO_MEM] =	/* I2S1 RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "i2s1_rx",
	 .config = {
		    .srcPeripheralPort = 2,	/* SRC: I2S1 */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_16,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_0,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_I2S1_MEM_TO_DEV] =	/* I2S1 TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "i2s1_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 3,	/* DST: I2S1 */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_16,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_0,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_ESW_MEM_TO_DEV] =	/* ESW TX */
	{
	 .name = "esw_tx",
	 .flags = DMA_DEVICE_FLAG_IS_DEDICATED,
	 .dedicatedController = 1,
	 .dedicatedChannel = 3,
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 1,	/* DST: ESW (MTP) */
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_DISABLE,
		    /* DMAx_AHB_SSTATARy */
		    .srcStatusRegisterAddress = 0x00000000,
		    /* DMAx_AHB_DSTATARy */
		    .dstStatusRegisterAddress = 0x30490010,
		    /* DMAx_AHB_CFGy */
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    /* DMAx_AHB_CTLy */
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_0,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    },
	 },
	[DMA_DEVICE_ESW_DEV_TO_MEM] =	/* ESW RX */
	{
	 .name = "esw_rx",
	 .flags = DMA_DEVICE_FLAG_IS_DEDICATED,
	 .dedicatedController = 1,
	 .dedicatedChannel = 2,
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: ESW (PTM) */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_DISABLE,
		    /* DMAx_AHB_SSTATARy */
		    .srcStatusRegisterAddress = 0x30480010,
		    /* DMAx_AHB_DSTATARy */
		    .dstStatusRegisterAddress = 0x00000000,
		    /* DMAx_AHB_CFGy */
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    /* DMAx_AHB_CTLy */
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_0,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    },
	 },
	[DMA_DEVICE_APM_CODEC_A_DEV_TO_MEM] =	/* APM Codec A Ingress */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "apm_a_rx",
	 .config = {
		    .srcPeripheralPort = 2,	/* SRC: Codec A Ingress FIFO */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_APM_CODEC_A_MEM_TO_DEV] =	/* APM Codec A Egress */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "apm_a_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 3,	/* DST: Codec A Egress FIFO */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_APM_CODEC_B_DEV_TO_MEM] =	/* APM Codec B Ingress */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "apm_b_rx",
	 .config = {
		    .srcPeripheralPort = 4,	/* SRC: Codec B Ingress FIFO */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_APM_CODEC_B_MEM_TO_DEV] =	/* APM Codec B Egress */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "apm_b_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 5,	/* DST: Codec B Egress FIFO */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_APM_CODEC_C_DEV_TO_MEM] =	/* APM Codec C Ingress */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "apm_c_rx",
	 .config = {
		    .srcPeripheralPort = 4,	/* SRC: Codec C Ingress FIFO */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_APM_PCM0_DEV_TO_MEM] =	/* PCM0 RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "pcm0_rx",
	 .config = {
		    .srcPeripheralPort = 12,	/* SRC: PCM0 */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_APM_PCM0_MEM_TO_DEV] =	/* PCM0 TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0,
	 .name = "pcm0_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 13,	/* DST: PCM0 */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_APM_PCM1_DEV_TO_MEM] =	/* PCM1 RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "pcm1_rx",
	 .config = {
		    .srcPeripheralPort = 14,	/* SRC: PCM1 */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_4,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_CONTINUOUS,
		    },
	 },
	[DMA_DEVICE_APM_PCM1_MEM_TO_DEV] =	/* PCM1 TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "pcm1_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 15,	/* DST: PCM1 */
		    .srcStatusRegisterAddress = 0,
		    .dstStatusRegisterAddress = 0,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_4,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_SPUM_DEV_TO_MEM] =	/* SPUM RX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "spum_rx",
	 .config = {
		    .srcPeripheralPort = 6,	/* SRC: Codec A Ingress FIFO */
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_PERIPHERAL_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    /* Busrt size **MUST** be 16 for SPUM to work */
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_16,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_16,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    /* on the RX side, SPU needs to be the flow controller */
		    .flowControler = dmacHw_FLOW_CONTROL_PERIPHERAL,
		    },
	 },
	[DMA_DEVICE_SPUM_MEM_TO_DEV] =	/* SPUM TX */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "spum_tx",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .dstPeripheralPort = 7,	/* DST: SPUM */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_PERIPHERAL,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .blockTransferInterrupt = dmacHw_INTERRUPT_DISABLE,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_32,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_32,
		    /* Busrt size **MUST** be 16 for SPUM to work */
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_16,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_16,
		    .transferMode = dmacHw_TRANSFER_MODE_PERREQUEST,
		    },
	 },
	[DMA_DEVICE_MEM_TO_VRAM] =	/* MEM 2 VRAM */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "mem-to-vram",
	 .config = {
		    .srcPeripheralPort = 0,	/* SRC: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_1,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_2,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    },
	 },
	[DMA_DEVICE_VRAM_TO_MEM] =	/* VRAM 2 MEM */
	{
	 .flags = DMA_DEVICE_FLAG_ON_DMA0 | DMA_DEVICE_FLAG_ON_DMA1,
	 .name = "vram-to-mem",
	 .config = {
		    .dstPeripheralPort = 0,	/* DST: memory */
		    .srcStatusRegisterAddress = 0x00000000,
		    .dstStatusRegisterAddress = 0x00000000,
		    .srcUpdate = dmacHw_SRC_ADDRESS_UPDATE_MODE_INC,
		    .dstUpdate = dmacHw_DST_ADDRESS_UPDATE_MODE_INC,
		    .transferType = dmacHw_TRANSFER_TYPE_MEM_TO_MEM,
		    .srcMasterInterface = dmacHw_SRC_MASTER_INTERFACE_2,
		    .dstMasterInterface = dmacHw_DST_MASTER_INTERFACE_1,
		    .completeTransferInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .errorInterrupt = dmacHw_INTERRUPT_ENABLE,
		    .channelPriority = dmacHw_CHANNEL_PRIORITY_7,
		    .srcMaxTransactionWidth = dmacHw_SRC_TRANSACTION_WIDTH_64,
		    .dstMaxTransactionWidth = dmacHw_DST_TRANSACTION_WIDTH_64,
		    .srcMaxBurstWidth = dmacHw_SRC_BURST_WIDTH_8,
		    .dstMaxBurstWidth = dmacHw_DST_BURST_WIDTH_8,
		    },
	 },
};
EXPORT_SYMBOL(DMA_gDeviceAttribute);	/* primarily for dma-test.c */
