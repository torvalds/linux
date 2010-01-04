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
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC 0x00000408 /* WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC Register */
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2 0x0000040c /* WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2 Register */
#define PCIE_TL_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC 0x00000410 /* DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC Register */
#define PCIE_TL_DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2 0x00000414 /* DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2 Register */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC 0x00000418 /* DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC Register */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC 0x0000041c /* DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC Register */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC 0x00000420 /* READ_DMA_SPLIT_IDS_DIAGNOSTIC Register */
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC 0x00000424 /* READ_DMA_SPLIT_LENGTH_DIAGNOSTIC Register */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC 0x0000043c /* XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC Register */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC 0x00000458 /* DMA_COMPLETION_MISC__DIAGNOSTIC Register */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC 0x0000045c /* SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC Register */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC 0x00000460 /* SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC Register */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC 0x00000464 /* SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC Register */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO 0x00000468 /* TL_BUS_NO_DEV__NO__FUNC__NO Register */
#define PCIE_TL_TL_DEBUG               0x0000046c /* TL_DEBUG Register */


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_DLL
 ***************************************************************************/
#define PCIE_DLL_DATA_LINK_CONTROL     0x00000500 /* DATA_LINK_CONTROL Register */
#define PCIE_DLL_DATA_LINK_STATUS      0x00000504 /* DATA_LINK_STATUS Register */
#define PCIE_DLL_DATA_LINK_ATTENTION   0x00000508 /* DATA_LINK_ATTENTION Register */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK 0x0000050c /* DATA_LINK_ATTENTION_MASK Register */
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG 0x00000510 /* NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG Register */
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG 0x00000514 /* ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG Register */
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG 0x00000518 /* PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG Register */
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG 0x0000051c /* RECEIVE_SEQUENCE_NUMBER_DEBUG Register */
#define PCIE_DLL_DATA_LINK_REPLAY      0x00000520 /* DATA_LINK_REPLAY Register */
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT 0x00000524 /* DATA_LINK_ACK_TIMEOUT Register */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD 0x00000528 /* POWER_MANAGEMENT_THRESHOLD Register */
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG 0x0000052c /* RETRY_BUFFER_WRITE_POINTER_DEBUG Register */
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG 0x00000530 /* RETRY_BUFFER_READ_POINTER_DEBUG Register */
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG 0x00000534 /* RETRY_BUFFER_PURGED_POINTER_DEBUG Register */
#define PCIE_DLL_RETRY_BUFFER_READ_WRITE_DEBUG_PORT 0x00000538 /* RETRY_BUFFER_READ_WRITE_DEBUG_PORT Register */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD 0x0000053c /* ERROR_COUNT_THRESHOLD Register */
#define PCIE_DLL_TL_ERROR_COUNTER      0x00000540 /* TL_ERROR_COUNTER Register */
#define PCIE_DLL_DLLP_ERROR_COUNTER    0x00000544 /* DLLP_ERROR_COUNTER Register */
#define PCIE_DLL_NAK_RECEIVED_COUNTER  0x00000548 /* NAK_RECEIVED_COUNTER Register */
#define PCIE_DLL_DATA_LINK_TEST        0x0000054c /* DATA_LINK_TEST Register */
#define PCIE_DLL_PACKET_BIST           0x00000550 /* PACKET_BIST Register */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL 0x00000554 /* LINK_PCIE_1_1_CONTROL Register */


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_PHY
 ***************************************************************************/
#define PCIE_PHY_PHY_MODE              0x00000600 /* TYPE_PHY_MODE Register */
#define PCIE_PHY_PHY_LINK_STATUS       0x00000604 /* TYPE_PHY_LINK_STATUS Register */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL 0x00000608 /* TYPE_PHY_LINK_LTSSM_CONTROL Register */
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER 0x0000060c /* TYPE_PHY_LINK_TRAINING_LINK_NUMBER Register */
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER 0x00000610 /* TYPE_PHY_LINK_TRAINING_LANE_NUMBER Register */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS 0x00000614 /* TYPE_PHY_LINK_TRAINING_N_FTS Register */
#define PCIE_PHY_PHY_ATTENTION         0x00000618 /* TYPE_PHY_ATTENTION Register */
#define PCIE_PHY_PHY_ATTENTION_MASK    0x0000061c /* TYPE_PHY_ATTENTION_MASK Register */
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER 0x00000620 /* TYPE_PHY_RECEIVE_ERROR_COUNTER Register */
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER 0x00000624 /* TYPE_PHY_RECEIVE_FRAMING_ERROR_COUNTER Register */
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD 0x00000628 /* TYPE_PHY_RECEIVE_ERROR_THRESHOLD Register */
#define PCIE_PHY_PHY_TEST_CONTROL      0x0000062c /* TYPE_PHY_TEST_CONTROL Register */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE 0x00000630 /* TYPE_PHY_SERDES_CONTROL_OVERRIDE Register */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE 0x00000634 /* TYPE_PHY_TIMING_PARAMETER_OVERRIDE Register */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES 0x00000638 /* TYPE_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES Register */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES 0x0000063c /* TYPE_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES Register */


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
 * BCM70012_TGT_TOP_MDIO
 ***************************************************************************/
#define MDIO_CTRL0                     0x00000730 /* PCIE Serdes MDIO Control Register 0 */
#define MDIO_CTRL1                     0x00000734 /* PCIE Serdes MDIO Control Register 1 */
#define MDIO_CTRL2                     0x00000738 /* PCIE Serdes MDIO Control Register 2 */


/****************************************************************************
 * BCM70012_TGT_TOP_TGT_RGR_BRIDGE
 ***************************************************************************/
#define TGT_RGR_BRIDGE_REVISION        0x00000740 /* PCIE RGR Bridge Revision Register */
#define TGT_RGR_BRIDGE_CTRL            0x00000744 /* RGR Bridge Control Register */
#define TGT_RGR_BRIDGE_RBUS_TIMER      0x00000748 /* RGR Bridge RBUS Timer Register */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0 0x0000074c /* RGR Bridge Spare Software Reset 0 Register */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1 0x00000750 /* RGR Bridge Spare Software Reset 1 Register */


/****************************************************************************
 * BCM70012_I2C_TOP_I2C
 ***************************************************************************/
#define I2C_CHIP_ADDRESS               0x00000800 /* I2C Chip Address And Read/Write Control */
#define I2C_DATA_IN0                   0x00000804 /* I2C Write Data Byte 0 */
#define I2C_DATA_IN1                   0x00000808 /* I2C Write Data Byte 1 */
#define I2C_DATA_IN2                   0x0000080c /* I2C Write Data Byte 2 */
#define I2C_DATA_IN3                   0x00000810 /* I2C Write Data Byte 3 */
#define I2C_DATA_IN4                   0x00000814 /* I2C Write Data Byte 4 */
#define I2C_DATA_IN5                   0x00000818 /* I2C Write Data Byte 5 */
#define I2C_DATA_IN6                   0x0000081c /* I2C Write Data Byte 6 */
#define I2C_DATA_IN7                   0x00000820 /* I2C Write Data Byte 7 */
#define I2C_CNT_REG                    0x00000824 /* I2C Transfer Count Register */
#define I2C_CTL_REG                    0x00000828 /* I2C Control Register */
#define I2C_IIC_ENABLE                 0x0000082c /* I2C Read/Write Enable And Interrupt */
#define I2C_DATA_OUT0                  0x00000830 /* I2C Read Data Byte 0 */
#define I2C_DATA_OUT1                  0x00000834 /* I2C Read Data Byte 1 */
#define I2C_DATA_OUT2                  0x00000838 /* I2C Read Data Byte 2 */
#define I2C_DATA_OUT3                  0x0000083c /* I2C Read Data Byte 3 */
#define I2C_DATA_OUT4                  0x00000840 /* I2C Read Data Byte 4 */
#define I2C_DATA_OUT5                  0x00000844 /* I2C Read Data Byte 5 */
#define I2C_DATA_OUT6                  0x00000848 /* I2C Read Data Byte 6 */
#define I2C_DATA_OUT7                  0x0000084c /* I2C Read Data Byte 7 */
#define I2C_CTLHI_REG                  0x00000850 /* I2C Control Register */
#define I2C_SCL_PARAM                  0x00000854 /* I2C SCL Parameter Register */


/****************************************************************************
 * BCM70012_I2C_TOP_I2C_GR_BRIDGE
 ***************************************************************************/
#define I2C_GR_BRIDGE_REVISION         0x00000be0 /* GR Bridge Revision */
#define I2C_GR_BRIDGE_CTRL             0x00000be4 /* GR Bridge Control Register */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0 0x00000be8 /* GR Bridge Software Reset 0 Register */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1 0x00000bec /* GR Bridge Software Reset 1 Register */


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
#define GISB_ARBITER_BP_CTRL           0x00000f10 /* GISB ARBITER Breakpoint Control Register */
#define GISB_ARBITER_BP_CAP_CLR        0x00000f14 /* GISB ARBITER Breakpoint Capture Clear Register */
#define GISB_ARBITER_BP_START_ADDR_0   0x00000f18 /* GISB ARBITER Breakpoint Start Address 0 Register */
#define GISB_ARBITER_BP_END_ADDR_0     0x00000f1c /* GISB ARBITER Breakpoint End Address 0 Register */
#define GISB_ARBITER_BP_READ_0         0x00000f20 /* GISB ARBITER Breakpoint Master Read Control 0 Register */
#define GISB_ARBITER_BP_WRITE_0        0x00000f24 /* GISB ARBITER Breakpoint Master Write Control 0 Register */
#define GISB_ARBITER_BP_ENABLE_0       0x00000f28 /* GISB ARBITER Breakpoint Enable 0 Register */
#define GISB_ARBITER_BP_START_ADDR_1   0x00000f2c /* GISB ARBITER Breakpoint Start Address 1 Register */
#define GISB_ARBITER_BP_END_ADDR_1     0x00000f30 /* GISB ARBITER Breakpoint End Address 1 Register */
#define GISB_ARBITER_BP_READ_1         0x00000f34 /* GISB ARBITER Breakpoint Master Read Control 1 Register */
#define GISB_ARBITER_BP_WRITE_1        0x00000f38 /* GISB ARBITER Breakpoint Master Write Control 1 Register */
#define GISB_ARBITER_BP_ENABLE_1       0x00000f3c /* GISB ARBITER Breakpoint Enable 1 Register */
#define GISB_ARBITER_BP_START_ADDR_2   0x00000f40 /* GISB ARBITER Breakpoint Start Address 2 Register */
#define GISB_ARBITER_BP_END_ADDR_2     0x00000f44 /* GISB ARBITER Breakpoint End Address 2 Register */
#define GISB_ARBITER_BP_READ_2         0x00000f48 /* GISB ARBITER Breakpoint Master Read Control 2 Register */
#define GISB_ARBITER_BP_WRITE_2        0x00000f4c /* GISB ARBITER Breakpoint Master Write Control 2 Register */
#define GISB_ARBITER_BP_ENABLE_2       0x00000f50 /* GISB ARBITER Breakpoint Enable 2 Register */
#define GISB_ARBITER_BP_START_ADDR_3   0x00000f54 /* GISB ARBITER Breakpoint Start Address 3 Register */
#define GISB_ARBITER_BP_END_ADDR_3     0x00000f58 /* GISB ARBITER Breakpoint End Address 3 Register */
#define GISB_ARBITER_BP_READ_3         0x00000f5c /* GISB ARBITER Breakpoint Master Read Control 3 Register */
#define GISB_ARBITER_BP_WRITE_3        0x00000f60 /* GISB ARBITER Breakpoint Master Write Control 3 Register */
#define GISB_ARBITER_BP_ENABLE_3       0x00000f64 /* GISB ARBITER Breakpoint Enable 3 Register */
#define GISB_ARBITER_BP_START_ADDR_4   0x00000f68 /* GISB ARBITER Breakpoint Start Address 4 Register */
#define GISB_ARBITER_BP_END_ADDR_4     0x00000f6c /* GISB ARBITER Breakpoint End Address 4 Register */
#define GISB_ARBITER_BP_READ_4         0x00000f70 /* GISB ARBITER Breakpoint Master Read Control 4 Register */
#define GISB_ARBITER_BP_WRITE_4        0x00000f74 /* GISB ARBITER Breakpoint Master Write Control 4 Register */
#define GISB_ARBITER_BP_ENABLE_4       0x00000f78 /* GISB ARBITER Breakpoint Enable 4 Register */
#define GISB_ARBITER_BP_START_ADDR_5   0x00000f7c /* GISB ARBITER Breakpoint Start Address 5 Register */
#define GISB_ARBITER_BP_END_ADDR_5     0x00000f80 /* GISB ARBITER Breakpoint End Address 5 Register */
#define GISB_ARBITER_BP_READ_5         0x00000f84 /* GISB ARBITER Breakpoint Master Read Control 5 Register */
#define GISB_ARBITER_BP_WRITE_5        0x00000f88 /* GISB ARBITER Breakpoint Master Write Control 5 Register */
#define GISB_ARBITER_BP_ENABLE_5       0x00000f8c /* GISB ARBITER Breakpoint Enable 5 Register */
#define GISB_ARBITER_BP_START_ADDR_6   0x00000f90 /* GISB ARBITER Breakpoint Start Address 6 Register */
#define GISB_ARBITER_BP_END_ADDR_6     0x00000f94 /* GISB ARBITER Breakpoint End Address 6 Register */
#define GISB_ARBITER_BP_READ_6         0x00000f98 /* GISB ARBITER Breakpoint Master Read Control 6 Register */
#define GISB_ARBITER_BP_WRITE_6        0x00000f9c /* GISB ARBITER Breakpoint Master Write Control 6 Register */
#define GISB_ARBITER_BP_ENABLE_6       0x00000fa0 /* GISB ARBITER Breakpoint Enable 6 Register */
#define GISB_ARBITER_BP_START_ADDR_7   0x00000fa4 /* GISB ARBITER Breakpoint Start Address 7 Register */
#define GISB_ARBITER_BP_END_ADDR_7     0x00000fa8 /* GISB ARBITER Breakpoint End Address 7 Register */
#define GISB_ARBITER_BP_READ_7         0x00000fac /* GISB ARBITER Breakpoint Master Read Control 7 Register */
#define GISB_ARBITER_BP_WRITE_7        0x00000fb0 /* GISB ARBITER Breakpoint Master Write Control 7 Register */
#define GISB_ARBITER_BP_ENABLE_7       0x00000fb4 /* GISB ARBITER Breakpoint Enable 7 Register */
#define GISB_ARBITER_BP_CAP_ADDR       0x00000fb8 /* GISB ARBITER Breakpoint Capture Address Register */
#define GISB_ARBITER_BP_CAP_DATA       0x00000fbc /* GISB ARBITER Breakpoint Capture Data Register */
#define GISB_ARBITER_BP_CAP_STATUS     0x00000fc0 /* GISB ARBITER Breakpoint Capture Status Register */
#define GISB_ARBITER_BP_CAP_MASTER     0x00000fc4 /* GISB ARBITER Breakpoint Capture GISB MASTER Register */
#define GISB_ARBITER_ERR_CAP_CLR       0x00000fc8 /* GISB ARBITER Error Capture Clear Register */
#define GISB_ARBITER_ERR_CAP_ADDR      0x00000fcc /* GISB ARBITER Error Capture Address Register */
#define GISB_ARBITER_ERR_CAP_DATA      0x00000fd0 /* GISB ARBITER Error Capture Data Register */
#define GISB_ARBITER_ERR_CAP_STATUS    0x00000fd4 /* GISB ARBITER Error Capture Status Register */
#define GISB_ARBITER_ERR_CAP_MASTER    0x00000fd8 /* GISB ARBITER Error Capture GISB MASTER Register */


/****************************************************************************
 * BCM70012_MISC_TOP_MISC_GR_BRIDGE
 ***************************************************************************/
#define MISC_GR_BRIDGE_REVISION        0x00000fe0 /* GR Bridge Revision */
#define MISC_GR_BRIDGE_CTRL            0x00000fe4 /* GR Bridge Control Register */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0 0x00000fe8 /* GR Bridge Software Reset 0 Register */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1 0x00000fec /* GR Bridge Software Reset 1 Register */


/****************************************************************************
 * BCM70012_DBU_TOP_DBU
 ***************************************************************************/
#define DBU_DBU_CMD                    0x00001000 /* DBU (Debug UART) command register */
#define DBU_DBU_STATUS                 0x00001004 /* DBU (Debug UART) status register */
#define DBU_DBU_CONFIG                 0x00001008 /* DBU (Debug UART) configuration register */
#define DBU_DBU_TIMING                 0x0000100c /* DBU (Debug UART) timing register */
#define DBU_DBU_RXDATA                 0x00001010 /* DBU (Debug UART) recieve data register */
#define DBU_DBU_TXDATA                 0x00001014 /* DBU (Debug UART) transmit data register */


/****************************************************************************
 * BCM70012_DBU_TOP_DBU_RGR_BRIDGE
 ***************************************************************************/
#define DBU_RGR_BRIDGE_REVISION        0x000013e0 /* RGR Bridge Revision */
#define DBU_RGR_BRIDGE_CTRL            0x000013e4 /* RGR Bridge Control Register */
#define DBU_RGR_BRIDGE_RBUS_TIMER      0x000013e8 /* RGR Bridge RBUS Timer Register */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0 0x000013ec /* RGR Bridge Software Reset 0 Register */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1 0x000013f0 /* RGR Bridge Software Reset 1 Register */


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
 * BCM70012_OTP_TOP_OTP_GR_BRIDGE
 ***************************************************************************/
#define OTP_GR_BRIDGE_REVISION         0x000017e0 /* GR Bridge Revision */
#define OTP_GR_BRIDGE_CTRL             0x000017e4 /* GR Bridge Control Register */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0 0x000017e8 /* GR Bridge Software Reset 0 Register */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1 0x000017ec /* GR Bridge Software Reset 1 Register */


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
 * BCM70012_AES_TOP_AES_RGR_BRIDGE
 ***************************************************************************/
#define AES_RGR_BRIDGE_REVISION        0x00001be0 /* RGR Bridge Revision */
#define AES_RGR_BRIDGE_CTRL            0x00001be4 /* RGR Bridge Control Register */
#define AES_RGR_BRIDGE_RBUS_TIMER      0x00001be8 /* RGR Bridge RBUS Timer Register */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0 0x00001bec /* RGR Bridge Software Reset 0 Register */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1 0x00001bf0 /* RGR Bridge Software Reset 1 Register */


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
 * BCM70012_DCI_TOP_DCI_RGR_BRIDGE
 ***************************************************************************/
#define DCI_RGR_BRIDGE_REVISION        0x00001fe0 /* RGR Bridge Revision */
#define DCI_RGR_BRIDGE_CTRL            0x00001fe4 /* RGR Bridge Control Register */
#define DCI_RGR_BRIDGE_RBUS_TIMER      0x00001fe8 /* RGR Bridge RBUS Timer Register */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0 0x00001fec /* RGR Bridge Software Reset 0 Register */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1 0x00001ff0 /* RGR Bridge Software Reset 1 Register */


/****************************************************************************
 * BCM70012_CCE_TOP_CCE_RGR_BRIDGE
 ***************************************************************************/
#define CCE_RGR_BRIDGE_REVISION        0x000023e0 /* RGR Bridge Revision */
#define CCE_RGR_BRIDGE_CTRL            0x000023e4 /* RGR Bridge Control Register */
#define CCE_RGR_BRIDGE_RBUS_TIMER      0x000023e8 /* RGR Bridge RBUS Timer Register */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0 0x000023ec /* RGR Bridge Software Reset 0 Register */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1 0x000023f0 /* RGR Bridge Software Reset 1 Register */


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_CFG
 ***************************************************************************/
/****************************************************************************
 * PCIE_CFG :: DEVICE_VENDOR_ID
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_VENDOR_ID :: DEVICE_ID [31:16] */
#define PCIE_CFG_DEVICE_VENDOR_ID_DEVICE_ID_MASK                   0xffff0000
#define PCIE_CFG_DEVICE_VENDOR_ID_DEVICE_ID_ALIGN                  0
#define PCIE_CFG_DEVICE_VENDOR_ID_DEVICE_ID_BITS                   16
#define PCIE_CFG_DEVICE_VENDOR_ID_DEVICE_ID_SHIFT                  16

/* PCIE_CFG :: DEVICE_VENDOR_ID :: VENDOR_ID [15:00] */
#define PCIE_CFG_DEVICE_VENDOR_ID_VENDOR_ID_MASK                   0x0000ffff
#define PCIE_CFG_DEVICE_VENDOR_ID_VENDOR_ID_ALIGN                  0
#define PCIE_CFG_DEVICE_VENDOR_ID_VENDOR_ID_BITS                   16
#define PCIE_CFG_DEVICE_VENDOR_ID_VENDOR_ID_SHIFT                  0


/****************************************************************************
 * PCIE_CFG :: STATUS_COMMAND
 ***************************************************************************/
/* PCIE_CFG :: STATUS_COMMAND :: DETECTED_PARITY_ERROR [31:31] */
#define PCIE_CFG_STATUS_COMMAND_DETECTED_PARITY_ERROR_MASK         0x80000000
#define PCIE_CFG_STATUS_COMMAND_DETECTED_PARITY_ERROR_ALIGN        0
#define PCIE_CFG_STATUS_COMMAND_DETECTED_PARITY_ERROR_BITS         1
#define PCIE_CFG_STATUS_COMMAND_DETECTED_PARITY_ERROR_SHIFT        31

/* PCIE_CFG :: STATUS_COMMAND :: SIGNALED_SYSTEM_ERROR [30:30] */
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_SYSTEM_ERROR_MASK         0x40000000
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_SYSTEM_ERROR_ALIGN        0
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_SYSTEM_ERROR_BITS         1
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_SYSTEM_ERROR_SHIFT        30

/* PCIE_CFG :: STATUS_COMMAND :: RECEIVED_MASTER_ABORT [29:29] */
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_MASTER_ABORT_MASK         0x20000000
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_MASTER_ABORT_ALIGN        0
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_MASTER_ABORT_BITS         1
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_MASTER_ABORT_SHIFT        29

/* PCIE_CFG :: STATUS_COMMAND :: RECEIVED_TARGET_ABORT [28:28] */
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_TARGET_ABORT_MASK         0x10000000
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_TARGET_ABORT_ALIGN        0
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_TARGET_ABORT_BITS         1
#define PCIE_CFG_STATUS_COMMAND_RECEIVED_TARGET_ABORT_SHIFT        28

/* PCIE_CFG :: STATUS_COMMAND :: SIGNALED_TARGET_ABORT [27:27] */
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_TARGET_ABORT_MASK         0x08000000
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_TARGET_ABORT_ALIGN        0
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_TARGET_ABORT_BITS         1
#define PCIE_CFG_STATUS_COMMAND_SIGNALED_TARGET_ABORT_SHIFT        27

/* PCIE_CFG :: STATUS_COMMAND :: DEVSEL_TIMING [26:25] */
#define PCIE_CFG_STATUS_COMMAND_DEVSEL_TIMING_MASK                 0x06000000
#define PCIE_CFG_STATUS_COMMAND_DEVSEL_TIMING_ALIGN                0
#define PCIE_CFG_STATUS_COMMAND_DEVSEL_TIMING_BITS                 2
#define PCIE_CFG_STATUS_COMMAND_DEVSEL_TIMING_SHIFT                25

/* PCIE_CFG :: STATUS_COMMAND :: MASTER_DATA_PARITY_ERROR [24:24] */
#define PCIE_CFG_STATUS_COMMAND_MASTER_DATA_PARITY_ERROR_MASK      0x01000000
#define PCIE_CFG_STATUS_COMMAND_MASTER_DATA_PARITY_ERROR_ALIGN     0
#define PCIE_CFG_STATUS_COMMAND_MASTER_DATA_PARITY_ERROR_BITS      1
#define PCIE_CFG_STATUS_COMMAND_MASTER_DATA_PARITY_ERROR_SHIFT     24

/* PCIE_CFG :: STATUS_COMMAND :: FAST_BACK_TO_BACK_CAPABLE [23:23] */
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_CAPABLE_MASK     0x00800000
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_CAPABLE_ALIGN    0
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_CAPABLE_BITS     1
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_CAPABLE_SHIFT    23

/* PCIE_CFG :: STATUS_COMMAND :: RESERVED_0 [22:22] */
#define PCIE_CFG_STATUS_COMMAND_RESERVED_0_MASK                    0x00400000
#define PCIE_CFG_STATUS_COMMAND_RESERVED_0_ALIGN                   0
#define PCIE_CFG_STATUS_COMMAND_RESERVED_0_BITS                    1
#define PCIE_CFG_STATUS_COMMAND_RESERVED_0_SHIFT                   22

/* PCIE_CFG :: STATUS_COMMAND :: CAPABLE_66MHZ [21:21] */
#define PCIE_CFG_STATUS_COMMAND_CAPABLE_66MHZ_MASK                 0x00200000
#define PCIE_CFG_STATUS_COMMAND_CAPABLE_66MHZ_ALIGN                0
#define PCIE_CFG_STATUS_COMMAND_CAPABLE_66MHZ_BITS                 1
#define PCIE_CFG_STATUS_COMMAND_CAPABLE_66MHZ_SHIFT                21

/* PCIE_CFG :: STATUS_COMMAND :: CAPABILITIES_LIST [20:20] */
#define PCIE_CFG_STATUS_COMMAND_CAPABILITIES_LIST_MASK             0x00100000
#define PCIE_CFG_STATUS_COMMAND_CAPABILITIES_LIST_ALIGN            0
#define PCIE_CFG_STATUS_COMMAND_CAPABILITIES_LIST_BITS             1
#define PCIE_CFG_STATUS_COMMAND_CAPABILITIES_LIST_SHIFT            20

/* PCIE_CFG :: STATUS_COMMAND :: INTERRUPT_STATUS [19:19] */
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_STATUS_MASK              0x00080000
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_STATUS_ALIGN             0
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_STATUS_BITS              1
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_STATUS_SHIFT             19

/* PCIE_CFG :: STATUS_COMMAND :: RESERVED_1 [18:16] */
#define PCIE_CFG_STATUS_COMMAND_RESERVED_1_MASK                    0x00070000
#define PCIE_CFG_STATUS_COMMAND_RESERVED_1_ALIGN                   0
#define PCIE_CFG_STATUS_COMMAND_RESERVED_1_BITS                    3
#define PCIE_CFG_STATUS_COMMAND_RESERVED_1_SHIFT                   16

/* PCIE_CFG :: STATUS_COMMAND :: RESERVED_2 [15:11] */
#define PCIE_CFG_STATUS_COMMAND_RESERVED_2_MASK                    0x0000f800
#define PCIE_CFG_STATUS_COMMAND_RESERVED_2_ALIGN                   0
#define PCIE_CFG_STATUS_COMMAND_RESERVED_2_BITS                    5
#define PCIE_CFG_STATUS_COMMAND_RESERVED_2_SHIFT                   11

/* PCIE_CFG :: STATUS_COMMAND :: INTERRUPT_DISABLE [10:10] */
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_DISABLE_MASK             0x00000400
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_DISABLE_ALIGN            0
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_DISABLE_BITS             1
#define PCIE_CFG_STATUS_COMMAND_INTERRUPT_DISABLE_SHIFT            10

/* PCIE_CFG :: STATUS_COMMAND :: FAST_BACK_TO_BACK_ENABLE [09:09] */
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_ENABLE_MASK      0x00000200
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_ENABLE_ALIGN     0
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_ENABLE_BITS      1
#define PCIE_CFG_STATUS_COMMAND_FAST_BACK_TO_BACK_ENABLE_SHIFT     9

/* PCIE_CFG :: STATUS_COMMAND :: SYSTEM_ERROR_ENABLE [08:08] */
#define PCIE_CFG_STATUS_COMMAND_SYSTEM_ERROR_ENABLE_MASK           0x00000100
#define PCIE_CFG_STATUS_COMMAND_SYSTEM_ERROR_ENABLE_ALIGN          0
#define PCIE_CFG_STATUS_COMMAND_SYSTEM_ERROR_ENABLE_BITS           1
#define PCIE_CFG_STATUS_COMMAND_SYSTEM_ERROR_ENABLE_SHIFT          8

/* PCIE_CFG :: STATUS_COMMAND :: STEPPING_CONTROL [07:07] */
#define PCIE_CFG_STATUS_COMMAND_STEPPING_CONTROL_MASK              0x00000080
#define PCIE_CFG_STATUS_COMMAND_STEPPING_CONTROL_ALIGN             0
#define PCIE_CFG_STATUS_COMMAND_STEPPING_CONTROL_BITS              1
#define PCIE_CFG_STATUS_COMMAND_STEPPING_CONTROL_SHIFT             7

/* PCIE_CFG :: STATUS_COMMAND :: PARITY_ERROR_ENABLE [06:06] */
#define PCIE_CFG_STATUS_COMMAND_PARITY_ERROR_ENABLE_MASK           0x00000040
#define PCIE_CFG_STATUS_COMMAND_PARITY_ERROR_ENABLE_ALIGN          0
#define PCIE_CFG_STATUS_COMMAND_PARITY_ERROR_ENABLE_BITS           1
#define PCIE_CFG_STATUS_COMMAND_PARITY_ERROR_ENABLE_SHIFT          6

/* PCIE_CFG :: STATUS_COMMAND :: VGA_PALETTE_SNOOP [05:05] */
#define PCIE_CFG_STATUS_COMMAND_VGA_PALETTE_SNOOP_MASK             0x00000020
#define PCIE_CFG_STATUS_COMMAND_VGA_PALETTE_SNOOP_ALIGN            0
#define PCIE_CFG_STATUS_COMMAND_VGA_PALETTE_SNOOP_BITS             1
#define PCIE_CFG_STATUS_COMMAND_VGA_PALETTE_SNOOP_SHIFT            5

/* PCIE_CFG :: STATUS_COMMAND :: MEMORY_WRITE_AND_INVALIDATE [04:04] */
#define PCIE_CFG_STATUS_COMMAND_MEMORY_WRITE_AND_INVALIDATE_MASK   0x00000010
#define PCIE_CFG_STATUS_COMMAND_MEMORY_WRITE_AND_INVALIDATE_ALIGN  0
#define PCIE_CFG_STATUS_COMMAND_MEMORY_WRITE_AND_INVALIDATE_BITS   1
#define PCIE_CFG_STATUS_COMMAND_MEMORY_WRITE_AND_INVALIDATE_SHIFT  4

/* PCIE_CFG :: STATUS_COMMAND :: SPECIAL_CYCLES [03:03] */
#define PCIE_CFG_STATUS_COMMAND_SPECIAL_CYCLES_MASK                0x00000008
#define PCIE_CFG_STATUS_COMMAND_SPECIAL_CYCLES_ALIGN               0
#define PCIE_CFG_STATUS_COMMAND_SPECIAL_CYCLES_BITS                1
#define PCIE_CFG_STATUS_COMMAND_SPECIAL_CYCLES_SHIFT               3

/* PCIE_CFG :: STATUS_COMMAND :: BUS_MASTER [02:02] */
#define PCIE_CFG_STATUS_COMMAND_BUS_MASTER_MASK                    0x00000004
#define PCIE_CFG_STATUS_COMMAND_BUS_MASTER_ALIGN                   0
#define PCIE_CFG_STATUS_COMMAND_BUS_MASTER_BITS                    1
#define PCIE_CFG_STATUS_COMMAND_BUS_MASTER_SHIFT                   2

/* PCIE_CFG :: STATUS_COMMAND :: MEMORY_SPACE [01:01] */
#define PCIE_CFG_STATUS_COMMAND_MEMORY_SPACE_MASK                  0x00000002
#define PCIE_CFG_STATUS_COMMAND_MEMORY_SPACE_ALIGN                 0
#define PCIE_CFG_STATUS_COMMAND_MEMORY_SPACE_BITS                  1
#define PCIE_CFG_STATUS_COMMAND_MEMORY_SPACE_SHIFT                 1

/* PCIE_CFG :: STATUS_COMMAND :: I_O_SPACE [00:00] */
#define PCIE_CFG_STATUS_COMMAND_I_O_SPACE_MASK                     0x00000001
#define PCIE_CFG_STATUS_COMMAND_I_O_SPACE_ALIGN                    0
#define PCIE_CFG_STATUS_COMMAND_I_O_SPACE_BITS                     1
#define PCIE_CFG_STATUS_COMMAND_I_O_SPACE_SHIFT                    0


/****************************************************************************
 * PCIE_CFG :: PCI_CLASSCODE_AND_REVISION_ID
 ***************************************************************************/
/* PCIE_CFG :: PCI_CLASSCODE_AND_REVISION_ID :: PCI_CLASSCODE [31:08] */
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_PCI_CLASSCODE_MASK  0xffffff00
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_PCI_CLASSCODE_ALIGN 0
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_PCI_CLASSCODE_BITS  24
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_PCI_CLASSCODE_SHIFT 8

/* PCIE_CFG :: PCI_CLASSCODE_AND_REVISION_ID :: REVISION_ID [07:00] */
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_REVISION_ID_MASK    0x000000ff
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_REVISION_ID_ALIGN   0
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_REVISION_ID_BITS    8
#define PCIE_CFG_PCI_CLASSCODE_AND_REVISION_ID_REVISION_ID_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE
 ***************************************************************************/
/* PCIE_CFG :: BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE :: BIST [31:24] */
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_BIST_MASK 0xff000000
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_BIST_ALIGN 0
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_BIST_BITS 8
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_BIST_SHIFT 24

/* PCIE_CFG :: BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE :: HEADER_TYPE [23:16] */
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_HEADER_TYPE_MASK 0x00ff0000
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_HEADER_TYPE_ALIGN 0
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_HEADER_TYPE_BITS 8
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_HEADER_TYPE_SHIFT 16

/* PCIE_CFG :: BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE :: LATENCY_TIMER [15:08] */
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_LATENCY_TIMER_MASK 0x0000ff00
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_LATENCY_TIMER_ALIGN 0
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_LATENCY_TIMER_BITS 8
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_LATENCY_TIMER_SHIFT 8

/* PCIE_CFG :: BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE :: CACHE_LINE_SIZE [07:00] */
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_CACHE_LINE_SIZE_MASK 0x000000ff
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_CACHE_LINE_SIZE_ALIGN 0
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_CACHE_LINE_SIZE_BITS 8
#define PCIE_CFG_BIST_HEADER_TYPE_LATENCY_TIMER_CACHE_LINE_SIZE_CACHE_LINE_SIZE_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: BASE_ADDRESS_1
 ***************************************************************************/
/* PCIE_CFG :: BASE_ADDRESS_1 :: BASE_ADDRESS [31:16] */
#define PCIE_CFG_BASE_ADDRESS_1_BASE_ADDRESS_MASK                  0xffff0000
#define PCIE_CFG_BASE_ADDRESS_1_BASE_ADDRESS_ALIGN                 0
#define PCIE_CFG_BASE_ADDRESS_1_BASE_ADDRESS_BITS                  16
#define PCIE_CFG_BASE_ADDRESS_1_BASE_ADDRESS_SHIFT                 16

/* PCIE_CFG :: BASE_ADDRESS_1 :: RESERVED_0 [15:04] */
#define PCIE_CFG_BASE_ADDRESS_1_RESERVED_0_MASK                    0x0000fff0
#define PCIE_CFG_BASE_ADDRESS_1_RESERVED_0_ALIGN                   0
#define PCIE_CFG_BASE_ADDRESS_1_RESERVED_0_BITS                    12
#define PCIE_CFG_BASE_ADDRESS_1_RESERVED_0_SHIFT                   4

/* PCIE_CFG :: BASE_ADDRESS_1 :: PREFETCHABLE [03:03] */
#define PCIE_CFG_BASE_ADDRESS_1_PREFETCHABLE_MASK                  0x00000008
#define PCIE_CFG_BASE_ADDRESS_1_PREFETCHABLE_ALIGN                 0
#define PCIE_CFG_BASE_ADDRESS_1_PREFETCHABLE_BITS                  1
#define PCIE_CFG_BASE_ADDRESS_1_PREFETCHABLE_SHIFT                 3

/* PCIE_CFG :: BASE_ADDRESS_1 :: TYPE [02:01] */
#define PCIE_CFG_BASE_ADDRESS_1_TYPE_MASK                          0x00000006
#define PCIE_CFG_BASE_ADDRESS_1_TYPE_ALIGN                         0
#define PCIE_CFG_BASE_ADDRESS_1_TYPE_BITS                          2
#define PCIE_CFG_BASE_ADDRESS_1_TYPE_SHIFT                         1

/* PCIE_CFG :: BASE_ADDRESS_1 :: MEMORY_SPACE_INDICATOR [00:00] */
#define PCIE_CFG_BASE_ADDRESS_1_MEMORY_SPACE_INDICATOR_MASK        0x00000001
#define PCIE_CFG_BASE_ADDRESS_1_MEMORY_SPACE_INDICATOR_ALIGN       0
#define PCIE_CFG_BASE_ADDRESS_1_MEMORY_SPACE_INDICATOR_BITS        1
#define PCIE_CFG_BASE_ADDRESS_1_MEMORY_SPACE_INDICATOR_SHIFT       0


/****************************************************************************
 * PCIE_CFG :: BASE_ADDRESS_2
 ***************************************************************************/
/* PCIE_CFG :: BASE_ADDRESS_2 :: EXTENDED_BASE_ADDRESS [31:00] */
#define PCIE_CFG_BASE_ADDRESS_2_EXTENDED_BASE_ADDRESS_MASK         0xffffffff
#define PCIE_CFG_BASE_ADDRESS_2_EXTENDED_BASE_ADDRESS_ALIGN        0
#define PCIE_CFG_BASE_ADDRESS_2_EXTENDED_BASE_ADDRESS_BITS         32
#define PCIE_CFG_BASE_ADDRESS_2_EXTENDED_BASE_ADDRESS_SHIFT        0


/****************************************************************************
 * PCIE_CFG :: BASE_ADDRESS_3
 ***************************************************************************/
/* PCIE_CFG :: BASE_ADDRESS_3 :: BASE_ADDRESS_2 [31:22] */
#define PCIE_CFG_BASE_ADDRESS_3_BASE_ADDRESS_2_MASK                0xffc00000
#define PCIE_CFG_BASE_ADDRESS_3_BASE_ADDRESS_2_ALIGN               0
#define PCIE_CFG_BASE_ADDRESS_3_BASE_ADDRESS_2_BITS                10
#define PCIE_CFG_BASE_ADDRESS_3_BASE_ADDRESS_2_SHIFT               22

/* PCIE_CFG :: BASE_ADDRESS_3 :: RESERVED_0 [21:04] */
#define PCIE_CFG_BASE_ADDRESS_3_RESERVED_0_MASK                    0x003ffff0
#define PCIE_CFG_BASE_ADDRESS_3_RESERVED_0_ALIGN                   0
#define PCIE_CFG_BASE_ADDRESS_3_RESERVED_0_BITS                    18
#define PCIE_CFG_BASE_ADDRESS_3_RESERVED_0_SHIFT                   4

/* PCIE_CFG :: BASE_ADDRESS_3 :: PREFETCHABLE [03:03] */
#define PCIE_CFG_BASE_ADDRESS_3_PREFETCHABLE_MASK                  0x00000008
#define PCIE_CFG_BASE_ADDRESS_3_PREFETCHABLE_ALIGN                 0
#define PCIE_CFG_BASE_ADDRESS_3_PREFETCHABLE_BITS                  1
#define PCIE_CFG_BASE_ADDRESS_3_PREFETCHABLE_SHIFT                 3

/* PCIE_CFG :: BASE_ADDRESS_3 :: TYPE [02:01] */
#define PCIE_CFG_BASE_ADDRESS_3_TYPE_MASK                          0x00000006
#define PCIE_CFG_BASE_ADDRESS_3_TYPE_ALIGN                         0
#define PCIE_CFG_BASE_ADDRESS_3_TYPE_BITS                          2
#define PCIE_CFG_BASE_ADDRESS_3_TYPE_SHIFT                         1

/* PCIE_CFG :: BASE_ADDRESS_3 :: MEMORY_SPACE_INDICATOR [00:00] */
#define PCIE_CFG_BASE_ADDRESS_3_MEMORY_SPACE_INDICATOR_MASK        0x00000001
#define PCIE_CFG_BASE_ADDRESS_3_MEMORY_SPACE_INDICATOR_ALIGN       0
#define PCIE_CFG_BASE_ADDRESS_3_MEMORY_SPACE_INDICATOR_BITS        1
#define PCIE_CFG_BASE_ADDRESS_3_MEMORY_SPACE_INDICATOR_SHIFT       0


/****************************************************************************
 * PCIE_CFG :: BASE_ADDRESS_4
 ***************************************************************************/
/* PCIE_CFG :: BASE_ADDRESS_4 :: EXTENDED_BASE_ADDRESS_2 [31:00] */
#define PCIE_CFG_BASE_ADDRESS_4_EXTENDED_BASE_ADDRESS_2_MASK       0xffffffff
#define PCIE_CFG_BASE_ADDRESS_4_EXTENDED_BASE_ADDRESS_2_ALIGN      0
#define PCIE_CFG_BASE_ADDRESS_4_EXTENDED_BASE_ADDRESS_2_BITS       32
#define PCIE_CFG_BASE_ADDRESS_4_EXTENDED_BASE_ADDRESS_2_SHIFT      0


/****************************************************************************
 * PCIE_CFG :: CARDBUS_CIS_POINTER
 ***************************************************************************/
/* PCIE_CFG :: CARDBUS_CIS_POINTER :: CARDBUS_CIS_POINTER [31:00] */
#define PCIE_CFG_CARDBUS_CIS_POINTER_CARDBUS_CIS_POINTER_MASK      0xffffffff
#define PCIE_CFG_CARDBUS_CIS_POINTER_CARDBUS_CIS_POINTER_ALIGN     0
#define PCIE_CFG_CARDBUS_CIS_POINTER_CARDBUS_CIS_POINTER_BITS      32
#define PCIE_CFG_CARDBUS_CIS_POINTER_CARDBUS_CIS_POINTER_SHIFT     0


/****************************************************************************
 * PCIE_CFG :: SUBSYSTEM_DEVICE_VENDOR_ID
 ***************************************************************************/
/* PCIE_CFG :: SUBSYSTEM_DEVICE_VENDOR_ID :: SUBSYSTEM_DEVICE_ID [31:16] */
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_DEVICE_ID_MASK 0xffff0000
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_DEVICE_ID_ALIGN 0
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_DEVICE_ID_BITS 16
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_DEVICE_ID_SHIFT 16

/* PCIE_CFG :: SUBSYSTEM_DEVICE_VENDOR_ID :: SUBSYSTEM_VENDOR_ID [15:00] */
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_VENDOR_ID_MASK 0x0000ffff
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_VENDOR_ID_ALIGN 0
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_VENDOR_ID_BITS 16
#define PCIE_CFG_SUBSYSTEM_DEVICE_VENDOR_ID_SUBSYSTEM_VENDOR_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: EXPANSION_ROM_BASE_ADDRESS
 ***************************************************************************/
/* PCIE_CFG :: EXPANSION_ROM_BASE_ADDRESS :: ROM_BASE_ADDRESS [31:16] */
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_BASE_ADDRESS_MASK  0xffff0000
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_BASE_ADDRESS_ALIGN 0
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_BASE_ADDRESS_BITS  16
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_BASE_ADDRESS_SHIFT 16

/* PCIE_CFG :: EXPANSION_ROM_BASE_ADDRESS :: ROM_SIZE_INDICATION [15:11] */
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_SIZE_INDICATION_MASK 0x0000f800
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_SIZE_INDICATION_ALIGN 0
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_SIZE_INDICATION_BITS 5
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_ROM_SIZE_INDICATION_SHIFT 11

/* PCIE_CFG :: EXPANSION_ROM_BASE_ADDRESS :: RESERVED_0 [10:01] */
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_RESERVED_0_MASK        0x000007fe
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_RESERVED_0_ALIGN       0
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_RESERVED_0_BITS        10
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_RESERVED_0_SHIFT       1

/* PCIE_CFG :: EXPANSION_ROM_BASE_ADDRESS :: EXPANSION_ROM_ENABLE [00:00] */
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_EXPANSION_ROM_ENABLE_MASK 0x00000001
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_EXPANSION_ROM_ENABLE_ALIGN 0
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_EXPANSION_ROM_ENABLE_BITS 1
#define PCIE_CFG_EXPANSION_ROM_BASE_ADDRESS_EXPANSION_ROM_ENABLE_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: CAPABILITIES_POINTER
 ***************************************************************************/
/* PCIE_CFG :: CAPABILITIES_POINTER :: CAPABILITIES_POINTER [31:00] */
#define PCIE_CFG_CAPABILITIES_POINTER_CAPABILITIES_POINTER_MASK    0xffffffff
#define PCIE_CFG_CAPABILITIES_POINTER_CAPABILITIES_POINTER_ALIGN   0
#define PCIE_CFG_CAPABILITIES_POINTER_CAPABILITIES_POINTER_BITS    32
#define PCIE_CFG_CAPABILITIES_POINTER_CAPABILITIES_POINTER_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: INTERRUPT
 ***************************************************************************/
/* PCIE_CFG :: INTERRUPT :: RESERVED_0 [31:16] */
#define PCIE_CFG_INTERRUPT_RESERVED_0_MASK                         0xffff0000
#define PCIE_CFG_INTERRUPT_RESERVED_0_ALIGN                        0
#define PCIE_CFG_INTERRUPT_RESERVED_0_BITS                         16
#define PCIE_CFG_INTERRUPT_RESERVED_0_SHIFT                        16

/* PCIE_CFG :: INTERRUPT :: INTERRUPT_PIN [15:08] */
#define PCIE_CFG_INTERRUPT_INTERRUPT_PIN_MASK                      0x0000ff00
#define PCIE_CFG_INTERRUPT_INTERRUPT_PIN_ALIGN                     0
#define PCIE_CFG_INTERRUPT_INTERRUPT_PIN_BITS                      8
#define PCIE_CFG_INTERRUPT_INTERRUPT_PIN_SHIFT                     8

/* PCIE_CFG :: INTERRUPT :: INTERRUPT_LINE [07:00] */
#define PCIE_CFG_INTERRUPT_INTERRUPT_LINE_MASK                     0x000000ff
#define PCIE_CFG_INTERRUPT_INTERRUPT_LINE_ALIGN                    0
#define PCIE_CFG_INTERRUPT_INTERRUPT_LINE_BITS                     8
#define PCIE_CFG_INTERRUPT_INTERRUPT_LINE_SHIFT                    0


/****************************************************************************
 * PCIE_CFG :: VPD_CAPABILITIES
 ***************************************************************************/
/* PCIE_CFG :: VPD_CAPABILITIES :: RESERVED_0 [31:00] */
#define PCIE_CFG_VPD_CAPABILITIES_RESERVED_0_MASK                  0xffffffff
#define PCIE_CFG_VPD_CAPABILITIES_RESERVED_0_ALIGN                 0
#define PCIE_CFG_VPD_CAPABILITIES_RESERVED_0_BITS                  32
#define PCIE_CFG_VPD_CAPABILITIES_RESERVED_0_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: VPD_DATA
 ***************************************************************************/
/* PCIE_CFG :: VPD_DATA :: RESERVED_0 [31:00] */
#define PCIE_CFG_VPD_DATA_RESERVED_0_MASK                          0xffffffff
#define PCIE_CFG_VPD_DATA_RESERVED_0_ALIGN                         0
#define PCIE_CFG_VPD_DATA_RESERVED_0_BITS                          32
#define PCIE_CFG_VPD_DATA_RESERVED_0_SHIFT                         0


/****************************************************************************
 * PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY
 ***************************************************************************/
/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: PME_SUPPORT [31:27] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_SUPPORT_MASK      0xf8000000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_SUPPORT_ALIGN     0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_SUPPORT_BITS      5
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_SUPPORT_SHIFT     27

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: D2_SUPPORT [26:26] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D2_SUPPORT_MASK       0x04000000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D2_SUPPORT_ALIGN      0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D2_SUPPORT_BITS       1
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D2_SUPPORT_SHIFT      26

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: D1_SUPPORT [25:25] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D1_SUPPORT_MASK       0x02000000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D1_SUPPORT_ALIGN      0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D1_SUPPORT_BITS       1
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_D1_SUPPORT_SHIFT      25

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: AUX_CURRENT [24:22] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_AUX_CURRENT_MASK      0x01c00000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_AUX_CURRENT_ALIGN     0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_AUX_CURRENT_BITS      3
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_AUX_CURRENT_SHIFT     22

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: DSI [21:21] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_DSI_MASK              0x00200000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_DSI_ALIGN             0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_DSI_BITS              1
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_DSI_SHIFT             21

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: RESERVED_0 [20:20] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_RESERVED_0_MASK       0x00100000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_RESERVED_0_ALIGN      0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_RESERVED_0_BITS       1
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_RESERVED_0_SHIFT      20

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: PME_CLOCK [19:19] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_CLOCK_MASK        0x00080000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_CLOCK_ALIGN       0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_CLOCK_BITS        1
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_PME_CLOCK_SHIFT       19

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: VERSION [18:16] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_VERSION_MASK          0x00070000
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_VERSION_ALIGN         0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_VERSION_BITS          3
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_VERSION_SHIFT         16

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: NEXT_POINTER [15:08] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_NEXT_POINTER_MASK     0x0000ff00
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_NEXT_POINTER_ALIGN    0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_NEXT_POINTER_BITS     8
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_NEXT_POINTER_SHIFT    8

/* PCIE_CFG :: POWER_MANAGEMENT_CAPABILITY :: CAPABILITY_ID [07:00] */
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_CAPABILITY_ID_MASK    0x000000ff
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_CAPABILITY_ID_ALIGN   0
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_CAPABILITY_ID_BITS    8
#define PCIE_CFG_POWER_MANAGEMENT_CAPABILITY_CAPABILITY_ID_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS
 ***************************************************************************/
/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: PM_DATA [31:24] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PM_DATA_MASK      0xff000000
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PM_DATA_ALIGN     0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PM_DATA_BITS      8
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PM_DATA_SHIFT     24

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: RESERVED_0 [23:16] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_0_MASK   0x00ff0000
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_0_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_0_BITS   8
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_0_SHIFT  16

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: PME_STATUS [15:15] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_STATUS_MASK   0x00008000
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_STATUS_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_STATUS_BITS   1
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_STATUS_SHIFT  15

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: DATA_SCALE [14:13] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SCALE_MASK   0x00006000
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SCALE_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SCALE_BITS   2
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SCALE_SHIFT  13

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: DATA_SELECT [12:09] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SELECT_MASK  0x00001e00
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SELECT_ALIGN 0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SELECT_BITS  4
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_DATA_SELECT_SHIFT 9

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: PME_ENABLE [08:08] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_ENABLE_MASK   0x00000100
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_ENABLE_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_ENABLE_BITS   1
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_PME_ENABLE_SHIFT  8

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: RESERVED_1 [07:04] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_1_MASK   0x000000f0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_1_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_1_BITS   4
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_1_SHIFT  4

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: NO_SOFT_RESET [03:03] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_NO_SOFT_RESET_MASK 0x00000008
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_NO_SOFT_RESET_ALIGN 0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_NO_SOFT_RESET_BITS 1
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_NO_SOFT_RESET_SHIFT 3

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: RESERVED_2 [02:02] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_2_MASK   0x00000004
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_2_ALIGN  0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_2_BITS   1
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_RESERVED_2_SHIFT  2

/* PCIE_CFG :: POWER_MANAGEMENT_CONTROL_STATUS :: POWER_STATE [01:00] */
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_POWER_STATE_MASK  0x00000003
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_POWER_STATE_ALIGN 0
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_POWER_STATE_BITS  2
#define PCIE_CFG_POWER_MANAGEMENT_CONTROL_STATUS_POWER_STATE_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: MSI_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: MSI_CONTROL [31:24] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_CONTROL_MASK            0xff000000
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_CONTROL_ALIGN           0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_CONTROL_BITS            8
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_CONTROL_SHIFT           24

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: ADDRESS_CAPABLE_64_BIT [23:23] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_ADDRESS_CAPABLE_64_BIT_MASK 0x00800000
#define PCIE_CFG_MSI_CAPABILITY_HEADER_ADDRESS_CAPABLE_64_BIT_ALIGN 0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_ADDRESS_CAPABLE_64_BIT_BITS 1
#define PCIE_CFG_MSI_CAPABILITY_HEADER_ADDRESS_CAPABLE_64_BIT_SHIFT 23

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: MULTIPLE_MESSAGE_ENABLE [22:20] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_ENABLE_MASK 0x00700000
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_ENABLE_ALIGN 0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_ENABLE_BITS 3
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_ENABLE_SHIFT 20

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: MULTIPLE_MESSAGE_CAPABLE [19:17] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_CAPABLE_MASK 0x000e0000
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_CAPABLE_ALIGN 0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_CAPABLE_BITS 3
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MULTIPLE_MESSAGE_CAPABLE_SHIFT 17

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: MSI_ENABLE [16:16] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_ENABLE_MASK             0x00010000
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_ENABLE_ALIGN            0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_ENABLE_BITS             1
#define PCIE_CFG_MSI_CAPABILITY_HEADER_MSI_ENABLE_SHIFT            16

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: NEXT_POINTER [15:08] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_NEXT_POINTER_MASK           0x0000ff00
#define PCIE_CFG_MSI_CAPABILITY_HEADER_NEXT_POINTER_ALIGN          0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_NEXT_POINTER_BITS           8
#define PCIE_CFG_MSI_CAPABILITY_HEADER_NEXT_POINTER_SHIFT          8

/* PCIE_CFG :: MSI_CAPABILITY_HEADER :: CAPABILITY_ID [07:00] */
#define PCIE_CFG_MSI_CAPABILITY_HEADER_CAPABILITY_ID_MASK          0x000000ff
#define PCIE_CFG_MSI_CAPABILITY_HEADER_CAPABILITY_ID_ALIGN         0
#define PCIE_CFG_MSI_CAPABILITY_HEADER_CAPABILITY_ID_BITS          8
#define PCIE_CFG_MSI_CAPABILITY_HEADER_CAPABILITY_ID_SHIFT         0


/****************************************************************************
 * PCIE_CFG :: MSI_LOWER_ADDRESS
 ***************************************************************************/
/* PCIE_CFG :: MSI_LOWER_ADDRESS :: MSI_LOWER_ADDRESS [31:02] */
#define PCIE_CFG_MSI_LOWER_ADDRESS_MSI_LOWER_ADDRESS_MASK          0xfffffffc
#define PCIE_CFG_MSI_LOWER_ADDRESS_MSI_LOWER_ADDRESS_ALIGN         0
#define PCIE_CFG_MSI_LOWER_ADDRESS_MSI_LOWER_ADDRESS_BITS          30
#define PCIE_CFG_MSI_LOWER_ADDRESS_MSI_LOWER_ADDRESS_SHIFT         2

/* PCIE_CFG :: MSI_LOWER_ADDRESS :: RESERVED_0 [01:00] */
#define PCIE_CFG_MSI_LOWER_ADDRESS_RESERVED_0_MASK                 0x00000003
#define PCIE_CFG_MSI_LOWER_ADDRESS_RESERVED_0_ALIGN                0
#define PCIE_CFG_MSI_LOWER_ADDRESS_RESERVED_0_BITS                 2
#define PCIE_CFG_MSI_LOWER_ADDRESS_RESERVED_0_SHIFT                0


/****************************************************************************
 * PCIE_CFG :: MSI_UPPER_ADDRESS_REGISTER
 ***************************************************************************/
/* PCIE_CFG :: MSI_UPPER_ADDRESS_REGISTER :: MSI_UPPER_ADDRESS [31:00] */
#define PCIE_CFG_MSI_UPPER_ADDRESS_REGISTER_MSI_UPPER_ADDRESS_MASK 0xffffffff
#define PCIE_CFG_MSI_UPPER_ADDRESS_REGISTER_MSI_UPPER_ADDRESS_ALIGN 0
#define PCIE_CFG_MSI_UPPER_ADDRESS_REGISTER_MSI_UPPER_ADDRESS_BITS 32
#define PCIE_CFG_MSI_UPPER_ADDRESS_REGISTER_MSI_UPPER_ADDRESS_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: MSI_DATA
 ***************************************************************************/
/* PCIE_CFG :: MSI_DATA :: RESERVED_0 [31:16] */
#define PCIE_CFG_MSI_DATA_RESERVED_0_MASK                          0xffff0000
#define PCIE_CFG_MSI_DATA_RESERVED_0_ALIGN                         0
#define PCIE_CFG_MSI_DATA_RESERVED_0_BITS                          16
#define PCIE_CFG_MSI_DATA_RESERVED_0_SHIFT                         16

/* PCIE_CFG :: MSI_DATA :: MSI_DATA [15:00] */
#define PCIE_CFG_MSI_DATA_MSI_DATA_MASK                            0x0000ffff
#define PCIE_CFG_MSI_DATA_MSI_DATA_ALIGN                           0
#define PCIE_CFG_MSI_DATA_MSI_DATA_BITS                            16
#define PCIE_CFG_MSI_DATA_MSI_DATA_SHIFT                           0


/****************************************************************************
 * PCIE_CFG :: BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER :: RESERVED_0 [31:24] */
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_RESERVED_0_MASK 0xff000000
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_RESERVED_0_ALIGN 0
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_RESERVED_0_BITS 8
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_RESERVED_0_SHIFT 24

/* PCIE_CFG :: BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER :: VENDOR_SPECIFIC_CAPABILITY_LENGTH [23:16] */
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_VENDOR_SPECIFIC_CAPABILITY_LENGTH_MASK 0x00ff0000
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_VENDOR_SPECIFIC_CAPABILITY_LENGTH_ALIGN 0
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_VENDOR_SPECIFIC_CAPABILITY_LENGTH_BITS 8
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_VENDOR_SPECIFIC_CAPABILITY_LENGTH_SHIFT 16

/* PCIE_CFG :: BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER :: NEXT_POINTER [15:08] */
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_NEXT_POINTER_MASK 0x0000ff00
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_NEXT_POINTER_ALIGN 0
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_NEXT_POINTER_BITS 8
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_NEXT_POINTER_SHIFT 8

/* PCIE_CFG :: BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER :: CAPABILITY_ID [07:00] */
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_CAPABILITY_ID_MASK 0x000000ff
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_CAPABILITY_ID_ALIGN 0
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_CAPABILITY_ID_BITS 8
#define PCIE_CFG_BROADCOM_VENDOR_SPECIFIC_CAPABILITY_HEADER_CAPABILITY_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES
 ***************************************************************************/
/* PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES :: POR_RESET_COUNTER [31:28] */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_POR_RESET_COUNTER_MASK 0xf0000000
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_POR_RESET_COUNTER_ALIGN 0
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_POR_RESET_COUNTER_BITS 4
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_POR_RESET_COUNTER_SHIFT 28

/* PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES :: HOT_RESET_COUNTER [27:24] */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_HOT_RESET_COUNTER_MASK 0x0f000000
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_HOT_RESET_COUNTER_ALIGN 0
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_HOT_RESET_COUNTER_BITS 4
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_HOT_RESET_COUNTER_SHIFT 24

/* PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES :: GRC_RESET_COUNTER [23:16] */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_GRC_RESET_COUNTER_MASK 0x00ff0000
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_GRC_RESET_COUNTER_ALIGN 0
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_GRC_RESET_COUNTER_BITS 8
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_GRC_RESET_COUNTER_SHIFT 16

/* PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES :: PERST_RESET_COUNTER [15:08] */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_PERST_RESET_COUNTER_MASK 0x0000ff00
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_PERST_RESET_COUNTER_ALIGN 0
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_PERST_RESET_COUNTER_BITS 8
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_PERST_RESET_COUNTER_SHIFT 8

/* PCIE_CFG :: RESET_COUNTERS_INITIAL_VALUES :: LINKDOWN_RESET_COUNTER [07:00] */
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_LINKDOWN_RESET_COUNTER_MASK 0x000000ff
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_LINKDOWN_RESET_COUNTER_ALIGN 0
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_LINKDOWN_RESET_COUNTER_BITS 8
#define PCIE_CFG_RESET_COUNTERS_INITIAL_VALUES_LINKDOWN_RESET_COUNTER_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: PRODUCT_ID [31:24] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_PRODUCT_ID_MASK        0xff000000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_PRODUCT_ID_ALIGN       0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_PRODUCT_ID_BITS        8
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_PRODUCT_ID_SHIFT       24

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ASIC_REVISION_ID [23:16] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ASIC_REVISION_ID_MASK  0x00ff0000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ASIC_REVISION_ID_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ASIC_REVISION_ID_BITS  8
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ASIC_REVISION_ID_SHIFT 16

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_TLP_MINOR_ERROR_TOLERANCE [15:15] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TLP_MINOR_ERROR_TOLERANCE_MASK 0x00008000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TLP_MINOR_ERROR_TOLERANCE_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TLP_MINOR_ERROR_TOLERANCE_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TLP_MINOR_ERROR_TOLERANCE_SHIFT 15

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: LOG_HEADER_OVERFLOW [14:14] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_LOG_HEADER_OVERFLOW_MASK 0x00004000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_LOG_HEADER_OVERFLOW_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_LOG_HEADER_OVERFLOW_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_LOG_HEADER_OVERFLOW_SHIFT 14

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: BOUNDARY_CHECK [13:13] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BOUNDARY_CHECK_MASK    0x00002000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BOUNDARY_CHECK_ALIGN   0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BOUNDARY_CHECK_BITS    1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BOUNDARY_CHECK_SHIFT   13

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: BYTE_ENABLE_RULE_CHECK [12:12] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BYTE_ENABLE_RULE_CHECK_MASK 0x00001000
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BYTE_ENABLE_RULE_CHECK_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BYTE_ENABLE_RULE_CHECK_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_BYTE_ENABLE_RULE_CHECK_SHIFT 12

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: INTERRUPT_CHECK [11:11] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_INTERRUPT_CHECK_MASK   0x00000800
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_INTERRUPT_CHECK_ALIGN  0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_INTERRUPT_CHECK_BITS   1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_INTERRUPT_CHECK_SHIFT  11

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: RCB_CHECK [10:10] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_RCB_CHECK_MASK         0x00000400
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_RCB_CHECK_ALIGN        0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_RCB_CHECK_BITS         1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_RCB_CHECK_SHIFT        10

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_TAGGED_STATUS_MODE [09:09] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TAGGED_STATUS_MODE_MASK 0x00000200
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TAGGED_STATUS_MODE_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TAGGED_STATUS_MODE_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_TAGGED_STATUS_MODE_SHIFT 9

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: MASK_INTERRUPT_MODE [08:08] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_MODE_MASK 0x00000100
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_MODE_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_MODE_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_MODE_SHIFT 8

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_INDIRECT_ACCESS [07:07] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_INDIRECT_ACCESS_MASK 0x00000080
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_INDIRECT_ACCESS_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_INDIRECT_ACCESS_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_INDIRECT_ACCESS_SHIFT 7

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_REGISTER_WORD_SWAP [06:06] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_REGISTER_WORD_SWAP_MASK 0x00000040
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_REGISTER_WORD_SWAP_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_REGISTER_WORD_SWAP_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_REGISTER_WORD_SWAP_SHIFT 6

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_CLOCK_CONTROL_REGISTER_READ_WRITE_CAPABILITY [05:05] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_CLOCK_CONTROL_REGISTER_READ_WRITE_CAPABILITY_MASK 0x00000020
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_CLOCK_CONTROL_REGISTER_READ_WRITE_CAPABILITY_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_CLOCK_CONTROL_REGISTER_READ_WRITE_CAPABILITY_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_CLOCK_CONTROL_REGISTER_READ_WRITE_CAPABILITY_SHIFT 5

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_PCI_STATE_REGISTER_READ_WRITE_CAPABILITY [04:04] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_PCI_STATE_REGISTER_READ_WRITE_CAPABILITY_MASK 0x00000010
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_PCI_STATE_REGISTER_READ_WRITE_CAPABILITY_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_PCI_STATE_REGISTER_READ_WRITE_CAPABILITY_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_PCI_STATE_REGISTER_READ_WRITE_CAPABILITY_SHIFT 4

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_ENDIAN_WORD_SWAP [03:03] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_WORD_SWAP_MASK 0x00000008
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_WORD_SWAP_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_WORD_SWAP_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_WORD_SWAP_SHIFT 3

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: ENABLE_ENDIAN_BYTE_SWAP [02:02] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_BYTE_SWAP_MASK 0x00000004
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_BYTE_SWAP_ALIGN 0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_BYTE_SWAP_BITS 1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_ENABLE_ENDIAN_BYTE_SWAP_SHIFT 2

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: MASK_INTERRUPT [01:01] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_MASK    0x00000002
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_ALIGN   0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_BITS    1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_MASK_INTERRUPT_SHIFT   1

/* PCIE_CFG :: MISCELLANEOUS_HOST_CONTROL :: CLEAR_INTERRUPT [00:00] */
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_CLEAR_INTERRUPT_MASK   0x00000001
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_CLEAR_INTERRUPT_ALIGN  0
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_CLEAR_INTERRUPT_BITS   1
#define PCIE_CFG_MISCELLANEOUS_HOST_CONTROL_CLEAR_INTERRUPT_SHIFT  0


/****************************************************************************
 * PCIE_CFG :: SPARE
 ***************************************************************************/
/* PCIE_CFG :: SPARE :: UNUSED_0 [31:16] */
#define PCIE_CFG_SPARE_UNUSED_0_MASK                               0xffff0000
#define PCIE_CFG_SPARE_UNUSED_0_ALIGN                              0
#define PCIE_CFG_SPARE_UNUSED_0_BITS                               16
#define PCIE_CFG_SPARE_UNUSED_0_SHIFT                              16

/* PCIE_CFG :: SPARE :: RESERVED_0 [15:15] */
#define PCIE_CFG_SPARE_RESERVED_0_MASK                             0x00008000
#define PCIE_CFG_SPARE_RESERVED_0_ALIGN                            0
#define PCIE_CFG_SPARE_RESERVED_0_BITS                             1
#define PCIE_CFG_SPARE_RESERVED_0_SHIFT                            15

/* PCIE_CFG :: SPARE :: UNUSED_1 [14:02] */
#define PCIE_CFG_SPARE_UNUSED_1_MASK                               0x00007ffc
#define PCIE_CFG_SPARE_UNUSED_1_ALIGN                              0
#define PCIE_CFG_SPARE_UNUSED_1_BITS                               13
#define PCIE_CFG_SPARE_UNUSED_1_SHIFT                              2

/* PCIE_CFG :: SPARE :: BAR2_TARGET_WORD_SWAP [01:01] */
#define PCIE_CFG_SPARE_BAR2_TARGET_WORD_SWAP_MASK                  0x00000002
#define PCIE_CFG_SPARE_BAR2_TARGET_WORD_SWAP_ALIGN                 0
#define PCIE_CFG_SPARE_BAR2_TARGET_WORD_SWAP_BITS                  1
#define PCIE_CFG_SPARE_BAR2_TARGET_WORD_SWAP_SHIFT                 1

/* PCIE_CFG :: SPARE :: BAR2_TARGET_BYTE_SWAP [00:00] */
#define PCIE_CFG_SPARE_BAR2_TARGET_BYTE_SWAP_MASK                  0x00000001
#define PCIE_CFG_SPARE_BAR2_TARGET_BYTE_SWAP_ALIGN                 0
#define PCIE_CFG_SPARE_BAR2_TARGET_BYTE_SWAP_BITS                  1
#define PCIE_CFG_SPARE_BAR2_TARGET_BYTE_SWAP_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: PCI_STATE
 ***************************************************************************/
/* PCIE_CFG :: PCI_STATE :: RESERVED_0 [31:16] */
#define PCIE_CFG_PCI_STATE_RESERVED_0_MASK                         0xffff0000
#define PCIE_CFG_PCI_STATE_RESERVED_0_ALIGN                        0
#define PCIE_CFG_PCI_STATE_RESERVED_0_BITS                         16
#define PCIE_CFG_PCI_STATE_RESERVED_0_SHIFT                        16

/* PCIE_CFG :: PCI_STATE :: CONFIG_RETRY [15:15] */
#define PCIE_CFG_PCI_STATE_CONFIG_RETRY_MASK                       0x00008000
#define PCIE_CFG_PCI_STATE_CONFIG_RETRY_ALIGN                      0
#define PCIE_CFG_PCI_STATE_CONFIG_RETRY_BITS                       1
#define PCIE_CFG_PCI_STATE_CONFIG_RETRY_SHIFT                      15

/* PCIE_CFG :: PCI_STATE :: RESERVED_1 [14:12] */
#define PCIE_CFG_PCI_STATE_RESERVED_1_MASK                         0x00007000
#define PCIE_CFG_PCI_STATE_RESERVED_1_ALIGN                        0
#define PCIE_CFG_PCI_STATE_RESERVED_1_BITS                         3
#define PCIE_CFG_PCI_STATE_RESERVED_1_SHIFT                        12

/* PCIE_CFG :: PCI_STATE :: MAX_PCI_TARGET_RETRY [11:09] */
#define PCIE_CFG_PCI_STATE_MAX_PCI_TARGET_RETRY_MASK               0x00000e00
#define PCIE_CFG_PCI_STATE_MAX_PCI_TARGET_RETRY_ALIGN              0
#define PCIE_CFG_PCI_STATE_MAX_PCI_TARGET_RETRY_BITS               3
#define PCIE_CFG_PCI_STATE_MAX_PCI_TARGET_RETRY_SHIFT              9

/* PCIE_CFG :: PCI_STATE :: FLAT_VIEW [08:08] */
#define PCIE_CFG_PCI_STATE_FLAT_VIEW_MASK                          0x00000100
#define PCIE_CFG_PCI_STATE_FLAT_VIEW_ALIGN                         0
#define PCIE_CFG_PCI_STATE_FLAT_VIEW_BITS                          1
#define PCIE_CFG_PCI_STATE_FLAT_VIEW_SHIFT                         8

/* PCIE_CFG :: PCI_STATE :: VPD_AVAILABLE [07:07] */
#define PCIE_CFG_PCI_STATE_VPD_AVAILABLE_MASK                      0x00000080
#define PCIE_CFG_PCI_STATE_VPD_AVAILABLE_ALIGN                     0
#define PCIE_CFG_PCI_STATE_VPD_AVAILABLE_BITS                      1
#define PCIE_CFG_PCI_STATE_VPD_AVAILABLE_SHIFT                     7

/* PCIE_CFG :: PCI_STATE :: PCI_EXPANSION_ROM_RETRY [06:06] */
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_RETRY_MASK            0x00000040
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_RETRY_ALIGN           0
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_RETRY_BITS            1
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_RETRY_SHIFT           6

/* PCIE_CFG :: PCI_STATE :: PCI_EXPANSION_ROM_DESIRED [05:05] */
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_DESIRED_MASK          0x00000020
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_DESIRED_ALIGN         0
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_DESIRED_BITS          1
#define PCIE_CFG_PCI_STATE_PCI_EXPANSION_ROM_DESIRED_SHIFT         5

/* PCIE_CFG :: PCI_STATE :: RESERVED_2 [04:00] */
#define PCIE_CFG_PCI_STATE_RESERVED_2_MASK                         0x0000001f
#define PCIE_CFG_PCI_STATE_RESERVED_2_ALIGN                        0
#define PCIE_CFG_PCI_STATE_RESERVED_2_BITS                         5
#define PCIE_CFG_PCI_STATE_RESERVED_2_SHIFT                        0


/****************************************************************************
 * PCIE_CFG :: CLOCK_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: CLOCK_CONTROL :: PL_CLOCK_DISABLE [31:31] */
#define PCIE_CFG_CLOCK_CONTROL_PL_CLOCK_DISABLE_MASK               0x80000000
#define PCIE_CFG_CLOCK_CONTROL_PL_CLOCK_DISABLE_ALIGN              0
#define PCIE_CFG_CLOCK_CONTROL_PL_CLOCK_DISABLE_BITS               1
#define PCIE_CFG_CLOCK_CONTROL_PL_CLOCK_DISABLE_SHIFT              31

/* PCIE_CFG :: CLOCK_CONTROL :: DLL_CLOCK_DISABLE [30:30] */
#define PCIE_CFG_CLOCK_CONTROL_DLL_CLOCK_DISABLE_MASK              0x40000000
#define PCIE_CFG_CLOCK_CONTROL_DLL_CLOCK_DISABLE_ALIGN             0
#define PCIE_CFG_CLOCK_CONTROL_DLL_CLOCK_DISABLE_BITS              1
#define PCIE_CFG_CLOCK_CONTROL_DLL_CLOCK_DISABLE_SHIFT             30

/* PCIE_CFG :: CLOCK_CONTROL :: TL_CLOCK_DISABLE [29:29] */
#define PCIE_CFG_CLOCK_CONTROL_TL_CLOCK_DISABLE_MASK               0x20000000
#define PCIE_CFG_CLOCK_CONTROL_TL_CLOCK_DISABLE_ALIGN              0
#define PCIE_CFG_CLOCK_CONTROL_TL_CLOCK_DISABLE_BITS               1
#define PCIE_CFG_CLOCK_CONTROL_TL_CLOCK_DISABLE_SHIFT              29

/* PCIE_CFG :: CLOCK_CONTROL :: PCI_EXPRESS_CLOCK_TO_CORE_CLOCK [28:28] */
#define PCIE_CFG_CLOCK_CONTROL_PCI_EXPRESS_CLOCK_TO_CORE_CLOCK_MASK 0x10000000
#define PCIE_CFG_CLOCK_CONTROL_PCI_EXPRESS_CLOCK_TO_CORE_CLOCK_ALIGN 0
#define PCIE_CFG_CLOCK_CONTROL_PCI_EXPRESS_CLOCK_TO_CORE_CLOCK_BITS 1
#define PCIE_CFG_CLOCK_CONTROL_PCI_EXPRESS_CLOCK_TO_CORE_CLOCK_SHIFT 28

/* PCIE_CFG :: CLOCK_CONTROL :: UNUSED_0 [27:21] */
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_0_MASK                       0x0fe00000
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_0_ALIGN                      0
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_0_BITS                       7
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_0_SHIFT                      21

/* PCIE_CFG :: CLOCK_CONTROL :: SELECT_FINAL_ALT_CLOCK_SOURCE [20:20] */
#define PCIE_CFG_CLOCK_CONTROL_SELECT_FINAL_ALT_CLOCK_SOURCE_MASK  0x00100000
#define PCIE_CFG_CLOCK_CONTROL_SELECT_FINAL_ALT_CLOCK_SOURCE_ALIGN 0
#define PCIE_CFG_CLOCK_CONTROL_SELECT_FINAL_ALT_CLOCK_SOURCE_BITS  1
#define PCIE_CFG_CLOCK_CONTROL_SELECT_FINAL_ALT_CLOCK_SOURCE_SHIFT 20

/* PCIE_CFG :: CLOCK_CONTROL :: UNUSED_1 [19:13] */
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_1_MASK                       0x000fe000
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_1_ALIGN                      0
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_1_BITS                       7
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_1_SHIFT                      13

/* PCIE_CFG :: CLOCK_CONTROL :: SELECT_ALT_CLOCK [12:12] */
#define PCIE_CFG_CLOCK_CONTROL_SELECT_ALT_CLOCK_MASK               0x00001000
#define PCIE_CFG_CLOCK_CONTROL_SELECT_ALT_CLOCK_ALIGN              0
#define PCIE_CFG_CLOCK_CONTROL_SELECT_ALT_CLOCK_BITS               1
#define PCIE_CFG_CLOCK_CONTROL_SELECT_ALT_CLOCK_SHIFT              12

/* PCIE_CFG :: CLOCK_CONTROL :: UNUSED_2 [11:08] */
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_2_MASK                       0x00000f00
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_2_ALIGN                      0
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_2_BITS                       4
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_2_SHIFT                      8

/* PCIE_CFG :: CLOCK_CONTROL :: UNUSED_3 [07:00] */
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_3_MASK                       0x000000ff
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_3_ALIGN                      0
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_3_BITS                       8
#define PCIE_CFG_CLOCK_CONTROL_UNUSED_3_SHIFT                      0


/****************************************************************************
 * PCIE_CFG :: REGISTER_BASE
 ***************************************************************************/
/* PCIE_CFG :: REGISTER_BASE :: RESERVED_0 [31:18] */
#define PCIE_CFG_REGISTER_BASE_RESERVED_0_MASK                     0xfffc0000
#define PCIE_CFG_REGISTER_BASE_RESERVED_0_ALIGN                    0
#define PCIE_CFG_REGISTER_BASE_RESERVED_0_BITS                     14
#define PCIE_CFG_REGISTER_BASE_RESERVED_0_SHIFT                    18

/* PCIE_CFG :: REGISTER_BASE :: REGISTER_BASE_REGISTER [17:02] */
#define PCIE_CFG_REGISTER_BASE_REGISTER_BASE_REGISTER_MASK         0x0003fffc
#define PCIE_CFG_REGISTER_BASE_REGISTER_BASE_REGISTER_ALIGN        0
#define PCIE_CFG_REGISTER_BASE_REGISTER_BASE_REGISTER_BITS         16
#define PCIE_CFG_REGISTER_BASE_REGISTER_BASE_REGISTER_SHIFT        2

/* PCIE_CFG :: REGISTER_BASE :: RESERVED_1 [01:00] */
#define PCIE_CFG_REGISTER_BASE_RESERVED_1_MASK                     0x00000003
#define PCIE_CFG_REGISTER_BASE_RESERVED_1_ALIGN                    0
#define PCIE_CFG_REGISTER_BASE_RESERVED_1_BITS                     2
#define PCIE_CFG_REGISTER_BASE_RESERVED_1_SHIFT                    0


/****************************************************************************
 * PCIE_CFG :: MEMORY_BASE
 ***************************************************************************/
/* PCIE_CFG :: MEMORY_BASE :: RESERVED_0 [31:24] */
#define PCIE_CFG_MEMORY_BASE_RESERVED_0_MASK                       0xff000000
#define PCIE_CFG_MEMORY_BASE_RESERVED_0_ALIGN                      0
#define PCIE_CFG_MEMORY_BASE_RESERVED_0_BITS                       8
#define PCIE_CFG_MEMORY_BASE_RESERVED_0_SHIFT                      24

/* PCIE_CFG :: MEMORY_BASE :: MEMORY_BASE_REGISTER [23:02] */
#define PCIE_CFG_MEMORY_BASE_MEMORY_BASE_REGISTER_MASK             0x00fffffc
#define PCIE_CFG_MEMORY_BASE_MEMORY_BASE_REGISTER_ALIGN            0
#define PCIE_CFG_MEMORY_BASE_MEMORY_BASE_REGISTER_BITS             22
#define PCIE_CFG_MEMORY_BASE_MEMORY_BASE_REGISTER_SHIFT            2

/* PCIE_CFG :: MEMORY_BASE :: RESERVED_1 [01:00] */
#define PCIE_CFG_MEMORY_BASE_RESERVED_1_MASK                       0x00000003
#define PCIE_CFG_MEMORY_BASE_RESERVED_1_ALIGN                      0
#define PCIE_CFG_MEMORY_BASE_RESERVED_1_BITS                       2
#define PCIE_CFG_MEMORY_BASE_RESERVED_1_SHIFT                      0


/****************************************************************************
 * PCIE_CFG :: REGISTER_DATA
 ***************************************************************************/
/* PCIE_CFG :: REGISTER_DATA :: REGISTER_DATA_REGISTER [31:00] */
#define PCIE_CFG_REGISTER_DATA_REGISTER_DATA_REGISTER_MASK         0xffffffff
#define PCIE_CFG_REGISTER_DATA_REGISTER_DATA_REGISTER_ALIGN        0
#define PCIE_CFG_REGISTER_DATA_REGISTER_DATA_REGISTER_BITS         32
#define PCIE_CFG_REGISTER_DATA_REGISTER_DATA_REGISTER_SHIFT        0


/****************************************************************************
 * PCIE_CFG :: MEMORY_DATA
 ***************************************************************************/
/* PCIE_CFG :: MEMORY_DATA :: MEMORY_DATA_REGISTER [31:00] */
#define PCIE_CFG_MEMORY_DATA_MEMORY_DATA_REGISTER_MASK             0xffffffff
#define PCIE_CFG_MEMORY_DATA_MEMORY_DATA_REGISTER_ALIGN            0
#define PCIE_CFG_MEMORY_DATA_MEMORY_DATA_REGISTER_BITS             32
#define PCIE_CFG_MEMORY_DATA_MEMORY_DATA_REGISTER_SHIFT            0


/****************************************************************************
 * PCIE_CFG :: EXPANSION_ROM_BAR_SIZE
 ***************************************************************************/
/* PCIE_CFG :: EXPANSION_ROM_BAR_SIZE :: RESERVED_0 [31:04] */
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_RESERVED_0_MASK            0xfffffff0
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_RESERVED_0_ALIGN           0
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_RESERVED_0_BITS            28
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_RESERVED_0_SHIFT           4

/* PCIE_CFG :: EXPANSION_ROM_BAR_SIZE :: BAR_SIZE [03:00] */
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_BAR_SIZE_MASK              0x0000000f
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_BAR_SIZE_ALIGN             0
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_BAR_SIZE_BITS              4
#define PCIE_CFG_EXPANSION_ROM_BAR_SIZE_BAR_SIZE_SHIFT             0


/****************************************************************************
 * PCIE_CFG :: EXPANSION_ROM_ADDRESS
 ***************************************************************************/
/* PCIE_CFG :: EXPANSION_ROM_ADDRESS :: ROM_CTL_ADDR [31:00] */
#define PCIE_CFG_EXPANSION_ROM_ADDRESS_ROM_CTL_ADDR_MASK           0xffffffff
#define PCIE_CFG_EXPANSION_ROM_ADDRESS_ROM_CTL_ADDR_ALIGN          0
#define PCIE_CFG_EXPANSION_ROM_ADDRESS_ROM_CTL_ADDR_BITS           32
#define PCIE_CFG_EXPANSION_ROM_ADDRESS_ROM_CTL_ADDR_SHIFT          0


/****************************************************************************
 * PCIE_CFG :: EXPANSION_ROM_DATA
 ***************************************************************************/
/* PCIE_CFG :: EXPANSION_ROM_DATA :: ROM_DATA [31:00] */
#define PCIE_CFG_EXPANSION_ROM_DATA_ROM_DATA_MASK                  0xffffffff
#define PCIE_CFG_EXPANSION_ROM_DATA_ROM_DATA_ALIGN                 0
#define PCIE_CFG_EXPANSION_ROM_DATA_ROM_DATA_BITS                  32
#define PCIE_CFG_EXPANSION_ROM_DATA_ROM_DATA_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: VPD_INTERFACE
 ***************************************************************************/
/* PCIE_CFG :: VPD_INTERFACE :: RESERVED_0 [31:01] */
#define PCIE_CFG_VPD_INTERFACE_RESERVED_0_MASK                     0xfffffffe
#define PCIE_CFG_VPD_INTERFACE_RESERVED_0_ALIGN                    0
#define PCIE_CFG_VPD_INTERFACE_RESERVED_0_BITS                     31
#define PCIE_CFG_VPD_INTERFACE_RESERVED_0_SHIFT                    1

/* PCIE_CFG :: VPD_INTERFACE :: VPD_REQUEST [00:00] */
#define PCIE_CFG_VPD_INTERFACE_VPD_REQUEST_MASK                    0x00000001
#define PCIE_CFG_VPD_INTERFACE_VPD_REQUEST_ALIGN                   0
#define PCIE_CFG_VPD_INTERFACE_VPD_REQUEST_BITS                    1
#define PCIE_CFG_VPD_INTERFACE_VPD_REQUEST_SHIFT                   0


/****************************************************************************
 * PCIE_CFG :: UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER :: UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX [31:00] */
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_MASK 0xffffffff
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_ALIGN 0
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_BITS 32
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER :: UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX [31:00] */
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_MASK 0xffffffff
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_ALIGN 0
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_BITS 32
#define PCIE_CFG_UNDI_RECEIVE_BD_STANDARD_PRODUCER_RING_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_RECEIVE_BD_STANDARD_RING_PRODUCER_INDEX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER :: UNDI_RECEIVE_RETURN_C_IDX [31:00] */
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER_UNDI_RECEIVE_RETURN_C_IDX_MASK 0xffffffff
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER_UNDI_RECEIVE_RETURN_C_IDX_ALIGN 0
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER_UNDI_RECEIVE_RETURN_C_IDX_BITS 32
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_UPPER_UNDI_RECEIVE_RETURN_C_IDX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER :: UNDI_RECEIVE_RETURN_C_IDX [31:00] */
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER_UNDI_RECEIVE_RETURN_C_IDX_MASK 0xffffffff
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER_UNDI_RECEIVE_RETURN_C_IDX_ALIGN 0
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER_UNDI_RECEIVE_RETURN_C_IDX_BITS 32
#define PCIE_CFG_UNDI_RECEIVE_RETURN_RING_CONSUMER_INDEX_LOWER_UNDI_RECEIVE_RETURN_C_IDX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER :: UNDI_SEND_BD_NIC_P_IDX [31:00] */
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_SEND_BD_NIC_P_IDX_MASK 0xffffffff
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_SEND_BD_NIC_P_IDX_ALIGN 0
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_SEND_BD_NIC_P_IDX_BITS 32
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_UPPER_UNDI_SEND_BD_NIC_P_IDX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER
 ***************************************************************************/
/* PCIE_CFG :: UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER :: UNDI_SEND_BD_NIC_P_IDX [31:00] */
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_SEND_BD_NIC_P_IDX_MASK 0xffffffff
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_SEND_BD_NIC_P_IDX_ALIGN 0
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_SEND_BD_NIC_P_IDX_BITS 32
#define PCIE_CFG_UNDI_SEND_BD_PRODUCER_INDEX_MAILBOX_LOWER_UNDI_SEND_BD_NIC_P_IDX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: INT_MAILBOX_UPPER
 ***************************************************************************/
/* PCIE_CFG :: INT_MAILBOX_UPPER :: INDIRECT_INTERRUPT_MAIL_BOX [31:00] */
#define PCIE_CFG_INT_MAILBOX_UPPER_INDIRECT_INTERRUPT_MAIL_BOX_MASK 0xffffffff
#define PCIE_CFG_INT_MAILBOX_UPPER_INDIRECT_INTERRUPT_MAIL_BOX_ALIGN 0
#define PCIE_CFG_INT_MAILBOX_UPPER_INDIRECT_INTERRUPT_MAIL_BOX_BITS 32
#define PCIE_CFG_INT_MAILBOX_UPPER_INDIRECT_INTERRUPT_MAIL_BOX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: INT_MAILBOX_LOWER
 ***************************************************************************/
/* PCIE_CFG :: INT_MAILBOX_LOWER :: INDIRECT_INTERRUPT_MAIL_BOX [31:00] */
#define PCIE_CFG_INT_MAILBOX_LOWER_INDIRECT_INTERRUPT_MAIL_BOX_MASK 0xffffffff
#define PCIE_CFG_INT_MAILBOX_LOWER_INDIRECT_INTERRUPT_MAIL_BOX_ALIGN 0
#define PCIE_CFG_INT_MAILBOX_LOWER_INDIRECT_INTERRUPT_MAIL_BOX_BITS 32
#define PCIE_CFG_INT_MAILBOX_LOWER_INDIRECT_INTERRUPT_MAIL_BOX_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: PRODUCT_ID_AND_ASIC_REVISION
 ***************************************************************************/
/* PCIE_CFG :: PRODUCT_ID_AND_ASIC_REVISION :: RESERVED_0 [31:28] */
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_RESERVED_0_MASK      0xf0000000
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_RESERVED_0_ALIGN     0
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_RESERVED_0_BITS      4
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_RESERVED_0_SHIFT     28

/* PCIE_CFG :: PRODUCT_ID_AND_ASIC_REVISION :: PRODUCT_ID [27:08] */
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_PRODUCT_ID_MASK      0x0fffff00
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_PRODUCT_ID_ALIGN     0
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_PRODUCT_ID_BITS      20
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_PRODUCT_ID_SHIFT     8

/* PCIE_CFG :: PRODUCT_ID_AND_ASIC_REVISION :: ASIC_REVISION_ID [07:00] */
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_ASIC_REVISION_ID_MASK 0x000000ff
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_ASIC_REVISION_ID_ALIGN 0
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_ASIC_REVISION_ID_BITS 8
#define PCIE_CFG_PRODUCT_ID_AND_ASIC_REVISION_ASIC_REVISION_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: FUNCTION_EVENT
 ***************************************************************************/
/* PCIE_CFG :: FUNCTION_EVENT :: RESERVED_0 [31:16] */
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_0_MASK                    0xffff0000
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_0_ALIGN                   0
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_0_BITS                    16
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_0_SHIFT                   16

/* PCIE_CFG :: FUNCTION_EVENT :: INTA_EVENT [15:15] */
#define PCIE_CFG_FUNCTION_EVENT_INTA_EVENT_MASK                    0x00008000
#define PCIE_CFG_FUNCTION_EVENT_INTA_EVENT_ALIGN                   0
#define PCIE_CFG_FUNCTION_EVENT_INTA_EVENT_BITS                    1
#define PCIE_CFG_FUNCTION_EVENT_INTA_EVENT_SHIFT                   15

/* PCIE_CFG :: FUNCTION_EVENT :: RESERVED_1 [14:05] */
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_1_MASK                    0x00007fe0
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_1_ALIGN                   0
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_1_BITS                    10
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_1_SHIFT                   5

/* PCIE_CFG :: FUNCTION_EVENT :: GWAKE_EVENT [04:04] */
#define PCIE_CFG_FUNCTION_EVENT_GWAKE_EVENT_MASK                   0x00000010
#define PCIE_CFG_FUNCTION_EVENT_GWAKE_EVENT_ALIGN                  0
#define PCIE_CFG_FUNCTION_EVENT_GWAKE_EVENT_BITS                   1
#define PCIE_CFG_FUNCTION_EVENT_GWAKE_EVENT_SHIFT                  4

/* PCIE_CFG :: FUNCTION_EVENT :: RESERVED_2 [03:00] */
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_2_MASK                    0x0000000f
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_2_ALIGN                   0
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_2_BITS                    4
#define PCIE_CFG_FUNCTION_EVENT_RESERVED_2_SHIFT                   0


/****************************************************************************
 * PCIE_CFG :: FUNCTION_EVENT_MASK
 ***************************************************************************/
/* PCIE_CFG :: FUNCTION_EVENT_MASK :: RESERVED_0 [31:16] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_0_MASK               0xffff0000
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_0_ALIGN              0
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_0_BITS               16
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_0_SHIFT              16

/* PCIE_CFG :: FUNCTION_EVENT_MASK :: INTA_MASK [15:15] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_INTA_MASK_MASK                0x00008000
#define PCIE_CFG_FUNCTION_EVENT_MASK_INTA_MASK_ALIGN               0
#define PCIE_CFG_FUNCTION_EVENT_MASK_INTA_MASK_BITS                1
#define PCIE_CFG_FUNCTION_EVENT_MASK_INTA_MASK_SHIFT               15

/* PCIE_CFG :: FUNCTION_EVENT_MASK :: WAKE_UP_MASK [14:14] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_WAKE_UP_MASK_MASK             0x00004000
#define PCIE_CFG_FUNCTION_EVENT_MASK_WAKE_UP_MASK_ALIGN            0
#define PCIE_CFG_FUNCTION_EVENT_MASK_WAKE_UP_MASK_BITS             1
#define PCIE_CFG_FUNCTION_EVENT_MASK_WAKE_UP_MASK_SHIFT            14

/* PCIE_CFG :: FUNCTION_EVENT_MASK :: RESERVED_1 [13:05] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_1_MASK               0x00003fe0
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_1_ALIGN              0
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_1_BITS               9
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_1_SHIFT              5

/* PCIE_CFG :: FUNCTION_EVENT_MASK :: GWAKE_MASK [04:04] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_GWAKE_MASK_MASK               0x00000010
#define PCIE_CFG_FUNCTION_EVENT_MASK_GWAKE_MASK_ALIGN              0
#define PCIE_CFG_FUNCTION_EVENT_MASK_GWAKE_MASK_BITS               1
#define PCIE_CFG_FUNCTION_EVENT_MASK_GWAKE_MASK_SHIFT              4

/* PCIE_CFG :: FUNCTION_EVENT_MASK :: RESERVED_2 [03:00] */
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_2_MASK               0x0000000f
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_2_ALIGN              0
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_2_BITS               4
#define PCIE_CFG_FUNCTION_EVENT_MASK_RESERVED_2_SHIFT              0


/****************************************************************************
 * PCIE_CFG :: FUNCTION_PRESENT
 ***************************************************************************/
/* PCIE_CFG :: FUNCTION_PRESENT :: RESERVED_0 [31:16] */
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_0_MASK                  0xffff0000
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_0_ALIGN                 0
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_0_BITS                  16
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_0_SHIFT                 16

/* PCIE_CFG :: FUNCTION_PRESENT :: INTA_STATUS [15:15] */
#define PCIE_CFG_FUNCTION_PRESENT_INTA_STATUS_MASK                 0x00008000
#define PCIE_CFG_FUNCTION_PRESENT_INTA_STATUS_ALIGN                0
#define PCIE_CFG_FUNCTION_PRESENT_INTA_STATUS_BITS                 1
#define PCIE_CFG_FUNCTION_PRESENT_INTA_STATUS_SHIFT                15

/* PCIE_CFG :: FUNCTION_PRESENT :: RESERVED_1 [14:05] */
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_1_MASK                  0x00007fe0
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_1_ALIGN                 0
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_1_BITS                  10
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_1_SHIFT                 5

/* PCIE_CFG :: FUNCTION_PRESENT :: PME_STATUS [04:04] */
#define PCIE_CFG_FUNCTION_PRESENT_PME_STATUS_MASK                  0x00000010
#define PCIE_CFG_FUNCTION_PRESENT_PME_STATUS_ALIGN                 0
#define PCIE_CFG_FUNCTION_PRESENT_PME_STATUS_BITS                  1
#define PCIE_CFG_FUNCTION_PRESENT_PME_STATUS_SHIFT                 4

/* PCIE_CFG :: FUNCTION_PRESENT :: RESERVED_2 [03:00] */
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_2_MASK                  0x0000000f
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_2_ALIGN                 0
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_2_BITS                  4
#define PCIE_CFG_FUNCTION_PRESENT_RESERVED_2_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: PCIE_CAPABILITIES
 ***************************************************************************/
/* PCIE_CFG :: PCIE_CAPABILITIES :: RESERVED_0 [31:30] */
#define PCIE_CFG_PCIE_CAPABILITIES_RESERVED_0_MASK                 0xc0000000
#define PCIE_CFG_PCIE_CAPABILITIES_RESERVED_0_ALIGN                0
#define PCIE_CFG_PCIE_CAPABILITIES_RESERVED_0_BITS                 2
#define PCIE_CFG_PCIE_CAPABILITIES_RESERVED_0_SHIFT                30

/* PCIE_CFG :: PCIE_CAPABILITIES :: INTERRUPT_MESSAGE_NUMBER [29:25] */
#define PCIE_CFG_PCIE_CAPABILITIES_INTERRUPT_MESSAGE_NUMBER_MASK   0x3e000000
#define PCIE_CFG_PCIE_CAPABILITIES_INTERRUPT_MESSAGE_NUMBER_ALIGN  0
#define PCIE_CFG_PCIE_CAPABILITIES_INTERRUPT_MESSAGE_NUMBER_BITS   5
#define PCIE_CFG_PCIE_CAPABILITIES_INTERRUPT_MESSAGE_NUMBER_SHIFT  25

/* PCIE_CFG :: PCIE_CAPABILITIES :: SLOT_IMPLEMENTED [24:24] */
#define PCIE_CFG_PCIE_CAPABILITIES_SLOT_IMPLEMENTED_MASK           0x01000000
#define PCIE_CFG_PCIE_CAPABILITIES_SLOT_IMPLEMENTED_ALIGN          0
#define PCIE_CFG_PCIE_CAPABILITIES_SLOT_IMPLEMENTED_BITS           1
#define PCIE_CFG_PCIE_CAPABILITIES_SLOT_IMPLEMENTED_SHIFT          24

/* PCIE_CFG :: PCIE_CAPABILITIES :: DEVICE_PORT_TYPE [23:20] */
#define PCIE_CFG_PCIE_CAPABILITIES_DEVICE_PORT_TYPE_MASK           0x00f00000
#define PCIE_CFG_PCIE_CAPABILITIES_DEVICE_PORT_TYPE_ALIGN          0
#define PCIE_CFG_PCIE_CAPABILITIES_DEVICE_PORT_TYPE_BITS           4
#define PCIE_CFG_PCIE_CAPABILITIES_DEVICE_PORT_TYPE_SHIFT          20

/* PCIE_CFG :: PCIE_CAPABILITIES :: CAPABILITY_VERSION [19:16] */
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_VERSION_MASK         0x000f0000
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_VERSION_ALIGN        0
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_VERSION_BITS         4
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_VERSION_SHIFT        16

/* PCIE_CFG :: PCIE_CAPABILITIES :: NEXT_POINTER [15:08] */
#define PCIE_CFG_PCIE_CAPABILITIES_NEXT_POINTER_MASK               0x0000ff00
#define PCIE_CFG_PCIE_CAPABILITIES_NEXT_POINTER_ALIGN              0
#define PCIE_CFG_PCIE_CAPABILITIES_NEXT_POINTER_BITS               8
#define PCIE_CFG_PCIE_CAPABILITIES_NEXT_POINTER_SHIFT              8

/* PCIE_CFG :: PCIE_CAPABILITIES :: CAPABILITY_ID [07:00] */
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_ID_MASK              0x000000ff
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_ID_ALIGN             0
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_ID_BITS              8
#define PCIE_CFG_PCIE_CAPABILITIES_CAPABILITY_ID_SHIFT             0


/****************************************************************************
 * PCIE_CFG :: DEVICE_CAPABILITIES
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_CAPABILITIES :: RESERVED_0 [31:28] */
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_0_MASK               0xf0000000
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_0_ALIGN              0
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_0_BITS               4
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_0_SHIFT              28

/* PCIE_CFG :: DEVICE_CAPABILITIES :: CAPTURED_SLOT_POWER_LIMIT_SCALE [27:26] */
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_SCALE_MASK 0x0c000000
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_SCALE_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_SCALE_BITS 2
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_SCALE_SHIFT 26

/* PCIE_CFG :: DEVICE_CAPABILITIES :: CAPTURED_SLOT_POWER_LIMIT_VALUE [25:18] */
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_VALUE_MASK 0x03fc0000
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_VALUE_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_VALUE_BITS 8
#define PCIE_CFG_DEVICE_CAPABILITIES_CAPTURED_SLOT_POWER_LIMIT_VALUE_SHIFT 18

/* PCIE_CFG :: DEVICE_CAPABILITIES :: RESERVED_1 [17:16] */
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_1_MASK               0x00030000
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_1_ALIGN              0
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_1_BITS               2
#define PCIE_CFG_DEVICE_CAPABILITIES_RESERVED_1_SHIFT              16

/* PCIE_CFG :: DEVICE_CAPABILITIES :: ROLE_BASED_ERROR_SUPPORT [15:15] */
#define PCIE_CFG_DEVICE_CAPABILITIES_ROLE_BASED_ERROR_SUPPORT_MASK 0x00008000
#define PCIE_CFG_DEVICE_CAPABILITIES_ROLE_BASED_ERROR_SUPPORT_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_ROLE_BASED_ERROR_SUPPORT_BITS 1
#define PCIE_CFG_DEVICE_CAPABILITIES_ROLE_BASED_ERROR_SUPPORT_SHIFT 15

/* PCIE_CFG :: DEVICE_CAPABILITIES :: POWER_INDICATOR_PRESENT [14:14] */
#define PCIE_CFG_DEVICE_CAPABILITIES_POWER_INDICATOR_PRESENT_MASK  0x00004000
#define PCIE_CFG_DEVICE_CAPABILITIES_POWER_INDICATOR_PRESENT_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_POWER_INDICATOR_PRESENT_BITS  1
#define PCIE_CFG_DEVICE_CAPABILITIES_POWER_INDICATOR_PRESENT_SHIFT 14

/* PCIE_CFG :: DEVICE_CAPABILITIES :: ATTENTION_INDICATOR_PRESENT [13:13] */
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_INDICATOR_PRESENT_MASK 0x00002000
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_INDICATOR_PRESENT_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_INDICATOR_PRESENT_BITS 1
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_INDICATOR_PRESENT_SHIFT 13

/* PCIE_CFG :: DEVICE_CAPABILITIES :: ATTENTION_BUTTON_PRESENT [12:12] */
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_BUTTON_PRESENT_MASK 0x00001000
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_BUTTON_PRESENT_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_BUTTON_PRESENT_BITS 1
#define PCIE_CFG_DEVICE_CAPABILITIES_ATTENTION_BUTTON_PRESENT_SHIFT 12

/* PCIE_CFG :: DEVICE_CAPABILITIES :: ENDPOINT_L1_ACCEPTABLE_LATENCY [11:09] */
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L1_ACCEPTABLE_LATENCY_MASK 0x00000e00
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L1_ACCEPTABLE_LATENCY_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L1_ACCEPTABLE_LATENCY_BITS 3
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L1_ACCEPTABLE_LATENCY_SHIFT 9

/* PCIE_CFG :: DEVICE_CAPABILITIES :: ENDPOINT_L0S_ACCEPTABLE_LATENCY [08:06] */
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L0S_ACCEPTABLE_LATENCY_MASK 0x000001c0
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L0S_ACCEPTABLE_LATENCY_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L0S_ACCEPTABLE_LATENCY_BITS 3
#define PCIE_CFG_DEVICE_CAPABILITIES_ENDPOINT_L0S_ACCEPTABLE_LATENCY_SHIFT 6

/* PCIE_CFG :: DEVICE_CAPABILITIES :: EXTENDED_TAG_FIELD_SUPPORTED [05:05] */
#define PCIE_CFG_DEVICE_CAPABILITIES_EXTENDED_TAG_FIELD_SUPPORTED_MASK 0x00000020
#define PCIE_CFG_DEVICE_CAPABILITIES_EXTENDED_TAG_FIELD_SUPPORTED_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_EXTENDED_TAG_FIELD_SUPPORTED_BITS 1
#define PCIE_CFG_DEVICE_CAPABILITIES_EXTENDED_TAG_FIELD_SUPPORTED_SHIFT 5

/* PCIE_CFG :: DEVICE_CAPABILITIES :: PHANTOM_FUNCTIONS_SUPPORTED [04:03] */
#define PCIE_CFG_DEVICE_CAPABILITIES_PHANTOM_FUNCTIONS_SUPPORTED_MASK 0x00000018
#define PCIE_CFG_DEVICE_CAPABILITIES_PHANTOM_FUNCTIONS_SUPPORTED_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_PHANTOM_FUNCTIONS_SUPPORTED_BITS 2
#define PCIE_CFG_DEVICE_CAPABILITIES_PHANTOM_FUNCTIONS_SUPPORTED_SHIFT 3

/* PCIE_CFG :: DEVICE_CAPABILITIES :: MAX_PAYLOAD_SIZE_SUPPORTED [02:00] */
#define PCIE_CFG_DEVICE_CAPABILITIES_MAX_PAYLOAD_SIZE_SUPPORTED_MASK 0x00000007
#define PCIE_CFG_DEVICE_CAPABILITIES_MAX_PAYLOAD_SIZE_SUPPORTED_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_MAX_PAYLOAD_SIZE_SUPPORTED_BITS 3
#define PCIE_CFG_DEVICE_CAPABILITIES_MAX_PAYLOAD_SIZE_SUPPORTED_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: DEVICE_STATUS_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: RESERVED_0 [31:22] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_0_MASK             0xffc00000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_0_ALIGN            0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_0_BITS             10
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_0_SHIFT            22

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: TRANSACTION_PENDING [21:21] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_TRANSACTION_PENDING_MASK    0x00200000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_TRANSACTION_PENDING_ALIGN   0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_TRANSACTION_PENDING_BITS    1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_TRANSACTION_PENDING_SHIFT   21

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: AUX_POWER_DETECTED [20:20] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_DETECTED_MASK     0x00100000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_DETECTED_ALIGN    0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_DETECTED_BITS     1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_DETECTED_SHIFT    20

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: UNSUPPORTED_REQUEST_DETECTED [19:19] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_DETECTED_MASK 0x00080000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_DETECTED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_DETECTED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_DETECTED_SHIFT 19

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: FATAL_ERROR_DETECTED [18:18] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_DETECTED_MASK   0x00040000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_DETECTED_ALIGN  0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_DETECTED_BITS   1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_DETECTED_SHIFT  18

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: NON_FATAL_ERROR_DETECTED [17:17] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_DETECTED_MASK 0x00020000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_DETECTED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_DETECTED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_DETECTED_SHIFT 17

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: CORRECTABLE_ERROR_DETECTED [16:16] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_DETECTED_MASK 0x00010000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_DETECTED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_DETECTED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_DETECTED_SHIFT 16

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: RESERVED_1 [15:15] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_1_MASK             0x00008000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_1_ALIGN            0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_1_BITS             1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_RESERVED_1_SHIFT            15

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: MAX_READ_REQUEST_SIZE [14:12] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_READ_REQUEST_SIZE_MASK  0x00007000
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_READ_REQUEST_SIZE_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_READ_REQUEST_SIZE_BITS  3
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_READ_REQUEST_SIZE_SHIFT 12

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: ENABLE_NO_SNOOP [11:11] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLE_NO_SNOOP_MASK        0x00000800
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLE_NO_SNOOP_ALIGN       0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLE_NO_SNOOP_BITS        1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLE_NO_SNOOP_SHIFT       11

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: AUX_POWER_PM_ENABLE [10:10] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_PM_ENABLE_MASK    0x00000400
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_PM_ENABLE_ALIGN   0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_PM_ENABLE_BITS    1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_AUX_POWER_PM_ENABLE_SHIFT   10

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: PHANTOM_FUNCTIONS_ENABLE [09:09] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_PHANTOM_FUNCTIONS_ENABLE_MASK 0x00000200
#define PCIE_CFG_DEVICE_STATUS_CONTROL_PHANTOM_FUNCTIONS_ENABLE_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_PHANTOM_FUNCTIONS_ENABLE_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_PHANTOM_FUNCTIONS_ENABLE_SHIFT 9

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: EXTENDED_TAG_FIELD_ENABLE [08:08] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_EXTENDED_TAG_FIELD_ENABLE_MASK 0x00000100
#define PCIE_CFG_DEVICE_STATUS_CONTROL_EXTENDED_TAG_FIELD_ENABLE_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_EXTENDED_TAG_FIELD_ENABLE_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_EXTENDED_TAG_FIELD_ENABLE_SHIFT 8

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: MAX_PAYLOAD_SIZE [07:05] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_PAYLOAD_SIZE_MASK       0x000000e0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_PAYLOAD_SIZE_ALIGN      0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_PAYLOAD_SIZE_BITS       3
#define PCIE_CFG_DEVICE_STATUS_CONTROL_MAX_PAYLOAD_SIZE_SHIFT      5

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: ENABLED_RELAXED_ORDERING [04:04] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLED_RELAXED_ORDERING_MASK 0x00000010
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLED_RELAXED_ORDERING_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLED_RELAXED_ORDERING_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_ENABLED_RELAXED_ORDERING_SHIFT 4

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: UNSUPPORTED_REQUEST_REPORTING_ENABLE [03:03] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_REPORTING_ENABLE_MASK 0x00000008
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_REPORTING_ENABLE_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_REPORTING_ENABLE_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_UNSUPPORTED_REQUEST_REPORTING_ENABLE_SHIFT 3

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: FATAL_ERROR_REPORTING_ENABLED [02:02] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_REPORTING_ENABLED_MASK 0x00000004
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_REPORTING_ENABLED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_REPORTING_ENABLED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_FATAL_ERROR_REPORTING_ENABLED_SHIFT 2

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: NON_FATAL_ERROR_REPORTING_ENABLED [01:01] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_REPORTING_ENABLED_MASK 0x00000002
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_REPORTING_ENABLED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_REPORTING_ENABLED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_NON_FATAL_ERROR_REPORTING_ENABLED_SHIFT 1

/* PCIE_CFG :: DEVICE_STATUS_CONTROL :: CORRECTABLE_ERROR_REPORTING_ENABLED [00:00] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_REPORTING_ENABLED_MASK 0x00000001
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_REPORTING_ENABLED_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_REPORTING_ENABLED_BITS 1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_CORRECTABLE_ERROR_REPORTING_ENABLED_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: LINK_CAPABILITY
 ***************************************************************************/
/* PCIE_CFG :: LINK_CAPABILITY :: PORT_NUMBER [31:24] */
#define PCIE_CFG_LINK_CAPABILITY_PORT_NUMBER_MASK                  0xff000000
#define PCIE_CFG_LINK_CAPABILITY_PORT_NUMBER_ALIGN                 0
#define PCIE_CFG_LINK_CAPABILITY_PORT_NUMBER_BITS                  8
#define PCIE_CFG_LINK_CAPABILITY_PORT_NUMBER_SHIFT                 24

/* PCIE_CFG :: LINK_CAPABILITY :: RESERVED_0 [23:19] */
#define PCIE_CFG_LINK_CAPABILITY_RESERVED_0_MASK                   0x00f80000
#define PCIE_CFG_LINK_CAPABILITY_RESERVED_0_ALIGN                  0
#define PCIE_CFG_LINK_CAPABILITY_RESERVED_0_BITS                   5
#define PCIE_CFG_LINK_CAPABILITY_RESERVED_0_SHIFT                  19

/* PCIE_CFG :: LINK_CAPABILITY :: CLOCK_POWER_MANAGEMENT [18:18] */
#define PCIE_CFG_LINK_CAPABILITY_CLOCK_POWER_MANAGEMENT_MASK       0x00040000
#define PCIE_CFG_LINK_CAPABILITY_CLOCK_POWER_MANAGEMENT_ALIGN      0
#define PCIE_CFG_LINK_CAPABILITY_CLOCK_POWER_MANAGEMENT_BITS       1
#define PCIE_CFG_LINK_CAPABILITY_CLOCK_POWER_MANAGEMENT_SHIFT      18

/* PCIE_CFG :: LINK_CAPABILITY :: L1_EXIT_LATENCY [17:15] */
#define PCIE_CFG_LINK_CAPABILITY_L1_EXIT_LATENCY_MASK              0x00038000
#define PCIE_CFG_LINK_CAPABILITY_L1_EXIT_LATENCY_ALIGN             0
#define PCIE_CFG_LINK_CAPABILITY_L1_EXIT_LATENCY_BITS              3
#define PCIE_CFG_LINK_CAPABILITY_L1_EXIT_LATENCY_SHIFT             15

/* PCIE_CFG :: LINK_CAPABILITY :: L0S_EXIT_LATENCY [14:12] */
#define PCIE_CFG_LINK_CAPABILITY_L0S_EXIT_LATENCY_MASK             0x00007000
#define PCIE_CFG_LINK_CAPABILITY_L0S_EXIT_LATENCY_ALIGN            0
#define PCIE_CFG_LINK_CAPABILITY_L0S_EXIT_LATENCY_BITS             3
#define PCIE_CFG_LINK_CAPABILITY_L0S_EXIT_LATENCY_SHIFT            12

/* PCIE_CFG :: LINK_CAPABILITY :: ACTIVE_STATE_POWER_MANAGEMENT_SUPPORT [11:10] */
#define PCIE_CFG_LINK_CAPABILITY_ACTIVE_STATE_POWER_MANAGEMENT_SUPPORT_MASK 0x00000c00
#define PCIE_CFG_LINK_CAPABILITY_ACTIVE_STATE_POWER_MANAGEMENT_SUPPORT_ALIGN 0
#define PCIE_CFG_LINK_CAPABILITY_ACTIVE_STATE_POWER_MANAGEMENT_SUPPORT_BITS 2
#define PCIE_CFG_LINK_CAPABILITY_ACTIVE_STATE_POWER_MANAGEMENT_SUPPORT_SHIFT 10

/* PCIE_CFG :: LINK_CAPABILITY :: MAXIMUM_LINK_WIDTH [09:04] */
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_WIDTH_MASK           0x000003f0
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_WIDTH_ALIGN          0
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_WIDTH_BITS           6
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_WIDTH_SHIFT          4

/* PCIE_CFG :: LINK_CAPABILITY :: MAXIMUM_LINK_SPEED [03:00] */
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_SPEED_MASK           0x0000000f
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_SPEED_ALIGN          0
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_SPEED_BITS           4
#define PCIE_CFG_LINK_CAPABILITY_MAXIMUM_LINK_SPEED_SHIFT          0


/****************************************************************************
 * PCIE_CFG :: LINK_STATUS_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_0 [31:29] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_0_MASK               0xe0000000
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_0_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_0_BITS               3
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_0_SHIFT              29

/* PCIE_CFG :: LINK_STATUS_CONTROL :: SLOT_CLOCK_CONFIGURATION [28:28] */
#define PCIE_CFG_LINK_STATUS_CONTROL_SLOT_CLOCK_CONFIGURATION_MASK 0x10000000
#define PCIE_CFG_LINK_STATUS_CONTROL_SLOT_CLOCK_CONFIGURATION_ALIGN 0
#define PCIE_CFG_LINK_STATUS_CONTROL_SLOT_CLOCK_CONFIGURATION_BITS 1
#define PCIE_CFG_LINK_STATUS_CONTROL_SLOT_CLOCK_CONFIGURATION_SHIFT 28

/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_1 [27:26] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_1_MASK               0x0c000000
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_1_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_1_BITS               2
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_1_SHIFT              26

/* PCIE_CFG :: LINK_STATUS_CONTROL :: NEGOTIATED_LINK_WIDTH [25:20] */
#define PCIE_CFG_LINK_STATUS_CONTROL_NEGOTIATED_LINK_WIDTH_MASK    0x03f00000
#define PCIE_CFG_LINK_STATUS_CONTROL_NEGOTIATED_LINK_WIDTH_ALIGN   0
#define PCIE_CFG_LINK_STATUS_CONTROL_NEGOTIATED_LINK_WIDTH_BITS    6
#define PCIE_CFG_LINK_STATUS_CONTROL_NEGOTIATED_LINK_WIDTH_SHIFT   20

/* PCIE_CFG :: LINK_STATUS_CONTROL :: LINK_SPEED [19:16] */
#define PCIE_CFG_LINK_STATUS_CONTROL_LINK_SPEED_MASK               0x000f0000
#define PCIE_CFG_LINK_STATUS_CONTROL_LINK_SPEED_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_LINK_SPEED_BITS               4
#define PCIE_CFG_LINK_STATUS_CONTROL_LINK_SPEED_SHIFT              16

/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_2 [15:09] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_2_MASK               0x0000fe00
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_2_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_2_BITS               7
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_2_SHIFT              9

/* PCIE_CFG :: LINK_STATUS_CONTROL :: CLOCK_REQUEST_ENABLE [08:08] */
#define PCIE_CFG_LINK_STATUS_CONTROL_CLOCK_REQUEST_ENABLE_MASK     0x00000100
#define PCIE_CFG_LINK_STATUS_CONTROL_CLOCK_REQUEST_ENABLE_ALIGN    0
#define PCIE_CFG_LINK_STATUS_CONTROL_CLOCK_REQUEST_ENABLE_BITS     1
#define PCIE_CFG_LINK_STATUS_CONTROL_CLOCK_REQUEST_ENABLE_SHIFT    8

/* PCIE_CFG :: LINK_STATUS_CONTROL :: EXTENDED_SYNCH [07:07] */
#define PCIE_CFG_LINK_STATUS_CONTROL_EXTENDED_SYNCH_MASK           0x00000080
#define PCIE_CFG_LINK_STATUS_CONTROL_EXTENDED_SYNCH_ALIGN          0
#define PCIE_CFG_LINK_STATUS_CONTROL_EXTENDED_SYNCH_BITS           1
#define PCIE_CFG_LINK_STATUS_CONTROL_EXTENDED_SYNCH_SHIFT          7

/* PCIE_CFG :: LINK_STATUS_CONTROL :: COMMON_CLOCK_CONFIGURATION [06:06] */
#define PCIE_CFG_LINK_STATUS_CONTROL_COMMON_CLOCK_CONFIGURATION_MASK 0x00000040
#define PCIE_CFG_LINK_STATUS_CONTROL_COMMON_CLOCK_CONFIGURATION_ALIGN 0
#define PCIE_CFG_LINK_STATUS_CONTROL_COMMON_CLOCK_CONFIGURATION_BITS 1
#define PCIE_CFG_LINK_STATUS_CONTROL_COMMON_CLOCK_CONFIGURATION_SHIFT 6

/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_3 [05:05] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_3_MASK               0x00000020
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_3_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_3_BITS               1
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_3_SHIFT              5

/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_4 [04:04] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_4_MASK               0x00000010
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_4_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_4_BITS               1
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_4_SHIFT              4

/* PCIE_CFG :: LINK_STATUS_CONTROL :: READ_COMPLETION_BOUNDARY [03:03] */
#define PCIE_CFG_LINK_STATUS_CONTROL_READ_COMPLETION_BOUNDARY_MASK 0x00000008
#define PCIE_CFG_LINK_STATUS_CONTROL_READ_COMPLETION_BOUNDARY_ALIGN 0
#define PCIE_CFG_LINK_STATUS_CONTROL_READ_COMPLETION_BOUNDARY_BITS 1
#define PCIE_CFG_LINK_STATUS_CONTROL_READ_COMPLETION_BOUNDARY_SHIFT 3

/* PCIE_CFG :: LINK_STATUS_CONTROL :: RESERVED_5 [02:02] */
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_5_MASK               0x00000004
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_5_ALIGN              0
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_5_BITS               1
#define PCIE_CFG_LINK_STATUS_CONTROL_RESERVED_5_SHIFT              2

/* PCIE_CFG :: LINK_STATUS_CONTROL :: ACTIVE_STATE_POWER_MANAGEMENT_CONTROL [01:00] */
#define PCIE_CFG_LINK_STATUS_CONTROL_ACTIVE_STATE_POWER_MANAGEMENT_CONTROL_MASK 0x00000003
#define PCIE_CFG_LINK_STATUS_CONTROL_ACTIVE_STATE_POWER_MANAGEMENT_CONTROL_ALIGN 0
#define PCIE_CFG_LINK_STATUS_CONTROL_ACTIVE_STATE_POWER_MANAGEMENT_CONTROL_BITS 2
#define PCIE_CFG_LINK_STATUS_CONTROL_ACTIVE_STATE_POWER_MANAGEMENT_CONTROL_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: DEVICE_CAPABILITIES_2
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_CAPABILITIES_2 :: RESERVED_0 [31:05] */
#define PCIE_CFG_DEVICE_CAPABILITIES_2_RESERVED_0_MASK             0xffffffe0
#define PCIE_CFG_DEVICE_CAPABILITIES_2_RESERVED_0_ALIGN            0
#define PCIE_CFG_DEVICE_CAPABILITIES_2_RESERVED_0_BITS             27
#define PCIE_CFG_DEVICE_CAPABILITIES_2_RESERVED_0_SHIFT            5

/* PCIE_CFG :: DEVICE_CAPABILITIES_2 :: CPL_DISABLE_SUPPORTED [04:04] */
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_DISABLE_SUPPORTED_MASK  0x00000010
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_DISABLE_SUPPORTED_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_DISABLE_SUPPORTED_BITS  1
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_DISABLE_SUPPORTED_SHIFT 4

/* PCIE_CFG :: DEVICE_CAPABILITIES_2 :: CPL_TIMEOUT_RANGE_SUPPORTED [03:00] */
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_TIMEOUT_RANGE_SUPPORTED_MASK 0x0000000f
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_TIMEOUT_RANGE_SUPPORTED_ALIGN 0
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_TIMEOUT_RANGE_SUPPORTED_BITS 4
#define PCIE_CFG_DEVICE_CAPABILITIES_2_CPL_TIMEOUT_RANGE_SUPPORTED_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: DEVICE_STATUS_CONTROL_2
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_STATUS_CONTROL_2 :: RESERVED_0 [31:05] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_RESERVED_0_MASK           0xffffffe0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_RESERVED_0_ALIGN          0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_RESERVED_0_BITS           27
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_RESERVED_0_SHIFT          5

/* PCIE_CFG :: DEVICE_STATUS_CONTROL_2 :: CPL_TIMEOUT_DISABLE [04:04] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_DISABLE_MASK  0x00000010
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_DISABLE_ALIGN 0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_DISABLE_BITS  1
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_DISABLE_SHIFT 4

/* PCIE_CFG :: DEVICE_STATUS_CONTROL_2 :: CPL_TIMEOUT_VALUE [03:00] */
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_VALUE_MASK    0x0000000f
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_VALUE_ALIGN   0
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_VALUE_BITS    4
#define PCIE_CFG_DEVICE_STATUS_CONTROL_2_CPL_TIMEOUT_VALUE_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: LINK_CAPABILITIES_2
 ***************************************************************************/
/* PCIE_CFG :: LINK_CAPABILITIES_2 :: RESERVED_0 [31:00] */
#define PCIE_CFG_LINK_CAPABILITIES_2_RESERVED_0_MASK               0xffffffff
#define PCIE_CFG_LINK_CAPABILITIES_2_RESERVED_0_ALIGN              0
#define PCIE_CFG_LINK_CAPABILITIES_2_RESERVED_0_BITS               32
#define PCIE_CFG_LINK_CAPABILITIES_2_RESERVED_0_SHIFT              0


/****************************************************************************
 * PCIE_CFG :: LINK_STATUS_CONTROL_2
 ***************************************************************************/
/* PCIE_CFG :: LINK_STATUS_CONTROL_2 :: RESERVED_0 [31:00] */
#define PCIE_CFG_LINK_STATUS_CONTROL_2_RESERVED_0_MASK             0xffffffff
#define PCIE_CFG_LINK_STATUS_CONTROL_2_RESERVED_0_ALIGN            0
#define PCIE_CFG_LINK_STATUS_CONTROL_2_RESERVED_0_BITS             32
#define PCIE_CFG_LINK_STATUS_CONTROL_2_RESERVED_0_SHIFT            0


/****************************************************************************
 * PCIE_CFG :: ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER :: NEXT_CAPABILITY_OFFSET [31:20] */
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_MASK 0xfff00000
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_BITS 12
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_SHIFT 20

/* PCIE_CFG :: ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER :: CAPABILITY_VERSION [19:16] */
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_MASK 0x000f0000
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_BITS 4
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_SHIFT 16

/* PCIE_CFG :: ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER :: PCIE_EXTENDED_CAPABILITY_ID [15:00] */
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_MASK 0x0000ffff
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_BITS 16
#define PCIE_CFG_ADVANCED_ERROR_REPORTING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS
 ***************************************************************************/
/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: RESERVED_0 [31:21] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_0_MASK        0xffe00000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_0_ALIGN       0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_0_BITS        11
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_0_SHIFT       21

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: UNSUPPORTED_REQUEST_ERROR_STATUS [20:20] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNSUPPORTED_REQUEST_ERROR_STATUS_MASK 0x00100000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNSUPPORTED_REQUEST_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNSUPPORTED_REQUEST_ERROR_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNSUPPORTED_REQUEST_ERROR_STATUS_SHIFT 20

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: ECRC_ERROR_STATUS [19:19] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_ECRC_ERROR_STATUS_MASK 0x00080000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_ECRC_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_ECRC_ERROR_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_ECRC_ERROR_STATUS_SHIFT 19

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: MALFORMED_TLP_STATUS [18:18] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_MALFORMED_TLP_STATUS_MASK 0x00040000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_MALFORMED_TLP_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_MALFORMED_TLP_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_MALFORMED_TLP_STATUS_SHIFT 18

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: RECEIVER_OVERFLOW_STATUS [17:17] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RECEIVER_OVERFLOW_STATUS_MASK 0x00020000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RECEIVER_OVERFLOW_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RECEIVER_OVERFLOW_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RECEIVER_OVERFLOW_STATUS_SHIFT 17

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: UNEXPECTED_COMPLETION_STATUS [16:16] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNEXPECTED_COMPLETION_STATUS_MASK 0x00010000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNEXPECTED_COMPLETION_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNEXPECTED_COMPLETION_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_UNEXPECTED_COMPLETION_STATUS_SHIFT 16

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: COMPLETER_ABORT_STATUS [15:15] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETER_ABORT_STATUS_MASK 0x00008000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETER_ABORT_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETER_ABORT_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETER_ABORT_STATUS_SHIFT 15

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: COMPLETION_TIMEOUT_STATUS [14:14] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETION_TIMEOUT_STATUS_MASK 0x00004000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETION_TIMEOUT_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETION_TIMEOUT_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_COMPLETION_TIMEOUT_STATUS_SHIFT 14

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: FLOW_CONTROL_PROTOCOL_ERROR_STATUS [13:13] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_STATUS_MASK 0x00002000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_STATUS_SHIFT 13

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: POISONED_TLP_STATUS [12:12] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_POISONED_TLP_STATUS_MASK 0x00001000
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_POISONED_TLP_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_POISONED_TLP_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_POISONED_TLP_STATUS_SHIFT 12

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: RESERVED_1 [11:05] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_1_MASK        0x00000fe0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_1_ALIGN       0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_1_BITS        7
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_1_SHIFT       5

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: DATA_LINK_PROTOCOL_ERROR_STATUS [04:04] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_DATA_LINK_PROTOCOL_ERROR_STATUS_MASK 0x00000010
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_DATA_LINK_PROTOCOL_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_DATA_LINK_PROTOCOL_ERROR_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_DATA_LINK_PROTOCOL_ERROR_STATUS_SHIFT 4

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: RESERVED_2 [03:01] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_2_MASK        0x0000000e
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_2_ALIGN       0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_2_BITS        3
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_RESERVED_2_SHIFT       1

/* PCIE_CFG :: UNCORRECTABLE_ERROR_STATUS :: TRAINING_ERROR_STATUS [00:00] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_TRAINING_ERROR_STATUS_MASK 0x00000001
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_TRAINING_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_TRAINING_ERROR_STATUS_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_STATUS_TRAINING_ERROR_STATUS_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNCORRECTABLE_ERROR_MASK
 ***************************************************************************/
/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: RESERVED_0 [31:21] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_0_MASK          0xffe00000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_0_ALIGN         0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_0_BITS          11
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_0_SHIFT         21

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: UNSUPPORTED_REQUEST_ERROR_MASK [20:20] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNSUPPORTED_REQUEST_ERROR_MASK_MASK 0x00100000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNSUPPORTED_REQUEST_ERROR_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNSUPPORTED_REQUEST_ERROR_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNSUPPORTED_REQUEST_ERROR_MASK_SHIFT 20

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: ECRC_ERROR_MASK [19:19] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_ECRC_ERROR_MASK_MASK     0x00080000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_ECRC_ERROR_MASK_ALIGN    0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_ECRC_ERROR_MASK_BITS     1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_ECRC_ERROR_MASK_SHIFT    19

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: MALFORMED_TLP_MASK [18:18] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_MALFORMED_TLP_MASK_MASK  0x00040000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_MALFORMED_TLP_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_MALFORMED_TLP_MASK_BITS  1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_MALFORMED_TLP_MASK_SHIFT 18

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: RECEIVER_OVERFLOW_MASK [17:17] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RECEIVER_OVERFLOW_MASK_MASK 0x00020000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RECEIVER_OVERFLOW_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RECEIVER_OVERFLOW_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RECEIVER_OVERFLOW_MASK_SHIFT 17

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: UNEXPECTED_COMPLETION_MASK [16:16] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNEXPECTED_COMPLETION_MASK_MASK 0x00010000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNEXPECTED_COMPLETION_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNEXPECTED_COMPLETION_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_UNEXPECTED_COMPLETION_MASK_SHIFT 16

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: COMPLETER_ABORT_MASK [15:15] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETER_ABORT_MASK_MASK 0x00008000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETER_ABORT_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETER_ABORT_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETER_ABORT_MASK_SHIFT 15

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: COMPLETION_TIMEOUT_MASK [14:14] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETION_TIMEOUT_MASK_MASK 0x00004000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETION_TIMEOUT_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETION_TIMEOUT_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_COMPLETION_TIMEOUT_MASK_SHIFT 14

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: FLOW_CONTROL_PROTOCOL_ERROR_MASK [13:13] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_FLOW_CONTROL_PROTOCOL_ERROR_MASK_MASK 0x00002000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_FLOW_CONTROL_PROTOCOL_ERROR_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_FLOW_CONTROL_PROTOCOL_ERROR_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_FLOW_CONTROL_PROTOCOL_ERROR_MASK_SHIFT 13

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: POISONED_TLP_MASK [12:12] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_POISONED_TLP_MASK_MASK   0x00001000
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_POISONED_TLP_MASK_ALIGN  0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_POISONED_TLP_MASK_BITS   1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_POISONED_TLP_MASK_SHIFT  12

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: RESERVED_1 [11:05] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_1_MASK          0x00000fe0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_1_ALIGN         0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_1_BITS          7
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_1_SHIFT         5

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: DATA_LINK_PROTOCOL_ERROR_MASK [04:04] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_DATA_LINK_PROTOCOL_ERROR_MASK_MASK 0x00000010
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_DATA_LINK_PROTOCOL_ERROR_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_DATA_LINK_PROTOCOL_ERROR_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_DATA_LINK_PROTOCOL_ERROR_MASK_SHIFT 4

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: RESERVED_2 [03:01] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_2_MASK          0x0000000e
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_2_ALIGN         0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_2_BITS          3
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_RESERVED_2_SHIFT         1

/* PCIE_CFG :: UNCORRECTABLE_ERROR_MASK :: TRAINING_ERROR_MASK [00:00] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_TRAINING_ERROR_MASK_MASK 0x00000001
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_TRAINING_ERROR_MASK_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_TRAINING_ERROR_MASK_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_MASK_TRAINING_ERROR_MASK_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY
 ***************************************************************************/
/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: RESERVED_0 [31:21] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_0_MASK      0xffe00000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_0_ALIGN     0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_0_BITS      11
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_0_SHIFT     21

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: UNSUPPORTED_REQUEST_ERROR_SEVERITY [20:20] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNSUPPORTED_REQUEST_ERROR_SEVERITY_MASK 0x00100000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNSUPPORTED_REQUEST_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNSUPPORTED_REQUEST_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNSUPPORTED_REQUEST_ERROR_SEVERITY_SHIFT 20

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: ECRC_ERROR_SEVERITY [19:19] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_ECRC_ERROR_SEVERITY_MASK 0x00080000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_ECRC_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_ECRC_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_ECRC_ERROR_SEVERITY_SHIFT 19

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: MALFORMED_TLP_SEVERITY [18:18] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_MALFORMED_TLP_SEVERITY_MASK 0x00040000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_MALFORMED_TLP_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_MALFORMED_TLP_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_MALFORMED_TLP_SEVERITY_SHIFT 18

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: RECEIVER_OVERFLOW_ERROR_SEVERITY [17:17] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RECEIVER_OVERFLOW_ERROR_SEVERITY_MASK 0x00020000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RECEIVER_OVERFLOW_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RECEIVER_OVERFLOW_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RECEIVER_OVERFLOW_ERROR_SEVERITY_SHIFT 17

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: UNEXPECTED_COMPLETION_ERROR_SEVERITY [16:16] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNEXPECTED_COMPLETION_ERROR_SEVERITY_MASK 0x00010000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNEXPECTED_COMPLETION_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNEXPECTED_COMPLETION_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_UNEXPECTED_COMPLETION_ERROR_SEVERITY_SHIFT 16

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: COMPLETER_ABORT_ERROR_SEVERITY [15:15] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETER_ABORT_ERROR_SEVERITY_MASK 0x00008000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETER_ABORT_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETER_ABORT_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETER_ABORT_ERROR_SEVERITY_SHIFT 15

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: COMPLETION_TIMEOUT_ERROR_SEVERITY [14:14] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETION_TIMEOUT_ERROR_SEVERITY_MASK 0x00004000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETION_TIMEOUT_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETION_TIMEOUT_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_COMPLETION_TIMEOUT_ERROR_SEVERITY_SHIFT 14

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: FLOW_CONTROL_PROTOCOL_ERROR_SEVERITY [13:13] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_FLOW_CONTROL_PROTOCOL_ERROR_SEVERITY_MASK 0x00002000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_FLOW_CONTROL_PROTOCOL_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_FLOW_CONTROL_PROTOCOL_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_FLOW_CONTROL_PROTOCOL_ERROR_SEVERITY_SHIFT 13

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: POISONED_TLP_SEVERITY [12:12] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_POISONED_TLP_SEVERITY_MASK 0x00001000
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_POISONED_TLP_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_POISONED_TLP_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_POISONED_TLP_SEVERITY_SHIFT 12

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: RESERVED_1 [11:05] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_1_MASK      0x00000fe0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_1_ALIGN     0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_1_BITS      7
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_1_SHIFT     5

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: DATA_LINK_PROTOCOL_ERROR_SEVERITY [04:04] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_DATA_LINK_PROTOCOL_ERROR_SEVERITY_MASK 0x00000010
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_DATA_LINK_PROTOCOL_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_DATA_LINK_PROTOCOL_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_DATA_LINK_PROTOCOL_ERROR_SEVERITY_SHIFT 4

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: RESERVED_2 [03:01] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_2_MASK      0x0000000e
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_2_ALIGN     0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_2_BITS      3
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_RESERVED_2_SHIFT     1

/* PCIE_CFG :: UNCORRECTABLE_ERROR_SEVERITY :: TRAINING_ERROR_SEVERITY [00:00] */
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_TRAINING_ERROR_SEVERITY_MASK 0x00000001
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_TRAINING_ERROR_SEVERITY_ALIGN 0
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_TRAINING_ERROR_SEVERITY_BITS 1
#define PCIE_CFG_UNCORRECTABLE_ERROR_SEVERITY_TRAINING_ERROR_SEVERITY_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: CORRECTABLE_ERROR_STATUS
 ***************************************************************************/
/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: RESERVED_0 [31:14] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_0_MASK          0xffffc000
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_0_ALIGN         0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_0_BITS          18
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_0_SHIFT         14

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: ADVISORY_NON_FATAL_ERROR_STATUS [13:13] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_ADVISORY_NON_FATAL_ERROR_STATUS_MASK 0x00002000
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_ADVISORY_NON_FATAL_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_ADVISORY_NON_FATAL_ERROR_STATUS_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_ADVISORY_NON_FATAL_ERROR_STATUS_SHIFT 13

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: REPLAY_TIMER_TIMEOUT_STATUS [12:12] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_TIMER_TIMEOUT_STATUS_MASK 0x00001000
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_TIMER_TIMEOUT_STATUS_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_TIMER_TIMEOUT_STATUS_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_TIMER_TIMEOUT_STATUS_SHIFT 12

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: RESERVED_1 [11:09] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_1_MASK          0x00000e00
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_1_ALIGN         0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_1_BITS          3
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_1_SHIFT         9

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: REPLAY_NUM_ROLLOVER_STATUS [08:08] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_NUM_ROLLOVER_STATUS_MASK 0x00000100
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_NUM_ROLLOVER_STATUS_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_NUM_ROLLOVER_STATUS_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_REPLAY_NUM_ROLLOVER_STATUS_SHIFT 8

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: BAD_DLLP_STATUS [07:07] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_DLLP_STATUS_MASK     0x00000080
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_DLLP_STATUS_ALIGN    0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_DLLP_STATUS_BITS     1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_DLLP_STATUS_SHIFT    7

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: BAD_TLP_STATUS [06:06] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_TLP_STATUS_MASK      0x00000040
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_TLP_STATUS_ALIGN     0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_TLP_STATUS_BITS      1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_BAD_TLP_STATUS_SHIFT     6

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: RESERVED_2 [05:01] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_2_MASK          0x0000003e
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_2_ALIGN         0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_2_BITS          5
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RESERVED_2_SHIFT         1

/* PCIE_CFG :: CORRECTABLE_ERROR_STATUS :: RECEIVER_ERROR_STATUS [00:00] */
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RECEIVER_ERROR_STATUS_MASK 0x00000001
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RECEIVER_ERROR_STATUS_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RECEIVER_ERROR_STATUS_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_STATUS_RECEIVER_ERROR_STATUS_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: CORRECTABLE_ERROR_MASK
 ***************************************************************************/
/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: RESERVED_0 [31:14] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_0_MASK            0xffffc000
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_0_ALIGN           0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_0_BITS            18
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_0_SHIFT           14

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: ADVISORY_NON_FATAL_ERROR_MASK [13:13] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_ADVISORY_NON_FATAL_ERROR_MASK_MASK 0x00002000
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_ADVISORY_NON_FATAL_ERROR_MASK_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_ADVISORY_NON_FATAL_ERROR_MASK_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_ADVISORY_NON_FATAL_ERROR_MASK_SHIFT 13

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: REPLAY_TIMER_TIMEOUT_MASK [12:12] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_TIMER_TIMEOUT_MASK_MASK 0x00001000
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_TIMER_TIMEOUT_MASK_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_TIMER_TIMEOUT_MASK_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_TIMER_TIMEOUT_MASK_SHIFT 12

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: RESERVED_1 [11:09] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_1_MASK            0x00000e00
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_1_ALIGN           0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_1_BITS            3
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_1_SHIFT           9

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: REPLAY_NUM_ROLLOVER_MASK [08:08] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_NUM_ROLLOVER_MASK_MASK 0x00000100
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_NUM_ROLLOVER_MASK_ALIGN 0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_NUM_ROLLOVER_MASK_BITS 1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_REPLAY_NUM_ROLLOVER_MASK_SHIFT 8

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: BAD_DLLP_MASK [07:07] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_DLLP_MASK_MASK         0x00000080
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_DLLP_MASK_ALIGN        0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_DLLP_MASK_BITS         1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_DLLP_MASK_SHIFT        7

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: BAD_TLP_MASK [06:06] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_TLP_MASK_MASK          0x00000040
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_TLP_MASK_ALIGN         0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_TLP_MASK_BITS          1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_BAD_TLP_MASK_SHIFT         6

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: RESERVED_2 [05:01] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_2_MASK            0x0000003e
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_2_ALIGN           0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_2_BITS            5
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RESERVED_2_SHIFT           1

/* PCIE_CFG :: CORRECTABLE_ERROR_MASK :: RECEIVER_ERROR_MASK [00:00] */
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RECEIVER_ERROR_MASK_MASK   0x00000001
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RECEIVER_ERROR_MASK_ALIGN  0
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RECEIVER_ERROR_MASK_BITS   1
#define PCIE_CFG_CORRECTABLE_ERROR_MASK_RECEIVER_ERROR_MASK_SHIFT  0


/****************************************************************************
 * PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: RESERVED_0 [31:09] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_RESERVED_0_MASK 0xfffffe00
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_RESERVED_0_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_RESERVED_0_BITS 23
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_RESERVED_0_SHIFT 9

/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: ECRC_CHECK_ENABLE [08:08] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_ENABLE_MASK 0x00000100
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_ENABLE_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_ENABLE_BITS 1
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_ENABLE_SHIFT 8

/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: ECRC_CHECK_CAPABLE [07:07] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_CAPABLE_MASK 0x00000080
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_CAPABLE_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_CAPABLE_BITS 1
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_CHECK_CAPABLE_SHIFT 7

/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: ECRC_GENERATION_ENABLE [06:06] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_ENABLE_MASK 0x00000040
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_ENABLE_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_ENABLE_BITS 1
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_ENABLE_SHIFT 6

/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: ECRC_GENERATION_CAPABLE [05:05] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_CAPABLE_MASK 0x00000020
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_CAPABLE_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_CAPABLE_BITS 1
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_ECRC_GENERATION_CAPABLE_SHIFT 5

/* PCIE_CFG :: ADVANCED_ERROR_CAPABILITIES_AND_CONTROL :: FIRST_ERROR_POINTER [04:00] */
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_FIRST_ERROR_POINTER_MASK 0x0000001f
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_FIRST_ERROR_POINTER_ALIGN 0
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_FIRST_ERROR_POINTER_BITS 5
#define PCIE_CFG_ADVANCED_ERROR_CAPABILITIES_AND_CONTROL_FIRST_ERROR_POINTER_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: HEADER_LOG_1
 ***************************************************************************/
/* PCIE_CFG :: HEADER_LOG_1 :: HEADER_BYTE_0 [31:24] */
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_0_MASK                   0xff000000
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_0_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_0_BITS                   8
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_0_SHIFT                  24

/* PCIE_CFG :: HEADER_LOG_1 :: HEADER_BYTE_1 [23:16] */
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_1_MASK                   0x00ff0000
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_1_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_1_BITS                   8
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_1_SHIFT                  16

/* PCIE_CFG :: HEADER_LOG_1 :: HEADER_BYTE_2 [15:08] */
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_2_MASK                   0x0000ff00
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_2_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_2_BITS                   8
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_2_SHIFT                  8

/* PCIE_CFG :: HEADER_LOG_1 :: HEADER_BYTE_3 [07:00] */
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_3_MASK                   0x000000ff
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_3_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_3_BITS                   8
#define PCIE_CFG_HEADER_LOG_1_HEADER_BYTE_3_SHIFT                  0


/****************************************************************************
 * PCIE_CFG :: HEADER_LOG_2
 ***************************************************************************/
/* PCIE_CFG :: HEADER_LOG_2 :: HEADER_BYTE_4 [31:24] */
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_4_MASK                   0xff000000
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_4_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_4_BITS                   8
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_4_SHIFT                  24

/* PCIE_CFG :: HEADER_LOG_2 :: HEADER_BYTE_5 [23:16] */
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_5_MASK                   0x00ff0000
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_5_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_5_BITS                   8
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_5_SHIFT                  16

/* PCIE_CFG :: HEADER_LOG_2 :: HEADER_BYTE_6 [15:08] */
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_6_MASK                   0x0000ff00
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_6_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_6_BITS                   8
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_6_SHIFT                  8

/* PCIE_CFG :: HEADER_LOG_2 :: HEADER_BYTE_7 [07:00] */
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_7_MASK                   0x000000ff
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_7_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_7_BITS                   8
#define PCIE_CFG_HEADER_LOG_2_HEADER_BYTE_7_SHIFT                  0


/****************************************************************************
 * PCIE_CFG :: HEADER_LOG_3
 ***************************************************************************/
/* PCIE_CFG :: HEADER_LOG_3 :: HEADER_BYTE_8 [31:24] */
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_8_MASK                   0xff000000
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_8_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_8_BITS                   8
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_8_SHIFT                  24

/* PCIE_CFG :: HEADER_LOG_3 :: HEADER_BYTE_9 [23:16] */
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_9_MASK                   0x00ff0000
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_9_ALIGN                  0
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_9_BITS                   8
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_9_SHIFT                  16

/* PCIE_CFG :: HEADER_LOG_3 :: HEADER_BYTE_10 [15:08] */
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_10_MASK                  0x0000ff00
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_10_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_10_BITS                  8
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_10_SHIFT                 8

/* PCIE_CFG :: HEADER_LOG_3 :: HEADER_BYTE_11 [07:00] */
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_11_MASK                  0x000000ff
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_11_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_11_BITS                  8
#define PCIE_CFG_HEADER_LOG_3_HEADER_BYTE_11_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: HEADER_LOG_4
 ***************************************************************************/
/* PCIE_CFG :: HEADER_LOG_4 :: HEADER_BYTE_12 [31:24] */
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_12_MASK                  0xff000000
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_12_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_12_BITS                  8
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_12_SHIFT                 24

/* PCIE_CFG :: HEADER_LOG_4 :: HEADER_BYTE_13 [23:16] */
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_13_MASK                  0x00ff0000
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_13_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_13_BITS                  8
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_13_SHIFT                 16

/* PCIE_CFG :: HEADER_LOG_4 :: HEADER_BYTE_14 [15:08] */
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_14_MASK                  0x0000ff00
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_14_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_14_BITS                  8
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_14_SHIFT                 8

/* PCIE_CFG :: HEADER_LOG_4 :: HEADER_BYTE_15 [07:00] */
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_15_MASK                  0x000000ff
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_15_ALIGN                 0
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_15_BITS                  8
#define PCIE_CFG_HEADER_LOG_4_HEADER_BYTE_15_SHIFT                 0


/****************************************************************************
 * PCIE_CFG :: VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER :: NEXT_CAPABILITY_OFFSET [31:20] */
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_MASK 0xfff00000
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_ALIGN 0
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_BITS 12
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_SHIFT 20

/* PCIE_CFG :: VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER :: CAPABILITY_VERSION [19:16] */
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_MASK 0x000f0000
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_ALIGN 0
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_BITS 4
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_SHIFT 16

/* PCIE_CFG :: VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER :: PCIE_EXTENDED_CAPABILITY_ID [15:00] */
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_MASK 0x0000ffff
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_ALIGN 0
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_BITS 16
#define PCIE_CFG_VIRTUAL_CHANNEL_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: PORT_VC_CAPABILITY
 ***************************************************************************/
/* PCIE_CFG :: PORT_VC_CAPABILITY :: RESERVED_0 [31:12] */
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_0_MASK                0xfffff000
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_0_ALIGN               0
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_0_BITS                20
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_0_SHIFT               12

/* PCIE_CFG :: PORT_VC_CAPABILITY :: PORT_ARBITRATION_TABLE_ENTRY_SIZE [11:10] */
#define PCIE_CFG_PORT_VC_CAPABILITY_PORT_ARBITRATION_TABLE_ENTRY_SIZE_MASK 0x00000c00
#define PCIE_CFG_PORT_VC_CAPABILITY_PORT_ARBITRATION_TABLE_ENTRY_SIZE_ALIGN 0
#define PCIE_CFG_PORT_VC_CAPABILITY_PORT_ARBITRATION_TABLE_ENTRY_SIZE_BITS 2
#define PCIE_CFG_PORT_VC_CAPABILITY_PORT_ARBITRATION_TABLE_ENTRY_SIZE_SHIFT 10

/* PCIE_CFG :: PORT_VC_CAPABILITY :: REFERENCE_CLOCK [09:08] */
#define PCIE_CFG_PORT_VC_CAPABILITY_REFERENCE_CLOCK_MASK           0x00000300
#define PCIE_CFG_PORT_VC_CAPABILITY_REFERENCE_CLOCK_ALIGN          0
#define PCIE_CFG_PORT_VC_CAPABILITY_REFERENCE_CLOCK_BITS           2
#define PCIE_CFG_PORT_VC_CAPABILITY_REFERENCE_CLOCK_SHIFT          8

/* PCIE_CFG :: PORT_VC_CAPABILITY :: RESERVED_1 [07:07] */
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_1_MASK                0x00000080
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_1_ALIGN               0
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_1_BITS                1
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_1_SHIFT               7

/* PCIE_CFG :: PORT_VC_CAPABILITY :: LOW_PRIORITY_EXTENDED_VC_COUNT [06:04] */
#define PCIE_CFG_PORT_VC_CAPABILITY_LOW_PRIORITY_EXTENDED_VC_COUNT_MASK 0x00000070
#define PCIE_CFG_PORT_VC_CAPABILITY_LOW_PRIORITY_EXTENDED_VC_COUNT_ALIGN 0
#define PCIE_CFG_PORT_VC_CAPABILITY_LOW_PRIORITY_EXTENDED_VC_COUNT_BITS 3
#define PCIE_CFG_PORT_VC_CAPABILITY_LOW_PRIORITY_EXTENDED_VC_COUNT_SHIFT 4

/* PCIE_CFG :: PORT_VC_CAPABILITY :: RESERVED_2 [03:03] */
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_2_MASK                0x00000008
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_2_ALIGN               0
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_2_BITS                1
#define PCIE_CFG_PORT_VC_CAPABILITY_RESERVED_2_SHIFT               3

/* PCIE_CFG :: PORT_VC_CAPABILITY :: EXTENDED_VC_COUNT [02:00] */
#define PCIE_CFG_PORT_VC_CAPABILITY_EXTENDED_VC_COUNT_MASK         0x00000007
#define PCIE_CFG_PORT_VC_CAPABILITY_EXTENDED_VC_COUNT_ALIGN        0
#define PCIE_CFG_PORT_VC_CAPABILITY_EXTENDED_VC_COUNT_BITS         3
#define PCIE_CFG_PORT_VC_CAPABILITY_EXTENDED_VC_COUNT_SHIFT        0


/****************************************************************************
 * PCIE_CFG :: PORT_VC_CAPABILITY_2
 ***************************************************************************/
/* PCIE_CFG :: PORT_VC_CAPABILITY_2 :: VC_ARBITRATION_TABLE_OFFSET [31:24] */
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_TABLE_OFFSET_MASK 0xff000000
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_TABLE_OFFSET_ALIGN 0
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_TABLE_OFFSET_BITS 8
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_TABLE_OFFSET_SHIFT 24

/* PCIE_CFG :: PORT_VC_CAPABILITY_2 :: RESERVED_0 [23:08] */
#define PCIE_CFG_PORT_VC_CAPABILITY_2_RESERVED_0_MASK              0x00ffff00
#define PCIE_CFG_PORT_VC_CAPABILITY_2_RESERVED_0_ALIGN             0
#define PCIE_CFG_PORT_VC_CAPABILITY_2_RESERVED_0_BITS              16
#define PCIE_CFG_PORT_VC_CAPABILITY_2_RESERVED_0_SHIFT             8

/* PCIE_CFG :: PORT_VC_CAPABILITY_2 :: VC_ARBITRATION_CAPABILITY [07:00] */
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_CAPABILITY_MASK 0x000000ff
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_CAPABILITY_ALIGN 0
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_CAPABILITY_BITS 8
#define PCIE_CFG_PORT_VC_CAPABILITY_2_VC_ARBITRATION_CAPABILITY_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: PORT_VC_STATUS_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: PORT_VC_STATUS_CONTROL :: RESERVED_0 [31:17] */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_0_MASK            0xfffe0000
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_0_ALIGN           0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_0_BITS            15
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_0_SHIFT           17

/* PCIE_CFG :: PORT_VC_STATUS_CONTROL :: VC_ARBITRATION_TABLE_STATUS [16:16] */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_TABLE_STATUS_MASK 0x00010000
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_TABLE_STATUS_ALIGN 0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_TABLE_STATUS_BITS 1
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_TABLE_STATUS_SHIFT 16

/* PCIE_CFG :: PORT_VC_STATUS_CONTROL :: RESERVED_1 [15:04] */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_1_MASK            0x0000fff0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_1_ALIGN           0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_1_BITS            12
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_RESERVED_1_SHIFT           4

/* PCIE_CFG :: PORT_VC_STATUS_CONTROL :: VC_ARBITRATION_SELECT [03:01] */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_SELECT_MASK 0x0000000e
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_SELECT_ALIGN 0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_SELECT_BITS 3
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_VC_ARBITRATION_SELECT_SHIFT 1

/* PCIE_CFG :: PORT_VC_STATUS_CONTROL :: LOAD_VC_ARBITRATION_TABLE [00:00] */
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_LOAD_VC_ARBITRATION_TABLE_MASK 0x00000001
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_LOAD_VC_ARBITRATION_TABLE_ALIGN 0
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_LOAD_VC_ARBITRATION_TABLE_BITS 1
#define PCIE_CFG_PORT_VC_STATUS_CONTROL_LOAD_VC_ARBITRATION_TABLE_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: VC_RESOURCE_CAPABILITY
 ***************************************************************************/
/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: PORT_ARBITRATION_TABLE_OFFSET [31:24] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_TABLE_OFFSET_MASK 0xff000000
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_TABLE_OFFSET_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_TABLE_OFFSET_BITS 8
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_TABLE_OFFSET_SHIFT 24

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: RESERVED_0 [23:23] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_0_MASK            0x00800000
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_0_ALIGN           0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_0_BITS            1
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_0_SHIFT           23

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: MAXIMUM_TIME_SLOTS [22:16] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_MAXIMUM_TIME_SLOTS_MASK    0x007f0000
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_MAXIMUM_TIME_SLOTS_ALIGN   0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_MAXIMUM_TIME_SLOTS_BITS    7
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_MAXIMUM_TIME_SLOTS_SHIFT   16

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: REJECT_SNOOP_TRANSACTIONS [15:15] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_REJECT_SNOOP_TRANSACTIONS_MASK 0x00008000
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_REJECT_SNOOP_TRANSACTIONS_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_REJECT_SNOOP_TRANSACTIONS_BITS 1
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_REJECT_SNOOP_TRANSACTIONS_SHIFT 15

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: ADVANCED_PACKET_SWITCHING [14:14] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_ADVANCED_PACKET_SWITCHING_MASK 0x00004000
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_ADVANCED_PACKET_SWITCHING_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_ADVANCED_PACKET_SWITCHING_BITS 1
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_ADVANCED_PACKET_SWITCHING_SHIFT 14

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: RESERVED_1 [13:08] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_1_MASK            0x00003f00
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_1_ALIGN           0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_1_BITS            6
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_RESERVED_1_SHIFT           8

/* PCIE_CFG :: VC_RESOURCE_CAPABILITY :: PORT_ARBITRATION_CAPABILITY [07:00] */
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_CAPABILITY_MASK 0x000000ff
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_CAPABILITY_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_CAPABILITY_BITS 8
#define PCIE_CFG_VC_RESOURCE_CAPABILITY_PORT_ARBITRATION_CAPABILITY_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: VC_RESOURCE_CONTROL
 ***************************************************************************/
/* PCIE_CFG :: VC_RESOURCE_CONTROL :: VC_ENABLE [31:31] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ENABLE_MASK                0x80000000
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ENABLE_ALIGN               0
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ENABLE_BITS                1
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ENABLE_SHIFT               31

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: RESERVED_0 [30:27] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_0_MASK               0x78000000
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_0_ALIGN              0
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_0_BITS               4
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_0_SHIFT              27

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: VC_ID [26:24] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ID_MASK                    0x07000000
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ID_ALIGN                   0
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ID_BITS                    3
#define PCIE_CFG_VC_RESOURCE_CONTROL_VC_ID_SHIFT                   24

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: RESERVED_1 [23:20] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_1_MASK               0x00f00000
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_1_ALIGN              0
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_1_BITS               4
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_1_SHIFT              20

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: PORT_ARBITRATION_SELECT [19:17] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_PORT_ARBITRATION_SELECT_MASK  0x000e0000
#define PCIE_CFG_VC_RESOURCE_CONTROL_PORT_ARBITRATION_SELECT_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CONTROL_PORT_ARBITRATION_SELECT_BITS  3
#define PCIE_CFG_VC_RESOURCE_CONTROL_PORT_ARBITRATION_SELECT_SHIFT 17

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: LOAD_PORT_ARBITRATION_TABLE [16:16] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_LOAD_PORT_ARBITRATION_TABLE_MASK 0x00010000
#define PCIE_CFG_VC_RESOURCE_CONTROL_LOAD_PORT_ARBITRATION_TABLE_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_CONTROL_LOAD_PORT_ARBITRATION_TABLE_BITS 1
#define PCIE_CFG_VC_RESOURCE_CONTROL_LOAD_PORT_ARBITRATION_TABLE_SHIFT 16

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: RESERVED_2 [15:08] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_2_MASK               0x0000ff00
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_2_ALIGN              0
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_2_BITS               8
#define PCIE_CFG_VC_RESOURCE_CONTROL_RESERVED_2_SHIFT              8

/* PCIE_CFG :: VC_RESOURCE_CONTROL :: TC_VC_MAP [07:00] */
#define PCIE_CFG_VC_RESOURCE_CONTROL_TC_VC_MAP_MASK                0x000000ff
#define PCIE_CFG_VC_RESOURCE_CONTROL_TC_VC_MAP_ALIGN               0
#define PCIE_CFG_VC_RESOURCE_CONTROL_TC_VC_MAP_BITS                8
#define PCIE_CFG_VC_RESOURCE_CONTROL_TC_VC_MAP_SHIFT               0


/****************************************************************************
 * PCIE_CFG :: VC_RESOURCE_STATUS
 ***************************************************************************/
/* PCIE_CFG :: VC_RESOURCE_STATUS :: RESERVED_0 [31:18] */
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_0_MASK                0xfffc0000
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_0_ALIGN               0
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_0_BITS                14
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_0_SHIFT               18

/* PCIE_CFG :: VC_RESOURCE_STATUS :: VC_NEGOTIATION_PENDING [17:17] */
#define PCIE_CFG_VC_RESOURCE_STATUS_VC_NEGOTIATION_PENDING_MASK    0x00020000
#define PCIE_CFG_VC_RESOURCE_STATUS_VC_NEGOTIATION_PENDING_ALIGN   0
#define PCIE_CFG_VC_RESOURCE_STATUS_VC_NEGOTIATION_PENDING_BITS    1
#define PCIE_CFG_VC_RESOURCE_STATUS_VC_NEGOTIATION_PENDING_SHIFT   17

/* PCIE_CFG :: VC_RESOURCE_STATUS :: PORT_ARBITRATION_TABLE_STATUS [16:16] */
#define PCIE_CFG_VC_RESOURCE_STATUS_PORT_ARBITRATION_TABLE_STATUS_MASK 0x00010000
#define PCIE_CFG_VC_RESOURCE_STATUS_PORT_ARBITRATION_TABLE_STATUS_ALIGN 0
#define PCIE_CFG_VC_RESOURCE_STATUS_PORT_ARBITRATION_TABLE_STATUS_BITS 1
#define PCIE_CFG_VC_RESOURCE_STATUS_PORT_ARBITRATION_TABLE_STATUS_SHIFT 16

/* PCIE_CFG :: VC_RESOURCE_STATUS :: RESERVED_1 [15:00] */
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_1_MASK                0x0000ffff
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_1_ALIGN               0
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_1_BITS                16
#define PCIE_CFG_VC_RESOURCE_STATUS_RESERVED_1_SHIFT               0


/****************************************************************************
 * PCIE_CFG :: DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER :: NEXT_CAPABILITY_OFFSET [31:20] */
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_MASK 0xfff00000
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_ALIGN 0
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_BITS 12
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_SHIFT 20

/* PCIE_CFG :: DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER :: CAPABILITY_VERSION [19:16] */
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_MASK 0x000f0000
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_ALIGN 0
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_BITS 4
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_SHIFT 16

/* PCIE_CFG :: DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER :: PCIE_EXTENDED_CAPABILITY_ID [15:00] */
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_MASK 0x0000ffff
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_ALIGN 0
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_BITS 16
#define PCIE_CFG_DEVICE_SERIAL_NO_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: DEVICE_SERIAL_NO_LOWER_DW
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_SERIAL_NO_LOWER_DW :: SERIAL_NO_LOWER [31:00] */
#define PCIE_CFG_DEVICE_SERIAL_NO_LOWER_DW_SERIAL_NO_LOWER_MASK    0xffffffff
#define PCIE_CFG_DEVICE_SERIAL_NO_LOWER_DW_SERIAL_NO_LOWER_ALIGN   0
#define PCIE_CFG_DEVICE_SERIAL_NO_LOWER_DW_SERIAL_NO_LOWER_BITS    32
#define PCIE_CFG_DEVICE_SERIAL_NO_LOWER_DW_SERIAL_NO_LOWER_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: DEVICE_SERIAL_NO_UPPER_DW
 ***************************************************************************/
/* PCIE_CFG :: DEVICE_SERIAL_NO_UPPER_DW :: SERIAL_NO_UPPER [31:00] */
#define PCIE_CFG_DEVICE_SERIAL_NO_UPPER_DW_SERIAL_NO_UPPER_MASK    0xffffffff
#define PCIE_CFG_DEVICE_SERIAL_NO_UPPER_DW_SERIAL_NO_UPPER_ALIGN   0
#define PCIE_CFG_DEVICE_SERIAL_NO_UPPER_DW_SERIAL_NO_UPPER_BITS    32
#define PCIE_CFG_DEVICE_SERIAL_NO_UPPER_DW_SERIAL_NO_UPPER_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER
 ***************************************************************************/
/* PCIE_CFG :: POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER :: NEXT_CAPABILITY_OFFSET [31:20] */
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_MASK 0xfff00000
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_ALIGN 0
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_BITS 12
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_NEXT_CAPABILITY_OFFSET_SHIFT 20

/* PCIE_CFG :: POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER :: CAPABILITY_VERSION [19:16] */
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_MASK 0x000f0000
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_ALIGN 0
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_BITS 4
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_CAPABILITY_VERSION_SHIFT 16

/* PCIE_CFG :: POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER :: PCIE_EXTENDED_CAPABILITY_ID [15:00] */
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_MASK 0x0000ffff
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_ALIGN 0
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_BITS 16
#define PCIE_CFG_POWER_BUDGETING_ENHANCED_CAPABILITY_HEADER_PCIE_EXTENDED_CAPABILITY_ID_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: POWER_BUDGETING_DATA_SELECT
 ***************************************************************************/
/* PCIE_CFG :: POWER_BUDGETING_DATA_SELECT :: RESERVED_0 [31:08] */
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_RESERVED_0_MASK       0xffffff00
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_RESERVED_0_ALIGN      0
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_RESERVED_0_BITS       24
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_RESERVED_0_SHIFT      8

/* PCIE_CFG :: POWER_BUDGETING_DATA_SELECT :: DATA_SELECT [07:00] */
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_DATA_SELECT_MASK      0x000000ff
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_DATA_SELECT_ALIGN     0
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_DATA_SELECT_BITS      8
#define PCIE_CFG_POWER_BUDGETING_DATA_SELECT_DATA_SELECT_SHIFT     0


/****************************************************************************
 * PCIE_CFG :: POWER_BUDGETING_DATA
 ***************************************************************************/
/* PCIE_CFG :: POWER_BUDGETING_DATA :: RESERVED_0 [31:21] */
#define PCIE_CFG_POWER_BUDGETING_DATA_RESERVED_0_MASK              0xffe00000
#define PCIE_CFG_POWER_BUDGETING_DATA_RESERVED_0_ALIGN             0
#define PCIE_CFG_POWER_BUDGETING_DATA_RESERVED_0_BITS              11
#define PCIE_CFG_POWER_BUDGETING_DATA_RESERVED_0_SHIFT             21

/* PCIE_CFG :: POWER_BUDGETING_DATA :: POWER_RAIL [20:18] */
#define PCIE_CFG_POWER_BUDGETING_DATA_POWER_RAIL_MASK              0x001c0000
#define PCIE_CFG_POWER_BUDGETING_DATA_POWER_RAIL_ALIGN             0
#define PCIE_CFG_POWER_BUDGETING_DATA_POWER_RAIL_BITS              3
#define PCIE_CFG_POWER_BUDGETING_DATA_POWER_RAIL_SHIFT             18

/* PCIE_CFG :: POWER_BUDGETING_DATA :: TYPE [17:15] */
#define PCIE_CFG_POWER_BUDGETING_DATA_TYPE_MASK                    0x00038000
#define PCIE_CFG_POWER_BUDGETING_DATA_TYPE_ALIGN                   0
#define PCIE_CFG_POWER_BUDGETING_DATA_TYPE_BITS                    3
#define PCIE_CFG_POWER_BUDGETING_DATA_TYPE_SHIFT                   15

/* PCIE_CFG :: POWER_BUDGETING_DATA :: PM_STATE [14:13] */
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_STATE_MASK                0x00006000
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_STATE_ALIGN               0
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_STATE_BITS                2
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_STATE_SHIFT               13

/* PCIE_CFG :: POWER_BUDGETING_DATA :: PM_SUB_STATE [12:10] */
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_SUB_STATE_MASK            0x00001c00
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_SUB_STATE_ALIGN           0
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_SUB_STATE_BITS            3
#define PCIE_CFG_POWER_BUDGETING_DATA_PM_SUB_STATE_SHIFT           10

/* PCIE_CFG :: POWER_BUDGETING_DATA :: DATA_SCALE [09:08] */
#define PCIE_CFG_POWER_BUDGETING_DATA_DATA_SCALE_MASK              0x00000300
#define PCIE_CFG_POWER_BUDGETING_DATA_DATA_SCALE_ALIGN             0
#define PCIE_CFG_POWER_BUDGETING_DATA_DATA_SCALE_BITS              2
#define PCIE_CFG_POWER_BUDGETING_DATA_DATA_SCALE_SHIFT             8

/* PCIE_CFG :: POWER_BUDGETING_DATA :: BASE_POWER [07:00] */
#define PCIE_CFG_POWER_BUDGETING_DATA_BASE_POWER_MASK              0x000000ff
#define PCIE_CFG_POWER_BUDGETING_DATA_BASE_POWER_ALIGN             0
#define PCIE_CFG_POWER_BUDGETING_DATA_BASE_POWER_BITS              8
#define PCIE_CFG_POWER_BUDGETING_DATA_BASE_POWER_SHIFT             0


/****************************************************************************
 * PCIE_CFG :: POWER_BUDGETING_CAPABILITY
 ***************************************************************************/
/* PCIE_CFG :: POWER_BUDGETING_CAPABILITY :: RESERVED_0 [31:01] */
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_RESERVED_0_MASK        0xfffffffe
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_RESERVED_0_ALIGN       0
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_RESERVED_0_BITS        31
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_RESERVED_0_SHIFT       1

/* PCIE_CFG :: POWER_BUDGETING_CAPABILITY :: LOM_CONFIGURATION [00:00] */
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_LOM_CONFIGURATION_MASK 0x00000001
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_LOM_CONFIGURATION_ALIGN 0
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_LOM_CONFIGURATION_BITS 1
#define PCIE_CFG_POWER_BUDGETING_CAPABILITY_LOM_CONFIGURATION_SHIFT 0


/****************************************************************************
 * PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1
 ***************************************************************************/
/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: POWER_RAIL_2 [31:29] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_2_MASK    0xe0000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_2_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_2_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_2_SHIFT   29

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: TYPE_2 [28:26] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_2_MASK          0x1c000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_2_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_2_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_2_SHIFT         26

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: PM_STATE_2 [25:24] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_2_MASK      0x03000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_2_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_2_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_2_SHIFT     24

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: BASE_POWER_2 [23:16] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_2_MASK    0x00ff0000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_2_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_2_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_2_SHIFT   16

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: POWER_RAIL_1 [15:13] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_1_MASK    0x0000e000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_1_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_1_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_POWER_RAIL_1_SHIFT   13

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: TYPE_1 [12:10] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_1_MASK          0x00001c00
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_1_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_1_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_TYPE_1_SHIFT         10

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: PM_STATE_1 [09:08] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_1_MASK      0x00000300
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_1_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_1_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_PM_STATE_1_SHIFT     8

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_2_1 :: BASE_POWER_1 [07:00] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_1_MASK    0x000000ff
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_1_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_1_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_2_1_BASE_POWER_1_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3
 ***************************************************************************/
/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: POWER_RAIL_4 [31:29] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_4_MASK    0xe0000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_4_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_4_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_4_SHIFT   29

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: TYPE_4 [28:26] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_4_MASK          0x1c000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_4_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_4_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_4_SHIFT         26

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: PM_STATE_4 [25:24] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_4_MASK      0x03000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_4_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_4_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_4_SHIFT     24

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: BASE_POWER_4 [23:16] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_4_MASK    0x00ff0000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_4_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_4_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_4_SHIFT   16

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: POWER_RAIL_3 [15:13] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_3_MASK    0x0000e000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_3_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_3_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_POWER_RAIL_3_SHIFT   13

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: TYPE_3 [12:10] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_3_MASK          0x00001c00
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_3_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_3_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_TYPE_3_SHIFT         10

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: PM_STATE_3 [09:08] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_3_MASK      0x00000300
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_3_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_3_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_PM_STATE_3_SHIFT     8

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_4_3 :: BASE_POWER_3 [07:00] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_3_MASK    0x000000ff
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_3_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_3_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_4_3_BASE_POWER_3_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5
 ***************************************************************************/
/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: POWER_RAIL_6 [31:29] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_6_MASK    0xe0000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_6_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_6_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_6_SHIFT   29

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: TYPE_6 [28:26] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_6_MASK          0x1c000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_6_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_6_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_6_SHIFT         26

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: PM_STATE_6 [25:24] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_6_MASK      0x03000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_6_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_6_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_6_SHIFT     24

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: BASE_POWER_6 [23:16] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_6_MASK    0x00ff0000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_6_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_6_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_6_SHIFT   16

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: POWER_RAIL_5 [15:13] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_5_MASK    0x0000e000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_5_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_5_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_POWER_RAIL_5_SHIFT   13

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: TYPE_5 [12:10] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_5_MASK          0x00001c00
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_5_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_5_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_TYPE_5_SHIFT         10

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: PM_STATE_5 [09:08] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_5_MASK      0x00000300
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_5_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_5_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_PM_STATE_5_SHIFT     8

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_6_5 :: BASE_POWER_5 [07:00] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_5_MASK    0x000000ff
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_5_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_5_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_6_5_BASE_POWER_5_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7
 ***************************************************************************/
/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: POWER_RAIL_8 [31:29] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_8_MASK    0xe0000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_8_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_8_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_8_SHIFT   29

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: TYPE_8 [28:26] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_8_MASK          0x1c000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_8_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_8_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_8_SHIFT         26

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: PM_STATE_8 [25:24] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_8_MASK      0x03000000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_8_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_8_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_8_SHIFT     24

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: BASE_POWER_8 [23:16] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_8_MASK    0x00ff0000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_8_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_8_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_8_SHIFT   16

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: POWER_RAIL_7 [15:13] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_7_MASK    0x0000e000
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_7_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_7_BITS    3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_POWER_RAIL_7_SHIFT   13

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: TYPE_7 [12:10] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_7_MASK          0x00001c00
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_7_ALIGN         0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_7_BITS          3
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_TYPE_7_SHIFT         10

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: PM_STATE_7 [09:08] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_7_MASK      0x00000300
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_7_ALIGN     0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_7_BITS      2
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_PM_STATE_7_SHIFT     8

/* PCIE_CFG :: FIRMWARE_POWER_BUDGETING_8_7 :: BASE_POWER_7 [07:00] */
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_7_MASK    0x000000ff
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_7_ALIGN   0
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_7_BITS    8
#define PCIE_CFG_FIRMWARE_POWER_BUDGETING_8_7_BASE_POWER_7_SHIFT   0


/****************************************************************************
 * PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING
 ***************************************************************************/
/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: UNUSED_0 [31:07] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNUSED_0_MASK 0xffffff80
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNUSED_0_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNUSED_0_BITS 25
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNUSED_0_SHIFT 7

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: D3HOT_MEMORY_READ_ADVISORY_NON_FATAL [06:06] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_D3HOT_MEMORY_READ_ADVISORY_NON_FATAL_MASK 0x00000040
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_D3HOT_MEMORY_READ_ADVISORY_NON_FATAL_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_D3HOT_MEMORY_READ_ADVISORY_NON_FATAL_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_D3HOT_MEMORY_READ_ADVISORY_NON_FATAL_SHIFT 6

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: RETRY_POISON_ENABLE [05:05] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_RETRY_POISON_ENABLE_MASK 0x00000020
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_RETRY_POISON_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_RETRY_POISON_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_RETRY_POISON_ENABLE_SHIFT 5

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: POISON_ADVISORY_NON_FATAL_ENABLE [04:04] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_POISON_ADVISORY_NON_FATAL_ENABLE_MASK 0x00000010
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_POISON_ADVISORY_NON_FATAL_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_POISON_ADVISORY_NON_FATAL_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_POISON_ADVISORY_NON_FATAL_ENABLE_SHIFT 4

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: UNEXPECTED_ADVISORY_NON_FATAL_ENABLE [03:03] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNEXPECTED_ADVISORY_NON_FATAL_ENABLE_MASK 0x00000008
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNEXPECTED_ADVISORY_NON_FATAL_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNEXPECTED_ADVISORY_NON_FATAL_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_UNEXPECTED_ADVISORY_NON_FATAL_ENABLE_SHIFT 3

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: NON_POSTED_CFG_ADVISORY_NON_FATAL_ENABLE [02:02] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NON_POSTED_CFG_ADVISORY_NON_FATAL_ENABLE_MASK 0x00000004
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NON_POSTED_CFG_ADVISORY_NON_FATAL_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NON_POSTED_CFG_ADVISORY_NON_FATAL_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NON_POSTED_CFG_ADVISORY_NON_FATAL_ENABLE_SHIFT 2

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: NP_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE [01:01] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NP_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_MASK 0x00000002
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NP_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NP_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_NP_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_SHIFT 1

/* PCIE_CFG :: PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING :: COMPLETION_ABORT_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE [00:00] */
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_COMPLETION_ABORT_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_MASK 0x00000001
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_COMPLETION_ABORT_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_ALIGN 0
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_COMPLETION_ABORT_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_BITS 1
#define PCIE_CFG_PCIE_1_1_ADVISORY_NON_FATAL_ERROR_MASKING_COMPLETION_ABORT_MEMORY_READ_ADVISORY_NON_FATAL_ENABLE_SHIFT 0


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_TL
 ***************************************************************************/
/****************************************************************************
 * PCIE_TL :: TL_CONTROL
 ***************************************************************************/
/* PCIE_TL :: TL_CONTROL :: RESERVED_0 [31:31] */
#define PCIE_TL_TL_CONTROL_RESERVED_0_MASK                         0x80000000
#define PCIE_TL_TL_CONTROL_RESERVED_0_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_0_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_0_SHIFT                        31

/* PCIE_TL :: TL_CONTROL :: CQ14298_FIX_ENA_N [30:30] */
#define PCIE_TL_TL_CONTROL_CQ14298_FIX_ENA_N_MASK                  0x40000000
#define PCIE_TL_TL_CONTROL_CQ14298_FIX_ENA_N_ALIGN                 0
#define PCIE_TL_TL_CONTROL_CQ14298_FIX_ENA_N_BITS                  1
#define PCIE_TL_TL_CONTROL_CQ14298_FIX_ENA_N_SHIFT                 30

/* PCIE_TL :: TL_CONTROL :: RESERVED_1 [29:29] */
#define PCIE_TL_TL_CONTROL_RESERVED_1_MASK                         0x20000000
#define PCIE_TL_TL_CONTROL_RESERVED_1_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_1_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_1_SHIFT                        29

/* PCIE_TL :: TL_CONTROL :: INTA_WAKEUP_LINK_CLKREQ_DA [28:28] */
#define PCIE_TL_TL_CONTROL_INTA_WAKEUP_LINK_CLKREQ_DA_MASK         0x10000000
#define PCIE_TL_TL_CONTROL_INTA_WAKEUP_LINK_CLKREQ_DA_ALIGN        0
#define PCIE_TL_TL_CONTROL_INTA_WAKEUP_LINK_CLKREQ_DA_BITS         1
#define PCIE_TL_TL_CONTROL_INTA_WAKEUP_LINK_CLKREQ_DA_SHIFT        28

/* PCIE_TL :: TL_CONTROL :: RESERVED_2 [27:27] */
#define PCIE_TL_TL_CONTROL_RESERVED_2_MASK                         0x08000000
#define PCIE_TL_TL_CONTROL_RESERVED_2_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_2_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_2_SHIFT                        27

/* PCIE_TL :: TL_CONTROL :: CQ9583_TYPE_1_VENDOR_DEFINED_MESSAGE_FIX [26:26] */
#define PCIE_TL_TL_CONTROL_CQ9583_TYPE_1_VENDOR_DEFINED_MESSAGE_FIX_MASK 0x04000000
#define PCIE_TL_TL_CONTROL_CQ9583_TYPE_1_VENDOR_DEFINED_MESSAGE_FIX_ALIGN 0
#define PCIE_TL_TL_CONTROL_CQ9583_TYPE_1_VENDOR_DEFINED_MESSAGE_FIX_BITS 1
#define PCIE_TL_TL_CONTROL_CQ9583_TYPE_1_VENDOR_DEFINED_MESSAGE_FIX_SHIFT 26

/* PCIE_TL :: TL_CONTROL :: RESERVED_3 [25:25] */
#define PCIE_TL_TL_CONTROL_RESERVED_3_MASK                         0x02000000
#define PCIE_TL_TL_CONTROL_RESERVED_3_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_3_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_3_SHIFT                        25

/* PCIE_TL :: TL_CONTROL :: RESERVED_4 [24:24] */
#define PCIE_TL_TL_CONTROL_RESERVED_4_MASK                         0x01000000
#define PCIE_TL_TL_CONTROL_RESERVED_4_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_4_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_4_SHIFT                        24

/* PCIE_TL :: TL_CONTROL :: RESERVED_5 [23:23] */
#define PCIE_TL_TL_CONTROL_RESERVED_5_MASK                         0x00800000
#define PCIE_TL_TL_CONTROL_RESERVED_5_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_5_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_5_SHIFT                        23

/* PCIE_TL :: TL_CONTROL :: CRC_SWAP [22:22] */
#define PCIE_TL_TL_CONTROL_CRC_SWAP_MASK                           0x00400000
#define PCIE_TL_TL_CONTROL_CRC_SWAP_ALIGN                          0
#define PCIE_TL_TL_CONTROL_CRC_SWAP_BITS                           1
#define PCIE_TL_TL_CONTROL_CRC_SWAP_SHIFT                          22

/* PCIE_TL :: TL_CONTROL :: SLV_CMP_DIS_CA_ERROR [21:21] */
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_CA_ERROR_MASK               0x00200000
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_CA_ERROR_ALIGN              0
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_CA_ERROR_BITS               1
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_CA_ERROR_SHIFT              21

/* PCIE_TL :: TL_CONTROL :: SLV_CMP_DIS_UR_ERROR [20:20] */
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_UR_ERROR_MASK               0x00100000
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_UR_ERROR_ALIGN              0
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_UR_ERROR_BITS               1
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_UR_ERROR_SHIFT              20

/* PCIE_TL :: TL_CONTROL :: SLV_CMP_DIS_RSV_ERROR [19:19] */
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_RSV_ERROR_MASK              0x00080000
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_RSV_ERROR_ALIGN             0
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_RSV_ERROR_BITS              1
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_RSV_ERROR_SHIFT             19

/* PCIE_TL :: TL_CONTROL :: RESERVED_6 [18:18] */
#define PCIE_TL_TL_CONTROL_RESERVED_6_MASK                         0x00040000
#define PCIE_TL_TL_CONTROL_RESERVED_6_ALIGN                        0
#define PCIE_TL_TL_CONTROL_RESERVED_6_BITS                         1
#define PCIE_TL_TL_CONTROL_RESERVED_6_SHIFT                        18

/* PCIE_TL :: TL_CONTROL :: SLV_CMP_DIS_EP_ERROR [17:17] */
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_EP_ERROR_MASK               0x00020000
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_EP_ERROR_ALIGN              0
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_EP_ERROR_BITS               1
#define PCIE_TL_TL_CONTROL_SLV_CMP_DIS_EP_ERROR_SHIFT              17

/* PCIE_TL :: TL_CONTROL :: ENABLE_BYTECOUNT_CHECK [16:16] */
#define PCIE_TL_TL_CONTROL_ENABLE_BYTECOUNT_CHECK_MASK             0x00010000
#define PCIE_TL_TL_CONTROL_ENABLE_BYTECOUNT_CHECK_ALIGN            0
#define PCIE_TL_TL_CONTROL_ENABLE_BYTECOUNT_CHECK_BITS             1
#define PCIE_TL_TL_CONTROL_ENABLE_BYTECOUNT_CHECK_SHIFT            16

/* PCIE_TL :: TL_CONTROL :: NOT_USED [15:14] */
#define PCIE_TL_TL_CONTROL_NOT_USED_MASK                           0x0000c000
#define PCIE_TL_TL_CONTROL_NOT_USED_ALIGN                          0
#define PCIE_TL_TL_CONTROL_NOT_USED_BITS                           2
#define PCIE_TL_TL_CONTROL_NOT_USED_SHIFT                          14

/* PCIE_TL :: TL_CONTROL :: TRAFFIC_CLASS_DR [13:11] */
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DR_MASK                   0x00003800
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DR_ALIGN                  0
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DR_BITS                   3
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DR_SHIFT                  11

/* PCIE_TL :: TL_CONTROL :: TRAFFIC_CLASS_DW [10:08] */
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DW_MASK                   0x00000700
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DW_ALIGN                  0
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DW_BITS                   3
#define PCIE_TL_TL_CONTROL_TRAFFIC_CLASS_DW_SHIFT                  8

/* PCIE_TL :: TL_CONTROL :: NOT_USED_0 [07:06] */
#define PCIE_TL_TL_CONTROL_NOT_USED_0_MASK                         0x000000c0
#define PCIE_TL_TL_CONTROL_NOT_USED_0_ALIGN                        0
#define PCIE_TL_TL_CONTROL_NOT_USED_0_BITS                         2
#define PCIE_TL_TL_CONTROL_NOT_USED_0_SHIFT                        6

/* PCIE_TL :: TL_CONTROL :: NOT_USED_1 [05:00] */
#define PCIE_TL_TL_CONTROL_NOT_USED_1_MASK                         0x0000003f
#define PCIE_TL_TL_CONTROL_NOT_USED_1_ALIGN                        0
#define PCIE_TL_TL_CONTROL_NOT_USED_1_BITS                         6
#define PCIE_TL_TL_CONTROL_NOT_USED_1_SHIFT                        0


/****************************************************************************
 * PCIE_TL :: TRANSACTION_CONFIGURATION
 ***************************************************************************/
/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_RETRY_BUFFER_TIMING_MOD [31:31] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_RETRY_BUFFER_TIMING_MOD_MASK 0x80000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_RETRY_BUFFER_TIMING_MOD_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_RETRY_BUFFER_TIMING_MOD_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_RETRY_BUFFER_TIMING_MOD_SHIFT 31

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: RESERVED_0 [30:30] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_0_MASK          0x40000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_0_ALIGN         0
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_0_BITS          1
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_0_SHIFT         30

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: MSI_SINGLE_SHOT_ENABLE [29:29] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_SINGLE_SHOT_ENABLE_MASK 0x20000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_SINGLE_SHOT_ENABLE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_SINGLE_SHOT_ENABLE_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_SINGLE_SHOT_ENABLE_SHIFT 29

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: RESERVED_1 [28:28] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_1_MASK          0x10000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_1_ALIGN         0
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_1_BITS          1
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_1_SHIFT         28

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: SELECT_CORE_CLOCK_OVERRIDE [27:27] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_SELECT_CORE_CLOCK_OVERRIDE_MASK 0x08000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_SELECT_CORE_CLOCK_OVERRIDE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_SELECT_CORE_CLOCK_OVERRIDE_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_SELECT_CORE_CLOCK_OVERRIDE_SHIFT 27

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: CQ9139_FIX_ENABLE [26:26] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_CQ9139_FIX_ENABLE_MASK   0x04000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_CQ9139_FIX_ENABLE_ALIGN  0
#define PCIE_TL_TRANSACTION_CONFIGURATION_CQ9139_FIX_ENABLE_BITS   1
#define PCIE_TL_TRANSACTION_CONFIGURATION_CQ9139_FIX_ENABLE_SHIFT  26

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_CMPT_PWR_CHECK [25:25] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CMPT_PWR_CHECK_MASK 0x02000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CMPT_PWR_CHECK_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CMPT_PWR_CHECK_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CMPT_PWR_CHECK_SHIFT 25

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_CQ12696_FIX [24:24] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12696_FIX_MASK  0x01000000
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12696_FIX_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12696_FIX_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12696_FIX_SHIFT 24

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: DEVICE_SERIAL_NO_OVERRIDE [23:23] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_DEVICE_SERIAL_NO_OVERRIDE_MASK 0x00800000
#define PCIE_TL_TRANSACTION_CONFIGURATION_DEVICE_SERIAL_NO_OVERRIDE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_DEVICE_SERIAL_NO_OVERRIDE_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_DEVICE_SERIAL_NO_OVERRIDE_SHIFT 23

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_CQ12455_FIX [22:22] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12455_FIX_MASK  0x00400000
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12455_FIX_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12455_FIX_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_CQ12455_FIX_SHIFT 22

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_TC_VC_FILTERING_CHECK [21:21] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_TC_VC_FILTERING_CHECK_MASK 0x00200000
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_TC_VC_FILTERING_CHECK_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_TC_VC_FILTERING_CHECK_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_TC_VC_FILTERING_CHECK_SHIFT 21

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: DONT_GEN_HOT_PLUG_MSG [20:20] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_DONT_GEN_HOT_PLUG_MSG_MASK 0x00100000
#define PCIE_TL_TRANSACTION_CONFIGURATION_DONT_GEN_HOT_PLUG_MSG_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_DONT_GEN_HOT_PLUG_MSG_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_DONT_GEN_HOT_PLUG_MSG_SHIFT 20

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: IGNORE_HOTPLUG_MSG [19:19] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_IGNORE_HOTPLUG_MSG_MASK  0x00080000
#define PCIE_TL_TRANSACTION_CONFIGURATION_IGNORE_HOTPLUG_MSG_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_IGNORE_HOTPLUG_MSG_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_IGNORE_HOTPLUG_MSG_SHIFT 19

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: MSI_MULTMSG_CAPABLE [18:16] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_MULTMSG_CAPABLE_MASK 0x00070000
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_MULTMSG_CAPABLE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_MULTMSG_CAPABLE_BITS 3
#define PCIE_TL_TRANSACTION_CONFIGURATION_MSI_MULTMSG_CAPABLE_SHIFT 16

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: DATA_SELECT_LIMIT [15:12] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_DATA_SELECT_LIMIT_MASK   0x0000f000
#define PCIE_TL_TRANSACTION_CONFIGURATION_DATA_SELECT_LIMIT_ALIGN  0
#define PCIE_TL_TRANSACTION_CONFIGURATION_DATA_SELECT_LIMIT_BITS   4
#define PCIE_TL_TRANSACTION_CONFIGURATION_DATA_SELECT_LIMIT_SHIFT  12

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_PCIE_1_1_PL [11:11] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_PL_MASK  0x00000800
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_PL_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_PL_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_PL_SHIFT 11

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_PCIE_1_1_DL [10:10] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_DL_MASK  0x00000400
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_DL_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_DL_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_DL_SHIFT 10

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_PCIE_1_1_TL [09:09] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_TL_MASK  0x00000200
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_TL_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_TL_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_PCIE_1_1_TL_SHIFT 9

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: RESERVED_2 [08:07] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_2_MASK          0x00000180
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_2_ALIGN         0
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_2_BITS          2
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_2_SHIFT         7

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: PCIE_POWER_BUDGET_CAP_ENABLE [06:06] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_PCIE_POWER_BUDGET_CAP_ENABLE_MASK 0x00000040
#define PCIE_TL_TRANSACTION_CONFIGURATION_PCIE_POWER_BUDGET_CAP_ENABLE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_PCIE_POWER_BUDGET_CAP_ENABLE_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_PCIE_POWER_BUDGET_CAP_ENABLE_SHIFT 6

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: LOM_CONFIGURATION [05:05] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_LOM_CONFIGURATION_MASK   0x00000020
#define PCIE_TL_TRANSACTION_CONFIGURATION_LOM_CONFIGURATION_ALIGN  0
#define PCIE_TL_TRANSACTION_CONFIGURATION_LOM_CONFIGURATION_BITS   1
#define PCIE_TL_TRANSACTION_CONFIGURATION_LOM_CONFIGURATION_SHIFT  5

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: CONCATE_SELECT [04:04] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_CONCATE_SELECT_MASK      0x00000010
#define PCIE_TL_TRANSACTION_CONFIGURATION_CONCATE_SELECT_ALIGN     0
#define PCIE_TL_TRANSACTION_CONFIGURATION_CONCATE_SELECT_BITS      1
#define PCIE_TL_TRANSACTION_CONFIGURATION_CONCATE_SELECT_SHIFT     4

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: RESERVED_3 [03:03] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_3_MASK          0x00000008
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_3_ALIGN         0
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_3_BITS          1
#define PCIE_TL_TRANSACTION_CONFIGURATION_RESERVED_3_SHIFT         3

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_9468_FIX [02:02] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9468_FIX_MASK     0x00000004
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9468_FIX_ALIGN    0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9468_FIX_BITS     1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9468_FIX_SHIFT    2

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: POWER_STATE_WRITE_MEM_ENABLE [01:01] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_POWER_STATE_WRITE_MEM_ENABLE_MASK 0x00000002
#define PCIE_TL_TRANSACTION_CONFIGURATION_POWER_STATE_WRITE_MEM_ENABLE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_POWER_STATE_WRITE_MEM_ENABLE_BITS 1
#define PCIE_TL_TRANSACTION_CONFIGURATION_POWER_STATE_WRITE_MEM_ENABLE_SHIFT 1

/* PCIE_TL :: TRANSACTION_CONFIGURATION :: ENABLE_9709_ENABLE [00:00] */
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9709_ENABLE_MASK  0x00000001
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9709_ENABLE_ALIGN 0
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9709_ENABLE_BITS  1
#define PCIE_TL_TRANSACTION_CONFIGURATION_ENABLE_9709_ENABLE_SHIFT 0


/****************************************************************************
 * PCIE_TL :: WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC :: RESERVED_0 [31:00] */
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_RESERVED_0_MASK 0xffffffff
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_RESERVED_0_BITS 32
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_RESERVED_0_SHIFT 0


/****************************************************************************
 * PCIE_TL :: WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2
 ***************************************************************************/
/* PCIE_TL :: WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2 :: RESERVED_0 [31:00] */
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2_RESERVED_0_MASK 0xffffffff
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2_RESERVED_0_ALIGN 0
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2_RESERVED_0_BITS 32
#define PCIE_TL_WRITE_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_2_RESERVED_0_SHIFT 0


/****************************************************************************
 * PCIE_TL :: DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC :: REG_MADDR_UPR [31:00] */
#define PCIE_TL_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_REG_MADDR_UPR_MASK 0xffffffff
#define PCIE_TL_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_REG_MADDR_UPR_ALIGN 0
#define PCIE_TL_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_REG_MADDR_UPR_BITS 32
#define PCIE_TL_DMA_REQUEST_UPPER_ADDRESS_DIAGNOSTIC_REG_MADDR_UPR_SHIFT 0


/****************************************************************************
 * PCIE_TL :: DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2
 ***************************************************************************/
/* PCIE_TL :: DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2 :: REG_MADDR_LWR [31:00] */
#define PCIE_TL_DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2_REG_MADDR_LWR_MASK 0xffffffff
#define PCIE_TL_DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2_REG_MADDR_LWR_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2_REG_MADDR_LWR_BITS 32
#define PCIE_TL_DMA_REQUEST_LOWER_ADDRESS_DIAGNOSTIC_2_REG_MADDR_LWR_SHIFT 0


/****************************************************************************
 * PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC :: REG_MLEN_BE [31:24] */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_REG_MLEN_BE_MASK 0xff000000
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_REG_MLEN_BE_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_REG_MLEN_BE_BITS 8
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_REG_MLEN_BE_SHIFT 24

/* PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC :: DMA_REQUEST_FIRST_DW_BYTE_ENABLES [23:20] */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_FIRST_DW_BYTE_ENABLES_MASK 0x00f00000
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_FIRST_DW_BYTE_ENABLES_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_FIRST_DW_BYTE_ENABLES_BITS 4
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_FIRST_DW_BYTE_ENABLES_SHIFT 20

/* PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC :: DMA_REQUEST_LAST_DW_BYTE_ENABLES [19:16] */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_LAST_DW_BYTE_ENABLES_MASK 0x000f0000
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_LAST_DW_BYTE_ENABLES_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_LAST_DW_BYTE_ENABLES_BITS 4
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_LAST_DW_BYTE_ENABLES_SHIFT 16

/* PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC :: RESERVED_0 [15:11] */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_RESERVED_0_MASK 0x0000f800
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_RESERVED_0_BITS 5
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_RESERVED_0_SHIFT 11

/* PCIE_TL :: DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC :: DMA_REQUEST_DW_LENGTH [10:00] */
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_DW_LENGTH_MASK 0x000007ff
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_DW_LENGTH_ALIGN 0
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_DW_LENGTH_BITS 11
#define PCIE_TL_DMA_REQUEST_LENGTH_BYTE_ENABLE_DIAGNOSTIC_DMA_REQUEST_DW_LENGTH_SHIFT 0


/****************************************************************************
 * PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: REG_MTAG_ATTR [31:19] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_REG_MTAG_ATTR_MASK 0xfff80000
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_REG_MTAG_ATTR_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_REG_MTAG_ATTR_BITS 13
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_REG_MTAG_ATTR_SHIFT 19

/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: DMA_REQUEST_FUNCTION [18:16] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_FUNCTION_MASK 0x00070000
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_FUNCTION_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_FUNCTION_BITS 3
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_FUNCTION_SHIFT 16

/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: RESERVED_0 [15:13] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_0_MASK 0x0000e000
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_0_BITS 3
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_0_SHIFT 13

/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: DMA_REQUEST_ATTRIBUTES [12:08] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_ATTRIBUTES_MASK 0x00001f00
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_ATTRIBUTES_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_ATTRIBUTES_BITS 5
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_ATTRIBUTES_SHIFT 8

/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: RESERVED_1 [07:05] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_1_MASK 0x000000e0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_1_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_1_BITS 3
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_RESERVED_1_SHIFT 5

/* PCIE_TL :: DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC :: DMA_REQUEST_TAG [04:00] */
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_TAG_MASK 0x0000001f
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_TAG_ALIGN 0
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_TAG_BITS 5
#define PCIE_TL_DMA_REQUEST_TAG_ATTRIBUTE_FUNCTION_DIAGNOSTIC_DMA_REQUEST_TAG_SHIFT 0


/****************************************************************************
 * PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: REG_SPLIT_ID [31:16] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_REG_SPLIT_ID_MASK    0xffff0000
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_REG_SPLIT_ID_ALIGN   0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_REG_SPLIT_ID_BITS    16
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_REG_SPLIT_ID_SHIFT   16

/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: RESERVED_0 [15:13] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_0_MASK      0x0000e000
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_0_ALIGN     0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_0_BITS      3
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_0_SHIFT     13

/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: READ_DMA_SPLIT_ATTRIBUTES [12:11] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_ATTRIBUTES_MASK 0x00001800
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_ATTRIBUTES_ALIGN 0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_ATTRIBUTES_BITS 2
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_ATTRIBUTES_SHIFT 11

/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: READ_DMA_SPLIT_TC [10:08] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TC_MASK 0x00000700
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TC_ALIGN 0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TC_BITS 3
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TC_SHIFT 8

/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: RESERVED_1 [07:05] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_1_MASK      0x000000e0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_1_ALIGN     0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_1_BITS      3
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_RESERVED_1_SHIFT     5

/* PCIE_TL :: READ_DMA_SPLIT_IDS_DIAGNOSTIC :: READ_DMA_SPLIT_TAG [04:00] */
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TAG_MASK 0x0000001f
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TAG_ALIGN 0
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TAG_BITS 5
#define PCIE_TL_READ_DMA_SPLIT_IDS_DIAGNOSTIC_READ_DMA_SPLIT_TAG_SHIFT 0


/****************************************************************************
 * PCIE_TL :: READ_DMA_SPLIT_LENGTH_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: READ_DMA_SPLIT_LENGTH_DIAGNOSTIC :: REG_SPLIT_LEN [31:13] */
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_REG_SPLIT_LEN_MASK 0xffffe000
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_REG_SPLIT_LEN_ALIGN 0
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_REG_SPLIT_LEN_BITS 19
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_REG_SPLIT_LEN_SHIFT 13

/* PCIE_TL :: READ_DMA_SPLIT_LENGTH_DIAGNOSTIC :: READ_DMA_SPLIT_INITIAL_BYTE_COUNT [12:00] */
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_READ_DMA_SPLIT_INITIAL_BYTE_COUNT_MASK 0x00001fff
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_READ_DMA_SPLIT_INITIAL_BYTE_COUNT_ALIGN 0
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_READ_DMA_SPLIT_INITIAL_BYTE_COUNT_BITS 13
#define PCIE_TL_READ_DMA_SPLIT_LENGTH_DIAGNOSTIC_READ_DMA_SPLIT_INITIAL_BYTE_COUNT_SHIFT 0


/****************************************************************************
 * PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: REG_SM_R0_R3 [31:31] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_REG_SM_R0_R3_MASK 0x80000000
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_REG_SM_R0_R3_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_REG_SM_R0_R3_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_REG_SM_R0_R3_SHIFT 31

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: TLP_TRANSMITTER_DATA_STATE_MACHINE [30:28] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_DATA_STATE_MACHINE_MASK 0x70000000
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_DATA_STATE_MACHINE_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_DATA_STATE_MACHINE_BITS 3
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_DATA_STATE_MACHINE_SHIFT 28

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: TLP_TRANSMITTER_ARBITRATION_STATE_MACHINE [27:23] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_ARBITRATION_STATE_MACHINE_MASK 0x0f800000
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_ARBITRATION_STATE_MACHINE_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_ARBITRATION_STATE_MACHINE_BITS 5
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TLP_TRANSMITTER_ARBITRATION_STATE_MACHINE_SHIFT 23

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: RESERVED_0 [22:07] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_RESERVED_0_MASK 0x007fff80
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_RESERVED_0_BITS 16
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_RESERVED_0_SHIFT 7

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: READ_DMA_RAW_REQUEST [06:06] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_RAW_REQUEST_MASK 0x00000040
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_RAW_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_RAW_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_RAW_REQUEST_SHIFT 6

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: WRITE_DMA_RAW_REQUEST [05:05] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_RAW_REQUEST_MASK 0x00000020
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_RAW_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_RAW_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_RAW_REQUEST_SHIFT 5

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: INTERRUPT_MSG_GATED_REQUEST [04:04] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_INTERRUPT_MSG_GATED_REQUEST_MASK 0x00000010
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_INTERRUPT_MSG_GATED_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_INTERRUPT_MSG_GATED_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_INTERRUPT_MSG_GATED_REQUEST_SHIFT 4

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: MSI_DMA_GATED_REQUEST [03:03] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_MSI_DMA_GATED_REQUEST_MASK 0x00000008
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_MSI_DMA_GATED_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_MSI_DMA_GATED_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_MSI_DMA_GATED_REQUEST_SHIFT 3

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: TARGET_COMPLETION_OR_MSG_GATED_REQUEST [02:02] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TARGET_COMPLETION_OR_MSG_GATED_REQUEST_MASK 0x00000004
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TARGET_COMPLETION_OR_MSG_GATED_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TARGET_COMPLETION_OR_MSG_GATED_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_TARGET_COMPLETION_OR_MSG_GATED_REQUEST_SHIFT 2

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: READ_DMA_GATED_REQUEST [01:01] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_GATED_REQUEST_MASK 0x00000002
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_GATED_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_GATED_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_READ_DMA_GATED_REQUEST_SHIFT 1

/* PCIE_TL :: XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC :: WRITE_DMA_GATED_REQUEST [00:00] */
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_GATED_REQUEST_MASK 0x00000001
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_GATED_REQUEST_ALIGN 0
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_GATED_REQUEST_BITS 1
#define PCIE_TL_XMT_STATE_MACHINES_AND_REQUEST_DIAGNOSTIC_WRITE_DMA_GATED_REQUEST_SHIFT 0


/****************************************************************************
 * PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: REG_DMA_CMPT_MISC2 [31:29] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_REG_DMA_CMPT_MISC2_MASK 0xe0000000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_REG_DMA_CMPT_MISC2_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_REG_DMA_CMPT_MISC2_BITS 3
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_REG_DMA_CMPT_MISC2_SHIFT 29

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: SPLIT_BYTE_LENGTH_REMAINING [28:16] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_BYTE_LENGTH_REMAINING_MASK 0x1fff0000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_BYTE_LENGTH_REMAINING_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_BYTE_LENGTH_REMAINING_BITS 13
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_BYTE_LENGTH_REMAINING_SHIFT 16

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: NOT_USED [15:15] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_NOT_USED_MASK      0x00008000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_NOT_USED_ALIGN     0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_NOT_USED_BITS      1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_NOT_USED_SHIFT     15

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: LAST_COMPLETION_TLP_INDICATOR_SPLITCTL_GENERATED [14:14] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_SPLITCTL_GENERATED_MASK 0x00004000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_SPLITCTL_GENERATED_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_SPLITCTL_GENERATED_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_SPLITCTL_GENERATED_SHIFT 14

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: LAST_COMPLETION_TLP_INDICATOR_DMA_CMPT_GENERATED [13:13] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_DMA_CMPT_GENERATED_MASK 0x00002000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_DMA_CMPT_GENERATED_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_DMA_CMPT_GENERATED_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_LAST_COMPLETION_TLP_INDICATOR_DMA_CMPT_GENERATED_SHIFT 13

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: DW_LENGTH_REMAINING_IN_CURRENT_COMPLETION_TLP_IS_GREATER_THAN_1 [12:12] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_DW_LENGTH_REMAINING_IN_CURRENT_COMPLETION_TLP_IS_GREATER_THAN_1_MASK 0x00001000
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_DW_LENGTH_REMAINING_IN_CURRENT_COMPLETION_TLP_IS_GREATER_THAN_1_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_DW_LENGTH_REMAINING_IN_CURRENT_COMPLETION_TLP_IS_GREATER_THAN_1_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_DW_LENGTH_REMAINING_IN_CURRENT_COMPLETION_TLP_IS_GREATER_THAN_1_SHIFT 12

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: SPLIT_TRANSACTION_ACTIVE_SPLIT_PENDING_BLOCK_REQUEST [11:11] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_TRANSACTION_ACTIVE_SPLIT_PENDING_BLOCK_REQUEST_MASK 0x00000800
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_TRANSACTION_ACTIVE_SPLIT_PENDING_BLOCK_REQUEST_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_TRANSACTION_ACTIVE_SPLIT_PENDING_BLOCK_REQUEST_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_TRANSACTION_ACTIVE_SPLIT_PENDING_BLOCK_REQUEST_SHIFT 11

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: COMPLETION_TLP_MATCHES_REQUEST_WITHOUT_BC_LADDR_CHECKS [10:10] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_WITHOUT_BC_LADDR_CHECKS_MASK 0x00000400
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_WITHOUT_BC_LADDR_CHECKS_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_WITHOUT_BC_LADDR_CHECKS_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_WITHOUT_BC_LADDR_CHECKS_SHIFT 10

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: COMPLETION_TLP_MATCHES_REQUEST_FULLY [09:09] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_FULLY_MASK 0x00000200
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_FULLY_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_FULLY_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TLP_MATCHES_REQUEST_FULLY_SHIFT 9

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: SPLIT_DW_DATA_VALID_ADDRESS_ACK [08:08] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_DW_DATA_VALID_ADDRESS_ACK_MASK 0x00000100
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_DW_DATA_VALID_ADDRESS_ACK_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_DW_DATA_VALID_ADDRESS_ACK_BITS 1
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_SPLIT_DW_DATA_VALID_ADDRESS_ACK_SHIFT 8

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: COMPLETION_TOO_MUCH_DATA_ERROR_COUNTER [07:04] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TOO_MUCH_DATA_ERROR_COUNTER_MASK 0x000000f0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TOO_MUCH_DATA_ERROR_COUNTER_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TOO_MUCH_DATA_ERROR_COUNTER_BITS 4
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_COMPLETION_TOO_MUCH_DATA_ERROR_COUNTER_SHIFT 4

/* PCIE_TL :: DMA_COMPLETION_MISC__DIAGNOSTIC :: FRAME_DEAD_TIME_ERROR_COUNTER [03:00] */
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_FRAME_DEAD_TIME_ERROR_COUNTER_MASK 0x0000000f
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_FRAME_DEAD_TIME_ERROR_COUNTER_ALIGN 0
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_FRAME_DEAD_TIME_ERROR_COUNTER_BITS 4
#define PCIE_TL_DMA_COMPLETION_MISC__DIAGNOSTIC_FRAME_DEAD_TIME_ERROR_COUNTER_SHIFT 0


/****************************************************************************
 * PCIE_TL :: SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC :: REG_SPLITCTL_MISC0 [31:29] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_REG_SPLITCTL_MISC0_MASK 0xe0000000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_REG_SPLITCTL_MISC0_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_REG_SPLITCTL_MISC0_BITS 3
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_REG_SPLITCTL_MISC0_SHIFT 29

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC :: LOOKUP_RESULT_FOR_EXPECTED_BYTE_COUNT_REMAINING [28:16] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_BYTE_COUNT_REMAINING_MASK 0x1fff0000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_BYTE_COUNT_REMAINING_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_BYTE_COUNT_REMAINING_BITS 13
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_BYTE_COUNT_REMAINING_SHIFT 16

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC :: LOOKUP_RESULT_FOR_EXPECTED_REQUESTER_ID [15:00] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_REQUESTER_ID_MASK 0x0000ffff
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_REQUESTER_ID_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_REQUESTER_ID_BITS 16
#define PCIE_TL_SPLIT_CONTROLLER_MISC_0_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_REQUESTER_ID_SHIFT 0


/****************************************************************************
 * PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: REG_SPLITCTL_MISC1 [31:16] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_REG_SPLITCTL_MISC1_MASK 0xffff0000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_REG_SPLITCTL_MISC1_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_REG_SPLITCTL_MISC1_BITS 16
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_REG_SPLITCTL_MISC1_SHIFT 16

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: RESERVED_0 [15:15] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_0_MASK 0x00008000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_0_BITS 1
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_0_SHIFT 15

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: LOOKUP_RESULT_FOR_EXPECTED_LOWER_ADDRESS [14:08] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_LOWER_ADDRESS_MASK 0x00007f00
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_LOWER_ADDRESS_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_LOWER_ADDRESS_BITS 7
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_LOWER_ADDRESS_SHIFT 8

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: RESERVED_1 [07:07] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_1_MASK 0x00000080
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_1_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_1_BITS 1
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_RESERVED_1_SHIFT 7

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: LOOKUP_RESULT_FOR_EXPECTED_ATTRIBUTE [06:05] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_ATTRIBUTE_MASK 0x00000060
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_ATTRIBUTE_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_ATTRIBUTE_BITS 2
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_RESULT_FOR_EXPECTED_ATTRIBUTE_SHIFT 5

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC :: LOOKUP_TAG [04:00] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_TAG_MASK 0x0000001f
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_TAG_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_TAG_BITS 5
#define PCIE_TL_SPLIT_CONTROLLER_MISC_1_DIAGNOSTIC_LOOKUP_TAG_SHIFT 0


/****************************************************************************
 * PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC
 ***************************************************************************/
/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: REG_SPLITCTL_MISC2 [31:31] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_REG_SPLITCTL_MISC2_MASK 0x80000000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_REG_SPLITCTL_MISC2_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_REG_SPLITCTL_MISC2_BITS 1
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_REG_SPLITCTL_MISC2_SHIFT 31

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: COMPLETION_TLP_MATCHES_EXPECTED_LOWER_ADDRESS [30:30] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_EXPECTED_LOWER_ADDRESS_MASK 0x40000000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_EXPECTED_LOWER_ADDRESS_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_EXPECTED_LOWER_ADDRESS_BITS 1
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_EXPECTED_LOWER_ADDRESS_SHIFT 30

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: COMPLETION_TLP_MATCHES_VALID_TAG [29:29] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_VALID_TAG_MASK 0x20000000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_VALID_TAG_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_VALID_TAG_BITS 1
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_COMPLETION_TLP_MATCHES_VALID_TAG_SHIFT 29

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: UPDATED_BYTE_COUNT [28:16] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_UPDATED_BYTE_COUNT_MASK 0x1fff0000
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_UPDATED_BYTE_COUNT_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_UPDATED_BYTE_COUNT_BITS 13
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_UPDATED_BYTE_COUNT_SHIFT 16

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: RESERVED_0 [15:08] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_RESERVED_0_MASK 0x0000ff00
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_RESERVED_0_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_RESERVED_0_BITS 8
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_RESERVED_0_SHIFT 8

/* PCIE_TL :: SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC :: SPLIT_TABLE_VALID_ARRAY [07:00] */
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_SPLIT_TABLE_VALID_ARRAY_MASK 0x000000ff
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_SPLIT_TABLE_VALID_ARRAY_ALIGN 0
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_SPLIT_TABLE_VALID_ARRAY_BITS 8
#define PCIE_TL_SPLIT_CONTROLLER_MISC_2_DIAGNOSTIC_SPLIT_TABLE_VALID_ARRAY_SHIFT 0


/****************************************************************************
 * PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO
 ***************************************************************************/
/* PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO :: RESERVED_0 [31:17] */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_RESERVED_0_MASK        0xfffe0000
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_RESERVED_0_ALIGN       0
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_RESERVED_0_BITS        15
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_RESERVED_0_SHIFT       17

/* PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO :: CONFIG_WRITE_INDICATER [16:16] */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_CONFIG_WRITE_INDICATER_MASK 0x00010000
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_CONFIG_WRITE_INDICATER_ALIGN 0
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_CONFIG_WRITE_INDICATER_BITS 1
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_CONFIG_WRITE_INDICATER_SHIFT 16

/* PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO :: BUS_NUMBER [15:08] */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_BUS_NUMBER_MASK        0x0000ff00
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_BUS_NUMBER_ALIGN       0
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_BUS_NUMBER_BITS        8
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_BUS_NUMBER_SHIFT       8

/* PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO :: DEVICE_NUMBER [07:03] */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_DEVICE_NUMBER_MASK     0x000000f8
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_DEVICE_NUMBER_ALIGN    0
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_DEVICE_NUMBER_BITS     5
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_DEVICE_NUMBER_SHIFT    3

/* PCIE_TL :: TL_BUS_NO_DEV__NO__FUNC__NO :: FUNCTION_NUMBER [02:00] */
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_FUNCTION_NUMBER_MASK   0x00000007
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_FUNCTION_NUMBER_ALIGN  0
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_FUNCTION_NUMBER_BITS   3
#define PCIE_TL_TL_BUS_NO_DEV__NO__FUNC__NO_FUNCTION_NUMBER_SHIFT  0


/****************************************************************************
 * PCIE_TL :: TL_DEBUG
 ***************************************************************************/
/* PCIE_TL :: TL_DEBUG :: A4_DEVICE_INDICATION_BIT [31:31] */
#define PCIE_TL_TL_DEBUG_A4_DEVICE_INDICATION_BIT_MASK             0x80000000
#define PCIE_TL_TL_DEBUG_A4_DEVICE_INDICATION_BIT_ALIGN            0
#define PCIE_TL_TL_DEBUG_A4_DEVICE_INDICATION_BIT_BITS             1
#define PCIE_TL_TL_DEBUG_A4_DEVICE_INDICATION_BIT_SHIFT            31

/* PCIE_TL :: TL_DEBUG :: B1_DEVICE_INDICATION_BIT [30:30] */
#define PCIE_TL_TL_DEBUG_B1_DEVICE_INDICATION_BIT_MASK             0x40000000
#define PCIE_TL_TL_DEBUG_B1_DEVICE_INDICATION_BIT_ALIGN            0
#define PCIE_TL_TL_DEBUG_B1_DEVICE_INDICATION_BIT_BITS             1
#define PCIE_TL_TL_DEBUG_B1_DEVICE_INDICATION_BIT_SHIFT            30

/* PCIE_TL :: TL_DEBUG :: RESERVED_0 [29:00] */
#define PCIE_TL_TL_DEBUG_RESERVED_0_MASK                           0x3fffffff
#define PCIE_TL_TL_DEBUG_RESERVED_0_ALIGN                          0
#define PCIE_TL_TL_DEBUG_RESERVED_0_BITS                           30
#define PCIE_TL_TL_DEBUG_RESERVED_0_SHIFT                          0


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_DLL
 ***************************************************************************/
/****************************************************************************
 * PCIE_DLL :: DATA_LINK_CONTROL
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_0 [31:30] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_0_MASK                 0xc0000000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_0_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_0_BITS                 2
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_0_SHIFT                30

/* PCIE_DLL :: DATA_LINK_CONTROL :: CQ28001_FIX_ENABLE [29:29] */
#define PCIE_DLL_DATA_LINK_CONTROL_CQ28001_FIX_ENABLE_MASK         0x20000000
#define PCIE_DLL_DATA_LINK_CONTROL_CQ28001_FIX_ENABLE_ALIGN        0
#define PCIE_DLL_DATA_LINK_CONTROL_CQ28001_FIX_ENABLE_BITS         1
#define PCIE_DLL_DATA_LINK_CONTROL_CQ28001_FIX_ENABLE_SHIFT        29

/* PCIE_DLL :: DATA_LINK_CONTROL :: CQ27820_FIX_ENABLE [28:28] */
#define PCIE_DLL_DATA_LINK_CONTROL_CQ27820_FIX_ENABLE_MASK         0x10000000
#define PCIE_DLL_DATA_LINK_CONTROL_CQ27820_FIX_ENABLE_ALIGN        0
#define PCIE_DLL_DATA_LINK_CONTROL_CQ27820_FIX_ENABLE_BITS         1
#define PCIE_DLL_DATA_LINK_CONTROL_CQ27820_FIX_ENABLE_SHIFT        28

/* PCIE_DLL :: DATA_LINK_CONTROL :: ASPM_L1_ENABLE [27:27] */
#define PCIE_DLL_DATA_LINK_CONTROL_ASPM_L1_ENABLE_MASK             0x08000000
#define PCIE_DLL_DATA_LINK_CONTROL_ASPM_L1_ENABLE_ALIGN            0
#define PCIE_DLL_DATA_LINK_CONTROL_ASPM_L1_ENABLE_BITS             1
#define PCIE_DLL_DATA_LINK_CONTROL_ASPM_L1_ENABLE_SHIFT            27

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_1 [26:25] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_1_MASK                 0x06000000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_1_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_1_BITS                 2
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_1_SHIFT                25

/* PCIE_DLL :: DATA_LINK_CONTROL :: CQ11211 [24:24] */
#define PCIE_DLL_DATA_LINK_CONTROL_CQ11211_MASK                    0x01000000
#define PCIE_DLL_DATA_LINK_CONTROL_CQ11211_ALIGN                   0
#define PCIE_DLL_DATA_LINK_CONTROL_CQ11211_BITS                    1
#define PCIE_DLL_DATA_LINK_CONTROL_CQ11211_SHIFT                   24

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_2 [23:23] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_2_MASK                 0x00800000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_2_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_2_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_2_SHIFT                23

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_3 [22:22] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_3_MASK                 0x00400000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_3_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_3_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_3_SHIFT                22

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_4 [21:21] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_4_MASK                 0x00200000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_4_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_4_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_4_SHIFT                21

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_5 [20:20] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_5_MASK                 0x00100000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_5_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_5_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_5_SHIFT                20

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_6 [19:19] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_6_MASK                 0x00080000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_6_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_6_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_6_SHIFT                19

/* PCIE_DLL :: DATA_LINK_CONTROL :: PLL_REFSEL_SWITCH_CONTROL_CQ11011 [18:18] */
#define PCIE_DLL_DATA_LINK_CONTROL_PLL_REFSEL_SWITCH_CONTROL_CQ11011_MASK 0x00040000
#define PCIE_DLL_DATA_LINK_CONTROL_PLL_REFSEL_SWITCH_CONTROL_CQ11011_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_PLL_REFSEL_SWITCH_CONTROL_CQ11011_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_PLL_REFSEL_SWITCH_CONTROL_CQ11011_SHIFT 18

/* PCIE_DLL :: DATA_LINK_CONTROL :: RESERVED_7 [17:17] */
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_7_MASK                 0x00020000
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_7_ALIGN                0
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_7_BITS                 1
#define PCIE_DLL_DATA_LINK_CONTROL_RESERVED_7_SHIFT                17

/* PCIE_DLL :: DATA_LINK_CONTROL :: POWER_MANAGEMENT_CONTROL [16:16] */
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_MASK   0x00010000
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_ALIGN  0
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_BITS   1
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_SHIFT  16

/* PCIE_DLL :: DATA_LINK_CONTROL :: POWER_DOWN_SERDES_TRANSMITTER [15:15] */
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_TRANSMITTER_MASK 0x00008000
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_TRANSMITTER_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_TRANSMITTER_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_TRANSMITTER_SHIFT 15

/* PCIE_DLL :: DATA_LINK_CONTROL :: POWER_DOWN_SERDES_PLL [14:14] */
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_PLL_MASK      0x00004000
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_PLL_ALIGN     0
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_PLL_BITS      1
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_PLL_SHIFT     14

/* PCIE_DLL :: DATA_LINK_CONTROL :: POWER_DOWN_SERDES_RECEIVER [13:13] */
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_RECEIVER_MASK 0x00002000
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_RECEIVER_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_RECEIVER_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_DOWN_SERDES_RECEIVER_SHIFT 13

/* PCIE_DLL :: DATA_LINK_CONTROL :: ENABLE_BEACON [12:12] */
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_BEACON_MASK              0x00001000
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_BEACON_ALIGN             0
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_BEACON_BITS              1
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_BEACON_SHIFT             12

/* PCIE_DLL :: DATA_LINK_CONTROL :: AUTOMATIC_TIMER_THRESHOLD_ENABLE [11:11] */
#define PCIE_DLL_DATA_LINK_CONTROL_AUTOMATIC_TIMER_THRESHOLD_ENABLE_MASK 0x00000800
#define PCIE_DLL_DATA_LINK_CONTROL_AUTOMATIC_TIMER_THRESHOLD_ENABLE_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_AUTOMATIC_TIMER_THRESHOLD_ENABLE_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_AUTOMATIC_TIMER_THRESHOLD_ENABLE_SHIFT 11

/* PCIE_DLL :: DATA_LINK_CONTROL :: ENABLE_DLLP_TIMEOUT_MECHANISM [10:10] */
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_DLLP_TIMEOUT_MECHANISM_MASK 0x00000400
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_DLLP_TIMEOUT_MECHANISM_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_DLLP_TIMEOUT_MECHANISM_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_ENABLE_DLLP_TIMEOUT_MECHANISM_SHIFT 10

/* PCIE_DLL :: DATA_LINK_CONTROL :: CHECK_RECEIVE_FLOW_CONTROL_CREDITS [09:09] */
#define PCIE_DLL_DATA_LINK_CONTROL_CHECK_RECEIVE_FLOW_CONTROL_CREDITS_MASK 0x00000200
#define PCIE_DLL_DATA_LINK_CONTROL_CHECK_RECEIVE_FLOW_CONTROL_CREDITS_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_CHECK_RECEIVE_FLOW_CONTROL_CREDITS_BITS 1
#define PCIE_DLL_DATA_LINK_CONTROL_CHECK_RECEIVE_FLOW_CONTROL_CREDITS_SHIFT 9

/* PCIE_DLL :: DATA_LINK_CONTROL :: LINK_ENABLE [08:08] */
#define PCIE_DLL_DATA_LINK_CONTROL_LINK_ENABLE_MASK                0x00000100
#define PCIE_DLL_DATA_LINK_CONTROL_LINK_ENABLE_ALIGN               0
#define PCIE_DLL_DATA_LINK_CONTROL_LINK_ENABLE_BITS                1
#define PCIE_DLL_DATA_LINK_CONTROL_LINK_ENABLE_SHIFT               8

/* PCIE_DLL :: DATA_LINK_CONTROL :: POWER_MANAGEMENT_CONTROL_2 [07:00] */
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_2_MASK 0x000000ff
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_2_ALIGN 0
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_2_BITS 8
#define PCIE_DLL_DATA_LINK_CONTROL_POWER_MANAGEMENT_CONTROL_2_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_STATUS
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_STATUS :: RESERVED_0 [31:26] */
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_0_MASK                  0xfc000000
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_0_ALIGN                 0
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_0_BITS                  6
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_0_SHIFT                 26

/* PCIE_DLL :: DATA_LINK_STATUS :: PHY_LINK_STATE [25:23] */
#define PCIE_DLL_DATA_LINK_STATUS_PHY_LINK_STATE_MASK              0x03800000
#define PCIE_DLL_DATA_LINK_STATUS_PHY_LINK_STATE_ALIGN             0
#define PCIE_DLL_DATA_LINK_STATUS_PHY_LINK_STATE_BITS              3
#define PCIE_DLL_DATA_LINK_STATUS_PHY_LINK_STATE_SHIFT             23

/* PCIE_DLL :: DATA_LINK_STATUS :: POWER_MANAGEMENT_STATE [22:19] */
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_STATE_MASK      0x00780000
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_STATE_ALIGN     0
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_STATE_BITS      4
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_STATE_SHIFT     19

/* PCIE_DLL :: DATA_LINK_STATUS :: POWER_MANAGEMENT_SUB_STATE [18:17] */
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_SUB_STATE_MASK  0x00060000
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_SUB_STATE_ALIGN 0
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_SUB_STATE_BITS  2
#define PCIE_DLL_DATA_LINK_STATUS_POWER_MANAGEMENT_SUB_STATE_SHIFT 17

/* PCIE_DLL :: DATA_LINK_STATUS :: DATA_LINK_UP [16:16] */
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_UP_MASK                0x00010000
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_UP_ALIGN               0
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_UP_BITS                1
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_UP_SHIFT               16

/* PCIE_DLL :: DATA_LINK_STATUS :: RESERVED_1 [15:11] */
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_1_MASK                  0x0000f800
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_1_ALIGN                 0
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_1_BITS                  5
#define PCIE_DLL_DATA_LINK_STATUS_RESERVED_1_SHIFT                 11

/* PCIE_DLL :: DATA_LINK_STATUS :: PME_TURN_OFF_STATUS_IN_D0 [10:10] */
#define PCIE_DLL_DATA_LINK_STATUS_PME_TURN_OFF_STATUS_IN_D0_MASK   0x00000400
#define PCIE_DLL_DATA_LINK_STATUS_PME_TURN_OFF_STATUS_IN_D0_ALIGN  0
#define PCIE_DLL_DATA_LINK_STATUS_PME_TURN_OFF_STATUS_IN_D0_BITS   1
#define PCIE_DLL_DATA_LINK_STATUS_PME_TURN_OFF_STATUS_IN_D0_SHIFT  10

/* PCIE_DLL :: DATA_LINK_STATUS :: FLOW_CONTROL_UPDATE_TIMEOUT [09:09] */
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_UPDATE_TIMEOUT_MASK 0x00000200
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_UPDATE_TIMEOUT_ALIGN 0
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_UPDATE_TIMEOUT_BITS 1
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_UPDATE_TIMEOUT_SHIFT 9

/* PCIE_DLL :: DATA_LINK_STATUS :: FLOW_CONTROL_RECEIVE_OVERFLOW [08:08] */
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_RECEIVE_OVERFLOW_MASK 0x00000100
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_RECEIVE_OVERFLOW_ALIGN 0
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_RECEIVE_OVERFLOW_BITS 1
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_RECEIVE_OVERFLOW_SHIFT 8

/* PCIE_DLL :: DATA_LINK_STATUS :: FLOW_CONTROL_PROTOCOL_ERROR [07:07] */
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_MASK 0x00000080
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_ALIGN 0
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_BITS 1
#define PCIE_DLL_DATA_LINK_STATUS_FLOW_CONTROL_PROTOCOL_ERROR_SHIFT 7

/* PCIE_DLL :: DATA_LINK_STATUS :: DATA_LINK_PROTOCOL_ERROR [06:06] */
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_PROTOCOL_ERROR_MASK    0x00000040
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_PROTOCOL_ERROR_ALIGN   0
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_PROTOCOL_ERROR_BITS    1
#define PCIE_DLL_DATA_LINK_STATUS_DATA_LINK_PROTOCOL_ERROR_SHIFT   6

/* PCIE_DLL :: DATA_LINK_STATUS :: REPLAY_ROLLOVER [05:05] */
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_ROLLOVER_MASK             0x00000020
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_ROLLOVER_ALIGN            0
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_ROLLOVER_BITS             1
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_ROLLOVER_SHIFT            5

/* PCIE_DLL :: DATA_LINK_STATUS :: REPLAY_TIMEOUT [04:04] */
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_TIMEOUT_MASK              0x00000010
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_TIMEOUT_ALIGN             0
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_TIMEOUT_BITS              1
#define PCIE_DLL_DATA_LINK_STATUS_REPLAY_TIMEOUT_SHIFT             4

/* PCIE_DLL :: DATA_LINK_STATUS :: NAK_RECEIVED [03:03] */
#define PCIE_DLL_DATA_LINK_STATUS_NAK_RECEIVED_MASK                0x00000008
#define PCIE_DLL_DATA_LINK_STATUS_NAK_RECEIVED_ALIGN               0
#define PCIE_DLL_DATA_LINK_STATUS_NAK_RECEIVED_BITS                1
#define PCIE_DLL_DATA_LINK_STATUS_NAK_RECEIVED_SHIFT               3

/* PCIE_DLL :: DATA_LINK_STATUS :: DLLP_ERROR [02:02] */
#define PCIE_DLL_DATA_LINK_STATUS_DLLP_ERROR_MASK                  0x00000004
#define PCIE_DLL_DATA_LINK_STATUS_DLLP_ERROR_ALIGN                 0
#define PCIE_DLL_DATA_LINK_STATUS_DLLP_ERROR_BITS                  1
#define PCIE_DLL_DATA_LINK_STATUS_DLLP_ERROR_SHIFT                 2

/* PCIE_DLL :: DATA_LINK_STATUS :: BAD_TLP_SEQUENCE_NUMBER [01:01] */
#define PCIE_DLL_DATA_LINK_STATUS_BAD_TLP_SEQUENCE_NUMBER_MASK     0x00000002
#define PCIE_DLL_DATA_LINK_STATUS_BAD_TLP_SEQUENCE_NUMBER_ALIGN    0
#define PCIE_DLL_DATA_LINK_STATUS_BAD_TLP_SEQUENCE_NUMBER_BITS     1
#define PCIE_DLL_DATA_LINK_STATUS_BAD_TLP_SEQUENCE_NUMBER_SHIFT    1

/* PCIE_DLL :: DATA_LINK_STATUS :: TLP_ERROR [00:00] */
#define PCIE_DLL_DATA_LINK_STATUS_TLP_ERROR_MASK                   0x00000001
#define PCIE_DLL_DATA_LINK_STATUS_TLP_ERROR_ALIGN                  0
#define PCIE_DLL_DATA_LINK_STATUS_TLP_ERROR_BITS                   1
#define PCIE_DLL_DATA_LINK_STATUS_TLP_ERROR_SHIFT                  0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_ATTENTION
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_ATTENTION :: RESERVED_0 [31:06] */
#define PCIE_DLL_DATA_LINK_ATTENTION_RESERVED_0_MASK               0xffffffc0
#define PCIE_DLL_DATA_LINK_ATTENTION_RESERVED_0_ALIGN              0
#define PCIE_DLL_DATA_LINK_ATTENTION_RESERVED_0_BITS               26
#define PCIE_DLL_DATA_LINK_ATTENTION_RESERVED_0_SHIFT              6

/* PCIE_DLL :: DATA_LINK_ATTENTION :: DATA_LINK_LAYER_PACKET_TEST_INDICATOR [05:05] */
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_PACKET_TEST_INDICATOR_MASK 0x00000020
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_PACKET_TEST_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_PACKET_TEST_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_PACKET_TEST_INDICATOR_SHIFT 5

/* PCIE_DLL :: DATA_LINK_ATTENTION :: DATA_LINK_LAYER_ERROR_ATTENTION_INDICATOR [04:04] */
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_ERROR_ATTENTION_INDICATOR_MASK 0x00000010
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_ERROR_ATTENTION_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_ERROR_ATTENTION_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_DATA_LINK_LAYER_ERROR_ATTENTION_INDICATOR_SHIFT 4

/* PCIE_DLL :: DATA_LINK_ATTENTION :: NAK_RECEIVED_COUNTER_ATTENTION_INDICATOR [03:03] */
#define PCIE_DLL_DATA_LINK_ATTENTION_NAK_RECEIVED_COUNTER_ATTENTION_INDICATOR_MASK 0x00000008
#define PCIE_DLL_DATA_LINK_ATTENTION_NAK_RECEIVED_COUNTER_ATTENTION_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_NAK_RECEIVED_COUNTER_ATTENTION_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_NAK_RECEIVED_COUNTER_ATTENTION_INDICATOR_SHIFT 3

/* PCIE_DLL :: DATA_LINK_ATTENTION :: DLLP_ERROR_COUNTER_ATTENTION_INDICATOR [02:02] */
#define PCIE_DLL_DATA_LINK_ATTENTION_DLLP_ERROR_COUNTER_ATTENTION_INDICATOR_MASK 0x00000004
#define PCIE_DLL_DATA_LINK_ATTENTION_DLLP_ERROR_COUNTER_ATTENTION_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_DLLP_ERROR_COUNTER_ATTENTION_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_DLLP_ERROR_COUNTER_ATTENTION_INDICATOR_SHIFT 2

/* PCIE_DLL :: DATA_LINK_ATTENTION :: TLP_BAD_SEQUENCE_COUNTER_ATTENTION_INDICATOR [01:01] */
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_INDICATOR_MASK 0x00000002
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_INDICATOR_SHIFT 1

/* PCIE_DLL :: DATA_LINK_ATTENTION :: TLP_ERROR_COUNTER_ATTENTION_INDICATOR [00:00] */
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_ERROR_COUNTER_ATTENTION_INDICATOR_MASK 0x00000001
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_ERROR_COUNTER_ATTENTION_INDICATOR_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_ERROR_COUNTER_ATTENTION_INDICATOR_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_TLP_ERROR_COUNTER_ATTENTION_INDICATOR_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_ATTENTION_MASK
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: RESERVED_0 [31:08] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_RESERVED_0_MASK          0xffffff00
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_RESERVED_0_ALIGN         0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_RESERVED_0_BITS          24
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_RESERVED_0_SHIFT         8

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: UNUSED_0 [07:06] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_UNUSED_0_MASK            0x000000c0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_UNUSED_0_ALIGN           0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_UNUSED_0_BITS            2
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_UNUSED_0_SHIFT           6

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: DATA_LINK_LAYER_PACKET_TEST_ATTENTION_MASK [05:05] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_PACKET_TEST_ATTENTION_MASK_MASK 0x00000020
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_PACKET_TEST_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_PACKET_TEST_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_PACKET_TEST_ATTENTION_MASK_SHIFT 5

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: DATA_LINK_LAYER_ERROR_ATTENTION_MASK [04:04] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_ERROR_ATTENTION_MASK_MASK 0x00000010
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_ERROR_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_ERROR_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DATA_LINK_LAYER_ERROR_ATTENTION_MASK_SHIFT 4

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: NAK_RECEIVED_COUNTER_ATTENTION_MASK [03:03] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_NAK_RECEIVED_COUNTER_ATTENTION_MASK_MASK 0x00000008
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_NAK_RECEIVED_COUNTER_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_NAK_RECEIVED_COUNTER_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_NAK_RECEIVED_COUNTER_ATTENTION_MASK_SHIFT 3

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: DLLP_ERROR_COUNTER_ATTENTION_MASK [02:02] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DLLP_ERROR_COUNTER_ATTENTION_MASK_MASK 0x00000004
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DLLP_ERROR_COUNTER_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DLLP_ERROR_COUNTER_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_DLLP_ERROR_COUNTER_ATTENTION_MASK_SHIFT 2

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: TLP_BAD_SEQUENCE_COUNTER_ATTENTION_MASK [01:01] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_MASK_MASK 0x00000002
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_BAD_SEQUENCE_COUNTER_ATTENTION_MASK_SHIFT 1

/* PCIE_DLL :: DATA_LINK_ATTENTION_MASK :: TLP_ERROR_COUNTER_ATTENTION_MASK [00:00] */
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_ERROR_COUNTER_ATTENTION_MASK_MASK 0x00000001
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_ERROR_COUNTER_ATTENTION_MASK_ALIGN 0
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_ERROR_COUNTER_ATTENTION_MASK_BITS 1
#define PCIE_DLL_DATA_LINK_ATTENTION_MASK_TLP_ERROR_COUNTER_ATTENTION_MASK_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: RESERVED_0 [31:12] */
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_MASK 0xfffff000
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_ALIGN 0
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_BITS 20
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_SHIFT 12

/* PCIE_DLL :: NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: NEXT_TRANSMIT_SEQUENCE_NUMBER [11:00] */
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_NEXT_TRANSMIT_SEQUENCE_NUMBER_MASK 0x00000fff
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_NEXT_TRANSMIT_SEQUENCE_NUMBER_ALIGN 0
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_NEXT_TRANSMIT_SEQUENCE_NUMBER_BITS 12
#define PCIE_DLL_NEXT_TRANSMIT_SEQUENCE_NUMBER_DEBUG_NEXT_TRANSMIT_SEQUENCE_NUMBER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: RESERVED_0 [31:12] */
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_MASK 0xfffff000
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_ALIGN 0
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_BITS 20
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_SHIFT 12

/* PCIE_DLL :: ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: ACK_ED_TRANSMIT_SEQUENCE_NUMBER [11:00] */
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_MASK 0x00000fff
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_ALIGN 0
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_BITS 12
#define PCIE_DLL_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_ACK_ED_TRANSMIT_SEQUENCE_NUMBER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: RESERVED_0 [31:12] */
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_MASK 0xfffff000
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_ALIGN 0
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_BITS 20
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_RESERVED_0_SHIFT 12

/* PCIE_DLL :: PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG :: PURGED_TRANSMIT_SEQUENCE_NUMBER [11:00] */
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_PURGED_TRANSMIT_SEQUENCE_NUMBER_MASK 0x00000fff
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_PURGED_TRANSMIT_SEQUENCE_NUMBER_ALIGN 0
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_PURGED_TRANSMIT_SEQUENCE_NUMBER_BITS 12
#define PCIE_DLL_PURGED_TRANSMIT_SEQUENCE_NUMBER_DEBUG_PURGED_TRANSMIT_SEQUENCE_NUMBER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: RECEIVE_SEQUENCE_NUMBER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: RECEIVE_SEQUENCE_NUMBER_DEBUG :: RESERVED_0 [31:12] */
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RESERVED_0_MASK     0xfffff000
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RESERVED_0_ALIGN    0
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RESERVED_0_BITS     20
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RESERVED_0_SHIFT    12

/* PCIE_DLL :: RECEIVE_SEQUENCE_NUMBER_DEBUG :: RECEIVE_SEQUENCE_NUMBER [11:00] */
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RECEIVE_SEQUENCE_NUMBER_MASK 0x00000fff
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RECEIVE_SEQUENCE_NUMBER_ALIGN 0
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RECEIVE_SEQUENCE_NUMBER_BITS 12
#define PCIE_DLL_RECEIVE_SEQUENCE_NUMBER_DEBUG_RECEIVE_SEQUENCE_NUMBER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_REPLAY
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_REPLAY :: RESERVED_0 [31:23] */
#define PCIE_DLL_DATA_LINK_REPLAY_RESERVED_0_MASK                  0xff800000
#define PCIE_DLL_DATA_LINK_REPLAY_RESERVED_0_ALIGN                 0
#define PCIE_DLL_DATA_LINK_REPLAY_RESERVED_0_BITS                  9
#define PCIE_DLL_DATA_LINK_REPLAY_RESERVED_0_SHIFT                 23

/* PCIE_DLL :: DATA_LINK_REPLAY :: REPLAY_TIMEOUT_VALUE [22:10] */
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_TIMEOUT_VALUE_MASK        0x007ffc00
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_TIMEOUT_VALUE_ALIGN       0
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_TIMEOUT_VALUE_BITS        13
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_TIMEOUT_VALUE_SHIFT       10

/* PCIE_DLL :: DATA_LINK_REPLAY :: REPLAY_BUFFER_SIZE [09:00] */
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_BUFFER_SIZE_MASK          0x000003ff
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_BUFFER_SIZE_ALIGN         0
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_BUFFER_SIZE_BITS          10
#define PCIE_DLL_DATA_LINK_REPLAY_REPLAY_BUFFER_SIZE_SHIFT         0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_ACK_TIMEOUT
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_ACK_TIMEOUT :: RESERVED_0 [31:11] */
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_RESERVED_0_MASK             0xfffff800
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_RESERVED_0_ALIGN            0
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_RESERVED_0_BITS             21
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_RESERVED_0_SHIFT            11

/* PCIE_DLL :: DATA_LINK_ACK_TIMEOUT :: ACK_LATENCY_TIMEOUT_VALUE [10:00] */
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_ACK_LATENCY_TIMEOUT_VALUE_MASK 0x000007ff
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_ACK_LATENCY_TIMEOUT_VALUE_ALIGN 0
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_ACK_LATENCY_TIMEOUT_VALUE_BITS 11
#define PCIE_DLL_DATA_LINK_ACK_TIMEOUT_ACK_LATENCY_TIMEOUT_VALUE_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD
 ***************************************************************************/
/* PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD :: RESERVED_0 [31:24] */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_RESERVED_0_MASK        0xff000000
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_RESERVED_0_ALIGN       0
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_RESERVED_0_BITS        8
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_RESERVED_0_SHIFT       24

/* PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD :: L0_STAY_TIME [23:20] */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0_STAY_TIME_MASK      0x00f00000
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0_STAY_TIME_ALIGN     0
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0_STAY_TIME_BITS      4
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0_STAY_TIME_SHIFT     20

/* PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD :: L1_STAY_TIME [19:16] */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_STAY_TIME_MASK      0x000f0000
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_STAY_TIME_ALIGN     0
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_STAY_TIME_BITS      4
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_STAY_TIME_SHIFT     16

/* PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD :: L1_THRESHOLD [15:08] */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_THRESHOLD_MASK      0x0000ff00
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_THRESHOLD_ALIGN     0
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_THRESHOLD_BITS      8
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L1_THRESHOLD_SHIFT     8

/* PCIE_DLL :: POWER_MANAGEMENT_THRESHOLD :: L0S_THRESHOLD [07:00] */
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0S_THRESHOLD_MASK     0x000000ff
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0S_THRESHOLD_ALIGN    0
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0S_THRESHOLD_BITS     8
#define PCIE_DLL_POWER_MANAGEMENT_THRESHOLD_L0S_THRESHOLD_SHIFT    0


/****************************************************************************
 * PCIE_DLL :: RETRY_BUFFER_WRITE_POINTER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: RETRY_BUFFER_WRITE_POINTER_DEBUG :: RESERVED_0 [31:11] */
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RESERVED_0_MASK  0xfffff800
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RESERVED_0_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RESERVED_0_BITS  21
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RESERVED_0_SHIFT 11

/* PCIE_DLL :: RETRY_BUFFER_WRITE_POINTER_DEBUG :: RETRY_BUFFER_WRITE_POINTER [10:00] */
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RETRY_BUFFER_WRITE_POINTER_MASK 0x000007ff
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RETRY_BUFFER_WRITE_POINTER_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RETRY_BUFFER_WRITE_POINTER_BITS 11
#define PCIE_DLL_RETRY_BUFFER_WRITE_POINTER_DEBUG_RETRY_BUFFER_WRITE_POINTER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: RETRY_BUFFER_READ_POINTER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: RETRY_BUFFER_READ_POINTER_DEBUG :: RESERVED_0 [31:11] */
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RESERVED_0_MASK   0xfffff800
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RESERVED_0_ALIGN  0
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RESERVED_0_BITS   21
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RESERVED_0_SHIFT  11

/* PCIE_DLL :: RETRY_BUFFER_READ_POINTER_DEBUG :: RETRY_BUFFER_READ_POINTER [10:00] */
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RETRY_BUFFER_READ_POINTER_MASK 0x000007ff
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RETRY_BUFFER_READ_POINTER_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RETRY_BUFFER_READ_POINTER_BITS 11
#define PCIE_DLL_RETRY_BUFFER_READ_POINTER_DEBUG_RETRY_BUFFER_READ_POINTER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: RETRY_BUFFER_PURGED_POINTER_DEBUG
 ***************************************************************************/
/* PCIE_DLL :: RETRY_BUFFER_PURGED_POINTER_DEBUG :: RESERVED_0 [31:11] */
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RESERVED_0_MASK 0xfffff800
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RESERVED_0_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RESERVED_0_BITS 21
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RESERVED_0_SHIFT 11

/* PCIE_DLL :: RETRY_BUFFER_PURGED_POINTER_DEBUG :: RETRY_BUFFER_PURGED_POINTER [10:00] */
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RETRY_BUFFER_PURGED_POINTER_MASK 0x000007ff
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RETRY_BUFFER_PURGED_POINTER_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RETRY_BUFFER_PURGED_POINTER_BITS 11
#define PCIE_DLL_RETRY_BUFFER_PURGED_POINTER_DEBUG_RETRY_BUFFER_PURGED_POINTER_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: RETRY_BUFFER_READ_WRITE_DEBUG_PORT
 ***************************************************************************/
/* PCIE_DLL :: RETRY_BUFFER_READ_WRITE_DEBUG_PORT :: RETRY_BUFFER_DATA [31:00] */
#define PCIE_DLL_RETRY_BUFFER_READ_WRITE_DEBUG_PORT_RETRY_BUFFER_DATA_MASK 0xffffffff
#define PCIE_DLL_RETRY_BUFFER_READ_WRITE_DEBUG_PORT_RETRY_BUFFER_DATA_ALIGN 0
#define PCIE_DLL_RETRY_BUFFER_READ_WRITE_DEBUG_PORT_RETRY_BUFFER_DATA_BITS 32
#define PCIE_DLL_RETRY_BUFFER_READ_WRITE_DEBUG_PORT_RETRY_BUFFER_DATA_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: ERROR_COUNT_THRESHOLD
 ***************************************************************************/
/* PCIE_DLL :: ERROR_COUNT_THRESHOLD :: RESERVED_0 [31:15] */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_RESERVED_0_MASK             0xffff8000
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_RESERVED_0_ALIGN            0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_RESERVED_0_BITS             17
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_RESERVED_0_SHIFT            15

/* PCIE_DLL :: ERROR_COUNT_THRESHOLD :: BAD_SEQUENCE_NUMBER_COUNT_THRESHOLD [14:12] */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_BAD_SEQUENCE_NUMBER_COUNT_THRESHOLD_MASK 0x00007000
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_BAD_SEQUENCE_NUMBER_COUNT_THRESHOLD_ALIGN 0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_BAD_SEQUENCE_NUMBER_COUNT_THRESHOLD_BITS 3
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_BAD_SEQUENCE_NUMBER_COUNT_THRESHOLD_SHIFT 12

/* PCIE_DLL :: ERROR_COUNT_THRESHOLD :: NAK_RECEIVED_COUNT_THRESHOLD [11:08] */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_NAK_RECEIVED_COUNT_THRESHOLD_MASK 0x00000f00
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_NAK_RECEIVED_COUNT_THRESHOLD_ALIGN 0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_NAK_RECEIVED_COUNT_THRESHOLD_BITS 4
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_NAK_RECEIVED_COUNT_THRESHOLD_SHIFT 8

/* PCIE_DLL :: ERROR_COUNT_THRESHOLD :: DLLP_ERROR_COUNT_THRESHOLD [07:04] */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_DLLP_ERROR_COUNT_THRESHOLD_MASK 0x000000f0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_DLLP_ERROR_COUNT_THRESHOLD_ALIGN 0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_DLLP_ERROR_COUNT_THRESHOLD_BITS 4
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_DLLP_ERROR_COUNT_THRESHOLD_SHIFT 4

/* PCIE_DLL :: ERROR_COUNT_THRESHOLD :: TLP_ERROR_COUNT_THRESHOLD [03:00] */
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_TLP_ERROR_COUNT_THRESHOLD_MASK 0x0000000f
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_TLP_ERROR_COUNT_THRESHOLD_ALIGN 0
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_TLP_ERROR_COUNT_THRESHOLD_BITS 4
#define PCIE_DLL_ERROR_COUNT_THRESHOLD_TLP_ERROR_COUNT_THRESHOLD_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: TL_ERROR_COUNTER
 ***************************************************************************/
/* PCIE_DLL :: TL_ERROR_COUNTER :: RESERVED_0 [31:24] */
#define PCIE_DLL_TL_ERROR_COUNTER_RESERVED_0_MASK                  0xff000000
#define PCIE_DLL_TL_ERROR_COUNTER_RESERVED_0_ALIGN                 0
#define PCIE_DLL_TL_ERROR_COUNTER_RESERVED_0_BITS                  8
#define PCIE_DLL_TL_ERROR_COUNTER_RESERVED_0_SHIFT                 24

/* PCIE_DLL :: TL_ERROR_COUNTER :: TLP_BAD_SEQUENCE_NUMBER_COUNTER [23:16] */
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_BAD_SEQUENCE_NUMBER_COUNTER_MASK 0x00ff0000
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_BAD_SEQUENCE_NUMBER_COUNTER_ALIGN 0
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_BAD_SEQUENCE_NUMBER_COUNTER_BITS 8
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_BAD_SEQUENCE_NUMBER_COUNTER_SHIFT 16

/* PCIE_DLL :: TL_ERROR_COUNTER :: TLP_ERROR_COUNTER [15:00] */
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_ERROR_COUNTER_MASK           0x0000ffff
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_ERROR_COUNTER_ALIGN          0
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_ERROR_COUNTER_BITS           16
#define PCIE_DLL_TL_ERROR_COUNTER_TLP_ERROR_COUNTER_SHIFT          0


/****************************************************************************
 * PCIE_DLL :: DLLP_ERROR_COUNTER
 ***************************************************************************/
/* PCIE_DLL :: DLLP_ERROR_COUNTER :: RESERVED_0 [31:16] */
#define PCIE_DLL_DLLP_ERROR_COUNTER_RESERVED_0_MASK                0xffff0000
#define PCIE_DLL_DLLP_ERROR_COUNTER_RESERVED_0_ALIGN               0
#define PCIE_DLL_DLLP_ERROR_COUNTER_RESERVED_0_BITS                16
#define PCIE_DLL_DLLP_ERROR_COUNTER_RESERVED_0_SHIFT               16

/* PCIE_DLL :: DLLP_ERROR_COUNTER :: DLLP_ERROR_COUNTER [15:00] */
#define PCIE_DLL_DLLP_ERROR_COUNTER_DLLP_ERROR_COUNTER_MASK        0x0000ffff
#define PCIE_DLL_DLLP_ERROR_COUNTER_DLLP_ERROR_COUNTER_ALIGN       0
#define PCIE_DLL_DLLP_ERROR_COUNTER_DLLP_ERROR_COUNTER_BITS        16
#define PCIE_DLL_DLLP_ERROR_COUNTER_DLLP_ERROR_COUNTER_SHIFT       0


/****************************************************************************
 * PCIE_DLL :: NAK_RECEIVED_COUNTER
 ***************************************************************************/
/* PCIE_DLL :: NAK_RECEIVED_COUNTER :: RESERVED_0 [31:16] */
#define PCIE_DLL_NAK_RECEIVED_COUNTER_RESERVED_0_MASK              0xffff0000
#define PCIE_DLL_NAK_RECEIVED_COUNTER_RESERVED_0_ALIGN             0
#define PCIE_DLL_NAK_RECEIVED_COUNTER_RESERVED_0_BITS              16
#define PCIE_DLL_NAK_RECEIVED_COUNTER_RESERVED_0_SHIFT             16

/* PCIE_DLL :: NAK_RECEIVED_COUNTER :: NAK_RECEIVED_COUNTER [15:00] */
#define PCIE_DLL_NAK_RECEIVED_COUNTER_NAK_RECEIVED_COUNTER_MASK    0x0000ffff
#define PCIE_DLL_NAK_RECEIVED_COUNTER_NAK_RECEIVED_COUNTER_ALIGN   0
#define PCIE_DLL_NAK_RECEIVED_COUNTER_NAK_RECEIVED_COUNTER_BITS    16
#define PCIE_DLL_NAK_RECEIVED_COUNTER_NAK_RECEIVED_COUNTER_SHIFT   0


/****************************************************************************
 * PCIE_DLL :: DATA_LINK_TEST
 ***************************************************************************/
/* PCIE_DLL :: DATA_LINK_TEST :: RESERVED_0 [31:16] */
#define PCIE_DLL_DATA_LINK_TEST_RESERVED_0_MASK                    0xffff0000
#define PCIE_DLL_DATA_LINK_TEST_RESERVED_0_ALIGN                   0
#define PCIE_DLL_DATA_LINK_TEST_RESERVED_0_BITS                    16
#define PCIE_DLL_DATA_LINK_TEST_RESERVED_0_SHIFT                   16

/* PCIE_DLL :: DATA_LINK_TEST :: STORE_RECEIVE_TLPS [15:15] */
#define PCIE_DLL_DATA_LINK_TEST_STORE_RECEIVE_TLPS_MASK            0x00008000
#define PCIE_DLL_DATA_LINK_TEST_STORE_RECEIVE_TLPS_ALIGN           0
#define PCIE_DLL_DATA_LINK_TEST_STORE_RECEIVE_TLPS_BITS            1
#define PCIE_DLL_DATA_LINK_TEST_STORE_RECEIVE_TLPS_SHIFT           15

/* PCIE_DLL :: DATA_LINK_TEST :: DISABLE_TLPS [14:14] */
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_TLPS_MASK                  0x00004000
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_TLPS_ALIGN                 0
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_TLPS_BITS                  1
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_TLPS_SHIFT                 14

/* PCIE_DLL :: DATA_LINK_TEST :: DISABLE_DLLPS [13:13] */
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_DLLPS_MASK                 0x00002000
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_DLLPS_ALIGN                0
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_DLLPS_BITS                 1
#define PCIE_DLL_DATA_LINK_TEST_DISABLE_DLLPS_SHIFT                13

/* PCIE_DLL :: DATA_LINK_TEST :: FORCE_PHY_LINK_UP [12:12] */
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PHY_LINK_UP_MASK             0x00001000
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PHY_LINK_UP_ALIGN            0
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PHY_LINK_UP_BITS             1
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PHY_LINK_UP_SHIFT            12

/* PCIE_DLL :: DATA_LINK_TEST :: BYPASS_FLOW_CONTROL [11:11] */
#define PCIE_DLL_DATA_LINK_TEST_BYPASS_FLOW_CONTROL_MASK           0x00000800
#define PCIE_DLL_DATA_LINK_TEST_BYPASS_FLOW_CONTROL_ALIGN          0
#define PCIE_DLL_DATA_LINK_TEST_BYPASS_FLOW_CONTROL_BITS           1
#define PCIE_DLL_DATA_LINK_TEST_BYPASS_FLOW_CONTROL_SHIFT          11

/* PCIE_DLL :: DATA_LINK_TEST :: ENABLE_RAM_CORE_CLOCK_MARGIN_TEST_MODE [10:10] */
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_CORE_CLOCK_MARGIN_TEST_MODE_MASK 0x00000400
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_CORE_CLOCK_MARGIN_TEST_MODE_ALIGN 0
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_CORE_CLOCK_MARGIN_TEST_MODE_BITS 1
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_CORE_CLOCK_MARGIN_TEST_MODE_SHIFT 10

/* PCIE_DLL :: DATA_LINK_TEST :: ENABLE_RAM_OVERSTRESS_TEST_MODE [09:09] */
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_OVERSTRESS_TEST_MODE_MASK 0x00000200
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_OVERSTRESS_TEST_MODE_ALIGN 0
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_OVERSTRESS_TEST_MODE_BITS 1
#define PCIE_DLL_DATA_LINK_TEST_ENABLE_RAM_OVERSTRESS_TEST_MODE_SHIFT 9

/* PCIE_DLL :: DATA_LINK_TEST :: SPEED_UP_SLOW_CLOCK [08:08] */
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_SLOW_CLOCK_MASK           0x00000100
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_SLOW_CLOCK_ALIGN          0
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_SLOW_CLOCK_BITS           1
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_SLOW_CLOCK_SHIFT          8

/* PCIE_DLL :: DATA_LINK_TEST :: SPEED_UP_COMPLETION_TIMER [07:07] */
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_COMPLETION_TIMER_MASK     0x00000080
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_COMPLETION_TIMER_ALIGN    0
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_COMPLETION_TIMER_BITS     1
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_COMPLETION_TIMER_SHIFT    7

/* PCIE_DLL :: DATA_LINK_TEST :: SPEED_UP_REPLAY_TIMER [06:06] */
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_REPLAY_TIMER_MASK         0x00000040
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_REPLAY_TIMER_ALIGN        0
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_REPLAY_TIMER_BITS         1
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_REPLAY_TIMER_SHIFT        6

/* PCIE_DLL :: DATA_LINK_TEST :: SPEED_UP_ACK_LATENCY_TIMER [05:05] */
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_ACK_LATENCY_TIMER_MASK    0x00000020
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_ACK_LATENCY_TIMER_ALIGN   0
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_ACK_LATENCY_TIMER_BITS    1
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_ACK_LATENCY_TIMER_SHIFT   5

/* PCIE_DLL :: DATA_LINK_TEST :: SPEED_UP_PME_SERVICE_TIMER [04:04] */
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_PME_SERVICE_TIMER_MASK    0x00000010
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_PME_SERVICE_TIMER_ALIGN   0
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_PME_SERVICE_TIMER_BITS    1
#define PCIE_DLL_DATA_LINK_TEST_SPEED_UP_PME_SERVICE_TIMER_SHIFT   4

/* PCIE_DLL :: DATA_LINK_TEST :: FORCE_PURGE [03:03] */
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PURGE_MASK                   0x00000008
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PURGE_ALIGN                  0
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PURGE_BITS                   1
#define PCIE_DLL_DATA_LINK_TEST_FORCE_PURGE_SHIFT                  3

/* PCIE_DLL :: DATA_LINK_TEST :: FORCE_RETRY [02:02] */
#define PCIE_DLL_DATA_LINK_TEST_FORCE_RETRY_MASK                   0x00000004
#define PCIE_DLL_DATA_LINK_TEST_FORCE_RETRY_ALIGN                  0
#define PCIE_DLL_DATA_LINK_TEST_FORCE_RETRY_BITS                   1
#define PCIE_DLL_DATA_LINK_TEST_FORCE_RETRY_SHIFT                  2

/* PCIE_DLL :: DATA_LINK_TEST :: INVERT_CRC [01:01] */
#define PCIE_DLL_DATA_LINK_TEST_INVERT_CRC_MASK                    0x00000002
#define PCIE_DLL_DATA_LINK_TEST_INVERT_CRC_ALIGN                   0
#define PCIE_DLL_DATA_LINK_TEST_INVERT_CRC_BITS                    1
#define PCIE_DLL_DATA_LINK_TEST_INVERT_CRC_SHIFT                   1

/* PCIE_DLL :: DATA_LINK_TEST :: SEND_BAD_CRC_BIT [00:00] */
#define PCIE_DLL_DATA_LINK_TEST_SEND_BAD_CRC_BIT_MASK              0x00000001
#define PCIE_DLL_DATA_LINK_TEST_SEND_BAD_CRC_BIT_ALIGN             0
#define PCIE_DLL_DATA_LINK_TEST_SEND_BAD_CRC_BIT_BITS              1
#define PCIE_DLL_DATA_LINK_TEST_SEND_BAD_CRC_BIT_SHIFT             0


/****************************************************************************
 * PCIE_DLL :: PACKET_BIST
 ***************************************************************************/
/* PCIE_DLL :: PACKET_BIST :: RESERVED_0 [31:24] */
#define PCIE_DLL_PACKET_BIST_RESERVED_0_MASK                       0xff000000
#define PCIE_DLL_PACKET_BIST_RESERVED_0_ALIGN                      0
#define PCIE_DLL_PACKET_BIST_RESERVED_0_BITS                       8
#define PCIE_DLL_PACKET_BIST_RESERVED_0_SHIFT                      24

/* PCIE_DLL :: PACKET_BIST :: PACKET_CHECKER_LOCKED [23:23] */
#define PCIE_DLL_PACKET_BIST_PACKET_CHECKER_LOCKED_MASK            0x00800000
#define PCIE_DLL_PACKET_BIST_PACKET_CHECKER_LOCKED_ALIGN           0
#define PCIE_DLL_PACKET_BIST_PACKET_CHECKER_LOCKED_BITS            1
#define PCIE_DLL_PACKET_BIST_PACKET_CHECKER_LOCKED_SHIFT           23

/* PCIE_DLL :: PACKET_BIST :: RECEIVE_MISMATCH [22:22] */
#define PCIE_DLL_PACKET_BIST_RECEIVE_MISMATCH_MASK                 0x00400000
#define PCIE_DLL_PACKET_BIST_RECEIVE_MISMATCH_ALIGN                0
#define PCIE_DLL_PACKET_BIST_RECEIVE_MISMATCH_BITS                 1
#define PCIE_DLL_PACKET_BIST_RECEIVE_MISMATCH_SHIFT                22

/* PCIE_DLL :: PACKET_BIST :: ENABLE_RANDOM_TLP_LENGTH [21:21] */
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_TLP_LENGTH_MASK         0x00200000
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_TLP_LENGTH_ALIGN        0
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_TLP_LENGTH_BITS         1
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_TLP_LENGTH_SHIFT        21

/* PCIE_DLL :: PACKET_BIST :: TLP_LENGTH [20:10] */
#define PCIE_DLL_PACKET_BIST_TLP_LENGTH_MASK                       0x001ffc00
#define PCIE_DLL_PACKET_BIST_TLP_LENGTH_ALIGN                      0
#define PCIE_DLL_PACKET_BIST_TLP_LENGTH_BITS                       11
#define PCIE_DLL_PACKET_BIST_TLP_LENGTH_SHIFT                      10

/* PCIE_DLL :: PACKET_BIST :: ENABLE_RANDOM_IPG_LENGTH [09:09] */
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_IPG_LENGTH_MASK         0x00000200
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_IPG_LENGTH_ALIGN        0
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_IPG_LENGTH_BITS         1
#define PCIE_DLL_PACKET_BIST_ENABLE_RANDOM_IPG_LENGTH_SHIFT        9

/* PCIE_DLL :: PACKET_BIST :: IPG_LENGTH [08:02] */
#define PCIE_DLL_PACKET_BIST_IPG_LENGTH_MASK                       0x000001fc
#define PCIE_DLL_PACKET_BIST_IPG_LENGTH_ALIGN                      0
#define PCIE_DLL_PACKET_BIST_IPG_LENGTH_BITS                       7
#define PCIE_DLL_PACKET_BIST_IPG_LENGTH_SHIFT                      2

/* PCIE_DLL :: PACKET_BIST :: TRANSMIT_START [01:01] */
#define PCIE_DLL_PACKET_BIST_TRANSMIT_START_MASK                   0x00000002
#define PCIE_DLL_PACKET_BIST_TRANSMIT_START_ALIGN                  0
#define PCIE_DLL_PACKET_BIST_TRANSMIT_START_BITS                   1
#define PCIE_DLL_PACKET_BIST_TRANSMIT_START_SHIFT                  1

/* PCIE_DLL :: PACKET_BIST :: ENABLE_PACKET_GENERATOR_TEST_MODE [00:00] */
#define PCIE_DLL_PACKET_BIST_ENABLE_PACKET_GENERATOR_TEST_MODE_MASK 0x00000001
#define PCIE_DLL_PACKET_BIST_ENABLE_PACKET_GENERATOR_TEST_MODE_ALIGN 0
#define PCIE_DLL_PACKET_BIST_ENABLE_PACKET_GENERATOR_TEST_MODE_BITS 1
#define PCIE_DLL_PACKET_BIST_ENABLE_PACKET_GENERATOR_TEST_MODE_SHIFT 0


/****************************************************************************
 * PCIE_DLL :: LINK_PCIE_1_1_CONTROL
 ***************************************************************************/
/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: RTBF_CT_2_0 [31:29] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_CT_2_0_MASK            0xe0000000
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_CT_2_0_ALIGN           0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_CT_2_0_BITS            3
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_CT_2_0_SHIFT           29

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: RTBF_SAM_1_0 [28:27] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_SAM_1_0_MASK           0x18000000
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_SAM_1_0_ALIGN          0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_SAM_1_0_BITS           2
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_RTBF_SAM_1_0_SHIFT          27

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: UNUSED_0 [26:10] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_UNUSED_0_MASK               0x07fffc00
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_UNUSED_0_ALIGN              0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_UNUSED_0_BITS               17
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_UNUSED_0_SHIFT              10

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: SELOCALXTAL [09:09] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_SELOCALXTAL_MASK            0x00000200
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_SELOCALXTAL_ALIGN           0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_SELOCALXTAL_BITS            1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_SELOCALXTAL_SHIFT           9

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L2_PLL_POWERDOWN_DISABLE [08:08] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_PLL_POWERDOWN_DISABLE_MASK 0x00000100
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_PLL_POWERDOWN_DISABLE_ALIGN 0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_PLL_POWERDOWN_DISABLE_BITS 1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_PLL_POWERDOWN_DISABLE_SHIFT 8

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L1_PLL_POWERDOWN_DISABLE [07:07] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_POWERDOWN_DISABLE_MASK 0x00000080
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_POWERDOWN_DISABLE_ALIGN 0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_POWERDOWN_DISABLE_BITS 1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_POWERDOWN_DISABLE_SHIFT 7

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L2_D3PM_CLKREQ_DISABLE [06:06] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_D3PM_CLKREQ_DISABLE_MASK 0x00000040
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_D3PM_CLKREQ_DISABLE_ALIGN 0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_D3PM_CLKREQ_DISABLE_BITS 1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L2_D3PM_CLKREQ_DISABLE_SHIFT 6

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L1_D3PM_CLKREQ_DISABLE [05:05] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_D3PM_CLKREQ_DISABLE_MASK 0x00000020
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_D3PM_CLKREQ_DISABLE_ALIGN 0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_D3PM_CLKREQ_DISABLE_BITS 1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_D3PM_CLKREQ_DISABLE_SHIFT 5

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L1_ASPM_CLKREQ_DISABLE [04:04] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_ASPM_CLKREQ_DISABLE_MASK 0x00000010
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_ASPM_CLKREQ_DISABLE_ALIGN 0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_ASPM_CLKREQ_DISABLE_BITS 1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_ASPM_CLKREQ_DISABLE_SHIFT 4

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: L1_PLL_PD_W_O_CLKREQ [03:03] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_PD_W_O_CLKREQ_MASK   0x00000008
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_PD_W_O_CLKREQ_ALIGN  0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_PD_W_O_CLKREQ_BITS   1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_L1_PLL_PD_W_O_CLKREQ_SHIFT  3

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: DASPM10USTIMER [02:02] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DASPM10USTIMER_MASK         0x00000004
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DASPM10USTIMER_ALIGN        0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DASPM10USTIMER_BITS         1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DASPM10USTIMER_SHIFT        2

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: DFFU_EL1 [01:01] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFFU_EL1_MASK               0x00000002
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFFU_EL1_ALIGN              0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFFU_EL1_BITS               1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFFU_EL1_SHIFT              1

/* PCIE_DLL :: LINK_PCIE_1_1_CONTROL :: DFLOWCTLUPDATE1_1 [00:00] */
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFLOWCTLUPDATE1_1_MASK      0x00000001
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFLOWCTLUPDATE1_1_ALIGN     0
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFLOWCTLUPDATE1_1_BITS      1
#define PCIE_DLL_LINK_PCIE_1_1_CONTROL_DFLOWCTLUPDATE1_1_SHIFT     0


/****************************************************************************
 * BCM70012_TGT_TOP_PCIE_PHY
 ***************************************************************************/
/****************************************************************************
 * PCIE_PHY :: PHY_MODE
 ***************************************************************************/
/* PCIE_PHY :: PHY_MODE :: RESERVED_0 [31:04] */
#define PCIE_PHY_PHY_MODE_RESERVED_0_MASK                          0xfffffff0
#define PCIE_PHY_PHY_MODE_RESERVED_0_ALIGN                         0
#define PCIE_PHY_PHY_MODE_RESERVED_0_BITS                          28
#define PCIE_PHY_PHY_MODE_RESERVED_0_SHIFT                         4

/* PCIE_PHY :: PHY_MODE :: UPSTREAM_DEV [03:03] */
#define PCIE_PHY_PHY_MODE_UPSTREAM_DEV_MASK                        0x00000008
#define PCIE_PHY_PHY_MODE_UPSTREAM_DEV_ALIGN                       0
#define PCIE_PHY_PHY_MODE_UPSTREAM_DEV_BITS                        1
#define PCIE_PHY_PHY_MODE_UPSTREAM_DEV_SHIFT                       3

/* PCIE_PHY :: PHY_MODE :: SERDES_SA_MODE [02:02] */
#define PCIE_PHY_PHY_MODE_SERDES_SA_MODE_MASK                      0x00000004
#define PCIE_PHY_PHY_MODE_SERDES_SA_MODE_ALIGN                     0
#define PCIE_PHY_PHY_MODE_SERDES_SA_MODE_BITS                      1
#define PCIE_PHY_PHY_MODE_SERDES_SA_MODE_SHIFT                     2

/* PCIE_PHY :: PHY_MODE :: LINK_DISABLE [01:01] */
#define PCIE_PHY_PHY_MODE_LINK_DISABLE_MASK                        0x00000002
#define PCIE_PHY_PHY_MODE_LINK_DISABLE_ALIGN                       0
#define PCIE_PHY_PHY_MODE_LINK_DISABLE_BITS                        1
#define PCIE_PHY_PHY_MODE_LINK_DISABLE_SHIFT                       1

/* PCIE_PHY :: PHY_MODE :: SOFT_RESET [00:00] */
#define PCIE_PHY_PHY_MODE_SOFT_RESET_MASK                          0x00000001
#define PCIE_PHY_PHY_MODE_SOFT_RESET_ALIGN                         0
#define PCIE_PHY_PHY_MODE_SOFT_RESET_BITS                          1
#define PCIE_PHY_PHY_MODE_SOFT_RESET_SHIFT                         0


/****************************************************************************
 * PCIE_PHY :: PHY_LINK_STATUS
 ***************************************************************************/
/* PCIE_PHY :: PHY_LINK_STATUS :: RESERVED_0 [31:10] */
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_0_MASK                   0xfffffc00
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_0_ALIGN                  0
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_0_BITS                   22
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_0_SHIFT                  10

/* PCIE_PHY :: PHY_LINK_STATUS :: BUFFER_OVERRUN [09:09] */
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_OVERRUN_MASK               0x00000200
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_OVERRUN_ALIGN              0
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_OVERRUN_BITS               1
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_OVERRUN_SHIFT              9

/* PCIE_PHY :: PHY_LINK_STATUS :: BUFFER_UNDERRUN [08:08] */
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_UNDERRUN_MASK              0x00000100
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_UNDERRUN_ALIGN             0
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_UNDERRUN_BITS              1
#define PCIE_PHY_PHY_LINK_STATUS_BUFFER_UNDERRUN_SHIFT             8

/* PCIE_PHY :: PHY_LINK_STATUS :: LINK_PARTNER_REQUEST_LOOPBACK [07:07] */
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_REQUEST_LOOPBACK_MASK 0x00000080
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_REQUEST_LOOPBACK_ALIGN 0
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_REQUEST_LOOPBACK_BITS 1
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_REQUEST_LOOPBACK_SHIFT 7

/* PCIE_PHY :: PHY_LINK_STATUS :: LINK_PARTNER_DISABLE_SCRAMBLER [06:06] */
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_DISABLE_SCRAMBLER_MASK 0x00000040
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_DISABLE_SCRAMBLER_ALIGN 0
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_DISABLE_SCRAMBLER_BITS 1
#define PCIE_PHY_PHY_LINK_STATUS_LINK_PARTNER_DISABLE_SCRAMBLER_SHIFT 6

/* PCIE_PHY :: PHY_LINK_STATUS :: EXTENDED_SYNCH [05:05] */
#define PCIE_PHY_PHY_LINK_STATUS_EXTENDED_SYNCH_MASK               0x00000020
#define PCIE_PHY_PHY_LINK_STATUS_EXTENDED_SYNCH_ALIGN              0
#define PCIE_PHY_PHY_LINK_STATUS_EXTENDED_SYNCH_BITS               1
#define PCIE_PHY_PHY_LINK_STATUS_EXTENDED_SYNCH_SHIFT              5

/* PCIE_PHY :: PHY_LINK_STATUS :: POLARITY_INVERTED [04:04] */
#define PCIE_PHY_PHY_LINK_STATUS_POLARITY_INVERTED_MASK            0x00000010
#define PCIE_PHY_PHY_LINK_STATUS_POLARITY_INVERTED_ALIGN           0
#define PCIE_PHY_PHY_LINK_STATUS_POLARITY_INVERTED_BITS            1
#define PCIE_PHY_PHY_LINK_STATUS_POLARITY_INVERTED_SHIFT           4

/* PCIE_PHY :: PHY_LINK_STATUS :: LINK_UP [03:03] */
#define PCIE_PHY_PHY_LINK_STATUS_LINK_UP_MASK                      0x00000008
#define PCIE_PHY_PHY_LINK_STATUS_LINK_UP_ALIGN                     0
#define PCIE_PHY_PHY_LINK_STATUS_LINK_UP_BITS                      1
#define PCIE_PHY_PHY_LINK_STATUS_LINK_UP_SHIFT                     3

/* PCIE_PHY :: PHY_LINK_STATUS :: LINK_TRAINING [02:02] */
#define PCIE_PHY_PHY_LINK_STATUS_LINK_TRAINING_MASK                0x00000004
#define PCIE_PHY_PHY_LINK_STATUS_LINK_TRAINING_ALIGN               0
#define PCIE_PHY_PHY_LINK_STATUS_LINK_TRAINING_BITS                1
#define PCIE_PHY_PHY_LINK_STATUS_LINK_TRAINING_SHIFT               2

/* PCIE_PHY :: PHY_LINK_STATUS :: RECEIVE_DATA_VALID [01:01] */
#define PCIE_PHY_PHY_LINK_STATUS_RECEIVE_DATA_VALID_MASK           0x00000002
#define PCIE_PHY_PHY_LINK_STATUS_RECEIVE_DATA_VALID_ALIGN          0
#define PCIE_PHY_PHY_LINK_STATUS_RECEIVE_DATA_VALID_BITS           1
#define PCIE_PHY_PHY_LINK_STATUS_RECEIVE_DATA_VALID_SHIFT          1

/* PCIE_PHY :: PHY_LINK_STATUS :: RESERVED_1 [00:00] */
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_1_MASK                   0x00000001
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_1_ALIGN                  0
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_1_BITS                   1
#define PCIE_PHY_PHY_LINK_STATUS_RESERVED_1_SHIFT                  0


/****************************************************************************
 * PCIE_PHY :: PHY_LINK_LTSSM_CONTROL
 ***************************************************************************/
/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: RESERVED_0 [31:08] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESERVED_0_MASK            0xffffff00
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESERVED_0_ALIGN           0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESERVED_0_BITS            24
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESERVED_0_SHIFT           8

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: DISABLESCRAMBLE [07:07] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESCRAMBLE_MASK       0x00000080
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESCRAMBLE_ALIGN      0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESCRAMBLE_BITS       1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESCRAMBLE_SHIFT      7

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: DETECTSTATE [06:06] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DETECTSTATE_MASK           0x00000040
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DETECTSTATE_ALIGN          0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DETECTSTATE_BITS           1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DETECTSTATE_SHIFT          6

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: POLLINGSTATE [05:05] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_POLLINGSTATE_MASK          0x00000020
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_POLLINGSTATE_ALIGN         0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_POLLINGSTATE_BITS          1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_POLLINGSTATE_SHIFT         5

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: CONFIGSTATE [04:04] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_CONFIGSTATE_MASK           0x00000010
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_CONFIGSTATE_ALIGN          0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_CONFIGSTATE_BITS           1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_CONFIGSTATE_SHIFT          4

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: RECOVSTATE [03:03] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RECOVSTATE_MASK            0x00000008
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RECOVSTATE_ALIGN           0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RECOVSTATE_BITS            1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RECOVSTATE_SHIFT           3

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: EXTLBSTATE [02:02] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_EXTLBSTATE_MASK            0x00000004
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_EXTLBSTATE_ALIGN           0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_EXTLBSTATE_BITS            1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_EXTLBSTATE_SHIFT           2

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: RESETSTATE [01:01] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESETSTATE_MASK            0x00000002
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESETSTATE_ALIGN           0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESETSTATE_BITS            1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_RESETSTATE_SHIFT           1

/* PCIE_PHY :: PHY_LINK_LTSSM_CONTROL :: DISABLESTATE [00:00] */
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESTATE_MASK          0x00000001
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESTATE_ALIGN         0
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESTATE_BITS          1
#define PCIE_PHY_PHY_LINK_LTSSM_CONTROL_DISABLESTATE_SHIFT         0


/****************************************************************************
 * PCIE_PHY :: PHY_LINK_TRAINING_LINK_NUMBER
 ***************************************************************************/
/* PCIE_PHY :: PHY_LINK_TRAINING_LINK_NUMBER :: RESERVED_0 [31:08] */
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_RESERVED_0_MASK     0xffffff00
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_RESERVED_0_ALIGN    0
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_RESERVED_0_BITS     24
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_RESERVED_0_SHIFT    8

/* PCIE_PHY :: PHY_LINK_TRAINING_LINK_NUMBER :: LANE_NUMBER [07:00] */
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_LANE_NUMBER_MASK    0x000000ff
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_LANE_NUMBER_ALIGN   0
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_LANE_NUMBER_BITS    8
#define PCIE_PHY_PHY_LINK_TRAINING_LINK_NUMBER_LANE_NUMBER_SHIFT   0


/****************************************************************************
 * PCIE_PHY :: PHY_LINK_TRAINING_LANE_NUMBER
 ***************************************************************************/
/* PCIE_PHY :: PHY_LINK_TRAINING_LANE_NUMBER :: RESERVED_0 [31:08] */
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_RESERVED_0_MASK     0xffffff00
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_RESERVED_0_ALIGN    0
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_RESERVED_0_BITS     24
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_RESERVED_0_SHIFT    8

/* PCIE_PHY :: PHY_LINK_TRAINING_LANE_NUMBER :: LANE_NUMBER [07:00] */
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_LANE_NUMBER_MASK    0x000000ff
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_LANE_NUMBER_ALIGN   0
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_LANE_NUMBER_BITS    8
#define PCIE_PHY_PHY_LINK_TRAINING_LANE_NUMBER_LANE_NUMBER_SHIFT   0


/****************************************************************************
 * PCIE_PHY :: PHY_LINK_TRAINING_N_FTS
 ***************************************************************************/
/* PCIE_PHY :: PHY_LINK_TRAINING_N_FTS :: RESERVED_0 [31:25] */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RESERVED_0_MASK           0xfe000000
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RESERVED_0_ALIGN          0
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RESERVED_0_BITS           7
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RESERVED_0_SHIFT          25

/* PCIE_PHY :: PHY_LINK_TRAINING_N_FTS :: TRANSMITTER_N_FTS_OVERRIDE [24:24] */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_MASK 0x01000000
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_BITS 1
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_SHIFT 24

/* PCIE_PHY :: PHY_LINK_TRAINING_N_FTS :: TRANSMITTER_N_FTS_OVERRIDE_VALUE [23:16] */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_VALUE_MASK 0x00ff0000
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_VALUE_ALIGN 0
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_VALUE_BITS 8
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_OVERRIDE_VALUE_SHIFT 16

/* PCIE_PHY :: PHY_LINK_TRAINING_N_FTS :: TRANSMITTER_N_FTS [15:08] */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_MASK    0x0000ff00
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_ALIGN   0
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_BITS    8
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_TRANSMITTER_N_FTS_SHIFT   8

/* PCIE_PHY :: PHY_LINK_TRAINING_N_FTS :: RECEIVER_N_FTS [07:00] */
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RECEIVER_N_FTS_MASK       0x000000ff
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RECEIVER_N_FTS_ALIGN      0
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RECEIVER_N_FTS_BITS       8
#define PCIE_PHY_PHY_LINK_TRAINING_N_FTS_RECEIVER_N_FTS_SHIFT      0


/****************************************************************************
 * PCIE_PHY :: PHY_ATTENTION
 ***************************************************************************/
/* PCIE_PHY :: PHY_ATTENTION :: RESERVED_0 [31:08] */
#define PCIE_PHY_PHY_ATTENTION_RESERVED_0_MASK                     0xffffff00
#define PCIE_PHY_PHY_ATTENTION_RESERVED_0_ALIGN                    0
#define PCIE_PHY_PHY_ATTENTION_RESERVED_0_BITS                     24
#define PCIE_PHY_PHY_ATTENTION_RESERVED_0_SHIFT                    8

/* PCIE_PHY :: PHY_ATTENTION :: HOT_RESET [07:07] */
#define PCIE_PHY_PHY_ATTENTION_HOT_RESET_MASK                      0x00000080
#define PCIE_PHY_PHY_ATTENTION_HOT_RESET_ALIGN                     0
#define PCIE_PHY_PHY_ATTENTION_HOT_RESET_BITS                      1
#define PCIE_PHY_PHY_ATTENTION_HOT_RESET_SHIFT                     7

/* PCIE_PHY :: PHY_ATTENTION :: LINK_DOWN [06:06] */
#define PCIE_PHY_PHY_ATTENTION_LINK_DOWN_MASK                      0x00000040
#define PCIE_PHY_PHY_ATTENTION_LINK_DOWN_ALIGN                     0
#define PCIE_PHY_PHY_ATTENTION_LINK_DOWN_BITS                      1
#define PCIE_PHY_PHY_ATTENTION_LINK_DOWN_SHIFT                     6

/* PCIE_PHY :: PHY_ATTENTION :: TRAINING_ERROR [05:05] */
#define PCIE_PHY_PHY_ATTENTION_TRAINING_ERROR_MASK                 0x00000020
#define PCIE_PHY_PHY_ATTENTION_TRAINING_ERROR_ALIGN                0
#define PCIE_PHY_PHY_ATTENTION_TRAINING_ERROR_BITS                 1
#define PCIE_PHY_PHY_ATTENTION_TRAINING_ERROR_SHIFT                5

/* PCIE_PHY :: PHY_ATTENTION :: BUFFER_OVERRUN [04:04] */
#define PCIE_PHY_PHY_ATTENTION_BUFFER_OVERRUN_MASK                 0x00000010
#define PCIE_PHY_PHY_ATTENTION_BUFFER_OVERRUN_ALIGN                0
#define PCIE_PHY_PHY_ATTENTION_BUFFER_OVERRUN_BITS                 1
#define PCIE_PHY_PHY_ATTENTION_BUFFER_OVERRUN_SHIFT                4

/* PCIE_PHY :: PHY_ATTENTION :: BUFFER_UNDERRUN [03:03] */
#define PCIE_PHY_PHY_ATTENTION_BUFFER_UNDERRUN_MASK                0x00000008
#define PCIE_PHY_PHY_ATTENTION_BUFFER_UNDERRUN_ALIGN               0
#define PCIE_PHY_PHY_ATTENTION_BUFFER_UNDERRUN_BITS                1
#define PCIE_PHY_PHY_ATTENTION_BUFFER_UNDERRUN_SHIFT               3

/* PCIE_PHY :: PHY_ATTENTION :: RECEIVE_FRAMING_ERROR [02:02] */
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_FRAMING_ERROR_MASK          0x00000004
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_FRAMING_ERROR_ALIGN         0
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_FRAMING_ERROR_BITS          1
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_FRAMING_ERROR_SHIFT         2

/* PCIE_PHY :: PHY_ATTENTION :: RECEIVE_DISPARITY_ERROR [01:01] */
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_DISPARITY_ERROR_MASK        0x00000002
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_DISPARITY_ERROR_ALIGN       0
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_DISPARITY_ERROR_BITS        1
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_DISPARITY_ERROR_SHIFT       1

/* PCIE_PHY :: PHY_ATTENTION :: RECEIVE_CODE_ERROR [00:00] */
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_CODE_ERROR_MASK             0x00000001
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_CODE_ERROR_ALIGN            0
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_CODE_ERROR_BITS             1
#define PCIE_PHY_PHY_ATTENTION_RECEIVE_CODE_ERROR_SHIFT            0


/****************************************************************************
 * PCIE_PHY :: PHY_ATTENTION_MASK
 ***************************************************************************/
/* PCIE_PHY :: PHY_ATTENTION_MASK :: RESERVED_0 [31:08] */
#define PCIE_PHY_PHY_ATTENTION_MASK_RESERVED_0_MASK                0xffffff00
#define PCIE_PHY_PHY_ATTENTION_MASK_RESERVED_0_ALIGN               0
#define PCIE_PHY_PHY_ATTENTION_MASK_RESERVED_0_BITS                24
#define PCIE_PHY_PHY_ATTENTION_MASK_RESERVED_0_SHIFT               8

/* PCIE_PHY :: PHY_ATTENTION_MASK :: HOT_RESET_MASK [07:07] */
#define PCIE_PHY_PHY_ATTENTION_MASK_HOT_RESET_MASK_MASK            0x00000080
#define PCIE_PHY_PHY_ATTENTION_MASK_HOT_RESET_MASK_ALIGN           0
#define PCIE_PHY_PHY_ATTENTION_MASK_HOT_RESET_MASK_BITS            1
#define PCIE_PHY_PHY_ATTENTION_MASK_HOT_RESET_MASK_SHIFT           7

/* PCIE_PHY :: PHY_ATTENTION_MASK :: LINK_DOWN_MASK [06:06] */
#define PCIE_PHY_PHY_ATTENTION_MASK_LINK_DOWN_MASK_MASK            0x00000040
#define PCIE_PHY_PHY_ATTENTION_MASK_LINK_DOWN_MASK_ALIGN           0
#define PCIE_PHY_PHY_ATTENTION_MASK_LINK_DOWN_MASK_BITS            1
#define PCIE_PHY_PHY_ATTENTION_MASK_LINK_DOWN_MASK_SHIFT           6

/* PCIE_PHY :: PHY_ATTENTION_MASK :: TRAINING_ERROR_MASK [05:05] */
#define PCIE_PHY_PHY_ATTENTION_MASK_TRAINING_ERROR_MASK_MASK       0x00000020
#define PCIE_PHY_PHY_ATTENTION_MASK_TRAINING_ERROR_MASK_ALIGN      0
#define PCIE_PHY_PHY_ATTENTION_MASK_TRAINING_ERROR_MASK_BITS       1
#define PCIE_PHY_PHY_ATTENTION_MASK_TRAINING_ERROR_MASK_SHIFT      5

/* PCIE_PHY :: PHY_ATTENTION_MASK :: BUFFER_OVERRUN_MASK [04:04] */
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_OVERRUN_MASK_MASK       0x00000010
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_OVERRUN_MASK_ALIGN      0
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_OVERRUN_MASK_BITS       1
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_OVERRUN_MASK_SHIFT      4

/* PCIE_PHY :: PHY_ATTENTION_MASK :: BUFFER_UNDERRUN_MASK [03:03] */
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_UNDERRUN_MASK_MASK      0x00000008
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_UNDERRUN_MASK_ALIGN     0
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_UNDERRUN_MASK_BITS      1
#define PCIE_PHY_PHY_ATTENTION_MASK_BUFFER_UNDERRUN_MASK_SHIFT     3

/* PCIE_PHY :: PHY_ATTENTION_MASK :: RECEIVE_FRAME_ERROR_MASK [02:02] */
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_FRAME_ERROR_MASK_MASK  0x00000004
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_FRAME_ERROR_MASK_ALIGN 0
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_FRAME_ERROR_MASK_BITS  1
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_FRAME_ERROR_MASK_SHIFT 2

/* PCIE_PHY :: PHY_ATTENTION_MASK :: RECEIVE_DISPARITY_ERROR_MASK [01:01] */
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_DISPARITY_ERROR_MASK_MASK 0x00000002
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_DISPARITY_ERROR_MASK_ALIGN 0
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_DISPARITY_ERROR_MASK_BITS 1
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_DISPARITY_ERROR_MASK_SHIFT 1

/* PCIE_PHY :: PHY_ATTENTION_MASK :: RECEIVE_CODE_ERROR_MASK [00:00] */
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_CODE_ERROR_MASK_MASK   0x00000001
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_CODE_ERROR_MASK_ALIGN  0
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_CODE_ERROR_MASK_BITS   1
#define PCIE_PHY_PHY_ATTENTION_MASK_RECEIVE_CODE_ERROR_MASK_SHIFT  0


/****************************************************************************
 * PCIE_PHY :: PHY_RECEIVE_ERROR_COUNTER
 ***************************************************************************/
/* PCIE_PHY :: PHY_RECEIVE_ERROR_COUNTER :: DISPARITY_ERROR_COUNT [31:16] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_DISPARITY_ERROR_COUNT_MASK 0xffff0000
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_DISPARITY_ERROR_COUNT_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_DISPARITY_ERROR_COUNT_BITS 16
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_DISPARITY_ERROR_COUNT_SHIFT 16

/* PCIE_PHY :: PHY_RECEIVE_ERROR_COUNTER :: CODE_ERROR_COUNT [15:00] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_CODE_ERROR_COUNT_MASK   0x0000ffff
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_CODE_ERROR_COUNT_ALIGN  0
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_CODE_ERROR_COUNT_BITS   16
#define PCIE_PHY_PHY_RECEIVE_ERROR_COUNTER_CODE_ERROR_COUNT_SHIFT  0


/****************************************************************************
 * PCIE_PHY :: PHY_RECEIVE_FRAMING_ERROR_COUNTER
 ***************************************************************************/
/* PCIE_PHY :: PHY_RECEIVE_FRAMING_ERROR_COUNTER :: RESERVED_0 [31:16] */
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_RESERVED_0_MASK 0xffff0000
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_RESERVED_0_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_RESERVED_0_BITS 16
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_RESERVED_0_SHIFT 16

/* PCIE_PHY :: PHY_RECEIVE_FRAMING_ERROR_COUNTER :: FRAMING_ERROR_COUNT [15:00] */
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_FRAMING_ERROR_COUNT_MASK 0x0000ffff
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_FRAMING_ERROR_COUNT_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_FRAMING_ERROR_COUNT_BITS 16
#define PCIE_PHY_PHY_RECEIVE_FRAMING_ERROR_COUNTER_FRAMING_ERROR_COUNT_SHIFT 0


/****************************************************************************
 * PCIE_PHY :: PHY_RECEIVE_ERROR_THRESHOLD
 ***************************************************************************/
/* PCIE_PHY :: PHY_RECEIVE_ERROR_THRESHOLD :: RESERVED_0 [31:12] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_RESERVED_0_MASK       0xfffff000
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_RESERVED_0_ALIGN      0
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_RESERVED_0_BITS       20
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_RESERVED_0_SHIFT      12

/* PCIE_PHY :: PHY_RECEIVE_ERROR_THRESHOLD :: FRAME_ERROR_THRESHOLD [11:08] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_FRAME_ERROR_THRESHOLD_MASK 0x00000f00
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_FRAME_ERROR_THRESHOLD_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_FRAME_ERROR_THRESHOLD_BITS 4
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_FRAME_ERROR_THRESHOLD_SHIFT 8

/* PCIE_PHY :: PHY_RECEIVE_ERROR_THRESHOLD :: DISPARITY_ERROR_THRESHOLD [07:04] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_DISPARITY_ERROR_THRESHOLD_MASK 0x000000f0
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_DISPARITY_ERROR_THRESHOLD_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_DISPARITY_ERROR_THRESHOLD_BITS 4
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_DISPARITY_ERROR_THRESHOLD_SHIFT 4

/* PCIE_PHY :: PHY_RECEIVE_ERROR_THRESHOLD :: CODE_ERROR_THRESHOLD [03:00] */
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_CODE_ERROR_THRESHOLD_MASK 0x0000000f
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_CODE_ERROR_THRESHOLD_ALIGN 0
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_CODE_ERROR_THRESHOLD_BITS 4
#define PCIE_PHY_PHY_RECEIVE_ERROR_THRESHOLD_CODE_ERROR_THRESHOLD_SHIFT 0


/****************************************************************************
 * PCIE_PHY :: PHY_TEST_CONTROL
 ***************************************************************************/
/* PCIE_PHY :: PHY_TEST_CONTROL :: UNUSED_0 [31:31] */
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_0_MASK                    0x80000000
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_0_ALIGN                   0
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_0_BITS                    1
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_0_SHIFT                   31

/* PCIE_PHY :: PHY_TEST_CONTROL :: DELAY_HOTRESET_ENABLE [30:30] */
#define PCIE_PHY_PHY_TEST_CONTROL_DELAY_HOTRESET_ENABLE_MASK       0x40000000
#define PCIE_PHY_PHY_TEST_CONTROL_DELAY_HOTRESET_ENABLE_ALIGN      0
#define PCIE_PHY_PHY_TEST_CONTROL_DELAY_HOTRESET_ENABLE_BITS       1
#define PCIE_PHY_PHY_TEST_CONTROL_DELAY_HOTRESET_ENABLE_SHIFT      30

/* PCIE_PHY :: PHY_TEST_CONTROL :: CQ27039_TSX_MAJORITY_CHECK [29:29] */
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_TSX_MAJORITY_CHECK_MASK  0x20000000
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_TSX_MAJORITY_CHECK_ALIGN 0
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_TSX_MAJORITY_CHECK_BITS  1
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_TSX_MAJORITY_CHECK_SHIFT 29

/* PCIE_PHY :: PHY_TEST_CONTROL :: CQ27039_MASK_OFF_BOGUS [28:28] */
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_MASK_OFF_BOGUS_MASK      0x10000000
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_MASK_OFF_BOGUS_ALIGN     0
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_MASK_OFF_BOGUS_BITS      1
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_MASK_OFF_BOGUS_SHIFT     28

/* PCIE_PHY :: PHY_TEST_CONTROL :: CQ27039_POLARITY_CHECK [27:27] */
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_POLARITY_CHECK_MASK      0x08000000
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_POLARITY_CHECK_ALIGN     0
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_POLARITY_CHECK_BITS      1
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_POLARITY_CHECK_SHIFT     27

/* PCIE_PHY :: PHY_TEST_CONTROL :: CQ27039_STICKY_POLARITY_CHECK [26:26] */
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_STICKY_POLARITY_CHECK_MASK 0x04000000
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_STICKY_POLARITY_CHECK_ALIGN 0
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_STICKY_POLARITY_CHECK_BITS 1
#define PCIE_PHY_PHY_TEST_CONTROL_CQ27039_STICKY_POLARITY_CHECK_SHIFT 26

/* PCIE_PHY :: PHY_TEST_CONTROL :: UNUSED_1 [25:23] */
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_1_MASK                    0x03800000
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_1_ALIGN                   0
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_1_BITS                    3
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_1_SHIFT                   23

/* PCIE_PHY :: PHY_TEST_CONTROL :: TWO_OS_RULE_RELAXING [22:22] */
#define PCIE_PHY_PHY_TEST_CONTROL_TWO_OS_RULE_RELAXING_MASK        0x00400000
#define PCIE_PHY_PHY_TEST_CONTROL_TWO_OS_RULE_RELAXING_ALIGN       0
#define PCIE_PHY_PHY_TEST_CONTROL_TWO_OS_RULE_RELAXING_BITS        1
#define PCIE_PHY_PHY_TEST_CONTROL_TWO_OS_RULE_RELAXING_SHIFT       22

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_HOT_RESET [21:21] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_HOT_RESET_MASK           0x00200000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_HOT_RESET_ALIGN          0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_HOT_RESET_BITS           1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_HOT_RESET_SHIFT          21

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_LINK_DOWN_RESET [20:20] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_LINK_DOWN_RESET_MASK     0x00100000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_LINK_DOWN_RESET_ALIGN    0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_LINK_DOWN_RESET_BITS     1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_LINK_DOWN_RESET_SHIFT    20

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_EIDLE_SET_TRANSMITTING_AT_TIME_OUT_TO_DETECT_STATE [19:19] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_EIDLE_SET_TRANSMITTING_AT_TIME_OUT_TO_DETECT_STATE_MASK 0x00080000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_EIDLE_SET_TRANSMITTING_AT_TIME_OUT_TO_DETECT_STATE_ALIGN 0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_EIDLE_SET_TRANSMITTING_AT_TIME_OUT_TO_DETECT_STATE_BITS 1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_EIDLE_SET_TRANSMITTING_AT_TIME_OUT_TO_DETECT_STATE_SHIFT 19

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_ERROR_EXIT [18:18] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_EXIT_MASK          0x00040000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_EXIT_ALIGN         0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_EXIT_BITS          1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_EXIT_SHIFT         18

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_ERROR_MASK [17:17] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_MASK_MASK          0x00020000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_MASK_ALIGN         0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_MASK_BITS          1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_MASK_SHIFT         17

/* PCIE_PHY :: PHY_TEST_CONTROL :: DISABLE_ERROR_RECOVERY [16:16] */
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_RECOVERY_MASK      0x00010000
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_RECOVERY_ALIGN     0
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_RECOVERY_BITS      1
#define PCIE_PHY_PHY_TEST_CONTROL_DISABLE_ERROR_RECOVERY_SHIFT     16

/* PCIE_PHY :: PHY_TEST_CONTROL :: RESERVED_0 [15:08] */
#define PCIE_PHY_PHY_TEST_CONTROL_RESERVED_0_MASK                  0x0000ff00
#define PCIE_PHY_PHY_TEST_CONTROL_RESERVED_0_ALIGN                 0
#define PCIE_PHY_PHY_TEST_CONTROL_RESERVED_0_BITS                  8
#define PCIE_PHY_PHY_TEST_CONTROL_RESERVED_0_SHIFT                 8

/* PCIE_PHY :: PHY_TEST_CONTROL :: UNUSED_2 [07:04] */
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_2_MASK                    0x000000f0
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_2_ALIGN                   0
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_2_BITS                    4
#define PCIE_PHY_PHY_TEST_CONTROL_UNUSED_2_SHIFT                   4

/* PCIE_PHY :: PHY_TEST_CONTROL :: CQ22100_FIX_DISABLE [03:03] */
#define PCIE_PHY_PHY_TEST_CONTROL_CQ22100_FIX_DISABLE_MASK         0x00000008
#define PCIE_PHY_PHY_TEST_CONTROL_CQ22100_FIX_DISABLE_ALIGN        0
#define PCIE_PHY_PHY_TEST_CONTROL_CQ22100_FIX_DISABLE_BITS         1
#define PCIE_PHY_PHY_TEST_CONTROL_CQ22100_FIX_DISABLE_SHIFT        3

/* PCIE_PHY :: PHY_TEST_CONTROL :: TRAINING_BYPASS [02:02] */
#define PCIE_PHY_PHY_TEST_CONTROL_TRAINING_BYPASS_MASK             0x00000004
#define PCIE_PHY_PHY_TEST_CONTROL_TRAINING_BYPASS_ALIGN            0
#define PCIE_PHY_PHY_TEST_CONTROL_TRAINING_BYPASS_BITS             1
#define PCIE_PHY_PHY_TEST_CONTROL_TRAINING_BYPASS_SHIFT            2

/* PCIE_PHY :: PHY_TEST_CONTROL :: EXTERNAL_LOOPBACK [01:01] */
#define PCIE_PHY_PHY_TEST_CONTROL_EXTERNAL_LOOPBACK_MASK           0x00000002
#define PCIE_PHY_PHY_TEST_CONTROL_EXTERNAL_LOOPBACK_ALIGN          0
#define PCIE_PHY_PHY_TEST_CONTROL_EXTERNAL_LOOPBACK_BITS           1
#define PCIE_PHY_PHY_TEST_CONTROL_EXTERNAL_LOOPBACK_SHIFT          1

/* PCIE_PHY :: PHY_TEST_CONTROL :: INTERNAL_LOOPBACK [00:00] */
#define PCIE_PHY_PHY_TEST_CONTROL_INTERNAL_LOOPBACK_MASK           0x00000001
#define PCIE_PHY_PHY_TEST_CONTROL_INTERNAL_LOOPBACK_ALIGN          0
#define PCIE_PHY_PHY_TEST_CONTROL_INTERNAL_LOOPBACK_BITS           1
#define PCIE_PHY_PHY_TEST_CONTROL_INTERNAL_LOOPBACK_SHIFT          0


/****************************************************************************
 * PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE
 ***************************************************************************/
/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: RESERVED_0 [31:18] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RESERVED_0_MASK       0xfffc0000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RESERVED_0_ALIGN      0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RESERVED_0_BITS       14
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RESERVED_0_SHIFT      18

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: OBSVELECIDLEVALUE [17:17] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEVALUE_MASK 0x00020000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEVALUE_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEVALUE_BITS 1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEVALUE_SHIFT 17

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: OBSVELECIDLEOVERRIDE [16:16] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEOVERRIDE_MASK 0x00010000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEOVERRIDE_BITS 1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_OBSVELECIDLEOVERRIDE_SHIFT 16

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: PLLISUPVALUE [15:15] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPVALUE_MASK     0x00008000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPVALUE_ALIGN    0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPVALUE_BITS     1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPVALUE_SHIFT    15

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: PLLISUPOVERRIDE [14:14] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPOVERRIDE_MASK  0x00004000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPOVERRIDE_BITS  1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_PLLISUPOVERRIDE_SHIFT 14

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: RCVRDETVALUE [13:13] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETVALUE_MASK     0x00002000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETVALUE_ALIGN    0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETVALUE_BITS     1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETVALUE_SHIFT    13

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: RCVRDETOVERRIDE [12:12] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETOVERRIDE_MASK  0x00001000
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETOVERRIDE_BITS  1
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETOVERRIDE_SHIFT 12

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: RCVRDETTIMECONTROL [11:10] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETTIMECONTROL_MASK 0x00000c00
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETTIMECONTROL_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETTIMECONTROL_BITS 2
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETTIMECONTROL_SHIFT 10

/* PCIE_PHY :: PHY_SERDES_CONTROL_OVERRIDE :: RCVRDETECTIONTIME [09:00] */
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETECTIONTIME_MASK 0x000003ff
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETECTIONTIME_ALIGN 0
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETECTIONTIME_BITS 10
#define PCIE_PHY_PHY_SERDES_CONTROL_OVERRIDE_RCVRDETECTIONTIME_SHIFT 0


/****************************************************************************
 * PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE
 ***************************************************************************/
/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: TS1NUMOVERRIDE [31:31] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TS1NUMOVERRIDE_MASK 0x80000000
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TS1NUMOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TS1NUMOVERRIDE_BITS 1
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TS1NUMOVERRIDE_SHIFT 31

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: TXIDLEMINOVERRIDE [30:30] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINOVERRIDE_MASK 0x40000000
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINOVERRIDE_BITS 1
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINOVERRIDE_SHIFT 30

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: TXIDLE2IDLEOVERRIDE [29:29] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLE2IDLEOVERRIDE_MASK 0x20000000
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLE2IDLEOVERRIDE_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLE2IDLEOVERRIDE_BITS 1
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLE2IDLEOVERRIDE_SHIFT 29

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: UNUSED_0 [28:28] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_UNUSED_0_MASK       0x10000000
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_UNUSED_0_ALIGN      0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_UNUSED_0_BITS       1
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_UNUSED_0_SHIFT      28

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: N_TS1INPOLLINGACTIVE [27:16] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_N_TS1INPOLLINGACTIVE_MASK 0x0fff0000
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_N_TS1INPOLLINGACTIVE_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_N_TS1INPOLLINGACTIVE_BITS 12
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_N_TS1INPOLLINGACTIVE_SHIFT 16

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: TXIDLEMINTIME [15:08] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINTIME_MASK  0x0000ff00
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINTIME_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINTIME_BITS  8
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLEMINTIME_SHIFT 8

/* PCIE_PHY :: PHY_TIMING_PARAMETER_OVERRIDE :: TXIDLESETTOIDLETIME [07:00] */
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLESETTOIDLETIME_MASK 0x000000ff
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLESETTOIDLETIME_ALIGN 0
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLESETTOIDLETIME_BITS 8
#define PCIE_PHY_PHY_TIMING_PARAMETER_OVERRIDE_TXIDLESETTOIDLETIME_SHIFT 0


/****************************************************************************
 * PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES
 ***************************************************************************/
/* PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES :: RESERVED_0 [31:10] */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RESERVED_0_MASK 0xfffffc00
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RESERVED_0_ALIGN 0
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RESERVED_0_BITS 22
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RESERVED_0_SHIFT 10

/* PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES :: TRANSMIT_STATE_MACHINE_STATE [09:04] */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_TRANSMIT_STATE_MACHINE_STATE_MASK 0x000003f0
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_TRANSMIT_STATE_MACHINE_STATE_ALIGN 0
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_TRANSMIT_STATE_MACHINE_STATE_BITS 6
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_TRANSMIT_STATE_MACHINE_STATE_SHIFT 4

/* PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES :: RECEIVE_STATE_MACHINE_STATE [03:00] */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RECEIVE_STATE_MACHINE_STATE_MASK 0x0000000f
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RECEIVE_STATE_MACHINE_STATE_ALIGN 0
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RECEIVE_STATE_MACHINE_STATE_BITS 4
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC1_TX_RX_SM_STATES_RECEIVE_STATE_MACHINE_STATE_SHIFT 0


/****************************************************************************
 * PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES
 ***************************************************************************/
/* PCIE_PHY :: PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES :: LTSSM_STATE_MACHINE_STATE [31:00] */
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES_LTSSM_STATE_MACHINE_STATE_MASK 0xffffffff
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES_LTSSM_STATE_MACHINE_STATE_ALIGN 0
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES_LTSSM_STATE_MACHINE_STATE_BITS 32
#define PCIE_PHY_PHY_HARDWARE_DIAGNOSTIC2_LTSSM_STATES_LTSSM_STATE_MACHINE_STATE_SHIFT 0


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
 * INTR :: INTR_SET
 ***************************************************************************/
/* INTR :: INTR_SET :: reserved0 [31:26] */
#define INTR_INTR_SET_reserved0_MASK                               0xfc000000
#define INTR_INTR_SET_reserved0_ALIGN                              0
#define INTR_INTR_SET_reserved0_BITS                               6
#define INTR_INTR_SET_reserved0_SHIFT                              26

/* INTR :: INTR_SET :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_SET_PCIE_TGT_CA_ATTN_MASK                        0x02000000
#define INTR_INTR_SET_PCIE_TGT_CA_ATTN_ALIGN                       0
#define INTR_INTR_SET_PCIE_TGT_CA_ATTN_BITS                        1
#define INTR_INTR_SET_PCIE_TGT_CA_ATTN_SHIFT                       25

/* INTR :: INTR_SET :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_SET_PCIE_TGT_UR_ATTN_MASK                        0x01000000
#define INTR_INTR_SET_PCIE_TGT_UR_ATTN_ALIGN                       0
#define INTR_INTR_SET_PCIE_TGT_UR_ATTN_BITS                        1
#define INTR_INTR_SET_PCIE_TGT_UR_ATTN_SHIFT                       24

/* INTR :: INTR_SET :: reserved1 [23:14] */
#define INTR_INTR_SET_reserved1_MASK                               0x00ffc000
#define INTR_INTR_SET_reserved1_ALIGN                              0
#define INTR_INTR_SET_reserved1_BITS                               10
#define INTR_INTR_SET_reserved1_SHIFT                              14

/* INTR :: INTR_SET :: UV_RX_DMA_L1_ERR_INTR [13:13] */
#define INTR_INTR_SET_UV_RX_DMA_L1_ERR_INTR_MASK                   0x00002000
#define INTR_INTR_SET_UV_RX_DMA_L1_ERR_INTR_ALIGN                  0
#define INTR_INTR_SET_UV_RX_DMA_L1_ERR_INTR_BITS                   1
#define INTR_INTR_SET_UV_RX_DMA_L1_ERR_INTR_SHIFT                  13

/* INTR :: INTR_SET :: UV_RX_DMA_L1_DONE_INTR [12:12] */
#define INTR_INTR_SET_UV_RX_DMA_L1_DONE_INTR_MASK                  0x00001000
#define INTR_INTR_SET_UV_RX_DMA_L1_DONE_INTR_ALIGN                 0
#define INTR_INTR_SET_UV_RX_DMA_L1_DONE_INTR_BITS                  1
#define INTR_INTR_SET_UV_RX_DMA_L1_DONE_INTR_SHIFT                 12

/* INTR :: INTR_SET :: Y_RX_DMA_L1_ERR_INTR [11:11] */
#define INTR_INTR_SET_Y_RX_DMA_L1_ERR_INTR_MASK                    0x00000800
#define INTR_INTR_SET_Y_RX_DMA_L1_ERR_INTR_ALIGN                   0
#define INTR_INTR_SET_Y_RX_DMA_L1_ERR_INTR_BITS                    1
#define INTR_INTR_SET_Y_RX_DMA_L1_ERR_INTR_SHIFT                   11

/* INTR :: INTR_SET :: Y_RX_DMA_L1_DONE_INTR [10:10] */
#define INTR_INTR_SET_Y_RX_DMA_L1_DONE_INTR_MASK                   0x00000400
#define INTR_INTR_SET_Y_RX_DMA_L1_DONE_INTR_ALIGN                  0
#define INTR_INTR_SET_Y_RX_DMA_L1_DONE_INTR_BITS                   1
#define INTR_INTR_SET_Y_RX_DMA_L1_DONE_INTR_SHIFT                  10

/* INTR :: INTR_SET :: TX_DMA_L1_ERR_INTR [09:09] */
#define INTR_INTR_SET_TX_DMA_L1_ERR_INTR_MASK                      0x00000200
#define INTR_INTR_SET_TX_DMA_L1_ERR_INTR_ALIGN                     0
#define INTR_INTR_SET_TX_DMA_L1_ERR_INTR_BITS                      1
#define INTR_INTR_SET_TX_DMA_L1_ERR_INTR_SHIFT                     9

/* INTR :: INTR_SET :: TX_DMA_L1_DONE_INTR [08:08] */
#define INTR_INTR_SET_TX_DMA_L1_DONE_INTR_MASK                     0x00000100
#define INTR_INTR_SET_TX_DMA_L1_DONE_INTR_ALIGN                    0
#define INTR_INTR_SET_TX_DMA_L1_DONE_INTR_BITS                     1
#define INTR_INTR_SET_TX_DMA_L1_DONE_INTR_SHIFT                    8

/* INTR :: INTR_SET :: reserved2 [07:06] */
#define INTR_INTR_SET_reserved2_MASK                               0x000000c0
#define INTR_INTR_SET_reserved2_ALIGN                              0
#define INTR_INTR_SET_reserved2_BITS                               2
#define INTR_INTR_SET_reserved2_SHIFT                              6

/* INTR :: INTR_SET :: UV_RX_DMA_L0_ERR_INTR [05:05] */
#define INTR_INTR_SET_UV_RX_DMA_L0_ERR_INTR_MASK                   0x00000020
#define INTR_INTR_SET_UV_RX_DMA_L0_ERR_INTR_ALIGN                  0
#define INTR_INTR_SET_UV_RX_DMA_L0_ERR_INTR_BITS                   1
#define INTR_INTR_SET_UV_RX_DMA_L0_ERR_INTR_SHIFT                  5

/* INTR :: INTR_SET :: UV_RX_DMA_L0_DONE_INTR [04:04] */
#define INTR_INTR_SET_UV_RX_DMA_L0_DONE_INTR_MASK                  0x00000010
#define INTR_INTR_SET_UV_RX_DMA_L0_DONE_INTR_ALIGN                 0
#define INTR_INTR_SET_UV_RX_DMA_L0_DONE_INTR_BITS                  1
#define INTR_INTR_SET_UV_RX_DMA_L0_DONE_INTR_SHIFT                 4

/* INTR :: INTR_SET :: Y_RX_DMA_L0_ERR_INTR [03:03] */
#define INTR_INTR_SET_Y_RX_DMA_L0_ERR_INTR_MASK                    0x00000008
#define INTR_INTR_SET_Y_RX_DMA_L0_ERR_INTR_ALIGN                   0
#define INTR_INTR_SET_Y_RX_DMA_L0_ERR_INTR_BITS                    1
#define INTR_INTR_SET_Y_RX_DMA_L0_ERR_INTR_SHIFT                   3

/* INTR :: INTR_SET :: Y_RX_DMA_L0_DONE_INTR [02:02] */
#define INTR_INTR_SET_Y_RX_DMA_L0_DONE_INTR_MASK                   0x00000004
#define INTR_INTR_SET_Y_RX_DMA_L0_DONE_INTR_ALIGN                  0
#define INTR_INTR_SET_Y_RX_DMA_L0_DONE_INTR_BITS                   1
#define INTR_INTR_SET_Y_RX_DMA_L0_DONE_INTR_SHIFT                  2

/* INTR :: INTR_SET :: TX_DMA_L0_ERR_INTR [01:01] */
#define INTR_INTR_SET_TX_DMA_L0_ERR_INTR_MASK                      0x00000002
#define INTR_INTR_SET_TX_DMA_L0_ERR_INTR_ALIGN                     0
#define INTR_INTR_SET_TX_DMA_L0_ERR_INTR_BITS                      1
#define INTR_INTR_SET_TX_DMA_L0_ERR_INTR_SHIFT                     1

/* INTR :: INTR_SET :: TX_DMA_L0_DONE_INTR [00:00] */
#define INTR_INTR_SET_TX_DMA_L0_DONE_INTR_MASK                     0x00000001
#define INTR_INTR_SET_TX_DMA_L0_DONE_INTR_ALIGN                    0
#define INTR_INTR_SET_TX_DMA_L0_DONE_INTR_BITS                     1
#define INTR_INTR_SET_TX_DMA_L0_DONE_INTR_SHIFT                    0


/****************************************************************************
 * INTR :: INTR_CLR_REG
 ***************************************************************************/
/* INTR :: INTR_CLR_REG :: reserved0 [31:26] */
#define INTR_INTR_CLR_REG_reserved0_MASK                           0xfc000000
#define INTR_INTR_CLR_REG_reserved0_ALIGN                          0
#define INTR_INTR_CLR_REG_reserved0_BITS                           6
#define INTR_INTR_CLR_REG_reserved0_SHIFT                          26

/* INTR :: INTR_CLR_REG :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_CLR_REG_PCIE_TGT_CA_ATTN_MASK                    0x02000000
#define INTR_INTR_CLR_REG_PCIE_TGT_CA_ATTN_ALIGN                   0
#define INTR_INTR_CLR_REG_PCIE_TGT_CA_ATTN_BITS                    1
#define INTR_INTR_CLR_REG_PCIE_TGT_CA_ATTN_SHIFT                   25

/* INTR :: INTR_CLR_REG :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_CLR_REG_PCIE_TGT_UR_ATTN_MASK                    0x01000000
#define INTR_INTR_CLR_REG_PCIE_TGT_UR_ATTN_ALIGN                   0
#define INTR_INTR_CLR_REG_PCIE_TGT_UR_ATTN_BITS                    1
#define INTR_INTR_CLR_REG_PCIE_TGT_UR_ATTN_SHIFT                   24

/* INTR :: INTR_CLR_REG :: reserved1 [23:14] */
#define INTR_INTR_CLR_REG_reserved1_MASK                           0x00ffc000
#define INTR_INTR_CLR_REG_reserved1_ALIGN                          0
#define INTR_INTR_CLR_REG_reserved1_BITS                           10
#define INTR_INTR_CLR_REG_reserved1_SHIFT                          14

/* INTR :: INTR_CLR_REG :: L1_UV_RX_DMA_ERR_INTR_CLR [13:13] */
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_ERR_INTR_CLR_MASK           0x00002000
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_ERR_INTR_CLR_ALIGN          0
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_ERR_INTR_CLR_BITS           1
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_ERR_INTR_CLR_SHIFT          13

/* INTR :: INTR_CLR_REG :: L1_UV_RX_DMA_DONE_INTR_CLR [12:12] */
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_DONE_INTR_CLR_MASK          0x00001000
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_DONE_INTR_CLR_ALIGN         0
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_DONE_INTR_CLR_BITS          1
#define INTR_INTR_CLR_REG_L1_UV_RX_DMA_DONE_INTR_CLR_SHIFT         12

/* INTR :: INTR_CLR_REG :: L1_Y_RX_DMA_ERR_INTR_CLR [11:11] */
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_ERR_INTR_CLR_MASK            0x00000800
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_ERR_INTR_CLR_ALIGN           0
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_ERR_INTR_CLR_BITS            1
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_ERR_INTR_CLR_SHIFT           11

/* INTR :: INTR_CLR_REG :: L1_Y_RX_DMA_DONE_INTR_CLR [10:10] */
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_DONE_INTR_CLR_MASK           0x00000400
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_DONE_INTR_CLR_ALIGN          0
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_DONE_INTR_CLR_BITS           1
#define INTR_INTR_CLR_REG_L1_Y_RX_DMA_DONE_INTR_CLR_SHIFT          10

/* INTR :: INTR_CLR_REG :: L1_TX_DMA_ERR_INTR_CLR [09:09] */
#define INTR_INTR_CLR_REG_L1_TX_DMA_ERR_INTR_CLR_MASK              0x00000200
#define INTR_INTR_CLR_REG_L1_TX_DMA_ERR_INTR_CLR_ALIGN             0
#define INTR_INTR_CLR_REG_L1_TX_DMA_ERR_INTR_CLR_BITS              1
#define INTR_INTR_CLR_REG_L1_TX_DMA_ERR_INTR_CLR_SHIFT             9

/* INTR :: INTR_CLR_REG :: L1_TX_DMA_DONE_INTR_CLR [08:08] */
#define INTR_INTR_CLR_REG_L1_TX_DMA_DONE_INTR_CLR_MASK             0x00000100
#define INTR_INTR_CLR_REG_L1_TX_DMA_DONE_INTR_CLR_ALIGN            0
#define INTR_INTR_CLR_REG_L1_TX_DMA_DONE_INTR_CLR_BITS             1
#define INTR_INTR_CLR_REG_L1_TX_DMA_DONE_INTR_CLR_SHIFT            8

/* INTR :: INTR_CLR_REG :: reserved2 [07:06] */
#define INTR_INTR_CLR_REG_reserved2_MASK                           0x000000c0
#define INTR_INTR_CLR_REG_reserved2_ALIGN                          0
#define INTR_INTR_CLR_REG_reserved2_BITS                           2
#define INTR_INTR_CLR_REG_reserved2_SHIFT                          6

/* INTR :: INTR_CLR_REG :: L0_UV_RX_DMA_ERR_INTR_CLR [05:05] */
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_ERR_INTR_CLR_MASK           0x00000020
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_ERR_INTR_CLR_ALIGN          0
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_ERR_INTR_CLR_BITS           1
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_ERR_INTR_CLR_SHIFT          5

/* INTR :: INTR_CLR_REG :: L0_UV_RX_DMA_DONE_INTR_CLR [04:04] */
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_DONE_INTR_CLR_MASK          0x00000010
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_DONE_INTR_CLR_ALIGN         0
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_DONE_INTR_CLR_BITS          1
#define INTR_INTR_CLR_REG_L0_UV_RX_DMA_DONE_INTR_CLR_SHIFT         4

/* INTR :: INTR_CLR_REG :: L0_Y_RX_DMA_ERR_INTR_CLR [03:03] */
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_ERR_INTR_CLR_MASK            0x00000008
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_ERR_INTR_CLR_ALIGN           0
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_ERR_INTR_CLR_BITS            1
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_ERR_INTR_CLR_SHIFT           3

/* INTR :: INTR_CLR_REG :: L0_Y_RX_DMA_DONE_INTR_CLR [02:02] */
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_DONE_INTR_CLR_MASK           0x00000004
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_DONE_INTR_CLR_ALIGN          0
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_DONE_INTR_CLR_BITS           1
#define INTR_INTR_CLR_REG_L0_Y_RX_DMA_DONE_INTR_CLR_SHIFT          2

/* INTR :: INTR_CLR_REG :: L0_TX_DMA_ERR_INTR_CLR [01:01] */
#define INTR_INTR_CLR_REG_L0_TX_DMA_ERR_INTR_CLR_MASK              0x00000002
#define INTR_INTR_CLR_REG_L0_TX_DMA_ERR_INTR_CLR_ALIGN             0
#define INTR_INTR_CLR_REG_L0_TX_DMA_ERR_INTR_CLR_BITS              1
#define INTR_INTR_CLR_REG_L0_TX_DMA_ERR_INTR_CLR_SHIFT             1

/* INTR :: INTR_CLR_REG :: L0_TX_DMA_DONE_INTR_CLR [00:00] */
#define INTR_INTR_CLR_REG_L0_TX_DMA_DONE_INTR_CLR_MASK             0x00000001
#define INTR_INTR_CLR_REG_L0_TX_DMA_DONE_INTR_CLR_ALIGN            0
#define INTR_INTR_CLR_REG_L0_TX_DMA_DONE_INTR_CLR_BITS             1
#define INTR_INTR_CLR_REG_L0_TX_DMA_DONE_INTR_CLR_SHIFT            0


/****************************************************************************
 * INTR :: INTR_MSK_STS_REG
 ***************************************************************************/
/* INTR :: INTR_MSK_STS_REG :: reserved0 [31:26] */
#define INTR_INTR_MSK_STS_REG_reserved0_MASK                       0xfc000000
#define INTR_INTR_MSK_STS_REG_reserved0_ALIGN                      0
#define INTR_INTR_MSK_STS_REG_reserved0_BITS                       6
#define INTR_INTR_MSK_STS_REG_reserved0_SHIFT                      26

/* INTR :: INTR_MSK_STS_REG :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_CA_ATTN_MASK                0x02000000
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_CA_ATTN_ALIGN               0
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_CA_ATTN_BITS                1
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_CA_ATTN_SHIFT               25

/* INTR :: INTR_MSK_STS_REG :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_UR_ATTN_MASK                0x01000000
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_UR_ATTN_ALIGN               0
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_UR_ATTN_BITS                1
#define INTR_INTR_MSK_STS_REG_PCIE_TGT_UR_ATTN_SHIFT               24

/* INTR :: INTR_MSK_STS_REG :: reserved1 [23:14] */
#define INTR_INTR_MSK_STS_REG_reserved1_MASK                       0x00ffc000
#define INTR_INTR_MSK_STS_REG_reserved1_ALIGN                      0
#define INTR_INTR_MSK_STS_REG_reserved1_BITS                       10
#define INTR_INTR_MSK_STS_REG_reserved1_SHIFT                      14

/* INTR :: INTR_MSK_STS_REG :: L1_UV_RX_DMA_ERR_INTR_MSK [13:13] */
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_ERR_INTR_MSK_MASK       0x00002000
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_ERR_INTR_MSK_ALIGN      0
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_ERR_INTR_MSK_BITS       1
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_ERR_INTR_MSK_SHIFT      13

/* INTR :: INTR_MSK_STS_REG :: L1_UV_RX_DMA_DONE_INTR_MSK [12:12] */
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_DONE_INTR_MSK_MASK      0x00001000
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_DONE_INTR_MSK_ALIGN     0
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_DONE_INTR_MSK_BITS      1
#define INTR_INTR_MSK_STS_REG_L1_UV_RX_DMA_DONE_INTR_MSK_SHIFT     12

/* INTR :: INTR_MSK_STS_REG :: LIST1_Y_RX_DMA_ERR_INTR_MSK [11:11] */
#define INTR_INTR_MSK_STS_REG_LIST1_Y_RX_DMA_ERR_INTR_MSK_MASK     0x00000800
#define INTR_INTR_MSK_STS_REG_LIST1_Y_RX_DMA_ERR_INTR_MSK_ALIGN    0
#define INTR_INTR_MSK_STS_REG_LIST1_Y_RX_DMA_ERR_INTR_MSK_BITS     1
#define INTR_INTR_MSK_STS_REG_LIST1_Y_RX_DMA_ERR_INTR_MSK_SHIFT    11

/* INTR :: INTR_MSK_STS_REG :: L1_Y_RX_DMA_DONE_INTR_MSK [10:10] */
#define INTR_INTR_MSK_STS_REG_L1_Y_RX_DMA_DONE_INTR_MSK_MASK       0x00000400
#define INTR_INTR_MSK_STS_REG_L1_Y_RX_DMA_DONE_INTR_MSK_ALIGN      0
#define INTR_INTR_MSK_STS_REG_L1_Y_RX_DMA_DONE_INTR_MSK_BITS       1
#define INTR_INTR_MSK_STS_REG_L1_Y_RX_DMA_DONE_INTR_MSK_SHIFT      10

/* INTR :: INTR_MSK_STS_REG :: L1_TX_DMA_ERR_INTR_MSK [09:09] */
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_ERR_INTR_MSK_MASK          0x00000200
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_ERR_INTR_MSK_ALIGN         0
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_ERR_INTR_MSK_BITS          1
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_ERR_INTR_MSK_SHIFT         9

/* INTR :: INTR_MSK_STS_REG :: L1_TX_DMA_DONE_INTR_MSK [08:08] */
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_DONE_INTR_MSK_MASK         0x00000100
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_DONE_INTR_MSK_ALIGN        0
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_DONE_INTR_MSK_BITS         1
#define INTR_INTR_MSK_STS_REG_L1_TX_DMA_DONE_INTR_MSK_SHIFT        8

/* INTR :: INTR_MSK_STS_REG :: reserved2 [07:06] */
#define INTR_INTR_MSK_STS_REG_reserved2_MASK                       0x000000c0
#define INTR_INTR_MSK_STS_REG_reserved2_ALIGN                      0
#define INTR_INTR_MSK_STS_REG_reserved2_BITS                       2
#define INTR_INTR_MSK_STS_REG_reserved2_SHIFT                      6

/* INTR :: INTR_MSK_STS_REG :: L0_UV_RX_DMA_ERR_INTR_MSK [05:05] */
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_ERR_INTR_MSK_MASK       0x00000020
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_ERR_INTR_MSK_ALIGN      0
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_ERR_INTR_MSK_BITS       1
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_ERR_INTR_MSK_SHIFT      5

/* INTR :: INTR_MSK_STS_REG :: L0_UV_RX_DMA_DONE_INTR_MSK [04:04] */
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_DONE_INTR_MSK_MASK      0x00000010
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_DONE_INTR_MSK_ALIGN     0
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_DONE_INTR_MSK_BITS      1
#define INTR_INTR_MSK_STS_REG_L0_UV_RX_DMA_DONE_INTR_MSK_SHIFT     4

/* INTR :: INTR_MSK_STS_REG :: LIST0_Y_RX_DMA_ERR_INTR_MSK [03:03] */
#define INTR_INTR_MSK_STS_REG_LIST0_Y_RX_DMA_ERR_INTR_MSK_MASK     0x00000008
#define INTR_INTR_MSK_STS_REG_LIST0_Y_RX_DMA_ERR_INTR_MSK_ALIGN    0
#define INTR_INTR_MSK_STS_REG_LIST0_Y_RX_DMA_ERR_INTR_MSK_BITS     1
#define INTR_INTR_MSK_STS_REG_LIST0_Y_RX_DMA_ERR_INTR_MSK_SHIFT    3

/* INTR :: INTR_MSK_STS_REG :: L0_Y_RX_DMA_DONE_INTR_MSK [02:02] */
#define INTR_INTR_MSK_STS_REG_L0_Y_RX_DMA_DONE_INTR_MSK_MASK       0x00000004
#define INTR_INTR_MSK_STS_REG_L0_Y_RX_DMA_DONE_INTR_MSK_ALIGN      0
#define INTR_INTR_MSK_STS_REG_L0_Y_RX_DMA_DONE_INTR_MSK_BITS       1
#define INTR_INTR_MSK_STS_REG_L0_Y_RX_DMA_DONE_INTR_MSK_SHIFT      2

/* INTR :: INTR_MSK_STS_REG :: L0_TX_DMA_ERR_INTR_MSK [01:01] */
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_ERR_INTR_MSK_MASK          0x00000002
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_ERR_INTR_MSK_ALIGN         0
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_ERR_INTR_MSK_BITS          1
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_ERR_INTR_MSK_SHIFT         1

/* INTR :: INTR_MSK_STS_REG :: L0_TX_DMA_DONE_INTR_MSK [00:00] */
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_DONE_INTR_MSK_MASK         0x00000001
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_DONE_INTR_MSK_ALIGN        0
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_DONE_INTR_MSK_BITS         1
#define INTR_INTR_MSK_STS_REG_L0_TX_DMA_DONE_INTR_MSK_SHIFT        0


/****************************************************************************
 * INTR :: INTR_MSK_SET_REG
 ***************************************************************************/
/* INTR :: INTR_MSK_SET_REG :: reserved0 [31:26] */
#define INTR_INTR_MSK_SET_REG_reserved0_MASK                       0xfc000000
#define INTR_INTR_MSK_SET_REG_reserved0_ALIGN                      0
#define INTR_INTR_MSK_SET_REG_reserved0_BITS                       6
#define INTR_INTR_MSK_SET_REG_reserved0_SHIFT                      26

/* INTR :: INTR_MSK_SET_REG :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_CA_ATTN_MASK                0x02000000
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_CA_ATTN_ALIGN               0
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_CA_ATTN_BITS                1
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_CA_ATTN_SHIFT               25

/* INTR :: INTR_MSK_SET_REG :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_UR_ATTN_MASK                0x01000000
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_UR_ATTN_ALIGN               0
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_UR_ATTN_BITS                1
#define INTR_INTR_MSK_SET_REG_PCIE_TGT_UR_ATTN_SHIFT               24

/* INTR :: INTR_MSK_SET_REG :: reserved1 [23:14] */
#define INTR_INTR_MSK_SET_REG_reserved1_MASK                       0x00ffc000
#define INTR_INTR_MSK_SET_REG_reserved1_ALIGN                      0
#define INTR_INTR_MSK_SET_REG_reserved1_BITS                       10
#define INTR_INTR_MSK_SET_REG_reserved1_SHIFT                      14

/* INTR :: INTR_MSK_SET_REG :: L1_UV_RX_DMA_ERR_INTR_MSK_SET [13:13] */
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_ERR_INTR_MSK_SET_MASK   0x00002000
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_ERR_INTR_MSK_SET_ALIGN  0
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_ERR_INTR_MSK_SET_BITS   1
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_ERR_INTR_MSK_SET_SHIFT  13

/* INTR :: INTR_MSK_SET_REG :: L1_UV_RX_DMA_DONE_INTR_MSK_SET [12:12] */
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_DONE_INTR_MSK_SET_MASK  0x00001000
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_DONE_INTR_MSK_SET_ALIGN 0
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_DONE_INTR_MSK_SET_BITS  1
#define INTR_INTR_MSK_SET_REG_L1_UV_RX_DMA_DONE_INTR_MSK_SET_SHIFT 12

/* INTR :: INTR_MSK_SET_REG :: L1_Y_RX_DMA_ERR_INTR_MSK_SET [11:11] */
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_ERR_INTR_MSK_SET_MASK    0x00000800
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_ERR_INTR_MSK_SET_ALIGN   0
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_ERR_INTR_MSK_SET_BITS    1
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_ERR_INTR_MSK_SET_SHIFT   11

/* INTR :: INTR_MSK_SET_REG :: L1_Y_RX_DMA_DONE_INTR_MSK_SET [10:10] */
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_DONE_INTR_MSK_SET_MASK   0x00000400
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_DONE_INTR_MSK_SET_ALIGN  0
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_DONE_INTR_MSK_SET_BITS   1
#define INTR_INTR_MSK_SET_REG_L1_Y_RX_DMA_DONE_INTR_MSK_SET_SHIFT  10

/* INTR :: INTR_MSK_SET_REG :: L1_TX_DMA_ERR_INTR_MSK_SET [09:09] */
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_ERR_INTR_MSK_SET_MASK      0x00000200
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_ERR_INTR_MSK_SET_ALIGN     0
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_ERR_INTR_MSK_SET_BITS      1
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_ERR_INTR_MSK_SET_SHIFT     9

/* INTR :: INTR_MSK_SET_REG :: L1_TX_DMA_DONE_INTR_MSK_SET [08:08] */
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_DONE_INTR_MSK_SET_MASK     0x00000100
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_DONE_INTR_MSK_SET_ALIGN    0
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_DONE_INTR_MSK_SET_BITS     1
#define INTR_INTR_MSK_SET_REG_L1_TX_DMA_DONE_INTR_MSK_SET_SHIFT    8

/* INTR :: INTR_MSK_SET_REG :: reserved2 [07:06] */
#define INTR_INTR_MSK_SET_REG_reserved2_MASK                       0x000000c0
#define INTR_INTR_MSK_SET_REG_reserved2_ALIGN                      0
#define INTR_INTR_MSK_SET_REG_reserved2_BITS                       2
#define INTR_INTR_MSK_SET_REG_reserved2_SHIFT                      6

/* INTR :: INTR_MSK_SET_REG :: L0_UV_RX_DMA_ERR_INTR_MSK_SET [05:05] */
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_ERR_INTR_MSK_SET_MASK   0x00000020
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_ERR_INTR_MSK_SET_ALIGN  0
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_ERR_INTR_MSK_SET_BITS   1
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_ERR_INTR_MSK_SET_SHIFT  5

/* INTR :: INTR_MSK_SET_REG :: L0_UV_RX_DMA_DONE_INTR_MSK_SET [04:04] */
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_DONE_INTR_MSK_SET_MASK  0x00000010
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_DONE_INTR_MSK_SET_ALIGN 0
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_DONE_INTR_MSK_SET_BITS  1
#define INTR_INTR_MSK_SET_REG_L0_UV_RX_DMA_DONE_INTR_MSK_SET_SHIFT 4

/* INTR :: INTR_MSK_SET_REG :: L0_Y_RX_DMA_ERR_INTR_MSK_SET [03:03] */
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_ERR_INTR_MSK_SET_MASK    0x00000008
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_ERR_INTR_MSK_SET_ALIGN   0
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_ERR_INTR_MSK_SET_BITS    1
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_ERR_INTR_MSK_SET_SHIFT   3

/* INTR :: INTR_MSK_SET_REG :: L0_Y_RX_DMA_DONE_INTR_MSK_SET [02:02] */
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_DONE_INTR_MSK_SET_MASK   0x00000004
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_DONE_INTR_MSK_SET_ALIGN  0
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_DONE_INTR_MSK_SET_BITS   1
#define INTR_INTR_MSK_SET_REG_L0_Y_RX_DMA_DONE_INTR_MSK_SET_SHIFT  2

/* INTR :: INTR_MSK_SET_REG :: L0_TX_DMA_ERR_INTR_MSK_SET [01:01] */
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_ERR_INTR_MSK_SET_MASK      0x00000002
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_ERR_INTR_MSK_SET_ALIGN     0
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_ERR_INTR_MSK_SET_BITS      1
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_ERR_INTR_MSK_SET_SHIFT     1

/* INTR :: INTR_MSK_SET_REG :: L0_TX_DMA_DONE_INTR_MSK_SET [00:00] */
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_DONE_INTR_MSK_SET_MASK     0x00000001
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_DONE_INTR_MSK_SET_ALIGN    0
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_DONE_INTR_MSK_SET_BITS     1
#define INTR_INTR_MSK_SET_REG_L0_TX_DMA_DONE_INTR_MSK_SET_SHIFT    0


/****************************************************************************
 * INTR :: INTR_MSK_CLR_REG
 ***************************************************************************/
/* INTR :: INTR_MSK_CLR_REG :: reserved0 [31:26] */
#define INTR_INTR_MSK_CLR_REG_reserved0_MASK                       0xfc000000
#define INTR_INTR_MSK_CLR_REG_reserved0_ALIGN                      0
#define INTR_INTR_MSK_CLR_REG_reserved0_BITS                       6
#define INTR_INTR_MSK_CLR_REG_reserved0_SHIFT                      26

/* INTR :: INTR_MSK_CLR_REG :: PCIE_TGT_CA_ATTN [25:25] */
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_CA_ATTN_MASK                0x02000000
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_CA_ATTN_ALIGN               0
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_CA_ATTN_BITS                1
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_CA_ATTN_SHIFT               25

/* INTR :: INTR_MSK_CLR_REG :: PCIE_TGT_UR_ATTN [24:24] */
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_UR_ATTN_MASK                0x01000000
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_UR_ATTN_ALIGN               0
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_UR_ATTN_BITS                1
#define INTR_INTR_MSK_CLR_REG_PCIE_TGT_UR_ATTN_SHIFT               24

/* INTR :: INTR_MSK_CLR_REG :: reserved1 [23:14] */
#define INTR_INTR_MSK_CLR_REG_reserved1_MASK                       0x00ffc000
#define INTR_INTR_MSK_CLR_REG_reserved1_ALIGN                      0
#define INTR_INTR_MSK_CLR_REG_reserved1_BITS                       10
#define INTR_INTR_MSK_CLR_REG_reserved1_SHIFT                      14

/* INTR :: INTR_MSK_CLR_REG :: L1_UV_RX_DMA_ERR_INTR_MSK_CLR [13:13] */
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_ERR_INTR_MSK_CLR_MASK   0x00002000
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_ERR_INTR_MSK_CLR_ALIGN  0
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_ERR_INTR_MSK_CLR_BITS   1
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_ERR_INTR_MSK_CLR_SHIFT  13

/* INTR :: INTR_MSK_CLR_REG :: L1_UV_RX_DMA_DONE_INTR_MSK_CLR [12:12] */
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_DONE_INTR_MSK_CLR_MASK  0x00001000
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_DONE_INTR_MSK_CLR_ALIGN 0
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_DONE_INTR_MSK_CLR_BITS  1
#define INTR_INTR_MSK_CLR_REG_L1_UV_RX_DMA_DONE_INTR_MSK_CLR_SHIFT 12

/* INTR :: INTR_MSK_CLR_REG :: L1_Y_RX_DMA_ERR_INTR_MSK_CLR [11:11] */
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_ERR_INTR_MSK_CLR_MASK    0x00000800
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_ERR_INTR_MSK_CLR_ALIGN   0
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_ERR_INTR_MSK_CLR_BITS    1
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_ERR_INTR_MSK_CLR_SHIFT   11

/* INTR :: INTR_MSK_CLR_REG :: L1_Y_RX_DMA_DONE_INTR_MSK_CLR [10:10] */
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_DONE_INTR_MSK_CLR_MASK   0x00000400
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_DONE_INTR_MSK_CLR_ALIGN  0
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_DONE_INTR_MSK_CLR_BITS   1
#define INTR_INTR_MSK_CLR_REG_L1_Y_RX_DMA_DONE_INTR_MSK_CLR_SHIFT  10

/* INTR :: INTR_MSK_CLR_REG :: L1_TX_DMA_ERR_INTR_MSK_CLR [09:09] */
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_ERR_INTR_MSK_CLR_MASK      0x00000200
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_ERR_INTR_MSK_CLR_ALIGN     0
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_ERR_INTR_MSK_CLR_BITS      1
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_ERR_INTR_MSK_CLR_SHIFT     9

/* INTR :: INTR_MSK_CLR_REG :: L1_TX_DMA_DONE_INTR_MSK_CLR [08:08] */
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_DONE_INTR_MSK_CLR_MASK     0x00000100
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_DONE_INTR_MSK_CLR_ALIGN    0
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_DONE_INTR_MSK_CLR_BITS     1
#define INTR_INTR_MSK_CLR_REG_L1_TX_DMA_DONE_INTR_MSK_CLR_SHIFT    8

/* INTR :: INTR_MSK_CLR_REG :: reserved2 [07:06] */
#define INTR_INTR_MSK_CLR_REG_reserved2_MASK                       0x000000c0
#define INTR_INTR_MSK_CLR_REG_reserved2_ALIGN                      0
#define INTR_INTR_MSK_CLR_REG_reserved2_BITS                       2
#define INTR_INTR_MSK_CLR_REG_reserved2_SHIFT                      6

/* INTR :: INTR_MSK_CLR_REG :: L0_UV_RX_DMA_ERR_INTR_MSK_CLR [05:05] */
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_ERR_INTR_MSK_CLR_MASK   0x00000020
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_ERR_INTR_MSK_CLR_ALIGN  0
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_ERR_INTR_MSK_CLR_BITS   1
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_ERR_INTR_MSK_CLR_SHIFT  5

/* INTR :: INTR_MSK_CLR_REG :: L0_UV_RX_DMA_DONE_INTR_MSK_CLR [04:04] */
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_DONE_INTR_MSK_CLR_MASK  0x00000010
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_DONE_INTR_MSK_CLR_ALIGN 0
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_DONE_INTR_MSK_CLR_BITS  1
#define INTR_INTR_MSK_CLR_REG_L0_UV_RX_DMA_DONE_INTR_MSK_CLR_SHIFT 4

/* INTR :: INTR_MSK_CLR_REG :: L0_Y_RX_DMA_ERR_INTR_MSK_CLR [03:03] */
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_ERR_INTR_MSK_CLR_MASK    0x00000008
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_ERR_INTR_MSK_CLR_ALIGN   0
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_ERR_INTR_MSK_CLR_BITS    1
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_ERR_INTR_MSK_CLR_SHIFT   3

/* INTR :: INTR_MSK_CLR_REG :: L0_Y_RX_DMA_DONE_INTR_MSK_CLR [02:02] */
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_DONE_INTR_MSK_CLR_MASK   0x00000004
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_DONE_INTR_MSK_CLR_ALIGN  0
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_DONE_INTR_MSK_CLR_BITS   1
#define INTR_INTR_MSK_CLR_REG_L0_Y_RX_DMA_DONE_INTR_MSK_CLR_SHIFT  2

/* INTR :: INTR_MSK_CLR_REG :: L0_TX_DMA_ERR_INTR_MSK_CLR [01:01] */
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_ERR_INTR_MSK_CLR_MASK      0x00000002
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_ERR_INTR_MSK_CLR_ALIGN     0
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_ERR_INTR_MSK_CLR_BITS      1
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_ERR_INTR_MSK_CLR_SHIFT     1

/* INTR :: INTR_MSK_CLR_REG :: L0_TX_DMA_DONE_INTR_MSK_CLR [00:00] */
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_DONE_INTR_MSK_CLR_MASK     0x00000001
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_DONE_INTR_MSK_CLR_ALIGN    0
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_DONE_INTR_MSK_CLR_BITS     1
#define INTR_INTR_MSK_CLR_REG_L0_TX_DMA_DONE_INTR_MSK_CLR_SHIFT    0


/****************************************************************************
 * INTR :: EOI_CTRL
 ***************************************************************************/
/* INTR :: EOI_CTRL :: reserved0 [31:01] */
#define INTR_EOI_CTRL_reserved0_MASK                               0xfffffffe
#define INTR_EOI_CTRL_reserved0_ALIGN                              0
#define INTR_EOI_CTRL_reserved0_BITS                               31
#define INTR_EOI_CTRL_reserved0_SHIFT                              1

/* INTR :: EOI_CTRL :: EOI [00:00] */
#define INTR_EOI_CTRL_EOI_MASK                                     0x00000001
#define INTR_EOI_CTRL_EOI_ALIGN                                    0
#define INTR_EOI_CTRL_EOI_BITS                                     1
#define INTR_EOI_CTRL_EOI_SHIFT                                    0


/****************************************************************************
 * BCM70012_TGT_TOP_MDIO
 ***************************************************************************/
/****************************************************************************
 * MDIO :: CTRL0
 ***************************************************************************/
/* MDIO :: CTRL0 :: reserved0 [31:22] */
#define MDIO_CTRL0_reserved0_MASK                                  0xffc00000
#define MDIO_CTRL0_reserved0_ALIGN                                 0
#define MDIO_CTRL0_reserved0_BITS                                  10
#define MDIO_CTRL0_reserved0_SHIFT                                 22

/* MDIO :: CTRL0 :: WRITE_READ_COMMAND [21:21] */
#define MDIO_CTRL0_WRITE_READ_COMMAND_MASK                         0x00200000
#define MDIO_CTRL0_WRITE_READ_COMMAND_ALIGN                        0
#define MDIO_CTRL0_WRITE_READ_COMMAND_BITS                         1
#define MDIO_CTRL0_WRITE_READ_COMMAND_SHIFT                        21

/* MDIO :: CTRL0 :: PHYAD [20:16] */
#define MDIO_CTRL0_PHYAD_MASK                                      0x001f0000
#define MDIO_CTRL0_PHYAD_ALIGN                                     0
#define MDIO_CTRL0_PHYAD_BITS                                      5
#define MDIO_CTRL0_PHYAD_SHIFT                                     16

/* MDIO :: CTRL0 :: reserved1 [15:05] */
#define MDIO_CTRL0_reserved1_MASK                                  0x0000ffe0
#define MDIO_CTRL0_reserved1_ALIGN                                 0
#define MDIO_CTRL0_reserved1_BITS                                  11
#define MDIO_CTRL0_reserved1_SHIFT                                 5

/* MDIO :: CTRL0 :: REGAD [04:00] */
#define MDIO_CTRL0_REGAD_MASK                                      0x0000001f
#define MDIO_CTRL0_REGAD_ALIGN                                     0
#define MDIO_CTRL0_REGAD_BITS                                      5
#define MDIO_CTRL0_REGAD_SHIFT                                     0


/****************************************************************************
 * MDIO :: CTRL1
 ***************************************************************************/
/* MDIO :: CTRL1 :: WR_STATUS [31:31] */
#define MDIO_CTRL1_WR_STATUS_MASK                                  0x80000000
#define MDIO_CTRL1_WR_STATUS_ALIGN                                 0
#define MDIO_CTRL1_WR_STATUS_BITS                                  1
#define MDIO_CTRL1_WR_STATUS_SHIFT                                 31

/* MDIO :: CTRL1 :: reserved0 [30:16] */
#define MDIO_CTRL1_reserved0_MASK                                  0x7fff0000
#define MDIO_CTRL1_reserved0_ALIGN                                 0
#define MDIO_CTRL1_reserved0_BITS                                  15
#define MDIO_CTRL1_reserved0_SHIFT                                 16

/* MDIO :: CTRL1 :: Write_Data [15:00] */
#define MDIO_CTRL1_Write_Data_MASK                                 0x0000ffff
#define MDIO_CTRL1_Write_Data_ALIGN                                0
#define MDIO_CTRL1_Write_Data_BITS                                 16
#define MDIO_CTRL1_Write_Data_SHIFT                                0


/****************************************************************************
 * MDIO :: CTRL2
 ***************************************************************************/
/* MDIO :: CTRL2 :: RD_STATUS [31:31] */
#define MDIO_CTRL2_RD_STATUS_MASK                                  0x80000000
#define MDIO_CTRL2_RD_STATUS_ALIGN                                 0
#define MDIO_CTRL2_RD_STATUS_BITS                                  1
#define MDIO_CTRL2_RD_STATUS_SHIFT                                 31

/* MDIO :: CTRL2 :: reserved0 [30:16] */
#define MDIO_CTRL2_reserved0_MASK                                  0x7fff0000
#define MDIO_CTRL2_reserved0_ALIGN                                 0
#define MDIO_CTRL2_reserved0_BITS                                  15
#define MDIO_CTRL2_reserved0_SHIFT                                 16

/* MDIO :: CTRL2 :: Read_Data [15:00] */
#define MDIO_CTRL2_Read_Data_MASK                                  0x0000ffff
#define MDIO_CTRL2_Read_Data_ALIGN                                 0
#define MDIO_CTRL2_Read_Data_BITS                                  16
#define MDIO_CTRL2_Read_Data_SHIFT                                 0


/****************************************************************************
 * BCM70012_TGT_TOP_TGT_RGR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * TGT_RGR_BRIDGE :: REVISION
 ***************************************************************************/
/* TGT_RGR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define TGT_RGR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define TGT_RGR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define TGT_RGR_BRIDGE_REVISION_reserved0_BITS                     16
#define TGT_RGR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* TGT_RGR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define TGT_RGR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define TGT_RGR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define TGT_RGR_BRIDGE_REVISION_MAJOR_BITS                         8
#define TGT_RGR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* TGT_RGR_BRIDGE :: REVISION :: MINOR [07:00] */
#define TGT_RGR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define TGT_RGR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define TGT_RGR_BRIDGE_REVISION_MINOR_BITS                         8
#define TGT_RGR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * TGT_RGR_BRIDGE :: CTRL
 ***************************************************************************/
/* TGT_RGR_BRIDGE :: CTRL :: reserved0 [31:02] */
#define TGT_RGR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffc
#define TGT_RGR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define TGT_RGR_BRIDGE_CTRL_reserved0_BITS                         30
#define TGT_RGR_BRIDGE_CTRL_reserved0_SHIFT                        2

/* TGT_RGR_BRIDGE :: CTRL :: RBUS_ERROR_INTR [01:01] */
#define TGT_RGR_BRIDGE_CTRL_RBUS_ERROR_INTR_MASK                   0x00000002
#define TGT_RGR_BRIDGE_CTRL_RBUS_ERROR_INTR_ALIGN                  0
#define TGT_RGR_BRIDGE_CTRL_RBUS_ERROR_INTR_BITS                   1
#define TGT_RGR_BRIDGE_CTRL_RBUS_ERROR_INTR_SHIFT                  1

/* TGT_RGR_BRIDGE :: CTRL :: GISB_ERROR_INTR [00:00] */
#define TGT_RGR_BRIDGE_CTRL_GISB_ERROR_INTR_MASK                   0x00000001
#define TGT_RGR_BRIDGE_CTRL_GISB_ERROR_INTR_ALIGN                  0
#define TGT_RGR_BRIDGE_CTRL_GISB_ERROR_INTR_BITS                   1
#define TGT_RGR_BRIDGE_CTRL_GISB_ERROR_INTR_SHIFT                  0


/****************************************************************************
 * TGT_RGR_BRIDGE :: RBUS_TIMER
 ***************************************************************************/
/* TGT_RGR_BRIDGE :: RBUS_TIMER :: reserved0 [31:16] */
#define TGT_RGR_BRIDGE_RBUS_TIMER_reserved0_MASK                   0xffff0000
#define TGT_RGR_BRIDGE_RBUS_TIMER_reserved0_ALIGN                  0
#define TGT_RGR_BRIDGE_RBUS_TIMER_reserved0_BITS                   16
#define TGT_RGR_BRIDGE_RBUS_TIMER_reserved0_SHIFT                  16

/* TGT_RGR_BRIDGE :: RBUS_TIMER :: RBUS_TO_RBUS_TRANS_TIMER_CNT [15:00] */
#define TGT_RGR_BRIDGE_RBUS_TIMER_RBUS_TO_RBUS_TRANS_TIMER_CNT_MASK 0x0000ffff
#define TGT_RGR_BRIDGE_RBUS_TIMER_RBUS_TO_RBUS_TRANS_TIMER_CNT_ALIGN 0
#define TGT_RGR_BRIDGE_RBUS_TIMER_RBUS_TO_RBUS_TRANS_TIMER_CNT_BITS 16
#define TGT_RGR_BRIDGE_RBUS_TIMER_RBUS_TO_RBUS_TRANS_TIMER_CNT_SHIFT 0


/****************************************************************************
 * TGT_RGR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* TGT_RGR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* TGT_RGR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0


/****************************************************************************
 * TGT_RGR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* TGT_RGR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* TGT_RGR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define TGT_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0


/****************************************************************************
 * BCM70012_I2C_TOP_I2C
 ***************************************************************************/
/****************************************************************************
 * I2C :: CHIP_ADDRESS
 ***************************************************************************/
/* I2C :: CHIP_ADDRESS :: reserved0 [31:08] */
#define I2C_CHIP_ADDRESS_reserved0_MASK                            0xffffff00
#define I2C_CHIP_ADDRESS_reserved0_ALIGN                           0
#define I2C_CHIP_ADDRESS_reserved0_BITS                            24
#define I2C_CHIP_ADDRESS_reserved0_SHIFT                           8

/* I2C :: CHIP_ADDRESS :: CHIP_ADDRESS [07:01] */
#define I2C_CHIP_ADDRESS_CHIP_ADDRESS_MASK                         0x000000fe
#define I2C_CHIP_ADDRESS_CHIP_ADDRESS_ALIGN                        0
#define I2C_CHIP_ADDRESS_CHIP_ADDRESS_BITS                         7
#define I2C_CHIP_ADDRESS_CHIP_ADDRESS_SHIFT                        1

/* I2C :: CHIP_ADDRESS :: RESERVED [00:00] */
#define I2C_CHIP_ADDRESS_RESERVED_MASK                             0x00000001
#define I2C_CHIP_ADDRESS_RESERVED_ALIGN                            0
#define I2C_CHIP_ADDRESS_RESERVED_BITS                             1
#define I2C_CHIP_ADDRESS_RESERVED_SHIFT                            0


/****************************************************************************
 * I2C :: DATA_IN0
 ***************************************************************************/
/* I2C :: DATA_IN0 :: reserved0 [31:08] */
#define I2C_DATA_IN0_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN0_reserved0_ALIGN                               0
#define I2C_DATA_IN0_reserved0_BITS                                24
#define I2C_DATA_IN0_reserved0_SHIFT                               8

/* I2C :: DATA_IN0 :: DATA_IN0 [07:00] */
#define I2C_DATA_IN0_DATA_IN0_MASK                                 0x000000ff
#define I2C_DATA_IN0_DATA_IN0_ALIGN                                0
#define I2C_DATA_IN0_DATA_IN0_BITS                                 8
#define I2C_DATA_IN0_DATA_IN0_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN1
 ***************************************************************************/
/* I2C :: DATA_IN1 :: reserved0 [31:08] */
#define I2C_DATA_IN1_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN1_reserved0_ALIGN                               0
#define I2C_DATA_IN1_reserved0_BITS                                24
#define I2C_DATA_IN1_reserved0_SHIFT                               8

/* I2C :: DATA_IN1 :: DATA_IN1 [07:00] */
#define I2C_DATA_IN1_DATA_IN1_MASK                                 0x000000ff
#define I2C_DATA_IN1_DATA_IN1_ALIGN                                0
#define I2C_DATA_IN1_DATA_IN1_BITS                                 8
#define I2C_DATA_IN1_DATA_IN1_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN2
 ***************************************************************************/
/* I2C :: DATA_IN2 :: reserved0 [31:08] */
#define I2C_DATA_IN2_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN2_reserved0_ALIGN                               0
#define I2C_DATA_IN2_reserved0_BITS                                24
#define I2C_DATA_IN2_reserved0_SHIFT                               8

/* I2C :: DATA_IN2 :: DATA_IN2 [07:00] */
#define I2C_DATA_IN2_DATA_IN2_MASK                                 0x000000ff
#define I2C_DATA_IN2_DATA_IN2_ALIGN                                0
#define I2C_DATA_IN2_DATA_IN2_BITS                                 8
#define I2C_DATA_IN2_DATA_IN2_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN3
 ***************************************************************************/
/* I2C :: DATA_IN3 :: reserved0 [31:08] */
#define I2C_DATA_IN3_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN3_reserved0_ALIGN                               0
#define I2C_DATA_IN3_reserved0_BITS                                24
#define I2C_DATA_IN3_reserved0_SHIFT                               8

/* I2C :: DATA_IN3 :: DATA_IN3 [07:00] */
#define I2C_DATA_IN3_DATA_IN3_MASK                                 0x000000ff
#define I2C_DATA_IN3_DATA_IN3_ALIGN                                0
#define I2C_DATA_IN3_DATA_IN3_BITS                                 8
#define I2C_DATA_IN3_DATA_IN3_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN4
 ***************************************************************************/
/* I2C :: DATA_IN4 :: reserved0 [31:08] */
#define I2C_DATA_IN4_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN4_reserved0_ALIGN                               0
#define I2C_DATA_IN4_reserved0_BITS                                24
#define I2C_DATA_IN4_reserved0_SHIFT                               8

/* I2C :: DATA_IN4 :: DATA_IN4 [07:00] */
#define I2C_DATA_IN4_DATA_IN4_MASK                                 0x000000ff
#define I2C_DATA_IN4_DATA_IN4_ALIGN                                0
#define I2C_DATA_IN4_DATA_IN4_BITS                                 8
#define I2C_DATA_IN4_DATA_IN4_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN5
 ***************************************************************************/
/* I2C :: DATA_IN5 :: reserved0 [31:08] */
#define I2C_DATA_IN5_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN5_reserved0_ALIGN                               0
#define I2C_DATA_IN5_reserved0_BITS                                24
#define I2C_DATA_IN5_reserved0_SHIFT                               8

/* I2C :: DATA_IN5 :: DATA_IN5 [07:00] */
#define I2C_DATA_IN5_DATA_IN5_MASK                                 0x000000ff
#define I2C_DATA_IN5_DATA_IN5_ALIGN                                0
#define I2C_DATA_IN5_DATA_IN5_BITS                                 8
#define I2C_DATA_IN5_DATA_IN5_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN6
 ***************************************************************************/
/* I2C :: DATA_IN6 :: reserved0 [31:08] */
#define I2C_DATA_IN6_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN6_reserved0_ALIGN                               0
#define I2C_DATA_IN6_reserved0_BITS                                24
#define I2C_DATA_IN6_reserved0_SHIFT                               8

/* I2C :: DATA_IN6 :: DATA_IN6 [07:00] */
#define I2C_DATA_IN6_DATA_IN6_MASK                                 0x000000ff
#define I2C_DATA_IN6_DATA_IN6_ALIGN                                0
#define I2C_DATA_IN6_DATA_IN6_BITS                                 8
#define I2C_DATA_IN6_DATA_IN6_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_IN7
 ***************************************************************************/
/* I2C :: DATA_IN7 :: reserved0 [31:08] */
#define I2C_DATA_IN7_reserved0_MASK                                0xffffff00
#define I2C_DATA_IN7_reserved0_ALIGN                               0
#define I2C_DATA_IN7_reserved0_BITS                                24
#define I2C_DATA_IN7_reserved0_SHIFT                               8

/* I2C :: DATA_IN7 :: DATA_IN7 [07:00] */
#define I2C_DATA_IN7_DATA_IN7_MASK                                 0x000000ff
#define I2C_DATA_IN7_DATA_IN7_ALIGN                                0
#define I2C_DATA_IN7_DATA_IN7_BITS                                 8
#define I2C_DATA_IN7_DATA_IN7_SHIFT                                0


/****************************************************************************
 * I2C :: CNT_REG
 ***************************************************************************/
/* I2C :: CNT_REG :: reserved0 [31:08] */
#define I2C_CNT_REG_reserved0_MASK                                 0xffffff00
#define I2C_CNT_REG_reserved0_ALIGN                                0
#define I2C_CNT_REG_reserved0_BITS                                 24
#define I2C_CNT_REG_reserved0_SHIFT                                8

/* I2C :: CNT_REG :: CNT_REG2 [07:04] */
#define I2C_CNT_REG_CNT_REG2_MASK                                  0x000000f0
#define I2C_CNT_REG_CNT_REG2_ALIGN                                 0
#define I2C_CNT_REG_CNT_REG2_BITS                                  4
#define I2C_CNT_REG_CNT_REG2_SHIFT                                 4

/* I2C :: CNT_REG :: CNT_REG1 [03:00] */
#define I2C_CNT_REG_CNT_REG1_MASK                                  0x0000000f
#define I2C_CNT_REG_CNT_REG1_ALIGN                                 0
#define I2C_CNT_REG_CNT_REG1_BITS                                  4
#define I2C_CNT_REG_CNT_REG1_SHIFT                                 0


/****************************************************************************
 * I2C :: CTL_REG
 ***************************************************************************/
/* I2C :: CTL_REG :: reserved0 [31:08] */
#define I2C_CTL_REG_reserved0_MASK                                 0xffffff00
#define I2C_CTL_REG_reserved0_ALIGN                                0
#define I2C_CTL_REG_reserved0_BITS                                 24
#define I2C_CTL_REG_reserved0_SHIFT                                8

/* I2C :: CTL_REG :: DIV_CLK [07:07] */
#define I2C_CTL_REG_DIV_CLK_MASK                                   0x00000080
#define I2C_CTL_REG_DIV_CLK_ALIGN                                  0
#define I2C_CTL_REG_DIV_CLK_BITS                                   1
#define I2C_CTL_REG_DIV_CLK_SHIFT                                  7

/* I2C :: CTL_REG :: INT_EN [06:06] */
#define I2C_CTL_REG_INT_EN_MASK                                    0x00000040
#define I2C_CTL_REG_INT_EN_ALIGN                                   0
#define I2C_CTL_REG_INT_EN_BITS                                    1
#define I2C_CTL_REG_INT_EN_SHIFT                                   6

/* I2C :: CTL_REG :: SCL_SEL [05:04] */
#define I2C_CTL_REG_SCL_SEL_MASK                                   0x00000030
#define I2C_CTL_REG_SCL_SEL_ALIGN                                  0
#define I2C_CTL_REG_SCL_SEL_BITS                                   2
#define I2C_CTL_REG_SCL_SEL_SHIFT                                  4

/* I2C :: CTL_REG :: DELAY_DIS [03:03] */
#define I2C_CTL_REG_DELAY_DIS_MASK                                 0x00000008
#define I2C_CTL_REG_DELAY_DIS_ALIGN                                0
#define I2C_CTL_REG_DELAY_DIS_BITS                                 1
#define I2C_CTL_REG_DELAY_DIS_SHIFT                                3

/* I2C :: CTL_REG :: DEGLITCH_DIS [02:02] */
#define I2C_CTL_REG_DEGLITCH_DIS_MASK                              0x00000004
#define I2C_CTL_REG_DEGLITCH_DIS_ALIGN                             0
#define I2C_CTL_REG_DEGLITCH_DIS_BITS                              1
#define I2C_CTL_REG_DEGLITCH_DIS_SHIFT                             2

/* I2C :: CTL_REG :: DTF [01:00] */
#define I2C_CTL_REG_DTF_MASK                                       0x00000003
#define I2C_CTL_REG_DTF_ALIGN                                      0
#define I2C_CTL_REG_DTF_BITS                                       2
#define I2C_CTL_REG_DTF_SHIFT                                      0


/****************************************************************************
 * I2C :: IIC_ENABLE
 ***************************************************************************/
/* I2C :: IIC_ENABLE :: reserved0 [31:07] */
#define I2C_IIC_ENABLE_reserved0_MASK                              0xffffff80
#define I2C_IIC_ENABLE_reserved0_ALIGN                             0
#define I2C_IIC_ENABLE_reserved0_BITS                              25
#define I2C_IIC_ENABLE_reserved0_SHIFT                             7

/* I2C :: IIC_ENABLE :: RESTART [06:06] */
#define I2C_IIC_ENABLE_RESTART_MASK                                0x00000040
#define I2C_IIC_ENABLE_RESTART_ALIGN                               0
#define I2C_IIC_ENABLE_RESTART_BITS                                1
#define I2C_IIC_ENABLE_RESTART_SHIFT                               6

/* I2C :: IIC_ENABLE :: NO_START [05:05] */
#define I2C_IIC_ENABLE_NO_START_MASK                               0x00000020
#define I2C_IIC_ENABLE_NO_START_ALIGN                              0
#define I2C_IIC_ENABLE_NO_START_BITS                               1
#define I2C_IIC_ENABLE_NO_START_SHIFT                              5

/* I2C :: IIC_ENABLE :: NO_STOP [04:04] */
#define I2C_IIC_ENABLE_NO_STOP_MASK                                0x00000010
#define I2C_IIC_ENABLE_NO_STOP_ALIGN                               0
#define I2C_IIC_ENABLE_NO_STOP_BITS                                1
#define I2C_IIC_ENABLE_NO_STOP_SHIFT                               4

/* I2C :: IIC_ENABLE :: reserved1 [03:03] */
#define I2C_IIC_ENABLE_reserved1_MASK                              0x00000008
#define I2C_IIC_ENABLE_reserved1_ALIGN                             0
#define I2C_IIC_ENABLE_reserved1_BITS                              1
#define I2C_IIC_ENABLE_reserved1_SHIFT                             3

/* I2C :: IIC_ENABLE :: NO_ACK [02:02] */
#define I2C_IIC_ENABLE_NO_ACK_MASK                                 0x00000004
#define I2C_IIC_ENABLE_NO_ACK_ALIGN                                0
#define I2C_IIC_ENABLE_NO_ACK_BITS                                 1
#define I2C_IIC_ENABLE_NO_ACK_SHIFT                                2

/* I2C :: IIC_ENABLE :: INTRP [01:01] */
#define I2C_IIC_ENABLE_INTRP_MASK                                  0x00000002
#define I2C_IIC_ENABLE_INTRP_ALIGN                                 0
#define I2C_IIC_ENABLE_INTRP_BITS                                  1
#define I2C_IIC_ENABLE_INTRP_SHIFT                                 1

/* I2C :: IIC_ENABLE :: ENABLE [00:00] */
#define I2C_IIC_ENABLE_ENABLE_MASK                                 0x00000001
#define I2C_IIC_ENABLE_ENABLE_ALIGN                                0
#define I2C_IIC_ENABLE_ENABLE_BITS                                 1
#define I2C_IIC_ENABLE_ENABLE_SHIFT                                0


/****************************************************************************
 * I2C :: DATA_OUT0
 ***************************************************************************/
/* I2C :: DATA_OUT0 :: reserved0 [31:08] */
#define I2C_DATA_OUT0_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT0_reserved0_ALIGN                              0
#define I2C_DATA_OUT0_reserved0_BITS                               24
#define I2C_DATA_OUT0_reserved0_SHIFT                              8

/* I2C :: DATA_OUT0 :: DATA_OUT0 [07:00] */
#define I2C_DATA_OUT0_DATA_OUT0_MASK                               0x000000ff
#define I2C_DATA_OUT0_DATA_OUT0_ALIGN                              0
#define I2C_DATA_OUT0_DATA_OUT0_BITS                               8
#define I2C_DATA_OUT0_DATA_OUT0_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT1
 ***************************************************************************/
/* I2C :: DATA_OUT1 :: reserved0 [31:08] */
#define I2C_DATA_OUT1_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT1_reserved0_ALIGN                              0
#define I2C_DATA_OUT1_reserved0_BITS                               24
#define I2C_DATA_OUT1_reserved0_SHIFT                              8

/* I2C :: DATA_OUT1 :: DATA_OUT1 [07:00] */
#define I2C_DATA_OUT1_DATA_OUT1_MASK                               0x000000ff
#define I2C_DATA_OUT1_DATA_OUT1_ALIGN                              0
#define I2C_DATA_OUT1_DATA_OUT1_BITS                               8
#define I2C_DATA_OUT1_DATA_OUT1_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT2
 ***************************************************************************/
/* I2C :: DATA_OUT2 :: reserved0 [31:08] */
#define I2C_DATA_OUT2_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT2_reserved0_ALIGN                              0
#define I2C_DATA_OUT2_reserved0_BITS                               24
#define I2C_DATA_OUT2_reserved0_SHIFT                              8

/* I2C :: DATA_OUT2 :: DATA_OUT2 [07:00] */
#define I2C_DATA_OUT2_DATA_OUT2_MASK                               0x000000ff
#define I2C_DATA_OUT2_DATA_OUT2_ALIGN                              0
#define I2C_DATA_OUT2_DATA_OUT2_BITS                               8
#define I2C_DATA_OUT2_DATA_OUT2_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT3
 ***************************************************************************/
/* I2C :: DATA_OUT3 :: reserved0 [31:08] */
#define I2C_DATA_OUT3_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT3_reserved0_ALIGN                              0
#define I2C_DATA_OUT3_reserved0_BITS                               24
#define I2C_DATA_OUT3_reserved0_SHIFT                              8

/* I2C :: DATA_OUT3 :: DATA_OUT3 [07:00] */
#define I2C_DATA_OUT3_DATA_OUT3_MASK                               0x000000ff
#define I2C_DATA_OUT3_DATA_OUT3_ALIGN                              0
#define I2C_DATA_OUT3_DATA_OUT3_BITS                               8
#define I2C_DATA_OUT3_DATA_OUT3_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT4
 ***************************************************************************/
/* I2C :: DATA_OUT4 :: reserved0 [31:08] */
#define I2C_DATA_OUT4_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT4_reserved0_ALIGN                              0
#define I2C_DATA_OUT4_reserved0_BITS                               24
#define I2C_DATA_OUT4_reserved0_SHIFT                              8

/* I2C :: DATA_OUT4 :: DATA_OUT4 [07:00] */
#define I2C_DATA_OUT4_DATA_OUT4_MASK                               0x000000ff
#define I2C_DATA_OUT4_DATA_OUT4_ALIGN                              0
#define I2C_DATA_OUT4_DATA_OUT4_BITS                               8
#define I2C_DATA_OUT4_DATA_OUT4_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT5
 ***************************************************************************/
/* I2C :: DATA_OUT5 :: reserved0 [31:08] */
#define I2C_DATA_OUT5_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT5_reserved0_ALIGN                              0
#define I2C_DATA_OUT5_reserved0_BITS                               24
#define I2C_DATA_OUT5_reserved0_SHIFT                              8

/* I2C :: DATA_OUT5 :: DATA_OUT5 [07:00] */
#define I2C_DATA_OUT5_DATA_OUT5_MASK                               0x000000ff
#define I2C_DATA_OUT5_DATA_OUT5_ALIGN                              0
#define I2C_DATA_OUT5_DATA_OUT5_BITS                               8
#define I2C_DATA_OUT5_DATA_OUT5_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT6
 ***************************************************************************/
/* I2C :: DATA_OUT6 :: reserved0 [31:08] */
#define I2C_DATA_OUT6_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT6_reserved0_ALIGN                              0
#define I2C_DATA_OUT6_reserved0_BITS                               24
#define I2C_DATA_OUT6_reserved0_SHIFT                              8

/* I2C :: DATA_OUT6 :: DATA_OUT6 [07:00] */
#define I2C_DATA_OUT6_DATA_OUT6_MASK                               0x000000ff
#define I2C_DATA_OUT6_DATA_OUT6_ALIGN                              0
#define I2C_DATA_OUT6_DATA_OUT6_BITS                               8
#define I2C_DATA_OUT6_DATA_OUT6_SHIFT                              0


/****************************************************************************
 * I2C :: DATA_OUT7
 ***************************************************************************/
/* I2C :: DATA_OUT7 :: reserved0 [31:08] */
#define I2C_DATA_OUT7_reserved0_MASK                               0xffffff00
#define I2C_DATA_OUT7_reserved0_ALIGN                              0
#define I2C_DATA_OUT7_reserved0_BITS                               24
#define I2C_DATA_OUT7_reserved0_SHIFT                              8

/* I2C :: DATA_OUT7 :: DATA_OUT7 [07:00] */
#define I2C_DATA_OUT7_DATA_OUT7_MASK                               0x000000ff
#define I2C_DATA_OUT7_DATA_OUT7_ALIGN                              0
#define I2C_DATA_OUT7_DATA_OUT7_BITS                               8
#define I2C_DATA_OUT7_DATA_OUT7_SHIFT                              0


/****************************************************************************
 * I2C :: CTLHI_REG
 ***************************************************************************/
/* I2C :: CTLHI_REG :: reserved0 [31:02] */
#define I2C_CTLHI_REG_reserved0_MASK                               0xfffffffc
#define I2C_CTLHI_REG_reserved0_ALIGN                              0
#define I2C_CTLHI_REG_reserved0_BITS                               30
#define I2C_CTLHI_REG_reserved0_SHIFT                              2

/* I2C :: CTLHI_REG :: IGNORE_ACK [01:01] */
#define I2C_CTLHI_REG_IGNORE_ACK_MASK                              0x00000002
#define I2C_CTLHI_REG_IGNORE_ACK_ALIGN                             0
#define I2C_CTLHI_REG_IGNORE_ACK_BITS                              1
#define I2C_CTLHI_REG_IGNORE_ACK_SHIFT                             1

/* I2C :: CTLHI_REG :: WAIT_DIS [00:00] */
#define I2C_CTLHI_REG_WAIT_DIS_MASK                                0x00000001
#define I2C_CTLHI_REG_WAIT_DIS_ALIGN                               0
#define I2C_CTLHI_REG_WAIT_DIS_BITS                                1
#define I2C_CTLHI_REG_WAIT_DIS_SHIFT                               0


/****************************************************************************
 * I2C :: SCL_PARAM
 ***************************************************************************/
/* I2C :: SCL_PARAM :: reserved0 [31:00] */
#define I2C_SCL_PARAM_reserved0_MASK                               0xffffffff
#define I2C_SCL_PARAM_reserved0_ALIGN                              0
#define I2C_SCL_PARAM_reserved0_BITS                               32
#define I2C_SCL_PARAM_reserved0_SHIFT                              0


/****************************************************************************
 * BCM70012_I2C_TOP_I2C_GR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * I2C_GR_BRIDGE :: REVISION
 ***************************************************************************/
/* I2C_GR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define I2C_GR_BRIDGE_REVISION_reserved0_MASK                      0xffff0000
#define I2C_GR_BRIDGE_REVISION_reserved0_ALIGN                     0
#define I2C_GR_BRIDGE_REVISION_reserved0_BITS                      16
#define I2C_GR_BRIDGE_REVISION_reserved0_SHIFT                     16

/* I2C_GR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define I2C_GR_BRIDGE_REVISION_MAJOR_MASK                          0x0000ff00
#define I2C_GR_BRIDGE_REVISION_MAJOR_ALIGN                         0
#define I2C_GR_BRIDGE_REVISION_MAJOR_BITS                          8
#define I2C_GR_BRIDGE_REVISION_MAJOR_SHIFT                         8

/* I2C_GR_BRIDGE :: REVISION :: MINOR [07:00] */
#define I2C_GR_BRIDGE_REVISION_MINOR_MASK                          0x000000ff
#define I2C_GR_BRIDGE_REVISION_MINOR_ALIGN                         0
#define I2C_GR_BRIDGE_REVISION_MINOR_BITS                          8
#define I2C_GR_BRIDGE_REVISION_MINOR_SHIFT                         0


/****************************************************************************
 * I2C_GR_BRIDGE :: CTRL
 ***************************************************************************/
/* I2C_GR_BRIDGE :: CTRL :: reserved0 [31:01] */
#define I2C_GR_BRIDGE_CTRL_reserved0_MASK                          0xfffffffe
#define I2C_GR_BRIDGE_CTRL_reserved0_ALIGN                         0
#define I2C_GR_BRIDGE_CTRL_reserved0_BITS                          31
#define I2C_GR_BRIDGE_CTRL_reserved0_SHIFT                         1

/* I2C_GR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_MASK                    0x00000001
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_ALIGN                   0
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_BITS                    1
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_SHIFT                   0
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE            0
#define I2C_GR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE             1


/****************************************************************************
 * I2C_GR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* I2C_GR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK              0xfffffffe
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN             0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS              31
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT             1

/* I2C_GR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK         0x00000001
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN        0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS         1
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT        0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT     0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT       1


/****************************************************************************
 * I2C_GR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* I2C_GR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK              0xfffffffe
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN             0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS              31
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT             1

/* I2C_GR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK         0x00000001
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN        0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS         1
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT        0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT     0
#define I2C_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT       1


/****************************************************************************
 * BCM70012_MISC_TOP_MISC1
 ***************************************************************************/
/****************************************************************************
 * MISC1 :: TX_FIRST_DESC_L_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST0 :: DESC_ADDR [31:05] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_MASK            0xffffffe0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_ALIGN           0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_BITS            27
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_SHIFT           5

/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST0 :: reserved0 [04:01] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_reserved0_MASK            0x0000001e
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_reserved0_ALIGN           0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_reserved0_BITS            4
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_reserved0_SHIFT           1

/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST0 :: TX_DESC_LIST0_VALID [00:00] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_TX_DESC_LIST0_VALID_MASK  0x00000001
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_TX_DESC_LIST0_VALID_ALIGN 0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_TX_DESC_LIST0_VALID_BITS  1
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST0_TX_DESC_LIST0_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: TX_FIRST_DESC_U_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: TX_FIRST_DESC_U_ADDR_LIST0 :: DESC_ADDR [31:00] */
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_MASK            0xffffffff
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_ALIGN           0
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_BITS            32
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_SHIFT           0


/****************************************************************************
 * MISC1 :: TX_FIRST_DESC_L_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST1 :: DESC_ADDR [31:05] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_MASK            0xffffffe0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_ALIGN           0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_BITS            27
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_SHIFT           5

/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST1 :: reserved0 [04:01] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_reserved0_MASK            0x0000001e
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_reserved0_ALIGN           0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_reserved0_BITS            4
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_reserved0_SHIFT           1

/* MISC1 :: TX_FIRST_DESC_L_ADDR_LIST1 :: TX_DESC_LIST1_VALID [00:00] */
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_TX_DESC_LIST1_VALID_MASK  0x00000001
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_TX_DESC_LIST1_VALID_ALIGN 0
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_TX_DESC_LIST1_VALID_BITS  1
#define MISC1_TX_FIRST_DESC_L_ADDR_LIST1_TX_DESC_LIST1_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: TX_FIRST_DESC_U_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: TX_FIRST_DESC_U_ADDR_LIST1 :: DESC_ADDR [31:00] */
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_MASK            0xffffffff
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_ALIGN           0
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_BITS            32
#define MISC1_TX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_SHIFT           0


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
 * MISC1 :: TX_DMA_LIST0_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST0_CUR_DESC_L_ADDR :: TX_DMA_L0_CUR_DESC_L_ADDR [31:05] */
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_TX_DMA_L0_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_TX_DMA_L0_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_TX_DMA_L0_CUR_DESC_L_ADDR_BITS 27
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_TX_DMA_L0_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: TX_DMA_LIST0_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_reserved0_MASK          0x0000001f
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_reserved0_ALIGN         0
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_reserved0_BITS          5
#define MISC1_TX_DMA_LIST0_CUR_DESC_L_ADDR_reserved0_SHIFT         0


/****************************************************************************
 * MISC1 :: TX_DMA_LIST0_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST0_CUR_DESC_U_ADDR :: TX_DMA_L0_CUR_DESC_U_ADDR [31:00] */
#define MISC1_TX_DMA_LIST0_CUR_DESC_U_ADDR_TX_DMA_L0_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_TX_DMA_LIST0_CUR_DESC_U_ADDR_TX_DMA_L0_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_TX_DMA_LIST0_CUR_DESC_U_ADDR_TX_DMA_L0_CUR_DESC_U_ADDR_BITS 32
#define MISC1_TX_DMA_LIST0_CUR_DESC_U_ADDR_TX_DMA_L0_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: TX_DMA_LIST0_CUR_BYTE_CNT_REM
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST0_CUR_BYTE_CNT_REM :: reserved0 [31:24] */
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved0_MASK         0xff000000
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved0_ALIGN        0
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved0_BITS         8
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved0_SHIFT        24

/* MISC1 :: TX_DMA_LIST0_CUR_BYTE_CNT_REM :: TX_DMA_L0_CUR_BYTE_CNT_REM [23:02] */
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_TX_DMA_L0_CUR_BYTE_CNT_REM_MASK 0x00fffffc
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_TX_DMA_L0_CUR_BYTE_CNT_REM_ALIGN 0
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_TX_DMA_L0_CUR_BYTE_CNT_REM_BITS 22
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_TX_DMA_L0_CUR_BYTE_CNT_REM_SHIFT 2

/* MISC1 :: TX_DMA_LIST0_CUR_BYTE_CNT_REM :: reserved1 [01:00] */
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved1_MASK         0x00000003
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved1_ALIGN        0
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved1_BITS         2
#define MISC1_TX_DMA_LIST0_CUR_BYTE_CNT_REM_reserved1_SHIFT        0


/****************************************************************************
 * MISC1 :: TX_DMA_LIST1_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST1_CUR_DESC_L_ADDR :: TX_DMA_L1_CUR_DESC_L_ADDR [31:05] */
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_TX_DMA_L1_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_TX_DMA_L1_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_TX_DMA_L1_CUR_DESC_L_ADDR_BITS 27
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_TX_DMA_L1_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: TX_DMA_LIST1_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_reserved0_MASK          0x0000001f
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_reserved0_ALIGN         0
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_reserved0_BITS          5
#define MISC1_TX_DMA_LIST1_CUR_DESC_L_ADDR_reserved0_SHIFT         0


/****************************************************************************
 * MISC1 :: TX_DMA_LIST1_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST1_CUR_DESC_U_ADDR :: TX_DMA_L1_CUR_DESC_U_ADDR [31:00] */
#define MISC1_TX_DMA_LIST1_CUR_DESC_U_ADDR_TX_DMA_L1_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_TX_DMA_LIST1_CUR_DESC_U_ADDR_TX_DMA_L1_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_TX_DMA_LIST1_CUR_DESC_U_ADDR_TX_DMA_L1_CUR_DESC_U_ADDR_BITS 32
#define MISC1_TX_DMA_LIST1_CUR_DESC_U_ADDR_TX_DMA_L1_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: TX_DMA_LIST1_CUR_BYTE_CNT_REM
 ***************************************************************************/
/* MISC1 :: TX_DMA_LIST1_CUR_BYTE_CNT_REM :: reserved0 [31:24] */
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved0_MASK         0xff000000
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved0_ALIGN        0
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved0_BITS         8
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved0_SHIFT        24

/* MISC1 :: TX_DMA_LIST1_CUR_BYTE_CNT_REM :: TX_DMA_L1_CUR_BYTE_CNT_REM [23:02] */
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_TX_DMA_L1_CUR_BYTE_CNT_REM_MASK 0x00fffffc
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_TX_DMA_L1_CUR_BYTE_CNT_REM_ALIGN 0
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_TX_DMA_L1_CUR_BYTE_CNT_REM_BITS 22
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_TX_DMA_L1_CUR_BYTE_CNT_REM_SHIFT 2

/* MISC1 :: TX_DMA_LIST1_CUR_BYTE_CNT_REM :: reserved1 [01:00] */
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved1_MASK         0x00000003
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved1_ALIGN        0
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved1_BITS         2
#define MISC1_TX_DMA_LIST1_CUR_BYTE_CNT_REM_reserved1_SHIFT        0


/****************************************************************************
 * MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST0 :: DESC_ADDR [31:05] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_MASK          0xffffffe0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_BITS          27
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_SHIFT         5

/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST0 :: reserved0 [04:01] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_MASK          0x0000001e
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_BITS          4
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_SHIFT         1

/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST0 :: RX_DESC_LIST0_VALID [00:00] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_MASK 0x00000001
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_ALIGN 0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_BITS 1
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: Y_RX_FIRST_DESC_U_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: Y_RX_FIRST_DESC_U_ADDR_LIST0 :: DESC_ADDR [31:00] */
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_MASK          0xffffffff
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_BITS          32
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_SHIFT         0


/****************************************************************************
 * MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST1 :: DESC_ADDR [31:05] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_MASK          0xffffffe0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_BITS          27
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_SHIFT         5

/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST1 :: reserved0 [04:01] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_MASK          0x0000001e
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_BITS          4
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_SHIFT         1

/* MISC1 :: Y_RX_FIRST_DESC_L_ADDR_LIST1 :: RX_DESC_LIST1_VALID [00:00] */
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_MASK 0x00000001
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_ALIGN 0
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_BITS 1
#define MISC1_Y_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: Y_RX_FIRST_DESC_U_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: Y_RX_FIRST_DESC_U_ADDR_LIST1 :: DESC_ADDR [31:00] */
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_MASK          0xffffffff
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_ALIGN         0
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_BITS          32
#define MISC1_Y_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_SHIFT         0


/****************************************************************************
 * MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS
 ***************************************************************************/
/* MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS :: reserved0 [31:04] */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_reserved0_MASK            0xfffffff0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_reserved0_ALIGN           0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_reserved0_BITS            28
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_reserved0_SHIFT           4

/* MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS :: DMA_DATA_SERV_PTR [03:03] */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_MASK    0x00000008
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_ALIGN   0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_BITS    1
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_SHIFT   3

/* MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS :: DESC_SERV_PTR [02:02] */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_MASK        0x00000004
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_ALIGN       0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_BITS        1
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_SHIFT       2

/* MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS :: RX_HALT_ON_ERROR [01:01] */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_MASK     0x00000002
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_ALIGN    0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_BITS     1
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_SHIFT    1

/* MISC1 :: Y_RX_SW_DESC_LIST_CTRL_STS :: RX_RUN_STOP [00:00] */
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_MASK          0x00000001
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_ALIGN         0
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_BITS          1
#define MISC1_Y_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_SHIFT         0


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
 * MISC1 :: Y_RX_LIST0_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST0_CUR_DESC_L_ADDR :: RX_L0_CUR_DESC_L_ADDR [31:05] */
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_BITS 27
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: Y_RX_LIST0_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_reserved0_MASK            0x0000001f
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_reserved0_ALIGN           0
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_reserved0_BITS            5
#define MISC1_Y_RX_LIST0_CUR_DESC_L_ADDR_reserved0_SHIFT           0


/****************************************************************************
 * MISC1 :: Y_RX_LIST0_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST0_CUR_DESC_U_ADDR :: RX_L0_CUR_DESC_U_ADDR [31:00] */
#define MISC1_Y_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_Y_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_Y_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_BITS 32
#define MISC1_Y_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: Y_RX_LIST0_CUR_BYTE_CNT
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST0_CUR_BYTE_CNT :: RX_L0_CUR_BYTE_CNT [31:00] */
#define MISC1_Y_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_MASK      0xffffffff
#define MISC1_Y_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_ALIGN     0
#define MISC1_Y_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_BITS      32
#define MISC1_Y_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_SHIFT     0


/****************************************************************************
 * MISC1 :: Y_RX_LIST1_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST1_CUR_DESC_L_ADDR :: RX_L1_CUR_DESC_L_ADDR [31:05] */
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_BITS 27
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: Y_RX_LIST1_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_reserved0_MASK            0x0000001f
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_reserved0_ALIGN           0
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_reserved0_BITS            5
#define MISC1_Y_RX_LIST1_CUR_DESC_L_ADDR_reserved0_SHIFT           0


/****************************************************************************
 * MISC1 :: Y_RX_LIST1_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST1_CUR_DESC_U_ADDR :: RX_L1_CUR_DESC_U_ADDR [31:00] */
#define MISC1_Y_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_Y_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_Y_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_BITS 32
#define MISC1_Y_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: Y_RX_LIST1_CUR_BYTE_CNT
 ***************************************************************************/
/* MISC1 :: Y_RX_LIST1_CUR_BYTE_CNT :: RX_L1_CUR_BYTE_CNT [31:00] */
#define MISC1_Y_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_MASK      0xffffffff
#define MISC1_Y_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_ALIGN     0
#define MISC1_Y_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_BITS      32
#define MISC1_Y_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_SHIFT     0


/****************************************************************************
 * MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST0 :: DESC_ADDR [31:05] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_MASK         0xffffffe0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_BITS         27
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_DESC_ADDR_SHIFT        5

/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST0 :: reserved0 [04:01] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_MASK         0x0000001e
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_BITS         4
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_reserved0_SHIFT        1

/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST0 :: RX_DESC_LIST0_VALID [00:00] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_MASK 0x00000001
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_ALIGN 0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_BITS 1
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST0_RX_DESC_LIST0_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: UV_RX_FIRST_DESC_U_ADDR_LIST0
 ***************************************************************************/
/* MISC1 :: UV_RX_FIRST_DESC_U_ADDR_LIST0 :: DESC_ADDR [31:00] */
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_MASK         0xffffffff
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_BITS         32
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST0_DESC_ADDR_SHIFT        0


/****************************************************************************
 * MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST1 :: DESC_ADDR [31:05] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_MASK         0xffffffe0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_BITS         27
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_DESC_ADDR_SHIFT        5

/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST1 :: reserved0 [04:01] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_MASK         0x0000001e
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_BITS         4
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_reserved0_SHIFT        1

/* MISC1 :: UV_RX_FIRST_DESC_L_ADDR_LIST1 :: RX_DESC_LIST1_VALID [00:00] */
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_MASK 0x00000001
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_ALIGN 0
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_BITS 1
#define MISC1_UV_RX_FIRST_DESC_L_ADDR_LIST1_RX_DESC_LIST1_VALID_SHIFT 0


/****************************************************************************
 * MISC1 :: UV_RX_FIRST_DESC_U_ADDR_LIST1
 ***************************************************************************/
/* MISC1 :: UV_RX_FIRST_DESC_U_ADDR_LIST1 :: DESC_ADDR [31:00] */
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_MASK         0xffffffff
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_ALIGN        0
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_BITS         32
#define MISC1_UV_RX_FIRST_DESC_U_ADDR_LIST1_DESC_ADDR_SHIFT        0


/****************************************************************************
 * MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS
 ***************************************************************************/
/* MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS :: reserved0 [31:04] */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_reserved0_MASK           0xfffffff0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_reserved0_ALIGN          0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_reserved0_BITS           28
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_reserved0_SHIFT          4

/* MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS :: DMA_DATA_SERV_PTR [03:03] */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_MASK   0x00000008
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_ALIGN  0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_BITS   1
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DMA_DATA_SERV_PTR_SHIFT  3

/* MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS :: DESC_SERV_PTR [02:02] */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_MASK       0x00000004
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_ALIGN      0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_BITS       1
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_DESC_SERV_PTR_SHIFT      2

/* MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS :: RX_HALT_ON_ERROR [01:01] */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_MASK    0x00000002
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_ALIGN   0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_BITS    1
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_HALT_ON_ERROR_SHIFT   1

/* MISC1 :: UV_RX_SW_DESC_LIST_CTRL_STS :: RX_RUN_STOP [00:00] */
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_MASK         0x00000001
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_ALIGN        0
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_BITS         1
#define MISC1_UV_RX_SW_DESC_LIST_CTRL_STS_RX_RUN_STOP_SHIFT        0


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
 * MISC1 :: UV_RX_LIST0_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST0_CUR_DESC_L_ADDR :: RX_L0_CUR_DESC_L_ADDR [31:05] */
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_BITS 27
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_RX_L0_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: UV_RX_LIST0_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_reserved0_MASK           0x0000001f
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_reserved0_ALIGN          0
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_reserved0_BITS           5
#define MISC1_UV_RX_LIST0_CUR_DESC_L_ADDR_reserved0_SHIFT          0


/****************************************************************************
 * MISC1 :: UV_RX_LIST0_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST0_CUR_DESC_U_ADDR :: RX_L0_CUR_DESC_U_ADDR [31:00] */
#define MISC1_UV_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_UV_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_UV_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_BITS 32
#define MISC1_UV_RX_LIST0_CUR_DESC_U_ADDR_RX_L0_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: UV_RX_LIST0_CUR_BYTE_CNT
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST0_CUR_BYTE_CNT :: RX_L0_CUR_BYTE_CNT [31:00] */
#define MISC1_UV_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_MASK     0xffffffff
#define MISC1_UV_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_ALIGN    0
#define MISC1_UV_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_BITS     32
#define MISC1_UV_RX_LIST0_CUR_BYTE_CNT_RX_L0_CUR_BYTE_CNT_SHIFT    0


/****************************************************************************
 * MISC1 :: UV_RX_LIST1_CUR_DESC_L_ADDR
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST1_CUR_DESC_L_ADDR :: RX_L1_CUR_DESC_L_ADDR [31:05] */
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_MASK 0xffffffe0
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_ALIGN 0
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_BITS 27
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_RX_L1_CUR_DESC_L_ADDR_SHIFT 5

/* MISC1 :: UV_RX_LIST1_CUR_DESC_L_ADDR :: reserved0 [04:00] */
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_reserved0_MASK           0x0000001f
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_reserved0_ALIGN          0
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_reserved0_BITS           5
#define MISC1_UV_RX_LIST1_CUR_DESC_L_ADDR_reserved0_SHIFT          0


/****************************************************************************
 * MISC1 :: UV_RX_LIST1_CUR_DESC_U_ADDR
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST1_CUR_DESC_U_ADDR :: RX_L1_CUR_DESC_U_ADDR [31:00] */
#define MISC1_UV_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_MASK 0xffffffff
#define MISC1_UV_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_ALIGN 0
#define MISC1_UV_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_BITS 32
#define MISC1_UV_RX_LIST1_CUR_DESC_U_ADDR_RX_L1_CUR_DESC_U_ADDR_SHIFT 0


/****************************************************************************
 * MISC1 :: UV_RX_LIST1_CUR_BYTE_CNT
 ***************************************************************************/
/* MISC1 :: UV_RX_LIST1_CUR_BYTE_CNT :: RX_L1_CUR_BYTE_CNT [31:00] */
#define MISC1_UV_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_MASK     0xffffffff
#define MISC1_UV_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_ALIGN    0
#define MISC1_UV_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_BITS     32
#define MISC1_UV_RX_LIST1_CUR_BYTE_CNT_RX_L1_CUR_BYTE_CNT_SHIFT    0


/****************************************************************************
 * MISC1 :: DMA_DEBUG_OPTIONS_REG
 ***************************************************************************/
/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_TX_DMA_SOFT_RST [31:31] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_SOFT_RST_MASK 0x80000000
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_SOFT_RST_ALIGN 0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_SOFT_RST_BITS 1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_SOFT_RST_SHIFT 31

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_RX_DMA_SOFT_RST [30:30] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_SOFT_RST_MASK 0x40000000
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_SOFT_RST_ALIGN 0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_SOFT_RST_BITS 1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_SOFT_RST_SHIFT 30

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_TX_DMA_RD_Q_SOFT_RST [29:29] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_RD_Q_SOFT_RST_MASK 0x20000000
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_RD_Q_SOFT_RST_ALIGN 0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_RD_Q_SOFT_RST_BITS 1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_TX_DMA_RD_Q_SOFT_RST_SHIFT 29

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_RX_DMA_WR_Q_SOFT_RST [28:28] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_WR_Q_SOFT_RST_MASK 0x10000000
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_WR_Q_SOFT_RST_ALIGN 0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_WR_Q_SOFT_RST_BITS 1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_RX_DMA_WR_Q_SOFT_RST_SHIFT 28

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: RSVD_DMA_DEBUG_0 [27:05] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_0_MASK          0x0fffffe0
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_0_ALIGN         0
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_0_BITS          23
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_0_SHIFT         5

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_EN_RX_DMA_XFER_CNT [04:04] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_EN_RX_DMA_XFER_CNT_MASK 0x00000010
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_EN_RX_DMA_XFER_CNT_ALIGN 0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_EN_RX_DMA_XFER_CNT_BITS 1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_EN_RX_DMA_XFER_CNT_SHIFT 4

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: RSVD_DMA_DEBUG_1 [03:03] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_1_MASK          0x00000008
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_1_ALIGN         0
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_1_BITS          1
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_1_SHIFT         3

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_SINGLE_RD_Q [02:02] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_RD_Q_MASK     0x00000004
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_RD_Q_ALIGN    0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_RD_Q_BITS     1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_RD_Q_SHIFT    2

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: DMA_DEBUG_SINGLE_WR_Q [01:01] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_WR_Q_MASK     0x00000002
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_WR_Q_ALIGN    0
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_WR_Q_BITS     1
#define MISC1_DMA_DEBUG_OPTIONS_REG_DMA_DEBUG_SINGLE_WR_Q_SHIFT    1

/* MISC1 :: DMA_DEBUG_OPTIONS_REG :: RSVD_DMA_DEBUG_2 [00:00] */
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_2_MASK          0x00000001
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_2_ALIGN         0
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_2_BITS          1
#define MISC1_DMA_DEBUG_OPTIONS_REG_RSVD_DMA_DEBUG_2_SHIFT         0


/****************************************************************************
 * MISC1 :: READ_CHANNEL_ERROR_STATUS
 ***************************************************************************/
/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_7 [31:28] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_7_MASK     0xf0000000
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_7_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_7_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_7_SHIFT    28

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_6 [27:24] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_6_MASK     0x0f000000
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_6_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_6_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_6_SHIFT    24

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_5 [23:20] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_5_MASK     0x00f00000
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_5_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_5_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_5_SHIFT    20

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_4 [19:16] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_4_MASK     0x000f0000
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_4_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_4_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_4_SHIFT    16

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_3 [15:12] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_3_MASK     0x0000f000
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_3_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_3_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_3_SHIFT    12

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_2 [11:08] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_2_MASK     0x00000f00
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_2_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_2_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_2_SHIFT    8

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_1 [07:04] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_1_MASK     0x000000f0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_1_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_1_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_1_SHIFT    4

/* MISC1 :: READ_CHANNEL_ERROR_STATUS :: TX_ERR_STS_CHAN_0 [03:00] */
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_0_MASK     0x0000000f
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_0_ALIGN    0
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_0_BITS     4
#define MISC1_READ_CHANNEL_ERROR_STATUS_TX_ERR_STS_CHAN_0_SHIFT    0


/****************************************************************************
 * MISC1 :: PCIE_DMA_CTRL
 ***************************************************************************/
/* MISC1 :: PCIE_DMA_CTRL :: reserved0 [31:18] */
#define MISC1_PCIE_DMA_CTRL_reserved0_MASK                         0xfffc0000
#define MISC1_PCIE_DMA_CTRL_reserved0_ALIGN                        0
#define MISC1_PCIE_DMA_CTRL_reserved0_BITS                         14
#define MISC1_PCIE_DMA_CTRL_reserved0_SHIFT                        18

/* MISC1 :: PCIE_DMA_CTRL :: DESC_ENDIAN_MODE [17:16] */
#define MISC1_PCIE_DMA_CTRL_DESC_ENDIAN_MODE_MASK                  0x00030000
#define MISC1_PCIE_DMA_CTRL_DESC_ENDIAN_MODE_ALIGN                 0
#define MISC1_PCIE_DMA_CTRL_DESC_ENDIAN_MODE_BITS                  2
#define MISC1_PCIE_DMA_CTRL_DESC_ENDIAN_MODE_SHIFT                 16

/* MISC1 :: PCIE_DMA_CTRL :: reserved1 [15:10] */
#define MISC1_PCIE_DMA_CTRL_reserved1_MASK                         0x0000fc00
#define MISC1_PCIE_DMA_CTRL_reserved1_ALIGN                        0
#define MISC1_PCIE_DMA_CTRL_reserved1_BITS                         6
#define MISC1_PCIE_DMA_CTRL_reserved1_SHIFT                        10

/* MISC1 :: PCIE_DMA_CTRL :: EN_WEIGHTED_RR [09:09] */
#define MISC1_PCIE_DMA_CTRL_EN_WEIGHTED_RR_MASK                    0x00000200
#define MISC1_PCIE_DMA_CTRL_EN_WEIGHTED_RR_ALIGN                   0
#define MISC1_PCIE_DMA_CTRL_EN_WEIGHTED_RR_BITS                    1
#define MISC1_PCIE_DMA_CTRL_EN_WEIGHTED_RR_SHIFT                   9

/* MISC1 :: PCIE_DMA_CTRL :: EN_ROUND_ROBIN [08:08] */
#define MISC1_PCIE_DMA_CTRL_EN_ROUND_ROBIN_MASK                    0x00000100
#define MISC1_PCIE_DMA_CTRL_EN_ROUND_ROBIN_ALIGN                   0
#define MISC1_PCIE_DMA_CTRL_EN_ROUND_ROBIN_BITS                    1
#define MISC1_PCIE_DMA_CTRL_EN_ROUND_ROBIN_SHIFT                   8

/* MISC1 :: PCIE_DMA_CTRL :: reserved2 [07:05] */
#define MISC1_PCIE_DMA_CTRL_reserved2_MASK                         0x000000e0
#define MISC1_PCIE_DMA_CTRL_reserved2_ALIGN                        0
#define MISC1_PCIE_DMA_CTRL_reserved2_BITS                         3
#define MISC1_PCIE_DMA_CTRL_reserved2_SHIFT                        5

/* MISC1 :: PCIE_DMA_CTRL :: RELAXED_ORDERING [04:04] */
#define MISC1_PCIE_DMA_CTRL_RELAXED_ORDERING_MASK                  0x00000010
#define MISC1_PCIE_DMA_CTRL_RELAXED_ORDERING_ALIGN                 0
#define MISC1_PCIE_DMA_CTRL_RELAXED_ORDERING_BITS                  1
#define MISC1_PCIE_DMA_CTRL_RELAXED_ORDERING_SHIFT                 4

/* MISC1 :: PCIE_DMA_CTRL :: NO_SNOOP [03:03] */
#define MISC1_PCIE_DMA_CTRL_NO_SNOOP_MASK                          0x00000008
#define MISC1_PCIE_DMA_CTRL_NO_SNOOP_ALIGN                         0
#define MISC1_PCIE_DMA_CTRL_NO_SNOOP_BITS                          1
#define MISC1_PCIE_DMA_CTRL_NO_SNOOP_SHIFT                         3

/* MISC1 :: PCIE_DMA_CTRL :: TRAFFIC_CLASS [02:00] */
#define MISC1_PCIE_DMA_CTRL_TRAFFIC_CLASS_MASK                     0x00000007
#define MISC1_PCIE_DMA_CTRL_TRAFFIC_CLASS_ALIGN                    0
#define MISC1_PCIE_DMA_CTRL_TRAFFIC_CLASS_BITS                     3
#define MISC1_PCIE_DMA_CTRL_TRAFFIC_CLASS_SHIFT                    0


/****************************************************************************
 * BCM70012_MISC_TOP_MISC2
 ***************************************************************************/
/****************************************************************************
 * MISC2 :: GLOBAL_CTRL
 ***************************************************************************/
/* MISC2 :: GLOBAL_CTRL :: reserved0 [31:21] */
#define MISC2_GLOBAL_CTRL_reserved0_MASK                           0xffe00000
#define MISC2_GLOBAL_CTRL_reserved0_ALIGN                          0
#define MISC2_GLOBAL_CTRL_reserved0_BITS                           11
#define MISC2_GLOBAL_CTRL_reserved0_SHIFT                          21

/* MISC2 :: GLOBAL_CTRL :: EN_WRITE_ALL [20:20] */
#define MISC2_GLOBAL_CTRL_EN_WRITE_ALL_MASK                        0x00100000
#define MISC2_GLOBAL_CTRL_EN_WRITE_ALL_ALIGN                       0
#define MISC2_GLOBAL_CTRL_EN_WRITE_ALL_BITS                        1
#define MISC2_GLOBAL_CTRL_EN_WRITE_ALL_SHIFT                       20

/* MISC2 :: GLOBAL_CTRL :: reserved1 [19:17] */
#define MISC2_GLOBAL_CTRL_reserved1_MASK                           0x000e0000
#define MISC2_GLOBAL_CTRL_reserved1_ALIGN                          0
#define MISC2_GLOBAL_CTRL_reserved1_BITS                           3
#define MISC2_GLOBAL_CTRL_reserved1_SHIFT                          17

/* MISC2 :: GLOBAL_CTRL :: EN_SINGLE_DMA [16:16] */
#define MISC2_GLOBAL_CTRL_EN_SINGLE_DMA_MASK                       0x00010000
#define MISC2_GLOBAL_CTRL_EN_SINGLE_DMA_ALIGN                      0
#define MISC2_GLOBAL_CTRL_EN_SINGLE_DMA_BITS                       1
#define MISC2_GLOBAL_CTRL_EN_SINGLE_DMA_SHIFT                      16

/* MISC2 :: GLOBAL_CTRL :: reserved2 [15:11] */
#define MISC2_GLOBAL_CTRL_reserved2_MASK                           0x0000f800
#define MISC2_GLOBAL_CTRL_reserved2_ALIGN                          0
#define MISC2_GLOBAL_CTRL_reserved2_BITS                           5
#define MISC2_GLOBAL_CTRL_reserved2_SHIFT                          11

/* MISC2 :: GLOBAL_CTRL :: Y_UV_FIFO_LEN_SEL [10:10] */
#define MISC2_GLOBAL_CTRL_Y_UV_FIFO_LEN_SEL_MASK                   0x00000400
#define MISC2_GLOBAL_CTRL_Y_UV_FIFO_LEN_SEL_ALIGN                  0
#define MISC2_GLOBAL_CTRL_Y_UV_FIFO_LEN_SEL_BITS                   1
#define MISC2_GLOBAL_CTRL_Y_UV_FIFO_LEN_SEL_SHIFT                  10

/* MISC2 :: GLOBAL_CTRL :: ODD_DE_EOFRST_DIS [09:09] */
#define MISC2_GLOBAL_CTRL_ODD_DE_EOFRST_DIS_MASK                   0x00000200
#define MISC2_GLOBAL_CTRL_ODD_DE_EOFRST_DIS_ALIGN                  0
#define MISC2_GLOBAL_CTRL_ODD_DE_EOFRST_DIS_BITS                   1
#define MISC2_GLOBAL_CTRL_ODD_DE_EOFRST_DIS_SHIFT                  9

/* MISC2 :: GLOBAL_CTRL :: DIS_BIT_STUFFING [08:08] */
#define MISC2_GLOBAL_CTRL_DIS_BIT_STUFFING_MASK                    0x00000100
#define MISC2_GLOBAL_CTRL_DIS_BIT_STUFFING_ALIGN                   0
#define MISC2_GLOBAL_CTRL_DIS_BIT_STUFFING_BITS                    1
#define MISC2_GLOBAL_CTRL_DIS_BIT_STUFFING_SHIFT                   8

/* MISC2 :: GLOBAL_CTRL :: reserved3 [07:05] */
#define MISC2_GLOBAL_CTRL_reserved3_MASK                           0x000000e0
#define MISC2_GLOBAL_CTRL_reserved3_ALIGN                          0
#define MISC2_GLOBAL_CTRL_reserved3_BITS                           3
#define MISC2_GLOBAL_CTRL_reserved3_SHIFT                          5

/* MISC2 :: GLOBAL_CTRL :: EN_PROG_MODE [04:04] */
#define MISC2_GLOBAL_CTRL_EN_PROG_MODE_MASK                        0x00000010
#define MISC2_GLOBAL_CTRL_EN_PROG_MODE_ALIGN                       0
#define MISC2_GLOBAL_CTRL_EN_PROG_MODE_BITS                        1
#define MISC2_GLOBAL_CTRL_EN_PROG_MODE_SHIFT                       4

/* MISC2 :: GLOBAL_CTRL :: reserved4 [03:01] */
#define MISC2_GLOBAL_CTRL_reserved4_MASK                           0x0000000e
#define MISC2_GLOBAL_CTRL_reserved4_ALIGN                          0
#define MISC2_GLOBAL_CTRL_reserved4_BITS                           3
#define MISC2_GLOBAL_CTRL_reserved4_SHIFT                          1

/* MISC2 :: GLOBAL_CTRL :: EN_188B [00:00] */
#define MISC2_GLOBAL_CTRL_EN_188B_MASK                             0x00000001
#define MISC2_GLOBAL_CTRL_EN_188B_ALIGN                            0
#define MISC2_GLOBAL_CTRL_EN_188B_BITS                             1
#define MISC2_GLOBAL_CTRL_EN_188B_SHIFT                            0


/****************************************************************************
 * MISC2 :: INTERNAL_STATUS
 ***************************************************************************/
/* MISC2 :: INTERNAL_STATUS :: reserved0 [31:12] */
#define MISC2_INTERNAL_STATUS_reserved0_MASK                       0xfffff000
#define MISC2_INTERNAL_STATUS_reserved0_ALIGN                      0
#define MISC2_INTERNAL_STATUS_reserved0_BITS                       20
#define MISC2_INTERNAL_STATUS_reserved0_SHIFT                      12

/* MISC2 :: INTERNAL_STATUS :: UV_BYTE_COUNT_FIFO_FULL [11:11] */
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_FULL_MASK         0x00000800
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_FULL_ALIGN        0
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_FULL_BITS         1
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_FULL_SHIFT        11

/* MISC2 :: INTERNAL_STATUS :: UV_DATA_FIFO_FULL [10:10] */
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_FULL_MASK               0x00000400
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_FULL_ALIGN              0
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_FULL_BITS               1
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_FULL_SHIFT              10

/* MISC2 :: INTERNAL_STATUS :: Y_BYTE_COUNT_FIFO_FULL [09:09] */
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_FULL_MASK          0x00000200
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_FULL_ALIGN         0
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_FULL_BITS          1
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_FULL_SHIFT         9

/* MISC2 :: INTERNAL_STATUS :: Y_DATA_FIFO_FULL [08:08] */
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_FULL_MASK                0x00000100
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_FULL_ALIGN               0
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_FULL_BITS                1
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_FULL_SHIFT               8

/* MISC2 :: INTERNAL_STATUS :: UV_BYTE_COUNT_FIFO_EMPTY [07:07] */
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_EMPTY_MASK        0x00000080
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_EMPTY_ALIGN       0
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_EMPTY_BITS        1
#define MISC2_INTERNAL_STATUS_UV_BYTE_COUNT_FIFO_EMPTY_SHIFT       7

/* MISC2 :: INTERNAL_STATUS :: UV_DATA_FIFO_EMPTY [06:06] */
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_EMPTY_MASK              0x00000040
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_EMPTY_ALIGN             0
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_EMPTY_BITS              1
#define MISC2_INTERNAL_STATUS_UV_DATA_FIFO_EMPTY_SHIFT             6

/* MISC2 :: INTERNAL_STATUS :: Y_BYTE_COUNT_FIFO_EMPTY [05:05] */
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_EMPTY_MASK         0x00000020
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_EMPTY_ALIGN        0
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_EMPTY_BITS         1
#define MISC2_INTERNAL_STATUS_Y_BYTE_COUNT_FIFO_EMPTY_SHIFT        5

/* MISC2 :: INTERNAL_STATUS :: Y_DATA_FIFO_EMPTY [04:04] */
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_EMPTY_MASK               0x00000010
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_EMPTY_ALIGN              0
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_EMPTY_BITS               1
#define MISC2_INTERNAL_STATUS_Y_DATA_FIFO_EMPTY_SHIFT              4

/* MISC2 :: INTERNAL_STATUS :: reserved1 [03:00] */
#define MISC2_INTERNAL_STATUS_reserved1_MASK                       0x0000000f
#define MISC2_INTERNAL_STATUS_reserved1_ALIGN                      0
#define MISC2_INTERNAL_STATUS_reserved1_BITS                       4
#define MISC2_INTERNAL_STATUS_reserved1_SHIFT                      0


/****************************************************************************
 * MISC2 :: INTERNAL_STATUS_MUX_CTRL
 ***************************************************************************/
/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: reserved0 [31:16] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved0_MASK              0xffff0000
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved0_ALIGN             0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved0_BITS              16
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved0_SHIFT             16

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: CLK_OUT_ALT_SRC [15:15] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_CLK_OUT_ALT_SRC_MASK        0x00008000
#define MISC2_INTERNAL_STATUS_MUX_CTRL_CLK_OUT_ALT_SRC_ALIGN       0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_CLK_OUT_ALT_SRC_BITS        1
#define MISC2_INTERNAL_STATUS_MUX_CTRL_CLK_OUT_ALT_SRC_SHIFT       15

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: DEBUG_CLK_SEL [14:12] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CLK_SEL_MASK          0x00007000
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CLK_SEL_ALIGN         0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CLK_SEL_BITS          3
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CLK_SEL_SHIFT         12

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: reserved1 [11:09] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved1_MASK              0x00000e00
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved1_ALIGN             0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved1_BITS              3
#define MISC2_INTERNAL_STATUS_MUX_CTRL_reserved1_SHIFT             9

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: DEBUG_TOP_CORE_SEL [08:08] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_TOP_CORE_SEL_MASK     0x00000100
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_TOP_CORE_SEL_ALIGN    0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_TOP_CORE_SEL_BITS     1
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_TOP_CORE_SEL_SHIFT    8

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: DEBUG_CORE_BLK_SEL [07:04] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CORE_BLK_SEL_MASK     0x000000f0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CORE_BLK_SEL_ALIGN    0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CORE_BLK_SEL_BITS     4
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_CORE_BLK_SEL_SHIFT    4

/* MISC2 :: INTERNAL_STATUS_MUX_CTRL :: DEBUG_VECTOR_SEL [03:00] */
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_VECTOR_SEL_MASK       0x0000000f
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_VECTOR_SEL_ALIGN      0
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_VECTOR_SEL_BITS       4
#define MISC2_INTERNAL_STATUS_MUX_CTRL_DEBUG_VECTOR_SEL_SHIFT      0


/****************************************************************************
 * MISC2 :: DEBUG_FIFO_LENGTH
 ***************************************************************************/
/* MISC2 :: DEBUG_FIFO_LENGTH :: reserved0 [31:21] */
#define MISC2_DEBUG_FIFO_LENGTH_reserved0_MASK                     0xffe00000
#define MISC2_DEBUG_FIFO_LENGTH_reserved0_ALIGN                    0
#define MISC2_DEBUG_FIFO_LENGTH_reserved0_BITS                     11
#define MISC2_DEBUG_FIFO_LENGTH_reserved0_SHIFT                    21

/* MISC2 :: DEBUG_FIFO_LENGTH :: FIFO_LENGTH [20:00] */
#define MISC2_DEBUG_FIFO_LENGTH_FIFO_LENGTH_MASK                   0x001fffff
#define MISC2_DEBUG_FIFO_LENGTH_FIFO_LENGTH_ALIGN                  0
#define MISC2_DEBUG_FIFO_LENGTH_FIFO_LENGTH_BITS                   21
#define MISC2_DEBUG_FIFO_LENGTH_FIFO_LENGTH_SHIFT                  0


/****************************************************************************
 * BCM70012_MISC_TOP_MISC3
 ***************************************************************************/
/****************************************************************************
 * MISC3 :: RESET_CTRL
 ***************************************************************************/
/* MISC3 :: RESET_CTRL :: reserved0 [31:09] */
#define MISC3_RESET_CTRL_reserved0_MASK                            0xfffffe00
#define MISC3_RESET_CTRL_reserved0_ALIGN                           0
#define MISC3_RESET_CTRL_reserved0_BITS                            23
#define MISC3_RESET_CTRL_reserved0_SHIFT                           9

/* MISC3 :: RESET_CTRL :: PLL_RESET [08:08] */
#define MISC3_RESET_CTRL_PLL_RESET_MASK                            0x00000100
#define MISC3_RESET_CTRL_PLL_RESET_ALIGN                           0
#define MISC3_RESET_CTRL_PLL_RESET_BITS                            1
#define MISC3_RESET_CTRL_PLL_RESET_SHIFT                           8

/* MISC3 :: RESET_CTRL :: reserved1 [07:02] */
#define MISC3_RESET_CTRL_reserved1_MASK                            0x000000fc
#define MISC3_RESET_CTRL_reserved1_ALIGN                           0
#define MISC3_RESET_CTRL_reserved1_BITS                            6
#define MISC3_RESET_CTRL_reserved1_SHIFT                           2

/* MISC3 :: RESET_CTRL :: POR_RESET [01:01] */
#define MISC3_RESET_CTRL_POR_RESET_MASK                            0x00000002
#define MISC3_RESET_CTRL_POR_RESET_ALIGN                           0
#define MISC3_RESET_CTRL_POR_RESET_BITS                            1
#define MISC3_RESET_CTRL_POR_RESET_SHIFT                           1

/* MISC3 :: RESET_CTRL :: CORE_RESET [00:00] */
#define MISC3_RESET_CTRL_CORE_RESET_MASK                           0x00000001
#define MISC3_RESET_CTRL_CORE_RESET_ALIGN                          0
#define MISC3_RESET_CTRL_CORE_RESET_BITS                           1
#define MISC3_RESET_CTRL_CORE_RESET_SHIFT                          0


/****************************************************************************
 * MISC3 :: BIST_CTRL
 ***************************************************************************/
/* MISC3 :: BIST_CTRL :: MBIST_OVERRIDE [31:31] */
#define MISC3_BIST_CTRL_MBIST_OVERRIDE_MASK                        0x80000000
#define MISC3_BIST_CTRL_MBIST_OVERRIDE_ALIGN                       0
#define MISC3_BIST_CTRL_MBIST_OVERRIDE_BITS                        1
#define MISC3_BIST_CTRL_MBIST_OVERRIDE_SHIFT                       31

/* MISC3 :: BIST_CTRL :: reserved0 [30:15] */
#define MISC3_BIST_CTRL_reserved0_MASK                             0x7fff8000
#define MISC3_BIST_CTRL_reserved0_ALIGN                            0
#define MISC3_BIST_CTRL_reserved0_BITS                             16
#define MISC3_BIST_CTRL_reserved0_SHIFT                            15

/* MISC3 :: BIST_CTRL :: MBIST_EN_2 [14:14] */
#define MISC3_BIST_CTRL_MBIST_EN_2_MASK                            0x00004000
#define MISC3_BIST_CTRL_MBIST_EN_2_ALIGN                           0
#define MISC3_BIST_CTRL_MBIST_EN_2_BITS                            1
#define MISC3_BIST_CTRL_MBIST_EN_2_SHIFT                           14

/* MISC3 :: BIST_CTRL :: MBIST_EN_1 [13:13] */
#define MISC3_BIST_CTRL_MBIST_EN_1_MASK                            0x00002000
#define MISC3_BIST_CTRL_MBIST_EN_1_ALIGN                           0
#define MISC3_BIST_CTRL_MBIST_EN_1_BITS                            1
#define MISC3_BIST_CTRL_MBIST_EN_1_SHIFT                           13

/* MISC3 :: BIST_CTRL :: MBIST_EN_0 [12:12] */
#define MISC3_BIST_CTRL_MBIST_EN_0_MASK                            0x00001000
#define MISC3_BIST_CTRL_MBIST_EN_0_ALIGN                           0
#define MISC3_BIST_CTRL_MBIST_EN_0_BITS                            1
#define MISC3_BIST_CTRL_MBIST_EN_0_SHIFT                           12

/* MISC3 :: BIST_CTRL :: reserved1 [11:06] */
#define MISC3_BIST_CTRL_reserved1_MASK                             0x00000fc0
#define MISC3_BIST_CTRL_reserved1_ALIGN                            0
#define MISC3_BIST_CTRL_reserved1_BITS                             6
#define MISC3_BIST_CTRL_reserved1_SHIFT                            6

/* MISC3 :: BIST_CTRL :: MBIST_SETUP [05:04] */
#define MISC3_BIST_CTRL_MBIST_SETUP_MASK                           0x00000030
#define MISC3_BIST_CTRL_MBIST_SETUP_ALIGN                          0
#define MISC3_BIST_CTRL_MBIST_SETUP_BITS                           2
#define MISC3_BIST_CTRL_MBIST_SETUP_SHIFT                          4

/* MISC3 :: BIST_CTRL :: reserved2 [03:01] */
#define MISC3_BIST_CTRL_reserved2_MASK                             0x0000000e
#define MISC3_BIST_CTRL_reserved2_ALIGN                            0
#define MISC3_BIST_CTRL_reserved2_BITS                             3
#define MISC3_BIST_CTRL_reserved2_SHIFT                            1

/* MISC3 :: BIST_CTRL :: MBIST_ASYNC_RESET [00:00] */
#define MISC3_BIST_CTRL_MBIST_ASYNC_RESET_MASK                     0x00000001
#define MISC3_BIST_CTRL_MBIST_ASYNC_RESET_ALIGN                    0
#define MISC3_BIST_CTRL_MBIST_ASYNC_RESET_BITS                     1
#define MISC3_BIST_CTRL_MBIST_ASYNC_RESET_SHIFT                    0


/****************************************************************************
 * MISC3 :: BIST_STATUS
 ***************************************************************************/
/* MISC3 :: BIST_STATUS :: reserved0 [31:31] */
#define MISC3_BIST_STATUS_reserved0_MASK                           0x80000000
#define MISC3_BIST_STATUS_reserved0_ALIGN                          0
#define MISC3_BIST_STATUS_reserved0_BITS                           1
#define MISC3_BIST_STATUS_reserved0_SHIFT                          31

/* MISC3 :: BIST_STATUS :: MBIST_GO_2 [30:30] */
#define MISC3_BIST_STATUS_MBIST_GO_2_MASK                          0x40000000
#define MISC3_BIST_STATUS_MBIST_GO_2_ALIGN                         0
#define MISC3_BIST_STATUS_MBIST_GO_2_BITS                          1
#define MISC3_BIST_STATUS_MBIST_GO_2_SHIFT                         30

/* MISC3 :: BIST_STATUS :: MBIST_GO_1 [29:29] */
#define MISC3_BIST_STATUS_MBIST_GO_1_MASK                          0x20000000
#define MISC3_BIST_STATUS_MBIST_GO_1_ALIGN                         0
#define MISC3_BIST_STATUS_MBIST_GO_1_BITS                          1
#define MISC3_BIST_STATUS_MBIST_GO_1_SHIFT                         29

/* MISC3 :: BIST_STATUS :: MBIST_GO_0 [28:28] */
#define MISC3_BIST_STATUS_MBIST_GO_0_MASK                          0x10000000
#define MISC3_BIST_STATUS_MBIST_GO_0_ALIGN                         0
#define MISC3_BIST_STATUS_MBIST_GO_0_BITS                          1
#define MISC3_BIST_STATUS_MBIST_GO_0_SHIFT                         28

/* MISC3 :: BIST_STATUS :: reserved1 [27:27] */
#define MISC3_BIST_STATUS_reserved1_MASK                           0x08000000
#define MISC3_BIST_STATUS_reserved1_ALIGN                          0
#define MISC3_BIST_STATUS_reserved1_BITS                           1
#define MISC3_BIST_STATUS_reserved1_SHIFT                          27

/* MISC3 :: BIST_STATUS :: MBIST_DONE_2 [26:26] */
#define MISC3_BIST_STATUS_MBIST_DONE_2_MASK                        0x04000000
#define MISC3_BIST_STATUS_MBIST_DONE_2_ALIGN                       0
#define MISC3_BIST_STATUS_MBIST_DONE_2_BITS                        1
#define MISC3_BIST_STATUS_MBIST_DONE_2_SHIFT                       26

/* MISC3 :: BIST_STATUS :: MBIST_DONE_1 [25:25] */
#define MISC3_BIST_STATUS_MBIST_DONE_1_MASK                        0x02000000
#define MISC3_BIST_STATUS_MBIST_DONE_1_ALIGN                       0
#define MISC3_BIST_STATUS_MBIST_DONE_1_BITS                        1
#define MISC3_BIST_STATUS_MBIST_DONE_1_SHIFT                       25

/* MISC3 :: BIST_STATUS :: MBIST_DONE_0 [24:24] */
#define MISC3_BIST_STATUS_MBIST_DONE_0_MASK                        0x01000000
#define MISC3_BIST_STATUS_MBIST_DONE_0_ALIGN                       0
#define MISC3_BIST_STATUS_MBIST_DONE_0_BITS                        1
#define MISC3_BIST_STATUS_MBIST_DONE_0_SHIFT                       24

/* MISC3 :: BIST_STATUS :: reserved2 [23:06] */
#define MISC3_BIST_STATUS_reserved2_MASK                           0x00ffffc0
#define MISC3_BIST_STATUS_reserved2_ALIGN                          0
#define MISC3_BIST_STATUS_reserved2_BITS                           18
#define MISC3_BIST_STATUS_reserved2_SHIFT                          6

/* MISC3 :: BIST_STATUS :: MBIST_MBIST_MEMORY_GO_2 [05:04] */
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_2_MASK             0x00000030
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_2_ALIGN            0
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_2_BITS             2
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_2_SHIFT            4

/* MISC3 :: BIST_STATUS :: MBIST_MBIST_MEMORY_GO_1 [03:02] */
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_1_MASK             0x0000000c
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_1_ALIGN            0
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_1_BITS             2
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_1_SHIFT            2

/* MISC3 :: BIST_STATUS :: MBIST_MBIST_MEMORY_GO_0 [01:00] */
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_0_MASK             0x00000003
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_0_ALIGN            0
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_0_BITS             2
#define MISC3_BIST_STATUS_MBIST_MBIST_MEMORY_GO_0_SHIFT            0


/****************************************************************************
 * MISC3 :: RX_CHECKSUM
 ***************************************************************************/
/* MISC3 :: RX_CHECKSUM :: RX_CHECKSUM [31:00] */
#define MISC3_RX_CHECKSUM_RX_CHECKSUM_MASK                         0xffffffff
#define MISC3_RX_CHECKSUM_RX_CHECKSUM_ALIGN                        0
#define MISC3_RX_CHECKSUM_RX_CHECKSUM_BITS                         32
#define MISC3_RX_CHECKSUM_RX_CHECKSUM_SHIFT                        0


/****************************************************************************
 * MISC3 :: TX_CHECKSUM
 ***************************************************************************/
/* MISC3 :: TX_CHECKSUM :: TX_CHECKSUM [31:00] */
#define MISC3_TX_CHECKSUM_TX_CHECKSUM_MASK                         0xffffffff
#define MISC3_TX_CHECKSUM_TX_CHECKSUM_ALIGN                        0
#define MISC3_TX_CHECKSUM_TX_CHECKSUM_BITS                         32
#define MISC3_TX_CHECKSUM_TX_CHECKSUM_SHIFT                        0


/****************************************************************************
 * MISC3 :: ECO_CTRL_CORE
 ***************************************************************************/
/* MISC3 :: ECO_CTRL_CORE :: reserved0 [31:16] */
#define MISC3_ECO_CTRL_CORE_reserved0_MASK                         0xffff0000
#define MISC3_ECO_CTRL_CORE_reserved0_ALIGN                        0
#define MISC3_ECO_CTRL_CORE_reserved0_BITS                         16
#define MISC3_ECO_CTRL_CORE_reserved0_SHIFT                        16

/* MISC3 :: ECO_CTRL_CORE :: ECO_CORE_RST_N [15:00] */
#define MISC3_ECO_CTRL_CORE_ECO_CORE_RST_N_MASK                    0x0000ffff
#define MISC3_ECO_CTRL_CORE_ECO_CORE_RST_N_ALIGN                   0
#define MISC3_ECO_CTRL_CORE_ECO_CORE_RST_N_BITS                    16
#define MISC3_ECO_CTRL_CORE_ECO_CORE_RST_N_SHIFT                   0


/****************************************************************************
 * MISC3 :: CSI_TEST_CTRL
 ***************************************************************************/
/* MISC3 :: CSI_TEST_CTRL :: ENABLE_CSI_TEST [31:31] */
#define MISC3_CSI_TEST_CTRL_ENABLE_CSI_TEST_MASK                   0x80000000
#define MISC3_CSI_TEST_CTRL_ENABLE_CSI_TEST_ALIGN                  0
#define MISC3_CSI_TEST_CTRL_ENABLE_CSI_TEST_BITS                   1
#define MISC3_CSI_TEST_CTRL_ENABLE_CSI_TEST_SHIFT                  31

/* MISC3 :: CSI_TEST_CTRL :: reserved0 [30:24] */
#define MISC3_CSI_TEST_CTRL_reserved0_MASK                         0x7f000000
#define MISC3_CSI_TEST_CTRL_reserved0_ALIGN                        0
#define MISC3_CSI_TEST_CTRL_reserved0_BITS                         7
#define MISC3_CSI_TEST_CTRL_reserved0_SHIFT                        24

/* MISC3 :: CSI_TEST_CTRL :: CSI_CLOCK_ENABLE [23:16] */
#define MISC3_CSI_TEST_CTRL_CSI_CLOCK_ENABLE_MASK                  0x00ff0000
#define MISC3_CSI_TEST_CTRL_CSI_CLOCK_ENABLE_ALIGN                 0
#define MISC3_CSI_TEST_CTRL_CSI_CLOCK_ENABLE_BITS                  8
#define MISC3_CSI_TEST_CTRL_CSI_CLOCK_ENABLE_SHIFT                 16

/* MISC3 :: CSI_TEST_CTRL :: CSI_SYNC [15:08] */
#define MISC3_CSI_TEST_CTRL_CSI_SYNC_MASK                          0x0000ff00
#define MISC3_CSI_TEST_CTRL_CSI_SYNC_ALIGN                         0
#define MISC3_CSI_TEST_CTRL_CSI_SYNC_BITS                          8
#define MISC3_CSI_TEST_CTRL_CSI_SYNC_SHIFT                         8

/* MISC3 :: CSI_TEST_CTRL :: CSI_DATA [07:00] */
#define MISC3_CSI_TEST_CTRL_CSI_DATA_MASK                          0x000000ff
#define MISC3_CSI_TEST_CTRL_CSI_DATA_ALIGN                         0
#define MISC3_CSI_TEST_CTRL_CSI_DATA_BITS                          8
#define MISC3_CSI_TEST_CTRL_CSI_DATA_SHIFT                         0


/****************************************************************************
 * MISC3 :: HD_DVI_TEST_CTRL
 ***************************************************************************/
/* MISC3 :: HD_DVI_TEST_CTRL :: reserved0 [31:25] */
#define MISC3_HD_DVI_TEST_CTRL_reserved0_MASK                      0xfe000000
#define MISC3_HD_DVI_TEST_CTRL_reserved0_ALIGN                     0
#define MISC3_HD_DVI_TEST_CTRL_reserved0_BITS                      7
#define MISC3_HD_DVI_TEST_CTRL_reserved0_SHIFT                     25

/* MISC3 :: HD_DVI_TEST_CTRL :: POS_VIDO_VBLANK_N [24:24] */
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_VBLANK_N_MASK              0x01000000
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_VBLANK_N_ALIGN             0
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_VBLANK_N_BITS              1
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_VBLANK_N_SHIFT             24

/* MISC3 :: HD_DVI_TEST_CTRL :: NEG_VIDO_DVI_DATA [23:12] */
#define MISC3_HD_DVI_TEST_CTRL_NEG_VIDO_DVI_DATA_MASK              0x00fff000
#define MISC3_HD_DVI_TEST_CTRL_NEG_VIDO_DVI_DATA_ALIGN             0
#define MISC3_HD_DVI_TEST_CTRL_NEG_VIDO_DVI_DATA_BITS              12
#define MISC3_HD_DVI_TEST_CTRL_NEG_VIDO_DVI_DATA_SHIFT             12

/* MISC3 :: HD_DVI_TEST_CTRL :: POS_VIDO_DVI_DATA [11:00] */
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_DVI_DATA_MASK              0x00000fff
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_DVI_DATA_ALIGN             0
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_DVI_DATA_BITS              12
#define MISC3_HD_DVI_TEST_CTRL_POS_VIDO_DVI_DATA_SHIFT             0


/****************************************************************************
 * BCM70012_MISC_TOP_MISC_PERST
 ***************************************************************************/
/****************************************************************************
 * MISC_PERST :: ECO_CTRL_PERST
 ***************************************************************************/
/* MISC_PERST :: ECO_CTRL_PERST :: reserved0 [31:16] */
#define MISC_PERST_ECO_CTRL_PERST_reserved0_MASK                   0xffff0000
#define MISC_PERST_ECO_CTRL_PERST_reserved0_ALIGN                  0
#define MISC_PERST_ECO_CTRL_PERST_reserved0_BITS                   16
#define MISC_PERST_ECO_CTRL_PERST_reserved0_SHIFT                  16

/* MISC_PERST :: ECO_CTRL_PERST :: ECO_PERST_N [15:00] */
#define MISC_PERST_ECO_CTRL_PERST_ECO_PERST_N_MASK                 0x0000ffff
#define MISC_PERST_ECO_CTRL_PERST_ECO_PERST_N_ALIGN                0
#define MISC_PERST_ECO_CTRL_PERST_ECO_PERST_N_BITS                 16
#define MISC_PERST_ECO_CTRL_PERST_ECO_PERST_N_SHIFT                0


/****************************************************************************
 * MISC_PERST :: DECODER_CTRL
 ***************************************************************************/
/* MISC_PERST :: DECODER_CTRL :: reserved0 [31:05] */
#define MISC_PERST_DECODER_CTRL_reserved0_MASK                     0xffffffe0
#define MISC_PERST_DECODER_CTRL_reserved0_ALIGN                    0
#define MISC_PERST_DECODER_CTRL_reserved0_BITS                     27
#define MISC_PERST_DECODER_CTRL_reserved0_SHIFT                    5

/* MISC_PERST :: DECODER_CTRL :: STOP_BCM7412_CLK [04:04] */
#define MISC_PERST_DECODER_CTRL_STOP_BCM7412_CLK_MASK              0x00000010
#define MISC_PERST_DECODER_CTRL_STOP_BCM7412_CLK_ALIGN             0
#define MISC_PERST_DECODER_CTRL_STOP_BCM7412_CLK_BITS              1
#define MISC_PERST_DECODER_CTRL_STOP_BCM7412_CLK_SHIFT             4

/* MISC_PERST :: DECODER_CTRL :: reserved1 [03:01] */
#define MISC_PERST_DECODER_CTRL_reserved1_MASK                     0x0000000e
#define MISC_PERST_DECODER_CTRL_reserved1_ALIGN                    0
#define MISC_PERST_DECODER_CTRL_reserved1_BITS                     3
#define MISC_PERST_DECODER_CTRL_reserved1_SHIFT                    1

/* MISC_PERST :: DECODER_CTRL :: BCM7412_RESET [00:00] */
#define MISC_PERST_DECODER_CTRL_BCM7412_RESET_MASK                 0x00000001
#define MISC_PERST_DECODER_CTRL_BCM7412_RESET_ALIGN                0
#define MISC_PERST_DECODER_CTRL_BCM7412_RESET_BITS                 1
#define MISC_PERST_DECODER_CTRL_BCM7412_RESET_SHIFT                0


/****************************************************************************
 * MISC_PERST :: CCE_STATUS
 ***************************************************************************/
/* MISC_PERST :: CCE_STATUS :: CCE_DONE [31:31] */
#define MISC_PERST_CCE_STATUS_CCE_DONE_MASK                        0x80000000
#define MISC_PERST_CCE_STATUS_CCE_DONE_ALIGN                       0
#define MISC_PERST_CCE_STATUS_CCE_DONE_BITS                        1
#define MISC_PERST_CCE_STATUS_CCE_DONE_SHIFT                       31

/* MISC_PERST :: CCE_STATUS :: reserved0 [30:03] */
#define MISC_PERST_CCE_STATUS_reserved0_MASK                       0x7ffffff8
#define MISC_PERST_CCE_STATUS_reserved0_ALIGN                      0
#define MISC_PERST_CCE_STATUS_reserved0_BITS                       28
#define MISC_PERST_CCE_STATUS_reserved0_SHIFT                      3

/* MISC_PERST :: CCE_STATUS :: CCE_BAD_GISB_ACCESS [02:02] */
#define MISC_PERST_CCE_STATUS_CCE_BAD_GISB_ACCESS_MASK             0x00000004
#define MISC_PERST_CCE_STATUS_CCE_BAD_GISB_ACCESS_ALIGN            0
#define MISC_PERST_CCE_STATUS_CCE_BAD_GISB_ACCESS_BITS             1
#define MISC_PERST_CCE_STATUS_CCE_BAD_GISB_ACCESS_SHIFT            2

/* MISC_PERST :: CCE_STATUS :: CCE_BAD_I2C_ACCESS [01:01] */
#define MISC_PERST_CCE_STATUS_CCE_BAD_I2C_ACCESS_MASK              0x00000002
#define MISC_PERST_CCE_STATUS_CCE_BAD_I2C_ACCESS_ALIGN             0
#define MISC_PERST_CCE_STATUS_CCE_BAD_I2C_ACCESS_BITS              1
#define MISC_PERST_CCE_STATUS_CCE_BAD_I2C_ACCESS_SHIFT             1

/* MISC_PERST :: CCE_STATUS :: CCE_BAD_SECTION_ID [00:00] */
#define MISC_PERST_CCE_STATUS_CCE_BAD_SECTION_ID_MASK              0x00000001
#define MISC_PERST_CCE_STATUS_CCE_BAD_SECTION_ID_ALIGN             0
#define MISC_PERST_CCE_STATUS_CCE_BAD_SECTION_ID_BITS              1
#define MISC_PERST_CCE_STATUS_CCE_BAD_SECTION_ID_SHIFT             0


/****************************************************************************
 * MISC_PERST :: PCIE_DEBUG
 ***************************************************************************/
/* MISC_PERST :: PCIE_DEBUG :: SERDES_TERM_CNT [31:16] */
#define MISC_PERST_PCIE_DEBUG_SERDES_TERM_CNT_MASK                 0xffff0000
#define MISC_PERST_PCIE_DEBUG_SERDES_TERM_CNT_ALIGN                0
#define MISC_PERST_PCIE_DEBUG_SERDES_TERM_CNT_BITS                 16
#define MISC_PERST_PCIE_DEBUG_SERDES_TERM_CNT_SHIFT                16

/* MISC_PERST :: PCIE_DEBUG :: reserved0 [15:11] */
#define MISC_PERST_PCIE_DEBUG_reserved0_MASK                       0x0000f800
#define MISC_PERST_PCIE_DEBUG_reserved0_ALIGN                      0
#define MISC_PERST_PCIE_DEBUG_reserved0_BITS                       5
#define MISC_PERST_PCIE_DEBUG_reserved0_SHIFT                      11

/* MISC_PERST :: PCIE_DEBUG :: PLL_VCO_RESCUE [10:10] */
#define MISC_PERST_PCIE_DEBUG_PLL_VCO_RESCUE_MASK                  0x00000400
#define MISC_PERST_PCIE_DEBUG_PLL_VCO_RESCUE_ALIGN                 0
#define MISC_PERST_PCIE_DEBUG_PLL_VCO_RESCUE_BITS                  1
#define MISC_PERST_PCIE_DEBUG_PLL_VCO_RESCUE_SHIFT                 10

/* MISC_PERST :: PCIE_DEBUG :: PLL_PDN_OVERRIDE [09:09] */
#define MISC_PERST_PCIE_DEBUG_PLL_PDN_OVERRIDE_MASK                0x00000200
#define MISC_PERST_PCIE_DEBUG_PLL_PDN_OVERRIDE_ALIGN               0
#define MISC_PERST_PCIE_DEBUG_PLL_PDN_OVERRIDE_BITS                1
#define MISC_PERST_PCIE_DEBUG_PLL_PDN_OVERRIDE_SHIFT               9

/* MISC_PERST :: PCIE_DEBUG :: CORE_CLOCK_OVR [08:08] */
#define MISC_PERST_PCIE_DEBUG_CORE_CLOCK_OVR_MASK                  0x00000100
#define MISC_PERST_PCIE_DEBUG_CORE_CLOCK_OVR_ALIGN                 0
#define MISC_PERST_PCIE_DEBUG_CORE_CLOCK_OVR_BITS                  1
#define MISC_PERST_PCIE_DEBUG_CORE_CLOCK_OVR_SHIFT                 8

/* MISC_PERST :: PCIE_DEBUG :: reserved1 [07:04] */
#define MISC_PERST_PCIE_DEBUG_reserved1_MASK                       0x000000f0
#define MISC_PERST_PCIE_DEBUG_reserved1_ALIGN                      0
#define MISC_PERST_PCIE_DEBUG_reserved1_BITS                       4
#define MISC_PERST_PCIE_DEBUG_reserved1_SHIFT                      4

/* MISC_PERST :: PCIE_DEBUG :: PCIE_TMUX_SEL [03:00] */
#define MISC_PERST_PCIE_DEBUG_PCIE_TMUX_SEL_MASK                   0x0000000f
#define MISC_PERST_PCIE_DEBUG_PCIE_TMUX_SEL_ALIGN                  0
#define MISC_PERST_PCIE_DEBUG_PCIE_TMUX_SEL_BITS                   4
#define MISC_PERST_PCIE_DEBUG_PCIE_TMUX_SEL_SHIFT                  0


/****************************************************************************
 * MISC_PERST :: PCIE_DEBUG_STATUS
 ***************************************************************************/
/* MISC_PERST :: PCIE_DEBUG_STATUS :: reserved0 [31:06] */
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved0_MASK                0xffffffc0
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved0_ALIGN               0
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved0_BITS                26
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved0_SHIFT               6

/* MISC_PERST :: PCIE_DEBUG_STATUS :: DATALINKATTN [05:05] */
#define MISC_PERST_PCIE_DEBUG_STATUS_DATALINKATTN_MASK             0x00000020
#define MISC_PERST_PCIE_DEBUG_STATUS_DATALINKATTN_ALIGN            0
#define MISC_PERST_PCIE_DEBUG_STATUS_DATALINKATTN_BITS             1
#define MISC_PERST_PCIE_DEBUG_STATUS_DATALINKATTN_SHIFT            5

/* MISC_PERST :: PCIE_DEBUG_STATUS :: PHYLINKATTN [04:04] */
#define MISC_PERST_PCIE_DEBUG_STATUS_PHYLINKATTN_MASK              0x00000010
#define MISC_PERST_PCIE_DEBUG_STATUS_PHYLINKATTN_ALIGN             0
#define MISC_PERST_PCIE_DEBUG_STATUS_PHYLINKATTN_BITS              1
#define MISC_PERST_PCIE_DEBUG_STATUS_PHYLINKATTN_SHIFT             4

/* MISC_PERST :: PCIE_DEBUG_STATUS :: reserved1 [03:02] */
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved1_MASK                0x0000000c
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved1_ALIGN               0
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved1_BITS                2
#define MISC_PERST_PCIE_DEBUG_STATUS_reserved1_SHIFT               2

/* MISC_PERST :: PCIE_DEBUG_STATUS :: DATA_LINKUP [01:01] */
#define MISC_PERST_PCIE_DEBUG_STATUS_DATA_LINKUP_MASK              0x00000002
#define MISC_PERST_PCIE_DEBUG_STATUS_DATA_LINKUP_ALIGN             0
#define MISC_PERST_PCIE_DEBUG_STATUS_DATA_LINKUP_BITS              1
#define MISC_PERST_PCIE_DEBUG_STATUS_DATA_LINKUP_SHIFT             1

/* MISC_PERST :: PCIE_DEBUG_STATUS :: PHY_LINKUP [00:00] */
#define MISC_PERST_PCIE_DEBUG_STATUS_PHY_LINKUP_MASK               0x00000001
#define MISC_PERST_PCIE_DEBUG_STATUS_PHY_LINKUP_ALIGN              0
#define MISC_PERST_PCIE_DEBUG_STATUS_PHY_LINKUP_BITS               1
#define MISC_PERST_PCIE_DEBUG_STATUS_PHY_LINKUP_SHIFT              0


/****************************************************************************
 * MISC_PERST :: VREG_CTRL
 ***************************************************************************/
/* MISC_PERST :: VREG_CTRL :: reserved0 [31:08] */
#define MISC_PERST_VREG_CTRL_reserved0_MASK                        0xffffff00
#define MISC_PERST_VREG_CTRL_reserved0_ALIGN                       0
#define MISC_PERST_VREG_CTRL_reserved0_BITS                        24
#define MISC_PERST_VREG_CTRL_reserved0_SHIFT                       8

/* MISC_PERST :: VREG_CTRL :: VREG1P2_SEL [07:04] */
#define MISC_PERST_VREG_CTRL_VREG1P2_SEL_MASK                      0x000000f0
#define MISC_PERST_VREG_CTRL_VREG1P2_SEL_ALIGN                     0
#define MISC_PERST_VREG_CTRL_VREG1P2_SEL_BITS                      4
#define MISC_PERST_VREG_CTRL_VREG1P2_SEL_SHIFT                     4

/* MISC_PERST :: VREG_CTRL :: VREG2P5_SEL [03:00] */
#define MISC_PERST_VREG_CTRL_VREG2P5_SEL_MASK                      0x0000000f
#define MISC_PERST_VREG_CTRL_VREG2P5_SEL_ALIGN                     0
#define MISC_PERST_VREG_CTRL_VREG2P5_SEL_BITS                      4
#define MISC_PERST_VREG_CTRL_VREG2P5_SEL_SHIFT                     0


/****************************************************************************
 * MISC_PERST :: MEM_CTRL
 ***************************************************************************/
/* MISC_PERST :: MEM_CTRL :: reserved0 [31:04] */
#define MISC_PERST_MEM_CTRL_reserved0_MASK                         0xfffffff0
#define MISC_PERST_MEM_CTRL_reserved0_ALIGN                        0
#define MISC_PERST_MEM_CTRL_reserved0_BITS                         28
#define MISC_PERST_MEM_CTRL_reserved0_SHIFT                        4

/* MISC_PERST :: MEM_CTRL :: Y_RM [03:03] */
#define MISC_PERST_MEM_CTRL_Y_RM_MASK                              0x00000008
#define MISC_PERST_MEM_CTRL_Y_RM_ALIGN                             0
#define MISC_PERST_MEM_CTRL_Y_RM_BITS                              1
#define MISC_PERST_MEM_CTRL_Y_RM_SHIFT                             3

/* MISC_PERST :: MEM_CTRL :: Y_CCM [02:02] */
#define MISC_PERST_MEM_CTRL_Y_CCM_MASK                             0x00000004
#define MISC_PERST_MEM_CTRL_Y_CCM_ALIGN                            0
#define MISC_PERST_MEM_CTRL_Y_CCM_BITS                             1
#define MISC_PERST_MEM_CTRL_Y_CCM_SHIFT                            2

/* MISC_PERST :: MEM_CTRL :: UV_RM [01:01] */
#define MISC_PERST_MEM_CTRL_UV_RM_MASK                             0x00000002
#define MISC_PERST_MEM_CTRL_UV_RM_ALIGN                            0
#define MISC_PERST_MEM_CTRL_UV_RM_BITS                             1
#define MISC_PERST_MEM_CTRL_UV_RM_SHIFT                            1

/* MISC_PERST :: MEM_CTRL :: UV_CCM [00:00] */
#define MISC_PERST_MEM_CTRL_UV_CCM_MASK                            0x00000001
#define MISC_PERST_MEM_CTRL_UV_CCM_ALIGN                           0
#define MISC_PERST_MEM_CTRL_UV_CCM_BITS                            1
#define MISC_PERST_MEM_CTRL_UV_CCM_SHIFT                           0


/****************************************************************************
 * MISC_PERST :: CLOCK_CTRL
 ***************************************************************************/
/* MISC_PERST :: CLOCK_CTRL :: reserved0 [31:20] */
#define MISC_PERST_CLOCK_CTRL_reserved0_MASK                       0xfff00000
#define MISC_PERST_CLOCK_CTRL_reserved0_ALIGN                      0
#define MISC_PERST_CLOCK_CTRL_reserved0_BITS                       12
#define MISC_PERST_CLOCK_CTRL_reserved0_SHIFT                      20

/* MISC_PERST :: CLOCK_CTRL :: PLL_DIV [19:16] */
#define MISC_PERST_CLOCK_CTRL_PLL_DIV_MASK                         0x000f0000
#define MISC_PERST_CLOCK_CTRL_PLL_DIV_ALIGN                        0
#define MISC_PERST_CLOCK_CTRL_PLL_DIV_BITS                         4
#define MISC_PERST_CLOCK_CTRL_PLL_DIV_SHIFT                        16

/* MISC_PERST :: CLOCK_CTRL :: PLL_MULT [15:08] */
#define MISC_PERST_CLOCK_CTRL_PLL_MULT_MASK                        0x0000ff00
#define MISC_PERST_CLOCK_CTRL_PLL_MULT_ALIGN                       0
#define MISC_PERST_CLOCK_CTRL_PLL_MULT_BITS                        8
#define MISC_PERST_CLOCK_CTRL_PLL_MULT_SHIFT                       8

/* MISC_PERST :: CLOCK_CTRL :: reserved1 [07:03] */
#define MISC_PERST_CLOCK_CTRL_reserved1_MASK                       0x000000f8
#define MISC_PERST_CLOCK_CTRL_reserved1_ALIGN                      0
#define MISC_PERST_CLOCK_CTRL_reserved1_BITS                       5
#define MISC_PERST_CLOCK_CTRL_reserved1_SHIFT                      3

/* MISC_PERST :: CLOCK_CTRL :: PLL_PWRDOWN [02:02] */
#define MISC_PERST_CLOCK_CTRL_PLL_PWRDOWN_MASK                     0x00000004
#define MISC_PERST_CLOCK_CTRL_PLL_PWRDOWN_ALIGN                    0
#define MISC_PERST_CLOCK_CTRL_PLL_PWRDOWN_BITS                     1
#define MISC_PERST_CLOCK_CTRL_PLL_PWRDOWN_SHIFT                    2

/* MISC_PERST :: CLOCK_CTRL :: STOP_CORE_CLK [01:01] */
#define MISC_PERST_CLOCK_CTRL_STOP_CORE_CLK_MASK                   0x00000002
#define MISC_PERST_CLOCK_CTRL_STOP_CORE_CLK_ALIGN                  0
#define MISC_PERST_CLOCK_CTRL_STOP_CORE_CLK_BITS                   1
#define MISC_PERST_CLOCK_CTRL_STOP_CORE_CLK_SHIFT                  1

/* MISC_PERST :: CLOCK_CTRL :: SEL_ALT_CLK [00:00] */
#define MISC_PERST_CLOCK_CTRL_SEL_ALT_CLK_MASK                     0x00000001
#define MISC_PERST_CLOCK_CTRL_SEL_ALT_CLK_ALIGN                    0
#define MISC_PERST_CLOCK_CTRL_SEL_ALT_CLK_BITS                     1
#define MISC_PERST_CLOCK_CTRL_SEL_ALT_CLK_SHIFT                    0


/****************************************************************************
 * BCM70012_MISC_TOP_GISB_ARBITER
 ***************************************************************************/
/****************************************************************************
 * GISB_ARBITER :: REVISION
 ***************************************************************************/
/* GISB_ARBITER :: REVISION :: reserved0 [31:16] */
#define GISB_ARBITER_REVISION_reserved0_MASK                       0xffff0000
#define GISB_ARBITER_REVISION_reserved0_ALIGN                      0
#define GISB_ARBITER_REVISION_reserved0_BITS                       16
#define GISB_ARBITER_REVISION_reserved0_SHIFT                      16

/* GISB_ARBITER :: REVISION :: MAJOR [15:08] */
#define GISB_ARBITER_REVISION_MAJOR_MASK                           0x0000ff00
#define GISB_ARBITER_REVISION_MAJOR_ALIGN                          0
#define GISB_ARBITER_REVISION_MAJOR_BITS                           8
#define GISB_ARBITER_REVISION_MAJOR_SHIFT                          8

/* GISB_ARBITER :: REVISION :: MINOR [07:00] */
#define GISB_ARBITER_REVISION_MINOR_MASK                           0x000000ff
#define GISB_ARBITER_REVISION_MINOR_ALIGN                          0
#define GISB_ARBITER_REVISION_MINOR_BITS                           8
#define GISB_ARBITER_REVISION_MINOR_SHIFT                          0


/****************************************************************************
 * GISB_ARBITER :: SCRATCH
 ***************************************************************************/
/* GISB_ARBITER :: SCRATCH :: scratch_bit [31:00] */
#define GISB_ARBITER_SCRATCH_scratch_bit_MASK                      0xffffffff
#define GISB_ARBITER_SCRATCH_scratch_bit_ALIGN                     0
#define GISB_ARBITER_SCRATCH_scratch_bit_BITS                      32
#define GISB_ARBITER_SCRATCH_scratch_bit_SHIFT                     0


/****************************************************************************
 * GISB_ARBITER :: REQ_MASK
 ***************************************************************************/
/* GISB_ARBITER :: REQ_MASK :: reserved0 [31:06] */
#define GISB_ARBITER_REQ_MASK_reserved0_MASK                       0xffffffc0
#define GISB_ARBITER_REQ_MASK_reserved0_ALIGN                      0
#define GISB_ARBITER_REQ_MASK_reserved0_BITS                       26
#define GISB_ARBITER_REQ_MASK_reserved0_SHIFT                      6

/* GISB_ARBITER :: REQ_MASK :: bsp [05:05] */
#define GISB_ARBITER_REQ_MASK_bsp_MASK                             0x00000020
#define GISB_ARBITER_REQ_MASK_bsp_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_bsp_BITS                             1
#define GISB_ARBITER_REQ_MASK_bsp_SHIFT                            5
#define GISB_ARBITER_REQ_MASK_bsp_UNMASK                           0

/* GISB_ARBITER :: REQ_MASK :: tgt [04:04] */
#define GISB_ARBITER_REQ_MASK_tgt_MASK                             0x00000010
#define GISB_ARBITER_REQ_MASK_tgt_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_tgt_BITS                             1
#define GISB_ARBITER_REQ_MASK_tgt_SHIFT                            4
#define GISB_ARBITER_REQ_MASK_tgt_UNMASK                           0

/* GISB_ARBITER :: REQ_MASK :: aes [03:03] */
#define GISB_ARBITER_REQ_MASK_aes_MASK                             0x00000008
#define GISB_ARBITER_REQ_MASK_aes_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_aes_BITS                             1
#define GISB_ARBITER_REQ_MASK_aes_SHIFT                            3
#define GISB_ARBITER_REQ_MASK_aes_UNMASK                           0

/* GISB_ARBITER :: REQ_MASK :: dci [02:02] */
#define GISB_ARBITER_REQ_MASK_dci_MASK                             0x00000004
#define GISB_ARBITER_REQ_MASK_dci_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_dci_BITS                             1
#define GISB_ARBITER_REQ_MASK_dci_SHIFT                            2
#define GISB_ARBITER_REQ_MASK_dci_UNMASK                           0

/* GISB_ARBITER :: REQ_MASK :: cce [01:01] */
#define GISB_ARBITER_REQ_MASK_cce_MASK                             0x00000002
#define GISB_ARBITER_REQ_MASK_cce_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_cce_BITS                             1
#define GISB_ARBITER_REQ_MASK_cce_SHIFT                            1
#define GISB_ARBITER_REQ_MASK_cce_UNMASK                           0

/* GISB_ARBITER :: REQ_MASK :: dbu [00:00] */
#define GISB_ARBITER_REQ_MASK_dbu_MASK                             0x00000001
#define GISB_ARBITER_REQ_MASK_dbu_ALIGN                            0
#define GISB_ARBITER_REQ_MASK_dbu_BITS                             1
#define GISB_ARBITER_REQ_MASK_dbu_SHIFT                            0
#define GISB_ARBITER_REQ_MASK_dbu_UNMASK                           0


/****************************************************************************
 * GISB_ARBITER :: TIMER
 ***************************************************************************/
/* GISB_ARBITER :: TIMER :: hi_count [31:16] */
#define GISB_ARBITER_TIMER_hi_count_MASK                           0xffff0000
#define GISB_ARBITER_TIMER_hi_count_ALIGN                          0
#define GISB_ARBITER_TIMER_hi_count_BITS                           16
#define GISB_ARBITER_TIMER_hi_count_SHIFT                          16

/* GISB_ARBITER :: TIMER :: lo_count [15:00] */
#define GISB_ARBITER_TIMER_lo_count_MASK                           0x0000ffff
#define GISB_ARBITER_TIMER_lo_count_ALIGN                          0
#define GISB_ARBITER_TIMER_lo_count_BITS                           16
#define GISB_ARBITER_TIMER_lo_count_SHIFT                          0


/****************************************************************************
 * GISB_ARBITER :: BP_CTRL
 ***************************************************************************/
/* GISB_ARBITER :: BP_CTRL :: reserved0 [31:02] */
#define GISB_ARBITER_BP_CTRL_reserved0_MASK                        0xfffffffc
#define GISB_ARBITER_BP_CTRL_reserved0_ALIGN                       0
#define GISB_ARBITER_BP_CTRL_reserved0_BITS                        30
#define GISB_ARBITER_BP_CTRL_reserved0_SHIFT                       2

/* GISB_ARBITER :: BP_CTRL :: breakpoint_tea [01:01] */
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_MASK                   0x00000002
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_ALIGN                  0
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_BITS                   1
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_SHIFT                  1
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_DISABLE                0
#define GISB_ARBITER_BP_CTRL_breakpoint_tea_ENABLE                 1

/* GISB_ARBITER :: BP_CTRL :: repeat_capture [00:00] */
#define GISB_ARBITER_BP_CTRL_repeat_capture_MASK                   0x00000001
#define GISB_ARBITER_BP_CTRL_repeat_capture_ALIGN                  0
#define GISB_ARBITER_BP_CTRL_repeat_capture_BITS                   1
#define GISB_ARBITER_BP_CTRL_repeat_capture_SHIFT                  0
#define GISB_ARBITER_BP_CTRL_repeat_capture_DISABLE                0
#define GISB_ARBITER_BP_CTRL_repeat_capture_ENABLE                 1


/****************************************************************************
 * GISB_ARBITER :: BP_CAP_CLR
 ***************************************************************************/
/* GISB_ARBITER :: BP_CAP_CLR :: reserved0 [31:01] */
#define GISB_ARBITER_BP_CAP_CLR_reserved0_MASK                     0xfffffffe
#define GISB_ARBITER_BP_CAP_CLR_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_CAP_CLR_reserved0_BITS                     31
#define GISB_ARBITER_BP_CAP_CLR_reserved0_SHIFT                    1

/* GISB_ARBITER :: BP_CAP_CLR :: clear [00:00] */
#define GISB_ARBITER_BP_CAP_CLR_clear_MASK                         0x00000001
#define GISB_ARBITER_BP_CAP_CLR_clear_ALIGN                        0
#define GISB_ARBITER_BP_CAP_CLR_clear_BITS                         1
#define GISB_ARBITER_BP_CAP_CLR_clear_SHIFT                        0


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_0
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_0 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_0_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_0_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_0_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_0_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_0
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_0 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_0_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_0_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_0_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_0_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_0
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_0 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_0_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_0_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_0_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_0_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_0 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_0_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_0_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_0_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_0_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_0 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_0_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_0_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_aes_BITS                            1
#define GISB_ARBITER_BP_READ_0_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_0_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_0 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_0_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_0_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_fve_BITS                            1
#define GISB_ARBITER_BP_READ_0_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_0_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_0 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_0_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_0_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_0_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_0_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_0 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_0_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_0_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_0_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_0_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_0 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_0_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_0_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_0_cce_BITS                            1
#define GISB_ARBITER_BP_READ_0_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_0_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_0_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_0
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_0 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_0_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_0_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_0_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_0_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_0 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_0_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_0_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_0_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_0 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_0_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_0_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_0_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_0 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_0_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_0_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_0_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_0 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_0_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_0_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_0_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_0 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_0_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_0_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_0_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_0 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_0_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_0_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_0_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_0_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_0_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_0_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_0
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_0 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_0_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_0_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_0_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_0_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_0 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_0_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_0_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_0_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_0_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_0_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_0_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_0 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_0_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_0_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_0_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_0_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_0_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_0_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_0 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_0_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_0_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_0_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_0_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_0_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_0_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_1
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_1 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_1_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_1_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_1_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_1_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_1
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_1 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_1_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_1_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_1_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_1_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_1
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_1 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_1_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_1_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_1_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_1_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_1 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_1_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_1_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_1_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_1_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_1 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_1_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_1_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_aes_BITS                            1
#define GISB_ARBITER_BP_READ_1_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_1_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_1 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_1_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_1_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_fve_BITS                            1
#define GISB_ARBITER_BP_READ_1_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_1_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_1 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_1_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_1_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_1_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_1_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_1 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_1_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_1_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_1_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_1_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_1 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_1_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_1_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_1_cce_BITS                            1
#define GISB_ARBITER_BP_READ_1_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_1_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_1_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_1
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_1 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_1_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_1_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_1_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_1_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_1 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_1_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_1_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_1_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_1 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_1_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_1_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_1_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_1 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_1_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_1_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_1_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_1 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_1_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_1_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_1_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_1 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_1_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_1_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_1_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_1 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_1_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_1_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_1_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_1_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_1_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_1_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_1
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_1 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_1_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_1_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_1_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_1_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_1 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_1_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_1_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_1_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_1_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_1_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_1_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_1 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_1_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_1_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_1_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_1_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_1_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_1_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_1 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_1_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_1_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_1_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_1_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_1_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_1_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_2
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_2 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_2_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_2_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_2_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_2_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_2
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_2 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_2_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_2_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_2_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_2_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_2
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_2 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_2_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_2_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_2_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_2_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_2 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_2_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_2_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_2_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_2_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_2 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_2_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_2_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_aes_BITS                            1
#define GISB_ARBITER_BP_READ_2_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_2_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_2 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_2_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_2_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_fve_BITS                            1
#define GISB_ARBITER_BP_READ_2_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_2_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_2 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_2_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_2_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_2_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_2_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_2 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_2_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_2_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_2_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_2_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_2 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_2_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_2_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_2_cce_BITS                            1
#define GISB_ARBITER_BP_READ_2_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_2_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_2_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_2
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_2 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_2_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_2_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_2_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_2_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_2 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_2_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_2_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_2_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_2 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_2_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_2_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_2_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_2 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_2_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_2_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_2_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_2 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_2_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_2_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_2_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_2 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_2_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_2_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_2_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_2 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_2_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_2_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_2_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_2_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_2_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_2_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_2
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_2 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_2_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_2_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_2_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_2_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_2 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_2_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_2_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_2_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_2_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_2_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_2_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_2 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_2_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_2_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_2_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_2_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_2_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_2_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_2 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_2_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_2_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_2_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_2_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_2_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_2_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_3
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_3 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_3_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_3_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_3_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_3_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_3
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_3 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_3_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_3_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_3_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_3_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_3
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_3 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_3_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_3_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_3_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_3_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_3 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_3_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_3_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_3_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_3_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_3 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_3_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_3_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_aes_BITS                            1
#define GISB_ARBITER_BP_READ_3_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_3_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_3 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_3_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_3_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_fve_BITS                            1
#define GISB_ARBITER_BP_READ_3_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_3_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_3 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_3_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_3_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_3_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_3_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_3 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_3_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_3_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_3_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_3_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_3 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_3_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_3_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_3_cce_BITS                            1
#define GISB_ARBITER_BP_READ_3_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_3_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_3_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_3
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_3 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_3_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_3_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_3_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_3_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_3 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_3_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_3_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_3_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_3 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_3_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_3_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_3_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_3 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_3_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_3_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_3_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_3 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_3_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_3_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_3_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_3 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_3_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_3_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_3_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_3 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_3_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_3_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_3_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_3_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_3_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_3_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_3
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_3 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_3_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_3_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_3_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_3_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_3 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_3_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_3_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_3_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_3_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_3_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_3_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_3 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_3_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_3_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_3_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_3_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_3_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_3_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_3 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_3_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_3_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_3_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_3_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_3_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_3_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_4
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_4 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_4_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_4_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_4_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_4_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_4
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_4 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_4_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_4_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_4_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_4_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_4
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_4 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_4_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_4_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_4_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_4_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_4 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_4_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_4_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_4_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_4_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_4 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_4_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_4_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_aes_BITS                            1
#define GISB_ARBITER_BP_READ_4_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_4_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_4 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_4_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_4_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_fve_BITS                            1
#define GISB_ARBITER_BP_READ_4_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_4_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_4 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_4_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_4_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_4_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_4_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_4 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_4_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_4_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_4_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_4_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_4 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_4_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_4_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_4_cce_BITS                            1
#define GISB_ARBITER_BP_READ_4_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_4_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_4_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_4
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_4 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_4_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_4_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_4_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_4_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_4 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_4_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_4_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_4_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_4 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_4_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_4_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_4_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_4 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_4_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_4_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_4_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_4 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_4_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_4_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_4_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_4 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_4_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_4_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_4_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_4 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_4_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_4_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_4_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_4_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_4_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_4_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_4
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_4 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_4_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_4_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_4_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_4_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_4 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_4_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_4_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_4_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_4_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_4_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_4_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_4 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_4_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_4_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_4_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_4_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_4_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_4_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_4 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_4_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_4_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_4_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_4_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_4_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_4_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_5
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_5 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_5_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_5_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_5_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_5_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_5
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_5 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_5_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_5_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_5_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_5_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_5
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_5 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_5_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_5_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_5_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_5_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_5 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_5_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_5_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_5_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_5_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_5 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_5_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_5_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_aes_BITS                            1
#define GISB_ARBITER_BP_READ_5_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_5_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_5 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_5_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_5_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_fve_BITS                            1
#define GISB_ARBITER_BP_READ_5_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_5_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_5 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_5_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_5_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_5_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_5_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_5 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_5_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_5_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_5_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_5_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_5 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_5_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_5_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_5_cce_BITS                            1
#define GISB_ARBITER_BP_READ_5_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_5_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_5_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_5
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_5 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_5_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_5_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_5_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_5_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_5 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_5_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_5_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_5_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_5 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_5_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_5_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_5_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_5 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_5_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_5_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_5_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_5 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_5_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_5_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_5_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_5 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_5_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_5_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_5_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_5 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_5_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_5_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_5_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_5_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_5_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_5_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_5
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_5 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_5_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_5_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_5_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_5_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_5 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_5_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_5_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_5_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_5_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_5_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_5_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_5 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_5_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_5_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_5_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_5_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_5_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_5_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_5 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_5_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_5_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_5_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_5_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_5_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_5_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_6
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_6 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_6_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_6_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_6_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_6_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_6
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_6 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_6_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_6_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_6_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_6_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_6
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_6 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_6_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_6_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_6_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_6_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_6 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_6_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_6_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_6_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_6_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_6 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_6_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_6_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_aes_BITS                            1
#define GISB_ARBITER_BP_READ_6_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_6_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_6 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_6_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_6_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_fve_BITS                            1
#define GISB_ARBITER_BP_READ_6_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_6_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_6 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_6_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_6_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_6_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_6_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_6 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_6_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_6_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_6_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_6_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_6 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_6_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_6_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_6_cce_BITS                            1
#define GISB_ARBITER_BP_READ_6_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_6_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_6_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_6
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_6 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_6_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_6_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_6_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_6_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_6 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_6_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_6_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_6_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_6 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_6_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_6_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_6_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_6 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_6_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_6_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_6_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_6 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_6_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_6_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_6_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_6 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_6_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_6_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_6_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_6 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_6_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_6_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_6_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_6_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_6_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_6_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_6
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_6 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_6_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_6_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_6_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_6_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_6 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_6_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_6_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_6_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_6_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_6_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_6_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_6 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_6_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_6_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_6_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_6_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_6_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_6_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_6 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_6_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_6_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_6_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_6_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_6_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_6_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_START_ADDR_7
 ***************************************************************************/
/* GISB_ARBITER :: BP_START_ADDR_7 :: start [31:00] */
#define GISB_ARBITER_BP_START_ADDR_7_start_MASK                    0xffffffff
#define GISB_ARBITER_BP_START_ADDR_7_start_ALIGN                   0
#define GISB_ARBITER_BP_START_ADDR_7_start_BITS                    32
#define GISB_ARBITER_BP_START_ADDR_7_start_SHIFT                   0


/****************************************************************************
 * GISB_ARBITER :: BP_END_ADDR_7
 ***************************************************************************/
/* GISB_ARBITER :: BP_END_ADDR_7 :: end [31:00] */
#define GISB_ARBITER_BP_END_ADDR_7_end_MASK                        0xffffffff
#define GISB_ARBITER_BP_END_ADDR_7_end_ALIGN                       0
#define GISB_ARBITER_BP_END_ADDR_7_end_BITS                        32
#define GISB_ARBITER_BP_END_ADDR_7_end_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: BP_READ_7
 ***************************************************************************/
/* GISB_ARBITER :: BP_READ_7 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_READ_7_reserved0_MASK                      0xffffffc0
#define GISB_ARBITER_BP_READ_7_reserved0_ALIGN                     0
#define GISB_ARBITER_BP_READ_7_reserved0_BITS                      26
#define GISB_ARBITER_BP_READ_7_reserved0_SHIFT                     6

/* GISB_ARBITER :: BP_READ_7 :: bsp [05:05] */
#define GISB_ARBITER_BP_READ_7_bsp_MASK                            0x00000020
#define GISB_ARBITER_BP_READ_7_bsp_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_bsp_BITS                            1
#define GISB_ARBITER_BP_READ_7_bsp_SHIFT                           5
#define GISB_ARBITER_BP_READ_7_bsp_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_bsp_ENABLE                          1

/* GISB_ARBITER :: BP_READ_7 :: aes [04:04] */
#define GISB_ARBITER_BP_READ_7_aes_MASK                            0x00000010
#define GISB_ARBITER_BP_READ_7_aes_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_aes_BITS                            1
#define GISB_ARBITER_BP_READ_7_aes_SHIFT                           4
#define GISB_ARBITER_BP_READ_7_aes_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_aes_ENABLE                          1

/* GISB_ARBITER :: BP_READ_7 :: fve [03:03] */
#define GISB_ARBITER_BP_READ_7_fve_MASK                            0x00000008
#define GISB_ARBITER_BP_READ_7_fve_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_fve_BITS                            1
#define GISB_ARBITER_BP_READ_7_fve_SHIFT                           3
#define GISB_ARBITER_BP_READ_7_fve_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_fve_ENABLE                          1

/* GISB_ARBITER :: BP_READ_7 :: tgt [02:02] */
#define GISB_ARBITER_BP_READ_7_tgt_MASK                            0x00000004
#define GISB_ARBITER_BP_READ_7_tgt_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_tgt_BITS                            1
#define GISB_ARBITER_BP_READ_7_tgt_SHIFT                           2
#define GISB_ARBITER_BP_READ_7_tgt_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_tgt_ENABLE                          1

/* GISB_ARBITER :: BP_READ_7 :: dbu [01:01] */
#define GISB_ARBITER_BP_READ_7_dbu_MASK                            0x00000002
#define GISB_ARBITER_BP_READ_7_dbu_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_dbu_BITS                            1
#define GISB_ARBITER_BP_READ_7_dbu_SHIFT                           1
#define GISB_ARBITER_BP_READ_7_dbu_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_dbu_ENABLE                          1

/* GISB_ARBITER :: BP_READ_7 :: cce [00:00] */
#define GISB_ARBITER_BP_READ_7_cce_MASK                            0x00000001
#define GISB_ARBITER_BP_READ_7_cce_ALIGN                           0
#define GISB_ARBITER_BP_READ_7_cce_BITS                            1
#define GISB_ARBITER_BP_READ_7_cce_SHIFT                           0
#define GISB_ARBITER_BP_READ_7_cce_DISABLE                         0
#define GISB_ARBITER_BP_READ_7_cce_ENABLE                          1


/****************************************************************************
 * GISB_ARBITER :: BP_WRITE_7
 ***************************************************************************/
/* GISB_ARBITER :: BP_WRITE_7 :: reserved0 [31:06] */
#define GISB_ARBITER_BP_WRITE_7_reserved0_MASK                     0xffffffc0
#define GISB_ARBITER_BP_WRITE_7_reserved0_ALIGN                    0
#define GISB_ARBITER_BP_WRITE_7_reserved0_BITS                     26
#define GISB_ARBITER_BP_WRITE_7_reserved0_SHIFT                    6

/* GISB_ARBITER :: BP_WRITE_7 :: bsp [05:05] */
#define GISB_ARBITER_BP_WRITE_7_bsp_MASK                           0x00000020
#define GISB_ARBITER_BP_WRITE_7_bsp_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_bsp_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_bsp_SHIFT                          5
#define GISB_ARBITER_BP_WRITE_7_bsp_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_bsp_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_7 :: aes [04:04] */
#define GISB_ARBITER_BP_WRITE_7_aes_MASK                           0x00000010
#define GISB_ARBITER_BP_WRITE_7_aes_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_aes_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_aes_SHIFT                          4
#define GISB_ARBITER_BP_WRITE_7_aes_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_aes_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_7 :: fve [03:03] */
#define GISB_ARBITER_BP_WRITE_7_fve_MASK                           0x00000008
#define GISB_ARBITER_BP_WRITE_7_fve_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_fve_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_fve_SHIFT                          3
#define GISB_ARBITER_BP_WRITE_7_fve_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_fve_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_7 :: tgt [02:02] */
#define GISB_ARBITER_BP_WRITE_7_tgt_MASK                           0x00000004
#define GISB_ARBITER_BP_WRITE_7_tgt_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_tgt_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_tgt_SHIFT                          2
#define GISB_ARBITER_BP_WRITE_7_tgt_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_tgt_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_7 :: dbu [01:01] */
#define GISB_ARBITER_BP_WRITE_7_dbu_MASK                           0x00000002
#define GISB_ARBITER_BP_WRITE_7_dbu_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_dbu_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_dbu_SHIFT                          1
#define GISB_ARBITER_BP_WRITE_7_dbu_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_dbu_ENABLE                         1

/* GISB_ARBITER :: BP_WRITE_7 :: cce [00:00] */
#define GISB_ARBITER_BP_WRITE_7_cce_MASK                           0x00000001
#define GISB_ARBITER_BP_WRITE_7_cce_ALIGN                          0
#define GISB_ARBITER_BP_WRITE_7_cce_BITS                           1
#define GISB_ARBITER_BP_WRITE_7_cce_SHIFT                          0
#define GISB_ARBITER_BP_WRITE_7_cce_DISABLE                        0
#define GISB_ARBITER_BP_WRITE_7_cce_ENABLE                         1


/****************************************************************************
 * GISB_ARBITER :: BP_ENABLE_7
 ***************************************************************************/
/* GISB_ARBITER :: BP_ENABLE_7 :: reserved0 [31:03] */
#define GISB_ARBITER_BP_ENABLE_7_reserved0_MASK                    0xfffffff8
#define GISB_ARBITER_BP_ENABLE_7_reserved0_ALIGN                   0
#define GISB_ARBITER_BP_ENABLE_7_reserved0_BITS                    29
#define GISB_ARBITER_BP_ENABLE_7_reserved0_SHIFT                   3

/* GISB_ARBITER :: BP_ENABLE_7 :: block [02:02] */
#define GISB_ARBITER_BP_ENABLE_7_block_MASK                        0x00000004
#define GISB_ARBITER_BP_ENABLE_7_block_ALIGN                       0
#define GISB_ARBITER_BP_ENABLE_7_block_BITS                        1
#define GISB_ARBITER_BP_ENABLE_7_block_SHIFT                       2
#define GISB_ARBITER_BP_ENABLE_7_block_DISABLE                     0
#define GISB_ARBITER_BP_ENABLE_7_block_ENABLE                      1

/* GISB_ARBITER :: BP_ENABLE_7 :: address [01:01] */
#define GISB_ARBITER_BP_ENABLE_7_address_MASK                      0x00000002
#define GISB_ARBITER_BP_ENABLE_7_address_ALIGN                     0
#define GISB_ARBITER_BP_ENABLE_7_address_BITS                      1
#define GISB_ARBITER_BP_ENABLE_7_address_SHIFT                     1
#define GISB_ARBITER_BP_ENABLE_7_address_DISABLE                   0
#define GISB_ARBITER_BP_ENABLE_7_address_ENABLE                    1

/* GISB_ARBITER :: BP_ENABLE_7 :: access [00:00] */
#define GISB_ARBITER_BP_ENABLE_7_access_MASK                       0x00000001
#define GISB_ARBITER_BP_ENABLE_7_access_ALIGN                      0
#define GISB_ARBITER_BP_ENABLE_7_access_BITS                       1
#define GISB_ARBITER_BP_ENABLE_7_access_SHIFT                      0
#define GISB_ARBITER_BP_ENABLE_7_access_DISABLE                    0
#define GISB_ARBITER_BP_ENABLE_7_access_ENABLE                     1


/****************************************************************************
 * GISB_ARBITER :: BP_CAP_ADDR
 ***************************************************************************/
/* GISB_ARBITER :: BP_CAP_ADDR :: address [31:00] */
#define GISB_ARBITER_BP_CAP_ADDR_address_MASK                      0xffffffff
#define GISB_ARBITER_BP_CAP_ADDR_address_ALIGN                     0
#define GISB_ARBITER_BP_CAP_ADDR_address_BITS                      32
#define GISB_ARBITER_BP_CAP_ADDR_address_SHIFT                     0


/****************************************************************************
 * GISB_ARBITER :: BP_CAP_DATA
 ***************************************************************************/
/* GISB_ARBITER :: BP_CAP_DATA :: data [31:00] */
#define GISB_ARBITER_BP_CAP_DATA_data_MASK                         0xffffffff
#define GISB_ARBITER_BP_CAP_DATA_data_ALIGN                        0
#define GISB_ARBITER_BP_CAP_DATA_data_BITS                         32
#define GISB_ARBITER_BP_CAP_DATA_data_SHIFT                        0


/****************************************************************************
 * GISB_ARBITER :: BP_CAP_STATUS
 ***************************************************************************/
/* GISB_ARBITER :: BP_CAP_STATUS :: reserved0 [31:06] */
#define GISB_ARBITER_BP_CAP_STATUS_reserved0_MASK                  0xffffffc0
#define GISB_ARBITER_BP_CAP_STATUS_reserved0_ALIGN                 0
#define GISB_ARBITER_BP_CAP_STATUS_reserved0_BITS                  26
#define GISB_ARBITER_BP_CAP_STATUS_reserved0_SHIFT                 6

/* GISB_ARBITER :: BP_CAP_STATUS :: bs_b [05:02] */
#define GISB_ARBITER_BP_CAP_STATUS_bs_b_MASK                       0x0000003c
#define GISB_ARBITER_BP_CAP_STATUS_bs_b_ALIGN                      0
#define GISB_ARBITER_BP_CAP_STATUS_bs_b_BITS                       4
#define GISB_ARBITER_BP_CAP_STATUS_bs_b_SHIFT                      2

/* GISB_ARBITER :: BP_CAP_STATUS :: write [01:01] */
#define GISB_ARBITER_BP_CAP_STATUS_write_MASK                      0x00000002
#define GISB_ARBITER_BP_CAP_STATUS_write_ALIGN                     0
#define GISB_ARBITER_BP_CAP_STATUS_write_BITS                      1
#define GISB_ARBITER_BP_CAP_STATUS_write_SHIFT                     1

/* GISB_ARBITER :: BP_CAP_STATUS :: valid [00:00] */
#define GISB_ARBITER_BP_CAP_STATUS_valid_MASK                      0x00000001
#define GISB_ARBITER_BP_CAP_STATUS_valid_ALIGN                     0
#define GISB_ARBITER_BP_CAP_STATUS_valid_BITS                      1
#define GISB_ARBITER_BP_CAP_STATUS_valid_SHIFT                     0


/****************************************************************************
 * GISB_ARBITER :: BP_CAP_MASTER
 ***************************************************************************/
/* GISB_ARBITER :: BP_CAP_MASTER :: reserved0 [31:06] */
#define GISB_ARBITER_BP_CAP_MASTER_reserved0_MASK                  0xffffffc0
#define GISB_ARBITER_BP_CAP_MASTER_reserved0_ALIGN                 0
#define GISB_ARBITER_BP_CAP_MASTER_reserved0_BITS                  26
#define GISB_ARBITER_BP_CAP_MASTER_reserved0_SHIFT                 6

/* GISB_ARBITER :: BP_CAP_MASTER :: bsp [05:05] */
#define GISB_ARBITER_BP_CAP_MASTER_bsp_MASK                        0x00000020
#define GISB_ARBITER_BP_CAP_MASTER_bsp_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_bsp_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_bsp_SHIFT                       5

/* GISB_ARBITER :: BP_CAP_MASTER :: aes [04:04] */
#define GISB_ARBITER_BP_CAP_MASTER_aes_MASK                        0x00000010
#define GISB_ARBITER_BP_CAP_MASTER_aes_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_aes_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_aes_SHIFT                       4

/* GISB_ARBITER :: BP_CAP_MASTER :: fve [03:03] */
#define GISB_ARBITER_BP_CAP_MASTER_fve_MASK                        0x00000008
#define GISB_ARBITER_BP_CAP_MASTER_fve_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_fve_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_fve_SHIFT                       3

/* GISB_ARBITER :: BP_CAP_MASTER :: tgt [02:02] */
#define GISB_ARBITER_BP_CAP_MASTER_tgt_MASK                        0x00000004
#define GISB_ARBITER_BP_CAP_MASTER_tgt_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_tgt_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_tgt_SHIFT                       2

/* GISB_ARBITER :: BP_CAP_MASTER :: dbu [01:01] */
#define GISB_ARBITER_BP_CAP_MASTER_dbu_MASK                        0x00000002
#define GISB_ARBITER_BP_CAP_MASTER_dbu_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_dbu_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_dbu_SHIFT                       1

/* GISB_ARBITER :: BP_CAP_MASTER :: cce [00:00] */
#define GISB_ARBITER_BP_CAP_MASTER_cce_MASK                        0x00000001
#define GISB_ARBITER_BP_CAP_MASTER_cce_ALIGN                       0
#define GISB_ARBITER_BP_CAP_MASTER_cce_BITS                        1
#define GISB_ARBITER_BP_CAP_MASTER_cce_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: ERR_CAP_CLR
 ***************************************************************************/
/* GISB_ARBITER :: ERR_CAP_CLR :: reserved0 [31:01] */
#define GISB_ARBITER_ERR_CAP_CLR_reserved0_MASK                    0xfffffffe
#define GISB_ARBITER_ERR_CAP_CLR_reserved0_ALIGN                   0
#define GISB_ARBITER_ERR_CAP_CLR_reserved0_BITS                    31
#define GISB_ARBITER_ERR_CAP_CLR_reserved0_SHIFT                   1

/* GISB_ARBITER :: ERR_CAP_CLR :: clear [00:00] */
#define GISB_ARBITER_ERR_CAP_CLR_clear_MASK                        0x00000001
#define GISB_ARBITER_ERR_CAP_CLR_clear_ALIGN                       0
#define GISB_ARBITER_ERR_CAP_CLR_clear_BITS                        1
#define GISB_ARBITER_ERR_CAP_CLR_clear_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: ERR_CAP_ADDR
 ***************************************************************************/
/* GISB_ARBITER :: ERR_CAP_ADDR :: address [31:00] */
#define GISB_ARBITER_ERR_CAP_ADDR_address_MASK                     0xffffffff
#define GISB_ARBITER_ERR_CAP_ADDR_address_ALIGN                    0
#define GISB_ARBITER_ERR_CAP_ADDR_address_BITS                     32
#define GISB_ARBITER_ERR_CAP_ADDR_address_SHIFT                    0


/****************************************************************************
 * GISB_ARBITER :: ERR_CAP_DATA
 ***************************************************************************/
/* GISB_ARBITER :: ERR_CAP_DATA :: data [31:00] */
#define GISB_ARBITER_ERR_CAP_DATA_data_MASK                        0xffffffff
#define GISB_ARBITER_ERR_CAP_DATA_data_ALIGN                       0
#define GISB_ARBITER_ERR_CAP_DATA_data_BITS                        32
#define GISB_ARBITER_ERR_CAP_DATA_data_SHIFT                       0


/****************************************************************************
 * GISB_ARBITER :: ERR_CAP_STATUS
 ***************************************************************************/
/* GISB_ARBITER :: ERR_CAP_STATUS :: reserved0 [31:13] */
#define GISB_ARBITER_ERR_CAP_STATUS_reserved0_MASK                 0xffffe000
#define GISB_ARBITER_ERR_CAP_STATUS_reserved0_ALIGN                0
#define GISB_ARBITER_ERR_CAP_STATUS_reserved0_BITS                 19
#define GISB_ARBITER_ERR_CAP_STATUS_reserved0_SHIFT                13

/* GISB_ARBITER :: ERR_CAP_STATUS :: timeout [12:12] */
#define GISB_ARBITER_ERR_CAP_STATUS_timeout_MASK                   0x00001000
#define GISB_ARBITER_ERR_CAP_STATUS_timeout_ALIGN                  0
#define GISB_ARBITER_ERR_CAP_STATUS_timeout_BITS                   1
#define GISB_ARBITER_ERR_CAP_STATUS_timeout_SHIFT                  12

/* GISB_ARBITER :: ERR_CAP_STATUS :: tea [11:11] */
#define GISB_ARBITER_ERR_CAP_STATUS_tea_MASK                       0x00000800
#define GISB_ARBITER_ERR_CAP_STATUS_tea_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_STATUS_tea_BITS                       1
#define GISB_ARBITER_ERR_CAP_STATUS_tea_SHIFT                      11

/* GISB_ARBITER :: ERR_CAP_STATUS :: reserved1 [10:06] */
#define GISB_ARBITER_ERR_CAP_STATUS_reserved1_MASK                 0x000007c0
#define GISB_ARBITER_ERR_CAP_STATUS_reserved1_ALIGN                0
#define GISB_ARBITER_ERR_CAP_STATUS_reserved1_BITS                 5
#define GISB_ARBITER_ERR_CAP_STATUS_reserved1_SHIFT                6

/* GISB_ARBITER :: ERR_CAP_STATUS :: bs_b [05:02] */
#define GISB_ARBITER_ERR_CAP_STATUS_bs_b_MASK                      0x0000003c
#define GISB_ARBITER_ERR_CAP_STATUS_bs_b_ALIGN                     0
#define GISB_ARBITER_ERR_CAP_STATUS_bs_b_BITS                      4
#define GISB_ARBITER_ERR_CAP_STATUS_bs_b_SHIFT                     2

/* GISB_ARBITER :: ERR_CAP_STATUS :: write [01:01] */
#define GISB_ARBITER_ERR_CAP_STATUS_write_MASK                     0x00000002
#define GISB_ARBITER_ERR_CAP_STATUS_write_ALIGN                    0
#define GISB_ARBITER_ERR_CAP_STATUS_write_BITS                     1
#define GISB_ARBITER_ERR_CAP_STATUS_write_SHIFT                    1

/* GISB_ARBITER :: ERR_CAP_STATUS :: valid [00:00] */
#define GISB_ARBITER_ERR_CAP_STATUS_valid_MASK                     0x00000001
#define GISB_ARBITER_ERR_CAP_STATUS_valid_ALIGN                    0
#define GISB_ARBITER_ERR_CAP_STATUS_valid_BITS                     1
#define GISB_ARBITER_ERR_CAP_STATUS_valid_SHIFT                    0


/****************************************************************************
 * GISB_ARBITER :: ERR_CAP_MASTER
 ***************************************************************************/
/* GISB_ARBITER :: ERR_CAP_MASTER :: reserved0 [31:06] */
#define GISB_ARBITER_ERR_CAP_MASTER_reserved0_MASK                 0xffffffc0
#define GISB_ARBITER_ERR_CAP_MASTER_reserved0_ALIGN                0
#define GISB_ARBITER_ERR_CAP_MASTER_reserved0_BITS                 26
#define GISB_ARBITER_ERR_CAP_MASTER_reserved0_SHIFT                6

/* GISB_ARBITER :: ERR_CAP_MASTER :: bsp [05:05] */
#define GISB_ARBITER_ERR_CAP_MASTER_bsp_MASK                       0x00000020
#define GISB_ARBITER_ERR_CAP_MASTER_bsp_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_bsp_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_bsp_SHIFT                      5

/* GISB_ARBITER :: ERR_CAP_MASTER :: aes [04:04] */
#define GISB_ARBITER_ERR_CAP_MASTER_aes_MASK                       0x00000010
#define GISB_ARBITER_ERR_CAP_MASTER_aes_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_aes_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_aes_SHIFT                      4

/* GISB_ARBITER :: ERR_CAP_MASTER :: fve [03:03] */
#define GISB_ARBITER_ERR_CAP_MASTER_fve_MASK                       0x00000008
#define GISB_ARBITER_ERR_CAP_MASTER_fve_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_fve_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_fve_SHIFT                      3

/* GISB_ARBITER :: ERR_CAP_MASTER :: tgt [02:02] */
#define GISB_ARBITER_ERR_CAP_MASTER_tgt_MASK                       0x00000004
#define GISB_ARBITER_ERR_CAP_MASTER_tgt_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_tgt_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_tgt_SHIFT                      2

/* GISB_ARBITER :: ERR_CAP_MASTER :: dbu [01:01] */
#define GISB_ARBITER_ERR_CAP_MASTER_dbu_MASK                       0x00000002
#define GISB_ARBITER_ERR_CAP_MASTER_dbu_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_dbu_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_dbu_SHIFT                      1

/* GISB_ARBITER :: ERR_CAP_MASTER :: cce [00:00] */
#define GISB_ARBITER_ERR_CAP_MASTER_cce_MASK                       0x00000001
#define GISB_ARBITER_ERR_CAP_MASTER_cce_ALIGN                      0
#define GISB_ARBITER_ERR_CAP_MASTER_cce_BITS                       1
#define GISB_ARBITER_ERR_CAP_MASTER_cce_SHIFT                      0


/****************************************************************************
 * BCM70012_MISC_TOP_MISC_GR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * MISC_GR_BRIDGE :: REVISION
 ***************************************************************************/
/* MISC_GR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define MISC_GR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define MISC_GR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define MISC_GR_BRIDGE_REVISION_reserved0_BITS                     16
#define MISC_GR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* MISC_GR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define MISC_GR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define MISC_GR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define MISC_GR_BRIDGE_REVISION_MAJOR_BITS                         8
#define MISC_GR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* MISC_GR_BRIDGE :: REVISION :: MINOR [07:00] */
#define MISC_GR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define MISC_GR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define MISC_GR_BRIDGE_REVISION_MINOR_BITS                         8
#define MISC_GR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * MISC_GR_BRIDGE :: CTRL
 ***************************************************************************/
/* MISC_GR_BRIDGE :: CTRL :: reserved0 [31:01] */
#define MISC_GR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffe
#define MISC_GR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define MISC_GR_BRIDGE_CTRL_reserved0_BITS                         31
#define MISC_GR_BRIDGE_CTRL_reserved0_SHIFT                        1

/* MISC_GR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_MASK                   0x00000001
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_ALIGN                  0
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_BITS                   1
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_SHIFT                  0
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE           0
#define MISC_GR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE            1


/****************************************************************************
 * MISC_GR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* MISC_GR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* MISC_GR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT    0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * MISC_GR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* MISC_GR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* MISC_GR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT    0
#define MISC_GR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * BCM70012_DBU_TOP_DBU
 ***************************************************************************/
/****************************************************************************
 * DBU :: DBU_CMD
 ***************************************************************************/
/* DBU :: DBU_CMD :: reserved0 [31:03] */
#define DBU_DBU_CMD_reserved0_MASK                                 0xfffffff8
#define DBU_DBU_CMD_reserved0_ALIGN                                0
#define DBU_DBU_CMD_reserved0_BITS                                 29
#define DBU_DBU_CMD_reserved0_SHIFT                                3

/* DBU :: DBU_CMD :: RX_OVERFLOW [02:02] */
#define DBU_DBU_CMD_RX_OVERFLOW_MASK                               0x00000004
#define DBU_DBU_CMD_RX_OVERFLOW_ALIGN                              0
#define DBU_DBU_CMD_RX_OVERFLOW_BITS                               1
#define DBU_DBU_CMD_RX_OVERFLOW_SHIFT                              2

/* DBU :: DBU_CMD :: RX_ERROR [01:01] */
#define DBU_DBU_CMD_RX_ERROR_MASK                                  0x00000002
#define DBU_DBU_CMD_RX_ERROR_ALIGN                                 0
#define DBU_DBU_CMD_RX_ERROR_BITS                                  1
#define DBU_DBU_CMD_RX_ERROR_SHIFT                                 1

/* DBU :: DBU_CMD :: ENABLE [00:00] */
#define DBU_DBU_CMD_ENABLE_MASK                                    0x00000001
#define DBU_DBU_CMD_ENABLE_ALIGN                                   0
#define DBU_DBU_CMD_ENABLE_BITS                                    1
#define DBU_DBU_CMD_ENABLE_SHIFT                                   0


/****************************************************************************
 * DBU :: DBU_STATUS
 ***************************************************************************/
/* DBU :: DBU_STATUS :: reserved0 [31:02] */
#define DBU_DBU_STATUS_reserved0_MASK                              0xfffffffc
#define DBU_DBU_STATUS_reserved0_ALIGN                             0
#define DBU_DBU_STATUS_reserved0_BITS                              30
#define DBU_DBU_STATUS_reserved0_SHIFT                             2

/* DBU :: DBU_STATUS :: TXDATA_OCCUPIED [01:01] */
#define DBU_DBU_STATUS_TXDATA_OCCUPIED_MASK                        0x00000002
#define DBU_DBU_STATUS_TXDATA_OCCUPIED_ALIGN                       0
#define DBU_DBU_STATUS_TXDATA_OCCUPIED_BITS                        1
#define DBU_DBU_STATUS_TXDATA_OCCUPIED_SHIFT                       1

/* DBU :: DBU_STATUS :: RXDATA_VALID [00:00] */
#define DBU_DBU_STATUS_RXDATA_VALID_MASK                           0x00000001
#define DBU_DBU_STATUS_RXDATA_VALID_ALIGN                          0
#define DBU_DBU_STATUS_RXDATA_VALID_BITS                           1
#define DBU_DBU_STATUS_RXDATA_VALID_SHIFT                          0


/****************************************************************************
 * DBU :: DBU_CONFIG
 ***************************************************************************/
/* DBU :: DBU_CONFIG :: reserved0 [31:03] */
#define DBU_DBU_CONFIG_reserved0_MASK                              0xfffffff8
#define DBU_DBU_CONFIG_reserved0_ALIGN                             0
#define DBU_DBU_CONFIG_reserved0_BITS                              29
#define DBU_DBU_CONFIG_reserved0_SHIFT                             3

/* DBU :: DBU_CONFIG :: CRLF_ENABLE [02:02] */
#define DBU_DBU_CONFIG_CRLF_ENABLE_MASK                            0x00000004
#define DBU_DBU_CONFIG_CRLF_ENABLE_ALIGN                           0
#define DBU_DBU_CONFIG_CRLF_ENABLE_BITS                            1
#define DBU_DBU_CONFIG_CRLF_ENABLE_SHIFT                           2

/* DBU :: DBU_CONFIG :: DEBUGSM_ENABLE [01:01] */
#define DBU_DBU_CONFIG_DEBUGSM_ENABLE_MASK                         0x00000002
#define DBU_DBU_CONFIG_DEBUGSM_ENABLE_ALIGN                        0
#define DBU_DBU_CONFIG_DEBUGSM_ENABLE_BITS                         1
#define DBU_DBU_CONFIG_DEBUGSM_ENABLE_SHIFT                        1

/* DBU :: DBU_CONFIG :: TIMING_OVERRIDE [00:00] */
#define DBU_DBU_CONFIG_TIMING_OVERRIDE_MASK                        0x00000001
#define DBU_DBU_CONFIG_TIMING_OVERRIDE_ALIGN                       0
#define DBU_DBU_CONFIG_TIMING_OVERRIDE_BITS                        1
#define DBU_DBU_CONFIG_TIMING_OVERRIDE_SHIFT                       0


/****************************************************************************
 * DBU :: DBU_TIMING
 ***************************************************************************/
/* DBU :: DBU_TIMING :: BIT_INTERVAL [31:16] */
#define DBU_DBU_TIMING_BIT_INTERVAL_MASK                           0xffff0000
#define DBU_DBU_TIMING_BIT_INTERVAL_ALIGN                          0
#define DBU_DBU_TIMING_BIT_INTERVAL_BITS                           16
#define DBU_DBU_TIMING_BIT_INTERVAL_SHIFT                          16

/* DBU :: DBU_TIMING :: FB_SMPL_OFFSET [15:00] */
#define DBU_DBU_TIMING_FB_SMPL_OFFSET_MASK                         0x0000ffff
#define DBU_DBU_TIMING_FB_SMPL_OFFSET_ALIGN                        0
#define DBU_DBU_TIMING_FB_SMPL_OFFSET_BITS                         16
#define DBU_DBU_TIMING_FB_SMPL_OFFSET_SHIFT                        0


/****************************************************************************
 * DBU :: DBU_RXDATA
 ***************************************************************************/
/* DBU :: DBU_RXDATA :: reserved0 [31:09] */
#define DBU_DBU_RXDATA_reserved0_MASK                              0xfffffe00
#define DBU_DBU_RXDATA_reserved0_ALIGN                             0
#define DBU_DBU_RXDATA_reserved0_BITS                              23
#define DBU_DBU_RXDATA_reserved0_SHIFT                             9

/* DBU :: DBU_RXDATA :: ERROR [08:08] */
#define DBU_DBU_RXDATA_ERROR_MASK                                  0x00000100
#define DBU_DBU_RXDATA_ERROR_ALIGN                                 0
#define DBU_DBU_RXDATA_ERROR_BITS                                  1
#define DBU_DBU_RXDATA_ERROR_SHIFT                                 8

/* DBU :: DBU_RXDATA :: VALUE [07:00] */
#define DBU_DBU_RXDATA_VALUE_MASK                                  0x000000ff
#define DBU_DBU_RXDATA_VALUE_ALIGN                                 0
#define DBU_DBU_RXDATA_VALUE_BITS                                  8
#define DBU_DBU_RXDATA_VALUE_SHIFT                                 0


/****************************************************************************
 * DBU :: DBU_TXDATA
 ***************************************************************************/
/* DBU :: DBU_TXDATA :: reserved0 [31:08] */
#define DBU_DBU_TXDATA_reserved0_MASK                              0xffffff00
#define DBU_DBU_TXDATA_reserved0_ALIGN                             0
#define DBU_DBU_TXDATA_reserved0_BITS                              24
#define DBU_DBU_TXDATA_reserved0_SHIFT                             8

/* DBU :: DBU_TXDATA :: VALUE [07:00] */
#define DBU_DBU_TXDATA_VALUE_MASK                                  0x000000ff
#define DBU_DBU_TXDATA_VALUE_ALIGN                                 0
#define DBU_DBU_TXDATA_VALUE_BITS                                  8
#define DBU_DBU_TXDATA_VALUE_SHIFT                                 0


/****************************************************************************
 * BCM70012_DBU_TOP_DBU_RGR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * DBU_RGR_BRIDGE :: REVISION
 ***************************************************************************/
/* DBU_RGR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define DBU_RGR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define DBU_RGR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define DBU_RGR_BRIDGE_REVISION_reserved0_BITS                     16
#define DBU_RGR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* DBU_RGR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define DBU_RGR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define DBU_RGR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define DBU_RGR_BRIDGE_REVISION_MAJOR_BITS                         8
#define DBU_RGR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* DBU_RGR_BRIDGE :: REVISION :: MINOR [07:00] */
#define DBU_RGR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define DBU_RGR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define DBU_RGR_BRIDGE_REVISION_MINOR_BITS                         8
#define DBU_RGR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * DBU_RGR_BRIDGE :: CTRL
 ***************************************************************************/
/* DBU_RGR_BRIDGE :: CTRL :: reserved0 [31:02] */
#define DBU_RGR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffc
#define DBU_RGR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define DBU_RGR_BRIDGE_CTRL_reserved0_BITS                         30
#define DBU_RGR_BRIDGE_CTRL_reserved0_SHIFT                        2

/* DBU_RGR_BRIDGE :: CTRL :: rbus_error_intr [01:01] */
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_MASK                   0x00000002
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_ALIGN                  0
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_BITS                   1
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_SHIFT                  1
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_DISABLE           0
#define DBU_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_ENABLE            1

/* DBU_RGR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_MASK                   0x00000001
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_ALIGN                  0
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_BITS                   1
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_SHIFT                  0
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE           0
#define DBU_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE            1


/****************************************************************************
 * DBU_RGR_BRIDGE :: RBUS_TIMER
 ***************************************************************************/
/* DBU_RGR_BRIDGE :: RBUS_TIMER :: reserved0 [31:16] */
#define DBU_RGR_BRIDGE_RBUS_TIMER_reserved0_MASK                   0xffff0000
#define DBU_RGR_BRIDGE_RBUS_TIMER_reserved0_ALIGN                  0
#define DBU_RGR_BRIDGE_RBUS_TIMER_reserved0_BITS                   16
#define DBU_RGR_BRIDGE_RBUS_TIMER_reserved0_SHIFT                  16

/* DBU_RGR_BRIDGE :: RBUS_TIMER :: timer_value [15:00] */
#define DBU_RGR_BRIDGE_RBUS_TIMER_timer_value_MASK                 0x0000ffff
#define DBU_RGR_BRIDGE_RBUS_TIMER_timer_value_ALIGN                0
#define DBU_RGR_BRIDGE_RBUS_TIMER_timer_value_BITS                 16
#define DBU_RGR_BRIDGE_RBUS_TIMER_timer_value_SHIFT                0


/****************************************************************************
 * DBU_RGR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* DBU_RGR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* DBU_RGR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT    0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * DBU_RGR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* DBU_RGR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* DBU_RGR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT    0
#define DBU_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * BCM70012_OTP_TOP_OTP
 ***************************************************************************/
/****************************************************************************
 * OTP :: CONFIG_INFO
 ***************************************************************************/
/* OTP :: CONFIG_INFO :: reserved0 [31:22] */
#define OTP_CONFIG_INFO_reserved0_MASK                             0xffc00000
#define OTP_CONFIG_INFO_reserved0_ALIGN                            0
#define OTP_CONFIG_INFO_reserved0_BITS                             10
#define OTP_CONFIG_INFO_reserved0_SHIFT                            22

/* OTP :: CONFIG_INFO :: SELVL [21:20] */
#define OTP_CONFIG_INFO_SELVL_MASK                                 0x00300000
#define OTP_CONFIG_INFO_SELVL_ALIGN                                0
#define OTP_CONFIG_INFO_SELVL_BITS                                 2
#define OTP_CONFIG_INFO_SELVL_SHIFT                                20

/* OTP :: CONFIG_INFO :: reserved1 [19:17] */
#define OTP_CONFIG_INFO_reserved1_MASK                             0x000e0000
#define OTP_CONFIG_INFO_reserved1_ALIGN                            0
#define OTP_CONFIG_INFO_reserved1_BITS                             3
#define OTP_CONFIG_INFO_reserved1_SHIFT                            17

/* OTP :: CONFIG_INFO :: PTEST [16:16] */
#define OTP_CONFIG_INFO_PTEST_MASK                                 0x00010000
#define OTP_CONFIG_INFO_PTEST_ALIGN                                0
#define OTP_CONFIG_INFO_PTEST_BITS                                 1
#define OTP_CONFIG_INFO_PTEST_SHIFT                                16

/* OTP :: CONFIG_INFO :: reserved2 [15:13] */
#define OTP_CONFIG_INFO_reserved2_MASK                             0x0000e000
#define OTP_CONFIG_INFO_reserved2_ALIGN                            0
#define OTP_CONFIG_INFO_reserved2_BITS                             3
#define OTP_CONFIG_INFO_reserved2_SHIFT                            13

/* OTP :: CONFIG_INFO :: MAX_REDUNDANT_ROWS [12:08] */
#define OTP_CONFIG_INFO_MAX_REDUNDANT_ROWS_MASK                    0x00001f00
#define OTP_CONFIG_INFO_MAX_REDUNDANT_ROWS_ALIGN                   0
#define OTP_CONFIG_INFO_MAX_REDUNDANT_ROWS_BITS                    5
#define OTP_CONFIG_INFO_MAX_REDUNDANT_ROWS_SHIFT                   8

/* OTP :: CONFIG_INFO :: MAX_RETRY [07:04] */
#define OTP_CONFIG_INFO_MAX_RETRY_MASK                             0x000000f0
#define OTP_CONFIG_INFO_MAX_RETRY_ALIGN                            0
#define OTP_CONFIG_INFO_MAX_RETRY_BITS                             4
#define OTP_CONFIG_INFO_MAX_RETRY_SHIFT                            4

/* OTP :: CONFIG_INFO :: reserved3 [03:01] */
#define OTP_CONFIG_INFO_reserved3_MASK                             0x0000000e
#define OTP_CONFIG_INFO_reserved3_ALIGN                            0
#define OTP_CONFIG_INFO_reserved3_BITS                             3
#define OTP_CONFIG_INFO_reserved3_SHIFT                            1

/* OTP :: CONFIG_INFO :: PROG_MODE [00:00] */
#define OTP_CONFIG_INFO_PROG_MODE_MASK                             0x00000001
#define OTP_CONFIG_INFO_PROG_MODE_ALIGN                            0
#define OTP_CONFIG_INFO_PROG_MODE_BITS                             1
#define OTP_CONFIG_INFO_PROG_MODE_SHIFT                            0


/****************************************************************************
 * OTP :: CMD
 ***************************************************************************/
/* OTP :: CMD :: reserved0 [31:02] */
#define OTP_CMD_reserved0_MASK                                     0xfffffffc
#define OTP_CMD_reserved0_ALIGN                                    0
#define OTP_CMD_reserved0_BITS                                     30
#define OTP_CMD_reserved0_SHIFT                                    2

/* OTP :: CMD :: KEYS_AVAIL [01:01] */
#define OTP_CMD_KEYS_AVAIL_MASK                                    0x00000002
#define OTP_CMD_KEYS_AVAIL_ALIGN                                   0
#define OTP_CMD_KEYS_AVAIL_BITS                                    1
#define OTP_CMD_KEYS_AVAIL_SHIFT                                   1

/* OTP :: CMD :: PROGRAM_OTP [00:00] */
#define OTP_CMD_PROGRAM_OTP_MASK                                   0x00000001
#define OTP_CMD_PROGRAM_OTP_ALIGN                                  0
#define OTP_CMD_PROGRAM_OTP_BITS                                   1
#define OTP_CMD_PROGRAM_OTP_SHIFT                                  0


/****************************************************************************
 * OTP :: STATUS
 ***************************************************************************/
/* OTP :: STATUS :: reserved0 [31:30] */
#define OTP_STATUS_reserved0_MASK                                  0xc0000000
#define OTP_STATUS_reserved0_ALIGN                                 0
#define OTP_STATUS_reserved0_BITS                                  2
#define OTP_STATUS_reserved0_SHIFT                                 30

/* OTP :: STATUS :: TOTAL_RETRIES [29:17] */
#define OTP_STATUS_TOTAL_RETRIES_MASK                              0x3ffe0000
#define OTP_STATUS_TOTAL_RETRIES_ALIGN                             0
#define OTP_STATUS_TOTAL_RETRIES_BITS                              13
#define OTP_STATUS_TOTAL_RETRIES_SHIFT                             17

/* OTP :: STATUS :: ROWS_USED [16:12] */
#define OTP_STATUS_ROWS_USED_MASK                                  0x0001f000
#define OTP_STATUS_ROWS_USED_ALIGN                                 0
#define OTP_STATUS_ROWS_USED_BITS                                  5
#define OTP_STATUS_ROWS_USED_SHIFT                                 12

/* OTP :: STATUS :: MAX_RETRIES [11:08] */
#define OTP_STATUS_MAX_RETRIES_MASK                                0x00000f00
#define OTP_STATUS_MAX_RETRIES_ALIGN                               0
#define OTP_STATUS_MAX_RETRIES_BITS                                4
#define OTP_STATUS_MAX_RETRIES_SHIFT                               8

/* OTP :: STATUS :: PROG_STATUS [07:04] */
#define OTP_STATUS_PROG_STATUS_MASK                                0x000000f0
#define OTP_STATUS_PROG_STATUS_ALIGN                               0
#define OTP_STATUS_PROG_STATUS_BITS                                4
#define OTP_STATUS_PROG_STATUS_SHIFT                               4

/* OTP :: STATUS :: reserved1 [03:03] */
#define OTP_STATUS_reserved1_MASK                                  0x00000008
#define OTP_STATUS_reserved1_ALIGN                                 0
#define OTP_STATUS_reserved1_BITS                                  1
#define OTP_STATUS_reserved1_SHIFT                                 3

/* OTP :: STATUS :: CHECKSUM_MISMATCH [02:02] */
#define OTP_STATUS_CHECKSUM_MISMATCH_MASK                          0x00000004
#define OTP_STATUS_CHECKSUM_MISMATCH_ALIGN                         0
#define OTP_STATUS_CHECKSUM_MISMATCH_BITS                          1
#define OTP_STATUS_CHECKSUM_MISMATCH_SHIFT                         2

/* OTP :: STATUS :: INSUFFICIENT_ROWS [01:01] */
#define OTP_STATUS_INSUFFICIENT_ROWS_MASK                          0x00000002
#define OTP_STATUS_INSUFFICIENT_ROWS_ALIGN                         0
#define OTP_STATUS_INSUFFICIENT_ROWS_BITS                          1
#define OTP_STATUS_INSUFFICIENT_ROWS_SHIFT                         1

/* OTP :: STATUS :: PROGRAMMED [00:00] */
#define OTP_STATUS_PROGRAMMED_MASK                                 0x00000001
#define OTP_STATUS_PROGRAMMED_ALIGN                                0
#define OTP_STATUS_PROGRAMMED_BITS                                 1
#define OTP_STATUS_PROGRAMMED_SHIFT                                0


/****************************************************************************
 * OTP :: CONTENT_MISC
 ***************************************************************************/
/* OTP :: CONTENT_MISC :: reserved0 [31:06] */
#define OTP_CONTENT_MISC_reserved0_MASK                            0xffffffc0
#define OTP_CONTENT_MISC_reserved0_ALIGN                           0
#define OTP_CONTENT_MISC_reserved0_BITS                            26
#define OTP_CONTENT_MISC_reserved0_SHIFT                           6

/* OTP :: CONTENT_MISC :: DCI_SECURITY_ENABLE [05:05] */
#define OTP_CONTENT_MISC_DCI_SECURITY_ENABLE_MASK                  0x00000020
#define OTP_CONTENT_MISC_DCI_SECURITY_ENABLE_ALIGN                 0
#define OTP_CONTENT_MISC_DCI_SECURITY_ENABLE_BITS                  1
#define OTP_CONTENT_MISC_DCI_SECURITY_ENABLE_SHIFT                 5

/* OTP :: CONTENT_MISC :: AES_SECURITY_ENABLE [04:04] */
#define OTP_CONTENT_MISC_AES_SECURITY_ENABLE_MASK                  0x00000010
#define OTP_CONTENT_MISC_AES_SECURITY_ENABLE_ALIGN                 0
#define OTP_CONTENT_MISC_AES_SECURITY_ENABLE_BITS                  1
#define OTP_CONTENT_MISC_AES_SECURITY_ENABLE_SHIFT                 4

/* OTP :: CONTENT_MISC :: DISABLE_JTAG [03:03] */
#define OTP_CONTENT_MISC_DISABLE_JTAG_MASK                         0x00000008
#define OTP_CONTENT_MISC_DISABLE_JTAG_ALIGN                        0
#define OTP_CONTENT_MISC_DISABLE_JTAG_BITS                         1
#define OTP_CONTENT_MISC_DISABLE_JTAG_SHIFT                        3

/* OTP :: CONTENT_MISC :: DISABLE_UART [02:02] */
#define OTP_CONTENT_MISC_DISABLE_UART_MASK                         0x00000004
#define OTP_CONTENT_MISC_DISABLE_UART_ALIGN                        0
#define OTP_CONTENT_MISC_DISABLE_UART_BITS                         1
#define OTP_CONTENT_MISC_DISABLE_UART_SHIFT                        2

/* OTP :: CONTENT_MISC :: ENABLE_RANDOMIZATION [01:01] */
#define OTP_CONTENT_MISC_ENABLE_RANDOMIZATION_MASK                 0x00000002
#define OTP_CONTENT_MISC_ENABLE_RANDOMIZATION_ALIGN                0
#define OTP_CONTENT_MISC_ENABLE_RANDOMIZATION_BITS                 1
#define OTP_CONTENT_MISC_ENABLE_RANDOMIZATION_SHIFT                1

/* OTP :: CONTENT_MISC :: OTP_SECURITY_ENABLE [00:00] */
#define OTP_CONTENT_MISC_OTP_SECURITY_ENABLE_MASK                  0x00000001
#define OTP_CONTENT_MISC_OTP_SECURITY_ENABLE_ALIGN                 0
#define OTP_CONTENT_MISC_OTP_SECURITY_ENABLE_BITS                  1
#define OTP_CONTENT_MISC_OTP_SECURITY_ENABLE_SHIFT                 0


/****************************************************************************
 * OTP :: CONTENT_AES_0
 ***************************************************************************/
/* OTP :: CONTENT_AES_0 :: AES_KEY_0 [31:00] */
#define OTP_CONTENT_AES_0_AES_KEY_0_MASK                           0xffffffff
#define OTP_CONTENT_AES_0_AES_KEY_0_ALIGN                          0
#define OTP_CONTENT_AES_0_AES_KEY_0_BITS                           32
#define OTP_CONTENT_AES_0_AES_KEY_0_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_AES_1
 ***************************************************************************/
/* OTP :: CONTENT_AES_1 :: AES_KEY_1 [31:00] */
#define OTP_CONTENT_AES_1_AES_KEY_1_MASK                           0xffffffff
#define OTP_CONTENT_AES_1_AES_KEY_1_ALIGN                          0
#define OTP_CONTENT_AES_1_AES_KEY_1_BITS                           32
#define OTP_CONTENT_AES_1_AES_KEY_1_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_AES_2
 ***************************************************************************/
/* OTP :: CONTENT_AES_2 :: AES_KEY_2 [31:00] */
#define OTP_CONTENT_AES_2_AES_KEY_2_MASK                           0xffffffff
#define OTP_CONTENT_AES_2_AES_KEY_2_ALIGN                          0
#define OTP_CONTENT_AES_2_AES_KEY_2_BITS                           32
#define OTP_CONTENT_AES_2_AES_KEY_2_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_AES_3
 ***************************************************************************/
/* OTP :: CONTENT_AES_3 :: AES_KEY_3 [31:00] */
#define OTP_CONTENT_AES_3_AES_KEY_3_MASK                           0xffffffff
#define OTP_CONTENT_AES_3_AES_KEY_3_ALIGN                          0
#define OTP_CONTENT_AES_3_AES_KEY_3_BITS                           32
#define OTP_CONTENT_AES_3_AES_KEY_3_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_0
 ***************************************************************************/
/* OTP :: CONTENT_SHA_0 :: SHA_KEY_0 [31:00] */
#define OTP_CONTENT_SHA_0_SHA_KEY_0_MASK                           0xffffffff
#define OTP_CONTENT_SHA_0_SHA_KEY_0_ALIGN                          0
#define OTP_CONTENT_SHA_0_SHA_KEY_0_BITS                           32
#define OTP_CONTENT_SHA_0_SHA_KEY_0_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_1
 ***************************************************************************/
/* OTP :: CONTENT_SHA_1 :: SHA_KEY_1 [31:00] */
#define OTP_CONTENT_SHA_1_SHA_KEY_1_MASK                           0xffffffff
#define OTP_CONTENT_SHA_1_SHA_KEY_1_ALIGN                          0
#define OTP_CONTENT_SHA_1_SHA_KEY_1_BITS                           32
#define OTP_CONTENT_SHA_1_SHA_KEY_1_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_2
 ***************************************************************************/
/* OTP :: CONTENT_SHA_2 :: SHA_KEY_2 [31:00] */
#define OTP_CONTENT_SHA_2_SHA_KEY_2_MASK                           0xffffffff
#define OTP_CONTENT_SHA_2_SHA_KEY_2_ALIGN                          0
#define OTP_CONTENT_SHA_2_SHA_KEY_2_BITS                           32
#define OTP_CONTENT_SHA_2_SHA_KEY_2_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_3
 ***************************************************************************/
/* OTP :: CONTENT_SHA_3 :: SHA_KEY_3 [31:00] */
#define OTP_CONTENT_SHA_3_SHA_KEY_3_MASK                           0xffffffff
#define OTP_CONTENT_SHA_3_SHA_KEY_3_ALIGN                          0
#define OTP_CONTENT_SHA_3_SHA_KEY_3_BITS                           32
#define OTP_CONTENT_SHA_3_SHA_KEY_3_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_4
 ***************************************************************************/
/* OTP :: CONTENT_SHA_4 :: SHA_KEY_4 [31:00] */
#define OTP_CONTENT_SHA_4_SHA_KEY_4_MASK                           0xffffffff
#define OTP_CONTENT_SHA_4_SHA_KEY_4_ALIGN                          0
#define OTP_CONTENT_SHA_4_SHA_KEY_4_BITS                           32
#define OTP_CONTENT_SHA_4_SHA_KEY_4_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_5
 ***************************************************************************/
/* OTP :: CONTENT_SHA_5 :: SHA_KEY_5 [31:00] */
#define OTP_CONTENT_SHA_5_SHA_KEY_5_MASK                           0xffffffff
#define OTP_CONTENT_SHA_5_SHA_KEY_5_ALIGN                          0
#define OTP_CONTENT_SHA_5_SHA_KEY_5_BITS                           32
#define OTP_CONTENT_SHA_5_SHA_KEY_5_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_6
 ***************************************************************************/
/* OTP :: CONTENT_SHA_6 :: SHA_KEY_6 [31:00] */
#define OTP_CONTENT_SHA_6_SHA_KEY_6_MASK                           0xffffffff
#define OTP_CONTENT_SHA_6_SHA_KEY_6_ALIGN                          0
#define OTP_CONTENT_SHA_6_SHA_KEY_6_BITS                           32
#define OTP_CONTENT_SHA_6_SHA_KEY_6_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_SHA_7
 ***************************************************************************/
/* OTP :: CONTENT_SHA_7 :: SHA_KEY_7 [31:00] */
#define OTP_CONTENT_SHA_7_SHA_KEY_7_MASK                           0xffffffff
#define OTP_CONTENT_SHA_7_SHA_KEY_7_ALIGN                          0
#define OTP_CONTENT_SHA_7_SHA_KEY_7_BITS                           32
#define OTP_CONTENT_SHA_7_SHA_KEY_7_SHIFT                          0


/****************************************************************************
 * OTP :: CONTENT_CHECKSUM
 ***************************************************************************/
/* OTP :: CONTENT_CHECKSUM :: reserved0 [31:16] */
#define OTP_CONTENT_CHECKSUM_reserved0_MASK                        0xffff0000
#define OTP_CONTENT_CHECKSUM_reserved0_ALIGN                       0
#define OTP_CONTENT_CHECKSUM_reserved0_BITS                        16
#define OTP_CONTENT_CHECKSUM_reserved0_SHIFT                       16

/* OTP :: CONTENT_CHECKSUM :: CHECKSUM [15:00] */
#define OTP_CONTENT_CHECKSUM_CHECKSUM_MASK                         0x0000ffff
#define OTP_CONTENT_CHECKSUM_CHECKSUM_ALIGN                        0
#define OTP_CONTENT_CHECKSUM_CHECKSUM_BITS                         16
#define OTP_CONTENT_CHECKSUM_CHECKSUM_SHIFT                        0


/****************************************************************************
 * OTP :: PROG_CTRL
 ***************************************************************************/
/* OTP :: PROG_CTRL :: reserved0 [31:02] */
#define OTP_PROG_CTRL_reserved0_MASK                               0xfffffffc
#define OTP_PROG_CTRL_reserved0_ALIGN                              0
#define OTP_PROG_CTRL_reserved0_BITS                               30
#define OTP_PROG_CTRL_reserved0_SHIFT                              2

/* OTP :: PROG_CTRL :: ENABLE [01:01] */
#define OTP_PROG_CTRL_ENABLE_MASK                                  0x00000002
#define OTP_PROG_CTRL_ENABLE_ALIGN                                 0
#define OTP_PROG_CTRL_ENABLE_BITS                                  1
#define OTP_PROG_CTRL_ENABLE_SHIFT                                 1

/* OTP :: PROG_CTRL :: RST [00:00] */
#define OTP_PROG_CTRL_RST_MASK                                     0x00000001
#define OTP_PROG_CTRL_RST_ALIGN                                    0
#define OTP_PROG_CTRL_RST_BITS                                     1
#define OTP_PROG_CTRL_RST_SHIFT                                    0


/****************************************************************************
 * OTP :: PROG_STATUS
 ***************************************************************************/
/* OTP :: PROG_STATUS :: reserved0 [31:02] */
#define OTP_PROG_STATUS_reserved0_MASK                             0xfffffffc
#define OTP_PROG_STATUS_reserved0_ALIGN                            0
#define OTP_PROG_STATUS_reserved0_BITS                             30
#define OTP_PROG_STATUS_reserved0_SHIFT                            2

/* OTP :: PROG_STATUS :: ORDY [01:01] */
#define OTP_PROG_STATUS_ORDY_MASK                                  0x00000002
#define OTP_PROG_STATUS_ORDY_ALIGN                                 0
#define OTP_PROG_STATUS_ORDY_BITS                                  1
#define OTP_PROG_STATUS_ORDY_SHIFT                                 1

/* OTP :: PROG_STATUS :: IRDY [00:00] */
#define OTP_PROG_STATUS_IRDY_MASK                                  0x00000001
#define OTP_PROG_STATUS_IRDY_ALIGN                                 0
#define OTP_PROG_STATUS_IRDY_BITS                                  1
#define OTP_PROG_STATUS_IRDY_SHIFT                                 0


/****************************************************************************
 * OTP :: PROG_PULSE
 ***************************************************************************/
/* OTP :: PROG_PULSE :: PROG_HI [31:00] */
#define OTP_PROG_PULSE_PROG_HI_MASK                                0xffffffff
#define OTP_PROG_PULSE_PROG_HI_ALIGN                               0
#define OTP_PROG_PULSE_PROG_HI_BITS                                32
#define OTP_PROG_PULSE_PROG_HI_SHIFT                               0


/****************************************************************************
 * OTP :: VERIFY_PULSE
 ***************************************************************************/
/* OTP :: VERIFY_PULSE :: PROG_LOW [31:16] */
#define OTP_VERIFY_PULSE_PROG_LOW_MASK                             0xffff0000
#define OTP_VERIFY_PULSE_PROG_LOW_ALIGN                            0
#define OTP_VERIFY_PULSE_PROG_LOW_BITS                             16
#define OTP_VERIFY_PULSE_PROG_LOW_SHIFT                            16

/* OTP :: VERIFY_PULSE :: VERIFY [15:00] */
#define OTP_VERIFY_PULSE_VERIFY_MASK                               0x0000ffff
#define OTP_VERIFY_PULSE_VERIFY_ALIGN                              0
#define OTP_VERIFY_PULSE_VERIFY_BITS                               16
#define OTP_VERIFY_PULSE_VERIFY_SHIFT                              0


/****************************************************************************
 * OTP :: PROG_MASK
 ***************************************************************************/
/* OTP :: PROG_MASK :: reserved0 [31:17] */
#define OTP_PROG_MASK_reserved0_MASK                               0xfffe0000
#define OTP_PROG_MASK_reserved0_ALIGN                              0
#define OTP_PROG_MASK_reserved0_BITS                               15
#define OTP_PROG_MASK_reserved0_SHIFT                              17

/* OTP :: PROG_MASK :: PROG_MASK [16:00] */
#define OTP_PROG_MASK_PROG_MASK_MASK                               0x0001ffff
#define OTP_PROG_MASK_PROG_MASK_ALIGN                              0
#define OTP_PROG_MASK_PROG_MASK_BITS                               17
#define OTP_PROG_MASK_PROG_MASK_SHIFT                              0


/****************************************************************************
 * OTP :: DATA_INPUT
 ***************************************************************************/
/* OTP :: DATA_INPUT :: reserved0 [31:31] */
#define OTP_DATA_INPUT_reserved0_MASK                              0x80000000
#define OTP_DATA_INPUT_reserved0_ALIGN                             0
#define OTP_DATA_INPUT_reserved0_BITS                              1
#define OTP_DATA_INPUT_reserved0_SHIFT                             31

/* OTP :: DATA_INPUT :: CMD [30:28] */
#define OTP_DATA_INPUT_CMD_MASK                                    0x70000000
#define OTP_DATA_INPUT_CMD_ALIGN                                   0
#define OTP_DATA_INPUT_CMD_BITS                                    3
#define OTP_DATA_INPUT_CMD_SHIFT                                   28

/* OTP :: DATA_INPUT :: reserved1 [27:26] */
#define OTP_DATA_INPUT_reserved1_MASK                              0x0c000000
#define OTP_DATA_INPUT_reserved1_ALIGN                             0
#define OTP_DATA_INPUT_reserved1_BITS                              2
#define OTP_DATA_INPUT_reserved1_SHIFT                             26

/* OTP :: DATA_INPUT :: ADDR [25:20] */
#define OTP_DATA_INPUT_ADDR_MASK                                   0x03f00000
#define OTP_DATA_INPUT_ADDR_ALIGN                                  0
#define OTP_DATA_INPUT_ADDR_BITS                                   6
#define OTP_DATA_INPUT_ADDR_SHIFT                                  20

/* OTP :: DATA_INPUT :: reserved2 [19:18] */
#define OTP_DATA_INPUT_reserved2_MASK                              0x000c0000
#define OTP_DATA_INPUT_reserved2_ALIGN                             0
#define OTP_DATA_INPUT_reserved2_BITS                              2
#define OTP_DATA_INPUT_reserved2_SHIFT                             18

/* OTP :: DATA_INPUT :: WRCOL [17:17] */
#define OTP_DATA_INPUT_WRCOL_MASK                                  0x00020000
#define OTP_DATA_INPUT_WRCOL_ALIGN                                 0
#define OTP_DATA_INPUT_WRCOL_BITS                                  1
#define OTP_DATA_INPUT_WRCOL_SHIFT                                 17

/* OTP :: DATA_INPUT :: DIN [16:00] */
#define OTP_DATA_INPUT_DIN_MASK                                    0x0001ffff
#define OTP_DATA_INPUT_DIN_ALIGN                                   0
#define OTP_DATA_INPUT_DIN_BITS                                    17
#define OTP_DATA_INPUT_DIN_SHIFT                                   0


/****************************************************************************
 * OTP :: DATA_OUTPUT
 ***************************************************************************/
/* OTP :: DATA_OUTPUT :: reserved0 [31:17] */
#define OTP_DATA_OUTPUT_reserved0_MASK                             0xfffe0000
#define OTP_DATA_OUTPUT_reserved0_ALIGN                            0
#define OTP_DATA_OUTPUT_reserved0_BITS                             15
#define OTP_DATA_OUTPUT_reserved0_SHIFT                            17

/* OTP :: DATA_OUTPUT :: DOUT [16:00] */
#define OTP_DATA_OUTPUT_DOUT_MASK                                  0x0001ffff
#define OTP_DATA_OUTPUT_DOUT_ALIGN                                 0
#define OTP_DATA_OUTPUT_DOUT_BITS                                  17
#define OTP_DATA_OUTPUT_DOUT_SHIFT                                 0


/****************************************************************************
 * BCM70012_OTP_TOP_OTP_GR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * OTP_GR_BRIDGE :: REVISION
 ***************************************************************************/
/* OTP_GR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define OTP_GR_BRIDGE_REVISION_reserved0_MASK                      0xffff0000
#define OTP_GR_BRIDGE_REVISION_reserved0_ALIGN                     0
#define OTP_GR_BRIDGE_REVISION_reserved0_BITS                      16
#define OTP_GR_BRIDGE_REVISION_reserved0_SHIFT                     16

/* OTP_GR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define OTP_GR_BRIDGE_REVISION_MAJOR_MASK                          0x0000ff00
#define OTP_GR_BRIDGE_REVISION_MAJOR_ALIGN                         0
#define OTP_GR_BRIDGE_REVISION_MAJOR_BITS                          8
#define OTP_GR_BRIDGE_REVISION_MAJOR_SHIFT                         8

/* OTP_GR_BRIDGE :: REVISION :: MINOR [07:00] */
#define OTP_GR_BRIDGE_REVISION_MINOR_MASK                          0x000000ff
#define OTP_GR_BRIDGE_REVISION_MINOR_ALIGN                         0
#define OTP_GR_BRIDGE_REVISION_MINOR_BITS                          8
#define OTP_GR_BRIDGE_REVISION_MINOR_SHIFT                         0


/****************************************************************************
 * OTP_GR_BRIDGE :: CTRL
 ***************************************************************************/
/* OTP_GR_BRIDGE :: CTRL :: reserved0 [31:01] */
#define OTP_GR_BRIDGE_CTRL_reserved0_MASK                          0xfffffffe
#define OTP_GR_BRIDGE_CTRL_reserved0_ALIGN                         0
#define OTP_GR_BRIDGE_CTRL_reserved0_BITS                          31
#define OTP_GR_BRIDGE_CTRL_reserved0_SHIFT                         1

/* OTP_GR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_MASK                    0x00000001
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_ALIGN                   0
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_BITS                    1
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_SHIFT                   0
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE            0
#define OTP_GR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE             1


/****************************************************************************
 * OTP_GR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* OTP_GR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK              0xfffffffe
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN             0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS              31
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT             1

/* OTP_GR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK         0x00000001
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN        0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS         1
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT        0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT     0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT       1


/****************************************************************************
 * OTP_GR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* OTP_GR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK              0xfffffffe
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN             0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS              31
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT             1

/* OTP_GR_BRIDGE :: SPARE_SW_RESET_1 :: OTP_00_SW_RESET [00:00] */
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_MASK        0x00000001
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_ALIGN       0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_BITS        1
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_SHIFT       0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_DEASSERT    0
#define OTP_GR_BRIDGE_SPARE_SW_RESET_1_OTP_00_SW_RESET_ASSERT      1


/****************************************************************************
 * BCM70012_AES_TOP_AES
 ***************************************************************************/
/****************************************************************************
 * AES :: CONFIG_INFO
 ***************************************************************************/
/* AES :: CONFIG_INFO :: SWAP [31:31] */
#define AES_CONFIG_INFO_SWAP_MASK                                  0x80000000
#define AES_CONFIG_INFO_SWAP_ALIGN                                 0
#define AES_CONFIG_INFO_SWAP_BITS                                  1
#define AES_CONFIG_INFO_SWAP_SHIFT                                 31

/* AES :: CONFIG_INFO :: reserved0 [30:19] */
#define AES_CONFIG_INFO_reserved0_MASK                             0x7ff80000
#define AES_CONFIG_INFO_reserved0_ALIGN                            0
#define AES_CONFIG_INFO_reserved0_BITS                             12
#define AES_CONFIG_INFO_reserved0_SHIFT                            19

/* AES :: CONFIG_INFO :: OFFSET [18:02] */
#define AES_CONFIG_INFO_OFFSET_MASK                                0x0007fffc
#define AES_CONFIG_INFO_OFFSET_ALIGN                               0
#define AES_CONFIG_INFO_OFFSET_BITS                                17
#define AES_CONFIG_INFO_OFFSET_SHIFT                               2

/* AES :: CONFIG_INFO :: reserved1 [01:00] */
#define AES_CONFIG_INFO_reserved1_MASK                             0x00000003
#define AES_CONFIG_INFO_reserved1_ALIGN                            0
#define AES_CONFIG_INFO_reserved1_BITS                             2
#define AES_CONFIG_INFO_reserved1_SHIFT                            0


/****************************************************************************
 * AES :: CMD
 ***************************************************************************/
/* AES :: CMD :: reserved0 [31:13] */
#define AES_CMD_reserved0_MASK                                     0xffffe000
#define AES_CMD_reserved0_ALIGN                                    0
#define AES_CMD_reserved0_BITS                                     19
#define AES_CMD_reserved0_SHIFT                                    13

/* AES :: CMD :: WRITE_EEPROM [12:12] */
#define AES_CMD_WRITE_EEPROM_MASK                                  0x00001000
#define AES_CMD_WRITE_EEPROM_ALIGN                                 0
#define AES_CMD_WRITE_EEPROM_BITS                                  1
#define AES_CMD_WRITE_EEPROM_SHIFT                                 12

/* AES :: CMD :: reserved1 [11:09] */
#define AES_CMD_reserved1_MASK                                     0x00000e00
#define AES_CMD_reserved1_ALIGN                                    0
#define AES_CMD_reserved1_BITS                                     3
#define AES_CMD_reserved1_SHIFT                                    9

/* AES :: CMD :: START_EEPROM_COPY [08:08] */
#define AES_CMD_START_EEPROM_COPY_MASK                             0x00000100
#define AES_CMD_START_EEPROM_COPY_ALIGN                            0
#define AES_CMD_START_EEPROM_COPY_BITS                             1
#define AES_CMD_START_EEPROM_COPY_SHIFT                            8

/* AES :: CMD :: reserved2 [07:05] */
#define AES_CMD_reserved2_MASK                                     0x000000e0
#define AES_CMD_reserved2_ALIGN                                    0
#define AES_CMD_reserved2_BITS                                     3
#define AES_CMD_reserved2_SHIFT                                    5

/* AES :: CMD :: PREPARE_ENCRYPTION [04:04] */
#define AES_CMD_PREPARE_ENCRYPTION_MASK                            0x00000010
#define AES_CMD_PREPARE_ENCRYPTION_ALIGN                           0
#define AES_CMD_PREPARE_ENCRYPTION_BITS                            1
#define AES_CMD_PREPARE_ENCRYPTION_SHIFT                           4

/* AES :: CMD :: reserved3 [03:01] */
#define AES_CMD_reserved3_MASK                                     0x0000000e
#define AES_CMD_reserved3_ALIGN                                    0
#define AES_CMD_reserved3_BITS                                     3
#define AES_CMD_reserved3_SHIFT                                    1

/* AES :: CMD :: START_KEY_LOAD [00:00] */
#define AES_CMD_START_KEY_LOAD_MASK                                0x00000001
#define AES_CMD_START_KEY_LOAD_ALIGN                               0
#define AES_CMD_START_KEY_LOAD_BITS                                1
#define AES_CMD_START_KEY_LOAD_SHIFT                               0


/****************************************************************************
 * AES :: STATUS
 ***************************************************************************/
/* AES :: STATUS :: reserved0 [31:23] */
#define AES_STATUS_reserved0_MASK                                  0xff800000
#define AES_STATUS_reserved0_ALIGN                                 0
#define AES_STATUS_reserved0_BITS                                  9
#define AES_STATUS_reserved0_SHIFT                                 23

/* AES :: STATUS :: STUCK_AT_ZERO [22:22] */
#define AES_STATUS_STUCK_AT_ZERO_MASK                              0x00400000
#define AES_STATUS_STUCK_AT_ZERO_ALIGN                             0
#define AES_STATUS_STUCK_AT_ZERO_BITS                              1
#define AES_STATUS_STUCK_AT_ZERO_SHIFT                             22

/* AES :: STATUS :: STUCK_AT_ONE [21:21] */
#define AES_STATUS_STUCK_AT_ONE_MASK                               0x00200000
#define AES_STATUS_STUCK_AT_ONE_ALIGN                              0
#define AES_STATUS_STUCK_AT_ONE_BITS                               1
#define AES_STATUS_STUCK_AT_ONE_SHIFT                              21

/* AES :: STATUS :: RANDOM_READY [20:20] */
#define AES_STATUS_RANDOM_READY_MASK                               0x00100000
#define AES_STATUS_RANDOM_READY_ALIGN                              0
#define AES_STATUS_RANDOM_READY_BITS                               1
#define AES_STATUS_RANDOM_READY_SHIFT                              20

/* AES :: STATUS :: reserved1 [19:16] */
#define AES_STATUS_reserved1_MASK                                  0x000f0000
#define AES_STATUS_reserved1_ALIGN                                 0
#define AES_STATUS_reserved1_BITS                                  4
#define AES_STATUS_reserved1_SHIFT                                 16

/* AES :: STATUS :: WRITE_EEPROM_TIMEOUT [15:15] */
#define AES_STATUS_WRITE_EEPROM_TIMEOUT_MASK                       0x00008000
#define AES_STATUS_WRITE_EEPROM_TIMEOUT_ALIGN                      0
#define AES_STATUS_WRITE_EEPROM_TIMEOUT_BITS                       1
#define AES_STATUS_WRITE_EEPROM_TIMEOUT_SHIFT                      15

/* AES :: STATUS :: WRITE_DATA_MISMATCH [14:14] */
#define AES_STATUS_WRITE_DATA_MISMATCH_MASK                        0x00004000
#define AES_STATUS_WRITE_DATA_MISMATCH_ALIGN                       0
#define AES_STATUS_WRITE_DATA_MISMATCH_BITS                        1
#define AES_STATUS_WRITE_DATA_MISMATCH_SHIFT                       14

/* AES :: STATUS :: WRITE_GISB_ERROR [13:13] */
#define AES_STATUS_WRITE_GISB_ERROR_MASK                           0x00002000
#define AES_STATUS_WRITE_GISB_ERROR_ALIGN                          0
#define AES_STATUS_WRITE_GISB_ERROR_BITS                           1
#define AES_STATUS_WRITE_GISB_ERROR_SHIFT                          13

/* AES :: STATUS :: WRITE_DONE [12:12] */
#define AES_STATUS_WRITE_DONE_MASK                                 0x00001000
#define AES_STATUS_WRITE_DONE_ALIGN                                0
#define AES_STATUS_WRITE_DONE_BITS                                 1
#define AES_STATUS_WRITE_DONE_SHIFT                                12

/* AES :: STATUS :: reserved2 [11:11] */
#define AES_STATUS_reserved2_MASK                                  0x00000800
#define AES_STATUS_reserved2_ALIGN                                 0
#define AES_STATUS_reserved2_BITS                                  1
#define AES_STATUS_reserved2_SHIFT                                 11

/* AES :: STATUS :: COPY_EEPROM_ERROR [10:10] */
#define AES_STATUS_COPY_EEPROM_ERROR_MASK                          0x00000400
#define AES_STATUS_COPY_EEPROM_ERROR_ALIGN                         0
#define AES_STATUS_COPY_EEPROM_ERROR_BITS                          1
#define AES_STATUS_COPY_EEPROM_ERROR_SHIFT                         10

/* AES :: STATUS :: COPY_GISB_ERROR [09:09] */
#define AES_STATUS_COPY_GISB_ERROR_MASK                            0x00000200
#define AES_STATUS_COPY_GISB_ERROR_ALIGN                           0
#define AES_STATUS_COPY_GISB_ERROR_BITS                            1
#define AES_STATUS_COPY_GISB_ERROR_SHIFT                           9

/* AES :: STATUS :: COPY_DONE [08:08] */
#define AES_STATUS_COPY_DONE_MASK                                  0x00000100
#define AES_STATUS_COPY_DONE_ALIGN                                 0
#define AES_STATUS_COPY_DONE_BITS                                  1
#define AES_STATUS_COPY_DONE_SHIFT                                 8

/* AES :: STATUS :: PREPARE_EEPROM_TIMEOUT [07:07] */
#define AES_STATUS_PREPARE_EEPROM_TIMEOUT_MASK                     0x00000080
#define AES_STATUS_PREPARE_EEPROM_TIMEOUT_ALIGN                    0
#define AES_STATUS_PREPARE_EEPROM_TIMEOUT_BITS                     1
#define AES_STATUS_PREPARE_EEPROM_TIMEOUT_SHIFT                    7

/* AES :: STATUS :: PREPARE_DATA_MISMATCH [06:06] */
#define AES_STATUS_PREPARE_DATA_MISMATCH_MASK                      0x00000040
#define AES_STATUS_PREPARE_DATA_MISMATCH_ALIGN                     0
#define AES_STATUS_PREPARE_DATA_MISMATCH_BITS                      1
#define AES_STATUS_PREPARE_DATA_MISMATCH_SHIFT                     6

/* AES :: STATUS :: PREPARE_GISB_ERROR [05:05] */
#define AES_STATUS_PREPARE_GISB_ERROR_MASK                         0x00000020
#define AES_STATUS_PREPARE_GISB_ERROR_ALIGN                        0
#define AES_STATUS_PREPARE_GISB_ERROR_BITS                         1
#define AES_STATUS_PREPARE_GISB_ERROR_SHIFT                        5

/* AES :: STATUS :: PREPARE_DONE [04:04] */
#define AES_STATUS_PREPARE_DONE_MASK                               0x00000010
#define AES_STATUS_PREPARE_DONE_ALIGN                              0
#define AES_STATUS_PREPARE_DONE_BITS                               1
#define AES_STATUS_PREPARE_DONE_SHIFT                              4

/* AES :: STATUS :: reserved3 [03:02] */
#define AES_STATUS_reserved3_MASK                                  0x0000000c
#define AES_STATUS_reserved3_ALIGN                                 0
#define AES_STATUS_reserved3_BITS                                  2
#define AES_STATUS_reserved3_SHIFT                                 2

/* AES :: STATUS :: KEY_LOAD_GISB_ERROR [01:01] */
#define AES_STATUS_KEY_LOAD_GISB_ERROR_MASK                        0x00000002
#define AES_STATUS_KEY_LOAD_GISB_ERROR_ALIGN                       0
#define AES_STATUS_KEY_LOAD_GISB_ERROR_BITS                        1
#define AES_STATUS_KEY_LOAD_GISB_ERROR_SHIFT                       1

/* AES :: STATUS :: KEY_LOAD_DONE [00:00] */
#define AES_STATUS_KEY_LOAD_DONE_MASK                              0x00000001
#define AES_STATUS_KEY_LOAD_DONE_ALIGN                             0
#define AES_STATUS_KEY_LOAD_DONE_BITS                              1
#define AES_STATUS_KEY_LOAD_DONE_SHIFT                             0


/****************************************************************************
 * AES :: EEPROM_CONFIG
 ***************************************************************************/
/* AES :: EEPROM_CONFIG :: LENGTH [31:20] */
#define AES_EEPROM_CONFIG_LENGTH_MASK                              0xfff00000
#define AES_EEPROM_CONFIG_LENGTH_ALIGN                             0
#define AES_EEPROM_CONFIG_LENGTH_BITS                              12
#define AES_EEPROM_CONFIG_LENGTH_SHIFT                             20

/* AES :: EEPROM_CONFIG :: reserved0 [19:16] */
#define AES_EEPROM_CONFIG_reserved0_MASK                           0x000f0000
#define AES_EEPROM_CONFIG_reserved0_ALIGN                          0
#define AES_EEPROM_CONFIG_reserved0_BITS                           4
#define AES_EEPROM_CONFIG_reserved0_SHIFT                          16

/* AES :: EEPROM_CONFIG :: START_ADDR [15:02] */
#define AES_EEPROM_CONFIG_START_ADDR_MASK                          0x0000fffc
#define AES_EEPROM_CONFIG_START_ADDR_ALIGN                         0
#define AES_EEPROM_CONFIG_START_ADDR_BITS                          14
#define AES_EEPROM_CONFIG_START_ADDR_SHIFT                         2

/* AES :: EEPROM_CONFIG :: reserved1 [01:00] */
#define AES_EEPROM_CONFIG_reserved1_MASK                           0x00000003
#define AES_EEPROM_CONFIG_reserved1_ALIGN                          0
#define AES_EEPROM_CONFIG_reserved1_BITS                           2
#define AES_EEPROM_CONFIG_reserved1_SHIFT                          0


/****************************************************************************
 * AES :: EEPROM_DATA_0
 ***************************************************************************/
/* AES :: EEPROM_DATA_0 :: DATA [31:00] */
#define AES_EEPROM_DATA_0_DATA_MASK                                0xffffffff
#define AES_EEPROM_DATA_0_DATA_ALIGN                               0
#define AES_EEPROM_DATA_0_DATA_BITS                                32
#define AES_EEPROM_DATA_0_DATA_SHIFT                               0


/****************************************************************************
 * AES :: EEPROM_DATA_1
 ***************************************************************************/
/* AES :: EEPROM_DATA_1 :: DATA [31:00] */
#define AES_EEPROM_DATA_1_DATA_MASK                                0xffffffff
#define AES_EEPROM_DATA_1_DATA_ALIGN                               0
#define AES_EEPROM_DATA_1_DATA_BITS                                32
#define AES_EEPROM_DATA_1_DATA_SHIFT                               0


/****************************************************************************
 * AES :: EEPROM_DATA_2
 ***************************************************************************/
/* AES :: EEPROM_DATA_2 :: DATA [31:00] */
#define AES_EEPROM_DATA_2_DATA_MASK                                0xffffffff
#define AES_EEPROM_DATA_2_DATA_ALIGN                               0
#define AES_EEPROM_DATA_2_DATA_BITS                                32
#define AES_EEPROM_DATA_2_DATA_SHIFT                               0


/****************************************************************************
 * AES :: EEPROM_DATA_3
 ***************************************************************************/
/* AES :: EEPROM_DATA_3 :: DATA [31:00] */
#define AES_EEPROM_DATA_3_DATA_MASK                                0xffffffff
#define AES_EEPROM_DATA_3_DATA_ALIGN                               0
#define AES_EEPROM_DATA_3_DATA_BITS                                32
#define AES_EEPROM_DATA_3_DATA_SHIFT                               0


/****************************************************************************
 * BCM70012_AES_TOP_AES_RGR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * AES_RGR_BRIDGE :: REVISION
 ***************************************************************************/
/* AES_RGR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define AES_RGR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define AES_RGR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define AES_RGR_BRIDGE_REVISION_reserved0_BITS                     16
#define AES_RGR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* AES_RGR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define AES_RGR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define AES_RGR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define AES_RGR_BRIDGE_REVISION_MAJOR_BITS                         8
#define AES_RGR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* AES_RGR_BRIDGE :: REVISION :: MINOR [07:00] */
#define AES_RGR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define AES_RGR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define AES_RGR_BRIDGE_REVISION_MINOR_BITS                         8
#define AES_RGR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * AES_RGR_BRIDGE :: CTRL
 ***************************************************************************/
/* AES_RGR_BRIDGE :: CTRL :: reserved0 [31:02] */
#define AES_RGR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffc
#define AES_RGR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define AES_RGR_BRIDGE_CTRL_reserved0_BITS                         30
#define AES_RGR_BRIDGE_CTRL_reserved0_SHIFT                        2

/* AES_RGR_BRIDGE :: CTRL :: rbus_error_intr [01:01] */
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_MASK                   0x00000002
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_ALIGN                  0
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_BITS                   1
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_SHIFT                  1
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_DISABLE           0
#define AES_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_ENABLE            1

/* AES_RGR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_MASK                   0x00000001
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_ALIGN                  0
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_BITS                   1
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_SHIFT                  0
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE           0
#define AES_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE            1


/****************************************************************************
 * AES_RGR_BRIDGE :: RBUS_TIMER
 ***************************************************************************/
/* AES_RGR_BRIDGE :: RBUS_TIMER :: reserved0 [31:16] */
#define AES_RGR_BRIDGE_RBUS_TIMER_reserved0_MASK                   0xffff0000
#define AES_RGR_BRIDGE_RBUS_TIMER_reserved0_ALIGN                  0
#define AES_RGR_BRIDGE_RBUS_TIMER_reserved0_BITS                   16
#define AES_RGR_BRIDGE_RBUS_TIMER_reserved0_SHIFT                  16

/* AES_RGR_BRIDGE :: RBUS_TIMER :: timer_value [15:00] */
#define AES_RGR_BRIDGE_RBUS_TIMER_timer_value_MASK                 0x0000ffff
#define AES_RGR_BRIDGE_RBUS_TIMER_timer_value_ALIGN                0
#define AES_RGR_BRIDGE_RBUS_TIMER_timer_value_BITS                 16
#define AES_RGR_BRIDGE_RBUS_TIMER_timer_value_SHIFT                0


/****************************************************************************
 * AES_RGR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* AES_RGR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* AES_RGR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT    0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * AES_RGR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* AES_RGR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* AES_RGR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT    0
#define AES_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * BCM70012_DCI_TOP_DCI
 ***************************************************************************/
/****************************************************************************
 * DCI :: CMD
 ***************************************************************************/
/* DCI :: CMD :: reserved0 [31:09] */
#define DCI_CMD_reserved0_MASK                                     0xfffffe00
#define DCI_CMD_reserved0_ALIGN                                    0
#define DCI_CMD_reserved0_BITS                                     23
#define DCI_CMD_reserved0_SHIFT                                    9

/* DCI :: CMD :: FORCE_FW_VALIDATED [08:08] */
#define DCI_CMD_FORCE_FW_VALIDATED_MASK                            0x00000100
#define DCI_CMD_FORCE_FW_VALIDATED_ALIGN                           0
#define DCI_CMD_FORCE_FW_VALIDATED_BITS                            1
#define DCI_CMD_FORCE_FW_VALIDATED_SHIFT                           8

/* DCI :: CMD :: reserved1 [07:05] */
#define DCI_CMD_reserved1_MASK                                     0x000000e0
#define DCI_CMD_reserved1_ALIGN                                    0
#define DCI_CMD_reserved1_BITS                                     3
#define DCI_CMD_reserved1_SHIFT                                    5

/* DCI :: CMD :: START_PROCESSOR [04:04] */
#define DCI_CMD_START_PROCESSOR_MASK                               0x00000010
#define DCI_CMD_START_PROCESSOR_ALIGN                              0
#define DCI_CMD_START_PROCESSOR_BITS                               1
#define DCI_CMD_START_PROCESSOR_SHIFT                              4

/* DCI :: CMD :: reserved2 [03:02] */
#define DCI_CMD_reserved2_MASK                                     0x0000000c
#define DCI_CMD_reserved2_ALIGN                                    0
#define DCI_CMD_reserved2_BITS                                     2
#define DCI_CMD_reserved2_SHIFT                                    2

/* DCI :: CMD :: DOWNLOAD_COMPLETE [01:01] */
#define DCI_CMD_DOWNLOAD_COMPLETE_MASK                             0x00000002
#define DCI_CMD_DOWNLOAD_COMPLETE_ALIGN                            0
#define DCI_CMD_DOWNLOAD_COMPLETE_BITS                             1
#define DCI_CMD_DOWNLOAD_COMPLETE_SHIFT                            1

/* DCI :: CMD :: INITIATE_FW_DOWNLOAD [00:00] */
#define DCI_CMD_INITIATE_FW_DOWNLOAD_MASK                          0x00000001
#define DCI_CMD_INITIATE_FW_DOWNLOAD_ALIGN                         0
#define DCI_CMD_INITIATE_FW_DOWNLOAD_BITS                          1
#define DCI_CMD_INITIATE_FW_DOWNLOAD_SHIFT                         0


/****************************************************************************
 * DCI :: STATUS
 ***************************************************************************/
/* DCI :: STATUS :: reserved0 [31:10] */
#define DCI_STATUS_reserved0_MASK                                  0xfffffc00
#define DCI_STATUS_reserved0_ALIGN                                 0
#define DCI_STATUS_reserved0_BITS                                  22
#define DCI_STATUS_reserved0_SHIFT                                 10

/* DCI :: STATUS :: SIGNATURE_MATCHED [09:09] */
#define DCI_STATUS_SIGNATURE_MATCHED_MASK                          0x00000200
#define DCI_STATUS_SIGNATURE_MATCHED_ALIGN                         0
#define DCI_STATUS_SIGNATURE_MATCHED_BITS                          1
#define DCI_STATUS_SIGNATURE_MATCHED_SHIFT                         9

/* DCI :: STATUS :: SIGNATURE_MISMATCH [08:08] */
#define DCI_STATUS_SIGNATURE_MISMATCH_MASK                         0x00000100
#define DCI_STATUS_SIGNATURE_MISMATCH_ALIGN                        0
#define DCI_STATUS_SIGNATURE_MISMATCH_BITS                         1
#define DCI_STATUS_SIGNATURE_MISMATCH_SHIFT                        8

/* DCI :: STATUS :: reserved1 [07:06] */
#define DCI_STATUS_reserved1_MASK                                  0x000000c0
#define DCI_STATUS_reserved1_ALIGN                                 0
#define DCI_STATUS_reserved1_BITS                                  2
#define DCI_STATUS_reserved1_SHIFT                                 6

/* DCI :: STATUS :: GISB_ERROR [05:05] */
#define DCI_STATUS_GISB_ERROR_MASK                                 0x00000020
#define DCI_STATUS_GISB_ERROR_ALIGN                                0
#define DCI_STATUS_GISB_ERROR_BITS                                 1
#define DCI_STATUS_GISB_ERROR_SHIFT                                5

/* DCI :: STATUS :: DOWNLOAD_READY [04:04] */
#define DCI_STATUS_DOWNLOAD_READY_MASK                             0x00000010
#define DCI_STATUS_DOWNLOAD_READY_ALIGN                            0
#define DCI_STATUS_DOWNLOAD_READY_BITS                             1
#define DCI_STATUS_DOWNLOAD_READY_SHIFT                            4

/* DCI :: STATUS :: reserved2 [03:01] */
#define DCI_STATUS_reserved2_MASK                                  0x0000000e
#define DCI_STATUS_reserved2_ALIGN                                 0
#define DCI_STATUS_reserved2_BITS                                  3
#define DCI_STATUS_reserved2_SHIFT                                 1

/* DCI :: STATUS :: FIRMWARE_VALIDATED [00:00] */
#define DCI_STATUS_FIRMWARE_VALIDATED_MASK                         0x00000001
#define DCI_STATUS_FIRMWARE_VALIDATED_ALIGN                        0
#define DCI_STATUS_FIRMWARE_VALIDATED_BITS                         1
#define DCI_STATUS_FIRMWARE_VALIDATED_SHIFT                        0


/****************************************************************************
 * DCI :: DRAM_BASE_ADDR
 ***************************************************************************/
/* DCI :: DRAM_BASE_ADDR :: reserved0 [31:13] */
#define DCI_DRAM_BASE_ADDR_reserved0_MASK                          0xffffe000
#define DCI_DRAM_BASE_ADDR_reserved0_ALIGN                         0
#define DCI_DRAM_BASE_ADDR_reserved0_BITS                          19
#define DCI_DRAM_BASE_ADDR_reserved0_SHIFT                         13

/* DCI :: DRAM_BASE_ADDR :: BASE_ADDR [12:00] */
#define DCI_DRAM_BASE_ADDR_BASE_ADDR_MASK                          0x00001fff
#define DCI_DRAM_BASE_ADDR_BASE_ADDR_ALIGN                         0
#define DCI_DRAM_BASE_ADDR_BASE_ADDR_BITS                          13
#define DCI_DRAM_BASE_ADDR_BASE_ADDR_SHIFT                         0


/****************************************************************************
 * DCI :: FIRMWARE_ADDR
 ***************************************************************************/
/* DCI :: FIRMWARE_ADDR :: reserved0 [31:19] */
#define DCI_FIRMWARE_ADDR_reserved0_MASK                           0xfff80000
#define DCI_FIRMWARE_ADDR_reserved0_ALIGN                          0
#define DCI_FIRMWARE_ADDR_reserved0_BITS                           13
#define DCI_FIRMWARE_ADDR_reserved0_SHIFT                          19

/* DCI :: FIRMWARE_ADDR :: FW_ADDR [18:02] */
#define DCI_FIRMWARE_ADDR_FW_ADDR_MASK                             0x0007fffc
#define DCI_FIRMWARE_ADDR_FW_ADDR_ALIGN                            0
#define DCI_FIRMWARE_ADDR_FW_ADDR_BITS                             17
#define DCI_FIRMWARE_ADDR_FW_ADDR_SHIFT                            2

/* DCI :: FIRMWARE_ADDR :: reserved1 [01:00] */
#define DCI_FIRMWARE_ADDR_reserved1_MASK                           0x00000003
#define DCI_FIRMWARE_ADDR_reserved1_ALIGN                          0
#define DCI_FIRMWARE_ADDR_reserved1_BITS                           2
#define DCI_FIRMWARE_ADDR_reserved1_SHIFT                          0


/****************************************************************************
 * DCI :: FIRMWARE_DATA
 ***************************************************************************/
/* DCI :: FIRMWARE_DATA :: FW_DATA [31:00] */
#define DCI_FIRMWARE_DATA_FW_DATA_MASK                             0xffffffff
#define DCI_FIRMWARE_DATA_FW_DATA_ALIGN                            0
#define DCI_FIRMWARE_DATA_FW_DATA_BITS                             32
#define DCI_FIRMWARE_DATA_FW_DATA_SHIFT                            0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_0
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_0 :: SIG_DATA_0 [31:00] */
#define DCI_SIGNATURE_DATA_0_SIG_DATA_0_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_0_SIG_DATA_0_ALIGN                      0
#define DCI_SIGNATURE_DATA_0_SIG_DATA_0_BITS                       32
#define DCI_SIGNATURE_DATA_0_SIG_DATA_0_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_1
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_1 :: SIG_DATA_1 [31:00] */
#define DCI_SIGNATURE_DATA_1_SIG_DATA_1_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_1_SIG_DATA_1_ALIGN                      0
#define DCI_SIGNATURE_DATA_1_SIG_DATA_1_BITS                       32
#define DCI_SIGNATURE_DATA_1_SIG_DATA_1_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_2
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_2 :: SIG_DATA_2 [31:00] */
#define DCI_SIGNATURE_DATA_2_SIG_DATA_2_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_2_SIG_DATA_2_ALIGN                      0
#define DCI_SIGNATURE_DATA_2_SIG_DATA_2_BITS                       32
#define DCI_SIGNATURE_DATA_2_SIG_DATA_2_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_3
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_3 :: SIG_DATA_3 [31:00] */
#define DCI_SIGNATURE_DATA_3_SIG_DATA_3_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_3_SIG_DATA_3_ALIGN                      0
#define DCI_SIGNATURE_DATA_3_SIG_DATA_3_BITS                       32
#define DCI_SIGNATURE_DATA_3_SIG_DATA_3_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_4
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_4 :: SIG_DATA_4 [31:00] */
#define DCI_SIGNATURE_DATA_4_SIG_DATA_4_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_4_SIG_DATA_4_ALIGN                      0
#define DCI_SIGNATURE_DATA_4_SIG_DATA_4_BITS                       32
#define DCI_SIGNATURE_DATA_4_SIG_DATA_4_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_5
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_5 :: SIG_DATA_5 [31:00] */
#define DCI_SIGNATURE_DATA_5_SIG_DATA_5_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_5_SIG_DATA_5_ALIGN                      0
#define DCI_SIGNATURE_DATA_5_SIG_DATA_5_BITS                       32
#define DCI_SIGNATURE_DATA_5_SIG_DATA_5_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_6
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_6 :: SIG_DATA_6 [31:00] */
#define DCI_SIGNATURE_DATA_6_SIG_DATA_6_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_6_SIG_DATA_6_ALIGN                      0
#define DCI_SIGNATURE_DATA_6_SIG_DATA_6_BITS                       32
#define DCI_SIGNATURE_DATA_6_SIG_DATA_6_SHIFT                      0


/****************************************************************************
 * DCI :: SIGNATURE_DATA_7
 ***************************************************************************/
/* DCI :: SIGNATURE_DATA_7 :: SIG_DATA_7 [31:00] */
#define DCI_SIGNATURE_DATA_7_SIG_DATA_7_MASK                       0xffffffff
#define DCI_SIGNATURE_DATA_7_SIG_DATA_7_ALIGN                      0
#define DCI_SIGNATURE_DATA_7_SIG_DATA_7_BITS                       32
#define DCI_SIGNATURE_DATA_7_SIG_DATA_7_SHIFT                      0


/****************************************************************************
 * BCM70012_DCI_TOP_DCI_RGR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * DCI_RGR_BRIDGE :: REVISION
 ***************************************************************************/
/* DCI_RGR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define DCI_RGR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define DCI_RGR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define DCI_RGR_BRIDGE_REVISION_reserved0_BITS                     16
#define DCI_RGR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* DCI_RGR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define DCI_RGR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define DCI_RGR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define DCI_RGR_BRIDGE_REVISION_MAJOR_BITS                         8
#define DCI_RGR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* DCI_RGR_BRIDGE :: REVISION :: MINOR [07:00] */
#define DCI_RGR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define DCI_RGR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define DCI_RGR_BRIDGE_REVISION_MINOR_BITS                         8
#define DCI_RGR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * DCI_RGR_BRIDGE :: CTRL
 ***************************************************************************/
/* DCI_RGR_BRIDGE :: CTRL :: reserved0 [31:02] */
#define DCI_RGR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffc
#define DCI_RGR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define DCI_RGR_BRIDGE_CTRL_reserved0_BITS                         30
#define DCI_RGR_BRIDGE_CTRL_reserved0_SHIFT                        2

/* DCI_RGR_BRIDGE :: CTRL :: rbus_error_intr [01:01] */
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_MASK                   0x00000002
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_ALIGN                  0
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_BITS                   1
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_SHIFT                  1
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_DISABLE           0
#define DCI_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_ENABLE            1

/* DCI_RGR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_MASK                   0x00000001
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_ALIGN                  0
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_BITS                   1
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_SHIFT                  0
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE           0
#define DCI_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE            1


/****************************************************************************
 * DCI_RGR_BRIDGE :: RBUS_TIMER
 ***************************************************************************/
/* DCI_RGR_BRIDGE :: RBUS_TIMER :: reserved0 [31:16] */
#define DCI_RGR_BRIDGE_RBUS_TIMER_reserved0_MASK                   0xffff0000
#define DCI_RGR_BRIDGE_RBUS_TIMER_reserved0_ALIGN                  0
#define DCI_RGR_BRIDGE_RBUS_TIMER_reserved0_BITS                   16
#define DCI_RGR_BRIDGE_RBUS_TIMER_reserved0_SHIFT                  16

/* DCI_RGR_BRIDGE :: RBUS_TIMER :: timer_value [15:00] */
#define DCI_RGR_BRIDGE_RBUS_TIMER_timer_value_MASK                 0x0000ffff
#define DCI_RGR_BRIDGE_RBUS_TIMER_timer_value_ALIGN                0
#define DCI_RGR_BRIDGE_RBUS_TIMER_timer_value_BITS                 16
#define DCI_RGR_BRIDGE_RBUS_TIMER_timer_value_SHIFT                0


/****************************************************************************
 * DCI_RGR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* DCI_RGR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* DCI_RGR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT    0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * DCI_RGR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* DCI_RGR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* DCI_RGR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT    0
#define DCI_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * BCM70012_CCE_TOP_CCE_RGR_BRIDGE
 ***************************************************************************/
/****************************************************************************
 * CCE_RGR_BRIDGE :: REVISION
 ***************************************************************************/
/* CCE_RGR_BRIDGE :: REVISION :: reserved0 [31:16] */
#define CCE_RGR_BRIDGE_REVISION_reserved0_MASK                     0xffff0000
#define CCE_RGR_BRIDGE_REVISION_reserved0_ALIGN                    0
#define CCE_RGR_BRIDGE_REVISION_reserved0_BITS                     16
#define CCE_RGR_BRIDGE_REVISION_reserved0_SHIFT                    16

/* CCE_RGR_BRIDGE :: REVISION :: MAJOR [15:08] */
#define CCE_RGR_BRIDGE_REVISION_MAJOR_MASK                         0x0000ff00
#define CCE_RGR_BRIDGE_REVISION_MAJOR_ALIGN                        0
#define CCE_RGR_BRIDGE_REVISION_MAJOR_BITS                         8
#define CCE_RGR_BRIDGE_REVISION_MAJOR_SHIFT                        8

/* CCE_RGR_BRIDGE :: REVISION :: MINOR [07:00] */
#define CCE_RGR_BRIDGE_REVISION_MINOR_MASK                         0x000000ff
#define CCE_RGR_BRIDGE_REVISION_MINOR_ALIGN                        0
#define CCE_RGR_BRIDGE_REVISION_MINOR_BITS                         8
#define CCE_RGR_BRIDGE_REVISION_MINOR_SHIFT                        0


/****************************************************************************
 * CCE_RGR_BRIDGE :: CTRL
 ***************************************************************************/
/* CCE_RGR_BRIDGE :: CTRL :: reserved0 [31:02] */
#define CCE_RGR_BRIDGE_CTRL_reserved0_MASK                         0xfffffffc
#define CCE_RGR_BRIDGE_CTRL_reserved0_ALIGN                        0
#define CCE_RGR_BRIDGE_CTRL_reserved0_BITS                         30
#define CCE_RGR_BRIDGE_CTRL_reserved0_SHIFT                        2

/* CCE_RGR_BRIDGE :: CTRL :: rbus_error_intr [01:01] */
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_MASK                   0x00000002
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_ALIGN                  0
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_BITS                   1
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_SHIFT                  1
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_DISABLE           0
#define CCE_RGR_BRIDGE_CTRL_rbus_error_intr_INTR_ENABLE            1

/* CCE_RGR_BRIDGE :: CTRL :: gisb_error_intr [00:00] */
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_MASK                   0x00000001
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_ALIGN                  0
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_BITS                   1
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_SHIFT                  0
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_DISABLE           0
#define CCE_RGR_BRIDGE_CTRL_gisb_error_intr_INTR_ENABLE            1


/****************************************************************************
 * CCE_RGR_BRIDGE :: RBUS_TIMER
 ***************************************************************************/
/* CCE_RGR_BRIDGE :: RBUS_TIMER :: reserved0 [31:16] */
#define CCE_RGR_BRIDGE_RBUS_TIMER_reserved0_MASK                   0xffff0000
#define CCE_RGR_BRIDGE_RBUS_TIMER_reserved0_ALIGN                  0
#define CCE_RGR_BRIDGE_RBUS_TIMER_reserved0_BITS                   16
#define CCE_RGR_BRIDGE_RBUS_TIMER_reserved0_SHIFT                  16

/* CCE_RGR_BRIDGE :: RBUS_TIMER :: timer_value [15:00] */
#define CCE_RGR_BRIDGE_RBUS_TIMER_timer_value_MASK                 0x0000ffff
#define CCE_RGR_BRIDGE_RBUS_TIMER_timer_value_ALIGN                0
#define CCE_RGR_BRIDGE_RBUS_TIMER_timer_value_BITS                 16
#define CCE_RGR_BRIDGE_RBUS_TIMER_timer_value_SHIFT                0


/****************************************************************************
 * CCE_RGR_BRIDGE :: SPARE_SW_RESET_0
 ***************************************************************************/
/* CCE_RGR_BRIDGE :: SPARE_SW_RESET_0 :: reserved0 [31:01] */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_MASK             0xfffffffe
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_ALIGN            0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_BITS             31
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_reserved0_SHIFT            1

/* CCE_RGR_BRIDGE :: SPARE_SW_RESET_0 :: SPARE_SW_RESET [00:00] */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_MASK        0x00000001
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ALIGN       0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_BITS        1
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_SHIFT       0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_DEASSERT    0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_0_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * CCE_RGR_BRIDGE :: SPARE_SW_RESET_1
 ***************************************************************************/
/* CCE_RGR_BRIDGE :: SPARE_SW_RESET_1 :: reserved0 [31:01] */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_MASK             0xfffffffe
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_ALIGN            0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_BITS             31
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_reserved0_SHIFT            1

/* CCE_RGR_BRIDGE :: SPARE_SW_RESET_1 :: SPARE_SW_RESET [00:00] */
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_MASK        0x00000001
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ALIGN       0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_BITS        1
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_SHIFT       0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_DEASSERT    0
#define CCE_RGR_BRIDGE_SPARE_SW_RESET_1_SPARE_SW_RESET_ASSERT      1


/****************************************************************************
 * Datatype Definitions.
 ***************************************************************************/
#endif /* #ifndef MACFILE_H__ */

/* End of File */

