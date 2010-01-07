/***************************************************************************
 * Copyright (c) 1999-2009, Broadcom Corporation.
 *
 *  Name: bcm_70012_regs.h
 *
 *  Description: BCM70012 registers
 *
 ********************************************************************
 * This header is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License.
 *
 * This header is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public License
 * along with this header.  If not, see <http://www.gnu.org/licenses/>.
 ***************************************************************************/

#ifndef MACFILE_H__
#define MACFILE_H__

/**
 * m = memory, c = core, r = register, f = field, d = data.
 */
#if !defined(GET_FIELD) && !defined(SET_FIELD)
#define BRCM_ALIGN(c,r,f)   c##_##r##_##f##_ALIGN
#define BRCM_BITS(c,r,f)    c##_##r##_##f##_BITS
#define BRCM_MASK(c,r,f)    c##_##r##_##f##_MASK
#define BRCM_SHIFT(c,r,f)   c##_##r##_##f##_SHIFT

#define GET_FIELD(m,c,r,f) \
	((((m) & BRCM_MASK(c,r,f)) >> BRCM_SHIFT(c,r,f)) << BRCM_ALIGN(c,r,f))

#define SET_FIELD(m,c,r,f,d) \
	((m) = (((m) & ~BRCM_MASK(c,r,f)) | ((((d) >> BRCM_ALIGN(c,r,f)) << \
	 BRCM_SHIFT(c,r,f)) & BRCM_MASK(c,r,f))) \
	)

#define SET_TYPE_FIELD(m,c,r,f,d) SET_FIELD(m,c,r,f,c##_##d)
#define SET_NAME_FIELD(m,c,r,f,d) SET_FIELD(m,c,r,f,c##_##r##_##f##_##d)
#define SET_VALUE_FIELD(m,c,r,f,d) SET_FIELD(m,c,r,f,d)

#endif /* GET & SET */

/****************************************************************************
 * Core Enums.
 ***************************************************************************/
/****************************************************************************
 * Enums: AES_RGR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define AES_RGR_BRIDGE_RESET_CTRL_DEASSERT                 0
#define AES_RGR_BRIDGE_RESET_CTRL_ASSERT                   1

/****************************************************************************
 * Enums: CCE_RGR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define CCE_RGR_BRIDGE_RESET_CTRL_DEASSERT                 0
#define CCE_RGR_BRIDGE_RESET_CTRL_ASSERT                   1

/****************************************************************************
 * Enums: DBU_RGR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define DBU_RGR_BRIDGE_RESET_CTRL_DEASSERT                 0
#define DBU_RGR_BRIDGE_RESET_CTRL_ASSERT                   1

/****************************************************************************
 * Enums: DCI_RGR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define DCI_RGR_BRIDGE_RESET_CTRL_DEASSERT                 0
#define DCI_RGR_BRIDGE_RESET_CTRL_ASSERT                   1

/****************************************************************************
 * Enums: GISB_ARBITER_DEASSERT_ASSERT
 ***************************************************************************/
#define GISB_ARBITER_DEASSERT_ASSERT_DEASSERT              0
#define GISB_ARBITER_DEASSERT_ASSERT_ASSERT                1

/****************************************************************************
 * Enums: GISB_ARBITER_UNMASK_MASK
 ***************************************************************************/
#define GISB_ARBITER_UNMASK_MASK_UNMASK                    0
#define GISB_ARBITER_UNMASK_MASK_MASK                      1

/****************************************************************************
 * Enums: GISB_ARBITER_DISABLE_ENABLE
 ***************************************************************************/
#define GISB_ARBITER_DISABLE_ENABLE_DISABLE                0
#define GISB_ARBITER_DISABLE_ENABLE_ENABLE                 1

/****************************************************************************
 * Enums: I2C_GR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define I2C_GR_BRIDGE_RESET_CTRL_DEASSERT                  0
#define I2C_GR_BRIDGE_RESET_CTRL_ASSERT                    1

/****************************************************************************
 * Enums: MISC_GR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define MISC_GR_BRIDGE_RESET_CTRL_DEASSERT                 0
#define MISC_GR_BRIDGE_RESET_CTRL_ASSERT                   1

/****************************************************************************
 * Enums: OTP_GR_BRIDGE_RESET_CTRL
 ***************************************************************************/
#define OTP_GR_BRIDGE_RESET_CTRL_DEASSERT                  0
#define OTP_GR_BRIDGE_RESET_CTRL_ASSERT                    1

/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_CFG
 ***************************************************************************/
#define PCIE_CFG_DEVICE_VENDOR_ID      0x00000000 /* DEVICE_VENDOR_ID Register */
#define PCIE_CFG_STATUS_COMMAND        0x00000004 /* STATUS_COMMAND Register */
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID 0x00000008 /* PCI_CLASSCODE_AND_REVISION_ID Register */
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE 0x0000000c /* BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE Register */
#define PCIE_CFG_BASE_ADDRESS_1        0x00000010 /* BASE_ADDRESS_1 Register */
#define PCIE_CFG_BASE_ADDRESS_2        0x00000014 /* BASE_ADDRESS_2 Register */
#define PCIE_CFG_BASE_ADDRESS_3        0x00000018 /* BASE_ADDRESS_3 Register */
#define PCIE_CFG_BASE_ADDRESS_4        0x0000001c /* BASE_ADDRESS_4 Register */
#define PCIE_CFG_CARDBUS_CIS_POINTER   0x00000028 /* CARDBUS_CIS_POINTER Register */
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID 0x0000002c /* SUBSYSTEM_DEVICE_VENDOR_ID Register */
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS 0x00000030 /* EXPANSION_ROM_BASE_ADDRESS Register */
#define PCIE_CFG_CAPABILITIES_POINTER  0x00000034 /* CAPABILITIES_POINTER Register */
#define PCIE_CFG_INTERRUPT             0x0000003c /* INTERRUPT Register */
#define PCIE_CFG_VPD_CAPABILITIES      0x00000040 /* VPD_CAPABILITIES Register */
#define PCIE_CFG_VPD_DATA              0x00000044 /* VPD_DATA Register */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY 0x00000048 /* POWER_MANAGEMENT_CAPABILITY Register */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS 0x0000004c /* POWER_MANAGEMENT_CONTROL_STATUS Register */
#define PCIE_CFG_MSI_CAPABILITY_HEADER 0x00000050 /* MSI_CAPABILITY_HEADER Register */
#define PCIE_CFG_MSI_LOWER_ADDRESS     0x00000054 /* MSI_LOWER_ADDRESS Register */
#define PCIE_CFG_MSI_UPPER_ADDRESS_REGISTER 0x00000058 /* MSI_UPPER_ADDRESS_REGISTER Register */
#define PCIE_CFG_MSI_DATA              0x0000005c /* MSI_DATA Register */
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER 0x00000060 /* BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER Register */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES 0x00000064 /* RESET_COUNTERS_INITIAL_VALUES Register */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL 0x00000068 /* MISCELLANEOUS_HOST_CONTROL Register */
#define PCIE_CFG_SPARE                 0x0000006c /* SPARE Register */
#define PCIE_CFG_PCI_STATE             0x00000070 /* PCI_STATE Register */
#define PCIE_CFG_CLOCK_CONTROL         0x00000074 /* CLOCK_CONTROL Register */
#define PCIE_CFG_REGISTER_BASE         0x00000078 /* REGISTER_BASE Register */
#define PCIE_CFG_MEMORY_BASE           0x0000007c /* MEMORY_BASE Register */
#define PCIE_CFG_REGISTER_DATA         0x00000080 /* REGISTER_DATA Register */
#define PCIE_CFG_MEMORY_DATA           0x00000084 /* MEMORY_DATA Register */
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE 0x00000088 /* EXPANSION_ROM_BAR_SIZE Register */
#define PCIE_CFG_EXPANSION_ROM_ADDRESS 0x0000008c /* EXPANSION_ROM_ADDRESS Register */
#define PCIE_CFG_EXPANSION_ROM_DATA    0x00000090 /* EXPANSION_ROM_DATA Register */
#define PCIE_CFG_VPD_INTERFACE         0x00000094 /* VPD_INTERFACE Register */
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER 0x00000098 /* UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER Register */
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER 0x0000009c /* UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER Register */
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER 0x000000a0 /* UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER Register */
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER 0x000000a4 /* UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER Register */
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER 0x000000a8 /* UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER Register */
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER 0x000000ac /* UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER Register */
#define PCIE_CFG_INT_MAILBOX_UPPER     0x000000b0 /* INT_MAILBOX_UPPER Register */
#define PCIE_CFG_INT_MAILBOX_LOWER     0x000000b4 /* INT_MAILBOX_LOWER Register */
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION 0x000000bc /* PRODUCT_ID_AND_ASIC_REVISION Register */
#define PCIE_CFG_FUNCTION_EVENT        0x000000c0 /* FUNCTION_EVENT Register */
#define PCIE_CFG_FUNCTION_EVENT_MASK   0x000000c4 /* FUNCTION_EVENT_MASK Register */
#define PCIE_CFG_FUNCTION_PRESENT      0x000000c8 /* FUNCTION_PRESENT Register */
#define PCIE_CFG_PCIE_CAPABILITIES     0x000000cc /* PCIE_CAPABILITIES Register */
#define PCIE_CFG_DEVICE_CAPABILITIES   0x000000d0 /* DEVICE_CAPABILITIES Register */
#define PCIE_CFG_DEVICE_STATUS_CONTROL 0x000000d4 /* DEVICE_STATUS_CONTROL Register */
#define PCIE_CFG_LINK_CAPABILITY       0x000000d8 /* LINK_CAPABILITY Register */
#define PCIE_CFG_LINK_STATUS_CONTROL   0x000000dc /* LINK_STATUS_CONTROL Register */
#define PCIE_CFG_DEVICE_CAPABILITIES_2 0x000000f0 /* DEVICE_CAPABILITIES_2 Register */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2 0x000000f4 /* DEVICE_STATUS_CONTROL_2 Register */
#define PCIE_CFG_LINK_CAPABILITIES_2   0x000000f8 /* LINK_CAPABILITIES_2 Register */
#define PCIE_CFG_LINK_STATUS_CONTROL_2 0x000000fc /* LINK_STATUS_CONTROL_2 Register */
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER 0x00000100 /* ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER Register */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS 0x00000104 /* UNCORRECTABLE_ERROR_STATUS Register */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK 0x00000108 /* UNCORRECTABLE_ERROR_MASK Register */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY 0x0000010c /* UNCORRECTABLE_ERROR_SEVERITY Register */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS 0x00000110 /* CORRECTABLE_ERROR_STATUS Register */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK 0x00000114 /* CORRECTABLE_ERROR_MASK Register */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL 0x00000118 /* ADVANCED_ERROR_CAPABILITIES_AND_CONTROL Register */
#define PCIE_CFG_HEADER_LOG_1          0x0000011c /* HEADER_LOG_1 Register */
#define PCIE_CFG_HEADER_LOG_2          0x00000120 /* HEADER_LOG_2 Register */
#define PCIE_CFG_HEADER_LOG_3          0x00000124 /* HEADER_LOG_3 Register */
#define PCIE_CFG_HEADER_LOG_4          0x00000128 /* HEADER_LOG_4 Register */
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER 0x0000013c /* VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER Register */
#define PCIE_CFG_PORT_VC_CAPABILITY    0x00000140 /* PORT_VC_CAPABILITY Register */
#define PCIE_CFG_PORT_VC_CAPABILITY_2  0x00000144 /* PORT_VC_CAPABILITY_2 Register */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL 0x00000148 /* PORT_VC_STATUS_CONTROL Register */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY 0x0000014c /* VC_RESOURCE_CAPABILITY Register */
#define PCIE_CFG_VC_RESOURCE_CONTROL   0x00000150 /* VC_RESOURCE_CONTROL Register */
#define PCIE_CFG_VC_RESOURCE_STATUS    0x00000154 /* VC_RESOURCE_STATUS Register */
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER 0x00000160 /* DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER Register */
#define PCIE_CFG_DEVICE_SERIAL_NO_LOWER_DW 0x00000164 /* DEVICE_SERIAL_NO_LOWER_DW Register */
#define PCIE_CFG_DEVICE_SERIAL_NO_UPPER_DW 0x00000168 /* DEVICE_SERIAL_NO_UPPER_DW Register */
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER 0x0000016c /* POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER Register */
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT 0x00000170 /* POWER_BUDGETING_DATA_SELECT Register */
#define PCIE_CFG_POWER_BUDGETING_DATA  0x00000174 /* POWER_BUDGETING_DATA Register */
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY 0x00000178 /* POWER_BUDGETING_CAPABILITY Register */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1 0x0000017c /* FIRMWARE_POWER_BUDGETING_2_1 Register */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3 0x00000180 /* FIRMWARE_POWER_BUDGETING_4_3 Register */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5 0x00000184 /* FIRMWARE_POWER_BUDGETING_6_5 Register */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7 0x00000188 /* FIRMWARE_POWER_BUDGETING_8_7 Register */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING 0x0000018c /* PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING Register */


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_TL
 ***************************************************************************/
#define PCIE_TL_TL_CONTROL             0x00000400 /* TL_CONTROL Register */
#define PCIE_TL_TRANSACTION_CONFIGURATION 0x00000404 /* TRANSACTION_CONFIGURATION Register */


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_DLL
 ***************************************************************************/
#define PCIE_DLL_DATA_LINK_CONTROL     0x00000500 /* DATA_LINK_CONTROL Register */
#define PCIE_DLL_DATA_LINK_STATUS      0x00000504 /* DATA_LINK_STATUS Register */


/****************************************************************************
 * BCM70012_TGT_TOP_INTR
 ***************************************************************************/
#define INTR_INTR_STATUS               0x00000700 /* Interrupt Status Register */
#define INTR_INTR_SET                  0x00000704 /* Interrupt Set Register */
#define INTR_INTR_CLR_REG              0x00000708 /* Interrupt Clear Register */
#define INTR_INTR_MSK_STS_REG          0x0000070c /* Interrupt Mask Status Register */
#define INTR_INTR_MSK_SET_REG          0x00000710 /* Interrupt Mask Set Register */
#define INTR_INTR_MSK_CLR_REG          0x00000714 /* Interrupt Mask Clear Register */
#define INTR_EOI_CTRL                  0x00000720 /* End of interrupt control register */


/****************************************************************************
 * BCM70012_MISC_TOP_MISC1
 ***************************************************************************/
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0 0x00000c00 /* Tx DMA Descriptor List0 First Descriptor lower Address */
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST0 0x00000c04 /* Tx DMA Descriptor List0 First Descriptor Upper Address */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1 0x00000c08 /* Tx DMA Descriptor List1 First Descriptor Lower Address */
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST1 0x00000c0c /* Tx DMA Descriptor List1 First Descriptor Upper Address */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS 0x00000c10 /* Tx DMA Software Descriptor List Control and Status */
#define MISC1_TX_DMA_ERROR_STATUS      0x00000c18 /* Tx DMA Engine Error Status */
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR 0x00000c1c /* Tx DMA List0 Current Descriptor Lower Address */
#define MISC1_TX_DMA_LIST0_CUR_DESC_U_ADDR 0x00000c20 /* Tx DMA List0 Current Descriptor Upper Address */
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM 0x00000c24 /* Tx DMA List0 Current Descriptor Upper Address */
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR 0x00000c28 /* Tx DMA List1 Current Descriptor Lower Address */
#define MISC1_TX_DMA_LIST1_CUR_DESC_U_ADDR 0x00000c2c /* Tx DMA List1 Current Descriptor Upper Address */
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM 0x00000c30 /* Tx DMA List1 Current Descriptor Upper Address */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0 0x00000c34 /* Y Rx Descriptor List0 First Descriptor Lower Address */
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0 0x00000c38 /* Y Rx Descriptor List0 First Descriptor Upper Address */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1 0x00000c3c /* Y Rx Descriptor List1 First Descriptor Lower Address */
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1 0x00000c40 /* Y Rx Descriptor List1 First Descriptor Upper Address */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS 0x00000c44 /* Y Rx Software Descriptor List Control and Status */
#define MISC1_Y_RX_ERROR_STATUS        0x00000c4c /* Y Rx Engine Error Status */
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR 0x00000c50 /* Y Rx List0 Current Descriptor Lower Address */
#define MISC1_Y_RX_LIST0_CUR_DESC_U_ADDR 0x00000c54 /* Y Rx List0 Current Descriptor Upper Address */
#define MISC1_Y_RX_LIST0_CUR_BYTE_CNT  0x00000c58 /* Y Rx List0 Current Descriptor Byte Count */
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR 0x00000c5c /* Y Rx List1 Current Descriptor Lower address */
#define MISC1_Y_RX_LIST1_CUR_DESC_U_ADDR 0x00000c60 /* Y Rx List1 Current Descriptor Upper address */
#define MISC1_Y_RX_LIST1_CUR_BYTE_CNT  0x00000c64 /* Y Rx List1 Current Descriptor Byte Count */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0 0x00000c68 /* UV Rx Descriptor List0 First Descriptor lower Address */
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0 0x00000c6c /* UV Rx Descriptor List0 First Descriptor Upper Address */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1 0x00000c70 /* UV Rx Descriptor List1 First Descriptor Lower Address */
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1 0x00000c74 /* UV Rx Descriptor List1 First Descriptor Upper Address */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS 0x00000c78 /* UV Rx Software Descriptor List Control and Status */
#define MISC1_UV_RX_ERROR_STATUS       0x00000c7c /* UV Rx Engine Error Status */
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR 0x00000c80 /* UV Rx List0 Current Descriptor Lower Address */
#define MISC1_UV_RX_LIST0_CUR_DESC_U_ADDR 0x00000c84 /* UV Rx List0 Current Descriptor Upper Address */
#define MISC1_UV_RX_LIST0_CUR_BYTE_CNT 0x00000c88 /* UV Rx List0 Current Descriptor Byte Count */
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR 0x00000c8c /* UV Rx List1 Current Descriptor Lower Address */
#define MISC1_UV_RX_LIST1_CUR_DESC_U_ADDR 0x00000c90 /* UV Rx List1 Current Descriptor Upper Address */
#define MISC1_UV_RX_LIST1_CUR_BYTE_CNT 0x00000c94 /* UV Rx List1 Current Descriptor Byte Count */
#define MISC1_DMA_DEBUG_OPTIONS_REG    0x00000c98 /* DMA Debug Options Register */
#define MISC1_READ_CHANNEL_ERROR_STATUS 0x00000c9c /* Read Channel Error Status */
#define MISC1_PCIE_DMA_CTRL            0x00000ca0 /* PCIE DMA Control Register */


/****************************************************************************
 * BCM70012_MISC_TOP_MISC2
 ***************************************************************************/
#define MISC2_GLOBAL_CTRL              0x00000d00 /* Global Control Register */
#define MISC2_INTERNAL_STATUS          0x00000d04 /* Internal Status Register */
#define MISC2_INTERNAL_STATUS_MUX_CTRL 0x00000d08 /* Internal Debug Mux Control */
#define MISC2_DEBUG_FIFO_LENGTH        0x00000d0c /* Debug FIFO Length */


/****************************************************************************
 * BCM70012_MISC_TOP_MISC3
 ***************************************************************************/
#define MISC3_RESET_CTRL               0x00000e00 /* Reset Control Register */
#define MISC3_BIST_CTRL                0x00000e04 /* BIST Control Register */
#define MISC3_BIST_STATUS              0x00000e08 /* BIST Status Register */
#define MISC3_RX_CHECKSUM              0x00000e0c /* Receive Checksum */
#define MISC3_TX_CHECKSUM              0x00000e10 /* Transmit Checksum */
#define MISC3_ECO_CTRL_CORE            0x00000e14 /* ECO Core Reset Control Register */
#define MISC3_CSI_TEST_CTRL            0x00000e18 /* CSI Test Control Register */
#define MISC3_HD_DVI_TEST_CTRL         0x00000e1c /* HD DVI Test Control Register */


/****************************************************************************
 * BCM70012_MISC_TOP_MISC_PERST
 ***************************************************************************/
#define MISC_PERST_ECO_CTRL_PERST      0x00000e80 /* ECO PCIE Reset Control Register */
#define MISC_PERST_DECODER_CTRL        0x00000e84 /* Decoder Control Register */
#define MISC_PERST_CCE_STATUS          0x00000e88 /* Config Copy Engine Status */
#define MISC_PERST_PCIE_DEBUG          0x00000e8c /* PCIE Debug Control Register */
#define MISC_PERST_PCIE_DEBUG_STATUS   0x00000e90 /* PCIE Debug Status Register */
#define MISC_PERST_VREG_CTRL           0x00000e94 /* Voltage Regulator Control Register */
#define MISC_PERST_MEM_CTRL            0x00000e98 /* Memory Control Register */
#define MISC_PERST_CLOCK_CTRL          0x00000e9c /* Clock Control Register */


/****************************************************************************
 * BCM70012_MISC_TOP_GISB_ARBITER
 ***************************************************************************/
#define GISB_ARBITER_REVISION          0x00000f00 /* GISB ARBITER REVISION */
#define GISB_ARBITER_SCRATCH           0x00000f04 /* GISB ARBITER Scratch Register */
#define GISB_ARBITER_REQ_MASK          0x00000f08 /* GISB ARBITER Master Request Mask Register */
#define GISB_ARBITER_TIMER             0x00000f0c /* GISB ARBITER Timer Value Register */


/****************************************************************************
 * BCM70012_OTP_TOP_OTP
 ***************************************************************************/
#define OTP_CONFIG_INFO                0x00001400 /* OTP Configuration Register */
#define OTP_CMD                        0x00001404 /* OTP Command Register */
#define OTP_STATUS                     0x00001408 /* OTP Status Register */
#define OTP_CONTENT_MISC               0x0000140c /* Content : Miscellaneous Register */
#define OTP_CONTENT_AES_0              0x00001410 /* Content : AES Key 0 Register */
#define OTP_CONTENT_AES_1              0x00001414 /* Content : AES Key 1 Register */
#define OTP_CONTENT_AES_2              0x00001418 /* Content : AES Key 2 Register */
#define OTP_CONTENT_AES_3              0x0000141c /* Content : AES Key 3 Register */
#define OTP_CONTENT_SHA_0              0x00001420 /* Content : SHA Key 0 Register */
#define OTP_CONTENT_SHA_1              0x00001424 /* Content : SHA Key 1 Register */
#define OTP_CONTENT_SHA_2              0x00001428 /* Content : SHA Key 2 Register */
#define OTP_CONTENT_SHA_3              0x0000142c /* Content : SHA Key 3 Register */
#define OTP_CONTENT_SHA_4              0x00001430 /* Content : SHA Key 4 Register */
#define OTP_CONTENT_SHA_5              0x00001434 /* Content : SHA Key 5 Register */
#define OTP_CONTENT_SHA_6              0x00001438 /* Content : SHA Key 6 Register */
#define OTP_CONTENT_SHA_7              0x0000143c /* Content : SHA Key 7 Register */
#define OTP_CONTENT_CHECKSUM           0x00001440 /* Content : Checksum  Register */
#define OTP_PROG_CTRL                  0x00001444 /* Programming Control Register */
#define OTP_PROG_STATUS                0x00001448 /* Programming Status Register */
#define OTP_PROG_PULSE                 0x0000144c /* Program Pulse Width Register */
#define OTP_VERIFY_PULSE               0x00001450 /* Verify Pulse Width Register */
#define OTP_PROG_MASK                  0x00001454 /* Program Mask Register */
#define OTP_DATA_INPUT                 0x00001458 /* Data Input Register */
#define OTP_DATA_OUTPUT                0x0000145c /* Data Output Register */


/****************************************************************************
 * BCM70012_AES_TOP_AES
 ***************************************************************************/
#define AES_CONFIG_INFO                0x00001800 /* AES Configuration Information Register */
#define AES_CMD                        0x00001804 /* AES Command Register */
#define AES_STATUS                     0x00001808 /* AES Status Register */
#define AES_EEPROM_CONFIG              0x0000180c /* AES EEPROM Configuration Register */
#define AES_EEPROM_DATA_0              0x00001810 /* AES EEPROM Data Register 0 */
#define AES_EEPROM_DATA_1              0x00001814 /* AES EEPROM Data Register 1 */
#define AES_EEPROM_DATA_2              0x00001818 /* AES EEPROM Data Register 2 */
#define AES_EEPROM_DATA_3              0x0000181c /* AES EEPROM Data Register 3 */


/****************************************************************************
 * BCM70012_DCI_TOP_DCI
 ***************************************************************************/
#define DCI_CMD                        0x00001c00 /* DCI Command Register */
#define DCI_STATUS                     0x00001c04 /* DCI Status Register */
#define DCI_DRAM_BASE_ADDR             0x00001c08 /* DRAM Base Address Register */
#define DCI_FIRMWARE_ADDR              0x00001c0c /* Firmware Address Register */
#define DCI_FIRMWARE_DATA              0x00001c10 /* Firmware Data Register */
#define DCI_SIGNATURE_DATA_0           0x00001c14 /* Signature Data Register 0 */
#define DCI_SIGNATURE_DATA_1           0x00001c18 /* Signature Data Register 1 */
#define DCI_SIGNATURE_DATA_2           0x00001c1c /* Signature Data Register 2 */
#define DCI_SIGNATURE_DATA_3           0x00001c20 /* Signature Data Register 3 */
#define DCI_SIGNATURE_DATA_4           0x00001c24 /* Signature Data Register 4 */
#define DCI_SIGNATURE_DATA_5           0x00001c28 /* Signature Data Register 5 */
#define DCI_SIGNATURE_DATA_6           0x00001c2c /* Signature Data Register 6 */
#define DCI_SIGNATURE_DATA_7           0x00001c30 /* Signature Data Register 7 */


/****************************************************************************
 * BCM70012_TGT_TOP_INTR
 ***************************************************************************/
/****************************************************************************
 * INTR :: INTR_STATUS
 ***************************************************************************/
/* INTR :: INTR_STATUS :: reserved0 [31:26] */
#define INTR_INTR_STATUS_reserved0_MASK                            0xfc000000
#define INTR_INTR_STATUS_reserved0_ALIGN                           0
#define INTR_INTR_STATUS_reserved0_BITS                            6
#define INTR_INTR_STATUS_reserved0_SHIFT                           26

/* INTR :: INTR_STATUS :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_STATUS_PCIE_TGT_CA_ATTN_MASK                     0x02000000
#define INTR_INTR_STATUS_PCIE_TGT_CA_ATTN_ALIGN                    0
#define INTR_INTR_STATUS_PCIE_TGT_CA_ATTN_BITS                     1
#define INTR_INTR_STATUS_PCIE_TGT_CA_ATTN_SHIFT                    25

/* INTR :: INTR_STATUS :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_STATUS_PCIE_TGT_UR_ATTN_MASK                     0x01000000
#define INTR_INTR_STATUS_PCIE_TGT_UR_ATTN_ALIGN                    0
#define INTR_INTR_STATUS_PCIE_TGT_UR_ATTN_BITS                     1
#define INTR_INTR_STATUS_PCIE_TGT_UR_ATTN_SHIFT                    24

/* INTR :: INTR_STATUS :: reserved1 [23:14] */
#define INTR_INTR_STATUS_reserved1_MASK                            0x00ffc000
#define INTR_INTR_STATUS_reserved1_ALIGN                           0
#define INTR_INTR_STATUS_reserved1_BITS                            10
#define INTR_INTR_STATUS_reserved1_SHIFT                           14

/* INTR :: INTR_STATUS :: L1_UV_RX_DMA_ERR_INTR [13:13] */
#define INTR_INTR_STATUS_L1_UV_RX_DMA_ERR_INTR_MASK                0x00002000
#define INTR_INTR_STATUS_L1_UV_RX_DMA_ERR_INTR_ALIGN               0
#define INTR_INTR_STATUS_L1_UV_RX_DMA_ERR_INTR_BITS                1
#define INTR_INTR_STATUS_L1_UV_RX_DMA_ERR_INTR_SHIFT               13

/* INTR :: INTR_STATUS :: L1_UV_RX_DMA_DONE_INTR [12:12] */
#define INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_MASK               0x00001000
#define INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_ALIGN              0
#define INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_BITS               1
#define INTR_INTR_STATUS_L1_UV_RX_DMA_DONE_INTR_SHIFT              12

/* INTR :: INTR_STATUS :: L1_Y_RX_DMA_ERR_INTR [11:11] */
#define INTR_INTR_STATUS_L1_Y_RX_DMA_ERR_INTR_MASK                 0x00000800
#define INTR_INTR_STATUS_L1_Y_RX_DMA_ERR_INTR_ALIGN                0
#define INTR_INTR_STATUS_L1_Y_RX_DMA_ERR_INTR_BITS                 1
#define INTR_INTR_STATUS_L1_Y_RX_DMA_ERR_INTR_SHIFT                11

/* INTR :: INTR_STATUS :: L1_Y_RX_DMA_DONE_INTR [10:10] */
#define INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_MASK                0x00000400
#define INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_ALIGN               0
#define INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_BITS                1
#define INTR_INTR_STATUS_L1_Y_RX_DMA_DONE_INTR_SHIFT               10

/* INTR :: INTR_STATUS :: L1_TX_DMA_ERR_INTR [09:09] */
#define INTR_INTR_STATUS_L1_TX_DMA_ERR_INTR_MASK                   0x00000200
#define INTR_INTR_STATUS_L1_TX_DMA_ERR_INTR_ALIGN                  0
#define INTR_INTR_STATUS_L1_TX_DMA_ERR_INTR_BITS                   1
#define INTR_INTR_STATUS_L1_TX_DMA_ERR_INTR_SHIFT                  9

/* INTR :: INTR_STATUS :: L1_TX_DMA_DONE_INTR [08:08] */
#define INTR_INTR_STATUS_L1_TX_DMA_DONE_INTR_MASK                  0x00000100
#define INTR_INTR_STATUS_L1_TX_DMA_DONE_INTR_ALIGN                 0
#define INTR_INTR_STATUS_L1_TX_DMA_DONE_INTR_BITS                  1
#define INTR_INTR_STATUS_L1_TX_DMA_DONE_INTR_SHIFT                 8

/* INTR :: INTR_STATUS :: reserved2 [07:06] */
#define INTR_INTR_STATUS_reserved2_MASK                            0x000000c0
#define INTR_INTR_STATUS_reserved2_ALIGN                           0
#define INTR_INTR_STATUS_reserved2_BITS                            2
#define INTR_INTR_STATUS_reserved2_SHIFT                           6

/* INTR :: INTR_STATUS :: L0_UV_RX_DMA_ERR_INTR [05:05] */
#define INTR_INTR_STATUS_L0_UV_RX_DMA_ERR_INTR_MASK                0x00000020
#define INTR_INTR_STATUS_L0_UV_RX_DMA_ERR_INTR_ALIGN               0
#define INTR_INTR_STATUS_L0_UV_RX_DMA_ERR_INTR_BITS                1
#define INTR_INTR_STATUS_L0_UV_RX_DMA_ERR_INTR_SHIFT               5

/* INTR :: INTR_STATUS :: L0_UV_RX_DMA_DONE_INTR [04:04] */
#define INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_MASK               0x00000010
#define INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_ALIGN              0
#define INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_BITS               1
#define INTR_INTR_STATUS_L0_UV_RX_DMA_DONE_INTR_SHIFT              4

/* INTR :: INTR_STATUS :: L0_Y_RX_DMA_ERR_INTR [03:03] */
#define INTR_INTR_STATUS_L0_Y_RX_DMA_ERR_INTR_MASK                 0x00000008
#define INTR_INTR_STATUS_L0_Y_RX_DMA_ERR_INTR_ALIGN                0
#define INTR_INTR_STATUS_L0_Y_RX_DMA_ERR_INTR_BITS                 1
#define INTR_INTR_STATUS_L0_Y_RX_DMA_ERR_INTR_SHIFT                3

/* INTR :: INTR_STATUS :: L0_Y_RX_DMA_DONE_INTR [02:02] */
#define INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_MASK                0x00000004
#define INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_ALIGN               0
#define INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_BITS                1
#define INTR_INTR_STATUS_L0_Y_RX_DMA_DONE_INTR_SHIFT               2

/* INTR :: INTR_STATUS :: L0_TX_DMA_ERR_INTR [01:01] */
#define INTR_INTR_STATUS_L0_TX_DMA_ERR_INTR_MASK                   0x00000002
#define INTR_INTR_STATUS_L0_TX_DMA_ERR_INTR_ALIGN                  0
#define INTR_INTR_STATUS_L0_TX_DMA_ERR_INTR_BITS                   1
#define INTR_INTR_STATUS_L0_TX_DMA_ERR_INTR_SHIFT                  1

/* INTR :: INTR_STATUS :: L0_TX_DMA_DONE_INTR [00:00] */
#define INTR_INTR_STATUS_L0_TX_DMA_DONE_INTR_MASK                  0x00000001
#define INTR_INTR_STATUS_L0_TX_DMA_DONE_INTR_ALIGN                 0
#define INTR_INTR_STATUS_L0_TX_DMA_DONE_INTR_BITS                  1
#define INTR_INTR_STATUS_L0_TX_DMA_DONE_INTR_SHIFT                 0


/****************************************************************************
 * MISC1 :: TX_SW_DESC_LIST_CTRL_STS
 ***************************************************************************/
/* MISC1 :: TX_SW_DESC_LIST_CTRL_STS :: reserved0 [31:04] */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_reserved0_MASK              0xfffffff0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_reserved0_ALIGN             0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_reserved0_BITS              28
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_reserved0_SHIFT             4

/* MISC1 :: TX_SW_DESC_LIST_CTRL_STS :: DMA_DATA_SERV_PTR [03:03] */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_MASK      0x00000008
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_ALIGN     0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_BITS      1
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_SHIFT     3

/* MISC1 :: TX_SW_DESC_LIST_CTRL_STS :: DESC_SERV_PTR [02:02] */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_MASK          0x00000004
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_ALIGN         0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_BITS          1
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_SHIFT         2

/* MISC1 :: TX_SW_DESC_LIST_CTRL_STS :: TX_DMA_HALT_ON_ERROR [01:01] */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_HALT_ON_ERROR_MASK   0x00000002
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_HALT_ON_ERROR_ALIGN  0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_HALT_ON_ERROR_BITS   1
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_HALT_ON_ERROR_SHIFT  1

/* MISC1 :: TX_SW_DESC_LIST_CTRL_STS :: TX_DMA_RUN_STOP [00:00] */
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_RUN_STOP_MASK        0x00000001
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_RUN_STOP_ALIGN       0
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_RUN_STOP_BITS        1
#define MISC1_TX_SW_DESC_LIST_CTRL_STS_TX_DMA_RUN_STOP_SHIFT       0


/****************************************************************************
 * MISC1 :: TX_DMA_ERROR_STATUS
 ***************************************************************************/
/* MISC1 :: TX_DMA_ERROR_STATUS :: reserved0 [31:10] */
#define MISC1_TX_DMA_ERROR_STATUS_reserved0_MASK                   0xfffffc00
#define MISC1_TX_DMA_ERROR_STATUS_reserved0_ALIGN                  0
#define MISC1_TX_DMA_ERROR_STATUS_reserved0_BITS                   22
#define MISC1_TX_DMA_ERROR_STATUS_reserved0_SHIFT                  10

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L1_DESC_TX_ABORT_ERRORS [09:09] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DESC_TX_ABORT_ERRORS_MASK  0x00000200
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DESC_TX_ABORT_ERRORS_ALIGN 0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DESC_TX_ABORT_ERRORS_BITS  1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DESC_TX_ABORT_ERRORS_SHIFT 9

/* MISC1 :: TX_DMA_ERROR_STATUS :: reserved1 [08:08] */
#define MISC1_TX_DMA_ERROR_STATUS_reserved1_MASK                   0x00000100
#define MISC1_TX_DMA_ERROR_STATUS_reserved1_ALIGN                  0
#define MISC1_TX_DMA_ERROR_STATUS_reserved1_BITS                   1
#define MISC1_TX_DMA_ERROR_STATUS_reserved1_SHIFT                  8

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L0_DESC_TX_ABORT_ERRORS [07:07] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DESC_TX_ABORT_ERRORS_MASK  0x00000080
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DESC_TX_ABORT_ERRORS_ALIGN 0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DESC_TX_ABORT_ERRORS_BITS  1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DESC_TX_ABORT_ERRORS_SHIFT 7

/* MISC1 :: TX_DMA_ERROR_STATUS :: reserved2 [06:06] */
#define MISC1_TX_DMA_ERROR_STATUS_reserved2_MASK                   0x00000040
#define MISC1_TX_DMA_ERROR_STATUS_reserved2_ALIGN                  0
#define MISC1_TX_DMA_ERROR_STATUS_reserved2_BITS                   1
#define MISC1_TX_DMA_ERROR_STATUS_reserved2_SHIFT                  6

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L1_DMA_DATA_TX_ABORT_ERRORS [05:05] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DMA_DATA_TX_ABORT_ERRORS_MASK 0x00000020
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DMA_DATA_TX_ABORT_ERRORS_ALIGN 0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DMA_DATA_TX_ABORT_ERRORS_BITS 1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_DMA_DATA_TX_ABORT_ERRORS_SHIFT 5

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L1_FIFO_FULL_ERRORS [04:04] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_MASK      0x00000010
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_ALIGN     0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_BITS      1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L1_FIFO_FULL_ERRORS_SHIFT     4

/* MISC1 :: TX_DMA_ERROR_STATUS :: reserved3 [03:03] */
#define MISC1_TX_DMA_ERROR_STATUS_reserved3_MASK                   0x00000008
#define MISC1_TX_DMA_ERROR_STATUS_reserved3_ALIGN                  0
#define MISC1_TX_DMA_ERROR_STATUS_reserved3_BITS                   1
#define MISC1_TX_DMA_ERROR_STATUS_reserved3_SHIFT                  3

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L0_DMA_DATA_TX_ABORT_ERRORS [02:02] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DMA_DATA_TX_ABORT_ERRORS_MASK 0x00000004
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DMA_DATA_TX_ABORT_ERRORS_ALIGN 0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DMA_DATA_TX_ABORT_ERRORS_BITS 1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_DMA_DATA_TX_ABORT_ERRORS_SHIFT 2

/* MISC1 :: TX_DMA_ERROR_STATUS :: TX_L0_FIFO_FULL_ERRORS [01:01] */
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_MASK      0x00000002
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_ALIGN     0
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_BITS      1
#define MISC1_TX_DMA_ERROR_STATUS_TX_L0_FIFO_FULL_ERRORS_SHIFT     1

/* MISC1 :: TX_DMA_ERROR_STATUS :: reserved4 [00:00] */
#define MISC1_TX_DMA_ERROR_STATUS_reserved4_MASK                   0x00000001
#define MISC1_TX_DMA_ERROR_STATUS_reserved4_ALIGN                  0
#define MISC1_TX_DMA_ERROR_STATUS_reserved4_BITS                   1
#define MISC1_TX_DMA_ERROR_STATUS_reserved4_SHIFT                  0


/****************************************************************************
 * MISC1 :: Y_RX_ERROR_STATUS
 ***************************************************************************/
/* MISC1 :: Y_RX_ERROR_STATUS :: reserved0 [31:14] */
#define MISC1_Y_RX_ERROR_STATUS_reserved0_MASK                     0xffffc000
#define MISC1_Y_RX_ERROR_STATUS_reserved0_ALIGN                    0
#define MISC1_Y_RX_ERROR_STATUS_reserved0_BITS                     18
#define MISC1_Y_RX_ERROR_STATUS_reserved0_SHIFT                    14

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L1_UNDERRUN_ERROR [13:13] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK          0x00002000
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_ALIGN         0
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_BITS          1
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_SHIFT         13

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L1_OVERRUN_ERROR [12:12] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_MASK           0x00001000
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_ALIGN          0
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_BITS           1
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_SHIFT          12

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L0_UNDERRUN_ERROR [11:11] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK          0x00000800
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_ALIGN         0
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_BITS          1
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_SHIFT         11

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L0_OVERRUN_ERROR [10:10] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_MASK           0x00000400
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_ALIGN          0
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_BITS           1
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_SHIFT          10

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L1_DESC_TX_ABORT_ERRORS [09:09] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_MASK    0x00000200
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_ALIGN   0
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_BITS    1
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_SHIFT   9

/* MISC1 :: Y_RX_ERROR_STATUS :: reserved1 [08:08] */
#define MISC1_Y_RX_ERROR_STATUS_reserved1_MASK                     0x00000100
#define MISC1_Y_RX_ERROR_STATUS_reserved1_ALIGN                    0
#define MISC1_Y_RX_ERROR_STATUS_reserved1_BITS                     1
#define MISC1_Y_RX_ERROR_STATUS_reserved1_SHIFT                    8

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L0_DESC_TX_ABORT_ERRORS [07:07] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_MASK    0x00000080
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_ALIGN   0
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_BITS    1
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_SHIFT   7

/* MISC1 :: Y_RX_ERROR_STATUS :: reserved2 [06:05] */
#define MISC1_Y_RX_ERROR_STATUS_reserved2_MASK                     0x00000060
#define MISC1_Y_RX_ERROR_STATUS_reserved2_ALIGN                    0
#define MISC1_Y_RX_ERROR_STATUS_reserved2_BITS                     2
#define MISC1_Y_RX_ERROR_STATUS_reserved2_SHIFT                    5

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L1_FIFO_FULL_ERRORS [04:04] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK        0x00000010
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_ALIGN       0
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_BITS        1
#define MISC1_Y_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_SHIFT       4

/* MISC1 :: Y_RX_ERROR_STATUS :: reserved3 [03:02] */
#define MISC1_Y_RX_ERROR_STATUS_reserved3_MASK                     0x0000000c
#define MISC1_Y_RX_ERROR_STATUS_reserved3_ALIGN                    0
#define MISC1_Y_RX_ERROR_STATUS_reserved3_BITS                     2
#define MISC1_Y_RX_ERROR_STATUS_reserved3_SHIFT                    2

/* MISC1 :: Y_RX_ERROR_STATUS :: RX_L0_FIFO_FULL_ERRORS [01:01] */
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK        0x00000002
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_ALIGN       0
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_BITS        1
#define MISC1_Y_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_SHIFT       1

/* MISC1 :: Y_RX_ERROR_STATUS :: reserved4 [00:00] */
#define MISC1_Y_RX_ERROR_STATUS_reserved4_MASK                     0x00000001
#define MISC1_Y_RX_ERROR_STATUS_reserved4_ALIGN                    0
#define MISC1_Y_RX_ERROR_STATUS_reserved4_BITS                     1
#define MISC1_Y_RX_ERROR_STATUS_reserved4_SHIFT                    0


/****************************************************************************
 * MISC1 :: UV_RX_ERROR_STATUS
 ***************************************************************************/
/* MISC1 :: UV_RX_ERROR_STATUS :: reserved0 [31:14] */
#define MISC1_UV_RX_ERROR_STATUS_reserved0_MASK                    0xffffc000
#define MISC1_UV_RX_ERROR_STATUS_reserved0_ALIGN                   0
#define MISC1_UV_RX_ERROR_STATUS_reserved0_BITS                    18
#define MISC1_UV_RX_ERROR_STATUS_reserved0_SHIFT                   14

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L1_UNDERRUN_ERROR [13:13] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_MASK         0x00002000
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_ALIGN        0
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_BITS         1
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_UNDERRUN_ERROR_SHIFT        13

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L1_OVERRUN_ERROR [12:12] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_MASK          0x00001000
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_ALIGN         0
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_BITS          1
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_OVERRUN_ERROR_SHIFT         12

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L0_UNDERRUN_ERROR [11:11] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_MASK         0x00000800
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_ALIGN        0
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_BITS         1
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_UNDERRUN_ERROR_SHIFT        11

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L0_OVERRUN_ERROR [10:10] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_MASK          0x00000400
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_ALIGN         0
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_BITS          1
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_OVERRUN_ERROR_SHIFT         10

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L1_DESC_TX_ABORT_ERRORS [09:09] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_MASK   0x00000200
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_ALIGN  0
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_BITS   1
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_DESC_TX_ABORT_ERRORS_SHIFT  9

/* MISC1 :: UV_RX_ERROR_STATUS :: reserved1 [08:08] */
#define MISC1_UV_RX_ERROR_STATUS_reserved1_MASK                    0x00000100
#define MISC1_UV_RX_ERROR_STATUS_reserved1_ALIGN                   0
#define MISC1_UV_RX_ERROR_STATUS_reserved1_BITS                    1
#define MISC1_UV_RX_ERROR_STATUS_reserved1_SHIFT                   8

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L0_DESC_TX_ABORT_ERRORS [07:07] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_MASK   0x00000080
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_ALIGN  0
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_BITS   1
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_DESC_TX_ABORT_ERRORS_SHIFT  7

/* MISC1 :: UV_RX_ERROR_STATUS :: reserved2 [06:05] */
#define MISC1_UV_RX_ERROR_STATUS_reserved2_MASK                    0x00000060
#define MISC1_UV_RX_ERROR_STATUS_reserved2_ALIGN                   0
#define MISC1_UV_RX_ERROR_STATUS_reserved2_BITS                    2
#define MISC1_UV_RX_ERROR_STATUS_reserved2_SHIFT                   5

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L1_FIFO_FULL_ERRORS [04:04] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_MASK       0x00000010
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_ALIGN      0
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_BITS       1
#define MISC1_UV_RX_ERROR_STATUS_RX_L1_FIFO_FULL_ERRORS_SHIFT      4

/* MISC1 :: UV_RX_ERROR_STATUS :: reserved3 [03:02] */
#define MISC1_UV_RX_ERROR_STATUS_reserved3_MASK                    0x0000000c
#define MISC1_UV_RX_ERROR_STATUS_reserved3_ALIGN                   0
#define MISC1_UV_RX_ERROR_STATUS_reserved3_BITS                    2
#define MISC1_UV_RX_ERROR_STATUS_reserved3_SHIFT                   2

/* MISC1 :: UV_RX_ERROR_STATUS :: RX_L0_FIFO_FULL_ERRORS [01:01] */
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_MASK       0x00000002
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_ALIGN      0
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_BITS       1
#define MISC1_UV_RX_ERROR_STATUS_RX_L0_FIFO_FULL_ERRORS_SHIFT      1

/* MISC1 :: UV_RX_ERROR_STATUS :: reserved4 [00:00] */
#define MISC1_UV_RX_ERROR_STATUS_reserved4_MASK                    0x00000001
#define MISC1_UV_RX_ERROR_STATUS_reserved4_ALIGN                   0
#define MISC1_UV_RX_ERROR_STATUS_reserved4_BITS                    1
#define MISC1_UV_RX_ERROR_STATUS_reserved4_SHIFT                   0

/****************************************************************************
 * Datatype Definitions.
 ***************************************************************************/
#endif /* #ifndef MACFILE_H__ */

/* End of File */

